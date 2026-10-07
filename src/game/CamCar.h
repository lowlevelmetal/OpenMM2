#pragma once

// Car camera base, ported from the Angel engine's BaseCamCS / AppCamCS /
// CarCamCS (Midtown Madness 1; Open1560 code/midtown/mmcamcs and game.asm,
// GPL-3.0, Copyright (C) Brick). See docs/camera.md.
//
// Every car camera keeps two matrices: the goal (AGE matrix_) computed by
// the concrete camera each update, and the camera actually used for
// rendering (AGE camera_), which "approaches" the goal at the tuned rates.

#include "core/Math.h"
#include "game/Camera.h"
#include "game/CamParams.h"

#include <cstdint>
#include <functional>

namespace mm2::game {

// What the cameras read from the followed car each update.
struct CameraTarget {
    // Car frame in the world (AGE: mmCar+0x298, the simulation's matrix).
    // TrackTo / Offset are expressed in this frame; it faces -m2.
    Mat34 matrix;
    Vec3 velocity;        // world m/s (mmCarSim ICS velocity)
    Vec3 angularVelocity; // world rad/s
    float steering = 0.0f; // steering input, -1 (left) .. 1 (right) (mmCarSim::Steering)
    int wheelsOnGround = 4; // wheels touching the ground
    bool reverse = false;   // transmission in reverse

    // Point-of-view engine/road shake (PovCamCS::UpdatePOV). The original
    // reads these from mmCar at fixed offsets whose meaning is only partly
    // known; leaving them at zero disables the shake.
    bool onGround = true;
    float shakeWheelSpin = 0.0f; // mmCar+0xBA4: front-left wheel spin rate (rad/s), inferred
    float shakePhase = 0.0f;     // mmCar+0xB64: angle whose sine drives the vibration, inferred
    float shakeAmplitude = 0.0f; // mmCar+0x1904: meaning unknown
    bool shakeFlag = false;      // mmCar+0x2018 & 0x20: selects the 0.005 (set) or 0.008 scale
};

// Collision probe: nearest hit on the segment from -> to. Returns false when
// nothing is hit. (AGE: mmIntersection::InitSegment + mmPhysicsMGR::Collide.)
using CameraProbe = std::function<bool(const Vec3& from, const Vec3& to, Vec3& hitPoint, Vec3& hitNormal)>;

// Player view input.
struct CameraInput {
    // mmInput::GetCamPan: fraction of a full turn to look around, 0 .. 1.
    // Look left 0.25, back 0.5, right 0.75, forward 0; back+left 0.375,
    // back+right 0.625, forward+left 0.125, forward+right 0.875; an analog
    // "Camera Pan" axis may give any value. See cameraPanFor().
    float camPan = 0.0f;
};

// mmInput::GetCamPan for the digital look buttons.
float cameraPanFor(bool lookLeft, bool lookRight, bool lookBack, bool lookForward);

// How the car itself should be drawn while a camera is active
// (TrackCamCS / PovCamCS::MakeActive).
enum class CarDisplay : std::uint8_t {
    Body,   // chase cameras: the whole car
    Hidden, // point of view from the hood: no body
    Dash,   // dashboard view: dash model instead of the body
};

class CarCamera {
public:
    virtual ~CarCamera() = default;
    // Cameras point into their own parameter blocks; they are not copyable.
    CarCamera(const CarCamera&) = delete;
    CarCamera& operator=(const CarCamera&) = delete;

    // asNode::Reset: snap to the car on the next update.
    virtual void reset(const CameraTarget& target) = 0;
    // One simulation update (AGE: Update with Sim()->GetUpdateDelta() == dt).
    virtual void update(float dt, const CameraTarget& target, const CameraProbe& probe, const CameraInput& input) = 0;
    virtual CarDisplay display() const = 0;

    const Mat34& matrix() const { return m_camera; } // camera_: where the view is
    const Mat34& goal() const { return m_goal; }     // matrix_: where it wants to be
    const BaseCamParams& base() const { return *m_base; }
    const AppCamParams& app() const { return *m_app; }

    // BaseCamCS::ForceMatrixDelta: shift both matrices (teleports).
    void forceMatrixDelta(const Vec3& d);

    // Writes the view into `out` (FOV in radians for a 4:3 screen).
    void apply(Camera& out) const;

protected:
    CarCamera(BaseCamParams& base, AppCamParams& app) : m_base(&base), m_app(&app) {}

    // AppCamCS::ApproachIt: approach the goal, or copy it when approach is
    // off or on the first update after a reset (OneShot).
    void approachIt(float dt, const Mat34& car);

    // AppCamCS::DApproach (one scalar towards a goal; returns true when it
    // arrived this update).
    bool dApproach(float& value, float goal, float rampDist, float maxSpeed, float& velocity, float rateDt) const;

    BaseCamParams* m_base;
    AppCamParams* m_app;
    Mat34 m_camera;      // camera_
    Mat34 m_goal;        // matrix_
    int m_oneShot = 0;   // OneShot: 1 after a reset, 2 also clears the swing state
    Vec3 m_target;       // field_F0: TrackTo in world space
    Vec3 m_rotVelocity;  // field_FC: filtered rotation approach speeds (AppAppOn)
    Vec3 m_posVelocity;  // field_108: filtered position approach speeds

private:
    void updateApproach(float dt, const Mat34& car);
    void updateMaxDist();
};

} // namespace mm2::game
