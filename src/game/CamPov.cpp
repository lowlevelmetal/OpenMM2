// Point-of-view camera: camPovCS, ported from Midtown Madness 2 (MM2Recomp,
// build 3393).
#include "game/CamPov.h"

#include "game/CamMath.h"

namespace mm2::game {

PovCamera::PovCamera(const PovCamParams& params, bool dash)
    : CarCamera(m_params.base, m_params.app), m_params(params), m_dash(dash) {}

void PovCamera::setParams(const PovCamParams& params) { m_params = params; }

void PovCamera::reset(const CameraTarget& t) {
    // camPovCS::Reset
    m_oneShot = 1;
    m_resetPosition = t.matrix.m3;
}

void PovCamera::update(float dt, const CameraTarget& t, const CameraProbe&, const CameraInput&,
                       const CameraPerspective&) {
    // camPovCS::Update / UpdatePOV: the eye is fixed in the car's frame; the
    // view is pitched about its right axis, then turned about its up axis
    // to look around.
    m_goal = Mat34::identity();
    m_goal.m3 = m_reverse ? m_params.reverseOffset : m_params.offset;
    cam::dot(m_goal, t.matrix);
    const float pitch = m_reverse ? -m_params.pitch : m_params.pitch;
    if (pitch != 0.0f)
        cam::rotate(m_goal, m_goal.m0, pitch);
    float yaw = m_reverse ? cam::kPi : 0.0f;
    if (m_pan != 0.0f)
        yaw = m_pan;
    if (yaw != 0.0f)
        cam::rotate(m_goal, m_goal.m1, yaw);
    approachIt(dt, t.matrix);
    if (m_oneShot != 0)
        m_oneShot = 0;
}

} // namespace mm2::game
