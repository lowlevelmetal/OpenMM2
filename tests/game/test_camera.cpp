#include "TestData.h"
#include "data/DatFile.h"
#include "game/CamMath.h"
#include "game/CamPlayer.h"

#include <gtest/gtest.h>

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <optional>
#include <vector>

using namespace mm2;
using namespace mm2::game;

namespace {

// tune/camera/vpbug_near.camtrackcs (after camTrackCS::AfterLoad).
TrackCamParams bugNear() {
    TrackCamParams p;
    p.offset = {0.0f, 1.0f, 4.06f};
    p.collideType = 1;
    p.minMaxOn = 1;
    p.trackBreak = 0;
    p.minAppXZPos = 1.5f;
    p.maxAppXZPos = 8.0f;
    p.minSpeed = 0.0f;
    p.maxSpeed = 15.350011f;
    p.appInc = 2.5f;
    p.appDec = 10.0f;
    p.vertOffset = 1.0f;
    p.steerOn = 0;
    p.hillMin = -0.571f;
    p.hillMax = 0.429f;
    p.hillLerp = 0.093f;
    p.reverseOn = 1;
    p.revDelay = 2.0f;
    p.revOnApp = 2.0f;
    p.revOffApp = 4.0f;
    p.app.approachOn = 1;
    p.app.appAppOn = 1;
    p.app.appRot = 60.0f;
    p.app.appXRot = 2.0f;
    p.app.appYPos = 8.06f;
    p.app.appXZPos = 3.082831f;
    p.app.appApp = 0.7f;
    p.app.appRotMin = 0.01f;
    p.app.appPosMin = 0.25f;
    p.app.lookAbove = 0.2f;
    p.app.trackTo = {0.0f, 1.7f, 0.0f};
    p.app.maxDist = 5.3f;
    p.app.minDist = 3.95f;
    p.app.lookAt = 1.0f;
    p.base = {1.2f, 1.0f, 70.0f, 0.5f, 600.0f};
    return p;
}

// Simple kinematic car on flat ground, facing -Z at yaw 0.
struct Car {
    Vec3 pos;
    float yaw = 0.0f;
    float speed = 0.0f; // along forward (negative = backwards)
    float yawRate = 0.0f;
    float throttle = 0.0f;
    float handBrake = 0.0f;
    float steering = 0.0f;
    bool reverse = false;
    Vec3 groundNormal{0.0f, 1.0f, 0.0f};

    Vec3 forward() const { return Mat34::rotationY(yaw).transformDir({0, 0, -1}); }
    void step(float dt) {
        yaw += yawRate * dt;
        pos += forward() * (speed * dt);
    }
    CameraTarget target() const {
        CameraTarget t;
        t.matrix = Mat34::rotationY(yaw);
        t.matrix.m3 = pos;
        t.angularVelocity = {0, yawRate, 0};
        t.speed = std::abs(speed);
        t.steering = steering;
        t.throttle = throttle;
        t.handBrake = handBrake;
        t.reverseGear = reverse;
        for (auto& w : t.wheels)
            w.normal = groundNormal;
        return t;
    }
};

// Ground plane y = 0, optional wall planes x = wallX and z = wallZ.
struct World {
    std::optional<float> wallX, wallZ;
    CameraProbe probe() const {
        return [this](const Vec3& a, const Vec3& b, CameraHit& hit) {
            float best = 2.0f;
            auto plane = [&](float da, float db, const Vec3& n) {
                if ((da > 0) == (db > 0) || da == db)
                    return;
                const float s = da / (da - db);
                if (s > 0.0f && s < best) { // a segment starting on the plane does not hit it
                    best = s;
                    hit.point = a + (b - a) * s;
                    hit.normal = n;
                    hit.fraction = s;
                }
            };
            plane(a.y, b.y, {0, a.y > 0 ? 1.0f : -1.0f, 0});
            if (wallX)
                plane(a.x - *wallX, b.x - *wallX, {a.x < *wallX ? -1.0f : 1.0f, 0, 0});
            if (wallZ)
                plane(a.z - *wallZ, b.z - *wallZ, {0, 0, a.z < *wallZ ? -1.0f : 1.0f});
            return best <= 1.0f;
        };
    }
};

float viewDot(const Mat34& cam, const Vec3& point) {
    // > 0 when `point` is in front of the camera (cameras look down -m2).
    return (point - cam.m3).normalized().dot(-cam.m2);
}

bool finite(const Mat34& m) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 3; ++j)
            if (!std::isfinite(m.row(i)[j]))
                return false;
    return true;
}

bool matNear(const Mat34& a, const Mat34& b, float eps) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 3; ++j)
            if (std::abs(a.row(i)[j] - b.row(i)[j]) > eps)
                return false;
    return true;
}

constexpr float kDt = 1.0f / 30.0f;

// The point the near camera aims at: TrackTo (0, 1.7, 0) above the car.
Vec3 aim(const Car& car) { return car.pos + Vec3{0.0f, 1.7f, 0.0f}; }

void run(TrackCamera& cam, Car& car, const CameraProbe& probe, int updates, const CameraInput& input = {}) {
    for (int i = 0; i < updates; ++i) {
        car.step(kDt);
        cam.update(kDt, car.target(), probe, input, cam.perspective());
    }
}

// Exposes camAppCS::DApproach.
struct ApproachProbe final : CarCamera {
    BaseCamParams baseParams;
    AppCamParams appParams;
    ApproachProbe() : CarCamera(baseParams, appParams) {}
    void reset(const CameraTarget&) override {}
    void update(float, const CameraTarget&, const CameraProbe&, const CameraInput&, const CameraPerspective&) override {}
    using CarCamera::dApproach;
};

} // namespace

TEST(CamMath, EulersRoundTrip) {
    for (const Vec3 e : {Vec3{0.1f, 0.2f, 0.3f}, Vec3{-0.7f, 2.5f, -1.0f}, Vec3{1.2f, -3.0f, 0.0f}}) {
        Mat34 m;
        cam::fromEulersZXY(m, e);
        const Vec3 back = cam::getEulersZXY(m);
        EXPECT_NEAR(back.x, e.x, 1e-5f);
        EXPECT_NEAR(back.y, e.y, 1e-5f);
        EXPECT_NEAR(back.z, e.z, 1e-5f);
        // Rz * Rx * Ry in row-vector order.
        const Mat34 ref = Mat34::rotationZ(e.z) * Mat34::rotationX(e.x) * Mat34::rotationY(e.y);
        EXPECT_TRUE(matNear(m, ref, 1e-5f));
    }
}

TEST(CamMath, LookAt) {
    Mat34 m;
    cam::lookAt(m, {3, 4, 5}, {0, 1, -2});
    EXPECT_NEAR(viewDot(m, {0, 1, -2}), 1.0f, 1e-5f);
    EXPECT_NEAR(m.m0.dot(m.m1), 0.0f, 1e-5f);
    EXPECT_NEAR(m.m0.y, 0.0f, 1e-6f); // no roll
    EXPECT_GT(m.m1.y, 0.0f);
    // m1 = m2 x m0, not renormalised (unit anyway for unit m2, m0).
    EXPECT_NEAR(m.m1.mag(), 1.0f, 1e-5f);
    EXPECT_EQ(m.m3, (Vec3{3, 4, 5}));
}

TEST(CamMath, RotateMatchesMat34) {
    Mat34 m;
    cam::lookAt(m, {3, 4, 5}, {0, 1, -2});
    // Axis-aligned axes take MakeRotateX/Y/Z; a negative axis negates the angle.
    for (const auto& [axis, ref] : {std::pair{Vec3{0, 1, 0}, Mat34::rotationY(0.7f)},
                                    std::pair{Vec3{0, -2, 0}, Mat34::rotationY(-0.7f)},
                                    std::pair{Vec3{1, 0, 0}, Mat34::rotationX(0.7f)},
                                    std::pair{Vec3{0, 0, 3}, Mat34::rotationZ(0.7f)},
                                    std::pair{Vec3{1, 2, 3}, Mat34::rotationAxis(Vec3{1, 2, 3}.normalized(), 0.7f)}}) {
        Mat34 a = m;
        cam::rotateFull(a, axis, 0.7f);
        EXPECT_TRUE(matNear(a, m * ref, 1e-5f));
        Mat34 b = m;
        cam::rotate(b, axis, 0.7f);
        Mat34 c = m * ref;
        c.m3 = m.m3; // Rotate leaves the position
        EXPECT_TRUE(matNear(b, c, 1e-5f));
    }
    Mat34 d = m;
    cam::rotateFull(d, {1, 2, 3}, 0.0f);
    EXPECT_EQ(std::memcmp(&d, &m, sizeof(Mat34)), 0);
}

TEST(CamMath, PolarView) {
    // PolarView(distance, azimuth, incline, twist) = FromEulers(-incline,
    // azimuth, twist), position distance along m2.
    Mat34 p;
    cam::polarView(p, 6.0f, 0.4f, 0.3f, 0.1f);
    Mat34 rot;
    cam::fromEulersZXY(rot, {-0.3f, 0.4f, 0.1f});
    EXPECT_TRUE(matNear(Mat34{rot.m0, rot.m1, rot.m2, rot.m2 * 6.0f}, p, 1e-6f));
    // Incline lifts the camera above the origin, looking down at it.
    cam::polarView(p, 10.0f, 0.0f, 0.5f, 0.0f);
    EXPECT_NEAR(p.m3.y, 10.0f * std::sin(0.5f), 1e-4f);
    EXPECT_NEAR(p.m3.z, 10.0f * std::cos(0.5f), 1e-4f);
    EXPECT_NEAR(viewDot(p, {0, 0, 0}), 1.0f, 1e-5f);
}

TEST(CamMath, ApproachMovesOneAxisAtATime) {
    // Vector3::Approach: y only moves once x has arrived, z once y has.
    Vec3 v{0, 0, 0};
    const Vec3 goal{1, 1, 1};
    EXPECT_FALSE(cam::approach(v, goal, 2.0f, 0.25f));
    EXPECT_EQ(v, (Vec3{0.5f, 0, 0}));
    EXPECT_FALSE(cam::approach(v, goal, 2.0f, 0.25f));
    EXPECT_EQ(v, (Vec3{1, 0.5f, 0}));
    EXPECT_FALSE(cam::approach(v, goal, 2.0f, 0.25f));
    EXPECT_EQ(v, (Vec3{1, 1, 0.5f}));
    EXPECT_TRUE(cam::approach(v, goal, 2.0f, 0.25f));
    EXPECT_EQ(v, goal);
}

TEST(CamMath, AngleAndFov) {
    EXPECT_NEAR(cam::angle({0, 1, 0}, {1, 0, 0}), cam::kHalfPi, 1e-6f);
    EXPECT_EQ(cam::angle({0, 1, 0}, {0, 2, 0}), 0.0f);
    EXPECT_EQ(cam::angle({0, 1, 0}, {0, -1, 0}), cam::kPi);
    // CameraFOV is vertical: 70 degrees spans ~86 degrees across a 4:3 screen.
    EXPECT_NEAR(cam::horizontalFov4x3(70.0f), 2.0f * std::atan(std::tan(35.0f * kDegToRad) * 4.0f / 3.0f), 1e-6f);
    EXPECT_NEAR(cam::horizontalFov4x3(70.0f) * kRadToDeg, 86.07f, 0.01f);
}

TEST(Camera, PanMapping) {
    EXPECT_EQ(cameraPanFor(true, false, false, false), 0.25f);
    EXPECT_EQ(cameraPanFor(false, true, false, false), 0.75f);
    EXPECT_EQ(cameraPanFor(false, false, true, false), 0.5f);
    EXPECT_EQ(cameraPanFor(true, false, true, false), 0.375f);
    EXPECT_EQ(cameraPanFor(false, true, false, true), 0.875f);
    EXPECT_EQ(cameraPanFor(false, false, false, false), 0.0f);
}

TEST(CamParams, ConstructorDefaultsAndAfterLoad) {
    // camTrackCS::camTrackCS
    const TrackCamParams t;
    EXPECT_EQ(t.collideType, 0);
    EXPECT_EQ(t.reverseOn, 1);
    EXPECT_FLOAT_EQ(t.hillMin, -0.56f);
    EXPECT_FLOAT_EQ(t.hillMax, 0.56f);
    EXPECT_FLOAT_EQ(t.hillLerp, 0.05f);
    EXPECT_FLOAT_EQ(t.base.cameraFov, 60.0f);
    EXPECT_FLOAT_EQ(t.base.cameraNear, 1.0f);
    EXPECT_FLOAT_EQ(t.app.appXRot, 10.0f);
    EXPECT_FLOAT_EQ(t.app.appXZPos, 0.0f);
    // camPovCS::camPovCS (AppXRot keeps camAppCS's 0.5)
    const PovCamParams p;
    EXPECT_FLOAT_EQ(p.base.cameraNear, 3.0f);
    EXPECT_FLOAT_EQ(p.app.appXRot, 0.5f);
    EXPECT_EQ(p.reverseOffset, (Vec3{0.0f, 1.7f, 0.75f}));

    // AfterLoad forces the near plane.
    const auto dat = data::parseDat("type: a\ncamTrackCS {\n  CameraNear 2.0\n  Offset 1 2 3\n}\n");
    ASSERT_TRUE(dat && dat->top());
    TrackCamParams loaded;
    loaded.load(*dat->top());
    EXPECT_FLOAT_EQ(loaded.base.cameraNear, 0.5f);
    EXPECT_EQ(loaded.offset, (Vec3{1, 2, 3}));
    PovCamParams pov;
    pov.load(*dat->top());
    EXPECT_FLOAT_EQ(pov.base.cameraNear, 0.1f);
}

TEST(CarCamera, DApproach) {
    ApproachProbe c;
    c.appParams.appAppOn = 0;
    float v = 0.0f, vel = 0.0f;
    // Speed = distance; the step is added, then clamped to the goal.
    EXPECT_FALSE(c.dApproach(v, 10.0f, 0.0f, 0.0f, vel, 0.5f));
    EXPECT_FLOAT_EQ(v, 5.0f);
    EXPECT_FALSE(c.dApproach(v, 10.0f, 0.0f, 0.0f, vel, 0.5f));
    EXPECT_FLOAT_EQ(v, 7.5f);
    // Within rampDist the speed is d^2 / rampDist.
    v = 8.0f;
    EXPECT_FALSE(c.dApproach(v, 10.0f, 4.0f, 0.0f, vel, 0.5f));
    EXPECT_FLOAT_EQ(v, 8.5f);
    // A step past the goal ends on it.
    v = 9.0f;
    EXPECT_TRUE(c.dApproach(v, 10.0f, 0.0f, 0.0f, vel, 5.0f));
    EXPECT_FLOAT_EQ(v, 10.0f);
    // Downwards.
    v = 4.0f;
    EXPECT_FALSE(c.dApproach(v, 0.0f, 0.0f, 0.0f, vel, 0.25f));
    EXPECT_FLOAT_EQ(v, 3.0f);
}

TEST(TrackCamera, FollowsStraightSmoothlyWithinLimits) {
    TrackCamera cam(bugNear());
    World world;
    const auto probe = world.probe();
    Car car;
    car.speed = 20.0f;
    car.throttle = 1.0f;
    cam.reset(car.target());
    Vec3 last;
    for (int i = 0; i < 300; ++i) {
        run(cam, car, probe, 1);
        const Mat34& m = cam.matrix();
        ASSERT_TRUE(finite(m)) << i;
        const Vec3 target = car.target().matrix.transform(cam.params().app.trackTo);
        const float dist = (m.m3 - target).mag();
        EXPECT_LE(dist, cam.params().app.maxDist + 1e-3f) << i;
        EXPECT_GE(dist, cam.params().app.minDist - 1e-3f) << i;
        EXPECT_GE(m.m3.y, 0.5f - 1e-4f) << i; // MinMax floor
        if (i > 0) {
            EXPECT_LT((m.m3 - last).mag(), 20.0f * kDt * 2.0f + 0.05f) << i; // no jumps
        }
        last = m.m3;
    }
    const Mat34& m = cam.matrix();
    // Behind the car, looking at it.
    EXPECT_LT((m.m3 - car.pos).dot(car.forward()), -2.0f);
    EXPECT_GT(viewDot(m, aim(car)), 0.95f);
    // PreApproach: at full speed AppXZPos settled at MinAppXZPos.
    EXPECT_NEAR(cam.params().app.appXZPos, 1.5f, 1e-4f);
}

TEST(TrackCamera, PreApproachSpeedRange) {
    TrackCamera cam(bugNear());
    World world;
    const auto probe = world.probe();
    Car car;
    car.speed = 15.350011f * 0.5f; // halfway between MinSpeed 0 and MaxSpeed
    cam.reset(car.target());
    run(cam, car, probe, 600);
    EXPECT_NEAR(cam.params().app.appXZPos, (1.5f - 8.0f) * 0.5f + 8.0f, 1e-3f);
    // AppDec limits how fast it drops: 10 per second.
    car.speed = 30.0f;
    run(cam, car, probe, 1);
    EXPECT_NEAR(cam.params().app.appXZPos, 4.75f - 10.0f * kDt, 1e-4f);
}

TEST(TrackCamera, HardTurnLagsThenCatchesUp) {
    TrackCamera cam(bugNear());
    World world;
    const auto probe = world.probe();
    Car car;
    car.speed = 15.0f;
    cam.reset(car.target());
    run(cam, car, probe, 60);
    car.yawRate = kPi * 0.5f; // 90 degrees per second
    float minBehind = 1.0f;
    for (int i = 0; i < 30; ++i) {
        run(cam, car, probe, 1);
        ASSERT_TRUE(finite(cam.matrix()));
        const Vec3 rel = (cam.matrix().m3 - car.pos).normalized();
        minBehind = std::min(minBehind, -rel.dot(car.forward()));
        EXPECT_GT(viewDot(cam.matrix(), aim(car)), 0.7f) << i; // keeps the car in view
    }
    EXPECT_LT(minBehind, 0.95f); // lagged out of line during the turn
    car.yawRate = 0.0f;
    run(cam, car, probe, 90);
    const Vec3 rel = (cam.matrix().m3 - car.pos).normalized();
    EXPECT_GT(-rel.dot(car.forward()), 0.9f); // back behind
}

TEST(TrackCamera, ReverseSwingsToFrontAfterTwoSeconds) {
    TrackCamera cam(bugNear());
    World world;
    const auto probe = world.probe();
    Car car;
    cam.reset(car.target());
    run(cam, car, probe, 30);
    car.speed = -5.0f;
    car.reverse = true;
    // Without throttle the delay does not run.
    run(cam, car, probe, 90);
    EXPECT_FALSE(cam.reverseViewActive());
    car.throttle = 1.0f;
    run(cam, car, probe, 59); // 1.97 s
    EXPECT_FALSE(cam.reverseViewActive());
    EXPECT_EQ(cam.swingAngle(), 0.0f);
    run(cam, car, probe, 2); // 2 s (RevDelay is not read: always 2 s)
    EXPECT_TRUE(cam.reverseViewActive());
    // Swings at RevOnApp rad/s; steering <= 0.1 goes round to +pi.
    EXPECT_NEAR(cam.swingAngle(), 2.0f * kDt, 1e-5f);
    run(cam, car, probe, 10);
    EXPECT_NEAR(cam.swingAngle(), 11.0f * 2.0f * kDt, 1e-4f);
    run(cam, car, probe, 60);
    EXPECT_FLOAT_EQ(cam.swingAngle(), cam::kPi);
    run(cam, car, probe, 60);
    ASSERT_TRUE(finite(cam.matrix()));
    EXPECT_GT((cam.matrix().m3 - car.pos).dot(car.forward()), 2.0f); // camera in front
    EXPECT_GT(viewDot(cam.matrix(), aim(car)), 0.9f);

    // Out of reverse: carries on round (-pi) and back at RevOffApp rad/s.
    car.speed = 10.0f;
    car.reverse = false;
    run(cam, car, probe, 1);
    EXPECT_FALSE(cam.reverseViewActive());
    EXPECT_NEAR(cam.swingAngle(), -cam::kPi + 4.0f * kDt, 1e-5f);
    run(cam, car, probe, 30);
    EXPECT_EQ(cam.swingAngle(), 0.0f);
    run(cam, car, probe, 120);
    EXPECT_LT((cam.matrix().m3 - car.pos).dot(car.forward()), -2.0f);
}

TEST(TrackCamera, ReverseSteeringPicksTheSide) {
    TrackCamera cam(bugNear());
    World world;
    const auto probe = world.probe();
    Car car;
    car.reverse = true;
    car.throttle = 1.0f;
    car.steering = 0.5f;
    cam.reset(car.target());
    run(cam, car, probe, 61);
    EXPECT_TRUE(cam.reverseViewActive());
    EXPECT_LT(cam.swingAngle(), 0.0f);
}

TEST(TrackCamera, HandBrakeInReverseHoldsTheCamera) {
    TrackCamera cam(bugNear());
    World world;
    const auto probe = world.probe();
    Car car;
    cam.reset(car.target());
    run(cam, car, probe, 30);
    car.reverse = true;
    car.handBrake = 1.0f;
    run(cam, car, probe, 1);
    // UpdateCar zeroes AppXZPos; PreApproach then raises it by AppInc * dt.
    EXPECT_FLOAT_EQ(cam.params().app.appXZPos, 2.5f * kDt);
}

TEST(TrackCamera, HillFollowsTheGroundNormal) {
    // Expected camTrackCS::UpdateHill output for a steady slope.
    auto expected = [](const TrackCamParams& p, float slopeRad, bool uphill) {
        const float s = std::min(slopeRad / cam::kQuarterPi, 1.0f);
        const float u = std::min(2.0f * s, 1.0f);
        const float k = 1.0f - std::cos(u * cam::kHalfPi);
        return uphill ? -p.hillMax * k : -p.hillMin * k;
    };
    World world;
    const auto probe = world.probe();
    const float slope = 10.0f * kDegToRad;
    for (const bool uphill : {true, false}) {
        TrackCamera cam(bugNear());
        Car car;
        // Facing -Z; uphill the ground normal leans back towards +Z.
        car.groundNormal = {0.0f, std::cos(slope), (uphill ? 1.0f : -1.0f) * std::sin(slope)};
        cam.reset(car.target());
        run(cam, car, probe, 1);
        // OneShot: the filter starts on the slope.
        EXPECT_NEAR(cam.hillAngle(), expected(cam.params(), slope, uphill), 1e-4f);
        run(cam, car, probe, 200);
        EXPECT_NEAR(cam.hillAngle(), expected(cam.params(), slope, uphill), 1e-4f);
    }
    // Flat ground and no wheels on the ground: no pitch.
    TrackCamera flat(bugNear());
    Car car;
    flat.reset(car.target());
    run(flat, car, probe, 30);
    EXPECT_EQ(flat.hillAngle(), -0.0f);

    // Uphill the camera drops towards the slope behind the car.
    TrackCamera up(bugNear());
    Car c2;
    c2.groundNormal = {0.0f, std::cos(slope), std::sin(slope)};
    up.reset(c2.target());
    run(up, c2, probe, 120);
    EXPECT_LT(up.matrix().m3.y, flat.matrix().m3.y - 0.1f);
}

TEST(TrackCamera, HillLerpFiltersTheNormal) {
    TrackCamera cam(bugNear());
    World world;
    const auto probe = world.probe();
    Car car;
    cam.reset(car.target());
    run(cam, car, probe, 1);
    car.groundNormal = Vec3{0.0f, 1.0f, 1.0f}.normalized();
    run(cam, car, probe, 1);
    // One update moves the filtered normal HillLerp of the way (then renormalised).
    const Vec3 want = (Vec3{0, 1, 0} + (car.groundNormal - Vec3{0, 1, 0}) * 0.093f).normalized();
    EXPECT_NEAR((cam.groundNormal() - want).mag(), 0.0f, 1e-5f);
}

TEST(TrackCamera, ChaseCamerasDoNotLookAround) {
    // mmGame::UpdateGameInput applies CamPan to the point-of-view cameras only.
    World world;
    const auto probe = world.probe();
    TrackCamera a(bugNear()), b(bugNear());
    Car ca, cb;
    ca.speed = cb.speed = 10.0f;
    a.reset(ca.target());
    b.reset(cb.target());
    CameraInput back;
    back.camPan = 0.5f;
    run(a, ca, probe, 60);
    run(b, cb, probe, 60, back);
    EXPECT_EQ(std::memcmp(&a.matrix(), &b.matrix(), sizeof(Mat34)), 0);
}

TEST(TrackCamera, CollideType1KeepsTheNearPlaneOutOfWalls) {
    TrackCamParams p = bugNear();
    TrackCamera cam(p);
    World world;
    world.wallZ = 3.0f; // 3 m behind the car, facing it
    const auto probe = world.probe();
    Car car;
    cam.reset(car.target());
    run(cam, car, probe, 120);
    ASSERT_TRUE(finite(cam.matrix()));
    const Vec3 target = car.target().matrix.transform(p.app.trackTo);
    // From each corner of the near plane (widened by 0.33), a ray along the
    // view line; the camera ends the nearest wall hit plus CameraNear minus
    // the margin from the target.
    auto expected = [&](const Mat34& m, float margin) {
        const Vec3 dir = (m.m3 - target).normalized();
        const float tanY = std::tan(70.0f * 0.5f * kDegToRad);
        const float hw = tanY * (4.0f / 3.0f) * 0.5f + 0.33f, hh = tanY * 0.5f + 0.33f;
        float best = 1e9f;
        for (const float sx : {-hw, hw})
            for (const float sy : {-hh, hh}) {
                const Vec3 corner = m.m0 * sx + m.m1 * sy;
                best = std::min(best, (3.0f - (target.z + corner.z)) / dir.z);
            }
        return best + 0.5f - margin;
    };
    EXPECT_NEAR((cam.matrix().m3 - target).mag(), expected(cam.matrix(), 0.33f), 1e-3f);
    EXPECT_LT((cam.matrix().m3 - target).mag(), 3.4f);

    // A wider margin (rooms with flag 0x08) pulls it in further.
    TrackCamera cam2(p);
    cam2.setCollideMargin(1.11f);
    Car car2;
    cam2.reset(car2.target());
    run(cam2, car2, probe, 120);
    EXPECT_NEAR((cam2.matrix().m3 - target).mag(), expected(cam2.matrix(), 1.11f), 1e-3f);

    // No wall: nothing changes.
    TrackCamera noWall(p);
    Car car3;
    World open;
    const auto openProbe = open.probe();
    noWall.reset(car3.target());
    run(noWall, car3, openProbe, 120);
    EXPECT_GT((noWall.matrix().m3 - target).mag(), 3.9f);
}

TEST(TrackCamera, CollideType2PullsInFrontOfWalls) {
    TrackCamParams p = bugNear();
    p.collideType = 2;
    TrackCamera cam(p);
    World walled;
    walled.wallX = 3.0f;
    const auto wallProbe = walled.probe();
    Car side;
    side.yaw = kPi * 0.5f; // facing -X: "behind" is +X
    cam.reset(side.target());
    run(cam, side, wallProbe, 120);
    ASSERT_TRUE(finite(cam.matrix()));
    EXPECT_LT(cam.matrix().m3.x, 3.0f);
}

TEST(TrackCamera, Deterministic) {
    auto runOnce = [] {
        TrackCamera cam(bugNear());
        World world;
        world.wallX = 6.0f;
        const auto probe = world.probe();
        Car car;
        car.speed = 12.0f;
        cam.reset(car.target());
        std::vector<Mat34> out;
        for (int i = 0; i < 200; ++i) {
            car.yawRate = std::sin(i * 0.05f);
            car.groundNormal = Vec3{0.0f, 1.0f, 0.1f * std::sin(i * 0.1f)}.normalized();
            run(cam, car, probe, 1);
            out.push_back(cam.matrix());
        }
        return out;
    };
    const auto a = runOnce(), b = runOnce();
    ASSERT_EQ(a.size(), b.size());
    EXPECT_EQ(std::memcmp(a.data(), b.data(), a.size() * sizeof(Mat34)), 0);
}

TEST(PovCamera, EyeAndLookAround) {
    PovCamParams p;
    p.offset = {0.0f, 1.19f, -0.5519f};
    p.reverseOffset = {0.0f, 1.7f, 0.75f};
    p.app.approachOn = 0;
    PovCamera pov(p);
    Car car;
    car.pos = {10, 0, 20};
    car.yaw = 0.3f;
    const Mat34 t = car.target().matrix;
    pov.reset(car.target());
    pov.update(kDt, car.target(), {}, {}, pov.perspective());
    EXPECT_NEAR((pov.matrix().m3 - t.transform(p.offset)).mag(), 0.0f, 1e-4f);
    EXPECT_NEAR(pov.matrix().m2.dot(t.m2), 1.0f, 1e-5f); // looks where the car looks
    // Looking back turns the view about its up axis; the eye stays put
    // (ReverseOffset is only used by the reverse mode, which MM2 never sets).
    pov.setPan(0.5f * cam::kTwoPi);
    pov.update(kDt, car.target(), {}, {}, pov.perspective());
    EXPECT_NEAR(pov.matrix().m2.dot(t.m2), -1.0f, 1e-4f);
    EXPECT_NEAR((pov.matrix().m3 - t.transform(p.offset)).mag(), 0.0f, 1e-4f);
    pov.setPan(0.25f * cam::kTwoPi);
    pov.update(kDt, car.target(), {}, {}, pov.perspective());
    EXPECT_NEAR((-pov.matrix().m2).dot(-t.m0), 1.0f, 1e-4f); // looking left
    // Reverse mode: ReverseOffset, turned round.
    pov.setPan(0.0f);
    pov.setReverseMode(true);
    pov.update(kDt, car.target(), {}, {}, pov.perspective());
    EXPECT_NEAR((pov.matrix().m3 - t.transform(p.reverseOffset)).mag(), 0.0f, 1e-4f);
    EXPECT_NEAR(pov.matrix().m2.dot(t.m2), -1.0f, 1e-4f);
}

TEST(PovCamera, PitchAndNoShake) {
    PovCamParams p;
    p.pitch = 0.2f;
    p.app.approachOn = 0;
    PovCamera pov(p);
    Car car;
    car.speed = 30.0f;
    pov.reset(car.target());
    pov.update(kDt, car.target(), {}, {}, pov.perspective());
    const Mat34 first = pov.matrix();
    // Pitched about the car's right axis (positive pitch looks up).
    EXPECT_NEAR(-first.m2.y, std::sin(0.2f), 1e-5f);
    // camPovCS has no road or engine shake: the view is the car's frame.
    car.step(kDt);
    pov.update(kDt, car.target(), {}, {}, pov.perspective());
    EXPECT_TRUE(matNear(Mat34{first.m0, first.m1, first.m2, {}}, Mat34{pov.matrix().m0, pov.matrix().m1, pov.matrix().m2, {}},
                     1e-6f));
}

TEST(CameraView, BlendsBetweenCameras) {
    PlayerCameras cams;
    cams.nearCam().setParams(bugNear());
    TrackCamParams farP = bugNear();
    farP.offset = {0.0f, 1.8f, 6.0f};
    farP.app.maxDist = 0.0f;
    farP.base.cameraFov = 60.0f;
    cams.farCam().setParams(farP);
    World world;
    const auto probe = world.probe();
    Car car;
    car.speed = 10.0f;
    cams.reset(car.target());
    for (int i = 0; i < 60; ++i) {
        car.step(kDt);
        cams.update(kDt, car.target(), probe, {});
    }
    EXPECT_FLOAT_EQ(cams.viewManager().perspective().fov, 70.0f);
    cams.select(PlayerCameras::View::Far);
    EXPECT_TRUE(cams.viewManager().inTransition());
    Vec3 last = cams.viewManager().matrix().m3;
    float maxJump = 0.0f;
    for (int i = 0; i < 30; ++i) { // 1 s > 0.8 s blend
        car.step(kDt);
        cams.update(kDt, car.target(), probe, {});
        const Vec3 p = cams.viewManager().matrix().m3;
        maxJump = std::max(maxJump, (p - last).mag());
        last = p;
        const float fov = cams.viewManager().perspective().fov;
        EXPECT_GE(fov, 60.0f - 1e-4f);
        EXPECT_LE(fov, 70.0f + 1e-4f);
    }
    EXPECT_FALSE(cams.viewManager().inTransition());
    EXPECT_EQ(cams.view(), PlayerCameras::View::Far);
    EXPECT_LT(maxJump, 1.5f);
    // The perspective keeps the last blending update's value (a little
    // short of the far camera's 60 degrees at 30 updates per second).
    EXPECT_GE(cams.viewManager().perspective().fov, 60.0f);
    EXPECT_LT(cams.viewManager().perspective().fov, 60.5f);
    Camera view;
    cams.apply(view);
    EXPECT_EQ(view.transform.m3.x, cams.farCam().matrix().m3.x);
    EXPECT_NEAR(view.horizontalFov, cam::horizontalFov4x3(cams.viewManager().perspective().fov), 1e-6f);

    // Asking for the current camera does nothing.
    EXPECT_FALSE(cams.viewManager().newCam(&cams.farCam(), CameraView::Blend::EaseInOut, 0.8f));
}

TEST(CameraView, AskingForTheBlendsSourceReversesIt) {
    PlayerCameras cams;
    cams.nearCam().setParams(bugNear());
    World world;
    const auto probe = world.probe();
    Car car;
    cams.reset(car.target());
    for (int i = 0; i < 10; ++i)
        cams.update(kDt, car.target(), probe, {});
    auto& v = cams.viewManager();
    v.newCam(&cams.farCam(), CameraView::Blend::EaseInOut, 0.8f);
    for (int i = 0; i < 6; ++i)
        cams.update(kDt, car.target(), probe, {});
    ASSERT_TRUE(v.inTransition());
    v.newCam(&cams.nearCam(), CameraView::Blend::EaseInOut, 0.8f);
    EXPECT_EQ(v.transitionTo(), &cams.nearCam());
    EXPECT_EQ(v.transitionFrom(), &cams.farCam());
    for (int i = 0; i < 30; ++i)
        cams.update(kDt, car.target(), probe, {});
    EXPECT_EQ(v.current(), &cams.nearCam());
}

TEST(PlayerCameras, ChangeCameraCyclesNearPovFar) {
    PlayerCameras cams;
    World world;
    const auto probe = world.probe();
    Car car;
    cams.reset(car.target());
    cams.update(kDt, car.target(), probe, {});
    EXPECT_EQ(cams.view(), PlayerCameras::View::Near);
    cams.toggleCamera();
    EXPECT_EQ(cams.view(), PlayerCameras::View::Pov);
    EXPECT_EQ(cams.viewManager().transitionTo(), &cams.povCam());
    EXPECT_EQ(cams.display(), CarDisplay::Body); // the body shows while blending
    for (int i = 0; i < 30; ++i)
        cams.update(kDt, car.target(), probe, {});
    EXPECT_EQ(cams.display(), CarDisplay::Hidden);
    cams.toggleCamera();
    EXPECT_EQ(cams.view(), PlayerCameras::View::Far);
    cams.toggleCamera();
    EXPECT_EQ(cams.view(), PlayerCameras::View::Near);
}

TEST(PlayerCameras, PovLooksAroundWithCamPan) {
    PlayerCameras cams;
    World world;
    const auto probe = world.probe();
    Car car;
    cams.reset(car.target());
    cams.select(PlayerCameras::View::Pov);
    for (int i = 0; i < 30; ++i)
        cams.update(kDt, car.target(), probe, {});
    ASSERT_EQ(cams.viewManager().current(), &cams.povCam());
    CameraInput left;
    left.camPan = 0.25f;
    cams.update(kDt, car.target(), probe, left);
    EXPECT_FLOAT_EQ(cams.povCam().pan(), 0.25f * cam::kTwoPi);
    cams.update(kDt, car.target(), probe, {});
    EXPECT_EQ(cams.povCam().pan(), 0.0f);
}

TEST(PlayerCameras, DashboardCutsInAndBlendsOut) {
    PlayerCameras cams;
    World world;
    const auto probe = world.probe();
    Car car;
    cams.reset(car.target());
    cams.update(kDt, car.target(), probe, {});
    cams.toggleDashboard();
    EXPECT_TRUE(cams.dashboard());
    EXPECT_EQ(cams.viewManager().current(), &cams.dashCam()); // no blend
    EXPECT_EQ(cams.display(), CarDisplay::Dash);
    cams.update(kDt, car.target(), probe, {});
    // Off: 0.8 s back to the cycled camera.
    cams.toggleDashboard();
    EXPECT_FALSE(cams.dashboard());
    EXPECT_EQ(cams.viewManager().transitionTo(), &cams.nearCam());
    for (int i = 0; i < 30; ++i)
        cams.update(kDt, car.target(), probe, {});
    EXPECT_EQ(cams.viewManager().current(), &cams.nearCam());
    // "Change Camera" from the dashboard returns to the cycled camera
    // without advancing.
    cams.toggleDashboard();
    cams.toggleCamera();
    EXPECT_FALSE(cams.dashboard());
    EXPECT_EQ(cams.view(), PlayerCameras::View::Near);
    EXPECT_EQ(cams.viewManager().transitionTo(), &cams.nearCam());
}

TEST(PlayerCameras, DashboardAfterReset) {
    PlayerCameras cams;
    World world;
    const auto probe = world.probe();
    Car car;
    cams.reset(car.target());
    cams.toggleDashboard();
    cams.reset(car.target());
    EXPECT_EQ(cams.viewManager().current(), &cams.dashCam());
    EXPECT_EQ(cams.display(), CarDisplay::Dash);
    cams.update(kDt, car.target(), probe, {});
    // As in the original, the dashboard comes back on the point-of-view
    // index of the cycled cameras: switching it off leaves the view at the
    // dashboard eye (without the dash model) until the camera is changed.
    cams.toggleDashboard();
    EXPECT_FALSE(cams.dashboard());
    EXPECT_EQ(cams.viewManager().current(), &cams.dashCam());
    EXPECT_EQ(cams.display(), CarDisplay::Hidden);
    cams.toggleCamera();
    EXPECT_EQ(cams.view(), PlayerCameras::View::Far);
}

TEST(PlayerCameras, BigVehiclesUseTheIndCameraUnderCover) {
    PlayerCameras cams;
    cams.setVehicleFlags(16); // vpbus
    World world;
    const auto probe = world.probe();
    Car car;
    cams.reset(car.target());
    cams.update(kDt, car.target(), probe, {});
    CameraTarget covered = car.target();
    covered.roomFlags = 0x08;
    cams.update(kDt, covered, probe, {});
    EXPECT_EQ(cams.viewManager().transitionTo(), &cams.indCam());
    EXPECT_FLOAT_EQ(cams.nearCam().collideMargin(), 1.11f);
    for (int i = 0; i < 31; ++i)
        cams.update(kDt, covered, probe, {});
    EXPECT_EQ(cams.viewManager().current(), &cams.indCam());
    cams.update(kDt, car.target(), probe, {});
    EXPECT_EQ(cams.viewManager().transitionTo(), &cams.nearCam());
    EXPECT_FLOAT_EQ(cams.nearCam().collideMargin(), 0.33f);
}

TEST(Camera, RetailCameraFiles) {
    MM2_REQUIRE_GAME_DATA();
    int loaded = 0;
    for (const auto& e : test::gameData()->listFiles()) {
        if (!e.path.starts_with("tune/camera/"))
            continue;
        const std::string name = e.path.substr(12, e.path.rfind('.') - 12);
        std::string err;
        if (e.path.ends_with(".camtrackcs")) {
            auto p = loadTrackCamParams(*test::gameData(), name, &err);
            ASSERT_TRUE(p) << err;
            EXPECT_GT(p->base.cameraFov, 30.0f) << name;
            EXPECT_FLOAT_EQ(p->base.cameraNear, 0.5f) << name;
            EXPECT_EQ(p->collideType, 1) << name; // every MM2 chase camera
            ++loaded;
        } else if (e.path.ends_with(".campovcs")) {
            auto p = loadPovCamParams(*test::gameData(), name, &err);
            ASSERT_TRUE(p) << err;
            EXPECT_FLOAT_EQ(p->base.cameraNear, 0.1f);
            ++loaded;
        }
    }
    EXPECT_EQ(loaded, 96);

    PlayerCameras cams;
    std::vector<std::string> missing;
    cams.load(*test::gameData(), "vpbug", &missing);
    EXPECT_TRUE(missing.empty());
    EXPECT_FLOAT_EQ(cams.nearCam().params().offset.z, 4.06f);
    EXPECT_FLOAT_EQ(cams.farCam().params().offset.z, 6.0f);
    EXPECT_FLOAT_EQ(cams.nearCam().params().hillMax, 0.429f);
    EXPECT_EQ(cams.dashCam().params().reverseOffset, (Vec3{0.0f, 1.7f, 0.75f}));
    // A narrow screen moves the dashboard eye forward.
    PlayerCameras narrow;
    narrow.load(*test::gameData(), "vpbug", nullptr, 1.25f);
    EXPECT_FLOAT_EQ(narrow.dashCam().params().offset.z, cams.dashCam().params().offset.z * 0.7352941f);
    missing.clear();
    cams.load(*test::gameData(), "vpvwcup", &missing);
    ASSERT_EQ(missing.size(), 2u); // "vpvwcup__far" typo and no _ind
    EXPECT_EQ(missing[0], "vpvwcup_far");
    // The far camera keeps the constructor defaults: no collision.
    EXPECT_EQ(cams.farCam().params().collideType, 0);
    EXPECT_FLOAT_EQ(cams.farCam().params().base.cameraNear, 1.0f);
}

TEST(Camera, RetailCarsStayStableAndFramed) {
    MM2_REQUIRE_GAME_DATA();
    auto cars = test::gameData()->readAll("tune/cars.txt");
    ASSERT_TRUE(cars);
    std::string list(reinterpret_cast<const char*>(cars->data()), cars->size());
    int checked = 0;
    for (std::size_t pos = 0; pos < list.size();) {
        std::size_t end = list.find('\n', pos);
        if (end == std::string::npos)
            end = list.size();
        std::string line = list.substr(pos, end - pos);
        pos = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (line.size() < 6)
            continue;
        std::string car = line.substr(0, line.rfind('.'));
        for (auto& c : car)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        PlayerCameras cams;
        cams.load(*test::gameData(), car);
        World world;
        const auto probe = world.probe();
        for (auto view : {PlayerCameras::View::Near, PlayerCameras::View::Far, PlayerCameras::View::Pov,
                          PlayerCameras::View::Dash}) {
            CarCamera& cam = cams.camera(view);
            Car c;
            cam.reset(c.target());
            for (int i = 0; i < 600; ++i) {
                const float phase = i * kDt;
                c.speed = phase < 6.0f ? 25.0f : (phase < 8.0f ? 0.0f : -6.0f);
                c.throttle = c.speed != 0.0f ? 1.0f : 0.0f;
                c.reverse = phase >= 8.0f;
                c.yawRate = (phase > 2.0f && phase < 4.0f) ? 1.2f : 0.0f;
                const float hill = (phase > 4.5f && phase < 5.5f) ? 0.15f : 0.0f;
                c.groundNormal = Vec3{0.0f, 1.0f, hill}.normalized();
                c.step(kDt);
                cam.update(kDt, c.target(), probe, {}, cam.perspective());
                ASSERT_TRUE(finite(cam.matrix())) << car << " " << viewName(view) << " " << i;
                if (view == PlayerCameras::View::Near || view == PlayerCameras::View::Far) {
                    const Vec3 tgt = c.target().matrix.transform(cam.app().trackTo);
                    EXPECT_LT((cam.matrix().m3 - tgt).mag(), 40.0f) << car << " " << viewName(view) << " " << i;
                    // Not while swinging round for the reverse view.
                    const bool swinging = phase > 9.9f && phase < 12.5f;
                    if (i > 30 && !swinging) {
                        EXPECT_GT(viewDot(cam.matrix(), tgt), 0.5f) << car << " " << viewName(view) << " " << i;
                    }
                }
            }
        }
        ++checked;
    }
    EXPECT_EQ(checked, 20);
}

// Writes car and camera paths (top-down) to $OPENMM2_CAMERA_PLOT/<view>.csv
// for visual checks; skipped otherwise.
TEST(Camera, PlotPaths) {
    const char* dir = std::getenv("OPENMM2_CAMERA_PLOT");
    if (!dir)
        GTEST_SKIP() << "set OPENMM2_CAMERA_PLOT to a directory";
    PlayerCameras cams;
    if (test::gameData())
        cams.load(*test::gameData(), "vpbug");
    else
        cams.nearCam().setParams(bugNear());
    World world;
    const auto probe = world.probe();
    for (auto view : {PlayerCameras::View::Near, PlayerCameras::View::Far}) {
        CarCamera& cam = cams.camera(view);
        Car c;
        cam.reset(c.target());
        std::string csv = "t,carx,carz,camx,camz,fwdx,fwdz\n";
        for (int i = 0; i < 540; ++i) {
            const float phase = i * kDt;
            c.speed = phase < 9.0f ? 18.0f : (phase < 10.0f ? 0.0f : -5.0f);
            c.throttle = c.speed != 0.0f ? 1.0f : 0.0f;
            c.reverse = phase >= 10.0f;
            c.yawRate = (phase > 2.0f && phase < 3.5f) ? 1.05f : ((phase > 5.0f && phase < 6.5f) ? -1.05f : 0.0f);
            c.step(kDt);
            cam.update(kDt, c.target(), probe, {}, cam.perspective());
            const Mat34& m = cam.matrix();
            csv += std::format("{},{},{},{},{},{},{}\n", phase, c.pos.x, c.pos.z, m.m3.x, m.m3.z, -m.m2.x, -m.m2.z);
        }
        FILE* f = std::fopen((std::string(dir) + "/" + viewName(view) + ".csv").c_str(), "wb");
        ASSERT_TRUE(f);
        std::fwrite(csv.data(), 1, csv.size(), f);
        std::fclose(f);
    }
}
