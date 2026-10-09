// The damage of the cars drawn from the network (OpenMM2). See DamageSync.h
// and docs/multiplayer.md, "Damage".
#include "game/net/DamageSync.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "game/net/NetGame.h"
#include "phys/Constants.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>

namespace mm2::game {
namespace {

// vehCarModel::Init's breakables in the order the managers hold them:
// manager A (vehBreakableMgr::Impact) the BREAK parts and the paint job's
// VARIANT, manager B (EjectOneshot) the wheels, hubs, fenders and engine.
constexpr std::array<std::string_view, net::kDamagePartCount> kParts = {
    "BREAK0", "BREAK1", "BREAK2", "BREAK3", "BREAK01", "BREAK12", "BREAK23", "BREAK03", "VARIANT", "WHL0",
    "WHL1",   "WHL2",   "WHL3",   "HUB0",   "HUB1",    "HUB2",    "HUB3",    "FNDR0",   "FNDR1",   "ENGINE"};
constexpr int kVariant = 8;
constexpr int kFirstWreckPart = 9;

bool finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

bool within(const Vec3& v, float range) {
    return finite(v) && std::abs(v.x) <= range && std::abs(v.y) <= range && std::abs(v.z) <= range;
}

std::uint8_t delayOf(std::uint32_t time, std::uint32_t base) {
    const auto d = static_cast<std::int32_t>(time - base);
    return static_cast<std::uint8_t>(std::clamp(d, 0, 255));
}

} // namespace

int damagePartIndex(std::string_view part) {
    const std::string name = str::upper(part);
    for (std::size_t i = 0; i < kParts.size(); ++i)
        if (i != kVariant && name == kParts[i])
            return static_cast<int>(i);
    if (name.size() > 7 && name.starts_with("VARIANT") &&
        std::all_of(name.begin() + 7, name.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return kVariant;
    return -1;
}

std::string damagePartName(int index, int paintjob) {
    if (index < 0 || index >= net::kDamagePartCount)
        return {};
    if (index == kVariant)
        return std::format("VARIANT{}", paintjob);
    return std::string(kParts[static_cast<std::size_t>(index)]);
}

bool damagePartIsWreckPart(int index) { return index >= kFirstWreckPart; }

net::DamageImpact damageImpactOf(const phys::CarImpact& impact, const phys::CarSim& car) {
    const Mat34 m = car.modelMatrix();
    const Vec3& n = impact.normal;
    net::DamageImpact d;
    d.point = impact.localPosition;
    d.normal = {n.dot(m.m0), n.dot(m.m1), n.dot(m.m2)};
    d.total = impact.total;
    d.speed = car.speed();
    d.sound = impact.sound ? impact.soundStrength : 0.0f;
    d.audioId = static_cast<std::uint16_t>(std::clamp(impact.audioId, 0, 1000));
    return d;
}

float damageFromFraction(const phys::CarDamageParams& params, float fraction) {
    if (!(fraction > 0.0f))
        return 0.0f;
    if (fraction >= 1.0f)
        return params.maxDamage;
    return params.medDamage + fraction * (params.maxDamage - params.medDamage);
}

// --- DamageRecorder ----------------------------------------------------------------------

DamageRecorder::DamageRecorder(std::uint16_t subject, const Options& options)
    : m_subject(subject), m_options(options) {
    m_batches.emplace_back();
}

void DamageRecorder::reset(std::uint32_t time) {
    ++m_epoch;
    m_count = 0;
    m_parts = 0;
    // A batch that only said "reset" (or nothing) is superseded: the newer
    // reset clears the car as well.
    const Batch& last = open();
    if (last.patches.empty() && !last.partsTime && last.impacts.empty())
        m_batches.pop_back();
    Batch b;
    b.epoch = m_epoch;
    b.resetTime = time;
    m_batches.push_back(std::move(b));
}

Vec3 DamageRecorder::patch(std::uint32_t time, const Vec3& point, std::uint32_t seed) {
    const Vec3 q = net::quantizeDamagePoint(point);
    if (m_count >= net::kMaxDamageRecord)
        return q;
    Batch& b = open();
    if (b.patches.empty())
        b.first = m_count;
    b.patches.push_back({time, {0, q, seed}});
    ++m_count;
    return q;
}

void DamageRecorder::part(std::uint32_t time, std::string_view name) {
    const int index = damagePartIndex(name);
    if (index < 0 || (m_parts & (1u << index)) != 0)
        return;
    m_parts |= 1u << index;
    Batch& b = open();
    b.parts = m_parts;
    b.partsTime = time;
}

void DamageRecorder::impact(std::uint32_t time, const net::DamageImpact& impact) {
    Batch& b = open();
    if (b.impacts.size() < net::kMaxDamageImpacts)
        b.impacts.push_back({time, impact});
}

std::vector<net::VehicleDamageEvent> DamageRecorder::take(std::uint64_t nowMs) {
    std::vector<net::VehicleDamageEvent> out;
    const bool due = !m_sentAny || nowMs - m_lastSent >= m_options.intervalMs;
    if (!due && m_batches.size() < 2)
        return out;
    // The sender's token bucket.
    if (m_tokens < 0.0)
        m_tokens = m_options.burst;
    else
        m_tokens = std::min(m_options.burst,
                            m_tokens + static_cast<double>(nowMs - m_tokensAt) * 0.001 * m_options.perSecond);
    m_tokensAt = nowMs;
    bool starved = false; // out of tokens: the rest waits for the next ones
    while (!m_batches.empty() && !starved) {
        Batch& b = m_batches.front();
        const bool isOpen = m_batches.size() == 1;
        if (b.empty()) {
            if (isOpen)
                break;
            m_batches.pop_front();
            continue;
        }
        if (isOpen && !due)
            break;
        // One event per kMaxDamagePatches patches; the reset, the impacts and
        // the newly broken parts go with the first.
        while (!b.empty()) {
            if (m_tokens < 1.0) {
                starved = true;
                break;
            }
            m_tokens -= 1.0;
            net::VehicleDamageEvent e;
            e.subject = m_subject;
            e.epoch = b.epoch;
            std::uint32_t base = 0;
            if (b.resetTime)
                base = *b.resetTime;
            else if (!b.patches.empty())
                base = b.patches.front().time;
            else if (!b.impacts.empty())
                base = b.impacts.front().time;
            else if (b.partsTime)
                base = *b.partsTime;
            e.time = base;
            e.first = static_cast<std::uint16_t>(b.first);
            const std::size_t n = std::min(b.patches.size(), net::kMaxDamagePatches);
            for (std::size_t i = 0; i < n; ++i) {
                net::DamagePatch p = b.patches[i].patch;
                p.delay = delayOf(b.patches[i].time, base);
                e.patches.push_back(p);
            }
            b.patches.erase(b.patches.begin(), b.patches.begin() + static_cast<std::ptrdiff_t>(n));
            b.first += static_cast<std::uint32_t>(n);
            for (const Hit& h : b.impacts) {
                net::DamageImpact m = h.impact;
                m.delay = delayOf(h.time, base);
                e.impacts.push_back(m);
            }
            b.impacts.clear();
            e.parts = b.parts;
            if (b.partsTime)
                e.partsDelay = delayOf(*b.partsTime, base);
            b.partsTime.reset();
            b.resetTime.reset();
            out.push_back(std::move(e));
        }
        if (isOpen || starved)
            break;
        m_batches.pop_front();
    }
    if (m_batches.empty()) {
        Batch b;
        b.epoch = m_epoch;
        b.first = m_count;
        b.parts = m_parts;
        m_batches.push_back(std::move(b));
    }
    // The open batch carries on from what was sent.
    Batch& b = open();
    if (b.patches.empty())
        b.first = m_count;
    b.parts = m_parts;
    if (!out.empty()) {
        m_lastSent = nowMs;
        m_sentAny = true;
    }
    return out;
}

// --- DamageReplica -----------------------------------------------------------------------

bool DamageReplica::receive(std::uint8_t from, const net::VehicleDamageEvent& e, double arrival) {
    ++m_stats.events;
    std::uint32_t key = 0;
    if (e.subject == net::kDamageOwnCar) {
        if (from >= net::kMaxPlayers) {
            ++m_stats.refused;
            return false;
        }
        key = playerKey(from);
    } else {
        // Only the host runs the shared police.
        if (from != net::kHostPlayerId || e.subject >= net::kMaxAmbientIds) {
            ++m_stats.refused;
            return false;
        }
        key = ambientKey(e.subject);
    }
    bool ok = e.patches.size() <= net::kMaxDamagePatches && e.impacts.size() <= net::kMaxDamageImpacts &&
              e.first + e.patches.size() <= net::kMaxDamageRecord &&
              (e.parts >> net::kDamagePartCount) == 0 && std::isfinite(arrival);
    for (const auto& p : e.patches)
        ok = ok && within(p.point, net::kDamagePointRange);
    for (const auto& m : e.impacts)
        ok = ok && within(m.point, net::kDamagePointRange) && within(m.normal, 1.0f) &&
             std::isfinite(m.total) && m.total >= 0.0f && std::isfinite(m.speed) && m.speed >= 0.0f &&
             m.speed <= net::kDamageMaxSpeed && std::isfinite(m.sound) && m.sound >= 0.0f &&
             m.audioId <= 1000;
    if (!ok) {
        ++m_stats.refused;
        return false;
    }
    auto it = m_records.find(key);
    if (it == m_records.end()) {
        if (m_records.size() >= kMaxRecords) {
            ++m_stats.refused;
            return false;
        }
        it = m_records.emplace(key, Record{}).first;
    }
    Record& r = it->second;
    // Events piling up (a car not drawn, or a flood): the oldest go into the
    // record at once, and the car shows its whole record when next drawn.
    while (r.pending.size() >= kMaxPending) {
        r.pending.front().overdue = true;
        const std::size_t before = r.pending.size();
        process(r, -std::numeric_limits<double>::infinity(), arrival, nullptr);
        if (r.pending.size() >= before)
            r.pending.pop_front();
        r.replay = true;
    }
    Pending p;
    p.event = e;
    p.arrival = arrival;
    r.pending.push_back(std::move(p));
    return true;
}

void DamageReplica::process(Record& r, double sampleTime, double now, Actions* out) {
    while (!r.pending.empty()) {
        Pending& p = r.pending.front();
        const net::VehicleDamageEvent& e = p.event;
        const bool overdue = p.overdue || now - p.arrival >= kMaxHoldMs;
        const double base = static_cast<double>(e.time);
        auto due = [&](std::uint8_t delay) { return overdue || sampleTime >= base + delay; };
        auto fresh = [&](std::uint8_t delay) { return !overdue && sampleTime - (base + delay) < kFreshMs; };
        if (!p.started) {
            // A new epoch: the car's damage was cleared at the event's time.
            if (!r.any || e.epoch != r.epoch) {
                if (!due(0))
                    return;
                r.any = true;
                r.epoch = e.epoch;
                r.patches.clear();
                r.next = 0;
                r.parts = 0;
                if (out) {
                    *out = Actions{};
                    out->clear = true;
                }
            }
            p.started = true;
        }
        while (p.nextPatch < e.patches.size() && due(e.patches[p.nextPatch].delay)) {
            const net::DamagePatch& d = e.patches[p.nextPatch];
            const std::uint32_t index = e.first + static_cast<std::uint32_t>(p.nextPatch);
            if (index >= r.next) {
                m_stats.missed += index - r.next;
                r.patches.push_back({d.point, d.seed});
                r.next = index + 1;
                ++m_stats.patches;
                if (out)
                    out->patches.push_back({d.point, d.seed});
            }
            ++p.nextPatch;
        }
        while (p.nextImpact < e.impacts.size() && due(e.impacts[p.nextImpact].delay)) {
            const net::DamageImpact& m = e.impacts[p.nextImpact];
            if (out && fresh(m.delay))
                out->impacts.push_back(m);
            ++p.nextImpact;
        }
        if (!p.partsDone && due(e.partsDelay)) {
            const std::uint32_t added = e.parts & ~r.parts;
            r.parts |= added;
            if (out && added) {
                out->partsOff |= added;
                out->partsFresh = out->partsFresh || fresh(e.partsDelay);
            }
            p.partsDone = true;
        }
        if (p.nextPatch < e.patches.size() || p.nextImpact < e.impacts.size() || !p.partsDone)
            return;
        r.pending.pop_front();
    }
}

DamageReplica::Actions DamageReplica::advance(std::uint32_t key, double sampleTime, double now) {
    Actions out;
    const auto it = m_records.find(key);
    if (it == m_records.end())
        return out;
    Record& r = it->second;
    r.touched = true;
    if (r.replay) {
        out = replay(key);
        r.replay = false;
    }
    process(r, sampleTime, now, &out);
    return out;
}

void DamageReplica::settle(double now) {
    for (auto& [key, r] : m_records) {
        if (!r.touched) {
            const std::size_t before = r.pending.size();
            process(r, -std::numeric_limits<double>::infinity(), now, nullptr);
            // Should the car be drawn again without being shown afresh, it
            // shows its whole record then.
            r.replay = r.replay || r.pending.size() != before;
        }
        r.touched = false;
    }
}

DamageReplica::Actions DamageReplica::replay(std::uint32_t key) const {
    Actions out;
    const auto it = m_records.find(key);
    if (it == m_records.end())
        return out;
    out.clear = true;
    out.patches = it->second.patches;
    out.partsOff = it->second.parts;
    return out;
}

std::size_t DamageReplica::recordSize(std::uint32_t key) const {
    const auto it = m_records.find(key);
    return it == m_records.end() ? 0 : it->second.patches.size();
}

// --- applyDamage -------------------------------------------------------------------------

void applyDamage(const DamageReplica::Actions& a, DamageTarget& t) {
    if (!t.renderer)
        return;
    VehicleRenderer& r = *t.renderer;
    // vehCarModel::ClearDamage: the texel damage and every part back on.
    if (a.clear)
        r.resetDamage();
    // fxTexelDamage::ApplyDamage as the owner ran it.
    for (const auto& p : a.patches)
        r.applyDamage(p.point, t.texelRadius, p.seed);
    // vehBreakableMgr::Eject: thrown off as it breaks, or just gone.
    for (int i = 0; i < net::kDamagePartCount; ++i) {
        if ((a.partsOff & (1u << i)) == 0)
            continue;
        const auto part = r.attachedPart(damagePartName(i, r.paintjob()));
        if (!part)
            continue;
        const float speed = damagePartIsWreckPart(i) ? t.speed * 1.3f : 4.0f;
        if (!(a.partsFresh && t.eject && t.eject(*part, speed)))
            r.detach(part->part);
    }
    // vehCarDamage::ApplyImpact's sparks, shards and AudImpact.
    for (const auto& m : a.impacts) {
        const Vec3 position = t.body.transform(m.point);
        Vec3 normal = t.body.transformDir(m.normal);
        if (const float len = normal.mag(); len > 1e-6f)
            normal = normal * (1.0f / len);
        if (t.effects)
            t.effects->impactEffects(position, normal, m.total, m.speed,
                                     m.speed * phys::kMetersPerSecondToMph, t.body);
        if (t.sound && m.sound > 0.001f)
            t.sound(position, m.sound, m.audioId);
    }
}

// --- NetDamage ---------------------------------------------------------------------------

DamageRecorder& NetDamage::police(std::uint16_t id) {
    return m_police.try_emplace(id, id).first->second;
}

void NetDamage::send(NetGame& net, std::uint64_t nowMs) {
    auto send = [&](DamageRecorder& recorder) {
        for (auto& e : recorder.take(nowMs)) {
            if (m_verbose)
                log::info("netdamage: send car {} epoch {} t {} patches {}+{} parts {:#x} impacts {}",
                          e.subject, e.epoch, e.time, e.first, e.patches.size(), e.parts, e.impacts.size());
            auto payload = net::encodePayload(std::move(e));
            ++m_stats.sentEvents;
            m_stats.sentBytes += payload.size();
            net.sendEvent(net::kVehicleDamageEvent, std::move(payload));
        }
    };
    send(m_own);
    for (auto& [id, recorder] : m_police)
        send(recorder);
}

void NetDamage::receive(std::span<const NetGameEvent> events, double now) {
    for (const NetGameEvent& ev : events) {
        if (static_cast<std::uint16_t>(ev.type) != net::kVehicleDamageEvent)
            continue;
        ++m_stats.receivedEvents;
        m_stats.receivedBytes += ev.payload.size();
        const auto e = ev.as<net::VehicleDamageEvent>();
        const bool taken = e && m_replica.receive(ev.from, *e, now);
        if (m_verbose && e)
            log::info("netdamage: from {} car {} epoch {} t {} patches {}+{} parts {:#x} impacts {}{}",
                      ev.from, e->subject, e->epoch, e->time, e->first, e->patches.size(), e->parts,
                      e->impacts.size(), taken ? "" : " (refused)");
    }
}

void NetDamage::update(std::uint32_t key, DamageTarget& target, double sampleTime, double now, bool fresh) {
    if (fresh) {
        // A car shown afresh starts clean, then shows its record so far.
        if (target.renderer)
            target.renderer->resetDamage();
        applyDamage(m_replica.replay(key), target);
    }
    const auto actions = m_replica.advance(key, sampleTime, now);
    if (!actions.empty())
        applyDamage(actions, target);
}

void NetDamage::logStats(std::uint64_t nowMs) {
    if (m_statsAt == 0)
        m_statsAt = nowMs;
    if (nowMs - m_statsAt < 10000)
        return;
    const double seconds = static_cast<double>(nowMs - m_statsAt) / 1000.0;
    if (m_stats.sentEvents || m_stats.receivedEvents)
        log::info("netdamage: sent {} events ({:.0f} B/s payload), received {} ({:.0f} B/s); {} records, {} "
                  "patches, {} missed, {} refused",
                  m_stats.sentEvents, static_cast<double>(m_stats.sentBytes) / seconds,
                  m_stats.receivedEvents, static_cast<double>(m_stats.receivedBytes) / seconds,
                  m_replica.records(), m_replica.stats().patches, m_replica.stats().missed,
                  m_replica.stats().refused);
    m_stats = {};
    m_statsAt = nowMs;
}

} // namespace mm2::game
