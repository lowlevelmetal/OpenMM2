// Shared AI driving pieces. Ported from MM1 (Open1560 code/midtown/mmai and
// game.asm, GPL-3.0); see Driving.h and docs/ai.md.
#include "ai/Driving.h"

#include "phys/AgeMath.h"
#include "phys/World.h"
#include "phys/vehicle/CarSim.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mm2::ai {
namespace {

Vec3 rightOf(const Vec3& dir) {
    return {-dir.z, 0.0f, dir.x};
}

Vec3 flatUnit(const Vec3& v) {
    const Vec2 f{v.x, v.z};
    const float m = f.mag();
    return m > 1e-6f ? Vec3{f.x / m, 0.0f, f.y / m} : Vec3{0, 0, -1};
}

} // namespace

float perFrame(float factor, float dt) {
    return std::pow(factor, clampf(30.0f * dt, 0.01f, 2.0f));
}

float headingError(const Mat34& m, const Vec3& target) {
    const Vec3 d = target - m.m3;
    return std::atan2(d.dot(m.m0), -d.dot(m.m2));
}

float forwardSpeed(const phys::CarSim& car) {
    return -car.body.ics.linearVelocity.dot(car.body.ics.matrix.m2);
}

// --- aiStuck -----------------------------------------------------------------

void AiStuck::reset() {
    m_state = Idle;
    m_impacted = false;
    m_time = 0.0f;
}

bool AiStuck::pegged(const phys::CarSim& car) const {
    // aiStuck::Pegged: Throttle > 0.75 MaxThrottle, and while watching also
    // |Steering| < 0.5 (driving straight into something).
    if (!(car.engine.maxThrottle * 0.75f < car.engine.throttle))
        return false;
    return m_state == Stuck || std::abs(car.steering) < 0.5f;
}

void AiStuck::update(phys::CarSim& car, float dt) {
    const Vec3 pos = car.body.ics.matrix.m3;
    const float posSq = posThresh * posThresh;
    const float moveSq = moveThresh * moveThresh;
    if (m_lastPos.dist2(pos) > posSq)
        m_state = Idle;
    if (m_impacted && m_state == Idle) {
        m_time = 0.0f;
        m_lastPos = pos;
        m_state = Watching;
    }
    m_impacted = false;
    if (m_state == Watching) {
        m_time += dt;
        const float dx = m_lastPos.x - pos.x, dz = m_lastPos.z - pos.z;
        if (dx * dx + dz * dz < posSq && !(m_time < timeThresh) && pegged(car)) {
            m_state = Stuck;
            return;
        }
        if (m_lastPos.dist2(pos) > moveSq)
            m_state = Idle;
    } else if (m_state == Stuck) {
        if (!(m_lastPos.dist2(pos) > moveSq) && pegged(car)) {
            const float sign = car.steering < 0.0f ? -1.0f : 1.0f;
            yawInPlace(car, -(dt * rotAmount * sign));
        } else {
            m_state = Idle;
        }
    }
}

// --- aiGoalBackup ------------------------------------------------------------

void BackupGoal::start(phys::CarSim& car) {
    m_active = true;
    m_time = 0.0f;
    // aiGoalBackup::Reset: select reverse.
    if (car.trans.getCurrentGear() != -1)
        car.trans.setReverse();
}

bool BackupGoal::update(phys::CarSim& car, const Vec3& target, float dt) {
    if (!m_active)
        return false;
    m_time += dt;
    if (car.trans.getCurrentGear() != -1 && m_time < 0.1f)
        car.trans.setReverse();
    const float angle = headingError(car.body.ics.matrix, target);
    const float limit = car.speed() > 2.0f ? 5.0f : 3.0f;
    if ((angle < -0.1f || angle > 0.1f) && car.trans.timeInGear < limit && m_time < limit) {
        car.setInputs(clampf(std::abs(angle), 0.1f, 0.85f), 0.0f, clampf(angle * -(20.0f / 7.0f), -1.0f, 1.0f),
                      0.0f);
        return true;
    }
    // FinishedBackingUp.
    car.setInputs(0.0f, 1.0f, car.steering, 0.0f);
    if (car.speed() < 2.0f) {
        m_active = false;
        car.stuck.reset();
        car.trans.setDrive();
        return false;
    }
    return true;
}

// --- obstacles ---------------------------------------------------------------

bool ObstacleScan::freeSide(float want, float minSide, float maxSide, float& out) const {
    if (minSide > maxSide)
        minSide = maxSide = 0.5f * (minSide + maxSide);
    auto isFree = [&](float x) {
        for (const Range& r : blocked)
            if (x > r.lo && x < r.hi)
                return false;
        return true;
    };
    const float clamped = clampf(want, minSide, maxSide);
    if (isFree(clamped)) {
        out = clamped;
        return true;
    }
    float best = std::numeric_limits<float>::max();
    bool found = false;
    auto consider = [&](float x) {
        if (x < minSide - 1e-3f || x > maxSide + 1e-3f || !isFree(x))
            return;
        if (std::abs(x - want) < best) {
            best = std::abs(x - want);
            out = x;
            found = true;
        }
    };
    consider(minSide);
    consider(maxSide);
    for (const Range& r : blocked) {
        consider(r.lo);
        consider(r.hi);
    }
    return found;
}

const ObstacleScan::Range* ObstacleScan::blocking(float side) const {
    const Range* best = nullptr;
    for (const Range& r : blocked)
        if (side > r.lo && side < r.hi && (!best || r.along < best->along))
            best = &r;
    return best;
}

ObstacleScan scanObstacles(const ScanInput& in, std::span<const TrackedCar> cars) {
    ObstacleScan scan;
    const Vec3 fwd = flatUnit(in.lineDir);
    const Vec3 right = rightOf(fwd);
    constexpr float kOwnHalfLength = 2.3f;
    for (const TrackedCar& c : cars) {
        if (c.id == in.selfId)
            continue;
        const Vec3 rel = c.position - in.position;
        if (std::abs(rel.y) > 4.0f)
            continue;
        const float along = rel.dot(fwd);
        if (along < 0.0f || along > in.range)
            continue;
        const Vec3 cf = flatUnit(c.forward);
        const float fx = std::abs(cf.dot(right)), fz = std::abs(cf.dot(fwd));
        const float across = c.halfLength * fx + c.halfWidth * fz;
        const float alongExtent = c.halfLength * fz + c.halfWidth * fx;
        const float vAlong = c.velocity.dot(fwd);
        const float closing = in.speed - vAlong;
        const float gap = std::max(0.0f, along - alongExtent - kOwnHalfLength);
        // Pulling away, or more than 3 s ahead: no need to steer round it yet.
        if (gap > 6.0f && closing < 0.5f)
            continue;
        if (gap > 10.0f && gap > 3.0f * closing)
            continue;
        const float lat = rel.dot(right) + in.lateral;
        const float margin = in.halfWidth + 0.75f;
        scan.blocked.push_back({lat - across - margin, lat + across + margin, gap, vAlong, c.id});
    }
    return scan;
}

// --- aiGoalFollowWayPts::CalcSpeed -------------------------------------------
//
// Decoded from game.asm (CalcSpeed@aiGoalFollowWayPts, 0x469160): for the
// turn between the current road and the next (its deflection d measured
// between the road directions; when |d| <= 0.5 the turn after it is used),
//   R    = max(W - sign(d) * DistToSide, 0.5)   W: road half width
//   h    = |pi - |d|| / 2
//   r    = R / (1 - sin h)                       racing-line radius
//   vmax = sqrt(23.76 r)                         flt_61BCBC = 23.76 m/s^2
//   entry distance = r cos h before the turn
//   brake = (Speed - vmax) / (23.76 * T),  T = (distance to entry) / Speed
// and brakes (Throttle 0, Brakes = clamp(brake, 0, 1)) only when brake > 0.7
// (flt_61BCC0), otherwise Throttle = MaxThrottle.
float turnBrake(const Course& course, float s, float side, float speed, float lateralAccel, float brakeDecel,
                float* vmax) {
    float worst = 0.0f;
    float limit = std::numeric_limits<float>::max();
    const float len = course.length();
    for (const CourseTurn& t : course.turns()) {
        float d = t.s - s;
        if (course.loop()) {
            d = std::fmod(d, len);
            if (d < -0.5f * len)
                d += len;
            else if (d > 0.5f * len)
                d -= len;
        }
        if (d < -15.0f || d > 200.0f || std::abs(t.deflection) <= 0.5f)
            continue;
        const float sign = t.deflection >= 0.0f ? 1.0f : -1.0f;
        const float R = std::max(t.halfWidth - sign * side, 0.5f);
        const float h = std::abs(kPi - std::min(std::abs(t.deflection), kPi)) * 0.5f;
        const float r = R / std::max(1.0f - std::sin(h), 1e-3f);
        const float v = std::sqrt(lateralAccel * r);
        const float entry = d - r * std::cos(h);
        limit = std::min(limit, v);
        if (speed <= v)
            continue;
        // MM1 divides by a time that is negative inside the turn; OpenMM2
        // brakes fully there instead (inferred guard).
        const float b = entry > 0.0f ? (speed - v) / (brakeDecel * (entry / std::max(speed, 0.1f))) : 1.0f;
        worst = std::max(worst, b);
    }
    if (vmax)
        *vmax = limit;
    return worst;
}

void BrakeMeter::update(const phys::CarSim& car, float dt) {
    const float speed = car.speed();
    // Measured over frames spent braking hard with the wheels on the ground,
    // going forwards fast enough for the tyres to matter.
    if (m_braking && m_lastSpeed > 4.0f && dt > 0.0f && car.onGround()) {
        const float decel = (m_lastSpeed - speed) / dt;
        if (decel > 0.0f && decel < 60.0f)
            m_decel = clampf(m_decel + (decel - m_decel) * 0.15f, 1.0f, 30.0f);
    }
    m_braking = car.brakes >= 0.7f && car.engine.throttle < 0.05f && car.trans.getCurrentGear() > 0;
    m_lastSpeed = speed;
}

void yawInPlace(phys::CarSim& car, float angle) {
    phys::age::rotate(car.body.ics.matrix, {0.0f, 1.0f, 0.0f}, angle);
}

bool blocked(const phys::GroundQuery* world, const Vec3& a, const Vec3& b, float height) {
    if (!world)
        return false;
    const Vec3 up{0.0f, height, 0.0f};
    phys::RayHit hit;
    return world->probe(a + up, b + up, hit);
}

void placeOnCourse(phys::CarSim& car, const Course& course, float s, float side, std::span<const TrackedCar> others,
                   int selfId, const std::function<void(const Mat34&)>& resetCar, const phys::GroundQuery* world) {
    Vec3 dir;
    const Vec3 p = course.pointAt(s, &dir);
    const Vec3 f = flatUnit(dir);
    const Vec3 r = rightOf(f);
    float left, right;
    course.edges(s, left, right);
    const float lo = -left + 1.5f, hi = right - 1.5f;
    // The car's own side of the road first (there may be a median).
    const float own = side >= 0.0f ? 1.0f : -1.0f;
    const float sideEdge = own > 0.0f ? hi : lo;
    float best = clampf(side, lo, hi), bestScore = -1e9f;
    for (float cand : {side, 0.5f * sideEdge, 0.75f * sideEdge, 0.25f * sideEdge, 0.0f, -0.5f * sideEdge}) {
        cand = clampf(cand, lo, hi);
        const Vec3 at = p + r * cand;
        float clear = 1e9f;
        for (const TrackedCar& c : others)
            if (c.id != selfId && std::abs(c.position.y - at.y) < 4.0f)
                clear = std::min(clear, Vec2{c.position.x - at.x, c.position.z - at.z}.mag());
        // A car-sized cross of probes must not touch any wall.
        const bool walls = blocked(world, at - r * 1.3f, at + r * 1.3f, 0.8f) ||
                           blocked(world, at - f * 2.8f, at + f * 2.8f, 0.8f);
        const float score = std::min(clear, 7.0f) - (walls ? 100.0f : 0.0f);
        if (score > bestScore + 1e-3f) {
            bestScore = score;
            best = cand;
        }
        if (score >= 7.0f)
            break;
    }
    Mat34 m = Mat34::rotationY(-std::atan2(f.x, -f.z));
    m.m3 = p + r * best;
    // Rest on the ground below the line point when it can be found.
    if (world) {
        phys::RayHit hit;
        if (world->probe(m.m3 + Vec3{0, 3, 0}, m.m3 - Vec3{0, 6, 0}, hit))
            m.m3 = hit.position;
    }
    const float current = car.damage.currentDamage, damage = car.damage.damage;
    if (resetCar)
        resetCar(m);
    else
        car.reset(m);
    car.damage.currentDamage = current;
    car.damage.damage = damage;
}

} // namespace mm2::ai
