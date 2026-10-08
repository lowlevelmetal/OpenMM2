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

// mmPlayer::Update: big vehicles also switch to the _ind camera in rooms
// with flag 0x20 when a segment from 100 m above the camera down to it hits
// something.
TEST(ParityCameraProps, BigVehicleIndCameraUnderGeometryInFlag20Rooms) {
    for (const bool roofed : {false, true}) {
        PlayerCameras cams;
        cams.setVehicleFlags(0x10);
        int vertical = 0;
        const CameraProbe probe = [&](const Vec3& from, const Vec3& to, CameraHit& hit) {
            if (from.x == to.x && from.z == to.z && std::abs(from.y - to.y - 100.0f) < 1e-3f) {
                ++vertical;
                if (roofed) {
                    hit.point = {to.x, to.y + 6.0f, to.z};
                    hit.normal = {0.0f, -1.0f, 0.0f};
                    hit.fraction = 0.94f;
                    return true;
                }
            }
            return false;
        };
        CameraTarget t = carAt({}, 0.0f, true);
        cams.reset(t);
        cams.update(kStep, t, probe, {});
        t.roomFlags = 0x20;
        cams.update(kStep, t, probe, {});
        EXPECT_EQ(vertical, 1);
        if (roofed)
            EXPECT_EQ(cams.viewManager().transitionTo(), &cams.indCam());
        else
            EXPECT_EQ(cams.viewManager().current(), &cams.nearCam());
    }
}
