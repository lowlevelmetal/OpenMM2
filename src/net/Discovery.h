#pragma once

// LAN session discovery over plain UDP (separate from the ENet game port).
//
// Hosts run a LanBeacon bound to the discovery port (default 2301). It answers
// broadcast queries from scanners with a unicast advert and also broadcasts an
// unsolicited advert every couple of seconds. Scanners send queries from an
// ephemeral port (so replies reach exactly them, even with several programs on
// one machine) and optionally listen on the discovery port for announcements.

#include "net/Net.h"
#include "net/Protocol.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace mm2::net {

struct LanAdvert {
    std::uint16_t protocolVersion = kProtocolVersion;
    std::string sessionName;
    std::string hostName;
    std::string city;
    GameMode mode = GameMode::Cruise;
    std::uint16_t raceId = 0;
    std::uint8_t players = 0;
    std::uint8_t maxPlayers = 0;
    bool hasPassword = false;
    SessionPhase phase = SessionPhase::Lobby;
    std::uint16_t gamePort = kDefaultGamePort;
    std::string build;

    bool operator==(const LanAdvert&) const = default;
};

struct DiscoveredSession {
    Address address; // host IP + game port: pass to Session::join
    LanAdvert advert;
    std::uint32_t pingMs = 0; // 0 until a query round-trip has been measured
    std::uint64_t lastSeenMs = 0;
    bool compatible() const { return advert.protocolVersion == kProtocolVersion; }
};

// Wire format helpers (exposed for tests).
std::vector<std::byte> encodeLanQuery(std::uint32_t nonce);
std::vector<std::byte> encodeLanAdvert(std::uint32_t nonce, const LanAdvert& advert);
bool decodeLanQuery(std::span<const std::byte> packet, std::uint32_t& nonce);
bool decodeLanAdvert(std::span<const std::byte> packet, std::uint32_t& nonce, LanAdvert& advert);

// IPv4 broadcast addresses of all up, non-loopback interfaces, plus
// 255.255.255.255.
std::vector<std::uint32_t> broadcastAddresses();

// An IPv4 interface address and its netmask, host byte order.
struct Subnet {
    std::uint32_t ip = 0;
    std::uint32_t mask = 0;
};
// The subnets of all up, non-loopback IPv4 interfaces (VPN adapters included).
std::vector<Subnet> localSubnets();

// Whether a beacon answers a query that claims to come from `from`: loopback,
// a private address (Address::isPrivate) or one on `subnets`, so a spoofed
// query from the Internet cannot make it send adverts elsewhere, and never a
// broadcast, multicast or zero address, where one reply would reach many
// machines or none.
bool answersLanQueryFrom(const Address& from, std::span<const Subnet> subnets);

// Most sessions a LanScanner lists; adverts for further sessions are ignored.
inline constexpr std::size_t kMaxLanSessions = 64;

// The LAN address this machine uses for Internet traffic (no packets are
// sent), or 0 if there is no route.
std::uint32_t primaryLocalAddress();

class UdpSocket;

class LanBeacon {
public:
    LanBeacon();
    ~LanBeacon();

    bool start(std::uint16_t discoveryPort = kDefaultDiscoveryPort, std::string* error = nullptr);
    void stop();
    bool running() const;
    // Bound port (useful when started with port 0).
    std::uint16_t port() const;

    void setAdvert(const LanAdvert& advert) { m_advert = advert; }
    void setAnnounceInterval(std::uint32_t ms) { m_announceIntervalMs = ms; }
    // Where unsolicited adverts go (default: broadcastAddresses()).
    void setAnnounceTargets(std::vector<Address> targets) { m_targets = std::move(targets); }

    void update();

private:
    NetLibrary m_lib;
    std::unique_ptr<UdpSocket> m_socket;
    std::uint16_t m_port = 0;
    LanAdvert m_advert;
    std::uint32_t m_announceIntervalMs = 2000;
    std::uint64_t m_nextAnnounce = 0;
    std::vector<Address> m_targets;
    std::vector<Subnet> m_subnets;
    // Replies left in the budget (see kBeaconReplyRate) and when it was filled.
    double m_replyTokens = 0.0;
    std::uint64_t m_replyRefill = 0;
};

class LanScanner {
public:
    LanScanner();
    ~LanScanner();

    // `listenForAnnouncements` also binds the discovery port (shared with
    // other programs) to pick up unsolicited adverts. Off by default: active
    // queries already find every host, and on Linux a passive socket on the
    // shared port can swallow unicast queries meant for a host on the same
    // machine (UDP SO_REUSEADDR delivers unicast to only one socket).
    bool start(std::uint16_t discoveryPort = kDefaultDiscoveryPort, bool listenForAnnouncements = false,
               std::string* error = nullptr);
    void stop();

    // Extra unicast query targets (e.g. 127.0.0.1 or a known host); the
    // discovery port is used if the address has port 0.
    void addTarget(const Address& target) { m_extraTargets.push_back(target); }
    void setBroadcast(bool enabled) { m_broadcast = enabled; }
    void setExpiry(std::uint32_t ms) { m_expiryMs = ms; }

    // Sends a query round.
    void scan();
    // Receives replies/announcements and expires stale entries.
    void update();

    std::vector<DiscoveredSession> sessions() const;
    void clear() { m_sessions.clear(); }

private:
    void receive(UdpSocket& socket);

    NetLibrary m_lib;
    std::unique_ptr<UdpSocket> m_query;  // ephemeral port
    std::unique_ptr<UdpSocket> m_listen; // discovery port, optional
    std::uint16_t m_port = 0;
    bool m_broadcast = true;
    std::uint32_t m_expiryMs = 6000;
    std::vector<Address> m_extraTargets;
    std::map<std::uint32_t, std::uint64_t> m_pendingQueries; // nonce -> send time
    std::map<std::uint64_t, DiscoveredSession> m_sessions;   // key: ip << 16 | port
    std::uint32_t m_nonceCounter = 0;
};

// Minimal non-blocking UDP socket on top of ENet's portable socket layer.
class UdpSocket {
public:
    ~UdpSocket();
    // Binds to `bind` (port 0 = ephemeral). `reuse` allows several sockets on
    // the same port (needed for the shared discovery port).
    static std::unique_ptr<UdpSocket> open(const Address& bind, bool broadcast, bool reuse,
                                           std::string* error = nullptr);
    bool sendTo(const Address& to, std::span<const std::byte> data);
    // Returns bytes received (0 = nothing pending, -1 = error).
    int receiveFrom(Address& from, std::span<std::byte> buffer);
    // Sets the default destination (UDP has no handshake); afterwards
    // localAddress() reports the interface address used to reach `to`.
    bool connect(const Address& to);
    // Waits up to `timeoutMs` for data. Returns true if readable.
    bool wait(std::uint32_t timeoutMs);
    Address localAddress() const;

private:
    UdpSocket() = default;
    NetLibrary m_lib;
    std::intptr_t m_socket = -1;
};

} // namespace mm2::net
