// A network race's props as the race screen runs them (OpenMM2; see
// NetProps.h).

#include "game/net/NetProps.h"

#include "core/Log.h"
#include "game/net/NetGame.h"
#include "net/PropState.h"

#include <cmath>
#include <cstdlib>
#include <format>

namespace mm2::game {

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

void PropTrace::impact(double t, std::string_view cause, float value, float damage) {
    m_net.traceLine(std::format("i {:.0f} {} {:.1f} {:.1f}", t, cause, value, damage));
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
                     std::function<bool(const phys::Instance&)> localToucher) {
    m_set = &set;
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
    if (!m_client || !m_set)
        return;
    for (const auto& msg : messages)
        m_client->receive(msg, net.frameTime());
    for (const auto& ev : events) {
        if (static_cast<std::uint16_t>(ev.type) != net::kPropKnocksEvent || ev.from != net::kHostPlayerId)
            continue;
        if (const auto knocks = ev.as<net::PropKnocksEvent>())
            m_client->receiveKnocks(*knocks);
    }
    m_client->update(*m_set, now, resolve);
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
        if (const auto msg = m_host->build(*m_set, time, nowMs, m_catalog)) {
            for (const auto& p : net.players()) {
                if (p.id == net.localId() || !net.playerLoaded(p.id))
                    continue;
                const std::size_t bytes = net.sendPropState(p.id, *msg);
                m_sentBytes += bytes;
                m_host->stats().bytes += bytes;
                ++m_sentMessages;
            }
        }
    }
    if (m_client) {
        m_client->predicted(knocks, now);
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
                      "slots and {:.1f} states a message), {} knocks in {} events ({} bytes)",
                      static_cast<double>(s.messages) / seconds,
                      static_cast<double>(m_sentBytes) / seconds / static_cast<double>(clients), clients,
                      s.messages ? static_cast<double>(s.slots) / static_cast<double>(s.messages) : 0.0,
                      s.messages ? static_cast<double>(s.fullStates) / static_cast<double>(s.messages) : 0.0,
                      s.knocks, m_sentEvents, m_eventBytes);
        s = {};
        m_sentBytes = m_sentMessages = m_sentEvents = m_eventBytes = 0;
    }
    if (m_client) {
        const auto& s = m_client->stats();
        log::info("netprops: client {} messages ({} late, {} refused), host knocks {}, predicted {} "
                  "(confirmed {}, undone {}), hand-overs {}, orphans {}, delay {:.0f} ms",
                  s.messages, s.outdated, s.refused, s.knocks, s.predicted, s.confirmed, s.undone,
                  s.handovers, s.orphans, m_client->delayMs());
    }
}

} // namespace mm2::game
