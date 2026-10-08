// Chase camera: camTrackCS, ported from Midtown Madness 2 (MM2Recomp, build
// 3393). Function and field names refer to the original; see docs/camera.md.
#include "game/CamTrack.h"

#include "game/CamMath.h"

#include <cmath>

namespace mm2::game {
namespace {

constexpr Vec3 kY = cam::kYAxis;

// YAXIS x v, written out as the original computes it.
Vec3 yCross(const Vec3& v) {
    return {kY.y * v.z - kY.z * v.y, kY.z * v.x - kY.x * v.z, kY.x * v.y - kY.y * v.x};
}

// Average of the ground normals of the wheels on the ground of one axle.
Vec3 axleNormal(const CameraTarget::Wheel& a, const CameraTarget::Wheel& b) {
    if (a.onGround && b.onGround)
        return {(a.normal.x + b.normal.x) * 0.5f, (a.normal.y + b.normal.y) * 0.5f, (a.normal.z + b.normal.z) * 0.5f};
    if (a.onGround)
        return a.normal;
    if (b.onGround)
        return b.normal;
    return {};
}

float mag2ZYX(const Vec3& v) { return (v.z * v.z + v.y * v.y) + v.x * v.x; }

} // namespace

TrackCamera::TrackCamera(const TrackCamParams& params) : CarCamera(m_params.base, m_params.app), m_params(params) {}

void TrackCamera::setParams(const TrackCamParams& params) { m_params = params; }

void TrackCamera::reset(const CameraTarget&) {
    // camTrackCS::Reset
    m_camera = m_goal;
    m_oneShot = 1;
    m_frozen = false;
    m_matrixTouched = false;
    m_reverseTimer = 0.0f;
    m_reverseView = false;
    m_reverseSign = 1.0f;
}

void TrackCamera::update(float dt, const CameraTarget& t, const CameraProbe& probe, const CameraInput& input,
                         const CameraPerspective& view) {
    // camTrackCS::Update
    if (m_oneShot != 0)
        m_shared = {};
    const Mat34 previous = m_camera;
    updateCar(dt, t);
    updateHill(t);
    updateTrack(dt, t);
    // UpdateSwing is empty in MM2; the swing spline (Front, Rear,
    // SwingToRear) is updated every frame but nothing starts it.
    preApproach(dt, t);
    approachIt(dt, t.matrix);
    if (m_params.minMaxOn != 0)
        minMax(previous, probe);
    if (m_params.collideType != 0)
        collide(dt, previous.m3, probe, input, view);
    if (m_oneShot != 0)
        m_oneShot = 0;
    if (m_matrixTouched) {
        m_matrixTouched = false;
        m_camera = m_goal;
    }
}

void TrackCamera::updateCar(float dt, const CameraTarget& t) {
    // camTrackCS::UpdateCar
    m_carSteering = t.steering;
    m_carSpeed = t.speed;
    // Swing to the front after 2 s of driving backwards (RevDelay is loaded
    // but not read), and back behind when out of reverse. The steering
    // picks which way round the camera goes.
    if (m_params.reverseOn == 1) {
        if (t.reverseGear) {
            if (t.handBrake > 0.5f)
                m_params.app.appXZPos = 0.0f;
            if (!m_reverseView && t.throttle >= 0.05f) {
                m_reverseTimer = dt + m_reverseTimer;
                if (m_reverseTimer >= 2.0f) {
                    m_reverseView = true;
                    m_reverseSign = static_cast<double>(m_carSteering) <= 0.1 ? 1.0f : -1.0f;
                }
            }
        } else {
            if (m_reverseView) {
                const float sign = static_cast<double>(m_carSteering) <= 0.1 ? -1.0f : 1.0f;
                m_reverseSign = sign;
                m_reverseView = false;
                // Fully round: carry on turning the same way back behind.
                if (std::abs(m_shared.yRot) > 3.1405928f)
                    m_shared.yRot = sign * cam::kPi;
            }
            m_reverseTimer = 0.0f;
        }
    } else if (m_params.reverseOn == -1) {
        m_reverseView = true;
    }

    if ((t.matrix.m1.y < 0.35f && m_params.trackBreak == 1) || m_params.trackBreak == 2) {
        m_spinning = 1;
        m_isOnGround = false;
        return;
    }
    // The spin test reads the angular momentum (vehCarSim +0x60, the
    // inertial body's +0x48), not the angular velocity.
    const Vec3& l = t.angularMomentum;
    const float spin2 = (l.x * l.x + l.y * l.y) + l.z * l.z;
    if (t.wheelsOnGround() < 3) {
        m_inAirTime = dt + m_inAirTime;
        if (m_inAirTime > 0.1f) {
            m_isOnGround = false;
            m_onGroundTime = 0.0f;
        }
    } else {
        m_onGroundTime = dt + m_onGroundTime;
        if (m_onGroundTime > 0.1f) {
            m_isOnGround = true;
            m_inAirTime = 0.0f;
        }
    }
    // |L| > 1500 kg m^2/s while airborne: a car tumbling or spinning in the
    // air at roughly one radian per second or more (it depends on the
    // car's inertia). UpdateTrack then keeps the camera's offset instead
    // of following the car round.
    m_spinning = (spin2 > 2250000.0f && !m_isOnGround) ? 1 : 0;
}

void TrackCamera::updateHill(const CameraTarget& t) {
    // camTrackCS::UpdateHill: pitch the camera with the slope under the car.
    // The ground normal (front and rear axle averaged) is low-pass filtered
    // by HillLerp per update; its tilt along the car's heading, up to 45
    // degrees, maps through a quarter cosine to [-HillMax, -HillMin].
    const Vec3 front = axleNormal(t.wheels[0], t.wheels[1]);
    const Vec3 rear = axleNormal(t.wheels[2], t.wheels[3]);
    Vec3 n;
    if (mag2ZYX(front) > 0.01f)
        n = mag2ZYX(rear) > 0.01f ? Vec3{(front.x + rear.x) * 0.5f, (rear.y + front.y) * 0.5f, (rear.z + front.z) * 0.5f}
                                  : front;
    else
        n = mag2ZYX(rear) > 0.01f ? rear : kY;

    Vec3& g = m_groundNormal;
    if (m_oneShot == 0) {
        const float k = m_params.hillLerp;
        g = {(n.x - g.x) * k + g.x, (n.y - g.y) * k + g.y, (n.z - g.z) * k + g.z};
    } else {
        g = n;
    }
    g = cam::scaled(g, cam::invMag(g));
    const float tilt = cam::angle(kY, g);
    Vec3 right = t.matrix.m0;
    const float rr = (right.y * right.y + right.z * right.z) + right.x * right.x;
    right = cam::scaled(right, rr == 0.0f ? 0.0f : 1.0f / std::sqrt(rr));

    // Signed slope in [-1, 1]: > 0 nose up.
    float slope;
    if (tilt < 0.01f) {
        slope = !(tilt > 0.0f) ? 0.0f : (tilt < cam::kQuarterPi ? tilt * 1.27323949f : 1.0f);
    } else {
        // The horizontal axis the ground is tilted about, against the car's
        // right axis: the part of the tilt along the heading.
        Vec3 axis = yCross(g);
        const float aa = mag2ZYX(axis);
        axis = cam::scaled(axis, aa == 0.0f ? 0.0f : 1.0f / std::sqrt(aa));
        const float f = !(tilt > 0.0f) ? 0.0f : (tilt < cam::kQuarterPi ? tilt * 1.27323949f : 1.0f);
        slope = ((right.z * axis.z + right.y * axis.y) + axis.x * right.x) * f;
    }
    float h;
    if (slope > 0.0f) {
        const float u = slope < 0.5f ? slope + slope : 1.0f;
        h = (std::cos(u * cam::kHalfPi + cam::kPi) + 1.0f) + 1.0f;
    } else {
        const float s = -slope;
        const float u = !(s > 0.0f) ? 0.0f : (s < 0.5f ? s + s : 1.0f);
        h = std::sin((1.0f - u) * cam::kHalfPi);
    }
    h = h * 0.5f;
    if (h < 0.5f) {
        const float v = -m_params.hillMin * h;
        m_hill = -((v + v) + m_params.hillMin);
    } else {
        const float v = (h - 0.5f) * m_params.hillMax;
        m_hill = -(v + v);
    }
}

void TrackCamera::updateTrack(float dt, const CameraTarget& t) {
    // camTrackCS::UpdateTrack
    const Mat34& car = t.matrix;
    const Vec3& tt = m_params.app.trackTo;
    m_target = {((car.m1.x * tt.y + car.m2.x * tt.z) + tt.x * car.m0.x) + car.m3.x,
                ((car.m0.y * tt.x + car.m1.y * tt.y) + car.m2.y * tt.z) + car.m3.y,
                ((car.m0.z * tt.x + car.m1.z * tt.y) + car.m2.z * tt.z) + car.m3.z};
    const Vec3 delta{m_target.x - m_previousTarget.x, m_target.y - m_previousTarget.y,
                     m_target.z - m_previousTarget.z};
    m_previousTarget = m_target;

    const Vec3& o = m_params.offset;
    Vec3 position;
    if ((!m_isOnGround && (m_spinning != 0 || m_frozen)) || m_spinning == 2) {
        // Airborne or upside down: keep the last offset, just follow the car.
        m_spinning = 1;
        m_frozen = true;
        position = {delta.x + m_previousDesired.x, delta.y + m_previousDesired.y, delta.z + m_previousDesired.z};
    } else {
        m_frozen = false;
        Vec3 behind{car.m2.x, 0.0f, car.m2.z};
        behind = cam::scaled(behind, cam::invMag(behind));
        Vec3 side{behind.z * kY.y - behind.y * kY.z, behind.x * kY.z - behind.z * kY.x,
                  behind.y * kY.x - behind.x * kY.y};
        side = cam::scaled(side, cam::invMag(side));
        const float distance = o.z > 0.01f ? o.z : 0.01f;
        const Vec3 back = cam::scaled(behind, distance);
        const Vec3 sideways{o.x * side.x, side.y * o.x, side.z * o.x};
        const Vec3 beside{sideways.x + m_target.x, sideways.y + m_target.y, sideways.z + m_target.z};
        const Vec3 p{back.x + beside.x, back.y + beside.y, back.z + beside.z};
        position = {p.x, o.y + p.y, p.z};
    }

    m_target.y = m_target.y + 0.4f;
    m_desiredPosition = position;
    m_previousDesired = position;
    m_params.app.lookAbove = (o.y - 0.8f) * m_params.vertOffset;
    cam::lookAt(m_goal, m_desiredPosition, m_target);

    const float approachAmount = m_shared.approachAmount;
    float yRot = m_shared.yRot;
    float xRot;
    // Pitch with the slope; in front of the car the slope is seen the other
    // way round, so the hill pitch flips sign as the camera swings round.
    auto hillPitch = [&] {
        const float a = std::abs(yRot);
        const float round = !(a > 0.0f) ? 0.0f : (a < cam::kPi ? a * cam::kInvPi : 1.0f);
        return ((-m_hill - m_hill) * round + m_hill) + m_shared.xRot;
    };
    if (m_params.reverseOn == 1) {
        const float rate = m_reverseView ? m_params.revOnApp : m_params.revOffApp;
        const float goal = m_reverseView ? m_reverseSign * cam::kPi : 0.0f;
        if (yRot < goal) {
            yRot = rate * dt + yRot;
            if (yRot > goal)
                yRot = goal;
        } else if (yRot > goal) {
            yRot = yRot - rate * dt;
            if (yRot < goal)
                yRot = goal;
        }
        xRot = hillPitch();
        m_shared.yRot = yRot;
    } else if (m_params.reverseOn == -1) {
        yRot = m_reverseView ? cam::kPi : 0.0f;
        xRot = hillPitch();
        m_shared.yRot = yRot;
    } else {
        xRot = m_shared.xRot + m_hill;
    }

    if (!(approachAmount == 0.0f && yRot == 0.0f && xRot == 0.0f)) {
        // Swing the goal about the target point.
        m_goal.m3 = {m_goal.m3.x - m_target.x, m_goal.m3.y - m_target.y, m_goal.m3.z - m_target.z};
        if (xRot != 0.0f)
            cam::rotateFull(m_goal, yCross(m_goal.m2), -xRot);
        if (yRot != 0.0f)
            cam::rotateFull(m_goal, kY, yRot);
        m_goal.m3 = {m_goal.m3.x + m_target.x, m_target.y + m_goal.m3.y, m_target.z + m_goal.m3.z};
        if (approachAmount != 0.0f) {
            const Vec3 d{m_goal.m3.x - m_target.x, m_goal.m3.y - m_target.y, m_goal.m3.z - m_target.z};
            m_goal.m3 = {d.x * approachAmount + m_goal.m3.x, d.y * approachAmount + m_goal.m3.y,
                         d.z * approachAmount + m_goal.m3.z};
        }
    }

    const Vec3 lookPoint{m_target.x, m_params.app.lookAbove + m_target.y, m_target.z};
    const Vec3 from = m_goal.m3;
    cam::lookAt(m_goal, from, lookPoint);
}

void TrackCamera::preApproach(float dt, const CameraTarget& t) {
    // camTrackCS::PreApproach: AppXZPos moves from MaxAppXZPos (slow) to
    // MinAppXZPos (fast) with speed, changing by at most AppInc / AppDec per
    // second. Reversing without the reverse view uses a fixed 20.
    const TrackCamParams& p = m_params;
    float xz = m_params.app.appXZPos;
    float goal = p.maxAppXZPos;
    if (p.minAppXZPos != 0.0f) {
        float k = 0.0f;
        if (p.minSpeed != p.maxSpeed && m_carSpeed > p.minSpeed)
            k = m_carSpeed < p.maxSpeed ? (m_carSpeed - p.minSpeed) / (p.maxSpeed - p.minSpeed) : 1.0f;
        goal = (p.minAppXZPos - goal) * k + goal;
    }
    if (p.reverseOn == 0 && t.reverseGear)
        goal = 20.0f;
    if (xz < goal) {
        xz = p.appInc * dt + xz;
        if (xz > goal)
            xz = goal;
    } else if (xz > goal) {
        xz = xz - p.appDec * dt;
        if (xz < goal)
            xz = goal;
    }
    m_params.app.appXZPos = xz;
}

void TrackCamera::minMax(const Mat34& previous, const CameraProbe& probe) {
    // camTrackCS::MinMax: keep the camera between the ground and any
    // overhang, probing vertically from last update's height.
    if (!probe)
        return;
    const Vec3 c = m_camera.m3;
    const float prevY = previous.m3.y;
    CameraHit hit;
    bool ceilingHit = false, floorHit = false;
    float ceiling = 0.0f, floor = 0.0f;
    if (probe({c.x, prevY, c.z}, {c.x, prevY + 5.0f, c.z}, hit) && hit.normal.y < 0.7f) {
        ceiling = hit.point.y - 0.5f;
        ceilingHit = true;
    }
    const Vec3 from = ceilingHit ? Vec3{c.x, ceiling, c.z} : Vec3{c.x, prevY, c.z};
    if (probe(from, {c.x, prevY - 5.0f, c.z}, hit) && hit.normal.y > 0.7f) {
        floor = hit.point.y + 0.5f;
        floorHit = true;
    }
    if (ceilingHit) {
        if (floorHit && ceiling < floor)
            return;
        if (ceiling < m_camera.m3.y)
            m_camera.m3.y = ceiling;
    }
    if (floorHit && m_camera.m3.y < floor)
        m_camera.m3.y = floor;
}

void TrackCamera::collide(float dt, Vec3 prev, const CameraProbe& probe, const CameraInput& input,
                          const CameraPerspective& view) {
    // camTrackCS::Collide
    if (!probe)
        return;
    Vec3& c = m_camera.m3;
    const Vec3 tgt = m_target;
    if (m_params.collideType == 1) {
        // Probe from the target along the view line from each corner of the
        // near plane (plus the margin); the camera comes in to where the
        // nearest wall facing it would cut the near plane.
        const float maxDist = m_params.app.maxDist;
        Vec3 dir{c.x - tgt.x, c.y - tgt.y, c.z - tgt.z};
        dir = cam::scaled(dir, cam::invMag(dir));
        const float tanY = std::tan(view.fov * 0.00872664633f); // gfxViewport::Perspective
        const float tanX = tanY * input.aspect;
        const float hw = tanX * view.nearPlane + 0.33f;
        const float hh = tanY * view.nearPlane + 0.33f;
        const Vec3& r = m_camera.m0;
        const Vec3& u = m_camera.m1;
        auto corner = [&](float sx, float sy) {
            return Vec3{sy * u.x + sx * r.x, sy * u.y + sx * r.y, sy * u.z + sx * r.z};
        };
        const Vec3 corners[4] = {corner(-hw, -hh), corner(hw, -hh), corner(-hw, hh), corner(hw, hh)};
        float best = 1.0e7f;
        for (const Vec3& k : corners) {
            const Vec3 from{k.x + tgt.x, k.y + tgt.y, k.z + tgt.z};
            const Vec3 to{dir.x * maxDist + from.x, dir.y * maxDist + from.y, dir.z * maxDist + from.z};
            CameraHit hit;
            if (probe(from, to, hit) && hit.fraction < best &&
                (hit.normal.x * dir.x + hit.normal.z * dir.z) + hit.normal.y * dir.y < -1.0e-5f)
                best = hit.fraction;
        }
        const float distance = (best * maxDist + view.nearPlane) - m_collideMargin;
        const Vec3 d{tgt.x - c.x, tgt.y - c.y, tgt.z - c.z};
        if (distance < std::sqrt(mag2ZYX(d)))
            c = {dir.x * distance + tgt.x, dir.y * distance + tgt.y, dir.z * distance + tgt.z};
        return;
    }
    if (m_params.collideType != 2)
        return;

    // CollideType 2 (not used by any MM2 camera file): if geometry more than
    // 2 m out is between the target and the camera, pull the camera in front
    // of it, then ease back out.
    Vec3 dir{c.x - tgt.x, c.y - tgt.y, c.z - tgt.z};
    {
        const float m2 = mag2ZYX(dir);
        dir = cam::scaled(dir, m2 == 0.0f ? 0.0f : 1.0f / std::sqrt(m2));
    }
    CameraHit hit;
    const Vec3 seg{c.x - tgt.x, c.y - tgt.y, c.z - tgt.z};
    const float length = std::sqrt((seg.x * seg.x + seg.y * seg.y) + seg.z * seg.z);
    if (probe(tgt, c, hit) && length * hit.fraction > 2.0f) {
        m_wasColliding = true;
        const Vec3 tc{tgt.x - c.x, tgt.y - c.y, tgt.z - c.z};
        m_collideBaseDist2 = mag2ZYX(tc);
        const Vec3 v{hit.point.x - dir.x, hit.point.y - dir.y, hit.point.z - dir.z};
        const float close2 = mag2ZYX({tgt.x - v.x, tgt.y - v.y, tgt.z - v.z});
        Vec3 pulled;
        float rate;
        if (close2 < 50.0f) {
            const float k = close2 * 0.02f;
            pulled = {hit.point.x - k * dir.x, hit.point.y - dir.y * k, hit.point.z - k * dir.z};
            rate = m_params.app.maxDist;
        } else {
            pulled = v;
            rate = m_params.app.minDist;
        }
        if (m_oneShot == 0) {
            cam::approach(prev, pulled, rate, dt * 15.0f);
            pulled = prev;
        }
        c = pulled;
        const float d2 = mag2ZYX({tgt.x - c.x, tgt.y - c.y, tgt.z - c.z});
        m_collideDist2 = d2;
        m_params.app.lookAbove = d2 / m_collideBaseDist2 * m_params.app.lookAbove;
        return;
    }
    if (!m_wasColliding)
        return;
    if (m_collideDist2 < m_collideBaseDist2 && m_oneShot == 0) {
        m_collideDist2 = dt * 30.0f + m_collideDist2;
        if (m_collideDist2 > m_collideBaseDist2)
            m_collideDist2 = m_collideBaseDist2;
        const float distance = std::sqrt(m_collideDist2);
        c = {distance * dir.x + tgt.x, dir.y * distance + tgt.y, dir.z * distance + tgt.z};
        return;
    }
    m_wasColliding = false;
}

} // namespace mm2::game
