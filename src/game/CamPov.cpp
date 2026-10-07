// Point-of-view camera: PovCamCS.
// Ported from Open1560 (Midtown Madness 1 build 1560, code/midtown/game.asm),
// GPL-3.0, Copyright (C) Brick.
#include "game/CamPov.h"

#include "game/CamMath.h"

#include <cmath>

namespace mm2::game {

PovCamera::PovCamera(const PovCamParams& params, bool dash)
    : CarCamera(m_params.base, m_params.app), m_params(params), m_dash(dash) {}

void PovCamera::setParams(const PovCamParams& params) { m_params = params; }

float PovCamera::frand() {
    // xorshift32; the original's frand() generator is not known.
    m_random ^= m_random << 13;
    m_random ^= m_random >> 17;
    m_random ^= m_random << 5;
    return static_cast<float>(m_random >> 8) * (1.0f / 16777216.0f);
}

void PovCamera::reset(const CameraTarget& t) {
    // PovCamCS::Reset
    m_oneShot = 1;
    m_resetPosition = t.matrix.m3;
}

void PovCamera::update(float dt, const CameraTarget& t, const CameraProbe&, const CameraInput& input) {
    // PovCamCS::Update
    updatePov(dt, t, input);
    if (m_oneShot != 0)
        m_oneShot = 0;
}

void PovCamera::updatePov(float dt, const CameraTarget& t, const CameraInput& input) {
    // PovCamCS::UpdatePOV
    Mat34 car = t.matrix;
    car.m0 = cam::scaled(car.m0, cam::invMag(car.m0));
    car.m1 = cam::scaled(car.m1, cam::invMag(car.m1));
    car.m2 = cam::scaled(car.m2, cam::invMag(car.m2));

    const float pan = input.camPan * cam::kTwoPi;
    // Inferred (MM2 ReverseOffset): looking back moves the eye to the rear.
    const bool lookingBack = input.camPan >= 0.375f && input.camPan <= 0.625f;
    const Vec3 eye = options.reverseOffset && lookingBack && m_params.reverseOffset ? *m_params.reverseOffset
                                                                                    : m_params.offset;

    // matrix_ = (identity with m3 = Offset) * car
    m_goal.m0 = car.m0;
    m_goal.m1 = car.m1;
    m_goal.m2 = car.m2;
    m_goal.m3 = car.transform(eye);
    cam::rotateAxis(m_goal, m_goal.m0, m_params.pitch, false);
    if (pan > 0.0f)
        cam::rotateAxis(m_goal, m_goal.m1, pan, false);

    if (t.onGround) {
        // Road and engine shake.
        float shake = (std::abs(t.shakeWheelSpin) * dt - cam::kHalfPi) * 0.318309873f;
        shake = !(shake > 0.0f) ? 0.0f : (shake < 1.0f ? shake : 1.0f);
        const Vec3 axis{m_goal.m0.x + m_goal.m2.x, m_goal.m0.y + m_goal.m2.y, m_goal.m0.z + m_goal.m2.z};
        const float jitter =
            static_cast<float>(static_cast<double>(t.shakeAmplitude * static_cast<float>(frand() - 0.5)) * shake * 0.03);
        cam::rotateAxis(m_goal, axis, jitter, false);
        const float s = std::sin(t.shakePhase);
        const Vec3 axis2{m_goal.m0.x + m_goal.m2.x, m_goal.m0.y + m_goal.m2.y, m_goal.m0.z + m_goal.m2.z};
        const double scale = t.shakeFlag ? 0.005 : 0.008;
        const float vibration = static_cast<float>(static_cast<double>((1.0f - shake) * t.shakeAmplitude * s) * scale);
        cam::rotateAxis(m_goal, axis2, vibration, false);
    }

    approachIt(dt, t.matrix); // TrackTo is relative to the unnormalised car matrix
}

} // namespace mm2::game
