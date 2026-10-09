// NetGame's replication timing: a car's state carries the time the
// simulation was at, and remote cars are sampled with the simulation's lag.
#include "game/net/NetGame.h"

#include <gtest/gtest.h>

#include <chrono>
#include <format>
#include <functional>
#include <thread>

using namespace mm2;
using game::NetGame;

namespace {

// Each test file has its own block of ports, all below 49152: Windows hands
// out and reserves ports in its dynamic range (49152-65535), where a bind can
// fail. Within a block, distinct ports per process for parallel ctest runs.
std::uint16_t syncPort() {
    static const std::uint16_t port = static_cast<std::uint16_t>(
        37000 + (std::chrono::steady_clock::now().time_since_epoch().count() / 1000) % 2000 * 2);
    return port;
}

game::NetOptions options(const std::string& name) {
    game::NetOptions o;
    o.playerName = name;
    o.port = syncPort();
    o.portMapping = false;
    o.discoveryPort = static_cast<std::uint16_t>(syncPort() + 1);
    return o;
}

bool pump(std::initializer_list<NetGame*> games, const std::function<bool()>& done, int timeoutMs = 4000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < end) {
        for (NetGame* g : games)
            g->update();
        if (done())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

} // namespace

TEST(NetGameSync, StatesCarryTheSimulationTimeAndCarsAreSampledWithItsLag) {
    NetGame host(options("Host"));
    NetGame client(options("Client"));
    game::RaceConfig cruise;
    std::string err;
    ASSERT_TRUE(host.host(cruise, {"Sync", "", 2, false}, {"vpbug", 0, 0}, &err)) << err;
    ASSERT_TRUE(client.join(std::format("127.0.0.1:{}", syncPort()), "", {"vpbug", 0, 0}, &err)) << err;
    ASSERT_TRUE(pump({&host, &client}, [&] { return host.players().size() == 2; }));
    pump({&host, &client}, [] { return false; }, 400); // clock sync
    host.startRace(200);
    ASSERT_TRUE(pump({&host, &client}, [&] { return host.raceStarted() && client.raceStarted(); }));

    // The client's simulation is 10 ms behind each frame; its car runs along
    // +X at 10 m/s, at x = time / 100 of the session time the state belongs
    // to. Stamped with the frame's own time instead, it would be drawn 10 cm
    // off.
    constexpr double kLag = 10.0;
    auto drive = [&] {
        const double simulated = client.frameTime() - kLag;
        Mat34 m = Mat34::identity();
        m.m3 = {static_cast<float>(simulated / 100.0), 0, 0};
        client.submitLocalState(m, {10, 0, 0}, {}, {}, 0.0f, 0, kLag);
    };
    ASSERT_TRUE(pump({&host, &client}, [&] {
        drive();
        const auto cars = host.remoteCars();
        return cars.size() == 1 && cars[0].hasState && !cars[0].stale && cars[0].transform.m3.x > 1.0f;
    }));
    for (int i = 0; i < 20; ++i) {
        pump({&host, &client}, [&] {
            drive();
            return true;
        });
        const auto cars = host.remoteCars();
        ASSERT_EQ(cars.size(), 1u);
        if (cars[0].stale)
            continue;
        EXPECT_NEAR(cars[0].transform.m3.x, cars[0].time / 100.0, 0.01);
        // A simulation 5 ms behind the frame samples the car 5 ms earlier.
        const auto lagged = host.remoteCars(5.0);
        EXPECT_DOUBLE_EQ(lagged[0].time, cars[0].time - 5.0);
        // Shown at least the least playout delay in the past.
        EXPECT_GE(host.frameTime() - cars[0].time, 50.0 - 1e-9);
    }
}
