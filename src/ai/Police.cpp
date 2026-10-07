// Police: MM2's aiPoliceOfficer and aiPoliceForce (build 3393). See Police.h
// and docs/ai.md for what is ported and what is inferred.
#include "ai/Police.h"

#include "phys/World.h"
#include "phys/vehicle/CarSim.h"

#include <algorithm>
#include <cmath>

namespace mm2::ai {
namespace {

const TrackedCar* findCar(std::span<const TrackedCar> cars, int id) {
    for (const TrackedCar& c : cars)
        if (c.id == id)
            return &c;
    return nullptr;
}

const TrackedCar* firstPlayer(std::span<const TrackedCar> cars) {
    for (const TrackedCar& c : cars)
        if (c.isPlayer)
            return &c;
    return nullptr;
}

Vec3 flatUnit(const Vec3& v) {
    const Vec2 f{v.x, v.z};
    const float m = f.mag();
    return m > 1e-6f ? Vec3{f.x / m, 0.0f, f.y / m} : Vec3{0, 0, -1};
}

float xzDist(const Vec3& a, const Vec3& b) {
    return Vec2{a.x - b.x, a.z - b.z}.mag();
}

constexpr float kApprehendRange = 25.0f; // aiPoliceForce::State (dat_5d848c)

} // namespace

// --- aiPoliceForce -------------------------------------------------------------

PoliceForce::PoliceForce() {
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
    // aiPoliceForce::RegisterPerp: a fourth cop on a suspect, or a fourth
    // suspect, is turned down.
    if (const int i = findPerp(perp); i >= 0) {
        const auto ui = static_cast<std::size_t>(i);
        if (find(cop, perp))
            return true;
        if (m_numCops[ui] >= kMaxCops)
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
    // aiPoliceForce::UnRegisterCop: the later pursuers move up; the suspect
    // goes when its last pursuer does.
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
    for (int k = j; k + 1 < m_numCops[ui]; ++k)
        m_cops[ui][static_cast<std::size_t>(k)] = m_cops[ui][static_cast<std::size_t>(k + 1)];
    m_cops[ui][static_cast<std::size_t>(--m_numCops[ui])] = -1;
    if (m_numCops[ui] == 0) {
        const auto last = static_cast<std::size_t>(--m_numPerps);
        m_perps[ui] = m_perps[last];
        m_numCops[ui] = m_numCops[last];
        m_cops[ui] = m_cops[last];
        m_perps[last] = -1;
        m_numCops[last] = 0;
        m_cops[last].fill(-1);
    }
    return true;
}

int PoliceForce::state(int cop, int perp, std::span<const TrackedCar> cars, float copDistance) const {
    const int i = findPerp(perp);
    if (i < 0)
        return kNotPursued;
    const auto ui = static_cast<std::size_t>(i);
    const TrackedCar* p = findCar(cars, perp);
    if (!p)
        return kFollow;
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
    return best == cop && copDistance <= kApprehendRange ? kApprehend : kFollow;
}

// --- settings ------------------------------------------------------------------

PoliceSettings PoliceSettings::fromData(std::span<const float> params, std::optional<float> chaseDistance) {
    PoliceSettings s;
    // params[0]: an int aiPoliceOfficer never reads.
    if (params.size() > 1)
        s.behaviours = static_cast<unsigned>(std::max(params[1], 0.0f)) & 0xF;
    if (params.size() > 2)
        s.opponentChance = params[2];
    if (params.size() > 3)
        s.opponentRange = params[3];
    if (chaseDistance && *chaseDistance > 0.0f)
        s.chaseDistance = *chaseDistance;
    return s;
}

// --- aiPoliceOfficer -------------------------------------------------------------

PoliceCar::PoliceCar(const RoadNetwork& net, phys::CarSim& car, const Mat34& post, int selfId,
                     const PoliceSettings& settings)
    : m_net(net), m_car(car), m_post(post), m_selfId(selfId), m_settings(settings), m_driver(car, selfId),
      m_random(settings.seed + static_cast<std::uint64_t>(selfId) * 7919u) {
    m_prevCallback = car.onImpactCallback;
    car.onImpactCallback = [this](const phys::Impact& impact) { onImpact(impact); };
    // aiPoliceOfficer::Init: vehStuck's TimeThresh 0.75 s.
    configureAiVehStuck(car, 0.75f);
    m_destination = post.m3;
    m_pursuit = 0;
    m_lastPursuit = -1;
    m_apprehend = 3;
    m_driver.setState(PhysicsDriver::State::Stop);
}

PoliceCar::~PoliceCar() {
    m_car.onImpactCallback = m_prevCallback;
}

void PoliceCar::reset() {
    // aiPoliceOfficer::Reset: at the post, braked, watching.
    m_car.reset(m_post);
    m_driver.reset();
    m_ignored.clear();
    m_target = -1;
    m_pursuit = 0;
    m_lastPursuit = -1;
    m_apprehend = 3;
    m_siren = false;
    m_mode = Mode::Parked;
    m_reason = Reason::None;
    m_route.reset();
    m_destination = m_post.m3;
    m_destinationHeading = {};
    m_driver.setState(PhysicsDriver::State::Stop);
}

void PoliceCar::onImpact(const phys::Impact& impact) {
    if (m_prevCallback)
        m_prevCallback(impact);
    if (impact.other && impact.other == m_playerBody)
        m_touchingPlayer = true;
}

bool PoliceCar::inView(const TrackedCar& c) const {
    // aiPoliceOfficer::Fov: within 1.57 rad either side of the heading.
    const Mat34& m = m_car.body.ics.matrix;
    const Vec3 rel = c.position - m.m3;
    const float angle = std::atan2(rel.dot(m.m0), -rel.dot(m.m2));
    return angle > -m_settings.fov && angle < m_settings.fov;
}

void PoliceCar::detect(std::span<const TrackedCar> cars, PoliceForce& force) {
    // aiPoliceOfficer::DetectPerpetrator: the players first, then the
    // opponents, each within 75 m and in front.
    const Vec3 pos = m_car.body.ics.matrix.m3;
    const float range2 = m_settings.detectRange * m_settings.detectRange;
    for (const TrackedCar& c : cars) {
        if (!c.isPlayer || c.id == m_selfId)
            continue;
        if (pos.dist2(c.position) < range2 && inView(c) && force.registerPerp(m_selfId, c.id)) {
            acquire(c, Reason::PlayerInView, cars);
            return;
        }
    }
    const TrackedCar* player = firstPlayer(cars);
    for (const TrackedCar& c : cars) {
        if (!c.isOpponent() || c.id == m_selfId)
            continue;
        if (std::find(m_ignored.begin(), m_ignored.end(), c.id) != m_ignored.end())
            continue;
        if (!(pos.dist2(c.position) < range2) || !inView(c))
            continue;
        // Only while the player is near enough to see it, and not always.
        if (!player || !(xzDist(pos, player->position) < m_settings.opponentRange))
            continue;
        if (m_random.frand() <= m_settings.opponentChance) {
            if (force.registerPerp(m_selfId, c.id)) {
                acquire(c, Reason::OpponentInView, cars);
                return;
            }
        } else {
            m_ignored.push_back(c.id);
        }
    }
}

void PoliceCar::acquire(const TrackedCar& c, Reason why, std::span<const TrackedCar> cars) {
    m_target = c.id;
    m_reason = why;
    m_driver.setState(PhysicsDriver::State::Forward);
    m_pursuit = PoliceForce::kFollow;
    m_route.reset();
    (void)cars;
    follow(c, xzDist(m_car.body.ics.matrix.m3, c.position));
}

void PoliceCar::escape(PoliceForce& force) {
    // aiPoliceOfficer::PerpEscapes: siren off, out of the force, stop.
    if (m_target >= 0)
        force.unregisterCop(m_selfId, m_target);
    m_target = -1;
    m_siren = false;
    m_pursuit = 0;
    m_driver.setState(PhysicsDriver::State::Stop);
    m_route.reset();
}

void PoliceCar::routeTo(const Vec3& goal, const Vec3& heading) {
    // aiMap::CalcRoute + aiVehiclePhysics::RegisterRoute: the roads from the
    // cop to `goal` (OpenMM2 rebuilds the course every second or when the
    // goal moves on; MM2 every frame).
    m_destination = goal;
    m_destinationHeading = heading;
    const Vec3 pos = m_car.body.ics.matrix.m3;
    const bool stale = !m_route || m_routeAge > 1.0f || xzDist(goal, m_routeGoal) > 15.0f;
    if (stale) {
        m_route = Course::alongRoad(m_net, pos, goal);
        if (!m_route) {
            const int to = nearestIntersection(m_net, goal);
            if (to >= 0) {
                const int ids[] = {to};
                m_route = Course::build(m_net, ids, pos, goal, false);
            }
        }
        m_routeGoal = goal;
        m_routeAge = 0.0f;
        m_lastLeg = 0.0f;
        if (m_route) {
            m_s = m_route->locate(pos, m_route->startDistance(), 40.0f, &m_lateral);
            const float finish = m_route->finishDistance();
            m_lastLeg = finish;
            for (const CourseLeg& leg : m_route->legs())
                if (leg.end <= finish)
                    m_lastLeg = std::min(m_lastLeg, finish - leg.end);
        }
    }
}

DriveContext PoliceCar::context() {
    DriveContext ctx;
    ctx.destination = m_destination;
    ctx.destinationHeading = m_destinationHeading;
    if (m_route) {
        const Vec3 pos = m_car.body.ics.matrix.m3;
        m_s = m_route->locate(pos, m_s, 30.0f + m_car.speed() * 0.1f, &m_lateral);
        ctx.course = &*m_route;
        ctx.s = m_s;
        ctx.lateral = m_lateral;
        ctx.remaining = std::max(m_route->finishDistance() - m_s, 0.0f);
        ctx.finalApproach = ctx.remaining <= m_lastLeg + 0.5f;
    } else {
        ctx.finalApproach = true;
        ctx.remaining = xzDist(m_car.body.ics.matrix.m3, m_destination);
    }
    // RegisterRoute restarts the waypoint count with every new route, which
    // FollowPerpetrator and Block register every frame: a cop never gets
    // past its first waypoint, so it never steers round racers.
    ctx.waypointsPassed = 1;
    return ctx;
}

void PoliceCar::follow(const TrackedCar& perp, float dist) {
    // aiPoliceOfficer::FollowPerpetrator: siren on, the road route to the
    // suspect, arriving 5 m short of it at its speed + (distance - 12.5 m).
    if (m_pursuit != m_lastPursuit) {
        m_siren = true; // StartSiren
        m_lastPursuit = m_pursuit;
    }
    const auto state = m_driver.state();
    if (state != PhysicsDriver::State::Forward && state != PhysicsDriver::State::Shortcut)
        return;
    routeTo(perp.position, flatUnit(perp.forward));
    RouteParams& p = m_driver.params;
    p = {};
    p.maxThrottle = 1.0f;
    p.cornerSpeedFactor = 2.0f;
    p.brakeThreshold = 0.7f;
    p.lookAhead = 75.0f;
    p.avoidTraffic = true;
    p.avoidProps = true;
    p.avoidPlayers = false;
    p.avoidOpponents = true;
    p.preferSidewalk = false;
    p.destinationSpeed = perp.velocity.mag() + dist - 12.5f;
    p.stopShort = 5.0f;
}

void PoliceCar::apprehend(const TrackedCar& perp, std::span<const TrackedCar> cars) {
    // aiPoliceOfficer::ApprehendPerpetrator: block the suspect (its Push and
    // Barricade behaviours are never chosen in this build).
    (void)cars;
    if (m_pursuit != m_lastPursuit) {
        m_lastPursuit = m_pursuit;
        (void)m_random.frand(); // drawn and unused
        m_apprehend = kBlock;
    }
    if (m_apprehend == kBlock || m_apprehend == kMirror)
        block(perp);
}

void PoliceCar::block(const TrackedCar& perp) {
    // aiPoliceOfficer::Block.
    const auto state = m_driver.state();
    if (state != PhysicsDriver::State::Forward && state != PhysicsDriver::State::Shortcut)
        return;
    const Vec3 pos = m_car.body.ics.matrix.m3;
    const Vec3 f = flatUnit(perp.forward);
    const Vec3 r{-f.z, 0.0f, f.x};
    const Vec3 rel = pos - perp.position;
    const float along = rel.dot(f);
    if (m_apprehend != kBlock) {
        // Mirroring: until the suspect gets ahead of the cop again.
        if (along < 0.0f)
            m_apprehend = kBlock;
        return;
    }
    const float back = perp.halfLength;
    Vec3 goal;
    if (-back <= along) {
        // Level with or ahead of the suspect: 12 m in front of it.
        goal = perp.position + f * 12.0f;
    } else {
        // Behind it: beside its tail, on the cop's side (or, more than 20 m
        // behind on a road, the side that is on the road).
        const float sideOff = m_car.body.shape.half.x + perp.halfWidth + 1.0f;
        const float side = rel.dot(r);
        const Vec3 tail = perp.position - f * back;
        const Vec3 rightGoal = tail + r * sideOff, leftGoal = tail - r * sideOff;
        const RoadSpot perpSpot = locateOnRoads(m_net, perp.position);
        if (along > -20.0f || perpSpot.path < 0) {
            goal = side > 0.0f ? rightGoal : leftGoal;
        } else {
            const bool rightOn = locateOnRoads(m_net, rightGoal).onRoad;
            const bool leftOn = locateOnRoads(m_net, leftGoal).onRoad;
            if (rightOn == leftOn)
                goal = side <= 0.0f ? leftGoal : rightGoal;
            else
                goal = leftOn ? leftGoal : rightGoal;
        }
    }
    routeTo(goal, f);
    RouteParams& p = m_driver.params;
    p = {};
    p.maxThrottle = 1.0f;
    p.cornerSpeedFactor = 1.0f;
    p.brakeThreshold = 0.7f;
    p.lookAhead = 75.0f;
    p.avoidTraffic = true;
    p.avoidProps = true;
    p.avoidPlayers = false;
    p.avoidOpponents = true;
    p.destinationSpeed = perp.velocity.mag() + (-back <= along ? 0.0f : 25.0f);
    p.stopShort = 0.0f;
    if (xzDist(pos, goal) < 3.0f)
        m_apprehend = kMirror;
}

void PoliceCar::update(float dt, std::span<const TrackedCar> cars, PoliceForce& force, const phys::GroundQuery* los,
                       bool active) {
    // aiPoliceOfficer::Update.
    (void)los;
    for (const TrackedCar& c : cars)
        if (c.isPlayer)
            m_playerBody = c.body;
    const bool touching = m_touchingPlayer;
    m_touchingPlayer = false;
    m_routeAge += dt;
    auto& ics = m_car.body.ics;

    if (!active) {
        // OpenMM2: sessions in which the police are held.
        if (m_pursuit != 0 && m_pursuit != kOutOfAction)
            escape(force);
        m_driver.setState(PhysicsDriver::State::Stop);
        DriveContext ctx = context();
        m_driver.driveRoute(dt, cars, ctx);
        m_mode = m_pursuit == kOutOfAction ? Mode::Disabled : Mode::Parked;
        return;
    }

    const TrackedCar* perp = nullptr;
    if (m_pursuit != kOutOfAction) {
        if (m_pursuit == 0) {
            detect(cars, force);
            perp = findCar(cars, m_target);
        } else {
            perp = findCar(cars, m_target);
            if (!perp) {
                escape(force);
            } else {
                const Vec3 pos = ics.matrix.m3;
                const float dist = xzDist(pos, perp->position);
                int st = force.state(m_selfId, perp->id, cars, dist);
                const TrackedCar* player = firstPlayer(cars);
                // Follow only while the player reverses, the cop backs up,
                // it has no apprehend behaviours, or the suspect is slow.
                if ((player && player->reversing) || m_driver.state() == PhysicsDriver::State::Backup ||
                    m_settings.behaviours == 0 || perp->velocity.mag() < 10.0f)
                    st = PoliceForce::kFollow;
                m_pursuit = st == PoliceForce::kApprehend ? PoliceForce::kApprehend : PoliceForce::kFollow;
                if (m_pursuit == PoliceForce::kFollow)
                    follow(*perp, dist);
                else
                    apprehend(*perp, cars);
                if (dist > m_settings.chaseDistance) {
                    escape(force);
                    m_mode = Mode::Parked;
                    return;
                }
                if (m_driver.wrecked()) {
                    escape(force);
                    m_pursuit = kOutOfAction;
                    m_mode = Mode::Disabled;
                    return;
                }
            }
        }
        // Full throttle under 50 m/s: 3 % more momentum a frame.
        if (m_driver.throttle() == 1.0f && m_car.speed() < 50.0f)
            ics.linearMomentum = ics.linearMomentum * perFrame(1.03f, dt);
    }

    DriveContext ctx = context();
    ctx.touchingPlayer = touching;
    if (m_pursuit == PoliceForce::kApprehend && m_apprehend == kMirror && perp)
        m_driver.mirror(dt, *perp);
    else
        m_driver.driveRoute(dt, cars, ctx);
    if (m_driver.wrecked() && m_pursuit != kOutOfAction) {
        escape(force);
        m_pursuit = kOutOfAction;
    }
    // Fallen through the world: back to the post.
    if (ics.matrix.m3.y < -200.0f)
        reset();

    if (m_pursuit == kOutOfAction)
        m_mode = Mode::Disabled;
    else if (m_pursuit == 0)
        m_mode = Mode::Parked;
    else
        m_mode = Mode::Chasing;
}

// --- squad ---------------------------------------------------------------------

PoliceSquad::PoliceSquad(const RoadNetwork& net) : m_net(net) {}

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

std::size_t PoliceSquad::countForDensity(std::size_t count, float density) {
    return static_cast<std::size_t>(static_cast<float>(count) * clampf(density, 0.0f, 1.0f));
}

} // namespace mm2::ai
