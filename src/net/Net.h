#pragma once

// Common networking types for OpenMM2 multiplayer.
//
// Threading: everything in mm2::net except PortMapper is single-threaded and
// meant to be polled from the game thread (Session::update(),
// LanScanner::update(), ...). Nothing blocks except name resolution in
// Address::resolve(). PortMapper talks to the router on its own worker thread
// because UPnP/NAT-PMP requests can take seconds.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace mm2::net {

// Default UDP ports. The game port carries ENet traffic; the discovery port
// carries LAN session beacons (see Discovery.h).
inline constexpr std::uint16_t kDefaultGamePort = 2300;
inline constexpr std::uint16_t kDefaultDiscoveryPort = 2301;

// Reference-counted ENet/Winsock initialization. Every object that opens a
// socket holds one; it can also be held explicitly by the application.
class NetLibrary {
public:
    NetLibrary();
    ~NetLibrary();
    NetLibrary(const NetLibrary&);
    NetLibrary& operator=(const NetLibrary&) = default;

    bool ok() const { return m_ok; }

private:
    bool m_ok = false;
};

// IPv4 endpoint. ENet 1.3 is IPv4-only, so the whole module is too.
struct Address {
    std::uint32_t ip = 0; // host byte order, e.g. 0x7F000001 for 127.0.0.1
    std::uint16_t port = 0;

    static constexpr Address any(std::uint16_t port) { return {0, port}; }
    static constexpr Address loopback(std::uint16_t port) { return {0x7F000001u, port}; }
    static constexpr Address broadcast(std::uint16_t port) { return {0xFFFFFFFFu, port}; }

    // Parses a dotted quad, optionally followed by ":port". No DNS.
    static std::optional<Address> parse(std::string_view text, std::uint16_t defaultPort = 0);
    // Like parse(), but also resolves host names (blocking DNS lookup).
    static std::optional<Address> resolve(std::string_view text, std::uint16_t defaultPort = 0);

    std::string ipString() const;
    std::string toString() const; // "a.b.c.d:port"

    bool isPrivate() const; // RFC 1918, CGNAT 100.64/10, link-local, loopback
    constexpr bool operator==(const Address&) const = default;
};

// Monotonic milliseconds since the first call in this process.
std::uint64_t monotonicMs();

} // namespace mm2::net
