// Parity checks for the hud-views audit (docs/parity/mm2/hud-views.md):
// mmViewMgr::SetViewSetting's coupling of the HUD map's modes with the
// camera, the wide angle and the dashboard, mmHudMap::SetMapMode's 3D view
// placement and mmHUD::Update's message placement.
#include "game/CamPlayer.h"
#include "game/session/Hud.h"

#include <gtest/gtest.h>

using namespace mm2;
using namespace mm2::game;

namespace {

CameraTarget parked() {
    CameraTarget t;
    t.matrix = Mat34::identity();
    return t;
}

} // namespace

TEST(HudViewsParity, MapCycleThroughSplit) {
    // mmViewMgr::SetViewSetting(1): off -> small -> split -> off.
    PlayerCameras cams;
    cams.reset(parked());
    EXPECT_EQ(cams.mapMode(), MapMode::Off);
    cams.cycleMap();
    EXPECT_EQ(cams.mapMode(), MapMode::Small);
    EXPECT_FALSE(cams.wideAngle());
    cams.cycleMap();
    // Split: the wide view (70 degrees) is forced, the player's choice stays off.
    EXPECT_EQ(cams.mapMode(), MapMode::Split);
    EXPECT_TRUE(cams.wideAngle());
    EXPECT_FALSE(cams.wideAngleChoice());
    EXPECT_FLOAT_EQ(cams.viewManager().perspective().fov, 70.0f);
    EXPECT_FALSE(cams.viewSettings().wideAngle); // what the driver keeps
    cams.cycleMap();
    EXPECT_EQ(cams.mapMode(), MapMode::Off);
    EXPECT_FALSE(cams.wideAngle()); // the player's choice back
}

TEST(HudViewsParity, SplitKeepsThePlayersWideAngle) {
    PlayerCameras cams;
    cams.reset(parked());
    cams.toggleWideAngle();
    ASSERT_TRUE(cams.wideAngle());
    cams.cycleMap();
    cams.cycleMap(); // split
    EXPECT_TRUE(cams.wideAngleChoice());
    cams.cycleMap(); // off: still wide
    EXPECT_TRUE(cams.wideAngle());
}

TEST(HudViewsParity, SplitTurnsTheDashboardOffAndBack) {
    PlayerCameras cams;
    cams.reset(parked());
    cams.toggleDashboard();
    ASSERT_TRUE(cams.dashboard());
    ASSERT_EQ(cams.view(), PlayerCameras::View::Dash);
    cams.cycleMap(); // small: the dashboard stays
    EXPECT_TRUE(cams.dashboard());
    cams.cycleMap(); // split: the dash is turned off, its camera stays
    EXPECT_FALSE(cams.dashboard());
    EXPECT_EQ(cams.view(), PlayerCameras::View::Dash);
    EXPECT_EQ(cams.display(), CarDisplay::Hidden);
    // Nor can it be turned on, or the wide angle changed, with the split map.
    cams.toggleDashboard();
    EXPECT_FALSE(cams.dashboard());
    cams.toggleWideAngle();
    EXPECT_TRUE(cams.wideAngle());
    cams.cycleMap(); // off: remembered, and its camera still shows
    EXPECT_TRUE(cams.dashboard());
    EXPECT_FALSE(cams.wideAngle());
    EXPECT_EQ(cams.display(), CarDisplay::Dash);
}

TEST(HudViewsParity, MirrorKeyForgetsTheRememberedDashboard) {
    // SetViewSetting(9) runs SetDash with the current (off) dashboard, which
    // clears the dash view's activated flag: leaving the split map then
    // leaves the dashboard camera without the dash.
    PlayerCameras cams;
    cams.reset(parked());
    cams.toggleDashboard();
    cams.cycleMap();
    cams.cycleMap(); // split
    cams.setViewSetting(ViewSetting::Mirror);
    cams.cycleMap(); // off
    EXPECT_FALSE(cams.dashboard());
    EXPECT_EQ(cams.view(), PlayerCameras::View::Dash);
}

TEST(HudViewsParity, FullScreenMapRemembersTheMode) {
    PlayerCameras cams;
    cams.reset(parked());
    cams.cycleMap(); // small
    cams.toggleFullScreenMap();
    EXPECT_EQ(cams.mapMode(), MapMode::FullScreen);
    // No wide angle change with the full-screen map; the camera still cycles.
    cams.toggleWideAngle();
    EXPECT_FALSE(cams.wideAngle());
    cams.toggleFullScreenMap();
    EXPECT_EQ(cams.mapMode(), MapMode::Small);
    // "Map Toggle" from full screen goes back to the mode it came from too.
    cams.toggleFullScreenMap();
    cams.cycleMap();
    EXPECT_EQ(cams.mapMode(), MapMode::Small);
}

TEST(HudViewsParity, ResetUsesThePlayersWideAngle) {
    // mmPlayer::Reset: SetWideFOV with the view settings' wide byte, so the
    // split map's forced wide view is not kept (mmHudMap::Reset only puts
    // the top-half viewport back).
    PlayerCameras cams;
    cams.reset(parked());
    cams.cycleMap();
    cams.cycleMap(); // split
    ASSERT_TRUE(cams.wideAngle());
    cams.reset(parked());
    EXPECT_FALSE(cams.wideAngle());
    EXPECT_EQ(cams.mapMode(), MapMode::Split);
}

TEST(HudViewsParity, SceneRect) {
    using session::hud::sceneRect;
    const session::HudMapParams map; // Pos (0.78, 0.75), Size (0.21, 0.25)
    // The whole screen, the wide angle's letterbox (0.18 down, 0.66 tall).
    EXPECT_EQ(sceneRect(640, 480, map, MapMode::Off, false), (render::Rect{0, 0, 640, 480}));
    EXPECT_EQ(sceneRect(640, 480, map, MapMode::Small, true), (render::Rect{0, 86, 640, 316}));
    // Split: the top half, whatever the wide angle.
    EXPECT_EQ(sceneRect(640, 480, map, MapMode::Split, true), (render::Rect{0, 0, 640, 240}));
    // Full screen: the small map's place (without its 10-pixel inset).
    EXPECT_EQ(sceneRect(640, 480, map, MapMode::FullScreen, false), (render::Rect{499, 360, 134, 120}));
    EXPECT_EQ(sceneRect(1280, 720, map, MapMode::FullScreen, true), (render::Rect{998, 540, 268, 180}));
}

TEST(HudViewsParity, MessagePlacement) {
    using session::hud::messageTop;
    EXPECT_FLOAT_EQ(messageTop(false, false, true), 0.8f);
    EXPECT_FLOAT_EQ(messageTop(false, true, true), 0.875f);
    EXPECT_FLOAT_EQ(messageTop(true, false, true), 0.2f);
    EXPECT_FLOAT_EQ(messageTop(true, true, true), 0.35f);
    // With the 3D view lower down (letterbox, full-screen map) both modes
    // move to the top.
    EXPECT_FLOAT_EQ(messageTop(false, false, false), 0.05f);
    EXPECT_FLOAT_EQ(messageTop(true, true, false), 0.1f);
}
