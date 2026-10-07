#pragma once

// PCP (RFC 6887) and NAT-PMP (RFC 6886) message encoding. Both talk to the
// default gateway on UDP 5351. A PCP request sent to a NAT-PMP-only router is
// answered with a NAT-PMP "unsupported version" reply, which is how the
// backend decides to fall back.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace mm2::net::natpmp {

inline constexpr std::uint16_t kServerPort = 5351;
inline constexpr std::uint8_t kPcpVersion = 2;
inline constexpr std::uint8_t kNatPmpVersion = 0;
inline constexpr std::uint8_t kProtocolUdp = 17;

using Nonce = std::array<std::byte, 12>;

enum PcpOpcode : std::uint8_t { kPcpAnnounce = 0, kPcpMap = 1 };
enum PcpResult : std::uint8_t {
    kPcpSuccess = 0,
    kPcpUnsuppVersion = 1,
    kPcpNotAuthorized = 2,
    kPcpMalformedRequest = 3,
    kPcpUnsuppOpcode = 4,
    kPcpUnsuppOption = 5,
    kPcpMalformedOption = 6,
    kPcpNetworkFailure = 7,
    kPcpNoResources = 8,
    kPcpUnsuppProtocol = 9,
    kPcpUserExQuota = 10,
    kPcpCannotProvideExternal = 11,
    kPcpAddressMismatch = 12,
    kPcpExcessiveRemotePeers = 13,
};

// IPv4 addresses are host byte order; PCP carries them as IPv4-mapped IPv6.
std::vector<std::byte> encodePcpAnnounce(std::uint32_t clientIp);
std::vector<std::byte> encodePcpMap(const Nonce& nonce, std::uint32_t clientIp, std::uint16_t internalPort,
                                    std::uint16_t suggestedExternalPort, std::uint32_t suggestedExternalIp,
                                    std::uint32_t lifetime);

struct PcpResponse {
    std::uint8_t version = 0;
    std::uint8_t opcode = 0; // without the response bit
    std::uint8_t result = 0;
    std::uint32_t lifetime = 0;
    std::uint32_t epoch = 0;
    // MAP only:
    Nonce nonce{};
    std::uint8_t protocol = 0;
    std::uint16_t internalPort = 0;
    std::uint16_t externalPort = 0;
    std::uint32_t externalIp = 0;
};

// Decodes a PCP response. A NAT-PMP "unsupported version" reply decodes to
// {version 0, result kPcpUnsuppVersion}.
std::optional<PcpResponse> decodePcpResponse(std::span<const std::byte> packet);

std::vector<std::byte> encodeNatPmpExternalAddress();
std::vector<std::byte> encodeNatPmpMapUdp(std::uint16_t internalPort, std::uint16_t suggestedExternalPort,
                                          std::uint32_t lifetime);

struct NatPmpResponse {
    std::uint8_t opcode = 0; // without the response bit: 0 = external address, 1 = map UDP
    std::uint16_t result = 0;
    std::uint32_t epoch = 0;
    std::uint32_t externalIp = 0; // opcode 0
    std::uint16_t internalPort = 0;
    std::uint16_t externalPort = 0;
    std::uint32_t lifetime = 0;
};
std::optional<NatPmpResponse> decodeNatPmpResponse(std::span<const std::byte> packet);

const char* describePcpResult(std::uint8_t result);
const char* describeNatPmpResult(std::uint16_t result);

} // namespace mm2::net::natpmp
