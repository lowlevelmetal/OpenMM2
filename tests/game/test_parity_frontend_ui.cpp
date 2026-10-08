// Frontend rules checked against MM2's own code (MM2Recomp, midtown2.exe
// build 3393); see docs/parity/frontend-ui.md.
#include "game/Catalog.h"
#include "game/net/NetGame.h"

#include <gtest/gtest.h>

using namespace mm2;

// mmMultiCR::InitMyPlayer: in Free-For-All a car with the police flag
// (0x08) plays for team 0 and any other car for team 1.
TEST(FrontendParity, FreeForAllTeamFollowsThePoliceFlag) {
    game::VehicleInfo cop;
    cop.baseName = "vpcop";
    cop.flags = game::VehicleInfo::kFlagCop;
    game::VehicleInfo bus;
    bus.baseName = "vpbus";
    bus.flags = game::VehicleInfo::kFlagLarge;
    EXPECT_EQ(game::freeForAllTeam(&cop), 0);
    EXPECT_EQ(game::freeForAllTeam(&bus), 1);
    EXPECT_EQ(game::freeForAllTeam(nullptr), 1);
}
