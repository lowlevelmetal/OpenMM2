#include "game/net/NetGame.h"

#include "core/Log.h"
#include "game/Catalog.h"
#include "core/StringUtil.h"
#include "net/PortMapper.h"
#include "net/Session.h"

#include <algorithm>
#include <format>
#include <thread>

namespace mm2::game {
namespace {

constexpr std::size_t kMaxChatLines = 64;
constexpr std::uint64_t kScanIntervalMs = 1000;

// The protocol's mode enum predates the game's; map explicitly.
net::GameMode toNetMode(GameMode m) {
    switch (m) {
    case GameMode::Cruise: return net::GameMode::Cruise;
    case GameMode::Blitz: return net::GameMode::Blitz;
    case GameMode::Checkpoint: return net::GameMode::Checkpoint;
    case GameMode::Circuit: return net::GameMode::Circuit;
    case GameMode::CrashCourse: return net::GameMode::CrashCourse;
    case GameMode::CopsAndRobbers: return net::GameMode::CopsAndRobbers;
    }
    return net::GameMode::Cruise;
}

GameMode fromNetMode(net::GameMode m) {
    switch (m) {
    case net::GameMode::Cruise: return GameMode::Cruise;
    case net::GameMode::Blitz: return GameMode::Blitz;
    case net::GameMode::Checkpoint: return GameMode::Checkpoint;
    case net::GameMode::Circuit: return GameMode::Circuit;
    case net::GameMode::CrashCourse: return GameMode::CrashCourse;
    case net::GameMode::CopsAndRobbers: return GameMode::CopsAndRobbers;
    }
    return GameMode::Cruise;
}

std::uint8_t percent(float v) { return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 1.0f) * 100.0f + 0.5f); }

const std::string* extra(const net::SessionSettings& s, std::string_view key) {
    for (const auto& [k, v] : s.extra)
        if (k == key)
            return &v;
    return nullptr;
}

int extraInt(const net::SessionSettings& s, std::string_view key, int fallback) {
    const std::string* v = extra(s, key);
    if (!v)
        return fallback;
    return static_cast<int>(str::parseInt(*v).value_or(fallback));
}

void setExtra(net::SessionSettings& s, const std::string& key, std::string value) {
    for (auto& [k, v] : s.extra) {
        if (k == key) {
            v = std::move(value);
            return;
        }
    }
    s.extra.emplace_back(key, std::move(value));
}

std::string reasonText(net::DisconnectReason r, const std::string& message) {
    switch (r) {
    case net::DisconnectReason::HostShutdown: return "The Host has quit"; // string 98
    case net::DisconnectReason::Kicked: return "You have been ejected"; // string 44
    case net::DisconnectReason::VersionMismatch: return "ERROR: Network versions do not match."; // string 83
    case net::DisconnectReason::BadPassword: return "Enter a valid password"; // menu.csv dialog title
    case net::DisconnectReason::ServerFull: return "The session is full.";
    case net::DisconnectReason::GameInProgress: return "The race has already started.";
    default: break;
    }
    if (!message.empty())
        return message;
    return std::format("**You have been disconnected** ({})", net::describe(r)); // string 72
}

} // namespace

// --- Settings translation ------------------------------------------------------------------

net::SessionSettings toSessionSettings(const RaceConfig& c, const std::string& name, int maxPlayers) {
    net::SessionSettings s;
    s.name = name.substr(0, net::kMaxNameLength * 2);
    s.city = c.city;
    s.mode = toNetMode(c.mode);
    s.raceId = c.raceIndex < 0 ? 0xFFFF : static_cast<std::uint16_t>(c.raceIndex);
    s.laps = static_cast<std::uint8_t>(std::clamp(c.laps, 0, 255));
    s.timeOfDay = static_cast<net::TimeOfDay>(std::clamp(static_cast<int>(c.timeOfDay), 0, 3));
    switch (c.weather) {
    case Weather::Clear: s.weather = net::Weather::Clear; break;
    case Weather::Cloudy:
    case Weather::Fog: s.weather = net::Weather::Cloudy; break;
    case Weather::Rain: s.weather = net::Weather::Rain; break;
    case Weather::Snow: s.weather = net::Weather::Snow; break;
    }
    s.trafficDensity = percent(c.trafficDensity);
    s.pedDensity = percent(c.pedestrianDensity);
    s.cops = c.copDensity > 0.0f;
    s.maxPlayers = static_cast<std::uint8_t>(std::clamp(maxPlayers, 2, static_cast<int>(net::kMaxPlayers)));
    // Options without a protocol field.
    setExtra(s, "weather", std::to_string(static_cast<int>(c.weather)));
    setExtra(s, "copDensity", std::to_string(percent(c.copDensity)));
    setExtra(s, "difficulty", c.difficulty == Difficulty::Professional ? "pro" : "amateur");
    setExtra(s, "opponents", std::to_string(c.opponents));
    setExtra(s, "crMode", std::to_string(static_cast<int>(c.copsAndRobbers)));
    setExtra(s, "timeLimit", std::to_string(static_cast<int>(c.timeLimitMinutes)));
    setExtra(s, "pointLimit", std::to_string(c.pointLimit));
    return s;
}

RaceConfig fromSessionSettings(const net::SessionSettings& s) {
    RaceConfig c;
    c.multiplayer = true;
    c.city = s.city;
    c.mode = fromNetMode(s.mode);
    c.raceIndex = s.raceId == 0xFFFF ? -1 : s.raceId;
    if (c.mode == GameMode::Cruise || c.mode == GameMode::CopsAndRobbers)
        c.raceIndex = -1;
    c.laps = s.laps;
    c.timeOfDay = static_cast<TimeOfDay>(std::clamp(static_cast<int>(s.timeOfDay), 0, 3));
    switch (s.weather) {
    case net::Weather::Clear: c.weather = Weather::Clear; break;
    case net::Weather::Cloudy: c.weather = Weather::Cloudy; break;
    case net::Weather::Rain: c.weather = Weather::Rain; break;
    case net::Weather::Snow: c.weather = Weather::Snow; break;
    }
    c.weather = static_cast<Weather>(std::clamp(extraInt(s, "weather", static_cast<int>(c.weather)), 0, 4));
    // Percent on the wire; the protocol's byte would allow 255.
    c.trafficDensity = std::min<int>(s.trafficDensity, 100) / 100.0f;
    c.pedestrianDensity = std::min<int>(s.pedDensity, 100) / 100.0f;
    c.copDensity = std::clamp(extraInt(s, "copDensity", s.cops ? 50 : 0), 0, 100) / 100.0f;
    if (const std::string* d = extra(s, "difficulty"))
        c.difficulty = *d == "pro" ? Difficulty::Professional : Difficulty::Amateur;
    c.opponents = std::clamp(extraInt(s, "opponents", 0), 0, 7);
    c.copsAndRobbers = static_cast<CopsAndRobbersMode>(std::clamp(extraInt(s, "crMode", 0), 0, 2));
    c.timeLimitMinutes = static_cast<float>(std::max(0, extraInt(s, "timeLimit", 0)));
    c.pointLimit = std::max(0, extraInt(s, "pointLimit", 0));
    return c;
}

// --- NetGame ------------------------------------------------------------------------------

struct NetGame::Impl {
    std::unique_ptr<net::Session> session;
    std::unique_ptr<net::PortMapper> mapper;
    std::vector<std::thread> stoppers; // finish removing old port mappings in the background
    std::unique_ptr<net::LanScanner> scanner;
    std::uint64_t lastScan = 0;
    bool mappingWanted = false;
    // Host-side copies of what the settings were built from.
    RaceConfig hostConfig;
    std::string sessionName;
    int maxPlayers = 8;
    int goldMass = 0; // HostRaceMenu's GOLD MASS index starts at 0, Weightless
    bool joining = false;
};

NetGame::NetGame(NetOptions options) : m_options(std::move(options)), m_impl(std::make_unique<Impl>()) {}

NetGame::~NetGame() {
    leave();
    stopLanScan();
    for (auto& t : m_impl->stoppers)
        if (t.joinable())
            t.join();
}

bool NetGame::host(const RaceConfig& config, const NetHostOptions& hostOptions, const NetCar& car,
                   std::string* error) {
    leave();
    m_closed = false;
    m_car = car;
    auto& impl = *m_impl;
    impl.hostConfig = config;
    impl.hostConfig.multiplayer = true;
    impl.maxPlayers = std::clamp(hostOptions.maxPlayers, 2, static_cast<int>(net::kMaxPlayers));
    impl.sessionName = hostOptions.sessionName.empty() ? std::format("{}'s game", m_options.playerName)
                                                       : hostOptions.sessionName;

    net::HostParams params;
    params.settings = toSessionSettings(impl.hostConfig, impl.sessionName, impl.maxPlayers);
    setExtra(params.settings, "goldMass", std::to_string(impl.goldMass));
    params.player = {m_options.playerName, car.vehicle, static_cast<std::uint8_t>(car.color),
                     static_cast<std::uint8_t>(car.team)};
    params.password = hostOptions.password;
    params.bind = net::Address::any(m_options.port ? m_options.port : net::kDefaultGamePort);
    params.advertiseOnLan = hostOptions.advertiseOnLan;
    params.discoveryPort = m_options.discoveryPort;

    impl.session = std::make_unique<net::Session>();
    if (!impl.session->host(params, error)) {
        impl.session.reset();
        return false;
    }
    log::info("netgame: hosting '{}' on port {}", impl.sessionName, impl.session->port());
    m_chat.clear();
    m_notices.clear(); // a new session: nothing left over from the last
    addSystemLine("**You are now the host**"); // string 70
    startPortMapping();
    return true;
}

bool NetGame::join(const std::string& address, const std::string& password, const NetCar& car,
                   std::string* error) {
    const auto addr = net::Address::resolve(str::trim(address), net::kDefaultGamePort);
    if (!addr) {
        if (error)
            *error = std::format("Cannot find '{}'", address);
        return false;
    }
    return join(*addr, password, car, error);
}

bool NetGame::join(const net::Address& address, const std::string& password, const NetCar& car,
                   std::string* error) {
    leave();
    m_closed = false;
    m_joinFailure = net::DisconnectReason::None;
    m_car = car;
    net::JoinParams params;
    params.host = address;
    params.password = password;
    params.player = {m_options.playerName, car.vehicle, static_cast<std::uint8_t>(car.color),
                     static_cast<std::uint8_t>(car.team)};
    m_impl->session = std::make_unique<net::Session>();
    if (!m_impl->session->join(params, error)) {
        m_impl->session.reset();
        return false;
    }
    m_impl->joining = true;
    m_chat.clear();
    m_notices.clear(); // a new session: nothing left over from the last
    log::info("netgame: joining {}", address.toString());
    return true;
}

void NetGame::leave() {
    stopPortMapping();
    if (m_impl->session) {
        m_impl->session->leave();
        m_impl->session->update(); // flush the disconnect
        m_impl->session.reset();
    }
    m_impl->joining = false;
    m_raceStartPending = m_raceStarted = false;
    m_raceNumber = m_lobbyAfterRace = 0;
    m_gameEvents.clear();
}

void NetGame::update() {
    auto& impl = *m_impl;
    if (impl.session) {
        impl.session->update();
        handleEvents();
    }
    if (impl.scanner) {
        impl.scanner->update();
        const std::uint64_t now = net::monotonicMs();
        if (now - impl.lastScan >= kScanIntervalMs) {
            impl.scanner->scan();
            impl.lastScan = now;
        }
    }
}

void NetGame::handleEvents() {
    auto& s = *m_impl->session;
    for (auto& e : s.takeEvents()) {
        std::visit(
            [&](auto& ev) {
                using T = std::decay_t<decltype(ev)>;
                if constexpr (std::is_same_v<T, net::ev::JoinAccepted>) {
                    m_impl->joining = false;
                    log::debug("netgame: joined as player {}", ev.localId);
                } else if constexpr (std::is_same_v<T, net::ev::JoinFailed>) {
                    m_impl->joining = false;
                    m_closed = true;
                    m_joinFailure = ev.reason;
                    m_notices.push_back(reasonText(ev.reason, ev.message));
                } else if constexpr (std::is_same_v<T, net::ev::Disconnected>) {
                    m_closed = true;
                    m_notices.push_back(reasonText(ev.reason, ev.message));
                } else if constexpr (std::is_same_v<T, net::ev::PlayerJoined>) {
                    addSystemLine(std::format("{} has joined", ev.player.name)); // string 68
                } else if constexpr (std::is_same_v<T, net::ev::PlayerLeft>) {
                    addSystemLine(std::format("{} {}", ev.player.name,
                                              ev.reason == net::DisconnectReason::Kicked ? "has been ejected"
                                                                                         : "has left")); // string 69
                } else if constexpr (std::is_same_v<T, net::ev::Chat>) {
                    addChatLine({ev.from, playerName(ev.from), ev.text, false});
                } else if constexpr (std::is_same_v<T, net::ev::CountdownStarted>) {
                    ++m_raceNumber;
                    log::info("netgame: race {} starts at session time {}", m_raceNumber, ev.startTime);
                    m_raceStartPending = true;
                    m_raceStarted = false;
                    // Whatever is still queued belongs to an earlier race
                    // (late events, or a cruise that reads none).
                    m_gameEvents.clear();
                } else if constexpr (std::is_same_v<T, net::ev::GameStarted>) {
                    m_raceStarted = true;
                } else if constexpr (std::is_same_v<T, net::ev::ReturnedToLobby>) {
                    log::info("netgame: back to the lobby after race {}", m_raceNumber);
                    m_lobbyAfterRace = m_raceNumber;
                    m_raceStartPending = false;
                    m_raceStarted = false;
                    addSystemLine("**Session returning to Lobby**"); // string 73
                } else if constexpr (std::is_same_v<T, net::ev::GameEvent>) {
                    m_gameEvents.push_back(
                        {ev.from, static_cast<net::GameEventType>(ev.type), ev.time, std::move(ev.payload)});
                } else {
                    // PlayerUpdated, SettingsChanged: the UI reads the current state.
                }
            },
            e);
    }
}

NetGame::Phase NetGame::phase() const {
    const auto& s = m_impl->session;
    if (m_closed)
        return Phase::Closed;
    if (!s)
        return Phase::Idle;
    switch (s->state()) {
    case net::Session::State::Idle: return Phase::Idle;
    case net::Session::State::Connecting:
    case net::Session::State::Joining: return Phase::Connecting;
    case net::Session::State::Closed: return Phase::Closed;
    case net::Session::State::Active: break;
    }
    switch (s->phase()) {
    case net::SessionPhase::Lobby: return Phase::Lobby;
    case net::SessionPhase::Countdown: return Phase::Countdown;
    case net::SessionPhase::InGame: return Phase::Racing;
    }
    return Phase::Lobby;
}

bool NetGame::inSession() const {
    const Phase p = phase();
    return p == Phase::Connecting || p == Phase::Lobby || p == Phase::Countdown || p == Phase::Racing;
}

bool NetGame::isHost() const { return m_impl->session && m_impl->session->isHost(); }

std::optional<std::string> NetGame::takeNotice() {
    if (m_notices.empty())
        return std::nullopt;
    std::string n = std::move(m_notices.front());
    m_notices.pop_front();
    return n;
}

// --- LAN browser -----------------------------------------------------------------------------

bool NetGame::startLanScan(std::string* error) {
    if (m_impl->scanner)
        return true;
    auto scanner = std::make_unique<net::LanScanner>();
    if (!scanner->start(m_options.discoveryPort, false, error))
        return false;
    // Hosts on this machine answer even when a firewall drops LAN broadcasts.
    scanner->addTarget(net::Address::loopback(m_options.discoveryPort));
    scanner->scan();
    m_impl->scanner = std::move(scanner);
    m_impl->lastScan = net::monotonicMs();
    return true;
}

void NetGame::stopLanScan() { m_impl->scanner.reset(); }

bool NetGame::scanning() const { return m_impl->scanner != nullptr; }

void NetGame::addScanTarget(const net::Address& address) {
    if (m_impl->scanner) {
        net::Address a = address;
        a.port = m_options.discoveryPort;
        m_impl->scanner->addTarget(a);
        m_impl->scanner->scan();
    }
}

std::vector<net::DiscoveredSession> NetGame::lanSessions() const {
    if (!m_impl->scanner)
        return {};
    auto list = m_impl->scanner->sessions();
    std::ranges::sort(list, [](const auto& a, const auto& b) { return a.advert.sessionName < b.advert.sessionName; });
    return list;
}

// --- Lobby -------------------------------------------------------------------------------

std::uint8_t NetGame::localId() const {
    return m_impl->session ? m_impl->session->localId() : net::kInvalidPlayerId;
}

const std::vector<net::PlayerInfo>& NetGame::players() const {
    static const std::vector<net::PlayerInfo> none;
    return m_impl->session ? m_impl->session->players() : none;
}

const net::PlayerInfo* NetGame::player(std::uint8_t id) const {
    return m_impl->session ? m_impl->session->player(id) : nullptr;
}

const net::SessionSettings& NetGame::settings() const {
    static const net::SessionSettings none;
    return m_impl->session ? m_impl->session->settings() : none;
}

int freeForAllTeam(const VehicleInfo* car) { return car && (car->flags & VehicleInfo::kFlagCop) ? 0 : 1; }

std::string netVehicle(const Catalog& catalog, const std::string& name) {
    if (catalog.vehicle(name) || !catalog.vehicle(kDefaultVehicle))
        return name;
    return kDefaultVehicle;
}

NetCar raceCar(const RaceConfig& race, NetCar lobby) {
    // "The Cop Team in Mustang Cruisers takes the gold to the bank, while the
    // Robber Team in Mustang GTs takes it back to the hideout" (help picture
    // host_cvr.jpg): Cops vs. Robbers fixes the cars by team. Robber Teams lets
    // everyone choose (host_rt.jpg).
    if (race.mode == GameMode::CopsAndRobbers && race.copsAndRobbers == CopsAndRobbersMode::CopsVsRobbers) {
        lobby.vehicle = lobby.team == 0 ? "vpcop" : "vpmustang99";
        lobby.color = 0;
    }
    return lobby;
}

RaceConfig NetGame::raceConfig() const {
    RaceConfig c = fromSessionSettings(settings());
    const NetCar car = raceCar(c, m_car);
    c.vehicle = car.vehicle;
    c.vehicleColor = car.color;
    c.automatic = car.automatic;
    return c;
}

NetCar NetGame::playerCar(std::uint8_t playerId) const {
    const RaceConfig race = fromSessionSettings(settings());
    if (playerId == localId())
        return raceCar(race, m_car);
    const net::PlayerInfo* p = player(playerId);
    if (!p)
        return {};
    return raceCar(race, {p->car, p->color, p->team});
}

int NetGame::maxPlayers() const { return settings().maxPlayers; }

bool NetGame::hasPassword() const { return settings().hasPassword; }

int NetGame::goldMass() const { return std::clamp(extraInt(settings(), "goldMass", 0), 0, kGoldMassChoices - 1); }

std::uint16_t NetGame::pingMs(std::uint8_t playerId) const {
    const net::PlayerInfo* p = player(playerId);
    return p ? p->ping : 0;
}

void NetGame::setLocalCar(const NetCar& car) {
    // The transmission stays here; the rest goes to the session when it
    // changes.
    const bool sent = car.vehicle != m_car.vehicle || car.color != m_car.color || car.team != m_car.team;
    m_car = car;
    if (sent && m_impl->session)
        m_impl->session->setLocalPlayer(car.vehicle, static_cast<std::uint8_t>(car.color),
                                        static_cast<std::uint8_t>(car.team));
}

void NetGame::setReady(bool ready) {
    if (m_impl->session)
        m_impl->session->setReady(ready);
}

bool NetGame::localReady() const {
    const net::PlayerInfo* p = player(localId());
    return p && p->ready;
}

void NetGame::sendChat(const std::string& text) {
    const auto trimmed = str::trim(text);
    if (trimmed.empty() || !m_impl->session)
        return;
    const std::string t(trimmed.substr(0, net::kMaxChatLength));
    // The session reports our own line back as a Chat event (the host echoes it).
    m_impl->session->sendChat(t);
}

void NetGame::setRaceConfig(const RaceConfig& config) {
    if (!isHost())
        return;
    m_impl->hostConfig = config;
    m_impl->hostConfig.multiplayer = true;
    auto s = toSessionSettings(m_impl->hostConfig, m_impl->sessionName, m_impl->maxPlayers);
    setExtra(s, "goldMass", std::to_string(m_impl->goldMass));
    m_impl->session->updateSettings(s); // keeps hasPassword from the password set
}

void NetGame::setMaxPlayers(int maxPlayers) {
    m_impl->maxPlayers = std::clamp(maxPlayers, 2, static_cast<int>(net::kMaxPlayers));
    setRaceConfig(m_impl->hostConfig);
}

void NetGame::setPassword(const std::string& password) {
    if (isHost())
        m_impl->session->setPassword(password);
}

void NetGame::setGoldMass(int index) {
    m_impl->goldMass = std::clamp(index, 0, kGoldMassChoices - 1);
    setRaceConfig(m_impl->hostConfig);
}

// mmInterface::BootPlayerCB: the host only, never itself.
void NetGame::kick(std::uint8_t playerId) {
    if (isHost() && playerId != localId())
        m_impl->session->kick(playerId, "You have been ejected");
}

// mmInterface::MultiAllReady over the roster's ready flags
// (NetArena::GetStatus).
bool NetGame::everyoneReady(bool allowAlone) const {
    int others = 0;
    for (const auto& p : players()) {
        if (p.id == localId())
            continue;
        ++others;
        if (!p.ready)
            return false;
    }
    return others > 0 || allowAlone;
}

void NetGame::startRace(std::uint32_t delayMs) {
    if (isHost() && phase() == Phase::Lobby)
        m_impl->session->startCountdown(delayMs);
}

void NetGame::returnToLobby() {
    if (isHost())
        m_impl->session->returnToLobby();
}

// --- Port forwarding ---------------------------------------------------------------------

void NetGame::startPortMapping() {
    m_impl->mappingWanted = m_options.portMapping;
    if (!m_options.portMapping || !m_impl->session)
        return;
    net::PortMapperConfig cfg;
    cfg.internalPort = m_impl->session->port();
    cfg.description = "OpenMM2";
    cfg.stateFile = m_options.portMapState;
    m_impl->mapper = std::make_unique<net::PortMapper>();
    m_impl->mapper->start(cfg);
}

void NetGame::stopPortMapping() {
    if (!m_impl->mapper)
        return;
    // Removing the mapping waits for the router; don't stall the frame.
    m_impl->stoppers.emplace_back([mapper = std::move(m_impl->mapper)]() mutable {
        mapper->stop();
        mapper.reset();
    });
}

std::string NetGame::portMappingStatus() const {
    if (!isHost())
        return {};
    if (!m_impl->mappingWanted)
        return std::format("Port forwarding off: players outside your network need UDP port {} open.",
                           m_impl->session->port());
    if (!m_impl->mapper)
        return {};
    return m_impl->mapper->status().message;
}

// --- Race --------------------------------------------------------------------------------

bool NetGame::takeRaceStart() { return std::exchange(m_raceStartPending, false); }

std::uint32_t NetGame::sessionTime() const { return m_impl->session ? m_impl->session->time() : 0; }

std::uint32_t NetGame::raceStartTime() const { return m_impl->session ? m_impl->session->countdownEnd() : 0; }

double NetGame::secondsToStart() const {
    if (!m_impl->session)
        return 0.0;
    return (static_cast<double>(raceStartTime()) - static_cast<double>(sessionTime())) / 1000.0;
}

bool NetGame::raceStarted() const { return m_raceStarted; }

void NetGame::submitLocalState(const Mat34& transform, const Vec3& velocity, const Vec3& angularVelocity,
                               const net::VehicleControls& controls, float damage, std::uint8_t flags) {
    if (!m_impl->session)
        return;
    net::VehicleSnapshot s;
    s.time = m_impl->session->time();
    s.position = transform.m3;
    s.orientation = Quat::fromMatrix(transform);
    s.linearVelocity = velocity;
    s.angularVelocity = angularVelocity;
    s.controls = controls;
    s.damage = std::clamp(damage, 0.0f, 1.0f);
    s.flags = flags;
    m_impl->session->submitLocalState(s);
}

std::vector<NetRemoteCar> NetGame::remoteCars() const {
    std::vector<NetRemoteCar> out;
    if (!m_impl->session)
        return out;
    const RaceConfig race = fromSessionSettings(settings());
    for (const auto& p : players()) {
        if (p.id == localId())
            continue;
        NetRemoteCar car;
        car.id = p.id;
        car.name = p.name;
        car.car = raceCar(race, {p.car, p.color, p.team});
        net::VehicleSnapshot snap;
        const auto r = m_impl->session->sampleRemote(p.id, snap);
        if (r != net::SnapshotBuffer::Result::Empty) {
            car.hasState = true;
            car.stale = r != net::SnapshotBuffer::Result::Interpolated;
            car.transform = snap.orientation.toMatrix(snap.position);
            car.velocity = snap.linearVelocity;
            car.angularVelocity = snap.angularVelocity;
            car.controls = snap.controls;
            car.damage = snap.damage;
            car.flags = snap.flags;
        }
        out.push_back(std::move(car));
    }
    return out;
}

void NetGame::sendEvent(std::uint16_t type, std::vector<std::byte> payload, std::uint8_t target) {
    if (m_impl->session)
        m_impl->session->sendGameEvent(type, std::move(payload), target);
}

void NetGame::sendCheckpoint(int index, std::uint32_t raceTimeMs) {
    sendEvent(static_cast<std::uint16_t>(net::GameEventType::CheckpointReached),
              net::encodePayload(net::CheckpointEvent{static_cast<std::uint16_t>(index), raceTimeMs}));
}

void NetGame::sendLap(int lap, std::uint32_t lapTimeMs) {
    sendEvent(static_cast<std::uint16_t>(net::GameEventType::LapCompleted),
              net::encodePayload(net::LapEvent{static_cast<std::uint8_t>(lap), lapTimeMs}));
}

void NetGame::sendFinish(std::uint32_t raceTimeMs, int position) {
    sendEvent(static_cast<std::uint16_t>(net::GameEventType::RaceFinished),
              net::encodePayload(net::FinishEvent{raceTimeMs, static_cast<std::uint8_t>(position)}));
}

void NetGame::sendGold(net::GameEventType type, const Vec3& position, int team) {
    sendEvent(static_cast<std::uint16_t>(type),
              net::encodePayload(net::GoldEvent{position, static_cast<std::uint8_t>(team)}));
}

void NetGame::sendCollision(std::uint8_t otherPlayer, const Vec3& position, float impulse) {
    sendEvent(static_cast<std::uint16_t>(net::GameEventType::Collision),
              net::encodePayload(net::CollisionEvent{otherPlayer, position, impulse}));
}

void NetGame::sendDamage(float damage, std::uint8_t source) {
    sendEvent(static_cast<std::uint16_t>(net::GameEventType::Damage),
              net::encodePayload(net::DamageEvent{damage, source}));
}

std::vector<NetGameEvent> NetGame::takeGameEvents() { return std::exchange(m_gameEvents, {}); }

// --- Helpers -----------------------------------------------------------------------------

// NetArena::AddGameChatLine.
void NetGame::addSystemLine(std::string text) { addChatLine({net::kInvalidPlayerId, {}, std::move(text), true}); }

void NetGame::addChatLine(NetChatLine line) {
    line.serial = m_chatSerial++;
    m_chat.push_back(std::move(line));
    while (m_chat.size() > kMaxChatLines)
        m_chat.pop_front();
}

std::string NetGame::playerName(std::uint8_t id) const {
    const net::PlayerInfo* p = player(id);
    return p ? p->name : std::string("?");
}

} // namespace mm2::game
