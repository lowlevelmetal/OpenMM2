// Parity checks for the AI vehicles against MM2's code (build 3393, see
// docs/parity/ai-vehicles.md): aiPoliceForce's bookkeeping, aiMap::CalcRoute,
// the obstacle geometry of aiVehicle and aiVehiclePhysics' route planner, on
// synthetic data.
#include "TestData.h"
#include "ai/Course.h"
#include "ai/Driving.h"
#include "ai/MapView.h"
#include "ai/Opponent.h"
#include "ai/Police.h"
#include "ai/Traffic.h"
#include "ai/World.h"
#include "city/CityData.h"
#include "phys/Bound.h"
#include "phys/vehicle/CarSim.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <tuple>
#include <utility>

using namespace mm2;

namespace {

// Four two-way roads round a 200 m square block: road i runs from corner i
// to corner i + 1 (c0 (0,0), c1 (0,-200), c2 (200,-200), c3 (200,0)),
// starting and ending 15 m from the corners; curbs at 5 m.
city::AiMap squareBlock() {
    const Vec3 corners[4] = {{0, 0, 0}, {0, 0, -200}, {200, 0, -200}, {200, 0, 0}};
    city::AiMap map;
    for (int i = 0; i < 4; ++i) {
        const Vec3 a = corners[i], b = corners[(i + 1) % 4];
        const Vec3 d = (b - a).normalized();
        const Vec3 left{d.z, 0.0f, -d.x};
        city::AiPath p;
        p.id = static_cast<std::uint16_t>(i);
        p.halfWidth = 8.0f;
        for (int k = 0; k < 3; ++k) {
            const float t = static_cast<float>(k) / 2.0f;
            p.center.push_back(lerp(a + d * 15.0f, b - d * 15.0f, t));
            p.xAxis.push_back(left);
            p.yAxis.push_back({0, 1, 0});
            p.zAxis.push_back(d * -1.0f);
            p.wAxis.push_back(d);
            if (k > 0)
                p.centerLengths.push_back(170.0f * t);
        }
        for (city::AiRoadSide* s : {&p.left, &p.right}) {
            s->numLanes = 1;
            s->numSidewalks = 1;
            const float sign = s == &p.left ? 1.0f : -1.0f;
            std::vector<Vec3> lane, walk, curb, edge;
            for (const Vec3& c : p.center) {
                lane.push_back(c + left * (2.0f * sign));
                walk.push_back(c + left * (6.5f * sign));
                curb.push_back(c + left * (5.0f * sign));
                edge.push_back(c + left * (8.0f * sign));
            }
            s->polylines = {lane, walk, curb, edge};
        }
        p.ends[0].intersection = static_cast<std::uint32_t>((i + 1) % 4); // at center.back()
        p.ends[1].intersection = static_cast<std::uint32_t>(i);           // at center.front()
        p.ends[0].vehicleRule = p.ends[1].vehicleRule = 3;
        map.paths.push_back(p);
    }
    for (int i = 0; i < 4; ++i) {
        const auto here = static_cast<std::uint32_t>(i), before = static_cast<std::uint32_t>((i + 3) % 4);
        map.intersections.push_back({static_cast<std::uint16_t>(i), 0, corners[i], {here, before}});
    }
    return map;
}

} // namespace

// aiPoliceForce::RegisterPerp does not check whether the cop is on the
// suspect already: a second registration takes a second place.
TEST(ParityAiPolice, RegisterPerpCountsARepeatedCopTwice) {
    ai::PoliceForce force;
    EXPECT_TRUE(force.registerPerp(1, 100));
    EXPECT_TRUE(force.registerPerp(1, 100));
    EXPECT_EQ(force.copsOn(100), 2);
    EXPECT_TRUE(force.registerPerp(2, 100));
    EXPECT_FALSE(force.registerPerp(3, 100));
    // Unregistering removes one of the two places.
    EXPECT_TRUE(force.unregisterCop(1, 100));
    EXPECT_EQ(force.copsOn(100), 2);
    EXPECT_TRUE(force.find(1, 100));
}

// DetectPerpetrator's opponent registration as coded passes the opponent
// as the pursuer and the cop's last suspect (none: -1) as the suspect.
TEST(ParityAiPolice, AnEmptiedSlotMatchesTheNoSuspectRegistration) {
    ai::PoliceForce force;
    EXPECT_TRUE(force.registerPerp(100, 0));  // cop 100 on the player
    EXPECT_TRUE(force.registerPerp(101, 5));  // cop 101 on opponent 5
    EXPECT_TRUE(force.unregisterCop(100, 0)); // slot 0 emptied (no suspect)
    // The next "no suspect" registration lands in the emptied slot 0.
    EXPECT_TRUE(force.registerPerp(7, -1));
    EXPECT_EQ(force.findPerp(-1), 0);
    EXPECT_EQ(force.copsOn(-1), 1);
}

// aiMap::PositionToAIMapComp stand-in: an intersection first, else the
// roads the point lies across.
TEST(ParityAiRoute, ComponentsOfAPoint) {
    const auto map = squareBlock();
    const auto net = ai::RoadNetwork::build(map, {});
    auto at = ai::componentsAt(net, {1, 0, -100});
    ASSERT_EQ(at.size(), 1u);
    EXPECT_EQ(at[0].id, 0);
    EXPECT_EQ(at[0].type, 1);
    at = ai::componentsAt(net, {1, 0, -1});
    ASSERT_EQ(at.size(), 1u);
    EXPECT_EQ(at[0].id, 0);
    EXPECT_EQ(at[0].type, 3);
    EXPECT_TRUE(ai::componentsAt(net, {100, 0, -100}).empty());
}

// aiMap::CalcRoute: from a point on road 0, 35 m from corner 0, to road 2.
// The ends of the start road are seeded with their distances along it
// (corner 0 at 35 m, corner 1 at 135 m); corner 3 (205 m) is reached
// before corner 2 (305 m) and ends the search. The route begins with the
// start road's far end so that its first leg is the car's own road.
TEST(ParityAiRoute, CalcRouteFromARoadToARoad) {
    const auto map = squareBlock();
    const auto net = ai::RoadNetwork::build(map, {});
    EXPECT_EQ(ai::calcRoute(net, {0, 0, -50}, {200, 0, -150}), (std::vector<int>{1, 0, 3}));
    // From corner 1 (an intersection) to road 2: its neighbour corner 2 is
    // one road away.
    EXPECT_EQ(ai::calcRoute(net, {0, 0, -200}, {200, 0, -150}), (std::vector<int>{1, 2}));
    // Both on the same road: no waypoints.
    EXPECT_TRUE(ai::calcRoute(net, {0, 0, -50}, {0, 0, -150}).empty());
}

// aiVehicle::IsBlockingTarget takes the corners from the bumper and side
// distances measured from the car's position, which need not be centred.
TEST(ParityAiDriving, BlockingCornersUseTheBumperDistances) {
    ai::TrackedCar car;
    car.position = {0, 0, -20};
    car.forward = {0, 0, -1};
    car.right = {1, 0, 0};
    car.frontBumper = 1.0f; // the front left corner is 21 m along the way
    car.backBumper = 3.0f;
    car.leftSide = 1.0f;
    car.rightSide = 1.0f;
    EXPECT_NEAR(ai::blockingDistance(car, {0, 0, 0}, {0, 0, -50}, 9.2f, 2.0f), 21.0f, 1e-3f);
    // Turned round, its front bumper 1 m behind its position is nearest.
    car.forward = {0, 0, 1};
    car.right = {-1, 0, 0};
    EXPECT_NEAR(ai::blockingDistance(car, {0, 0, 0}, {0, 0, -50}, 9.2f, 2.0f), 19.0f, 1e-3f);
}

// aiTrafficLightSet's constructor and SetFourWay take a path's end-1 rule,
// light position and light index when the path's end-1 intersection is the
// light set's intersection, else end 0's: a loop road with lights at both
// ends gets the end-1 light, controlling the lanes that arrive at end 1.
TEST(ParityAiLights, ALoopRoadTakesItsEndOneLight) {
    city::AiMap map = squareBlock();
    city::AiPath& loop = map.paths[0];
    loop.ends[0].intersection = loop.ends[1].intersection = 0;
    loop.ends[0].vehicleRule = loop.ends[1].vehicleRule = 1;
    loop.ends[0].trafficLightPos = {1, 0, 0};
    loop.ends[0].trafficLightAxis = {1, 0, 1};
    loop.ends[1].trafficLightPos = {2, 0, 0};
    loop.ends[1].trafficLightAxis = {3, 0, 0};
    const ai::RoadNetwork net = ai::RoadNetwork::build(map, {});
    ASSERT_EQ(net.lights().size(), 1u);
    const ai::TrafficLightSite& site = net.lights().front();
    EXPECT_EQ(site.path, 0);
    EXPECT_FLOAT_EQ(site.position.x, 2.0f);
    EXPECT_FLOAT_EQ(site.axis.x, 1.0f);
    // The lanes arriving at end 1 ride the first side (direction -1).
    const ai::Lane& lane = net.lanes()[static_cast<std::size_t>(site.lane)];
    EXPECT_EQ(lane.side, 0);
    EXPECT_EQ(lane.lightSlot, 0);
}

// aiGoalAvoidPlayer::Reset is where MM2 plays the avoidance horn (and the
// driver's reaction): Traffic reports each car that starts avoiding once.
TEST(ParityAiTraffic, ACarThatStartsAvoidingThePlayerIsReportedOnce) {
    const city::AiMap map = squareBlock();
    const ai::RoadNetwork net = ai::RoadNetwork::build(map, {});
    ai::TrafficLights lights;
    lights.build(net);
    ai::TrafficSettings settings;
    settings.density = 1.0f;
    ai::VehicleData sedan;
    sedan.model = "test";
    sedan.size = {2.0f, 1.5f, 4.5f};
    ai::Traffic traffic(net, lights, {sedan}, settings, 3);
    traffic.populateAll();
    ai::PlayerCar far;
    far.transform = Mat34::identity();
    far.transform.m3 = {1000, 0, 1000};
    for (int i = 0; i < 60; ++i)
        traffic.step(ai::kAiStepSeconds, far, 0);
    EXPECT_TRUE(traffic.takeAvoidEvents().empty());
    ASSERT_FALSE(traffic.cars().empty());
    const ai::AmbientCar car = traffic.cars().front();
    ASSERT_GT(car.speed, 1.0f);
    // The player parked in the car's lane 15 m ahead of it.
    ai::PlayerCar player;
    player.transform = car.transform;
    player.transform.m3 = car.transform.m3 - car.transform.m2 * 15.0f;
    std::vector<int> events;
    for (int i = 0; i < 30; ++i) {
        traffic.step(ai::kAiStepSeconds, player, 0);
        for (int id : traffic.takeAvoidEvents())
            events.push_back(id);
    }
    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events.front(), car.id);
    EXPECT_EQ(std::count(events.begin(), events.end(), car.id), 1);
}

// aiIntersection::StopSources marks every road whose end at the
// intersection has a stop sign or a light (aiPath::AllwaysStop); their
// ambient cars then never enter (OkayToEnterIntersection).
TEST(ParityAiTraffic, StopSourcesHoldsTheControlledRoads) {
    city::AiMap map = squareBlock();
    map.paths[0].ends[0].vehicleRule = 0; // road 0 arriving at corner 1: stop sign
    // road 1 leaves corner 1 (its end 1 there) uncontrolled
    const ai::RoadNetwork net = ai::RoadNetwork::build(map, {});
    ai::TrafficLights lights;
    lights.build(net);
    ai::TrafficSettings settings;
    settings.density = 1.0f;
    ai::VehicleData sedan;
    sedan.model = "test";
    sedan.size = {2.0f, 1.5f, 4.5f};
    ai::Traffic traffic(net, lights, {sedan}, settings, 3);
    traffic.stopSources(1, true);
    EXPECT_TRUE(traffic.alwaysStop(0));
    EXPECT_FALSE(traffic.alwaysStop(1));
    EXPECT_FALSE(traffic.alwaysStop(2));
    traffic.stopSources(1, false);
    EXPECT_FALSE(traffic.alwaysStop(0));
}

// aiPath::InitRoadTurns on the retail roads: London's main roads have 55
// sharp turns, San Francisco's 9, every one of two sections (a short
// section taken with the next); none is a single vertex. The shortcut roads
// have 67 (London; MM2 counts one more for road 578, whose record it never
// fills) and 56 (SF).
TEST(ParityAiRoads, RetailSharpTurns) {
    MM2_REQUIRE_GAME_DATA();
    for (const auto& [name, total, shortcutTurns] : {std::tuple{"london", 55, 67}, std::tuple{"sf", 9, 56}}) {
        const auto city = city::loadCity(*test::gameData(), name);
        ASSERT_TRUE(city && city->aiMap) << name;
        int turns = 0, merged = 0, onShortcuts = 0;
        const auto& paths = city->aiMap->paths;
        for (std::size_t k = 0; k < paths.size(); ++k) {
            const city::AiPath& p = paths[k];
            if (city->aiMap->isShortcut(k)) {
                onShortcuts += static_cast<int>(ai::initRoadTurns(p).size());
                continue;
            }
            for (const ai::SharpTurn& t : ai::initRoadTurns(p)) {
                ++turns;
                // A turn of two sections has a corner found by crossing the
                // two curb lines; a single one is 1 m off the curb.
                const auto& curb = ai::pathBoundary(t.dir < 0.0f ? p.left : p.right, 0);
                const Vec3 single = t.dir < 0.0f ? curb[static_cast<std::size_t>(t.vertex)] -
                                                       p.xAxis[static_cast<std::size_t>(t.vertex)]
                                                 : p.xAxis[static_cast<std::size_t>(t.vertex)] +
                                                       curb[static_cast<std::size_t>(t.vertex)];
                merged += t.point.x != single.x || t.point.z != single.z;
            }
        }
        EXPECT_EQ(turns, total) << name;
        EXPECT_EQ(merged, total) << name;
        EXPECT_EQ(onShortcuts, shortcutTurns) << name;
    }
}

namespace {

// The square block with the right side's lateral layout of a one-lane road
// (lane to the curb at 5 m, sidewalk to 8 m), which aiPath::IsPosOnRoad
// reads.
city::AiMap plannerBlock() {
    city::AiMap map = squareBlock();
    for (city::AiPath& p : map.paths)
        for (city::AiRoadSide* s : {&p.left, &p.right})
            s->params = {-8.0f, 5.0f, 5.0f, 8.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    return map;
}

// A car's frame facing -Z at (x, 0, z).
Mat34 facingNorth(float x, float z) {
    Mat34 m = Mat34::identity();
    m.m3 = {x, 0.0f, z};
    return m;
}

// A physics car with a 2 m x 4.6 m collision box (the driver's bumper and
// side distances, aiVehiclePhysics::Init), not simulated.
struct PlannerCar {
    phys::Bound bound{phys::BoundType::Box};
    phys::CarSim sim;
    PlannerCar(float x, float z) {
        bound.boxMin = {-1.0f, 0.0f, -2.3f};
        bound.boxMax = {1.0f, 1.4f, 2.3f};
        sim.body.collisionBound = &bound;
        sim.body.ics.matrix = facingNorth(x, z);
    }
};

} // namespace

// aiVehiclePhysics::RegisterRoute: a car on a road takes the window of the
// roads between its first waypoints (driven with or against their vertex
// order) and drives Forward towards waypoint 1; a car on no road or
// intersection drives Shortcut towards waypoint 0.
TEST(ParityAiPlanner, RegisterRouteOpensTheWindow) {
    const city::AiMap map = plannerBlock();
    const ai::RoadNetwork net = ai::RoadNetwork::build(map, {});
    const ai::MapView view(net);
    PlannerCar car(2.0f, -40.0f);
    phys::CarSim& sim = car.sim;
    ai::PhysicsDriver driver(sim, 1, &view, 0, "vppanoz");
    driver.registerRoute({{0, 1, 2, 3, 0}, 1, {0, 0, -45}, {}});
    EXPECT_EQ(driver.state(), ai::PhysicsDriver::State::Forward);
    EXPECT_EQ(driver.wayPointIndex(), 1);
    EXPECT_EQ(driver.windowRoad(0), 0);
    EXPECT_TRUE(driver.windowForward(0)); // corner 0 is road 0's end 1 (its vertex 0)
    EXPECT_EQ(driver.windowRoad(1), 1);
    EXPECT_TRUE(driver.windowForward(1));
    EXPECT_EQ(driver.windowRoad(2), 2);
    // aiVehiclePhysics::StopRoadTraffic: the intersections the window's
    // roads drive into.
    std::vector<int> held;
    driver.stopRoadTraffic(true, [&](int node, bool stop) {
        if (stop)
            held.push_back(node);
    });
    EXPECT_EQ(held, (std::vector<int>{1, 2, 3}));

    // Inside the block, on no road: Shortcut for waypoint 0.
    sim.body.ics.matrix = facingNorth(100.0f, -100.0f);
    driver.registerRoute({{0, 1, 2, 3, 0}, 1, {0, 0, -45}, {}});
    EXPECT_EQ(driver.state(), ai::PhysicsDriver::State::Shortcut);
    EXPECT_EQ(driver.wayPointIndex(), 0);
}

// aiRouteRacer::Init: each .opp row between the grid place and the
// destination names the first intersection of its room; circuits are
// driven the race's laps, other races once.
TEST(ParityAiPlanner, OppRowsBecomeWaypointIntersections) {
    const city::AiMap map = plannerBlock();
    const ai::RoadNetwork net = ai::RoadNetwork::build(map, {});
    const ai::MapView view(net);
    std::vector<city::OpponentPoint> rows(7);
    const Vec3 at[] = {{2, 0, -40},  {1, 0, -1}, {1, 0, -199}, {199, 0, -199},
                       {199, 0, -1}, {1, 0, -1}, {2, 0, -45}};
    for (std::size_t i = 0; i < rows.size(); ++i)
        rows[i].position = at[i];
    const ai::RouteRegistration circuit = ai::Opponent::routeFromPath(view, rows, 3);
    EXPECT_EQ(circuit.wayPoints, (std::vector<int>{0, 1, 2, 3, 0}));
    EXPECT_EQ(circuit.laps, 3);
    EXPECT_FLOAT_EQ(circuit.destination.z, -45.0f);
    EXPECT_EQ(ai::Opponent::routeFromPath(view, rows, 0).laps, 1);
}

// aiVehiclePhysics::Shortcut / SolveShortcutTargetPoint: off the roads the
// car aims at the next waypoint intersection's centre (1 m up); inside it
// the window is rebuilt from the waypoints after it and the car drives on
// Forward.
TEST(ParityAiPlanner, ShortcutAimsAtTheWaypointIntersection) {
    const city::AiMap map = plannerBlock();
    const ai::RoadNetwork net = ai::RoadNetwork::build(map, {});
    const ai::MapView view(net);
    PlannerCar car(100.0f, -100.0f);
    phys::CarSim& sim = car.sim;
    ai::PhysicsDriver driver(sim, 1, &view, 0, "vppanoz");
    driver.registerRoute({{0, 1, 2, 3, 0}, 1, {0, 0, -45}, {}});
    ASSERT_EQ(driver.state(), ai::PhysicsDriver::State::Shortcut);
    driver.driveRoute(1.0f / 30.0f, {}, {});
    EXPECT_FLOAT_EQ(driver.target().x, 0.0f);
    EXPECT_FLOAT_EQ(driver.target().y, 1.0f);
    EXPECT_FLOAT_EQ(driver.target().z, 0.0f);
    // In corner 0: on along roads 0, 1 and 2, heading for waypoint 1.
    sim.body.ics.matrix = facingNorth(1.0f, -1.0f);
    driver.driveRoute(1.0f / 30.0f, {}, {});
    EXPECT_EQ(driver.state(), ai::PhysicsDriver::State::Forward);
    EXPECT_EQ(driver.wayPointIndex(), 1);
    EXPECT_EQ(driver.windowRoad(0), 0);
    EXPECT_EQ(driver.windowRoad(1), 1);
    EXPECT_EQ(driver.windowRoad(2), 2);
}

// aiVehiclePhysics::CalcRoute on a clear road 80 m before a right turn: the
// junction turn's circle (CalcRoadTurns / CalcTurnIntersection) has its
// corner where the two roads' right curbs, moved in by the car's right side
// (1 m), cross: (4, -196). The car's room is its distance across the road
// from the corner (2 m) + 1 m for being beyond the set-back, so the radius
// is 3 / (1 - sin((3.14 - pi/2) / 2)) = 10.22 m and the set-back 7.23 m.
// CalcRoadTarget's first target is that corner, which lies in the circle,
// so CalcSharpTurnTarget aims at the arc's start instead, then steps round
// the arc (SaveTurnTarget) onto road 1, whose own turn circle (sized from
// where the car is now: the least room, the car's width) gives the next
// target. The car aims at the first.
TEST(ParityAiPlanner, RoadTargetsFollowTheTurnCircles) {
    const city::AiMap map = plannerBlock();
    const ai::RoadNetwork net = ai::RoadNetwork::build(map, {});
    const ai::MapView view(net);
    PlannerCar car(2.0f, -120.0f);
    ai::PhysicsDriver driver(car.sim, 1, &view, 0, "vppanoz");
    driver.params.lookAhead = 150.0f;
    driver.registerRoute({{0, 1, 2, 3, 0}, 1, {0, 0, -45}, {}});
    driver.driveRoute(1.0f / 30.0f, {}, {});
    const auto nodes = driver.bestNodes();
    ASSERT_EQ(nodes.size(), 5u);
    const float r = 3.0f / (1.0f - std::sin((3.14f - kHalfPi) * 0.5f));
    const float setback = std::cos((3.14f - kHalfPi) * 0.5f) * r;
    const Vec3 centre{4.0f + (r - 3.0f), 0.0f, -196.0f + setback};
    // The arc's start, on road 0, 1 m up.
    EXPECT_NEAR(nodes[1].pos.x, centre.x - r, 0.01f);
    EXPECT_NEAR(nodes[1].pos.z, centre.z, 0.01f);
    EXPECT_FLOAT_EQ(nodes[1].pos.y, 1.0f);
    EXPECT_EQ(nodes[1].road, 0);
    // Round the arc (turn targets) onto road 1.
    for (std::size_t i = 2; i <= 3; ++i) {
        EXPECT_EQ(nodes[i].kind, 1);
        EXPECT_NEAR(Vec2(nodes[i].pos.x - centre.x, nodes[i].pos.z - centre.z).mag(), r, 0.02f);
        EXPECT_EQ(nodes[i].road, 1);
    }
    EXPECT_NEAR(nodes[3].pos.z, centre.z - r, 0.01f);
    // The next turn's arc start, on road 1: radius 2 / (1 - sin) round the
    // corner (196, -196).
    const float r2 = 2.0f / (1.0f - std::sin((3.14f - kHalfPi) * 0.5f));
    EXPECT_NEAR(nodes[4].pos.x, 196.0f - std::cos((3.14f - kHalfPi) * 0.5f) * r2, 0.01f);
    EXPECT_NEAR(nodes[4].pos.z, -196.0f - 2.0f, 0.01f);
    EXPECT_EQ(nodes[4].road, 1);
    EXPECT_FLOAT_EQ(driver.target().x, nodes[1].pos.x);
    EXPECT_FLOAT_EQ(driver.target().z, nodes[1].pos.z);
}

