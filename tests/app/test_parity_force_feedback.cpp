// Parity checks of the force feedback against MM2's mmPlayer::UpdateFF /
// FFImpactCallback / ResetFF, mmCarRoadFF and the effect classes (MM2Recomp,
// build 3393). See docs/parity/mm2/input-ff.md.

#include "app/ForceFeedback.h"

#include <gtest/gtest.h>

#include <vector>

using namespace mm2;
using namespace mm2::app::controls;
using platform::FFEffect;

namespace {

// Records what the effects were told, in DirectInput units.
class FakeDevice final : public platform::FFDevice {
public:
    Kind kind() const override { return Kind::Haptic; }
    bool has(FFEffect) const override { return true; }
    bool play(FFEffect e) override {
        plays.push_back(e);
        return true;
    }
    bool stop(FFEffect e) override {
        stops.push_back(e);
        return true;
    }
    void stopAll() override {}
    bool setCondition(FFEffect e, int c) override {
        (e == FFEffect::Spring ? spring : friction) = c;
        return true;
    }
    bool setRoad(int magnitude, int period) override {
        roadMagnitude = magnitude;
        roadPeriod = period;
        return true;
    }
    bool setCollision(int g, int d) override {
        gain = g;
        direction = d;
        return true;
    }
    void update(float) override {}

    std::vector<FFEffect> plays, stops;
    int friction = -1, spring = -1, roadMagnitude = -1, roadPeriod = -1, gain = -1, direction = -1;
};

ForceFeedback makeFF(FakeDevice& d, Controller c = Controller::Wheel, float road = 1.0f, float collision = 1.0f) {
    ForceFeedback ff;
    Options o;
    o.forceFeedback = true;
    o.ffRoadForce = road;
    o.ffCollision = collision;
    ff.configure(c, o);
    ff.setDevice(&d);
    return ff;
}

int count(const std::vector<FFEffect>& v, FFEffect e) {
    int n = 0;
    for (FFEffect x : v)
        n += x == e;
    return n;
}

} // namespace

TEST(ParityForceFeedback, DoingFFNeedsTheOptionTheDeviceAndAStick) {
    FakeDevice d;
    EXPECT_TRUE(makeFF(d, Controller::Wheel).doing());
    EXPECT_TRUE(makeFF(d, Controller::Joystick).doing());
    EXPECT_FALSE(makeFF(d, Controller::GamePad).doing());
    EXPECT_FALSE(makeFF(d, Controller::Keyboard).doing());
    ForceFeedback off;
    off.configure(Controller::Wheel, Options{});
    off.setDevice(&d);
    EXPECT_FALSE(off.doing());
    ForceFeedback none = makeFF(d);
    none.setDevice(nullptr);
    EXPECT_FALSE(none.doing());
}

TEST(ParityForceFeedback, FrictionAndSpringFollowTheCar) {
    FakeDevice d;
    ForceFeedback ff = makeFF(d);
    FFCar car;
    car.speed = 45.0f; // halfway between 10 and 80 m/s
    car.friction = 0.8f;
    ff.update(car, 0.016f, false);
    EXPECT_EQ(count(d.plays, FFEffect::Friction), 1);
    EXPECT_EQ(count(d.plays, FFEffect::Spring), 1);
    EXPECT_EQ(d.friction, static_cast<int>(0.7f * 0.8f * 10000.0f)); // 0.7 x the tyre's friction
    EXPECT_EQ(d.spring, 5000);
    // Still playing: not started again; the spring is resent only when it
    // moves by more than 0.01.
    d.spring = -1;
    car.speed = 45.5f;
    ff.update(car, 0.016f, false);
    EXPECT_EQ(count(d.plays, FFEffect::Friction), 1);
    EXPECT_EQ(d.spring, -1);
    // Sliding (lateral slip 0.05 or more): no spring.
    car.latSlip = 0.2f;
    ff.update(car, 0.016f, false);
    EXPECT_EQ(d.spring, 0);
    // Above 80 m/s: full.
    car.latSlip = 0.0f;
    car.speed = 90.0f;
    ff.update(car, 0.016f, false);
    EXPECT_EQ(d.spring, 10000);
}

TEST(ParityForceFeedback, SpringScalesByTheWholeRoadForce) {
    // mmSpringFF::Assign multiplies by the road force truncated to an int.
    FakeDevice half, two;
    ForceFeedback a = makeFF(half, Controller::Wheel, 0.5f);
    ForceFeedback b = makeFF(two, Controller::Wheel, 2.0f);
    FFCar car;
    car.speed = 45.0f;
    a.update(car, 0.016f, false);
    b.update(car, 0.016f, false);
    EXPECT_EQ(half.spring, 0);
    EXPECT_EQ(two.spring, 10000);
}

TEST(ParityForceFeedback, RoadWaveFromBumpsAndDamage) {
    FakeDevice d;
    ForceFeedback ff = makeFF(d);
    FFCar car;
    car.speed = 10.0f;
    car.bumpHeight = 0.05f;
    car.bumpWidth = 0.5f; // 20 bumps a second: the 20 Hz cap
    ff.update(car, 0.016f, false);
    EXPECT_EQ(count(d.plays, FFEffect::Road), 1);
    EXPECT_EQ(d.roadPeriod, 50000); // 1 / 20 s
    EXPECT_EQ(d.roadMagnitude, 500); // the bump height x 10000
    // A flat surface stops it.
    car.bumpHeight = 0.0f;
    ff.update(car, 0.016f, false);
    EXPECT_EQ(count(d.stops, FFEffect::Road), 1);
    // A damaged car on the ground: speed / (radius x 10) at 0.4 x damage.
    car.damage = 0.5f;
    car.onGround = true;
    car.radius = 0.25f; // 4 a second
    ff.update(car, 1.0f, false); // 0.5 s after the last impact jolt (none yet)
    EXPECT_EQ(d.roadPeriod, 250000);
    EXPECT_EQ(d.roadMagnitude, 2000);
}

TEST(ParityForceFeedback, ImpactsJoltOncePerHalfSecond) {
    FakeDevice d;
    ForceFeedback ff = makeFF(d, Controller::Wheel, 1.0f, 2.0f);
    FFCar car;
    ff.update(car, 1.0f, false);
    ff.impact(50.0f, 20.0f);
    EXPECT_EQ(count(d.plays, FFEffect::Collision), 1);
    EXPECT_EQ(d.gain, 10000); // 0.5 x 10000 x the collision intensity 2
    EXPECT_GE(d.direction, 0);
    EXPECT_LE(d.direction, 36000);
    ff.impact(50.0f, 20.0f); // within 0.5 s
    EXPECT_EQ(count(d.plays, FFEffect::Collision), 1);
    ff.update(car, 0.6f, false);
    ff.impact(1.0f, 4.0f); // too slow
    EXPECT_EQ(count(d.plays, FFEffect::Collision), 1);
    ff.impact(1.0f, 20.0f); // at least 0.1
    EXPECT_EQ(d.gain, 2000);
    // A hard suspension bump jolts at 0.2.
    car.suspensionSpeed = {0.0f, 2.0f, 0.0f, 0.0f};
    car.speedMph = 30.0f;
    ff.update(car, 0.6f, false);
    EXPECT_EQ(count(d.plays, FFEffect::Collision), 3);
    // 0.2 x 10000 x 2: 20 x 0.01 rounds (to even) to the float below 0.2,
    // and ftol of 1999.9999 is 1999.
    EXPECT_EQ(d.gain, 3998);
}

TEST(ParityForceFeedback, StopAllLeavesTheCollisionAndPausedResets) {
    FakeDevice d;
    ForceFeedback ff = makeFF(d);
    ff.stopAll();
    EXPECT_EQ(count(d.stops, FFEffect::Collision), 0); // mmCollideFF::Stop does nothing
    EXPECT_EQ(count(d.stops, FFEffect::Road), 1);
    EXPECT_EQ(count(d.stops, FFEffect::Spring), 1);
    EXPECT_EQ(count(d.stops, FFEffect::Friction), 1);
    // Paused: ResetFF stops the road and starts the friction and spring.
    FFCar car;
    ff.update(car, 0.016f, true);
    EXPECT_EQ(count(d.stops, FFEffect::Road), 2);
    EXPECT_EQ(count(d.plays, FFEffect::Friction), 1);
    EXPECT_EQ(count(d.plays, FFEffect::Spring), 1);
}
