#include "TestData.h"
#include "city/PathSet.h"
#include "game/bangers/BangerData.h"
#include "game/bangers/BangerSet.h"
#include "game/bangers/PropPlacement.h"
#include "phys/World.h"
#include "vfs/DirectoryFs.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

using namespace mm2;
using namespace mm2::game::bangers;

namespace {

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
        write("light", 800.0f, 100.0f * 100.0f, 0); // capped at 100 N s
        write("heavy", 50000.0f, 1e15f, 0);
        write("split", 40.0f, 1e6f, 2);
        write("split_break01", 20.0f, 1e6f, 0);
        write("split_break02", 20.0f, 1e6f, 0);
        vfs.mount(std::make_shared<vfs::DirectoryFs>(dir));
    }
    ~TempBangers() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    void write(const char* name, float mass, float limit2, int parts) {
        std::ofstream f(dir / "tune" / "banger" / (std::string(name) + ".dgbangerdata"));
        f << "type: a\ndgBangerData {\n  Size 0.5 2.0 0.5\n  CG 0 1 0\n  Mass " << mass << "\n  Elasticity 0.5\n"
          << "  Friction 0.9\n  ImpulseLimit2 " << limit2 << "\n  NumParts " << parts
          << "\n  BirthRule {\n    InitialBlast 0\n  }\n  TexNumber 0\n}\n";
    }
};

phys::World flatWorld() {
    phys::World w;
    phys::PolygonSoup soup;
    phys::Polygon ground;
    ground.v = {Vec3{-100, 0, -100}, Vec3{-100, 0, 100}, Vec3{100, 0, 100}, Vec3{100, 0, -100}};
    ground.count = 4;
    ground.finalize();
    soup.add(ground);
    soup.finalize();
    w.setStatic(std::move(soup));
    return w;
}

phys::Body car(const Vec3& pos, const Vec3& vel) {
    phys::Body b;
    b.shape.half = {0.9f, 0.6f, 2.0f};
    b.ics.setMass(1.8f, 1.2f, 4.0f, 1000.0f);
    b.ics.place(Mat34::translation(pos));
    b.ics.linearVelocity = vel;
    b.ics.linearMomentum = vel * 1000.0f;
    b.ics.state = phys::InertialCS::Awake;
    return b;
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

TEST(Bangers, HardHitBreaksThePropLoose) {
    TempBangers t;
    BangerDataLibrary lib(t.vfs);
    BangerSet set(lib);
    phys::World world = flatWorld();
    set.setWorld(&world);
    set.add({{"light", Mat34::translation({0, 0, 0}), 0, PlacedProp::Source::Instance, true},
             {"heavy", Mat34::translation({20, 0, 0}), 0, PlacedProp::Source::Instance, true}});
    ASSERT_EQ(set.instances().size(), 2u);
    EXPECT_FLOAT_EQ(set.instances()[0].matrix.m3.y, 1.0f); // frame at the CG

    // A car at 10 m/s: stopping it would take far more than the light prop's
    // limit of 100 N s, so the car spends 100 N s breaking it and the rest
    // is a two-body collision.
    phys::Body c = car({0, 0.6f, 2.1f}, {0, 0, -10});
    const Vec3 point{0, 1, 0.25f};
    const float mCar = c.ics.effectiveMass({0, 0, -1}, point);
    set.impact(0, c, point, {0, 0, 1});
    EXPECT_EQ(set.instances()[0].state, BangerSet::State::Active);
    const float residual = 10.0f - 100.0f / mCar;
    const float j2 = 1.5f * residual / (1.0f / mCar + 1.0f / 800.0f);
    EXPECT_NEAR(c.ics.linearImpulse.z, 100.0f + j2, 1.0f); // opposite impulse queued for the car
    // The prop flies off on its own (the car is not simulated here).
    for (int i = 0; i < 600; ++i) {
        set.update(1.0f / 60.0f, {});
        world.step(1.0f / 60.0f);
    }
    EXPECT_EQ(set.instances()[0].state, BangerSet::State::Hit); // came to rest, can be hit again
    EXPECT_LT(set.instances()[0].matrix.m3.z, 0.0f);            // pushed along -Z
    EXPECT_GT(set.instances()[0].matrix.m3.y, 0.0f);            // lying on the ground
    EXPECT_LT(set.instances()[0].matrix.m3.y, 1.0f);            // toppled (CG was 1 m up)
    set.setWorld(nullptr);
}

TEST(Bangers, GentleHitLeavesThePropStandingAndSplitPropsBreak) {
    TempBangers t;
    BangerDataLibrary lib(t.vfs);
    BangerSet set(lib);
    set.add({{"heavy", Mat34::identity(), 0, PlacedProp::Source::Instance, true},
             {"split", Mat34::translation({10, 0, 0}), 0, PlacedProp::Source::Instance, true}});
    // The heavy prop's limit (1e15) is never reached: it holds like a wall
    // and the car bounces off with (1 + e) times the stopping impulse.
    phys::Body c = car({0, 0.6f, 2.1f}, {0, 0, -10});
    const Vec3 point{0, 1, 0.25f};
    const float mCar = c.ics.effectiveMass({0, 0, -1}, point);
    set.impact(0, c, point, {0, 0, 1});
    EXPECT_EQ(set.instances()[0].state, BangerSet::State::Unhit);
    EXPECT_NEAR(c.ics.linearImpulse.z, 1.5f * 10.0f * mCar, 1.0f);

    phys::Body c2 = car({10, 0.6f, 2.1f}, {0, 0, -10});
    set.impact(1, c2, {10, 1, 0.25f}, {0, 0, 1});
    EXPECT_EQ(set.instances()[1].state, BangerSet::State::Gone);
    ASSERT_EQ(set.instances().size(), 4u);
    EXPECT_EQ(set.instances()[2].part, 0);
    EXPECT_EQ(set.instances()[3].part, 1);
    EXPECT_EQ(set.activeCount(), 2);
    set.reset();
    EXPECT_EQ(set.instances().size(), 2u);
    EXPECT_EQ(set.activeCount(), 0);
}

TEST(Bangers, HitPropsLiveInARingOfForty) {
    TempBangers t;
    BangerDataLibrary lib(t.vfs);
    BangerSet set(lib);
    std::vector<PlacedProp> props;
    for (int i = 0; i < 45; ++i)
        props.push_back({"light", Mat34::translation({static_cast<float>(i) * 10.0f, 0, 0}), 0,
                         PlacedProp::Source::Instance, true});
    set.add(props);
    for (std::size_t i = 0; i < 45; ++i) {
        phys::Body c = car({static_cast<float>(i) * 10.0f, 0.6f, 2.1f}, {0, 0, -10});
        set.impact(i, c, {static_cast<float>(i) * 10.0f, 1, 0.25f}, {0, 0, 1});
    }
    // The 41st knocked-over prop took the first one's slot, and so on.
    EXPECT_EQ(set.activeCount(), BangerSet::kMaxActive);
    for (std::size_t i = 0; i < 5; ++i)
        EXPECT_EQ(set.instances()[i].state, BangerSet::State::Gone) << i;
    EXPECT_NE(set.instances()[5].state, BangerSet::State::Gone);
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

TEST(Bangers, EjectedCarPartsFlyOff) {
    TempBangers t;
    BangerDataLibrary lib(t.vfs);
    BangerSet set(lib);
    const BangerData* d = lib.find("light");
    ASSERT_TRUE(d);
    set.ejectPart(*d, "vp4x4", "BREAK01", 2, Mat34::translation({1, 1, 1}), 4.0f);
    ASSERT_EQ(set.instances().size(), 1u);
    const auto& inst = set.instances()[0];
    EXPECT_EQ(inst.state, BangerSet::State::Active);
    EXPECT_EQ(inst.mesh, "BREAK01");
    EXPECT_EQ(inst.paint, 2);
    EXPECT_EQ(set.activeCount(), 1);
    set.reset(); // ejected parts go with a reset
    EXPECT_TRUE(set.instances().empty());
    EXPECT_EQ(set.activeCount(), 0);
}
