// Parity checks for the AI vehicles against MM2's code (build 3393, see
// docs/parity/ai-vehicles.md): aiPoliceForce's bookkeeping, aiMap::CalcRoute
// and the obstacle geometry of aiVehicle, on synthetic data.
#include "ai/Course.h"
#include "ai/Driving.h"
#include "ai/Police.h"

#include <gtest/gtest.h>

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
