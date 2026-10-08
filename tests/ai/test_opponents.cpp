// Racer and police AI logic on a synthetic road network (no game data):
// driving lines from .opp rows, obstacle geometry, aiRaceData settings,
// aiPoliceForce. (The route planner's tests are in test_parity_ai_vehicles.)
#include "ai/Course.h"
#include "ai/Driving.h"
#include "ai/Opponent.h"
#include "ai/Police.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace mm2;

namespace {

// A 200 m square block, driven clockwise seen from above: corners
// c0 (0,0) -> c1 (0,-200) -> c2 (200,-200) -> c3 (200,0). Each side is one
// two-way road starting 15 m from its intersections, with a lane each side
// at 2 m, curbs at 5 m and sidewalks beyond.
city::AiMap squareBlock() {
    const Vec3 corners[4] = {{0, 0, 0}, {0, 0, -200}, {200, 0, -200}, {200, 0, 0}};
    city::AiMap map;
    for (int i = 0; i < 4; ++i) {
        const Vec3 a = corners[i], b = corners[(i + 1) % 4];
        const Vec3 d = (b - a).normalized();
        const Vec3 left{d.z, 0.0f, -d.x}; // section x axis: left of travel
        city::AiPath p;
        p.id = static_cast<std::uint16_t>(i);
        p.halfWidth = 8.0f;
        const int sections = 3;
        for (int k = 0; k < sections; ++k) {
            const float t = static_cast<float>(k) / (sections - 1);
            p.center.push_back(lerp(a + d * 15.0f, b - d * 15.0f, t));
            p.xAxis.push_back(left);
            p.yAxis.push_back({0, 1, 0});
            p.zAxis.push_back(d * -1.0f);
            p.wAxis.push_back(d);
            if (k > 0)
                p.centerLengths.push_back(170.0f * t);
        }
        auto side = [&](float sign, bool reversed) {
            city::AiRoadSide s;
            s.numLanes = 1;
            s.numSidewalks = 1;
            std::vector<Vec3> lane, walk, curb, edge;
            for (int k = 0; k < sections; ++k) {
                const Vec3& c = p.center[static_cast<std::size_t>(reversed ? sections - 1 - k : k)];
                lane.push_back(c + left * (2.0f * sign));
                walk.push_back(p.center[static_cast<std::size_t>(k)] + left * (6.5f * sign));
                curb.push_back(p.center[static_cast<std::size_t>(k)] + left * (5.0f * sign));
                edge.push_back(p.center[static_cast<std::size_t>(k)] + left * (8.0f * sign));
            }
            s.polylines = {lane, walk, curb, edge};
            return s;
        };
        p.left = side(1.0f, true);
        p.right = side(-1.0f, false);
        p.ends[0].intersection = static_cast<std::uint32_t>((i + 1) % 4); // at center.back()
        p.ends[1].intersection = static_cast<std::uint32_t>(i);           // at center.front()
        p.ends[0].vehicleRule = p.ends[1].vehicleRule = 3;
        map.paths.push_back(p);
    }
    for (int i = 0; i < 4; ++i)
        map.intersections.push_back({static_cast<std::uint16_t>(i), 0, corners[i],
                                     {static_cast<std::uint32_t>(i), static_cast<std::uint32_t>((i + 3) % 4)}});
    return map;
}

// A circuit as the .opp files store it: grid place 40 m up the first road,
// the corners, then the finish line beside the grid.
std::vector<city::OpponentPoint> circuitRows() {
    std::vector<city::OpponentPoint> rows;
    auto row = [&](float x, float z) {
        city::OpponentPoint p;
        p.position = {x, 0, z};
        rows.push_back(p);
    };
    row(0, -40);
    row(0, 0);
    row(0, -200);
    row(200, -200);
    row(200, 0);
    row(0, 0);
    row(0, -45);
    return rows;
}

} // namespace

TEST(AiCourse, CircuitFromOpponentRows) {
    const auto map = squareBlock();
    const auto net = ai::RoadNetwork::build(map, {});
    std::string error;
    const auto course = ai::Course::fromOpponentPath(net, circuitRows(), true, &error);
    ASSERT_TRUE(course) << error;
    EXPECT_TRUE(course->loop());
    EXPECT_EQ(course->intersections(), (std::vector<int>{0, 1, 2, 3, 0}));
    // Four roads of 170 m and four chords of 15 * sqrt(2) m across the corners.
    EXPECT_NEAR(course->length(), 4.0f * (170.0f + 15.0f * std::sqrt(2.0f)), 0.5f);
    EXPECT_NEAR(course->startDistance(), 25.0f, 0.5f);
    EXPECT_NEAR(course->finishDistance(), 30.0f, 0.5f);
    EXPECT_NEAR(course->raceDistance(2), 2.0f * course->length() + 5.0f, 0.5f);
    // Four right turns of 90 degrees (the two bends of each chord merged).
    ASSERT_EQ(course->turns().size(), 4u);
    for (const auto& t : course->turns()) {
        EXPECT_NEAR(t.deflection, kHalfPi, 0.01f);
        EXPECT_NEAR(t.halfWidth, 5.0f, 0.01f);
    }
    // Curbs at 5 m either side; lateral offsets are + right of travel.
    float left = 0, right = 0;
    course->edges(100.0f, left, right);
    EXPECT_NEAR(left, 5.0f, 1e-3f);
    EXPECT_NEAR(right, 5.0f, 1e-3f);
    float lateral = 0;
    const float s = course->locate({3, 0, -100}, 80.0f, 40.0f, &lateral);
    EXPECT_NEAR(s, 85.0f, 0.01f);
    EXPECT_NEAR(lateral, 3.0f, 0.01f); // +X is right when driving -Z
    // Loops wrap.
    EXPECT_NEAR(course->pointAt(course->length() + 10.0f).z, course->pointAt(10.0f).z, 1e-3f);
}

TEST(AiCourse, RoutesAroundGapsAndAvoidedRoads) {
    const auto map = squareBlock();
    const auto net = ai::RoadNetwork::build(map, {});
    // Opposite corners: two equal routes; avoiding road 0 picks the other.
    const int avoid = 0;
    EXPECT_EQ(ai::findRoute(net, 0, 2, std::span<const int>(&avoid, 1)), (std::vector<int>{0, 3, 2}));
    // A point-to-point line with a gap is filled along the roads.
    const std::vector<int> waypoints{0, 2};
    const auto course = ai::Course::build(net, waypoints, {0, 0, -20}, Vec3{200, 0, -180}, false);
    ASSERT_TRUE(course);
    EXPECT_EQ(course->intersections().front(), 0);
    EXPECT_EQ(course->intersections().back(), 2);
    EXPECT_EQ(course->intersections().size(), 3u);
    EXPECT_NEAR(course->pointAt(course->finishDistance()).z, -180.0f, 0.5f);
    // Off-road points are not on the road; road and intersection points are.
    EXPECT_TRUE(ai::locateOnRoads(net, {2, 0, -100}).onRoad);
    EXPECT_TRUE(ai::locateOnRoads(net, {1, 0, -1}).onRoad);
    EXPECT_FALSE(ai::locateOnRoads(net, {50, 0, -100}).onRoad);
}

// aiVehicle::IsBlockingTarget / PreAvoid.
TEST(AiDriving, ObstacleBlockingAndAvoidPoints) {
    ai::TrackedCar car;
    car.id = 7;
    car.position = {0, 0, -20};
    car.forward = {0, 0, -1};
    car.halfWidth = 1.0f;
    car.halfLength = 2.3f;
    const Vec3 from{0, 0, 0}, to{0, 0, -50};
    // The first corner in the path (front left) is 22.3 m along the way.
    EXPECT_NEAR(ai::blockingDistance(car, from, to, 9.2f, 2.0f), 22.3f, 1e-3f);
    // 4 m to the side: clear of a 2 m wide car (+1 m either side).
    car.position.x = 4.0f;
    EXPECT_EQ(ai::blockingDistance(car, from, to, 9.2f, 2.0f), -1.0f);
    // Beyond the way plus the extra length: clear.
    car.position = {0, 0, -70};
    EXPECT_EQ(ai::blockingDistance(car, from, to, 9.2f, 2.0f), -1.0f);

    car.position = {0, 0, -20};
    Vec3 left, right;
    ai::avoidPoints(car, from, {0, 0, -1}, 3.0f, left, right);
    // The rear corners pushed 3 m out across the line of sight to them.
    EXPECT_NEAR(left.x, -3.99f, 0.01f);
    EXPECT_NEAR(left.z, -17.53f, 0.01f);
    EXPECT_NEAR(right.x, 3.99f, 0.01f);
    EXPECT_NEAR(right.z, -17.53f, 0.01f);
}

// aiPath::IsPosOnRoad's limits: the right side's lateral layout, lane and
// sidewalk boundaries in turn.
TEST(AiCourse, OnRoadLimitsFromTheSideLayout) {
    city::AiMap map = squareBlock();
    city::AiPath& p = map.paths[0];
    float road = 0.0f, sidewalk = 0.0f;
    // Two lanes (as London's "-10 2 2 5 5 10", curbs at 6): road to 5 m.
    p.right.numLanes = 2;
    p.right.params = {-10.0f, 2.0f, 2.0f, 5.0f, 5.0f, 10.0f, -4e8f, -4e8f, -4e8f, -4e8f};
    ai::pathOnRoadLimits(p, road, sidewalk);
    EXPECT_FLOAT_EQ(road, 5.0f);
    EXPECT_FLOAT_EQ(sidewalk, 10.0f);
    // One lane ("-9 4.25 4.25 9").
    p.right.numLanes = 1;
    p.right.params = {-9.0f, 4.25f, 4.25f, 9.0f, -4e8f, -4e8f, -4e8f, -4e8f, -4e8f, -4e8f};
    ai::pathOnRoadLimits(p, road, sidewalk);
    EXPECT_FLOAT_EQ(road, 4.25f);
    EXPECT_FLOAT_EQ(sidewalk, 9.0f);
    // No lanes on the right (one-way SF alleys): sidewalk only, to params[1].
    p.right.numLanes = 0;
    p.right.params = {-2e37f, 3.5f, -4e8f, -4e8f, -4e8f, -4e8f, -4e8f, -4e8f, -4e8f, -4e8f};
    ai::pathOnRoadLimits(p, road, sidewalk);
    EXPECT_FLOAT_EQ(road, 0.0f);
    EXPECT_FLOAT_EQ(sidewalk, 3.5f);
    // No layout (this hand-built map): the curbs (5 m) and outer edges (8 m).
    p.right.numLanes = 1;
    p.right.params = {};
    ai::pathOnRoadLimits(p, road, sidewalk);
    EXPECT_NEAR(road, 5.0f, 1e-4f);
    EXPECT_NEAR(sidewalk, 8.0f, 1e-4f);

    // aiVehiclePhysics::Init's type 3 is the only one kept off the sidewalk.
    EXPECT_FALSE(ai::goesOverSidewalks("vppanozgt"));
    EXPECT_TRUE(ai::goesOverSidewalks("vpsemi"));
    EXPECT_TRUE(ai::goesOverSidewalks("vppanoz"));
    EXPECT_TRUE(ai::goesOverSidewalks(""));
}

// aiRaceData's [Opponent] record.
TEST(AiOpponent, SettingsFromTheAimapLine) {
    const float line[] = {0.86f, 0.0f, 150.0f, 0.69f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.49f};
    const auto s = ai::OpponentSettings::fromData(line, 3);
    EXPECT_FLOAT_EQ(s.route.maxThrottle, 0.86f);
    EXPECT_FLOAT_EQ(s.route.lookAhead, 150.0f);
    EXPECT_FLOAT_EQ(s.route.brakeThreshold, 0.69f);
    EXPECT_TRUE(s.route.avoidTraffic);
    EXPECT_FALSE(s.route.avoidProps);
    EXPECT_FALSE(s.route.avoidPlayers);
    EXPECT_TRUE(s.route.avoidOpponents);
    EXPECT_FALSE(s.route.preferSidewalk);
    EXPECT_FLOAT_EQ(s.route.cornerSpeedFactor, 1.49f);
    EXPECT_EQ(s.laps, 3);
    EXPECT_TRUE(s.repairWhenWrecked); // circuits only
    // Missing numbers: aiRaceData's defaults.
    const auto d = ai::OpponentSettings::fromData({}, 0);
    EXPECT_FLOAT_EQ(d.route.maxThrottle, 1.0f);
    EXPECT_FLOAT_EQ(d.route.lookAhead, 50.0f);
    EXPECT_FLOAT_EQ(d.route.brakeThreshold, 0.7f);
    EXPECT_TRUE(d.route.avoidTraffic && d.route.avoidProps && d.route.avoidPlayers && d.route.avoidOpponents);
    EXPECT_FLOAT_EQ(d.route.cornerSpeedFactor, 1.0f);
    EXPECT_FALSE(d.repairWhenWrecked);
}

TEST(AiPolice, SettingsFromTheAimapLine) {
    const float line[] = {0.0f, 15.0f, 0.5f, 70.0f};
    auto s = ai::PoliceSettings::fromData(line, 150.0f);
    EXPECT_EQ(s.behaviours, 15u);
    EXPECT_FLOAT_EQ(s.opponentChance, 0.5f);
    EXPECT_FLOAT_EQ(s.opponentRange, 70.0f);
    EXPECT_FLOAT_EQ(s.chaseDistance, 150.0f);
    s = ai::PoliceSettings::fromData({});
    EXPECT_FLOAT_EQ(s.chaseDistance, 250.0f); // aiRaceData default
    EXPECT_FLOAT_EQ(s.detectRange, 75.0f);
    EXPECT_FLOAT_EQ(s.opponentChance, 0.5f);
    EXPECT_FLOAT_EQ(s.opponentRange, 50.0f);
}

TEST(AiPolice, ForceLimitsAndApprehendState) {
    ai::PoliceForce force; // three cops per suspect, three suspects
    EXPECT_TRUE(force.registerPerp(1, 100));
    EXPECT_TRUE(force.registerPerp(2, 100));
    EXPECT_TRUE(force.registerPerp(3, 100));
    EXPECT_FALSE(force.registerPerp(4, 100));
    EXPECT_EQ(force.copsOn(100), 3);
    EXPECT_TRUE(force.registerPerp(5, 101));
    EXPECT_TRUE(force.registerPerp(6, 102));
    EXPECT_FALSE(force.registerPerp(7, 103));

    std::vector<ai::TrackedCar> cars(3);
    cars[0].id = 100;
    cars[1].id = 1;
    cars[1].position = {10, 0, 0};
    cars[2].id = 2;
    cars[2].position = {30, 0, 0};
    // aiPoliceForce::State: the nearest pursuer within 25 m apprehends.
    EXPECT_EQ(force.state(1, 100, cars, 10.0f), ai::PoliceForce::kApprehend);
    EXPECT_EQ(force.state(2, 100, cars, 30.0f), ai::PoliceForce::kFollow);
    EXPECT_EQ(force.state(1, 100, cars, 26.0f), ai::PoliceForce::kFollow);
    EXPECT_EQ(force.state(1, 999, cars, 10.0f), ai::PoliceForce::kNotPursued);

    EXPECT_TRUE(force.unregisterCop(1, 100));
    EXPECT_FALSE(force.find(1, 100));
    EXPECT_EQ(force.copsOn(100), 2);
    EXPECT_TRUE(force.unregisterCop(5, 101)); // last cop: the suspect is dropped
    EXPECT_EQ(force.findPerp(101), -1);
    // aiPoliceForce::UnRegisterCop does not move the later suspects down:
    // 102, in the third slot, is no longer among the two counted.
    EXPECT_EQ(force.findPerp(102), -1);
    EXPECT_EQ(force.state(6, 102, cars, 10.0f), ai::PoliceForce::kNotPursued);
    EXPECT_TRUE(force.registerPerp(7, 103)); // takes the third slot
    EXPECT_EQ(force.findPerp(103), 2);
}

TEST(AiPolice, DensityPlacesTheFirstPosts) {
    // aiMap::Init: trunc(count * clamp(density, 0, 1)).
    EXPECT_EQ(ai::PoliceSquad::countForDensity(20, 0.0f), 0u);
    EXPECT_EQ(ai::PoliceSquad::countForDensity(20, 1.0f), 20u);
    EXPECT_EQ(ai::PoliceSquad::countForDensity(20, 0.5f), 10u);
    EXPECT_EQ(ai::PoliceSquad::countForDensity(7, 0.5f), 3u);
    EXPECT_EQ(ai::PoliceSquad::countForDensity(7, 3.0f), 7u); // a race's cop count
}
