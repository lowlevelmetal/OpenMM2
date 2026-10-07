// sdlPage16::Collect port (city/SdlCollect): synthetic rooms, then every room
// of the retail cities ($OPENMM2_GAME_DATA).
#include "TestData.h"
#include "city/Environment.h"
#include "city/Psdl.h"
#include "city/SdlCollect.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace mm2;
using namespace mm2::city;

namespace {

constexpr std::uint16_t header(PsdlAttrType type, int subtype, bool last = false) {
    return static_cast<std::uint16_t>((static_cast<int>(type) << 3) | subtype | (last ? 0x80 : 0));
}
// A Texture attribute is this header and the texture value (< 256 here).
constexpr std::uint16_t kTexture = header(PsdlAttrType::Texture, 0);

// A PSDL with one room (id 1) whose attribute words are `words`.
Psdl makePsdl(std::vector<Vec3> vertices, std::initializer_list<std::uint16_t> words,
              std::vector<float> heights = {}, std::uint8_t flags = RoomFlag::Road) {
    Psdl p;
    p.version = 2;
    p.vertices = std::move(vertices);
    p.heights = std::move(heights);
    p.textures = {"road", "swalk", "xwalk", "divider_side", "divider_top", "wall"};
    p.rooms.resize(2);
    std::vector<std::uint16_t> w(words);
    std::string err;
    EXPECT_TRUE(decodePsdlAttributes(w, p.rooms[1].attributes, &err)) << err;
    p.rooms[1].flags = flags;
    return p;
}

// Material = texture index * 10, so the tests see which texture a polygon used.
std::vector<std::uint8_t> materials(const Psdl& p) {
    std::vector<std::uint8_t> m(p.textures.size() + 1);
    for (std::size_t i = 0; i < m.size(); ++i)
        m[i] = static_cast<std::uint8_t>(i * 10);
    return m;
}

struct Collected {
    SdlPolyBuffer buf;
    int count = 0;
    bool overflow = false;
};

Collected collect(const Psdl& p, const SdlSphere* sphere = nullptr, int capacity = 256,
                  std::uint32_t* state = nullptr, std::uint16_t probeRoom = 0) {
    Collected c;
    c.buf.reset(p);
    c.count = collectRoomPolygons(p, 1, sphere, materials(p), c.buf, capacity, &c.overflow, state, probeRoom);
    return c;
}

Vec3 expectedNormal(const SdlPolyBuffer& b, const SdlPoly& poly) {
    const Vec3 a = b.vertices[poly.v[0]], m = b.vertices[poly.v[1]], c = b.vertices[poly.v[2]];
    return (c - m).cross(a - m).normalized();
}

std::vector<Vec3> corners(const SdlPolyBuffer& b, const SdlPoly& poly) {
    std::vector<Vec3> out;
    for (int i = 0; i < (poly.v[3] ? 4 : 3); ++i)
        out.push_back(b.vertices[poly.v[i]]);
    return out;
}

void expectNear(const Vec3& a, const Vec3& b, float tol = 1e-5f) {
    EXPECT_NEAR(a.x, b.x, tol);
    EXPECT_NEAR(a.y, b.y, tol);
    EXPECT_NEAR(a.z, b.z, tol);
}

// A straight road strip along -z: sections at z = 0, -10, -20 (outer L, curb L,
// curb R, outer R at x = -6, -4, 4, 6), sidewalks 0.15 above the road.
std::vector<Vec3> roadVertices(int sections = 3) {
    std::vector<Vec3> v{{0, -100, 0}}; // index 0 stays unused (v[3] == 0 marks triangles)
    for (int s = 0; s < sections; ++s) {
        const float z = -10.0f * static_cast<float>(s);
        v.push_back({-6, 0.15f, z});
        v.push_back({-4, 0, z});
        v.push_back({4, 0, z});
        v.push_back({6, 0.15f, z});
    }
    return v;
}

} // namespace

TEST(SdlCollect, RectangleStripQuadsKeepWindingAndFlatNormal) {
    // Two quads: flat, then rising.
    const Psdl p =
        makePsdl({{0, -100, 0}, {0, 0, 0}, {10, 0, 0}, {0, 0, -10}, {10, 0, -10}, {0, 2, -20}, {10, 2, -20}},
                 {kTexture, 1, header(PsdlAttrType::RectangleStrip, 3, true), 1, 2, 3, 4, 5, 6});
    const auto c = collect(p);
    ASSERT_EQ(c.count, 2);
    ASSERT_EQ(c.buf.polys.size(), 2u);
    const auto& flat = c.buf.polys[0];
    // sdlPoly::SetQuad winds v0 v1 v3 v2.
    EXPECT_EQ(flat.v, (std::array<std::uint32_t, 4>{1, 2, 4, 3}));
    EXPECT_EQ(flat.normal, Vec3(0, 1, 0)); // all y equal: exactly up, whatever the winding
    EXPECT_EQ(flat.material, 10);          // road texture (+0) of group 1
    const auto& slope = c.buf.polys[1];
    EXPECT_EQ(slope.v, (std::array<std::uint32_t, 4>{3, 4, 6, 5}));
    expectNear(slope.normal, expectedNormal(c.buf, slope));
    EXPECT_EQ(c.buf.generatedVertexCount(), 0u);
}

TEST(SdlCollect, DegeneratePolygonIsRejectedButUsesCapacity) {
    // First quad collinear (zero area, not flat), second fine.
    const Psdl p =
        makePsdl({{0, -100, 0}, {0, 0, 0}, {1, 1, 0}, {2, 2, 0}, {3, 3, 0}, {0, 0, -10}, {3, 3, -10}},
                 {kTexture, 1, header(PsdlAttrType::RectangleStrip, 3, true), 1, 2, 3, 4, 5, 6});
    auto c = collect(p);
    EXPECT_EQ(c.count, 1);
    EXPECT_FALSE(c.overflow);
    // Capacity 1: the rejected quad used it up.
    c = collect(p, nullptr, 1);
    EXPECT_EQ(c.count, 0);
    EXPECT_TRUE(c.overflow);
}

TEST(SdlCollect, RoadStripSidewalksCurbsAndRoad) {
    const Psdl p = makePsdl(roadVertices(2),
                            {kTexture, 1, header(PsdlAttrType::RoadStrip, 2, true), 1, 2, 3, 4, 5, 6, 7, 8});
    const auto c = collect(p);
    ASSERT_EQ(c.count, 5);
    const auto& b = c.buf;
    // Pass 1: left sidewalk (curb raised 0.15) and the curb face, sidewalk texture (+1).
    const auto& walk = b.polys[0];
    EXPECT_EQ(walk.material, 20);
    const auto w = corners(b, walk);
    ASSERT_EQ(w.size(), 4u);
    expectNear(w[0], {-6, 0.15f, 0});
    expectNear(w[1], {-4, 0.15f, 0});
    expectNear(w[2], {-4, 0.15f, -10});
    expectNear(w[3], {-6, 0.15f, -10});
    EXPECT_EQ(walk.normal, Vec3(0, 1, 0));
    const auto& curb = b.polys[1];
    EXPECT_EQ(curb.material, 20);
    const auto k = corners(b, curb);
    expectNear(k[0], {-4, 0, 0});
    expectNear(k[1], {-4, 0, -10});
    expectNear(k[2], {-4, 0.15f, -10});
    expectNear(k[3], {-4, 0.15f, 0});
    expectNear(curb.normal, {1, 0, 0}); // faces the road
    // Pass 2: the road, road texture.
    EXPECT_EQ(b.polys[2].material, 10);
    EXPECT_EQ(b.polys[2].v, (std::array<std::uint32_t, 4>{2, 3, 7, 6}));
    // Pass 3: right sidewalk and its curb face (towards the road).
    EXPECT_EQ(b.polys[3].material, 20);
    expectNear(b.polys[4].normal, {-1, 0, 0});
    // 2 + 2 raised curb corners and 2 + 2 wall tops.
    EXPECT_EQ(b.generatedVertexCount(), 8u);
    EXPECT_EQ(b.vertexBudget, kSdlGeneratedVertexBudget - 8);
}

TEST(SdlCollect, ProbeInSpecialBoundRoomUsesTriangles) {
    const Psdl p = makePsdl(roadVertices(2),
                            {kTexture, 1, header(PsdlAttrType::RoadStrip, 2, true), 1, 2, 3, 4, 5, 6, 7, 8},
                            {}, RoomFlag::Road | RoomFlag::SpecialBound);
    // Outside lvlSDL::CollideProbe (probe room 0): quads as usual.
    EXPECT_EQ(collect(p).count, 5);
    const auto c = collect(p, nullptr, 256, nullptr, 1);
    // Per strip piece two triangles; the curb walls stay.
    ASSERT_EQ(c.count, 8);
    for (const auto& poly : c.buf.polys)
        EXPECT_EQ(poly.material, 20); // even the road surface uses the sidewalk texture
    EXPECT_EQ(c.buf.polys[0].v[3], 0u);
}

TEST(SdlCollect, SidewalkStripAndEndCap) {
    std::vector<Vec3> v{{0, -100, 0}, {0, 0, 0}, {-2, 0.15f, 0}, {0, 0, -10}, {-2, 0.15f, -10}};
    const Psdl strip = makePsdl(v, {kTexture, 1, header(PsdlAttrType::SidewalkStrip, 2, true), 1, 2, 3, 4});
    auto c = collect(strip);
    ASSERT_EQ(c.count, 2); // the walk and the curb face
    expectNear(c.buf.vertices[c.buf.polys[0].v[0]], {0, 0.15f, 0});
    const Psdl cap = makePsdl(v, {kTexture, 1, header(PsdlAttrType::SidewalkStrip, 2, true), 0, 0, 1, 2});
    c = collect(cap);
    ASSERT_EQ(c.count, 1);
    EXPECT_EQ(c.buf.polys[0].v[3], 0u);
    // (curb raised, outer, curb): the end face of the curb.
    const auto t = corners(c.buf, c.buf.polys[0]);
    expectNear(t[0], {0, 0.15f, 0});
    expectNear(t[1], {-2, 0.15f, 0});
    expectNear(t[2], {0, 0, 0});
}

TEST(SdlCollect, TextureZeroSkipsSurfacesButNotBoundsOrRoofs) {
    std::vector<Vec3> v{{0, -100, 0}, {0, 0, 0}, {10, 0, 0}, {0, 0, -10}, {10, 0, -10}};
    const Psdl p = makePsdl(v,
                            {kTexture, 0, header(PsdlAttrType::RectangleStrip, 2), 1, 2, 3, 4,
                             header(PsdlAttrType::FacadeBound, 4), 0, 1, 1, 2,
                             header(PsdlAttrType::RoofTriangleFan, 3, true), 1, 1, 2, 4, 3},
                            {0.0f, 12.0f});
    const auto c = collect(p);
    ASSERT_EQ(c.count, 3); // facade bound wall + two roof triangles
    const auto wall = corners(c.buf, c.buf.polys[0]);
    expectNear(wall[2], {10, 12, 0});
    for (int i = 1; i < 3; ++i) {
        EXPECT_EQ(c.buf.polys[i].normal, Vec3(0, 1, 0));
        for (const auto& corner : corners(c.buf, c.buf.polys[i]))
            EXPECT_EQ(corner.y, 12.0f);
    }
}

TEST(SdlCollect, FansSkipMaterialTwoAndRoadFansCullByHeight) {
    std::vector<Vec3> v{{0, -100, 0}, {0, 1, 0}, {10, 0, 0}, {10, 0, -10}, {0, 0, -10}};
    const Psdl p = makePsdl(v, {kTexture, 1, header(PsdlAttrType::RoadTriangleFan, 2, true), 1, 2, 3, 4});
    auto c = collect(p);
    ASSERT_EQ(c.count, 2);
    for (const auto& poly : c.buf.polys) {
        EXPECT_EQ(poly.normal, Vec3(0, 1, 0));
        for (const auto& corner : corners(c.buf, poly))
            EXPECT_EQ(corner.y, 1.0f); // flat at the hub's height
    }
    SdlSphere above{{5, 5, -5}, 1};
    EXPECT_EQ(collect(p, &above).count, 0);
    SdlSphere on{{5, 1.5f, -5}, 1};
    EXPECT_EQ(collect(p, &on).count, 2);

    auto mats = materials(p);
    mats[1] = 2; // the retail "deepwater" index
    SdlPolyBuffer buf;
    buf.reset(p);
    EXPECT_EQ(collectRoomPolygons(p, 1, nullptr, mats, buf, 256), 0);
}

TEST(SdlCollect, SphereCullsSectionsAndFarSphereGetsNothing) {
    // A long rectangle strip along -z, 20 sections.
    std::vector<Vec3> v{{0, -100, 0}};
    std::vector<std::uint16_t> words{kTexture, 1, header(PsdlAttrType::RectangleStrip, 0, true), 20};
    for (int s = 0; s < 20; ++s) {
        v.push_back({0, 0, -5.0f * static_cast<float>(s)});
        v.push_back({8, 0, -5.0f * static_cast<float>(s)});
        words.push_back(static_cast<std::uint16_t>(2 * s + 1));
        words.push_back(static_cast<std::uint16_t>(2 * s + 2));
    }
    Psdl p = makePsdl(v, {});
    ASSERT_TRUE(decodePsdlAttributes(words, p.rooms[1].attributes));
    const auto all = collect(p);
    EXPECT_EQ(all.count, 19);
    SdlSphere s{{4, 0, -50}, 2};
    const auto near = collect(p, &s);
    EXPECT_GT(near.count, 0);
    EXPECT_LT(near.count, 6);
    for (const auto& poly : near.buf.polys) {
        bool found = false;
        for (const auto& q : all.buf.polys)
            found = found || q.v == poly.v;
        EXPECT_TRUE(found);
    }
    SdlSphere far{{1000, 0, 1000}, 5};
    EXPECT_EQ(collect(p, &far).count, 0);
}

TEST(SdlCollect, OverflowStoresResumeState) {
    std::vector<Vec3> v{{0, -100, 0}};
    for (int s = 0; s < 4; ++s) {
        v.push_back({0, 0, -5.0f * static_cast<float>(s)});
        v.push_back({8, 0, -5.0f * static_cast<float>(s)});
    }
    const Psdl p = makePsdl(
        v, {kTexture, 1, header(PsdlAttrType::RectangleStrip, 4, true), 1, 2, 3, 4, 5, 6, 7, 8});
    std::uint32_t state = 0;
    auto c = collect(p, nullptr, 2, &state);
    EXPECT_EQ(c.count, 2);
    EXPECT_TRUE(c.overflow);
    // The strip starts at word 2 (after the texture's two words), texture 1.
    EXPECT_EQ(state, (2u << 11) | 1u);
    // Resuming redoes the strip.
    c = collect(p, nullptr, 256, &state);
    EXPECT_EQ(c.count, 3);
    EXPECT_FALSE(c.overflow);
    EXPECT_EQ(state, (2u << 11) | 1u); // untouched when Collect finishes
    // Overflowing again in the first attribute of a resumed call writes 0 (MM2 quirk).
    c = collect(p, nullptr, 1, &state);
    EXPECT_TRUE(c.overflow);
    EXPECT_EQ(state, 0u);
}

TEST(SdlCollect, DividedRoadMedians) {
    // One section pair, 6 per section: outer L, curb L, median L, median R, curb R, outer R.
    std::vector<Vec3> v{{0, -100, 0}};
    for (int s = 0; s < 2; ++s)
        for (float x : {-10.0f, -8.0f, -1.0f, 1.0f, 8.0f, 10.0f})
            v.push_back({x, 0, -10.0f * static_cast<float>(s)});
    const auto divided = [&](std::uint16_t flags, std::uint16_t height) {
        return makePsdl(v, {kTexture, 1, header(PsdlAttrType::DividedRoadStrip, 2, true),
                            static_cast<std::uint16_t>(flags | (3 << 8)), height, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                            10, 11, 12});
    };
    // Flat median (type 1): sidewalk, curb, road, median (divider texture + 1), road, sidewalk, curb.
    auto c = collect(divided(1, 0x0100));
    ASSERT_EQ(c.count, 7);
    EXPECT_EQ(c.buf.polys[3].material, 40);
    // Raised median (type 2, 1 m): two walls (road texture + 1) and a top (divider texture + 2).
    c = collect(divided(2, 0x0100));
    ASSERT_EQ(c.count, 9);
    EXPECT_EQ(c.buf.polys[3].material, 20);
    EXPECT_EQ(c.buf.polys[4].material, 20);
    EXPECT_EQ(c.buf.polys[5].material, 50);
    for (const auto& corner : corners(c.buf, c.buf.polys[5]))
        EXPECT_EQ(corner.y, 1.0f);
}

TEST(SdlCollect, TunnelWallsAndJunctions) {
    // A strip tunnel (left and right walls) over a two-section road strip.
    auto v = roadVertices(2);
    const Psdl p = makePsdl(v, {header(PsdlAttrType::Tunnel, 3), 0x0003, 0x0100, 0x0200, kTexture, 1,
                                header(PsdlAttrType::RoadStrip, 2, true), 1, 2, 3, 4, 5, 6, 7, 8});
    auto c = collect(p);
    ASSERT_EQ(c.count, 7); // 2 walls, then the road strip's 5
    for (int i = 0; i < 2; ++i) {
        // Walls are at least 3 m (height2 = 2 m here) and use the texture current at the tunnel (0).
        EXPECT_EQ(c.buf.polys[i].material, 0);
        float top = 0;
        for (const auto& corner : corners(c.buf, c.buf.polys[i]))
            top = std::max(top, corner.y);
        EXPECT_FLOAT_EQ(top, 3.15f);
    }
    // A junction tunnel walls off the perimeter edges whose bit is set.
    Psdl j = makePsdl(v, {kTexture, 1, header(PsdlAttrType::Tunnel, 0, true), 10, 0x0003, 0x0100, 0x0600, 0,
                          0x0005, 0, 0, 0, 0, 0});
    j.rooms[1].perimeter = {{1, 0}, {4, 0}, {8, 0}, {5, 0}};
    c = collect(j);
    ASSERT_EQ(c.count, 2); // edges 0 (perimeter 3 -> 0) and 2 (1 -> 2)
    const auto w = corners(c.buf, c.buf.polys[0]);
    expectNear(w[0], v[1]);
    expectNear(w[1], v[5]);
    EXPECT_FLOAT_EQ(w[2].y, 0.15f + 6.0f);
}

TEST(SdlCollect, TextureMaterialTable) {
    Psdl p;
    p.textures = {"r2_f", "", "s_thames-0009", "unknown", "grassy"};
    const std::vector<TextureMaterial> rows{
        {"r2_f", "cobblestone"}, {"s_thames", "deepwater"}, {"grassy", "none"}, {"r2_f", "grass"}};
    std::vector<PhysMaterial> mtl(8);
    const char* names[] = {"deepwater", "_default", "grass", "water", "dirt", "sand", "cobblestone", "wood"};
    for (std::size_t i = 0; i < mtl.size(); ++i)
        mtl[i].name = names[i];
    EXPECT_EQ(sdlMaterialIndex(mtl, "default"), 1);
    EXPECT_EQ(sdlMaterialIndex(mtl, "deepwater"), 2);
    EXPECT_EQ(sdlMaterialIndex(mtl, "_default"), 3);
    EXPECT_EQ(sdlMaterialIndex(mtl, "cobblestone"), 8);
    EXPECT_EQ(sdlMaterialIndex(mtl, "wood"), 9);
    EXPECT_EQ(sdlMaterialIndex(mtl, "mud"), 0);
    const auto table =
        sdlTextureMaterials(p, rows, [&](std::string_view name) { return sdlMaterialIndex(mtl, name); });
    EXPECT_EQ(table, (std::vector<std::uint8_t>{0, 8, 0, 2, 0, 0}));
}

// ---------------------------------------------------------------------------
// Retail data
// ---------------------------------------------------------------------------

namespace {

struct RetailCity {
    Psdl psdl;
    std::vector<std::uint8_t> materials;
};

const RetailCity& retailCity(const char* cityName) {
    static std::map<std::string, RetailCity> cache;
    const std::string name = cityName;
    auto it = cache.find(name);
    if (it != cache.end())
        return it->second;
    RetailCity c;
    const auto& vfs = *test::gameData();
    if (auto b = vfs.readAll("city/" + name + ".psdl")) {
        std::string err;
        if (auto p = parsePsdl(*b, &err))
            c.psdl = std::move(*p);
        else
            ADD_FAILURE() << name << ": " << err;
    } else {
        ADD_FAILURE() << "city/" << name << ".psdl missing";
    }
    std::vector<TextureMaterial> rows;
    std::vector<PhysMaterial> mtl;
    const auto text = [](const std::vector<std::byte>& b) {
        return std::string_view(reinterpret_cast<const char*>(b.data()), b.size());
    };
    if (auto b = vfs.readAll("city/materials.csv"))
        rows = parseTextureMaterials(text(*b));
    if (auto b = vfs.readAll("city/materials.mtl"))
        if (auto m = parseMaterialLibrary(text(*b)))
            mtl = std::move(*m);
    c.materials =
        sdlTextureMaterials(c.psdl, rows, [&](std::string_view n) { return sdlMaterialIndex(mtl, n); });
    return cache.emplace(name, std::move(c)).first->second;
}

// A polygon by its geometry (generated vertex indices differ between calls).
std::vector<float> key(const SdlPolyBuffer& b, const SdlPoly& p) {
    std::vector<float> k{static_cast<float>(p.material)};
    for (const auto& c : corners(b, p))
        k.insert(k.end(), {c.x, c.y, c.z});
    return k;
}

class RetailSdl : public ::testing::TestWithParam<const char*> {
protected:
    void SetUp() override { MM2_REQUIRE_GAME_DATA(); }
};

} // namespace

TEST_P(RetailSdl, TextureMaterialsFollowMaterialsCsv) {
    const auto& c = retailCity(GetParam());
    ASSERT_EQ(c.materials.size(), c.psdl.textures.size() + 1);
    int deepwater = 0, cobblestone = 0;
    for (std::size_t i = 0; i < c.psdl.textures.size(); ++i) {
        const auto& name = c.psdl.textures[i];
        if (name.starts_with("s_thames") || name.starts_with("s_ocean")) {
            EXPECT_EQ(c.materials[i + 1], 2) << name;
            ++deepwater;
        }
        if (name == "swalk_f" || name == "s_asphalt") {
            EXPECT_EQ(c.materials[i + 1], 8) << name;
            ++cobblestone;
        }
    }
    EXPECT_GT(deepwater, 0);
    EXPECT_GT(cobblestone, 0);
}

TEST_P(RetailSdl, EveryRoomCollectsNearItsPerimeter) {
    const auto& c = retailCity(GetParam());
    const auto& p = c.psdl;
    ASSERT_GT(p.rooms.size(), 1000u);
    SdlPolyBuffer all, near;
    std::size_t rooms = 0, polys = 0;
    for (std::size_t r = 1; r < p.rooms.size(); ++r) {
        all.reset(p);
        bool overflow = false;
        const int total = collectRoomPolygons(p, r, nullptr, c.materials, all, 100000, &overflow);
        EXPECT_FALSE(overflow);
        EXPECT_EQ(total, static_cast<int>(all.polys.size()));
        std::multiset<std::vector<float>> keys;
        for (const auto& poly : all.polys)
            keys.insert(key(all, poly));
        // A car-sized sphere at every perimeter corner, as lvlSDL::CollidePolyToLevel asks.
        for (const auto& corner : p.rooms[r].perimeter) {
            SdlSphere s{p.vertices[corner.vertex] + Vec3(0, 1, 0), 3.0f};
            near.reset(p);
            std::uint32_t state = 0;
            overflow = false;
            const int n = collectRoomPolygons(p, r, &s, c.materials, near, 256, &overflow, &state);
            ASSERT_FALSE(overflow) << "room " << r;
            EXPECT_EQ(state, 0u);
            EXPECT_GE(near.vertexBudget, 0) << "room " << r; // within lvlSDL's 0x200 spare vertices
            EXPECT_LE(n, total);
            for (const auto& poly : near.polys) {
                EXPECT_NEAR(poly.normal.mag(), 1.0f, 1e-4f);
                for (std::uint32_t v : poly.v)
                    ASSERT_LT(v, near.vertices.size());
                EXPECT_TRUE(keys.contains(key(near, poly))) << "room " << r << ": not in the unculled set";
            }
            polys += static_cast<std::size_t>(n);
        }
        ++rooms;
    }
    EXPECT_GT(polys, rooms);
}

TEST_P(RetailSdl, FarSphereCollectsNothing) {
    const auto& c = retailCity(GetParam());
    const auto& p = c.psdl;
    // Far enough that even the bay's 2 km water triangles are out of reach of
    // Collect's crude test (within sqrt(2) times the longest edge of a corner).
    const SdlSphere far{p.bounds.max + Vec3(1e5f, 0, 1e5f), 5.0f};
    SdlPolyBuffer buf;
    buf.reset(p);
    for (std::size_t r = 1; r < p.rooms.size(); ++r)
        EXPECT_EQ(collectRoomPolygons(p, r, &far, c.materials, buf, 256), 0) << "room " << r;
    EXPECT_EQ(buf.generatedVertexCount(), 0u);
}

TEST_P(RetailSdl, RoadSurfaceUnderRoadPoints) {
    const auto& c = retailCity(GetParam());
    const auto& p = c.psdl;
    int tried = 0, found = 0, up = 0;
    std::string missing;
    SdlPolyBuffer buf;
    for (std::size_t r = 1; r < p.rooms.size(); ++r) {
        const auto& room = p.rooms[r];
        if (!(room.flags & RoomFlag::Road))
            continue;
        int tex = 0;
        for (const auto& a : room.attributes) {
            if (a.type == PsdlAttrType::Texture)
                tex = a.textureBase() + 1;
            if (a.type != PsdlAttrType::RoadStrip || tex == 0 || a.vertices().size() < 8)
                continue;
            // The middle of the first road quad (curb L/R of sections 0 and 1).
            const auto v = a.vertices();
            const Vec3 point =
                (p.vertices[v[1]] + p.vertices[v[2]] + p.vertices[v[5]] + p.vertices[v[6]]) * 0.25f;
            SdlSphere s{point, 1.0f};
            buf.reset(p);
            collectRoomPolygons(p, r, &s, c.materials, buf, 256);
            ++tried;
            const int foundBefore = found;
            for (const auto& poly : buf.polys) {
                const auto k = corners(buf, poly);
                if (std::abs(poly.normal.y) < 0.3f)
                    continue; // a wall (London room 1013's ramp drops 4 m in 2.5 m: n.y = 0.5)
                // Point in polygon (xz), as two triangles.
                const auto inTri = [&](const Vec3& a0, const Vec3& b0, const Vec3& c0) {
                    const auto side = [&](const Vec3& e0, const Vec3& e1) {
                        return (e1.x - e0.x) * (point.z - e0.z) - (e1.z - e0.z) * (point.x - e0.x);
                    };
                    const float s0 = side(a0, b0), s1 = side(b0, c0), s2 = side(c0, a0);
                    return (s0 >= 0 && s1 >= 0 && s2 >= 0) || (s0 <= 0 && s1 <= 0 && s2 <= 0);
                };
                if (inTri(k[0], k[1], k[2]) || (k.size() == 4 && inTri(k[0], k[2], k[3]))) {
                    ++found;
                    up += poly.normal.y > 0 ? 1 : 0;
                    break;
                }
            }
            if (found == foundBefore)
                missing += " " + std::to_string(r);
            break; // one strip per room
        }
    }
    ASSERT_GT(tried, 100);
    EXPECT_EQ(found, tried) << "rooms:" << missing;
    EXPECT_GT(up, tried * 95 / 100);
}

INSTANTIATE_TEST_SUITE_P(Cities, RetailSdl, ::testing::Values("london", "sf"));
