// What NetGame does with settings and car names that come from other
// machines (docs/review/multiplayer-input.md).
#include "game/Catalog.h"
#include "game/net/NetGame.h"

#include "TestData.h"

#include <gtest/gtest.h>

using namespace mm2;

// The densities travel as percent in a byte: a host could send up to 255.
TEST(NetGameInput, DensitiesFromTheNetworkAreAtMostFull) {
    net::SessionSettings s;
    s.trafficDensity = 255;
    s.pedDensity = 101;
    const game::RaceConfig c = game::fromSessionSettings(s);
    EXPECT_FLOAT_EQ(c.trafficDensity, 1.0f);
    EXPECT_FLOAT_EQ(c.pedestrianDensity, 1.0f);
    s.trafficDensity = 30;
    EXPECT_FLOAT_EQ(game::fromSessionSettings(s).trafficDensity, 0.3f);
}

// mmVehList::GetVehicleInfo: another player's car this machine does not have
// is shown as the default vehicle, vpcoop.
TEST(NetGameInput, UnknownRemoteCarIsTheDefaultVehicle) {
    MM2_REQUIRE_GAME_DATA();
    const auto catalog = game::Catalog::load(*test::gameData());
    EXPECT_EQ(game::netVehicle(catalog, "vpbug"), "vpbug");
    EXPECT_EQ(game::netVehicle(catalog, "VPMUSTANG99"), "VPMUSTANG99"); // the list ignores case
    EXPECT_EQ(game::netVehicle(catalog, "vpaddon"), "vpcoop");
    EXPECT_EQ(game::netVehicle(catalog, ""), "vpcoop");
}
