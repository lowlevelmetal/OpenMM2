// A network race's props as the race screen runs them (OpenMM2; see
// NetProps.h).

#include "game/net/NetProps.h"

#include "core/Log.h"
#include "game/net/NetGame.h"
#include "net/PropState.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>

namespace mm2::game {
namespace {

// The ring grows rather than wrap onto a prop knocked this recently.
constexpr double kRingBusySeconds = 10.0;

} // namespace

// --- Trace ---------------------------------------------------------------------------------------

std::unique_ptr<PropTrace> PropTrace::of(NetGame& net) {
    return net.tracing() ? std::make_unique<PropTrace>(net) : nullptr;
}

std::string describe(const net::PropDescriptor& what) {
    switch (what.source) {
    case net::PropSource::Prop: return std::format("p{}", what.prop);
    case net::PropSource::PropPart: return std::format("p{}.{}", what.prop, what.part);
    case net::PropSource::CarPart:
        return std::format("c{}{}.{}", what.owner == net::PropOwner::Player ? "p" : "m", what.ownerId,
                           what.part);
    }
    return "?";
}

void PropTrace::placed(const bangers::BangerSet& set) {
    const std::size_t n = placedProps(set);
    for (std::size_t i = 0; i < n; ++i) {
        const auto& inst = set.instances()[i];
        const Vec3& p = inst.ground.m3;
        m_net.traceLine(std::format("p {} {} {:.2f} {:.2f} {:.2f}", i, inst.model, p.x, p.y, p.z));
    }
}

void PropTrace::knock(double t, std::size_t prop, std::string_view model, std::string_view cause) {
    m_net.traceLine(std::format("k {:.0f} {} {} {}", t, prop, model, cause));
}

void PropTrace::undo(double t, std::size_t prop) { m_net.traceLine(std::format("x {:.0f} {}", t, prop)); }

void PropTrace::impact(double t, std::string_view car, std::string_view cause, float value, float damage) {
    m_net.traceLine(std::format("i {:.0f} {} {} {:.1f} {:.1f}", t, car, cause, value, damage));
}

bool PropTrace::tick(double t) {
    constexpr double kTick = 250.0;
    if (m_next < 0.0)
        m_next = std::ceil(t / kTick) * kTick;
    if (t < m_next)
        return false;
    m_net.traceLine(std::format("t {:.0f} {:.1f}", m_next, t));
    m_next += kTick * (std::floor((t - m_next) / kTick) + 1.0);
    return true;
}

void PropTrace::broken(const bangers::BangerSet& set) {
    std::string line = "b";
    const std::size_t n = placedProps(set);
    for (std::size_t i = 0; i < n; ++i)
        if (!set.standing(i))
            line += std::format(" {}", i);
    m_net.traceLine(line);
}

void PropTrace::shown(const net::PropDescriptor& what, const Mat34& m, bool moving, bool local) {
    m_net.traceLine(std::format("h {} {:.3f} {:.3f} {:.3f} {} {}", describe(what), m.m3.x, m.m3.y, m.m3.z,
                                moving ? 1 : 0, local ? 1 : 0));
}

void PropTrace::car(std::string_view name, float level, float dents) {
    m_net.traceLine(std::format("d {} {:.4f} {:.0f}", name, level, dents));
}

// --- NetProps ------------------------------------------------------------------------------------

NetProps::NetProps() = default;
NetProps::~NetProps() = default;

void NetProps::setup(NetGame& net, bangers::BangerSet& set,
                     std::function<bool(const phys::Instance&)> localToucher, CarOf carOf) {
    m_set = &set;
    m_carOf = std::move(carOf);
    m_catalog = propCatalog(set);
    m_trace = PropTrace::of(net);
    if (m_trace)
        m_trace->placed(set);
    set.recordKnocks(true);
    const std::size_t placed = placedProps(set);
    if (const char* mode = std::getenv("OPENMM2_NETPROPS"); mode && std::string_view(mode) == "local") {
        log::info("race: props, each machine its own (OPENMM2_NETPROPS=local; {} placed, catalog {:08x})",
                  placed, m_catalog);
        return;
    }
    // OpenMM2: a pile-up of more knocks than MM2's ring of 40 holds (the
    // oldest still moving, or knocked in the last 10 s) grows it, up to 40 a
    // player (inferred: a player's knocks then last about as long as in
    // single player).
    const int players = static_cast<int>(std::max<std::size_t>(2, net.players().size()));
    set.setRingGrowth(std::min(bangers::BangerSet::kMaxHit * players, static_cast<int>(net::kMaxPropSlots)),
                      kRingBusySeconds);
    if (net.isHost()) {
        m_host.emplace();
    } else {
        m_client.emplace(m_catalog);
        set.setReplica(std::move(localToucher));
    }
    log::info("race: props, {} ({} placed, catalog {:08x})", net.isHost() ? "host" : "client", placed,
              m_catalog);
}

void NetProps::beforeStep(NetGame& net, std::span<const NetGameEvent> events, double now,
                          const PropClient::CarPartResolver& resolve) {
    auto messages = net.takePropStates();
    auto full = net.takePropFull();
    if (!m_client || !m_set)
        return;
    for (const auto& msg : messages)
        m_client->receive(msg, net.frameTime());
    for (const auto& msg : full)
        m_client->receiveFull(msg);
    for (const auto& ev : events) {
        if (static_cast<std::uint16_t>(ev.type) != net::kPropKnocksEvent || ev.from != net::kHostPlayerId)
            continue;
        if (const auto knocks = ev.as<net::PropKnocksEvent>())
            m_client->receiveKnocks(*knocks);
    }
    m_client->update(*m_set, now, resolve, m_hostCar, m_ownCarAt ? m_ownCarAt() : std::nullopt);
}

void NetProps::sendFull(NetGame& net, std::uint8_t id, std::uint32_t time, const phys::Body& car) {
    if (!m_host || !m_set)
        return;
    for (const auto& msg : m_host->buildFull(*m_set, id, time, car.ics.matrix.m3, &car))
        m_fullBytes += net.sendPropFull(id, msg);
}

void NetProps::fullCompanions(std::uint32_t time, double now, std::vector<CarPrediction::Companion>& out,
                              std::vector<const phys::Body*>& bodies, phys::Body& car,
                              const Recorded& recorded) {
    if (!m_client || !m_set)
        return;
    bangers::BangerSet* set = m_set;
    const CarPrediction::Options tolerances{};
    for (const auto& piece : m_client->fullPieces(*set, time, now, &car)) {
        phys::Body* body = set->simulate(piece.instance);
        if (!body)
            continue;
        CarPrediction::Companion c;
        c.body = body;
        // (After the car's own state: the piece may have pushed it hardest.)
        c.rebase = [set, piece, body, &car] {
            set->setBodyState(piece.instance, piece.state, piece.pusher);
            if (piece.pushedCar)
                car.collider.lastMaxPusher = body->collider.key();
        };
        c.position = piece.state.matrix.m3;
        c.velocity = piece.state.linearVelocity;
        // As the car's own state is compared (CarPrediction::acknowledge).
        if (const auto had = recorded ? recorded(body) : std::nullopt) {
            const Mat34& a = had->first;
            const Mat34& b = piece.state.matrix;
            float rotation = 0.0f;
            for (const auto& [u, v] : {std::pair{a.m0, b.m0}, std::pair{a.m1, b.m1}, std::pair{a.m2, b.m2}})
                rotation =
                    std::max({rotation, std::abs(u.x - v.x), std::abs(u.y - v.y), std::abs(u.z - v.z)});
            c.differs = a.m3.dist(b.m3) > tolerances.positionTolerance ||
                        (had->second - piece.state.linearVelocity).mag() > tolerances.velocityTolerance ||
                        rotation > tolerances.rotationTolerance;
        }
        out.push_back(std::move(c));
        bodies.push_back(body);
    }
}

void NetProps::afterReplay(std::uint32_t time) {
    if (m_client)
        m_client->forgetFull(time);
}

void NetProps::sendCatchUps(NetGame& net, std::uint32_t time) {
    for (const auto& p : net.players()) {
        if (p.id == net.localId() || !net.playerLoaded(p.id) || m_caughtUp.contains(p.id))
            continue;
        m_caughtUp.insert(p.id);
        for (auto& payload : PropHost::catchUp(*m_set, time)) {
            m_eventBytes += payload.size();
            ++m_sentEvents;
            net.sendEvent(net::kPropKnocksEvent, std::move(payload), p.id);
        }
    }
}

void NetProps::afterStep(NetGame& net, double now, std::uint64_t nowMs, const Classifier& classify) {
    m_traced = false;
    if (!m_set)
        return;
    const auto knocks = m_set->takeKnocks();
    if (m_trace)
        for (const auto& k : knocks)
            m_trace->knock(now, k.prop, m_set->instances()[k.prop].model, classify ? classify(k.by) : "?");
    const auto time = static_cast<std::uint32_t>(std::max(0.0, now));
    if (m_host) {
        m_host->knocked(knocks, time);
        for (auto& payload : m_host->takeKnockEvents()) {
            m_eventBytes += payload.size();
            ++m_sentEvents;
            net.sendEvent(net::kPropKnocksEvent, std::move(payload));
        }
        sendCatchUps(net, time);
        std::vector<PropHost::Viewer> viewers;
        for (const auto& p : net.players())
            if (p.id != net.localId() && net.playerLoaded(p.id))
                viewers.push_back({p.id, m_carOf ? m_carOf(p.id) : std::nullopt});
        for (const auto& [id, msg] : m_host->build(*m_set, time, nowMs, m_catalog, viewers)) {
            const std::size_t bytes = net.sendPropState(id, msg);
            m_sentBytes += bytes;
            m_host->stats().bytes += bytes;
            ++m_sentMessages;
        }
    }
    if (m_client) {
        m_client->predicted(knocks, now, net.localId(), m_carOfToucher);
        for (const auto& [prop, undone] : m_client->takeApplied()) {
            if (!m_trace)
                continue;
            if (undone)
                m_trace->undo(now, prop);
            else
                m_trace->knock(now, prop, m_set->instances()[prop].model, "host");
        }
    }
    if (m_trace && m_trace->tick(now)) {
        m_traced = true;
        m_trace->broken(*m_set);
        if (m_client) {
            for (const auto& s : m_client->shown(*m_set))
                m_trace->shown(s.what, s.matrix, s.moving, s.local);
        } else {
            const auto& instances = m_set->instances();
            for (std::size_t i = 0; i < instances.size(); ++i) {
                const auto& inst = instances[i];
                if (!inst.everHit || inst.state == bangers::BangerSet::State::Gone)
                    continue;
                if (const auto d = describeHit(inst))
                    m_trace->shown(*d, inst.matrix, m_set->moving(i), true);
            }
        }
    }
    logStats(net, nowMs);
}

void NetProps::logStats(NetGame& net, std::uint64_t nowMs) {
    if (m_statsAt == 0) {
        m_statsAt = nowMs;
        return;
    }
    if (nowMs - m_statsAt < 10000)
        return;
    const double seconds = static_cast<double>(nowMs - m_statsAt) / 1000.0;
    m_statsAt = nowMs;
    if (m_host) {
        auto& s = m_host->stats();
        const std::size_t clients = std::max<std::size_t>(1, net.players().size() - 1);
        if (s.messages > 0 || m_sentEvents > 0)
            log::info("netprops: host sent {:.1f} messages/s ({:.0f} bytes/s to each of {} clients, {:.1f} "
                      "slots and {:.1f} states a message, {:.0f}% of them near; {} deferred), ring {}, {} "
                      "knocks in {} events ({} bytes); in full {:.0f} bytes/s, {} pieces",
                      static_cast<double>(s.messages) / seconds / static_cast<double>(clients),
                      static_cast<double>(m_sentBytes) / seconds / static_cast<double>(clients), clients,
                      s.messages ? static_cast<double>(s.slots) / static_cast<double>(s.messages) : 0.0,
                      s.messages ? static_cast<double>(s.fullStates) / static_cast<double>(s.messages) : 0.0,
                      s.fullStates
                          ? 100.0 * static_cast<double>(s.nearStates) / static_cast<double>(s.fullStates)
                          : 0.0,
                      s.deferred, m_set->ringSize(), s.knocks, m_sentEvents, m_eventBytes,
                      static_cast<double>(m_fullBytes) / seconds / static_cast<double>(clients),
                      s.fullPieces);
        s = {};
        m_sentBytes = m_sentMessages = m_sentEvents = m_eventBytes = m_fullBytes = 0;
    }
    if (m_client) {
        const auto& s = m_client->stats();
        log::info("netprops: client {} messages ({} late, {} refused), host knocks {}, predicted {} "
                  "(confirmed {}, undone {}, {} of them early), hand-overs {}, orphans {}, delay {:.0f} ms",
                  s.messages, s.outdated, s.refused, s.knocks, s.predicted, s.confirmed, s.undone,
                  s.undoneEarly, s.handovers, s.orphans, m_client->delayMs());
    }
}

} // namespace mm2::game
