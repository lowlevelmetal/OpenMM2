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

// Two players' cars in one world, the host's (car `a`, player 0) first, as
// every machine orders them.
struct TwoCars {
    phys::World world;
    std::unique_ptr<SimVehicle> a, b;
    NetCarDriver da, db;
    explicit TwoCars(const vfs::Vfs& vfs) {
        world.setStatic(flatGround());
        for (auto* car : {&a, &b}) {
            std::string error;
            *car = SimVehicle::loadPlayer(vfs, "vpbug", &error, false);
            EXPECT_TRUE(*car) << error;
            (*car)->sim().options.player = true;
            (*car)->sim().setPolygonalBound(true);
            (*car)->sim().ownRandom = true;
            (*car)->sim().randomState = 1;
            (*car)->addTo(world);
        }
        da.attach(*a);
        db.attach(*b);
    }
};

// The host's car's input: steady at `throttle` until sample `brakeAt`, then
// braking hard.
net::CarInputFrame leaderInput(std::uint32_t t, float throttle, std::uint32_t brakeAt) {
    phys::PedalInput p;
    p.accelerator = t < brakeAt ? throttle : 0.0f;
    p.brake = t < brakeAt ? 0.0f : 1.0f;
    net::CarInputFrame f = inputFrame(p);
    f.flags = net::kInputAutomatic;
    return f;
}

struct ShuntOutcome {
    int corrections = 0;
    float largest = 0.0f;
    float closest = 1e9f; // the cars' centres on the host (m)
};

// A client's car shunts the host's from behind: both start in line, the
// client's 9 m behind at full throttle, the host's at `throttle` (braking
// from sample `brakeAt`). The client simulates the host's car with its own
// from the host's full states (companion) when `companion`, and otherwise
// meets it held where the host's newest state put it (the host's car not
// simulated: a body that does not give way).
ShuntOutcome shunt(const vfs::Vfs& vfs, bool companion, float throttle, std::uint32_t brakeAt) {
    TwoCars host(vfs), client(vfs);
    const Mat34 front = Mat34::translation({0.0f, 0.6f, 0.0f});
    const Mat34 back = Mat34::translation({0.0f, 0.6f, 9.0f}); // facing -Z: behind
    for (TwoCars* m : {&host, &client}) {
        m->a->setResetPos(front);
        m->a->reset();
        m->b->setResetPos(back);
        m->b->reset();
    }
    if (!companion) {
        client.a->sim().body.kinematic = true;
        client.a->sim().body.resetCollider();
    }
    CarPrediction prediction;
    HostInputQueue queue;
    struct Down {
        std::uint32_t ack = 0;
        net::OwnCarState own;
        net::NearCarState near;
    };
    std::deque<InFlight<net::PlayerInputMsg>> up;
    std::deque<InFlight<Down>> down;
    ShuntOutcome run;
    const float dt = phys::kFixedSampleStep;
    const std::uint32_t latency = 6;
    net::CarInputFrame leader = leaderInput(0, throttle, brakeAt);
    for (std::uint32_t t = 1; t <= 300; ++t) {
        while (!down.empty() && down.front().due <= t) {
            const Down& d = down.front().msg;
            CarPrediction::Companion c{client.a.get(), &client.da, &d.near.state, d.near.input, true};
            const auto r = companion
                               ? prediction.acknowledge(*client.b, client.db, client.world, d.ack, d.own, {},
                                                        {}, std::span(&c, 1))
                               : prediction.acknowledge(*client.b, client.db, client.world, d.ack, d.own);
            if (!companion) {
                // The host's car where its newest state says, held there.
                applyOwnCarState(*client.a, d.near.state);
            } else {
                leader = d.near.input;
            }
            if (r.corrected) {
                ++run.corrections;
                run.largest = std::max(run.largest, r.moved.mag());
            }
            down.pop_front();
        }
        if (prediction.nextSeq() == 1)
            prediction.command(*client.b, {0, net::CarCommandKind::ResetTo, client.b->sim().resetPos(),
                                           client.b->sim().resetRotation});
        if (companion)
            client.da.apply(*client.a, leader);
        phys::PedalInput full;
        full.accelerator = 1.0f;
        net::CarInputFrame mine = inputFrame(full);
        mine.flags = net::kInputAutomatic;
        prediction.beginSample(*client.b, client.db, mine);
        client.world.step(dt);
        prediction.endSample(*client.b, client.db);
        if (const auto m = prediction.message())
            up.push_back({t + latency, *m});
        // Host.
        while (!up.empty() && up.front().due <= t) {
            queue.receive(up.front().msg);
            up.pop_front();
        }
        host.da.apply(*host.a, leaderInput(t, throttle, brakeAt));
        if (auto next = queue.next()) {
            for (const auto& c : next->commands)
                NetCarDriver::command(*host.b, c);
            host.db.apply(*host.b, next->frame);
        }
        host.world.step(dt);
        const Vec3& pa = host.a->sim().body.ics.matrix.m3;
        run.closest = std::min(run.closest, pa.dist(host.b->sim().body.ics.matrix.m3));
        if (t % 3 == 0 && queue.lastApplied() != 0) {
            Down d;
            d.ack = queue.lastApplied();
            d.own = ownCarState(*host.b, 0);
            d.near.id = 0;
            d.near.state = ownCarState(*host.a, 0);
            d.near.input = leaderInput(t, throttle, brakeAt);
            down.push_back({t + latency, d});
        }
    }
    return run;
}

} // namespace

TEST(PlayerCars, AShuntPredictedWithTheOtherCarAsTheHostRunsIt) {
    MM2_REQUIRE_GAME_DATA();
    // The host's car at a steady pace, the client's catching it up and
    // pushing it: simulating the host's car with its own from the host's
    // states and in the host's order, the client predicts the shunt as the
    // host runs it.
    const ShuntOutcome with = shunt(*test::gameData(), true, 0.3f, 1000);
    std::printf("[ measure  ] shunt with the other car simulated: %d corrections, largest %.3f m, "
                "closest %.2f m\n",
                with.corrections, static_cast<double>(with.largest), static_cast<double>(with.closest));
    EXPECT_LT(with.closest, 4.6f); // they touched
    EXPECT_LE(with.corrections, 1);
    EXPECT_LT(with.largest, 0.05f);
    // Meeting the host's car as a body that does not give way, it is
    // corrected through the shunt.
    const ShuntOutcome without = shunt(*test::gameData(), false, 0.3f, 1000);
    std::printf("[ measure  ] shunt with the other car held: %d corrections, largest %.3f m\n",
                without.corrections, static_cast<double>(without.largest));
    EXPECT_GT(without.corrections, with.corrections + 5);
}

TEST(PlayerCars, AShuntIsCorrectedOnlyWhereTheOtherPlayerChangedItsInput) {
    MM2_REQUIRE_GAME_DATA();
    // The host's car brakes hard while the client's pushes it: the client
    // learns of it a trip late and is corrected, by less than a metre.
    const ShuntOutcome run = shunt(*test::gameData(), true, 0.3f, 200);
    std::printf("[ measure  ] shunt with the host's car braking: %d corrections, largest %.3f m\n",
                run.corrections, static_cast<double>(run.largest));
    EXPECT_GE(run.corrections, 1);
    EXPECT_LT(run.largest, 1.0f);
}

TEST(PlayerCars, TheHostResetsAClientsCarOnlyWhereTheRulesDo) {
    ResetRules r;
    r.city = {{-2000.0f, -100.0f, -2000.0f}, {2000.0f, 300.0f, 2000.0f}};
    const net::CarCommand placeAt{1, net::CarCommandKind::ResetTo, {10.0f, 2.0f, 30.0f}, 0.5f};
    // The start: once, in the city.
    EXPECT_TRUE(r.allows(placeAt));
    net::CarCommand far = placeAt;
    far.position.x = 9000.0f;
    EXPECT_FALSE(r.allows(far));
    r.placed = true;
    r.lastMoveSeq = 1;
    EXPECT_FALSE(r.allows({600, net::CarCommandKind::ResetTo, {10.0f, 2.0f, 30.0f}, 0.5f}));
    // Back to the reset position or at a checkpoint: never (the water and
    // the fall are the host's own, NetCarDriver::setWaterHandler).
    const net::CarCommand reset{600, net::CarCommandKind::Reset, {}, 0.0f};
    EXPECT_FALSE(r.allows(reset));
    EXPECT_FALSE(r.allows({600, net::CarCommandKind::RespawnAt, {100.0f, 5.0f, 200.0f}, 1.0f}));
    // Repairs: a wreck's only (Cops and Robbers' at a delivery is the host's).
    const net::CarCommand repair{600, net::CarCommandKind::ClearDamage, {}, 0.0f};
    EXPECT_FALSE(r.allows(repair));
    r.wrecked = true;
    EXPECT_TRUE(r.allows(repair));
    // The development hook lets any reset in the city through, not four times
    // a second.
    ResetRules d = r;
    d.debug = true;
    d.wrecked = false;
    EXPECT_TRUE(d.allows(reset));
    EXPECT_TRUE(d.allows({600, net::CarCommandKind::RespawnAt, {0.0f, 1.0f, 0.0f}, 0.0f}));
    EXPECT_FALSE(d.allows({600, net::CarCommandKind::RespawnAt, {0.0f, 1.0f, 9000.0f}, 0.0f}));
    d.lastMoveSeq = 595;
    EXPECT_FALSE(d.allows(reset));
}

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

TEST(PlayerCars, AQueueFarBehindItsClientCatchesUp) {
    // A backlog (the client drove while the host was loading): a second
    // later the queue drops all but the newest few.
    HostInputQueue q;
    net::PlayerInputMsg m;
    m.first = 1;
    m.frames.resize(net::kMaxInputFrames);
    q.receive(m);
    std::uint32_t newest = net::kMaxInputFrames;
    for (int i = 0; i < HostInputQueue::kSlackSamples; ++i) {
        ASSERT_TRUE(q.next());
        m.first = ++newest; // the client keeps sending one a sample
        m.frames.resize(1);
        q.receive(m);
    }
    EXPECT_GT(q.skipped(), 0u);
    EXPECT_EQ(q.lastApplied() + HostInputQueue::kStartMargin + 1, newest); // before the last one came
    EXPECT_EQ(q.missed(), 0u);
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
