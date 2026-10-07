#include "net/NatPmp.h"

#include <algorithm>

namespace mm2::net::natpmp {
namespace {

void put8(std::vector<std::byte>& out, std::uint8_t v) { out.push_back(static_cast<std::byte>(v)); }
void put16(std::vector<std::byte>& out, std::uint16_t v) {
    put8(out, static_cast<std::uint8_t>(v >> 8));
    put8(out, static_cast<std::uint8_t>(v));
}
void put32(std::vector<std::byte>& out, std::uint32_t v) {
    put16(out, static_cast<std::uint16_t>(v >> 16));
    put16(out, static_cast<std::uint16_t>(v));
}
// IPv4-mapped IPv6 address ::ffff:a.b.c.d
void putMapped(std::vector<std::byte>& out, std::uint32_t ip) {
    for (int i = 0; i < 10; ++i)
        put8(out, 0);
    put16(out, 0xFFFF);
    put32(out, ip);
}

std::uint8_t get8(std::span<const std::byte> p, std::size_t at) { return std::to_integer<std::uint8_t>(p[at]); }
std::uint16_t get16(std::span<const std::byte> p, std::size_t at) {
    return static_cast<std::uint16_t>((get8(p, at) << 8) | get8(p, at + 1));
}
std::uint32_t get32(std::span<const std::byte> p, std::size_t at) {
    return (std::uint32_t{get16(p, at)} << 16) | get16(p, at + 2);
}

void putHeader(std::vector<std::byte>& out, std::uint8_t opcode, std::uint32_t lifetime, std::uint32_t clientIp) {
    put8(out, kPcpVersion);
    put8(out, opcode & 0x7F); // R bit clear: request
    put16(out, 0);            // reserved
    put32(out, lifetime);
    putMapped(out, clientIp);
}

} // namespace

std::vector<std::byte> encodePcpAnnounce(std::uint32_t clientIp) {
    std::vector<std::byte> out;
    putHeader(out, kPcpAnnounce, 0, clientIp);
    return out;
}

std::vector<std::byte> encodePcpMap(const Nonce& nonce, std::uint32_t clientIp, std::uint16_t internalPort,
                                    std::uint16_t suggestedExternalPort, std::uint32_t suggestedExternalIp,
                                    std::uint32_t lifetime) {
    std::vector<std::byte> out;
    out.reserve(60);
    putHeader(out, kPcpMap, lifetime, clientIp);
    out.insert(out.end(), nonce.begin(), nonce.end());
    put8(out, kProtocolUdp);
    put8(out, 0);
    put16(out, 0); // reserved (24 bits total)
    put16(out, internalPort);
    put16(out, suggestedExternalPort);
    putMapped(out, suggestedExternalIp);
    return out;
}

std::optional<PcpResponse> decodePcpResponse(std::span<const std::byte> p) {
    if (p.size() < 4)
        return std::nullopt;
    PcpResponse r;
    r.version = get8(p, 0);
    const std::uint8_t op = get8(p, 1);
    if (!(op & 0x80))
        return std::nullopt; // not a response
    r.opcode = op & 0x7F;
    if (r.version == kNatPmpVersion) {
        // NAT-PMP reply: 16-bit result code at offset 2.
        if (p.size() < 8)
            return std::nullopt;
        const std::uint16_t result = get16(p, 2);
        r.result = result == 1 ? std::uint8_t{kPcpUnsuppVersion}
                                : static_cast<std::uint8_t>(std::min<std::uint16_t>(result, 255));
        r.epoch = get32(p, 4);
        return r;
    }
    if (r.version != kPcpVersion || p.size() < 24)
        return std::nullopt;
    r.result = get8(p, 3);
    r.lifetime = get32(p, 4);
    r.epoch = get32(p, 8);
    if (r.opcode == kPcpMap && p.size() >= 60) {
        for (std::size_t i = 0; i < 12; ++i)
            r.nonce[i] = p[24 + i];
        r.protocol = get8(p, 36);
        r.internalPort = get16(p, 40);
        r.externalPort = get16(p, 42);
        r.externalIp = get32(p, 56); // low 32 bits of the mapped address
    } else if (r.opcode == kPcpMap && r.result == kPcpSuccess) {
        return std::nullopt; // truncated success
    }
    return r;
}

std::vector<std::byte> encodeNatPmpExternalAddress() { return {std::byte{0}, std::byte{0}}; }

std::vector<std::byte> encodeNatPmpMapUdp(std::uint16_t internalPort, std::uint16_t suggestedExternalPort,
                                          std::uint32_t lifetime) {
    std::vector<std::byte> out;
    put8(out, kNatPmpVersion);
    put8(out, 1); // map UDP
    put16(out, 0);
    put16(out, internalPort);
    put16(out, suggestedExternalPort);
    put32(out, lifetime);
    return out;
}

std::optional<NatPmpResponse> decodeNatPmpResponse(std::span<const std::byte> p) {
    if (p.size() < 8 || get8(p, 0) != kNatPmpVersion || !(get8(p, 1) & 0x80))
        return std::nullopt;
    NatPmpResponse r;
    r.opcode = get8(p, 1) & 0x7F;
    r.result = get16(p, 2);
    r.epoch = get32(p, 4);
    if (r.result != 0)
        return r;
    if (r.opcode == 0) {
        if (p.size() < 12)
            return std::nullopt;
        r.externalIp = get32(p, 8);
    } else if (r.opcode == 1 || r.opcode == 2) {
        if (p.size() < 16)
            return std::nullopt;
        r.internalPort = get16(p, 8);
        r.externalPort = get16(p, 10);
        r.lifetime = get32(p, 12);
    }
    return r;
}

const char* describePcpResult(std::uint8_t result) {
    switch (result) {
    case kPcpSuccess: return "success";
    case kPcpUnsuppVersion: return "unsupported protocol version";
    case kPcpNotAuthorized: return "not authorized (port mapping disabled on the router)";
    case kPcpMalformedRequest: return "malformed request";
    case kPcpUnsuppOpcode: return "unsupported opcode";
    case kPcpUnsuppOption: return "unsupported option";
    case kPcpMalformedOption: return "malformed option";
    case kPcpNetworkFailure: return "router network failure";
    case kPcpNoResources: return "router out of mapping resources";
    case kPcpUnsuppProtocol: return "unsupported transport protocol";
    case kPcpUserExQuota: return "per-user mapping quota exceeded";
    case kPcpCannotProvideExternal: return "cannot provide the requested external port";
    case kPcpAddressMismatch: return "address mismatch (another NAT in between?)";
    case kPcpExcessiveRemotePeers: return "too many remote peers";
    }
    return "unknown error";
}

const char* describeNatPmpResult(std::uint16_t result) {
    switch (result) {
    case 0: return "success";
    case 1: return "unsupported version";
    case 2: return "not authorized (port mapping disabled on the router)";
    case 3: return "router network failure";
    case 4: return "router out of resources";
    case 5: return "unsupported opcode";
    }
    return "unknown error";
}

} // namespace mm2::net::natpmp
