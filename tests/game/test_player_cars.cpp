// The host-simulated players' cars (game/net/PlayerCars): the per-sample
// driving, the host's input queue, the client's prediction and its
// corrections, run in one process with a simulated network between a
// "host" world and a "client" world.
#include "TestData.h"
#include "game/PlayerVehicle.h"
#include "game/net/PlayerCars.h"
#include "net/PlayerCars.h"
#include "phys/World.h"

#include <gtest/gtest.h>

#include <cmath>
#include <deque>
#include <memory>

using namespace mm2;
using namespace mm2::game;

namespace {

phys::PolygonSoup flatGround(float half = 3000.0f) {
    phys::SoupGeometry g;
    g.vertices = {{-half, 0, -half}, {-half, 0, half}, {half, 0, half}, {half, 0, -half}};
    g.polys.push_back({{0, 1, 2, 3}, 4, 0});
    g.materialNames = {"default"};
    phys::PolygonSoup soup;
    soup.add(g, Mat34::identity(), phys::MaterialTable{});
    soup.finalize(2048.0f);
    return soup;
}

// A player's input for sample `seq` of a test drive: accelerate, weave,
// brake, a gear key now and then.
net::CarInputFrame driveInput(std::uint32_t seq) {
    phys::PedalInput p;
    p.accelerator = (seq / 120) % 4 == 3 ? 0.0f : 1.0f;
    p.brake = (seq / 120) % 4 == 3 ? 0.7f : 0.0f;
    p.steering = std::sin(static_cast<float>(seq) * 0.02f) * 0.6f;
    net::CarInputFrame f = inputFrame(p);
    f.flags = net::kInputAutomatic | net::kInputAutoReverse;
    return f;
}

// One machine's world with one car.
struct Machine {
    phys::World world;
    std::unique_ptr<SimVehicle> car;
    NetCarDriver driver;
    explicit Machine(const vfs::Vfs& vfs) {
        world.setStatic(flatGround());
        std::string error;
        car = SimVehicle::loadPlayer(vfs, "vpbug", &error, false);
        EXPECT_TRUE(car) << error;
        car->sim().options.player = true;
        car->sim().setPolygonalBound(true);
        car->sim().ownRandom = true;
        car->sim().randomState = 1;
        car->addTo(world);
        driver.attach(*car);
    }
};

// A message on its way: due at a client sample count.
template <class M>
struct InFlight {
    std::uint32_t due = 0;
    M msg;
};

struct Outcome {
    int corrections = 0;
    int replayed = 0;
    float largest = 0.0f; // the largest correction's move (m)
    std::uint64_t missed = 0;
};

// The client drives `samples` samples; its messages reach the host
// `upLatency` samples later and the host's states come back `downLatency`
// samples after it sent them (every third sample). `lose` drops every n-th
// message of the client's (0 none).
Outcome simulate(const vfs::Vfs& vfs, int samples, std::uint32_t upLatency, std::uint32_t downLatency,
                 int lose = 0) {
    Machine host(vfs), client(vfs);
    const Mat34 start = Mat34::translation({0.0f, 0.6f, 0.0f});
    client.car->setResetPos(start);
    client.car->reset();
    CarPrediction prediction;
    HostInputQueue queue;
    bool placed = false;
    std::deque<InFlight<net::PlayerInputMsg>> up;
    std::deque<InFlight<std::pair<std::uint32_t, net::OwnCarState>>> down;
    Outcome run;
    const float dt = phys::kFixedSampleStep;
    for (std::uint32_t t = 1; t <= static_cast<std::uint32_t>(samples); ++t) {
        // Client: the host's states due by now, then its sample.
        while (!down.empty() && down.front().due <= t) {
            const auto& [ack, state] = down.front().msg;
            const auto c = prediction.acknowledge(*client.car, client.driver, client.world, ack, state);
            if (c.corrected) {
                ++run.corrections;
                run.replayed += c.replayed;
                run.largest = std::max(run.largest, c.moved.mag());
            }
            down.pop_front();
        }
        if (prediction.nextSeq() == 1)
            prediction.command(*client.car, {0, net::CarCommandKind::ResetTo, client.car->sim().resetPos(),
                                             client.car->sim().resetRotation});
        prediction.beginSample(*client.car, client.driver, driveInput(prediction.nextSeq()));
        client.world.step(dt);
        prediction.endSample(*client.car, client.driver);
        const bool lost = lose > 0 && t % static_cast<std::uint32_t>(lose) == 0;
        if (const auto m = prediction.message(); m && !lost)
            up.push_back({t + upLatency, *m});
        // Host: the inputs due by now, then its sample.
        while (!up.empty() && up.front().due <= t) {
            queue.receive(up.front().msg);
            up.pop_front();
        }
        if (!placed && queue.ready()) {
            if (const auto c = queue.placement()) {
                NetCarDriver::command(*host.car, *c);
                placed = true;
            }
        }
        if (placed) {
            if (auto next = queue.next()) {
                for (const auto& c : next->commands)
                    NetCarDriver::command(*host.car, c);
                host.driver.apply(*host.car, next->frame);
            }
        }
        host.world.step(dt);
        if (placed && t % 3 == 0 && queue.lastApplied() != 0)
            down.push_back({t + downLatency, {queue.lastApplied(), ownCarState(*host.car, 0)}});
    }
    run.missed = queue.missed();
    return run;
}

} // namespace

TEST(PlayerCars, InputFramesCarryTheRecordedPedalsExactly) {
    for (int s = -127; s <= 127; ++s) {
        phys::PedalInput p;
        p.steering = static_cast<float>(s) * 0.007874016f;
        p.accelerator = static_cast<float>((s + 127) % 256) * 0.003921569f;
        const phys::PedalInput back = pedalsOf(inputFrame(p));
        EXPECT_EQ(back.steering, p.steering);
        EXPECT_EQ(back.accelerator, p.accelerator);
    }
}

TEST(PlayerCars, TheHostQueueRepeatsThenCoastsAndSkipsLateInputs) {
    HostInputQueue q;
    EXPECT_FALSE(q.next()); // nothing before the first message
    net::PlayerInputMsg m;
    m.first = 1;
    net::CarInputFrame f;
    f.throttle = 200;
    f.events = net::kInputShiftUp;
    m.frames.assign(5, f);
    m.commands.push_back({3, net::CarCommandKind::Reset, {}, 0.0f});
    q.receive(m);
    EXPECT_TRUE(q.ready());
    // It starts at the first.
    auto n = q.next();
    ASSERT_TRUE(n);
    EXPECT_EQ(n->seq, 1u);
    EXPECT_TRUE(n->real);
    EXPECT_TRUE(n->commands.empty());
    EXPECT_EQ(q.lastApplied(), 1u);
    EXPECT_EQ(q.takeLeastWaiting(), 4);
    n = q.next();
    n = q.next();
    ASSERT_EQ(n->commands.size(), 1u); // the command for 3, with its sample
    n = q.next();
    n = q.next();
    EXPECT_EQ(n->seq, 5u);
    // Nothing more: the last input again without its keys, then coasting.
    for (int i = 0; i < HostInputQueue::kRepeatSamples; ++i) {
        n = q.next();
        EXPECT_FALSE(n->real);
        EXPECT_EQ(n->frame.throttle, 200);
        EXPECT_EQ(n->frame.events, 0);
    }
    n = q.next();
    EXPECT_EQ(n->frame.throttle, 0);
    EXPECT_EQ(q.missed(), static_cast<std::uint64_t>(HostInputQueue::kRepeatSamples) + 1);
    // Inputs for samples already applied change nothing; newer ones count.
    m.first = 6;
    q.receive(m); // 6..10: all but the current ones are past
    n = q.next();
    EXPECT_FALSE(n->real);
    m.first = n->seq + 1;
    q.receive(m);
    n = q.next();
    EXPECT_TRUE(n->real);
    EXPECT_TRUE(n->commands.empty()); // the command was carried out
}

TEST(PlayerCars, AnInputFarAheadIsIgnored) {
    HostInputQueue q;
    net::PlayerInputMsg m;
    m.first = 10;
    m.frames.resize(2);
    q.receive(m);
    m.first = 10 + HostInputQueue::kMaxAhead * 4;
    q.receive(m);
    EXPECT_LE(q.takeLeastWaiting(), 2);
}

TEST(PlayerCars, ACarPredictedOnTheSameInputsNeedsNoCorrection) {
    MM2_REQUIRE_GAME_DATA();
    // 10 s of driving with 100 ms of latency each way: after the start (the
    // host's car takes over at its first input) the prediction is the
    // host's simulation exactly.
    const Outcome run = simulate(*test::gameData(), 600, 6, 6);
    EXPECT_LE(run.corrections, 1);
    EXPECT_EQ(run.missed, 0u);
}

TEST(PlayerCars, LostInputsAreRepeatedFromTheNextMessage) {
    MM2_REQUIRE_GAME_DATA();
    // Every fourth message lost: the next one carries its inputs again.
    const Outcome run = simulate(*test::gameData(), 600, 6, 6, 4);
    EXPECT_LE(run.corrections, 1);
    EXPECT_EQ(run.missed, 0u);
}
