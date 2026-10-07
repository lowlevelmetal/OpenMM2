// Racer and police AI logic on a synthetic road network (no game data):
// driving lines from .opp rows, MM1's turn braking, routing, aiPoliceForce.
#include "ai/Course.h"
#include "ai/Driving.h"
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

TEST(AiDriving, TurnBrakeFollowsCalcSpeed) {
    const auto map = squareBlock();
    const auto net = ai::RoadNetwork::build(map, {});
    const auto course = ai::Course::fromOpponentPath(net, circuitRows(), true);
    ASSERT_TRUE(course);
    ASSERT_EQ(course->turns().size(), 4u);
    const ai::CourseTurn turn = course->turns()[1];
    // r = W / (1 - sin((pi - d) / 2)), vmax = sqrt(23.76 r).
    const float r = 5.0f / (1.0f - std::sin(kHalfPi * 0.5f));
    const float vmax = std::sqrt(23.76f * r);
    float limit = 0.0f;
    const float far = ai::turnBrake(*course, turn.s - 100.0f, 0.0f, 30.0f, 23.76f, 23.76f, &limit);
    EXPECT_NEAR(limit, vmax, 0.05f);
    const float entry = 100.0f - r * std::cos(kHalfPi * 0.5f);
    EXPECT_NEAR(far, (30.0f - vmax) / (23.76f * entry / 30.0f), 0.01f);
    EXPECT_LT(far, 0.7f); // MM1 keeps the throttle on
    EXPECT_GT(ai::turnBrake(*course, turn.s - 20.0f, 0.0f, 30.0f, 23.76f, 23.76f), 0.7f);
    // Slow enough: no braking. On the inside of a right turn the arc is tighter.
    EXPECT_EQ(ai::turnBrake(*course, turn.s - 20.0f, 0.0f, vmax - 1.0f, 23.76f, 23.76f), 0.0f);
    float inside = 0.0f;
    ai::turnBrake(*course, turn.s - 100.0f, 3.0f, 30.0f, 23.76f, 23.76f, &inside);
    EXPECT_LT(inside, vmax);
}

TEST(AiDriving, ObstacleScanFindsTheNearestGap) {
    ai::TrackedCar slow;
    slow.id = 7;
    slow.position = {0, 0, -20};
    slow.forward = {0, 0, -1};
    slow.velocity = {0, 0, -5};
    ai::ScanInput in;
    in.position = {0, 0, 0};
    in.lineDir = {0, 0, -1};
    in.speed = 20.0f;
    in.selfId = 1;
    const auto scan = ai::scanObstacles(in, std::span<const ai::TrackedCar>(&slow, 1));
    ASSERT_EQ(scan.blocked.size(), 1u);
    float side = 0.0f;
    ASSERT_TRUE(scan.freeSide(0.5f, -6.0f, 6.0f, side));
    EXPECT_NEAR(side, 1.0f + 1.0f + 0.75f, 1e-4f); // its half width + ours + 0.75 m, on the near side
    EXPECT_FALSE(scan.freeSide(0.0f, -1.0f, 1.0f, side)); // no room on a narrow road
    ASSERT_TRUE(scan.blocking(0.0f));
    EXPECT_NEAR(scan.blocking(0.0f)->speed, 5.0f, 1e-4f);
    // A car pulling away is not an obstacle.
    slow.velocity = {0, 0, -30};
    EXPECT_TRUE(ai::scanObstacles(in, std::span<const ai::TrackedCar>(&slow, 1)).blocked.empty());
}

TEST(AiPolice, ForceLimitsAndCloseInState) {
    ai::PoliceForce force; // three cops per suspect, three suspects
    EXPECT_TRUE(force.registerPerp(1, 100));
    EXPECT_TRUE(force.registerPerp(2, 100));
    EXPECT_TRUE(force.registerPerp(2, 100)); // already pursuing
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
    EXPECT_EQ(force.state(1, 100, cars, 10.0f), 3); // nearest and within 25 m: close in
    EXPECT_EQ(force.state(2, 100, cars, 30.0f), 4); // follow
    EXPECT_EQ(force.state(1, 100, cars, 26.0f), 4);
    EXPECT_EQ(force.state(1, 999, cars, 10.0f), 9);

    EXPECT_TRUE(force.unregisterCop(1, 100));
    EXPECT_FALSE(force.find(1, 100));
    EXPECT_EQ(force.copsOn(100), 2);
    EXPECT_TRUE(force.unregisterCop(5, 101)); // last cop: the suspect is dropped
    EXPECT_EQ(force.findPerp(101), -1);
    EXPECT_TRUE(force.registerPerp(7, 103));
}

TEST(AiPolice, DensityPicksEvenlySpacedPosts) {
    EXPECT_TRUE(ai::PoliceSquad::pickByDensity(20, 0.0f).empty());
    EXPECT_EQ(ai::PoliceSquad::pickByDensity(20, 1.0f).size(), 20u);
    EXPECT_EQ(ai::PoliceSquad::pickByDensity(20, 0.5f), (std::vector<std::size_t>{0, 2, 4, 6, 8, 10, 12, 14, 16, 18}));
}
