#include "TestData.h"
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

// tune/camera/vpbug_near.camtrackcs
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
    bool reverse = false;

    Vec3 forward() const { return Mat34::rotationY(yaw).transformDir({0, 0, -1}); }
    void step(float dt) {
        yaw += yawRate * dt;
        pos += forward() * (speed * dt);
    }
    CameraTarget target() const {
        CameraTarget t;
        t.matrix = Mat34::rotationY(yaw);
        t.matrix.m3 = pos;
        t.velocity = forward() * speed;
        t.angularVelocity = {0, yawRate, 0};
        t.reverse = reverse;
        return t;
    }
};

// Ground plane y = 0, optional wall plane x = wallX (normal -X).
struct World {
    std::optional<float> wallX;
    CameraProbe probe() const {
        return [this](const Vec3& a, const Vec3& b, Vec3& hit, Vec3& normal) {
            float best = 2.0f;
            auto plane = [&](float da, float db, const Vec3& n) {
                if ((da > 0) == (db > 0) || da == db)
                    return;
                const float s = da / (da - db);
                if (s > 0.0f && s < best) { // a segment starting on the plane does not hit it
                    best = s;
                    hit = a + (b - a) * s;
                    normal = n;
                }
            };
            plane(a.y, b.y, {0, a.y > 0 ? 1.0f : -1.0f, 0});
            if (wallX)
                plane(a.x - *wallX, b.x - *wallX, {a.x < *wallX ? -1.0f : 1.0f, 0, 0});
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

constexpr float kDt = 1.0f / 30.0f;

// The point the near camera aims at: TrackTo (0, 1.7, 0) above the car.
Vec3 aim(const Car& car) { return car.pos + Vec3{0.0f, 1.7f, 0.0f}; }

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
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                EXPECT_NEAR(m.row(i)[j], ref.row(i)[j], 1e-5f);
    }
}

TEST(CamMath, LookAtAndRotations) {
    Mat34 m;
    cam::lookAt(m, {3, 4, 5}, {0, 1, -2});
    EXPECT_NEAR(viewDot(m, {0, 1, -2}), 1.0f, 1e-5f);
    EXPECT_NEAR(m.m0.dot(m.m1), 0.0f, 1e-5f);
    EXPECT_NEAR(m.m0.y, 0.0f, 1e-6f); // no roll
    EXPECT_GT(m.m1.y, 0.0f);

    Mat34 a = m, b = m * Mat34::rotationY(0.7f);
    cam::rotateFullY(a, 0.7f);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 3; ++j)
            EXPECT_NEAR(a.row(i)[j], b.row(i)[j], 1e-5f);

    Mat34 c = m, d = m * Mat34::rotationAxis(Vec3{1, 2, 3}.normalized(), -0.4f);
    cam::rotateAxis(c, {1, 2, 3}, -0.4f, true);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 3; ++j)
            EXPECT_NEAR(c.row(i)[j], d.row(i)[j], 1e-5f);

    // PolarView(d, e.y, -e.x, -e.z) rebuilds a camera d back from its target.
    Mat34 rot;
    const Vec3 e{0.3f, -1.1f, 0.05f};
    cam::fromEulersZXY(rot, e);
    Mat34 p;
    cam::polarView(p, 6.0f, e.y, -e.x, -e.z);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            EXPECT_NEAR(p.row(i)[j], rot.row(i)[j], 1e-5f);
    EXPECT_NEAR((p.m3 - rot.m2 * 6.0f).mag(), 0.0f, 1e-4f);
}

TEST(CamMath, Approach) {
    Vec3 v{0, 0, 0};
    EXPECT_FALSE(cam::approach(v, {10, 0, 0}, 2.0f, 0.5f, nullptr));
    EXPECT_NEAR(v.x, 1.0f, 1e-6f);
    float carry = 0.0f;
    EXPECT_FALSE(cam::approach(v, {1.5f, 0, 0}, 2.0f, 0.5f, &carry));
    EXPECT_FLOAT_EQ(v.x, 1.5f);
    EXPECT_NEAR(carry, 0.5f, 1e-6f);
}

TEST(Camera, PanMapping) {
    EXPECT_EQ(cameraPanFor(true, false, false, false), 0.25f);
    EXPECT_EQ(cameraPanFor(false, true, false, false), 0.75f);
    EXPECT_EQ(cameraPanFor(false, false, true, false), 0.5f);
    EXPECT_EQ(cameraPanFor(true, false, true, false), 0.375f);
    EXPECT_EQ(cameraPanFor(false, true, false, true), 0.875f);
    EXPECT_EQ(cameraPanFor(false, false, false, false), 0.0f);
}

TEST(TrackCamera, FollowsStraightSmoothlyWithinLimits) {
    TrackCamera cam(bugNear());
    World world;
    const auto probe = world.probe();
    Car car;
    car.speed = 20.0f;
    cam.reset(car.target());
    Vec3 last;
    for (int i = 0; i < 300; ++i) {
        car.step(kDt);
        cam.update(kDt, car.target(), probe, {});
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

TEST(TrackCamera, HardTurnLagsThenCatchesUp) {
    TrackCamera cam(bugNear());
    World world;
    const auto probe = world.probe();
    Car car;
    car.speed = 15.0f;
    cam.reset(car.target());
    for (int i = 0; i < 60; ++i) {
        car.step(kDt);
        cam.update(kDt, car.target(), probe, {});
    }
    car.yawRate = kPi * 0.5f; // 90 degrees per second
    float minBehind = 1.0f;
    for (int i = 0; i < 30; ++i) {
        car.step(kDt);
        cam.update(kDt, car.target(), probe, {});
        ASSERT_TRUE(finite(cam.matrix()));
        const Vec3 rel = (cam.matrix().m3 - car.pos).normalized();
        minBehind = std::min(minBehind, -rel.dot(car.forward()));
        EXPECT_GT(viewDot(cam.matrix(), aim(car)), 0.7f) << i; // keeps the car in view
    }
    EXPECT_LT(minBehind, 0.95f); // lagged out of line during the turn
    car.yawRate = 0.0f;
    for (int i = 0; i < 90; ++i) {
        car.step(kDt);
        cam.update(kDt, car.target(), probe, {});
    }
    const Vec3 rel = (cam.matrix().m3 - car.pos).normalized();
    EXPECT_GT(-rel.dot(car.forward()), 0.9f); // back behind
}

TEST(TrackCamera, ReverseSwingsToFrontAfterDelay) {
    TrackCamera cam(bugNear());
    World world;
    const auto probe = world.probe();
    Car car;
    cam.reset(car.target());
    for (int i = 0; i < 30; ++i)
        cam.update(kDt, car.target(), probe, {});
    car.speed = -5.0f;
    car.reverse = true;
    for (int i = 0; i < 50; ++i) { // 1.67 s: not yet
        car.step(kDt);
        cam.update(kDt, car.target(), probe, {});
    }
    EXPECT_FALSE(cam.reverseViewActive());
    for (int i = 0; i < 150; ++i) {
        car.step(kDt);
        cam.update(kDt, car.target(), probe, {});
        ASSERT_TRUE(finite(cam.matrix()));
    }
    EXPECT_TRUE(cam.reverseViewActive());
    EXPECT_GT((cam.matrix().m3 - car.pos).dot(car.forward()), 2.0f); // camera in front
    EXPECT_GT(viewDot(cam.matrix(), aim(car)), 0.9f);
    // Driving forward again swings back behind.
    car.speed = 10.0f;
    car.reverse = false;
    for (int i = 0; i < 150; ++i) {
        car.step(kDt);
        cam.update(kDt, car.target(), probe, {});
    }
    EXPECT_FALSE(cam.reverseViewActive());
    EXPECT_LT((cam.matrix().m3 - car.pos).dot(car.forward()), -2.0f);
}

TEST(TrackCamera, StopsAndSettles) {
    TrackCamera cam(bugNear());
    World world;
    const auto probe = world.probe();
    Car car;
    car.speed = 25.0f;
    cam.reset(car.target());
    for (int i = 0; i < 90; ++i) {
        car.step(kDt);
        cam.update(kDt, car.target(), probe, {});
    }
    car.speed = 0.0f;
    for (int i = 0; i < 300; ++i)
        cam.update(kDt, car.target(), probe, {});
    const Vec3 before = cam.matrix().m3;
    cam.update(kDt, car.target(), probe, {});
    EXPECT_LT((cam.matrix().m3 - before).mag(), 1e-3f);
    // Settled at the goal: Offset.z behind TrackTo, Offset.y above it.
    const Vec3 target = car.target().matrix.transform(cam.params().app.trackTo);
    EXPECT_NEAR((cam.matrix().m3 - target).dot(-car.forward()), 4.06f, 0.5f);
}

TEST(TrackCamera, LookBackAndCollision) {
    TrackCamera cam(bugNear());
    World world;
    Car car;
    cam.reset(car.target());
    const auto probe = world.probe();
    for (int i = 0; i < 60; ++i)
        cam.update(kDt, car.target(), probe, {});
    CameraInput back;
    back.camPan = 0.5f;
    for (int i = 0; i < 90; ++i)
        cam.update(kDt, car.target(), probe, back);
    EXPECT_GT((cam.matrix().m3 - car.pos).dot(car.forward()), 2.0f); // swung to the front

    // A wall right behind the car pulls the camera in front of it.
    World walled;
    walled.wallX = std::nullopt;
    TrackCamera cam2(bugNear());
    Car side;
    side.yaw = kPi * 0.5f; // facing -X: "behind" is +X
    cam2.reset(side.target());
    walled.wallX = 2.0f;
    const auto wallProbe = walled.probe();
    for (int i = 0; i < 120; ++i) {
        cam2.update(kDt, side.target(), wallProbe, {});
        ASSERT_TRUE(finite(cam2.matrix()));
    }
    EXPECT_LT(cam2.matrix().m3.x, 2.0f);
}

TEST(TrackCamera, Deterministic) {
    auto run = [] {
        TrackCamera cam(bugNear());
        World world;
        const auto probe = world.probe();
        Car car;
        car.speed = 12.0f;
        cam.reset(car.target());
        std::vector<Mat34> out;
        for (int i = 0; i < 200; ++i) {
            car.yawRate = std::sin(i * 0.05f);
            car.step(kDt);
            CameraInput in;
            in.camPan = (i / 50) % 2 ? 0.25f : 0.0f;
            cam.update(kDt, car.target(), probe, in);
            out.push_back(cam.matrix());
        }
        return out;
    };
    const auto a = run(), b = run();
    ASSERT_EQ(a.size(), b.size());
    EXPECT_EQ(std::memcmp(a.data(), b.data(), a.size() * sizeof(Mat34)), 0);
}

TEST(PovCamera, EyeAndLookAround) {
    PovCamParams p;
    p.offset = {0.0f, 1.19f, -0.5519f};
    p.reverseOffset = Vec3{0.0f, 1.7f, 0.75f};
    p.app.approachOn = 0;
    PovCamera pov(p);
    Car car;
    car.pos = {10, 0, 20};
    car.yaw = 0.3f;
    pov.reset(car.target());
    pov.update(kDt, car.target(), {}, {});
    const Mat34 t = car.target().matrix;
    EXPECT_NEAR((pov.matrix().m3 - t.transform(p.offset)).mag(), 0.0f, 1e-4f);
    EXPECT_NEAR(pov.matrix().m2.dot(t.m2), 1.0f, 1e-5f); // looks where the car looks
    CameraInput back;
    back.camPan = 0.5f;
    pov.update(kDt, car.target(), {}, back);
    EXPECT_NEAR(pov.matrix().m2.dot(t.m2), -1.0f, 1e-4f);
    EXPECT_NEAR((pov.matrix().m3 - t.transform(*p.reverseOffset)).mag(), 0.0f, 1e-4f);
    CameraInput left;
    left.camPan = 0.25f;
    pov.update(kDt, car.target(), {}, left);
    EXPECT_NEAR((-pov.matrix().m2).dot(-t.m0), 1.0f, 1e-4f); // looking left
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
    Camera view;
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
        cams.apply(view);
        EXPECT_GE(view.horizontalFov, 60.0f * cam::kDegToRad - 1e-5f);
        EXPECT_LE(view.horizontalFov, 70.0f * cam::kDegToRad + 1e-5f);
    }
    EXPECT_FALSE(cams.viewManager().inTransition());
    EXPECT_EQ(cams.view(), PlayerCameras::View::Far);
    EXPECT_LT(maxJump, 1.5f);
    cams.apply(view);
    EXPECT_NEAR(view.horizontalFov, 60.0f * cam::kDegToRad, 1e-5f);
    EXPECT_EQ(view.transform.m3.x, cams.farCam().matrix().m3.x);

    cams.toggleCamera(); // cycle: near -> pov -> far -> near
    EXPECT_EQ(cams.view(), PlayerCameras::View::Near);
    cams.toggleCamera();
    EXPECT_EQ(cams.view(), PlayerCameras::View::Pov);
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
    EXPECT_TRUE(cams.dashCam().params().reverseOffset.has_value());
    missing.clear();
    cams.load(*test::gameData(), "vpvwcup", &missing);
    ASSERT_EQ(missing.size(), 2u); // "vpvwcup__far" typo and no _ind
    EXPECT_EQ(missing[0], "vpvwcup_far");
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
                c.reverse = phase >= 8.0f;
                c.yawRate = (phase > 2.0f && phase < 4.0f) ? 1.2f : 0.0f;
                c.step(kDt);
                CameraInput in;
                in.camPan = (phase > 4.5f && phase < 5.5f) ? 0.5f : 0.0f;
                cam.update(kDt, c.target(), probe, in);
                ASSERT_TRUE(finite(cam.matrix())) << car << " " << viewName(view) << " " << i;
                if (view == PlayerCameras::View::Near || view == PlayerCameras::View::Far) {
                    const Vec3 tgt = c.target().matrix.transform(cam.app().trackTo);
                    EXPECT_LT((cam.matrix().m3 - tgt).mag(), 40.0f) << car << " " << viewName(view) << " " << i;
                    // Not while swinging for the look-back or the reverse view.
                    const bool swinging = (phase > 4.4f && phase < 6.5f) || (phase > 9.9f && phase < 12.5f);
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
            c.reverse = phase >= 10.0f;
            c.yawRate = (phase > 2.0f && phase < 3.5f) ? 1.05f : ((phase > 5.0f && phase < 6.5f) ? -1.05f : 0.0f);
            c.step(kDt);
            cam.update(kDt, c.target(), probe, {});
            const Mat34& m = cam.matrix();
            csv += std::format("{},{},{},{},{},{},{}\n", phase, c.pos.x, c.pos.z, m.m3.x, m.m3.z, -m.m2.x, -m.m2.z);
        }
        FILE* f = std::fopen((std::string(dir) + "/" + viewName(view) + ".csv").c_str(), "wb");
        ASSERT_TRUE(f);
        std::fwrite(csv.data(), 1, csv.size(), f);
        std::fclose(f);
    }
}
