// Parity checks for the city's loading and drawing against MM2's cityLevel,
// lvlLevel and lvlInstance (midtown2.exe build 3393, see
// docs/parity/mm2/city-render.md).

#include "TestData.h"
#include "asset/Ped.h"
#include "asset/Pkg.h"
#include "city/CityData.h"
#include "core/StringUtil.h"
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
