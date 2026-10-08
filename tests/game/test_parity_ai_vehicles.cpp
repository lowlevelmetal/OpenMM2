// Parity tests for the AI drivers on physics cars (ai-vehicles audit).
#include "TestData.h"
#include "ai/MapView.h"
#include "ai/Police.h"
#include "ai/RoadNetwork.h"
#include "game/PlayerVehicle.h"
#include "phys/World.h"

#include <gtest/gtest.h>

using namespace mm2;

// aiPoliceOfficer::Update: a cop whose car is in a room with lvlRoomInfo
// flag 4 drops out (PerpEscapes, then out of action).
TEST(ParityPoliceRooms, ACopInAWaterRoomDropsOut) {
    MM2_REQUIRE_GAME_DATA();
    std::string error;
    auto cop = game::SimVehicle::load(*test::gameData(), "vpcop", &error);
    ASSERT_TRUE(cop) << error;
    phys::World world;
    cop->addTo(world);
    const Mat34 post = Mat34::identity();
    cop->reset(post);

    const ai::RoadNetwork net;
    const ai::MapView map(net);
    ai::PoliceSquad police(map);
    ai::PoliceCar& officer = police.add(cop->sim(), post, 100);
    std::vector<std::uint16_t> flags(10, 0);
    flags[6] = 0x01 | 0x02 | 0x08; // other lvlRoomInfo flags
    flags[7] = 0x04;               // water of death
    police.setRoomFlags(flags);

    const std::vector<ai::TrackedCar> cars{ai::trackedCar(cop->sim(), 100, false)};
    cop->sim().body.room = 6;
    police.update(1.0f / 30.0f, cars, nullptr, true);
    EXPECT_EQ(officer.mode(), ai::PoliceCar::Mode::Parked);
    cop->sim().body.room = 7;
    police.update(1.0f / 30.0f, cars, nullptr, true);
    EXPECT_EQ(officer.mode(), ai::PoliceCar::Mode::Disabled);
    // Out of action until it is reset.
    cop->sim().body.room = 6;
    police.update(1.0f / 30.0f, cars, nullptr, true);
    EXPECT_EQ(officer.mode(), ai::PoliceCar::Mode::Disabled);
    police.reset();
    police.update(1.0f / 30.0f, cars, nullptr, true);
    EXPECT_EQ(officer.mode(), ai::PoliceCar::Mode::Parked);
}
