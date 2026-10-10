// The shared traffic predicted forward on a client (OpenMM2 extra). See
// TrafficPrediction.h.
#include "game/net/TrafficPrediction.h"

#include "core/Libm.h"

#include <algorithm>
#include <cmath>

namespace mm2::game {
namespace {

// `v` turned by `angle` about the unit axis `k` (Rodrigues: a physical
// rotation, the right-hand way round the axis).
Vec3 rotated(const Vec3& v, const Vec3& k, float angle) {
    const float c = libm::cos(angle), s = libm::sin(angle);
    return v * c + k.cross(v) * s + k * (k.dot(v) * (1.0f - c));
}

// Turned by `angle` about +y (from +z toward +x).
Vec3 yawed(const Vec3& v, float angle) {
    const float c = libm::cos(angle), s = libm::sin(angle);
    return {v.x * c + v.z * s, v.y, v.z * c - v.x * s};
}

float wrapAngle(float a) {
    while (a > kPi)
        a -= kTwoPi;
    while (a < -kPi)
        a += kTwoPi;
    return a;
}

// The chord of an arc of length `s` that turns by `angle`: its length.
float chord(float s, float angle) {
    return std::abs(angle) > 1e-4f ? s * (2.0f * libm::sin(angle * 0.5f) / angle) : s;
}

} // namespace

float groundHeading(const Vec3& forward) { return libm::atan2(forward.x, forward.z); }

// --- RailMotionTracker --------------------------------------------------------------------

void RailMotionTracker::update(std::span<const ai::AmbientCar> cars, double time) {
    for (auto& [id, last] : m_cars)
        last.seen = false;
    for (const ai::AmbientCar& c : cars) {
        Last& last = m_cars[c.id];
        last.seen = true;
        const Vec3 forward = -c.transform.m2;
        const float heading = groundHeading(forward);
        const Vec3& p = c.transform.m3;
        if (last.spawns != c.spawns) {
            last = {c.spawns, p, heading, c.speed, c.speed, time, {}, true};
            continue;
        }
        const double dtMs = time - last.time;
        if (dtMs <= 0.5)
            continue; // the AI has not stepped since
        const auto dt = static_cast<float>(dtMs / 1000.0);
        last.motion.accel = (c.speed - last.speed) / dt;
        const float ds = libm::hypot(p.x - last.position.x, p.z - last.position.z);
        // Over a short move the heading's change is mostly rounding: the
        // curvature stays as it was.
        if (ds > 0.05f)
            last.motion.curvature = std::clamp(wrapAngle(heading - last.heading) / ds, -2.0f, 2.0f);
        // Its speed over the ground in the step (a move of more than 40 m/s
        // put it somewhere else: the AI's speed stands).
        const float ground = p.dist(last.position) / dt;
        last.ground = ground > 40.0f ? c.speed : ground;
        last.motion.groundSpeed = last.ground;
        last.motion.slips = std::abs(last.ground - c.speed) > 0.25f;
        last.position = p;
        last.heading = heading;
        last.speed = c.speed;
        last.time = time;
    }
    std::erase_if(m_cars, [](const auto& e) { return !e.second.seen; });
}

RailMotion RailMotionTracker::motion(int id) const {
    const auto it = m_cars.find(id);
    return it == m_cars.end() ? RailMotion{} : it->second.motion;
}

// --- StillBodies --------------------------------------------------------------------------

bool StillBodies::still(int id, const Vec3& position, double time) {
    auto [it, fresh] = m_anchors.try_emplace(id, Anchor{position, time, time});
    Anchor& a = it->second;
    a.seen = time;
    if (fresh || a.position.dist2(position) > 0.05f * 0.05f) {
        a.position = position;
        a.since = time;
        return false;
    }
    return time - a.since >= 100.0;
}

void StillBodies::prune(double time) {
    std::erase_if(m_anchors, [time](const auto& e) { return e.second.seen < time - 1000.0; });
}

// --- Prediction ---------------------------------------------------------------------------

PredictedPose predictRailCar(const Mat34& transform, float speed, const RailMotion& motion, float dt) {
    PredictedPose out;
    dt = std::max(dt, 0.0f);
    // The distance its speed over the ground and acceleration give (the
    // acceleration scaled as the ground speed is), stopping at 0 (or at the
    // speed it had, for a car reversing).
    const float ground = motion.slips ? motion.groundSpeed : speed;
    const bool scaled = motion.slips && std::abs(speed) > 0.5f;
    const float scale = scaled ? std::clamp(ground / speed, 0.0f, 2.0f) : 1.0f;
    const float a = motion.accel * scale;
    float g = ground + a * dt;
    float s = 0.0f;
    if ((ground >= 0.0f && g < 0.0f) || (ground < 0.0f && g > 0.0f)) {
        const float stop = a != 0.0f ? -ground / a : 0.0f;
        s = ground * stop * 0.5f;
        g = 0.0f;
    } else {
        s = (ground + g) * 0.5f * dt;
    }
    // Its own speed, which a body it takes when hit moves at.
    float v = speed + motion.accel * dt;
    if ((speed >= 0.0f && v < 0.0f) || (speed < 0.0f && v > 0.0f))
        v = 0.0f;
    const float turn = motion.curvature * s;
    const Vec3 forward = -transform.m2;
    out.transform.m0 = yawed(transform.m0, turn);
    out.transform.m1 = yawed(transform.m1, turn);
    out.transform.m2 = yawed(transform.m2, turn);
    // Along the arc: its chord, half way round.
    out.transform.m3 = transform.m3 + yawed(forward, turn * 0.5f) * chord(s, turn);
    out.speed = v;
    out.velocity = -out.transform.m2 * v;
    return out;
}

PredictedPose predictBody(const Mat34& transform, const Vec3& velocity, const Vec3& spin, float dt) {
    PredictedPose out;
    dt = std::max(dt, 0.0f);
    // The path turns with the yaw rate: an arc on the ground plane, straight
    // up and down.
    const float turn = spin.y * dt;
    const Vec3 ground{velocity.x, 0.0f, velocity.z};
    const float s = ground.mag() * dt;
    Vec3 move = s > 1e-6f ? yawed(ground * (1.0f / ground.mag()), turn * 0.5f) * chord(s, turn) : Vec3{};
    move.y = velocity.y * dt;
    const float w = spin.mag();
    out.transform = transform;
    if (w > 1e-6f) {
        const Vec3 k = spin * (1.0f / w);
        out.transform.m0 = rotated(transform.m0, k, w * dt);
        out.transform.m1 = rotated(transform.m1, k, w * dt);
        out.transform.m2 = rotated(transform.m2, k, w * dt);
    }
    out.transform.m3 = transform.m3 + move;
    out.velocity = yawed(velocity, turn);
    out.speed = -out.transform.m2.dot(out.velocity);
    return out;
}

} // namespace mm2::game
