// Parity checks of the in-race controls against MM2's mmInput (MM2Recomp,
// build 3393). See docs/parity/session.md.

#include "app/Controls.h"
#include "core/Ini.h"
#include "game/session/Hud.h"

#include <gtest/gtest.h>

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

TEST(ParityControls, KeyboardSteeringIsFilteredAndSquared) {
    // mmInput::FilterDiscreteSteering: 1 per second toward the key, squared.
    DiscreteSteering s;
    EXPECT_FLOAT_EQ(s.update(1.0f, 0.5f), 0.25f);
    EXPECT_FLOAT_EQ(s.value, 0.5f);
    EXPECT_FLOAT_EQ(s.update(1.0f, 1.0f), 1.0f); // clamped at the target
    EXPECT_FLOAT_EQ(s.update(0.0f, 0.25f), 0.5625f);
    EXPECT_FLOAT_EQ(s.update(-1.0f, 1.0f), -0.0625f); // through the centre to -0.25
    EXPECT_FLOAT_EQ(s.value, -0.25f);
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
