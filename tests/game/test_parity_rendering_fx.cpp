// Parity checks for the rendering and effects code against MM2's own
// (midtown2.exe build 3393, see docs/parity/rendering-fx.md).

#include "TestData.h"
#include "asset/Ped.h"
#include "city/CityData.h"
#include "city/SdlDraw.h"
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
    // retail wall has one before it (that one takes entry 0, which Draw
    // starts each room with), and the angle word fits the 64-entry table.
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

TEST(ParityRenderingFx, SdlArcMapRunsBackAndForth) {
    // sdlPage16::ArcMap: the distance along the first vertices, scaled to a
    // whole number of repeats of the average width (6 over 8 m here), added
    // while the running value is not positive and subtracted while it is.
    city::Psdl psdl;
    psdl.vertices = {{0, 0, 0}, {2, 0, 0}, {0, 0, 4}, {2, 0, 4}, {0, 0, 8}, {2, 0, 8}};
    const std::vector<std::uint16_t> strip = {0, 1, 2, 3, 4, 5};
    const auto s = city::sdlArcMap(psdl, strip, 2, 3, 1);
    ASSERT_EQ(s.size(), 3u);
    EXPECT_FLOAT_EQ(s[0], 0.0f);
    EXPECT_FLOAT_EQ(s[1], 3.0f);
    EXPECT_FLOAT_EQ(s[2], 0.0f);
}

TEST(ParityRenderingFx, SdlLevelOfDetailAndBackface) {
    // cityLevel::DrawRooms: beyond 300 m level 0, 100 m level 1, 50 m level 2.
    EXPECT_EQ(city::sdlRoomLod(301.0f), 0);
    EXPECT_EQ(city::sdlRoomLod(300.0f), 1);
    EXPECT_EQ(city::sdlRoomLod(100.0f), 2);
    EXPECT_EQ(city::sdlRoomLod(50.0f), 3);
    EXPECT_EQ(city::sdlRoomLod(-20.0f), 3);
    // sdlCommon::BACKFACE: a wall from a to b faces (b - a) x up.
    EXPECT_FALSE(city::sdlBackface({0.5f, 0, 5}, {0, 0, 0}, {1, 0, 0}));
    EXPECT_TRUE(city::sdlBackface({0.5f, 0, -5}, {0, 0, 0}, {1, 0, 0}));
}

TEST(ParityRenderingFx, SdlDrawLevelsOfDetail) {
    // sdlPage16::Draw: only the top level draws the half-bright curb faces,
    // and the lower levels draw less.
    MM2_REQUIRE_GAME_DATA();
    auto city = city::loadCity(*test::gameData(), "london");
    ASSERT_TRUE(city);
    std::array<std::size_t, 4> triangles{};
    std::size_t halfBright = 0, halfBrightBelowTop = 0;
    for (std::size_t r = 1; r < city->psdl.rooms.size(); ++r) {
        const auto draw = city::buildSdlRoomDraw(city->psdl, r);
        for (std::size_t l = 0; l < 4; ++l)
            for (const auto& p : draw.lods[l]) {
                triangles[l] += p.indexCount / 3;
                if (p.shade == city::SdlShade::HalfRoom)
                    ++(l == 3 ? halfBright : halfBrightBelowTop);
            }
    }
    EXPECT_GT(halfBright, 1000u);
    EXPECT_GT(halfBrightBelowTop, 0u); // raised dividers' walls at level 2
    EXPECT_LT(triangles[0], triangles[1]);
    EXPECT_LE(triangles[1], triangles[2]);
    EXPECT_LT(triangles[2], triangles[3]);
}

TEST(ParityRenderingFx, PedestrianMeshesFaceOutCounterClockwise) {
    // modModel::Draw runs under the default culling, so the pedestrians'
    // triangles must face out with OpenMM2's counter-clockwise convention:
    // nearly every bind-pose triangle's winding agrees with its normals.
    MM2_REQUIRE_GAME_DATA();
    const auto& vfs = *test::gameData();
    auto read = [&](std::string_view path) { return vfs.readAll(path); };
    for (const char* name : {"pedmodel_man", "pedmodel_woman"}) {
        auto type = asset::loadPedType(name, read);
        ASSERT_TRUE(type) << name;
        std::vector<Mat34> bones;
        asset::posePed(type->skeleton, nullptr, 0.0f, bones);
        const auto& mesh = type->mesh;
        auto world = [&](std::size_t i, bool normal) {
            const auto& v = mesh.vertices[i];
            const Mat34 b = v.bone < bones.size() ? bones[v.bone] : Mat34::identity();
            return normal ? b.transformDir(v.normal) : b.transform(v.position);
        };
        int agree = 0, total = 0;
        for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
            const auto a = mesh.indices[t], b = mesh.indices[t + 1], c = mesh.indices[t + 2];
            const Vec3 n = (world(b, false) - world(a, false)).cross(world(c, false) - world(a, false));
            if (n.mag2() < 1e-12f)
                continue;
            ++total;
            agree += n.dot(world(a, true) + world(b, true) + world(c, true)) > 0.0f;
        }
        EXPECT_GT(total, 100) << name;
        EXPECT_GT(agree, total * 9 / 10) << name;
    }
}
