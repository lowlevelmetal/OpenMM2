// mmViewCS and TransitionCS.
// Ported from Open1560 (Midtown Madness 1 build 1560, code/midtown/game.asm),
// GPL-3.0, Copyright (C) Brick.
#include "game/CamView.h"

#include "game/CamMath.h"

#include <cmath>

namespace mm2::game {

void CameraView::takeView(const CarCamera& camera) {
    m_matrix = camera.matrix();
    m_fov = camera.base().cameraFov;
    m_near = camera.base().cameraNear;
    m_far = camera.base().cameraFar;
}

void CameraView::setCurrent(CarCamera* camera) {
    // mmViewCS::SetCurrentCam (MakeActive is reflected by CarCamera::display()).
    m_current = camera;
    m_transitioning = false;
    if (camera)
        takeView(*camera);
}

void CameraView::reset(const CameraTarget& target) {
    // mmViewCS::Reset
    m_tr = {};
    m_transitioning = false;
    if (m_current) {
        m_current->reset(target);
        takeView(*m_current);
    }
    m_target = m_current;
}

void CameraView::newTransition(CarCamera* from, CarCamera* to) {
    // TransitionCS::NewTransition
    m_tr.to = to;
    m_tr.from = from;
    m_transitioning = true;
    startTransition();
}

void CameraView::startTransition() {
    // TransitionCS::StartTransition
    if (!m_tr.to) {
        setCurrent(m_tr.from);
        return;
    }
    m_tr.t = 0.0f;
    m_tr.blendGoal = m_tr.from ? m_tr.from->base().blendGoal : 1.0f;
    m_tr.remaining = m_blendTime;
    m_tr.startPending = true; // to->Reset(); to->Update() with the next car input
}

void CameraView::startNextTransition() {
    // TransitionCS::StartNextTransition
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
    // TransitionCS::ReverseTransition
    CarCamera* oldFrom = m_tr.from;
    m_tr.blendGoal = oldFrom ? oldFrom->base().blendGoal : 1.0f;
    m_tr.remaining = m_blendTime * m_tr.t;
    m_tr.from = m_tr.to;
    m_tr.to = oldFrom;
    m_tr.t = 1.0f - m_tr.t;
}

bool CameraView::newCam(CarCamera* camera, Blend blend, float seconds) {
    // mmViewCS::NewCam
    m_blend = blend;
    m_blendTime = seconds;
    if (m_transitioning) {
        // TransitionCS::NextTransition
        m_tr.next = camera;
        if (camera == m_tr.from || camera == m_tr.to) {
            reverseTransition();
            m_tr.next = nullptr;
        }
        m_target = camera;
        return true;
    }
    if (m_locked)
        return false;
    newTransition(m_current, camera);
    m_target = camera;
    return true;
}

void CameraView::update(float dt, const CameraTarget& t, const CameraProbe& probe, const CameraInput& input) {
    // mmViewCS::Update
    if (m_transitioning) {
        updateTransition(dt, t, probe, input);
        return;
    }
    if (m_current) {
        m_current->update(dt, t, probe, input);
        takeView(*m_current);
    }
}

void CameraView::updateTransition(float dt, const CameraTarget& t, const CameraProbe& probe,
                                  const CameraInput& input) {
    // TransitionCS::Update
    if (m_tr.startPending) {
        m_tr.startPending = false;
        if (m_tr.to) {
            m_tr.to->reset(t);
            m_tr.to->update(dt, t, probe, input);
        }
    }
    if (!m_tr.from || !m_tr.to) {
        setCurrent(m_tr.from ? m_tr.from : m_tr.to);
        return;
    }
    m_tr.from->update(dt, t, probe, input);
    m_tr.to->update(dt, t, probe, input);

    m_tr.remaining = m_tr.remaining - dt;
    const float remaining = m_tr.remaining;
    if (!(remaining > 0.0f) && m_tr.blendGoal == 1.0f) {
        if (!m_tr.next) {
            setCurrent(m_tr.to);
        } else {
            m_blend = Blend::EaseOut;
            startNextTransition();
        }
        return; // the view keeps last update's blended matrix if still blending
    }

    if (m_tr.t < m_tr.blendGoal) {
        float fraction = 1.0f; // of the time still to go
        if (m_blendTime != 0.0f) {
            const float r = !(remaining > 0.0f) ? 0.0f : (remaining < m_blendTime ? remaining : m_blendTime);
            fraction = r / m_blendTime;
        }
        m_tr.t = 1.0f - m_tr.blendGoal * fraction;
        const BaseCamParams& a = m_tr.from->base();
        const BaseCamParams& b = m_tr.to->base();
        m_tr.fov = (b.cameraFov - a.cameraFov) * m_tr.t + a.cameraFov;
        m_tr.nearPlane = (b.cameraNear - a.cameraNear) * m_tr.t + a.cameraNear;
    }

    const float x = clampf(m_tr.t, 0.0f, 1.0f);
    switch (m_blend) {
    case Blend::EaseIn: m_tr.t = std::cos(cam::kPi - x * -cam::kPi * 0.5f) - -1.0f; break;
    case Blend::EaseOut: m_tr.t = 1.0f - (std::cos((x - -1.0f) * cam::kPi * 0.5f) - -1.0f); break;
    case Blend::EaseInOut: m_tr.t = (std::cos((x - -1.0f) * cam::kPi) - -1.0f) * 0.5f; break;
    case Blend::Linear: break;
    }
    const float w = m_tr.t;

    // Blend in polar form about the point each camera looks at, at the
    // distance of the car, so the view swings round instead of cutting
    // through the car.
    const Vec3 car = t.matrix.m3;
    Mat34 a = m_tr.from->matrix();
    Mat34 b = m_tr.to->matrix();
    const float da = cam::mag(a.m3 - car);
    const Vec3 ta{a.m3.x - da * a.m2.x, a.m3.y - da * a.m2.y, a.m3.z - da * a.m2.z};
    const float db = cam::mag(b.m3 - car);
    const Vec3 tb{b.m3.x - db * b.m2.x, b.m3.y - db * b.m2.y, b.m3.z - db * b.m2.z};
    a.m3 = {};
    b.m3 = {};
    const Vec3 ea = cam::getEulersZXY(a);
    Vec3 eb = cam::getEulersZXY(b);
    auto unwrap = [](float from, float& to) {
        if (from - to > cam::kPi)
            to = to - (-cam::kTwoPi);
        if (from - to < -cam::kPi)
            to = to - cam::kTwoPi;
    };
    unwrap(ea.x, eb.x);
    unwrap(ea.y, eb.y);
    unwrap(ea.z, eb.z);
    const Vec4 pa{da, ea.y, -ea.x, -ea.z};
    const Vec4 pb{db, eb.y, -eb.x, -eb.z};
    const float iw = 1.0f - w;
    const Vec4 p{pb.x * w + pa.x * iw, pb.y * w + pa.y * iw, pb.z * w + pa.z * iw, pb.w * w + pa.w * iw};
    const Vec3 look{(tb.x - ta.x) * w + ta.x, (tb.y - ta.y) * w + ta.y, (tb.z - ta.z) * w + ta.z};
    cam::polarView(m_tr.camera, p.x, p.y, p.z, p.w);
    m_tr.camera.m3 = {look.x + m_tr.camera.m3.x, look.y + m_tr.camera.m3.y, look.z + m_tr.camera.m3.z};

    m_matrix = m_tr.camera;
    m_fov = m_tr.fov;
    m_near = m_tr.nearPlane;
    m_far = m_tr.to->base().cameraFar;
}

void CameraView::apply(Camera& out) const {
    out.transform = m_matrix;
    out.horizontalFov = m_wide ? 1.74f : m_fov * cam::kDegToRad;
    out.nearPlane = m_near;
    out.farPlane = m_far;
}

} // namespace mm2::game
