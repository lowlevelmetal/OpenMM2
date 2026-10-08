// Parity checks for the frame clock (docs/parity/openmm2-only.md).
#include "platform/Clock.h"
#include "platform/Platform.h"

#include <gtest/gtest.h>

using namespace mm2::platform;

// datTimeManager::Update holds every frame's time step to ClampMin .. ClampMax.
TEST(FrameClockParity, ClampsToMM2FrameLimits) {
    EXPECT_FLOAT_EQ(kMinFrameSeconds, 0.0001f);
    EXPECT_FLOAT_EQ(kMaxFrameSeconds, 0.1f);
    ASSERT_TRUE(init({.video = false, .gamepad = false}));
    FrameClock clock;
    sleepPrecise(0.25); // a hitch longer than a tenth of a second
    EXPECT_FLOAT_EQ(static_cast<float>(clock.tick()), 0.1f);
    const double next = clock.tick(); // back to back: at least ClampMin
    EXPECT_GE(next, static_cast<double>(kMinFrameSeconds));
    EXPECT_LT(next, 0.05);
    shutdown();
}
