// Parity checks for the city text formats against MM2's loaders (build 3393):
// numbers read with atoi / atof / sscanf, and aiCityData / aiRaceData lists.
#include "city/Race.h"
#include "city/Reader.h"

#include <gtest/gtest.h>

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
