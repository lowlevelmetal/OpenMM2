#pragma once

// OpenMM2 multiplayer wire protocol (see docs/multiplayer.md).
//
// Every ENet packet is one message: a MsgType byte followed by the message
// body, bit-packed with BitStream. The ENet connect request carries
// kConnectData (magic + protocol version) so incompatible clients are turned
// away before any message is parsed.

#include "net/BitStream.h"
#include "net/Snapshot.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mm2::net {

inline constexpr std::uint16_t kProtocolMagic = 0x4D32; // "M2"
// 2: the shared ambient traffic of multiplayer cruise (AmbientState, the
// Ambient channel, SessionSettings::sharedTraffic).
// 3: the race start handshake (RaceLoad in place of Countdown, RaceLoaded,
// RaceStart; the race in Welcome).
inline constexpr std::uint16_t kProtocolVersion = 3;
inline constexpr std::uint32_t kConnectData = (std::uint32_t{kProtocolMagic} << 16) | kProtocolVersion;

// ENet channels.
enum class Channel : std::uint8_t {
    Control = 0, // reliable, ordered: handshake, lobby, chat, clock sync
    State = 1,   // unreliable, sequenced: vehicle snapshots
    Events = 2,  // reliable, ordered: in-game events
    Ambient = 3, // unreliable, sequenced: the shared ambient traffic (host -> client)
};
inline constexpr std::size_t kChannelCount = 4;

// Limits. The original game supported 8 players; the protocol allows more.
inline constexpr std::size_t kMaxPlayers = 16;
inline constexpr std::uint8_t kHostPlayerId = 0;
inline constexpr std::uint8_t kInvalidPlayerId = 0xFF;
inline constexpr std::uint8_t kBroadcastTarget = 0xFF;
inline constexpr std::size_t kMaxNameLength = 24;
inline constexpr std::size_t kMaxShortStringLength = 32; // car, city, build
inline constexpr std::size_t kMaxChatLength = 200;
inline constexpr std::size_t kMaxReasonLength = 200;
inline constexpr std::size_t kMaxExtraSettings = 32;
inline constexpr std::size_t kMaxExtraValueLength = 64;
inline constexpr std::size_t kMaxEventPayload = 1024;
inline constexpr const char* kDefaultCar = "vpbug"; // a player's car when none (or no valid one) was given

// --- Received text -----------------------------------------------------------------
//
// Everything below arrives from other machines and is shown in the UI, logged,
// or (the city) stored in the profile, so it is cleaned up on receipt.

// Valid UTF-8 without control characters (C0, DEL, C1), trimmed, at most
// `maxBytes` bytes cut on a character boundary. Invalid bytes are dropped.
std::string sanitizeText(std::string_view text, std::size_t maxBytes);

// Car and city names are base names of game files (tune/<car>.info,
// city/<city>.psdl): 1 to kMaxShortStringLength characters from [A-Za-z0-9_-],
// so a name from the network can never name a path.
bool isValidAssetName(std::string_view name);

enum class MsgType : std::uint8_t {
    Challenge = 1,
    Hello,
    Welcome,
    Reject,
    PlayerJoined,
    PlayerLeft,
    PlayerUpdate,
    PlayerRequest,
    Chat,
    Settings,
    RaceLoad, // was Countdown (protocol 2): the start time now follows the loading
    ReturnToLobby,
    Kick,
    TimeRequest,
    TimeResponse,
    VehicleState,
    WorldState,
    GameEvent,
    PlayerPings,
    AmbientState, // net/AmbientState.h
    RaceLoaded,
    RaceStart,
    Last = RaceStart,
};

// Sent as ENet disconnect data and in Reject/PlayerLeft messages.
enum class DisconnectReason : std::uint8_t {
    None,
    Left,            // player quit
    VersionMismatch, // incompatible protocol
    ServerFull,
    BadPassword,
    GameInProgress, // host does not allow joining a running game
    Kicked,
    HostShutdown,
    Timeout,     // no traffic from the peer
    JoinTimeout, // handshake not completed in time
    ProtocolError,
    Last = ProtocolError,
};
const char* describe(DisconnectReason reason);

// Game options. Values mirror the original's multiplayer setup screen; the
// game module decides which modes and races are valid for a city.
enum class GameMode : std::uint8_t { Cruise, Checkpoint, Circuit, Blitz, CopsAndRobbers, CrashCourse, Last = CrashCourse };
enum class TimeOfDay : std::uint8_t { Morning, Noon, Evening, Night, Last = Night };
enum class Weather : std::uint8_t { Clear, Cloudy, Rain, Snow, Last = Snow };
// Countdown: a race is loading and starting (from GO DRIVE until the start
// time the host sends once everyone has loaded).
enum class SessionPhase : std::uint8_t { Lobby, Countdown, InGame, Last = InGame };

struct SessionSettings {
    std::string name = "OpenMM2 session";
    std::string city = "london"; // city base name ("london", "sf")
    GameMode mode = GameMode::Cruise;
    std::uint16_t raceId = 0;
    std::uint8_t laps = 3;
    TimeOfDay timeOfDay = TimeOfDay::Noon;
    Weather weather = Weather::Clear;
    std::uint8_t trafficDensity = 50; // percent
    std::uint8_t pedDensity = 50;     // percent
    bool cops = true;
    std::uint8_t maxPlayers = 8;
    bool hasPassword = false; // the password itself never leaves the host
    bool allowJoinInProgress = false;
    // Multiplayer cruise: the host's ambient traffic and police are shared
    // with every player (OpenMM2 extra); false: MM2's network cruise, with
    // local pedestrians only.
    bool sharedTraffic = true;
    // Game-specific options that don't warrant a protocol change.
    std::vector<std::pair<std::string, std::string>> extra;

    bool operator==(const SessionSettings&) const = default;
};

struct PlayerInfo {
    std::uint8_t id = kInvalidPlayerId;
    std::string name;
    std::string car = "vpbug"; // vehicle base name (tune/<car>.info)
    std::uint8_t color = 0;    // paint index
    std::uint8_t team = 0;     // e.g. cops/robbers; meaning is up to the game mode
    bool ready = false;
    bool host = false;
    std::uint16_t ping = 0; // ms, as measured by the host

    bool operator==(const PlayerInfo&) const = default;
};

// --- Messages ------------------------------------------------------------------

struct ChallengeMsg {
    static constexpr MsgType kType = MsgType::Challenge;
    std::array<std::byte, 16> nonce{};
    bool passwordRequired = false;
};

struct HelloMsg {
    static constexpr MsgType kType = MsgType::Hello;
    std::uint16_t protocolVersion = kProtocolVersion;
    std::string build; // client version string, informational
    std::string name;
    std::string car;
    std::uint8_t color = 0;
    std::uint8_t team = 0;
    std::array<std::byte, 32> passwordProof{}; // SHA-256(nonce || password), zero if no password
};

struct WelcomeMsg {
    static constexpr MsgType kType = MsgType::Welcome;
    std::uint8_t yourId = kInvalidPlayerId;
    SessionSettings settings;
    std::vector<PlayerInfo> players;
    SessionPhase phase = SessionPhase::Lobby;
    std::uint32_t hostTime = 0;
    // The current race (RaceLoad's number and time; in the lobby the last).
    std::uint32_t race = 0;
    std::uint32_t raceOrderTime = 0;
    bool startKnown = false;      // the host has sent the race's start (RaceStart)
    std::uint32_t startTime = 0;  // valid when startKnown
    std::vector<std::uint8_t> loaded; // the players who have reported the race loaded
};

struct RejectMsg {
    static constexpr MsgType kType = MsgType::Reject;
    DisconnectReason reason = DisconnectReason::None;
    std::string message;
};

struct PlayerJoinedMsg {
    static constexpr MsgType kType = MsgType::PlayerJoined;
    PlayerInfo player;
};

struct PlayerLeftMsg {
    static constexpr MsgType kType = MsgType::PlayerLeft;
    std::uint8_t id = kInvalidPlayerId;
    DisconnectReason reason = DisconnectReason::Left;
};

struct PlayerUpdateMsg {
    static constexpr MsgType kType = MsgType::PlayerUpdate;
    PlayerInfo player;
};

// Client -> host: change own car/colour/team/ready state.
struct PlayerRequestMsg {
    static constexpr MsgType kType = MsgType::PlayerRequest;
    std::string car;
    std::uint8_t color = 0;
    std::uint8_t team = 0;
    bool ready = false;
};

struct ChatMsg {
    static constexpr MsgType kType = MsgType::Chat;
    std::uint8_t from = kInvalidPlayerId; // set by the host when relaying
    std::string text;
};

struct SettingsMsg {
    static constexpr MsgType kType = MsgType::Settings;
    SessionSettings settings;
};

// Host -> clients: GO DRIVE. Every machine loads race `race` (the host
// numbers its races from 1) and reports it loaded (RaceLoaded); the start
// time follows (RaceStart). `orderTime` is the session time of the order.
struct RaceLoadMsg {
    static constexpr MsgType kType = MsgType::RaceLoad;
    std::uint32_t race = 0;
    std::uint32_t orderTime = 0;
};

// Client -> host: this machine has loaded race `race` (mmGameMulti::
// SendRaceReady, 0x1f6). Host -> clients: player `player` has (the host sets
// it when relaying).
struct RaceLoadedMsg {
    static constexpr MsgType kType = MsgType::RaceLoaded;
    std::uint32_t race = 0;
    std::uint8_t player = kInvalidPlayerId;
};

// Host -> clients: race `race` starts (its countdown ends, the cars go) at
// session time `startTime` (MM2's start message, 0x20f, as a shared time).
struct RaceStartMsg {
    static constexpr MsgType kType = MsgType::RaceStart;
    std::uint32_t race = 0;
    std::uint32_t startTime = 0;
};

struct ReturnToLobbyMsg {
    static constexpr MsgType kType = MsgType::ReturnToLobby;
};

struct KickMsg {
    static constexpr MsgType kType = MsgType::Kick;
    std::string reason;
};

struct TimeRequestMsg {
    static constexpr MsgType kType = MsgType::TimeRequest;
    std::uint32_t clientTime = 0;
};

struct TimeResponseMsg {
    static constexpr MsgType kType = MsgType::TimeResponse;
    std::uint32_t clientTime = 0;
    std::uint32_t hostTime = 0;
};

// Client -> host: the sender's own vehicle.
struct VehicleStateMsg {
    static constexpr MsgType kType = MsgType::VehicleState;
    VehicleSnapshot state;
};

// Host -> clients: latest state of every other vehicle, one packet per tick.
struct WorldStateMsg {
    static constexpr MsgType kType = MsgType::WorldState;
    std::vector<std::pair<std::uint8_t, VehicleSnapshot>> vehicles;
};

struct GameEventMsg {
    static constexpr MsgType kType = MsgType::GameEvent;
    std::uint8_t from = kInvalidPlayerId;      // set by the host when relaying
    std::uint8_t target = kBroadcastTarget;    // player id, or broadcast
    std::uint16_t type = 0;                    // GameEventType or custom
    std::uint32_t time = 0;                    // session time of the event
    std::vector<std::byte> payload;
};

struct PlayerPingsMsg {
    static constexpr MsgType kType = MsgType::PlayerPings;
    std::vector<std::pair<std::uint8_t, std::uint16_t>> pings;
};

// --- Serialization ---------------------------------------------------------------

template <class S>
bool serialize(S& s, SessionSettings& m) {
    s.string(m.name, kMaxNameLength * 2);
    s.string(m.city, kMaxShortStringLength);
    s.enumeration(m.mode, GameMode::Last);
    s.u16(m.raceId);
    s.u8(m.laps);
    s.enumeration(m.timeOfDay, TimeOfDay::Last);
    s.enumeration(m.weather, Weather::Last);
    s.u8(m.trafficDensity);
    s.u8(m.pedDensity);
    s.boolean(m.cops);
    s.u8(m.maxPlayers);
    s.boolean(m.hasPassword);
    s.boolean(m.allowJoinInProgress);
    s.boolean(m.sharedTraffic);
    auto count = static_cast<std::uint32_t>(std::min(m.extra.size(), kMaxExtraSettings));
    s.varU32(count);
    if (count > kMaxExtraSettings)
        return s.fail();
    if constexpr (S::kReading)
        m.extra.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        s.string(m.extra[i].first, kMaxShortStringLength);
        s.string(m.extra[i].second, kMaxExtraValueLength);
    }
    if (m.maxPlayers == 0 || m.maxPlayers > kMaxPlayers)
        return s.fail();
    return s.ok();
}

template <class S>
bool serialize(S& s, PlayerInfo& m) {
    s.u8(m.id);
    s.string(m.name, kMaxNameLength);
    s.string(m.car, kMaxShortStringLength);
    s.u8(m.color);
    s.u8(m.team);
    s.boolean(m.ready);
    s.boolean(m.host);
    s.u16(m.ping);
    return s.ok();
}

template <class S>
bool serialize(S& s, ChallengeMsg& m) {
    for (auto& b : m.nonce) {
        auto v = std::to_integer<std::uint8_t>(b);
        s.u8(v);
        b = static_cast<std::byte>(v);
    }
    s.boolean(m.passwordRequired);
    return s.ok();
}

template <class S>
bool serialize(S& s, HelloMsg& m) {
    s.u16(m.protocolVersion);
    s.string(m.build, kMaxShortStringLength);
    s.string(m.name, kMaxNameLength);
    s.string(m.car, kMaxShortStringLength);
    s.u8(m.color);
    s.u8(m.team);
    for (auto& b : m.passwordProof) {
        auto v = std::to_integer<std::uint8_t>(b);
        s.u8(v);
        b = static_cast<std::byte>(v);
    }
    return s.ok();
}

template <class S>
bool serialize(S& s, WelcomeMsg& m) {
    s.u8(m.yourId);
    serialize(s, m.settings);
    auto count = static_cast<std::uint32_t>(m.players.size());
    s.varU32(count);
    if (count > kMaxPlayers)
        return s.fail();
    if constexpr (S::kReading)
        m.players.resize(count);
    for (auto& p : m.players)
        serialize(s, p);
    s.enumeration(m.phase, SessionPhase::Last);
    s.u32(m.hostTime);
    s.u32(m.race);
    s.u32(m.raceOrderTime);
    s.boolean(m.startKnown);
    s.u32(m.startTime);
    auto loaded = static_cast<std::uint32_t>(m.loaded.size());
    s.varU32(loaded);
    if (loaded > kMaxPlayers)
        return s.fail();
    if constexpr (S::kReading)
        m.loaded.resize(loaded);
    for (auto& id : m.loaded)
        s.u8(id);
    return s.ok();
}

template <class S>
bool serialize(S& s, RejectMsg& m) {
    s.enumeration(m.reason, DisconnectReason::Last);
    s.string(m.message, kMaxReasonLength);
    return s.ok();
}

template <class S>
bool serialize(S& s, PlayerJoinedMsg& m) {
    return serialize(s, m.player);
}

template <class S>
bool serialize(S& s, PlayerLeftMsg& m) {
    s.u8(m.id);
    s.enumeration(m.reason, DisconnectReason::Last);
    return s.ok();
}

template <class S>
bool serialize(S& s, PlayerUpdateMsg& m) {
    return serialize(s, m.player);
}

template <class S>
bool serialize(S& s, PlayerRequestMsg& m) {
    s.string(m.car, kMaxShortStringLength);
    s.u8(m.color);
    s.u8(m.team);
    s.boolean(m.ready);
    return s.ok();
}

template <class S>
bool serialize(S& s, ChatMsg& m) {
    s.u8(m.from);
    s.string(m.text, kMaxChatLength);
    return s.ok();
}

template <class S>
bool serialize(S& s, SettingsMsg& m) {
    return serialize(s, m.settings);
}

template <class S>
bool serialize(S& s, RaceLoadMsg& m) {
    s.u32(m.race);
    s.u32(m.orderTime);
    return s.ok();
}

template <class S>
bool serialize(S& s, RaceLoadedMsg& m) {
    s.u32(m.race);
    s.u8(m.player);
    return s.ok();
}

template <class S>
bool serialize(S& s, RaceStartMsg& m) {
    s.u32(m.race);
    s.u32(m.startTime);
    return s.ok();
}

template <class S>
bool serialize(S& s, ReturnToLobbyMsg&) {
    return s.ok();
}

template <class S>
bool serialize(S& s, KickMsg& m) {
    s.string(m.reason, kMaxReasonLength);
    return s.ok();
}

template <class S>
bool serialize(S& s, TimeRequestMsg& m) {
    s.u32(m.clientTime);
    return s.ok();
}

template <class S>
bool serialize(S& s, TimeResponseMsg& m) {
    s.u32(m.clientTime);
    s.u32(m.hostTime);
    return s.ok();
}

template <class S>
bool serialize(S& s, VehicleStateMsg& m) {
    return serialize(s, m.state);
}

template <class S>
bool serialize(S& s, WorldStateMsg& m) {
    auto count = static_cast<std::uint32_t>(m.vehicles.size());
    s.varU32(count);
    if (count > kMaxPlayers)
        return s.fail();
    if constexpr (S::kReading)
        m.vehicles.resize(count);
    for (auto& [id, state] : m.vehicles) {
        s.u8(id);
        serialize(s, state);
    }
    return s.ok();
}

template <class S>
bool serialize(S& s, GameEventMsg& m) {
    s.u8(m.from);
    s.u8(m.target);
    s.u16(m.type);
    s.u32(m.time);
    s.bytes(m.payload, kMaxEventPayload);
    return s.ok();
}

template <class S>
bool serialize(S& s, PlayerPingsMsg& m) {
    auto count = static_cast<std::uint32_t>(m.pings.size());
    s.varU32(count);
    if (count > kMaxPlayers)
        return s.fail();
    if constexpr (S::kReading)
        m.pings.resize(count);
    for (auto& [id, ping] : m.pings) {
        s.u8(id);
        s.u16(ping);
    }
    return s.ok();
}

// Encodes a message with its type byte.
template <class M>
std::vector<std::byte> encodeMessage(M msg) {
    WriteStream s;
    auto type = static_cast<std::uint8_t>(M::kType);
    s.u8(type);
    serialize(s, msg);
    return s.writer().take();
}

// Type of an encoded message, or nullopt if empty/unknown.
std::optional<MsgType> peekMessageType(std::span<const std::byte> packet);

// Decodes a packet produced by encodeMessage<M>. Fails on type mismatch,
// malformed content or trailing bytes.
template <class M>
bool decodeMessage(std::span<const std::byte> packet, M& out) {
    if (packet.empty() || std::to_integer<std::uint8_t>(packet[0]) != static_cast<std::uint8_t>(M::kType))
        return false;
    ReadStream s(packet.subspan(1));
    M msg{};
    if (!serialize(s, msg) || !s.ok() || s.reader().bitsRemaining() >= 8)
        return false;
    out = std::move(msg);
    return true;
}

// --- Game events ------------------------------------------------------------------

// Well-known in-game events. Payload layouts are defined below; the session
// layer only relays them, the game module interprets them.
enum class GameEventType : std::uint16_t {
    CheckpointReached = 1,
    LapCompleted = 2,
    RaceFinished = 3,
    GoldPickedUp = 4,  // cops & robbers
    GoldDropped = 5,
    GoldDelivered = 6,
    Collision = 7,
    Damage = 8,
    Wrecked = 9,
    LeftRace = 10, // the sender quit the race it was driving (it stays in the session); no payload
    Custom = 0x8000, // first id for game-specific events
};

struct CheckpointEvent {
    std::uint16_t index = 0;
    std::uint32_t raceTime = 0; // ms since race start
};
struct LapEvent {
    std::uint8_t lap = 0;
    std::uint32_t lapTime = 0;
};
struct FinishEvent {
    std::uint32_t raceTime = 0;
    std::uint8_t position = 0;
};
struct GoldEvent {
    Vec3 position;
    std::uint8_t team = 0;
};
struct CollisionEvent {
    std::uint8_t other = kInvalidPlayerId; // other player, or invalid for world/ambient
    Vec3 position;
    float impulse = 0.0f;
};
struct DamageEvent {
    float damage = 0.0f; // new damage level 0..1
    std::uint8_t source = kInvalidPlayerId;
};

template <class S>
bool serialize(S& s, CheckpointEvent& e) {
    s.u16(e.index);
    s.u32(e.raceTime);
    return s.ok();
}
template <class S>
bool serialize(S& s, LapEvent& e) {
    s.u8(e.lap);
    s.u32(e.lapTime);
    return s.ok();
}
template <class S>
bool serialize(S& s, FinishEvent& e) {
    s.u32(e.raceTime);
    s.u8(e.position);
    return s.ok();
}
template <class S>
bool serialize(S& s, GoldEvent& e) {
    s.vec3(e.position);
    s.u8(e.team);
    return s.ok();
}
template <class S>
bool serialize(S& s, CollisionEvent& e) {
    s.u8(e.other);
    s.vec3(e.position);
    s.f32(e.impulse);
    return s.ok();
}
template <class S>
bool serialize(S& s, DamageEvent& e) {
    s.f32(e.damage);
    s.u8(e.source);
    return s.ok();
}

// Packs/unpacks an event payload struct.
template <class E>
std::vector<std::byte> encodePayload(E e) {
    WriteStream s;
    serialize(s, e);
    return s.writer().take();
}
template <class E>
bool decodePayload(std::span<const std::byte> payload, E& out) {
    ReadStream s(payload);
    E e{};
    if (!serialize(s, e) || !s.ok())
        return false;
    out = e;
    return true;
}

} // namespace mm2::net
