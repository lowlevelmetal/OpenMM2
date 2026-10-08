// mmGame::Reset's music (StartMusic after a restart) and the popup's pause
// music, as the game modes drive MusicDirector. See
// docs/parity/mm2/game-flow.md.

#include "audio/MusicDirector.h"

#include <gtest/gtest.h>

using namespace mm2::audio;

namespace {

void run(MusicDirector& d, float seconds) {
    for (float t = 0.0f; t < seconds; t += 1.0f / 60.0f)
        d.update(1.0f / 60.0f, 20.0f, 0, false);
}

} // namespace

// A wreck stops the music; a restart (mmGame::Reset -> StartMusic) plays the
// Start segment again at once once the music has been running 1.25 s.
TEST(GameFlowMusic, RestartStartsTheMusicAgain) {
    MusicDirector d(false);
    run(d, 2.0f);
    ASSERT_TRUE(d.started());
    d.damagedOut();
    d.takeCommands();
    d.restart();
    const auto cmds = d.takeCommands();
    ASSERT_EQ(cmds.size(), 1u);
    EXPECT_EQ(cmds[0].state, MusicState::Start);
    EXPECT_EQ(d.current(), MusicState::Start);
    EXPECT_TRUE(d.started());
}

// Before 1.25 s StartMusic does nothing and UpdateDMusic starts it later.
TEST(GameFlowMusic, RestartBeforeTheStartDelayWaits) {
    MusicDirector d(true);
    run(d, 0.5f);
    d.restart();
    EXPECT_TRUE(d.takeCommands().empty());
    EXPECT_FALSE(d.started());
    run(d, 1.0f);
    EXPECT_TRUE(d.started());
}

// mmPopup::PlayPauseMusic / PlayReturnMusic.
TEST(GameFlowMusic, PauseAndReturn) {
    MusicDirector d(false);
    run(d, 2.0f);
    d.takeCommands();
    d.pause();
    EXPECT_EQ(d.current(), MusicState::Paused);
    d.resume();
    EXPECT_EQ(d.current(), MusicState::Start);
}
