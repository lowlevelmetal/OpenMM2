#pragma once

// A player's set of car cameras and the view switching between them, as
// Midtown Madness 2's mmPlayer and mmViewMgr handle them (MM2Recomp, build
// 3393):
//
//   near   camTrackCS  tune/camera/<car>_near.camtrackcs
//   far    camTrackCS  tune/camera/<car>_far.camtrackcs
//   ind    camTrackCS  tune/camera/<car>_ind.camtrackcs (big vehicles under cover)
//   pov    camPovCS    tune/camera/<car>.campovcs (hood view)
//   dash   camPovCS    tune/camera/<car>_dash.campovcs (dashboard)
//   pre    camPreCS    before the race (constructor defaults)
//   point  camPointCS  after the race, and when the car is in the water
//   polar  camPolarCS  multiplayer finish line (constructor defaults)
//   xcam   camPolarCS  the "XCam" orbit cameras (SetViewSetting(2))
//
// "Change Camera" cycles near -> pov -> far with a 0.8 s ease-in-out blend
// (mmViewMgr::SetViewSetting, mmPlayer::SetCamera). See docs/camera.md.
//
// Usage per car:
//   PlayerCameras cams;
//   cams.load(vfs, "vpbug");
//   cams.reset(target);                      // after placing the car
//   cams.startPreRace();                     // race modes, before the first update
//   each update:
//     cams.update(dt, target, probe, input);  // input.camPan from cameraPanFor()
//     cams.apply(camera);                     // game::Camera for rendering
//   cams.toggleCamera(); cams.toggleDashboard(); cams.startPostRace(); ...

#include "game/CamPov.h"
#include "game/CamRace.h"
#include "game/CamTrack.h"
#include "game/CamView.h"

#include <string>
#include <string_view>
#include <vector>

namespace mm2::vfs {
class Vfs;
}

namespace mm2::game {

// mmViewMgr::SetViewSetting's settings: what mmGame::UpdateGameInput passes
// for each view key (input events in brackets).
enum class ViewSetting : int {
    ChangeCamera = 0,   // "Change Camera" (0x0B)
    MapCycle = 1,       // "Map Toggle" (0): off -> small -> split -> off
    XCam = 2,           // (0x0C, 0x2F)
    Hud = 3,            // mmHUD::Toggle; no input event passes it
    Cluster = 4,        // "HUD Toggle" (4): mmHUD::ToggleExternalView
    WideAngle = 5,      // (0x12)
    Dashboard = 6,      // (0x13)
    MapZoom = 7,        // (2): mmHudMap::ToggleMapRes
    MapOrient = 8,      // (3): mmHudMap::ToggleMapOrient
    Mirror = 9,         // (0x1E)
    FullScreenMap = 10, // "Full Screen Map" (1)
};

// mmHudMap's map modes (mmHudMap +0x44, the view settings' map byte). They
// move the 3D view too (mmHudMap::SetMapMode; session::Hud::sceneRect).
enum class MapMode : std::uint8_t {
    Off = 0,
    Small = 1,      // tune/<city>.mmhudmap Pos/Size, minus 10 pixels
    Split = 2,      // the bottom half of the screen; the 3D view takes the top half
    FullScreen = 3, // the whole screen; the 3D view moves into the small map's place
};

// mmHudMap::GetNextMapMode ("Map Toggle"): Off -> Small -> Split -> Off;
// from full screen, the mode it was opened from.
MapMode nextMapMode(MapMode mode, MapMode beforeFullScreen);

class PlayerCameras {
public:
    enum class View : std::uint8_t { Near, Far, Ind, Pov, Dash, Pre, Point, Polar, XCam };

    PlayerCameras();

    // mmPlayer::Init: loads the car's camera files. Missing files leave that
    // camera at the constructor defaults, as the original did; their names
    // are returned in `missing` (vpvwcup ships its far camera as
    // "vpvwcup__far", and only vpbug and vpbus have an _ind camera). On a
    // screen narrower than 1.3:1 the dashboard eye moves forward (its
    // Offset.z is scaled by 0.7352941).
    void load(const vfs::Vfs& vfs, std::string_view car, std::vector<std::string>* missing = nullptr,
              float screenAspect = 4.0f / 3.0f);
    // The car's tune/<car>.info Flags. Vehicles with 0x13 (bus, double-
    // decker, limousine, semi) switch to the _ind camera inside rooms with
    // flag 0x02 or 0x08 (mmPlayer::Update).
    void setVehicleFlags(int flags) { m_vehicleFlags = flags; }

    // The view settings MM2 keeps per driver: the selected camera, wide
    // angle and dashboard (globals that mmPlayer::Init and Reset read;
    // mmPlayerConfig::SetViewSettings restores them from the driver's
    // config when a race is set up, GetViewSettings saves them when the
    // game ends). Set before reset(); read after the race.
    struct ViewSettings {
        int camera = 0; // index into near, pov (hood), far
        bool wideAngle = false;
        bool dashboard = false;
    };
    // The wide angle saved is the player's choice (the view settings' wide
    // byte), not the wide view the split map forces.
    ViewSettings viewSettings() const { return {m_savedIndex, m_wideChoice, m_dashActive}; }
    void setViewSettings(const ViewSettings& settings);

    // (Not named near()/far(): those are macros in <windows.h>.)
    TrackCamera& nearCam() { return m_near; }
    TrackCamera& farCam() { return m_far; }
    TrackCamera& indCam() { return m_ind; }
    PovCamera& povCam() { return m_pov; }
    PovCamera& dashCam() { return m_dash; }
    PreCamera& preCam() { return m_pre; }
    PointCamera& pointCam() { return m_point; }
    PolarCamera& polarCam() { return m_polar; }
    PolarCamera& xCam(int index) { return m_xcam[index == 1 ? 1 : 0]; }
    CarCamera& camera(View view);

    // mmPlayer::Reset (camera part): back to the selected camera, or the
    // dashboard when it is on; ends the pre/post-race views.
    void reset(const CameraTarget& target);
    // mmGame::UpdateGameInput (look around), mmPlayer::Update (camera part)
    // and camViewCS::Update.
    void update(float dt, const CameraTarget& target, const CameraProbe& probe, const CameraInput& input);
    void apply(Camera& out) const { m_view.apply(out); }

    // mmViewMgr::SetViewSetting for the settings that change the camera,
    // the wide angle, the dashboard and the HUD map's mode (the HUD-only
    // ones, Hud, Cluster, MapZoom, MapOrient and Mirror, do nothing here).
    // Each ends as the original does: mmPlayer::SetCamera, SetWideFOV,
    // mmHUD::SetDash and mmHudMap::SetMapMode with the new values.
    void setViewSetting(ViewSetting setting);

    // mmViewMgr::SetViewSetting(0), "Change Camera": next camera in the
    // cycle near -> pov -> far; from the dashboard, back to the cycled camera.
    void toggleCamera() { setViewSetting(ViewSetting::ChangeCamera); }
    // mmViewMgr::SetViewSetting(6), "Dashboard": the dashboard cuts in at
    // once; switching it off blends back to the cycled camera. Not with the
    // split map, from an XCam, before or after the race.
    void toggleDashboard() { setViewSetting(ViewSetting::Dashboard); }
    void setDashboard(bool on);
    // The HUD's dashboard flag (mmHUD::ActivateDash / DeactivateDash): the
    // dash model is drawn while the dashboard camera shows.
    bool dashboard() const { return m_dashActive; }
    // mmViewMgr::SetViewSetting(5), wide angle (not with the dashboard, nor
    // with the split or full-screen map).
    void toggleWideAngle() { setViewSetting(ViewSetting::WideAngle); }
    // mmViewMgr::SetViewSetting(1), "Map Toggle": off -> small -> split ->
    // off, or from full screen back to the mode it was opened from
    // (mmHudMap::GetNextMapMode). The split map forces the wide angle (the
    // 3D view in the top half at 70 degrees) and turns the dashboard off,
    // remembering it; leaving it restores the player's wide angle and the
    // dashboard.
    void cycleMap() { setViewSetting(ViewSetting::MapCycle); }
    // mmViewMgr::SetViewSetting(10), "Full Screen Map": to full screen,
    // remembering the mode (mmHudMap +0x40), and back to it.
    void toggleFullScreenMap() { setViewSetting(ViewSetting::FullScreenMap); }
    MapMode mapMode() const { return m_mapMode; }
    // mmHudMap::Reset applies the view settings' map mode at the start
    // (mmHudMap::SetMapMode, without the camera side of SetViewSetting).
    void setMapMode(MapMode mode) { m_mapMode = mode; }
    // mmViewMgr::SetViewSetting(2) (input events 0x0C and 0x2F): the first
    // press blends (mode 3, 0.8 s) to an "XCam", a camPolarCS orbiting the
    // car that the keyboard steers (CameraInput::orbit) and that remembers
    // whether the dashboard was on; the next press goes back to the cycled
    // camera, or to the dashboard. With the XCam cheat the presses cycle
    // between the two XCams instead (mmPlayer::GetNextCycleXCamIndex); the
    // cheat's flag is never set in midtown2.exe, so only the first is
    // reached.
    void toggleXCam() { setViewSetting(ViewSetting::XCam); }
    void setXCamCheat(bool on) { m_xcamCheat = on; }
    // The wide view (camViewCS +0x18): letterboxed at 70 degrees, or the
    // split map's top half.
    bool wideAngle() const { return m_wide; }
    // The player's wide-angle choice (the view settings' wide byte, which
    // SetWideFOV sets and entering the split map clears).
    bool wideAngleChoice() const { return m_wideChoice; }
    // Selects a camera of the cycle (Near, Pov or Far) as "Change Camera"
    // would (mmPlayer::SetCamera(0, index)), or the dashboard.
    void select(View view);

    // mmPlayer::SetPreRaceCam: start on the pre-race view and blend to the
    // selected camera over 3.5 s. Only right after reset(), as the original.
    void startPreRace();
    // mmPlayer::SetPostRaceCam: watch the car from above where the far
    // camera is. Applied at the next update.
    void startPostRace();
    // mmPlayer::SetMPPostCam, from mmGameMulti::SetFinishCam at the end of
    // a multiplayer checkpoint race or circuit (multiplayer blitz uses
    // startPostRace): orbit `finish` (the last waypoint, or the first in a
    // circuit) 2.5 m up at `azimuth`, 21.5 m away and 0.34 rad above the
    // horizon, or 15.5 m away level with it while the car is in a room with
    // flag 0x02 or 0x08. mmGameMulti passes azimuth = (heading + 180) x
    // -pi / 180 with the waypoint's heading in degrees
    // (mmWaypoints::GetHeading). The keyboard then orbits it
    // (CameraInput::orbit). Applied at the next update.
    void startMultiplayerPostRace(const Vec3& finish, float azimuth);
    // mmPlayer::Update when the car has gone into the water: watch it from
    // 9 m above the view. Applied at the next update; once per reset.
    void startWaterCam();
    bool preRace() const { return m_preRace; }
    bool postRace() const { return m_postRace; }

    // The camera selected with toggleCamera / toggleDashboard.
    View view() const;
    // How to draw the car: hidden for the point-of-view cameras
    // (mmPlayer::IsPOV), the dash model with the dashboard.
    CarDisplay display() const;
    CameraView& viewManager() { return m_view; }

private:
    CarCamera* carCam(int index);
    CarCamera* currentCameraPtr();
    bool isPov() const;
    void setCamera(int group, int index);
    void setWideFov(bool wide);
    // mmHUD::SetDash: ActivateDash / DeactivateDash (the dash view's
    // Activate / Deactivate set and clear its activated flag too).
    void setDash(bool on);

    TrackCamera m_near, m_far, m_ind;
    PovCamera m_pov, m_dash;
    PreCamera m_pre;
    PointCamera m_point;
    PolarCamera m_polar; // mmPlayer +0x1FBC
    PolarCamera m_xcam[2]; // mmPlayer +0x18B8, +0x19E0 (XCams)
    CameraView m_view;

    int m_camIndex = 0;   // mmPlayer+0xE48 into near, pov, far
    int m_savedIndex = 0; // kept across resets (the original's global)
    int m_group = 0;      // mmPlayer+0x28: 0 cycled cameras, 1 XCams, 2 dashboard
    int m_xcamIndex = 0;  // mmPlayer+0xE50
    // The dash view's "activated" flag (mmDashView +0x5DE, mmHUD +0x5FA):
    // set and cleared with the dashboard, and set again to remember it while
    // an XCam or the split map has turned it off.
    bool m_xcamDash = false;
    bool m_xcamCheat = false; // XcamCheat
    bool m_dashActive = false; // HUD dashboard on (the view settings' dash byte)
    bool m_wide = false;       // camViewCS +0x18
    bool m_wideChoice = false; // the view settings' wide byte
    MapMode m_mapMode = MapMode::Off;         // mmHudMap +0x44 (the view settings' map byte)
    MapMode m_mapBeforeFull = MapMode::Off;   // mmHudMap +0x40
    bool m_firstUpdate = true; // +0xE58
    bool m_preRace = false;    // +0xE5A
    bool m_postRace = false;   // +0xE59
    bool m_postPending = false;
    bool m_mpPostPending = false;
    Vec3 m_mpPostFinish;
    float m_mpPostAzimuth = 0.0f;
    bool m_waterPending = false;
    bool m_waterDone = false;  // +0x2344
    bool m_restoreCityCam = false;
    int m_vehicleFlags = 0;
};

const char* viewName(PlayerCameras::View view);

} // namespace mm2::game
