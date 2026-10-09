#include "net/Transport.h"

#include "core/Log.h"

#include <enet/enet.h>

#include <format>

namespace mm2::net {
namespace {

void setError(std::string* error, std::string msg) {
    if (error)
        *error = std::move(msg);
}

ENetAddress toEnet(const Address& a) {
    ENetAddress e{};
    e.host = a.ip == 0 ? ENET_HOST_ANY : ENET_HOST_TO_NET_32(a.ip);
    e.port = a.port;
    return e;
}

Address fromEnet(const ENetAddress& e) { return {ENET_NET_TO_HOST_32(e.host), e.port}; }

std::uint32_t packetFlags(Channel channel) {
    switch (channel) {
    case Channel::Control:
    case Channel::Events: return ENET_PACKET_FLAG_RELIABLE;
    case Channel::State: return 0; // unreliable, sequenced
    }
    return ENET_PACKET_FLAG_RELIABLE;
}

PeerId idOf(const ENetPeer* peer) {
    return static_cast<PeerId>(reinterpret_cast<std::uintptr_t>(peer->data));
}

} // namespace

Transport::Transport(TransportConfig config) : m_config(config) {}

Transport::~Transport() {
    if (m_host)
        enet_host_destroy(m_host);
}

bool Transport::listen(const Address& bind, std::string* error) {
    if (!m_lib.ok()) {
        setError(error, "network initialization failed");
        return false;
    }
    if (m_host) {
        setError(error, "transport already active");
        return false;
    }
    const ENetAddress addr = toEnet(bind);
    m_host = enet_host_create(&addr, m_config.maxPeers, kChannelCount, m_config.incomingBandwidth,
                              m_config.outgoingBandwidth);
    if (!m_host) {
        setError(error, std::format("cannot listen on UDP {} (port in use?)", bind.toString()));
        return false;
    }
    configureHost();
    log::info("net: listening on UDP port {}", port());
    return true;
}

bool Transport::startClient(std::string* error) {
    if (!m_lib.ok()) {
        setError(error, "network initialization failed");
        return false;
    }
    if (m_host) {
        setError(error, "transport already active");
        return false;
    }
    m_host = enet_host_create(nullptr, 1, kChannelCount, m_config.incomingBandwidth, m_config.outgoingBandwidth);
    if (!m_host) {
        setError(error, "cannot create a UDP socket");
        return false;
    }
    configureHost();
    return true;
}

void Transport::configureHost() {
    if (m_config.compress)
        enet_host_compress_with_range_coder(m_host);
    m_host->maximumPacketSize = m_config.maxPacketSize;
    m_host->maximumWaitingData = m_config.maxWaitingData;
}

PeerId Transport::registerPeer(ENetPeer* peer) {
    PeerId id = m_nextId++;
    if (m_nextId == kInvalidPeer)
        m_nextId = 1;
    peer->data = reinterpret_cast<void*>(static_cast<std::uintptr_t>(id));
    m_peers[id] = peer;
    return id;
}

void Transport::configurePeer(ENetPeer* peer) {
    enet_peer_timeout(peer, 0, m_config.timeoutMinMs, m_config.timeoutMaxMs);
    enet_peer_ping_interval(peer, 500);
}

PeerId Transport::connect(const Address& address, std::uint32_t data) {
    if (!m_host)
        return kInvalidPeer;
    const ENetAddress addr = toEnet(address);
    ENetPeer* peer = enet_host_connect(m_host, &addr, kChannelCount, data);
    if (!peer)
        return kInvalidPeer;
    configurePeer(peer);
    return registerPeer(peer);
}

ENetPeer* Transport::find(PeerId id) const {
    const auto it = m_peers.find(id);
    return it == m_peers.end() ? nullptr : it->second;
}

void Transport::accountTraffic() {
    m_totalSent += m_host->totalSentData;
    m_totalReceived += m_host->totalReceivedData;
    m_host->totalSentData = 0;
    m_host->totalReceivedData = 0;
}

void Transport::service(std::vector<TransportEvent>& out, std::uint32_t timeoutMs) {
    if (!m_host)
        return;
    ENetEvent ev;
    int rc = enet_host_service(m_host, &ev, timeoutMs);
    while (rc > 0) {
        TransportEvent te;
        switch (ev.type) {
        case ENET_EVENT_TYPE_CONNECT: {
            PeerId id = idOf(ev.peer);
            if (id == kInvalidPeer || !m_peers.contains(id)) { // incoming connection
                configurePeer(ev.peer);
                id = registerPeer(ev.peer);
            }
            te.type = TransportEvent::Type::Connected;
            te.peer = id;
            te.data = ev.data;
            out.push_back(std::move(te));
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT: {
            const PeerId id = idOf(ev.peer);
            ev.peer->data = nullptr;
            if (m_peers.erase(id)) {
                te.type = TransportEvent::Type::Disconnected;
                te.peer = id;
                te.data = ev.data;
                out.push_back(std::move(te));
            }
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE: {
            const PeerId id = idOf(ev.peer);
            if (m_peers.contains(id) && ev.channelID < kChannelCount) {
                te.type = TransportEvent::Type::Received;
                te.peer = id;
                te.channel = static_cast<Channel>(ev.channelID);
                const auto* bytes = reinterpret_cast<const std::byte*>(ev.packet->data);
                te.payload.assign(bytes, bytes + ev.packet->dataLength);
                out.push_back(std::move(te));
            }
            enet_packet_destroy(ev.packet);
            break;
        }
        case ENET_EVENT_TYPE_NONE: break;
        }
        rc = enet_host_check_events(m_host, &ev);
    }
    if (rc < 0)
        log::warn("net: enet_host_service failed");
    accountTraffic();
}

bool Transport::send(PeerId peer, Channel channel, std::span<const std::byte> data) {
    ENetPeer* p = find(peer);
    if (!p || p->state != ENET_PEER_STATE_CONNECTED)
        return false;
    ENetPacket* packet = enet_packet_create(data.data(), data.size(), packetFlags(channel));
    if (!packet)
        return false;
    if (enet_peer_send(p, static_cast<enet_uint8>(channel), packet) != 0) {
        enet_packet_destroy(packet);
        return false;
    }
    return true;
}

void Transport::broadcast(Channel channel, std::span<const std::byte> data) {
    for (const auto& [id, peer] : m_peers)
        if (peer->state == ENET_PEER_STATE_CONNECTED)
            send(id, channel, data);
}

void Transport::disconnect(PeerId peer, std::uint32_t data, bool afterPending) {
    ENetPeer* p = find(peer);
    if (!p)
        return;
    if (afterPending)
        enet_peer_disconnect_later(p, data);
    else
        enet_peer_disconnect(p, data);
}

void Transport::reset(PeerId peer) {
    ENetPeer* p = find(peer);
    if (!p)
        return;
    p->data = nullptr;
    m_peers.erase(peer);
    enet_peer_reset(p);
}

void Transport::flush() {
    if (m_host) {
        enet_host_flush(m_host);
        accountTraffic();
    }
}

void Transport::shutdown(std::uint32_t data, std::uint32_t graceMs) {
    if (!m_host)
        return;
    for (const auto& [id, peer] : m_peers)
        enet_peer_disconnect(peer, data);
    const std::uint64_t deadline = monotonicMs() + graceMs;
    while (!m_peers.empty() && monotonicMs() < deadline) {
        ENetEvent ev;
        const int rc = enet_host_service(m_host, &ev, 10);
        if (rc < 0)
            break;
        if (rc > 0) {
            if (ev.type == ENET_EVENT_TYPE_RECEIVE)
                enet_packet_destroy(ev.packet);
            else if (ev.type == ENET_EVENT_TYPE_DISCONNECT) {
                m_peers.erase(idOf(ev.peer));
                ev.peer->data = nullptr;
            }
        }
    }
    for (const auto& [id, peer] : m_peers) {
        peer->data = nullptr;
        enet_peer_reset(peer);
    }
    m_peers.clear();
    accountTraffic();
    enet_host_destroy(m_host);
    m_host = nullptr;
}

std::uint16_t Transport::port() const { return m_host ? m_host->address.port : 0; }

PeerStats Transport::stats(PeerId peer) const {
    PeerStats s;
    if (const ENetPeer* p = find(peer)) {
        s.address = fromEnet(p->address);
        s.rttMs = p->roundTripTime;
        s.rttVarianceMs = p->roundTripTimeVariance;
        s.packetLoss = static_cast<float>(p->packetLoss) / static_cast<float>(ENET_PEER_PACKET_LOSS_SCALE);
        s.bytesSent = p->outgoingDataTotal;
        s.bytesReceived = p->incomingDataTotal;
    }
    return s;
}

} // namespace mm2::net
