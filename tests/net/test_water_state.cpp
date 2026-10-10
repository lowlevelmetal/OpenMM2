// The water in a car's full state (protocol 14, net/PlayerCarState.h
// OwnCarState): the host decides when the water puts a car back, and a
// client carries on from the host's state with its vehSplash and the water
// handler's time.
#include "net/PlayerCarState.h"

#include <gtest/gtest.h>

#include <limits>

using namespace mm2;
using namespace mm2::net;

namespace {

CarStatesMsg ownOnly(const OwnCarState& own) {
    CarStatesMsg m;
    m.ack = 300;
    m.hasOwn = true;
    m.own = own;
    m.own.matrix = Mat34::identity();
    return m;
}

} // namespace

TEST(WaterState, TheSplashAndTheHandlersTimeTravelWithTheCar) {
    OwnCarState wet;
    wet.splash = true;
    wet.buoyancy = 0.61f;
    wet.waterLevel = -1.9f;
    wet.waterTime = 3.25f;
    CarStatesMsg out;
    ASSERT_TRUE(decodeMessage(encodeMessage(ownOnly(wet)), out));
    EXPECT_TRUE(out.own.splash);
    EXPECT_EQ(out.own.buoyancy, 0.61f);
    EXPECT_EQ(out.own.waterLevel, -1.9f);
    EXPECT_EQ(out.own.waterTime, 3.25f);
    // Out again (vehCar::Reset clears the latch, not the buoyancy).
    OwnCarState out2 = wet;
    out2.splash = false;
    out2.waterTime = 0.0f;
    ASSERT_TRUE(decodeMessage(encodeMessage(ownOnly(out2)), out));
    EXPECT_FALSE(out.own.splash);
    EXPECT_EQ(out.own.buoyancy, 0.61f);
    EXPECT_EQ(out.own.waterLevel, 0.0f);
    // A car that never met the water costs two bits more than before.
    const auto dry = encodeMessage(ownOnly(OwnCarState{}));
    const auto inWater = encodeMessage(ownOnly(wet));
    EXPECT_EQ(inWater.size(), dry.size() + 12);
    ASSERT_TRUE(decodeMessage(dry, out));
    EXPECT_FALSE(out.own.splash);
    EXPECT_EQ(out.own.buoyancy, 0.7f);
    EXPECT_EQ(out.own.waterTime, 0.0f);
}

TEST(WaterState, AWaterStateOutOfRangeIsRefused) {
    CarStatesMsg out;
    OwnCarState bad;
    bad.splash = true;
    bad.waterLevel = 1.0e6f;
    EXPECT_FALSE(decodeMessage(encodeMessage(ownOnly(bad)), out));
    bad = {};
    bad.waterTime = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(decodeMessage(encodeMessage(ownOnly(bad)), out));
    bad = {};
    bad.buoyancy = 1.0e9f;
    EXPECT_FALSE(decodeMessage(encodeMessage(ownOnly(bad)), out));
}
