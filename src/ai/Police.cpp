// Police. aiPoliceForce is ported from Open1560's aiPoliceForce.cpp; the chase
// rules from aiGoalChase (Open1560 aiGoalChase.cpp and game.asm: Context,
// Fov, Update), GPL-3.0. See Police.h and docs/ai.md for the inferred parts.
#include "ai/Police.h"

#include "phys/World.h"
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

const TrackedCar* findCar(std::span<const TrackedCar> cars, int id) {
    for (const TrackedCar& c : cars)
        if (c.id == id)
            return &c;
    return nullptr;
}

constexpr float kMphPerMs = 2.2369363f;
constexpr float kCloseInDistance = 25.0f; // aiPoliceForce::State
constexpr float kFollowFar = 20.0f;       // aiGoalChase::Follow: + 10 m/s beyond 20 m
constexpr float kPatrolSpeed = 12.0f;     // driving back to the post (inferred)

} // namespace

// --- aiPoliceForce -------------------------------------------------------------

PoliceForce::PoliceForce(int maxCops) : m_maxCops(std::clamp(maxCops, 1, kMaxCops)) {
    reset();
}

void PoliceForce::reset() {
    m_numPerps = 0;
    m_perps.fill(-1);
    m_numCops.fill(0);
    for (auto& row : m_cops)
        row.fill(-1);
}

int PoliceForce::findPerp(int perp) const {
    for (int i = 0; i < m_numPerps; ++i)
        if (m_perps[static_cast<std::size_t>(i)] == perp)
            return i;
    return -1;
}

bool PoliceForce::find(int cop, int perp) const {
    const int i = findPerp(perp);
    if (i < 0)
        return false;
    for (int j = 0; j < m_numCops[static_cast<std::size_t>(i)]; ++j)
        if (m_cops[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] == cop)
            return true;
    return false;
}

int PoliceForce::copsOn(int perp) const {
    const int i = findPerp(perp);
    return i < 0 ? 0 : m_numCops[static_cast<std::size_t>(i)];
}

bool PoliceForce::registerPerp(int cop, int perp) {
    if (const int i = findPerp(perp); i >= 0) {
        const auto ui = static_cast<std::size_t>(i);
        if (find(cop, perp))
            return true;
        if (m_numCops[ui] >= m_maxCops)
            return false;
        m_cops[ui][static_cast<std::size_t>(m_numCops[ui]++)] = cop;
        return true;
    }
    if (m_numPerps >= kMaxPerps)
        return false;
    const auto ui = static_cast<std::size_t>(m_numPerps++);
    m_perps[ui] = perp;
    m_cops[ui][0] = cop;
    m_numCops[ui] = 1;
    return true;
}

bool PoliceForce::unregisterCop(int cop, int perp) {
    const int i = findPerp(perp);
    if (i < 0)
        return false;
    const auto ui = static_cast<std::size_t>(i);
    int j = -1;
    for (int k = 0; k < m_numCops[ui]; ++k)
        if (m_cops[ui][static_cast<std::size_t>(k)] == cop)
            j = k;
    if (j < 0)
        return false;
    m_cops[ui][static_cast<std::size_t>(j)] = m_cops[ui][static_cast<std::size_t>(--m_numCops[ui])];
    if (m_numCops[ui] == 0) {
        const auto last = static_cast<std::size_t>(--m_numPerps);
        m_perps[ui] = m_perps[last];
        m_numCops[ui] = m_numCops[last];
        m_cops[ui] = m_cops[last];
        m_perps[last] = -1;
        m_numCops[last] = 0;
    }
    return true;
}

int PoliceForce::state(int cop, int perp, std::span<const TrackedCar> cars, float copDistance) const {
    const int i = findPerp(perp);
    if (i < 0)
        return 9;
    const auto ui = static_cast<std::size_t>(i);
    const TrackedCar* p = findCar(cars, perp);
    if (!p)
        return 4;
    int best = -1;
    float bestDist = 1e9f;
    for (int j = 0; j < m_numCops[ui]; ++j) {
        const int id = m_cops[ui][static_cast<std::size_t>(j)];
        if (const TrackedCar* c = findCar(cars, id)) {
            const float d = p->position.dist2(c->position);
            if (d < bestDist) {
                bestDist = d;
                best = id;
            }
        }
    }
    return best == cop && copDistance <= kCloseInDistance ? 3 : 4;
}

// --- aiVehiclePolice / aiGoalChase ---------------------------------------------

PoliceCar::PoliceCar(const RoadNetwork& net, phys::CarSim& car, const Mat34& post, int selfId,
                     const PoliceSettings& settings)
    : m_net(net), m_car(car), m_post(post), m_selfId(selfId), m_settings(settings) {
    m_prevCallback = car.onImpactCallback;
    car.onImpactCallback = [this](const phys::Impact& impact) { onImpact(impact); };
    reset();
}

PoliceCar::~PoliceCar() {
    m_car.onImpactCallback = m_prevCallback;
}

void PoliceCar::reset() {
    m_mode = Mode::Parked;
    m_reason = Reason::None;
    m_siren = false;
    m_closingIn = false;
    m_target = -1;
    m_hitBy = nullptr;
    m_route.reset();
    m_lostSight = 0.0f;
    m_slowTime = 0.0f;
    m_stuck.reset();
    m_backup.cancel();
    m_brakeMeter.reset();
    m_aim = m_car.body.ics.matrix.m3;
}

void PoliceCar::onImpact(const phys::Impact& impact) {
    if (m_prevCallback)
        m_prevCallback(impact);
    m_stuck.impact();
    if (impact.other)
        m_hitBy = impact.other;
}

void PoliceCar::park() {
    m_car.setInputs(0.0f, 1.0f, 0.0f, 1.0f);
}

void PoliceCar::escape(PoliceForce& force) {
    // aiVehiclePolice::PerpEscapes: stop the siren, leave the force.
    if (m_target >= 0)
        force.unregisterCop(m_selfId, m_target);
    m_target = -1;
    m_siren = false;
    m_closingIn = false;
    m_route.reset();
    m_mode = Mode::Returning;
}

bool PoliceCar::lineOfSight(const Vec3& a, const Vec3& b, const phys::GroundQuery* los) const {
    if (!los)
        return true;
    const Vec3 up{0.0f, m_settings.losHeight, 0.0f};
    phys::RayHit hit;
    return !los->probe(a + up, b + up, hit);
}

bool PoliceCar::inView(const TrackedCar& c, const phys::GroundQuery* los) const {
    // aiGoalChase::Fov: within +-90 degrees of the heading and in sight.
    const Mat34& m = m_car.body.ics.matrix;
    const Vec3 rel = c.position - m.m3;
    const float angle = std::atan2(rel.dot(m.m0), -rel.dot(m.m2));
    if (angle <= -kHalfPi || angle >= kHalfPi)
        return false;
    return lineOfSight(m.m3, c.position, los);
}

bool PoliceCar::lookForSuspects(std::span<const TrackedCar> cars, PoliceForce& force,
                                const phys::GroundQuery* los) {
    // aiGoalChase::Context: the player first, then the opponents.
    const Vec3 pos = m_car.body.ics.matrix.m3;
    for (int pass = 0; pass < 2; ++pass) {
        for (const TrackedCar& c : cars) {
            if (!c.suspect || c.isPolice || c.id == m_selfId || c.isPlayer != (pass == 0))
                continue;
            if (pos.dist2(c.position) >= sq(m_settings.detectRange))
                continue;
            Reason why = Reason::None;
            if (c.body && c.body == m_hitBy) {
                why = Reason::HitMe;
            } else if (inView(c, los)) {
                // aiGoalChase::Speeding: 70 mph on four-lane roads, else 40.
                const RoadSpot here = locateOnRoads(m_net, pos);
                int lanes = 1;
                if (here.path >= 0 && m_net.source()) {
                    const auto& p = m_net.source()->paths[static_cast<std::size_t>(here.path)];
                    lanes = std::max<int>(p.left.numLanes, p.right.numLanes);
                }
                const float limitMph = lanes >= 4 ? 70.0f : 40.0f;
                const float mph = c.velocity.mag() * kMphPerMs;
                if (mph > limitMph)
                    why = Reason::Speeding;
                else if (c.collided)
                    why = Reason::Collision;
                else if (!locateOnRoads(m_net, c.position).onRoad)
                    why = Reason::OffRoad;
                else if (m_settings.chaseStoppedPlayer && c.isPlayer && c.velocity.mag() < 1.0f)
                    why = Reason::Stopped;
            }
            if (why == Reason::None || !force.registerPerp(m_selfId, c.id))
                continue;
            m_target = c.id;
            m_reason = why;
            m_mode = Mode::Chasing;
            m_siren = true;
            m_lostSight = 0.0f;
            m_route.reset();
            return true;
        }
    }
    return false;
}

void PoliceCar::update(float dt, std::span<const TrackedCar> cars, PoliceForce& force, const phys::GroundQuery* los,
                       bool active) {
    auto& ics = m_car.body.ics;
    // aiGoalChase::Update: a wrecked cop gives up.
    if (m_car.damage.wrecked() || m_mode == Mode::Disabled) {
        if (m_mode == Mode::Chasing)
            escape(force);
        m_mode = Mode::Disabled;
        m_siren = false;
        m_car.setInputs(0.0f, 0.0f, 0.0f, 0.0f);
        ics.linearMomentum = ics.linearMomentum * perFrame(0.95f, dt);
        m_hitBy = nullptr;
        return;
    }
    if (!active) {
        if (m_mode == Mode::Chasing)
            escape(force);
        m_mode = Mode::Parked;
        park();
        m_hitBy = nullptr;
        return;
    }
    m_brakeMeter.update(m_car, dt);
    if (m_mode != Mode::Chasing)
        lookForSuspects(cars, force, los);
    m_hitBy = nullptr;

    switch (m_mode) {
    case Mode::Parked:
        park();
        break;
    case Mode::Returning:
        driveBack(dt, cars);
        break;
    case Mode::Chasing:
        if (const TrackedCar* perp = findCar(cars, m_target))
            chase(dt, *perp, cars, force, los);
        else
            escape(force);
        break;
    case Mode::Disabled:
        break;
    }
}

bool PoliceCar::handleStuck(float dt) {
    m_stuck.update(m_car, dt);
    if (m_backup.active()) {
        if (m_backup.update(m_car, m_aim, dt))
            return true;
    }
    if (m_car.stuck.state == phys::Stuck::Pegged) {
        m_car.body.ics.linearMomentum = {};
        m_car.body.ics.angularMomentum = {};
        m_backup.start(m_car);
        return true;
    }
    if (m_stuck.state() == AiStuck::Stuck) {
        m_car.setInputs(1.0f, 0.0f, 1.0f, 0.0f);
        m_car.stuck.state = phys::Stuck::Idle;
        return true;
    }
    // Pushing against something without moving (inferred).
    if (m_car.engine.throttle > 0.5f && m_car.speed() < 1.0f)
        m_slowTime += dt;
    else
        m_slowTime = 0.0f;
    if (m_slowTime > 3.0f) {
        m_slowTime = 0.0f;
        m_backup.start(m_car);
        return true;
    }
    if (m_car.trans.getCurrentGear() <= 0)
        m_car.trans.setDrive();
    return false;
}

void PoliceCar::planRoute(const Vec3& goal) {
    const Mat34& m = m_car.body.ics.matrix;
    const Vec3 pos = m.m3;
    const Vec3 forward = -m.m2;
    // Start from the end of the current road the car is facing.
    int from = -1;
    const RoadSpot here = locateOnRoads(m_net, pos);
    if (here.intersection >= 0) {
        from = here.intersection;
    } else if (here.path >= 0) {
        const PathInfo& info = m_net.paths()[static_cast<std::size_t>(here.path)];
        float best = -1e9f;
        for (int end : info.intersection) {
            if (end < 0)
                continue;
            const Vec3 d = m_net.intersections()[static_cast<std::size_t>(end)].centre - pos;
            const float score = d.dot(forward) / std::max(d.mag(), 1.0f);
            if (score > best) {
                best = score;
                from = end;
            }
        }
    }
    if (from < 0)
        from = nearestIntersection(m_net, pos);
    const int to = nearestIntersection(m_net, goal);
    const std::vector<int> route = findRoute(m_net, from, to);
    m_route = Course::build(m_net, route, pos, goal, false);
    m_routeGoal = goal;
    m_routeAge = 0.0f;
    if (m_route)
        m_s = m_route->locate(pos, m_route->startDistance(), 40.0f, &m_lateral);
}

// aiGoalChase::CalcSpeed / CopSpeedBoost / CopBrakeBoost / CopSteerBoost1.
void PoliceCar::driveAt(float dt, const Vec3& aim, float targetSpeed, bool ram) {
    auto& ics = m_car.body.ics;
    m_aim = aim;
    const float speed = m_car.speed();
    float throttle = 0.4f, brakes = 0.0f;
    if (ram || speed < targetSpeed - 0.5f) {
        throttle = 1.0f;
    } else if (speed > targetSpeed + 1.5f) {
        throttle = 0.0f;
        brakes = clampf((speed - targetSpeed) / 8.0f, 0.2f, 1.0f);
        ics.linearMomentum = ics.linearMomentum * perFrame(0.95f, dt);
    }
    Vec3 target = aim;
    target.y = ics.matrix.m3.y;
    const float angle = headingError(ics.matrix, target);
    // Far behind the car: turn hard.
    const float steering = clampf(angle, -1.0f, 1.0f);
    if (throttle == 1.0f && speed < 50.0f)
        ics.linearMomentum = ics.linearMomentum * perFrame(1.01f, dt);
    if (angle < 0.05f && angle > -0.05f)
        ics.angularMomentum = ics.angularMomentum * perFrame(0.5f, dt);
    m_car.setInputs(throttle, brakes, steering, 0.0f);
}

void PoliceCar::followRoute(float dt, float targetSpeed, std::span<const TrackedCar> cars) {
    if (!m_route) {
        driveAt(dt, m_routeGoal, targetSpeed, false);
        return;
    }
    const Course& route = *m_route;
    const Vec3 pos = m_car.body.ics.matrix.m3;
    m_s = route.locate(pos, m_s, 30.0f + m_car.speed() * dt * 2.0f, &m_lateral);
    if (route.finishDistance() - m_s < 6.0f) {
        driveAt(dt, m_routeGoal, std::min(targetSpeed, 8.0f), false);
        return;
    }
    const float speed = m_car.speed();
    const float lookahead = clampf(7.0f + speed * speed * 0.488f / m_settings.lateralAccel, 7.0f, 20.0f);
    float left, right;
    route.edgesAhead(m_s, lookahead + speed, left, right);
    const float margin = m_car.body.shape.half.x + 1.5f;
    float minSide = -(left - margin), maxSide = right - margin;
    if (minSide > maxSide)
        minSide = maxSide = 0.5f * (minSide + maxSide);
    float side = clampf(m_lateral, minSide, maxSide);

    Vec3 lineDir;
    route.pointAt(m_s, &lineDir);
    ScanInput in;
    in.position = pos;
    in.lineDir = lineDir;
    in.lateral = m_lateral;
    in.speed = forwardSpeed(m_car);
    in.halfWidth = m_car.body.shape.half.x;
    in.selfId = m_selfId;
    in.range = clampf(speed * 2.5f + 10.0f, 15.0f, 50.0f);
    const ObstacleScan scan = scanObstacles(in, cars);
    float freeSide = side;
    float limit = targetSpeed;
    if (scan.freeSide(side, minSide, maxSide, freeSide))
        side = freeSide;
    else if (const auto* r = scan.blocking(side); r && r->along < 15.0f)
        limit = std::min(limit, std::max(r->speed, 0.0f));

    float vmax = 1e9f;
    const float brake = turnBrake(route, m_s, side, speed, m_settings.lateralAccel, m_brakeMeter.decel(), &vmax);
    if (brake > 0.7f)
        limit = std::min(limit, vmax);
    Vec3 dir;
    const Vec3 p = route.pointAt(m_s + lookahead, &dir);
    driveAt(dt, p + rightOf(dir) * side, limit, false);
}

void PoliceCar::chase(float dt, const TrackedCar& perp, std::span<const TrackedCar> cars, PoliceForce& force,
                      const phys::GroundQuery* los) {
    const Vec3 pos = m_car.body.ics.matrix.m3;
    const float dist = pos.dist(perp.position);
    const bool visible = lineOfSight(pos, perp.position, los);
    m_lostSight = visible ? 0.0f : m_lostSight + dt;
    if (dist > m_settings.escapeDistance || m_lostSight > m_settings.lostSightSeconds) {
        escape(force);
        return;
    }
    m_siren = true;
    m_routeAge += dt;
    if (handleStuck(dt))
        return;
    m_closingIn = force.state(m_selfId, perp.id, cars, dist) == 3;
    const float perpSpeed = perp.velocity.mag();
    // aiGoalChase::Follow: the suspect's speed, + 10 m/s beyond 20 m.
    const float targetSpeed = perpSpeed + (dist > kFollowFar ? 10.0f : 0.0f);
    if (m_closingIn || (visible && dist < 40.0f)) {
        // Close in on the suspect: aim where it will be (inferred).
        const float carSpeed = std::max(m_car.speed(), 5.0f);
        const Vec3 lead = perp.position + perp.velocity * clampf(dist / carSpeed, 0.0f, 1.0f);
        driveAt(dt, lead, m_closingIn ? perpSpeed + 5.0f : targetSpeed, m_closingIn);
        m_route.reset();
        return;
    }
    if (!m_route || m_routeAge > 1.5f || perp.position.dist(m_routeGoal) > 25.0f)
        planRoute(perp.position);
    followRoute(dt, targetSpeed, cars);
}

void PoliceCar::driveBack(float dt, std::span<const TrackedCar> cars) {
    const Vec3 pos = m_car.body.ics.matrix.m3;
    if (pos.dist(m_post.m3) < 8.0f) {
        m_mode = Mode::Parked;
        park();
        return;
    }
    m_routeAge += dt;
    if (handleStuck(dt))
        return;
    if (!m_route || m_routeAge > 10.0f)
        planRoute(m_post.m3);
    followRoute(dt, kPatrolSpeed, cars);
}

// --- squad ---------------------------------------------------------------------

PoliceSquad::PoliceSquad(const RoadNetwork& net, int maxCopsPerSuspect) : m_net(net), m_force(maxCopsPerSuspect) {}

PoliceCar& PoliceSquad::add(phys::CarSim& car, const Mat34& post, int selfId, const PoliceSettings& settings) {
    m_cars.push_back(std::make_unique<PoliceCar>(m_net, car, post, selfId, settings));
    return *m_cars.back();
}

void PoliceSquad::update(float dt, std::span<const TrackedCar> cars, const phys::GroundQuery* los, bool active) {
    for (auto& c : m_cars)
        c->update(dt, cars, m_force, los, active);
}

void PoliceSquad::reset() {
    m_force.reset();
    for (auto& c : m_cars)
        c->reset();
}

bool PoliceSquad::anySiren() const {
    return std::any_of(m_cars.begin(), m_cars.end(), [](const auto& c) { return c->siren(); });
}

std::vector<std::size_t> PoliceSquad::pickByDensity(std::size_t count, float density) {
    const auto n = static_cast<std::size_t>(std::lround(clampf(density, 0.0f, 1.0f) * static_cast<float>(count)));
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < n; ++i)
        out.push_back(i * count / n);
    return out;
}

} // namespace mm2::ai
