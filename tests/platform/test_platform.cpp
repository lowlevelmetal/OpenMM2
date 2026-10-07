#include "platform/Clock.h"
#include "platform/Input.h"
#include "platform/Platform.h"
#include "platform/Window.h"

#include <gtest/gtest.h>

using namespace mm2::platform;

TEST(Window, ParsesModes) {
    WindowMode m{};
    EXPECT_TRUE(parseWindowMode("Fullscreen", m));
    EXPECT_EQ(m, WindowMode::Fullscreen);
    EXPECT_TRUE(parseWindowMode("borderless", m));
    EXPECT_EQ(m, WindowMode::Borderless);
    EXPECT_FALSE(parseWindowMode("maximised", m));
    for (WindowMode w : {WindowMode::Windowed, WindowMode::Borderless, WindowMode::Fullscreen}) {
        WindowMode back{};
        EXPECT_TRUE(parseWindowMode(windowModeName(w), back));
        EXPECT_EQ(back, w);
    }
}

TEST(Input, KeyNamesRoundTrip) {
    for (Key k : {Key::A, Key::Num0, Key::Space, Key::F12, Key::Left, Key::KpEnter, Key::LShift, Key::RCtrl}) {
        const std::string name = keyName(k);
        ASSERT_FALSE(name.empty()) << static_cast<int>(k);
        EXPECT_EQ(keyFromName(name), k) << name;
    }
}

TEST(Input, StartsReleased) {
    Input in;
    EXPECT_FALSE(in.keyDown(Key::Up));
    EXPECT_FALSE(in.mouseDown(MouseButton::Left));
    EXPECT_TRUE(in.gamepads().empty());
}

TEST(Clock, MonotonicAndLimiterHoldsRate) {
    ASSERT_TRUE(init({.video = false, .gamepad = false}));
    const auto a = nowNs();
    sleepPrecise(0.002);
    const auto b = nowNs();
    EXPECT_GE(b - a, 1'500'000u);

    FrameLimiter limiter;
    limiter.setTargetFps(200.0); // 5 ms
    const double start = nowSeconds();
    for (int i = 0; i < 11; ++i)
        limiter.wait();
    const double elapsed = nowSeconds() - start;
    EXPECT_GT(elapsed, 0.045);
    EXPECT_LT(elapsed, 0.150);
    shutdown();
}
