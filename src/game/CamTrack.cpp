// Chase camera: TrackCamCS.
// Ported from Open1560 (Midtown Madness 1 build 1560, code/midtown/game.asm and
// mmcamcs/trackcamcs.cpp), GPL-3.0, Copyright (C) Brick. Function and field
// names refer to the original; "inferred" marks MM2 behaviour that is not in
// the MM1 code (see docs/camera.md).
#include "game/CamTrack.h"

#include "game/CamMath.h"

#include <algorithm>
#include <cmath>

namespace mm2::game {
namespace {

// YAXIS x v, written out as the original computes it.
Vec3 yCross(const Vec3& v) {
    constexpr float yx = 0.0f, yy = 1.0f, yz = 0.0f;
    return {v.z * yy - v.y * yz, v.x * yz - v.z * yx, v.y * yx - v.x * yy};
}

// |a - b|^2 summed as (dz^2 + dy^2) + dx^2 (TrackCamCS::Collide).
float dist2ZYX(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return (dz * dz + dy * dy) + dx * dx;
}

} // namespace

TrackCamera::TrackCamera(const TrackCamParams& params) : CarCamera(m_params.base, m_params.app), m_params(params) {}

void TrackCamera::setParams(const TrackCamParams& params) { m_params = params; }

void TrackCamera::reset(const CameraTarget&) {
    // TrackCamCS::Reset
    m_oneShot = 1;
    m_camera = Mat34::identity();
    m_goal = m_camera;
    m_frozen = false;
    m_matrixTouched = false;
    m_reverseTimer = 0.0f;
    m_reverseView = false;
    m_shared = {};
    m_hill = 0.0f;
}

void TrackCamera::update(float dt, const CameraTarget& t, const CameraProbe& probe, const CameraInput& input) {
    // TrackCamCS::Update
    const Mat34 previous = m_camera;
    updateCar(dt, t);
    updateHillState(dt, t);
    updateLookAndReverse(dt, t, input);
    updateTrack(dt, t);
    // UpdateSwing: the swing transitions only start when SplineState2/3 are
    // set, which no MM1 code does. Its tail resets SplineState1.
    if (m_splineState1 == 2)
        m_splineState1 = 0;
    preApproach(dt, t);
    approachIt(dt, t.matrix);
    if (m_params.minMaxOn != 0)
        minMax(previous, probe);
    if (m_params.collideType != 0)
        collide(dt, previous.m3, probe);
    if (m_oneShot != 0)
        m_oneShot = 0;
    if (m_matrixTouched) {
        m_matrixTouched = false;
        m_camera = m_goal;
    }
}

void TrackCamera::updateCar(float dt, const CameraTarget& t) {
    // TrackCamCS::UpdateCar
    m_carSteering = t.steering;
    m_carVelocity = cam::mag(t.velocity);
    const Vec3 up = cam::scaled(t.matrix.m1, cam::invMag(t.matrix.m1));
    if ((up.y < 0.35f && m_params.trackBreak == 1) || m_params.trackBreak == 2) {
        m_isOnGround = false;
        m_spinning = 1;
        return;
    }
    const Vec3& w = t.angularVelocity;
    const float spin2 = w.x * w.x + (w.y * w.y + w.z * w.z);
    if (t.wheelsOnGround > 2) {
        m_onGroundTime = m_onGroundTime + dt;
        if (m_onGroundTime > 0.1f) {
            m_isOnGround = true;
            m_inAirTime = 0.0f;
        }
    } else {
        m_inAirTime = m_inAirTime + dt;
        if (m_inAirTime > 0.1f) {
            m_isOnGround = false;
            m_onGroundTime = 0.0f;
        }
    }
    // 2.25e8 rad^2/s^2 (15000 rad/s): effectively never reached.
    m_spinning = (spin2 > 225000000.0f && !m_isOnGround) ? 1 : 0;
}

void TrackCamera::updateHillState(float dt, const CameraTarget& t) {
    // Inferred (MM2 HillMin/HillMax/HillLerp; MM1's UpdateHill is empty):
    // follow the car's pitch, clamped, with HillLerp as a per-update blend
    // factor at the original's 30 updates per second.
    if (!options.hill || (m_params.hillMin == 0.0f && m_params.hillMax == 0.0f)) {
        m_hill = 0.0f;
        return;
    }
    const Vec3 forward = cam::scaled(-t.matrix.m2, cam::invMag(t.matrix.m2));
    const float pitch = std::asin(clampf(forward.y, -1.0f, 1.0f)); // nose up > 0
    const float target = clampf(pitch, m_params.hillMin, m_params.hillMax);
    const float lerp = clampf(m_params.hillLerp, 0.0f, 1.0f);
    const float k = lerp >= 1.0f ? 1.0f : 1.0f - std::pow(1.0f - lerp, std::max(dt, 0.0f) * 30.0f);
    m_hill += (target - m_hill) * k;
}

void TrackCamera::applyHill() {
    // Inferred: tilt the camera's offset from the target about the camera's
    // right axis by the hill angle, so it follows the slope (lower uphill,
    // higher downhill).
    if (!options.hill || m_hill == 0.0f)
        return;
    const Vec3 axis = cam::scaled(m_goal.m0, cam::invMag(m_goal.m0));
    const Vec3 rel = m_goal.m3 - m_target;
    m_goal.m3 = Mat34::rotationAxis(axis, m_hill).transformDir(rel) + m_target;
}

void TrackCamera::updateLookAndReverse(float dt, const CameraTarget& t, const CameraInput& input) {
    // Inferred (MM2): TrackCamCS::UpdateInput is empty in MM1 and nothing
    // drives the shared swing angle there. Look around uses the same CamPan
    // turn fractions as MM1's point-of-view camera; the reverse view swings
    // the camera to the front after RevDelay seconds of reversing.
    const Vec3 forward = cam::scaled(-t.matrix.m2, cam::invMag(t.matrix.m2));
    const bool backing = t.reverse && t.velocity.dot(forward) < -0.5f;
    const bool wantReverse = options.reverseView && m_params.reverseOn != 0 && backing;
    m_reverseTimer = wantReverse ? m_reverseTimer + dt : 0.0f;
    const bool reverseView = wantReverse && m_reverseTimer >= m_params.revDelay;
    if (reverseView != m_reverseView) {
        m_reverseView = reverseView;
        const float rate = reverseView ? m_params.revOnApp : m_params.revOffApp;
        if (rate > 0.0f)
            m_params.app.appXZPos = rate;
    }

    float yaw = options.lookAround ? input.camPan * cam::kTwoPi : 0.0f;
    if (yaw > cam::kPi)
        yaw -= cam::kTwoPi;
    if (m_reverseView && input.camPan == 0.0f)
        yaw = cam::kPi;
    m_shared.yRot = yaw;
}

void TrackCamera::updateTrack(float dt, const CameraTarget& t) {
    // TrackCamCS::UpdateTrack
    const Mat34& car = t.matrix;
    if (m_splineState1 == 1) {
        m_camera = m_goal;
        m_splineState1 = 2;
    }
    if (m_splineState3 == 2) {
        m_camera = m_goal;
        m_splineState3 = 3;
    }
    if (m_oneShot == 2) {
        m_shared = {};
        m_oneShot = 1;
    }
    // TrackSpline (swing animation) is never active in MM1: SplineState1 = 0.
    m_splineState1 = 0;

    m_target = car.transform(m_params.app.trackTo);
    const Vec3 delta{m_target.x - m_previousTarget.x, m_target.y - m_previousTarget.y,
                     m_target.z - m_previousTarget.z};
    m_previousTarget = m_target;

    const Vec3& o = m_params.offset;
    const float distance = o.z > 0.01f ? o.z : 0.01f;
    auto place = [&](const Vec3& dir) {
        Vec3 side = yCross(dir);
        side = cam::scaled(side, cam::invMag(side));
        const Vec3& f = m_target;
        return Vec3{distance * dir.x + (f.x + o.x * side.x), o.y + (distance * dir.y + (f.y + o.x * side.y)),
                    distance * dir.z + (f.z + o.x * side.z)};
    };

    Vec3 position;
    if (m_splineState3 == 1) {
        // Placed ahead of the car (start of a swing to the rear).
        Vec3 ahead{-car.m2.x, 0.0f, -car.m2.z};
        position = place(cam::scaled(ahead, cam::invMag(ahead)));
    } else if ((!m_isOnGround && (m_spinning != 0 || m_frozen)) || m_spinning == 2) {
        // Airborne or upside down: keep the last offset, just follow the car.
        m_spinning = 1;
        m_frozen = true;
        position = {m_previousDesired.x + delta.x, m_previousDesired.y + delta.y, m_previousDesired.z + delta.z};
    } else {
        m_frozen = false;
        Vec3 behind{car.m2.x, 0.0f, car.m2.z};
        position = place(cam::scaled(behind, cam::invMag(behind)));
    }

    m_target.y = m_target.y - (-0.4f);
    m_desiredPosition = position;
    m_previousDesired = position;
    m_params.app.lookAbove = static_cast<float>(static_cast<double>(o.y) - 0.8) * m_params.vertOffset;
    cam::lookAt(m_goal, m_desiredPosition, m_target);
    if (m_splineState1 != 2)
        applyHill();

    const float approachAmount = m_shared.approachAmount;
    float yRot = m_shared.yRot;
    if (m_params.steerOn != 0) {
        if (!(m_steerTimer < m_params.driftDelay)) {
            // Let the steering swing decay back to zero.
            const float step = dt * 1.2f;
            if (m_steerTarget > 0.0f)
                m_steerTarget = m_steerTarget > step ? m_steerTarget - step : 0.0f;
            else
                m_steerTarget = -m_steerTarget > step ? m_steerTarget + step : 0.0f;
        }
        float speedFactor = 1.0f;
        if (m_params.minSpeed != m_params.maxSpeed) {
            const float v = m_carVelocity;
            const float c = !(v > m_params.minSpeed) ? m_params.minSpeed : (v < m_params.maxSpeed ? v : m_params.maxSpeed);
            speedFactor = (c - m_params.minSpeed) / (m_params.maxSpeed - m_params.minSpeed);
        }
        float steerFactor = 1.0f;
        if (m_params.steerMin != 1.0f) {
            const float a = std::abs(m_carSteering);
            const float c = !(a > m_params.steerMin) ? m_params.steerMin : (a < 1.0f ? a : 1.0f);
            steerFactor = (c - m_params.steerMin) / (1.0f - m_params.steerMin);
        }
        const float target = speedFactor * (m_params.steerAmt * m_carSteering * steerFactor);
        const float rate = dt * 0.8f;
        const float diff = target - m_steerTarget;
        if (diff < 0.0f)
            m_steerTarget = -diff > rate ? m_steerTarget - rate : target;
        else
            m_steerTarget = diff > rate ? rate + m_steerTarget : target;
        yRot = m_shared.yRot + m_steerTarget;
    }

    const float xRot = m_xRotBase + m_shared.xRot;
    if (!(approachAmount == 0.0f && yRot == 0.0f && xRot == 0.0f)) {
        // Swing the goal about the target point.
        m_goal.m3 = {m_goal.m3.x - m_target.x, m_goal.m3.y - m_target.y, m_goal.m3.z - m_target.z};
        if (xRot != 0.0f)
            cam::rotateAxis(m_goal, yCross(m_goal.m2), -xRot, true);
        if (yRot != 0.0f)
            cam::rotateFullY(m_goal, yRot);
        m_goal.m3 = {m_goal.m3.x + m_target.x, m_goal.m3.y + m_target.y, m_goal.m3.z + m_target.z};
        if (approachAmount != 0.0f) {
            const Vec3 d{m_goal.m3.x - m_target.x, m_goal.m3.y - m_target.y, m_goal.m3.z - m_target.z};
            m_goal.m3 = {d.x * approachAmount + m_goal.m3.x, d.y * approachAmount + m_goal.m3.y,
                         d.z * approachAmount + m_goal.m3.z};
        }
    }

    const Vec3 lookPoint{m_target.x, m_target.y + m_params.app.lookAbove, m_target.z};
    const Vec3 from = m_goal.m3;
    cam::lookAt(m_goal, from, lookPoint);
}

void TrackCamera::preApproach(float dt, const CameraTarget& t) {
    // TrackCamCS::PreApproach as reconstructed by Open1560 (mmcamcs/
    // trackcamcs.cpp): AppXZPos moves from MaxAppXZPos (slow) to MinAppXZPos
    // (fast) with speed, limited by AppInc/AppDec per second. Inferred for MM2.
    if (!options.preApproach || m_reverseView)
        return;
    const TrackCamParams& p = m_params;
    if (p.minAppXZPos == 0.0f || p.minSpeed == p.maxSpeed)
        return;
    const float v = m_carVelocity;
    float scale;
    if (v <= p.minSpeed)
        scale = 0.0f;
    else if (v >= p.maxSpeed)
        scale = 1.0f;
    else
        scale = (v - p.minSpeed) / (p.maxSpeed - p.minSpeed);
    float targetXz = (p.minAppXZPos - p.maxAppXZPos) * scale + p.maxAppXZPos;
    if (t.reverse && !(options.reverseView && p.reverseOn != 0))
        targetXz = 20.0f;
    float& xz = m_params.app.appXZPos;
    if (targetXz < xz)
        xz = std::max(targetXz, xz - p.appDec * dt);
    else if (targetXz > xz)
        xz = std::min(targetXz, xz + p.appInc * dt);
}

void TrackCamera::minMax(const Mat34& previous, const CameraProbe& probe) {
    // TrackCamCS::MinMax: keep the camera between the ground and any
    // overhang, probing vertically from last update's height.
    if (!probe)
        return;
    const Vec3 c = m_camera.m3;
    const float prevY = previous.m3.y;
    Vec3 hit, normal;
    bool ceilingHit = false, floorHit = false;
    float ceiling = 0.0f, floor = 0.0f;
    if (probe({c.x, prevY, c.z}, {c.x, prevY + 5.0f, c.z}, hit, normal) && normal.y < 0.7f) {
        ceiling = hit.y - 0.5f;
        ceilingHit = true;
    }
    const Vec3 from = ceilingHit ? Vec3{c.x, ceiling, c.z} : Vec3{c.x, prevY, c.z};
    if (probe(from, {c.x, prevY - 5.0f, c.z}, hit, normal) && normal.y > 0.7f) {
        floor = hit.y + 0.5f;
        floorHit = true;
    }
    if (ceilingHit) {
        if (floorHit && ceiling < floor)
            ceilingHit = floorHit = false;
        if (ceilingHit && m_camera.m3.y > ceiling)
            m_camera.m3.y = ceiling;
    }
    if (floorHit && m_camera.m3.y < floor)
        m_camera.m3.y = floor;
}

void TrackCamera::collide(float dt, Vec3 prev, const CameraProbe& probe) {
    // TrackCamCS::Collide (CollideType 2): if geometry is between the target
    // and the camera, pull the camera in front of it, then ease back out.
    const bool enabled = m_params.collideType == 2 || (m_params.collideType == 1 && options.collideType1);
    if (!enabled || !probe)
        return;
    const Vec3 cam0 = m_camera.m3;
    const Vec3 tgt = m_target;
    Vec3 dir{cam0.x - tgt.x, cam0.y - tgt.y, cam0.z - tgt.z};
    dir = cam::scaled(dir, cam::invMag(dir));
    const Vec3 end{cam0.x + dir.x, cam0.y + dir.y, cam0.z + dir.z};

    Vec3 hit, normal;
    if (probe(tgt, end, hit, normal)) {
        m_wasColliding = true;
        m_collideBaseDist2 = dist2ZYX(tgt, cam0);
        const Vec3 v{hit.x - dir.x, hit.y - dir.y, hit.z - dir.z};
        const float dx = tgt.x - v.x, dy = tgt.y - v.y, dz = tgt.z - v.z;
        const float close2 = std::abs((dx * dx + dy * dy) + dz * dz);
        Vec3 position;
        if (close2 < 50.0f) {
            const float k = close2 * 0.02f;
            const Vec3 pulled{hit.x - k * dir.x, hit.y - k * dir.y, hit.z - k * dir.z};
            if (m_oneShot != 0) {
                position = pulled;
            } else {
                cam::approach(prev, pulled, m_params.app.maxDist, dt * 15.0f, nullptr);
                position = prev;
            }
        } else {
            const Vec3 pulled{hit.x - dir.x, hit.y - dir.y, hit.z - dir.z};
            if (m_oneShot != 0) {
                position = pulled;
            } else {
                cam::approach(prev, pulled, m_params.app.minDist, dt * 15.0f, nullptr);
                position = prev;
            }
        }
        const float inv = 1.0f / m_collideBaseDist2;
        m_camera.m3 = position;
        const float d2 = dist2ZYX(tgt, position);
        m_collideDist2 = d2;
        m_params.app.lookAbove = inv * d2 * m_params.app.lookAbove;
        return;
    }

    if (m_wasColliding) {
        if (m_collideDist2 < m_collideBaseDist2 && m_oneShot == 0) {
            const float diff = m_collideBaseDist2 - m_collideDist2;
            const float step = dt * 30.0f;
            if (!(diff < 0.0f))
                m_collideDist2 = diff > step ? m_collideDist2 + step : m_collideBaseDist2;
            else
                m_collideDist2 = -diff > step ? m_collideDist2 - step : m_collideBaseDist2;
            // invsqrtf_fast(d2) * d2 in the original (a table-seeded approximation).
            const float distance = m_collideDist2 / std::sqrt(m_collideDist2);
            m_camera.m3 = {distance * dir.x + tgt.x, distance * dir.y + tgt.y, distance * dir.z + tgt.z};
            return;
        }
        m_wasColliding = false;
    }
}

} // namespace mm2::game
