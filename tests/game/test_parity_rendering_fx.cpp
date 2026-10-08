// Parity checks for the rendering and effects code against MM2's own
// (midtown2.exe build 3393, see docs/parity/rendering-fx.md).

#include "TestData.h"
#include "city/CityData.h"
#include "game/CityLevel.h"
#include "game/CityRenderer.h"
#include "game/MeshDraw.h"
#include "game/fx/LineSparks.h"
#include "game/fx/Particles.h"
#include "game/fx/Shards.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace mm2;
using namespace mm2::game;

TEST(ParityRenderingFx, ShardMaterialsCycleLikeFxShardManagerDraw) {
    // Shaders are taken in turn and start again at 0 after 16 / count of
    // them; with more than 16 materials shard i shows material i.
    for (std::size_t i = 0; i < 16; ++i) {
        EXPECT_EQ(fx::Shards::materialFor(i, 16), 0u);
        EXPECT_EQ(fx::Shards::materialFor(i, 10), 0u);
        EXPECT_EQ(fx::Shards::materialFor(i, 8), i % 2);
        EXPECT_EQ(fx::Shards::materialFor(i, 4), i % 4);
        EXPECT_EQ(fx::Shards::materialFor(i, 18), i);
    }
}

TEST(ParityRenderingFx, SparkAxesFollowRadialBlast) {
    // asLineSparks::RadialBlast: t = normal x Y, b = t x normal. For a
    // normal along +Z, t is -X and b is +Y, so a spark born off to +X flies
    // towards -X and one born off to +Z flies upwards.
    fx::LineSparks sparks;
    const Vec3 origin{10, 0, 0};
    sparks.radialBlast(64, origin, {0, 0, 1});
    ASSERT_EQ(sparks.count(), 64);
    int checked = 0;
    for (const auto& s : sparks.sparks()) {
        const Vec3 local = s.position - origin;
        if (std::abs(local.x) > 0.005f) {
            EXPECT_LT(s.velocity.x * local.x, 0.0f);
            ++checked;
        }
        if (std::abs(local.z) > 0.005f) {
            EXPECT_GT(s.velocity.y * local.z, 0.0f);
            ++checked;
        }
    }
    EXPECT_GT(checked, 32);
}

TEST(ParityRenderingFx, ParticleResetDropsTheBirthMatrix) {
    // asParticles::Reset clears the matrix Blast transforms births with.
    fx::BirthRule rule;
    fx::ParticleSystem s;
    s.init(4, 1, 1);
    s.setBirthRule(&rule);
    const Mat34 m = Mat34::translation({100, 0, 0});
    s.setMatrix(&m);
    s.blast(1);
    EXPECT_NEAR(s.positions()[0].position.x, 100.0f, 1.0f);
    s.reset();
    s.blast(1);
    ASSERT_EQ(s.count(), 1);
    EXPECT_NEAR(s.positions()[0].position.x, 0.0f, 1.0f);
}

TEST(ParityRenderingFx, SdlMovieFrameNamesUseTheBaseName) {
    // lvlSDL::LoadBinary strips "-0NNN" so gfxGetTextureMovie plays them.
    EXPECT_EQ(sdlTextureName("s_thames-0009"), "s_thames");
    EXPECT_EQ(sdlTextureName("s_ocean-0001"), "s_ocean");
    EXPECT_EQ(sdlTextureName("cw_apt-1234"), "cw_apt-1234");
    EXPECT_EQ(sdlTextureName("r2_f"), "r2_f");
}

TEST(ParityRenderingFx, EveryWallHasAFacadeBoundLight) {
    // sdlPage16::Draw shades facades and slivers with the wall light table
    // entry the room's preceding FacadeBound attribute names. All but one
    // retail wall has one before it (MM2 would use whatever its local held;
    // CityRenderer takes the wall's facing), and the angle word fits the
    // 64-entry table.
    MM2_REQUIRE_GAME_DATA();
    for (const char* name : {"london", "sf"}) {
        auto city = city::loadCity(*test::gameData(), name);
        ASSERT_TRUE(city) << name;
        int walls = 0, unlit = 0;
        for (const auto& room : city->psdl.rooms) {
            int angle = -1;
            for (const auto& a : room.attributes) {
                if (a.type == city::PsdlAttrType::FacadeBound) {
                    angle = a.facadeBoundAngle();
                    EXPECT_LT(angle, 64) << name;
                } else if (a.type == city::PsdlAttrType::Facade || a.type == city::PsdlAttrType::Sliver) {
                    ++walls;
                    unlit += angle < 0;
                }
            }
        }
        EXPECT_GT(walls, 1000) << name;
        EXPECT_LE(unlit, 1) << name;
    }
}

TEST(ParityRenderingFx, ObjectDetailLevels) {
    // cityLevel::SetObjectDetail.
    const auto l0 = ObjectDetail::forLevel(0);
    EXPECT_FLOAT_EQ(l0.med, 20.0f);
    EXPECT_FLOAT_EQ(l0.noDraw, 200.0f);
    const auto l3 = ObjectDetail::forLevel(3);
    EXPECT_FLOAT_EQ(l3.med, 70.0f);
    EXPECT_FLOAT_EQ(l3.low, 130.0f);
    // lvlInstance::IsVisible: strictly beyond each threshold.
    const ObjectDetail d = ObjectDetail::forLevel(2);
    EXPECT_EQ(objectLod(41.0f, 1.0f, d), asset::Lod::High);
    EXPECT_EQ(objectLod(41.5f, 1.0f, d), asset::Lod::Medium);
    EXPECT_EQ(objectLod(301.0f, 0.0f, d, d.noDraw), std::nullopt);
}

TEST(ParityRenderingFx, MultiRoomInstancesLiveInTheNeighboursTheyReach) {
    // lvlMultiRoomInstance::Create: a collidable (.inst flag 0x2000) object
    // is listed in the neighbours of its room that its sphere reaches, never
    // in its own room; terrain-local ones (0x100) only in their own room.
    MM2_REQUIRE_GAME_DATA();
    auto city = city::loadCity(*test::gameData(), "london");
    ASSERT_TRUE(city);
    CityLevel level(*city, *test::gameData(), {});
    std::vector<phys::Instance*> list;
    int multi = 0, terrain = 0;
    for (const auto& inst : city->instances) {
        if (inst.room == 0 || (inst.flags & 0x200) || !(inst.flags & 0x2100))
            continue;
        list.clear();
        level.instances(inst.room, list);
        bool inOwn = false;
        for (const phys::Instance* i : list) {
            const auto* si = dynamic_cast<const StaticInstance*>(i);
            if (si && si->name == inst.name && si->position().dist2(inst.transform.m3) < 1e-6f)
                inOwn = true;
        }
        if (inst.flags & 0x100) {
            EXPECT_TRUE(inOwn) << inst.name;
            ++terrain;
        } else {
            EXPECT_FALSE(inOwn) << inst.name;
            ++multi;
        }
    }
    EXPECT_GT(multi, 0);
    EXPECT_GT(terrain, 0);
}
