// A player's car cameras: the camera handling of Midtown Madness 2's
// mmPlayer, mmViewMgr::SetViewSetting and mmGame::UpdateGameInput
// (MM2Recomp, build 3393).
#include "game/CamPlayer.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "game/CamMath.h"

#include <format>

namespace mm2::game {

const char* viewName(PlayerCameras::View view) {
    switch (view) {
    case PlayerCameras::View::Near: return "near";
    case PlayerCameras::View::Far: return "far";
    case PlayerCameras::View::Ind: return "ind";
    case PlayerCameras::View::Pov: return "pov";
    case PlayerCameras::View::Dash: return "dash";
    case PlayerCameras::View::Pre: return "pre";
    case PlayerCameras::View::Point: return "point";
    case PlayerCameras::View::Polar: return "polar";
    case PlayerCameras::View::XCam: return "xcam";
    }
    return "?";
}

PlayerCameras::PlayerCameras() : m_dash({}, true) {
    m_view.setCurrent(&m_near);
    // mmPlayer::Init: the second XCam follows the car's heading
    // (AzimuthLock), 38.1 m away and 0.321 rad up; the first keeps the
    // camPolarCS defaults.
    PolarCamera::Params& x = m_xcam[1].params();
    x.azimuthLock = 1;
    x.polarDistance = 38.131401f;
    x.polarIncline = 0.321209013f;
}

void PlayerCameras::load(const vfs::Vfs& vfs, std::string_view carIn, std::vector<std::string>* missing,
                         float screenAspect) {
    const std::string car = str::lower(carIn);
    auto track = [&](TrackCamera& cam, const std::string& name) {
        std::string error;
        if (auto p = loadTrackCamParams(vfs, name, &error)) {
            cam.setParams(*p);
        } else {
            cam.setParams({});
            log::debug("camera: {} (using defaults)", error);
            if (missing)
                missing->push_back(name);
        }
    };
    auto pov = [&](PovCamera& cam, const std::string& name) {
        std::string error;
        if (auto p = loadPovCamParams(vfs, name, &error)) {
            cam.setParams(*p);
        } else {
            cam.setParams({});
            log::debug("camera: {} (using defaults)", error);
            if (missing)
                missing->push_back(name);
        }
    };
    track(m_near, car + "_near");
    track(m_far, car + "_far");
    track(m_ind, car + "_ind");
    pov(m_pov, car);
    pov(m_dash, car + "_dash");
    if (screenAspect < 1.3f) {
        PovCamParams p = m_dash.params();
        p.offset.z = p.offset.z * 0.7352941f;
        m_dash.setParams(p);
    }
}

CarCamera& PlayerCameras::camera(View view) {
    switch (view) {
    case View::Near: return m_near;
    case View::Far: return m_far;
    case View::Ind: return m_ind;
    case View::Pov: return m_pov;
    case View::Dash: return m_dash;
    case View::Pre: return m_pre;
    case View::Point: return m_point;
    case View::Polar: return m_polar;
    case View::XCam: return m_xcam[m_xcamIndex];
    }
    return m_near;
}

CarCamera* PlayerCameras::carCam(int index) {
    // mmPlayer::CarCams: near, pov, far ("XCamStart" = 3).
    switch (index) {
    case 0: return &m_near;
    case 1: return &m_pov;
    default: return &m_far;
    }
}

CarCamera* PlayerCameras::currentCameraPtr() {
    // mmPlayer::GetCurrentCameraPtr
    if (m_group == 1)
        return &m_xcam[m_xcamIndex];
    return m_group == 2 ? static_cast<CarCamera*>(&m_dash) : carCam(m_camIndex);
}

bool PlayerCameras::isPov() const {
    // mmPlayer::IsPOV: false while a blend runs (the view's camera is then
    // the transition).
    const CarCamera* cur = m_view.current();
    return cur == &m_pov || cur == &m_dash;
}

void PlayerCameras::setWideFov(bool wide) {
    // mmPlayer::SetWideFOV: the perspective of the selected camera, or
    // 70 degrees on the letterboxed wide view.
    CarCamera* cam = m_dashActive ? static_cast<CarCamera*>(&m_dash) : currentCameraPtr();
    m_view.setPerspective(wide ? CameraPerspective{70.0f, cam->base().cameraNear} : cam->perspective());
    m_view.setWideAngle(wide);
    m_wide = wide;
}

void PlayerCameras::setCamera(int group, int index) {
    // mmPlayer::SetCamera: ignored before and after the race.
    if (m_preRace || m_postRace)
        return;
    // mmPlayer::GetCamera
    const int currentIndex = m_group == 0 ? m_camIndex : (m_group == 1 ? m_xcamIndex : 0);
    if (group == m_group && index == currentIndex)
        return;
    if (group == 0) {
        if (index >= 0 && index < 3) {
            m_camIndex = index;
            m_view.newCam(carCam(index), CameraView::Blend::EaseInOut, 0.8f);
        }
        m_savedIndex = m_camIndex;
        m_group = 0;
    } else if (group == 1) {
        if (index >= 0 && index < 2) {
            m_xcamIndex = index;
            m_view.newCam(&m_xcam[index], CameraView::Blend::EaseInOut, 0.8f);
        }
        m_group = 1;
    } else if (group == 2) {
        m_view.setCurrent(&m_dash);
        m_group = 2;
    }
}

void PlayerCameras::reset(const CameraTarget& target) {
    // mmPlayer::Reset
    m_firstUpdate = true;
    m_preRace = false;
    m_postRace = false;
    m_postPending = false;
    m_mpPostPending = false;
    m_waterPending = false;
    m_waterDone = false;
    m_restoreCityCam = false;
    m_camIndex = m_savedIndex;
    m_view.setCurrent(carCam(m_camIndex));
    m_group = 0;
    if (m_dashActive) {
        // The dashboard comes back on the point-of-view index, but in the
        // cycled-camera group: switching it off then does not move the view
        // until the camera is changed (as in the original).
        m_savedIndex = 1;
        m_camIndex = 1;
        m_view.setCurrent(&m_dash);
    }
    setWideFov(m_wide);
    m_view.reset(target);
}

void PlayerCameras::update(float dt, const CameraTarget& target, const CameraProbe& probe, const CameraInput& input) {
    // mmGame::UpdateGameInput: only the point-of-view cameras look around.
    if (isPov())
        static_cast<PovCamera*>(m_view.current())->setPan(input.camPan * cam::kTwoPi);

    // mmPlayer::Update
    if (m_firstUpdate) {
        m_view.reset(target);
        m_firstUpdate = false;
    }
    if (m_postPending) {
        // mmPlayer::SetPostRaceCam (from the race's update)
        m_postPending = false;
        m_far.update(dt, target, probe, input, m_view.perspective());
        const Vec3 p = m_far.matrix().m3;
        m_point.setPosition({p.x, p.y + 3.5f, p.z});
        m_point.setVelocity({});
        m_point.setMaxDist(25.0f);
        m_point.setMinDist(5.0f);
        m_point.setAppRate(5.0f);
        m_view.newCam(&m_point, CameraView::Blend::EaseInOut, 0.8f);
        m_postRace = true;
    }
    if (m_mpPostPending) {
        // mmPlayer::SetMPPostCam (from mmGameMulti::SetFinishCam)
        m_mpPostPending = false;
        m_far.update(dt, target, probe, input, m_view.perspective());
        m_polar.setInterest(Mat34::translation(m_mpPostFinish));
        PolarCamera::Params& p = m_polar.params();
        if ((target.roomFlags & 0x0A) == 0) {
            p.polarDistance = 21.5f;
            p.polarIncline = 0.34f;
        } else {
            p.polarDistance = 15.5f;
            p.polarIncline = 0.0f;
        }
        p.polarAzimuth = m_mpPostAzimuth;
        // Set like the post-race point camera's, but camPolarCS never reads them.
        p.app.maxDist = 25.0f;
        p.app.minDist = 5.0f;
        m_view.newCam(&m_polar, CameraView::Blend::EaseInOut, 0.8f);
        m_postRace = true;
    }
    if (m_waterPending && !m_waterDone) {
        m_waterPending = false;
        m_waterDone = true;
        const Vec3 p = m_view.matrix().m3;
        m_point.setPosition({p.x, p.y + 9.0f, p.z});
        m_point.setVelocity({});
        m_view.newCam(&m_point, CameraView::Blend::EaseInOut, 0.8f);
        m_postRace = true;
    }
    if (m_preRace) {
        // From the pre-race view to the selected camera: the point-of-view
        // camera and the dashboard are reached through the near camera.
        CarCamera* want = m_dashActive ? static_cast<CarCamera*>(&m_dash) : carCam(m_savedIndex);
        const CarCamera* cur = m_view.current();
        if (cur == want) {
            m_preRace = false;
        } else if (want == &m_dash) {
            if (cur == &m_near)
                m_view.newCam(&m_pov, CameraView::Blend::EaseIn, 0.3f);
            else if (cur == &m_pov)
                m_view.newCam(&m_dash, CameraView::Blend::EaseOut, 0.3f);
        } else if (want == &m_pov && cur == &m_near) {
            m_view.newCam(&m_pov, CameraView::Blend::EaseInOut, 0.5f);
        }
    }
    if ((m_vehicleFlags & 0x13) != 0) {
        // Big vehicles use the _ind camera under cover: in rooms with flag
        // 0x02 or 0x08, or in rooms with flag 0x20 when there is geometry
        // over the camera (a segment from 100 m above the last rendered
        // camera position down to it, dgPhysManager::Collide).
        bool overhead = false;
        if ((target.roomFlags & 0x20) != 0 && probe) {
            const Vec3 eye = m_view.matrix().m3;
            CameraHit hit;
            overhead = probe({eye.x, eye.y + 100.0f, eye.z}, eye, hit);
        }
        if ((target.roomFlags & 0x0A) == 0 && !overhead) {
            if (m_restoreCityCam) {
                if (!isPov())
                    m_view.newCam(carCam(m_camIndex), CameraView::Blend::EaseInOut, 1.0f);
                m_restoreCityCam = false;
            }
        } else {
            const CarCamera* cur = m_view.current();
            if (cur == &m_far || cur == &m_near) {
                m_view.newCam(&m_ind, CameraView::Blend::EaseInOut, 1.0f);
                m_restoreCityCam = true;
            }
        }
    }
    const float margin = (target.roomFlags & 0x08) == 0 ? 0.33f : 1.11f;
    m_near.setCollideMargin(margin);
    m_far.setCollideMargin(margin);

    m_view.update(dt, target, probe, input);
}

void PlayerCameras::toggleCamera() {
    // mmViewMgr::SetViewSetting(0)
    if (m_group != 0)
        setCamera(0, m_camIndex);
    else
        setCamera(0, (m_camIndex + 1) % 3);
    setWideFov(m_wide);
    m_dashActive = false;
}

void PlayerCameras::toggleDashboard() {
    // mmViewMgr::SetViewSetting(6): not from an XCam.
    if (m_preRace || m_postRace || m_group == 1)
        return;
    bool dash = !m_dashActive;
    if (!dash)
        setCamera(0, m_camIndex);
    else if (!m_wide)
        setCamera(2, 0);
    else
        dash = false;
    setWideFov(m_wide);
    m_dashActive = dash;
}

void PlayerCameras::toggleXCam() {
    // mmViewMgr::SetViewSetting(2)
    const int next = m_xcamCheat ? (m_xcamIndex + 1) % 2 : 0; // GetNextCycleXCamIndex
    bool dash = false;
    bool remember = false;
    int group = 1, index = 0;
    if (m_group != 1) {
        remember = m_dashActive; // mmHUD::IsDashActive
        index = m_xcamIndex;
    } else if (next == m_xcamIndex) {
        if (m_xcamDash) {
            dash = true;
            group = 2;
        } else {
            group = 0;
            index = m_camIndex;
        }
    } else {
        index = next;
    }
    setCamera(group, index);
    setWideFov(m_wide);
    // mmHUD::SetDash: switching the dash view on or off sets or clears its
    // flag; entering an XCam with the dashboard on sets it again after.
    m_dashActive = dash;
    m_xcamDash = dash;
    if (remember)
        m_xcamDash = true;
}

void PlayerCameras::setDashboard(bool on) {
    if (on != m_dashActive)
        toggleDashboard();
}

void PlayerCameras::toggleWideAngle() {
    // mmViewMgr::SetViewSetting(5)
    if (m_group == 2)
        return;
    const bool wide = !m_wide;
    setWideFov(wide);
    if (wide)
        m_dashActive = false;
}

void PlayerCameras::select(View view) {
    switch (view) {
    case View::Near: setCamera(0, 0); break;
    case View::Pov: setCamera(0, 1); break;
    case View::Far: setCamera(0, 2); break;
    case View::Dash:
        if (!m_dashActive)
            toggleDashboard();
        return;
    default: return;
    }
    setWideFov(m_wide);
    m_dashActive = false;
}

void PlayerCameras::startPreRace() {
    // mmPlayer::SetPreRaceCam
    if (!m_firstUpdate)
        return;
    m_view.setCurrent(&m_pre);
    CarCamera* to = carCam(m_camIndex);
    if (to == &m_pov)
        to = &m_near;
    m_view.newCam(to, CameraView::Blend::EaseInOut, 3.5f);
    m_preRace = true;
}

void PlayerCameras::setViewSettings(const ViewSettings& settings) {
    // mmPlayerConfig::SetViewSettings: mmPlayer::Reset starts on this camera
    // (the dashboard on the hood index), Init and Reset apply the wide angle.
    // (An index out of range, which MM2 never stores, starts on the near
    // camera.)
    m_savedIndex = settings.camera >= 0 && settings.camera < 3 ? settings.camera : 0;
    m_camIndex = m_savedIndex;
    m_wide = settings.wideAngle;
    m_dashActive = settings.dashboard;
}

void PlayerCameras::startPostRace() { m_postPending = true; }

void PlayerCameras::startMultiplayerPostRace(const Vec3& finish, float azimuth) {
    m_mpPostPending = true;
    m_mpPostFinish = finish;
    m_mpPostAzimuth = azimuth;
}

void PlayerCameras::startWaterCam() { m_waterPending = true; }

PlayerCameras::View PlayerCameras::view() const {
    if (m_group == 2)
        return View::Dash;
    if (m_group == 1)
        return View::XCam;
    switch (m_camIndex) {
    case 0: return View::Near;
    case 1: return View::Pov;
    default: return View::Far;
    }
}

CarDisplay PlayerCameras::display() const {
    const CarCamera* cur = m_view.current();
    if (cur == &m_dash)
        return m_dashActive ? CarDisplay::Dash : CarDisplay::Hidden;
    if (cur == &m_pov)
        return CarDisplay::Hidden;
    return CarDisplay::Body;
}

} // namespace mm2::game
