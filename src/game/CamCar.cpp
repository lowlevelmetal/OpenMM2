// Car camera base: camBaseCS / camAppCS / camCarCS, ported from Midtown
// Madness 2 (MM2Recomp, build 3393). Function names refer to the original
// methods.
#include "game/CamCar.h"

#include "game/CamMath.h"

#include <cmath>

namespace mm2::game {

int CameraTarget::wheelsOnGround() const {
    // vehCarSim::OnGround
    int n = 0;
    for (const auto& w : wheels)
        n += w.onGround ? 1 : 0;
    return n;
}

float cameraPanFor(bool left, bool right, bool back, bool forward) {
    // mmInput::GetCamPan (digital part): bits 0x10000 back, 0x20000 forward,
    // 0x4000 left, 0x8000 right.
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
    m_camera.m3 = {m_camera.m3.x + d.x, d.y + m_camera.m3.y, d.z + m_camera.m3.z};
    m_goal.m3 = {m_goal.m3.x + d.x, d.y + m_goal.m3.y, d.z + m_goal.m3.z};
}

void CarCamera::apply(Camera& out) const {
    out.transform = m_camera;
    out.horizontalFov = cam::horizontalFov4x3(m_base->cameraFov);
    out.nearPlane = m_base->cameraNear;
    out.farPlane = m_base->cameraFar;
}

bool CarCamera::dApproach(float& value, float goal, float rampDist, float maxSpeed, float& velocity,
                          float rateDt) const {
    // camAppCS::DApproach
    const float d = std::abs(goal - value);
    float speed = d;
    // Within rampDist the speed falls off quadratically, so the camera eases in.
    if (rampDist != 0.0f && d <= rampDist)
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
    if (value < goal) {
        value = speed * rateDt + value;
        if (value > goal)
            value = goal;
    } else if (value > goal) {
        value = value - speed * rateDt;
        if (value < goal)
            value = goal;
    }
    return value == goal;
}

Vec3 CarCamera::trackToWorld(const Mat34& c) const {
    const Vec3& t = m_app->trackTo;
    return {((c.m2.x * t.z + c.m1.x * t.y) + t.x * c.m0.x) + c.m3.x,
            ((c.m2.y * t.z + c.m0.y * t.x) + c.m1.y * t.y) + c.m3.y,
            ((c.m2.z * t.z + c.m0.z * t.x) + c.m1.z * t.y) + c.m3.z};
}

void CarCamera::updateMaxDist() {
    // camAppCS::UpdateMaxDist
    const AppCamParams& a = *m_app;
    if (a.minDist > a.maxDist)
        return;
    Vec3& c = m_camera.m3;
    const Vec3& f = m_target;
    if (c.x == f.x && c.y == f.y && c.z == f.z)
        return;

    auto dist2 = [&] {
        const float dx = c.x - f.x, dy = c.y - f.y, dz = c.z - f.z;
        return (dz * dz + dy * dy) + dx * dx;
    };
    auto placeAt = [&](float distance) {
        Vec3 d{c.x - f.x, c.y - f.y, c.z - f.z};
        const float m2 = (d.z * d.z + d.y * d.y) + d.x * d.x;
        d = cam::scaled(d, m2 == 0.0f ? 0.0f : 1.0f / std::sqrt(m2));
        c = {d.x * distance + f.x, d.y * distance + f.y, d.z * distance + f.z};
    };
    if (a.maxDist * a.maxDist < dist2())
        placeAt(a.maxDist);
    if (a.minDist * a.minDist > dist2())
        placeAt(a.minDist);
}

void CarCamera::updateApproach(float dt, const Mat34& car) {
    // camAppCS::UpdateApproach
    const AppCamParams& a = *m_app;
    m_target = trackToWorld(car);

    dApproach(m_camera.m3.x, m_goal.m3.x, a.appPosMin, 0.0f, m_posVelocity.x, a.appXZPos * dt);
    dApproach(m_camera.m3.y, m_goal.m3.y, a.appPosMin, 0.0f, m_posVelocity.y, a.appYPos * dt);
    dApproach(m_camera.m3.z, m_goal.m3.z, a.appPosMin, 0.0f, m_posVelocity.z, a.appXZPos * dt);
    if (a.maxDist != 0.0f)
        updateMaxDist();

    Vec3 cur = cam::getEulersZXY(m_camera);
    Vec3 want = cam::getEulersZXY(m_goal);

    // Take the short way round when the angles straddle +-180 degrees.
    auto unwrap = [](float c, float& g) {
        if (c > cam::kHalfPi && g < -cam::kHalfPi)
            g = g + cam::kTwoPi;
        if (c < -cam::kHalfPi && g > cam::kHalfPi)
            g = g - cam::kTwoPi;
    };
    unwrap(cur.x, want.x);
    unwrap(cur.y, want.y);
    unwrap(cur.z, want.z);

    if (a.lookAt != 0.0f) {
        // Blend the goal orientation towards looking straight at TrackTo.
        const Vec3 lookPoint{m_target.x, a.lookAbove + m_target.y, m_target.z};
        Mat34 look = m_camera;
        cam::lookAt(look, m_camera.m3, lookPoint);
        Vec3 l = cam::getEulersZXY(look);
        unwrap(cur.x, l.x);
        unwrap(cur.y, l.y);
        unwrap(cur.z, l.z);
        const float la = a.lookAt, ila = 1.0f - a.lookAt;
        want = {ila * want.x + la * l.x, ila * want.y + la * l.y, ila * want.z + la * l.z};
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
    // camAppCS::ApproachIt
    if (m_app->approachOn != 0 && m_oneShot == 0)
        updateApproach(dt, car);
    else
        m_camera = m_goal;
}

} // namespace mm2::game
