// Shared ambient traffic of a multiplayer cruise (OpenMM2 extra): the
// catalog, the host's choice of cars per client and the client's
// interpolation. See TrafficSync.h and docs/multiplayer.md.
#include "game/net/TrafficSync.h"

#include "core/StringUtil.h"

#include <algorithm>
#include <cmath>

namespace mm2::game {
namespace {

constexpr double kAiStepMs = 1000.0 / 30.0; // ai::kAiStepSeconds

bool finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// Signed difference of two session times (they may wrap).
std::int64_t later(std::uint32_t a, std::uint32_t b) {
    return static_cast<std::int64_t>(static_cast<std::int32_t>(a - b));
}

net::AmbientEntity toEntity(const SharedCar& c) {
    net::AmbientEntity e;
    e.id = static_cast<std::uint16_t>(c.id);
    e.generation = static_cast<std::uint8_t>(static_cast<unsigned>(c.generation) % net::kAmbientGenerations);
    e.kind = c.kind;
    e.model = static_cast<std::uint8_t>(c.model);
    e.paint = static_cast<std::uint8_t>(std::clamp(c.paint, 0, static_cast<int>(net::kMaxAmbientPaint)));
    e.position = c.transform.m3;
    e.orientation = Quat::fromMatrix(c.transform);
    e.speed = c.speed;
    // A rail car's acceleration and curvature, 0 where they would move it by
    // a centimetre or so over half a second (or 5 m): those cost 1 bit. A
    // car standing still turns nowhere.
    if (std::abs(c.motion.accel) >= 0.1f)
        e.accel = std::clamp(c.motion.accel, -net::kAmbientAccelRange, net::kAmbientAccelRange);
    constexpr float kMaxCurvature = net::kAmbientCurvatureRange;
    if (std::abs(c.motion.curvature) >= 0.001f && (c.speed != 0.0f || e.accel > 0.0f))
        e.curvature = std::clamp(c.motion.curvature, -kMaxCurvature, kMaxCurvature);
    e.velocity = c.velocity;
    e.angularVelocity = c.angularVelocity;
    e.flags = c.flags;
    e.wheels = c.wheels && c.kind == net::AmbientKind::Traffic && (c.flags & net::kAmbientOffRail) != 0;
    if (e.wheels)
        e.wheelOffsets = c.wheelOffsets;
    e.target = c.target;
    e.damage = c.damage;
    e.rpm = c.rpm;
    e.throttle = c.throttle;
    e.gear = static_cast<std::int8_t>(std::clamp(c.gear, -1, 8));
    return e;
}

} // namespace

// --- TrafficCatalog ----------------------------------------------------------------------

void TrafficCatalog::add(std::string_view model) {
    if (model.empty() || find(model) >= 0 || m_models.size() >= net::kMaxAmbientModels)
        return;
    m_models.push_back(str::lower(model));
}

int TrafficCatalog::find(std::string_view model) const {
    for (std::size_t i = 0; i < m_models.size(); ++i)
        if (str::iequals(m_models[i], model))
            return static_cast<int>(i);
    return -1;
}

const std::string* TrafficCatalog::name(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= m_models.size())
        return nullptr;
    return &m_models[static_cast<std::size_t>(index)];
}

std::uint16_t TrafficCatalog::checksum() const {
    std::uint32_t h = 2166136261u;
    for (const auto& m : m_models) {
        for (char c : m) {
            h ^= static_cast<std::uint8_t>(c);
            h *= 16777619u;
        }
        h ^= 0xFFu; // separator
        h *= 16777619u;
    }
    return static_cast<std::uint16_t>((h >> 16) ^ (h & 0xFFFFu));
}

// --- TrafficHost -------------------------------------------------------------------------

net::AmbientStateMsg TrafficHost::build(const TrafficViewer& viewer, std::span<const SharedCar> cars,
                                        std::uint32_t time, std::uint32_t lightSteps, std::uint16_t catalog) {
    net::AmbientStateMsg msg;
    msg.time = time;
    msg.lightSteps = lightSteps;
    msg.catalog = catalog;
    msg.setOrigin(viewer.position);
    const Vec3 origin = msg.originVec();
    std::unordered_set<int>& set = m_sets[viewer.player];
    const std::uint32_t sequence = m_sequence[viewer.player]++;
    const float near2 = m_options.fullRateRadius * m_options.fullRateRadius;

    struct Candidate {
        const SharedCar* car;
        float distance2;
        bool chasing;
    };
    std::vector<Candidate> candidates;
    const float enter2 = m_options.enterRadius * m_options.enterRadius;
    const float leave2 = m_options.leaveRadius * m_options.leaveRadius;
    // Kept inside the encodable offsets with a margin for the rounding.
    const float reach = net::kAmbientOffsetRange - 2.0f, reachUp = net::kAmbientHeightRange - 2.0f;
    for (const SharedCar& c : cars) {
        if (c.id < 0 || c.id >= static_cast<int>(net::kMaxAmbientIds) || c.model < 0 ||
            c.model >= static_cast<int>(net::kMaxAmbientModels) || !finite(c.transform.m3))
            continue;
        const Vec3 d = c.transform.m3 - origin;
        if (std::abs(d.x) > reach || std::abs(d.z) > reach || std::abs(d.y) > reachUp)
            continue;
        const float dx = c.transform.m3.x - viewer.position.x, dz = c.transform.m3.z - viewer.position.z;
        const float d2 = dx * dx + dz * dz;
        const bool chasing = c.kind == net::AmbientKind::Police && c.target == viewer.player;
        if (chasing || d2 < enter2 || (d2 < leave2 && set.contains(c.id)))
            candidates.push_back({&c, d2, chasing});
    }
    std::ranges::stable_sort(candidates, [](const Candidate& a, const Candidate& b) {
        if (a.chasing != b.chasing)
            return a.chasing;
        return a.distance2 < b.distance2;
    });

    // The header: type, time, light steps, catalog, origin, the count.
    constexpr std::size_t kHeaderBits = 8 + 32 + 32 + 16 + 48 + 8;
    const std::size_t total = m_options.maxBytes * 8;
    const std::size_t budget = total > kHeaderBits ? total - kHeaderBits : 0;
    std::size_t used = 0;
    std::unordered_set<int> sent;
    for (const Candidate& c : candidates) {
        if (msg.entities.size() >= net::kMaxAmbientPerMessage)
            break;
        net::AmbientEntity e = toEntity(*c.car);
        // A far car on its rail, already known to the client: its state
        // every other message, the ids alternating between messages.
        const bool full = c.chasing || c.distance2 < near2 || !set.contains(c.car->id) ||
                          c.car->kind != net::AmbientKind::Traffic ||
                          (c.car->flags & net::kAmbientOffRail) != 0 ||
                          ((sequence + static_cast<std::uint32_t>(c.car->id)) & 1u) == 0;
        if (!full)
            e.hasState = false;
        const std::size_t bits = net::ambientEntityBits(e);
        if (used + bits > budget)
            break;
        used += bits;
        if (!sent.insert(c.car->id).second)
            continue; // the same id twice: the first one counts
        msg.entities.push_back(std::move(e));
    }
    set = std::move(sent);
    return msg;
}

// --- TrafficClient -----------------------------------------------------------------------

TrafficClient::TrafficClient(std::size_t catalogSize, std::uint16_t catalogChecksum, const Options& options)
    : m_options(options), m_catalogSize(catalogSize), m_catalog(catalogChecksum) {}

void TrafficClient::clear() {
    m_entries.clear();
    m_cars.clear();
    m_any = false;
    m_latest = 0;
}

void TrafficClient::restart(Entry& entry, const net::AmbientEntity& e, std::uint32_t time) {
    entry.generation = e.generation;
    entry.kind = e.kind;
    entry.model = e.model;
    entry.paint = e.paint;
    entry.buffer.clear();
    entry.wheels.clear();
    entry.firstTime = time;
    entry.lastTime = time;
    entry.goneAt.reset();
    entry.horn = false;
    entry.shown = false;
    entry.motion = {};
    entry.basis.reset();
    entry.offset = {};
    entry.offsetTurn = 0.0f;
}

void TrafficClient::receive(const net::AmbientStateMsg& msg) {
    ++m_stats.messages;
    if (msg.catalog != m_catalog)
        m_mismatch = true;
    const bool newest = !m_any || later(msg.time, m_latest) >= 0;
    if (!newest)
        ++m_stats.outdated;
    std::unordered_set<int> listed;
    for (const net::AmbientEntity& e : msg.entities) {
        ++m_stats.entities;
        const int id = e.id;
        if (!e.hasState) {
            // Still there: the same car keeps going on its last state (an
            // unknown one waits for a message with its state).
            if (id >= static_cast<int>(net::kMaxAmbientIds) || !listed.insert(id).second) {
                ++m_stats.refused;
                continue;
            }
            const auto it = m_entries.find(id);
            if (newest && it != m_entries.end() && it->second.generation == e.generation)
                it->second.goneAt.reset();
            else if (it != m_entries.end())
                listed.erase(id); // another car now: the old one has gone
            continue;
        }
        if (id >= static_cast<int>(net::kMaxAmbientIds) || e.model >= m_catalogSize ||
            e.paint > net::kMaxAmbientPaint || e.kind > net::AmbientKind::Last || !finite(e.position) ||
            !finite(e.velocity) || !finite(e.angularVelocity) || !std::isfinite(e.speed) ||
            !std::isfinite(e.accel) || !std::isfinite(e.curvature) || !listed.insert(id).second) {
            ++m_stats.refused;
            continue;
        }
        auto it = m_entries.find(id);
        const bool sameCar = it != m_entries.end() && it->second.generation == e.generation &&
                             it->second.kind == e.kind && it->second.model == e.model &&
                             it->second.paint == e.paint;
        if (!newest && !sameCar)
            continue; // an old message must not bring a car back or swap it
        if (it == m_entries.end())
            it = m_entries.emplace(id, Entry{}).first;
        Entry& entry = it->second;
        if (!sameCar) {
            restart(entry, e, msg.time);
        } else if (newest && entry.buffer.latest()) {
            // Put somewhere else without a new generation (a police car back
            // at its post): shown there at once.
            const net::VehicleSnapshot& last = *entry.buffer.latest();
            const auto ms = std::max<std::int64_t>(0, later(msg.time, last.time));
            const float dt = static_cast<float>(ms) * 0.001f;
            const Vec3 expected = last.position + last.linearVelocity * dt;
            // (30 m/s of velocity change on top of the allowance.)
            if (expected.dist(e.position) > m_options.teleportDistance + 30.0f * dt) {
                ++m_stats.teleports;
                restart(entry, e, msg.time);
            }
        }
        net::VehicleSnapshot s;
        s.time = msg.time;
        s.position = e.position;
        s.orientation = e.orientation;
        s.linearVelocity = e.velocity;
        s.angularVelocity = e.angularVelocity;
        s.controls.throttle = e.throttle;
        s.controls.gear = e.gear;
        s.damage = e.damage;
        s.flags = e.flags;
        entry.buffer.push(s);
        // A knocked car's wheels, kept in time order (an older message's
        // are not inserted: the newer ones say more).
        if (entry.wheels.empty() || later(msg.time, entry.wheels.back().time) > 0) {
            WheelSample w;
            w.time = msg.time;
            w.wheels = e.wheels &&
                       std::ranges::all_of(e.wheelOffsets, [](const Vec3& v) { return finite(v); });
            if (w.wheels)
                w.offsets = e.wheelOffsets;
            entry.wheels.push_back(w);
            if (entry.wheels.size() > 16)
                entry.wheels.pop_front();
        }
        if (later(msg.time, entry.lastTime) >= 0) {
            entry.lastTime = msg.time;
            entry.target = e.target;
            entry.rpm = e.rpm;
            // A rail car's (the others move on at their velocities).
            entry.motion = e.fullMotion() ? RailMotion{} : RailMotion{e.accel, e.curvature};
        }
        if (later(msg.time, entry.firstTime) < 0)
            entry.firstTime = msg.time;
        if (newest)
            entry.goneAt.reset();
    }
    if (!newest)
        return;
    // Complete for this client: whatever it knows and this newer message
    // leaves out has gone (out of range, or back in the pool) by now.
    for (auto& [id, entry] : m_entries)
        if (!listed.contains(id) && !entry.goneAt && later(msg.time, entry.lastTime) > 0)
            entry.goneAt = msg.time;
    m_any = true;
    m_latest = msg.time;
    m_lightTime = msg.time;
    m_lightSteps = msg.lightSteps;
}

void TrafficClient::update(double renderTime) {
    m_cars.clear();
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        Entry& entry = it->second;
        const bool gone = entry.goneAt && renderTime >= static_cast<double>(*entry.goneAt);
        const bool stale = renderTime - static_cast<double>(entry.lastTime) > m_options.staleMs;
        if (gone || stale || entry.buffer.size() == 0) {
            it = m_entries.erase(it);
            continue;
        }
        const int id = it->first;
        ++it;
        if (renderTime < static_cast<double>(entry.firstTime))
            continue; // not on the host's screen yet at this time
        // The drawing's correction fades (OpenMM2 presentation).
        const double elapsed = std::max(0.0, renderTime - entry.updatedAt);
        entry.updatedAt = renderTime;
        if (m_options.correctionMs > 0.0) {
            const auto keep = static_cast<float>(std::exp(-elapsed / m_options.correctionMs));
            entry.offset = entry.offset * keep;
            entry.offsetTurn *= keep;
        }
        net::VehicleSnapshot s;
        const net::VehicleSnapshot& newest = *entry.buffer.latest();
        PredictedPose predicted;
        const bool ahead = predict(entry, newest, entry.motion, renderTime, predicted);
        auto result = net::SnapshotBuffer::Result::Interpolated;
        if (ahead) {
            // A newer basis: the drawing stays where it was and blends the
            // difference away (beyond the snap distance it jumps).
            PredictedPose before;
            if (entry.shown && entry.basis && entry.basis->time != newest.time &&
                predict(entry, *entry.basis, entry.basisMotion, renderTime, before)) {
                const Vec3 offset = before.transform.m3 + entry.offset - predicted.transform.m3;
                const float turn = std::remainder(groundHeading(-before.transform.m2) + entry.offsetTurn -
                                                      groundHeading(-predicted.transform.m2),
                                                  kTwoPi);
                if (offset.mag() > m_options.snapDistance || std::abs(turn) > m_options.snapTurn) {
                    ++m_stats.snaps;
                    entry.offset = {};
                    entry.offsetTurn = 0.0f;
                } else {
                    entry.offset = offset;
                    entry.offsetTurn = turn;
                }
            }
            entry.basis = newest;
            entry.basisMotion = entry.motion;
            s = newest;
            result = net::SnapshotBuffer::Result::Extrapolated;
        } else {
            result = entry.buffer.sample(renderTime, s, 0.0);
            if (result == net::SnapshotBuffer::Result::Empty)
                continue;
            entry.offset = {};
            entry.offsetTurn = 0.0f;
        }
        Car c;
        c.id = id;
        c.kind = entry.kind;
        c.generation = entry.generation;
        c.model = entry.model;
        c.paint = entry.paint;
        if (ahead) {
            c.transform = predicted.transform;
            c.velocity = predicted.velocity;
            c.angularVelocity = s.angularVelocity;
        } else {
            c.transform = s.orientation.normalized().toMatrix(s.position);
            c.velocity = s.linearVelocity;
            c.angularVelocity = s.angularVelocity;
        }
        c.speed = -c.transform.m2.dot(c.velocity);
        c.flags = s.flags;
        c.target = entry.target;
        c.damage = s.damage;
        c.rpm = entry.rpm;
        c.throttle = s.controls.throttle;
        c.gear = s.controls.gear;
        // The wheels at renderTime: from the newest message at or before it,
        // blended toward the next one when that has them too.
        if (!entry.wheels.empty()) {
            std::size_t i = 0;
            while (i + 1 < entry.wheels.size() && static_cast<double>(entry.wheels[i + 1].time) <= renderTime)
                ++i;
            const WheelSample& a = entry.wheels[i];
            if (a.wheels) {
                c.wheels = true;
                c.wheelOffsets = a.offsets;
                if (i + 1 < entry.wheels.size() && entry.wheels[i + 1].wheels &&
                    static_cast<double>(a.time) <= renderTime) {
                    const WheelSample& b = entry.wheels[i + 1];
                    const auto span = static_cast<double>(later(b.time, a.time));
                    const double into = renderTime - static_cast<double>(a.time);
                    const float t = span > 0.0 ? static_cast<float>(std::clamp(into / span, 0.0, 1.0)) : 0.0f;
                    for (std::size_t k = 0; k < c.wheelOffsets.size(); ++k)
                        c.wheelOffsets[k] = a.offsets[k] + (b.offsets[k] - a.offsets[k]) * t;
                }
            }
            for (; i > 0; --i)
                entry.wheels.pop_front(); // older than the one in use
        }
        c.extrapolated = result != net::SnapshotBuffer::Result::Interpolated;
        const bool horn = (s.flags & net::kAmbientHorn) != 0;
        c.hornStarted = horn && !entry.horn;
        entry.horn = horn;
        c.fresh = !entry.shown;
        entry.shown = true;
        m_cars.push_back(c);
        entry.buffer.prune(renderTime - kDrawBehindMs);
    }
}

TrafficCatalog buildTrafficCatalog(std::span<const ai::VehicleData> types,
                                   std::span<const std::string> police) {
    TrafficCatalog c;
    for (const ai::VehicleData& t : types)
        c.add(t.model);
    for (const std::string& p : police)
        c.add(p);
    return c;
}

SharedCar shareTrafficCar(const ai::AmbientCar& car, int model, int paint, const TrafficBodyState* body,
                          bool horn) {
    SharedCar s;
    s.id = car.id;
    s.kind = net::AmbientKind::Traffic;
    s.generation = car.spawns;
    s.model = model;
    s.paint = paint;
    s.transform = body ? body->transform : car.transform;
    s.speed = car.speed;
    s.velocity = body ? body->velocity : car.velocity;
    s.angularVelocity = body ? body->angularVelocity : Vec3{};
    s.body = body != nullptr;
    if (body && body->wheels) {
        s.wheels = true;
        s.wheelOffsets = *body->wheels;
    }
    std::uint8_t f = 0;
    if (car.braking)
        f |= net::kAmbientBrake;
    if (horn)
        f |= net::kAmbientHorn;
    if (car.signal == ai::TurnSignal::Left || car.signal == ai::TurnSignal::Hazard)
        f |= net::kAmbientSignalLeft;
    if (car.signal == ai::TurnSignal::Right || car.signal == ai::TurnSignal::Hazard)
        f |= net::kAmbientSignalRight;
    // Out of normal driving (aiObstacle::InAccident: any goal but driving
    // its rail), or simulated.
    if (body || car.physical || car.goal != ai::AmbientGoal::RandomDrive)
        f |= net::kAmbientOffRail;
    if (car.wreck)
        f |= net::kAmbientWrecked;
    s.flags = f;
    return s;
}

ai::AmbientCar ambientCarOf(const TrafficClient::Car& car, const std::string& model,
                            const ai::VehicleData* data, int paintJobs, float tireRotation) {
    ai::AmbientCar a;
    a.id = car.id;
    a.data = data;
    a.model = model;
    // AiRenderer's paint job is trunc(paint x (jobs - 1)): the middle of the
    // job's range gives it back exactly. The job comes from the network: it
    // is held to the jobs aiVehicleInstance::SetColor can pick (all but the
    // last).
    const int job = paintJobs > 1 ? std::clamp(car.paint, 0, paintJobs - 2) : 0;
    a.paint = paintJobs > 1 ? (static_cast<float>(job) + 0.5f) / static_cast<float>(paintJobs - 1) : 0.0f;
    a.transform = car.transform;
    a.velocity = car.velocity;
    a.speed = car.speed;
    a.tireRotation = tireRotation;
    a.braking = (car.flags & net::kAmbientBrake) != 0;
    const bool left = (car.flags & net::kAmbientSignalLeft) != 0;
    const bool right = (car.flags & net::kAmbientSignalRight) != 0;
    a.signal = left && right ? ai::TurnSignal::Hazard
               : left        ? ai::TurnSignal::Left
               : right       ? ai::TurnSignal::Right
                             : ai::TurnSignal::None;
    // The blink phase is the host's slot's own random number; the client
    // keeps one per id (inferred: the blinking need not match).
    a.blinkPhase = (car.id * 37) & 0xFF;
    a.horn = car.hornStarted;
    const bool offRail = (car.flags & net::kAmbientOffRail) != 0;
    a.goal = offRail ? ai::AmbientGoal::Collision : ai::AmbientGoal::RandomDrive;
    a.physical = offRail;
    a.wreck = (car.flags & net::kAmbientWrecked) != 0;
    a.spawns = car.generation;
    return a;
}

std::array<Vec3, 4> trafficWheelOffsets(const Mat34& transform, const std::array<Mat34, 6>& wheels,
                                        const ai::VehicleData& data) {
    std::array<Vec3, 4> out{};
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = transform.untransform(wheels[i].m3) - data.wheels[i];
    return out;
}

void trafficWheelMatrices(const Mat34& transform, const std::array<Vec3, 4>& offsets,
                          const ai::VehicleData& data, std::array<Mat34, 6>& matrices,
                          std::array<bool, 6>& valid) {
    valid = {};
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        matrices[i] = Mat34::mul(Mat34::translation(data.wheels[i] + offsets[i]), transform);
        valid[i] = true;
    }
    for (std::size_t i = 4; i < 6; ++i) {
        if (data.wheelCount <= static_cast<int>(i))
            continue;
        const Vec3& p = data.wheels[i];
        const float drawn = data.wheels[i - 2].y + offsets[i - 2].y;
        matrices[i] = Mat34::mul(Mat34::translation({p.x, (drawn - data.wheelRadius) + p.y, p.z}), transform);
        valid[i] = true;
    }
}

bool TrafficClient::predict(const Entry& entry, const net::VehicleSnapshot& state, const RailMotion& motion,
                            double time, PredictedPose& out) const {
    const double ahead = time - static_cast<double>(state.time);
    if (ahead <= 0.0)
        return false;
    const Mat34 m = state.orientation.normalized().toMatrix(state.position);
    // A car on its rail goes on along it; a body (a police car, a car off
    // its rail) at its velocity, for a shorter while.
    const bool rail = entry.kind == net::AmbientKind::Traffic && (state.flags & net::kAmbientOffRail) == 0;
    if (rail) {
        const auto dt = static_cast<float>(std::min(ahead, m_options.maxRailPredictionMs) / 1000.0);
        out = predictRailCar(m, -m.m2.dot(state.linearVelocity), motion, dt);
    } else {
        const auto dt = static_cast<float>(std::min(ahead, m_options.maxBodyPredictionMs) / 1000.0);
        out = predictBody(m, state.linearVelocity, state.angularVelocity, dt);
    }
    return true;
}

std::optional<Mat34> TrafficClient::transformAt(int id, double time) const {
    const auto it = m_entries.find(id);
    if (it == m_entries.end())
        return std::nullopt;
    const Entry& entry = it->second;
    const net::VehicleSnapshot* newest = entry.buffer.latest();
    if (!newest)
        return std::nullopt;
    PredictedPose p;
    if (predict(entry, *newest, entry.motion, time, p)) {
        // Drawn with the correction still being blended away.
        Mat34 m = p.transform;
        if (entry.offsetTurn != 0.0f) {
            const Mat34 turn = Mat34::rotationY(entry.offsetTurn); // +y, from +z toward +x
            m.m0 = turn.transformDir(m.m0);
            m.m1 = turn.transformDir(m.m1);
            m.m2 = turn.transformDir(m.m2);
        }
        m.m3 += entry.offset;
        return m;
    }
    net::VehicleSnapshot s;
    if (entry.buffer.sample(time, s, 0.0) == net::SnapshotBuffer::Result::Empty)
        return std::nullopt;
    return s.orientation.normalized().toMatrix(s.position);
}

std::optional<std::uint32_t> TrafficClient::lightSteps(double renderTime) const {
    if (!m_any)
        return std::nullopt;
    const double steps = static_cast<double>(m_lightSteps) +
                         std::floor((renderTime - static_cast<double>(m_lightTime)) / kAiStepMs);
    return static_cast<std::uint32_t>(std::max(0.0, steps));
}

} // namespace mm2::game
