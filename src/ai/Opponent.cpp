// Race opponents: MM2's aiRouteRacer (build 3393) driving an ai::PhysicsDriver
// (aiVehiclePhysics). See Opponent.h and docs/ai.md for what is ported and
// what is inferred.
#include "ai/Opponent.h"

#include "phys/vehicle/CarSim.h"

#include <algorithm>
#include <cmath>

namespace mm2::ai {
namespace {

// OpenMM2: the race is over for a car that comes within 10 m of its
// destination on its final approach, when the game has not said so first
// (MM2 leaves finishing to the game's finish line).
constexpr float kFinishRadius = 10.0f;
// aiRouteRacer::DriveRoute: "Opponent %d has fallen through the world".
constexpr float kFallenThroughWorld = -200.0f;

float wrapAhead(const Course& c, float from, float to) {
    float d = to - from;
    if (c.loop() && c.length() > 0.0f) {
        d = std::fmod(d, c.length());
        if (d < 0.0f)
            d += c.length();
    }
    return d;
}

} // namespace

OpponentSettings OpponentSettings::fromData(std::span<const float> params, int laps) {
    // aiRaceData::aiRaceData's [Opponent] record and its defaults, passed to
    // RegisterRoute by aiRouteRacer::DriveRoute.
    auto at = [&](std::size_t i, float def) { return i < params.size() ? params[i] : def; };
    OpponentSettings s;
    s.route.maxThrottle = clampf(at(0, 1.0f), 0.0f, 1.0f);
    // params[1]: RegisterRoute's first flag; stored, never read.
    s.route.lookAhead = std::max(at(2, 50.0f), 1.0f);
    s.route.brakeThreshold = at(3, 0.7f);
    s.route.avoidTraffic = at(4, 1.0f) != 0.0f;
    s.route.avoidProps = at(5, 1.0f) != 0.0f;
    s.route.avoidPlayers = at(6, 1.0f) != 0.0f;
    s.route.avoidOpponents = at(7, 1.0f) != 0.0f;
    s.route.preferSidewalk = at(8, 0.0f) != 0.0f;
    s.route.cornerSpeedFactor = at(9, 1.0f);
    s.route.destinationSpeed = 0.0f;
    s.route.stopShort = 0.0f;
    s.laps = std::max(laps, 0);
    s.repairWhenWrecked = laps > 0;
    return s;
}

Opponent::Opponent(phys::CarSim& car, Course course, const OpponentSettings& settings, int selfId)
    : m_car(car), m_course(std::move(course)), m_settings(settings), m_selfId(selfId), m_driver(car, selfId) {
    m_prevCallback = car.onImpactCallback;
    car.onImpactCallback = [this](const phys::Impact& impact) { onImpact(impact); };
    configureAiVehStuck(car, 0.5f);
    m_driver.params = m_settings.route;

    // The waypoints: where each leg of the course ends, as progress from the
    // start, and the way from the last waypoint to the destination.
    const float start = m_course.startDistance();
    const float finish = m_course.finishDistance();
    m_lastLeg = m_course.loop() ? m_course.length() : std::max(0.0f, finish - start);
    for (const CourseLeg& leg : m_course.legs()) {
        const float wp = m_course.loop() ? wrapAhead(m_course, start, leg.end) : leg.end - start;
        m_waypointProgress.push_back(wp);
        const float toFinish = m_course.loop() ? wrapAhead(m_course, leg.end, finish) : finish - leg.end;
        if (toFinish >= 0.0f)
            m_lastLeg = std::min(m_lastLeg, toFinish);
    }
    std::sort(m_waypointProgress.begin(), m_waypointProgress.end());
    reset();
}

Opponent::~Opponent() {
    m_car.onImpactCallback = m_prevCallback;
    m_car.params.carFrictionHandling = 1.0f;
}

std::unique_ptr<Opponent> Opponent::create(const RoadNetwork& net, phys::CarSim& car,
                                           std::span<const city::OpponentPoint> path, std::span<const float> params,
                                           int laps, int selfId, std::string* error,
                                           const phys::GroundQuery* world) {
    auto course = Course::fromOpponentPath(net, path, laps > 0, error);
    if (!course)
        return nullptr;
    OpponentSettings s = OpponentSettings::fromData(params, laps);
    s.world = world;
    return std::make_unique<Opponent>(car, std::move(*course), s, selfId);
}

void Opponent::reset() {
    // aiRouteRacer::Reset / aiVehiclePhysics::Reset.
    m_mode = m_held ? Mode::Held : Mode::Racing;
    m_finished = false;
    m_disabled = false;
    m_touchingPlayer = false;
    m_driver.reset();
    m_driver.params = m_settings.route;
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
    m_bestProgress = m_progress;
    m_noProgressTime = 0.0f;
}

void Opponent::finish() {
    m_finished = true;
}

void Opponent::onImpact(const phys::Impact& impact) {
    if (m_prevCallback)
        m_prevCallback(impact);
    // dgPhysManager::CollideInstances flags a car touching the player.
    if (impact.other && impact.other == m_playerBody)
        m_touchingPlayer = true;
}

int Opponent::lapsDone() const {
    if (!m_course.loop() || m_course.length() <= 0.0f)
        return 0;
    // Reaching the finish (within kFinishRadius) completes the last lap.
    if (m_settings.laps > 0 && m_course.raceDistance(m_settings.laps) - m_progress <= kFinishRadius)
        return m_settings.laps;
    // Laps end at the finish line, which can lie a few metres behind or
    // ahead of the car's grid place.
    const float lineOffset = m_course.raceDistance(1) - m_course.length();
    return std::max(0, static_cast<int>(std::floor((m_progress - lineOffset) / m_course.length())));
}

float Opponent::remainingDistance() const {
    if (m_course.loop() && m_settings.laps <= 0)
        return 1e9f;
    return m_course.raceDistance(m_settings.laps) - m_progress;
}

int Opponent::waypointsPassed() const {
    // aiVehiclePhysics 0x967a: the waypoint the car is heading for, from 1
    // on its start road; PlanRoute sets it back to 1 on every new lap.
    float lapProgress = m_progress;
    if (m_course.loop() && m_course.length() > 0.0f)
        lapProgress -= m_course.length() * std::floor(m_progress / m_course.length());
    int passed = 1;
    for (float wp : m_waypointProgress)
        if (wp <= lapProgress)
            ++passed;
    return passed;
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

void Opponent::update(float dt, std::span<const TrackedCar> cars) {
    for (const TrackedCar& c : cars)
        if (c.isPlayer)
            m_playerBody = c.body;
    trackProgress(dt);
    const bool touching = m_touchingPlayer;
    m_touchingPlayer = false;

    if (m_held && !m_finished) {
        m_mode = Mode::Held;
        m_car.setInputs(0.0f, 1.0f, 0.0f, 1.0f);
        m_noProgressTime = 0.0f;
        return;
    }

    const Vec3 pos = m_car.body.ics.matrix.m3;
    if (pos.y < kFallenThroughWorld)
        m_disabled = true;

    // OpenMM2 recovery (not in MM2): no progress for a long time, or fallen
    // out of the city, puts the car back on its line.
    if (!m_finished && !m_disabled && !m_driver.wrecked()) {
        if (m_progress > m_bestProgress + 5.0f) {
            m_bestProgress = m_progress;
            m_noProgressTime = 0.0f;
        } else {
            m_noProgressTime += dt;
        }
        const float lineY = m_course.pointAt(m_s).y;
        const bool fell = pos.y < lineY - 15.0f;
        if (fell || (m_settings.resetAfterSeconds > 0.0f && m_noProgressTime > m_settings.resetAfterSeconds)) {
            // Stuck (or fallen) at the same place again: put it further along
            // each time.
            m_resetStreak = std::abs(m_progress - m_lastResetProgress) < 15.0f ? m_resetStreak + 1 : 0;
            m_lastResetProgress = m_progress;
            float ahead = std::min(15.0f * static_cast<float>(m_resetStreak), 60.0f);
            if (!m_course.loop())
                ahead = std::max(0.0f, std::min(ahead, m_course.finishDistance() - m_s - 5.0f));
            placeOnCourse(m_car, m_course, m_s + ahead, m_lateral, cars, m_selfId, m_settings.resetCar,
                          m_settings.world);
            m_s = m_course.wrap(m_s + ahead);
            m_progress += ahead;
            m_driver.reset();
            m_noProgressTime = 0.0f;
            m_bestProgress = m_progress;
            ++m_resets;
            return;
        }
    }

    const float remaining = remainingDistance();
    DriveContext ctx;
    ctx.course = &m_course;
    ctx.s = m_s;
    ctx.lateral = m_lateral;
    ctx.remaining = remaining;
    ctx.destination = m_course.finishPoint();
    // Past the last waypoint of the last lap (or told the race is over):
    // plan to the destination.
    ctx.finalApproach = m_finished || remaining <= m_lastLeg + 0.5f;
    ctx.repairWhenWrecked = m_settings.repairWhenWrecked;
    ctx.touchingPlayer = touching;
    ctx.waypointsPassed = waypointsPassed();
    ctx.semi = m_settings.semi;

    if (!m_finished && ctx.finalApproach && remaining <= kFinishRadius)
        finish();
    // Disabled (aiRouteRacer::Disabled), or at the destination after the
    // finish: aiVehiclePhysics::Stop.
    if (m_disabled || (m_finished && (remaining < 2.5f || m_car.speed() < 1.0f)))
        m_driver.setState(PhysicsDriver::State::Stop);
    m_driver.driveRoute(dt, cars, ctx);

    if (m_settings.speedLimit > 0.0f && m_driver.state() == PhysicsDriver::State::Forward) {
        // OpenMM2 hook for scripted cars (tests): hold a top speed.
        const float speed = m_car.speed();
        if (speed > m_settings.speedLimit - 1.0f) {
            const float throttle =
                std::min(m_car.engine.throttle, speed > m_settings.speedLimit ? 0.0f : 0.3f);
            const float brakes =
                speed > m_settings.speedLimit + 2.0f ? std::max(m_car.brakes, 0.3f) : m_car.brakes;
            m_car.setInputs(throttle, brakes, m_car.steering, m_car.handBrake);
        }
    }

    if (m_driver.wrecked())
        m_mode = Mode::Wrecked;
    else if (m_driver.state() == PhysicsDriver::State::Backup)
        m_mode = Mode::BackingUp;
    else if (m_driver.state() == PhysicsDriver::State::Stop)
        m_mode = Mode::Stopped;
    else
        m_mode = Mode::Racing;
}

} // namespace mm2::ai
