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
    }
    return "?";
}

PlayerCameras::PlayerCameras() : m_dash({}, true) { m_view.setCurrent(&m_near); }

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
    // mmPlayer::SetCamera
    const int currentIndex = m_group == 0 ? m_camIndex : 0;
    if (group == m_group && index == currentIndex)
        return;
    if (group == 0) {
        if (index >= 0 && index < 3) {
            m_camIndex = index;
            m_view.newCam(carCam(index), CameraView::Blend::EaseInOut, 0.8f);
        }
        m_savedIndex = m_camIndex;
        m_group = 0;
    } else if (group == 2) {
        m_view.setCurrent(&m_dash);
        m_group = 2;
    }
}

void PlayerCameras::reset(const CameraTarget& target) {
    // mmPlayer::Reset
    m_firstUpdate = true;
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
    if ((m_vehicleFlags & 0x13) != 0) {
        // Big vehicles use the _ind camera under cover. (The original also
        // switches in rooms with flag 0x20 that have geometry overhead; that
        // probe is not ported.)
        if ((target.roomFlags & 0x0A) == 0) {
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
    // mmViewMgr::SetViewSetting(6)
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

PlayerCameras::View PlayerCameras::view() const {
    if (m_group == 2)
        return View::Dash;
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
