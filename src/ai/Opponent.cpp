// Race opponents. Ported from MM1's aiVehicleOpponent, aiGoalFollowWayPts,
// aiGoalBackup and aiGoalStop (Open1560, GPL-3.0); see Opponent.h and
// docs/ai.md for what is ported and what is inferred.
#include "ai/Opponent.h"

#include "phys/vehicle/CarSim.h"

#include <algorithm>
#include <cmath>

namespace mm2::ai {
namespace {

Vec3 rightOf(const Vec3& dir) {
    const Vec2 f{dir.x, dir.z};
    const float m = f.mag();
    return m > 1e-6f ? Vec3{-f.y / m, 0.0f, f.x / m} : Vec3{1, 0, 0};
}

// Seconds the car drives with full realism after touching the player (MM1
// tests INST_FLAG_COLLIDED_PLAYER; when the flag clears is not known).
constexpr float kPlayerContactSeconds = 2.0f;
// aiGoalFollowWayPts::PlanRoute: the race is over within 10 m of the last
// point (flt_61BC94).
constexpr float kFinishRadius = 10.0f;

} // namespace

Opponent::Opponent(phys::CarSim& car, Course course, const OpponentSettings& settings, int selfId)
    : m_car(car), m_course(std::move(course)), m_settings(settings), m_selfId(selfId) {
    m_prevCallback = car.onImpactCallback;
    car.onImpactCallback = [this](const phys::Impact& impact) { onImpact(impact); };
    reset();
}

Opponent::~Opponent() {
    m_car.onImpactCallback = m_prevCallback;
}

std::unique_ptr<Opponent> Opponent::create(const RoadNetwork& net, phys::CarSim& car,
                                           std::span<const city::OpponentPoint> path, std::span<const float> params,
                                           int laps, int selfId, std::string* error,
                                           const phys::GroundQuery* world) {
    auto course = Course::fromOpponentPath(net, path, laps > 0, error);
    if (!course)
        return nullptr;
    OpponentSettings s;
    if (!params.empty() && params[0] > 0.0f)
        s.maxThrottle = clampf(params[0], 0.1f, 1.0f);
    s.laps = std::max(laps, 0);
    s.repairWhenWrecked = laps > 0;
    s.world = world;
    return std::make_unique<Opponent>(car, std::move(*course), s, selfId);
}

void Opponent::reset() {
    // aiVehicleOpponent::Reset / aiGoalFollowWayPts::Reset.
    m_mode = m_held ? Mode::Held : Mode::Racing;
    m_finished = false;
    m_wrecked = false;
    m_wreckTime = 0.0f;
    m_collidedWithPlayer = false;
    m_contactTime = 0.0f;
    m_stuck.reset();
    m_backup.cancel();
    m_brakeMeter.reset();
    if (m_car.trans.getCurrentGear() == -1)
        m_car.trans.setDrive();
    m_car.setInputs(0.0f, 0.0f, 0.0f, 0.0f);
    const Vec3 pos = m_car.body.ics.matrix.m3;
    float dist = 0.0f;
    m_s = m_course.locate(pos, m_course.startDistance(), 40.0f, &m_lateral, &dist);
    if (dist > 20.0f) // not near the start: anywhere along the line
        m_s = m_course.locate(pos, &m_lateral);
    float d = m_s - m_course.startDistance();
    if (m_course.loop()) {
        const float len = m_course.length();
        if (d > 0.5f * len)
            d -= len;
        else if (d < -0.5f * len)
            d += len;
    }
    m_progress = d;
    m_side = m_lateral;
    m_target = pos;
    m_bestProgress = m_progress;
    m_noProgressTime = 0.0f;
}

void Opponent::finish() {
    m_finished = true;
    m_backup.cancel();
}

void Opponent::onImpact(const phys::Impact& impact) {
    if (m_prevCallback)
        m_prevCallback(impact);
    m_stuck.impact();
    if (impact.other && impact.other == m_playerBody)
        m_collidedWithPlayer = true;
}

int Opponent::lapsDone() const {
    if (!m_course.loop() || m_course.length() <= 0.0f)
        return 0;
    // Reaching the finish (within kFinishRadius, as PlanRoute decides)
    // completes the last lap.
    if (m_settings.laps > 0 && m_course.raceDistance(m_settings.laps) - m_progress <= kFinishRadius)
        return m_settings.laps;
    // Laps end at the finish line, which can lie a few metres behind or
    // ahead of the car's grid place.
    const float lineOffset = m_course.raceDistance(1) - m_course.length();
    return std::max(0, static_cast<int>(std::floor((m_progress - lineOffset) / m_course.length())));
}

void Opponent::trackProgress(float dt) {
    const Vec3 pos = m_car.body.ics.matrix.m3;
    const float window = 30.0f + m_car.speed() * dt * 2.0f;
    const float s = m_course.locate(pos, m_s, window, &m_lateral);
    float delta = s - m_s;
    if (m_course.loop()) {
        const float len = m_course.length();
        if (delta > 0.5f * len)
            delta -= len;
        else if (delta < -0.5f * len)
            delta += len;
    }
    m_progress += delta;
    m_s = s;
}

void Opponent::stopGoal(float dt) {
    // aiGoalStop::Update.
    m_mode = Mode::Stopped;
    m_car.setInputs(0.0f, 1.0f, 0.0f, 0.0f);
    m_car.body.ics.linearMomentum = m_car.body.ics.linearMomentum * perFrame(0.95f, dt);
}

void Opponent::update(float dt, std::span<const TrackedCar> cars) {
    for (const TrackedCar& c : cars)
        if (c.isPlayer)
            m_playerBody = c.body;
    trackProgress(dt);
    m_brakeMeter.update(m_car, dt);

    if (m_held && !m_finished) {
        m_mode = Mode::Held;
        m_car.setInputs(0.0f, 1.0f, 0.0f, 1.0f);
        m_noProgressTime = 0.0f;
        return;
    }

    // aiGoalFollowWayPts::PlanRoute: past the last lap's final waypoint and
    // within 10 m of the last point, the car is finished and stops.
    if (!m_finished && (!m_course.loop() || m_settings.laps > 0) &&
        m_course.raceDistance(m_settings.laps) - m_progress <= kFinishRadius)
        finish();
    if (m_finished) {
        stopGoal(dt);
        return;
    }

    // aiGoalFollowWayPts::Context: damage.
    if (m_car.damage.wrecked()) {
        if (!m_wrecked) {
            m_wrecked = true;
            m_wreckTime = 0.0f;
        }
        m_wreckTime += dt;
        if (m_settings.repairWhenWrecked && m_wreckTime > 5.0f) {
            m_car.damage.reset();
            m_wrecked = false;
        } else {
            m_mode = Mode::Wrecked;
            m_car.setInputs(0.0f, 0.0f, 0.0f, 0.0f);
            m_car.body.ics.linearMomentum = m_car.body.ics.linearMomentum * perFrame(0.95f, dt);
            return;
        }
    }

    // OpenMM2 recovery (inferred): no progress for a long time, or fallen
    // out of the city, puts the car back on its line.
    if (m_progress > m_bestProgress + 5.0f) {
        m_bestProgress = m_progress;
        m_noProgressTime = 0.0f;
    } else {
        m_noProgressTime += dt;
    }
    const float lineY = m_course.pointAt(m_s).y;
    const bool fell = m_car.body.ics.matrix.m3.y < lineY - 15.0f;
    if (fell || (m_settings.resetAfterSeconds > 0.0f && m_noProgressTime > m_settings.resetAfterSeconds)) {
        // Stuck at the same place again: put it further along each time.
        m_resetStreak = !fell && std::abs(m_progress - m_lastResetProgress) < 15.0f ? m_resetStreak + 1 : 0;
        m_lastResetProgress = m_progress;
        float ahead = std::min(15.0f * static_cast<float>(m_resetStreak), 60.0f);
        if (!m_course.loop())
            ahead = std::max(0.0f, std::min(ahead, m_course.finishDistance() - m_s - 5.0f));
        placeOnCourse(m_car, m_course, m_s + ahead, m_side, cars, m_selfId, m_settings.resetCar, m_settings.world);
        m_s = m_course.wrap(m_s + ahead);
        m_progress += ahead;
        m_stuck.reset();
        m_backup.cancel();
        m_noProgressTime = 0.0f;
        m_bestProgress = m_progress;
        ++m_resets;
        return;
    }

    if (m_backup.active()) {
        m_mode = Mode::BackingUp;
        // Aim along the line 8 m ahead, at the car's place across the road.
        Vec3 dir;
        const Vec3 p = m_course.pointAt(m_s + 8.0f, &dir);
        float left, right;
        m_course.edges(m_s + 8.0f, left, right);
        const Vec3 aim = p + rightOf(dir) * clampf(m_lateral, -left + 1.0f, right - 1.0f);
        if (m_backup.update(m_car, aim, dt))
            return;
    }
    followWayPoints(dt, cars);
}

void Opponent::followWayPoints(float dt, std::span<const TrackedCar> cars) {
    // aiGoalFollowWayPts::Update.
    m_mode = Mode::Racing;
    phys::CarSim& car = m_car;
    auto& ics = car.body.ics;

    m_stuck.update(car, dt);
    if (car.stuck.state == phys::Stuck::Pegged) {
        // The car's own vehStuck: back up (BackingUp = true, momenta zeroed).
        ics.linearMomentum = {};
        ics.angularMomentum = {};
        car.setInputs(0.0f, 0.0f, 0.0f, 0.0f);
        m_backup.start(car);
        ++m_backups;
        m_mode = Mode::BackingUp;
        return;
    }
    if (m_stuck.state() == AiStuck::Stuck) {
        car.setInputs(1.0f, 0.0f, 1.0f, 0.0f);
        car.stuck.state = phys::Stuck::Idle;
        return;
    }
    if (car.trans.getCurrentGear() <= 0)
        car.trans.setDrive();

    // DistToSide: the car keeps its place across the road, inside the curbs
    // less its half width and 1.5 m (as aiGoalChase::Follow limits it).
    const float speed = car.speed();
    // TargetPtOffset: DetermineOppMapComponent keeps it within 7..20 m
    // (flt_61B26C, flt_61B270); the speed term is inferred from the
    // 0.488 / 23.76 factors it uses.
    const float lookahead = clampf(7.0f + speed * speed * 0.488f / m_settings.lateralAccel, 7.0f, 20.0f);
    // The curbs limit it over the stretch ahead too, so that the car moves
    // over before a road narrows (inferred).
    float left, right;
    m_course.edgesAhead(m_s, lookahead + speed, left, right);
    const float margin = car.body.shape.half.x + 1.5f;
    float minSide = -(left - margin), maxSide = right - margin;
    if (minSide > maxSide)
        minSide = maxSide = 0.5f * (minSide + maxSide);
    m_side = clampf(m_lateral, minSide, maxSide);

    float throttle = m_settings.maxThrottle;
    float brakes = 0.0f;

    // DetectCollision / AvoidCollision (not for semis): steer for the
    // nearest free gap, else slow to the speed of the car ahead.
    Vec3 lineDir;
    m_course.pointAt(m_s, &lineDir);
    bool following = false;
    float followSpeed = 0.0f, followGap = 0.0f;
    if (!m_settings.semi) {
        ScanInput in;
        in.position = ics.matrix.m3;
        in.lineDir = lineDir;
        in.lateral = m_lateral;
        in.speed = forwardSpeed(car);
        in.halfWidth = car.body.shape.half.x;
        in.selfId = m_selfId;
        in.range = clampf(speed * 2.5f + 10.0f, 15.0f, 50.0f);
        const ObstacleScan scan = scanObstacles(in, cars);
        float freeSide = m_side;
        if (scan.freeSide(m_side, minSide, maxSide, freeSide)) {
            m_side = freeSide;
        } else if (const auto* r = scan.blocking(m_side)) {
            following = true;
            followSpeed = r->speed;
            followGap = r->along;
        }
    }

    // CalcSpeed.
    const float brake = turnBrake(m_course, m_s, m_side, speed, m_settings.lateralAccel, m_brakeMeter.decel());
    if (brake > 0.7f) {
        throttle = 0.0f;
        brakes = clampf(brake, 0.0f, 1.0f);
        ics.angularMomentum = ics.angularMomentum * perFrame(0.85f, dt);
    }
    if (m_settings.speedLimit > 0.0f && speed > m_settings.speedLimit - 1.0f) {
        throttle = std::min(throttle, speed > m_settings.speedLimit ? 0.0f : 0.3f);
        if (speed > m_settings.speedLimit + 2.0f)
            brakes = std::max(brakes, 0.3f);
    }
    if (following) {
        // Close up to the car ahead at its speed; a stopped car is crept up
        // to and nudged rather than waited behind forever (inferred).
        const float want = std::max(followSpeed, 3.0f);
        if (speed > want) {
            const float t = std::max(followGap - 2.0f, 0.1f) / std::max(speed, 0.1f);
            const float need = (speed - want) / (m_brakeMeter.decel() * t);
            if (need > 0.7f || followGap < 3.0f) {
                throttle = 0.0f;
                brakes = std::max(brakes, clampf(need, 0.3f, 1.0f));
            } else if (followGap < 10.0f) {
                throttle = std::min(throttle, 0.3f);
            }
        } else if (followGap < 6.0f) {
            throttle = std::min(throttle, 0.5f);
        }
    }

    // SolveTargetPoint: the course point TargetPtOffset ahead, DistToSide
    // across (aiRailSet::CalcCopRailPosition).
    // A target hidden behind a wall (a median, a corner) is pulled in.
    Vec3 dir;
    for (float reach : {lookahead, 0.6f * lookahead, 5.0f}) {
        const Vec3 p = m_course.pointAt(m_s + reach, &dir);
        m_target = p + rightOf(dir) * m_side;
        if (!blocked(m_settings.world, ics.matrix.m3, m_target))
            break;
    }
    m_target.y = ics.matrix.m3.y;
    const float angle = headingError(ics.matrix, m_target);
    const float steering = clampf(angle, -1.0f, 1.0f);

    // Yaw damping near the line, unless the player was hit. (MM1's Realism 0
    // for AI cars has no counterpart in MM2's vehWheel.)
    if (m_collidedWithPlayer) {
        m_collidedWithPlayer = false;
        m_contactTime = kPlayerContactSeconds;
    }
    if (m_contactTime > 0.0f) {
        m_contactTime -= dt;
    } else {
        if (angle < 0.1f && angle > -0.1f)
            ics.angularMomentum = ics.angularMomentum * perFrame(0.1f, dt);
    }
    car.setInputs(throttle, brakes, steering, 0.0f);
}

} // namespace mm2::ai
