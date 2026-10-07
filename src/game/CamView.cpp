// camViewCS and camTransitionCS, ported from Midtown Madness 2 (MM2Recomp,
// build 3393).
#include "game/CamView.h"

#include "game/CamMath.h"

#include <cmath>

namespace mm2::game {

void CameraView::setCurrent(CarCamera* camera) {
    // camViewCS::SetCam. The player's view does not set the perspective here
    // (camViewCS+0x48 is set by the mmPlayer constructor).
    m_current = camera;
    m_transitioning = false;
    m_target = nullptr;
    m_activate = camera;
    if (camera) {
        m_matrix = camera->matrix();
        m_far = camera->base().cameraFar;
    }
}

void CameraView::reset(const CameraTarget& target) {
    // camViewCS::Reset
    m_target = nullptr;
    if (m_transitioning) {
        m_persp = m_tr.persp;
        return;
    }
    if (m_current) {
        m_current->reset(target);
        m_persp = m_current->perspective();
        m_matrix = m_current->matrix();
    }
}

void CameraView::newTransition(CarCamera* from, CarCamera* to) {
    // camTransitionCS::NewTransition
    m_tr.to = to;
    m_tr.from = from;
    m_transitioning = true;
    m_target = nullptr;
    startTransition();
}

void CameraView::startTransition() {
    // camTransitionCS::StartTransition. Without a blend mode or time the
    // view goes back to the camera being left (no MM2 code asks for that).
    if (!m_tr.to || m_blend == Blend::None || m_blendTime == 0.0f) {
        m_tr.startPending = false;
        setCurrent(m_tr.from);
        return;
    }
    m_tr.t = 0.0f;
    m_tr.blendGoal = m_tr.from ? m_tr.from->base().blendGoal : 1.0f;
    m_tr.remaining = m_blendTime;
    m_tr.startPending = true; // to->Reset(); to->Update() with the next car input
}

void CameraView::startNextTransition() {
    // camTransitionCS::StartNextTransition
    if (m_tr.to == m_tr.next) {
        setCurrent(m_tr.to);
        return;
    }
    m_tr.from = m_tr.to;
    m_tr.to = m_tr.next;
    m_tr.next = nullptr;
    startTransition();
}

void CameraView::reverseTransition() {
    // camTransitionCS::ReverseTransition
    CarCamera* oldFrom = m_tr.from;
    m_tr.blendGoal = oldFrom ? oldFrom->base().blendGoal : 1.0f;
    m_tr.from = m_tr.to;
    m_tr.to = oldFrom;
    m_tr.remaining = m_blendTime * m_tr.t;
    m_tr.t = 1.0f - m_tr.t;
}

bool CameraView::newCam(CarCamera* camera, Blend blend, float seconds) {
    // camViewCS::NewCam
    m_blend = blend;
    m_blendTime = seconds;
    if (m_transitioning) {
        // camTransitionCS::NextTransition
        m_tr.next = camera;
        if (camera == m_tr.from || camera == m_tr.to) {
            reverseTransition();
            m_tr.next = nullptr;
        }
        m_activate = camera;
        m_target = camera;
        return true;
    }
    if (camera == m_current)
        return false;
    newTransition(m_current, camera);
    m_tr.activatePending = m_transitioning; // MakeActive after the first Reset / Update
    m_target = camera;
    return true;
}

void CameraView::updateCamera(CarCamera& camera, float dt, const CameraTarget& t, const CameraProbe& probe,
                              const CameraInput& input) {
    camera.update(dt, t, probe, input, m_persp);
    if (camera.drivesPerspective())
        m_persp = camera.perspective();
}

void CameraView::update(float dt, const CameraTarget& t, const CameraProbe& probe, const CameraInput& input) {
    // camViewCS::Update
    if (m_activate) {
        m_activate->makeActive(t);
        m_activate = nullptr;
    }
    if (m_transitioning) {
        updateTransition(dt, t, probe, input);
        return;
    }
    if (m_current) {
        updateCamera(*m_current, dt, t, probe, input);
        m_matrix = m_current->matrix();
        m_far = m_current->base().cameraFar;
    }
}

void CameraView::updateTransition(float dt, const CameraTarget& t, const CameraProbe& probe,
                                  const CameraInput& input) {
    // camTransitionCS::Update
    if (m_tr.startPending && m_tr.to) {
        m_tr.startPending = false;
        m_tr.to->reset(t);
        updateCamera(*m_tr.to, dt, t, probe, input);
        if (m_tr.activatePending)
            m_tr.to->makeActive(t);
        m_tr.activatePending = false;
    }
    if (!m_tr.from || !m_tr.to) {
        setCurrent(m_tr.from ? m_tr.from : m_tr.to);
        if (m_current)
            m_matrix = m_current->matrix();
        return;
    }
    updateCamera(*m_tr.from, dt, t, probe, input);
    updateCamera(*m_tr.to, dt, t, probe, input);

    m_tr.remaining = m_tr.remaining - dt;
    const float remaining = m_tr.remaining;
    if (!(remaining > 0.0f) && m_tr.blendGoal == 1.0f) {
        if (!m_tr.next) {
            setCurrent(m_tr.to);
        } else {
            m_blend = Blend::EaseOut;
            startNextTransition();
        }
        // A new blend shows last update's matrix until it has run once.
        if (!m_transitioning && m_current) {
            m_matrix = m_current->matrix();
            m_far = m_current->base().cameraFar;
        }
        return;
    }

    if (m_tr.t < m_tr.blendGoal) {
        float fraction; // of the time still to go
        if (!(remaining > 0.0f))
            fraction = 0.0f;
        else if (remaining < m_blendTime)
            fraction = remaining / m_blendTime;
        else
            fraction = 1.0f;
        m_tr.t = 1.0f - fraction * m_tr.blendGoal;
        const BaseCamParams& a = m_tr.from->base();
        const BaseCamParams& b = m_tr.to->base();
        m_tr.persp.fov = (b.cameraFov - a.cameraFov) * m_tr.t + a.cameraFov;
        m_tr.persp.nearPlane = (b.cameraNear - a.cameraNear) * m_tr.t + a.cameraNear;
        if (!m_wide)
            m_persp = m_tr.persp;
    }

    // The curve is applied to the stored position (again on every update
    // once a partial blend has reached BlendGoal).
    switch (m_blend) {
    case Blend::EaseIn: m_tr.t = std::cos(m_tr.t * cam::kHalfPi + cam::kPi) + 1.0f; break;
    case Blend::EaseOut: m_tr.t = std::sin(m_tr.t * cam::kHalfPi); break;
    case Blend::EaseInOut: m_tr.t = (std::cos((m_tr.t + 1.0f) * cam::kPi) + 1.0f) * 0.5f; break;
    case Blend::None: break;
    }
    const float w = m_tr.t;

    // Blend in polar form about the point each camera looks at, at the
    // distance of the car, so the view swings round instead of cutting
    // through the car.
    const Vec3 car = t.matrix.m3;
    const Mat34& a = m_tr.from->matrix();
    const Mat34& b = m_tr.to->matrix();
    auto dist = [&](const Vec3& p) {
        const Vec3 d{p.x - car.x, p.y - car.y, p.z - car.z};
        return std::sqrt((d.z * d.z + d.y * d.y) + d.x * d.x);
    };
    const float da = dist(a.m3);
    const Vec3 ta{a.m3.x - a.m2.x * da, a.m3.y - a.m2.y * da, a.m3.z - a.m2.z * da};
    const float db = dist(b.m3);
    const Vec3 tb{b.m3.x - b.m2.x * db, b.m3.y - b.m2.y * db, b.m3.z - b.m2.z * db};
    const Vec3 ea = cam::getEulersZXY(a);
    Vec3 eb = cam::getEulersZXY(b);
    auto unwrap = [](float from, float& to) {
        if (from - to > cam::kPi)
            to = to + cam::kTwoPi;
        if (from - to < -cam::kPi)
            to = to - cam::kTwoPi;
    };
    unwrap(ea.x, eb.x);
    unwrap(ea.y, eb.y);
    unwrap(ea.z, eb.z);
    const float distance = (db - da) * w + da;
    const float azimuth = (eb.y - ea.y) * w + ea.y;
    const float incline = (-eb.x - -ea.x) * w + -ea.x;
    const float twist = (eb.z - ea.z) * w + ea.z;
    const Vec3 look{(tb.x - ta.x) * w + ta.x, (tb.y - ta.y) * w + ta.y, (tb.z - ta.z) * w + ta.z};
    cam::polarView(m_tr.camera, distance, azimuth, incline, twist);
    m_tr.camera.m3 = {look.x + m_tr.camera.m3.x, look.y + m_tr.camera.m3.y, look.z + m_tr.camera.m3.z};

    m_matrix = m_tr.camera;
    m_far = m_tr.to->base().cameraFar;
}

void CameraView::apply(Camera& out) const {
    out.transform = m_matrix;
    out.horizontalFov = cam::horizontalFov4x3(m_persp.fov);
    out.nearPlane = m_persp.nearPlane;
    out.farPlane = m_far;
}

} // namespace mm2::game
