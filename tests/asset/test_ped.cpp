// Pedestrian formats: synthetic tests plus every retail anim/ file.
#include "AssetTestUtil.h"
#include "TestData.h"
#include "asset/Ped.h"
#include "core/StringUtil.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace mm2;

namespace {

void expectNear(const Vec3& a, const Vec3& b, float eps = 1e-5f) {
    EXPECT_NEAR(a.x, b.x, eps);
    EXPECT_NEAR(a.y, b.y, eps);
    EXPECT_NEAR(a.z, b.z, eps);
}

constexpr const char* kSkel = "NumBones 3\n"
                              "bone root {\n"
                              "\toffset 0 1 0\n"
                              "\tbone arm {\n"
                              "\t\toffset 0.5 0 0\n"
                              "\t\tbone hand {\n"
                              "\t\t\toffset 0.25 0 0\n"
                              "\t\t}\n"
                              "\t}\n"
                              "}\n";

Bytes animFile(std::uint32_t frames, std::uint32_t channels, const std::vector<float>& values) {
    Bytes b;
    b.u32(0).u32(frames).u32(channels).f32(0.5f).u8(1);
    for (float v : values)
        b.f32(v);
    return b;
}

} // namespace

TEST(Ped, ParsesSkeleton) {
    std::string err;
    auto s = asset::parseSkeleton(kSkel, &err);
    ASSERT_TRUE(s) << err;
    ASSERT_EQ(s->bones.size(), 3u);
    EXPECT_EQ(s->bones[2].name, "hand");
    EXPECT_EQ(s->bones[2].parent, 1);
    EXPECT_EQ(s->bones[0].parent, -1);
    EXPECT_EQ(s->find("arm"), 1);
    EXPECT_EQ(s->find("ARM"), -1); // crSkeletonData::FindBone is case-sensitive
    EXPECT_FALSE(asset::parseSkeleton("NumBones 2\nbone root {\n offset 0 0 0\n}\n"));
    EXPECT_FALSE(asset::parseSkeleton("NumBones 1\nbone root {\n offset 0 0\n}\n"));
    EXPECT_FALSE(asset::parseSkeleton("NumBones 1\nbone root {\n"));
}

TEST(Ped, EulerXZYMatchesComposedRotations) {
    const Vec3 e{0.3f, -0.7f, 1.1f};
    const Mat34 m = asset::matrixFromEulersXZY(e);
    const Mat34 ref = Mat34::rotationX(e.x) * Mat34::rotationZ(e.z) * Mat34::rotationY(e.y);
    expectNear(m.m0, ref.m0);
    expectNear(m.m1, ref.m1);
    expectNear(m.m2, ref.m2);
    expectNear(m.m3, {});
    const Mat34 id = asset::matrixFromEulersXZY({});
    EXPECT_EQ(id.m0, Vec3(1, 0, 0));
    EXPECT_EQ(id.m1, Vec3(0, 1, 0));
    EXPECT_EQ(id.m2, Vec3(0, 0, 1));
}

TEST(Ped, ParsesAnimationAndPoses) {
    auto skel = asset::parseSkeleton(kSkel);
    ASSERT_TRUE(skel);
    // Two frames, 3 bones: frame 1 moves the root and turns the arm 90 degrees about Y.
    std::vector<float> v(2 * 12, 0.0f);
    v[12 + 0] = 2.0f;               // frame 1 root x
    v[12 + 1] = 1.0f;               // frame 1 root y
    v[12 + 3 + 3 * 1 + 1] = kHalfPi; // frame 1, bone 1 (arm), Euler y
    auto bytes = animFile(2, 12, v);
    std::string err;
    auto a = asset::parsePedAnimation(bytes.data, &err);
    ASSERT_TRUE(a) << err;
    EXPECT_EQ(a->frameCount, 2u);
    EXPECT_EQ(a->boneCount(), 3u);
    EXPECT_FLOAT_EQ(a->cycleDistance, 0.5f);
    EXPECT_EQ(a->flags, 1);

    std::vector<Mat34> pose;
    asset::posePed(*skel, nullptr, 0.0f, pose);
    expectNear(pose[2].m3, {0.75f, 1.0f, 0.0f}); // bind pose: offsets accumulate

    asset::posePed(*skel, &*a, 1.0f, pose);
    expectNear(pose[0].m3, {2.0f, 1.0f, 0.0f}); // root translation replaces the offset
    expectNear(pose[1].m3, {2.5f, 1.0f, 0.0f});
    // The arm's rotation turns the hand's +X offset to -Z (rotationY(+90deg) maps x to -z).
    expectNear(pose[2].m3, {2.5f, 1.0f, -0.25f});

    asset::posePed(*skel, &*a, 0.5f, pose); // halfway
    expectNear(pose[0].m3, {1.0f, 0.5f, 0.0f});

    bytes.data.pop_back();
    EXPECT_FALSE(asset::parsePedAnimation(bytes.data));
    EXPECT_FALSE(asset::parsePedAnimation(animFile(1, 4, {0, 0, 0, 0}).data)); // channels not 3 + 3n
}

TEST(Ped, ParsesPacketMesh) {
    const char* text = "version: 1.09\nverts: 3\nnormals: 3\ncolors: 1\ntex1s: 1\ntex2s: 0\ntangents: 0\n"
                       "materials: 1\nadjuncts: 3\nprimitives: 1\nmatrices: 2\n\n"
                       "v\t0 0 0\nv\t1 0 0\nv\t0 1 0\n"
                       "n\t0 0 1\nn\t0 0 1\nn\t0 0 1\n"
                       "c\t1 1 1 1\n"
                       "t1\t0.5 0.5\n"
                       "mtl test:SKIN {\n\tpackets:\t1\n\tprimitives:\t1\n\ttextures:\t0\n\tillum: diffuse\n"
                       "\tambient:\t0.1 0.2 0.3\n\tdiffuse:\t0.4 0.5 0.6\n\tspecular:\t0.7 0.8 0.9\n}\n\n"
                       "packet 3 1 2 {\n\tadj 0 0 0 0 0 0\n\tadj 1 1 0 0 0 1\n\tadj 2 2 0 0 0 1\n"
                       "\ttri 0 1 2\n\tmtx 1 0\n}\n";
    std::string err;
    auto m = asset::parsePedMesh(text, &err);
    ASSERT_TRUE(m) << err;
    EXPECT_EQ(m->vertices.size(), 3u);
    EXPECT_EQ(m->vertices[0].bone, 1u); // slot 0 -> bone 1
    EXPECT_EQ(m->vertices[1].bone, 0u);
    EXPECT_EQ(m->materials[0].name, "test:SKIN");
    EXPECT_FLOAT_EQ(m->materials[0].diffuse.y, 0.5f);
    EXPECT_EQ(m->materials[0].indexCount, 3u);

    std::string broken = text;
    broken.replace(broken.find("mtx 1 0"), 7, "mtx 1 5");
    EXPECT_FALSE(asset::parsePedMesh(broken)); // bone out of range
}

TEST(Ped, ParsesFlatMesh) {
    const char* text = "version: 1.09\nverts: 3\nnormals: 3\ncolors: 1\ntex1s: 1\ntex2s: 0\ntangents: 0\n"
                       "materials: 1\nadjuncts: 3\nprimitives: 1\nmatrices: 2\n"
                       "v 0 0 0\nv 1 0 0\nv 0 1 0\nn 0 0 1\nn 0 0 1\nn 0 0 1\nc 1 1 1 1\nt1 0 0\n"
                       "mtl w:SKIN {\n adjuncts: 3\n primitives: 1\n textures: 0\n illum: diffuse\n"
                       " ambient: 0 0 0\n diffuse: 1 0 0\n specular: 0 0 0\n}\n"
                       "adj 0 0 0 0 0\nadj 1 1 0 0 0\nadj 2 2 0 0 0\ntri 0 1 2\n"
                       "mtxv 1 2\nmtxn 1 2\n";
    std::string err;
    auto m = asset::parsePedMesh(text, &err);
    ASSERT_TRUE(m) << err;
    EXPECT_EQ(m->vertices[0].bone, 0u);
    EXPECT_EQ(m->vertices[2].bone, 1u);
}

TEST(Ped, ParsesShadersRaysRemapAndTable) {
    Bytes sh;
    sh.u32(2).u32(1);
    for (int v = 0; v < 2; ++v) {
        sh.u8(v == 1 ? 3 : 0);
        if (v == 1)
            sh.str("tex");
        for (int i = 0; i < 17; ++i)
            sh.f32(static_cast<float>(v * 100 + i));
    }
    std::string err;
    auto set = asset::parsePedShaders(sh.data, &err);
    ASSERT_TRUE(set) << err;
    EXPECT_EQ(set->get(1, 0)->texture, "tex");
    // modShader::Load: colours above 0.95 become 1, the ambient colour is
    // replaced by the diffuse one.
    EXPECT_FLOAT_EQ(set->get(1, 0)->diffuse.x, 1.0f);
    EXPECT_FLOAT_EQ(set->get(1, 0)->ambient.x, 1.0f);
    EXPECT_FLOAT_EQ(set->get(1, 0)->power, 116.0f);
    EXPECT_EQ(set->get(2, 0), nullptr);
    sh.u8(0);
    EXPECT_TRUE(asset::parsePedShaders(sh.data)); // trailing data is ignored, as in MM2

    auto rays = asset::parsePedRays("2\r\n0.1 0.2 0.3 1 2\r\n0 0 0 0 0\r\n3 4\r\n5 6\r\n", &err);
    ASSERT_TRUE(rays) << err;
    EXPECT_EQ(rays->bones.size(), 2u);
    EXPECT_EQ(rays->variants.size(), 2u);
    EXPECT_EQ(rays->variants[1][1], 6);

    auto remap = asset::parsePedRemap("3\n2 1 3\n");
    ASSERT_TRUE(remap);
    EXPECT_EQ(remap->size(), 3u);
    EXPECT_FALSE(asset::parsePedRemap("4\n2 1 3\n"));

    auto table = asset::parsePedAnimTable("# comment,,,\nWALK,pedanim_manwalk,1,20,0.281,1.409,0,0,WALK\n"
                                          "WALK_LDIVE,pedanim_manw2dl,1,24,0,0,0,2.224,LDIVE_GROUNDL\n",
                                          &err);
    ASSERT_TRUE(table) << err;
    ASSERT_TRUE(table->find("WALK_LDIVE"));
    EXPECT_FALSE(table->find("walk_ldive")); // pedAnimation::LookupSequence is case-sensitive
    EXPECT_FLOAT_EQ(table->find("WALK")->forwardDistance, 1.409f);
    EXPECT_EQ(table->find("WALK_LDIVE")->next, "LDIVE_GROUNDL");
}

TEST(Ped, RetailFilesParse) {
    MM2_REQUIRE_GAME_DATA();
    std::vector<std::string> paths;
    int checked = 0;
    for (const auto& e : test::gameData()->listFiles()) {
        paths.push_back(e.path);
        if (!e.path.starts_with("anim/") || asset::isKnownBrokenPedAsset(e.path))
            continue;
        auto bytes = test::gameData()->readAll(e.path);
        ASSERT_TRUE(bytes);
        const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        std::string err;
        bool ok = true;
        if (e.path.ends_with(".anim"))
            ok = asset::parsePedAnimation(*bytes, &err).has_value();
        else if (e.path.ends_with(".skel"))
            ok = asset::parseSkeleton(text, &err).has_value();
        else if (e.path.ends_with(".mod"))
            ok = asset::parsePedMesh(text, &err).has_value();
        else if (e.path.ends_with(".shaders"))
            ok = asset::parsePedShaders(*bytes, &err).has_value();
        else if (e.path.ends_with(".rays"))
            ok = asset::parsePedRays(text, &err).has_value();
        else if (e.path.ends_with(".remap"))
            ok = asset::parsePedRemap(text, &err).has_value();
        else if (e.path.ends_with(".csv"))
            ok = asset::parsePedAnimTable(text, &err).has_value();
        else
            continue;
        EXPECT_TRUE(ok) << e.path << ": " << err;
        ++checked;
    }
    EXPECT_EQ(checked, 88);
    EXPECT_FALSE(asset::parsePedAnimation(*test::gameData()->readAll("anim/pedanim_manantrnch.anim")));

    const auto types = asset::findPedTypes(paths);
    EXPECT_EQ(types, (std::vector<std::string>{"pedmodel_man", "pedmodel_manw", "pedmodel_woman",
                                               "pedmodel_womanw"}));
}

TEST(Ped, RetailTypesAreConsistent) {
    MM2_REQUIRE_GAME_DATA();
    const asset::ReadFileFn read = [](std::string_view p) { return test::gameData()->readAll(p); };
    for (const char* name : {"pedmodel_man", "pedmodel_manw", "pedmodel_woman", "pedmodel_womanw"}) {
        std::string err;
        auto t = asset::loadPedType(name, read, &err);
        ASSERT_TRUE(t) << name << ": " << err;
        EXPECT_EQ(t->skeleton.bones.size(), 19u) << name;
        EXPECT_EQ(t->mesh.boneCount, 19u) << name;
        // Every table state's animation exists and animates all bones.
        for (const auto& s : t->table.states)
            EXPECT_TRUE(t->animation(s.name)) << name << " " << s.name;

        // One shader variant reproduces the mesh's own material colours (as
        // modShader::Load rounds them: to 1/32 steps, 0 below 0.05, 1 above 0.95).
        auto round32 = [](float c) {
            return c < 0.05f ? 0.0f : (c > 0.95f ? 1.0f : std::floor(c * 32.0f) * 0.03125f);
        };
        bool found = false;
        for (std::uint32_t v = 0; v < t->shaders.variantCount && !found; ++v) {
            bool all = true;
            for (std::uint32_t m = 0; m < t->shaders.materialCount && all; ++m) {
                const Vec3 a = t->shaders.get(v, m)->diffuse.xyz(), d = t->mesh.materials[m].diffuse;
                const Vec3 b{round32(d.x), round32(d.y), round32(d.z)};
                all = (a - b).mag() < 1e-4f;
            }
            found = all;
        }
        EXPECT_TRUE(found) << name;

        // Forward root motion of the walk/run loops matches the table.
        for (const char* state : {"WALK", "RUN"}) {
            const auto* s = t->table.find(state);
            const auto* a = t->animation(state);
            ASSERT_TRUE(s && a);
            const float fwd = -(a->rootTranslation(a->frameCount - 1).z - a->rootTranslation(0).z);
            EXPECT_NEAR(fwd, s->forwardDistance, 0.21f) << name << " " << state;
        }
    }
    // Man walk: the table's distance is exact.
    auto man = asset::loadPedType("pedmodel_man", read);
    ASSERT_TRUE(man);
    const auto* walk = man->animation("WALK");
    EXPECT_NEAR(-(walk->rootTranslation(19).z - walk->rootTranslation(0).z), 1.409f, 0.002f);
    EXPECT_NEAR(-walk->rootTranslation(0).z, man->table.find("WALK")->forwardOffset, 0.002f);
}
