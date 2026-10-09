#pragma once

// Lobby + in-game session on top of Transport (star topology: the host relays
// everything; there is no host migration — if the host leaves, the session
// ends for everyone).
//
// Usage (game thread):
//   Session s;
//   s.host({...}) or s.join({...});
//   every frame: s.update(); for (auto& e : s.takeEvents()) std::visit(..., e);
//   in game:     s.submitLocalState(snapshot);  s.sampleRemote(id, out);
//
// The session knows nothing about game rules: it carries settings, players,
// chat, a countdown, vehicle snapshots and opaque game events. The host may
// install an event filter to validate or rewrite events before relaying.

#include "net/AmbientState.h"
#include "net/ClockSync.h"
#include "net/Discovery.h"
#include "net/Protocol.h"
#include "net/Snapshot.h"
#include "net/Transport.h"

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace mm2::net {

struct LocalPlayer {
    std::string name = "Player";
    std::string car = "vpbug";
    std::uint8_t color = 0;
    std::uint8_t team = 0;
};

struct HostParams {
    SessionSettings settings;
    LocalPlayer player;
    std::string password; // empty = no password
    Address bind = Address::any(kDefaultGamePort);
    bool advertiseOnLan = true;
    std::uint16_t discoveryPort = kDefaultDiscoveryPort;
};

struct JoinParams {
    Address host;
    LocalPlayer player;
    std::string password;
};

struct SessionConfig {
    TransportConfig transport;
    std::uint32_t snapshotRateHz = 20;       // own vehicle -> host, and host -> clients
    double interpolationDelayMs = 100.0;     // remote vehicles are shown this far in the past
    double maxExtrapolationMs = 250.0;       // then held still
    std::uint32_t joinTimeoutMs = 10000;     // handshake must finish within this
    std::uint32_t connectTimeoutMs = 8000;   // ENet connect attempt
    std::uint32_t pingBroadcastIntervalMs = 2000;
    std::string build = "OpenMM2";           // reported in Hello, informational
};

// --- Events -----------------------------------------------------------------------

namespace ev {
struct JoinAccepted {
    std::uint8_t localId;
};
struct JoinFailed {
    DisconnectReason reason;
    std::string message;
};
struct Disconnected {
    DisconnectReason reason;
    std::string message;
};
struct PlayerJoined {
    PlayerInfo player;
};
struct PlayerLeft {
    PlayerInfo player;
    DisconnectReason reason;
};
struct PlayerUpdated {
    PlayerInfo player;
};
struct Chat {
    std::uint8_t from;
    std::string text;
};
struct SettingsChanged {
    SessionSettings settings;
};
struct CountdownStarted {
    std::uint32_t startTime; // session time
};
struct GameStarted {};
struct ReturnedToLobby {};
struct GameEvent {
    std::uint8_t from;
    std::uint16_t type;
    std::uint32_t time;
    std::vector<std::byte> payload;
};
} // namespace ev

using SessionEvent = std::variant<ev::JoinAccepted, ev::JoinFailed, ev::Disconnected, ev::PlayerJoined,
                                  ev::PlayerLeft, ev::PlayerUpdated, ev::Chat, ev::SettingsChanged,
                                  ev::CountdownStarted, ev::GameStarted, ev::ReturnedToLobby, ev::GameEvent>;

class Session {
public:
    enum class Role { None, Host, Client };
    enum class State { Idle, Connecting, Joining, Active, Closed };

    explicit Session(SessionConfig config = {});
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    bool host(const HostParams& params, std::string* error = nullptr);
    bool join(const JoinParams& params, std::string* error = nullptr);
    // Leaves gracefully (host: closes the session for everyone).
    void leave();

    // Services the network, timers, clock sync and snapshot sending.
    void update();
    std::vector<SessionEvent> takeEvents() { return std::exchange(m_events, {}); }

    Role role() const { return m_role; }
    State state() const { return m_state; }
    bool isHost() const { return m_role == Role::Host; }
    SessionPhase phase() const { return m_phase; }
    std::uint8_t localId() const { return m_localId; }
    const SessionSettings& settings() const { return m_settings; }
    const std::vector<PlayerInfo>& players() const { return m_players; }
    const PlayerInfo* player(std::uint8_t id) const;
    std::uint16_t port() const { return m_transport ? m_transport->port() : 0; }

    // Session clock in ms (host clock; estimated on clients).
    std::uint32_t time() const;
    bool clockSynced() const { return m_role == Role::Host || m_clock.synced(); }
    std::uint32_t countdownEnd() const { return m_countdownEnd; }

    // --- Lobby (both roles) ---
    void setLocalPlayer(const std::string& car, std::uint8_t color, std::uint8_t team);
    void setReady(bool ready);
    void sendChat(const std::string& text);

    // --- Host only ---
    void updateSettings(const SessionSettings& settings);
    void setPassword(const std::string& password);
    void kick(std::uint8_t playerId, const std::string& reason);
    // Starts the race `delayMs` from now; GameStarted fires on every machine
    // when the session clock reaches the start time.
    void startCountdown(std::uint32_t delayMs);
    void returnToLobby();
    // Called for every game event before the host relays it (including the
    // host's own). Return false to drop it.
    void setEventFilter(std::function<bool(std::uint8_t from, GameEventMsg&)> filter) {
        m_eventFilter = std::move(filter);
    }

    // --- Replication ---
    // Latest state of the local vehicle; sent at snapshotRateHz.
    void submitLocalState(const VehicleSnapshot& state);
    // Remote vehicle state at (time() - interpolationDelay).
    SnapshotBuffer::Result sampleRemote(std::uint8_t playerId, VehicleSnapshot& out) const;
    SnapshotBuffer::Result sampleRemoteAt(std::uint8_t playerId, double sessionTime, VehicleSnapshot& out) const;
    // Sends a game event to everyone (or one player). Delivered reliably and
    // in order; the sender does not receive its own event back.
    void sendGameEvent(std::uint16_t type, std::vector<std::byte> payload,
                       std::uint8_t target = kBroadcastTarget);

    // Connection quality to the host (clients) or to a player (host).
    PeerStats peerStats(std::uint8_t playerId) const;

    // --- Shared ambient traffic (multiplayer cruise) ---
    // Host: sends one player its cars on the unreliable Ambient channel.
    // Returns the encoded size in bytes (0 when nothing was sent).
    std::size_t sendAmbientState(std::uint8_t playerId, const AmbientStateMsg& msg);
    // Client: the messages received since the last call, oldest first (at
    // most kMaxQueuedAmbientStates; older ones are dropped).
    std::vector<AmbientStateMsg> takeAmbientStates() { return std::exchange(m_ambientStates, {}); }
    static constexpr std::size_t kMaxQueuedAmbientStates = 16;

private:
    struct Remote {
        PeerId peer = kInvalidPeer;
        std::uint8_t playerId = kInvalidPlayerId; // assigned after Hello
        std::array<std::byte, 16> nonce{};
        std::uint64_t connectedAt = 0;
    };

    void resetState();
    void handleTransportEvent(TransportEvent& e);
    void hostHandle(Remote& r, MsgType type, std::span<const std::byte> data);
    void clientHandle(MsgType type, std::span<const std::byte> data);
    void hostAcceptHello(Remote& r, HelloMsg& hello);
    void hostRemovePlayer(std::uint8_t id, DisconnectReason reason);
    void hostRelayEvent(std::uint8_t from, GameEventMsg msg);
    void hostSendWorldState();
    void hostAdvertise();
    void close(DisconnectReason reason, std::string message, bool failedJoin);
    void tickCountdown();
    PlayerInfo* findPlayer(std::uint8_t id);
    Remote* remoteForPlayer(std::uint8_t id);
    std::uint8_t allocatePlayerId() const;
    std::string uniqueName(std::string name) const;
    template <class M>
    void sendTo(PeerId peer, Channel channel, M msg);
    template <class M>
    void sendToPlayers(Channel channel, M msg, std::uint8_t except = kInvalidPlayerId);
    void emit(SessionEvent e) { m_events.push_back(std::move(e)); }

    SessionConfig m_config;
    std::unique_ptr<Transport> m_transport;
    Role m_role = Role::None;
    State m_state = State::Idle;
    SessionPhase m_phase = SessionPhase::Lobby;
    std::vector<SessionEvent> m_events;

    SessionSettings m_settings;
    std::vector<PlayerInfo> m_players;
    std::uint8_t m_localId = kInvalidPlayerId;
    std::string m_password;
    std::uint32_t m_countdownEnd = 0;

    // Host
    std::map<PeerId, Remote> m_remotes;
    std::uint64_t m_hostEpoch = 0; // monotonicMs() at session start
    std::uint64_t m_lastPingBroadcast = 0;
    std::map<std::uint8_t, VehicleSnapshot> m_pendingStates; // newest unsent state per player
    std::function<bool(std::uint8_t, GameEventMsg&)> m_eventFilter;
    std::unique_ptr<LanBeacon> m_beacon;

    // Client
    PeerId m_hostPeer = kInvalidPeer;
    std::uint64_t m_connectStarted = 0;
    ClockSync m_clock;
    std::uint64_t m_nextTimeRequest = 0;
    int m_timeRequestsSent = 0;
    std::string m_joinPassword;
    LocalPlayer m_joinPlayer;
    // Latest requested car/colour/team/ready: requests sent back to back must
    // not be built from the not-yet-confirmed player list.
    PlayerRequestMsg m_request;

    // Both
    std::map<std::uint8_t, SnapshotBuffer> m_remoteStates;
    std::optional<VehicleSnapshot> m_localState;
    std::uint64_t m_lastSnapshotSent = 0;
    std::vector<AmbientStateMsg> m_ambientStates; // client: received, not yet taken
};

// Proof sent in Hello: SHA-256(nonce || password); all zeros when empty.
std::array<std::byte, 32> passwordProof(const std::array<std::byte, 16>& nonce, const std::string& password);

} // namespace mm2::net
