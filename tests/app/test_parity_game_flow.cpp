// The player's inputs as mmGame::UpdateSteeringBrakes reads them back from
// mmReplayManager's frame buffer. See docs/parity/mm2/game-flow.md.

#include "app/Controls.h"

#include <gtest/gtest.h>

using namespace mm2::app::controls;

TEST(GameFlowControls, KeyboardInputsPassUnchanged) {
    const auto q = replayQuantize(1.0f, 1.0f, 0.0f, 1.0f);
    EXPECT_EQ(q.steering, 1.0f);
    EXPECT_EQ(q.throttle, 1.0f);
    EXPECT_EQ(q.brakes, 0.0f);
    EXPECT_EQ(q.handbrake, 1.0f);
    EXPECT_EQ(replayQuantize(-1.0f, 0.0f, 1.0f, 0.0f).steering, -1.0f);
}

TEST(GameFlowControls, AnalogInputsSnapTowardsZero) {
    // 0.5 x 127 = 63.5 -> 63; 63 x 0.007874016.
    EXPECT_FLOAT_EQ(replayQuantize(0.5f, 0, 0, 0).steering, 63.0f * 0.007874015718698502f);
    EXPECT_FLOAT_EQ(replayQuantize(-0.5f, 0, 0, 0).steering, -63.0f * 0.007874015718698502f);
    // Below 1/127 the steering is 0.
    EXPECT_EQ(replayQuantize(0.0078f, 0, 0, 0).steering, 0.0f);
    // 0.3 x 255 = 76.5 -> 76.
    EXPECT_FLOAT_EQ(replayQuantize(0, 0.3f, 0, 0).throttle, 76.0f * 0.003921568859368563f);
    EXPECT_EQ(replayQuantize(0, 0.0039f, 0, 0).throttle, 0.0f);
}
