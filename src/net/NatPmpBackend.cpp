// PCP / NAT-PMP port mapping backend and default-gateway lookup.
#include "core/Log.h"
#include "net/Discovery.h"
#include "net/NatPmp.h"
#include "net/PortMapper.h"

#include <enet/enet.h>

#include <algorithm>
#include <cstdio>
#include <format>
#include <fstream>
#include <random>

#ifdef _WIN32
// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
// clang-format on
#else
#include <arpa/inet.h>
#endif

namespace mm2::net {
namespace {

// Default IPv4 gateway in host byte order.
std::optional<std::uint32_t> defaultGateway() {
#ifdef _WIN32
    MIB_IPFORWARDROW row{};
    // Any public destination works; nothing is sent.
    if (GetBestRoute(htonl(0x08080808u), 0, &row) == NO_ERROR && row.dwForwardNextHop != 0)
        return ntohl(row.dwForwardNextHop);
    return std::nullopt;
#elif defined(__linux__)
    std::ifstream in("/proc/net/route");
    std::string line;
    std::getline(in, line); // header
    std::optional<std::uint32_t> best;
    unsigned long bestMetric = ~0ul;
    while (std::getline(in, line)) {
        char iface[64] = {};
        unsigned long dest = 0, gateway = 0, flags = 0, refcnt = 0, use = 0, metric = 0, mask = 0;
        if (std::sscanf(line.c_str(), "%63s %lx %lx %lx %lu %lu %lu %lx", iface, &dest, &gateway, &flags, &refcnt,
                        &use, &metric, &mask) != 8)
            continue;
        constexpr unsigned long kRtfUp = 0x1, kRtfGateway = 0x2;
        if (dest != 0 || mask != 0 || !(flags & kRtfUp) || !(flags & kRtfGateway) || gateway == 0)
            continue;
        if (metric < bestMetric) {
            bestMetric = metric;
            // The kernel prints the raw (network order) address as a native integer.
            best = ntohl(static_cast<std::uint32_t>(gateway));
        }
    }
    return best;
#else
    return std::nullopt; // TODO: sysctl(NET_RT_FLAGS) on BSD/macOS
#endif
}

// LAN address of the interface used to reach `remote` (host byte order).
std::uint32_t localAddressFor(std::uint32_t remote) {
    auto sock = UdpSocket::open(Address::any(0), false, false);
    if (!sock || !sock->connect({remote, natpmp::kServerPort}))
        return 0;
    return sock->localAddress().ip;
}

std::string toHex(std::span<const std::byte> bytes) {
    std::string out;
    for (std::byte b : bytes)
        out += std::format("{:02x}", std::to_integer<unsigned>(b));
    return out;
}

bool fromHex(std::string_view hex, std::span<std::byte> out) {
    if (hex.size() != out.size() * 2)
        return false;
    auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < out.size(); ++i) {
        const int hi = digit(hex[i * 2]), lo = digit(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return false;
        out[i] = static_cast<std::byte>(hi * 16 + lo);
    }
    return true;
}

class NatPmpBackend final : public PortMappingBackend {
public:
    MappingMethod method() const override { return m_pcp ? MappingMethod::Pcp : MappingMethod::NatPmp; }

    bool discover(std::uint32_t timeoutMs, GatewayInfo& out, std::string& error) override {
        m_timeoutMs = std::max<std::uint32_t>(timeoutMs, 500);
        const auto gw = defaultGateway();
        if (!gw) {
            error = "cannot determine the default gateway";
            return false;
        }
        m_gateway = *gw;
        m_local = localAddressFor(m_gateway);
        m_socket = UdpSocket::open(Address::any(0), false, false, &error);
        if (!m_socket)
            return false;
        const std::string gwText = Address{m_gateway, 0}.ipString();

        std::random_device rd;
        for (std::size_t i = 0; i < m_nonce.size(); ++i)
            m_nonce[i] = static_cast<std::byte>(rd());

        // Probe PCP first (ANNOUNCE); a NAT-PMP-only gateway answers with a
        // version-0 "unsupported version" reply.
        bool natpmpOnly = false;
        if (auto reply = request(natpmp::encodePcpAnnounce(m_local))) {
            if (const auto r = natpmp::decodePcpResponse(*reply)) {
                if (r->version == natpmp::kPcpVersion && r->result == natpmp::kPcpSuccess) {
                    m_pcp = true;
                    // PCP only reports the external address with a mapping;
                    // many PCP routers also speak NAT-PMP, so ask that way.
                    const std::uint32_t budget = m_timeoutMs;
                    m_timeoutMs = std::min<std::uint32_t>(m_timeoutMs, 750);
                    if (auto ext = request(natpmp::encodeNatPmpExternalAddress()))
                        if (const auto e = natpmp::decodeNatPmpResponse(*ext); e && e->opcode == 0 && e->result == 0)
                            m_externalIp = e->externalIp;
                    m_timeoutMs = budget;
                    fill(out, gwText);
                    return true;
                }
                natpmpOnly = r->version == natpmp::kNatPmpVersion || r->result == natpmp::kPcpUnsuppVersion;
            }
        }
        m_pcp = false;
        if (auto reply = request(natpmp::encodeNatPmpExternalAddress())) {
            if (const auto r = natpmp::decodeNatPmpResponse(*reply); r && r->opcode == 0) {
                if (r->result != 0) {
                    error = std::format("gateway {} refused NAT-PMP: {}", gwText, natpmp::describeNatPmpResult(r->result));
                    return false;
                }
                m_externalIp = r->externalIp;
                fill(out, gwText);
                return true;
            }
        }
        error = natpmpOnly ? std::format("gateway {} answered but NAT-PMP did not respond", gwText)
                           : std::format("gateway {} did not answer PCP or NAT-PMP (not supported or disabled)", gwText);
        return false;
    }

    MappingResult map(const MappingRequest& req) override {
        MappingResult res;
        if (m_pcp) {
            const auto reply = request(natpmp::encodePcpMap(m_nonce, m_local, req.internalPort, req.externalPort, 0,
                                                            req.leaseSeconds));
            const auto r = reply ? natpmp::decodePcpResponse(*reply) : std::nullopt;
            if (!r || r->opcode != natpmp::kPcpMap || r->nonce != m_nonce) {
                res.error = "no PCP response";
                return res;
            }
            if (r->result != natpmp::kPcpSuccess) {
                res.outcome = (r->result == natpmp::kPcpCannotProvideExternal || r->result == natpmp::kPcpNoResources)
                                  ? MappingResult::Outcome::Conflict
                                  : MappingResult::Outcome::Failed;
                res.error = natpmp::describePcpResult(r->result);
                return res;
            }
            res.outcome = MappingResult::Outcome::Ok;
            res.externalPort = r->externalPort;
            res.leaseSeconds = r->lifetime;
            m_externalIp = r->externalIp;
            res.externalIp = Address{r->externalIp, 0}.ipString();
            return res;
        }
        const auto reply = request(natpmp::encodeNatPmpMapUdp(req.internalPort, req.externalPort, req.leaseSeconds));
        const auto r = reply ? natpmp::decodeNatPmpResponse(*reply) : std::nullopt;
        if (!r || r->opcode != 1) {
            res.error = "no NAT-PMP response";
            return res;
        }
        if (r->result != 0) {
            res.outcome = r->result == 4 ? MappingResult::Outcome::Conflict : MappingResult::Outcome::Failed;
            res.error = natpmp::describeNatPmpResult(r->result);
            return res;
        }
        res.outcome = MappingResult::Outcome::Ok;
        res.externalPort = r->externalPort;
        res.leaseSeconds = r->lifetime;
        if (m_externalIp)
            res.externalIp = Address{m_externalIp, 0}.ipString();
        return res;
    }

    bool unmap(const MappingRecord& record, std::string& error) override {
        // RFC 6886 3.4 / RFC 6887 11.1: a deletion for internal port 0 removes
        // every mapping of this machine, including other programs'.
        if (record.internalPort == 0) {
            error = "no internal port";
            return false;
        }
        if (record.method == MappingMethod::Pcp) {
            natpmp::Nonce nonce{};
            if (!fromHex(record.token, nonce)) {
                error = "missing PCP mapping nonce";
                return false;
            }
            const auto local = Address::parse(record.internalIp);
            const auto reply = request(natpmp::encodePcpMap(nonce, local ? local->ip : m_local, record.internalPort,
                                                            record.externalPort, 0, 0));
            const auto r = reply ? natpmp::decodePcpResponse(*reply) : std::nullopt;
            if (!r) {
                error = "no PCP response";
                return false;
            }
            if (r->result != natpmp::kPcpSuccess) {
                error = natpmp::describePcpResult(r->result);
                return false;
            }
            return true;
        }
        // NAT-PMP deletes by internal port for the requesting host.
        const auto reply = request(natpmp::encodeNatPmpMapUdp(record.internalPort, 0, 0));
        const auto r = reply ? natpmp::decodeNatPmpResponse(*reply) : std::nullopt;
        if (!r) {
            error = "no NAT-PMP response";
            return false;
        }
        if (r->result != 0) {
            error = natpmp::describeNatPmpResult(r->result);
            return false;
        }
        return true;
    }

    std::string token() const override { return m_pcp ? toHex(m_nonce) : std::string(); }

private:
    void fill(GatewayInfo& out, const std::string& gwText) const {
        out.id = gwText;
        out.description = std::format("{} gateway {}", m_pcp ? "PCP" : "NAT-PMP", gwText);
        out.internalIp = Address{m_local, 0}.ipString();
        out.externalIp = m_externalIp ? Address{m_externalIp, 0}.ipString() : std::string();
        out.doubleNat = m_externalIp && Address{m_externalIp, 0}.isPrivate();
    }

    // Sends `packet` to the gateway with RFC 6886 style retransmission
    // (250 ms, doubling) until a reply arrives or the time budget is spent.
    std::optional<std::vector<std::byte>> request(const std::vector<std::byte>& packet) {
        if (!m_socket)
            return std::nullopt;
        const Address gateway{m_gateway, natpmp::kServerPort};
        const std::uint64_t deadline = monotonicMs() + m_timeoutMs;
        std::uint32_t wait = 250;
        std::array<std::byte, 1100> buf{};
        while (monotonicMs() < deadline) {
            m_socket->sendTo(gateway, packet);
            const std::uint64_t until = std::min(deadline, monotonicMs() + wait);
            while (monotonicMs() < until) {
                if (!m_socket->wait(static_cast<std::uint32_t>(until - monotonicMs())))
                    continue;
                Address from;
                const int n = m_socket->receiveFrom(from, buf);
                if (n > 0 && from == gateway)
                    return std::vector<std::byte>(buf.begin(), buf.begin() + n);
            }
            wait *= 2;
        }
        return std::nullopt;
    }

    NetLibrary m_lib;
    std::unique_ptr<UdpSocket> m_socket;
    std::uint32_t m_gateway = 0;
    std::uint32_t m_local = 0;
    std::uint32_t m_externalIp = 0;
    std::uint32_t m_timeoutMs = 2500;
    bool m_pcp = false;
    natpmp::Nonce m_nonce{};
};

} // namespace

std::unique_ptr<PortMappingBackend> makeNatPmpBackend() { return std::make_unique<NatPmpBackend>(); }

} // namespace mm2::net
