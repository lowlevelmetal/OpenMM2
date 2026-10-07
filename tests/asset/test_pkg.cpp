#include "AssetTestUtil.h"
#include "asset/Mtx.h"
#include "asset/Pkg.h"
#include "asset/VehicleModel.h"

#include <gtest/gtest.h>

#include <map>

using namespace mm2;

namespace {

// One geometry chunk body: one section, one triangle, FVF xyz|normal|tex1.
Bytes geometryBody(std::uint32_t shader, bool goodTotals = true) {
    Bytes b;
    b.u32(1).u32(goodTotals ? 3 : 999).u32(3).u32(1).u32(0x112);
    b.u16(1).u16(0).u32(shader);
    b.u32(3).u32(3);
    for (int i = 0; i < 3; ++i)
        b.f32(static_cast<float>(i)).f32(0).f32(-1).f32(0).f32(1).f32(0).f32(0.5f).f32(0.25f);
    b.u32(3).u16(0).u16(1).u16(2);
    return b;
}

Bytes chunk3(std::string_view name, const Bytes& body, std::optional<std::uint32_t> size = {}) {
    Bytes b;
    b.str("FILE").u8(static_cast<std::uint8_t>(name.size() + 1)).str(name).u8(0);
    b.u32(size.value_or(static_cast<std::uint32_t>(body.size()))).append(body);
    return b;
}

Bytes compactShaders() {
    Bytes b;
    b.u32(0x82).u32(1); // 2 paint jobs, 1 shader each, compact
    for (const char* tex : {"red_body", "blue_body"}) {
        b.u8(static_cast<std::uint8_t>(std::string_view(tex).size() + 1)).str(tex).u8(0);
        b.u8(255).u8(128).u8(0).u8(255); // diffuse RGBA
        b.u8(255).u8(255).u8(255).u8(255);
        b.u8(0).u8(0).u8(0).u8(0);
        b.f32(0.5f);
    }
    return b;
}

} // namespace

TEST(Pkg, ParsesPkg3Chunks) {
    Bytes file;
    file.str("PKG3");
    file.append(chunk3("BODY_H", geometryBody(0)));
    file.append(chunk3("WHL0_M", geometryBody(0)));
    file.append(chunk3("shaders", compactShaders()));
    Bytes offset;
    offset.f32(1).f32(2).f32(3);
    file.append(chunk3("offset", offset));
    std::string err;
    auto pkg = asset::parsePkg(file.data, &err);
    ASSERT_TRUE(pkg) << err;
    EXPECT_EQ(pkg->version, 3);
    ASSERT_EQ(pkg->meshes.size(), 2u);
    EXPECT_EQ(pkg->meshes[0].part, "BODY");
    EXPECT_EQ(pkg->meshes[0].lod, asset::Lod::High);
    EXPECT_EQ(pkg->meshes[1].part, "WHL0");
    EXPECT_EQ(pkg->meshes[1].lod, asset::Lod::Medium);
    EXPECT_EQ(pkg->meshes[0].triangleCount(), 1u);
    const auto& v = pkg->meshes[0].sections[0].packets[0].vertices[2];
    EXPECT_FLOAT_EQ(v.position.x, 2.0f);
    EXPECT_FLOAT_EQ(v.normal.y, 1.0f);
    EXPECT_FLOAT_EQ(v.uv.y, 0.25f);
    ASSERT_EQ(pkg->paintjobs.size(), 2u);
    EXPECT_EQ(pkg->paintjobs[1][0].texture, "blue_body");
    EXPECT_NEAR(pkg->paintjobs[0][0].diffuse.y, 128 / 255.0f, 1e-6f);
    EXPECT_FLOAT_EQ(pkg->paintjobs[0][0].shininess, 0.5f);
    ASSERT_TRUE(pkg->offset);
    EXPECT_FLOAT_EQ(pkg->offset->z, 3.0f);
    EXPECT_TRUE(pkg->warnings.empty());
    EXPECT_EQ(pkg->findBest("WHL0", asset::Lod::High), &pkg->meshes[1]);
    EXPECT_EQ(pkg->parts(), (std::vector<std::string>{"BODY", "WHL0"}));
}

TEST(Pkg, Pkg2HasNoChunkSizes) {
    Bytes file;
    file.str("PKG2");
    file.str("FILE").u8(2).str("H").u8(0).append(geometryBody(0));
    file.str("FILE").u8(8).str("shaders").u8(0).append(compactShaders());
    std::string err;
    auto pkg = asset::parsePkg(file.data, &err);
    ASSERT_TRUE(pkg) << err;
    EXPECT_EQ(pkg->version, 2);
    ASSERT_EQ(pkg->meshes.size(), 1u);
    EXPECT_EQ(pkg->meshes[0].part, "");
    EXPECT_EQ(pkg->meshes[0].lod, asset::Lod::High);
}

TEST(Pkg, RecoversFromBadChunkSizeAndTotals) {
    Bytes file;
    file.str("PKG3");
    file.append(chunk3("BODY_L", geometryBody(0, false), 0xFFFFFF00u));
    file.append(chunk3("shaders", compactShaders()));
    std::string err;
    auto pkg = asset::parsePkg(file.data, &err);
    ASSERT_TRUE(pkg) << err;
    EXPECT_EQ(pkg->meshes.size(), 1u);
    EXPECT_EQ(pkg->warnings.size(), 2u); // bad totals + bad size
}

TEST(Pkg, RejectsCorruptIndices) {
    Bytes body = geometryBody(0);
    body.data[body.size() - 1] = std::byte{9}; // index 2 -> 0x0902, out of range
    Bytes file;
    file.str("PKG3").append(chunk3("H", body));
    std::string err;
    EXPECT_FALSE(asset::parsePkg(file.data, &err));
    EXPECT_FALSE(err.empty());
}

TEST(Pkg, SplitLodName) {
    EXPECT_EQ(asset::splitLodName("BREAK01_VL"), (std::pair<std::string, asset::Lod>{"BREAK01", asset::Lod::VeryLow}));
    EXPECT_EQ(asset::splitLodName("VL").second, asset::Lod::VeryLow);
    EXPECT_EQ(asset::splitLodName("TRAILER_HITCH").second, asset::Lod::None);
    EXPECT_EQ(asset::splitLodName("dash_H").first, "dash");
}

TEST(Mtx, ParsesFloats) {
    Bytes b;
    for (int i = 0; i < 12; ++i)
        b.f32(static_cast<float>(i));
    auto m = asset::parseMtx(b.data);
    ASSERT_TRUE(m);
    EXPECT_FLOAT_EQ(m->origin.z, 11.0f);
    EXPECT_FLOAT_EQ(m->halfExtent().x, 1.5f);
    b.u8(0);
    EXPECT_FALSE(asset::parseMtx(b.data));
}

TEST(VehicleModel, WheelsFromPivots) {
    std::map<std::string, std::vector<std::byte>, std::less<>> files;
    Bytes pkg;
    pkg.str("PKG3").append(chunk3("BODY_H", geometryBody(0))).append(chunk3("WHL1_H", geometryBody(0)));
    pkg.append(chunk3("shaders", compactShaders()));
    files["geometry/car.pkg"] = pkg.data;
    Bytes mtx;
    for (float f : {-0.1f, -0.3f, -0.3f, 0.1f, 0.3f, 0.3f, 0.0f, 0.0f, 0.0f, 0.8f, 0.3f, -1.2f})
        mtx.f32(f);
    files["geometry/car_whl1.mtx"] = mtx.data;
    files["geometry/car_exhaust0.mtx"] = mtx.data;
    auto read = [&](std::string_view p) -> std::optional<std::vector<std::byte>> {
        auto it = files.find(p);
        if (it == files.end())
            return std::nullopt;
        return it->second;
    };
    std::string err;
    auto vm = asset::loadVehicleModel("CAR", read, &err);
    ASSERT_TRUE(vm) << err;
    ASSERT_EQ(vm->wheels.size(), 1u);
    EXPECT_EQ(vm->wheels[0].index, 1);
    EXPECT_FLOAT_EQ(vm->wheels[0].radius, 0.3f);
    EXPECT_FLOAT_EQ(vm->wheels[0].width, 0.2f);
    EXPECT_FLOAT_EQ(vm->wheels[0].position.z, -1.2f);
    EXPECT_TRUE(vm->pivot("EXHAUST0"));
    EXPECT_FALSE(vm->pivot("whl0"));
}
