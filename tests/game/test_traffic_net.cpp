// The shared cruise traffic over a real session in one process: a host and a
// client NetGame on loopback, the host's cars chosen for the client
// (game::TrafficHost), sent on the Ambient channel and followed by the client
// (game::TrafficClient) at the interpolation delay; then the client's hit
// report back to the host.
#include "game/net/NetGame.h"
#include "game/net/TrafficSync.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <format>
#include <functional>
#include <thread>

using namespace mm2;
using game::NetGame;
using game::SharedCar;
using game::TrafficClient;
using game::TrafficHost;

namespace {

// Each test file has its own block of ports, all below 49152: Windows hands
// out and reserves ports in its dynamic range (49152-65535), where a bind can
// fail. Within a block, distinct ports per process for parallel ctest runs.
std::uint16_t trafficPort() {
    static const std::uint16_t port = static_cast<std::uint16_t>(
        45000 + (std::chrono::steady_clock::now().time_since_epoch().count() / 1000) % 2000 * 2);
    return port;
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

SharedCar movingCar(int id, net::AmbientKind kind, Vec3 pos) {
    SharedCar c;
    c.id = id;
    c.kind = kind;
    c.model = kind == net::AmbientKind::Police ? 3 : 1;
    c.transform = Mat34::identity();
    c.transform.m3 = pos;
    c.speed = 10.0f;
    c.velocity = {0, 0, -10}; // along -Z, the car's heading
    if (kind == net::AmbientKind::Police)
        c.flags = net::kAmbientSiren | net::kAmbientPursuit;
    return c;
}

} // namespace

TEST(SharedTrafficNet, HostAndClientInOneProcess) {
    game::NetOptions ho, co;
    ho.playerName = "Host";
    co.playerName = "Client";
    ho.port = trafficPort();
    ho.portMapping = co.portMapping = false;
    ho.discoveryPort = co.discoveryPort = static_cast<std::uint16_t>(trafficPort() + 1);
    NetGame host(ho), client(co);
    game::RaceConfig cruise;
    cruise.mode = game::GameMode::Cruise;
    cruise.city = "sf";
    std::string err;
    ASSERT_TRUE(host.host(cruise, {"Traffic", "", 4, false}, {"vpbug", 0, 0}, &err)) << err;
    ASSERT_TRUE(client.join(std::format("127.0.0.1:{}", trafficPort()), "", {"vpbug", 1, 0}, &err)) << err;
    ASSERT_TRUE(pump({&host, &client}, [&] { return client.phase() == NetGame::Phase::Lobby; }));
    EXPECT_TRUE(host.sharedTraffic()); // on by default
    EXPECT_TRUE(client.sharedTraffic());
    // In the lobby nothing is sent.
    EXPECT_EQ(host.sendAmbientState(client.localId(), net::AmbientStateMsg{}), 0u);
    host.startRace();
    ASSERT_TRUE(pump({&host, &client}, [&] {
        host.reportLoaded(); // nothing to load
        client.reportLoaded();
        return host.raceStarted() && client.raceStarted();
    }));

    // Two cars along -Z at 10 m/s and a cop chasing the client.
    TrafficHost th;
    TrafficClient tc(8, 0x1234);
    const std::uint32_t t0 = host.sessionTime();
    auto zAt = [&](double t) { return static_cast<float>(-10.0 * (t - t0) / 1000.0); };
    float worst = 0.0f;
    int compared = 0;
    std::uint32_t lastSent = 0;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
    while (std::chrono::steady_clock::now() < end) {
        const std::uint32_t t = host.sessionTime();
        if (t - lastSent >= 50) {
            lastSent = t;
            SharedCar cop = movingCar(400, net::AmbientKind::Police, {0, 0, zAt(t) + 15.0f});
            cop.target = client.localId();
            const std::vector<SharedCar> cars = {
                movingCar(3, net::AmbientKind::Traffic, {5, 0, zAt(t)}),
                movingCar(4, net::AmbientKind::Traffic, {-5, 0, zAt(t) - 20.0f}), cop};
            const auto msg = th.build({client.localId(), {0, 0, zAt(t)}}, cars, t, t / 33, 0x1234);
            EXPECT_GT(host.sendAmbientState(client.localId(), msg), 0u);
        }
        host.update();
        client.update();
        for (const auto& m : client.takeAmbientStates())
            tc.receive(m);
        const double render = static_cast<double>(client.sessionTime()) - tc.options().interpolationDelayMs;
        tc.update(render);
        for (const auto& c : tc.cars()) {
            const float expected = zAt(render) + (c.id == 4 ? -20.0f : c.id == 400 ? 15.0f : 0.0f);
            worst = std::max(worst, std::abs(c.transform.m3.z - expected));
            ++compared;
            if (c.id == 400) {
                EXPECT_EQ(c.target, client.localId());
                EXPECT_TRUE(c.flags & net::kAmbientSiren);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_GT(compared, 100);
    // The client's estimate of the host's clock and the milliseconds the
    // times are kept in: a few centimetres at 10 m/s.
    EXPECT_LT(worst, 0.35f);
    EXPECT_EQ(tc.known(), 3u);
    EXPECT_EQ(tc.stats().refused, 0u);

    // Protocol 12: at traffic density 1 a message is two of ENet's fragments
    // (sent unreliably), and arrives whole.
    {
        std::vector<SharedCar> many;
        for (int i = 0; i < 200; ++i) {
            const Vec3 at{static_cast<float>(i % 20) * 6.0f - 60.0f, 0.0f, static_cast<float>(i / 20) * 8.0f};
            many.push_back(movingCar(10 + i, net::AmbientKind::Traffic, at));
        }
        const std::uint32_t t = host.sessionTime();
        const auto msg = th.build({client.localId(), {0, 0, 0}}, many, t, t / 33, 0x1234);
        const std::size_t bytes = host.sendAmbientState(client.localId(), msg);
        EXPECT_GT(bytes, 1400u);
        EXPECT_LE(bytes, TrafficHost::Options{}.maxBytes);
        std::vector<net::AmbientStateMsg> got;
        ASSERT_TRUE(pump({&host, &client}, [&] {
            for (auto& m : client.takeAmbientStates())
                got.push_back(std::move(m));
            return !got.empty();
        }));
        EXPECT_EQ(got.back().entities.size(), msg.entities.size());
    }
    // ... and the police and knocked cars near the client in full, on the
    // State channel.
    net::TrafficFullMsg full;
    full.time = host.sessionTime();
    net::TrafficFullCar cop;
    cop.id = 400;
    cop.kind = net::AmbientKind::Police;
    cop.car.matrix.m3 = {0, 0, -15};
    cop.throttle = 0.75f;
    cop.steering = -0.25f;
    net::TrafficFullCar knocked;
    knocked.id = 3;
    knocked.generation = 1;
    knocked.kind = net::AmbientKind::Traffic;
    knocked.body.matrix.m3 = {5, 0, 2};
    knocked.body.linearVelocity = {1, 0, -2};
    full.cars = {cop, knocked};
    EXPECT_GT(host.sendTrafficFull(client.localId(), full), 0u);
    std::vector<net::TrafficFullMsg> fulls;
    ASSERT_TRUE(pump({&host, &client}, [&] {
        for (auto& m : client.takeTrafficFull())
            fulls.push_back(std::move(m));
        return !fulls.empty();
    }));
    ASSERT_EQ(fulls.size(), 1u);
    EXPECT_EQ(fulls[0].time, full.time);
    ASSERT_EQ(fulls[0].cars.size(), 2u);
    EXPECT_EQ(fulls[0].cars[0].kind, net::AmbientKind::Police);
    EXPECT_EQ(fulls[0].cars[0].throttle, 0.75f);
    EXPECT_EQ(fulls[0].cars[1].id, 3);
    EXPECT_EQ(fulls[0].cars[1].body.linearVelocity, knocked.body.linearVelocity);

    // Back in the lobby nothing more is sent.
    const std::uint32_t race = client.raceNumber();
    host.returnToLobby();
    ASSERT_TRUE(pump({&host, &client}, [&] { return client.backToLobby(race); }));
    EXPECT_EQ(host.sendAmbientState(client.localId(), net::AmbientStateMsg{}), 0u);
    EXPECT_EQ(host.sendTrafficFull(client.localId(), full), 0u);
}
