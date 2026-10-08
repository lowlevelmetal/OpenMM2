// Parity checks for the AI from MM2's side (build 3393, MM2Recomp): what
// aiMap::Reset does on a restart, the ambient cars' set-up draws and the
// lights aiVehicleInstance::DrawGlow shows. See docs/parity/mm2/ai.md.
#include "ai/Pedestrians.h"
#include "ai/Random.h"
#include "ai/RoadNetwork.h"
#include "ai/Traffic.h"
#include "ai/TrafficLights.h"
#include "ai/World.h"

#include <gtest/gtest.h>

#include <map>
#include <vector>

using namespace mm2;

namespace {

// A straight two-way road from `a` to `b` with one lane per side and a
// sidewalk on each side, flat (path flag 0x8).
city::AiPath road(int id, Vec3 a, Vec3 b, std::uint32_t nodeA, std::uint32_t nodeB) {
    constexpr int kSections = 4;
    city::AiPath p;
    p.id = static_cast<std::uint16_t>(id);
    p.flags = 0x8;
    p.halfWidth = 7.0f;
    const Vec3 d = (b - a).normalized();
    const Vec3 left{d.z, 0.0f, -d.x};
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
        for (float offset : {2.0f, 5.5f, 4.0f, 7.0f}) { // lane, sidewalk, curb, edge
            std::vector<Vec3> line;
            for (int i = 0; i < kSections; ++i) {
                const int k = offset == 2.0f && reversed ? kSections - 1 - i : i;
                line.push_back(p.center[static_cast<std::size_t>(k)] + left * (sign * offset));
            }
            s.polylines.push_back(line);
        }
        s.params = {-4.0f, 0.0f, 4.0f, 7.0f};
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

// A square block, 200 m a side; room 1 lists all four roads.
city::AiMap square() {
    city::AiMap map;
    const Vec3 corner[4] = {{0, 0, 0}, {0, 0, -200}, {200, 0, -200}, {200, 0, 0}};
    for (int k = 0; k < 4; ++k)
        map.paths.push_back(road(k, corner[k], corner[(k + 1) % 4], static_cast<std::uint32_t>(k),
                                 static_cast<std::uint32_t>((k + 1) % 4)));
    for (int k = 0; k < 4; ++k)
        map.intersections.push_back(
            {static_cast<std::uint16_t>(k), 0, corner[k],
             {static_cast<std::uint32_t>((k + 3) % 4), static_cast<std::uint32_t>(k)}});
    for (auto& node : map.intersections)
        for (std::size_t k = 0; k < node.paths.size(); ++k)
            for (auto& e : map.paths[node.paths[k]].ends)
                if (e.intersection == node.id)
                    e.roadIndex = static_cast<std::uint16_t>(k);
    map.roomPathsNear = {{}, {0, 1, 2, 3}};
    map.roomPathsIn = {{}, {0, 1, 2, 3}};
    return map;
}

ai::VehicleData sedan() {
    ai::VehicleData d;
    d.model = "test";
    d.size = {2.0f, 1.5f, 4.5f};
    return d;
}

std::vector<ai::PedTypeInfo> walker() {
    ai::PedTypeInfo t;
    t.name = "pedmodel_test";
    asset::PedAnimState walk;
    walk.name = "WALK";
    walk.animFile = "walk";
    walk.firstFrame = 1;
    walk.lastFrame = 20;
    walk.forwardDistance = 1.4f;
    walk.next = "WALK";
    t.table.states.push_back(walk);
    return {t};
}

struct Snapshot {
    int id;
    Vec3 position;
    float speed;
};

std::vector<Snapshot> snapshot(const std::vector<ai::AmbientCar>& cars) {
    std::vector<Snapshot> out;
    for (const auto& c : cars)
        out.push_back({c.id, c.transform.m3, c.speed});
    return out;
}

void expectSame(const std::vector<Snapshot>& a, const std::vector<Snapshot>& b) {
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].id, b[i].id);
        EXPECT_FLOAT_EQ(a[i].position.x, b[i].position.x);
        EXPECT_FLOAT_EQ(a[i].position.z, b[i].position.z);
        EXPECT_FLOAT_EQ(a[i].speed, b[i].speed);
    }
}

const ai::PlayerCar kFarPlayer = ai::PlayerCar::at({5000, 0, 5000}, {});

} // namespace

// aiMap::Reset (a race restart): the random seed back to 1, every road and
// car cleared, the pool refilled in index order, and the roads round the
// player populated again: the traffic starts over exactly as it began.
TEST(ParityMm2Ai, TrafficRestartsAsItBegan) {
    const auto map = square();
    const auto net = ai::RoadNetwork::build(map, {});
    ai::TrafficLights lights;
    lights.build(net);
    ai::TrafficSettings settings;
    ai::Traffic traffic(net, lights, {sedan()}, settings, 1);
    traffic.reset(); // mmGame::Init: aiMap::Reset after aiMap::Init's draws
    traffic.step(ai::kAiStepSeconds, kFarPlayer, 1);
    const auto first = snapshot(traffic.cars());
    ASSERT_FALSE(first.empty());
    for (int i = 0; i < 30 * 20; ++i)
        traffic.step(ai::kAiStepSeconds, kFarPlayer, 1);
    traffic.reset();
    EXPECT_TRUE(traffic.cars().empty());
    EXPECT_EQ(traffic.poolFree(), ai::kAmbientPoolSize);
    traffic.step(ai::kAiStepSeconds, kFarPlayer, 1);
    expectSame(first, snapshot(traffic.cars()));
}

// aiMap::Init's draws per ambient car, after the rail set and spline
// constructors of the whole array (two each): the type (frand), the
// aiVehicleInstance's SetColor (frand), Init's SetColor again (frand, the
// paint kept) and aiGoalRandomDrive's acceleration and separation (frand).
// The instance's blink number is irand(int) of its address, no draw.
TEST(ParityMm2Ai, AmbientCarSetUpDraws) {
    const auto map = square();
    const auto net = ai::RoadNetwork::build(map, {});
    ai::TrafficLights lights;
    lights.build(net);
    ai::TrafficSettings settings;
    settings.poolSize = 12;
    ai::Traffic traffic(net, lights, {sedan()}, settings, 7);
    traffic.populateAll();
    traffic.step(ai::kAiStepSeconds, kFarPlayer, 1);
    ai::Random r(7);
    for (int i = 0; i < 2 * settings.poolSize; ++i)
        r.irand();
    std::map<int, std::pair<int, float>> expected; // id -> (phase, paint)
    for (int i = 0; i < settings.poolSize; ++i) {
        r.frand();
        r.frand();
        const float paint = r.frand();
        r.frand();
        const auto key = static_cast<std::uint32_t>(i) * 0x40u;
        expected[i] = {static_cast<int>((key * 214013u + 2531011u) >> 16 & 0x7FFFu), paint};
    }
    ASSERT_FALSE(traffic.cars().empty());
    for (const auto& c : traffic.cars()) {
        EXPECT_EQ(c.blinkPhase, expected[c.id].first);
        EXPECT_FLOAT_EQ(c.paint, expected[c.id].second);
    }
}

// aiVehicleInstance::DrawGlow lights the tail lights of a car that slows
// down or stands (acceleration below 0 or speed 0): every car placed this
// step stands.
TEST(ParityMm2Ai, StandingCarsShowTheirTailLights) {
    const auto map = square();
    const auto net = ai::RoadNetwork::build(map, {});
    ai::TrafficLights lights;
    lights.build(net);
    ai::TrafficSettings settings;
    ai::Traffic traffic(net, lights, {sedan()}, settings, 1);
    traffic.step(0.0f, kFarPlayer, 1);
    ASSERT_FALSE(traffic.cars().empty());
    for (const auto& c : traffic.cars()) {
        if (c.speed == 0.0f) {
            EXPECT_TRUE(c.braking);
        }
    }
    // Once they drive at a steady speed the lights go out.
    for (int i = 0; i < 30 * 20; ++i)
        traffic.step(ai::kAiStepSeconds, kFarPlayer, 1);
    int cruising = 0;
    for (const auto& c : traffic.cars())
        if (c.speed > 1.0f && !c.braking)
            ++cruising;
    EXPECT_GT(cruising, 0);
}

// aiMap::Reset for the pedestrians: back in the pool in index order, the
// seed reset, the roads round the player dealt again as at the start (the
// start being mmGame::Init's own aiMap::Reset).
TEST(ParityMm2Ai, PedestriansRestartAsTheyBegan) {
    const auto map = square();
    const auto net = ai::RoadNetwork::build(map, {});
    ai::PedSettings settings;
    settings.pool = 10;
    ai::Pedestrians peds(net, walker(), settings, 1);
    peds.reset(); // mmGame::Init: aiMap::Reset after aiMap::Init's draws
    peds.step(1.0f / 30.0f, kFarPlayer, 1);
    std::vector<std::pair<int, Vec3>> first;
    for (const auto& p : peds.peds())
        first.emplace_back(p.id, p.transform.m3);
    ASSERT_EQ(first.size(), 10u);
    for (int i = 0; i < 30 * 10; ++i)
        peds.step(1.0f / 30.0f, kFarPlayer, 1);
    peds.reset();
    EXPECT_EQ(peds.activeCount(), 0u);
    peds.step(1.0f / 30.0f, kFarPlayer, 1);
    ASSERT_EQ(peds.peds().size(), first.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        EXPECT_EQ(peds.peds()[i].id, first[i].first);
        EXPECT_FLOAT_EQ(peds.peds()[i].transform.m3.x, first[i].second.x);
        EXPECT_FLOAT_EQ(peds.peds()[i].transform.m3.z, first[i].second.z);
    }
}

// aiGoalAvoidPlayer::Update: off its rail, the car's rail distance comes
// from where it is (aiMap::DetermineRoadPosInfo, aiPath::RoadDistance on
// its road): on a straight road, its distance along its lane.
TEST(ParityMm2Ai, AvoidingCarKeepsItsRoadDistance) {
    const auto map = square();
    const auto net = ai::RoadNetwork::build(map, {});
    ai::TrafficLights lights;
    lights.build(net);
    ai::TrafficSettings settings;
    ai::Traffic traffic(net, lights, {sedan()}, settings, 1);
    traffic.reset();
    for (int i = 0; i < 30 * 4; ++i)
        traffic.step(ai::kAiStepSeconds, kFarPlayer, 1);
    // A car well inside a road, moving.
    int id = -1;
    for (const auto& c : traffic.cars()) {
        const auto d = traffic.debug(c.id);
        if (c.speed > 5.0f && !d.turning && !d.changingLane && d.s > 40.0f && d.s < 120.0f) {
            id = c.id;
            break;
        }
    }
    ASSERT_GE(id, 0);
    const ai::AmbientCar* car = nullptr;
    for (const auto& c : traffic.cars())
        if (c.id == id)
            car = &c;
    ASSERT_NE(car, nullptr);
    const Vec3 ahead = car->transform.m3 - car->transform.m2 * 15.0f;
    const ai::PlayerCar player = ai::PlayerCar::at(ahead, {});
    bool avoided = false;
    for (int i = 0; i < 30 && !avoided; ++i) {
        traffic.step(ai::kAiStepSeconds, player, 1);
        const auto d = traffic.debug(id);
        if (d.goal == ai::AmbientGoal::AvoidPlayer) {
            avoided = true;
            for (const auto& c : traffic.cars()) {
                if (c.id != id)
                    continue;
                const auto& lane = net.lanes()[static_cast<std::size_t>(d.lane)];
                EXPECT_NEAR(d.s, lane.line.project(c.transform.m3), 0.5f);
            }
        }
    }
    EXPECT_TRUE(avoided);
}
