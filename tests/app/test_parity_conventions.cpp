// Round 3 of the parity audit, conventions: tuning values reach the code
// that uses them (MM2Recomp, build 3393). See
// docs/parity/round3/conventions.md.

#include "TestData.h"
#include "app/GameInput.h"
#include "data/DatFile.h"
#include "vfs/Vfs.h"

#include <gtest/gtest.h>

#include <string_view>

using namespace mm2;
using namespace mm2::app::controls;

// mmPlayer::FileIO: the player node's tune/<car>.asnode replaces the
// constructor's steering values field by field; fields it lacks keep them.
TEST(ConventionsParity, PlayerTuneReplacesTheSteeringDefaults) {
    constexpr std::string_view text = R"(type: a
asNode {
  SpeedSensitive 2
  SpeedBaseLow 5.000000
  MouseSensitivityLow 1.300000
  JoySensitivityLow 2.500000
  SpeedBaseHi 44.600002
  DiscreteSteeringDeltaOutLo 2.573000
  DiscreteSteeringDeltaInLo 5.000000
  DiscreteSteeringFilterLo 1.200000
  DiscreteSteeringDeltaOutHi 0.800000
  JoyApp 1
  JoySteerApproachOutLo 2.572000
  JoySteerAppApp 0.127000
  WheelSensitivityHi 2.000000
  WheelApp 0
  ScoreWeight 1.000000
}
)";
    auto dat = data::parseDat(text);
    ASSERT_TRUE(dat && dat->top());
    GameInput in;
    const phys::SteeringFilter::Params defaults;
    const AnalogSteering analogDefaults;
    in.setPlayerTune(*dat->top());
    for (const auto* p : {&in.discreteSteering(), &in.padSteering()}) {
        EXPECT_EQ(p->speedSensitive, 2);
        EXPECT_FLOAT_EQ(p->speedBaseHi, 44.600002f);
        EXPECT_FLOAT_EQ(p->deltaOutLo, 2.573f);
        EXPECT_FLOAT_EQ(p->deltaInLo, 5.0f);
        EXPECT_FLOAT_EQ(p->filterLo, 1.2f);
        EXPECT_FLOAT_EQ(p->deltaOutHi, 0.8f);
        EXPECT_FLOAT_EQ(p->deltaInHi, defaults.deltaInHi); // not in the file
        EXPECT_FLOAT_EQ(p->filterHi, defaults.filterHi);
    }
    const AnalogSteering& a = in.analogSteering();
    EXPECT_FLOAT_EQ(a.speedBaseHi, 44.600002f);
    EXPECT_FLOAT_EQ(a.mouseSensitivityLow, 1.3f);
    EXPECT_FLOAT_EQ(a.mouseSensitivityHi, analogDefaults.mouseSensitivityHi);
    EXPECT_FLOAT_EQ(a.joystick.sensitivityLow, 2.5f);
    EXPECT_TRUE(a.joystick.approach);
    EXPECT_FLOAT_EQ(a.joystick.approachOutLow, 2.572f);
    EXPECT_FLOAT_EQ(a.joystick.approachGrowth, 0.127f);
    EXPECT_FLOAT_EQ(a.wheel.sensitivityHi, 2.0f);
    EXPECT_FALSE(a.wheel.approach);
}

// The retail cars ship the file (vpbug's: SpeedBaseHi 44.6, keyboard
// DeltaOutLo 2.573); two do not and keep mmPlayer's constructor values.
TEST(ConventionsParity, RetailPlayerTunes) {
    MM2_REQUIRE_GAME_DATA();
    const vfs::Vfs& vfs = *test::gameData();
    auto bytes = vfs.readAll("tune/vpbug.asnode");
    ASSERT_TRUE(bytes);
    auto dat = data::parseDat(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    ASSERT_TRUE(dat && dat->top());
    GameInput in;
    in.setPlayerTune(*dat->top());
    EXPECT_FLOAT_EQ(in.discreteSteering().speedBaseHi, 44.600002f);
    EXPECT_FLOAT_EQ(in.discreteSteering().deltaOutLo, 2.573f);
    EXPECT_TRUE(in.analogSteering().joystick.approach);
    EXPECT_FALSE(vfs.exists("tune/vpcentury.asnode"));
}
