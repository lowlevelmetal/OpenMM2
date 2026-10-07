// Race-flow cameras: camPreCS and camPointCS, ported from Midtown Madness 2
// (MM2Recomp, build 3393).
#include "game/CamRace.h"

#include "game/CamMath.h"

#include <cmath>

namespace mm2::game {

PreCamera::PreCamera() : CarCamera(m_params.base, m_params.app) {}

void PreCamera::makeActive(const CameraTarget& t) {
    // camPreCS::MakeActive
    m_azimuth = std::atan2(t.matrix.m2.x, t.matrix.m2.z) + m_params.azimuthOffset;
}

void PreCamera::update(float, const CameraTarget& t, const CameraProbe&, const CameraInput&,
                       const CameraPerspective&) {
    // camPreCS::Update: a polar view about the car, written straight to the
    // camera (no approach).
    cam::polarView(m_camera, m_params.polarDistance, m_azimuth, m_params.polarIncline, 0.0f);
    const Vec3& p = t.matrix.m3;
    m_camera.m3 = {p.x + m_camera.m3.x, p.y + m_camera.m3.y, p.z + m_camera.m3.z};
    m_camera.m3.y = m_params.polarHeight + m_camera.m3.y;
}

PointCamera::PointCamera() : CarCamera(m_baseParams, m_appParams) {}

void PointCamera::setPosition(const Vec3& p) {
    m_position = p;
    m_camera.m3 = p;
}

void PointCamera::setMaxDist(float d) {
    m_maxDist = d;
    m_maxDist2 = d * d;
}

void PointCamera::setMinDist(float d) {
    m_minDist = d;
    m_minDist2 = d * d;
}

void PointCamera::update(float dt, const CameraTarget& t, const CameraProbe&, const CameraInput&,
                         const CameraPerspective&) {
    // camPointCS::Update: drift with the velocity (which decays as
    // exp(-t / 2)), look at the car, and zoom in from 60 to 25 degrees as it gets
    // from 0.3 x MaxDist to MaxDist away.
    Vec3& v = m_velocity;
    if ((v.x * v.x + v.y * v.y) + v.z * v.z != 0.0f) {
        const Vec3 step{dt * v.x, dt * v.y, dt * v.z};
        m_camera.m3 = {step.x + m_camera.m3.x, step.y + m_camera.m3.y, step.z + m_camera.m3.z};
        const Vec3 drag{step.x * 0.5f, step.y * 0.5f, step.z * 0.5f};
        v = {v.x - drag.x, v.y - drag.y, v.z - drag.z};
    }
    const Vec3 car = t.matrix.m3;
    const Vec3 eye = m_camera.m3;
    cam::lookAt(m_camera, eye, car);
    const Vec3 d{eye.x - car.x, eye.y - car.y, eye.z - car.z};
    const float d2 = (d.z * d.z + d.y * d.y) + d.x * d.x;
    const float wide = m_maxDist * 0.3f;
    const float wide2 = wide * wide;
    if (d2 > wide2) {
        const float k = d2 < m_maxDist2 ? (d2 - wide2) / (m_maxDist2 - wide2) : 1.0f;
        m_baseParams.cameraFov = 60.0f - k * 35.0f;
    } else {
        m_baseParams.cameraFov = 60.0f;
    }
}

} // namespace mm2::game
