// The water and the fall in a network race (OpenMM2, protocol 14): mmGame::
// Update's checks run on a car's samples (game::NetCarDriver), on the host
// for every car it simulates; the car's own machine predicts them and the
// host's states confirm or correct them. A client cannot put its own car
// back (game::ResetRules).
#include "TestData.h"
#include "game/PlayerVehicle.h"
#include "game/net/PlayerCars.h"
#include "net/PlayerCarState.h"
#include "phys/World.h"

#include <gtest/gtest.h>

#include <cmath>
#include <deque>
#include <memory>
#include <optional>

using namespace mm2;
using namespace mm2::game;

namespace {

constexpr float kDt = phys::kFixedSampleStep;

phys::PolygonSoup flatGround() {
    constexpr float half = 3000.0f;
    phys::SoupGeometry g;
    g.vertices = {{-half, 0, -half}, {-half, 0, half}, {half, 0, half}, {half, 0, -half}};
    g.polys.push_back({{0, 1, 2, 3}, 4, 0});
    g.materialNames = {"default"};
    phys::PolygonSoup soup;
    soup.add(g, Mat34::identity(), phys::MaterialTable{});
    soup.finalize(2048.0f);
    return soup;
}

// One machine's world with one car on flat ground, all of it under water
// at `level` (a water room, mmGame::Update's vehSplash test).
struct Machine {
    phys::World world;
    std::unique_ptr<SimVehicle> car;
    NetCarDriver driver;
    Machine(const vfs::Vfs& vfs, std::optional<float> level) {
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
        car->setResetPos(Mat34::translation({0.0f, 0.6f, 0.0f}));
        car->reset();
        car->sim().setWaterLevel(level);
    }
};

const net::CarCommand kReset{0, net::CarCommandKind::Reset, {}, 0.0f};

net::CarInputFrame throttle(float amount) {
    phys::PedalInput p;
    p.accelerator = amount;
    net::CarInputFrame f = inputFrame(p);
    f.flags = net::kInputAutomatic;
    return f;
}

} // namespace

TEST(NetWater, TheHandlerPutsTheCarBackAfterFiveSecondsInTheWater) {
    MM2_REQUIRE_GAME_DATA();
    Machine m(*test::gameData(), 5.0f);
    NetCarDriver idle = m.driver; // no handler: a car this machine only follows
    m.driver.setWaterHandler(kReset);
    std::vector<int> resets;
    for (int t = 1; t <= 700; ++t) {
        const std::uint32_t before = m.driver.waterResets();
        m.driver.apply(*m.car, throttle(0.5f));
        if (m.driver.waterResets() != before) {
            resets.push_back(t);
            // mmPlayer::Reset: back at the reset position, out of the water
            // until the next sample finds it under the level again.
            EXPECT_FALSE(m.car->sim().splash.active());
            EXPECT_EQ(m.driver.waterTime(), 0.0f);
            EXPECT_LT(m.car->sim().body.ics.matrix.m3.dist2(m.car->sim().resetPos()), 0.01f);
        }
        m.world.step(kDt);
    }
    // The splash latches in the first sample; the next 300 samples are 5 s
    // (in 32-bit steps of 1/60 the sum passes 5 at the 301st).
    ASSERT_EQ(resets.size(), 2u);
    EXPECT_GE(resets[0], 300);
    EXPECT_LE(resets[0], 303);
    EXPECT_EQ(resets[1] - resets[0], resets[0] - 1);

    // No handler: never checked.
    Machine n(*test::gameData(), 5.0f);
    for (int t = 1; t <= 400; ++t) {
        idle.apply(*n.car, throttle(0.5f));
        n.world.step(kDt);
    }
    EXPECT_EQ(idle.waterResets(), 0u);
    EXPECT_TRUE(n.car->sim().splash.active());
}

TEST(NetWater, BelowTheCityTheHandlerRunsAtOnce) {
    MM2_REQUIRE_GAME_DATA();
    Machine m(*test::gameData(), std::nullopt);
    // mmGameMulti::HitWaterHandler in a race: at the last checkpoint cleared,
    // facing its heading.
    const Vec3 checkpoint{120.0f, 3.0f, -40.0f};
    m.driver.setWaterHandler(net::CarCommand{0, net::CarCommandKind::RespawnAt, checkpoint, 1.25f});
    for (int t = 0; t < 30; ++t) {
        m.driver.apply(*m.car, throttle(0.0f));
        m.world.step(kDt);
    }
    EXPECT_EQ(m.driver.waterResets(), 0u);
    m.car->sim().body.ics.matrix.m3.y = -50.5f; // mmGame::Update's -50 m
    m.driver.apply(*m.car, throttle(0.0f));
    EXPECT_EQ(m.driver.waterResets(), 1u);
    EXPECT_GT(m.car->sim().body.ics.matrix.m3.y, 0.0f);
    EXPECT_LT(std::abs(m.car->sim().body.ics.matrix.m3.x - checkpoint.x), 1.0f);
    EXPECT_LT(std::abs(m.car->sim().body.ics.matrix.m3.z - checkpoint.z), 1.0f);
}

namespace {

struct WaterRun {
    std::vector<std::uint32_t> clientResets; // the client's samples its prediction put the car back at
    std::vector<std::uint32_t> hostResets;   // the client's input numbers the host did at
    int corrections = 0;
    float finalDistance = 0.0f; // the client's car against the host's after its last sample
    std::uint64_t refused = 0;
};

// A client drives its car in the water, its inputs reaching the host
// `latency` samples later and the host's states coming back as late, every
// third sample. `predict`: the client runs the water's handler on its own
// car; `cheatAt`: it asks the host to put its car back at that sample.
WaterRun waterRace(const vfs::Vfs& vfs, int samples, std::uint32_t latency, bool predict,
                   std::uint32_t cheatAt = 0) {
    // (The host's car stays out of the water until the client's placement
    // puts it in the race.)
    Machine host(vfs, std::nullopt), client(vfs, 5.0f);
    host.driver.setWaterHandler(kReset);
    if (predict)
        client.driver.setWaterHandler(kReset);
    CarPrediction prediction;
    HostInputQueue queue;
    bool placed = false;
    std::uint32_t lastMove = 0;
    std::deque<std::pair<std::uint32_t, net::PlayerInputMsg>> up;
    std::deque<std::pair<std::uint32_t, std::pair<std::uint32_t, net::OwnCarState>>> down;
    WaterRun run;
    for (std::uint32_t t = 1; t <= static_cast<std::uint32_t>(samples); ++t) {
        while (!down.empty() && down.front().first <= t) {
            const auto& [ack, state] = down.front().second;
            if (prediction.acknowledge(*client.car, client.driver, client.world, ack, state).corrected)
                ++run.corrections;
            down.pop_front();
        }
        if (prediction.nextSeq() == 1)
            prediction.command(*client.car, {0, net::CarCommandKind::ResetTo, client.car->sim().resetPos(),
                                             client.car->sim().resetRotation});
        if (cheatAt != 0 && prediction.nextSeq() == cheatAt)
            prediction.command(*client.car, kReset);
        const std::uint32_t seq = prediction.nextSeq();
        const std::uint32_t before = client.driver.waterResets();
        prediction.beginSample(*client.car, client.driver, throttle(0.4f));
        if (client.driver.waterResets() != before)
            run.clientResets.push_back(seq);
        client.world.step(kDt);
        prediction.endSample(*client.car, client.driver);
        if (const auto m = prediction.message())
            up.emplace_back(t + latency, *m);
        while (!up.empty() && up.front().first <= t) {
            queue.receive(up.front().second);
            up.pop_front();
        }
        if (!placed && queue.ready())
            if (const auto c = queue.placement()) {
                NetCarDriver::command(*host.car, *c);
                host.car->sim().setWaterLevel(5.0f);
                lastMove = c->seq;
                placed = true;
            }
        if (placed)
            if (auto next = queue.next()) {
                for (const auto& c : next->commands) {
                    if (c.kind == net::CarCommandKind::ResetTo && c.seq == lastMove)
                        continue;
                    ResetRules r;
                    r.placed = true;
                    r.lastMoveSeq = lastMove;
                    r.city = {{-3000.0f, -100.0f, -3000.0f}, {3000.0f, 300.0f, 3000.0f}};
                    if (!r.allows(c)) {
                        ++run.refused;
                        continue;
                    }
                    NetCarDriver::command(*host.car, c);
                }
                const std::uint32_t hostBefore = host.driver.waterResets();
                host.driver.apply(*host.car, next->frame);
                if (host.driver.waterResets() != hostBefore)
                    run.hostResets.push_back(next->seq);
            }
        host.world.step(kDt);
        if (placed && t % 3 == 0 && queue.lastApplied() != 0)
            down.emplace_back(t + latency,
                              std::pair{queue.lastApplied(), ownCarState(*host.car, 0, &host.driver)});
    }
    // The host runs the client's last inputs: both cars at the same sample.
    for (const auto& [due, m] : up)
        queue.receive(m);
    while (queue.lastApplied() + 1 < prediction.nextSeq()) {
        auto next = queue.next();
        if (!next)
            break;
        host.driver.apply(*host.car, next->frame);
        host.world.step(kDt);
    }
    run.finalDistance =
        std::sqrt(client.car->sim().body.ics.matrix.m3.dist2(host.car->sim().body.ics.matrix.m3));
    return run;
}

} // namespace

TEST(NetWater, AClientPredictsTheWaterAsTheHostDecidesIt) {
    MM2_REQUIRE_GAME_DATA();
    // The same simulation on both: the client's car goes back at the very
    // samples the host's does, and nothing is corrected.
    const WaterRun run = waterRace(*test::gameData(), 900, 6, true);
    ASSERT_EQ(run.hostResets.size(), 2u);
    EXPECT_EQ(run.clientResets, run.hostResets);
    EXPECT_EQ(run.corrections, 0);
    EXPECT_LT(run.finalDistance, 0.01f);
}

TEST(NetWater, TheHostPutsBackACarWhoseClientDoesNot) {
    MM2_REQUIRE_GAME_DATA();
    // A client that leaves its car in the water (no handler of its own) is
    // put back by the host's states all the same.
    const WaterRun run = waterRace(*test::gameData(), 900, 6, false);
    ASSERT_EQ(run.hostResets.size(), 2u);
    EXPECT_TRUE(run.clientResets.empty());
    EXPECT_GE(run.corrections, 2);
    EXPECT_LT(run.finalDistance, 0.01f);
}

TEST(NetWater, AClientCannotPutItsOwnCarBack) {
    MM2_REQUIRE_GAME_DATA();
    // A client asking for a reset two seconds into the water: refused; the
    // host's states take its car back into the water, and the water's own
    // handler runs at the host's time.
    const WaterRun honest = waterRace(*test::gameData(), 900, 6, true);
    const WaterRun cheat = waterRace(*test::gameData(), 900, 6, true, 120);
    EXPECT_EQ(cheat.refused, 1u);
    EXPECT_EQ(cheat.hostResets, honest.hostResets);
    EXPECT_GE(cheat.corrections, 1);
    EXPECT_LT(cheat.finalDistance, 0.01f);
}
