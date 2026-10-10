// Ambient traffic on MM2's collision (aiVehicleInstance, aiVehicleManager,
// aiVehicleActive, vehWheelCheap, phSleep) through phys::World.
#include "TestData.h"
#include "ai/World.h"
#include "city/CityData.h"
#include "game/PlayerVehicle.h"
#include "game/TrafficBodies.h"
#include "phys/World.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <vector>

using namespace mm2;

namespace {

constexpr float kFrame = 1.0f / 60.0f;

// A one-room level: a flat floor (what sdlPage16::Collect would hand lvlSDL)
// and the instances of a source (the traffic's rail cars).
class FloorLevel final : public phys::Level {
public:
    FloorLevel(const Vec3& centre, float half) {
        const float y = centre.y;
        m_corners = {Vec3{centre.x - half, y, centre.z - half}, Vec3{centre.x - half, y, centre.z + half},
                     Vec3{centre.x + half, y, centre.z + half}, Vec3{centre.x + half, y, centre.z - half}};
    }

    int findRoom(const Vec3&, int) const override { return 1; }
    int touchedNeighbors(int*, int, int, const Vec3&, float) const override { return 0; }
    void collect(const int*, int, const Vec3&, float, phys::LevelBound& out) const override {
        out.clear();
        out.addPolygon(m_corners.data(), 4, {0.0f, 1.0f, 0.0f}, 0);
    }
    void instances(int room, std::vector<phys::Instance*>& out) const override {
        if (source)
            source->instancesIn(room, out);
    }

    // The floor as probe geometry (the wheels).
    phys::PolygonSoup soup(const phys::MaterialTable& materials) const {
        phys::SoupGeometry g;
        g.vertices.assign(m_corners.begin(), m_corners.end());
        phys::SoupGeometry::Poly p;
        p.v = {0, 1, 2, 3};
        p.count = 4;
        g.polys.push_back(p);
        g.materialNames.push_back(materials[0].name);
        phys::PolygonSoup s;
        s.add(g, Mat34::identity(), materials);
        s.finalize(64.0f);
        return s;
    }

    const game::InstanceSource* source = nullptr;

private:
    std::array<Vec3, 4> m_corners;
};

const ai::AmbientCar* findCar(const ai::World& ai, int id) {
    for (const auto& c : ai.cars())
        if (c.id == id)
            return &c;
    return nullptr;
}

// Where londonTraffic() populates the roads.
const Vec3 kLondonSpot{430, 0, -150};

// London's traffic after two seconds around a spot on the surface.
std::unique_ptr<ai::World> londonTraffic(const vfs::Vfs& vfs, const city::CityData& city) {
    ai::Settings settings;
    settings.trafficDensity = 1.0f;
    settings.pedestrianDensity = 0.0f;
    auto ai = ai::World::create(city, vfs, settings);
    if (!ai)
        return nullptr;
    for (int i = 0; i < 60; ++i)
        ai->update(1.0f / 30.0f, kLondonSpot, {});
    return ai;
}

// The slowest level car on the surface, preferably with no other car within
// 12 m of it or on the `runUp` metres of its lane behind it.
const ai::AmbientCar* pickTarget(const ai::World& ai, float runUp) {
    const ai::AmbientCar* best = nullptr;
    bool bestClear = false;
    for (const auto& c : ai.cars()) {
        if (!c.data || std::abs(c.transform.m3.y) > 1.0f || c.transform.m1.y < 0.99f)
            continue;
        const bool clear = std::ranges::none_of(ai.cars(), [&](const ai::AmbientCar& o) {
            if (o.id == c.id)
                return false;
            const Vec3 d = o.transform.m3 - c.transform.m3;
            const float behind = d.dot(c.transform.m2);
            return d.mag2() < 12.0f * 12.0f ||
                   (behind > 0.0f && behind < runUp && std::abs(d.dot(c.transform.m0)) < 4.0f);
        });
        if (!best || (clear && !bestClear) || (clear == bestClear && c.speed < best->speed)) {
            best = &c;
            bestClear = clear;
        }
    }
    return best;
}

struct Scene {
    std::unique_ptr<FloorLevel> level;
    phys::MaterialTable materials;
    std::unique_ptr<phys::World> world;
    std::unique_ptr<game::TrafficBodies> traffic;

    Scene(ai::World& ai, const Vec3& floorCentre) {
        level = std::make_unique<FloorLevel>(floorCentre, 400.0f);
        world = std::make_unique<phys::World>(materials);
        world->setLevel(level.get());
        world->setStatic(level->soup(materials));
        traffic = std::make_unique<game::TrafficBodies>(ai, *world);
        level->source = traffic.get();
    }
    // A frame as RaceScreen runs it (the AI first, when given).
    void frame(ai::World* ai = nullptr, const Vec3& focus = {}) {
        if (ai)
            ai->update(kFrame, focus, {});
        traffic->beforeStep();
        world->advanceFixed(kFrame);
        traffic->afterStep();
    }
};

} // namespace

// An opponent at 11 m/s runs into the back of a car on its rail: the car
// leaves its rail as a rigid body that takes the impact, the opponent is not
// thrown into the air, and once the car has come to rest it goes back to the
// AI, which drives it back to its lane.
TEST(TrafficBodies, HitCarLeavesItsRailAndGoesBackToTheAi) {
    MM2_REQUIRE_GAME_DATA();
    const vfs::Vfs& vfs = *test::gameData();
    auto city = city::loadCity(vfs, "london");
    ASSERT_TRUE(city);
    auto ai = londonTraffic(vfs, *city);
    ASSERT_TRUE(ai);
    constexpr float kRunUp = 45.0f;
    const ai::AmbientCar* target = pickTarget(*ai, kRunUp);
    ASSERT_TRUE(target) << "no car standing level on the surface";
    const int id = target->id;
    const Mat34 frame = target->transform;
    const Vec3 forward = -frame.m2;
    const float length = target->data->size.z;

    Scene scene(*ai, frame.m3);
    int impacts = 0;
    float strongest = 0.0f;
    scene.traffic->setImpactCallback([&](const game::TrafficImpact& e) {
        if (e.carId != id)
            return;
        ++impacts;
        strongest = std::max(strongest, e.strength);
    });

    // The opponent (an AI car: a box bound) accelerates along the lane and
    // coasts into the car's back at 11 m/s. The AI holds still meanwhile:
    // the car waits on its rail.
    std::string error;
    auto opponent = game::SimVehicle::load(vfs, "vpbug", &error);
    ASSERT_TRUE(opponent) << error;
    opponent->addTo(*scene.world);
    Mat34 start = frame;
    start.m3 = frame.m3 - forward * (length * 0.5f + kRunUp);
    opponent->reset(start);
    float rise = 0.0f;
    float hitSpeed = 0.0f;
    bool attached = false;
    for (int i = 0; i < 60 * 15 && !attached; ++i) {
        hitSpeed = opponent->sim().body.ics.linearVelocity.dot(forward);
        opponent->drive({hitSpeed < 11.0f ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f});
        scene.frame();
        rise = std::max(rise, opponent->sim().modelMatrix().m3.y - frame.m3.y);
        attached = scene.traffic->transformOf(id) != nullptr;
    }
    ASSERT_TRUE(attached) << "the hit car did not leave its rail";
    EXPECT_GT(hitSpeed, 10.0f);
    EXPECT_EQ(scene.traffic->activeCount(), 1u);
    const ai::AmbientCar* hit = findCar(*ai, id);
    ASSERT_TRUE(hit);
    EXPECT_TRUE(hit->physical);
    EXPECT_EQ(hit->goal, ai::AmbientGoal::Collision);
    for (int i = 0; i < 60; ++i) {
        opponent->drive({});
        scene.frame();
        rise = std::max(rise, opponent->sim().modelMatrix().m3.y - frame.m3.y);
    }
    EXPECT_GT(impacts, 0) << "aiVehicleActive::Impact reports the hit";
    EXPECT_GT(strongest, 100.0f);
    const Mat34* moved = scene.traffic->transformOf(id);
    ASSERT_TRUE(moved);
    EXPECT_GT((moved->m3 - frame.m3).dot(forward), 1.0f) << "the hit pushes the car forward";
    EXPECT_LT(std::abs(moved->m3.y - frame.m3.y), 0.3f) << "the car stays on its wheels";
    EXPECT_GT(moved->m1.y, 0.95f);
    EXPECT_LT(rise, 0.5f) << "the opponent must not be thrown upwards";

    // Asleep (phSleep: 15 still samples) it goes back to the AI upright,
    // which drives it back onto its lane (aiVehicleAmbient::Impact(0)).
    bool handedBack = false;
    for (int i = 0; i < 60 * 20 && !handedBack; ++i) {
        opponent->drive({0.0f, 1.0f, 0.0f, 0.0f});
        scene.frame(ai.get(), kLondonSpot);
        const ai::AmbientCar* c = findCar(*ai, id);
        ASSERT_TRUE(c);
        handedBack = !c->physical;
    }
    ASSERT_TRUE(handedBack) << "the car should come to rest and go back to the AI";
    const ai::AmbientCar* back = findCar(*ai, id);
    EXPECT_FALSE(back->wreck) << "it stopped upright";
    EXPECT_NE(back->goal, ai::AmbientGoal::Collision);
    scene.frame(ai.get(), kLondonSpot);
    EXPECT_EQ(scene.traffic->transformOf(id), nullptr) << "the AI's transform is current again";
}

// aiVehicleManager has 32 actives: the 33rd car hit takes the body of the
// first in its list, which goes back to the AI first.
TEST(TrafficBodies, ThirtyThirdCarTakesTheFirstBody) {
    MM2_REQUIRE_GAME_DATA();
    const vfs::Vfs& vfs = *test::gameData();
    auto city = city::loadCity(vfs, "london");
    ASSERT_TRUE(city);
    auto ai = londonTraffic(vfs, *city);
    ASSERT_TRUE(ai);
    Scene scene(*ai, kLondonSpot);
    scene.traffic->beforeStep();
    std::vector<phys::Instance*> rail;
    scene.traffic->instancesIn(1, rail);
    ASSERT_GE(rail.size(), 33u);

    const auto physicalIds = [&] {
        std::set<int> ids;
        for (const auto& c : ai->cars())
            if (c.physical)
                ids.insert(c.id);
        return ids;
    };
    ASSERT_TRUE(physicalIds().empty());
    // World::collideInstances does this for an instance it hits.
    phys::Body* first = rail[0]->attachEntity();
    ASSERT_TRUE(first);
    scene.world->addNewMover(first);
    const std::set<int> firstIds = physicalIds();
    ASSERT_EQ(firstIds.size(), 1u);
    const int firstId = *firstIds.begin();
    EXPECT_EQ(rail[0]->entity(), first);
    EXPECT_FALSE(rail[0]->collidable);
    EXPECT_EQ(rail[0]->attachEntity(), first) << "a car with a body keeps it";
    for (std::size_t i = 1; i < 32; ++i)
        scene.world->addNewMover(rail[i]->attachEntity());
    EXPECT_EQ(scene.traffic->activeCount(), 32u);
    EXPECT_EQ(physicalIds().size(), 32u);
    std::vector<phys::Instance*> listed;
    scene.traffic->instancesIn(1, listed);
    EXPECT_EQ(listed.size(), rail.size() - 32) << "cars with a body are not listed in their room";

    phys::Body* reused = rail[32]->attachEntity();
    scene.world->addNewMover(reused);
    EXPECT_EQ(reused, first);
    EXPECT_EQ(scene.traffic->activeCount(), 32u);
    EXPECT_EQ(rail[0]->entity(), nullptr);
    EXPECT_TRUE(rail[0]->collidable) << "back on its rail";
    const ai::AmbientCar* firstCar = findCar(*ai, firstId);
    ASSERT_TRUE(firstCar);
    EXPECT_FALSE(firstCar->physical);
    // Listed in its room again from the next frame.
    scene.traffic->beforeStep();
    EXPECT_EQ(scene.traffic->transformOf(firstId), nullptr);
    listed.clear();
    scene.traffic->instancesIn(1, listed);
    EXPECT_NE(std::ranges::find(listed, rail[0]), listed.end());
}

// OpenMM2 (a network client running its car's samples again): the cars on
// their rails near a place, and those put elsewhere for a while, their room
// lists following, then back.
TEST(TrafficBodies, RailCarsStandElsewhereForAReplayAndComeBack) {
    MM2_REQUIRE_GAME_DATA();
    const vfs::Vfs& vfs = *test::gameData();
    auto city = city::loadCity(vfs, "london");
    ASSERT_TRUE(city);
    auto ai = londonTraffic(vfs, *city);
    ASSERT_TRUE(ai);
    const ai::AmbientCar* target = pickTarget(*ai, 0.0f);
    ASSERT_TRUE(target);
    const int id = target->id;
    Scene scene(*ai, target->transform.m3);
    scene.traffic->beforeStep();
    const auto near = scene.traffic->railPoses(target->transform.m3, 30.0f);
    const auto it = std::ranges::find(near, id, &game::TrafficBodies::RailPose::id);
    ASSERT_NE(it, near.end());
    EXPECT_EQ(it->transform.m3, target->transform.m3);
    for (const auto& r : near)
        EXPECT_LE(r.transform.m3.dist(target->transform.m3), 30.0f);
    // Moved 3 m along its lane for a sample run again, then back.
    game::TrafficBodies::RailPose moved = *it;
    moved.transform.m3 = moved.transform.m3 - moved.transform.m2 * 3.0f;
    scene.traffic->placeRailCars(std::span(&moved, 1));
    const auto there = [&](const Vec3& at) {
        std::vector<phys::Instance*> list;
        scene.traffic->instancesIn(1, list);
        return std::ranges::any_of(list, [&](const phys::Instance* i) { return i->matrix().m3 == at; });
    };
    EXPECT_TRUE(there(moved.transform.m3));
    scene.traffic->placeRailCars(near);
    EXPECT_TRUE(there(target->transform.m3));
    EXPECT_FALSE(there(moved.transform.m3));
    // A car the AI no longer lists is left alone.
    game::TrafficBodies::RailPose unknown{100000, moved.transform};
    scene.traffic->placeRailCars(std::span(&unknown, 1));
    EXPECT_FALSE(there(moved.transform.m3));
}
