// MM2's AI driving controller, aiVehiclePhysics (build 3393), on an
// ai::Course. See Driving.h and docs/ai.md ("Opponents and police") for
// what is ported and what is inferred.
#include "ai/Driving.h"

#include "phys/AgeMath.h"
#include "phys/World.h"
#include "phys/vehicle/CarSim.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace mm2::ai {
namespace {

// Right of a driving direction (Angel: forward -Z, right +X).
Vec3 rightOf(const Vec3& dir) {
    return {-dir.z, 0.0f, dir.x};
}

Vec3 flatUnit(const Vec3& v) {
    const Vec2 f{v.x, v.z};
    const float m = f.mag();
    return m > 1e-6f ? Vec3{f.x / m, 0.0f, f.y / m} : Vec3{0, 0, -1};
}

float xzDist(const Vec3& a, const Vec3& b) {
    return Vec2{a.x - b.x, a.z - b.z}.mag();
}

// Vector3::Angle: the angle between two vectors (0 when nearly parallel).
float vectorAngle(const Vec3& a, const Vec3& b) {
    const float m = a.mag2() * b.mag2();
    if (m <= 0.0f)
        return 0.0f;
    const float c = a.dot(b) * (1.0f / std::sqrt(m));
    if (c > 0.9999999f)
        return 0.0f;
    if (c < -1.0f)
        return 3.1415927f;
    return std::acos(c);
}

// The steering MM2 computes from a heading error in
// aiVehiclePhysics::Forward and Mirror (x 1.33 x 1.428), before clamping.
float steeringGain(float angle) {
    return angle * 1.33f * 1.428f;
}

// Corners of a tracked car's box (front left, front right, back left, back
// right), as aiVehicle::PreAvoid / IsBlockingTarget build them from the
// bumper and side distances.
std::array<Vec3, 4> corners(const TrackedCar& c) {
    const Vec3 f = flatUnit(c.forward);
    const Vec3 r = rightOf(f);
    const Vec3 front = c.position + f * c.halfLength, back = c.position - f * c.halfLength;
    return {front - r * c.halfWidth, front + r * c.halfWidth, back - r * c.halfWidth, back + r * c.halfWidth};
}

// The course distance from arc length `from` forward to `to` (loops wrap).
float aheadOf(const Course& c, float from, float to) {
    float d = to - from;
    if (c.loop()) {
        const float len = c.length();
        d = std::fmod(d, len);
        if (d < 0.0f)
            d += len;
    }
    return d;
}

constexpr float kRouteHeight = 1.0f;  // MM2 keeps every target point 1 m up
constexpr int kMaxRouteNodes = 40;    // aiVehiclePhysics: 40 aiRouteNodes per route
constexpr int kMaxRoutes = 25;        // and 25 routes
constexpr int kMaxRoutesToExtend = 10; // ContinueCheck stops branching after ten
constexpr int kMaxAvoidDepth = 10;     // EnumTargets recursion (dat_5d8cf8)
constexpr float kDestinationNode = 9999.0f; // SetTargetPtToDestination's distance marker

// vehStuck states (vehStuck::Update): 1 watching after an impact, 2 stuck
// with the throttle pegged.
constexpr int kVehStuckIdle = 0;
constexpr int kVehStuckWatching = 1;
constexpr int kVehStuckPegged = 2;

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

void configureAiVehStuck(phys::CarSim& car, float timeThresh) {
    // aiVehiclePhysics::Init: TimeThresh 0.5, squared PosThresh 1.0, Rotation
    // 0 (vehStuck's rotation recovery, not part of OpenMM2's vehStuck).
    car.stuck.timeThresh = timeThresh;
    car.stuck.posThresh = 1.0f;
    car.stuck.posThreshSqr = 1.0f;
}

// --- aiStuck -----------------------------------------------------------------

void AiStuck::reset() {
    m_state = Idle;
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
    // aiStuck::Update.
    const Vec3 pos = car.body.ics.matrix.m3;
    const float posSq = posThresh * posThresh;
    const float moveSq = moveThresh * moveThresh;
    if (m_lastPos.dist2(pos) > posSq)
        m_state = Idle;
    // MM2 starts watching when the car's own vehStuck does (after an impact).
    if (car.stuck.state == kVehStuckWatching && m_state == Idle) {
        m_time = 0.0f;
        m_lastPos = pos;
        m_state = Watching;
    }
    if (m_state == Watching) {
        m_time += dt;
        const float dx = m_lastPos.x - pos.x, dz = m_lastPos.z - pos.z;
        if (dx * dx + dz * dz < posSq && !(m_time < timeThresh) && pegged(car)) {
            m_state = Stuck;
            return;
        }
        if (!(m_lastPos.dist2(pos) > moveSq))
            return;
    } else if (m_state == Stuck) {
        if (!(m_lastPos.dist2(pos) > moveSq) && pegged(car)) {
            const float sign = car.steering > 0.0f ? 1.0f : (car.steering < 0.0f ? -1.0f : 0.0f);
            yawInPlace(car, -(sign * rotAmount * dt));
            return;
        }
    } else {
        return;
    }
    m_state = Idle;
}

// --- aiVehicle obstacle geometry -------------------------------------------

float blockingDistance(const TrackedCar& obstacle, const Vec3& from, const Vec3& to, float extra,
                       float width) {
    // aiVehicle::IsBlockingTarget.
    const Vec3 d = flatUnit(to - from);
    const Vec3 r = rightOf(d);
    const float length = xzDist(from, to);
    const float halfWidth = width * 0.5f + 1.0f;
    for (const Vec3& c : corners(obstacle)) {
        const Vec3 rel = c - from;
        const float lateral = rel.x * r.x + rel.z * r.z;
        const float along = rel.x * d.x + rel.z * d.z;
        const float angle = std::atan2(lateral, along);
        if (-halfWidth < lateral && lateral < halfWidth && along > 0.0f && along < length + extra &&
            angle > -0.7f && angle < 0.7f)
            return along;
    }
    return -1.0f;
}

void avoidPoints(const TrackedCar& obstacle, const Vec3& from, const Vec3& dir, float clearance, Vec3& left,
                 Vec3& right) {
    // aiVehicle::PreAvoid: every corner pushed `clearance` both ways across
    // the line of sight to it; the leftmost and rightmost of these.
    std::array<Vec3, 8> points;
    const auto box = corners(obstacle);
    for (std::size_t i = 0; i < box.size(); ++i) {
        const Vec3 u = flatUnit(box[i] - from);
        const Vec3 across = rightOf(u) * clearance;
        points[2 * i] = box[i] + across;
        points[2 * i + 1] = box[i] - across;
    }
    const Vec3 d = flatUnit(dir);
    const Vec3 r = rightOf(d);
    float lo = 99999.0f, hi = -99999.0f;
    left = right = obstacle.position;
    for (const Vec3& p : points) {
        const Vec3 rel = p - from;
        const float a = std::atan2(rel.x * r.x + rel.z * r.z, rel.x * d.x + rel.z * d.z);
        if (a < lo) {
            lo = a;
            left = p;
        }
        if (a > hi) {
            hi = a;
            right = p;
        }
    }
}

// --- aiVehiclePhysics ----------------------------------------------------------

PhysicsDriver::PhysicsDriver(phys::CarSim& car, int selfId) : m_car(car), m_selfId(selfId) {
    reset();
}

void PhysicsDriver::reset() {
    // aiVehiclePhysics::Reset.
    m_state = State::Forward;
    m_lastState = State::Stop;
    m_throttle = m_brake = m_steering = 0.0f;
    m_target = m_car.body.ics.matrix.m3;
    m_backupFrames = 0;
    m_backupTime = 0.0f;
    m_wrecked = false;
    m_wreckTime = 0.0f;
    m_stuck.reset();
    m_routes.clear();
    m_best = {};
}

void PhysicsDriver::apply(float handBrake) {
    m_car.setInputs(m_throttle, m_brake, m_steering, handBrake);
}

void PhysicsDriver::applyBrake(float brake, float dt) {
    // CalcSpeed / CalcRoadSpeed: off the throttle, brake, and damp the yaw.
    m_throttle = 0.0f;
    m_brake = clampf(brake, 0.0f, 1.0f);
    auto& ics = m_car.body.ics;
    ics.angularMomentum = ics.angularMomentum * perFrame(0.85f, dt);
}

void PhysicsDriver::driveRoute(float dt, std::span<const TrackedCar> cars, const DriveContext& ctx) {
    // aiVehiclePhysics::DriveRoute.
    m_car.engine.maxThrottle = params.maxThrottle;
    auto& ics = m_car.body.ics;
    if (m_car.damage.wrecked()) {
        // Wrecked: circuits repair the car after 5 s (aiVehiclePhysics::Init
        // param_4); otherwise it coasts to a stop.
        if (ctx.repairWhenWrecked && m_wrecked && m_wreckTime > 5.0f) {
            m_wrecked = false;
            m_car.damage.reset();
            return;
        }
        if (!m_wrecked) {
            m_wreckTime = 0.0f;
            m_wrecked = true;
        }
        m_wreckTime += dt;
        m_throttle = m_brake = m_steering = 0.0f;
        m_car.setInputs(0.0f, 0.0f, 0.0f, m_car.handBrake);
        ics.linearMomentum = ics.linearMomentum * perFrame(0.95f, dt);
        return;
    }
    switch (m_state) {
    case State::Forward:
        if (m_lastState != State::Forward) {
            // InitForward.
            m_throttle = m_brake = m_steering = 0.0f;
            m_car.setInputs(0.0f, 0.0f, 0.0f, 0.0f);
            m_car.stuck.reset();
            m_target = ics.matrix.m3;
            m_routes.clear();
            if (m_car.trans.getCurrentGear() <= 0)
                m_car.trans.setDrive();
            m_lastState = State::Forward;
        }
        forward(dt, cars, ctx);
        break;
    case State::Backup:
        if (m_lastState != State::Backup) {
            initBackup();
            m_lastState = State::Backup;
        }
        backup(dt);
        if (m_state != State::Backup)
            finishedBackingUp(ctx);
        break;
    case State::Shortcut:
        if (m_lastState != State::Shortcut) {
            // InitShortcut.
            m_routes.clear();
            m_car.stuck.reset();
            if (m_car.trans.getCurrentGear() <= 0)
                m_car.trans.setDrive();
            m_lastState = State::Shortcut;
        }
        shortcut(dt, ctx);
        break;
    case State::Stop:
        stop(ctx);
        break;
    }
}

void PhysicsDriver::forward(float dt, std::span<const TrackedCar> cars, const DriveContext& ctx) {
    // aiVehiclePhysics::Forward.
    auto& ics = m_car.body.ics;
    m_stuck.update(m_car, dt);
    if (m_car.stuck.state == kVehStuckPegged) {
        // The car's own vehStuck: back up, momenta cleared.
        m_state = State::Backup;
        ics.linearMomentum = {};
        ics.angularMomentum = {};
        ++m_backups;
        return;
    }
    if (m_stuck.state() == AiStuck::Stuck) {
        m_steering = 1.0f;
        m_brake = 0.0f;
        m_throttle = 1.0f;
        apply();
        m_car.stuck.state = kVehStuckIdle;
        return;
    }
    if (!ctx.course) {
        // No road route: straight for the destination.
        m_state = State::Shortcut;
        m_lastState = State::Forward;
        shortcut(dt, ctx);
        return;
    }
    // Far off the course (no road under the car): drive straight back to it
    // (MM2 switches to Shortcut when the car is in no road's room; inferred
    // threshold: beyond the outer edge of the road plus 10 m).
    {
        float left, right;
        ctx.course->edges(ctx.s, left, right);
        const float limit = (ctx.lateral > 0.0f ? right : left) + 10.0f;
        if (std::abs(ctx.lateral) > limit) {
            m_state = State::Shortcut;
            m_lastState = State::Forward;
            shortcut(dt, ctx);
            return;
        }
    }

    // SolveRoadTargetPoint: plan, aim at the first point of the best route,
    // and set the speed for it.
    planRoutes(cars, ctx);
    if (m_best.nodes.size() > 1)
        m_target = m_best.nodes[1].pos;
    calcSpeed(dt, ctx);

    const Vec3 pos = ics.matrix.m3;
    const Vec3 fwd = -ics.matrix.m2;
    Vec3 aim = m_target;
    // Past the destination, heading the way it wants: brake, steering away
    // from the target (police chasing a car: do not turn round on it).
    const bool atDestination = m_best.nodes.size() > 1 && m_best.nodes[1].destination;
    const float dx = pos.x - ctx.destination.x, dz = pos.z - ctx.destination.z;
    const Vec3& h = ctx.destinationHeading;
    if (atDestination && dx * dx + dz * dz < 625.0f && fwd.x * h.x + fwd.z * h.z > 0.0f &&
        (ctx.destination.x - pos.x) * fwd.x + (ctx.destination.z - pos.z) * fwd.z < 0.0f) {
        m_brake = 1.0f;
        m_throttle = 0.0f;
        aim = pos - (m_target - pos);
    }
    const float raw = steeringGain(headingError(ics.matrix, aim));
    m_steering = clampf(raw, -1.0f, 1.0f);
    // Over 30 m/s and wanting more than full lock: the handbrake.
    const float handBrake = m_car.speed() > 30.0f && (raw < -1.0f || raw > 1.0f) ? 1.0f : 0.0f;
    apply(handBrake);
    // CarFrictionHandling 2 while in contact with the player, else 1.
    m_car.params.carFrictionHandling = ctx.touchingPlayer ? 2.0f : 1.0f;
}

void PhysicsDriver::initBackup() {
    // aiVehiclePhysics::InitBackup: back away from the first point of the
    // route planned last.
    m_backupFrames = 0;
    m_backupTime = 0.0f;
    if (m_best.nodes.size() > 1)
        m_target = m_best.nodes[1].pos;
    if (m_car.trans.getCurrentGear() != -1)
        m_car.trans.setReverse();
}

void PhysicsDriver::backup(float dt) {
    // aiVehiclePhysics::Backup: reverse with opposite lock until the car
    // points within 0.1 rad of the target (then it is turned onto it), for at
    // most 65 frames (66 / 30 s at OpenMM2's frame rate).
    auto& ics = m_car.body.ics;
    const Vec3 d = m_target - ics.matrix.m3;
    const float angle = std::atan2(d.dot(ics.matrix.m0), -d.dot(ics.matrix.m2));
    if (angle <= 0.1f && angle >= -0.1f) {
        m_steering = 0.0f;
        phys::age::rotate(ics.matrix, ics.matrix.m1, angle);
        m_state = State::Forward;
        return;
    }
    m_steering = clampf(angle * -2.857143f, -1.0f, 1.0f);
    m_backupTime += dt;
    if (m_backupTime > 66.0f / 30.0f) {
        m_state = State::Forward;
        return;
    }
    if (m_car.trans.getCurrentGear() != -1)
        m_car.trans.setReverse();
    m_throttle = 0.85f;
    m_brake = 0.0f;
    ++m_backupFrames;
    apply();
}

void PhysicsDriver::finishedBackingUp(const DriveContext& ctx) {
    // aiVehiclePhysics::FinishedBackingUp: forward again (Shortcut off the
    // roads), vehStuck reset, both momenta quartered, brakes on.
    m_state = ctx.course ? State::Forward : State::Shortcut;
    m_car.stuck.reset();
    auto& ics = m_car.body.ics;
    ics.linearMomentum = ics.linearMomentum * 0.25f;
    ics.angularMomentum = ics.angularMomentum * 0.25f;
    m_throttle = 0.0f;
    m_brake = 1.0f;
    apply();
}

void PhysicsDriver::shortcut(float dt, const DriveContext& ctx) {
    // aiVehiclePhysics::Shortcut / SolveShortcutTargetPoint: straight for the
    // next waypoint (here the course ahead), or the destination; steering
    // gain 1.33, at most 0.75.
    auto& ics = m_car.body.ics;
    m_stuck.update(m_car, dt);
    if (m_car.stuck.state == kVehStuckPegged) {
        m_state = State::Backup;
        ics.linearMomentum = {};
        ics.angularMomentum = {};
        ++m_backups;
        return;
    }
    if (m_stuck.state() == AiStuck::Stuck) {
        m_steering = 1.0f;
        m_brake = 0.0f;
        m_throttle = 1.0f;
        apply();
        m_car.stuck.state = kVehStuckIdle;
        return;
    }
    if (ctx.course) {
        float left, right;
        ctx.course->edges(ctx.s, left, right);
        if (std::abs(ctx.lateral) <= (ctx.lateral > 0.0f ? right : left)) {
            // Back on the road.
            m_state = State::Forward;
            return;
        }
    }
    Vec3 aim = ctx.destination;
    if (ctx.course && !ctx.finalApproach) {
        // The next intersection of the course (inferred stand-in for the next
        // waypoint's centre).
        float best = ctx.course->length();
        for (const CourseLeg& leg : ctx.course->legs()) {
            const float ahead = aheadOf(*ctx.course, ctx.s, leg.end);
            if (ahead > 5.0f && ahead < best)
                best = ahead;
        }
        aim = ctx.course->pointAt(ctx.s + std::min(best, ctx.remaining));
    } else if (params.stopShort > 0.0f) {
        const Vec3 pos = ics.matrix.m3;
        const float d = xzDist(pos, ctx.destination);
        aim = d >= params.stopShort ? pos + (ctx.destination - pos) * ((d - params.stopShort) / d) : pos;
    }
    m_target = aim + Vec3{0.0f, kRouteHeight, 0.0f};
    m_routes.clear();
    m_best = {};
    calcSpeed(dt, ctx);
    m_steering = clampf(headingError(ics.matrix, m_target) * 1.33f, -0.75f, 0.75f);
    apply();
}

void PhysicsDriver::stop(const DriveContext& ctx) {
    // aiVehiclePhysics::Stop: brakes on, steering for the destination.
    m_target = ctx.destination;
    m_steering = clampf(headingError(m_car.body.ics.matrix, m_target) * 1.33f, -1.0f, 1.0f);
    m_throttle = 0.0f;
    m_brake = 1.0f;
    m_routes.clear();
    m_best = {};
    apply();
}

void PhysicsDriver::mirror(float dt, const TrackedCar& target) {
    // aiVehiclePhysics::Mirror: hold 3 m/s under `target`'s speed and take
    // its heading (a police car blocking a suspect).
    auto& ics = m_car.body.ics;
    m_target = target.position;
    const float want = target.velocity.mag() - 3.0f;
    const float speed = m_car.speed();
    if (speed <= want) {
        m_brake = 0.0f;
        m_throttle = 0.5f;
    } else {
        const float brake = (speed - want) / kAiGrip;
        m_throttle = 0.0f;
        if (brake <= 0.3f) {
            m_brake = 0.0f;
        } else {
            applyBrake(brake, dt);
        }
    }
    const Vec3 f = flatUnit(target.forward);
    const float angle = std::atan2(f.dot(ics.matrix.m0), -f.dot(ics.matrix.m2));
    m_steering = clampf(steeringGain(angle), -1.0f, 1.0f);
    apply();
}

// --- speed ---------------------------------------------------------------------

void PhysicsDriver::calcSpeed(float dt, const DriveContext& ctx) {
    // aiVehiclePhysics::CalcSpeed: the bend at the first route point, from
    // the angle between the way to it and the way on from it. Its corner
    // speed takes the radius of a curve 10 m from the bend:
    // v = sqrt(10 tan((3.14 - a) / 2) * 23.76) * cornerSpeedFactor.
    const auto& n = m_best.nodes;
    if (n.size() > 2) {
        const float a = vectorAngle(n[1].pos - n[0].pos, n[2].pos - n[1].pos);
        if (a > kSharpTurn) {
            const float speed = m_car.speed();
            const float r = std::tan((3.14f - std::abs(a)) * 0.5f) * 10.0f;
            const float v = std::sqrt(r * kAiGrip) * params.cornerSpeedFactor;
            float brake = 0.0f;
            if (v < speed)
                brake = (speed - v) / ((n[1].dist / speed) * kAiGrip);
            if (params.brakeThreshold < brake) {
                applyBrake(brake, dt);
                return;
            }
        }
    }
    calcRoadSpeed(dt, ctx);
}

void PhysicsDriver::calcRoadSpeed(float dt, const DriveContext& ctx) {
    // aiVehiclePhysics::CalcRoadSpeed.
    m_brake = 0.0f;
    m_throttle = m_car.engine.maxThrottle;
    const Vec3 pos = m_car.body.ics.matrix.m3;
    const float speed = m_car.speed();
    const float dx = pos.x - ctx.destination.x, dz = pos.z - ctx.destination.z;
    const float d2 = dx * dx + dz * dz;
    if (d2 >= 5000.0f || !ctx.finalApproach) {
        // The bends of the road ahead.
        if (ctx.course) {
            const float brake = turnBrake(*ctx.course, ctx.s, ctx.lateral, speed, params.cornerSpeedFactor,
                                          std::min(params.lookAhead, ctx.remaining + 10.0f));
            if (params.brakeThreshold < brake)
                applyBrake(brake, dt);
        }
        return;
    }
    // Arriving: brake to the destination speed at the destination (less the
    // stop distance); stop dead within 2.5 m of it.
    const float d = std::sqrt(d2) - params.stopShort;
    float brake = 0.0f;
    if (params.destinationSpeed < speed)
        brake = (speed - params.destinationSpeed) / ((d / speed) * kAiGrip);
    if (d * 0.014f < brake)
        applyBrake(brake, dt);
    if (d < 2.5f && params.destinationSpeed <= 0.0f) {
        m_throttle = 0.0f;
        m_brake = 1.0f;
    }
}

float turnBrake(const Course& course, float s, float side, float speed, float cornerSpeedFactor,
                float lookAhead, float* vmax) {
    float worst = 0.0f;
    float limit = std::numeric_limits<float>::max();
    const float len = course.length();
    for (const CourseTurn& t : course.turns()) {
        if (std::abs(t.deflection) <= kSharpTurn)
            continue;
        float d = t.s - s;
        if (course.loop()) {
            d = std::fmod(d, len);
            if (d < -0.5f * len)
                d += len;
            else if (d > 0.5f * len)
                d -= len;
        }
        const float sign = t.deflection >= 0.0f ? 1.0f : -1.0f;
        const float R = std::max(t.halfWidth - sign * side, 0.5f);
        const float h = (3.14f - std::min(std::abs(t.deflection), 3.14f)) * 0.5f;
        const float r = R / std::max(1.0f - std::sin(h), 1e-3f);
        const float setback = r * std::cos(h);
        // aiVehiclePhysics::CheckDistance: turns within the set-back plus the
        // look-ahead (and the one just entered).
        if (d < -15.0f || d - setback > lookAhead)
            continue;
        float v = std::sqrt(r * kAiGrip) * cornerSpeedFactor;
        // A turn into an alley (path flag 0x2) at half speed.
        if (t.intoAlley)
            v *= 0.5f;
        limit = std::min(limit, v);
        if (speed <= v)
            continue;
        // The time to the turn-in point; once past it the turn is not braked
        // for (MM2's brake comes out negative there).
        const float entry = d - setback;
        if (entry <= 0.0f)
            continue;
        const float b = (speed - v) / ((entry / speed) * kAiGrip);
        worst = std::max(worst, b);
    }
    if (vmax)
        *vmax = limit;
    return worst;
}

// --- route planning --------------------------------------------------------------

void PhysicsDriver::planRoutes(std::span<const TrackedCar> cars, const DriveContext& ctx) {
    // aiVehiclePhysics::CalcRoute.
    m_routes.clear();
    m_best = {};
    if (!ctx.course)
        return;
    const Mat34& m = m_car.body.ics.matrix;
    RouteNode car;
    car.pos = m.m3 + Vec3{0.0f, kRouteHeight, 0.0f};
    car.s = 0.0f;
    std::vector<RouteNode> nodes{car};
    enumRoutes(nodes, cars, ctx, 1);

    // DetermineBestRoute: the route that turns least; first among those over
    // the sidewalk if preferred, then among those with a way round every
    // obstacle, then of all.
    auto pick = [&](auto&& accept) {
        int best = -1;
        float least = 99999.0f;
        for (std::size_t i = 0; i < m_routes.size(); ++i) {
            const auto& r = m_routes[i];
            if (!accept(r) || r.nodes.empty())
                continue;
            if (r.nodes.back().angle < least) {
                least = r.nodes.back().angle;
                best = static_cast<int>(i);
            }
        }
        return best;
    };
    int best = -1;
    if (params.preferSidewalk)
        best = pick([](const PlannedRoute& r) { return r.offRoad; });
    if (best < 0)
        best = pick([](const PlannedRoute& r) { return !r.blocked; });
    if (best < 0)
        best = pick([](const PlannedRoute&) { return true; });
    if (best >= 0)
        m_best = m_routes[static_cast<std::size_t>(best)];
}

void PhysicsDriver::finishRoute(const std::vector<RouteNode>& nodes) {
    if (static_cast<int>(m_routes.size()) >= kMaxRoutes)
        return;
    PlannedRoute r;
    r.nodes = nodes;
    for (const RouteNode& n : nodes) {
        r.offRoad = r.offRoad || n.offRoad;
        r.blocked = r.blocked || n.noWayAround;
    }
    m_routes.push_back(std::move(r));
}

int PhysicsDriver::roadState(const Vec3& p, const DriveContext& ctx, float hint) const {
    // aiPath::IsPosOnRoad: 1 between the curbs, 2 on the sidewalk, 3 beyond,
    // each less the car's side distance.
    const Course& c = *ctx.course;
    float lateral = 0.0f;
    const float s = c.locate(p, hint, 60.0f, &lateral);
    float left, right, leftEdge, rightEdge;
    c.edges(s, left, right, &leftEdge, &rightEdge);
    const float side = m_car.body.shape.half.x;
    const float a = std::abs(lateral);
    const float curb = (lateral > 0.0f ? right : left) - side;
    const float edge = (lateral > 0.0f ? rightEdge : leftEdge) - side;
    if (a < curb)
        return 1;
    if (a < edge)
        return 2;
    return 3;
}

RouteNode PhysicsDriver::roadTarget(const RouteNode& from, const RouteNode* before,
                                    const DriveContext& ctx) const {
    return courseTarget(from, before, -m_car.body.ics.matrix.m2, m_car.body.shape.half.x, params, ctx);
}

RouteNode courseTarget(const RouteNode& from, const RouteNode* before, const Vec3& carForward, float side,
                       const RouteParams& params, const DriveContext& ctx) {
    // aiVehiclePhysics::CalcRoadTarget (with CalcDestinationTarget and
    // SetTargetPtToDestination): the farthest point down the road that the
    // car can reach in a straight line from `from` without crossing a curb
    // (each curb moved in by the car's side distance + 1 m). Walking the
    // road's vertices, the window of directions between the left and the
    // right curb points narrows; when the next curb point falls outside it,
    // the road bends, and the target is the curb point that set that side of
    // the window (the inside of the bend). Otherwise the walk ends at the
    // look-ahead distance, the target there keeps the car's place across the
    // road (inside the window), or at the destination when that comes first.
    const Course& c = *ctx.course;
    const float insetL = side + 1.0f, insetR = side + 1.0f;
    const float fromAbs = ctx.s + from.s;
    const float budget = params.lookAhead - from.dist;
    const float toDestination = ctx.remaining - from.s;

    RouteNode out;
    out.dist = from.dist;
    out.angle = from.angle;
    auto finish = [&](const Vec3& p, float ahead, bool destination) {
        out.pos = p;
        out.pos.y = p.y + kRouteHeight;
        out.s = from.s + ahead;
        out.destination = destination;
        const Vec3 way = out.pos - from.pos;
        out.dist = destination ? kDestinationNode : from.dist + xzDist(from.pos, out.pos);
        if (before)
            out.angle = from.angle + vectorAngle(from.pos - before->pos, way);
        else
            out.angle = vectorAngle(carForward, way);
        return out;
    };
    // The destination, less the stop distance along the way to it.
    auto destinationPoint = [&] {
        Vec3 d = ctx.destination;
        if (params.stopShort > 0.0f) {
            const float len = xzDist(from.pos, d);
            d = len >= params.stopShort ? from.pos + (d - from.pos) * ((len - params.stopShort) / len)
                                        : from.pos;
        }
        return d;
    };
    if (ctx.finalApproach && toDestination <= 0.0f)
        return finish(destinationPoint(), 0.0f, true);

    // The first vertex ahead of the origin in its own section's frame
    // (aiPath::RoadVertice: no more than 0.1 m behind the origin), so that
    // a car level with the inside of a bend looks past it.
    std::size_t i = c.vertexAfter(fromAbs - 5.0f);
    for (std::size_t steps = 0; steps < c.vertexCount(); ++steps) {
        const Vec3 r = c.vertexRight(i);
        const Vec3 d{r.z, 0.0f, -r.x}; // forward of the section
        const Vec3 rel = c.vertex(i) - from.pos;
        if (rel.x * d.x + rel.z * d.z > 0.1f || c.nextVertex(i) == i)
            break;
        i = c.nextVertex(i);
    }
    float ahead = c.vertexDistance(i) - fromAbs;
    if (c.loop()) {
        const float len = c.length();
        ahead = std::fmod(ahead, len);
        if (ahead < -0.5f * len)
            ahead += len;
        else if (ahead >= 0.5f * len)
            ahead -= len;
    }
    if (!c.loop() && c.vertexDistance(i) <= fromAbs && c.nextVertex(i) == i) {
        // At the end of the line.
        if (ctx.finalApproach)
            return finish(destinationPoint(), 0.0f, true);
        return finish(c.pointAt(fromAbs + 1.0f), 1.0f, false);
    }
    // Directions are measured in the frame of that first section.
    const Vec3 right = c.vertexRight(i);
    const Vec3 fwd{right.z, 0.0f, -right.x};
    auto angleOf = [&](const Vec3& p) {
        const Vec3 rel = p - from.pos;
        return std::atan2(rel.x * right.x + rel.z * right.z, std::max(rel.x * fwd.x + rel.z * fwd.z, 1.0f));
    };
    // The origin's place across the road (aiVehiclePhysics 0x9738 for the
    // car), and which half of a divided road it keeps to.
    // Measured across the first section ahead, as MM2 measures it across
    // the road's vertex.
    const Vec3 firstAcross = c.vertexRight(i);
    const Vec3 relFirst = from.pos - c.vertex(i);
    const float lateral0 = relFirst.x * firstAcross.x + relFirst.z * firstAcross.z;
    struct Cross {
        Vec3 left, right, centre, across;
        float lo, hi;
    };
    auto crossAt = [&](std::size_t k) {
        const CoursePoint& info = c.vertexInfo(k);
        Cross x;
        x.centre = c.vertex(k);
        x.across = c.vertexRight(k);
        x.lo = -(info.left - insetL);
        x.hi = info.right - insetR;
        if ((info.flags & 0x1) != 0) {
            // aiPath flag 0x1: the centre line is a curb too.
            if (lateral0 >= 0.0f)
                x.lo = std::max(x.lo, insetL);
            else
                x.hi = std::min(x.hi, -insetR);
        }
        if (x.lo > x.hi)
            x.lo = x.hi = 0.5f * (x.lo + x.hi);
        x.left = x.centre + x.across * x.lo;
        x.right = x.centre + x.across * x.hi;
        return x;
    };
    // A point of cross-section `x` in the window's direction nearest the
    // origin's place across the road.
    auto inWindow = [&](const Cross& x, float lo, float hi) {
        const Vec3 want = x.centre + x.across * clampf(lateral0, x.lo, x.hi);
        const float a = clampf(angleOf(want), lo, hi);
        if (a == angleOf(want))
            return want;
        // Where the ray from the origin at angle `a` crosses the section.
        const Vec3 dir = fwd * std::cos(a) + right * std::sin(a);
        const Vec3 seg = x.right - x.left;
        const float den = dir.x * seg.z - dir.z * seg.x;
        if (std::abs(den) < 1e-6f)
            return want;
        const Vec3 rel = x.left - from.pos;
        const float u = clampf((dir.z * rel.x - dir.x * rel.z) / den, 0.0f, 1.0f);
        return x.left + seg * u;
    };

    if (ctx.finalApproach && ahead >= toDestination)
        return finish(destinationPoint(), toDestination, true);
    const Cross first = crossAt(i);
    float maxL = angleOf(first.left), minR = angleOf(first.right);
    Cross atL = first, atR = first;
    float aheadL = ahead, aheadR = ahead;
    if (maxL > minR) {
        // The section is not ahead of the origin (a hairpin): its point at
        // the origin's place across the road.
        return finish(first.centre + first.across * clampf(lateral0, first.lo, first.hi), ahead, false);
    }
    if (xzDist(from.pos, first.centre) >= budget)
        return finish(inWindow(first, maxL, minR), ahead, false);
    const std::size_t n = c.vertexCount();
    for (std::size_t steps = 0; steps < n; ++steps) {
        const std::size_t j = c.nextVertex(i);
        if (j == i)
            break;
        const float next = ahead + aheadOf(c, c.vertexDistance(i), c.vertexDistance(j));
        if (ctx.finalApproach && next >= toDestination) {
            // The destination lies before the next vertex.
            const Vec3 d = destinationPoint();
            const float a = angleOf(d);
            if (a < maxL)
                return finish(atL.left, aheadL, false);
            if (a > minR)
                return finish(atR.right, aheadR, false);
            return finish(d, toDestination, true);
        }
        i = j;
        ahead = next;
        const Cross x = crossAt(i);
        const float aL = angleOf(x.left), aR = angleOf(x.right);
        if (aL > minR)
            return finish(atR.right, aheadR, false); // bends right round atR
        if (aR < maxL)
            return finish(atL.left, aheadL, false); // bends left round atL
        if (aL > maxL - 0.001f) {
            maxL = aL;
            atL = x;
            aheadL = ahead;
        }
        if (aR < minR + 0.001f) {
            minR = aR;
            atR = x;
            aheadR = ahead;
        }
        if (xzDist(from.pos, x.centre) >= budget)
            return finish(inWindow(x, maxL, minR), ahead, false);
        if (!c.loop() && c.nextVertex(i) == i)
            return finish(inWindow(x, maxL, minR), ahead, false);
    }
    return finish(c.pointAt(fromAbs + std::max(budget, 1.0f)), std::max(budget, 1.0f), false);
}

const TrackedCar* PhysicsDriver::blocking(const Vec3& from, const Vec3& to, std::span<const TrackedCar> cars,
                                          const DriveContext& ctx, float* along) const {
    // aiVehiclePhysics::IsTargetBlocked: the nearest vehicle in the way, of
    // the kinds this route avoids (ambient traffic, players, other racers
    // once past the third waypoint). Police cars are no obstacle in MM2.
    const float myLength = 2.0f * m_car.body.shape.half.z;
    const float myWidth = 2.0f * m_car.body.shape.half.x;
    const TrackedCar* best = nullptr;
    float nearest = 99999.0f;
    for (const TrackedCar& c : cars) {
        if (c.id == m_selfId || c.isPolice)
            continue;
        if (c.isAmbient() && !params.avoidTraffic)
            continue;
        if (c.isPlayer && !params.avoidPlayers)
            continue;
        if (c.isOpponent() && (!params.avoidOpponents || ctx.waypointsPassed < 3))
            continue;
        if (std::abs(c.position.y - from.y + kRouteHeight) > 6.0f)
            continue;
        // A quick reject before the corner tests.
        if (xzDist(c.position, from) > xzDist(from, to) + 2.0f * myLength + c.halfLength + 2.0f)
            continue;
        const float d = blockingDistance(c, from, to, 2.0f * myLength, myWidth);
        if (d > -1.0f && d < nearest) {
            nearest = d;
            best = &c;
        }
    }
    if (along)
        *along = nearest;
    return best;
}

void PhysicsDriver::enumRoutes(std::vector<RouteNode>& nodes, std::span<const TrackedCar> cars,
                               const DriveContext& ctx, int depth) {
    // aiVehiclePhysics::EnumRoutes.
    if (depth >= kMaxRouteNodes)
        return;
    const RouteNode from = nodes.back();
    const RouteNode* before = nodes.size() > 1 ? &nodes[nodes.size() - 2] : nullptr;
    const RouteNode target = roadTarget(from, before, ctx);

    auto continueCheck = [&](const RouteNode& node) {
        // aiVehiclePhysics::ContinueCheck: carry on until the look-ahead is
        // covered (ten routes at most branch further), then keep the route.
        nodes.push_back(node);
        const bool more = node.dist < params.lookAhead && !node.destination &&
                          (ctx.course->loop() || ctx.s + node.s < ctx.course->length() - 0.5f);
        if (more && static_cast<int>(m_routes.size()) < kMaxRoutesToExtend)
            enumRoutes(nodes, cars, ctx, depth + 1);
        else
            finishRoute(nodes);
        nodes.pop_back();
    };

    const TrackedCar* obstacle = blocking(from.pos, target.pos, cars, ctx, nullptr);
    if (!obstacle || xzDist(from.pos, obstacle->position) >= params.lookAhead) {
        continueCheck(target);
        return;
    }

    // CalcObstacleAvoidPoints / EnumTargets: pass the obstacle on the left
    // and on the right (each corner 2 m clear of the car's side), each a
    // route of its own, as long as the point is on the road (or on the
    // sidewalk, except for semis) and the way to it is clear; a way blocked
    // by a further vehicle goes round that one on the same side.
    Vec3 roadDir;
    ctx.course->pointAt(ctx.s + from.s, &roadDir);
    roadDir = flatUnit(roadDir);
    const Vec3 lookDir = before ? flatUnit(from.pos - before->pos) : flatUnit(-m_car.body.ics.matrix.m2);
    const float clearance = m_car.body.shape.half.x + 2.0f;
    std::vector<RouteNode> found;
    auto accept = [&](const Vec3& p, const TrackedCar& by, int onRoad) {
        RouteNode node;
        node.pos = p;
        node.pos.y = from.pos.y;
        const float onCourse = ctx.course->locate(p, ctx.s + from.s + 5.0f, 40.0f);
        node.s = from.s + aheadOf(*ctx.course, ctx.s + from.s, onCourse);
        node.dist = from.dist + xzDist(from.pos, node.pos);
        node.angle = before ? from.angle + vectorAngle(from.pos - before->pos, node.pos - from.pos)
                            : vectorAngle(-m_car.body.ics.matrix.m2, node.pos - from.pos);
        node.obstacle = by.id;
        node.offRoad = onRoad == 2;
        found.push_back(node);
    };
    auto withinRoad = [&](const Vec3& p) {
        const Vec3 rel = p - from.pos;
        const float a =
            std::atan2(rel.x * -roadDir.z + rel.z * roadDir.x, rel.x * roadDir.x + rel.z * roadDir.z);
        return a > -1.57f && a < 1.57f;
    };
    // EnumTargets.
    auto tryPoint = [&](auto&& self, const Vec3& p, const TrackedCar& by, int sideSign, int level) -> void {
        if (++level == kMaxAvoidDepth)
            return;
        const int onRoad = roadState(p, ctx, ctx.s + from.s);
        if (onRoad == 3 || (onRoad == 2 && ctx.semi))
            return;
        const TrackedCar* next = blocking(from.pos, p, cars, ctx, nullptr);
        if (!next || next == &by || next->isOpponent()) {
            accept(p, by, onRoad);
            return;
        }
        Vec3 l, r;
        avoidPoints(*next, from.pos, lookDir, clearance, l, r);
        const Vec3& q = sideSign < 0 ? l : r;
        if (withinRoad(q))
            self(self, q, *next, sideSign, level);
    };
    Vec3 left, right;
    avoidPoints(*obstacle, from.pos, lookDir, clearance, left, right);
    if (withinRoad(left))
        tryPoint(tryPoint, left, *obstacle, -1, 0);
    if (withinRoad(right))
        tryPoint(tryPoint, right, *obstacle, 1, 0);
    if (found.empty()) {
        // No way round: keep the target, marked (state 3).
        RouteNode blockedNode = target;
        blockedNode.obstacle = obstacle->id;
        blockedNode.noWayAround = true;
        continueCheck(blockedNode);
        return;
    }
    for (const RouteNode& node : found)
        continueCheck(node);
}

// --- misc -------------------------------------------------------------------------

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
