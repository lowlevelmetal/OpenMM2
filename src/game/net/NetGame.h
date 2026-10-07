#pragma once

// Multiplayer for the game: one net::Session (lobby + race replication), the
// automatic port forwarding of the host (net::PortMapper) and the LAN session
// browser (net::LanScanner), translated to and from the game's own types.
//
// Everything runs on the game thread: call update() once per frame, from the
// menus and from the race screen (see docs/multiplayer.md for the exact
// per-frame contract of the race screen).

#include "core/Math.h"
#include "game/RaceConfig.h"
#include "net/Discovery.h"
#include "net/Protocol.h"
#include "net/Snapshot.h"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mm2::game {

// What the local player drives.
struct NetCar {
    std::string vehicle = "vpbug"; // VehicleInfo::baseName
    int color = 0;                 // paint job
    int team = 0;                  // Cops & Robbers: 0 cops / blue, 1 robbers / red
};

struct NetOptions {
    std::string playerName = "Player";
    std::uint16_t port = 0;              // game port when hosting, 0 = net::kDefaultGamePort
    bool portMapping = true;             // forward the port on the router (UPnP, PCP, NAT-PMP)
    std::filesystem::path portMapState;  // crash-safe cleanup record of the mapping
    std::uint16_t discoveryPort = net::kDefaultDiscoveryPort; // LAN browser port (tests use others)
};

struct NetHostOptions {
    std::string sessionName;  // shown in session lists; empty = "<player>'s game"
    std::string password;     // empty = open session
    int maxPlayers = 8;       // the original allowed up to 8
    bool advertiseOnLan = true;
};

// A line of the lobby/in-race chat window.
struct NetChatLine {
    std::uint8_t from = net::kInvalidPlayerId; // invalid for system notices
    std::string name;
    std::string text;
    bool system = false; // "has joined", "has left", "You are now the Host" ...
};

// Another player's car this frame, interpolated ~100 ms in the past.
struct NetRemoteCar {
    std::uint8_t id = net::kInvalidPlayerId;
    std::string name;
    NetCar car;
    Mat34 transform; // model space -> world (car model origin, Angel conventions)
    Vec3 velocity;
    Vec3 angularVelocity;
    net::VehicleControls controls;
    float damage = 0.0f;     // 0..1
    std::uint8_t flags = 0;  // net::VehicleFlags
    bool hasState = false;   // false until the first snapshot arrived
    bool stale = false;      // extrapolated beyond the buffer or held still
};

// A game event received from another player.
struct NetGameEvent {
    std::uint8_t from = net::kInvalidPlayerId;
    net::GameEventType type{};
    std::uint32_t time = 0; // session time (ms)
    std::vector<std::byte> payload;

    // Decodes the payload (net::CheckpointEvent, LapEvent, FinishEvent, ...).
    template <class E>
    std::optional<E> as() const {
        E e{};
        if (!net::decodePayload(payload, e))
            return std::nullopt;
        return e;
    }
};

// Conversions between the game's race description and the session settings
// that travel over the network. Options the protocol has no field for
// (fog, cop density, difficulty, Cops & Robbers variant and limits, gold
// mass) are carried in SessionSettings::extra.
net::SessionSettings toSessionSettings(const RaceConfig& config, const std::string& name, int maxPlayers);
RaceConfig fromSessionSettings(const net::SessionSettings& settings);

// Cops & Robbers gold weight choices (string table 332-335): Weightless,
// Quarter Ton, Half Ton (index 0..2).
inline constexpr int kGoldMassChoices = 3;

class NetGame {
public:
    enum class Phase {
        Idle,       // no session
        Connecting, // join in progress
        Lobby,
        Countdown,  // race starting: load the race now; it starts at raceStartTime()
        Racing,
        Closed,     // ended (see takeNotice())
    };

    explicit NetGame(NetOptions options);
    ~NetGame();
    NetGame(const NetGame&) = delete;
    NetGame& operator=(const NetGame&) = delete;

    // --- Session lifetime ----------------------------------------------------------
    bool host(const RaceConfig& config, const NetHostOptions& host, const NetCar& car, std::string* error = nullptr);
    // `address`: "a.b.c.d", "a.b.c.d:port" or a host name (blocking DNS lookup).
    bool join(const std::string& address, const std::string& password, const NetCar& car,
              std::string* error = nullptr);
    bool join(const net::Address& address, const std::string& password, const NetCar& car,
              std::string* error = nullptr);
    // Leaves the session (as host: ends it for everyone). Stops port forwarding.
    void leave();
    // Services the network. Call every frame while a NetGame exists.
    void update();

    Phase phase() const;
    bool inSession() const; // Connecting, Lobby, Countdown or Racing
    bool isHost() const;
    // Notices for the user ("The Host has quit", "has been ejected", join errors).
    std::optional<std::string> takeNotice();

    // --- LAN browser ------------------------------------------------------------------
    bool startLanScan(std::string* error = nullptr);
    void stopLanScan();
    bool scanning() const;
    // Also queries this address (e.g. a typed-in host) besides the LAN broadcast.
    void addScanTarget(const net::Address& address);
    std::vector<net::DiscoveredSession> lanSessions() const;

    // --- Lobby ------------------------------------------------------------------------
    std::uint8_t localId() const;
    const std::vector<net::PlayerInfo>& players() const;
    const net::PlayerInfo* player(std::uint8_t id) const;
    const net::SessionSettings& settings() const;
    // The race everyone will drive, with the local player's car filled in.
    RaceConfig raceConfig() const;
    NetCar localCar() const { return m_car; }
    int maxPlayers() const;
    bool hasPassword() const;
    int goldMass() const; // 0..kGoldMassChoices-1
    const std::deque<NetChatLine>& chat() const { return m_chat; }
    std::uint16_t pingMs(std::uint8_t playerId) const;

    void setLocalCar(const NetCar& car);
    void setReady(bool ready);
    bool localReady() const;
    void sendChat(const std::string& text);

    // Host only.
    void setRaceConfig(const RaceConfig& config);
    void setMaxPlayers(int maxPlayers);
    void setPassword(const std::string& password);
    void setGoldMass(int index);
    void kick(std::uint8_t playerId);
    // Everyone except the host has pressed READY (and there is at least one
    // other player, unless `allowAlone`).
    bool everyoneReady(bool allowAlone = true) const;
    // Starts the countdown: every machine loads the race and starts it at
    // raceStartTime(). `delayMs` must cover loading the city.
    void startRace(std::uint32_t delayMs = 6000);
    // Ends the race for everyone and returns to the lobby.
    void returnToLobby();

    // Port forwarding status for the host's lobby, one line. Empty when not hosting.
    std::string portMappingStatus() const;

    // --- Race -------------------------------------------------------------------------
    // True once when a countdown starts (the frontend switches to the race).
    bool takeRaceStart();
    // True once when the host returned everyone to the lobby.
    bool takeReturnToLobby();
    // Session clock (ms; the host's clock, estimated on clients).
    std::uint32_t sessionTime() const;
    std::uint32_t raceStartTime() const;
    // Seconds until the race starts (negative once it has started).
    double secondsToStart() const;
    bool raceStarted() const;

    // The local car's state; sent at the session's snapshot rate.
    void submitLocalState(const Mat34& transform, const Vec3& velocity, const Vec3& angularVelocity,
                          const net::VehicleControls& controls, float damage, std::uint8_t flags);
    // Every other player in the session, sampled for this frame.
    std::vector<NetRemoteCar> remoteCars() const;

    // Game events (reliable, ordered). Race time is ms since the race start.
    void sendCheckpoint(int index, std::uint32_t raceTimeMs);
    void sendLap(int lap, std::uint32_t lapTimeMs);
    void sendFinish(std::uint32_t raceTimeMs, int position);
    void sendGold(net::GameEventType type, const Vec3& position, int team); // GoldPickedUp/Dropped/Delivered
    void sendCollision(std::uint8_t otherPlayer, const Vec3& position, float impulse);
    void sendDamage(float damage, std::uint8_t source);
    void sendEvent(std::uint16_t type, std::vector<std::byte> payload,
                   std::uint8_t target = net::kBroadcastTarget);
    std::vector<NetGameEvent> takeGameEvents();

private:
    struct Impl;
    void handleEvents();
    void addSystemLine(std::string text);
    std::string playerName(std::uint8_t id) const;
    void startPortMapping();
    void stopPortMapping();

    NetOptions m_options;
    NetCar m_car;
    std::unique_ptr<Impl> m_impl;
    std::deque<NetChatLine> m_chat;
    std::deque<std::string> m_notices;
    std::vector<NetGameEvent> m_gameEvents;
    bool m_raceStartPending = false;
    bool m_returnPending = false;
    bool m_raceStarted = false;
    bool m_closed = false;
};

} // namespace mm2::game
