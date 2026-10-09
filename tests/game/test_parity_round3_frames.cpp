// Parity round 3, frames: every model, part, glow and shadow is drawn in the
// frame MM2 draws it in (see docs/parity/round3/frames.md).
#include "TestData.h"
#include "ai/World.h"
#include "city/CityData.h"
#include "game/TrafficBodies.h"
#include "game/VehicleRenderer.h"
#include "phys/World.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <memory>
#include <vector>

using namespace mm2;

namespace {

constexpr float kFrame = 1.0f / 60.0f;

// A one-room level with a flat floor, listing the traffic's rail cars.
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

void expectSameMatrix(const Mat34& a, const Mat34& b, const char* what) {
    for (int r = 0; r < 4; ++r) {
        EXPECT_FLOAT_EQ(a.row(r).x, b.row(r).x) << what << " row " << r;
        EXPECT_FLOAT_EQ(a.row(r).y, b.row(r).y) << what << " row " << r;
        EXPECT_FLOAT_EQ(a.row(r).z, b.row(r).z) << what << " row " << r;
    }
}

} // namespace

// aiVehicleInstance::Draw draws a traffic car that has a body
// (aiVehicleActive) with its wheels at the vehWheelCheaps' drawing matrices:
// unturned, at the pivot until the first update, then at the pivot moved by
// the spring's travel along the car's up axis (-Limit off the ground) and
// back by 0.2 / 0.3 of the tyre deflections; WHL4 / WHL5 unturned at their
// pivots raised by WHL2's / WHL3's drawn height less the wheel radius.
TEST(Round3Frames, ActiveTrafficWheelsAreTheCheapWheelsMatrices) {
    MM2_REQUIRE_GAME_DATA();
    const vfs::Vfs& vfs = *test::gameData();
    auto city = city::loadCity(vfs, "london");
    ASSERT_TRUE(city);
    ai::Settings settings;
    settings.trafficDensity = 1.0f;
    settings.pedestrianDensity = 0.0f;
    auto ai = ai::World::create(*city, vfs, settings);
    ASSERT_TRUE(ai);
    for (int i = 0; i < 60; ++i)
        ai->update(1.0f / 30.0f, {430, 0, -150}, {});
    // A car standing level, preferably one with a WHL4 pivot.
    const ai::AmbientCar* target = nullptr;
    for (const auto& c : ai->cars()) {
        if (!c.data || c.transform.m1.y < 0.99f)
            continue;
        if (!target || (c.data->wheelCount > target->data->wheelCount))
            target = &c;
    }
    ASSERT_TRUE(target);
    const int id = target->id;
    const Mat34 frame = target->transform;
    const ai::VehicleData& d = *target->data;

    // The floor 10 m below: the car falls with its wheels off the ground.
    auto level = std::make_unique<FloorLevel>(frame.m3 - Vec3{0.0f, 10.0f, 0.0f}, 400.0f);
    phys::MaterialTable materials;
    phys::World world(materials);
    world.setLevel(level.get());
    world.setStatic(level->soup(materials));
    game::TrafficBodies traffic(*ai, world);
    level->source = &traffic;
    traffic.beforeStep(); // the rail cars join their rooms
    std::vector<phys::Instance*> listed;
    traffic.instancesIn(1, listed);
    phys::Instance* rail = nullptr;
    for (phys::Instance* inst : listed)
        if (inst->matrix().m3.dist2(frame.m3) < 1e-6f)
            rail = inst;
    ASSERT_TRUE(rail) << "the car is an instance of its room";
    EXPECT_FALSE(traffic.wheelsOf(id)) << "no body yet: the rail's turning wheels";
    // What World::collideInstances does with a hit: the body, a new mover.
    phys::Body* attached = rail->attachEntity();
    ASSERT_TRUE(attached);
    world.addNewMover(attached);

    // vehWheelCheap::Init: at the pivots of the car's matrix.
    auto wheels = traffic.wheelsOf(id);
    ASSERT_TRUE(wheels);
    for (std::size_t i = 0; i < 4; ++i) {
        ASSERT_TRUE(wheels->valid[i]);
        expectSameMatrix(wheels->matrix[i], Mat34::mul(Mat34::translation(d.wheels[i]), frame), "init");
    }

    // Falling: no ground under the wheels, so each hangs Limit below its
    // pivot with no deflection. (A new mover takes part from the frame's
    // second sample: two frames.)
    for (int k = 0; k < 2; ++k) {
        ai->update(kFrame, {430, 0, -150}, {}); // the AI publishes the car as physical
        traffic.beforeStep();                   // aiVehicleManager::Update declares the body
        world.advanceFixed(kFrame);
        traffic.afterStep();
    }
    const Mat34* body = traffic.transformOf(id);
    ASSERT_TRUE(body);
    EXPECT_LT(body->m3.y, frame.m3.y) << "the car falls";
    wheels = traffic.wheelsOf(id);
    ASSERT_TRUE(wheels);
    for (std::size_t i = 0; i < 4; ++i) {
        const Vec3 local{d.wheels[i].x, d.wheels[i].y + -d.limit, d.wheels[i].z};
        expectSameMatrix(wheels->matrix[i], Mat34::mul(Mat34::translation(local), *body), "falling");
    }
    for (std::size_t i = 4; i < 6; ++i) {
        if (d.wheelCount <= static_cast<int>(i)) {
            EXPECT_FALSE(wheels->valid[i]);
            continue;
        }
        ASSERT_TRUE(wheels->valid[i]);
        const float raised = (d.wheels[i - 2].y + -d.limit) - d.wheelRadius;
        const Vec3 local{d.wheels[i].x, raised + d.wheels[i].y, d.wheels[i].z};
        expectSameMatrix(wheels->matrix[i], Mat34::mul(Mat34::translation(local), *body), "WHL4/5");
    }
}

// aiVehicleInstance::DrawShadow: with a body the shadow lies on the ground
// (lvlInstance::DrawPhysics); on its rail it sits at GetMatrix while the car
// is upright, on the ground only when it is upside down; without ground it
// stays at GetMatrix.
TEST(Round3Frames, TrafficShadowPlacement) {
    const float groundY = 2.0f;
    game::VehicleRenderer::GroundProbe flat = [&](const Vec3& from, const Vec3& to, Vec3& point, Vec3& normal) {
        if (!(from.y >= groundY && to.y <= groundY))
            return false;
        point = {from.x, groundY, from.z};
        normal = {0.0f, 1.0f, 0.0f};
        return true;
    };
    game::VehicleRenderer::GroundProbe none = [](const Vec3&, const Vec3&, Vec3&, Vec3&) { return false; };
    Mat34 upright = Mat34::rotationY(0.3f);
    upright.m3 = {5.0f, 2.5f, -7.0f};
    // On the rail, upright: at the body.
    expectSameMatrix(game::trafficShadowMatrix(upright, false, flat), upright, "rail upright");
    // With a body: on the ground.
    const Mat34 active = game::trafficShadowMatrix(upright, true, flat);
    EXPECT_FLOAT_EQ(active.m3.y, groundY);
    EXPECT_FLOAT_EQ(active.m3.x, upright.m3.x);
    EXPECT_NEAR(active.m1.y, 1.0f, 1e-6f);
    // With a body but no ground: at the body.
    expectSameMatrix(game::trafficShadowMatrix(upright, true, none), upright, "active, no ground");
    // Upside down on the rail: on the ground; DrawPhysics turns the flipped
    // up axis onto the ground's normal and negates it, so the shadow's up
    // axis points up again (its other two axes stay the car's).
    Mat34 flipped = upright;
    flipped.m1 = -upright.m1;
    flipped.m2 = -upright.m2;
    const Mat34 under = game::trafficShadowMatrix(flipped, false, flat);
    EXPECT_FLOAT_EQ(under.m3.y, groundY);
    EXPECT_NEAR(under.m1.y, 1.0f, 1e-6f);
    EXPECT_NEAR(under.m2.x, flipped.m2.x, 1e-6f);
    expectSameMatrix(game::trafficShadowMatrix(flipped, false, none), flipped, "upside down, no ground");
}
