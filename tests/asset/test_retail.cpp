// Retail-data tests: every asset of each supported type must parse, and a few
// decoded values are checked against what is visible in the game.
#include "TestData.h"
#include "asset/Bound.h"
#include "asset/Image.h"
#include "asset/Mtx.h"
#include "asset/Pkg.h"
#include "asset/VehicleModel.h"
#include "core/StringUtil.h"

#include <gtest/gtest.h>


using namespace mm2;

namespace {

std::vector<std::string> filesMatching(std::string_view prefix, std::string_view ext) {
    std::vector<std::string> out;
    for (const auto& e : test::gameData()->listFiles())
        if (e.path.starts_with(prefix) && str::iendsWith(e.path, ext))
            out.push_back(e.path);
    return out;
}

std::vector<std::byte> read(std::string_view path) {
    auto d = test::gameData()->readAll(path);
    return d ? std::move(*d) : std::vector<std::byte>{};
}

std::string_view text(const std::vector<std::byte>& d) {
    return {reinterpret_cast<const char*>(d.data()), d.size()};
}

// Average RGB of one row of the top mip level.
std::array<double, 3> rowAverage(const asset::Image& img, std::uint32_t row) {
    const auto& l = img.levels[0];
    std::array<double, 3> sum{};
    for (std::uint32_t x = 0; x < l.width; ++x)
        for (int c = 0; c < 3; ++c)
            sum[static_cast<std::size_t>(c)] += l.rgba[(std::size_t{row} * l.width + x) * 4 + static_cast<std::size_t>(c)];
    for (auto& s : sum)
        s /= l.width;
    return sum;
}

} // namespace

TEST(RetailAssets, AllTexturesDecode) {
    MM2_REQUIRE_GAME_DATA();
    int count = 0;
    for (const auto& path : filesMatching("texture/", ".tex")) {
        std::string err;
        auto tex = asset::parseTex(read(path), &err);
        EXPECT_TRUE(tex) << path << ": " << err;
        if (tex) {
            EXPECT_EQ(tex->image.levels.size(), tex->header.mipCount) << path;
        }
        ++count;
    }
    EXPECT_GE(count, 3600);
}

TEST(RetailAssets, AllTgaAndJpgDecode) {
    MM2_REQUIRE_GAME_DATA();
    int count = 0;
    for (const auto& e : test::gameData()->listFiles()) {
        if (!str::iendsWith(e.path, ".tga") && !str::iendsWith(e.path, ".jpg"))
            continue;
        std::string err;
        EXPECT_TRUE(asset::decodeImageFile(e.path, read(e.path), &err)) << e.path << ": " << err;
        ++count;
    }
    EXPECT_GE(count, 690);
}

// Byte order and row order, checked against pictures with an obvious look.
TEST(RetailAssets, TextureColoursAndOrientation) {
    MM2_REQUIRE_GAME_DATA();
    // Dusk sky (RGB888): orange horizon at the bottom (row 0), blue zenith at the top.
    auto sky = asset::parseTex(read("texture/sky_cd_f.tex"));
    ASSERT_TRUE(sky);
    const auto bottom = rowAverage(sky->image, 0);
    const auto top = rowAverage(sky->image, sky->image.height() - 1);
    EXPECT_GT(bottom[0], bottom[2]);
    EXPECT_GT(top[2], top[0]);

    // Yellow Beetle paint (P8 with a B,G,R,A palette): red and green dominate blue.
    auto bug = asset::parseTex(read("texture/vpbugyellow_sd.tex"));
    ASSERT_TRUE(bug);
    double r = 0, b = 0;
    const auto& px = bug->image.levels[0].rgba;
    for (std::size_t i = 0; i < px.size(); i += 4) {
        r += px[i];
        b += px[i + 2];
    }
    EXPECT_GT(r, b * 1.5);
}

TEST(RetailAssets, AllModelsParse) {
    MM2_REQUIRE_GAME_DATA();
    int ok = 0;
    for (const auto& path : filesMatching("geometry/", ".pkg")) {
        if (asset::isKnownBrokenRetailAsset(path))
            continue;
        std::string err;
        auto pkg = asset::parsePkg(read(path), &err);
        EXPECT_TRUE(pkg) << path << ": " << err;
        if (!pkg)
            continue;
        ++ok;
        for (const auto& m : pkg->meshes)
            for (const auto& s : m.sections)
                if (!pkg->paintjobs.empty()) {
                    EXPECT_LT(s.shaderIndex, pkg->shadersPerPaintjob) << path << " " << m.name;
                }
    }
    EXPECT_GE(ok, 1037);
}

TEST(RetailAssets, AllPivotsParse) {
    MM2_REQUIRE_GAME_DATA();
    int count = 0;
    for (const auto& path : filesMatching("geometry/", ".mtx")) {
        EXPECT_TRUE(asset::parseMtx(read(path))) << path;
        ++count;
    }
    EXPECT_GE(count, 800);
}

TEST(RetailAssets, BoundsParseAndAgree) {
    MM2_REQUIRE_GAME_DATA();
    int bnd = 0, bbnd = 0, ter = 0, compared = 0;
    for (const auto& path : filesMatching("bound/", ".bnd")) {
        std::string err;
        EXPECT_TRUE(asset::parseBnd(text(read(path)), &err)) << path << ": " << err;
        ++bnd;
    }
    for (const auto& path : filesMatching("bound/", ".bbnd")) {
        std::string err;
        auto bin = asset::parseBbnd(read(path), &err);
        EXPECT_TRUE(bin) << path << ": " << err;
        ++bbnd;
        const std::string textPath = path.substr(0, path.size() - 5) + ".bnd";
        if (!bin || !test::gameData()->exists(textPath))
            continue;
        auto txt = asset::parseBnd(text(read(textPath)));
        ASSERT_TRUE(txt) << textPath;
        ASSERT_EQ(txt->vertices.size(), bin->vertices.size()) << path;
        ASSERT_EQ(txt->polygons.size(), bin->polygons.size()) << path;
        for (std::size_t i = 0; i < txt->polygons.size(); ++i) {
            EXPECT_EQ(txt->polygons[i].indices, bin->polygons[i].indices) << path << " poly " << i;
            EXPECT_EQ(txt->polygons[i].material, bin->polygons[i].material) << path << " poly " << i;
        }
        ++compared;
    }
    for (const auto& path : filesMatching("bound/", ".ter")) {
        std::string err;
        auto t = asset::parseTer(read(path), &err);
        EXPECT_TRUE(t) << path << ": " << err;
        ++ter;
        const std::string binPath = path.substr(0, path.size() - 4) + ".bbnd";
        if (!t || !test::gameData()->exists(binPath))
            continue;
        auto g = asset::parseBbnd(read(binPath));
        ASSERT_TRUE(g);
        EXPECT_EQ(t->polygonCount(), g->polygons.size()) << path;
    }
    EXPECT_GE(bnd, 525);
    EXPECT_GE(bbnd, 324);
    EXPECT_GE(ter, 184);
    EXPECT_GE(compared, 300);
}

TEST(RetailAssets, BeetleWheels) {
    MM2_REQUIRE_GAME_DATA();
    auto vm = asset::loadVehicleModel("vpbug", [](std::string_view p) { return test::gameData()->readAll(p); });
    ASSERT_TRUE(vm);
    ASSERT_EQ(vm->wheels.size(), 4u);
    const auto* fl = vm->wheel(0);
    const auto* rr = vm->wheel(3);
    ASSERT_TRUE(fl && rr);
    EXPECT_LT(fl->position.x, 0.0f); // left
    EXPECT_LT(fl->position.z, 0.0f); // front (cars face -Z)
    EXPECT_GT(rr->position.x, 0.0f);
    EXPECT_GT(rr->position.z, 0.0f);
    EXPECT_NEAR(fl->radius, 0.336f, 1e-3f);
    EXPECT_TRUE(vm->pkg.find("BODY", asset::Lod::High));
    EXPECT_EQ(vm->pkg.paintjobs.size(), 4u); // Yellow|Blue|Silver|Red in tune/vpbug.info
}
