#include "net/Session.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "net/Sha256.h"

#include <algorithm>
#include <format>
#include <random>

namespace mm2::net {
namespace {

DisconnectReason reasonFromData(std::uint32_t data) {
    if (data <= static_cast<std::uint32_t>(DisconnectReason::Last))
        return static_cast<DisconnectReason>(data);
    return DisconnectReason::None;
}

std::uint32_t toData(DisconnectReason r) { return static_cast<std::uint32_t>(r); }

std::string sanitize(std::string_view text, std::size_t maxLength) { return sanitizeText(text, maxLength); }

// A car name from the network, or `fallback` when it is not a plain base name.
std::string carName(std::string_view car, std::string_view fallback) {
    return isValidAssetName(car) ? std::string(car) : std::string(fallback);
}

// What the host says about a player, made safe to show and to load.
void cleanPlayer(PlayerInfo& p) {
    p.name = sanitize(p.name, kMaxNameLength);
    p.car = carName(p.car, kDefaultCar);
}

// The host's settings. A city that is not a plain base name becomes empty,
// which no city loads: the name would otherwise reach file names and, as the
// last race, the profile file.
void cleanSettings(SessionSettings& s) {
    s.name = sanitize(s.name, kMaxNameLength * 2);
    if (!s.city.empty() && !isValidAssetName(s.city)) {
        log::warn("net: ignoring the host's city: not a valid name");
        s.city.clear();
    }
}

// Adds or replaces a player the host told about. False (and nothing changes)
// for the id no player has, or a new player beyond the protocol's limit:
// every listed player gets a car in the race.
bool upsertPlayer(std::vector<PlayerInfo>& players, const PlayerInfo& p) {
    if (p.id == kInvalidPlayerId)
        return false;
    if (const auto it = std::ranges::find(players, p.id, &PlayerInfo::id); it != players.end()) {
        *it = p;
        return true;
    }
    if (players.size() >= kMaxPlayers)
        return false;
    players.push_back(p);
    return true;
}

// What a joiner may make the host relay to everyone else, in messages per
// second and burst. Far above what the game sends (chat typed by hand, a car,
// colour, team or ready change, a few race events); a flood beyond it would
// otherwise go out once per player on reliable channels.
constexpr double kChatRate = 2.0, kChatBurst = 8.0;
constexpr double kUpdateRate = 10.0, kUpdateBurst = 20.0;
constexpr double kEventRate = 30.0, kEventBurst = 60.0;

// An address that sent this many wrong passwords within the window is turned
// away until the window ends, so guessing a lobby password online costs
// minutes per handful of guesses instead of one round trip each.
constexpr int kPasswordAttempts = 5;
constexpr std::uint64_t kPasswordWindowMs = 60000;
using PasswordFailures = std::map<std::uint32_t, std::pair<int, std::uint64_t>>;

bool passwordLockedOut(PasswordFailures& failures, std::uint32_t ip, std::uint64_t now) {
    std::erase_if(failures, [&](const auto& f) { return now - f.second.second > kPasswordWindowMs; });
    const auto it = failures.find(ip);
    return it != failures.end() && it->second.first >= kPasswordAttempts;
}

void notePasswordFailure(PasswordFailures& failures, std::uint32_t ip, std::uint64_t now) {
    const auto it = failures.try_emplace(ip, 0, now).first;
    if (now - it->second.second > kPasswordWindowMs)
        it->second = {0, now};
    ++it->second.first;
}

std::array<std::byte, 16> randomNonce() {
    std::random_device rd;
    std::array<std::byte, 16> n{};
    for (std::size_t i = 0; i < n.size(); i += 4) {
        const std::uint32_t v = rd();
        for (std::size_t j = 0; j < 4; ++j)
            n[i + j] = static_cast<std::byte>(v >> (8 * j));
    }
    return n;
}

} // namespace

std::array<std::byte, 32> passwordProof(const std::array<std::byte, 16>& nonce, const std::string& password) {
    if (password.empty())
        return {};
    Sha256 h;
    h.update(nonce);
    h.update(password);
    return h.finish();
}

bool Session::RateLimit::take(std::uint64_t now, double perSecond, double burst) {
    const double refill = static_cast<double>(now - last) * perSecond / 1000.0;
    tokens = tokens < 0.0 ? burst : std::min(burst, tokens + refill);
    last = now;
    if (tokens < 1.0)
        return false;
    tokens -= 1.0;
    return true;
}

Session::Session(SessionConfig config) : m_config(std::move(config)) {
    if (m_config.snapshotRateHz == 0)
        m_config.snapshotRateHz = 1;
}

Session::~Session() { leave(); }

// --- Setup ------------------------------------------------------------------------

bool Session::host(const HostParams& params, std::string* error) {
    if (m_role != Role::None && m_state != State::Closed) {
        if (error)
            *error = "already in a session";
        return false;
    }
    resetState();
    auto transport = std::make_unique<Transport>(m_config.transport);
    if (!transport->listen(params.bind, error))
        return false;
    m_transport = std::move(transport);
    m_role = Role::Host;
    m_state = State::Active;
    m_phase = SessionPhase::Lobby;
    m_hostEpoch = monotonicMs();
    m_password = params.password;
    m_settings = params.settings;
    m_settings.maxPlayers = static_cast<std::uint8_t>(std::clamp<int>(m_settings.maxPlayers, 1, kMaxPlayers));
    m_settings.hasPassword = !m_password.empty();
    m_settings.name = sanitize(m_settings.name, kMaxNameLength * 2);

    PlayerInfo self;
    self.id = kHostPlayerId;
    self.name = sanitize(params.player.name, kMaxNameLength);
    if (self.name.empty())
        self.name = "Host";
    self.car = carName(params.player.car, kDefaultCar);
    self.color = params.player.color;
    self.team = params.player.team;
    self.host = true;
    m_players = {self};
    m_localId = kHostPlayerId;

    if (params.advertiseOnLan) {
        m_beacon = std::make_unique<LanBeacon>();
        std::string beaconError;
        if (!m_beacon->start(params.discoveryPort, &beaconError)) {
            log::warn("net: LAN advertising disabled: {}", beaconError);
            m_beacon.reset();
        }
    }
    emit(ev::JoinAccepted{m_localId});
    return true;
}

bool Session::join(const JoinParams& params, std::string* error) {
    if (m_role != Role::None && m_state != State::Closed) {
        if (error)
            *error = "already in a session";
        return false;
    }
    resetState();
    auto transport = std::make_unique<Transport>(m_config.transport);
    if (!transport->startClient(error))
        return false;
    m_hostPeer = transport->connect(params.host);
    if (m_hostPeer == kInvalidPeer) {
        if (error)
            *error = "cannot start connection";
        return false;
    }
    m_transport = std::move(transport);
    m_role = Role::Client;
    m_state = State::Connecting;
    m_connectStarted = monotonicMs();
    m_joinPassword = params.password;
    m_joinPlayer = params.player;
    log::info("net: connecting to {}", params.host.toString());
    return true;
}

void Session::resetState() {
    leave();
    m_role = Role::None;
    m_state = State::Idle;
    m_phase = SessionPhase::Lobby;
    m_events.clear();
    m_settings = {};
    m_players.clear();
    m_localId = kInvalidPlayerId;
    m_password.clear();
    m_countdownEnd = 0;
    m_hostEpoch = 0;
    m_lastPingBroadcast = 0;
    m_passwordFailures.clear();
    m_hostPeer = kInvalidPeer;
    m_connectStarted = 0;
    m_clock.reset();
    m_nextTimeRequest = 0;
    m_timeRequestsSent = 0;
    m_joinPassword.clear();
    m_joinPlayer = {};
    m_request = {};
    m_localState.reset();
    m_lastSnapshotSent = 0;
    m_ambientStates.clear();
}

void Session::leave() {
    if (m_transport) {
        m_transport->shutdown(toData(m_role == Role::Host ? DisconnectReason::HostShutdown : DisconnectReason::Left));
        m_transport.reset();
    }
    if (m_beacon) {
        m_beacon->stop();
        m_beacon.reset();
    }
    if (m_role != Role::None)
        m_state = State::Closed;
    m_remotes.clear();
    m_remoteStates.clear();
    m_pendingStates.clear();
}

void Session::close(DisconnectReason reason, std::string message, bool failedJoin) {
    if (m_state == State::Closed)
        return;
    if (message.empty())
        message = describe(reason);
    log::info("net: session closed: {}", message);
    leave();
    m_state = State::Closed;
    if (failedJoin)
        emit(ev::JoinFailed{reason, std::move(message)});
    else
        emit(ev::Disconnected{reason, std::move(message)});
}

// --- Helpers ----------------------------------------------------------------------

std::uint32_t Session::time() const {
    if (m_role == Role::Host)
        return static_cast<std::uint32_t>(monotonicMs() - m_hostEpoch);
    if (!m_clock.synced())
        return 0;
    const double t = m_clock.toHostTime(static_cast<double>(monotonicMs()));
    return t <= 0.0 ? 0u : static_cast<std::uint32_t>(t);
}

const PlayerInfo* Session::player(std::uint8_t id) const {
    for (const auto& p : m_players)
        if (p.id == id)
            return &p;
    return nullptr;
}

PlayerInfo* Session::findPlayer(std::uint8_t id) {
    for (auto& p : m_players)
        if (p.id == id)
            return &p;
    return nullptr;
}

Session::Remote* Session::remoteForPlayer(std::uint8_t id) {
    for (auto& [peer, r] : m_remotes)
        if (r.playerId == id)
            return &r;
    return nullptr;
}

std::uint8_t Session::allocatePlayerId() const {
    for (int id = 1; id < kInvalidPlayerId; ++id)
        if (!player(static_cast<std::uint8_t>(id)))
            return static_cast<std::uint8_t>(id);
    return kInvalidPlayerId;
}

std::string Session::uniqueName(std::string name) const {
    if (name.empty())
        name = "Player";
    auto taken = [&](const std::string& n) {
        return std::ranges::any_of(m_players, [&](const PlayerInfo& p) { return str::iequals(p.name, n); });
    };
    if (!taken(name))
        return name;
    for (int i = 2;; ++i) {
        std::string suffix = std::format(" ({})", i);
        std::string base = sanitize(name, kMaxNameLength - suffix.size()); // cut on a character boundary
        if (!taken(base + suffix))
            return base + suffix;
    }
}

template <class M>
void Session::sendTo(PeerId peer, Channel channel, M msg) {
    if (m_transport)
        m_transport->send(peer, channel, encodeMessage(std::move(msg)));
}

template <class M>
void Session::sendToPlayers(Channel channel, M msg, std::uint8_t except) {
    if (!m_transport)
        return;
    const auto packet = encodeMessage(std::move(msg));
    for (const auto& [peer, r] : m_remotes)
        if (r.playerId != kInvalidPlayerId && r.playerId != except)
            m_transport->send(peer, channel, packet);
}

// --- Update -----------------------------------------------------------------------

void Session::update() {
    if (!m_transport || m_state == State::Closed)
        return;
    std::vector<TransportEvent> events;
    m_transport->service(events, 0);
    for (auto& e : events) {
        if (m_state == State::Closed)
            break;
        handleTransportEvent(e);
    }
    if (m_state == State::Closed || !m_transport)
        return;

    const std::uint64_t now = monotonicMs();
    const std::uint64_t snapshotInterval = 1000 / m_config.snapshotRateHz;

    if (m_role == Role::Host) {
        // Handshake timeouts.
        std::vector<PeerId> stale;
        for (const auto& [peer, r] : m_remotes)
            if (r.playerId == kInvalidPlayerId && now - r.connectedAt > m_config.joinTimeoutMs)
                stale.push_back(peer);
        for (PeerId p : stale) {
            m_transport->disconnect(p, toData(DisconnectReason::JoinTimeout));
            m_remotes.erase(p);
        }
        hostRelayUpdates();

        if (now - m_lastPingBroadcast >= m_config.pingBroadcastIntervalMs) {
            m_lastPingBroadcast = now;
            PlayerPingsMsg pings;
            for (const auto& [peer, r] : m_remotes) {
                if (r.playerId == kInvalidPlayerId)
                    continue;
                const auto rtt = static_cast<std::uint16_t>(std::min<std::uint32_t>(m_transport->stats(peer).rttMs, 65535));
                if (PlayerInfo* p = findPlayer(r.playerId))
                    p->ping = rtt;
                pings.pings.emplace_back(r.playerId, rtt);
            }
            if (!pings.pings.empty())
                sendToPlayers(Channel::Control, pings);
        }

        if (now - m_lastSnapshotSent >= snapshotInterval) {
            m_lastSnapshotSent = now;
            hostSendWorldState();
        }

        if (m_beacon) {
            hostAdvertise();
            m_beacon->update();
        }
    } else if (m_state == State::Connecting || m_state == State::Joining) {
        const std::uint64_t limit = m_state == State::Connecting ? m_config.connectTimeoutMs : m_config.joinTimeoutMs;
        if (now - m_connectStarted > limit) {
            close(DisconnectReason::JoinTimeout, m_state == State::Connecting ? "could not reach the host" : "",
                  true);
            return;
        }
    } else if (m_state == State::Active) {
        // Clock sync: a quick burst after joining, then a slow refresh.
        if (now >= m_nextTimeRequest) {
            sendTo(m_hostPeer, Channel::Control, TimeRequestMsg{static_cast<std::uint32_t>(now)});
            ++m_timeRequestsSent;
            m_nextTimeRequest = now + (m_timeRequestsSent < 6 ? 150 : 2000);
        }
        if (m_localState && now - m_lastSnapshotSent >= snapshotInterval) {
            m_lastSnapshotSent = now;
            sendTo(m_hostPeer, Channel::State, VehicleStateMsg{*m_localState});
        }
    }

    tickCountdown();

    const double renderTime = static_cast<double>(time()) - m_config.interpolationDelayMs;
    for (auto& [id, buffer] : m_remoteStates)
        buffer.prune(renderTime);

    if (m_transport)
        m_transport->flush();
}

void Session::tickCountdown() {
    if (m_phase != SessionPhase::Countdown || !clockSynced())
        return;
    if (static_cast<std::int64_t>(time()) - static_cast<std::int64_t>(m_countdownEnd) >= 0) {
        m_phase = SessionPhase::InGame;
        emit(ev::GameStarted{});
    }
}

void Session::hostSendWorldState() {
    if (m_localState)
        m_pendingStates[kHostPlayerId] = *m_localState;
    if (m_pendingStates.empty())
        return;
    for (const auto& [peer, r] : m_remotes) {
        if (r.playerId == kInvalidPlayerId)
            continue;
        WorldStateMsg msg;
        for (const auto& [id, state] : m_pendingStates)
            if (id != r.playerId)
                msg.vehicles.emplace_back(id, state);
        if (!msg.vehicles.empty())
            sendTo(peer, Channel::State, std::move(msg));
    }
    m_pendingStates.clear();
}

void Session::hostAdvertise() {
    LanAdvert a;
    a.sessionName = m_settings.name;
    a.hostName = m_players.empty() ? std::string() : m_players.front().name;
    a.city = m_settings.city;
    a.mode = m_settings.mode;
    a.raceId = m_settings.raceId;
    a.players = static_cast<std::uint8_t>(m_players.size());
    a.maxPlayers = m_settings.maxPlayers;
    a.hasPassword = m_settings.hasPassword;
    a.phase = m_phase;
    a.gamePort = port();
    a.build = m_config.build;
    m_beacon->setAdvert(a);
}

// --- Transport events -------------------------------------------------------------

void Session::handleTransportEvent(TransportEvent& e) {
    using Type = TransportEvent::Type;
    if (m_role == Role::Host) {
        switch (e.type) {
        case Type::Connected: {
            if (e.data != kConnectData) {
                log::info("net: rejecting connection with protocol {:#x}", e.data);
                m_transport->disconnect(e.peer, toData(DisconnectReason::VersionMismatch));
                return;
            }
            if (m_players.size() >= m_settings.maxPlayers) {
                m_transport->disconnect(e.peer, toData(DisconnectReason::ServerFull));
                return;
            }
            if (!m_password.empty() &&
                passwordLockedOut(m_passwordFailures, m_transport->stats(e.peer).address.ip, monotonicMs())) {
                m_transport->disconnect(e.peer, toData(DisconnectReason::BadPassword));
                return;
            }
            Remote r;
            r.peer = e.peer;
            r.nonce = randomNonce();
            r.connectedAt = monotonicMs();
            m_remotes[e.peer] = r;
            sendTo(e.peer, Channel::Control, ChallengeMsg{r.nonce, !m_password.empty()});
            return;
        }
        case Type::Disconnected: {
            const auto it = m_remotes.find(e.peer);
            if (it == m_remotes.end())
                return;
            const std::uint8_t id = it->second.playerId;
            m_remotes.erase(it);
            if (id != kInvalidPlayerId) {
                DisconnectReason reason = reasonFromData(e.data);
                if (reason == DisconnectReason::None)
                    reason = DisconnectReason::Timeout;
                hostRemovePlayer(id, reason);
            }
            return;
        }
        case Type::Received: {
            const auto it = m_remotes.find(e.peer);
            const auto type = peekMessageType(e.payload);
            if (it == m_remotes.end() || !type)
                return;
            hostHandle(it->second, *type, e.payload);
            return;
        }
        }
        return;
    }

    // Client
    if (e.peer != m_hostPeer)
        return;
    switch (e.type) {
    case Type::Connected:
        m_state = State::Joining;
        m_connectStarted = monotonicMs();
        return;
    case Type::Disconnected: {
        DisconnectReason reason = reasonFromData(e.data);
        const bool joining = m_state != State::Active;
        if (reason == DisconnectReason::None)
            reason = joining ? DisconnectReason::JoinTimeout : DisconnectReason::Timeout;
        m_hostPeer = kInvalidPeer;
        close(reason, joining && reason == DisconnectReason::JoinTimeout ? "could not reach the host" : "", joining);
        return;
    }
    case Type::Received:
        if (const auto type = peekMessageType(e.payload))
            clientHandle(*type, e.payload);
        return;
    }
}

// --- Host message handling --------------------------------------------------------

void Session::hostHandle(Remote& r, MsgType type, std::span<const std::byte> data) {
    if (r.playerId == kInvalidPlayerId) {
        if (type != MsgType::Hello)
            return;
        HelloMsg hello;
        const PeerId peer = r.peer;
        if (!decodeMessage(data, hello)) {
            m_transport->disconnect(peer, toData(DisconnectReason::ProtocolError));
            m_remotes.erase(peer);
            return;
        }
        auto reject = [&](DisconnectReason reason) {
            sendTo(peer, Channel::Control, RejectMsg{reason, describe(reason)});
            m_transport->disconnect(peer, toData(reason), true);
            m_remotes.erase(peer); // invalidates r
        };
        if (hello.protocolVersion != kProtocolVersion)
            return reject(DisconnectReason::VersionMismatch);
        if (!m_password.empty() && hello.passwordProof != passwordProof(r.nonce, m_password)) {
            notePasswordFailure(m_passwordFailures, m_transport->stats(peer).address.ip, monotonicMs());
            return reject(DisconnectReason::BadPassword);
        }
        if (m_players.size() >= m_settings.maxPlayers)
            return reject(DisconnectReason::ServerFull);
        if (m_phase != SessionPhase::Lobby && !m_settings.allowJoinInProgress)
            return reject(DisconnectReason::GameInProgress);
        hostAcceptHello(r, hello);
        return;
    }

    const std::uint8_t id = r.playerId;
    switch (type) {
    case MsgType::PlayerRequest: {
        PlayerRequestMsg req;
        PlayerInfo* p = findPlayer(id);
        if (!decodeMessage(data, req) || !p)
            return;
        p->car = carName(req.car, p->car); // a name that is not a base name keeps the car
        p->color = req.color;
        p->team = req.team;
        p->ready = req.ready;
        // Every request carries the player's whole state, so one relayed
        // later (hostRelayUpdates) still ends at the latest one.
        r.updatePending = true;
        hostRelayUpdates();
        return;
    }
    case MsgType::Chat: {
        ChatMsg chat;
        if (!decodeMessage(data, chat))
            return;
        chat.from = id;
        chat.text = sanitize(chat.text, kMaxChatLength);
        if (chat.text.empty())
            return;
        if (!r.chat.take(monotonicMs(), kChatRate, kChatBurst)) {
            log::debug("net: dropped chat from player {}: too many lines", id);
            return;
        }
        sendToPlayers(Channel::Control, chat);
        emit(ev::Chat{id, chat.text});
        return;
    }
    case MsgType::TimeRequest: {
        TimeRequestMsg req;
        if (decodeMessage(data, req))
            sendTo(r.peer, Channel::Control, TimeResponseMsg{req.clientTime, time()});
        return;
    }
    case MsgType::VehicleState: {
        VehicleStateMsg msg;
        if (!decodeMessage(data, msg))
            return;
        m_pendingStates[id] = msg.state;
        m_remoteStates[id].push(msg.state);
        return;
    }
    case MsgType::GameEvent: {
        GameEventMsg msg;
        if (!decodeMessage(data, msg))
            return;
        if (!r.events.take(monotonicMs(), kEventRate, kEventBurst)) {
            log::debug("net: dropped game event {} from player {}: too many events", msg.type, id);
            return;
        }
        msg.from = id;
        hostRelayEvent(id, std::move(msg));
        return;
    }
    default: return; // host-to-client messages are ignored
    }
}

void Session::hostAcceptHello(Remote& r, HelloMsg& hello) {
    const std::uint8_t id = allocatePlayerId();
    if (id == kInvalidPlayerId) {
        const PeerId peer = r.peer;
        m_transport->disconnect(peer, toData(DisconnectReason::ServerFull));
        m_remotes.erase(peer);
        return;
    }
    PlayerInfo p;
    p.id = id;
    p.name = uniqueName(sanitize(hello.name, kMaxNameLength));
    p.car = carName(hello.car, kDefaultCar);
    p.color = hello.color;
    p.team = hello.team;
    p.ping = static_cast<std::uint16_t>(std::min<std::uint32_t>(m_transport->stats(r.peer).rttMs, 65535));
    m_players.push_back(p);
    r.playerId = id;
    log::info("net: {} joined as player {} from {}", p.name, id, m_transport->stats(r.peer).address.toString());

    WelcomeMsg welcome;
    welcome.yourId = id;
    welcome.settings = m_settings;
    welcome.players = m_players;
    welcome.phase = m_phase;
    welcome.hostTime = time();
    welcome.countdownEnd = m_countdownEnd;
    sendTo(r.peer, Channel::Control, std::move(welcome));
    sendToPlayers(Channel::Control, PlayerJoinedMsg{p}, id);
    emit(ev::PlayerJoined{p});
}

// Relays the joiners' applied PlayerRequests while their budget allows; a
// joiner over it is relayed later, with whatever its latest state is then.
void Session::hostRelayUpdates() {
    const std::uint64_t now = monotonicMs();
    for (auto& [peer, r] : m_remotes) {
        if (!r.updatePending || !r.updates.take(now, kUpdateRate, kUpdateBurst))
            continue;
        r.updatePending = false;
        if (const PlayerInfo* p = findPlayer(r.playerId)) {
            sendToPlayers(Channel::Control, PlayerUpdateMsg{*p});
            emit(ev::PlayerUpdated{*p});
        }
    }
}

void Session::hostRemovePlayer(std::uint8_t id, DisconnectReason reason) {
    const auto it = std::ranges::find(m_players, id, &PlayerInfo::id);
    if (it == m_players.end())
        return;
    PlayerInfo p = *it;
    m_players.erase(it);
    m_remoteStates.erase(id);
    m_pendingStates.erase(id);
    log::info("net: {} left ({})", p.name, describe(reason));
    sendToPlayers(Channel::Control, PlayerLeftMsg{id, reason});
    emit(ev::PlayerLeft{p, reason});
}

void Session::hostRelayEvent(std::uint8_t from, GameEventMsg msg) {
    if (m_eventFilter && !m_eventFilter(from, msg))
        return;
    msg.from = from;
    if (msg.target == kBroadcastTarget) {
        sendToPlayers(Channel::Events, msg, from);
        if (from != kHostPlayerId)
            emit(ev::GameEvent{msg.from, msg.type, msg.time, msg.payload});
    } else if (msg.target == kHostPlayerId) {
        if (from != kHostPlayerId)
            emit(ev::GameEvent{msg.from, msg.type, msg.time, std::move(msg.payload)});
    } else if (Remote* target = remoteForPlayer(msg.target); target && msg.target != from) {
        sendTo(target->peer, Channel::Events, std::move(msg));
    }
}

// --- Client message handling ------------------------------------------------------

void Session::clientHandle(MsgType type, std::span<const std::byte> data) {
    switch (type) {
    case MsgType::Challenge: {
        ChallengeMsg c;
        if (!decodeMessage(data, c) || m_state != State::Joining)
            return;
        if (c.passwordRequired && m_joinPassword.empty())
            return close(DisconnectReason::BadPassword, "the session requires a password", true);
        HelloMsg hello;
        hello.build = m_config.build;
        hello.name = sanitize(m_joinPlayer.name, kMaxNameLength);
        hello.car = m_joinPlayer.car;
        hello.color = m_joinPlayer.color;
        hello.team = m_joinPlayer.team;
        if (c.passwordRequired)
            hello.passwordProof = passwordProof(c.nonce, m_joinPassword);
        m_joinPassword.clear();
        sendTo(m_hostPeer, Channel::Control, std::move(hello));
        return;
    }
    case MsgType::Welcome: {
        WelcomeMsg w;
        if (!decodeMessage(data, w) || m_state != State::Joining)
            return;
        cleanSettings(w.settings);
        std::erase_if(w.players, [](const PlayerInfo& p) { return p.id == kInvalidPlayerId; });
        for (auto& p : w.players)
            cleanPlayer(p);
        // The host always lists the joiner (hostAcceptHello).
        if (std::ranges::find(w.players, w.yourId, &PlayerInfo::id) == w.players.end())
            return close(DisconnectReason::ProtocolError, "the host sent an invalid welcome", true);
        const std::uint64_t now = monotonicMs();
        m_localId = w.yourId;
        m_settings = std::move(w.settings);
        m_players = std::move(w.players);
        m_phase = w.phase;
        m_countdownEnd = w.countdownEnd;
        m_state = State::Active;
        if (const PlayerInfo* self = player(m_localId))
            m_request = PlayerRequestMsg{self->car, self->color, self->team, self->ready};
        // Rough clock until the first TimeResponse: assume a symmetric trip.
        const std::uint32_t rtt = m_transport->stats(m_hostPeer).rttMs;
        m_clock.addSample(now - std::min<std::uint64_t>(rtt, now), w.hostTime, now);
        m_nextTimeRequest = now;
        emit(ev::JoinAccepted{m_localId});
        if (m_phase == SessionPhase::Countdown)
            emit(ev::CountdownStarted{m_countdownEnd});
        else if (m_phase == SessionPhase::InGame)
            emit(ev::GameStarted{});
        return;
    }
    case MsgType::Reject: {
        RejectMsg rej;
        if (decodeMessage(data, rej))
            close(rej.reason, sanitize(rej.message, kMaxReasonLength), true);
        return;
    }
    default: break;
    }

    if (m_state != State::Active)
        return;

    switch (type) {
    case MsgType::PlayerJoined: {
        PlayerJoinedMsg m;
        if (!decodeMessage(data, m))
            return;
        cleanPlayer(m.player);
        if (upsertPlayer(m_players, m.player))
            emit(ev::PlayerJoined{m.player});
        return;
    }
    case MsgType::PlayerLeft: {
        PlayerLeftMsg m;
        if (!decodeMessage(data, m))
            return;
        const auto it = std::ranges::find(m_players, m.id, &PlayerInfo::id);
        if (it == m_players.end())
            return;
        PlayerInfo p = *it;
        m_players.erase(it);
        m_remoteStates.erase(m.id);
        emit(ev::PlayerLeft{p, m.reason});
        return;
    }
    case MsgType::PlayerUpdate: {
        PlayerUpdateMsg m;
        if (!decodeMessage(data, m))
            return;
        cleanPlayer(m.player);
        if (upsertPlayer(m_players, m.player))
            emit(ev::PlayerUpdated{m.player});
        return;
    }
    case MsgType::Chat: {
        ChatMsg m;
        if (!decodeMessage(data, m))
            return;
        m.text = sanitize(m.text, kMaxChatLength);
        if (!m.text.empty())
            emit(ev::Chat{m.from, std::move(m.text)});
        return;
    }
    case MsgType::Settings: {
        SettingsMsg m;
        if (!decodeMessage(data, m))
            return;
        cleanSettings(m.settings);
        m_settings = m.settings;
        emit(ev::SettingsChanged{std::move(m.settings)});
        return;
    }
    case MsgType::Countdown: {
        CountdownMsg m;
        if (!decodeMessage(data, m))
            return;
        m_phase = SessionPhase::Countdown;
        m_countdownEnd = m.startTime;
        emit(ev::CountdownStarted{m.startTime});
        return;
    }
    case MsgType::ReturnToLobby: {
        ReturnToLobbyMsg m;
        if (!decodeMessage(data, m))
            return;
        m_phase = SessionPhase::Lobby;
        for (auto& p : m_players)
            p.ready = false;
        m_request.ready = false;
        m_remoteStates.clear();
        m_ambientStates.clear();
        emit(ev::ReturnedToLobby{});
        return;
    }
    case MsgType::Kick: {
        KickMsg m;
        decodeMessage(data, m);
        const std::string reason = sanitize(m.reason, kMaxReasonLength);
        close(DisconnectReason::Kicked,
              reason.empty() ? std::string(describe(DisconnectReason::Kicked)) : "kicked: " + reason, false);
        return;
    }
    case MsgType::TimeResponse: {
        TimeResponseMsg m;
        if (!decodeMessage(data, m))
            return;
        const std::uint64_t now = monotonicMs();
        // Rebuild the 64-bit send time from the truncated 32-bit echo.
        std::uint64_t sent = (now & ~std::uint64_t{0xFFFFFFFF}) | m.clientTime;
        if (sent > now)
            sent -= std::uint64_t{1} << 32;
        m_clock.addSample(sent, m.hostTime, now);
        return;
    }
    case MsgType::WorldState: {
        WorldStateMsg m;
        if (!decodeMessage(data, m))
            return;
        for (const auto& [id, state] : m.vehicles)
            if (id != m_localId)
                m_remoteStates[id].push(state);
        return;
    }
    case MsgType::GameEvent: {
        GameEventMsg m;
        if (decodeMessage(data, m))
            emit(ev::GameEvent{m.from, m.type, m.time, std::move(m.payload)});
        return;
    }
    case MsgType::PlayerPings: {
        PlayerPingsMsg m;
        if (!decodeMessage(data, m))
            return;
        for (const auto& [id, ping] : m.pings)
            if (PlayerInfo* p = findPlayer(id))
                p->ping = ping;
        return;
    }
    case MsgType::AmbientState: {
        AmbientStateMsg m;
        if (!decodeMessage(data, m))
            return;
        if (m_ambientStates.size() >= kMaxQueuedAmbientStates)
            m_ambientStates.erase(m_ambientStates.begin());
        m_ambientStates.push_back(std::move(m));
        return;
    }
    default: return;
    }
}

// --- Public API ---------------------------------------------------------------------

void Session::setLocalPlayer(const std::string& car, std::uint8_t color, std::uint8_t team) {
    PlayerInfo* self = findPlayer(m_localId);
    if (!self || m_state != State::Active)
        return;
    if (m_role == Role::Host) {
        self->car = carName(car, self->car);
        self->color = color;
        self->team = team;
        sendToPlayers(Channel::Control, PlayerUpdateMsg{*self});
        emit(ev::PlayerUpdated{*self});
    } else {
        m_request.car = car;
        m_request.color = color;
        m_request.team = team;
        sendTo(m_hostPeer, Channel::Control, m_request);
    }
}

void Session::setReady(bool ready) {
    PlayerInfo* self = findPlayer(m_localId);
    if (!self || m_state != State::Active)
        return;
    if (m_role == Role::Host) {
        self->ready = ready;
        sendToPlayers(Channel::Control, PlayerUpdateMsg{*self});
        emit(ev::PlayerUpdated{*self});
    } else {
        m_request.ready = ready;
        sendTo(m_hostPeer, Channel::Control, m_request);
    }
}

void Session::sendChat(const std::string& text) {
    if (m_state != State::Active)
        return;
    const std::string clean = sanitize(text, kMaxChatLength);
    if (clean.empty())
        return;
    if (m_role == Role::Host) {
        sendToPlayers(Channel::Control, ChatMsg{kHostPlayerId, clean});
        emit(ev::Chat{kHostPlayerId, clean});
    } else {
        sendTo(m_hostPeer, Channel::Control, ChatMsg{m_localId, clean}); // echoed back by the host
    }
}

void Session::updateSettings(const SessionSettings& settings) {
    if (m_role != Role::Host)
        return;
    m_settings = settings;
    m_settings.maxPlayers = static_cast<std::uint8_t>(
        std::clamp<int>(m_settings.maxPlayers, std::max<int>(1, static_cast<int>(m_players.size())), kMaxPlayers));
    m_settings.hasPassword = !m_password.empty();
    m_settings.name = sanitize(m_settings.name, kMaxNameLength * 2);
    if (m_settings.extra.size() > kMaxExtraSettings)
        m_settings.extra.resize(kMaxExtraSettings);
    sendToPlayers(Channel::Control, SettingsMsg{m_settings});
    emit(ev::SettingsChanged{m_settings});
}

void Session::setPassword(const std::string& password) {
    if (m_role != Role::Host)
        return;
    m_password = password;
    if (m_settings.hasPassword != !password.empty())
        updateSettings(m_settings);
}

void Session::kick(std::uint8_t playerId, const std::string& reason) {
    if (m_role != Role::Host || playerId == kHostPlayerId)
        return;
    Remote* r = remoteForPlayer(playerId);
    if (!r)
        return;
    const PeerId peer = r->peer;
    sendTo(peer, Channel::Control, KickMsg{sanitize(reason, kMaxReasonLength)});
    m_transport->disconnect(peer, toData(DisconnectReason::Kicked), true);
    m_remotes.erase(peer); // the later Disconnected event is then ignored
    hostRemovePlayer(playerId, DisconnectReason::Kicked);
}

void Session::startCountdown(std::uint32_t delayMs) {
    if (m_role != Role::Host || m_state != State::Active)
        return;
    m_phase = SessionPhase::Countdown;
    m_countdownEnd = time() + delayMs;
    sendToPlayers(Channel::Control, CountdownMsg{m_countdownEnd});
    emit(ev::CountdownStarted{m_countdownEnd});
}

void Session::returnToLobby() {
    if (m_role != Role::Host || m_state != State::Active)
        return;
    m_phase = SessionPhase::Lobby;
    for (auto& p : m_players)
        p.ready = false;
    m_remoteStates.clear();
    m_pendingStates.clear();
    m_localState.reset();
    sendToPlayers(Channel::Control, ReturnToLobbyMsg{});
    emit(ev::ReturnedToLobby{});
}

void Session::submitLocalState(const VehicleSnapshot& state) {
    m_localState = state;
    m_localState->time = time();
}

SnapshotBuffer::Result Session::sampleRemoteAt(std::uint8_t playerId, double sessionTime, VehicleSnapshot& out) const {
    const auto it = m_remoteStates.find(playerId);
    if (it == m_remoteStates.end())
        return SnapshotBuffer::Result::Empty;
    return it->second.sample(sessionTime, out, m_config.maxExtrapolationMs);
}

SnapshotBuffer::Result Session::sampleRemote(std::uint8_t playerId, VehicleSnapshot& out) const {
    return sampleRemoteAt(playerId, static_cast<double>(time()) - m_config.interpolationDelayMs, out);
}

void Session::sendGameEvent(std::uint16_t type, std::vector<std::byte> payload, std::uint8_t target) {
    if (m_state != State::Active || payload.size() > kMaxEventPayload)
        return;
    GameEventMsg msg;
    msg.from = m_localId;
    msg.target = target;
    msg.type = type;
    msg.time = time();
    msg.payload = std::move(payload);
    if (m_role == Role::Host)
        hostRelayEvent(kHostPlayerId, std::move(msg));
    else
        sendTo(m_hostPeer, Channel::Events, std::move(msg));
}

std::size_t Session::sendAmbientState(std::uint8_t playerId, const AmbientStateMsg& msg) {
    if (m_role != Role::Host || !m_transport || m_state != State::Active || playerId == m_localId)
        return 0;
    Remote* r = remoteForPlayer(playerId);
    if (!r)
        return 0;
    const auto packet = encodeMessage(msg);
    return m_transport->send(r->peer, Channel::Ambient, packet) ? packet.size() : 0;
}

PeerStats Session::peerStats(std::uint8_t playerId) const {
    if (!m_transport)
        return {};
    if (m_role == Role::Client)
        return m_transport->stats(m_hostPeer);
    for (const auto& [peer, r] : m_remotes)
        if (r.playerId == playerId)
            return m_transport->stats(peer);
    return {};
}

} // namespace mm2::net
