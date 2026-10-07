#pragma once

// The view a player sees: the Angel engine's mmViewCS (current camera,
// switching) and TransitionCS (blending from one camera to another).
// Ported from Midtown Madness 1 (Open1560, GPL-3.0, Copyright (C) Brick).

#include "game/CamCar.h"

namespace mm2::game {

class CameraView {
public:
    // TransitionCS blend curves (mmViewCS::NewCam mode).
    enum class Blend : int {
        Linear = 0,
        EaseIn = 1,    // 1 - cos(t * pi/2)
        EaseOut = 2,   // sin(t * pi/2)
        EaseInOut = 3, // (1 - cos(t * pi)) / 2; used by "Change Camera"
    };

    // mmViewCS::SetCurrentCam: switch immediately.
    void setCurrent(CarCamera* camera);
    // mmViewCS::NewCam: blend to `camera` over `seconds`. While a blend is
    // running, the new camera is queued; asking for the camera being blended
    // from reverses the blend. Returns false when switching is locked.
    bool newCam(CarCamera* camera, Blend blend, float seconds);
    void setLocked(bool locked) { m_locked = locked; }

    // mmViewCS::Reset: snap the current camera to the car.
    void reset(const CameraTarget& target);
    // mmViewCS::Update (+ TransitionCS::Update while blending).
    void update(float dt, const CameraTarget& target, const CameraProbe& probe, const CameraInput& input);

    // The camera in control, or the one being blended to.
    CarCamera* current() const { return m_transitioning ? m_target : m_current; }
    bool inTransition() const { return m_transitioning; }

    // MM1 wide-angle mode (mmViewCS+0x88): horizontal FOV fixed at 1.74 rad.
    void setWideAngle(bool wide) { m_wide = wide; }
    bool wideAngle() const { return m_wide; }

    const Mat34& matrix() const { return m_matrix; }
    void apply(Camera& out) const;

private:
    struct Transition {
        CarCamera* from = nullptr; // 0x118
        CarCamera* to = nullptr;   // 0x11C
        CarCamera* next = nullptr; // 0x120
        float t = 0.0f;            // 0x124 blend position (after easing)
        float blendGoal = 1.0f;    // 0x128
        float remaining = 0.0f;    // 0x12C seconds left
        bool startPending = false; // StartTransition's to->Reset()/Update() (needs car input)
        Mat34 camera;
        float fov = 60.0f, nearPlane = 1.0f;
    };

    void newTransition(CarCamera* from, CarCamera* to);
    void startTransition();
    void startNextTransition();
    void reverseTransition();
    void updateTransition(float dt, const CameraTarget& t, const CameraProbe& probe, const CameraInput& input);
    void takeView(const CarCamera& camera);

    CarCamera* m_current = nullptr; // 0xA8 (when not blending)
    CarCamera* m_target = nullptr;  // 0xAC
    bool m_transitioning = false;   // 0xA8 == the TransitionCS
    Blend m_blend = Blend::EaseInOut; // 0x8C
    float m_blendTime = 0.0f;       // 0x90
    bool m_locked = false;          // 0xB8
    bool m_wide = false;            // 0x88
    Transition m_tr;

    Mat34 m_matrix;
    float m_fov = 60.0f, m_near = 1.0f, m_far = 1600.0f;
};

} // namespace mm2::game
