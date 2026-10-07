#pragma once

// Car camera base, ported from Midtown Madness 2's camBaseCS / camAppCS /
// camCarCS (MM2Recomp, build 3393). See docs/camera.md.
//
// Every car camera keeps two matrices: the goal (AGE matrix_) computed by
// the concrete camera each update, and the camera actually used for
// rendering (AGE camera_), which "approaches" the goal at the tuned rates.

#include "core/Math.h"
#include "game/Camera.h"
#include "game/CamParams.h"

#include <array>
#include <cstdint>
#include <functional>

namespace mm2::game {

// What the cameras read from the followed car each update (vehCarSim).
struct CameraTarget {
    // The car's frame in the world (camCarCS::Init: the vehCarSim matrix).
    // TrackTo / Offset are expressed in this frame; it faces -m2.
    Mat34 matrix;
    Vec3 angularVelocity;   // world rad/s (only compared against a huge threshold)
    float speed = 0.0f;     // vehCarSim speed: |m2 . velocity| (m/s)
    float steering = 0.0f;  // steering input, -1 (left) .. 1 (right)
    float throttle = 0.0f;  // engine throttle input, 0 .. 1
    float handBrake = 0.0f; // hand brake input, 0 .. 1
    bool reverseGear = false; // transmission in reverse
    // Per wheel (front left, front right, rear left, rear right): touching
    // the ground, and the ground normal of the wheel's last probe hit
    // (vehWheel). The chase cameras' hill pitch follows these.
    struct Wheel {
        bool onGround = true;
        Vec3 normal{0.0f, 1.0f, 0.0f};
    };
    std::array<Wheel, 4> wheels;
    // Flags of the level room the car is in (lvlRoomInfo, mmPlayer::Update):
    // 0x08 widens the chase cameras' collision margin; 0x02 or 0x08 puts big
    // vehicles on the _ind camera. What the flags stand for in the city
    // data is not verified, so 0 (none) is a safe default.
    int roomFlags = 0;

    int wheelsOnGround() const;
};

// Collision probe: nearest hit on the segment from -> to. Returns false when
// nothing is hit; `fraction` is the hit's position along the segment, 0..1.
// (AGE: lvlSegment::Set + dgPhysManager::Collide.)
struct CameraHit {
    Vec3 point;
    Vec3 normal;
    float fraction = 1.0f;
};
using CameraProbe = std::function<bool(const Vec3& from, const Vec3& to, CameraHit& hit)>;

// Player view input and viewport.
struct CameraInput {
    // mmInput::GetCamPan: fraction of a full turn to look around, 0 .. 1.
    // Look left 0.25, back 0.5, right 0.75, forward 0; back+left 0.375,
    // back+right 0.625, forward+left 0.125, forward+right 0.875; an analog
    // "Camera Pan" axis may give any value. See cameraPanFor(). Only the
    // point-of-view cameras look around (mmGame::UpdateGameInput).
    float camPan = 0.0f;
    // Width / height of the 3D viewport. The chase cameras' wall collision
    // (camTrackCS::Collide) keeps the corners of the near plane out of
    // walls, and their spacing depends on it.
    float aspect = 4.0f / 3.0f;
};

// mmInput::GetCamPan for the digital look buttons.
float cameraPanFor(bool lookLeft, bool lookRight, bool lookBack, bool lookForward);

// How the car itself should be drawn while a camera is active.
enum class CarDisplay : std::uint8_t {
    Body,   // chase cameras: the whole car
    Hidden, // point of view: no body (mmPlayer::IsPOV)
    Dash,   // dashboard view: dash model instead of the body
};

// The perspective the view renders with (gfxViewport::Perspective).
struct CameraPerspective {
    float fov = 60.0f;  // vertical, degrees
    float nearPlane = 1.0f;
};

class CarCamera {
public:
    virtual ~CarCamera() = default;
    // Cameras point into their own parameter blocks; they are not copyable.
    CarCamera(const CarCamera&) = delete;
    CarCamera& operator=(const CarCamera&) = delete;

    // Reset: snap to the car on the next update.
    virtual void reset(const CameraTarget& target) = 0;
    // One update (AGE: Update with datTimeManager::Seconds == dt). The
    // perspective is that of the view, which the chase cameras' collision
    // uses for the size of the near plane.
    virtual void update(float dt, const CameraTarget& target, const CameraProbe& probe, const CameraInput& input,
                        const CameraPerspective& view) = 0;
    // MakeActive: the view switched to this camera.
    virtual void makeActive(const CameraTarget&) {}
    // Cameras that set the viewport's perspective themselves while they
    // update (camPointCS); the others leave it to the view.
    virtual bool drivesPerspective() const { return false; }
    virtual CarDisplay display() const { return CarDisplay::Body; }

    const Mat34& matrix() const { return m_camera; } // camera_: where the view is
    const Mat34& goal() const { return m_goal; }     // matrix_: where it wants to be
    const BaseCamParams& base() const { return *m_base; }
    const AppCamParams& app() const { return *m_app; }
    CameraPerspective perspective() const { return {m_base->cameraFov, m_base->cameraNear}; }

    // camBaseCS::ForceMatrixDelta: shift both matrices (teleports).
    void forceMatrixDelta(const Vec3& d);

    // Writes this camera's view into `out` (no transition).
    void apply(Camera& out) const;

protected:
    CarCamera(BaseCamParams& base, AppCamParams& app) : m_base(&base), m_app(&app) {}

    // camAppCS::ApproachIt: approach the goal, or copy it when approach is
    // off or on the first update after a reset (OneShot).
    void approachIt(float dt, const Mat34& car);

    // camAppCS::DApproach (one scalar towards a goal; returns true when it
    // got there).
    bool dApproach(float& value, float goal, float rampDist, float maxSpeed, float& velocity, float rateDt) const;

    // TrackTo in world space, in camAppCS::UpdateApproach's operation order.
    Vec3 trackToWorld(const Mat34& car) const;

    BaseCamParams* m_base;
    AppCamParams* m_app;
    Mat34 m_camera;      // camera_
    Mat34 m_goal;        // matrix_
    int m_oneShot = 0;   // OneShot: 1 after a reset
    Vec3 m_target;       // TrackTo in world space
    Vec3 m_rotVelocity;  // filtered rotation approach speeds (AppAppOn)
    Vec3 m_posVelocity;  // filtered position approach speeds

private:
    void updateApproach(float dt, const Mat34& car);
    void updateMaxDist();
};

} // namespace mm2::game
