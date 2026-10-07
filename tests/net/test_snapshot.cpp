#include "net/ClockSync.h"
#include "net/Snapshot.h"

#include <gtest/gtest.h>

using namespace mm2;
using namespace mm2::net;

namespace {

// A car moving at constant velocity along +X.
VehicleSnapshot linear(std::uint32_t timeMs, float speed = 20.0f) {
    VehicleSnapshot s;
    s.time = timeMs;
    s.position = {speed * static_cast<float>(timeMs) / 1000.0f, 0.5f, -10.0f};
    s.linearVelocity = {speed, 0, 0};
    s.orientation = Quat::fromAxisAngle({0, 1, 0}, 0.25f);
    return s;
}

} // namespace

TEST(SnapshotBuffer, EmptyAndSingle) {
    SnapshotBuffer b;
    VehicleSnapshot out;
    EXPECT_EQ(b.sample(100, out), SnapshotBuffer::Result::Empty);
    b.push(linear(100));
    EXPECT_EQ(b.sample(100, out), SnapshotBuffer::Result::Interpolated);
    EXPECT_EQ(b.sample(50, out), SnapshotBuffer::Result::Held);
    EXPECT_EQ(out.time, 100u);
}

TEST(SnapshotBuffer, InterpolatesLinearMotionExactly) {
    SnapshotBuffer b;
    for (std::uint32_t t = 0; t <= 500; t += 50)
        b.push(linear(t));
    VehicleSnapshot out;
    for (double t = 0; t <= 500; t += 7.5) {
        ASSERT_EQ(b.sample(t, out), SnapshotBuffer::Result::Interpolated) << t;
        EXPECT_NEAR(out.position.x, 20.0f * t / 1000.0, 1e-3) << t;
        EXPECT_NEAR(out.position.z, -10.0f, 1e-4);
    }
}

TEST(SnapshotBuffer, HermiteFollowsCurvedPath) {
    // Circular motion r = 10 m, w = 1 rad/s sampled every 100 ms: Hermite with
    // true tangents stays within a few mm of the arc between samples.
    SnapshotBuffer b;
    for (int i = 0; i <= 10; ++i) {
        const float t = i * 0.1f;
        VehicleSnapshot s;
        s.time = static_cast<std::uint32_t>(i * 100);
        s.position = {10 * std::cos(t), 0, 10 * std::sin(t)};
        s.linearVelocity = {-10 * std::sin(t), 0, 10 * std::cos(t)};
        b.push(s);
    }
    VehicleSnapshot out;
    for (double ms = 0; ms <= 1000; ms += 13) {
        b.sample(ms, out);
        const double t = ms / 1000.0;
        EXPECT_NEAR(out.position.x, 10 * std::cos(t), 0.005);
        EXPECT_NEAR(out.position.z, 10 * std::sin(t), 0.005);
    }
}

TEST(SnapshotBuffer, ExtrapolatesThenHolds) {
    SnapshotBuffer b;
    b.push(linear(0));
    b.push(linear(100));
    VehicleSnapshot out;
    EXPECT_EQ(b.sample(200, out, 250), SnapshotBuffer::Result::Extrapolated);
    EXPECT_NEAR(out.position.x, 4.0f, 1e-3); // 20 m/s * 0.2 s
    EXPECT_EQ(b.sample(1000, out, 250), SnapshotBuffer::Result::Held);
    EXPECT_NEAR(out.position.x, 2.0f + 20.0f * 0.25f, 1e-3); // capped at 250 ms past the last snapshot
}

TEST(SnapshotBuffer, ExtrapolatesRotation) {
    VehicleSnapshot s;
    s.time = 0;
    s.angularVelocity = {0, kHalfPi, 0}; // 90 deg/s yaw
    const auto out = extrapolateSnapshot(s, 1.0);
    const Mat34 m = out.orientation.toMatrix();
    // Rotating +X by 90 degrees about +Y (counter-clockwise from above) gives -Z.
    const Vec3 x = m.transformDir({1, 0, 0});
    EXPECT_NEAR(x.x, 0.0f, 1e-4);
    EXPECT_NEAR(x.z, -1.0f, 1e-4);
}

TEST(SnapshotBuffer, OrdersAndDeduplicates) {
    SnapshotBuffer b(4);
    b.push(linear(300));
    b.push(linear(100));
    b.push(linear(200));
    b.push(linear(200)); // duplicate replaces
    EXPECT_EQ(b.size(), 3u);
    b.push(linear(400));
    b.push(linear(500)); // capacity 4: drops 100
    EXPECT_EQ(b.size(), 4u);
    b.push(linear(50)); // older than everything kept: ignored
    EXPECT_EQ(b.size(), 4u);
    VehicleSnapshot out;
    EXPECT_EQ(b.sample(150, out), SnapshotBuffer::Result::Held);
    EXPECT_EQ(out.time, 200u);
    b.prune(450);
    EXPECT_EQ(b.size(), 2u); // keeps 400 (left edge) and 500
}

TEST(ClockSync, PicksLowestRttSample) {
    ClockSync c;
    EXPECT_FALSE(c.synced());
    // Host clock is 10'000 ms ahead of ours. Asymmetric delays bias every
    // sample except the fast one.
    c.addSample(1000, 11000 + 80, 1200);  // 200 ms rtt, request leg slow
    c.addSample(2000, 12000 + 10, 2020);  // 20 ms rtt, symmetric
    c.addSample(3000, 13000 + 150, 3300); // 300 ms rtt
    EXPECT_TRUE(c.synced());
    EXPECT_EQ(c.rtt(), 20u);
    EXPECT_EQ(c.offset(), 10000);
    EXPECT_DOUBLE_EQ(c.toHostTime(5000), 15000.0);
}

TEST(ClockSync, WindowForgetsOldSamples) {
    ClockSync c(2);
    c.addSample(0, 100 + 5, 10);
    c.addSample(100, 200 + 50, 200);
    c.addSample(200, 300 + 50, 300);
    EXPECT_EQ(c.rtt(), 100u); // the 10 ms sample fell out of the window
}
