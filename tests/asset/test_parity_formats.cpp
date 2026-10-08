// Format readers checked against MM2's loaders (docs/parity/formats.md).
#include "AssetTestUtil.h"
#include "asset/Image.h"
#include "asset/Mtx.h"
#include "asset/Ped.h"
#include "asset/Pkg.h"
#include "asset/VehicleModel.h"

#include <gtest/gtest.h>

#include <map>

using namespace mm2;

namespace {

Bytes texHeader(std::uint16_t w, std::uint16_t h, std::uint16_t fmt, std::uint16_t mips) {
    Bytes b;
    b.u16(w).u16(h).u16(fmt).u16(mips).u16(1).u32(0);
    return b;
}

Bytes palette(std::size_t entries, std::uint8_t alpha) {
    Bytes b;
    for (std::size_t i = 0; i < entries; ++i)
        b.u8(static_cast<std::uint8_t>(i)).u8(0x20).u8(0x30).u8(alpha); // B,G,R,A
    return b;
}

} // namespace

// gfxLoadTexImage builds an RGB888 image for P8 and P4: the palette's alpha is dropped.
TEST(ParityTex, PalettedOpaqueFormatsIgnorePaletteAlpha) {
    Bytes p8 = texHeader(2, 1, 1, 1);
    p8.append(palette(256, 0x10)).u8(5).u8(6);
    auto t = asset::parseTex(p8.data);
    ASSERT_TRUE(t);
    EXPECT_EQ(t->image.levels[0].rgba, (std::vector<std::uint8_t>{0x30, 0x20, 5, 255, 0x30, 0x20, 6, 255}));
    EXPECT_FALSE(t->image.alphaFormat);

    Bytes pa8 = texHeader(1, 1, 14, 1);
    pa8.append(palette(256, 0x10)).u8(7);
    auto ta = asset::parseTex(pa8.data);
    ASSERT_TRUE(ta);
    EXPECT_EQ(ta->image.levels[0].rgba[3], 0x10);
    EXPECT_TRUE(ta->image.alphaFormat);
}

TEST(ParityTex, P8A8AndArgb1555) {
    Bytes p8a8 = texHeader(1, 1, 2, 1);
    p8a8.append(palette(256, 0xFF)).u8(9).u8(0x40); // index 9, alpha 0x40
    auto a = asset::parseTex(p8a8.data);
    ASSERT_TRUE(a);
    EXPECT_EQ(a->image.levels[0].rgba, (std::vector<std::uint8_t>{0x30, 0x20, 9, 0x40}));

    Bytes argb = texHeader(2, 1, 6, 1);
    argb.u16(0x8000 | (31 << 10)).u16(31); // opaque red, transparent blue
    auto b = asset::parseTex(argb.data);
    ASSERT_TRUE(b);
    EXPECT_EQ(b->image.levels[0].rgba, (std::vector<std::uint8_t>{255, 0, 0, 255, 0, 0, 255, 0}));
    EXPECT_TRUE(b->image.alphaFormat);
}

// texImage_CheckRes: both sides must be powers of two.
TEST(ParityTex, RejectsSidesThatAreNotPowersOfTwo) {
    Bytes b = texHeader(3, 4, 18, 1);
    for (int i = 0; i < 12 * 4; ++i)
        b.u8(0);
    EXPECT_FALSE(asset::parseTex(b.data));
}

// gfxImage::Create adds a level only while both sides are above 1; a mip
// count of 0 means the whole chain.
TEST(ParityTex, MipChainStopsAtASideOfOne) {
    Bytes b = texHeader(4, 2, 17, 5);
    for (int i = 0; i < (8 + 2 + 1) * 3; ++i) // more data than MM2 reads
        b.u8(static_cast<std::uint8_t>(i));
    auto t = asset::parseTex(b.data);
    ASSERT_TRUE(t);
    ASSERT_EQ(t->image.levels.size(), 2u); // 4x2, 2x1
    EXPECT_EQ(t->image.levels[1].width, 2u);
    EXPECT_EQ(t->image.levels[1].height, 1u);

    Bytes all = texHeader(4, 4, 17, 0);
    for (int i = 0; i < (16 + 4 + 1) * 3; ++i)
        all.u8(0);
    auto full = asset::parseTex(all.data);
    ASSERT_TRUE(full);
    EXPECT_EQ(full->image.levels.size(), 3u);
}

// The 4-bit formats read w*h/2 bytes per level: a 1x1 level has none.
TEST(ParityTex, FourBitLevelsRoundDown) {
    Bytes b = texHeader(2, 2, 15, 2);
    b.append(palette(16, 0x00)).u8(0x21).u8(0x43); // 2x2: indices 1,2,3,4; 1x1: no data
    auto t = asset::parseTex(b.data);
    ASSERT_TRUE(t);
    ASSERT_EQ(t->image.levels.size(), 2u);
    EXPECT_EQ(t->image.levels[0].rgba[2], 1); // low nibble first
    EXPECT_EQ(t->image.levels[0].rgba[6], 2);
    EXPECT_EQ(t->image.levels[0].rgba[3], 255); // P4 is opaque
}

namespace {

Bytes chunk3(std::string_view name, const Bytes& body) {
    Bytes b;
    b.str("FILE").u8(static_cast<std::uint8_t>(name.size() + 1)).str(name).u8(0);
    b.u32(static_cast<std::uint32_t>(body.size())).append(body);
    return b;
}

} // namespace

// modGetStatic: u32 packet count, byte shader index, red/blue swapped vertex colour.
TEST(ParityPkg, GeometryChunkAsModGetStaticReadsIt) {
    Bytes g;
    g.u32(1).u32(3).u32(3).u32(1).u32(0x142); // xyz | diffuse | tex1
    g.u32(1).u32(0x105);                      // one packet, shader 0x105 -> 5
    g.u32(3).u32(3);
    for (int i = 0; i < 3; ++i)
        g.f32(static_cast<float>(i)).f32(0).f32(0).u32(0x80332211u).f32(0).f32(0);
    g.u32(3).u16(0).u16(1).u16(2);
    Bytes file;
    file.str("PKG3").append(chunk3("H", g));
    std::string err;
    auto pkg = asset::parsePkg(file.data, &err);
    ASSERT_TRUE(pkg) << err;
    const auto& section = pkg->meshes[0].sections[0];
    EXPECT_EQ(section.shaderIndex, 5u);
    EXPECT_EQ(section.packets[0].vertices[0].color, 0x80112233u);
}

// Bit 0x80 of the section count: no geometry, one shader byte per section.
TEST(ParityPkg, ShaderOnlyGeometryChunk) {
    Bytes g;
    g.u32(0x82).u32(0x102).u8(3).u8(4);
    Bytes file;
    file.str("PKG3").append(chunk3("BODY_H", g));
    std::string err;
    auto pkg = asset::parsePkg(file.data, &err);
    ASSERT_TRUE(pkg) << err;
    ASSERT_EQ(pkg->meshes[0].sections.size(), 2u);
    EXPECT_EQ(pkg->meshes[0].sections[1].shaderIndex, 4u);
    EXPECT_TRUE(pkg->meshes[0].sections[1].packets.empty());
}

// modShader::Load.
TEST(ParityPkg, MaterialsAsModShaderLoadBuildsThem) {
    Bytes full;
    full.u32(1).u32(1).u8(0);
    for (float f : {0.3f, 0.04f, 0.96f, 1.0f})
        full.f32(f); // diffuse
    for (int i = 0; i < 4; ++i)
        full.f32(0.7f); // ambient: replaced by diffuse
    for (float f : {0.9f, 0.5f, 0.0f, 1.0f})
        full.f32(f); // specular
    for (int i = 0; i < 4; ++i)
        full.f32(0.2f); // emissive
    full.f32(0.25f);
    auto table = asset::parseShaderTable(full.data);
    ASSERT_TRUE(table);
    const auto& m = table->paintjobs[0][0];
    EXPECT_FLOAT_EQ(m.diffuse.x, 0.28125f); // floor(0.3 * 32) / 32
    EXPECT_FLOAT_EQ(m.diffuse.y, 0.0f);     // below 0.05
    EXPECT_FLOAT_EQ(m.diffuse.z, 1.0f);     // above 0.95
    EXPECT_EQ(m.ambient.x, m.diffuse.x);
    EXPECT_FLOAT_EQ(m.specular.x, 0.875f);
    EXPECT_FLOAT_EQ(m.emissive.x, 0.1875f);
    EXPECT_FLOAT_EQ(m.shininess, 0.25f);

    // Compact: diffuse, specular, emissive bytes; no rounding.
    Bytes compact;
    compact.u32(0x81).u32(1).u8(0);
    compact.u8(1).u8(2).u8(3).u8(255).u8(10).u8(20).u8(30).u8(40).u8(100).u8(110).u8(120).u8(130).f32(0.0f);
    auto c = asset::parseShaderTable(compact.data);
    ASSERT_TRUE(c);
    const auto& cm = c->paintjobs[0][0];
    EXPECT_FLOAT_EQ(cm.diffuse.x, 1 * 0.003921569f);
    EXPECT_FLOAT_EQ(cm.specular.y, 20 * 0.003921569f);
    EXPECT_FLOAT_EQ(cm.emissive.z, 120 * 0.003921569f);
    EXPECT_EQ(cm.ambient.x, cm.diffuse.x);
}

// lvlInstance::GetGeomSet fills a missing level from a less detailed one only.
TEST(ParityPkg, MissingLodTakesTheNextLessDetailedOne) {
    Bytes g;
    g.u32(1).u32(3).u32(3).u32(1).u32(0x002);
    g.u32(1).u32(0).u32(3).u32(3);
    for (int i = 0; i < 9; ++i)
        g.f32(0);
    g.u32(3).u16(0).u16(1).u16(2);
    Bytes file;
    file.str("PKG3").append(chunk3("BODY_H", g)).append(chunk3("BODY_L", g));
    auto pkg = asset::parsePkg(file.data);
    ASSERT_TRUE(pkg);
    EXPECT_EQ(pkg->findBest("BODY", asset::Lod::Medium), pkg->find("BODY_L"));
    EXPECT_EQ(pkg->findBest("BODY", asset::Lod::VeryLow), nullptr);
}

// vehWheel::Init takes the radius as |max.y - min.y| / 2.
TEST(ParityPivots, WheelRadiusIsAbsolute) {
    std::map<std::string, std::vector<std::byte>, std::less<>> files;
    Bytes g;
    g.u32(1).u32(3).u32(3).u32(1).u32(0x002);
    g.u32(1).u32(0).u32(3).u32(3);
    for (int i = 0; i < 9; ++i)
        g.f32(0);
    g.u32(3).u16(0).u16(1).u16(2);
    Bytes pkg;
    pkg.str("PKG3").append(chunk3("WHL0_H", g));
    files["geometry/car.pkg"] = pkg.data;
    Bytes mtx;
    for (float f : {-0.1f, 0.4f, -0.4f, 0.1f, -0.2f, 0.4f, 0.0f, 0.0f, 0.0f, 1.0f, 0.3f, -1.0f})
        mtx.f32(f); // min.y above max.y
    files["geometry/car_whl0.mtx"] = mtx.data;
    const asset::ReadFileFn read = [&](std::string_view p) -> std::optional<std::vector<std::byte>> {
        auto it = files.find(p);
        if (it == files.end())
            return std::nullopt;
        return it->second;
    };
    auto model = asset::loadVehicleModel("car", read);
    ASSERT_TRUE(model);
    ASSERT_TRUE(model->wheel(0));
    EXPECT_FLOAT_EQ(model->wheel(0)->radius, 0.3f);
    EXPECT_FLOAT_EQ(model->wheel(0)->width, 0.2f);
}

// crAnimation::LoadAnim: a nonzero first word is the old layout's frame count
// and the channel word is a bone count.
TEST(ParityPed, OldAnimationLayout) {
    Bytes b;
    b.u32(2).u32(1).f32(0).u8(1); // 2 frames, 1 bone -> 6 channels
    for (int i = 0; i < 12; ++i)
        b.f32(static_cast<float>(i));
    b.u32(0xDEADBEEF); // trailing bytes are ignored
    std::string err;
    auto a = asset::parsePedAnimation(b.data, &err);
    ASSERT_TRUE(a) << err;
    EXPECT_EQ(a->frameCount, 2u);
    EXPECT_EQ(a->channelCount, 6u);
    EXPECT_FLOAT_EQ(a->boneRotation(1, 0).z, 11.0f);

    Bytes tooMany;
    tooMany.u32(0).u32(10001).u32(6).f32(0).u8(1);
    EXPECT_FALSE(asset::parsePedAnimation(tooMany.data));
}

TEST(ParityPed, SkeletonLimitsAndNumberPrefixes) {
    auto skel = asset::parseSkeleton("NumBones 1\nbone root {\n offset 1.5f 2 -0.5\n rotmin -1 -2 -3\n"
                                     " rotmax 1 2 3\n}\n");
    ASSERT_TRUE(skel);
    EXPECT_FLOAT_EQ(skel->bones[0].offset.x, 1.5f); // atof reads the numeric prefix
    EXPECT_FLOAT_EQ(skel->bones[0].rotMin.z, -3.0f);
    EXPECT_FLOAT_EQ(skel->bones[0].rotMax.y, 2.0f);
}

TEST(ParityPed, AnimationTableAsPedAnimationLoadReadsIt) {
    // strtok drops the empty field; the next state is optional; atoi/atof prefixes.
    auto table = asset::parsePedAnimTable("WALK,,pedanim_manwalk,1,20x,0.281,1.409m,0,0\n");
    ASSERT_TRUE(table);
    const auto* s = table->find("WALK");
    ASSERT_TRUE(s);
    EXPECT_EQ(s->animFile, "pedanim_manwalk");
    EXPECT_EQ(s->lastFrame, 20);
    EXPECT_FLOAT_EQ(s->forwardDistance, 1.409f);
    EXPECT_TRUE(s->next.empty());
}

TEST(ParityPed, MeshVersionAndSingleColour) {
    const char* mod = "version: 1.09\nverts: 1\nnormals: 1\ncolors: 1\ntex1s: 0\ntex2s: 0\ntangents: 0\n"
                      "materials: 1\nadjuncts: 3\nprimitives: 1\nmatrices: 2\n"
                      "v 0 0 0\nn 0 1 0\nc 0.5 0.5 0.5 1\n"
                      "mtl a {\n adjuncts: 3\n primitives: 1\n textures: 1\n texture: 0 skin\n"
                      " illum: diffuse\n"
                      " ambient: 1 1 1\n diffuse: 1 1 1\n specular: 0 0 0\n}\n"
                      "adj 0 0 0 0 0\nadj 0 0 0 0 0\nadj 0 0 0 0 0\ntri 0 1 2\nmtxv 1 0\nmtxn 1 0\n";
    std::string err;
    auto mesh = asset::parsePedMesh(mod, &err);
    ASSERT_TRUE(mesh) << err;
    // One colour: modModel::LoadAscii gives the vertices none.
    EXPECT_FLOAT_EQ(mesh->vertices[0].color.x, 1.0f);
    EXPECT_EQ(mesh->materials[0].texture, "skin");

    std::string v2(mod);
    v2.replace(v2.find("1.09"), 4, "2.00");
    EXPECT_FALSE(asset::parsePedMesh(v2));
}
