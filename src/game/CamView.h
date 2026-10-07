#pragma once

// The view a player sees: Midtown Madness 2's camViewCS (current camera,
// switching) and camTransitionCS (blending from one camera to another)
// (MM2Recomp, build 3393).

#include "game/CamCar.h"

namespace mm2::game {

class CameraView {
public:
    // camViewCS::NewCam modes: the blend curve of a transition. Mode 0 does
    // not blend at all (camTransitionCS::StartTransition goes back to the
    // camera being left).
    enum class Blend : int {
        None = 0,
        EaseIn = 1,    // 1 - cos(t * pi/2)
        EaseOut = 2,   // sin(t * pi/2)
        EaseInOut = 3, // (1 - cos(t * pi)) / 2; used by "Change Camera"
    };

    // camViewCS::SetCam: switch immediately (abandons a running blend).
    void setCurrent(CarCamera* camera);
    // camViewCS::NewCam: blend to `camera` over `seconds`. While a blend is
    // running the camera is queued; asking for either camera of the running
    // blend reverses it. Returns false when `camera` is already current.
    bool newCam(CarCamera* camera, Blend blend, float seconds);

    // camViewCS::Reset: snap the current camera to the car and take its
    // perspective.
    void reset(const CameraTarget& target);
    // camViewCS::Update (+ camTransitionCS::Update while blending).
    void update(float dt, const CameraTarget& target, const CameraProbe& probe, const CameraInput& input);

    // The camera the view shows; nullptr while a blend runs.
    CarCamera* current() const { return m_transitioning ? nullptr : m_current; }
    // The camera most recently asked for with newCam (camViewCS+0x34).
    CarCamera* target() const { return m_target; }
    bool inTransition() const { return m_transitioning; }
    CarCamera* transitionFrom() const { return m_transitioning ? m_tr.from : nullptr; }
    CarCamera* transitionTo() const { return m_transitioning ? m_tr.to : nullptr; }

    // The perspective the view renders with. Like gfxViewport::Perspective
    // in the original, it is set when the player changes the view
    // (mmPlayer::SetWideFOV), on reset, by every blending update of a
    // transition and by cameras that zoom (camPointCS); otherwise it keeps
    // its last value.
    const CameraPerspective& perspective() const { return m_persp; }
    void setPerspective(const CameraPerspective& p) { m_persp = p; }

    // Wide-angle mode (camViewCS+0x18, mmPlayer::SetWideFOV): the player's
    // view is letterboxed to 66% of the screen height and the perspective is
    // fixed at 70 degrees; transitions stop changing it.
    void setWideAngle(bool wide) { m_wide = wide; }
    bool wideAngle() const { return m_wide; }

    const Mat34& matrix() const { return m_matrix; }
    void apply(Camera& out) const;

private:
    struct Transition {
        CarCamera* from = nullptr; // 0x110
        CarCamera* to = nullptr;   // 0x114
        CarCamera* next = nullptr; // 0x118
        float t = 0.0f;            // 0x11C blend position (after easing)
        float blendGoal = 1.0f;    // 0x120
        float remaining = 0.0f;    // 0x124 seconds left
        bool startPending = false; // StartTransition's to->Reset() / Update() (needs car input)
        bool activatePending = false;
        Mat34 camera;              // camera_
        CameraPerspective persp;   // its own CameraFOV / CameraNear
    };

    void newTransition(CarCamera* from, CarCamera* to);
    void startTransition();
    void startNextTransition();
    void reverseTransition();
    void updateTransition(float dt, const CameraTarget& t, const CameraProbe& probe, const CameraInput& input);
    void updateCamera(CarCamera& camera, float dt, const CameraTarget& t, const CameraProbe& probe,
                      const CameraInput& input);

    CarCamera* m_current = nullptr; // 0x30 (when not blending)
    CarCamera* m_target = nullptr;  // 0x34
    CarCamera* m_activate = nullptr; // MakeActive still to run (needs car input)
    bool m_transitioning = false;   // 0x30 == the camTransitionCS
    Blend m_blend = Blend::EaseInOut; // 0x1C
    float m_blendTime = 0.0f;       // 0x20
    bool m_wide = false;            // 0x18
    Transition m_tr;

    Mat34 m_matrix;
    CameraPerspective m_persp;
    float m_far = 1600.0f;
};

} // namespace mm2::game
