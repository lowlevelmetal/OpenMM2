#include "TestData.h"
#include "game/Catalog.h"

#include <gtest/gtest.h>

using namespace mm2;

TEST(Catalog, ParsesVehicleInfo) {
    auto v = game::parseVehicleInfo("BaseName=vpbug\r\nDescription=VW New Beetle\r\nColors=Yellow|Blue\r\n"
                                    "Flags=64\r\nOrder=-1\r\nScoringBias=20.0\r\nUnlockScore=0\r\n"
                                    "UnlockFlags=0\r\nHorsepower=150\t\r\nTop Speed=91 \t\r\n"
                                    "Durability=760000\t\r\nMass=4250\r\nUIDist=5.5\r\n");
    ASSERT_TRUE(v);
    EXPECT_EQ(v->baseName, "vpbug");
    EXPECT_EQ(v->colors.size(), 2u);
    EXPECT_EQ(v->flags & game::VehicleInfo::kFlagBritish, game::VehicleInfo::kFlagBritish);
    EXPECT_EQ(v->topSpeedMph, 91);
    EXPECT_FLOAT_EQ(v->uiDistance, 5.5f);
    EXPECT_FALSE(game::parseVehicleInfo("Description=nothing\n"));
}

TEST(Catalog, RetailCatalog) {
    MM2_REQUIRE_GAME_DATA();
    const auto cat = game::Catalog::load(*test::gameData());
    EXPECT_EQ(cat.vehicles().size(), 20u);
    // mmVehList::LoadAll's built-in order.
    ASSERT_GE(cat.vehicles().size(), 20u);
    EXPECT_EQ(cat.vehicles()[0].baseName, "vpcoop");
    EXPECT_EQ(cat.vehicles()[1].baseName, "vpbug");
    EXPECT_EQ(cat.vehicles()[7].baseName, "vpbullet");
    EXPECT_EQ(cat.vehicles()[19].baseName, "vpsemi");
    ASSERT_TRUE(cat.vehicle("vpbug"));
    EXPECT_EQ(cat.vehicle("vpbug")->description, "VW New Beetle");
    ASSERT_TRUE(cat.city("london"));
    ASSERT_TRUE(cat.city("sf"));
    EXPECT_EQ(cat.city("london")->blitzNames.size(), 10u);
    EXPECT_EQ(cat.city("london")->checkpointNames.size(), 12u);
    EXPECT_EQ(cat.city("london")->blitzNames[0], "London's Calling");
}

#include "game/Strings.h"
#include "vfs/GameSource.h"

#include <cstdlib>

TEST(Strings, RetailStringTable) {
    MM2_REQUIRE_GAME_DATA();
    auto source = vfs::probeGameSource(std::getenv("OPENMM2_GAME_DATA"));
    ASSERT_TRUE(source);
    const auto s = game::Strings::load(*source);
    ASSERT_TRUE(s.loaded());
    EXPECT_EQ(s.get(game::Strings::kGo), "Go!");
    EXPECT_EQ(s.get(game::Strings::kFirstCrashCourseLesson), "London Leap");
    EXPECT_EQ(s.get(99999, "fallback"), "fallback");
}
