// The in-race popup's OPTIONS pages and the PUMenuBase layout, checked
// against MM2's own code (MM2Recomp, midtown2.exe build 3393); see
// docs/parity/mm2/frontend.md.
#include "app/frontend/PopupOptions.h"
#include "app/frontend/Results.h"
#include "app/frontend/Showroom.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

using namespace mm2;
using namespace mm2::app::frontend;
using app::controls::Controller;

// mmPopup's card is x 0.2-0.8, y 0.1-0.9 (mmGame::Init); PUControl and
// PUGraphics ask for 0.9 x 0.8, which PUMenuBase::PUMenuBase centres.
TEST(MM2FrontendParity, PopupCards) {
    const ui::Box c = popup::cardFor(PopupPage::Audio);
    EXPECT_FLOAT_EQ(c.x, 128.0f);
    EXPECT_FLOAT_EQ(c.y, 48.0f);
    EXPECT_FLOAT_EQ(c.w, 384.0f);
    EXPECT_FLOAT_EQ(c.h, 384.0f);
    EXPECT_FLOAT_EQ(popup::cardFor(PopupPage::Options).x, 128.0f);
    const ui::Box w = popup::cardFor(PopupPage::Graphics);
    EXPECT_FLOAT_EQ(w.x, 32.0f);
    EXPECT_FLOAT_EQ(w.w, 576.0f);
    EXPECT_FLOAT_EQ(popup::cardFor(PopupPage::Control).x, 32.0f);
    // Card2D: (16, 31, 93) at alpha 0x80.
    EXPECT_EQ(popup::cardColor(), render::packColor(0x10, 0x1F, 0x5D, 0x80));
}

// UIMenu::ScaleWidget: fractions of the card. PUAudioOptions' sliders start
// at 0.11 (after the title) every 2 x WIDGET_HEIGHT + 0.11; OK sits at
// (0.6, 0.9).
TEST(MM2FrontendParity, PopupWidgetPositions) {
    const ui::Box ok = popup::at(popup::kCard, 0.6f, 0.9f, 0.4f, 0.1f);
    EXPECT_NEAR(ok.x, 358.4f, 1e-3f);
    EXPECT_NEAR(ok.y, 393.6f, 1e-3f);
    EXPECT_NEAR(ok.w, 153.6f, 1e-3f);
    EXPECT_NEAR(ok.h, 38.4f, 1e-3f);
    const float step = 2.0f / 15.0f + 0.11f;
    EXPECT_NEAR(popup::at(popup::kCard, 0.05f, 0.11f + step, 0.6f, 0.0f).y, 183.68f, 1e-2f);
    EXPECT_NEAR(popup::at(popup::kCard, 0.05f, 0.11f + 2.0f * step, 0.6f, 0.0f).y, 277.12f, 1e-2f);
}

// PUGraphics' lighting slider: raised, the whole part of value + 1; lowered,
// its whole part; within 1..3; nothing when unchanged.
TEST(MM2FrontendParity, PopupLightingQualitySnaps) {
    EXPECT_FALSE(popup::lightQualityFromSlider(2.0f, 2).has_value());
    EXPECT_EQ(popup::lightQualityFromSlider(2.105f, 2), 3);
    EXPECT_EQ(popup::lightQualityFromSlider(1.895f, 2), 1);
    EXPECT_EQ(popup::lightQualityFromSlider(3.0f, 3), std::nullopt);
    // From the options page's 0, one step right lands on 2.
    EXPECT_EQ(popup::lightQualityFromSlider(1.105f, 0), 2);
    EXPECT_EQ(popup::lightQualityFromSlider(1.0f, 0), 2);
    EXPECT_EQ(popup::lightQualityFromSlider(0.5f, 1), 1); // kept at 1
}

// PUControl::PreSetup runs SetRWStates and then InitSensitivity;
// ControlSelect runs them the other way round, so a mouse keeps its dead
// zone usable on entry but not after picking it from the list.
TEST(MM2FrontendParity, PopupControlReadWriteStates) {
    popup::ControlStates s;
    popup::setRWStates(s, Controller::Mouse, false);
    popup::initSensitivity(s, Controller::Mouse);
    EXPECT_TRUE(s.sensitivity);
    EXPECT_TRUE(s.deadZone);
    EXPECT_FALSE(s.collision);
    popup::initSensitivity(s, Controller::Mouse);
    popup::setRWStates(s, Controller::Mouse, false);
    EXPECT_TRUE(s.sensitivity);
    EXPECT_FALSE(s.deadZone);

    for (Controller c : {Controller::Keyboard, Controller::GamePad}) {
        popup::setRWStates(s, c, true);
        popup::initSensitivity(s, c);
        EXPECT_FALSE(s.sensitivity || s.deadZone || s.collision || s.roadForce);
    }
    popup::setRWStates(s, Controller::Wheel, true);
    popup::initSensitivity(s, Controller::Wheel);
    EXPECT_TRUE(s.sensitivity && s.deadZone && s.collision && s.roadForce);
    popup::setRWStates(s, Controller::Joystick, false);
    EXPECT_TRUE(s.sensitivity && s.deadZone);
    EXPECT_FALSE(s.collision || s.roadForce);
}

TEST(MM2FrontendParity, PopupScriptCommands) {
    PopupScript script("wait:2;open:graphics;nav:down;bogus;nav:accept");
    EXPECT_TRUE(script.step().open.empty()); // wait:2 starts
    EXPECT_TRUE(script.step().open.empty());
    EXPECT_TRUE(script.step().open.empty());
    EXPECT_EQ(script.step().open, "graphics");
    EXPECT_EQ(script.step().key, platform::Key::Down);
    EXPECT_EQ(script.step().key, platform::Key::Return); // the unknown command is skipped
    EXPECT_FALSE(script.active());
}

// PUKey lists the slots mmInput::Init leaves on, walking 33 slots (34 when
// fewer than 32 are on): the keyboard skips Steering and Camera Pan and
// stops before Enter Chat Msg; the other devices skip Steer Left / Right and
// Camera Pan and list Enter Chat Msg.
TEST(MM2FrontendParity, PopupKeyMapSlots) {
    const auto kb = popup::keyMapSlots(Controller::Keyboard);
    EXPECT_EQ(kb.size(), 31u);
    EXPECT_EQ(std::count(kb.begin(), kb.end(), 5), 0);
    EXPECT_EQ(std::count(kb.begin(), kb.end(), 31), 0);
    EXPECT_EQ(kb.back(), 32);
    const auto pad = popup::keyMapSlots(Controller::GamePad);
    EXPECT_EQ(pad.size(), 31u);
    EXPECT_EQ(pad[5], 5);
    EXPECT_EQ(std::count(pad.begin(), pad.end(), 6), 0);
    EXPECT_EQ(pad.back(), 33);
    EXPECT_FLOAT_EQ(popup::cardFor(PopupPage::KeyMap).y, 24.0f);
}

// The garage's camera (MenuManager::Init, VehicleSelectBase::Update):
// viewport 0.05, 0.115, 0.95 x 0.4 of 640x480 cut to whole pixels; the
// polar view at incline 0.18 rad over the offset (0, 0.86, 0); the distance
// easing at 21 units a second.
TEST(MM2FrontendParity, ShowroomCamera) {
    const render::Rect vp = Showroom::viewport640();
    EXPECT_EQ(vp.x, 32);
    EXPECT_EQ(vp.y, 55);
    EXPECT_EQ(vp.width, 608u);
    EXPECT_EQ(vp.height, 192u);
    const Mat34 m = Showroom::cameraMatrix(6.0f);
    EXPECT_NEAR(m.m3.x, 0.0f, 1e-5f);
    EXPECT_NEAR(m.m3.y, 0.86f + 6.0f * std::sin(0.18f), 1e-4f);
    EXPECT_NEAR(m.m3.z, 6.0f * std::cos(0.18f), 1e-4f);
    // Looking down the -Z row at the offset point.
    EXPECT_NEAR(m.m2.y, std::sin(0.18f), 1e-5f);
    EXPECT_NEAR(m.m2.z, std::cos(0.18f), 1e-5f);
    EXPECT_FLOAT_EQ(Showroom::easeDistance(10.0f, 6.0f, 0.1f), 7.9f);
    EXPECT_FLOAT_EQ(Showroom::easeDistance(7.0f, 6.0f, 0.1f), 6.0f);
    EXPECT_FLOAT_EQ(Showroom::easeDistance(5.0f, 6.0f, 0.02f), 5.42f);
}

// mmMultiCR::FillResults: the winning team first (team 0 on a tie), then
// the players by points from row 3, the local player ahead of those it ties
// with, the others in their order on a tie.
TEST(MM2FrontendParity, CopsAndRobbersResultRows) {
    auto string = [](std::uint32_t id, const char*) { return std::to_string(id); };
    const std::vector<CrResultPlayer> players = {
        {"A", 2, false}, {"Me", 3, true}, {"B", 3, false}, {"C", 5, false}, {"D", 2, false}};
    const auto rows = crResultRows(game::CopsAndRobbersMode::CopsVsRobbers, 4, 9, players, string);
    ASSERT_EQ(rows.size(), 7u);
    EXPECT_EQ(rows[0].name, "126"); // ROBBERS, the winners
    EXPECT_EQ(rows[0].points, 9);
    EXPECT_EQ(rows[1].name, "127"); // COPS
    EXPECT_EQ(rows[1].points, 4);
    const char* order[] = {"C", "Me", "B", "A", "D"};
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(rows[2 + i].name, order[i]);
        EXPECT_EQ(rows[2 + i].place, 3 + i);
    }
    // A tie keeps team 0 (cops or blue) first; Free-For-All starts at row 1.
    const auto tie = crResultRows(game::CopsAndRobbersMode::RobberTeams, 5, 5, {}, string);
    ASSERT_EQ(tie.size(), 2u);
    EXPECT_EQ(tie[0].name, "124"); // BLUE
    EXPECT_EQ(tie[1].name, "125"); // RED
    const auto ffa = crResultRows(game::CopsAndRobbersMode::FreeForAll, 0, 0, {{"Me", 1, true}, {"X", 1, false}}, string);
    ASSERT_EQ(ffa.size(), 2u);
    EXPECT_EQ(ffa[0].name, "Me");
    EXPECT_EQ(ffa[0].place, 1);
    EXPECT_EQ(ffa[1].place, 2);
}
