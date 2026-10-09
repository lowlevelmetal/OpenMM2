// The shared traffic of a network cruise predicted to the present on a
// client (game/net/TrafficPrediction, TrafficClient): the host's rail motion
// measured from its AI steps, the prediction along a rail, and a host and a
// client over a simulated Internet link (docs/review/
// multiplayer-desync-traffic.md reproduces its numbers in the game).
#include "game/net/TrafficPrediction.h"
#include "game/net/TrafficSync.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <random>
#include <vector>

using namespace mm2;
using game::PredictedPose;
using game::RailMotion;
using game::SharedCar;
using game::TrafficClient;
using game::TrafficHost;

namespace {


constexpr double kAiStepMs = 1000.0 / 30.0;

// A model matrix facing heading `h` (forward -m2 = (sin h, 0, cos h)).
Mat34 facing(float h, const Vec3& p) {
    Mat34 m = Mat34::rotationY(h + kPi);
    m.m3 = p;
    return m;
}

// A car's true path: speed against time, curvature against distance,
// integrated in 1 ms steps.
struct Path {
    struct State {
        Vec3 position;
        float heading = 0.0f;
        float speed = 0.0f;
    };
    std::vector<State> states; // one per ms

    Path(Vec3 start, float heading, double ms, const std::function<float(double)>& speed,
         const std::function<float(double)>& curvature) {
        State s{start, heading, speed(0.0)};
        double distance = 0.0;
        for (int i = 0; i <= static_cast<int>(ms); ++i) {
            s.speed = speed(i);
            states.push_back(s);
            const float ds = s.speed * 0.001f;
            const float turn = curvature(distance) * ds;
            const float mid = s.heading + turn * 0.5f;
            s.position = s.position + Vec3{std::sin(mid), 0.0f, std::cos(mid)} * ds;
            s.heading += turn;
            distance += ds;
        }
    }
    State at(double ms) const {
        const auto i = static_cast<std::size_t>(std::clamp(ms, 0.0, static_cast<double>(states.size() - 2)));
        const double f = std::clamp(ms - static_cast<double>(i), 0.0, 1.0);
        const State& a = states[i];
        const State& b = states[i + 1];
        const auto t = static_cast<float>(f);
        return {a.position + (b.position - a.position) * t, a.heading + (b.heading - a.heading) * t,
                a.speed + (b.speed - a.speed) * t};
    }
};

// As ai::Traffic publishes it; `nominal`: its speed over its true speed (in
// a turn the AI's speed is not the ground it covers).
ai::AmbientCar aiCar(int id, const Path::State& s, float nominal = 1.0f) {
    ai::AmbientCar c;
    c.id = id;
    c.transform = facing(s.heading, s.position);
    c.speed = s.speed * nominal;
    c.velocity = -c.transform.m2 * c.speed;
    return c;
}

float percentile(std::vector<float> v, double q) {
    if (v.empty())
        return 0.0f;
    std::ranges::sort(v);
    return v[std::min(v.size() - 1, static_cast<std::size_t>(static_cast<double>(v.size()) * q))];
}

} // namespace

TEST(TrafficPrediction, ARailCarFollowsItsArc) {
    // 10 m/s on a 20 m radius, turning from +z toward +x.
    const float speed = 10.0f, radius = 20.0f;
    const RailMotion motion{0.0f, 1.0f / radius};
    const Mat34 start = facing(0.0f, {0, 5, 0});
    for (float dt : {0.1f, 0.3f, 0.6f}) {
        const PredictedPose p = game::predictRailCar(start, speed, motion, dt);
        const float turn = speed * dt / radius;
        const Vec3 truth{radius * (1.0f - std::cos(turn)), 5.0f, radius * std::sin(turn)};
        EXPECT_NEAR(p.transform.m3.dist(truth), 0.0f, 1e-3f) << dt;
        EXPECT_NEAR(game::groundHeading(-p.transform.m2), turn, 1e-4f) << dt;
        EXPECT_NEAR(p.speed, speed, 1e-4f);
        EXPECT_NEAR(p.velocity.dist(-p.transform.m2 * speed), 0.0f, 1e-4f);
        // The frame stays a rotation.
        EXPECT_NEAR(p.transform.m0.dot(p.transform.m2), 0.0f, 1e-5f);
        EXPECT_NEAR(p.transform.m1.y, 1.0f, 1e-5f);
    }
}

TEST(TrafficPrediction, ABrakingRailCarStopsAndNeverReverses) {
    const Mat34 start = facing(kPi * 0.5f, {0, 0, 0}); // along +x
    const PredictedPose p = game::predictRailCar(start, 10.0f, {-8.0f, 0.0f}, 3.0f);
    EXPECT_NEAR(p.transform.m3.x, 6.25f, 1e-3f); // v^2 / 2a
    EXPECT_NEAR(p.transform.m3.z, 0.0f, 1e-4f);
    EXPECT_EQ(p.speed, 0.0f);
    // Starting from a standstill: half a t^2.
    const PredictedPose q = game::predictRailCar(start, 0.0f, {4.0f, 0.0f}, 0.5f);
    EXPECT_NEAR(q.transform.m3.x, 0.5f, 1e-4f);
    EXPECT_NEAR(q.speed, 2.0f, 1e-5f);
}

// The ground it covers is its speed over the ground's; its velocity (what a
// body it takes when hit moves at) stays its own speed's.
TEST(TrafficPrediction, ARailCarCoversTheGroundItReallyCovers) {
    const Mat34 start = facing(0.0f, {0, 0, 0});
    const PredictedPose p = game::predictRailCar(start, 10.0f, {0.0f, 0.0f, true, 12.0f}, 0.5f);
    EXPECT_NEAR(p.transform.m3.z, 6.0f, 1e-4f);
    EXPECT_NEAR(p.speed, 10.0f, 1e-5f);
    // Held at the end of its lane: going nowhere at its speed.
    const PredictedPose held = game::predictRailCar(start, 14.0f, {0.0f, 0.0f, true, 0.0f}, 0.5f);
    EXPECT_NEAR(held.transform.m3.z, 0.0f, 1e-5f);
    EXPECT_NEAR(held.velocity.z, 14.0f, 1e-4f);
}

TEST(TrafficPrediction, ABodyTurnsWithItsYawRate) {
    // A police car at 20 m/s turning at 0.5 rad/s: a 40 m radius.
    const Mat34 start = facing(0.0f, {0, 0, 0});
    const Vec3 v = -start.m2 * 20.0f;
    const PredictedPose p = game::predictBody(start, v, {0, 0.5f, 0}, 0.4f);
    const float turn = 0.2f;
    const Vec3 truth{40.0f * (1.0f - std::cos(turn)), 0, 40.0f * std::sin(turn)};
    EXPECT_NEAR(p.transform.m3.dist(truth), 0.0f, 1e-3f);
    EXPECT_NEAR(game::groundHeading(-p.transform.m2), turn, 1e-4f);
    EXPECT_NEAR(game::groundHeading(p.velocity), turn, 1e-4f);
}

TEST(TrafficPrediction, TheHostMeasuresTheRailMotionFromItsSteps) {
    const auto speed = [](double ms) { return static_cast<float>(5.0 + 2.0 * ms / 1000.0); };
    const Path path({0, 0, 0}, 0.0f, 3000.0, speed, [](double) { return 0.05f; });
    game::RailMotionTracker tracker;
    for (int step = 0; step < 60; ++step) {
        const double t = step * kAiStepMs;
        // Car 5 covers more ground than its speed says (a turn's curve).
        const std::vector<ai::AmbientCar> cars{aiCar(3, path.at(t)), aiCar(5, path.at(t), 0.8f)};
        tracker.update(cars, t);
        tracker.update(cars, t); // a frame without an AI step changes nothing
    }
    const RailMotion m = tracker.motion(3);
    EXPECT_NEAR(m.accel, 2.0f, 0.01f);
    EXPECT_NEAR(m.curvature, 0.05f, 0.001f);
    EXPECT_FALSE(m.slips);
    const RailMotion slipping = tracker.motion(5);
    EXPECT_TRUE(slipping.slips);
    EXPECT_NEAR(slipping.groundSpeed, speed(58.5 * kAiStepMs), 0.02f); // over the last step
    EXPECT_EQ(tracker.motion(4).accel, 0.0f);
    // A recycled slot starts again.
    ai::AmbientCar again = aiCar(3, path.at(0.0));
    again.spawns = 1;
    tracker.update(std::vector<ai::AmbientCar>{again}, 3000.0);
    EXPECT_EQ(tracker.motion(3).curvature, 0.0f);
}

// The correction a newer message brings is blended away in the drawing; the
// physics take the newest prediction at once, and a big one is shown at once.
TEST(TrafficPrediction, CorrectionsAreBlendedInTheDrawingOnly) {
    TrafficHost host;
    TrafficClient client(8, 0x1234);
    auto send = [&](std::uint32_t time, float z) {
        SharedCar c;
        c.id = 5;
        c.model = 1;
        c.transform = facing(kPi, {0, 0, z}); // along -z
        c.speed = 10.0f;
        c.velocity = -c.transform.m2 * 10.0f;
        const std::vector<SharedCar> list{c};
        net::AmbientStateMsg out;
        const auto msg = host.build({1, {0, 0, 0}}, list, time, 0, 0x1234);
        ASSERT_TRUE(net::decodeMessage(net::encodeMessage(msg), out));
        client.receive(out);
    };
    send(1000, 0.0f);
    client.update(1100.0);
    ASSERT_EQ(client.cars().size(), 1u);
    // (Positions travel at 3 cm, speeds at 6 cm/s.)
    EXPECT_NEAR(client.cars()[0].transform.m3.z, -1.0f, 0.03f); // predicted 100 ms on
    // The car was half a metre further back than predicted.
    send(1050, 0.0f);
    client.update(1110.0);
    EXPECT_NEAR(client.cars()[0].transform.m3.z, -0.6f, 0.03f); // the physics: at once
    const auto raw = client.poseAt(5, 1110.0);
    ASSERT_TRUE(raw);
    EXPECT_NEAR(raw->transform.m3.z, client.cars()[0].transform.m3.z, 1e-4f);
    const auto drawn = client.transformAt(5, 1110.0);
    ASSERT_TRUE(drawn);
    EXPECT_NEAR(drawn->m3.z, -1.1f, 0.05f); // the drawing: where it was going
    client.update(1410.0);
    const auto later = client.transformAt(5, 1410.0);
    ASSERT_TRUE(later);
    EXPECT_NEAR(later->m3.z, client.cars()[0].transform.m3.z, 0.03f); // blended away
    // Ten metres off: shown there at once.
    send(1400, 10.0f);
    client.update(1420.0);
    const auto snapped = client.transformAt(5, 1420.0);
    ASSERT_TRUE(snapped);
    EXPECT_NEAR(snapped->m3.z, client.cars()[0].transform.m3.z, 1e-3f);
    EXPECT_EQ(client.stats().snaps, 1u);
}

// A host and a client over a simulated Internet link (60 +- 20 ms each way,
// 2 % loss, reordering): the client's cars against the host's at the same
// moment, shown at the present (predicted) and, as before, a playout delay
// in the past (interpolated).
TEST(TrafficPrediction, OverALossyLinkTheClientSeesTheHostsPresent) {
    const double kMs = 20000.0;
    std::vector<Path> paths;
    // Round a 25 m bend at 10 m/s.
    paths.emplace_back(Vec3{0, 0, 0}, 0.0f, kMs, [](double) { return 10.0f; }, [](double) { return 0.04f; });
    // Stop and go: 12 m/s, braking at 4 m/s^2, 2 s still, away at 3 m/s^2.
    paths.emplace_back(Vec3{50, 0, 0}, kPi * 0.5f, kMs,
                       [](double ms) {
                           const double t = std::fmod(ms / 1000.0, 10.0);
                           if (t < 2.0)
                               return 12.0f;
                           if (t < 5.0)
                               return static_cast<float>(std::max(0.0, 12.0 - 4.0 * (t - 2.0)));
                           if (t < 7.0)
                               return 0.0f;
                           return static_cast<float>(std::min(12.0, 3.0 * (t - 7.0)));
                       },
                       [](double) { return 0.0f; });
    // Straight, then a right turn of 12 m radius at a junction, every 60 m.
    const auto junctions = [](double s) {
        const double along = std::fmod(s, 60.0);
        return along > 40.0 && along < 58.85 ? 1.0f / 12.0f : 0.0f;
    };
    paths.emplace_back(Vec3{-50, 0, 0}, 0.0f, kMs, [](double) { return 8.0f; }, junctions);

    TrafficHost host;
    TrafficClient client(8, 0x1234);
    TrafficClient delayed(8, 0x1234);
    game::RailMotionTracker tracker;
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> jitter(-20.0, 20.0);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::multimap<double, net::AmbientStateMsg> link; // arrival -> message
    double lastAi = -1.0;
    std::vector<float> present, past, drawnErr;
    for (double now = 0.0; now < kMs; now += 1000.0 / 120.0) {
        // Host: its AI at 30 Hz, a message every 50 ms stamped with the AI step.
        const double aiTime = std::floor(now / kAiStepMs) * kAiStepMs;
        if (aiTime != lastAi) {
            std::vector<ai::AmbientCar> cars;
            for (std::size_t i = 0; i < paths.size(); ++i)
                cars.push_back(aiCar(static_cast<int>(i), paths[i].at(aiTime), i == 2 ? 0.8f : 1.0f));
            tracker.update(cars, aiTime);
            if (std::floor(aiTime / 50.0) != std::floor(lastAi / 50.0)) {
                std::vector<SharedCar> shared;
                for (const auto& c : cars) {
                    SharedCar s = game::shareTrafficCar(c, 1, 0, nullptr, false);
                    s.motion = tracker.motion(c.id);
                    shared.push_back(s);
                }
                const auto stamp = static_cast<std::uint32_t>(std::llround(aiTime));
                const auto msg = host.build({1, {0, 0, 0}}, shared, stamp, 0, 0x1234);
                net::AmbientStateMsg out;
                ASSERT_TRUE(net::decodeMessage(net::encodeMessage(msg), out));
                if (unit(rng) >= 0.02)
                    link.emplace(now + 60.0 + jitter(rng), out);
            }
            lastAi = aiTime;
        }
        // Client: what has arrived, the cars at the present.
        while (!link.empty() && link.begin()->first <= now) {
            client.receive(link.begin()->second);
            delayed.receive(link.begin()->second);
            link.erase(link.begin());
        }
        client.update(now);
        delayed.update(now - 160.0); // the host car's playout delay over such a link
        if (now < 1000.0)
            continue;
        for (const auto& c : client.cars()) {
            const Vec3 truth = paths[static_cast<std::size_t>(c.id)].at(now).position;
            present.push_back(c.transform.m3.dist(truth));
            if (const auto d = client.transformAt(c.id, now))
                drawnErr.push_back(d->m3.dist(truth));
        }
        for (const auto& c : delayed.cars())
            past.push_back(c.transform.m3.dist(paths[static_cast<std::size_t>(c.id)].at(now).position));
    }
    std::printf("[ measure  ] same-moment error, m: predicted median %.3f 90%% %.3f 99%% %.3f max %.3f; "
                "drawn median %.3f 99%% %.3f; interpolated 160 ms back median %.3f 99%% %.3f\n",
                percentile(present, 0.5), percentile(present, 0.9), percentile(present, 0.99),
                percentile(present, 1.0), percentile(drawnErr, 0.5), percentile(drawnErr, 0.99),
                percentile(past, 0.5), percentile(past, 0.99));
    EXPECT_LT(percentile(present, 0.5), 0.05f);
    EXPECT_LT(percentile(present, 0.99), 0.6f);
    EXPECT_LT(percentile(drawnErr, 0.99), 0.8f);
    EXPECT_GT(percentile(past, 0.5), 1.0f);
}
