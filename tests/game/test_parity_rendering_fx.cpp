// Parity checks for the rendering and effects code against MM2's own
// (midtown2.exe build 3393, see docs/parity/rendering-fx.md).

#include "game/CityRenderer.h"
#include "game/MeshDraw.h"
#include "game/fx/LineSparks.h"
#include "game/fx/Particles.h"
#include "game/fx/Shards.h"

#include <gtest/gtest.h>

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
