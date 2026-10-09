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
    // Remote vehicles are shown a playout delay in the past, which follows
    // what each one's snapshots need to arrive in time
    // (SnapshotBuffer::requiredDelay), within these bounds.
    double interpolationDelayMs = 50.0;
    double maxInterpolationDelayMs = 500.0;
    double maxExtrapolationMs = 250.0;       // then held still
    std::uint32_t joinTimeoutMs = 10000;     // handshake must finish within this
    std::uint32_t connectTimeoutMs = 8000;   // ENet connect attempt
    std::uint32_t pingBroadcastIntervalMs = 2000;
    // The race start (host): how long after GO DRIVE the host waits for
    // players still loading before it starts without them (OpenMM2's own
    // limit: MM2 waits for every player still in the session), and the
    // bounds of the lead it gives its start message to reach everyone
    // (twice the slowest round trip plus 100 ms).
    std::uint32_t loadWaitMs = 60000;
    std::uint32_t startLeadMinMs = 200;
    std::uint32_t startLeadMaxMs = 1000;
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
// GO DRIVE: load race `race` now (RaceLoad).
struct RaceLoading {
    std::uint32_t race;
    std::uint32_t orderTime; // session time of the order
};
// A player (this one included) has loaded the current race.
struct PlayerLoaded {
    std::uint8_t id;
    std::uint32_t race;
};
// The host has set the current race's start (its countdown's end).
struct RaceStartSet {
    std::uint32_t race;
    std::uint32_t startTime; // session time
};
// The session clock has reached the start time.
struct GameStarted {};
// The host took everyone back to the lobby, ending race `race`.
struct ReturnedToLobby {
    std::uint32_t race;
};
struct GameEvent {
    std::uint8_t from;
    std::uint16_t type;
    std::uint32_t time;
    std::vector<std::byte> payload;
};
} // namespace ev

using SessionEvent =
    std::variant<ev::JoinAccepted, ev::JoinFailed, ev::Disconnected, ev::PlayerJoined, ev::PlayerLeft,
                 ev::PlayerUpdated, ev::Chat, ev::SettingsChanged, ev::RaceLoading, ev::PlayerLoaded,
                 ev::RaceStartSet, ev::GameStarted, ev::ReturnedToLobby, ev::GameEvent>;

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

    // Session clock in ms (host clock; estimated on clients). On a client it
    // follows the estimate smoothly while a race runs (SlewedClock).
    std::uint32_t time() const;
    double timeMs() const; // the same with sub-millisecond precision
    bool clockSynced() const { return m_role == Role::Host || m_clock.synced(); }

    // --- The race (docs/multiplayer.md, "Race start") ---
    // The host's number of the current (or last) race, from 1; 0 before the
    // first.
    std::uint32_t raceNumber() const { return m_race; }
    // Session time at which the host ordered it (GO DRIVE).
    std::uint32_t raceOrderTime() const { return m_raceOrderTime; }
    // Whether the host has set its start yet, and the session time it
    // starts at (its countdown ends: the cars go).
    bool raceStartKnown() const { return m_startKnown; }
    std::uint32_t raceStartTime() const { return m_startKnown ? m_startTime : 0; }
    // Whether a player has reported the current race loaded.
    bool playerLoaded(std::uint8_t id) const;
    // This machine has loaded the current race (MM2's RaceReady): the host
    // starts the race once every player still in the session has.
    // Repeated calls, and calls outside a race, do nothing.
    void reportLoaded();

    // --- Lobby (both roles) ---
    void setLocalPlayer(const std::string& car, std::uint8_t color, std::uint8_t team);
    void setReady(bool ready);
    void sendChat(const std::string& text);

    // --- Host only ---
    void updateSettings(const SessionSettings& settings);
    void setPassword(const std::string& password);
    void kick(std::uint8_t playerId, const std::string& reason);
    // GO DRIVE: every machine loads a new race and reports it loaded
    // (reportLoaded). When every player still in the session has (or
    // SessionConfig::loadWaitMs after the order, without the ones still
    // loading), the host sets the start: a lead for the message to reach
    // everyone plus `countdownMs` from then. GameStarted fires on every
    // machine when the session clock reaches it.
    void startRace(std::uint32_t countdownMs);
    void returnToLobby();
    // Called for every game event before the host relays it (including the
    // host's own). Return false to drop it.
    void setEventFilter(std::function<bool(std::uint8_t from, GameEventMsg&)> filter) {
        m_eventFilter = std::move(filter);
    }

    // --- Replication ---
    // Latest state of the local vehicle; sent at snapshotRateHz during a
    // race (countdown and game; ignored in the lobby). It is stamped with
    // the session time at which the state was simulated, `sessionTimeMs`,
    // or now.
    void submitLocalState(const VehicleSnapshot& state);
    void submitLocalState(const VehicleSnapshot& state, double sessionTimeMs);
    // Remote vehicle state at (now - the player's playout delay), or at
    // (`sessionTimeMs` - the playout delay).
    SnapshotBuffer::Result sampleRemote(std::uint8_t playerId, VehicleSnapshot& out) const;
    SnapshotBuffer::Result sampleRemoteDelayed(std::uint8_t playerId, double sessionTimeMs, VehicleSnapshot& out) const;
    // Remote vehicle state at exactly `sessionTime`.
    SnapshotBuffer::Result sampleRemoteAt(std::uint8_t playerId, double sessionTime, VehicleSnapshot& out) const;
    // The playout delay (ms) a remote vehicle is shown with.
    double playoutDelay(std::uint8_t playerId) const;
    // Called with every remote vehicle snapshot taken in (player, snapshot,
    // session time of arrival); for diagnostics.
    void setStateObserver(std::function<void(std::uint8_t, const VehicleSnapshot&, double)> observer) {
        m_stateObserver = std::move(observer);
    }
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
    // Token bucket: how many messages of a kind a joiner may make the host
    // relay to everyone (each one goes out once per other player).
    struct RateLimit {
        double tokens = -1.0; // full on first use
        std::uint64_t last = 0;
        bool take(std::uint64_t now, double perSecond, double burst);
    };
    struct Remote {
        PeerId peer = kInvalidPeer;
        std::uint8_t playerId = kInvalidPlayerId; // assigned after Hello
        std::array<std::byte, 16> nonce{};
        std::uint64_t connectedAt = 0;
        RateLimit chat, updates, events, damage;
        bool updatePending = false; // a PlayerRequest applied but not yet relayed
    };

    void resetState();
    void handleTransportEvent(TransportEvent& e);
    void hostHandle(Remote& r, MsgType type, std::span<const std::byte> data);
    void clientHandle(MsgType type, std::span<const std::byte> data);
    void hostAcceptHello(Remote& r, HelloMsg& hello);
    void hostRemovePlayer(std::uint8_t id, DisconnectReason reason);
    void hostRelayEvent(std::uint8_t from, GameEventMsg msg);
    void hostRelayUpdates();
    void hostSendWorldState();
    void hostRelayState(std::uint8_t from, const VehicleSnapshot& state);
    void hostAdvertise();
    void close(DisconnectReason reason, std::string message, bool failedJoin);
    void tickCountdown();
    void hostCheckStart();
    void markLoaded(std::uint8_t id);
    void clearRace(); // no race loading: the loaded flags and the start
    void updateShownClock();
    // Takes in a remote vehicle's snapshot; false when it is ignored (in the
    // lobby, late packets of the last race; a time stamp far from this
    // clock's).
    bool receiveState(std::uint8_t id, const VehicleSnapshot& state);
    // Moves each remote vehicle's playout delay toward what it needs.
    void updatePlayout();
    bool replicating() const { return m_phase != SessionPhase::Lobby; }
    void resetReplication();
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
    // The race: its number, order time, start, and who has loaded it.
    std::uint32_t m_race = 0;
    std::uint32_t m_raceOrderTime = 0;
    bool m_startKnown = false;
    std::uint32_t m_startTime = 0;
    std::uint32_t m_countdownMs = 0; // host: the start's countdown
    std::vector<std::uint8_t> m_loaded;

    // Host
    std::map<PeerId, Remote> m_remotes;
    std::uint64_t m_hostEpoch = 0; // monotonicMs() at session start
    std::uint64_t m_lastPingBroadcast = 0;
    std::function<bool(std::uint8_t, GameEventMsg&)> m_eventFilter;
    std::unique_ptr<LanBeacon> m_beacon;
    // Wrong passwords per address: count and the time of the first one.
    std::map<std::uint32_t, std::pair<int, std::uint64_t>> m_passwordFailures;

    // Client
    PeerId m_hostPeer = kInvalidPeer;
    std::uint64_t m_connectStarted = 0;
    ClockSync m_clock;
    SlewedClock m_shownClock; // the offset time() uses
    std::uint64_t m_nextTimeRequest = 0;
    int m_timeRequestsSent = 0;
    std::string m_joinPassword;
    LocalPlayer m_joinPlayer;
    // Latest requested car/colour/team/ready: requests sent back to back must
    // not be built from the not-yet-confirmed player list.
    PlayerRequestMsg m_request;

    // Both
    struct RemoteVehicle {
        SnapshotBuffer buffer;
        double delay = -1.0; // playout delay (ms); negative until the first snapshot
        bool measured = false; // delay set from a measurement
    };
    std::map<std::uint8_t, RemoteVehicle> m_remoteStates;
    std::function<void(std::uint8_t, const VehicleSnapshot&, double)> m_stateObserver;
    std::optional<VehicleSnapshot> m_localState;
    std::uint32_t m_lastSentStateTime = 0;
    std::uint64_t m_lastSnapshotSent = 0;
    double m_lastPlayoutUpdate = -1.0;
    std::vector<AmbientStateMsg> m_ambientStates; // client: received, not yet taken
};

// Proof sent in Hello: SHA-256(nonce || password); all zeros when empty.
std::array<std::byte, 32> passwordProof(const std::array<std::byte, 16>& nonce, const std::string& password);

} // namespace mm2::net
