// Parity checks for the city text formats against MM2's loaders (build 3393):
// numbers read with atoi / atof / sscanf, aiCityData / aiRaceData lists,
// mmCityInfo counts and dgPath records.
#include "TestData.h"
#include "city/CityData.h"
#include "city/Environment.h"
#include "city/Inst.h"
#include "city/PathSet.h"
#include "city/Psdl.h"
#include "city/Race.h"
#include "city/Reader.h"
#include "city/RoomInfo.h"
#include "city/RoomLocator.h"
#include "data/CNumbers.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace mm2;

// The city loaders read numbers through data/CNumbers.h (atoi, atof and
// sscanf's prefixes).
TEST(ParityCityText, NumbersReadLikeTheCLibrary) {
    using namespace data;
    EXPECT_EQ(cAtoi("12abc"), 12);
    EXPECT_EQ(cAtoi("  -7"), -7);
    EXPECT_EQ(cAtoi("0x10"), 0); // no hexadecimal
    EXPECT_EQ(cAtoi("3.9"), 3);
    EXPECT_EQ(cAtoi("abc"), 0);
    EXPECT_DOUBLE_EQ(cAtof("1.5e2xyz"), 150.0);
    EXPECT_DOUBLE_EQ(cAtof(".5\t# comment"), 0.5);
    EXPECT_DOUBLE_EQ(cAtof("2e"), 2.0);
    EXPECT_FALSE(atofPrefix("#0").has_value());
    EXPECT_FALSE(atoiPrefix("-").has_value());
    EXPECT_EQ(atoiPrefix("1 # on").value_or(-1), 1);
}

TEST(ParityCityText, AimapListsTakeTheirCount) {
    // [Exceptions] says 1 entry: the second line is not read (aiRaceData).
    const auto cfg = city::parseAiMapConfig("[Exceptions]\n1\n5 0.5 10\n6 0.5 12\n"
                                            "[Speed Limit]\n17.5 m/s\n"
                                            "[AmbientLaneChanges]\n0\n");
    ASSERT_TRUE(cfg);
    ASSERT_EQ(cfg->exceptions.size(), 1u);
    EXPECT_EQ(cfg->exceptions[0].road, 5);
    EXPECT_FLOAT_EQ(cfg->speedLimit.value_or(0.0f), 17.5f);
    EXPECT_EQ(cfg->ambientLaneChanges.value_or(-1), 0);
}

TEST(ParityCityText, NegativeAmbientProbabilityMeansEqualShares) {
    const auto cfg = city::parseAiMapConfig("[Ambient Types/Density]\n4\na -1 0\nb 0 0\nc 0 0\nd 0 0\n");
    ASSERT_TRUE(cfg);
    ASSERT_EQ(cfg->ambientTypes.size(), 4u);
    EXPECT_FLOAT_EQ(cfg->ambientTypes[0].cumulative, 0.25f);
    EXPECT_FLOAT_EQ(cfg->ambientTypes[3].cumulative, 1.0f);
}

// mmCityInfo::Load: a nonzero race count becomes the number of names; a zero
// count leaves the names unread.
TEST(ParityCityText, CityInfoCountsComeFromTheNames) {
    const auto info = city::parseCityInfo("LocalizedName=X\r\nMapName=x\r\nRaceDir=x\r\nBlitzCount=5\r\n"
                                          "CircuitCount=0\r\nCheckpointCount=1\r\nBlitzNames=A|B\r\n"
                                          "CircuitNames=C|D\r\nCheckpointNames=E|F|G\r\n");
    EXPECT_EQ(info.blitzCount, 2);
    EXPECT_EQ(info.circuitCount, 0);
    EXPECT_TRUE(info.circuitNames.empty());
    EXPECT_EQ(info.checkpointCount, 3);
}

// dgPath::Load: per point a flags word and then the position; the path ends
// with its type and spacing (quarter metres) bytes.
TEST(ParityCityText, PathSetReadsLikeDgPathLoad) {
    std::vector<std::byte> b;
    auto put = [&](const void* p, std::size_t n) {
        const auto* c = static_cast<const std::byte*>(p);
        b.insert(b.end(), c, c + n);
    };
    auto u32 = [&](std::uint32_t v) { put(&v, 4); };
    auto f32 = [&](float v) { put(&v, 4); };
    put("PTH1", 4);
    u32(1); // paths
    u32(0);
    char name[32] = "fence";
    put(name, 32);
    u32(2); // points
    u32(2); // dgPath +0x2c
    u32(0x11);
    f32(1);
    f32(2);
    f32(3);
    u32(0x22);
    f32(4);
    f32(5);
    f32(6);
    const std::uint8_t trailer[4] = {2, 8, 0, 0};
    put(trailer, 4);
    const auto set = city::parsePathSet(b);
    ASSERT_TRUE(set);
    const auto& path = set->paths.at(0);
    ASSERT_EQ(path.points.size(), 2u);
    EXPECT_FLOAT_EQ(path.points[1].position.x, 4.0f);
    ASSERT_EQ(path.flags.size(), 2u);
    EXPECT_EQ(path.flags[0], 0x11u);
    EXPECT_EQ(path.flags[1], 0x22u);
    EXPECT_EQ(path.type, 2);
    EXPECT_FLOAT_EQ(path.spacing, 2.0f);
}

// cityLevel::Load / lvlLevel::LoadInstances: lvlRoomInfo's flags come from
// the PSDL room flags, the first texture's material, the .water list, the
// terrain-bound instances and the "sf" warp rooms.
TEST(ParityCityRooms, LevelRoomFlagsFollowCityLevelLoad) {
    using namespace city;
    Psdl psdl;
    psdl.textures = {"road", "deep"};
    psdl.rooms.resize(8);
    auto texture = [](int value) {
        PsdlAttribute a;
        a.type = PsdlAttrType::Texture;
        a.args = {static_cast<std::uint16_t>(value)};
        return a;
    };
    auto plain = [](PsdlAttrType type, std::uint8_t subtype = 2) {
        PsdlAttribute a;
        a.type = type;
        a.subtype = subtype;
        a.args = {1, 2};
        return a;
    };
    psdl.rooms[1].flags = RoomFlag::Intersection;
    psdl.rooms[2].flags = RoomFlag::Intersection | RoomFlag::Warp;
    psdl.rooms[3].flags = RoomFlag::Road;
    psdl.rooms[3].attributes = {texture(1), plain(PsdlAttrType::RoadStrip)};
    psdl.rooms[4].flags = RoomFlag::Road;
    psdl.rooms[4].attributes = {texture(1), plain(PsdlAttrType::Tunnel, 3), plain(PsdlAttrType::RoadStrip)};
    psdl.rooms[5].flags = RoomFlag::Road;
    psdl.rooms[5].attributes = {plain(PsdlAttrType::DividedRoadStrip)};
    psdl.rooms[6].flags = RoomFlag::Subterranean;
    psdl.rooms[6].attributes = {texture(2)};
    psdl.rooms[7].attributes = {plain(PsdlAttrType::Facade), texture(2)};
    // Texture value 2 ("deep") has lvlMaterialMgr's second material.
    const std::vector<std::uint8_t> materials = {0, 3, 2};
    WaterDef water;
    water.rooms = {7, 0, 99};
    std::vector<Instance> inst(2);
    inst[0].room = 5;
    inst[0].flags = 0x100;
    inst[1].room = 3;
    inst[1].flags = 0x2000;
    const std::vector<Instance> none;
    const auto f = levelRoomFlags(psdl, materials, &water, inst, none, "london");
    ASSERT_EQ(f.size(), 8u);
    EXPECT_EQ(f[1], LevelRoomFlag::OpenRoad);
    EXPECT_EQ(f[2], 0);
    EXPECT_EQ(f[3], LevelRoomFlag::OpenRoad);
    EXPECT_EQ(f[4], 0); // a tunnel with low header bits before the road
    EXPECT_EQ(f[5], LevelRoomFlag::TerrainInstance);
    EXPECT_EQ(f[6], LevelRoomFlag::Subterranean | LevelRoomFlag::Covered | LevelRoomFlag::WaterOfDeath);
    EXPECT_EQ(f[7], LevelRoomFlag::WaterOfDeath); // from the .water list only

    Psdl big;
    big.rooms.resize(700);
    const auto sf = levelRoomFlags(big, {}, nullptr, none, none, "sf");
    for (const int room : {411, 412, 423, 625})
        EXPECT_EQ(sf[static_cast<std::size_t>(room)], LevelRoomFlag::Warp) << room;
    EXPECT_EQ(levelRoomFlags(big, {}, nullptr, none, none, "london")[411], 0);
}

TEST(ParityCityRooms, RetailLevelRoomFlags) {
    MM2_REQUIRE_GAME_DATA();
    for (const char* name : {"london", "sf"}) {
        auto c = city::loadCity(*test::gameData(), name);
        ASSERT_TRUE(c) << name;
        ASSERT_EQ(c->levelRoomFlags.size(), c->psdl.rooms.size());
        int water = 0, terrain = 0, covered = 0;
        for (std::size_t r = 1; r < c->levelRoomFlags.size(); ++r) {
            const auto f = c->levelRoomFlags[r];
            water += (f & city::LevelRoomFlag::WaterOfDeath) != 0;
            terrain += (f & city::LevelRoomFlag::TerrainInstance) != 0;
            covered += (f & city::LevelRoomFlag::Covered) != 0;
            // Covered only ever comes with Subterranean.
            EXPECT_EQ((f & city::LevelRoomFlag::Covered) != 0, (f & city::LevelRoomFlag::Subterranean) != 0);
        }
        ASSERT_TRUE(c->water) << name;
        for (const int room : c->water->rooms)
            EXPECT_TRUE(c->levelRoomFlags[static_cast<std::size_t>(room)] &
                        city::LevelRoomFlag::WaterOfDeath);
        EXPECT_GT(terrain, 0) << name;
        std::printf("%s: %d water-of-death rooms, %d terrain-instance rooms, %d covered rooms\n", name, water,
                    terrain, covered);
    }
}

// cityLevel::FindRoomId: the hint room, then its neighbours, then the grid;
// a position off every room is in room 0 whatever the hint.
TEST(ParityCityRooms, FindRoomIdGivesZeroOffEveryRoom) {
    using namespace city;
    Psdl psdl;
    psdl.vertices = {{0, 0, 0}, {10, 0, 0}, {20, 0, 0}, {20, 0, 10}, {10, 0, 10}, {0, 0, 10}};
    psdl.rooms.resize(3);
    psdl.rooms[1].flags = RoomFlag::Road;
    psdl.rooms[1].perimeter = {{0, 0}, {1, 2}, {4, 0}, {5, 0}};
    psdl.rooms[2].flags = RoomFlag::Road;
    psdl.rooms[2].perimeter = {{1, 0}, {2, 0}, {3, 0}, {4, 1}};
    const RoomLocator locator(psdl, "london");
    EXPECT_EQ(locator.find({5, 0, 5}, 0), 1);
    EXPECT_EQ(locator.find({15, 0, 5}, 1), 2);
    EXPECT_EQ(locator.find({5, 0, 5}, 2), 1);
    EXPECT_EQ(locator.find({50, 0, 50}, 1), 0);
    EXPECT_EQ(locator.find({-5, 0, 5}, 2), 0);
}
