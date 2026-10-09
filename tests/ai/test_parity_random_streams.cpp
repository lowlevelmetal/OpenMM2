// Round 3 parity checks for MM2's one global random stream (gRandSeed) in the
// AI: aiMap::Init draws for the ambient pool and then the pedestrians from
// the stream as the race's set-up leaves it, aiMap::Reset sets it to 1 and
// populates the traffic and then the pedestrians from it. See
// docs/parity/round3/random-streams.md.
#include "ai/Pedestrians.h"
#include "ai/Random.h"
#include "ai/RoadNetwork.h"
#include "ai/Traffic.h"
#include "ai/TrafficLights.h"
#include "ai/World.h"

#include <gtest/gtest.h>

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

// Two pedestrian types, each with three clothing variants.
std::vector<ai::PedTypeInfo> walkers() {
    std::vector<ai::PedTypeInfo> out;
    for (const char* name : {"pedmodel_a", "pedmodel_b"}) {
        ai::PedTypeInfo t;
        t.name = name;
        t.variants = 3;
        asset::PedAnimState walk;
        walk.name = "WALK";
        walk.animFile = "walk";
        walk.firstFrame = 1;
        walk.lastFrame = 20;
        walk.forwardDistance = 1.4f;
        walk.next = "WALK";
        t.table.states.push_back(walk);
        out.push_back(std::move(t));
    }
    return out;
}

ai::PedSettings pedSettings(int pool) {
    ai::PedSettings s;
    s.pool = pool;
    s.names = {"pedmodel_a", "pedmodel_b"};
    return s;
}

std::uint32_t advanced(std::uint32_t state, int draws) {
    ai::Random r(state);
    r.discard(draws);
    return r.state();
}

const ai::PlayerCar kFarPlayer = ai::PlayerCar::at({5000, 0, 5000}, {});

} // namespace

TEST(ParityRandomStreams, GeneratorIsMsvcRand) {
    // irand from seed 1: 41, 18467, 6334 (MSVC rand()); frand = irand / 32768.
    ai::Random r(1);
    EXPECT_EQ(r.irand(), 41);
    EXPECT_EQ(r.irand(), 18467);
    EXPECT_FLOAT_EQ(r.frand(), 6334.0f / 32768.0f);
    ai::Random a(1), b(1);
    a.discard(3);
    for (int i = 0; i < 3; ++i)
        b.irand();
    EXPECT_EQ(a.state(), b.state());
}

// aiMap::Init on one stream: the ambient pool's constructors (two draws a
// car), each car's type, SetColor twice and aiGoalRandomDrive (four a car),
// then each pedestrian's type and aiPedestrian::Init's variant (two each),
// from where the traffic left the stream.
TEST(ParityRandomStreams, AiMapInitDrawsTrafficThenPedestrians) {
    const auto map = square();
    const auto net = ai::RoadNetwork::build(map, {});
    ai::TrafficLights lights;
    lights.build(net);
    ai::TrafficSettings traffic;
    traffic.poolSize = 9;
    constexpr std::uint32_t kStart = 0x1234567u;
    ai::Random stream(kStart);
    ai::Traffic cars(net, lights, {sedan()}, traffic, stream);
    EXPECT_EQ(stream.state(), advanced(kStart, 6 * traffic.poolSize));
    // The paint of car i is the third of its four draws.
    ai::Random expected(advanced(kStart, 2 * traffic.poolSize));
    std::vector<float> paints;
    for (int i = 0; i < traffic.poolSize; ++i) {
        expected.discard(2);
        paints.push_back(expected.frand());
        expected.discard(1);
    }
    cars.populateAll();
    cars.step(ai::kAiStepSeconds, kFarPlayer, 1);
    ASSERT_FALSE(cars.cars().empty());
    for (const auto& c : cars.cars())
        EXPECT_FLOAT_EQ(c.paint, paints[static_cast<std::size_t>(c.id)]);

    constexpr int kPeds = 6;
    ai::Random pedStream(advanced(kStart, 6 * traffic.poolSize));
    ai::Pedestrians peds(net, walkers(), pedSettings(kPeds), pedStream);
    EXPECT_EQ(pedStream.state(), advanced(kStart, 6 * traffic.poolSize + 2 * kPeds));
    ai::Random pick(advanced(kStart, 6 * traffic.poolSize));
    std::vector<std::pair<int, int>> types; // (type, variant)
    for (int i = 0; i < kPeds; ++i) {
        const int type = static_cast<int>(pick.frand() * 2.0f);
        const int variant = static_cast<int>(pick.frand() * 2.0f); // variants - 1
        types.emplace_back(type, variant);
    }
    peds.populateAll();
    peds.step(ai::kAiStepSeconds, kFarPlayer, 1);
    ASSERT_EQ(peds.peds().size(), static_cast<std::size_t>(kPeds));
    for (const auto& p : peds.peds()) {
        EXPECT_EQ(p.type, types[static_cast<std::size_t>(p.id)].first) << p.id;
        EXPECT_EQ(p.variant, types[static_cast<std::size_t>(p.id)].second) << p.id;
    }
}

// MM2 builds the ambient array of the state's vehicle count before it looks
// at the density: at density 0 there are no cars, but the constructors still
// drew (two each).
TEST(ParityRandomStreams, NoTrafficStillDrawsForThePool) {
    const auto map = square();
    const auto net = ai::RoadNetwork::build(map, {});
    ai::TrafficLights lights;
    lights.build(net);
    ai::TrafficSettings traffic;
    traffic.density = 0.0f;
    traffic.poolSize = 300;
    ai::Random stream(5);
    ai::Traffic cars(net, lights, {sedan()}, traffic, stream);
    EXPECT_EQ(stream.state(), advanced(5, 2 * 300));
    cars.step(ai::kAiStepSeconds, kFarPlayer, 1);
    EXPECT_TRUE(cars.cars().empty());
}

// aiMap::Reset: the seed back to 1, then AdjustAmbients and
// AdjustPedestrians round the player, before anything updates. On one stream
// the pedestrians are placed from where the traffic's placing left it.
TEST(ParityRandomStreams, ResetPlacesTrafficThenPedestriansFromSeedOne) {
    const auto map = square();
    const auto net = ai::RoadNetwork::build(map, {});
    ai::TrafficLights lights;
    lights.build(net);
    ai::TrafficSettings traffic;
    traffic.poolSize = 20;
    constexpr int kPeds = 8;

    ai::Random stream(0xABCDEFu);
    ai::Traffic cars(net, lights, {sedan()}, traffic, stream);
    ai::Pedestrians peds(net, walkers(), pedSettings(kPeds), stream);
    stream.seed(1); // aiMap::Reset's ResetRandomSeed
    cars.reset();
    peds.reset();
    ASSERT_TRUE(cars.populate(1));
    const std::uint32_t afterTraffic = stream.state();
    ASSERT_TRUE(peds.populate(1));
    EXPECT_FALSE(cars.populate(1)); // once per reset

    // The same traffic without pedestrians is placed alike: the
    // pedestrians' draws all come after the traffic's.
    ai::Random aloneStream(0xABCDEFu);
    ai::Traffic alone(net, lights, {sedan()}, traffic, aloneStream);
    aloneStream.seed(1);
    alone.reset();
    alone.step(0.0f, kFarPlayer, 1);
    cars.step(0.0f, kFarPlayer, 1);
    ASSERT_EQ(alone.cars().size(), cars.cars().size());
    ASSERT_FALSE(cars.cars().empty());
    for (std::size_t i = 0; i < cars.cars().size(); ++i) {
        EXPECT_EQ(alone.cars()[i].id, cars.cars()[i].id);
        EXPECT_EQ(alone.cars()[i].transform.m3, cars.cars()[i].transform.m3);
    }
    // The pedestrians on a stream of their own from that state are placed
    // where the shared ones were.
    ai::Pedestrians pedsAlone(net, walkers(), pedSettings(kPeds), afterTraffic);
    pedsAlone.reset();
    pedsAlone.step(0.0f, kFarPlayer, 1);
    peds.step(0.0f, kFarPlayer, 1);
    ASSERT_EQ(pedsAlone.peds().size(), peds.peds().size());
    ASSERT_FALSE(peds.peds().empty());
    for (std::size_t i = 0; i < peds.peds().size(); ++i) {
        EXPECT_EQ(pedsAlone.peds()[i].id, peds.peds()[i].id);
        EXPECT_EQ(pedsAlone.peds()[i].transform.m3, peds.peds()[i].transform.m3);
        EXPECT_TRUE(peds.peds()[i].placed);
    }
    // A shared stream is its owner's to seed: reset() leaves it alone.
    stream.seed(77);
    cars.reset();
    peds.reset();
    EXPECT_EQ(stream.state(), 77u);
}
