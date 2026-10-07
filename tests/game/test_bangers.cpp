#include "TestData.h"
#include "city/PathSet.h"
#include "game/bangers/BangerData.h"
#include "game/bangers/BangerSet.h"
#include "game/bangers/PropPlacement.h"
#include "phys/Bound.h"
#include "phys/World.h"
#include "vfs/DirectoryFs.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <utility>

using namespace mm2;
using namespace mm2::game::bangers;

namespace {

constexpr float kDt = 1.0f / 60.0f;

// A temporary game folder with a few banger files.
struct TempBangers {
    // One directory per test: ctest runs the tests in parallel processes, and
    // Windows cannot delete files another process still has open.
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                (std::string("openmm2_bangers_") +
                                 ::testing::UnitTest::GetInstance()->current_test_info()->name());
    vfs::Vfs vfs;
    TempBangers() {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir / "tune" / "banger");
        std::filesystem::create_directories(dir / "bound");
        write("light", 800.0f, 100.0f * 100.0f, 0); // breaks above 100 N s
        write("heavy", 50000.0f, 1e15f, 0);
        write("split", 40.0f, 1e6f, 2);
        write("split_break01", 20.0f, 1e6f, 0, {0.5f, 1.0f, 0.5f}, {0, 0.5f, 0});
        write("split_break02", 20.0f, 1e6f, 0, {0.5f, 1.0f, 0.5f}, {0, 1.5f, 0});
        write("pole", 10.0f, 1e4f, 0, {0.2f, 3.0f, 0.2f}, {0, 1.5f, 0},
              "  YRadius 0.1\n  ColliderId 2\n  CollisionPrim 2\n", 0.3f, 0.6f);
        write("bollard", 10.0f, 1e4f, 0, {0.4f, 1.2f, 0.4f}, {0, 0.6f, 0}, "  CollisionType 4\n");
        write("shaped", 30.0f, 1e4f, 0, {0.5f, 2.0f, 0.5f}, {0, 1.0f, 0}, "  CollisionPrim 0\n");
        write("noshape", 30.0f, 1e4f, 0, {0.5f, 2.0f, 0.5f}, {0, 1.0f, 0}, "  CollisionPrim 0\n");
        writeBox("shaped", 0.25f, 2.0f);
        vfs.mount(std::make_shared<vfs::DirectoryFs>(dir));
    }
    ~TempBangers() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    void write(const char* name, float mass, float limit2, int parts, Vec3 size = {0.5f, 2.0f, 0.5f},
               Vec3 cg = {0, 1, 0}, const char* extra = "  CollisionPrim 1\n", float elasticity = 0.5f,
               float friction = 0.9f) {
        std::ofstream f(dir / "tune" / "banger" / (std::string(name) + ".dgbangerdata"));
        f << "type: a\ndgBangerData {\n  Size " << size.x << " " << size.y << " " << size.z;
        f << "\n  CG " << cg.x << " " << cg.y << " " << cg.z << "\n  Mass " << mass;
        f << "\n  Elasticity " << elasticity << "\n  Friction " << friction;
        f << "\n  ImpulseLimit2 " << limit2 << "\n  NumParts " << parts;
        f << "\n  BirthRule {\n    InitialBlast 0\n  }\n  TexNumber 0\n" << extra << "}\n";
    }
    // bound/<name>_bound.bnd: a box of half width `a` from the ground up to
    // `h` (corners 0-3 on the ground, 4-7 on top).
    void writeBox(const char* name, float a, float h) {
        std::ofstream f(dir / "bound" / (std::string(name) + "_bound.bnd"));
        f << "version: 1.01\nverts: 8\nmaterials: 1\nedges: 0\npolys: 6\n\n";
        const std::pair<float, float> corners[] = {{-a, -a}, {a, -a}, {a, a}, {-a, a}};
        for (const float y : {0.0f, h})
            for (const auto& [x, z] : corners)
                f << "v " << x << " " << y << " " << z << "\n";
        f << "mtl default {\n\telasticity: 0.1\n\tfriction: 0.2\n\teffect: none\n\tsound: 0\n}\n";
        f << "quad 0 1 2 3 0\nquad 4 7 6 5 0\nquad 0 4 5 1 0\n";
        f << "quad 3 2 6 7 0\nquad 0 3 7 4 0\nquad 1 5 6 2 0\n";
    }
};

// The city level as the collision manager sees it: one room with a floor,
// whose objects are the set's props (CityLevel lists them through
// InstanceSource::instancesIn).
class PropLevel final : public phys::Level {
public:
    explicit PropLevel(const game::InstanceSource& source) : m_source(source) {}
    int findRoom(const Vec3&, int) const override { return 1; }
    int touchedNeighbors(int*, int, int, const Vec3&, float) const override { return 0; }
    void collect(const int*, int, const Vec3&, float, phys::LevelBound& out) const override {
        out.clear();
        const Vec3 floor[4] = {{-500, 0, -500}, {-500, 0, 500}, {500, 0, 500}, {500, 0, -500}};
        out.addPolygon(floor, 4, {0, 1, 0}, 0);
    }
    void instances(int room, std::vector<phys::Instance*>& out) const override {
        m_source.instancesIn(room, out);
    }

private:
    const game::InstanceSource& m_source;
};

// Data, a world on a one-room level and the set. Members are destroyed in
// reverse order: the set leaves the world before it goes.
struct Rig {
    TempBangers files;
    BangerDataLibrary lib{files.vfs};
    phys::World world;
    BangerSet set{lib};
    PropLevel level{set};
    Rig() {
        world.setLevel(&level);
        set.setWorld(&world);
    }
    void frame() {
        world.step(kDt);
        set.update(kDt);
    }
    PlacedProp placed(const char* model, const Vec3& at) {
        return {model, Mat34::translation(at), 1, PlacedProp::Source::Instance, true};
    }
};

// A box "car" 1.8 x 1.2 x 4 m of 1000 kg flying level (no gravity) at `vel`.
struct Car {
    std::unique_ptr<phys::BoundBox> bound = std::make_unique<phys::BoundBox>(Vec3{1.8f, 1.2f, 4.0f});
    phys::Body body;
    Car(const Vec3& pos, const Vec3& vel) {
        bound->makeOwnMaterial();
        body.ics.setMass(1.8f, 1.2f, 4.0f, 1000.0f);
        body.collisionBound = bound.get();
        body.place(Mat34::translation(pos));
        body.ics.gravity = {0, 0, 0};
        body.ics.linearVelocity = vel;
        body.ics.linearMomentum = vel * 1000.0f;
    }
};

std::vector<phys::Instance*> listed(const BangerSet& set) {
    std::vector<phys::Instance*> out;
    set.instancesIn(1, out);
    return out;
}

} // namespace

TEST(Bangers, ParsesDataAndParts) {
    TempBangers t;
    BangerDataLibrary lib(t.vfs);
    const auto* d = lib.find("SPLIT");
    ASSERT_TRUE(d);
    EXPECT_EQ(d->numParts, 2);
    EXPECT_FLOAT_EQ(d->cg.y, 1.0f);
    ASSERT_TRUE(lib.part("split", 1));
    EXPECT_EQ(lib.part("split", 1)->name, "split_break02");
    EXPECT_FALSE(lib.find("nothing"));
}

TEST(Bangers, PathPlacementDecodesTypeAndSpacing) {
    city::PathSet set;
    city::PathSetPath line;
    line.name = "open:fence";
    // Line strip (type 2), spacing byte 8 = 2 m (quarter metres): 10 m / 2 m.
    line.points = {{{0, 0, 0}, 77}, {{10, 0, 0}, 0x0802}};
    city::PathSetPath posts;
    posts.name = "post";
    posts.points = {{{5, 0, 5}, 1}, {{9, 0, 9}, 0x1400}}; // single points
    city::PathSetPath pairs;
    pairs.name = "bench";
    pairs.points = {{{0, 0, 0}, 0}, {{0, 0, 3}, 0x0001}}; // a position and a direction
    set.paths = {line, posts, pairs};
    EXPECT_EQ(decodePathPlacement(line).type, 2);
    EXPECT_FLOAT_EQ(decodePathPlacement(line).spacing, 2.0f);
    EXPECT_FLOAT_EQ(decodePathPlacement(posts).spacing, 5.0f); // 0x14 quarter metres
    city::PathSetPath zero = posts;
    zero.points.back().extra = 0x0000;
    EXPECT_FLOAT_EQ(decodePathPlacement(zero).spacing, 5.0f); // 0 means 5 m
    const auto props = placePathSet(set, PlacedProp::Source::PathSet);
    ASSERT_EQ(props.size(), 5u + 2u + 1u);
    EXPECT_EQ(props[0].model, "fence");
    // Equal steps from each segment's start, none at the last point.
    EXPECT_NEAR(props[4].transform.m3.x, 8.0f, 1e-4f);
    EXPECT_NEAR(props[0].transform.m0.x, 1.0f, 1e-5f); // +X along the line
    EXPECT_TRUE(props[0].fullMatrix);
    EXPECT_FALSE(props[5].fullMatrix);
    // The pair's +X points at its second point.
    EXPECT_NEAR(props[7].transform.m0.z, 1.0f, 1e-5f);
    EXPECT_NEAR(props[7].transform.m3.z, 0.0f, 1e-5f);
}

TEST(Bangers, StreetRulesWalkTheRoad) {
    city::Psdl psdl;
    // Road along +Z, 10 m wide with 3 m sidewalks: outer L (-8), curb L (-5), curb R (5), outer R (8).
    psdl.vertices = {{-8, 0.2f, 0}, {-5, 0, 0}, {5, 0, 0}, {8, 0.2f, 0}, {-8, 0.2f, 20}, {-5, 0, 20}, {5, 0, 20}, {8, 0.2f, 20}};
    psdl.rooms.resize(2);
    psdl.rooms[1].propRule = 1;
    city::PsdlAttribute a;
    a.type = city::PsdlAttrType::RoadStrip;
    a.subtype = 2;
    a.args = {0, 1, 2, 3, 4, 5, 6, 7};
    psdl.rooms[1].attributes.push_back(a);
    if (a.vertices().size() != 8)
        GTEST_SKIP() << "PsdlAttribute::vertices() layout differs for this synthetic RoadStrip";
    city::PsdlRoad road;
    road.flags = 0x40; // has sidewalks
    road.rooms = {1};
    psdl.roads.push_back(road);
    const std::vector<PropDef> defs = {{"lamp", 1.0f, 5.0f, 3, 0.1f, 0.1f, {"sp_lamp"}}};
    const std::vector<PropRule> rules = {{"n01left", {"lamp"}}, {"n01right", {"lamp"}}};
    // At most three per walk: 1, 6 and 11 m along each side.
    const auto props = placeStreetProps(psdl, defs, rules);
    ASSERT_EQ(props.size(), 6u);
    for (const auto& p : props) {
        EXPECT_NEAR(std::abs(p.transform.m3.x), 5.3f, 1e-4f);    // 10% of the way from curb to outer edge
        EXPECT_NEAR(p.transform.m3.y, 0.02f + 0.15f, 1e-5f);     // lifted 0.15 m
        EXPECT_GT(p.transform.m0.x * p.transform.m3.x, 0.0f);    // +X points away from the road
        EXPECT_EQ(p.room, 1);
        EXPECT_FALSE(p.fullMatrix);
    }
    EXPECT_LT(props[0].transform.m3.x, 0.0f); // the left rule's props stand on the left
    EXPECT_NEAR(props[0].transform.m3.z, 1.0f, 1e-4f);
    EXPECT_NEAR(props[1].transform.m3.z, 6.0f, 1e-4f);
    EXPECT_NEAR(props[2].transform.m3.z, 11.0f, 1e-4f);
    EXPECT_GT(props[3].transform.m3.x, 0.0f);

    // Without the sidewalk flag nothing is placed.
    psdl.roads[0].flags = 0;
    EXPECT_TRUE(placeStreetProps(psdl, defs, rules).empty());
}

TEST(Bangers, BoundsFollowCollisionPrim) {
    Rig r;
    r.set.add({r.placed("light", {0, 0, 0}), r.placed("pole", {10, 0, 0}), r.placed("shaped", {20, 0, 0}),
               r.placed("noshape", {30, 0, 0})});
    ASSERT_EQ(r.set.instances().size(), 4u);

    // 1: a box of Size centred on the CG, with its own material from the data.
    const phys::Bound* box = r.set.bound(*r.lib.find("light"));
    ASSERT_TRUE(box);
    EXPECT_EQ(box->type, phys::BoundType::Box);
    EXPECT_FLOAT_EQ(box->boxMax.y, 1.0f);
    EXPECT_FLOAT_EQ(box->boxMin.y, -1.0f);
    EXPECT_FLOAT_EQ(box->material(0).elasticity, 0.5f);
    EXPECT_FLOAT_EQ(box->material(0).friction, 0.9f);
    EXPECT_EQ(r.set.prop(0).bound(1), box); // GetBound(1) of a box: itself
    EXPECT_NEAR(r.set.prop(0).radius(), std::sqrt(0.25f * 0.25f * 2.0f + 1.0f), 1e-5f);
    EXPECT_TRUE(r.set.prop(0).isBanger());
    EXPECT_FLOAT_EQ(r.set.prop(0).bangerImpulseLimit2(), 1e4f);
    Vec3 centre;
    float radius = 0;
    EXPECT_FALSE(r.set.prop(0).bangerSphere(centre, radius)); // no YRadius

    // 2: a hotdog of YRadius and Size.y; GetBound(1) is the box around it.
    const phys::Bound* hotdog = r.set.prop(1).bound(0);
    ASSERT_TRUE(hotdog);
    ASSERT_EQ(hotdog->type, phys::BoundType::Hotdog);
    EXPECT_FLOAT_EQ(static_cast<const phys::BoundHotdog*>(hotdog)->capRadius, 0.1f);
    EXPECT_FLOAT_EQ(static_cast<const phys::BoundHotdog*>(hotdog)->height, 3.0f);
    EXPECT_FLOAT_EQ(hotdog->material(0).elasticity, 0.3f);
    EXPECT_FLOAT_EQ(hotdog->material(0).friction, 0.6f);
    const phys::Bound* around = r.set.prop(1).bound(1);
    ASSERT_TRUE(around);
    ASSERT_EQ(around->type, phys::BoundType::Box);
    EXPECT_NEAR(around->boxMax.y, 1.6f, 1e-5f);
    EXPECT_NEAR(around->boxMin.x, -0.1f, 1e-5f);
    EXPECT_EQ(r.set.prop(1).audioId, 2); // ColliderId
    // TrivialCollideInstances' sphere: YRadius about the ground point.
    ASSERT_TRUE(r.set.prop(1).bangerSphere(centre, radius));
    EXPECT_FLOAT_EQ(radius, 0.1f);
    EXPECT_NEAR(centre.x, 10.0f, 1e-5f);
    EXPECT_NEAR(centre.y, 0.0f, 1e-5f);

    // 0: the bound file, moved so that the CG is its origin; a box without one.
    const phys::Bound* shaped = r.set.prop(2).bound(0);
    ASSERT_TRUE(shaped);
    ASSERT_EQ(shaped->type, phys::BoundType::Geometry);
    EXPECT_NEAR(shaped->boxMin.y, -1.0f, 1e-5f);
    EXPECT_NEAR(shaped->boxMax.y, 1.0f, 1e-5f);
    EXPECT_FLOAT_EQ(shaped->material(0).friction, 0.9f); // its own material, not the file's
    ASSERT_TRUE(r.set.prop(2).bound(1));
    EXPECT_EQ(r.set.prop(2).bound(1)->type, phys::BoundType::Box);
    ASSERT_TRUE(r.set.prop(3).bound(0));
    EXPECT_EQ(r.set.prop(3).bound(0)->type, phys::BoundType::Box);

    // All four stand in room 1.
    EXPECT_EQ(listed(r.set).size(), 4u);
    std::vector<phys::Instance*> other;
    r.set.instancesIn(2, other);
    EXPECT_TRUE(other.empty());
}

TEST(Bangers, GentleHitLeavesThePropStanding) {
    Rig r;
    r.set.add({r.placed("heavy", {0, 0, 0})});
    // The heavy prop's limit (1e15) is never reached: it holds like a wall
    // and the car bounces off it.
    Car car({0, 1, 2.7f}, {0, 0, -3});
    r.world.add(&car.body);
    for (int i = 0; i < 60 && car.body.ics.linearVelocity.z < 0.0f; ++i)
        r.frame();
    EXPECT_GT(car.body.ics.linearVelocity.z, 0.0f);
    EXPECT_EQ(r.set.instances()[0].state, BangerSet::State::Unhit);
    EXPECT_EQ(r.set.instances().size(), 1u); // no hit instance taken
    EXPECT_EQ(r.set.activeCount(), 0);       // its active went back to the pool
    EXPECT_FALSE(r.set.body(0));
    EXPECT_EQ(listed(r.set).size(), 1u);
    EXPECT_FLOAT_EQ(r.set.instances()[0].matrix.m3.z, 0.0f);
    r.world.remove(&car.body);
}

TEST(Bangers, HardHitBreaksThePropLoose) {
    Rig r;
    r.set.add({r.placed("light", {0, 0, 0})});
    ASSERT_EQ(r.set.instances().size(), 1u);
    EXPECT_FLOAT_EQ(r.set.instances()[0].matrix.m3.y, 1.0f); // frame at the CG

    // A car at 10 m/s: stopping it would take far more than the light
    // prop's limit of 100 N s, so it breaks loose.
    Car car({0, 1, 2.7f}, {0, 0, -10});
    r.world.add(&car.body);
    for (int i = 0; i < 60 && r.set.instances()[0].state == BangerSet::State::Unhit; ++i)
        r.frame();
    ASSERT_EQ(r.set.instances()[0].state, BangerSet::State::Gone);
    // A hit instance took its place with the active.
    ASSERT_EQ(r.set.instances().size(), 2u);
    const auto& hit = r.set.instances()[1];
    EXPECT_TRUE(hit.everHit);
    EXPECT_EQ(hit.state, BangerSet::State::Active);
    EXPECT_EQ(hit.model, "light");
    EXPECT_EQ(r.set.activeCount(), 1);
    ASSERT_TRUE(r.set.body(1));
    EXPECT_TRUE(r.world.contains(r.set.body(1)));
    EXPECT_FALSE(r.set.body(0));
    EXPECT_FALSE(r.set.prop(0).isBanger());
    EXPECT_FALSE(r.set.prop(0).bound(0)); // out of the movers' lists
    EXPECT_TRUE(listed(r.set).empty());   // the hit instance is not collidable while active

    // The car paid for it and slowed down; the prop flies off ahead of it.
    r.frame();
    EXPECT_LT(car.body.ics.linearVelocity.z, -1.0f);
    EXPECT_GT(car.body.ics.linearVelocity.z, -9.9f);
    r.world.remove(&car.body);
    r.frame();
    EXPECT_LT(r.set.body(1)->ics.linearVelocity.z, -2.0f);

    // It falls over, comes to rest, sleeps and detaches: a hit instance on
    // the floor that can be hit again.
    for (int i = 0; i < 1200 && r.set.activeCount() > 0; ++i)
        r.frame();
    EXPECT_EQ(r.set.activeCount(), 0);
    EXPECT_EQ(hit.state, BangerSet::State::Hit);
    EXPECT_EQ(r.set.hitCount(), 1);
    EXPECT_LT(hit.matrix.m3.z, -1.0f);  // pushed along -Z
    EXPECT_GT(hit.matrix.m3.y, 0.2f);   // lying on the floor
    EXPECT_LT(hit.matrix.m3.y, 0.35f);  // toppled (CG was 1 m up, half width 0.25)
    const auto now = listed(r.set);
    ASSERT_EQ(now.size(), 1u);
    EXPECT_EQ(now[0], &r.set.prop(1));
    EXPECT_FALSE(r.set.prop(1).isBanger());

    // A restart puts the prop back and empties the ring.
    r.set.reset();
    EXPECT_EQ(r.set.instances()[0].state, BangerSet::State::Unhit);
    EXPECT_EQ(r.set.instances()[1].state, BangerSet::State::Gone);
    EXPECT_TRUE(r.set.prop(0).isBanger());
    EXPECT_FLOAT_EQ(r.set.instances()[0].matrix.m3.y, 1.0f);
    ASSERT_EQ(listed(r.set).size(), 1u);
    EXPECT_EQ(listed(r.set)[0], &r.set.prop(0));
}

TEST(Bangers, SplitPropsBreakIntoTheirParts) {
    Rig r;
    r.set.add({r.placed("split", {0, 0, 0})});
    Car car({0, 1, 2.7f}, {0, 0, -10});
    r.world.add(&car.body);
    for (int i = 0; i < 60 && r.set.instances()[0].state == BangerSet::State::Unhit; ++i)
        r.frame();
    r.world.remove(&car.body);
    ASSERT_EQ(r.set.instances()[0].state, BangerSet::State::Gone);
    ASSERT_EQ(r.set.instances().size(), 3u);
    // Each BREAKnn part is a hit instance with its own active at its own CG;
    // the prop's active went back to the pool.
    EXPECT_EQ(r.set.activeCount(), 2);
    for (std::size_t k = 0; k < 2; ++k) {
        const auto& part = r.set.instances()[1 + k];
        EXPECT_EQ(part.part, static_cast<int>(k));
        EXPECT_EQ(part.state, BangerSet::State::Active);
        EXPECT_EQ(part.data, r.lib.part("split", static_cast<int>(k)));
        ASSERT_TRUE(r.set.body(1 + k));
    }
    EXPECT_NEAR(r.set.instances()[1].matrix.m3.y, 0.5f, 1e-5f);
    EXPECT_NEAR(r.set.instances()[2].matrix.m3.y, 1.5f, 1e-5f);
    // They carry the prop's change of motion.
    EXPECT_LT(r.set.body(1)->ics.linearImpulse.z, 0.0f);
    EXPECT_LT(r.set.body(2)->ics.linearImpulse.z, 0.0f);
    r.frame();
    EXPECT_LT(r.set.body(1)->ics.linearVelocity.z, -1.0f);
    EXPECT_LT(r.set.body(2)->ics.linearVelocity.z, -1.0f);
}

TEST(Bangers, EjectedCarPartsAreHitBangers) {
    Rig r;
    const BangerData* d = r.lib.find("light");
    ASSERT_TRUE(d);
    r.set.ejectPart(*d, "vp4x4", "BREAK01", 2, Mat34::translation({1, 1, 1}), 4.0f);
    ASSERT_EQ(r.set.instances().size(), 1u);
    const auto& inst = r.set.instances()[0];
    EXPECT_EQ(inst.state, BangerSet::State::Active);
    EXPECT_EQ(inst.mesh, "BREAK01");
    EXPECT_EQ(inst.paint, 2);
    EXPECT_EQ(inst.room, 1); // found from the level
    EXPECT_EQ(r.set.activeCount(), 1);
    const phys::Body* b = r.set.body(0);
    ASSERT_TRUE(b);
    EXPECT_TRUE(r.world.contains(b));
    // vehBreakableMgr::Eject writes speed +- 1 as momentum, upwards.
    const float p = b->ics.linearMomentum.mag();
    EXPECT_GE(p, 3.0f - 1e-4f);
    EXPECT_LE(p, 5.0f + 1e-4f);
    EXPECT_GE(b->ics.linearMomentum.y, 0.0f);
    const float spin = b->ics.angularImpulse.mag();
    EXPECT_GE(spin, 1.0f - 1e-4f);
    EXPECT_LE(spin, 3.0f + 1e-4f);
    r.set.reset(); // ejected parts go with a reset
    EXPECT_EQ(inst.state, BangerSet::State::Gone);
    EXPECT_EQ(r.set.activeCount(), 0);
    EXPECT_FALSE(r.world.contains(b));
}

TEST(Bangers, HitInstancesLiveInARingAndActivesInAPool) {
    Rig r;
    const BangerData* d = r.lib.find("light");
    ASSERT_TRUE(d);
    for (int k = 0; k < 42; ++k) {
        const Mat34 at = Mat34::translation({static_cast<float>(k) * 10.0f, 5, 0});
        r.set.ejectPart(*d, "vp4x4", "BREAK01", 0, at, 4.0f);
    }
    // 40 hit instances. On wrapping, dgBangerManager::GetBanger hands out
    // slot 0 twice: the 41st part took it and the 42nd took it again.
    ASSERT_EQ(r.set.instances().size(), 40u);
    EXPECT_FLOAT_EQ(r.set.instances()[0].matrix.m3.x, 410.0f);
    EXPECT_FLOAT_EQ(r.set.instances()[1].matrix.m3.x, 10.0f);
    // 32 actives: with all in use, dgBangerActiveManager::Attach takes the
    // first of its list from its prop, which stays where it was.
    EXPECT_EQ(r.set.activeCount(), BangerSet::kMaxActive);
    EXPECT_EQ(r.set.instances()[0].state, BangerSet::State::Active);
    for (std::size_t i = 1; i < 32; ++i)
        EXPECT_EQ(r.set.instances()[i].state, BangerSet::State::Active) << i;
    for (std::size_t i = 32; i < 40; ++i)
        EXPECT_EQ(r.set.instances()[i].state, BangerSet::State::Hit) << i;
    EXPECT_EQ(r.set.hitCount(), 8);
}

TEST(Bangers, CollisionTypeDecidesWhatActivesCollideWith) {
    Rig r;
    const BangerData* city = r.lib.find("bollard");
    const BangerData* all = r.lib.find("light");
    ASSERT_TRUE(city && all);
    r.set.ejectPart(*city, "vp4x4", "BREAK01", 0, Mat34::translation({0, 5, 0}), 4.0f);
    r.set.ejectPart(*all, "vp4x4", "BREAK02", 0, Mat34::translation({10, 5, 0}), 4.0f);
    // New movers collide with everything until the manager declares them.
    EXPECT_TRUE(r.set.body(0)->collideMovers);
    r.frame();
    // CollisionType 4: the city only; 0x10: everything.
    EXPECT_TRUE(r.set.body(0)->collideTerrain);
    EXPECT_FALSE(r.set.body(0)->collideInstances);
    EXPECT_FALSE(r.set.body(0)->collideMovers);
    EXPECT_TRUE(r.set.body(1)->collideTerrain);
    EXPECT_TRUE(r.set.body(1)->collideInstances);
    EXPECT_TRUE(r.set.body(1)->collideMovers);
}

TEST(BangersRetail, AllBangerDataParsesAndCitiesPlaceProps) {
    MM2_REQUIRE_GAME_DATA();
    const vfs::Vfs& v = *test::gameData();
    BangerDataLibrary lib(v);
    EXPECT_GE(lib.available(), 990u);
    int parsed = 0;
    for (const auto& name : lib.names()) {
        const auto* d = lib.find(name);
        EXPECT_TRUE(d) << name;
        if (!d)
            continue;
        ++parsed;
        for (int p = 0; p < d->numParts && name.find("_break") == std::string::npos; ++p)
            EXPECT_TRUE(lib.part(name, p)) << name << " part " << p;
    }
    EXPECT_GE(parsed, 990);
    for (const char* c : {"london", "sf"}) {
        auto city = city::loadCity(v, c);
        ASSERT_TRUE(city) << c;
        const auto props = placeCityProps(*city, v, lib);
        EXPECT_GT(props.size(), 2000u) << c;
        for (const auto& p : props)
            ASSERT_TRUE(lib.find(p.model)) << c << " " << p.model;
    }
}

TEST(BangersRetail, EveryBangerHasABound) {
    MM2_REQUIRE_GAME_DATA();
    const vfs::Vfs& v = *test::gameData();
    BangerDataLibrary lib(v);
    BangerSet set(lib);
    int geometry = 0;
    for (const auto& name : lib.names()) {
        const auto* d = lib.find(name);
        if (!d)
            continue;
        const phys::Bound* b = set.bound(*d);
        ASSERT_TRUE(b) << name;
        EXPECT_GT(b->radius, 0.0f) << name;
        if (b->type == phys::BoundType::Geometry)
            ++geometry;
    }
    // CollisionPrim 0 props load their "<name>_bound" geometry.
    EXPECT_GT(geometry, 0);
}
