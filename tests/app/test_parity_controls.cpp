// Parity checks of the in-race controls against MM2's mmInput (MM2Recomp,
// build 3393). See docs/parity/session.md.

#include "app/Controls.h"
#include "core/Ini.h"
#include "game/session/Hud.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace mm2;
using namespace mm2::app::controls;
using platform::Key;

TEST(ParityControls, KeyboardDefaultsFollowSetDefaultConfig) {
    // mmInput::SetDefaultConfig, keyboard: slot order = event numbers.
    EXPECT_EQ(actions().size(), 34u);
    EXPECT_EQ(info(Action::MapToggle).key, Key::Tab);
    EXPECT_EQ(info(Action::HudToggle).stringId, 297u);
    EXPECT_EQ(info(Action::Throttle).key, Key::Up);
    EXPECT_EQ(info(Action::Horn).key, Key::Return);
    EXPECT_EQ(info(Action::Dashboard).key, Key::D);
    EXPECT_EQ(info(Action::Reverse).key, Key::R);
    EXPECT_EQ(info(Action::NextCheckpoint).key, Key::S);
    EXPECT_EQ(info(Action::OpponentPosition).key, Key::I);
    EXPECT_FALSE(info(Action::Steering).keyboard);
}

TEST(ParityControls, BindingsComeFromTheControlsSection) {
    IniFile ini;
    ini.set("Controls", bindKey(280), platform::keyName(Key::W)); // Throttle
    ini.set("Controls", bindKey(279), kUnbound);                  // Horn
    Bindings b;
    b.load(ini);
    EXPECT_EQ(b.key(Action::Throttle), Key::W);
    EXPECT_EQ(b.key(Action::Horn), Key::Unknown);
    EXPECT_EQ(b.key(Action::Brakes), Key::Down);
}

TEST(ParityControls, OptionsDefaults) {
    const Options o = Options::load(IniFile{});
    EXPECT_TRUE(o.autoReverse);
    EXPECT_FLOAT_EQ(o.deadZone, 0.1f); // mmInput +0x1B4
    EXPECT_FLOAT_EQ(o.sensitivity, 1.0f);
}

TEST(ParityControls, DeadZoneRescalesTheRest) {
    EXPECT_FLOAT_EQ(applyDeadZone(0.05f, 0.1f), 0.0f);
    EXPECT_FLOAT_EQ(applyDeadZone(0.55f, 0.1f), 0.5f);
    EXPECT_FLOAT_EQ(applyDeadZone(-1.0f, 0.1f), -1.0f);
    EXPECT_FLOAT_EQ(applyDeadZone(0.3f, 0.0f), 0.3f);
}

TEST(ParityControls, MapToggleCyclesLikeGetNextMapMode) {
    using game::session::MapMode;
    using game::session::hud::nextMapMode;
    EXPECT_EQ(nextMapMode(MapMode::Off, MapMode::Off), MapMode::Small);
    EXPECT_EQ(nextMapMode(MapMode::Small, MapMode::Off), MapMode::Split);
    EXPECT_EQ(nextMapMode(MapMode::Split, MapMode::Off), MapMode::Off);
    EXPECT_EQ(nextMapMode(MapMode::FullScreen, MapMode::Split), MapMode::Split);
}

// mmPlayer::FilterSteering with mmPlayer's constructor values, blended by
// speed in mmPlayer::Update (f = clamp(speed, 5, 100) / 95).
TEST(ParityControls, AnalogSteeringFollowsFilterSteering) {
    AnalogSteering s;
    s.setSpeed(0.0f, 1.0f); // f = 5 / 95
    const float f = 5.0f / 95.0f;
    // The mouse: sign x |x|^(1.5 + 2.5 f).
    const float mouseExp = (4.0f - 1.5f) * f + 1.5f;
    EXPECT_NEAR(s.filter(Controller::Mouse, -0.5f, 0.016f), -std::pow(0.5f, mouseExp), 1e-5f);
    // The joystick: v = x / sens (sens = 0.5 + 0.6 f, below 1), then
    // pow(|v| sens, 1 + 2 f) / sens.
    const float sens = (1.1f - 0.5f) * f + 0.5f;
    const float exponent = (3.0f - 1.0f) * f + 1.0f;
    const float v = 0.25f / sens;
    EXPECT_NEAR(s.filter(Controller::Joystick, 0.25f, 0.016f), std::pow(v * sens, exponent) / sens, 1e-5f);
    // Full lock: v is clamped to 1 first.
    EXPECT_NEAR(s.filter(Controller::Wheel, 1.0f, 0.016f), std::pow(sens, exponent) / sens, 1e-5f);
    // The keyboard and the gamepad pass through (mmInput filters them).
    EXPECT_FLOAT_EQ(s.filter(Controller::Keyboard, 0.3f, 0.016f), 0.3f);
    // mmInput::PollContinuous: the cursor across the window over the mouse
    // sensitivity.
    const float mouseSens = (1.8f - 0.6f) * f + 0.6f;
    EXPECT_NEAR(s.mouseAxis(960.0f, 1280.0f), 0.5f / mouseSens, 1e-5f);
}

TEST(ParityControls, ControllerOptionIsRead) {
    IniFile ini;
    EXPECT_EQ(Options::load(ini).controller, Controller::Keyboard);
    ini.setInt("Controls", "Controller", 4);
    EXPECT_EQ(Options::load(ini).controller, Controller::Wheel);
}
