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

// camPolarCS: constructor defaults and the keyboard orbit.
TEST(ParityCameraProps, PolarCameraDefaultsAndKeys) {
    PolarCamera cam;
    auto& p = cam.params();
    EXPECT_FLOAT_EQ(p.polarHeight, 2.5f);
    EXPECT_FLOAT_EQ(p.polarDistance, 10.0f);
    EXPECT_FLOAT_EQ(p.polarAzimuth, 2.5f);
    EXPECT_FLOAT_EQ(p.polarIncline, 0.25f);
    EXPECT_FLOAT_EQ(p.polarDelta, 2.0f);
    EXPECT_EQ(p.azimuthLock, 0);
    EXPECT_FLOAT_EQ(cam.base().cameraNear, 0.1f);

    const CameraTarget t = carAt({10.0f, 0.0f, -4.0f}, 0.3f, true);
    cam.update(0.5f, t, {}, {}, {});
    // The view sits PolarDistance from the car (plus PolarHeight) and looks
    // back at it along -m2.
    const Mat34& m = cam.matrix();
    const Vec3 rel = m.m3 - Vec3{10.0f, 2.5f, -4.0f};
    EXPECT_NEAR(rel.mag(), 10.0f, 1e-4f);
    EXPECT_NEAR(rel.normalized().dot(m.m2), 1.0f, 1e-5f);
    EXPECT_NEAR(std::asin(rel.y / 10.0f), 0.25f, 1e-5f);

    CameraInput in;
    in.orbit.azimuthUp = true;
    in.orbit.farther = true;
    in.orbit.inclineDown = true;
    cam.update(0.5f, t, {}, in, {});
    EXPECT_FLOAT_EQ(p.polarAzimuth, 2.5f + 0.5f * 2.0f * 0.3f);
    EXPECT_FLOAT_EQ(p.polarDistance, 10.0f + 2.0f);
    EXPECT_FLOAT_EQ(p.polarIncline, 0.25f - 0.3f);
    in.orbit.fast = true;
    cam.update(0.5f, t, {}, in, {});
    EXPECT_FLOAT_EQ(p.polarDistance, 12.0f + 5.0f);
    EXPECT_FLOAT_EQ(p.polarAzimuth, 2.8f + 1.0f);
    // Distance within 0.5 .. 200, incline within +-pi.
    CameraInput down;
    down.orbit.closer = true;
    down.orbit.inclineDown = true;
    down.orbit.fast = true;
    for (int i = 0; i < 100; ++i)
        cam.update(0.5f, t, {}, down, {});
    EXPECT_FLOAT_EQ(p.polarDistance, 0.5f);
    EXPECT_FLOAT_EQ(p.polarIncline, -cam::kPi);
}

// mmPlayer::SetMPPostCam: a 0.8 s blend to the polar camera orbiting the
// finish line, 21.5 m away and 0.34 rad up (15.5 m and level in rooms with
// flag 0x02 or 0x08); camera changes are then ignored.
TEST(ParityCameraProps, MultiplayerFinishCamera) {
    for (const int flags : {0, 0x08}) {
        PlayerCameras cams;
        CameraTarget t = carAt({}, 0.0f, true);
        t.roomFlags = flags;
        cams.reset(t);
        cams.update(kStep, t, {}, {});
        const Vec3 finish{50.0f, 1.0f, -80.0f};
        cams.startMultiplayerPostRace(finish, 1.0f);
        cams.update(kStep, t, {}, {});
        EXPECT_TRUE(cams.postRace());
        EXPECT_EQ(cams.viewManager().transitionTo(), &cams.polarCam());
        for (int i = 0; i < 30; ++i)
            cams.update(kStep, t, {}, {});
        ASSERT_EQ(cams.viewManager().current(), &cams.polarCam());
        const float distance = flags ? 15.5f : 21.5f;
        const float incline = flags ? 0.0f : 0.34f;
        const Vec3 rel = cams.polarCam().matrix().m3 - (finish + Vec3{0.0f, 2.5f, 0.0f});
        EXPECT_NEAR(rel.mag(), distance, 1e-3f);
        EXPECT_NEAR(std::asin(rel.y / distance), incline, 1e-4f);
        EXPECT_NEAR(std::atan2(rel.x, rel.z), 1.0f, 1e-4f);
        cams.toggleCamera();
        EXPECT_EQ(cams.viewManager().current(), &cams.polarCam());
        cams.reset(t);
        EXPECT_FALSE(cams.postRace());
    }
}

// mmPlayerConfig::SetViewSettings: the driver's camera, wide angle and
// dashboard carry over to the next race (mmPlayer::Reset).
TEST(ParityCameraProps, ViewSettingsCarryOver) {
    const CameraTarget t = carAt({}, 0.0f, true);
    {
        PlayerCameras cams;
        cams.setViewSettings({2, false, false});
        cams.reset(t);
        EXPECT_EQ(cams.view(), PlayerCameras::View::Far);
        EXPECT_EQ(cams.viewManager().current(), &cams.farCam());
        cams.toggleCamera(); // far -> near
        EXPECT_EQ(cams.viewSettings().camera, 0);
    }
    {
        PlayerCameras cams;
        cams.setViewSettings({0, false, true});
        cams.reset(t);
        EXPECT_EQ(cams.viewManager().current(), &cams.dashCam());
        EXPECT_EQ(cams.display(), CarDisplay::Dash);
        const auto s = cams.viewSettings();
        EXPECT_EQ(s.camera, 1); // the dashboard comes back on the hood index
        EXPECT_TRUE(s.dashboard);
    }
    {
        PlayerCameras cams;
        cams.setViewSettings({1, true, false});
        cams.reset(t);
        EXPECT_EQ(cams.viewManager().current(), &cams.povCam());
        EXPECT_TRUE(cams.wideAngle());
        EXPECT_TRUE(cams.viewManager().wideAngle()); // letterboxed
        // mmPlayer::Reset sets 70 degrees, but camViewCS::Reset (at its end
        // and again on the first update) sets the camera's own FOV: MM2
        // only shows 70 degrees again after a view setting changes.
        EXPECT_FLOAT_EQ(cams.viewManager().perspective().fov, cams.povCam().base().cameraFov);
    }
}
