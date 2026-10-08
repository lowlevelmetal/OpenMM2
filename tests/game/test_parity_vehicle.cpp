// Parity checks for the vehicle area (docs/parity/vehicle.md) that need the
// retail data: the player's car setup of mmPlayer::Init.

#include "TestData.h"
#include "game/PlayerVehicle.h"

#include <gtest/gtest.h>

using namespace mm2;

namespace {

bool sameSim(const phys::CarSim& a, const phys::CarSim& b) {
    if (a.params.engine.maxHorsePower != b.params.engine.maxHorsePower ||
        a.params.engine.optRPM != b.params.engine.optRPM || a.params.trans.high != b.params.trans.high ||
        a.params.mass != b.params.mass || a.params.boundFriction != b.params.boundFriction)
        return false;
    for (std::size_t i = 0; i < 4; ++i)
        if ((a.wheels[i].center - b.wheels[i].center).mag() != 0.0f || a.wheels[i].radius != b.wheels[i].radius)
            return false;
    return true;
}

} // namespace

// mmPlayer::Init re-runs vehCarSim::Init with "vpmustang99" when the player
// drives vpcop: the police car's physics are the Mustang's (tune and wheel
// pivots), while AI police keep their own.
TEST(VehicleParity, PlayerPoliceCarIsSimulatedAsTheMustang) {
    MM2_REQUIRE_GAME_DATA();
    const vfs::Vfs& fs = *test::gameData();
    std::string error;
    auto player = game::SimVehicle::loadPlayer(fs, "vpcop", &error);
    auto ai = game::SimVehicle::load(fs, "vpcop", &error);
    auto mustang = game::SimVehicle::load(fs, "vpmustang99", &error);
    ASSERT_TRUE(player && ai && mustang) << error;
    EXPECT_TRUE(sameSim(player->sim(), mustang->sim()));
    EXPECT_FALSE(sameSim(ai->sim(), mustang->sim()));
    // The model stays the police car's.
    EXPECT_EQ(player->model().baseName, "vpcop");
    // Any other car is the same for the player.
    auto bug = game::SimVehicle::loadPlayer(fs, "vpbug", &error);
    auto aiBug = game::SimVehicle::load(fs, "vpbug", &error);
    ASSERT_TRUE(bug && aiBug);
    EXPECT_TRUE(sameSim(bug->sim(), aiBug->sim()));
}
