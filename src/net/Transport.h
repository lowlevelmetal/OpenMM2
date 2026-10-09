#pragma once

// Thin ownership wrapper around an ENet host. Peers are identified by small
// integer ids that stay valid until the Disconnected event for that peer.
// Not thread-safe: create, service and destroy from one thread.

#include "net/Net.h"
#include "net/Protocol.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

struct _ENetHost;
struct _ENetPeer;

namespace mm2::net {

using PeerId = std::uint32_t;
inline constexpr PeerId kInvalidPeer = 0;

struct TransportEvent {
    enum class Type { Connected, Disconnected, Received };
    Type type = Type::Received;
    PeerId peer = kInvalidPeer;
    std::uint32_t data = 0; // connect/disconnect data
    Channel channel = Channel::Control;
    std::vector<std::byte> payload;
};

struct PeerStats {
    Address address;
    std::uint32_t rttMs = 0;
    std::uint32_t rttVarianceMs = 0;
    float packetLoss = 0.0f; // 0..1, reliable packets
    std::uint64_t bytesSent = 0;
    std::uint64_t bytesReceived = 0;
};

struct TransportConfig {
    std::size_t maxPeers = kMaxPlayers;
    std::uint32_t incomingBandwidth = 0; // bytes/s, 0 = unlimited
    std::uint32_t outgoingBandwidth = 0;
    // Peer timeout (ENet: minimum/maximum time without acknowledgement).
    std::uint32_t timeoutMinMs = 5000;
    std::uint32_t timeoutMaxMs = 15000;
    bool compress = true; // ENet range coder
    // Largest message a peer may send, and how much of a peer's received but
    // not yet serviced data may wait. ENet's defaults (32 MiB each) let any
    // connected peer make this machine allocate a whole packet's size with its
    // first fragment. OpenMM2's largest message (a Welcome with 16 players and
    // 32 extra settings) is about 5 KiB.
    std::size_t maxPacketSize = 64 * 1024;
    std::size_t maxWaitingData = 1024 * 1024;
};

class Transport {
public:
    explicit Transport(TransportConfig config = {});
    ~Transport();
    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;

    // Server mode: binds to `bind` (Address::any(port) for all interfaces;
    // port 0 picks a free port, see port()).
    bool listen(const Address& bind, std::string* error = nullptr);
    // Client mode: creates an unbound host with one outgoing peer slot.
    bool startClient(std::string* error = nullptr);
    bool active() const { return m_host != nullptr; }

    // Starts connecting; a Connected or Disconnected event follows.
    PeerId connect(const Address& address, std::uint32_t data = kConnectData);

    // Sends pending packets and appends received events to `out`. Waits at
    // most `timeoutMs` for the first event (0 = never block).
    void service(std::vector<TransportEvent>& out, std::uint32_t timeoutMs = 0);

    bool send(PeerId peer, Channel channel, std::span<const std::byte> data);
    void broadcast(Channel channel, std::span<const std::byte> data);
    // Graceful disconnect: queued reliable packets are delivered first when
    // `afterPending` is set. A Disconnected event follows.
    void disconnect(PeerId peer, std::uint32_t data, bool afterPending = false);
    // Drops the peer immediately without notifying it and without an event.
    void reset(PeerId peer);
    void flush();

    // Disconnects every peer, waits up to `graceMs` for acknowledgements and
    // destroys the host.
    void shutdown(std::uint32_t data, std::uint32_t graceMs = 250);

    std::uint16_t port() const;
    bool hasPeer(PeerId peer) const { return m_peers.contains(peer); }
    std::size_t peerCount() const { return m_peers.size(); }
    PeerStats stats(PeerId peer) const;
    std::uint64_t totalBytesSent() const { return m_totalSent; }
    std::uint64_t totalBytesReceived() const { return m_totalReceived; }

private:
    _ENetPeer* find(PeerId id) const;
    PeerId registerPeer(_ENetPeer* peer);
    void configureHost();
    void configurePeer(_ENetPeer* peer);
    void accountTraffic();

    NetLibrary m_lib;
    TransportConfig m_config;
    _ENetHost* m_host = nullptr;
    std::unordered_map<PeerId, _ENetPeer*> m_peers;
    PeerId m_nextId = 1;
    std::uint64_t m_totalSent = 0;
    std::uint64_t m_totalReceived = 0;
};

} // namespace mm2::net
