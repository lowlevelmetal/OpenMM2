// Parity checks for the camera fixes of the camera-props audit
// (docs/parity/camera-props.md), against MM2's camTrackCS::UpdateCar,
// mmPlayer::Update, mmPlayer::SetMPPostCam and camPolarCS.
#include "game/CamMath.h"
#include "game/CamPlayer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

using namespace mm2;
using namespace mm2::game;

namespace {

constexpr float kStep = 1.0f / 30.0f;

// A car facing -Z, moving forward at `speed`, with all wheels in the air
// or on the ground.
CameraTarget carAt(const Vec3& pos, float yaw, bool onGround, const Vec3& angularMomentum = {}) {
    CameraTarget t;
    t.matrix = Mat34::rotationY(yaw);
    t.matrix.m3 = pos;
    t.angularMomentum = angularMomentum;
    t.speed = 10.0f;
    for (auto& w : t.wheels)
        w.onGround = onGround;
    return t;
}

TrackCamParams snapping() {
    TrackCamParams p;
    p.app.approachOn = 0; // the camera is its goal: easy to inspect
    p.minMaxOn = 0;
    return p;
}

// Runs a chase camera over a car that is airborne for 0.5 s and turns 90
// degrees meanwhile, with the given angular momentum; returns the yaw of the
// camera's offset from the car at the end, in radians from straight behind.
float yawBehindAfterAirborneTurn(const Vec3& angularMomentum) {
    TrackCamera cam(snapping());
    Vec3 pos{0.0f, 5.0f, 0.0f};
    float yaw = 0.0f;
    CameraTarget t = carAt(pos, yaw, true);
    cam.reset(t);
    for (int i = 0; i < 10; ++i)
        cam.update(kStep, carAt(pos, yaw, true), {}, {}, {});
    for (int i = 0; i < 15; ++i) {
        yaw += (cam::kHalfPi / 15.0f);
        cam.update(kStep, carAt(pos, yaw, false, angularMomentum), {}, {}, {});
    }
    const Vec3 d = cam.matrix().m3 - pos;
    // Straight behind a car at `yaw` is +Z rotated by yaw.
    const Vec3 behind = Mat34::rotationY(yaw).transformDir({0.0f, 0.0f, 1.0f});
    const float c = (d.x * behind.x + d.z * behind.z) / std::sqrt(d.x * d.x + d.z * d.z);
    return std::acos(std::clamp(c, -1.0f, 1.0f));
}

} // namespace

// camTrackCS::UpdateCar compares the angular momentum (vehCarSim +0x60),
// not the angular velocity, with 1500: a car spinning in the air with more
// than that leaves the camera where it was relative to the car's motion.
TEST(ParityCameraProps, ChaseCameraStopsTrackingASpinningAirborneCar) {
    // Below the threshold the camera swings round behind the turned car.
    EXPECT_LT(yawBehindAfterAirborneTurn({0.0f, 1400.0f, 0.0f}), 0.05f);
    // Above it (|L|^2 > 2.25e6) it keeps its offset once the car has been
    // off the ground for 0.1 s: still about 70 degrees off.
    EXPECT_GT(yawBehindAfterAirborneTurn({0.0f, 1600.0f, 0.0f}), 1.0f);
    // The same rate as an angular velocity (rad/s) never triggers it.
    EXPECT_LT(yawBehindAfterAirborneTurn({0.0f, 3.0f, 0.0f}), 0.05f);
}
