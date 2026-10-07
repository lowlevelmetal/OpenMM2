// A player's car cameras (mmPlayer camera handling).
// Ported from Open1560 (Midtown Madness 1 build 1560, code/midtown/game.asm),
// GPL-3.0, Copyright (C) Brick.
#include "game/CamPlayer.h"

#include "core/Log.h"
#include "core/StringUtil.h"

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

void PlayerCameras::load(const vfs::Vfs& vfs, std::string_view carIn, std::vector<std::string>* missing) {
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

void PlayerCameras::reset(const CameraTarget& target) {
    m_view.setCurrent(&camera(m_viewId));
    m_view.reset(target);
}

void PlayerCameras::update(float dt, const CameraTarget& target, const CameraProbe& probe, const CameraInput& input) {
    m_view.update(dt, target, probe, input);
}

void PlayerCameras::select(View view, CameraView::Blend blend, float seconds) {
    // Keep "Change Camera" cycling on from the selected view (MM1 only ever
    // selected these views through ToggleCam, so CamIndex always matched).
    for (std::size_t i = 0; i < m_cycle.size(); ++i)
        if (m_cycle[i] == view)
            m_cycleIndex = static_cast<int>(i);
    m_viewId = view;
    m_view.newCam(&camera(view), blend, seconds);
}

void PlayerCameras::toggleCamera() {
    // mmPlayer::ToggleCam: CamIndex wraps at XCamStart (3); NewCam(cam, 3, 0.8).
    if (m_dashOn)
        m_dashOn = false;
    m_cycleIndex = m_cycleIndex == static_cast<int>(m_cycle.size()) - 1 ? 0 : m_cycleIndex + 1;
    select(m_cycle[static_cast<std::size_t>(m_cycleIndex)], CameraView::Blend::EaseInOut, 0.8f);
}

void PlayerCameras::setDashboard(bool on) {
    if (on == m_dashOn)
        return;
    m_dashOn = on;
    // Inferred: MM1 used NewCam mode 1 / 0.3 s into the cockpit and mode 2 /
    // 0.3 s back out for its dashboard transitions (mmPlayer::Update).
    if (on)
        select(View::Dash, CameraView::Blend::EaseIn, 0.3f);
    else
        select(m_cycle[static_cast<std::size_t>(m_cycleIndex)], CameraView::Blend::EaseOut, 0.3f);
}

CarDisplay PlayerCameras::display() const {
    const CarCamera* cam = m_view.current();
    return cam ? cam->display() : CarDisplay::Body;
}

} // namespace mm2::game
