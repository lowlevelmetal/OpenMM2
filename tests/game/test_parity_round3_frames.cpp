// Parity round 3, frames: every model, part, glow and shadow is drawn in the
// frame MM2 draws it in (see docs/parity/round3/frames.md).
#include "TestData.h"
#include "ai/World.h"
#include "asset/VehicleModel.h"
#include "city/CityData.h"
#include "game/AiRenderer.h"
#include "game/ModelLibrary.h"
#include "game/TextureLibrary.h"
#include "game/TrafficBodies.h"
#include "game/bangers/BangerSet.h"
#include "game/fx/ParticleRenderer.h"
#include "game/VehicleRenderer.h"
#include "phys/World.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <memory>
#include <set>
#include <string>
#include <utility>
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

// A device that keeps the draw calls (and draws nothing).
class RecordingDevice final : public render::Device {
public:
    const render::DeviceInfo& info() const override { return m_info; }
    render::TextureHandle createTexture(const render::TextureDesc&,
                                        std::span<const render::TextureData>) override {
        return {++m_next};
    }
    void updateTexture(render::TextureHandle, std::uint32_t, const render::Rect&, const void*,
                       std::uint32_t) override {}
    void destroyTexture(render::TextureHandle) override {}
    render::BufferHandle createBuffer(render::BufferKind, std::size_t, const void*) override {
        return {++m_next};
    }
    void updateBuffer(render::BufferHandle, std::size_t, std::span<const std::byte>) override {}
    void destroyBuffer(render::BufferHandle) override {}
    render::BufferSlice uploadTransient(render::BufferKind, std::span<const std::byte>) override {
        return {{++m_next}, 0};
    }
    void applySettings(const render::DisplaySettings&) override {}
    void notifyResized() override {}
    bool beginFrame() override { return true; }
    render::Extent2D outputExtent() const override { return {640, 480}; }
    render::Extent2D sceneExtent() const override { return {640, 480}; }
    void beginScene(const render::ClearValues&) override {}
    void endScene() override {}
    void beginOverlay(const Vec4&) override {}
    void endOverlay() override {}
    void endFrame() override {}
    void setViewport(const render::Viewport&) override {}
    void setScissor(const render::Rect*) override {}
    void clear(const render::ClearValues&) override {}
    void setFrameConstants(const render::FrameConstants&) override {}
    void draw(const render::DrawCall& call) override { calls.push_back(call); }
    void requestCapture() override {}
    bool readCapture(render::Image&) override { return false; }
    void waitIdle() override {}
    const render::FrameStats& stats() const override { return m_stats; }

    std::vector<render::DrawCall> calls;

private:
    render::DeviceInfo m_info;
    render::FrameStats m_stats;
    std::uint32_t m_next = 0;
};

// The meshes ("<PART>_<LOD>") of `model` the recorded calls drew, and the
// world matrix of each call that drew one.
struct Drawn {
    std::set<std::string> meshes;
    std::vector<std::pair<std::string, Mat44>> worlds;
};

Drawn drawnMeshes(const RecordingDevice& device, const game::GpuModel& model) {
    auto lodName = [](asset::Lod lod) {
        switch (lod) {
        case asset::Lod::High: return "H";
        case asset::Lod::Medium: return "M";
        case asset::Lod::Low: return "L";
        case asset::Lod::VeryLow: return "VL";
        default: return "?";
        }
    };
    Drawn out;
    for (const auto& call : device.calls)
        for (const auto& mesh : model.meshes)
            if (call.vertices.buffer == mesh.vertices) {
                const std::string name = mesh.part + "_" + lodName(mesh.lod);
                out.meshes.insert(name);
                out.worlds.emplace_back(name, call.constants.world);
            }
    return out;
}

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

// vehTrailerInstance::Draw: a semi trailer draws its body alone below the
// high LOD; at H the body, TLIGHT while the tow car brakes (in the object
// pass) and TWHL0-3 at its wheel matrices, nothing else of vehCarModel's
// (in particular no TWHL0/1 medium meshes, which are modelled away from
// their pivots), and no glows (vehTrailerInstance keeps lvlInstance's
// empty DrawGlow).
TEST(Round3Frames, TrailerDrawnAsVehTrailerInstance) {
    MM2_REQUIRE_GAME_DATA();
    const vfs::Vfs& vfs = *test::gameData();
    auto read = [&](std::string_view path) { return vfs.readAll(path); };
    auto model = asset::loadVehicleModel("vpsemi_trailer", read);
    ASSERT_TRUE(model);
    RecordingDevice device;
    game::TextureLibrary textures(device, vfs);
    game::ModelLibrary models(device, vfs);
    game::VehicleRenderer r(device, textures, models, *model, 0, "TRAILER", "TWHL");
    const game::GpuModel* gpu = models.get("vpsemi_trailer");
    ASSERT_TRUE(gpu);
    ASSERT_TRUE(gpu->find("TWHL0", asset::Lod::Medium)) << "the trailer has a TWHL0 medium mesh";

    // Side on to a camera at the origin looking down -Z, 15 m away.
    game::VehiclePose pose;
    pose.body = Mat34::rotationY(1.5707964f);
    pose.body.m3 = {0.0f, 0.0f, -15.0f};
    for (std::size_t i = 0; i < 4; ++i) {
        const auto* pivot = model->pivot("twhl" + std::to_string(i));
        ASSERT_TRUE(pivot);
        pose.wheelWorld[i] = Mat34::translation(pivot->origin) * pose.body;
        pose.wheelValid[i] = true;
    }
    pose.hasWheelWorld = true;
    pose.brakeLights = true;
    pose.headlights = true;
    const Mat34 camera;

    r.draw(pose, camera);
    const Drawn near = drawnMeshes(device, *gpu);
    EXPECT_EQ(near.meshes, (std::set<std::string>{"TRAILER_H", "TLIGHT_L", "TWHL0_H", "TWHL1_H", "TWHL2_H",
                                                  "TWHL3_H", "SHADOW_H"}));
    for (const auto& [name, world] : near.worlds)
        for (std::size_t i = 0; i < 4; ++i)
            if (name == "TWHL" + std::to_string(i) + "_H") {
                const Mat44 expect = Mat44::fromMat34(pose.wheelWorld[i]);
                for (int a = 0; a < 4; ++a)
                    for (int b = 0; b < 4; ++b)
                        EXPECT_FLOAT_EQ(world.m[a][b], expect.m[a][b]) << name;
            }
    EXPECT_EQ(near.worlds.size(), device.calls.size()) << "no glow cards";

    // Not braking: no TLIGHT.
    device.calls.clear();
    pose.brakeLights = false;
    r.draw(pose, camera);
    EXPECT_FALSE(drawnMeshes(device, *gpu).meshes.contains("TLIGHT_L"));

    // 80 m away: the medium LOD body and no wheels.
    device.calls.clear();
    pose.body.m3.z = -80.0f;
    for (std::size_t i = 0; i < 4; ++i)
        pose.wheelWorld[i].m3.z = pose.wheelWorld[i].m3.z - 65.0f;
    r.draw(pose, camera);
    EXPECT_EQ(drawnMeshes(device, *gpu).meshes, (std::set<std::string>{"TRAILER_M", "SHADOW_H"}));
}

// pedAnimation::DrawSkeleton widens the stick figures along the first row of
// the modelview matrix taken in the pedestrian's own space: the camera's
// right axis while both only turn about Y, tilted under a pitched camera.
TEST(Round3Frames, StickFigureWidthAxis) {
    Mat34 ped = Mat34::rotationY(1.1f);
    ped.m3 = {10.0f, 0.0f, -30.0f};
    Mat34 camera = Mat34::rotationY(0.4f);
    camera.m3 = {0.0f, 2.0f, 5.0f};
    const Vec3 flat = game::skeletonWidthAxis(ped, camera);
    EXPECT_NEAR(flat.x, camera.m0.x, 1e-6f);
    EXPECT_NEAR(flat.y, camera.m0.y, 1e-6f);
    EXPECT_NEAR(flat.z, camera.m0.z, 1e-6f);

    const float a = 0.7f, p = 0.3f;
    const Vec3 tilted = game::skeletonWidthAxis(Mat34::rotationY(a), Mat34::rotationX(p));
    const float ca = std::cos(a), sa = std::sin(a), cp = std::cos(p), sp = std::sin(p);
    EXPECT_NEAR(tilted.x, ca * ca + sa * sa * cp, 1e-5f);
    EXPECT_NEAR(tilted.y, -sa * sp, 1e-5f);
    EXPECT_NEAR(tilted.z, sa * ca * (cp - 1.0f), 1e-5f);
}

// vehCarModel::DrawHeadlights keeps the headlights' ltLight directions in
// world space: the car's forward axis without the siren; with it, turned
// about Y by +-42.411503 rad/s of frame time from wherever they point, so
// the sweep does not turn with the car.
TEST(Round3Frames, HeadlightSweepIsInWorldSpace) {
    MM2_REQUIRE_GAME_DATA();
    const vfs::Vfs& vfs = *test::gameData();
    auto read = [&](std::string_view path) { return vfs.readAll(path); };
    auto model = asset::loadVehicleModel("vpcop", read);
    ASSERT_TRUE(model);
    RecordingDevice device;
    game::TextureLibrary textures(device, vfs);
    game::ModelLibrary models(device, vfs);
    game::VehicleRenderer r(device, textures, models, *model, 0);
    Mat34 camera;
    camera.m3 = {0.0f, 1.0f, 20.0f};
    game::VehiclePose pose;
    pose.headlights = true;
    pose.body = Mat34::rotationY(0.3f);
    r.draw(pose, camera);
    const Vec3 forward = -pose.body.m2;
    EXPECT_FLOAT_EQ(r.headlightDirections()[0].x, forward.x);
    EXPECT_FLOAT_EQ(r.headlightDirections()[1].z, forward.z);

    // The siren on for a tenth of a second, the car turned meanwhile.
    pose.siren = true;
    pose.sirenAngle = 0.1f * 2.5f * 3.1415927f;
    pose.body = Mat34::rotationY(1.2f);
    r.draw(pose, camera);
    const float sweep = pose.sirenAngle / (2.5f * 3.1415927f) * 42.411503f;
    const Vec3 left = Mat34::rotationY(sweep).transformDir(forward);
    const Vec3 right = Mat34::rotationY(-sweep).transformDir(forward);
    EXPECT_NEAR(r.headlightDirections()[0].x, left.x, 1e-5f);
    EXPECT_NEAR(r.headlightDirections()[0].z, left.z, 1e-5f);
    EXPECT_NEAR(r.headlightDirections()[1].x, right.x, 1e-5f);
    EXPECT_NEAR(r.headlightDirections()[1].z, right.z, 1e-5f);

    // Off again: forward.
    pose.siren = false;
    r.draw(pose, camera);
    EXPECT_FLOAT_EQ(r.headlightDirections()[0].x, -pose.body.m2.x);
}

namespace {

// One room with a floor at height `y`, listing a source's instances.
class FloorRoom final : public phys::Level {
public:
    FloorRoom(const game::InstanceSource& source, float y) : m_source(source), m_y(y) {}
    int findRoom(const Vec3&, int) const override { return 1; }
    int touchedNeighbors(int*, int, int, const Vec3&, float) const override { return 0; }
    void collect(const int*, int, const Vec3& c, float, phys::LevelBound& out) const override {
        out.clear();
        const Vec3 floor[4] = {{c.x - 500, m_y, c.z - 500}, {c.x - 500, m_y, c.z + 500},
                               {c.x + 500, m_y, c.z + 500}, {c.x + 500, m_y, c.z - 500}};
        out.addPolygon(floor, 4, {0, 1, 0}, 0);
    }
    void instances(int room, std::vector<phys::Instance*>& out) const override {
        m_source.instancesIn(room, out);
    }

private:
    const game::InstanceSource& m_source;
    float m_y;
};

} // namespace

// aiTrafficLightInstance is an unhit Y banger of its model's data: it stands
// at the pole's base + R * CG (the body's frame), collides as a prop and
// breaks loose into its BREAKnn parts, which are then drawn as ordinary hit
// bangers; while it stands, BangerSet leaves its drawing to the signal
// (AiRenderer).
TEST(Round3Frames, TrafficLightsAreProps) {
    MM2_REQUIRE_GAME_DATA();
    const vfs::Vfs& vfs = *test::gameData();
    auto city = city::loadCity(vfs, "sf");
    ASSERT_TRUE(city);
    auto world = ai::World::create(*city, vfs, {});
    ASSERT_TRUE(world);
    ASSERT_FALSE(world->signals().empty());
    const ai::Signal& signal = world->signals().front();

    game::bangers::BangerDataLibrary lib(vfs);
    game::bangers::BangerSet set(lib);
    FloorRoom level(set, signal.transform.m3.y);
    phys::MaterialTable materials;
    phys::World physics(materials);
    physics.setLevel(&level);
    set.setWorld(&physics);

    game::bangers::PlacedProp p;
    p.model = signal.model;
    p.transform = signal.transform;
    p.room = 1;
    p.ownerDrawn = true;
    const auto index = set.addOne(p);
    ASSERT_TRUE(index);
    expectSameMatrix(set.instances()[*index].matrix, signal.frame(), "the light's frame");
    EXPECT_TRUE(set.standing(*index));
    std::vector<phys::Instance*> listed;
    set.instancesIn(1, listed);
    ASSERT_EQ(listed.size(), 1u) << "collidable in its room";
    ASSERT_TRUE(listed[0]->bound(0));

    // Standing: BangerSet draws nothing of it.
    RecordingDevice device;
    game::TextureLibrary textures(device, vfs);
    game::ModelLibrary models(device, vfs);
    game::fx::ParticleRenderer cards;
    game::Camera camera;
    camera.transform = game::Camera::lookAt(signal.frame().m3 + Vec3{12.0f, 2.0f, 12.0f}, signal.frame().m3);
    const Mat44 proj = Mat44::perspective(1.0f, 4.0f / 3.0f, camera.nearPlane, camera.farPlane, true);
    const game::Frustum frustum(camera.view() * proj);
    set.draw(device, models, textures, cards, frustum, camera, {});
    const game::GpuModel* gpu = models.get(signal.model);
    ASSERT_TRUE(gpu);
    EXPECT_TRUE(drawnMeshes(device, *gpu).meshes.empty());

    // A heavy box at 20 m/s into the pole's middle knocks it over.
    phys::BoundBox box({1.8f, 1.2f, 4.0f});
    box.makeOwnMaterial();
    phys::Body car;
    car.ics.setMass(1.8f, 1.2f, 4.0f, 1500.0f);
    car.collisionBound = &box;
    car.place(Mat34::translation(signal.frame().m3 + Vec3{0.0f, 0.0f, 3.0f}));
    car.ics.gravity = {0, 0, 0};
    car.ics.linearVelocity = {0.0f, 0.0f, -20.0f};
    car.ics.linearMomentum = {0.0f, 0.0f, -20.0f * 1500.0f};
    physics.add(&car);
    for (int i = 0; i < 60 && set.standing(*index); ++i) {
        physics.step(kFrame);
        set.update(kFrame);
    }
    ASSERT_FALSE(set.standing(*index)) << "the light broke loose";
    EXPECT_EQ(set.instances()[*index].state, game::bangers::BangerSet::State::Gone);
    std::size_t parts = 0;
    for (const auto& inst : set.instances())
        if (inst.everHit && inst.model == signal.model && inst.part >= 0) {
            ++parts;
            EXPECT_FALSE(inst.ownerDrawn);
        }
    EXPECT_EQ(static_cast<int>(parts), set.instances()[*index].data->numParts);
    device.calls.clear();
    set.draw(device, models, textures, cards, frustum, camera, {});
    const Drawn drawn = drawnMeshes(device, *gpu);
    EXPECT_FALSE(drawn.meshes.empty()) << "its parts are drawn";
    for (const auto& name : drawn.meshes)
        EXPECT_TRUE(name.starts_with("BREAK")) << name;
    physics.remove(&car);
    set.setWorld(nullptr);
}
