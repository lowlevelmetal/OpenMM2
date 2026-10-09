// The host's props in a network race (OpenMM2; see PropSync.h and
// docs/multiplayer.md, "Props").

#include "game/net/PropSync.h"

#include "core/Log.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <cmath>

namespace mm2::game {
namespace {

using bangers::BangerSet;

constexpr std::uint32_t kFnvBasis = 2166136261u;
constexpr std::uint32_t kFnvPrime = 16777619u;

void fnv(std::uint32_t& h, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        h ^= (v >> (i * 8)) & 0xFFu;
        h *= kFnvPrime;
    }
}

std::int32_t centimetres(float v) {
    if (!std::isfinite(v))
        return 0;
    return static_cast<std::int32_t>(std::lround(std::clamp(v, -2.0e7f, 2.0e7f) * 100.0f));
}

// VehicleSnapshot::flags bit for a prop the host simulates.
constexpr std::uint8_t kMovingFlag = 1;

Mat34 matrixOf(const net::VehicleSnapshot& s) { return s.orientation.toMatrix(s.position); }

Mat34 blendMatrix(const Mat34& from, const Mat34& to, float t) {
    const Quat q = Quat::slerp(Quat::fromMatrix(from), Quat::fromMatrix(to), t);
    return q.toMatrix(lerp(from.m3, to.m3, t));
}

constexpr std::uint32_t kTagMarker = 0x80000000u;

// What a slot of the host's ring holds this frame (PropHost::build).
struct RingSlot {
    std::optional<net::PropDescriptor> what;
    bool moving = false;
    bool slow = false;
};

} // namespace

std::size_t placedProps(const BangerSet& set) {
    std::size_t n = 0;
    for (const auto& inst : set.instances()) {
        if (inst.everHit)
            break;
        ++n;
    }
    return n;
}

std::uint32_t propCatalog(const BangerSet& set) {
    std::uint32_t h = kFnvBasis;
    const std::size_t n = placedProps(set);
    for (std::size_t i = 0; i < n; ++i) {
        const auto& inst = set.instances()[i];
        for (const char c : str::lower(inst.model)) {
            h ^= static_cast<std::uint8_t>(c);
            h *= kFnvPrime;
        }
        fnv(h, static_cast<std::uint32_t>(inst.paint));
        fnv(h, static_cast<std::uint32_t>(centimetres(inst.ground.m3.x)));
        fnv(h, static_cast<std::uint32_t>(centimetres(inst.ground.m3.y)));
        fnv(h, static_cast<std::uint32_t>(centimetres(inst.ground.m3.z)));
    }
    fnv(h, static_cast<std::uint32_t>(n));
    return h;
}

std::uint32_t carPartTag(net::PropOwner owner, int ownerId, int part, int paint) {
    const auto o = static_cast<std::uint32_t>(owner) & 0x1u;
    const auto id = static_cast<std::uint32_t>(std::clamp(ownerId, 0, 255));
    const auto p = static_cast<std::uint32_t>(std::clamp(part, 0, 255));
    const auto c = static_cast<std::uint32_t>(std::clamp(paint, 0, 255));
    return kTagMarker | (o << 24) | (id << 16) | (p << 8) | c;
}

std::optional<net::PropDescriptor> carPartOfTag(std::uint32_t tag) {
    if (!(tag & kTagMarker))
        return std::nullopt;
    net::PropDescriptor d;
    d.source = net::PropSource::CarPart;
    d.owner = static_cast<net::PropOwner>((tag >> 24) & 0x1u);
    const auto id = (tag >> 16) & 0xFFu, part = (tag >> 8) & 0xFFu, paint = tag & 0xFFu;
    if (id >= net::kMaxPropOwners || part >= net::kMaxPropCarParts || paint > net::kMaxPropPaint)
        return std::nullopt;
    d.ownerId = static_cast<std::uint8_t>(id);
    d.part = static_cast<std::uint8_t>(part);
    d.paint = static_cast<std::uint8_t>(paint);
    return d;
}

std::optional<net::PropDescriptor> describeHit(const BangerSet::Instance& inst) {
    if (inst.source >= 0) {
        if (static_cast<std::uint32_t>(inst.source) >= net::kMaxPropIds)
            return std::nullopt;
        net::PropDescriptor d;
        d.prop = static_cast<std::uint16_t>(inst.source);
        if (inst.part < 0) {
            d.source = net::PropSource::Prop;
        } else {
            if (static_cast<std::uint32_t>(inst.part) >= net::kMaxPropParts)
                return std::nullopt;
            d.source = net::PropSource::PropPart;
            d.part = static_cast<std::uint8_t>(inst.part);
        }
        return d;
    }
    if (inst.tag != 0)
        return carPartOfTag(inst.tag);
    return std::nullopt;
}

// --- Host ----------------------------------------------------------------------------------------

void PropHost::knocked(std::span<const BangerSet::Knock> knocks, std::uint32_t time) {
    for (const auto& k : knocks) {
        if (k.prop >= net::kMaxPropIds)
            continue; // beyond what the protocol names (no retail city has that many)
        if (m_pending.empty())
            m_pendingTime = time;
        const std::uint32_t delay = time >= m_pendingTime ? time - m_pendingTime : 0;
        m_pending.push_back(
            {static_cast<std::uint16_t>(k.prop), static_cast<std::uint16_t>(std::min(delay, 65535u))});
        ++m_stats.knocks;
    }
}

std::vector<std::vector<std::byte>> PropHost::takeKnockEvents() {
    // 31 bits a knock: 250 stay well within the 1 KiB payload.
    constexpr std::size_t kPerEvent = 250;
    std::vector<std::vector<std::byte>> out;
    for (std::size_t first = 0; first < m_pending.size(); first += kPerEvent) {
        net::PropKnocksEvent e;
        e.time = m_pendingTime;
        const std::size_t last = std::min(m_pending.size(), first + kPerEvent);
        e.knocks.assign(m_pending.begin() + static_cast<std::ptrdiff_t>(first),
                        m_pending.begin() + static_cast<std::ptrdiff_t>(last));
        out.push_back(net::encodePayload(e));
    }
    m_pending.clear();
    return out;
}

std::vector<std::vector<std::byte>> PropHost::catchUp(const BangerSet& set, std::uint32_t time) {
    // 15 bits a knock: 500 stay within the 1 KiB payload.
    constexpr std::size_t kPerEvent = 500;
    std::vector<net::PropKnock> all;
    const std::size_t n = std::min<std::size_t>(placedProps(set), net::kMaxPropIds);
    for (std::size_t i = 0; i < n; ++i)
        if (!set.standing(i))
            all.push_back({static_cast<std::uint16_t>(i), 0});
    std::vector<std::vector<std::byte>> out;
    for (std::size_t first = 0; first < all.size(); first += kPerEvent) {
        net::PropKnocksEvent e;
        e.time = time;
        e.catchUp = true;
        const std::size_t last = std::min(all.size(), first + kPerEvent);
        e.knocks.assign(all.begin() + static_cast<std::ptrdiff_t>(first),
                        all.begin() + static_cast<std::ptrdiff_t>(last));
        out.push_back(net::encodePayload(e));
    }
    return out;
}

std::optional<net::PropStateMsg> PropHost::build(const BangerSet& set, std::uint32_t time,
                                                 std::uint64_t nowMs, std::uint32_t catalog) {
    const auto& ring = set.ring();
    const std::size_t slots = std::min<std::size_t>(ring.size(), net::kMaxPropSlots);
    if (m_seen.size() < slots)
        m_seen.resize(slots);
    bool busy = !m_sentAny, creeping = false;
    std::vector<RingSlot> current(slots);
    for (std::size_t k = 0; k < slots; ++k) {
        const auto& inst = set.instances()[ring[k]];
        RingSlot& c = current[k];
        if (inst.state != BangerSet::State::Gone)
            c.what = describeHit(inst);
        c.moving = c.what && set.moving(ring[k]);
        if (c.moving)
            if (const phys::Body* body = set.body(ring[k]))
                c.slow = body->ics.linearVelocity.mag2() < m_options.slowSpeed * m_options.slowSpeed &&
                         body->ics.angularVelocity.mag2() < m_options.slowSpin * m_options.slowSpin;
        SlotSeen& seen = m_seen[k];
        const bool occupied = c.what.has_value();
        const std::uint32_t generation = set.generation(k);
        if (seen.generation != generation || seen.occupied != occupied || seen.moving != c.moving) {
            seen.generation = generation;
            seen.occupied = occupied;
            seen.moving = c.moving;
            seen.fresh = m_options.freshMessages;
        }
        if ((c.moving && !c.slow) || (occupied && seen.fresh > 0))
            busy = true;
        creeping = creeping || c.slow;
    }
    const std::uint32_t interval = busy ? m_options.intervalMs
                                   : creeping ? m_options.slowIntervalMs
                                              : m_options.idleIntervalMs;
    if (m_sentAny && nowMs - m_lastSent < interval)
        return std::nullopt;

    net::PropStateMsg msg;
    msg.time = time;
    msg.catalog = catalog;
    const int rotation = std::max(1, m_options.restRotation);
    for (std::size_t k = 0; k < slots; ++k) {
        const RingSlot& c = current[k];
        if (!c.what)
            continue;
        SlotSeen& seen = m_seen[k];
        const auto& inst = set.instances()[ring[k]];
        net::PropSlot p;
        p.slot = static_cast<std::uint8_t>(k);
        p.generation = static_cast<std::uint8_t>(seen.generation % net::kPropGenerations);
        // A creeping prop's state every fourth message while others move
        // fast (every one at the slow interval).
        const bool slowDue = !busy || (k + m_sequence) % 4 == 0;
        p.hasState = (c.moving && (!c.slow || slowDue)) || seen.fresh > 0 ||
                     static_cast<int>(k % static_cast<std::size_t>(rotation)) ==
                         static_cast<int>(m_sequence % static_cast<std::uint32_t>(rotation));
        if (seen.fresh > 0)
            --seen.fresh;
        if (p.hasState) {
            p.what = *c.what;
            p.moving = c.moving;
            p.position = inst.matrix.m3;
            p.orientation = Quat::fromMatrix(inst.matrix);
            if (c.moving)
                if (const phys::Body* body = set.body(ring[k])) {
                    p.velocity = body->ics.linearVelocity;
                    p.angularVelocity = body->ics.angularVelocity;
                }
            ++m_stats.fullStates;
        }
        msg.slots.push_back(p);
    }
    m_lastSent = nowMs;
    m_sentAny = true;
    ++m_sequence;
    ++m_stats.messages;
    m_stats.slots += msg.slots.size();
    return msg;
}

// --- Client --------------------------------------------------------------------------------------

PropClient::PropClient(std::uint32_t catalog, const Options& options)
    : m_options(options), m_catalog(catalog), m_delay(options.initialDelayMs) {}

void PropClient::receive(const net::PropStateMsg& msg, double arrival) {
    ++m_stats.messages;
    if (msg.catalog != m_catalog && !m_mismatch) {
        m_mismatch = true;
        log::warn("netprops: the host's props differ from this machine's (catalog {:08x}, ours {:08x}): only "
                  "the cars' parts follow the host",
                  msg.catalog, m_catalog);
    }
    m_lateness.emplace_back(arrival, arrival - static_cast<double>(msg.time));
    while (m_lateness.size() > 256)
        m_lateness.pop_front();
    const bool newer = !m_any || msg.time > m_latest;
    if (!newer)
        ++m_stats.outdated;
    std::set<int> listed;
    for (const net::PropSlot& p : msg.slots) {
        const int k = p.slot;
        if (!listed.insert(k).second) {
            ++m_stats.refused; // a repeated slot
            continue;
        }
        if (p.hasState && m_mismatch && p.what.source != net::PropSource::CarPart) {
            ++m_stats.refused;
            continue;
        }
        Generation* g = nullptr;
        if (newer) {
            Slot& slot = m_slots[k];
            Generation* last = slot.generations.empty() ? nullptr : &slot.generations.back();
            if (!last || last->generation != p.generation || last->goneAt) {
                if (last && !last->goneAt)
                    last->goneAt = msg.time;
                slot.generations.emplace_back();
                last = &slot.generations.back();
                last->generation = p.generation;
                last->firstTime = msg.time;
                while (slot.generations.size() > 4)
                    slot.generations.pop_front();
            }
            g = last;
        } else {
            // An older message only adds states to the generation it saw.
            const auto it = m_slots.find(k);
            if (it == m_slots.end())
                continue;
            for (auto& gen : it->second.generations)
                if (gen.generation == p.generation && (!gen.goneAt || msg.time < *gen.goneAt))
                    g = &gen;
            if (!g)
                continue;
            g->firstTime = std::min(g->firstTime, msg.time);
        }
        if (!p.hasState)
            continue;
        if (g->described && !(g->what == p.what)) {
            ++m_stats.refused; // the same generation holds something else: not from an honest host
            continue;
        }
        g->what = p.what;
        g->described = true;
        net::VehicleSnapshot s;
        s.time = msg.time;
        s.position = p.position;
        s.orientation = p.orientation;
        s.linearVelocity = p.velocity;
        s.angularVelocity = p.angularVelocity;
        s.flags = p.moving ? kMovingFlag : 0;
        g->buffer.push(s);
    }
    if (newer) {
        for (auto& [k, slot] : m_slots)
            if (!listed.contains(k) && !slot.generations.empty() && !slot.generations.back().goneAt)
                slot.generations.back().goneAt = msg.time;
        m_latest = msg.time;
        m_any = true;
    }
}

void PropClient::receiveKnocks(const net::PropKnocksEvent& event) {
    if (m_mismatch)
        return;
    for (const auto& k : event.knocks) {
        if (m_knocks.size() >= net::kMaxPropIds)
            m_knocks.pop_front();
        m_knocks.push_back({k.prop, event.time + k.delay, event.catchUp});
    }
}

void PropClient::predicted(std::span<const BangerSet::Knock> knocks, double now) {
    // This machine's car broke props loose: shown at once, until the host
    // confirms or corrects it.
    for (const auto& k : knocks) {
        if (m_mismatch || m_hostBroken.contains(k.prop))
            continue;
        if (m_predicted.emplace(k.prop, Prediction{now, false}).second)
            ++m_stats.predicted;
    }
}

void PropClient::updateDelay(double now) {
    while (!m_lateness.empty() && m_lateness.front().first < now - 3000.0)
        m_lateness.pop_front();
    double target = m_options.initialDelayMs;
    if (!m_lateness.empty()) {
        double worst = 0.0;
        for (const auto& [at, late] : m_lateness)
            worst = std::max(worst, late);
        // A moving prop's next state comes a send interval later.
        target = worst + 55.0;
    }
    target = std::clamp(target, m_options.minDelayMs, m_options.maxDelayMs);
    if (m_lastUpdate < 0.0) {
        m_delay = target;
    } else {
        const double dt = std::max(0.0, now - m_lastUpdate);
        m_delay = target > m_delay ? std::min(target, m_delay + dt * 0.25)
                                   : std::max(target, m_delay - dt * 0.02);
    }
    m_lastUpdate = now;
}

const PropClient::Generation* PropClient::shownAt(const Slot& slot, double time) {
    for (auto it = slot.generations.rbegin(); it != slot.generations.rend(); ++it) {
        if (static_cast<double>(it->firstTime) > time)
            continue;
        if (it->goneAt && time >= static_cast<double>(*it->goneAt))
            return nullptr; // the newest one begun by then is gone by then
        return it->described ? &*it : nullptr;
    }
    return nullptr;
}

std::optional<BangerSet::MirrorSpec> PropClient::resolve(const BangerSet& set,
                                                         const net::PropDescriptor& what,
                                                         const CarPartResolver& carParts) const {
    if (what.source == net::PropSource::CarPart)
        return carParts ? carParts(what) : std::nullopt;
    if (what.prop >= placedProps(set))
        return std::nullopt;
    const auto& placed = set.instances()[what.prop];
    BangerSet::MirrorSpec spec;
    spec.model = placed.model;
    spec.paint = placed.paint;
    spec.source = what.prop;
    if (what.source == net::PropSource::PropPart) {
        spec.data = set.dataLibrary().part(placed.model, what.part);
        spec.part = what.part;
    } else {
        spec.data = placed.data;
    }
    if (!spec.data)
        return std::nullopt;
    return spec;
}

std::optional<std::size_t> PropClient::findStandIn(const BangerSet& set,
                                                   const net::PropDescriptor& what) const {
    for (const std::size_t i : set.ring()) {
        const auto& inst = set.instances()[i];
        if (inst.state == BangerSet::State::Gone)
            continue;
        if (const auto d = describeHit(inst); d && *d == what)
            return i;
    }
    return std::nullopt;
}

void PropClient::applyKnock(BangerSet& set, std::size_t prop) {
    if (prop >= placedProps(set))
        return;
    if (m_hostBroken.insert(prop).second) {
        ++m_stats.knocks;
        m_applied.emplace_back(prop, false);
    }
    set.breakPlaced(prop);
    if (const auto it = m_predicted.find(prop); it != m_predicted.end()) {
        ++m_stats.confirmed;
        m_predicted.erase(it);
    }
}

void PropClient::update(BangerSet& set, double now, const CarPartResolver& carParts) {
    updateDelay(now);
    const double render = now - m_delay;
    const double drawn = render - m_options.drawBehindMs;
    if (m_mismatch && set.replica())
        set.clearReplica(); // simulate every contact here, as MM2 does

    // The host's knocks, as the props are shown at their time (at once for
    // a catch-up, or when the prop's pieces already show).
    std::set<std::size_t> inSlots;
    for (const auto& [k, slot] : m_slots)
        if (const Generation* g = shownAt(slot, render); g && g->what.source != net::PropSource::CarPart)
            inSlots.insert(g->what.prop);
    for (auto it = m_knocks.begin(); it != m_knocks.end();) {
        if (it->now || static_cast<double>(it->time) <= render || inSlots.contains(it->prop) ||
            static_cast<double>(it->time) > now + 10000.0) {
            applyKnock(set, it->prop);
            it = m_knocks.erase(it);
        } else {
            ++it;
        }
    }

    // Predictions the host has not made in time are undone.
    std::set<std::size_t> described;
    for (const auto& [k, slot] : m_slots)
        for (const auto& g : slot.generations)
            if (g.described && g.what.source != net::PropSource::CarPart)
                described.insert(g.what.prop);
    for (auto it = m_predicted.begin(); it != m_predicted.end();) {
        const bool coming =
            described.contains(it->first) ||
            std::ranges::any_of(m_knocks, [&](const PendingKnock& k) { return k.prop == it->first; });
        if (coming) {
            it->second.confirmed = true;
            ++it;
            continue;
        }
        if (!it->second.confirmed && now - it->second.at >= m_options.predictTimeoutMs) {
            set.restoreStanding(it->first);
            ++m_stats.undone;
            m_applied.emplace_back(it->first, true);
            log::info("netprops: the host did not knock prop {} ({}): standing again", it->first,
                      set.instances()[it->first].model);
            it = m_predicted.erase(it);
            continue;
        }
        ++it;
    }

    // This machine's own pieces (its ring): when each was made and came to rest.
    const auto& ring = set.ring();
    for (std::size_t k = 0; k < ring.size(); ++k) {
        LocalPiece& piece = m_pieces[k];
        const std::uint32_t generation = set.generation(k);
        if (piece.generation != generation)
            piece = {generation, now, std::nullopt};
        const bool resting = set.instances()[ring[k]].state != BangerSet::State::Gone && !set.body(ring[k]);
        if (!resting)
            piece.restedAt.reset();
        else if (!piece.restedAt)
            piece.restedAt = now;
    }

    // The host's ring.
    std::set<std::size_t> standIns;
    for (auto& [k, slot] : m_slots) {
        const auto index = static_cast<std::size_t>(k);
        while (slot.generations.size() > 1 && slot.generations.front().goneAt &&
               static_cast<double>(*slot.generations.front().goneAt) <= render)
            slot.generations.pop_front();
        Generation* g = shownAt(slot, render);
        const std::size_t mirror = set.mirror(index);
        const auto spec = g ? resolve(set, g->what, carParts) : std::nullopt;
        if (!g || !spec) {
            set.hideMirror(index);
            slot.local = false;
            slot.blendFrom.reset();
            slot.standIn.reset();
            continue;
        }
        net::VehicleSnapshot s, sd;
        if (g->buffer.sample(render, s) == net::SnapshotBuffer::Result::Empty) {
            set.hideMirror(index);
            continue;
        }
        if (g->buffer.sample(std::max(drawn, static_cast<double>(g->firstTime)), sd) ==
            net::SnapshotBuffer::Result::Empty)
            sd = s;
        const bool hostResting = (s.flags & kMovingFlag) == 0;
        // A piece this machine simulates (its car knocked the prop, or threw
        // the part, first) stands in for the host's until both are at rest.
        if (slot.standIn) {
            const auto& inst = set.instances()[*slot.standIn];
            const auto d = describeHit(inst);
            if (inst.state == BangerSet::State::Gone || !d || !(*d == g->what))
                slot.standIn.reset();
        }
        if (!slot.standIn)
            if (const auto i = findStandIn(set, g->what); i && !standIns.contains(*i)) {
                slot.standIn = i;
                slot.standInSince = now;
            }
        if (slot.standIn) {
            standIns.insert(*slot.standIn);
            const bool resting = !set.body(*slot.standIn);
            if ((!resting || !hostResting) && now - slot.standInSince < m_options.handoverMs) {
                set.hideMirror(index);
                continue;
            }
            const auto& inst = set.instances()[*slot.standIn];
            slot.blendFrom = inst.drawn ? *inst.drawn : inst.matrix;
            slot.blendStart = now;
            set.detachHit(*slot.standIn);
            standIns.erase(*slot.standIn);
            slot.standIn.reset();
            ++m_stats.handovers;
        }
        // This machine's car pushed the host's piece: simulated here, then
        // left where it stopped until the host has pushed it too and it rests
        // there (or the prediction's time is up).
        const auto& mine = set.instances()[mirror];
        if (mine.active >= 0 && slot.local && now - slot.localSince >= m_options.handoverMs) {
            // Still moving here after that long (creeping down a hill, which
            // phSleep may never stop): back to the host's.
            set.releaseMirror(index);
            slot.local = false;
            slot.blendFrom = mine.matrix;
            slot.blendStart = now;
            ++m_stats.handovers;
        } else if (mine.active >= 0) {
            if (!slot.local)
                slot.localSince = now;
            slot.local = true;
            slot.holdUntil = now + m_options.predictTimeoutMs;
            slot.hostMoved = false;
            continue;
        }
        if (slot.local) {
            if (!hostResting)
                slot.hostMoved = true;
            const bool settled = slot.hostMoved && hostResting;
            if (mine.state != BangerSet::State::Gone && now < slot.holdUntil && !settled)
                continue;
            slot.local = false;
            if (mine.state != BangerSet::State::Gone) {
                slot.blendFrom = mine.matrix;
                slot.blendStart = now;
                ++m_stats.handovers;
            }
        }
        Mat34 matrix = matrixOf(s);
        Mat34 draw = matrixOf(sd);
        if (slot.blendFrom) {
            const double t = (now - slot.blendStart) / m_options.blendMs;
            if (t >= 1.0) {
                slot.blendFrom.reset();
            } else {
                const float w = static_cast<float>(std::max(0.0, t));
                matrix = blendMatrix(*slot.blendFrom, matrix, w);
                draw = blendMatrix(*slot.blendFrom, draw, w);
            }
        }
        set.showMirror(index, *spec, matrix, draw, s.linearVelocity, s.angularVelocity);
        g->buffer.prune(std::min(render, drawn));
    }

    // Pieces simulated here that the host's ring does not hold: gone once at
    // rest (the host threw them differently, or its ring has moved on).
    for (std::size_t k = 0; k < ring.size(); ++k) {
        const std::size_t i = ring[k];
        const auto& inst = set.instances()[i];
        if (inst.state == BangerSet::State::Gone || standIns.contains(i))
            continue;
        const LocalPiece& piece = m_pieces[k];
        // At rest that long after it was made; or, one that never rests
        // (creeping), once a stand-in would have handed over.
        const bool due = piece.restedAt ? now - piece.since >= m_options.orphanMs
                                        : now - piece.since >= m_options.orphanMs + m_options.handoverMs;
        if (!due)
            continue;
        if (inst.source >= 0 && m_predicted.contains(static_cast<std::size_t>(inst.source)) &&
            !m_predicted[static_cast<std::size_t>(inst.source)].confirmed)
            continue; // undone with its prop, or confirmed later
        set.detachHit(i);
        ++m_stats.orphans;
    }
}

std::vector<PropClient::Shown> PropClient::shown(const BangerSet& set) const {
    std::vector<Shown> out;
    const auto& instances = set.instances();
    for (std::size_t i = 0; i < instances.size(); ++i) {
        const auto& inst = instances[i];
        if (!inst.everHit || inst.state == BangerSet::State::Gone)
            continue;
        const auto d = describeHit(inst);
        if (!d)
            continue;
        out.push_back(
            {*d, inst.drawn ? *inst.drawn : inst.matrix, set.moving(i), !inst.mirror || inst.active >= 0});
    }
    return out;
}

} // namespace mm2::game
