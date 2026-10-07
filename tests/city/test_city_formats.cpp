// Unit tests for the city/race parsers using small synthetic inputs.
#include "city/AiMap.h"
#include "city/CityMesh.h"
#include "city/Environment.h"
#include "city/Inst.h"
#include "city/PathSet.h"
#include "city/Psdl.h"
#include "city/Pvs.h"
#include "city/Race.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>

using namespace mm2;
using namespace mm2::city;

namespace {

// Little-endian byte builder.
struct Bytes {
    std::vector<std::byte> b;
    Bytes& raw(const void* p, std::size_t n) {
        const auto* c = static_cast<const std::byte*>(p);
        b.insert(b.end(), c, c + n);
        return *this;
    }
    Bytes& tag(const char* s) { return raw(s, 4); }
    Bytes& u8(std::uint8_t v) { return raw(&v, 1); }
    Bytes& u16(std::uint16_t v) { return raw(&v, 2); }
    Bytes& u32(std::uint32_t v) { return raw(&v, 4); }
    Bytes& f32(float v) { return raw(&v, 4); }
    Bytes& vec(float x, float y, float z) { return f32(x).f32(y).f32(z); }
    Bytes& str(const char* s, std::size_t fixed) {
        std::vector<char> buf(fixed, 0);
        std::memcpy(buf.data(), s, std::min(fixed, std::strlen(s)));
        return raw(buf.data(), fixed);
    }
};

// A one-room PSDL: a road strip of two sections with a facade and roof.
std::vector<std::byte> makePsdl() {
    Bytes p;
    p.tag("PSD0").u32(2);
    // Vertices: road section 0 (o1,i1,i2,o2) at z=0, section 1 at z=10, plus
    // a building corner pair.
    const float xs[4] = {-6, -4, 4, 6};
    const float ys[4] = {0, -0.15f, -0.15f, 0};
    p.u32(10);
    for (int s = 0; s < 2; ++s)
        for (int i = 0; i < 4; ++i)
            p.vec(xs[i], ys[i], s * 10.0f);
    p.vec(10, 0, 0).vec(10, 0, 10);
    p.u32(2).f32(0.0f).f32(12.0f); // heights
    // 3 texture names (count stored + 1): road group then a wall.
    p.u32(4);
    for (const char* t : {"r_road", "swalk", "r_road_lo"}) {
        p.u8(static_cast<std::uint8_t>(std::strlen(t) + 1));
        p.raw(t, std::strlen(t) + 1);
    }
    p.u32(2).u32(1); // rooms (incl. dummy), firstRoadRoom
    // Room 1: perimeter 4 points, attributes.
    std::vector<std::uint16_t> attrs = {
        0x0050, 1,                      // Texture value 1 -> base 0
        0x0002, 0, 1, 2, 3, 4, 5, 6, 7, // RoadStrip, 2 sections
        0x005E, 0, 1, 2, 1, 8, 9,       // Facade bottom h0 top h1, repeats 2x1, v8->v9
        0x00E2, 1, 8, 9, 3,             // Roof (last), subtype 2: height + 3 vertices
    };
    p.u32(4).u32(static_cast<std::uint32_t>(attrs.size()));
    for (std::uint16_t v : {0, 0, 3, 0, 7, 0, 4, 0})
        p.u16(v);
    for (auto w : attrs)
        p.u16(w);
    p.u8(0).u8(RoomFlag::Road); // flags
    p.u8(0).u8(5);              // prop rules
    p.vec(-6, -0.15f, 0).vec(10, 12, 10).vec(2, 6, 5).f32(9.0f);
    // One road with one value per side and one room.
    p.u32(1).u8(0x40).u8(4).u16(1).u8(1).u8(1).f32(0.5f).f32(0.25f).u8(2).u8(1);
    for (std::uint16_t v : {0, 1, 2, 3, 4, 5, 6, 7})
        p.u16(v);
    p.u8(1).u16(1);
    return p.b;
}

} // namespace

TEST(Psdl, ParsesSyntheticFile) {
    const auto data = makePsdl();
    std::string err;
    auto psdl = parsePsdl(data, &err);
    ASSERT_TRUE(psdl) << err;
    EXPECT_EQ(psdl->vertices.size(), 10u);
    EXPECT_EQ(psdl->heights.size(), 2u);
    ASSERT_EQ(psdl->textures.size(), 3u);
    EXPECT_EQ(psdl->textures[1], "swalk");
    ASSERT_EQ(psdl->roomCount(), 2u);
    const auto& room = psdl->rooms[1];
    EXPECT_EQ(room.perimeter.size(), 4u);
    EXPECT_EQ(room.flags, RoomFlag::Road);
    EXPECT_EQ(room.propRule, 5);
    ASSERT_EQ(room.attributes.size(), 4u);
    EXPECT_EQ(room.attributes[0].type, PsdlAttrType::Texture);
    EXPECT_EQ(room.attributes[0].textureBase(), 0);
    EXPECT_EQ(room.attributes[1].type, PsdlAttrType::RoadStrip);
    EXPECT_EQ(room.attributes[1].vertices().size(), 8u); // count word stripped
    EXPECT_EQ(room.attributes[2].type, PsdlAttrType::Facade);
    EXPECT_EQ(room.attributes[2].facadeTop(), 1);
    EXPECT_EQ(room.attributes[2].wallLeft(), 8);
    EXPECT_EQ(room.attributes[3].type, PsdlAttrType::RoofTriangleFan);
    EXPECT_TRUE(room.attributes[3].last);
    EXPECT_EQ(room.attributes[3].roofHeight(), 1);
    EXPECT_EQ(room.attributes[3].vertices().size(), 3u);
    ASSERT_EQ(psdl->roads.size(), 1u);
    EXPECT_FLOAT_EQ(psdl->roads[0].rightValues.at(0), 0.25f);
    EXPECT_EQ(psdl->roads[0].rooms.at(0), 1);
    EXPECT_FLOAT_EQ(psdl->sphereRadius, 9.0f);
    EXPECT_TRUE(validatePsdl(*psdl).empty());
}

TEST(Psdl, RejectsTruncatedAndTrailingData) {
    auto data = makePsdl();
    auto shortData = data;
    shortData.resize(shortData.size() - 3);
    EXPECT_FALSE(parsePsdl(shortData));
    data.push_back(std::byte{0});
    std::string err;
    EXPECT_FALSE(parsePsdl(data, &err));
    EXPECT_NE(err.find("trailing"), std::string::npos);
    EXPECT_FALSE(parsePsdl(std::vector<std::byte>(16)));
}

TEST(Psdl, DecodesCountWordsAndDetectsOverrun) {
    std::vector<PsdlAttribute> out;
    // Subtype 0 sidewalk strip with an explicit count of 2 pairs.
    const std::uint16_t words[] = {0x0008, 2, 10, 11, 12, 13, 0x008A, 1, 2, 3, 4};
    ASSERT_TRUE(decodePsdlAttributes(words, out));
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].type, PsdlAttrType::SidewalkStrip);
    EXPECT_EQ(out[0].vertices().size(), 4u);
    EXPECT_EQ(out[1].type, PsdlAttrType::SidewalkStrip);
    EXPECT_TRUE(out[1].last);
    const std::uint16_t bad[] = {0x0058, 7, 1, 2}; // facade needs 6 args
    EXPECT_FALSE(decodePsdlAttributes(bad, out));
    const std::uint16_t unknown[] = {0x0070}; // type 14
    EXPECT_FALSE(decodePsdlAttributes(unknown, out));
}

TEST(Psdl, TunnelAndDividerFields) {
    std::vector<PsdlAttribute> out;
    const std::uint16_t words[] = {0x004B, 0x0103, 0x0580, 0x0700, // tunnel, 3 words
                                   0x0040, 2,      0x90C2, 38,     1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    ASSERT_TRUE(decodePsdlAttributes(words, out));
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].tunnelFlags(), 0x0103);
    EXPECT_FLOAT_EQ(out[0].tunnelHeight1(), 5.5f);
    EXPECT_FLOAT_EQ(out[0].tunnelHeight2(), 7.0f);
    EXPECT_EQ(out[1].type, PsdlAttrType::DividedRoadStrip);
    EXPECT_EQ(out[1].dividerType(), 2);
    EXPECT_TRUE(out[1].dividerCapStart());
    EXPECT_TRUE(out[1].dividerCapEnd());
    EXPECT_EQ(out[1].dividerTexture(), 0x90);
    EXPECT_NEAR(out[1].dividerHeight(), 0.148f, 1e-3f);
    EXPECT_EQ(out[1].vertices().size(), 12u);
}

TEST(CityMesh, BuildsRoadSidewalkFacadeAndRoof) {
    const auto data = makePsdl();
    auto psdl = parsePsdl(data);
    ASSERT_TRUE(psdl);
    const auto mesh = buildCityMesh(*psdl);
    ASSERT_EQ(mesh.rooms.size(), 2u);
    const auto& room = mesh.rooms[1];
    auto find = [&](SurfaceKind k) -> const CityBatch* {
        for (const auto& b : room.batches)
            if (b.kind == k)
                return &b;
        return nullptr;
    };
    const auto* road = find(SurfaceKind::Road);
    ASSERT_TRUE(road);
    EXPECT_EQ(road->texture, 0);
    EXPECT_EQ(road->indices.size(), 6u);
    for (const auto& v : road->vertices)
        EXPECT_GT(v.normal.y, 0.99f);
    const auto* walk = find(SurfaceKind::Sidewalk);
    ASSERT_TRUE(walk);
    EXPECT_EQ(walk->texture, 1);          // group offset +1
    EXPECT_EQ(walk->indices.size(), 12u); // both sides
    const auto* curb = find(SurfaceKind::Curb);
    ASSERT_TRUE(curb);
    // Left curb at x=-4 faces the road (+X), right curb at x=4 faces -X.
    for (const auto& v : curb->vertices)
        EXPECT_LT(v.position.x * v.normal.x, 0.0f);
    const auto* wall = find(SurfaceKind::Wall);
    ASSERT_TRUE(wall);
    // Facade v8 (10,0,0) -> v9 (10,0,10): outward normal = (v9-v8) x up = (-10,0,0) direction.
    for (const auto& v : wall->vertices) {
        EXPECT_NEAR(v.normal.x, -1.0f, 1e-5f);
        EXPECT_TRUE(v.position.y == 0.0f || v.position.y == 12.0f);
    }
    const auto* roof = find(SurfaceKind::Roof);
    ASSERT_TRUE(roof);
    for (const auto& v : roof->vertices)
        EXPECT_FLOAT_EQ(v.position.y, 12.0f);
    // Counter-clockwise front faces: (b-a)x(c-a) agrees with the stored normal.
    for (const auto& b : room.batches)
        for (std::size_t i = 0; i < b.indices.size(); i += 3) {
            const auto& a = b.vertices[b.indices[i]];
            const auto& c1 = b.vertices[b.indices[i + 1]];
            const auto& c2 = b.vertices[b.indices[i + 2]];
            EXPECT_GT((c1.position - a.position).cross(c2.position - a.position).dot(a.normal), 0.0f);
        }
}

TEST(Inst, ParsesCompactAndMatrixRecords) {
    Bytes b;
    b.u16(7).u16(2).u8(0x80 | 4).raw("abc", 4).f32(0.6f).f32(0.8f).vec(1, 2, 3);
    b.u16(9).u16(0).u8(4).raw("xyz", 4).vec(1, 0, 0).vec(0, 1, 0).vec(0, 0, 1).vec(4, 5, 6);
    std::string err;
    auto inst = parseInst(b.b, &err);
    ASSERT_TRUE(inst) << err;
    ASSERT_EQ(inst->size(), 2u);
    EXPECT_TRUE((*inst)[0].rotY);
    EXPECT_EQ((*inst)[0].name, "abc");
    EXPECT_EQ((*inst)[0].room, 7);
    EXPECT_FLOAT_EQ((*inst)[0].transform.m0.x, 0.6f);
    EXPECT_FLOAT_EQ((*inst)[0].transform.m2.x, -0.8f); // Z = X x Y
    EXPECT_FLOAT_EQ((*inst)[0].transform.m3.z, 3.0f);
    EXPECT_FALSE((*inst)[1].rotY);
    EXPECT_FLOAT_EQ((*inst)[1].transform.m3.y, 5.0f);
    b.b.pop_back();
    EXPECT_FALSE(parseInst(b.b));
}

TEST(Pvs, DecodesRleRows) {
    // 3 rooms (incl. dummy). Room 1 sees rooms 1 and 2; room 2 sees room 2.
    // Room 1 row: byte 0 = 0b00111100 (rooms 1,2 -> level 3) as a literal.
    // Room 2 row: run of one byte 0b00110000.
    Bytes b;
    b.tag("PVS0").u32(4);
    b.u32(0).u32(2).u32(4); // offsets: row1 [0,2), row2 [2,4), end 4
    b.u8(0x80).u8(0x3C);    // literal 1 byte
    b.u8(0x01).u8(0x30);    // run of 1
    std::string err;
    auto pvs = parseCpvs(b.b, &err);
    ASSERT_TRUE(pvs) << err;
    EXPECT_EQ(pvs->roomCount(), 3u);
    EXPECT_TRUE(pvs->visible(1, 1));
    EXPECT_TRUE(pvs->visible(1, 2));
    EXPECT_FALSE(pvs->visible(2, 1));
    EXPECT_EQ(pvs->level(2, 2), 3);
    EXPECT_EQ(pvs->visibleFrom(1).size(), 2u);
    b.b[b.b.size() - 2] = std::byte{0x05}; // run longer than the row allows
    EXPECT_FALSE(parseCpvs(b.b));
}

TEST(PathSet, ParsesPaths) {
    Bytes b;
    b.tag("PTH1").u32(1).u32(0);
    b.str("sp_barricade", 32).u32(2).u32(0).u32(0x208);
    b.vec(1, 2, 3).u32(5).vec(4, 5, 6).u32(6);
    std::string err;
    auto ps = parsePathSet(b.b, &err);
    ASSERT_TRUE(ps) << err;
    ASSERT_EQ(ps->paths.size(), 1u);
    EXPECT_EQ(ps->paths[0].name, "sp_barricade");
    ASSERT_EQ(ps->paths[0].points.size(), 2u);
    EXPECT_FLOAT_EQ(ps->paths[0].points[1].position.z, 6.0f);
    b.u8(0);
    EXPECT_FALSE(parsePathSet(b.b));
}

TEST(AiMap, ParsesMinimalMap) {
    // One path with 2 sections, no lanes on either side, two intersections.
    Bytes b;
    b.tag("CAI1").u16(2).u16(1);
    b.u16(0).u16(2).u16(8).u16(1).u16(1).f32(4.0f).f32(15.0f);
    for (int side = 0; side < 2; ++side) {
        b.u16(side).u16(0).u16(0).u16(1).u16(0).u16(0).u16(0); // left: 0 lanes, right: 1 lane
        const int lanes = side;
        for (int e = 0; e <= lanes; ++e)
            b.f32(10.0f).f32(0.0f); // 1 length + end value
        for (int l = 0; l < lanes; ++l)
            b.f32(3.5f);
        for (int i = 0; i < 10; ++i)
            b.f32(0.0f);
        for (int line = 0; line < 3 + lanes; ++line)
            b.vec(0, 0, 0).vec(0, 0, 10);
    }
    b.u32(0).f32(10.0f);
    for (int arr = 0; arr < 5; ++arr)
        b.vec(0, 0, 0).vec(0, 0, 10);
    for (int end = 0; end < 2; ++end)
        b.u32(static_cast<std::uint32_t>(end))
            .u16(0xCDCD)
            .u16(3)
            .u16(0)
            .u16(0)
            .u16(0)
            .vec(0, 0, 0)
            .vec(0, 0, 0);
    for (int i = 0; i < 2; ++i)
        b.u16(static_cast<std::uint16_t>(i)).u16(1).vec(0, 0, i * 10.0f).u16(1).u32(0);
    b.u32(2).u16(0).u16(1).u16(0).u16(0).u16(1).u16(0);
    std::string err;
    auto map = parseBai(b.b, &err);
    ASSERT_TRUE(map) << err;
    ASSERT_EQ(map->paths.size(), 1u);
    const auto& p = map->paths[0];
    EXPECT_FLOAT_EQ(p.speedLimit, 15.0f);
    EXPECT_EQ(p.right.numLanes, 1);
    EXPECT_EQ(p.right.polylines.size(), 4u);
    EXPECT_EQ(p.left.polylines.size(), 3u);
    EXPECT_EQ(p.ends[1].intersection, 1u);
    EXPECT_EQ(p.ends[0].vehicleRule, 3);
    EXPECT_EQ(map->intersections.size(), 2u);
    EXPECT_EQ(map->roomPathsNear.at(1).size(), 1u);
    EXPECT_TRUE(validateAiMap(*map, 2).empty());
}

TEST(Race, ParsesTextFormats) {
    auto info = parseCityInfo("LocalizedName=London\r\nMapName=london\r\nRaceDir=london\r\nBlitzCount=2\r\n"
                              "BlitzNames=A|B\r\nMustPlace=3\r\n");
    EXPECT_EQ(info.mapName, "london");
    EXPECT_EQ(info.blitzNames.size(), 2u);

    auto wp = parseWaypoints("x,y,z,a,radius,frame rate,state changes,texture changes,msg\r\n"
                             "1.5,2,3,90,15,0,0,0,\r\n4,5,6,-45,11,0,0,0,\r\n");
    ASSERT_TRUE(wp);
    ASSERT_EQ(wp->size(), 2u);
    EXPECT_FLOAT_EQ((*wp)[0].position.x, 1.5f);
    EXPECT_FLOAT_EQ((*wp)[1].radius, 11.0f);

    auto opp =
        parseOpponentPath("x,y,z,brake,forward offset,side offset,target speed,speed start,side start\n"
                          "-770.7,-0.15,151.3,-75,0,0,0,0,0\n");
    ASSERT_TRUE(opp);
    EXPECT_FLOAT_EQ(opp->at(0).brake, -75.0f);

    auto cfg = parseAiMapConfig("# comment\n[Density]\n.1\n[Speed Limit]\n15\n[Exceptions]\n1\n413\t0.00\t0\n"
                                "[Police]\n1\nvpcop\t1 2 3 90 0 15 0.5 50\n[CopChaseDistance]\n150\n"
                                "[Opponent]\n1\nvpcoop race0-a-0.opp 1.00 0 50.0\n"
                                "[Traffic Lights]\nsp_a sp_b\n[Ambient Types/Density]\n1\nva_cab_l 0.5 0\n");
    ASSERT_TRUE(cfg);
    EXPECT_FLOAT_EQ(*cfg->density, 0.1f);
    EXPECT_FLOAT_EQ(*cfg->speedLimit, 15.0f);
    EXPECT_FLOAT_EQ(*cfg->copChaseDistance, 150.0f);
    ASSERT_EQ(cfg->exceptions.size(), 1u);
    EXPECT_EQ(cfg->exceptions[0].road, 413);
    ASSERT_EQ(cfg->police.size(), 1u);
    EXPECT_FLOAT_EQ(cfg->police[0].heading, 90.0f);
    ASSERT_EQ(cfg->opponents.size(), 1u);
    EXPECT_EQ(cfg->opponents[0].pathFile, "race0-a-0.opp");
    EXPECT_EQ(cfg->trafficLights.size(), 2u);
    EXPECT_EQ(cfg->ambientTypes.at(0).model, "va_cab_l");

    auto table =
        parseRaceTable("Description, CarType, TimeofDay, Weather, Opponents, Cops, Ambient, Peds, NumLaps, "
                       "TimeLimit, Difficulty, CarType, TimeofDay, Weather, Opponents, Cops, Ambient, Peds, "
                       "NumLaps, TimeLimit, Difficulty\n"
                       "none,0,0,0,4,0,0.1,0,3,50,1,0,0,1,6,0,0.2,0,4,40,1\n");
    ASSERT_TRUE(table);
    EXPECT_EQ(table->at(0).amateur.opponents, 4);
    EXPECT_EQ(table->at(0).professional.numLaps, 4);
    EXPECT_FLOAT_EQ(table->at(0).professional.ambientDensity, 0.2f);

    auto crash =
        parseCrashEvents("Filename,Event,Checkpoints,TimeLimit,AmbDensity,extra\nfinal1,2,1,200,0,0,0,1\n");
    ASSERT_TRUE(crash);
    EXPECT_EQ(crash->at(0).file, "final1");
    EXPECT_FLOAT_EQ(crash->at(0).timeLimit, 200.0f);

    auto rewards =
        parseRewards("RaceType,RaceNum,CarName,VariantNum\nblitz,half,vpcoop2k,0,Hello, world!,\n");
    ASSERT_EQ(rewards.size(), 1u);
    EXPECT_EQ(rewards[0].message, "Hello, world!");
}

TEST(Environment, ParsesFiles) {
    auto lt = parseLighting("type: a\nclear-morning {\n  KeyHeading 2.2\n  KeyColor 0.9 0.9 0.8\n"
                            "  Ambient -14803406\n}\n");
    ASSERT_TRUE(lt);
    EXPECT_EQ(lt->name, "clear-morning");
    EXPECT_FLOAT_EQ(lt->keyColor.z, 0.8f);
    EXPECT_EQ(lt->ambient, 0xFF1E1E32u);

    auto fog =
        parseFogTable("fog red,fog green,fog blue,fog start,fog end,description\n225,220,215,220,320,x\n");
    ASSERT_TRUE(fog);
    EXPECT_EQ(fog->at(0).g, 220);
    EXPECT_FLOAT_EQ(fog->at(0).end, 320.0f);

    EXPECT_EQ(parseSky("sky_dome_l 0 0.95 0.005\n")->params.size(), 3u);
    auto water = parseWater("-3.8\n345\n351\n");
    ASSERT_TRUE(water);
    EXPECT_FLOAT_EQ(water->height, -3.8f);
    EXPECT_EQ(water->rooms.size(), 2u);
    EXPECT_FLOAT_EQ(parseExtent("-1 -2 3 4\n")->maxZ, 4.0f);
    auto reset = parseResetPoints("-1835.8\t45.4\t631.6\t\t# GG fallthru\n-1872 20 485\n");
    ASSERT_EQ(reset.size(), 2u);
    EXPECT_EQ(reset[0].comment, "GG fallthru");

    auto mtl = parseMaterialLibrary("mtl grass {\n\telasticity: 0.9\n\tfriction: 0.8\n\teffect: none\n"
                                    "\tsound: 2\n\tptxindex: 1 2\n}\n");
    ASSERT_TRUE(mtl);
    ASSERT_EQ(mtl->size(), 1u);
    EXPECT_EQ((*mtl)[0].name, "grass");
    EXPECT_FLOAT_EQ((*mtl)[0].friction, 0.8f);
    EXPECT_EQ((*mtl)[0].ptxIndex[1], 2);

    Bytes lm;
    lm.tag("LMP0").u32(2).u32(0xFFFFFEF5).u32(1);
    auto colors = parseLightMap(lm.b);
    ASSERT_TRUE(colors);
    EXPECT_EQ(colors->at(0), 0xFFFFFEF5u);
    lm.u8(0);
    EXPECT_FALSE(parseLightMap(lm.b));
}
