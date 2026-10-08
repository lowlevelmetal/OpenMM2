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

class PlayerCameras {
public:
    enum class View : std::uint8_t { Near, Far, Ind, Pov, Dash, Pre, Point, Polar };

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

    // (Not named near()/far(): those are macros in <windows.h>.)
    TrackCamera& nearCam() { return m_near; }
    TrackCamera& farCam() { return m_far; }
    TrackCamera& indCam() { return m_ind; }
    PovCamera& povCam() { return m_pov; }
    PovCamera& dashCam() { return m_dash; }
    PreCamera& preCam() { return m_pre; }
    PointCamera& pointCam() { return m_point; }
    PolarCamera& polarCam() { return m_polar; }
    CarCamera& camera(View view);

    // mmPlayer::Reset (camera part): back to the selected camera, or the
    // dashboard when it is on; ends the pre/post-race views.
    void reset(const CameraTarget& target);
    // mmGame::UpdateGameInput (look around), mmPlayer::Update (camera part)
    // and camViewCS::Update.
    void update(float dt, const CameraTarget& target, const CameraProbe& probe, const CameraInput& input);
    void apply(Camera& out) const { m_view.apply(out); }

    // mmViewMgr::SetViewSetting(0), "Change Camera": next camera in the
    // cycle near -> pov -> far; from the dashboard, back to the cycled camera.
    void toggleCamera();
    // mmViewMgr::SetViewSetting(6), "Dashboard": the dashboard cuts in at
    // once; switching it off blends back to the cycled camera.
    void toggleDashboard();
    void setDashboard(bool on);
    bool dashboard() const { return m_dashActive; }
    // mmViewMgr::SetViewSetting(5), wide angle (not with the dashboard).
    void toggleWideAngle();
    bool wideAngle() const { return m_wide; }
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

    TrackCamera m_near, m_far, m_ind;
    PovCamera m_pov, m_dash;
    PreCamera m_pre;
    PointCamera m_point;
    PolarCamera m_polar; // mmPlayer +0x1FBC
    CameraView m_view;

    int m_camIndex = 0;   // mmPlayer+0xE48 into near, pov, far
    int m_savedIndex = 0; // kept across resets (the original's global)
    int m_group = 0;      // mmPlayer+0x28: 0 cycled cameras, 2 dashboard
    bool m_dashActive = false; // HUD dashboard on
    bool m_wide = false;
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
