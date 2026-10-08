// Parity checks for the ambient city AI against MM2 (build 3393): the
// random number generator and the pedestrian population rules
// (aiMap::AdjustPedestrians).
#include "ai/Pedestrians.h"
#include "ai/Random.h"
#include "ai/RoadNetwork.h"

#include <gtest/gtest.h>

using namespace mm2;

namespace {

// A straight two-way road from `a` to `b`, one lane per side, with a sidewalk
// on each side; `pedFlags` per side (bit 1 closes it to pedestrians).
city::AiPath road(int id, Vec3 a, Vec3 b, std::uint32_t nodeA, std::uint32_t nodeB, std::uint16_t leftFlags,
                  std::uint16_t rightFlags) {
    constexpr int kSections = 4;
    city::AiPath p;
    p.id = static_cast<std::uint16_t>(id);
    const Vec3 d = (b - a).normalized();
    const Vec3 left{d.z, 0.0f, -d.x};
    for (int i = 0; i < kSections; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSections - 1);
        p.center.push_back(lerp(a, b, t));
        p.xAxis.push_back(left);
        p.yAxis.push_back(Vec3::yAxis());
        p.zAxis.push_back(-d);
        p.wAxis.push_back(d);
    }
    auto side = [&](float sign, std::uint16_t flags) {
        city::AiRoadSide s;
        s.numLanes = 1;
        s.numSidewalks = 1;
        s.roadType = flags;
        for (float offset : {2.0f, 5.5f, 4.0f, 7.0f}) { // lane, sidewalk, curb, edge
            std::vector<Vec3> line;
            for (const Vec3& c : p.center)
                line.push_back(c + left * (sign * offset));
            s.polylines.push_back(line);
        }
        s.params = {-4.0f, 0.0f, 4.0f, 7.0f};
        return s;
    };
    p.left = side(1.0f, leftFlags);
    p.right = side(-1.0f, rightFlags);
    p.ends[0].intersection = nodeB;
    p.ends[1].intersection = nodeA;
    p.ends[0].vehicleRule = 3;
    p.ends[1].vehicleRule = 3;
    return p;
}

// Three parallel roads between their own pairs of intersections; room 1
// lists all three for pedestrians.
city::AiMap threeRoads(std::uint16_t flags1, std::uint16_t flags2) {
    city::AiMap map;
    for (int k = 0; k < 3; ++k) {
        const float x = 100.0f * static_cast<float>(k);
        const std::uint16_t f = k == 0 ? 0 : (k == 1 ? flags1 : flags2);
        map.paths.push_back(road(k, {x, 0, 0}, {x, 0, -90}, static_cast<std::uint32_t>(2 * k),
                                 static_cast<std::uint32_t>(2 * k + 1), f, f));
        map.intersections.push_back({static_cast<std::uint16_t>(2 * k), 0, {x, 0, 0}, {static_cast<std::uint32_t>(k)}});
        map.intersections.push_back(
            {static_cast<std::uint16_t>(2 * k + 1), 0, {x, 0, -90}, {static_cast<std::uint32_t>(k)}});
    }
    map.roomPathsNear = {{}, {0, 1, 2}};
    map.roomPathsIn = {{}, {0, 1, 2}};
    return map;
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

std::size_t populate(const city::AiMap& map, int pool) {
    const auto net = ai::RoadNetwork::build(map, {});
    ai::PedSettings settings;
    settings.pool = pool;
    ai::Pedestrians peds(net, walker(), settings, 1);
    ai::PlayerCar far = ai::PlayerCar::at({5000, 0, 5000}, {});
    peds.step(1.0f / 30.0f, far, 1);
    return peds.activeCount();
}

} // namespace

// irand/frand: the MSVC rand() generator, 15 bits, frand = irand / 32768.
TEST(ParityAmbientCity, RandomIsMm2Irand) {
    ai::Random r(1); // aiMap::Reset's ResetRandomSeed
    EXPECT_EQ(r.irand(), 41);
    EXPECT_EQ(r.irand(), 18467);
    EXPECT_EQ(r.irand(), 6334);
    EXPECT_EQ(r.irand(), 26500);
    ai::Random f(1);
    EXPECT_FLOAT_EQ(f.frand(), 41.0f / 32768.0f);
}

// aiMap::AdjustPedestrians deals the pool round the newly listed roads, one
// pedestrian per open sidewalk side, and stops when the turn comes back to
// the road where it last placed one: with a single open road that is one
// lap, whatever the pool.
TEST(ParityAmbientCity, PedestriansDealtUntilTheTurnComesBack) {
    // Roads 1 and 2 closed to pedestrians on both sides: two on road 0.
    EXPECT_EQ(populate(threeRoads(2, 2), 10), 2u);
    // Two open roads: the whole pool.
    EXPECT_EQ(populate(threeRoads(0, 2), 10), 10u);
    // A pool smaller than a lap.
    EXPECT_EQ(populate(threeRoads(0, 0), 3), 3u);
}
