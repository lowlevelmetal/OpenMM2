// Car camera base: BaseCamCS / AppCamCS / CarCamCS.
// Ported from Open1560 (Midtown Madness 1 build 1560, code/midtown/game.asm),
// GPL-3.0, Copyright (C) Brick. Function names refer to the original methods.
#include "game/CamCar.h"

#include "game/CamMath.h"

#include <cmath>

namespace mm2::game {

float cameraPanFor(bool left, bool right, bool back, bool forward) {
    // mmInput::GetCamPan (digital part): bits 0x8000 back, 0x10000 forward,
    // 0x2000 left, 0x4000 right.
    if (back)
        return left ? 0.375f : (right ? 0.625f : 0.5f);
    if (forward)
        return left ? 0.125f : (right ? 0.875f : 0.0f);
    if (left)
        return 0.25f;
    if (right)
        return 0.75f;
    return 0.0f;
}

void CarCamera::forceMatrixDelta(const Vec3& d) {
    m_camera.m3 = {d.x + m_camera.m3.x, m_camera.m3.y + d.y, d.z + m_camera.m3.z};
    m_goal.m3 = {d.x + m_goal.m3.x, m_goal.m3.y + d.y, m_goal.m3.z + d.z};
}

void CarCamera::apply(Camera& out) const {
    out.transform = m_camera;
    out.horizontalFov = m_base->cameraFov * cam::kDegToRad;
    out.nearPlane = m_base->cameraNear;
    out.farPlane = m_base->cameraFar;
}

bool CarCamera::dApproach(float& value, float goal, float rampDist, float maxSpeed, float& velocity,
                          float rateDt) const {
    const float d = std::abs(goal - value);
    float speed = d;
    // Within rampDist the speed falls off quadratically, so the camera eases in.
    if (rampDist != 0.0f && !(d > rampDist))
        speed = d * d / rampDist;
    if (maxSpeed != 0.0f && d > maxSpeed)
        speed = maxSpeed;
    if (m_app->appAppOn != 0) {
        // Low-pass filter the speed; a sign flip stops the motion.
        const float filtered = (speed - velocity) * m_app->appApp + velocity;
        velocity = filtered;
        if ((filtered < 0.0f && speed > 0.0f) || (filtered > 0.0f && speed < 0.0f)) {
            velocity = 0.0f;
            speed = 0.0f;
        } else {
            speed = filtered;
        }
    }
    const float diff = goal - value;
    const float step = speed * rateDt;
    if (!(diff < 0.0f)) {
        if (!(diff > step)) {
            value = goal;
            return true;
        }
        value = step + value;
        return false;
    }
    if (!(-diff > step)) {
        value = goal;
        return true;
    }
    value = value - step;
    return false;
}

void CarCamera::updateMaxDist() {
    const AppCamParams& a = *m_app;
    if (a.minDist > a.maxDist)
        return;
    Vec3& c = m_camera.m3;
    const Vec3& f = m_target;
    if (c.x == f.x && c.y == f.y && c.z == f.z)
        return;

    auto dist2 = [&] {
        const float dx = c.x - f.x, dy = c.y - f.y, dz = c.z - f.z;
        return (dy * dy + dz * dz) + dx * dx;
    };
    auto placeAt = [&](float distance) {
        const Vec3 d{c.x - f.x, c.y - f.y, c.z - f.z};
        const Vec3 v = cam::scaled(d, cam::invMag(d));
        c = {distance * v.x + f.x, distance * v.y + f.y, distance * v.z + f.z};
    };
    if (a.maxDist * a.maxDist < dist2())
        placeAt(a.maxDist);
    if (a.minDist * a.minDist > dist2())
        placeAt(a.minDist);
}

void CarCamera::updateApproach(float dt, const Mat34& car) {
    const AppCamParams& a = *m_app;
    m_target = car.transform(a.trackTo);

    dApproach(m_camera.m3.x, m_goal.m3.x, a.appPosMin, 0.0f, m_posVelocity.x, a.appXZPos * dt);
    dApproach(m_camera.m3.y, m_goal.m3.y, a.appPosMin, 0.0f, m_posVelocity.y, a.appYPos * dt);
    dApproach(m_camera.m3.z, m_goal.m3.z, a.appPosMin, 0.0f, m_posVelocity.z, a.appXZPos * dt);
    if (a.maxDist != 0.0f)
        updateMaxDist();

    Mat34 current = m_camera;
    current.m3 = {};
    Mat34 goal = m_goal;
    goal.m3 = {};
    Vec3 cur = cam::getEulersZXY(current);
    Vec3 want = cam::getEulersZXY(goal);

    // Take the short way round when the angles straddle +-180 degrees.
    auto unwrap = [](float c, float& g) {
        if (c > cam::kHalfPi && g < -cam::kHalfPi)
            g = g - (-cam::kTwoPi);
        if (c < -cam::kHalfPi && g > cam::kHalfPi)
            g = g - cam::kTwoPi;
    };
    unwrap(cur.x, want.x);
    unwrap(cur.y, want.y);
    unwrap(cur.z, want.z);

    if (a.lookAt != 0.0f) {
        // Blend the goal orientation towards looking straight at TrackTo.
        const Vec3 lookPoint{m_target.x, m_target.y + a.lookAbove, m_target.z};
        Mat34 look = m_camera;
        cam::lookAt(look, m_camera.m3, lookPoint);
        look.m3 = {};
        Vec3 l = cam::getEulersZXY(look);
        unwrap(cur.x, l.x);
        unwrap(cur.y, l.y);
        unwrap(cur.z, l.z);
        const float la = a.lookAt, ila = 1.0f - a.lookAt;
        want = {la * l.x + ila * want.x, la * l.y + ila * want.y, la * l.z + ila * want.z};
    }

    const float xRot = a.appXRot != 0.0f ? a.appXRot : a.appRot;
    dApproach(cur.z, want.z, a.appRotMin, 0.0f, m_rotVelocity.z, a.appRot * dt);
    dApproach(cur.x, want.x, a.appRotMin, 0.0f, m_rotVelocity.x, xRot * dt);
    dApproach(cur.y, want.y, a.appRotMin, 0.0f, m_rotVelocity.y, a.appRot * dt);
    Mat34 rot;
    cam::fromEulersZXY(rot, cur);
    m_camera.m0 = rot.m0;
    m_camera.m1 = rot.m1;
    m_camera.m2 = rot.m2;
}

void CarCamera::approachIt(float dt, const Mat34& car) {
    if (m_app->approachOn != 0 && m_oneShot == 0)
        updateApproach(dt, car);
    else
        m_camera = m_goal;
}

} // namespace mm2::game
