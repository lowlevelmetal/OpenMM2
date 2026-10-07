#pragma once

// A player's set of car cameras and the view switching between them, as
// mmPlayer sets them up in Midtown Madness 1 (Open1560, GPL-3.0,
// Copyright (C) Brick):
//
//   near  TrackCamCS  tune/camera/<car>_near.camtrackcs
//   far   TrackCamCS  tune/camera/<car>_far.camtrackcs
//   ind   TrackCamCS  tune/camera/<car>_ind.camtrackcs (loaded, not in the cycle)
//   pov   PovCamCS    tune/camera/<car>.campovcs (hood view)
//   dash  PovCamCS    tune/camera/<car>_dash.campovcs (dashboard)
//
// "Change Camera" cycles near -> pov -> far (mmPlayer::ToggleCam,
// CarCams[0..XCamStart-1]) with a 0.8 s ease-in-out blend.
//
// Usage per car:
//   PlayerCameras cams;
//   cams.load(vfs, "vpbug");
//   cams.reset(target);                      // after placing the car
//   each simulation update:
//     cams.update(dt, target, probe, input);  // input.camPan from cameraPanFor()
//     cams.apply(camera);                     // game::Camera for rendering
//   cams.toggleCamera(); cams.setDashboard(on); cams.select(...)

#include "game/CamPov.h"
#include "game/CamTrack.h"
#include "game/CamView.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace mm2::vfs {
class Vfs;
}

namespace mm2::game {

class PlayerCameras {
public:
    enum class View : std::uint8_t { Near, Far, Ind, Pov, Dash };

    PlayerCameras();

    // Loads the car's camera files. Missing files leave that camera at the
    // engine defaults, as the original did; their names are returned in
    // `missing` (vpvwcup ships its far camera as "vpvwcup__far").
    void load(const vfs::Vfs& vfs, std::string_view car, std::vector<std::string>* missing = nullptr);

    // (Not named near()/far(): those are macros in <windows.h>.)
    TrackCamera& nearCam() { return m_near; }
    TrackCamera& farCam() { return m_far; }
    TrackCamera& indCam() { return m_ind; }
    PovCamera& povCam() { return m_pov; }
    PovCamera& dashCam() { return m_dash; }
    CarCamera& camera(View view);

    // Snap the active camera to the car (race start, reset).
    void reset(const CameraTarget& target);
    void update(float dt, const CameraTarget& target, const CameraProbe& probe, const CameraInput& input);
    void apply(Camera& out) const { m_view.apply(out); }

    // mmPlayer::ToggleCam: next camera in the cycle near -> pov -> far.
    void toggleCamera();
    // Switch to a view. Blend::EaseInOut over 0.8 s matches ToggleCam.
    void select(View view, CameraView::Blend blend = CameraView::Blend::EaseInOut, float seconds = 0.8f);
    // Dashboard On/Off: blend between the cycled camera and the dashboard.
    // (MM1 blended near/pov with modes 1/2 over 0.3 s; the exact MM2 rules
    // are inferred, see docs/camera.md.)
    void setDashboard(bool on);
    bool dashboard() const { return m_dashOn; }

    View view() const { return m_viewId; }
    CarDisplay display() const;
    CameraView& viewManager() { return m_view; }

private:
    TrackCamera m_near, m_far, m_ind;
    PovCamera m_pov, m_dash;
    CameraView m_view;
    std::array<View, 3> m_cycle{View::Near, View::Pov, View::Far};
    int m_cycleIndex = 0;
    bool m_dashOn = false;
    View m_viewId = View::Near;
};

const char* viewName(PlayerCameras::View view);

} // namespace mm2::game
