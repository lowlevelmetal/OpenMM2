// Parity checks of the prop placement and banger instances against MM2's
// own code (MM2Recomp, build 3393): dgPath::Enumerate, lvlSDL::Propulate with
// lvlAiMap's sidewalk vertices, cityLevel::Load's order and race props,
// dgUnhitBangerInstance::Init. See docs/parity/camera-props.md.
#include "TestData.h"
#include "city/CityData.h"
#include "game/bangers/BangerData.h"
#include "game/bangers/BangerSet.h"
#include "game/bangers/PropPlacement.h"
#include "phys/World.h"
#include "vfs/DirectoryFs.h"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <tuple>

using namespace mm2;
using namespace mm2::game::bangers;

namespace {

// A temporary game folder with banger data for a few models.
struct TempData {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                (std::string("openmm2_parity_bangers_") +
                                 ::testing::UnitTest::GetInstance()->current_test_info()->name());
    vfs::Vfs vfs;
    TempData() {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir / "tune" / "banger");
        for (const char* name : {"sp_lamp", "sp_sign"}) {
            std::ofstream f(dir / "tune" / "banger" / (std::string(name) + ".dgbangerdata"));
            f << "type: a\ndgBangerData {\n  Size 0.4 2 0.4\n  CG 0.5 1 0\n  Mass 20\n  CollisionPrim 1\n}\n";
        }
        vfs.mount(std::make_shared<vfs::DirectoryFs>(dir));
    }
    ~TempData() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

city::PathSetPath path(const char* name, std::vector<Vec3> points, std::uint32_t trailer) {
    city::PathSetPath p;
    p.name = name;
    for (const auto& v : points)
        p.points.push_back({v, 0});
    p.points.back().extra = trailer;
    return p;
}

// A level whose rooms are split by height: room 1 below y = 0.5, room 2
// above (to tell a prop's ground point from its CG).
class TwoStoreyLevel final : public phys::Level {
public:
    int findRoom(const Vec3& p, int) const override { return p.y < 0.5f ? 1 : 2; }
    int touchedNeighbors(int*, int, int, const Vec3&, float) const override { return 0; }
    void collect(const int*, int, const Vec3&, float, phys::LevelBound& out) const override { out.clear(); }
    void instances(int, std::vector<phys::Instance*>&) const override {}
};

} // namespace

TEST(ParityBangers, LineStripPathsFollowDgPathEnumerate) {
    // A sloping segment 10 m long horizontally and 2 m up, spacing 2.5 m
    // (byte 10): floor(10.198 / 2.5) = 4 props, 10.198 / 4 apart.
    city::PathSet set;
    set.paths.push_back(path("ramp", {{0, 0, 0}, {10, 2, 0}}, 0x0A02));
    const auto props = placePathSet(set, PlacedProp::Source::PathSet);
    ASSERT_EQ(props.size(), 4u);
    const float length = std::sqrt(104.0f);
    const Vec3 x{10.0f / length, 2.0f / length, 0.0f};
    for (std::size_t k = 0; k < props.size(); ++k) {
        const Mat34& m = props[k].transform;
        EXPECT_NEAR(m.m3.x, x.x * length / 4.0f * static_cast<float>(k), 1e-4f);
        EXPECT_NEAR(m.m3.y, x.y * length / 4.0f * static_cast<float>(k), 1e-4f);
        EXPECT_NEAR(m.m0.x, x.x, 1e-6f);
        EXPECT_NEAR(m.m0.y, x.y, 1e-6f);
        // Z = X x Y and Y = Z x X are not normalised: on a slope they are
        // shorter than 1 (the horizontal part of X).
        EXPECT_NEAR(m.m2.mag(), x.x, 1e-6f);
        EXPECT_NEAR(m.m1.mag(), x.x, 1e-6f);
        EXPECT_TRUE(props[k].fullMatrix);
    }
    // dgPath::Enumerate places nothing for types other than 0, 1 and 2.
    city::PathSet other;
    other.paths.push_back(path("ramp", {{0, 0, 0}, {10, 0, 0}}, 0x0403));
    EXPECT_TRUE(placePathSet(other, PlacedProp::Source::PathSet).empty());
}

TEST(ParityBangers, StreetPropsWalkTheCutCornersOfTheSidewalk) {
    // One room, three road strip sections; the left curb turns right by 90
    // degrees at the middle section: (-5, 0, 0) -> (-5, 0, 10) -> (5, 0, 10).
    city::Psdl psdl;
    psdl.vertices = {{-8, 0, 0},   {-5, 0, 0},  {5, 0, 0},   {8, 0, 0},  {-8, 0, 13}, {-5, 0, 10},
                     {-4, 0, 3},   {4, 0, 3},   {5, 0, 13},  {5, 0, 10}, {6, 0, 4},   {7, 0, 3}};
    psdl.rooms.resize(2);
    psdl.rooms[1].propRule = 1;
    city::PsdlAttribute a;
    a.type = city::PsdlAttrType::RoadStrip;
    a.subtype = 3;
    a.args = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    psdl.rooms[1].attributes.push_back(a);
    ASSERT_EQ(a.vertices().size(), 12u);
    city::PsdlRoad road;
    road.flags = 0x40;
    road.rooms = {1};
    psdl.roads.push_back(road);
    // One lamp on the curb (lerp 0) 12 m along.
    const std::vector<PropDef> defs = {{"lamp", 12.0f, 100.0f, 1, 0.0f, 0.0f, {"sp_lamp"}}};
    const std::vector<PropRule> rules = {{"n01left", {"lamp"}}, {"n01right", {}}};
    const auto props = placeStreetProps(psdl, defs, rules);
    ASSERT_EQ(props.size(), 1u);
    // lvlAiMap::GetSidewalkVertex cuts the corner 0.1 m each way: the walk
    // reaches (-4.9, 0, 10) after 9.9 + sqrt(0.02) m, so 12 m along is
    // 1.9586 m further on (an uncut corner would give x = -3).
    const float cut = 9.9f + std::sqrt(0.02f);
    EXPECT_NEAR(props[0].transform.m3.x, -4.9f + (12.0f - cut), 1e-4f);
    EXPECT_NEAR(props[0].transform.m3.z, 10.0f, 1e-4f);
    EXPECT_NEAR(props[0].transform.m3.y, 0.15f, 1e-6f);
    EXPECT_EQ(props[0].room, 1);

    // Prop rule 0 has no props.
    psdl.rooms[1].propRule = 0;
    const std::vector<PropRule> zero = {{"n00left", {"lamp"}}, {"n00right", {}}};
    EXPECT_TRUE(placeStreetProps(psdl, defs, zero).empty());
}

TEST(ParityBangers, PropDefsDropOnlyTheCellAfterAFinalComma) {
    // parCsvFile keeps an empty cell between commas but not one after a
    // comma ending the line.
    const auto defs = parsePropDefs("name,start,distance,maxUse,minLerp,maxLerp,file1,file2,file3,file4\n"
                                    "a,1,2,3,0.1,0.2,x,,y\n"
                                    "b,1,2,3,0.1,0.2,x,\n");
    ASSERT_EQ(defs.size(), 2u);
    EXPECT_EQ(defs[0].files, (std::vector<std::string>{"x", "", "y"}));
    EXPECT_EQ(defs[1].files, (std::vector<std::string>{"x"}));
    EXPECT_FLOAT_EQ(defs[0].maxLerp, 0.2f);
    EXPECT_EQ(defs[0].maxUse, 3);
}

TEST(ParityBangers, RacePropsAreNamedLikeDgGameModeNames) {
    using game::GameMode;
    EXPECT_EQ(racePropsName(GameMode::Cruise, -1), "roam");
    EXPECT_EQ(racePropsName(GameMode::Checkpoint, 6), "race6");
    EXPECT_EQ(racePropsName(GameMode::Circuit, 0), "circuit0");
    EXPECT_EQ(racePropsName(GameMode::Blitz, 10), "blitz10");
    EXPECT_EQ(racePropsName(GameMode::CrashCourse, 9), "crash9");
    EXPECT_EQ(racePropsName(GameMode::CopsAndRobbers, -1), "multicop");
    EXPECT_EQ(racePropsName(GameMode::Circuit, -1), "");
}

TEST(ParityBangers, InstancesKeepTheirFormAndVariant) {
    TempData t;
    BangerDataLibrary lib(t.vfs);
    city::CityData c;
    c.info.mapName = "nowhere";
    city::Instance y;
    y.name = "sp_sign";
    y.flags = 0x0202; // banger, variant 2
    y.rotY = true;
    y.transform.m3 = {1, 0, 1};
    city::Instance full = y;
    full.rotY = false;
    full.flags = 0x0200;
    city::Instance plain = y;
    plain.name = "building"; // no banger data: not a prop
    c.aiInstances = {y, full, plain};
    const auto props = placeCityProps(c, t.vfs, lib);
    ASSERT_EQ(props.size(), 2u);
    EXPECT_FALSE(props[0].fullMatrix);
    EXPECT_EQ(props[0].variant, 2);
    EXPECT_TRUE(props[1].fullMatrix);
    EXPECT_EQ(props[1].variant, 0);
}

TEST(ParityBangers, YBangersTakeTheirCgFromThePlacedMatrix) {
    TempData t;
    BangerDataLibrary lib(t.vfs);
    TwoStoreyLevel level;
    phys::World world;
    world.setLevel(&level);
    BangerSet set(lib);
    set.setWorld(&world);
    // A street prop on a tilted sidewalk: +X rises. dgUnhitBangerInstance::
    // Init turns the CG (0.5, 1, 0) with this matrix before SetMatrix keeps
    // only the Y rotation.
    Mat34 m;
    m.m0 = Vec3{1, 0.2f, 0}.normalized();
    m.m1 = {0, 1, 0};
    m.m2 = m.m0.cross(m.m1);
    m.m3 = {0, 0, 0};
    PlacedProp p{"sp_lamp", m, 0, PlacedProp::Source::StreetRule, false, 1};
    set.add({p});
    ASSERT_EQ(set.instances().size(), 1u);
    const auto& inst = set.instances()[0];
    EXPECT_NEAR(inst.matrix.m3.x, m.m0.x * 0.5f, 1e-6f);
    EXPECT_NEAR(inst.matrix.m3.y, m.m0.y * 0.5f + 1.0f, 1e-6f);
    EXPECT_FLOAT_EQ(inst.matrix.m0.y, 0.0f); // Y rotation only
    EXPECT_EQ(inst.paint, 1);
    // cityLevel::LoadPath finds the room of the placement point, not the CG.
    EXPECT_EQ(inst.room, 1);
    set.setWorld(nullptr);
}

TEST(ParityBangersRetail, CityPropsComeInCityLevelLoadOrderWithTheRaceProps) {
    MM2_REQUIRE_GAME_DATA();
    const vfs::Vfs& v = *test::gameData();
    BangerDataLibrary lib(v);
    auto city = city::loadCity(v, "london");
    ASSERT_TRUE(city);
    const auto props = placeCityProps(*city, v, lib, racePropsName(game::GameMode::Circuit, 0));
    ASSERT_FALSE(props.empty());
    // Street rules, then the .inst bangers, then props.pathset, then the race.
    int last = 0, race = 0;
    for (const auto& p : props) {
        const int order = p.source == PlacedProp::Source::StreetRule ? 0
                          : p.source == PlacedProp::Source::Instance ? 1
                          : p.source == PlacedProp::Source::PathSet  ? 2
                                                                     : 3;
        EXPECT_GE(order, last);
        last = order;
        race += order == 3;
    }
    EXPECT_GT(race, 0); // race/london/circuit0.pathset has props with banger data
    EXPECT_EQ(props.front().source, PlacedProp::Source::StreetRule);
}

// parCsvFile::Load as cityPropulator reads the prop tables: 16 columns at
// most, '#' ends a line, blank lines are rows without cells, a cell ends at
// a comma or a control character and keeps its spaces.
TEST(ParityBangers, PropTablesReadLikeParCsvFile) {
    std::string header = "rulename";
    for (int i = 1; i <= 19; ++i)
        header += ",prop" + std::to_string(i);
    std::string row = "n01left";
    for (int i = 1; i <= 19; ++i)
        row += ",p" + std::to_string(i);
    const auto rules = parsePropRules(header + "\r\n\r\n" + row + "\r\nn02left,a,,b # c,d\r\nn03left,x\ty\r\n");
    ASSERT_EQ(rules.size(), 3u);
    EXPECT_EQ(rules[0].props.size(), 15u); // columns 16.. are dropped
    EXPECT_EQ(rules[0].props.back(), "p15");
    ASSERT_EQ(rules[1].props.size(), 2u); // "a", "" (skipped), "b " up to the '#'
    EXPECT_EQ(rules[1].props[1], "b ");
    ASSERT_EQ(rules[2].props.size(), 2u); // a tab ends a cell
    EXPECT_EQ(rules[2].props[1], "y");

    const auto defs = parsePropDefs("name,start,distance,maxUse,minLerp,maxLerp,file1,file2,file3,file4\n"
                                    "lamp, 9,29x,12.7,0.1,0.3,a,,c,\n");
    ASSERT_EQ(defs.size(), 1u);
    EXPECT_FLOAT_EQ(defs[0].start, 9.0f);     // atof skips the space
    EXPECT_FLOAT_EQ(defs[0].distance, 29.0f); // and stops at the 'x'
    EXPECT_EQ(defs[0].maxUse, 12);            // atoi stops at the '.'
    ASSERT_EQ(defs[0].files.size(), 3u);      // a, "", c; none after the final comma
    EXPECT_EQ(defs[0].files[1], "");
}

// lvlLevel::LoadInstances places each record's PKG xrefs as unhit bangers:
// full matrix (xref times record), the record's variant, its room as the
// hint; an xref without banger data (cl10's trees) is not placed.
TEST(ParityBangersRetail, PkgXrefsBecomeBangers) {
    MM2_REQUIRE_GAME_DATA();
    const vfs::Vfs& v = *test::gameData();
    // The xref reader agrees with the full PKG parser on every retail model
    // that has an xrefs chunk (33 of them).
    int withXrefs = 0;
    for (const auto& e : v.listFiles()) {
        if (!e.path.starts_with("geometry/") || !e.path.ends_with(".pkg") || asset::isKnownBrokenRetailAsset(e.path))
            continue;
        const std::string model = e.path.substr(9, e.path.size() - 13);
        const auto xrefs = pkgXrefs(v, model);
        if (xrefs.empty())
            continue;
        ++withXrefs;
        const auto bytes = v.readAll(e.path);
        ASSERT_TRUE(bytes);
        const auto pkg = asset::parsePkg(*bytes);
        ASSERT_TRUE(pkg) << model;
        ASSERT_EQ(pkg->xrefs.size(), xrefs.size()) << model;
        for (std::size_t i = 0; i < xrefs.size(); ++i) {
            EXPECT_EQ(pkg->xrefs[i].name, xrefs[i].name);
            EXPECT_EQ(pkg->xrefs[i].transform.m3.y, xrefs[i].transform.m3.y);
        }
    }
    EXPECT_EQ(withXrefs, 33);

    BangerDataLibrary lib(v);
    auto city = city::loadCity(v, "london");
    ASSERT_TRUE(city);
    const city::Instance* parliament = nullptr;
    for (const auto& inst : city->instances)
        if (inst.name == "wl_parliment_l")
            parliament = &inst;
    ASSERT_TRUE(parliament);
    const auto xrefs = pkgXrefs(v, "wl_parliment_l");
    ASSERT_EQ(xrefs.size(), 2u);
    const auto placed = placeXrefs(*parliament, xrefs, lib);
    ASSERT_EQ(placed.size(), 2u);
    for (std::size_t i = 0; i < placed.size(); ++i) {
        EXPECT_EQ(placed[i].model, "sp_light_red_f");
        EXPECT_TRUE(placed[i].fullMatrix);
        EXPECT_EQ(placed[i].room, 0);
        EXPECT_EQ(placed[i].roomHint, parliament->room);
        const Vec3 expect = parliament->transform.transform(xrefs[i].transform.m3);
        EXPECT_NEAR((placed[i].transform.m3 - expect).mag(), 0.0f, 1e-3f);
    }
    // The trees cl10 references have no banger data.
    city::Instance tree;
    tree.name = "cl10";
    EXPECT_TRUE(placeXrefs(tree, pkgXrefs(v, "cl10"), lib).empty());
}

namespace {

// Three rooms along x (1: x < 100, 2: 100..200, 3: beyond), each next to the
// following one.
class StripRooms final : public phys::Level {
public:
    int findRoom(const Vec3& p, int) const override { return p.x < 100.0f ? 1 : (p.x < 200.0f ? 2 : 3); }
    int touchedNeighbors(int*, int, int, const Vec3&, float) const override { return 0; }
    int neighbors(int* out, int max, int room) const override {
        int n = 0;
        if (room > 1 && n < max)
            out[n++] = room - 1;
        if (room < 3 && n < max)
            out[n++] = room + 1;
        return n;
    }
    void collect(const int*, int, const Vec3&, float, phys::LevelBound& out) const override { out.clear(); }
    void instances(int, std::vector<phys::Instance*>&) const override {}
};

} // namespace

// dgBangerActiveManager::Update declares a knocked-over prop by its
// CollisionType: 0x10 as a type-1 mover (all collisions), so
// dgPhysManager::Update detaches it outside the player's rooms
// (dgHitBangerInstance::Detach: it leaves its room and disappears); 0x40 as
// a type-2 mover, which is never detached.
TEST(ParityBangers, KnockedOverPropsAreDetachedOutsideTheActiveRooms) {
    TempData t;
    {
        std::ofstream f(t.dir / "tune" / "banger" / "sp_held.dgbangerdata");
        f << "type: a\ndgBangerData {\n  Size 0.4 2 0.4\n  Mass 20\n  CollisionPrim 1\n  CollisionType 64\n}\n";
    }
    vfs::Vfs files; // sees the file written after TempData mounted its folder
    files.mount(std::make_shared<vfs::DirectoryFs>(t.dir));
    BangerDataLibrary lib(files);
    for (const auto& [model, x, gone] : {std::tuple{"sp_lamp", 250.0f, true}, std::tuple{"sp_lamp", 150.0f, false},
                                         std::tuple{"sp_held", 250.0f, false}}) {
        StripRooms level;
        phys::World world;
        world.setLevel(&level);
        phys::Body player;
        player.place(Mat34::translation({0.0f, 10.0f, 0.0f}));
        player.declare(4, 0x1b);
        world.add(&player);
        BangerSet set(lib);
        set.setWorld(&world);
        const BangerData* d = lib.find(model);
        ASSERT_TRUE(d);
        set.ejectPart(*d, model, "", 0, Mat34::translation({x, 10.0f, 0.0f}), 0.0f);
        ASSERT_EQ(set.activeCount(), 1);
        set.update(1.0f / 60.0f); // declares the active for the next frame
        world.advanceFixed(1.0f / 60.0f);
        const auto& inst = set.instances().back();
        if (gone) {
            EXPECT_EQ(inst.state, BangerSet::State::Gone) << model << " at " << x;
            EXPECT_EQ(inst.room, 0);
            EXPECT_EQ(set.activeCount(), 0);
        } else {
            EXPECT_EQ(inst.state, BangerSet::State::Active) << model << " at " << x;
            EXPECT_EQ(set.activeCount(), 1);
        }
        set.setWorld(nullptr);
        world.remove(&player);
    }
}
