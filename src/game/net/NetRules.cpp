#include "game/net/NetRules.h"

#include "core/Log.h"

#include <algorithm>
#include <cmath>

namespace mm2::game {
namespace {

using net::RuleDecision;
using net::RuleDecisionMsg;
using CrMessage = session::CopsAndRobbers::Message;

std::uint32_t toMs(float seconds) {
    if (!(seconds < session::RaceReferee::kDnf))
        return net::kRuleDnfMs;
    return std::min(net::kRuleDnfMs - 1, static_cast<std::uint32_t>(std::lround(std::max(0.0f, seconds) * 1000.0f)));
}

float toSeconds(std::uint32_t ms) {
    return ms >= net::kRuleDnfMs ? session::Session::kNetDnf : static_cast<float>(ms) * 0.001f;
}

std::uint8_t toPlayer(int car) {
    return car >= 0 && car < net::kInvalidPlayerId ? static_cast<std::uint8_t>(car) : net::kInvalidPlayerId;
}

RuleDecisionMsg fromCops(const CrMessage& m) {
    RuleDecisionMsg d;
    d.player = toPlayer(m.car);
    switch (m.type) {
    case CrMessage::Type::PickupRequest:
    case CrMessage::Type::GoldTaken: d.type = RuleDecision::GoldTaken; break;
    case CrMessage::Type::GoldDropped:
        d.type = RuleDecision::GoldDropped;
        d.position = m.position;
        d.flag = m.knocked;
        break;
    case CrMessage::Type::GoldDelivered: d.type = RuleDecision::GoldDelivered; break;
    case CrMessage::Type::NewSet:
        d.type = RuleDecision::NewSet;
        d.gold = m.set.gold;
        d.bank = m.set.bank;
        d.hideout = m.set.hideout;
        break;
    case CrMessage::Type::Limit:
        d.type = RuleDecision::Limit;
        d.flag = m.pointLimit;
        d.value = std::clamp(m.value, 0, net::kMaxRuleScore);
        break;
    }
    return d;
}

CrMessage toCops(const RuleDecisionMsg& d) {
    CrMessage m;
    m.car = d.player == net::kInvalidPlayerId ? -1 : d.player;
    switch (d.type) {
    case RuleDecision::GoldTaken: m.type = CrMessage::Type::GoldTaken; break;
    case RuleDecision::GoldDropped:
        m.type = CrMessage::Type::GoldDropped;
        m.position = d.position;
        m.knocked = d.flag;
        break;
    case RuleDecision::GoldDelivered: m.type = CrMessage::Type::GoldDelivered; break;
    case RuleDecision::NewSet:
        m.type = CrMessage::Type::NewSet;
        m.set = {d.bank, d.gold, d.hideout};
        break;
    default:
        m.type = CrMessage::Type::Limit;
        m.pointLimit = d.flag;
        m.value = d.value;
        break;
    }
    return m;
}

} // namespace

bool hostAcceptsGameEvent(std::uint8_t from, std::uint16_t type) {
    if (from == net::kHostPlayerId)
        return true;
    // MM2's peers sent these about their own car (SendFinishReq, the waypoint
    // count, mmMultiCR's 0x25e, 0x259, 600, 0x261, SendLimitReached); under
    // the host's authority it decides them from its own simulation.
    using T = net::GameEventType;
    switch (static_cast<T>(type)) {
    case T::CheckpointReached:
    case T::LapCompleted:
    case T::RaceFinished:
    case T::GoldPickedUp:
    case T::GoldDropped:
    case T::GoldDelivered: return false;
    default: break;
    }
    constexpr auto kCustom = static_cast<std::uint16_t>(T::Custom);
    // Cops and Robbers' pickup request, places and limit (0x8001-0x8003)
    // and the rules message.
    return !(type >= kCustom + 1 && type <= kCustom + 3) && type != net::kRulesEvent;
}

void NetRules::begin(const Setup& setup) {
    *this = NetRules{};
    m_setup = setup;
    if (!m_setup.host)
        return;
    if (session::Session* s = m_setup.session) {
        session::RaceReferee::Config c;
        c.mode = s->mode();
        c.checkpoints = s->checkpoints();
        c.laps = s->laps();
        c.timeLimit = s->mode() == GameMode::Blitz ? s->setup().timeLimit : 0.0f;
        c.host = m_setup.self;
        c.sampleSeconds = m_setup.sampleSeconds;
        m_referee = std::make_unique<session::RaceReferee>(std::move(c));
        for (const std::uint8_t p : m_setup.players)
            m_referee->addPlayer(p);
    }
    for (const std::uint8_t p : m_setup.players)
        if (p != m_setup.self)
            m_clients[p] = {};
}

std::string NetRules::nameOf(std::uint8_t id) const {
    const auto it = m_names.find(id);
    return it != m_names.end() ? it->second : std::format("Player {}", id);
}

void NetRules::hostSample(std::uint8_t id, std::uint32_t seq, const Mat34& car, const Vec3& inertiaBox, bool held) {
    if (!m_setup.host || m_left.contains(id))
        return;
    m_carSamples[id] = seq;
    if (m_referee)
        m_referee->sample(id, seq, car, inertiaBox, held);
}

void NetRules::playerLeft(std::uint8_t id) {
    if (!m_left.insert(id).second)
        return;
    m_clients.erase(id);
    m_icons.erase(id);
    if (m_referee)
        m_referee->removePlayer(id);
}

void NetRules::hostDecided(const std::vector<CrMessage>& decisions) {
    if (!m_setup.host)
        return;
    for (const auto& m : decisions)
        decide(fromCops(m));
}

void NetRules::decide(RuleDecisionMsg d) {
    d.seq = static_cast<std::uint32_t>(m_log.size()) + 1;
    m_log.push_back(d);
    apply(d);
}

void NetRules::apply(const RuleDecisionMsg& d) {
    ++m_stats.decisions;
    session::Session* s = m_setup.session;
    switch (d.type) {
    case RuleDecision::Finished:
        m_finished.insert(d.player);
        if (s) {
            if (d.player == m_setup.self)
                s->netFinished(toSeconds(d.ms));
            else
                s->remoteFinished(nameOf(d.player), toSeconds(d.ms));
        }
        return;
    case RuleDecision::TimedOut:
        m_timedOut = true;
        if (s)
            s->netTimedOut();
        return;
    case RuleDecision::AllCounted:
        m_allCounted = true;
        if (s)
            s->netAllCounted();
        return;
    default:
        // Cops and Robbers: the host's rules applied them already.
        if (!m_setup.host && m_setup.cops)
            m_setup.cops->applyHost(toCops(d), m_setup.self);
        return;
    }
}

net::RulesMsg NetRules::message(std::uint8_t player, Client& c) const {
    net::RulesMsg m;
    m.race = m_setup.race;
    m.seq = ++c.seq;
    std::size_t k = c.lastDecision;
    for (; k < m_log.size() && m.decisions.size() < net::kMaxRuleDecisions; ++k)
        m.decisions.push_back(m_log[k]);
    c.lastDecision = static_cast<std::uint32_t>(k);
    if (const auto it = m_carSamples.find(player); it != m_carSamples.end())
        m.evaluated = it->second;
    if (const session::CopsAndRobbers* cops = m_setup.cops) {
        m.cops = true;
        const auto st = cops->state();
        m.carrier = toPlayer(st.carrier);
        m.goldActive = st.goldActive;
        m.goldPosition = st.goldPosition;
        m.bank = st.set.bank;
        m.setGold = st.set.gold;
        m.hideout = st.set.hideout;
        for (const auto& [id, sc] : st.scores)
            if (m.scores.size() < net::kMaxPlayers && id >= 0 && id < net::kInvalidPlayerId)
                m.scores.emplace_back(static_cast<std::uint8_t>(id), std::clamp(sc, 0, net::kMaxRuleScore));
        m.over = st.over;
        return m;
    }
    if (!m_referee)
        return m;
    if (const auto* p = m_referee->player(player)) {
        m.evaluated = p->evaluated;
        const std::size_t first = p->hits.size() > kHitWindow ? p->hits.size() - kHitWindow : 0;
        m.firstHit = static_cast<std::uint32_t>(first);
        m.hits.assign(p->hits.begin() + static_cast<std::ptrdiff_t>(first), p->hits.end());
        c.hits = p->hits.size();
        m.place = static_cast<std::uint8_t>(std::clamp(p->place, 1, static_cast<int>(net::kMaxPlayers)));
        m.racers = static_cast<std::uint8_t>(std::clamp(p->racers, 1, static_cast<int>(net::kMaxPlayers)));
    }
    for (const auto& [id, q] : m_referee->players())
        if (id != player && q.inRace && m.icons.size() < net::kMaxPlayers)
            m.icons.emplace_back(id, static_cast<std::uint8_t>(std::clamp(
                                         m_referee->iconPlace(player, id), 0, static_cast<int>(net::kMaxPlayers))));
    for (const auto& r : m_referee->results())
        if (m.results.size() < net::kMaxPlayers)
            m.results.emplace_back(r.player, toMs(r.seconds));
    m.timedOut = m_referee->timedOut();
    m.allCounted = m_referee->allCounted();
    return m;
}

void NetRules::hostUpdate(float dt, std::uint64_t nowMs, const Vec3& ownPosition, const Send& send) {
    if (!m_setup.host || !active())
        return;
    if (m_referee) {
        m_referee->update(dt);
        for (const auto& rd : m_referee->takeDecisions()) {
            RuleDecisionMsg d;
            d.player = rd.player;
            switch (rd.kind) {
            case session::RaceReferee::Decision::Kind::Finished:
                d.type = RuleDecision::Finished;
                d.ms = toMs(rd.seconds);
                break;
            case session::RaceReferee::Decision::Kind::TimedOut:
                d.type = RuleDecision::TimedOut;
                d.player = net::kInvalidPlayerId;
                break;
            case session::RaceReferee::Decision::Kind::AllCounted:
                d.type = RuleDecision::AllCounted;
                d.player = net::kInvalidPlayerId;
                break;
            }
            decide(d);
        }
        // The host's own player takes the referee's word as a client does,
        // with no delay.
        if (const auto* p = m_referee->player(m_setup.self); p && m_setup.session) {
            m_evaluated = p->evaluated;
            m_setup.session->applyNetProgress({p->evaluated, 0, p->hits}, ownPosition);
            m_setup.session->setNetStanding(p->place, p->racers);
        }
        m_icons.clear();
        for (const auto& [id, q] : m_referee->players())
            if (id != m_setup.self && q.inRace)
                m_icons[id] = m_referee->iconPlace(m_setup.self, id);
    }
    if (!send)
        return;
    for (auto& [id, c] : m_clients) {
        const bool news = c.lastDecision < m_log.size();
        const auto* p = m_referee ? m_referee->player(id) : nullptr;
        const bool hit = p && p->hits.size() != c.hits;
        if (!news && !hit && c.seq != 0 && nowMs - c.sentAt < kStateIntervalMs)
            continue;
        const net::RulesMsg m = message(id, c);
        auto payload = net::encodePayload(m);
        if (payload.size() > net::kMaxEventPayload) {
            log::warn("netrules: the message to player {} is {} bytes", id, payload.size());
            continue;
        }
        c.sentAt = nowMs;
        ++m_stats.sent;
        m_stats.bytes += payload.size();
        send(id, std::move(payload));
    }
}

void NetRules::receive(const std::vector<NetGameEvent>& events, const Vec3& carPosition) {
    if (m_setup.host || !active())
        return;
    for (const auto& ev : events) {
        if (static_cast<std::uint16_t>(ev.type) != net::kRulesEvent)
            continue;
        if (ev.from != net::kHostPlayerId) {
            ++m_stats.refused;
            continue;
        }
        net::RulesMsg m;
        if (!net::decodePayload(ev.payload, m)) {
            ++m_stats.malformed;
            continue;
        }
        if (m.race != m_setup.race || m.seq <= m_lastMessage) {
            ++m_stats.stale;
            continue;
        }
        m_lastMessage = m.seq;
        ++m_stats.received;
        m_evaluated = std::max(m_evaluated, m.evaluated);
        for (const auto& d : m.decisions) {
            if (d.seq <= m_lastDecision)
                continue;
            if (d.seq > m_lastDecision + 1) {
                ++m_stats.gaps;
                log::info("netrules: decisions {} to {} missed (the state stands in)", m_lastDecision + 1,
                          d.seq - 1);
            }
            m_lastDecision = d.seq;
            m_log.push_back(d);
            apply(d);
        }
        if (m.cops != (m_setup.cops != nullptr))
            continue;
        if (session::CopsAndRobbers* cops = m_setup.cops) {
            session::CopsAndRobbers::State st;
            st.carrier = m.carrier == net::kInvalidPlayerId ? -1 : m.carrier;
            st.goldActive = m.goldActive;
            st.goldPosition = m.goldPosition;
            st.set = {m.bank, m.setGold, m.hideout};
            for (const auto& [id, sc] : m.scores)
                st.scores.emplace_back(id, sc);
            st.over = m.over;
            const bool confirmed =
                m_pickupSample && m.evaluated >= *m_pickupSample + session::Session::kNetHitMargin;
            cops->adopt(st, m_setup.self, confirmed);
            if (!cops->pickupPending())
                m_pickupSample.reset();
        } else {
            applyRaceState(m, carPosition);
        }
    }
}

void NetRules::applyRaceState(const net::RulesMsg& m, const Vec3& carPosition) {
    session::Session* s = m_setup.session;
    if (!s)
        return;
    s->applyNetProgress({m.evaluated, m.firstHit, m.hits}, carPosition);
    s->setNetStanding(m.place, m.racers);
    m_icons.clear();
    for (const auto& [id, place] : m.icons)
        m_icons[id] = place;
    // A finish whose decision this machine missed: the results stand in.
    for (const auto& [id, ms] : m.results)
        if (!m_finished.contains(id))
            apply({0, RuleDecision::Finished, id, ms});
    if (m.timedOut && !m_timedOut)
        apply({0, RuleDecision::TimedOut});
    if (m.allCounted && !m_allCounted)
        apply({0, RuleDecision::AllCounted});
}

int NetRules::iconPlace(std::uint8_t id) const {
    const auto it = m_icons.find(id);
    return it == m_icons.end() ? 0 : it->second;
}

} // namespace mm2::game
