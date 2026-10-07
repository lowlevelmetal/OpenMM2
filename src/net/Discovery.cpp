#include "net/Discovery.h"

#include "core/Log.h"
#include "net/BitStream.h"

#include <enet/enet.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <random>

#ifdef _WIN32
// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
// clang-format on
#else
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#endif

namespace mm2::net {
namespace {

constexpr char kQueryMagic[4] = {'M', '2', 'L', 'Q'};
constexpr char kAdvertMagic[4] = {'M', '2', 'L', 'A'};

void setError(std::string* error, std::string msg) {
    if (error)
        *error = std::move(msg);
}

ENetSocket toEnet(std::intptr_t s) { return static_cast<ENetSocket>(s); }

ENetAddress toEnet(const Address& a) {
    ENetAddress e{};
    e.host = a.ip == 0 ? ENET_HOST_ANY : ENET_HOST_TO_NET_32(a.ip);
    e.port = a.port;
    return e;
}

void writeMagic(WriteStream& s, const char (&magic)[4]) {
    for (char c : magic) {
        auto v = static_cast<std::uint8_t>(c);
        s.u8(v);
    }
}

bool readMagic(ReadStream& s, const char (&magic)[4]) {
    for (char c : magic) {
        std::uint8_t v = 0;
        s.u8(v);
        if (v != static_cast<std::uint8_t>(c))
            return false;
    }
    return s.ok();
}

template <class S>
bool serializeAdvert(S& s, LanAdvert& a) {
    s.string(a.sessionName, kMaxNameLength * 2);
    s.string(a.hostName, kMaxNameLength);
    s.string(a.city, kMaxShortStringLength);
    s.enumeration(a.mode, GameMode::Last);
    s.u16(a.raceId);
    s.u8(a.players);
    s.u8(a.maxPlayers);
    s.boolean(a.hasPassword);
    s.enumeration(a.phase, SessionPhase::Last);
    s.u16(a.gamePort);
    s.string(a.build, kMaxShortStringLength);
    return s.ok();
}

std::uint64_t sessionKey(const Address& a) { return (std::uint64_t{a.ip} << 16) | a.port; }

} // namespace

// --- Wire format --------------------------------------------------------------

std::vector<std::byte> encodeLanQuery(std::uint32_t nonce) {
    WriteStream s;
    writeMagic(s, kQueryMagic);
    std::uint16_t version = kProtocolVersion;
    s.u16(version);
    s.u32(nonce);
    return s.writer().take();
}

std::vector<std::byte> encodeLanAdvert(std::uint32_t nonce, const LanAdvert& advert) {
    WriteStream s;
    writeMagic(s, kAdvertMagic);
    // The version comes first and outside the advert body so that future
    // protocol versions can still be listed as "incompatible".
    std::uint16_t version = advert.protocolVersion;
    s.u16(version);
    s.u32(nonce);
    LanAdvert copy = advert;
    serializeAdvert(s, copy);
    return s.writer().take();
}

bool decodeLanQuery(std::span<const std::byte> packet, std::uint32_t& nonce) {
    ReadStream s(packet);
    if (!readMagic(s, kQueryMagic))
        return false;
    std::uint16_t version = 0;
    s.u16(version);
    s.u32(nonce);
    return s.ok();
}

bool decodeLanAdvert(std::span<const std::byte> packet, std::uint32_t& nonce, LanAdvert& advert) {
    ReadStream s(packet);
    if (!readMagic(s, kAdvertMagic))
        return false;
    LanAdvert a;
    s.u16(a.protocolVersion);
    s.u32(nonce);
    if (!s.ok())
        return false;
    if (a.protocolVersion == kProtocolVersion) {
        if (!serializeAdvert(s, a))
            return false;
    } else {
        // Unknown layout: report only the version so the UI can say "newer version".
        a.sessionName = "(incompatible version)";
    }
    advert = std::move(a);
    return true;
}

std::vector<std::uint32_t> broadcastAddresses() {
    std::vector<std::uint32_t> out;
#ifdef _WIN32
    ULONG size = 16 * 1024;
    std::vector<std::byte> buffer(size);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    ULONG rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                    nullptr, adapters, &size);
    if (rc == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(size);
        adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                  nullptr, adapters, &size);
    }
    if (rc == NO_ERROR) {
        for (auto* a = adapters; a; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
                continue;
            for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
                if (u->Address.lpSockaddr->sa_family != AF_INET)
                    continue;
                const auto* sin = reinterpret_cast<const sockaddr_in*>(u->Address.lpSockaddr);
                const std::uint32_t ip = ntohl(sin->sin_addr.s_addr);
                const unsigned prefix = u->OnLinkPrefixLength;
                if (prefix == 0 || prefix >= 32)
                    continue;
                const std::uint32_t mask = 0xFFFFFFFFu << (32 - prefix);
                out.push_back(ip | ~mask);
            }
        }
    }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) == 0) {
        for (ifaddrs* i = list; i; i = i->ifa_next) {
            if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET)
                continue;
            if (!(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK) || !(i->ifa_flags & IFF_BROADCAST))
                continue;
            if (!i->ifa_broadaddr)
                continue;
            const auto* sin = reinterpret_cast<const sockaddr_in*>(i->ifa_broadaddr);
            out.push_back(ntohl(sin->sin_addr.s_addr));
        }
        freeifaddrs(list);
    }
#endif
    out.push_back(0xFFFFFFFFu);
    std::ranges::sort(out);
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::uint32_t primaryLocalAddress() {
    auto sock = UdpSocket::open(Address::any(0), false, false);
    // Connecting a UDP socket only selects a route; nothing is transmitted.
    if (!sock || !sock->connect({0x08080808u, 53}))
        return 0;
    return sock->localAddress().ip;
}

// --- UdpSocket ------------------------------------------------------------------

UdpSocket::~UdpSocket() {
    if (m_socket != -1)
        enet_socket_destroy(toEnet(m_socket));
}

std::unique_ptr<UdpSocket> UdpSocket::open(const Address& bind, bool broadcast, bool reuse, std::string* error) {
    std::unique_ptr<UdpSocket> s(new UdpSocket());
    if (!s->m_lib.ok()) {
        setError(error, "network initialization failed");
        return nullptr;
    }
    const ENetSocket sock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (sock == ENET_SOCKET_NULL) {
        setError(error, "cannot create UDP socket");
        return nullptr;
    }
    s->m_socket = static_cast<std::intptr_t>(sock);
    enet_socket_set_option(sock, ENET_SOCKOPT_NONBLOCK, 1);
    if (broadcast)
        enet_socket_set_option(sock, ENET_SOCKOPT_BROADCAST, 1);
    if (reuse)
        enet_socket_set_option(sock, ENET_SOCKOPT_REUSEADDR, 1);
    const ENetAddress addr = toEnet(bind);
    if (enet_socket_bind(sock, &addr) < 0) {
        setError(error, std::format("cannot bind UDP {}", bind.toString()));
        return nullptr;
    }
    return s;
}

bool UdpSocket::sendTo(const Address& to, std::span<const std::byte> data) {
    const ENetAddress addr = toEnet(to);
    ENetBuffer buf;
    buf.data = const_cast<std::byte*>(data.data());
    buf.dataLength = data.size();
    return enet_socket_send(toEnet(m_socket), &addr, &buf, 1) == static_cast<int>(data.size());
}

int UdpSocket::receiveFrom(Address& from, std::span<std::byte> buffer) {
    ENetAddress addr{};
    ENetBuffer buf;
    buf.data = buffer.data();
    buf.dataLength = buffer.size();
    const int n = enet_socket_receive(toEnet(m_socket), &addr, &buf, 1);
    if (n > 0)
        from = Address{ENET_NET_TO_HOST_32(addr.host), addr.port};
    return n;
}

bool UdpSocket::connect(const Address& to) {
    const ENetAddress addr = toEnet(to);
    return enet_socket_connect(toEnet(m_socket), &addr) == 0;
}

bool UdpSocket::wait(std::uint32_t timeoutMs) {
    enet_uint32 condition = ENET_SOCKET_WAIT_RECEIVE;
    if (enet_socket_wait(toEnet(m_socket), &condition, timeoutMs) < 0)
        return false;
    return (condition & ENET_SOCKET_WAIT_RECEIVE) != 0;
}

Address UdpSocket::localAddress() const {
    ENetAddress addr{};
    if (enet_socket_get_address(toEnet(m_socket), &addr) < 0)
        return {};
    return {ENET_NET_TO_HOST_32(addr.host), addr.port};
}

// --- LanBeacon --------------------------------------------------------------------

LanBeacon::LanBeacon() = default;
LanBeacon::~LanBeacon() = default;

bool LanBeacon::start(std::uint16_t discoveryPort, std::string* error) {
    m_socket = UdpSocket::open(Address::any(discoveryPort), true, true, error);
    if (!m_socket)
        return false;
    m_port = port();
    m_nextAnnounce = 0;
    if (m_targets.empty())
        for (std::uint32_t ip : broadcastAddresses())
            m_targets.push_back({ip, m_port});
    return true;
}

void LanBeacon::stop() { m_socket.reset(); }

bool LanBeacon::running() const { return m_socket != nullptr; }

std::uint16_t LanBeacon::port() const { return m_socket ? m_socket->localAddress().port : 0; }

void LanBeacon::update() {
    if (!m_socket)
        return;
    std::array<std::byte, 1500> buf{};
    for (int i = 0; i < 64; ++i) {
        Address from;
        const int n = m_socket->receiveFrom(from, buf);
        if (n <= 0)
            break;
        std::uint32_t nonce = 0;
        if (decodeLanQuery(std::span(buf.data(), static_cast<std::size_t>(n)), nonce))
            m_socket->sendTo(from, encodeLanAdvert(nonce, m_advert));
    }
    const std::uint64_t now = monotonicMs();
    if (m_announceIntervalMs && now >= m_nextAnnounce) {
        m_nextAnnounce = now + m_announceIntervalMs;
        const auto packet = encodeLanAdvert(0, m_advert);
        for (const auto& t : m_targets)
            m_socket->sendTo(t, packet);
    }
}

// --- LanScanner ---------------------------------------------------------------------

LanScanner::LanScanner() = default;
LanScanner::~LanScanner() = default;

bool LanScanner::start(std::uint16_t discoveryPort, bool listenForAnnouncements, std::string* error) {
    m_query = UdpSocket::open(Address::any(0), true, false, error);
    if (!m_query)
        return false;
    m_port = discoveryPort;
    if (listenForAnnouncements) {
        std::string listenError;
        m_listen = UdpSocket::open(Address::any(discoveryPort), true, true, &listenError);
        if (!m_listen)
            log::debug("net: LAN scanner not listening for announcements: {}", listenError);
    }
    std::random_device rd;
    m_nonceCounter = rd() | 1u;
    return true;
}

void LanScanner::stop() {
    m_query.reset();
    m_listen.reset();
}

void LanScanner::scan() {
    if (!m_query)
        return;
    const std::uint32_t nonce = ++m_nonceCounter == 0 ? ++m_nonceCounter : m_nonceCounter;
    const auto packet = encodeLanQuery(nonce);
    m_pendingQueries[nonce] = monotonicMs();
    if (m_broadcast)
        for (std::uint32_t ip : broadcastAddresses())
            m_query->sendTo({ip, m_port}, packet);
    for (Address t : m_extraTargets) {
        if (t.port == 0)
            t.port = m_port;
        m_query->sendTo(t, packet);
    }
    // Forget queries nobody answered.
    const std::uint64_t now = monotonicMs();
    std::erase_if(m_pendingQueries, [&](const auto& q) { return now - q.second > 10000; });
}

void LanScanner::receive(UdpSocket& socket) {
    std::array<std::byte, 1500> buf{};
    for (int i = 0; i < 256; ++i) {
        Address from;
        const int n = socket.receiveFrom(from, buf);
        if (n <= 0)
            break;
        std::uint32_t nonce = 0;
        LanAdvert advert;
        if (!decodeLanAdvert(std::span(buf.data(), static_cast<std::size_t>(n)), nonce, advert))
            continue;
        const std::uint64_t now = monotonicMs();
        const Address game{from.ip, advert.gamePort};
        auto& entry = m_sessions[sessionKey(game)];
        entry.address = game;
        entry.advert = std::move(advert);
        entry.lastSeenMs = now;
        if (nonce) {
            if (const auto it = m_pendingQueries.find(nonce); it != m_pendingQueries.end())
                entry.pingMs = static_cast<std::uint32_t>(std::max<std::uint64_t>(1, now - it->second));
        }
    }
}

void LanScanner::update() {
    if (m_query)
        receive(*m_query);
    if (m_listen)
        receive(*m_listen);
    const std::uint64_t now = monotonicMs();
    std::erase_if(m_sessions, [&](const auto& s) { return now - s.second.lastSeenMs > m_expiryMs; });
}

std::vector<DiscoveredSession> LanScanner::sessions() const {
    std::vector<DiscoveredSession> out;
    for (const auto& [key, s] : m_sessions)
        out.push_back(s);
    std::ranges::sort(out, [](const DiscoveredSession& a, const DiscoveredSession& b) {
        return a.advert.sessionName < b.advert.sessionName;
    });
    return out;
}

} // namespace mm2::net
