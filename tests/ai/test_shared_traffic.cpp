// The ambient traffic with several players (MM2's aiMap player list), as the
// shared traffic of a network cruise uses it: the roads round every player
// are populated, a player who leaves gives up its roads, and the cars avoid
// whichever player is in their way.
#include "ai/Traffic.h"
#include "ai/World.h"
#include "city/CityData.h"

#include "TestData.h"

#include <gtest/gtest.h>

#include <algorithm>

using namespace mm2;

namespace {

// A straight two-way road from `a` to `b`, one lane a side, as the retail
// files lay it out (see test_traffic.cpp's `road`).
city::AiPath road(int id, Vec3 a, Vec3 b, std::uint32_t nodeA, std::uint32_t nodeB) {
    city::AiPath p;
    p.id = static_cast<std::uint16_t>(id);
    p.flags = 0x8;
    p.halfWidth = 7.0f;
    const Vec3 d = (b - a).normalized();
    const Vec3 left{d.z, 0.0f, -d.x};
    constexpr int kSections = 4;
    for (int i = 0; i < kSections; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSections - 1);
        p.center.push_back(lerp(a, b, t));
        p.xAxis.push_back(left);
        p.yAxis.push_back(Vec3::yAxis());
        p.zAxis.push_back(-d);
        p.wAxis.push_back(d);
        if (i > 0)
            p.centerLengths.push_back(a.dist(b) * t);
    }
    auto side = [&](float sign, bool reversed) {
        city::AiRoadSide s;
        s.numLanes = 1;
        s.numSidewalks = 1;
        std::vector<Vec3> lane, walk, curb, edge;
        for (int i = 0; i < kSections; ++i) {
            const int k = reversed ? kSections - 1 - i : i;
            lane.push_back(p.center[static_cast<std::size_t>(k)] + left * (sign * 2.0f));
            walk.push_back(p.center[static_cast<std::size_t>(i)] + left * (sign * 5.5f));
            curb.push_back(p.center[static_cast<std::size_t>(i)] + left * (sign * 4.0f));
            edge.push_back(p.center[static_cast<std::size_t>(i)] + left * (sign * 7.0f));
        }
        s.polylines = {lane, walk, curb, edge};
        return s;
    };
    p.left = side(1.0f, true);
    p.right = side(-1.0f, false);
    p.ends[0].intersection = nodeB;
    p.ends[1].intersection = nodeA;
    p.ends[0].vehicleRule = 3;
    p.ends[1].vehicleRule = 3;
    return p;
}

// A square block, 200 m a side, without lights.
city::AiMap squareBlock() {
    city::AiMap map;
    const Vec3 corner[4] = {{0, 0, 0}, {0, 0, -200}, {200, 0, -200}, {200, 0, 0}};
    for (int k = 0; k < 4; ++k)
        map.paths.push_back(road(k, corner[k], corner[(k + 1) % 4], static_cast<std::uint32_t>(k),
                                 static_cast<std::uint32_t>((k + 1) % 4)));
    for (int k = 0; k < 4; ++k)
        map.intersections.push_back({static_cast<std::uint16_t>(k), 0, corner[k],
                                     {static_cast<std::uint32_t>((k + 3) % 4), static_cast<std::uint32_t>(k)}});
    for (auto& node : map.intersections)
        for (std::size_t k = 0; k < node.paths.size(); ++k)
            for (auto& e : map.paths[node.paths[k]].ends)
                if (e.intersection == node.id)
                    e.roadIndex = static_cast<std::uint16_t>(k);
    return map;
}

ai::PlayerCar parkedAt(const Vec3& p) {
    ai::PlayerCar c;
    c.transform = Mat34::identity();
    c.transform.m3 = p;
    return c;
}

} // namespace

TEST(SharedTraffic, CarsAvoidWhicheverPlayerIsInTheirWay) {
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
    const ai::TrafficPlayer local{0, parkedAt({1000, 0, 1000}), 0};
    for (int i = 0; i < 60; ++i)
        traffic.step(ai::kAiStepSeconds, std::span(&local, 1));
    EXPECT_TRUE(traffic.takeAvoidEvents().empty());
    ASSERT_FALSE(traffic.cars().empty());
    const ai::AmbientCar car = traffic.cars().front();
    ASSERT_GT(car.speed, 1.0f);
    // Another player (slot 5) parked in the car's lane 15 m ahead of it.
    ai::TrafficPlayer other{5, parkedAt(car.transform.m3 - car.transform.m2 * 15.0f), 0};
    other.car.transform.m0 = car.transform.m0;
    other.car.transform.m1 = car.transform.m1;
    other.car.transform.m2 = car.transform.m2;
    const ai::TrafficPlayer both[2] = {local, other};
    std::vector<int> events;
    bool avoiding = false;
    for (int i = 0; i < 30; ++i) {
        traffic.step(ai::kAiStepSeconds, both);
        for (int id : traffic.takeAvoidEvents())
            events.push_back(id);
        for (const auto& c : traffic.cars())
            if (c.id == car.id && c.goal == ai::AmbientGoal::AvoidPlayer)
                avoiding = true;
    }
    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events.front(), car.id);
    EXPECT_TRUE(avoiding);
    // The other player leaves; the traffic carries on round the last place
    // it saw it, and nothing breaks.
    for (int i = 0; i < 120; ++i)
        traffic.step(ai::kAiStepSeconds, std::span(&local, 1));
    EXPECT_FALSE(traffic.cars().empty());
}

TEST(SharedTraffic, EveryPlayersRoadsArePopulatedAndGivenUp) {
    MM2_REQUIRE_GAME_DATA();
    auto city = city::loadCity(*test::gameData(), "london");
    ASSERT_TRUE(city);
    ai::Settings settings;
    settings.trafficDensity = 1.0f;
    settings.pedestrianDensity = 0.0f;
    auto world = ai::World::create(*city, *test::gameData(), settings);
    ASSERT_TRUE(world);
    // Two intersections far apart.
    const auto& nodes = world->network().intersections();
    ASSERT_GT(nodes.size(), 10u);
    std::vector<Vec3> placed; // intersections inside a room (whose roads the traffic fills)
    for (const auto& n : nodes)
        if (world->roomAt(n.centre + Vec3{0, 1, 0}) != 0)
            placed.push_back(n.centre);
    ASSERT_GT(placed.size(), 10u);
    const Vec3 a = placed[placed.size() / 2];
    Vec3 b = a;
    for (const Vec3& n : placed)
        if (n.dist2(a) > b.dist2(a))
            b = n;
    ASSERT_GT(a.dist(b), 1500.0f);
    auto near = [&](const Vec3& p, float r) {
        return std::count_if(world->cars().begin(), world->cars().end(),
                             [&](const ai::AmbientCar& c) { return c.transform.m3.dist(p) < r; });
    };
    world->reset();
    world->step(parkedAt(a + Vec3{0, 1, 0}));
    EXPECT_GT(near(a, 400.0f), 0);
    EXPECT_EQ(near(b, 400.0f), 0);
    // A second player at b: the roads round it fill up as well.
    world->setOtherPlayers({{3, parkedAt(b + Vec3{0, 1, 0})}});
    world->step(parkedAt(a + Vec3{0, 1, 0}));
    EXPECT_GT(near(a, 400.0f), 0);
    EXPECT_GT(near(b, 400.0f), 0);
    // It leaves: its roads are given up, the local player's kept.
    world->setOtherPlayers({});
    world->step(parkedAt(a + Vec3{0, 1, 0}));
    EXPECT_GT(near(a, 400.0f), 0);
    EXPECT_EQ(near(b, 400.0f), 0);
}

TEST(SharedTraffic, ClientLightsFollowTheHostsSteps) {
    MM2_REQUIRE_GAME_DATA();
    auto city = city::loadCity(*test::gameData(), "london");
    ASSERT_TRUE(city);
    ai::Settings settings;
    settings.trafficDensity = 0.0f;
    settings.pedestrianDensity = 0.0f;
    auto host = ai::World::create(*city, *test::gameData(), settings);
    auto client = ai::World::create(*city, *test::gameData(), settings);
    ASSERT_TRUE(host && client);
    ASSERT_FALSE(host->signals().empty());
    host->reset();
    client->reset();
    client->setLightsDeferred(true);
    const ai::PlayerCar player = parkedAt(host->network().intersections().front().centre);
    // The host runs 10 s; the client, which loaded later, has run 3 s of its
    // own clock but follows the host's step count.
    for (int i = 0; i < 300; ++i)
        host->step(player);
    for (int i = 0; i < 90; ++i)
        client->step(player);
    EXPECT_EQ(host->lightSteps(), 300u);
    client->advanceLightsTo(host->lightSteps());
    EXPECT_EQ(client->lightSteps(), 300u);
    for (std::size_t i = 0; i < host->signals().size(); ++i)
        EXPECT_EQ(host->signals()[i].state, client->signals()[i].state) << i;
    // Ahead of the host's count: it waits.
    client->advanceLightsTo(200);
    EXPECT_EQ(client->lightSteps(), 300u);
}
