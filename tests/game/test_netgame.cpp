#include "NetTestPorts.h"
#include "game/net/NetGame.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <functional>
#include <thread>

using namespace mm2;
using game::NetGame;

namespace {

// Each test file has its own block of ports, all below 49152: Windows hands
// out and reserves ports in its dynamic range (49152-65535), where a bind can
// fail. Within a block, distinct ports per process for parallel ctest runs.
std::uint16_t basePort() {
    static const std::uint16_t port = static_cast<std::uint16_t>(
        41000 + test::processPortSlot(2000) * 2);
    return port;
}

game::NetOptions options(const std::string& name, std::uint16_t offset) {
    game::NetOptions o;
    o.playerName = name;
    o.port = static_cast<std::uint16_t>(basePort() + offset);
    o.portMapping = false; // never touch a real router from tests
    o.discoveryPort = static_cast<std::uint16_t>(basePort() + 1);
    return o;
}

// Pumps both sides until `done` or the timeout.
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

game::RaceConfig circuitConfig() {
    game::RaceConfig c;
    c.mode = game::GameMode::Circuit;
    c.city = "sf";
    c.raceIndex = 3;
    c.laps = 5;
    c.timeOfDay = game::TimeOfDay::Night;
    c.weather = game::Weather::Fog;
    c.trafficDensity = 0.3f;
    c.pedestrianDensity = 0.7f;
    c.copDensity = 0.25f;
    c.difficulty = game::Difficulty::Professional;
    return c;
}

} // namespace

TEST(NetGame, SettingsRoundTrip) {
    const auto in = circuitConfig();
    const auto s = game::toSessionSettings(in, "Test session", 6);
    EXPECT_EQ(s.maxPlayers, 6);
    EXPECT_EQ(s.mode, net::GameMode::Circuit);
    const auto out = game::fromSessionSettings(s);
    EXPECT_TRUE(out.multiplayer);
    EXPECT_EQ(out.mode, in.mode);
    EXPECT_EQ(out.city, in.city);
    EXPECT_EQ(out.raceIndex, in.raceIndex);
    EXPECT_EQ(out.laps, in.laps);
    EXPECT_EQ(out.timeOfDay, in.timeOfDay);
    EXPECT_EQ(out.weather, game::Weather::Fog); // carried in the extras
    EXPECT_NEAR(out.trafficDensity, 0.3f, 0.01f);
    EXPECT_NEAR(out.pedestrianDensity, 0.7f, 0.01f);
    EXPECT_NEAR(out.copDensity, 0.25f, 0.01f);
    EXPECT_EQ(out.difficulty, game::Difficulty::Professional);

    game::RaceConfig cr;
    cr.mode = game::GameMode::CopsAndRobbers;
    cr.copsAndRobbers = game::CopsAndRobbersMode::RobberTeams;
    cr.timeLimitMinutes = 10;
    cr.pointLimit = 0;
    const auto back = game::fromSessionSettings(game::toSessionSettings(cr, "x", 8));
    EXPECT_EQ(back.mode, game::GameMode::CopsAndRobbers);
    EXPECT_EQ(back.copsAndRobbers, game::CopsAndRobbersMode::RobberTeams);
    EXPECT_EQ(back.timeLimitMinutes, 10.0f);
    EXPECT_EQ(back.raceIndex, -1);
}

TEST(NetGame, HostJoinChatReadyCountdownAndState) {
    NetGame host(options("Hosty", 0));
    NetGame client(options("Clienty", 0));
    std::string err;
    ASSERT_TRUE(host.host(circuitConfig(), {"Test session", "", 4, true}, {"vpbug", 1, 0}, &err)) << err;
    EXPECT_TRUE(host.isHost());
    EXPECT_EQ(host.phase(), NetGame::Phase::Lobby);
    EXPECT_NE(host.portMappingStatus().find("Port forwarding off"), std::string::npos);

    ASSERT_TRUE(client.join(std::format("127.0.0.1:{}", basePort()), "", {"vpcab", 0, 1}, &err)) << err;
    ASSERT_TRUE(pump({&host, &client}, [&] { return client.phase() == NetGame::Phase::Lobby && host.players().size() == 2; }));
    EXPECT_FALSE(client.isHost());
    EXPECT_EQ(client.players().size(), 2u);
    // The joiner sees the host's race.
    const auto rc = client.raceConfig();
    EXPECT_EQ(rc.mode, game::GameMode::Circuit);
    EXPECT_EQ(rc.city, "sf");
    EXPECT_EQ(rc.laps, 5);
    EXPECT_EQ(rc.vehicle, "vpcab");
    EXPECT_EQ(client.maxPlayers(), 4);

    // Chat both ways (each side sees both lines once).
    host.sendChat("hello from host");
    client.sendChat("hello from client");
    auto countChat = [](const NetGame& g) {
        int n = 0;
        for (const auto& l : g.chat())
            n += !l.system;
        return n;
    };
    ASSERT_TRUE(pump({&host, &client}, [&] { return countChat(host) == 2 && countChat(client) == 2; }));
    EXPECT_EQ(client.chat().back().name.empty(), false);

    // Car change and ready.
    EXPECT_FALSE(host.everyoneReady());
    client.setLocalCar({"vpdb7", 2, 0});
    client.setReady(true);
    ASSERT_TRUE(pump({&host, &client}, [&] { return host.everyoneReady(); }));
    const auto* remote = host.player(client.localId());
    ASSERT_NE(remote, nullptr);
    EXPECT_EQ(remote->car, "vpdb7");
    EXPECT_EQ(remote->color, 2);

    // Host changes settings; the client follows.
    auto cfg = circuitConfig();
    cfg.laps = 2;
    cfg.city = "london";
    host.setRaceConfig(cfg);
    ASSERT_TRUE(pump({&host, &client}, [&] { return client.raceConfig().laps == 2; }));
    EXPECT_EQ(client.raceConfig().city, "london");

    // GO DRIVE: both sides load the race; its start follows once both have
    // reported it loaded, at the same session time.
    host.startRace();
    ASSERT_TRUE(pump({&host, &client}, [&] { return client.phase() == NetGame::Phase::Countdown; }));
    EXPECT_TRUE(host.takeRaceStart());
    EXPECT_TRUE(client.takeRaceStart());
    EXPECT_FALSE(client.takeRaceStart()); // only once
    EXPECT_EQ(host.raceOrderTime(), client.raceOrderTime());
    EXPECT_FALSE(client.raceStartKnown());
    EXPECT_TRUE(std::isinf(client.secondsToStart()));
    host.reportLoaded();
    client.reportLoaded();
    ASSERT_TRUE(pump({&host, &client}, [&] { return client.raceStartKnown(); }));
    EXPECT_EQ(host.raceStartTime(), client.raceStartTime());
    EXPECT_GT(client.secondsToStart(), 0.0);
    ASSERT_TRUE(pump({&host, &client}, [&] { return host.raceStarted() && client.raceStarted(); }));
    EXPECT_EQ(client.phase(), NetGame::Phase::Racing);

    // Vehicle state: the client drives along +X; the host sees it move.
    Mat34 t = Mat34::rotationY(0.5f);
    for (int i = 0; i < 40; ++i) {
        t.m3 = {10.0f + i * 0.5f, 1.0f, -20.0f};
        net::VehicleControls ctl;
        ctl.throttle = 1.0f;
        ctl.gear = 2;
        client.submitLocalState(t, {25, 0, 0}, {0, 0.1f, 0}, ctl, 0.2f, net::kVehicleBrakeLights);
        host.update();
        client.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    ASSERT_TRUE(pump({&host, &client}, [&] {
        const auto cars = host.remoteCars();
        return cars.size() == 1 && cars[0].hasState && cars[0].transform.m3.x > 20.0f;
    }));
    const auto cars = host.remoteCars();
    EXPECT_EQ(cars[0].car.vehicle, "vpdb7");
    EXPECT_NEAR(cars[0].transform.m3.z, -20.0f, 0.05f);
    EXPECT_NEAR(cars[0].transform.m0.dot(t.m0), 1.0f, 1e-3f); // orientation survives
    EXPECT_NEAR(cars[0].damage, 0.2f, 0.01f);
    EXPECT_EQ(cars[0].controls.gear, 2);

    // Game events. A player's own word on the rules (a checkpoint, a
    // finish) is refused: the host decides them (protocol 10,
    // game/net/NetRules); other events come through.
    client.sendCheckpoint(3, 61234);
    client.sendFinish(123456, 1);
    client.sendCollision(net::kHostPlayerId, {1, 2, 3}, 4.0f);
    client.sendDamage(0.5f, net::kHostPlayerId);
    std::vector<game::NetGameEvent> got;
    ASSERT_TRUE(pump({&host, &client}, [&] {
        for (auto& e : host.takeGameEvents())
            got.push_back(std::move(e));
        return got.size() == 2;
    }));
    pump({&host, &client}, [] { return false; }, 150);
    for (auto& e : host.takeGameEvents())
        got.push_back(std::move(e));
    ASSERT_EQ(got.size(), 2u);
    EXPECT_EQ(got[0].type, net::GameEventType::Collision);
    ASSERT_TRUE(got[0].as<net::CollisionEvent>());
    EXPECT_FLOAT_EQ(got[0].as<net::CollisionEvent>()->impulse, 4.0f);
    EXPECT_EQ(got[1].as<net::DamageEvent>()->damage, 0.5f);
    EXPECT_EQ(got[1].from, client.localId());

    // Back to the lobby.
    const std::uint32_t race = client.raceNumber();
    EXPECT_EQ(race, 1u);
    EXPECT_FALSE(client.backToLobby(race));
    host.returnToLobby();
    ASSERT_TRUE(pump({&host, &client}, [&] { return client.backToLobby(race); }));
    EXPECT_EQ(client.phase(), NetGame::Phase::Lobby);

    // Host quits: the client is told.
    host.leave();
    ASSERT_TRUE(pump({&client}, [&] { return client.phase() == NetGame::Phase::Closed; }));
    const auto notice = client.takeNotice();
    ASSERT_TRUE(notice);
    EXPECT_EQ(*notice, "The Host has quit");
}

// The sessions page gives a renamed driver to the NetGame it already has
// (no new one): the next host or join uses the new name.
TEST(NetGame, ANameSetBeforeHostingOrJoiningIsUsed) {
    NetGame host(options("Old host", 4));
    NetGame client(options("Old client", 4));
    host.setPlayerName("New host");
    client.setPlayerName("New client");
    std::string err;
    ASSERT_TRUE(host.host(circuitConfig(), {"", "", 4, true}, {"vpbug", 1, 0}, &err)) << err;
    ASSERT_TRUE(client.join(std::format("127.0.0.1:{}", basePort() + 4), "", {"vpcab", 0, 1}, &err)) << err;
    ASSERT_TRUE(pump({&host, &client},
                     [&] { return client.phase() == NetGame::Phase::Lobby && host.players().size() == 2; }));
    auto nameOf = [](const NetGame& g, std::uint8_t id) {
        for (const auto& p : g.players())
            if (p.id == id)
                return p.name;
        return std::string("?");
    };
    EXPECT_EQ(nameOf(host, host.localId()), "New host");
    EXPECT_EQ(nameOf(host, client.localId()), "New client");
    EXPECT_EQ(nameOf(client, host.localId()), "New host");
}

TEST(NetGame, LanDiscoveryPasswordAndKick) {
    NetGame host(options("Hosty", 2));
    NetGame browser(options("Browser", 2));
    std::string err;
    game::RaceConfig cfg;
    cfg.mode = game::GameMode::CopsAndRobbers;
    cfg.copsAndRobbers = game::CopsAndRobbersMode::CopsVsRobbers;
    ASSERT_TRUE(host.host(cfg, {"Secret game", "letmein", 8, true}, {"vpbug", 0, 0}, &err)) << err;

    ASSERT_TRUE(browser.startLanScan(&err)) << err;
    std::vector<net::DiscoveredSession> found;
    ASSERT_TRUE(pump({&host, &browser}, [&] {
        found = browser.lanSessions();
        return std::ranges::any_of(found, [](const auto& s) { return s.advert.sessionName == "Secret game"; });
    }, 6000));
    const auto it = std::ranges::find_if(found, [](const auto& s) { return s.advert.sessionName == "Secret game"; });
    EXPECT_TRUE(it->advert.hasPassword);
    EXPECT_EQ(it->advert.mode, net::GameMode::CopsAndRobbers);
    browser.stopLanScan();

    // Wrong password is refused with a notice.
    ASSERT_TRUE(browser.join(it->address, "nope", {"vpbug", 0, 1}, &err)) << err;
    ASSERT_TRUE(pump({&host, &browser}, [&] { return browser.phase() == NetGame::Phase::Closed; }));
    EXPECT_TRUE(browser.takeNotice());

    // Right password works; Cops vs. Robbers assigns cars by team.
    ASSERT_TRUE(browser.join(it->address, "letmein", {"vpbug", 0, 1}, &err)) << err;
    ASSERT_TRUE(pump({&host, &browser}, [&] { return browser.phase() == NetGame::Phase::Lobby; }));
    EXPECT_EQ(browser.raceConfig().vehicle, "vpmustang99"); // robber
    EXPECT_EQ(host.raceConfig().vehicle, "vpcop");           // cop

    // Eject.
    host.kick(browser.localId());
    ASSERT_TRUE(pump({&host, &browser}, [&] { return browser.phase() == NetGame::Phase::Closed; }));
    EXPECT_EQ(browser.takeNotice().value_or(""), "You have been ejected");
    ASSERT_TRUE(pump({&host}, [&] { return host.players().size() == 1; }));
}
