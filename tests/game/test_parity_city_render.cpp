// Parity checks for the city's loading and drawing against MM2's cityLevel,
// lvlLevel and lvlInstance (midtown2.exe build 3393, see
// docs/parity/mm2/city-render.md).

#include "TestData.h"
#include "asset/Ped.h"
#include "asset/Pkg.h"
#include "city/CityData.h"
#include "city/RoomLocator.h"
#include "city/SdlDraw.h"
#include "core/StringUtil.h"
#include "game/CityLevel.h"
#include "game/CityRenderer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>

using namespace mm2;
using namespace mm2::game;

TEST(ParityCityRender, InstanceRecordsBecomeLoadInstancesKinds) {
    // lvlLevel::LoadInstances: the banger bit first, then terrain local
    // (lvlLandmark), then collidable (lvlMultiRoomInstance::Create), else a
    // plain lvlFixedMatrix. The low byte is the variant.
    EXPECT_EQ(staticKind(0x0000), StaticKind::Fixed);
    EXPECT_EQ(staticKind(0x0005), StaticKind::Fixed);
    EXPECT_EQ(staticKind(0x0100), StaticKind::Landmark);
    EXPECT_EQ(staticKind(0x2100), StaticKind::Landmark);
    EXPECT_EQ(staticKind(0x0500), StaticKind::Landmark);
    EXPECT_EQ(staticKind(0x2000), StaticKind::MultiRoom);
    EXPECT_EQ(staticKind(0x0200), StaticKind::Banger);
    EXPECT_EQ(staticKind(0x2300), StaticKind::Banger);
    EXPECT_EQ(staticVariant(0x0003), 3);
    EXPECT_EQ(staticVariant(0x2105), 5);
}

TEST(ParityCityRender, FixedObjectsAreSkippedFromBehind) {
    // lvlFixedMatrix::IsVisible: hidden while (eye - origin) . Z < 0 in the
    // ground plane; the height does not matter.
    Mat34 m = Mat34::identity();
    m.m3 = {10, 5, 20};
    EXPECT_FALSE(fixedObjectFacesAway(m, {10, 0, 25}));
    EXPECT_TRUE(fixedObjectFacesAway(m, {10, 50, 15}));
    EXPECT_FALSE(fixedObjectFacesAway(m, {30, 5, 20})); // in the plane: drawn
    // The compact (lvlFixedRotY record) form: X = (x, 0, z), Z = (-z, 0, x).
    Mat34 r = Mat34::identity();
    r.m0 = {0, 0, 1};
    r.m2 = {-1, 0, 0};
    r.m3 = {0, 0, 0};
    EXPECT_TRUE(fixedObjectFacesAway(r, {2, 0, 0}));
    EXPECT_FALSE(fixedObjectFacesAway(r, {-2, 0, 0}));
}

TEST(ParityCityRender, PedestrianRootsAreNormalisedLikePedAnimationLoad) {
    // crAnimation::Normalize adds i * distance / frames to the root z, then
    // each row of the table takes frame 0's x/z and the straight line to
    // frame m = last - first out of frames 0..m.
    asset::PedType type;
    asset::PedAnimation a;
    a.frameCount = 5;
    a.channelCount = 3;
    a.cycleDistance = 2.0f;
    for (int i = 0; i < 5; ++i)
        for (float v : {1.0f + 0.5f * static_cast<float>(i), 7.0f, -3.0f - 0.4f * static_cast<float>(i)})
            a.channels.push_back(v);
    type.animations.emplace("pedanim_test", a);
    asset::PedAnimState s;
    s.name = "TEST";
    s.animFile = "pedanim_Test";
    s.firstFrame = 1;
    s.lastFrame = 4; // m = 3: frame 4 is left alone
    type.table.states.push_back(s);
    asset::normalizePedRoots(type);
    const auto& out = type.animations.at("pedanim_test");
    // After Normalize: z_i = -3 - 0.4 i + 0.4 i = -3 for every frame.
    for (std::uint32_t i = 0; i <= 3; ++i) {
        EXPECT_NEAR(out.rootTranslation(i).x, 0.0f, 1e-5f) << i; // x: a straight line, removed
        EXPECT_NEAR(out.rootTranslation(i).z, 0.0f, 1e-5f) << i;
        EXPECT_FLOAT_EQ(out.rootTranslation(i).y, 7.0f);
    }
    EXPECT_FLOAT_EQ(out.rootTranslation(4).x, 3.0f);
    EXPECT_NEAR(out.rootTranslation(4).z, -3.0f, 1e-5f);
}

TEST(ParityCityRender, RetailPedestrianSequencesStartAtTheOrigin) {
    // With the retail tables every sequence's first and last posed frame has
    // its root at the pedestrian's origin in x and z (the dives' raw data
    // start 2.2 m to the side, the run-to-walk 3.1 m ahead).
    MM2_REQUIRE_GAME_DATA();
    const auto& vfs = *test::gameData();
    auto read = [&](std::string_view p) { return vfs.readAll(p); };
    int rows = 0;
    for (const char* name : {"pedmodel_man", "pedmodel_woman"}) {
        auto type = asset::loadPedType(name, read);
        ASSERT_TRUE(type) << name;
        asset::normalizePedRoots(*type);
        for (const auto& s : type->table.states) {
            const auto* anim = type->animation(s.animFile);
            if (!anim)
                continue;
            const int m = std::min(s.lastFrame - s.firstFrame, static_cast<int>(anim->frameCount) - 1);
            for (int f : {0, m}) {
                EXPECT_NEAR(anim->rootTranslation(static_cast<std::uint32_t>(f)).x, 0.0f, 1e-4f) << s.name << " " << f;
                EXPECT_NEAR(anim->rootTranslation(static_cast<std::uint32_t>(f)).z, 0.0f, 1e-4f) << s.name << " " << f;
            }
            ++rows;
        }
    }
    EXPECT_GT(rows, 40);
}

TEST(ParityCityRender, RoomListsPutMoversFirstAndStaticsNewestFirst) {
    // lvlLevel::MoveToRoom: movable instances at the head of a room's list,
    // static ones (flag 0x400) after them, each before the earlier ones.
    MM2_REQUIRE_GAME_DATA();
    auto city = city::loadCity(*test::gameData(), "london");
    ASSERT_TRUE(city);
    CityLevel level(*city, *test::gameData(), {});
    struct Marker final : InstanceSource {
        void instancesIn(int, std::vector<phys::Instance*>& out) const override { out.push_back(nullptr); }
    } marker;
    level.addSource(&marker);
    // Load order of the collidable records, by name and position.
    auto order = [&](const StaticInstance& si) {
        for (std::size_t i = 0; i < city->instances.size(); ++i)
            if (city->instances[i].name == si.name &&
                city->instances[i].transform.m3.dist2(si.matrix().m3) < 1e-4f)
                return static_cast<int>(i);
        return -1;
    };
    int checked = 0;
    std::vector<phys::Instance*> list;
    for (int room = 1; room < static_cast<int>(city->psdl.rooms.size()); ++room) {
        list.clear();
        level.instances(room, list);
        ASSERT_FALSE(list.empty());
        EXPECT_EQ(list.front(), nullptr); // the source's instance comes first
        int previous = -1;
        for (std::size_t k = 1; k < list.size(); ++k) {
            const auto* si = dynamic_cast<const StaticInstance*>(list[k]);
            ASSERT_NE(si, nullptr);
            const int o = order(*si);
            if (previous >= 0 && o >= 0) {
                EXPECT_LT(o, previous) << "room " << room;
                ++checked;
            }
            if (o >= 0)
                previous = o;
        }
    }
    level.removeSource(&marker);
    EXPECT_GT(checked, 0);
}

TEST(ParityCityRender, DynamicObjectsAreDrawnFromListedRooms) {
    // cityLevel::DrawRooms: an object of a listed room whose distance is at
    // most NoDraw is drawn (cityLevel_drawObjects); its shadow and glows need
    // the distance under NoDraw (cityLevel_drawShadows / _drawLights); an
    // unlisted room draws nothing. Before any city view, everything passes.
    RoomVisibility v;
    EXPECT_TRUE(v.passes(3).objects);
    EXPECT_TRUE(v.passes(3).shadowsAndGlows);
    v.begin(nullptr, 6, 300.0f);
    v.list(1, -20.0f); // the camera's room: minus its radius
    v.list(2, 120.0f);
    v.list(3, 300.0f);
    v.list(4, 450.0f);
    EXPECT_TRUE(v.passes(1).objects && v.passes(1).shadowsAndGlows);
    EXPECT_TRUE(v.passes(2).objects && v.passes(2).shadowsAndGlows);
    EXPECT_TRUE(v.passes(3).objects);
    EXPECT_FALSE(v.passes(3).shadowsAndGlows);
    EXPECT_FALSE(v.passes(4).objects || v.passes(4).shadowsAndGlows);
    EXPECT_FALSE(v.passes(5).objects || v.passes(5).shadowsAndGlows); // not listed
    EXPECT_FALSE(v.passes(0).objects);                                // outside every room
    EXPECT_FALSE(v.passes(99).objects);
    EXPECT_EQ(v.findRoom({0, 0, 0}, 4), 0); // no locator
}

TEST(ParityCityRender, RetailObjectsKeepTheirRoomsLikeFindRoomId) {
    // vehCar::Update, aiPedestrian::Update: FindRoomId from the last room.
    MM2_REQUIRE_GAME_DATA();
    auto city = city::loadCity(*test::gameData(), "sf");
    ASSERT_TRUE(city);
    const city::RoomLocator locator(city->psdl, city->info.mapName);
    RoomVisibility v;
    v.begin(&locator, city->psdl.rooms.size(), 300.0f);
    int checked = 0;
    for (const auto& inst : city->instances) {
        if (inst.room == 0 || (inst.flags & 0x2300))
            continue;
        const Vec3 p = city::sdlRoomCentroid(city->psdl, inst.room);
        EXPECT_EQ(v.findRoom(p, inst.room), locator.find(p, inst.room));
        if (++checked == 50)
            break;
    }
    EXPECT_EQ(checked, 50);
}

TEST(ParityCityRender, RetailFacadesUseTheirRecordVariant) {
    // SF's storefront facades share models with up to 9 shader sets; the
    // record's low byte picks one (lvlFixedAny::SetVariant), so the same
    // model shows different shop fronts.
    MM2_REQUIRE_GAME_DATA();
    const auto& vfs = *test::gameData();
    auto city = city::loadCity(vfs, "sf");
    ASSERT_TRUE(city);
    std::map<std::string, std::optional<asset::Pkg>> pkgs;
    int variants = 0, differing = 0;
    for (const auto& inst : city->instances) {
        if (staticKind(inst.flags) == StaticKind::Banger || staticVariant(inst.flags) == 0)
            continue;
        auto it = pkgs.find(inst.name);
        if (it == pkgs.end()) {
            std::optional<asset::Pkg> pkg;
            if (auto bytes = vfs.readAll("geometry/" + str::lower(inst.name) + ".pkg"))
                pkg = asset::parsePkg(*bytes);
            it = pkgs.emplace(inst.name, std::move(pkg)).first;
        }
        if (!it->second || it->second->paintjobs.size() < 2)
            continue;
        ++variants;
        const auto& sets = it->second->paintjobs;
        const auto& chosen = sets[static_cast<std::size_t>(staticVariant(inst.flags)) % sets.size()];
        for (std::size_t i = 0; i < chosen.size() && i < sets[0].size(); ++i)
            if (chosen[i].texture != sets[0][i].texture) {
                ++differing;
                break;
            }
    }
    EXPECT_GT(variants, 2000); // 2657 in retail SF
    EXPECT_GT(differing, 1000);
}
