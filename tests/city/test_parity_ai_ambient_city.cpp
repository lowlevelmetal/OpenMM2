// Parity checks for the city text formats against MM2's loaders (build 3393):
// numbers read with atoi / atof / sscanf, aiCityData / aiRaceData lists,
// mmCityInfo counts and dgPath records.
#include "city/PathSet.h"
#include "city/Race.h"
#include "city/Reader.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using namespace mm2;

TEST(ParityCityText, NumbersReadLikeTheCLibrary) {
    using namespace city::detail;
    EXPECT_EQ(cAtoi("12abc"), 12);
    EXPECT_EQ(cAtoi("  -7"), -7);
    EXPECT_EQ(cAtoi("0x10"), 0); // no hexadecimal
    EXPECT_EQ(cAtoi("3.9"), 3);
    EXPECT_EQ(cAtoi("abc"), 0);
    EXPECT_FLOAT_EQ(cAtof("1.5e2xyz"), 150.0f);
    EXPECT_FLOAT_EQ(cAtof(".5\t# comment"), 0.5f);
    EXPECT_FLOAT_EQ(cAtof("2e"), 2.0f);
    EXPECT_FALSE(scanFloat("#0").has_value());
    EXPECT_FALSE(scanInt("-").has_value());
    EXPECT_EQ(scanInt("1 # on").value_or(-1), 1);
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
