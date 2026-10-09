#include "net/Session.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "net/Sha256.h"
#include "net/VehicleDamage.h"

#include <algorithm>
#include <cmath>
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
// A car's damage events (net/VehicleDamage.h) have a budget of their own, so
// a crash's events never use up the race events' or the other way round. The
// game sends at most ten a second.
constexpr double kDamageEventRate = 15.0, kDamageEventBurst = 30.0;
// A joiner's car's inputs (net/PlayerCars.h): the game sends one message a
// frame that simulated a sample, at most 60 a second; the host only queues
// them, so the budget bounds the work and the queue.
constexpr double kInputRate = 90.0, kInputBurst = 180.0;

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
    m_race = 0;
    m_raceOrderTime = 0;
    clearRace();
    m_hostEpoch = 0;
    m_lastPingBroadcast = 0;
    m_passwordFailures.clear();
    m_hostPeer = kInvalidPeer;
    m_connectStarted = 0;
    m_clock.reset();
    m_shownClock.reset();
    m_nextTimeRequest = 0;
    m_timeRequestsSent = 0;
    m_joinPassword.clear();
    m_joinPlayer = {};
    m_request = {};
    m_localState.reset();
    m_lastSentStateTime = 0;
    m_lastSnapshotSent = 0;
    m_lastPlayoutUpdate = -1.0;
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

std::uint32_t Session::time() const { return static_cast<std::uint32_t>(timeMs()); }

double Session::timeMs() const {
    if (m_role == Role::Host)
        return std::max(0.0, monotonicMsPrecise() - static_cast<double>(m_hostEpoch));
    if (!m_shownClock.valid())
        return 0.0;
    return std::max(0.0, monotonicMsPrecise() + m_shownClock.offset());
}

void Session::updateShownClock() {
    // Corrections slew while a race is loading or running (the remote cars
    // and the countdown are drawn on this clock); the lobby takes them at once.
    if (m_clock.synced())
        m_shownClock.update(monotonicMsPrecise(), m_clock.offset(), m_phase != SessionPhase::Lobby);
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
    // Snapshots go out on a steady cadence (the first frame at or after each
    // tick; after a stall the cadence starts again from now).
    auto snapshotDue = [&] {
        if (now - m_lastSnapshotSent < snapshotInterval)
            return false;
        m_lastSnapshotSent = now - m_lastSnapshotSent < 2 * snapshotInterval ? m_lastSnapshotSent + snapshotInterval
                                                                                 : now;
        return true;
    };

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

        if (snapshotDue() && replicating())
            hostSendWorldState();
        if (m_phase == SessionPhase::Countdown && !m_startKnown)
            hostCheckStart();

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
        updateShownClock();
        // Only a state the game has simulated since the last one is sent.
        if (snapshotDue() && replicating() && m_localState &&
            (m_lastSentStateTime == 0 || m_localState->time != m_lastSentStateTime)) {
            m_lastSentStateTime = m_localState->time;
            sendTo(m_hostPeer, Channel::State, VehicleStateMsg{*m_localState});
        }
    }

    tickCountdown();
    updatePlayout();

    if (m_transport)
        m_transport->flush();
}

void Session::updatePlayout() {
    // The delay grows quickly when snapshots start arriving later (the
    // remote car is shown up to a quarter slower for a moment rather than
    // extrapolated) and shrinks slowly; it never makes the shown time run
    // backwards.
    constexpr double kGrowRate = 0.25;
    constexpr double kShrinkRate = 0.02;
    // A snapshot that arrives in a frame is usable from that frame on.
    constexpr double kMarginMs = 4.0;
    // Snapshots kept behind the sample time: callers sample up to a
    // simulation step (and more after a stall) behind the frame's time.
    constexpr double kPruneSlackMs = 100.0;
    const double now = timeMs();
    const double elapsed = m_lastPlayoutUpdate < 0.0 ? 0.0 : std::max(0.0, now - m_lastPlayoutUpdate);
    m_lastPlayoutUpdate = now;
    const double lo = m_config.interpolationDelayMs;
    const double hi = std::max(lo, m_config.maxInterpolationDelayMs);
    for (auto& [id, rv] : m_remoteStates) {
        const double need = rv.buffer.requiredDelay();
        if (need < 0.0) {
            if (rv.delay < 0.0)
                rv.delay = lo;
        } else {
            const double target = std::clamp(need + kMarginMs, lo, hi);
            if (!rv.measured)
                rv.delay = target;
            else if (target > rv.delay)
                rv.delay = std::min(target, rv.delay + elapsed * kGrowRate);
            else
                rv.delay = std::max(target, rv.delay - elapsed * kShrinkRate);
            rv.measured = true;
        }
        rv.buffer.prune(now - rv.delay - kPruneSlackMs);
    }
}

bool Session::receiveState(std::uint8_t id, const VehicleSnapshot& state) {
    // A state is stamped with the sender's session time, which can be off
    // by its clock's error; one far ahead of this clock (a broken or hostile
    // sender: it would drag the car toward it and hold the playout delay) or
    // too old to be shown is dropped, and the host does not relay it.
    constexpr double kMaxAheadMs = 1000.0;
    constexpr double kMaxBehindMs = 5000.0;
    if (!replicating())
        return false;
    const double now = timeMs();
    const auto t = static_cast<double>(state.time);
    if (t > now + kMaxAheadMs || t < now - kMaxBehindMs)
        return false;
    m_remoteStates[id].buffer.push(state, now);
    if (m_stateObserver)
        m_stateObserver(id, state, now);
    return true;
}

void Session::tickCountdown() {
    if (m_phase != SessionPhase::Countdown || !m_startKnown || !clockSynced())
        return;
    if (static_cast<std::int64_t>(time()) - static_cast<std::int64_t>(m_startTime) >= 0) {
        m_phase = SessionPhase::InGame;
        emit(ev::GameStarted{});
    }
}

// --- The race start -------------------------------------------------------------
//
// MM2's network races (mmMultiRace / mmMultiCircuit / mmMultiBlitz::
// UpdateGame state 0): every machine sends RaceReady (0x1f6) once its race
// has loaded, every machine counts the others' (GameMessage 0x1f6 and 0x213,
// less one for each player who leaves meanwhile: SystemMessage 0x2d), and
// the host sends the start (0x20f) when its count reaches 0, at which every
// machine begins Ready / Set / Go. OpenMM2's host sends a shared start time
// instead, a lead ahead, so every countdown ends at the same moment.

void Session::clearRace() {
    m_startKnown = false;
    m_startTime = 0;
    m_countdownMs = 0;
    m_loaded.clear();
}

bool Session::playerLoaded(std::uint8_t id) const {
    return std::ranges::find(m_loaded, id) != m_loaded.end();
}

void Session::markLoaded(std::uint8_t id) {
    m_loaded.push_back(id);
    emit(ev::PlayerLoaded{id, m_race});
}

void Session::hostCheckStart() {
    if (m_role != Role::Host || m_phase != SessionPhase::Countdown || m_startKnown || !m_transport)
        return;
    const bool everyone =
        std::ranges::all_of(m_players, [&](const PlayerInfo& p) { return playerLoaded(p.id); });
    const std::uint32_t now = time();
    const bool waitedEnough = now - m_raceOrderTime >= m_config.loadWaitMs;
    if (!everyone && !waitedEnough)
        return;
    if (!everyone) {
        // OpenMM2's limit (MM2 has none): the race goes on without the
        // players still loading, who join it when they have loaded.
        std::string missing;
        for (const auto& p : m_players)
            if (!playerLoaded(p.id))
                missing += (missing.empty() ? "" : ", ") + p.name;
        log::warn("net: race {} starts without {}: still loading {} ms after the order", m_race, missing,
                  now - m_raceOrderTime);
    }
    // The start message must reach every machine before its countdown: twice
    // the slowest round trip (a loss and its resend) plus 100 ms.
    std::uint32_t rtt = 0;
    for (const auto& [peer, r] : m_remotes)
        if (r.playerId != kInvalidPlayerId)
            rtt = std::max(rtt, m_transport->stats(peer).rttMs);
    const std::uint32_t lead = std::clamp(2 * std::min(rtt, 60000u) + 100, m_config.startLeadMinMs,
                                          std::max(m_config.startLeadMinMs, m_config.startLeadMaxMs));
    m_startKnown = true;
    m_startTime = now + lead + m_countdownMs;
    log::info("net: race {} starts at session time {} (countdown from {}, lead {} ms)", m_race, m_startTime,
              m_startTime - m_countdownMs, lead);
    sendToPlayers(Channel::Control, RaceStartMsg{m_race, m_startTime});
    emit(ev::RaceStartSet{m_race, m_startTime});
}

void Session::hostSendWorldState() {
    // The host's own car, on its snapshot cadence.
    if (!m_localState || m_localState->time == m_lastSentStateTime)
        return;
    m_lastSentStateTime = m_localState->time;
    WorldStateMsg msg;
    msg.vehicles.emplace_back(kHostPlayerId, *m_localState);
    sendToPlayers(Channel::State, std::move(msg));
}

void Session::hostRelayState(std::uint8_t from, const VehicleSnapshot& state) {
    // A joiner's car goes on to the others as soon as it arrives. Held for
    // the host's next tick, it waited up to a tick longer, and of two that
    // arrived within one tick (network jitter) only the newer went on: the
    // other players lost a quarter of each other's snapshots at 30 ms of
    // jitter.
    WorldStateMsg msg;
    msg.vehicles.emplace_back(from, state);
    sendToPlayers(Channel::State, std::move(msg), from);
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
        // Cars move in a race only (receiveState ignores the lobby's: a
        // late one from the last race is old).
        if (decodeMessage(data, msg) && receiveState(id, msg.state))
            hostRelayState(id, msg.state);
        return;
    }
    case MsgType::PlayerInput: {
        // A joiner's car's inputs, during a race only (a late one from the
        // last race is old), within its budget: the next message repeats
        // what a dropped one carried.
        PlayerInputMsg msg;
        if (!replicating() || !r.inputs.take(monotonicMs(), kInputRate, kInputBurst) ||
            !decodeMessage(data, msg))
            return;
        if (m_playerInputs.size() >= kMaxQueuedPlayerInputs)
            m_playerInputs.erase(m_playerInputs.begin());
        m_playerInputs.push_back({id, std::move(msg)});
        return;
    }
    case MsgType::RaceLoaded: {
        // Only the first report of the race being loaded counts: one for an
        // earlier race (sent before the return to the lobby reached the
        // player), a repeat, or one in the lobby is dropped. A report that
        // comes after the start (a player the host stopped waiting for) no
        // longer changes it, but the others still learn that the player is
        // in the race.
        RaceLoadedMsg msg;
        if (!decodeMessage(data, msg) || m_phase == SessionPhase::Lobby || msg.race != m_race ||
            playerLoaded(id))
            return;
        markLoaded(id);
        sendToPlayers(Channel::Control, RaceLoadedMsg{m_race, id}, id);
        hostCheckStart();
        return;
    }
    case MsgType::GameEvent: {
        GameEventMsg msg;
        if (!decodeMessage(data, msg))
            return;
        const bool damage = msg.type == kVehicleDamageEvent;
        RateLimit& budget = damage ? r.damage : r.events;
        if (!budget.take(monotonicMs(), damage ? kDamageEventRate : kEventRate,
                         damage ? kDamageEventBurst : kEventBurst)) {
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
    welcome.race = m_race; // the last race in the lobby: the numbers go on from it
    welcome.raceOrderTime = m_raceOrderTime;
    if (m_phase != SessionPhase::Lobby) {
        welcome.startKnown = m_startKnown;
        welcome.startTime = m_startTime;
        welcome.loaded = m_loaded;
    }
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
    std::erase(m_loaded, id); // no longer waited for (update() checks the start)
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
        clearRace();
        m_race = w.race;
        m_raceOrderTime = w.raceOrderTime;
        if (m_phase != SessionPhase::Lobby) {
            m_startKnown = w.startKnown;
            m_startTime = w.startTime;
            // The others who have loaded (this machine reports itself).
            for (const std::uint8_t id : w.loaded)
                if (id != m_localId && player(id) && !playerLoaded(id))
                    m_loaded.push_back(id);
        }
        m_state = State::Active;
        if (const PlayerInfo* self = player(m_localId))
            m_request = PlayerRequestMsg{self->car, self->color, self->team, self->ready};
        // Rough clock until the first TimeResponse: assume a symmetric trip.
        const double receivedAt = monotonicMsPrecise();
        const double rtt = std::min(static_cast<double>(m_transport->stats(m_hostPeer).rttMs), receivedAt);
        m_clock.addSample(receivedAt - rtt, static_cast<double>(w.hostTime) + 0.5, receivedAt);
        updateShownClock();
        m_nextTimeRequest = now;
        emit(ev::JoinAccepted{m_localId});
        if (m_phase == SessionPhase::Countdown) {
            emit(ev::RaceLoading{m_race, m_raceOrderTime});
            if (m_startKnown)
                emit(ev::RaceStartSet{m_race, m_startTime});
        } else if (m_phase == SessionPhase::InGame) {
            emit(ev::GameStarted{});
        }
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
        std::erase(m_loaded, m.id);
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
    case MsgType::RaceLoad: {
        // The host numbers its races upwards; anything else is not a new race.
        RaceLoadMsg m;
        if (!decodeMessage(data, m) || m.race <= m_race)
            return;
        m_phase = SessionPhase::Countdown;
        m_race = m.race;
        m_raceOrderTime = m.orderTime;
        clearRace();
        resetReplication();
        emit(ev::RaceLoading{m.race, m.orderTime});
        return;
    }
    case MsgType::RaceLoaded: {
        RaceLoadedMsg m;
        // The host tells of the others (its own report it sends itself).
        if (!decodeMessage(data, m) || m_phase == SessionPhase::Lobby || m.race != m_race ||
            !player(m.player) || m.player == m_localId || playerLoaded(m.player))
            return;
        markLoaded(m.player);
        return;
    }
    case MsgType::RaceStart: {
        // Once per race, while it is starting.
        RaceStartMsg m;
        if (!decodeMessage(data, m) || m_phase != SessionPhase::Countdown || m.race != m_race || m_startKnown)
            return;
        m_startKnown = true;
        m_startTime = m.startTime;
        emit(ev::RaceStartSet{m.race, m.startTime});
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
        clearRace();
        resetReplication();
        emit(ev::ReturnedToLobby{m_race});
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
        const double receivedAt = monotonicMsPrecise();
        const auto now = static_cast<std::uint64_t>(receivedAt);
        // Rebuild the 64-bit send time from the truncated 32-bit echo.
        std::uint64_t sent = (now & ~std::uint64_t{0xFFFFFFFF}) | m.clientTime;
        if (sent > now)
            sent -= std::uint64_t{1} << 32;
        // Both times were whole milliseconds cut down: half a millisecond
        // puts them in the middle of their millisecond.
        m_clock.addSample(static_cast<double>(sent) + 0.5, static_cast<double>(m.hostTime) + 0.5, receivedAt);
        updateShownClock();
        return;
    }
    case MsgType::WorldState: {
        WorldStateMsg m;
        if (m_phase == SessionPhase::Lobby || !decodeMessage(data, m))
            return;
        for (const auto& [id, state] : m.vehicles) {
            if (id != m_localId)
                receiveState(id, state);
        }
        return;
    }
    case MsgType::CarStates: {
        // The host's states of the players' cars: this machine's own with
        // the last input it applied, the others into their buffers.
        CarStatesMsg m;
        if (m_phase == SessionPhase::Lobby || !decodeMessage(data, m))
            return;
        for (auto& [id, state] : m.cars) {
            state.time = m.time;
            if (id != m_localId)
                receiveState(id, state);
        }
        if (m_ownCarStates.size() >= kMaxQueuedOwnCarStates)
            m_ownCarStates.erase(m_ownCarStates.begin());
        m_ownCarStates.push_back({m.time, m.ack, m.waiting, m.hasOwn, m.own, timeMs()});
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
        // As the cars' states: only during a race.
        AmbientStateMsg m;
        if (m_phase == SessionPhase::Lobby || !decodeMessage(data, m))
            return;
        if (m_ambientStates.size() >= kMaxQueuedAmbientStates)
            m_ambientStates.erase(m_ambientStates.begin());
        m_ambientStates.push_back(std::move(m));
        return;
    }
    case MsgType::PropState: {
        // As the cars' states: only during a race.
        PropStateMsg m;
        if (m_phase == SessionPhase::Lobby || !decodeMessage(data, m))
            return;
        if (m_propStates.size() >= kMaxQueuedPropStates)
            m_propStates.erase(m_propStates.begin());
        m_propStates.push_back(std::move(m));
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

void Session::startRace(std::uint32_t countdownMs) {
    if (m_role != Role::Host || m_state != State::Active)
        return;
    m_phase = SessionPhase::Countdown;
    ++m_race;
    m_raceOrderTime = time();
    clearRace();
    m_countdownMs = countdownMs;
    resetReplication();
    log::info("net: race {} ordered at session time {}", m_race, m_raceOrderTime);
    sendToPlayers(Channel::Control, RaceLoadMsg{m_race, m_raceOrderTime});
    emit(ev::RaceLoading{m_race, m_raceOrderTime});
}

void Session::reportLoaded() {
    if (m_state != State::Active || m_phase == SessionPhase::Lobby || m_race == 0 || playerLoaded(m_localId))
        return;
    markLoaded(m_localId);
    if (m_role == Role::Host) {
        sendToPlayers(Channel::Control, RaceLoadedMsg{m_race, m_localId});
        hostCheckStart();
    } else {
        sendTo(m_hostPeer, Channel::Control, RaceLoadedMsg{m_race, m_localId});
    }
}

void Session::returnToLobby() {
    if (m_role != Role::Host || m_state != State::Active)
        return;
    m_phase = SessionPhase::Lobby;
    for (auto& p : m_players)
        p.ready = false;
    clearRace();
    resetReplication();
    sendToPlayers(Channel::Control, ReturnToLobbyMsg{});
    emit(ev::ReturnedToLobby{m_race});
}

void Session::submitLocalState(const VehicleSnapshot& state) { submitLocalState(state, timeMs()); }

void Session::submitLocalState(const VehicleSnapshot& state, double sessionTimeMs) {
    if (m_phase == SessionPhase::Lobby)
        return; // no race: nothing to show the others
    m_localState = state;
    m_localState->time = static_cast<std::uint32_t>(std::llround(std::max(0.0, sessionTimeMs)));
}

// The cars of one race: dropped when it starts and when it ends, so neither
// the lobby nor the next race sees where they were.
void Session::resetReplication() {
    m_remoteStates.clear();
    m_localState.reset();
    m_lastSentStateTime = 0;
    m_ambientStates.clear();
}

SnapshotBuffer::Result Session::sampleRemoteAt(std::uint8_t playerId, double sessionTime, VehicleSnapshot& out) const {
    const auto it = m_remoteStates.find(playerId);
    if (it == m_remoteStates.end())
        return SnapshotBuffer::Result::Empty;
    return it->second.buffer.sample(sessionTime, out, m_config.maxExtrapolationMs);
}

SnapshotBuffer::Result Session::sampleRemoteDelayed(std::uint8_t playerId, double sessionTimeMs,
                                                    VehicleSnapshot& out) const {
    return sampleRemoteAt(playerId, sessionTimeMs - playoutDelay(playerId), out);
}

SnapshotBuffer::Result Session::sampleRemote(std::uint8_t playerId, VehicleSnapshot& out) const {
    return sampleRemoteDelayed(playerId, timeMs(), out);
}

double Session::playoutDelay(std::uint8_t playerId) const {
    const auto it = m_remoteStates.find(playerId);
    if (it == m_remoteStates.end() || it->second.delay < 0.0)
        return m_config.interpolationDelayMs;
    return it->second.delay;
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

void Session::sendPlayerInput(const PlayerInputMsg& msg) {
    if (m_role != Role::Client || m_state != State::Active || !replicating() || msg.frames.empty())
        return;
    sendTo(m_hostPeer, Channel::State, msg);
}

std::size_t Session::sendCarStates(std::uint8_t playerId, const CarStatesMsg& msg) {
    if (m_role != Role::Host || !m_transport || m_state != State::Active || !replicating() ||
        playerId == m_localId)
        return 0;
    Remote* r = remoteForPlayer(playerId);
    if (!r)
        return 0;
    const auto packet = encodeMessage(msg);
    return m_transport->send(r->peer, Channel::State, packet) ? packet.size() : 0;
}

std::size_t Session::sendAmbientState(std::uint8_t playerId, const AmbientStateMsg& msg) {
    if (m_role != Role::Host || !m_transport || m_state != State::Active || m_phase == SessionPhase::Lobby ||
        playerId == m_localId)
        return 0;
    Remote* r = remoteForPlayer(playerId);
    if (!r)
        return 0;
    const auto packet = encodeMessage(msg);
    return m_transport->send(r->peer, Channel::Ambient, packet) ? packet.size() : 0;
}

std::size_t Session::sendPropState(std::uint8_t playerId, const PropStateMsg& msg) {
    if (m_role != Role::Host || !m_transport || m_state != State::Active || m_phase == SessionPhase::Lobby ||
        playerId == m_localId)
        return 0;
    Remote* r = remoteForPlayer(playerId);
    if (!r)
        return 0;
    const auto packet = encodeMessage(msg);
    // Unsequenced: a late message still fills the client's buffers (it
    // orders them by their time).
    return m_transport->send(r->peer, Channel::State, packet) ? packet.size() : 0;
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
