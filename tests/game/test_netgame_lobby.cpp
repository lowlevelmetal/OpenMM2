// The lobby and race flow of game::NetGame between a host and a client in
// one process (docs/review/multiplayer-lobby.md).
#include "game/net/NetGame.h"

#include <gtest/gtest.h>

#include <chrono>
#include <format>
#include <functional>
#include <thread>

using namespace mm2;
using game::NetGame;

namespace {

// Distinct ports per test process so parallel ctest runs do not collide.
std::uint16_t basePort() {
    static const std::uint16_t port = static_cast<std::uint16_t>(
        33000 + (std::chrono::steady_clock::now().time_since_epoch().count() / 1000) % 4000 * 2);
    return port;
}

game::NetOptions options(const std::string& name) {
    game::NetOptions o;
    o.playerName = name;
    o.port = basePort();
    o.portMapping = false; // never touch a real router from tests
    o.discoveryPort = static_cast<std::uint16_t>(basePort() + 1);
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

// Pumps for a while without a condition (lets messages in flight arrive).
void settle(std::initializer_list<NetGame*> games, int ms = 150) {
    pump(games, [] { return false; }, ms);
}

game::RaceConfig cruise() {
    game::RaceConfig c;
    c.mode = game::GameMode::Cruise;
    c.city = "sf";
    return c;
}

game::NetHostOptions hostOptions() {
    game::NetHostOptions h;
    h.sessionName = "Lobby test";
    h.advertiseOnLan = false;
    return h;
}

// A host and a joined, ready client in the lobby.
struct Lobby {
    NetGame host{options("Hosty")};
    NetGame client{options("Clienty")};

    void open() {
        std::string err;
        ASSERT_TRUE(host.host(cruise(), hostOptions(), {"vpbug", 0, 0}, &err)) << err;
        ASSERT_TRUE(client.join(std::format("127.0.0.1:{}", basePort()), "", {"vpcab", 0, 1}, &err)) << err;
        ASSERT_TRUE(pump({&host, &client}, [&] {
            return client.phase() == NetGame::Phase::Lobby && host.players().size() == 2;
        }));
        readyUp();
    }
    void readyUp() {
        client.setReady(true);
        ASSERT_TRUE(pump({&host, &client}, [&] { return host.everyoneReady(false); }));
    }
    // GO DRIVE: both machines see the race start once.
    void start() {
        host.startRace(200);
        bool h = false, c = false;
        ASSERT_TRUE(pump({&host, &client}, [&] {
            h = h || host.takeRaceStart();
            c = c || client.takeRaceStart();
            return h && c;
        }));
        ASSERT_TRUE(pump({&host, &client}, [&] { return host.raceStarted() && client.raceStarted(); }));
    }
};

} // namespace

// The maintainer's report: the second race of a session bounced everyone
// back to the lobby as soon as it had loaded. The host's own return to the
// lobby (RaceScreen::leaveRace) arrives as an event once the menus are up,
// where nothing took it, and the next race's first frame took it as its own.
TEST(NetGameLobby, ReturnToLobbyEndsOnlyItsOwnRace) {
    Lobby l;
    ASSERT_NO_FATAL_FAILURE(l.open());
    EXPECT_EQ(l.host.raceNumber(), 0u);
    EXPECT_FALSE(l.host.backToLobby(l.host.raceNumber())); // no race yet

    ASSERT_NO_FATAL_FAILURE(l.start());
    const std::uint32_t first = l.host.raceNumber();
    EXPECT_EQ(first, 1u);
    EXPECT_EQ(l.client.raceNumber(), 1u);
    EXPECT_FALSE(l.host.backToLobby(first));
    EXPECT_FALSE(l.client.backToLobby(first));

    // The host ends the race (a finish, or Quit to Lobby); the host's race
    // screen leaves without updating again, the menus update from now on.
    l.host.returnToLobby();
    ASSERT_TRUE(
        pump({&l.host, &l.client}, [&] { return l.host.backToLobby(first) && l.client.backToLobby(first); }));
    EXPECT_EQ(l.client.phase(), NetGame::Phase::Lobby);

    // In the lobby: both change cars, the host changes the settings, the
    // client gets ready again (the return cleared it).
    EXPECT_FALSE(l.host.everyoneReady(false));
    l.host.setLocalCar({"vpcop", 0, 0});
    l.client.setLocalCar({"vpmustang99", 1, 1});
    auto cfg = cruise();
    cfg.timeOfDay = game::TimeOfDay::Night;
    l.host.setRaceConfig(cfg);
    settle({&l.host, &l.client});
    ASSERT_NO_FATAL_FAILURE(l.readyUp());

    ASSERT_NO_FATAL_FAILURE(l.start());
    const std::uint32_t second = l.host.raceNumber();
    EXPECT_EQ(second, 2u);
    // The new race is not over on either machine...
    settle({&l.host, &l.client});
    EXPECT_FALSE(l.host.backToLobby(second));
    EXPECT_FALSE(l.client.backToLobby(l.client.raceNumber()));
    EXPECT_EQ(l.host.phase(), NetGame::Phase::Racing);
    EXPECT_EQ(l.client.phase(), NetGame::Phase::Racing);
    // ...while the first one still is.
    EXPECT_TRUE(l.host.backToLobby(first));
    EXPECT_EQ(l.client.raceConfig().vehicle, "vpmustang99");
    EXPECT_EQ(l.client.raceConfig().timeOfDay, game::TimeOfDay::Night);
}

// A client that quit the race early is in the menus when the host ends it:
// that return must not end the client's next race either.
TEST(NetGameLobby, ReturnSeenInTheMenusDoesNotEndTheNextRace) {
    Lobby l;
    ASSERT_NO_FATAL_FAILURE(l.open());
    ASSERT_NO_FATAL_FAILURE(l.start());
    // The client's race screen is gone (Quit to Race Menu); the host races on
    // and ends it later.
    l.host.returnToLobby();
    ASSERT_TRUE(pump({&l.host, &l.client}, [&] { return l.client.phase() == NetGame::Phase::Lobby; }));
    ASSERT_NO_FATAL_FAILURE(l.readyUp());
    ASSERT_NO_FATAL_FAILURE(l.start());
    settle({&l.host, &l.client});
    EXPECT_FALSE(l.client.backToLobby(l.client.raceNumber()));
}

// The host returns everyone and starts again before the client's race screen
// has updated: both messages arrive in one update. The old race is over, the
// new one is pending and not over.
TEST(NetGameLobby, ReturnAndNewCountdownInOneUpdate) {
    Lobby l;
    ASSERT_NO_FATAL_FAILURE(l.open());
    ASSERT_NO_FATAL_FAILURE(l.start());
    const std::uint32_t first = l.client.raceNumber();
    l.host.returnToLobby();
    l.host.startRace(500); // MultiAllReady is the lobby's check, not the session's
    // Only the host is serviced until both messages have left it.
    settle({&l.host}, 100);
    ASSERT_TRUE(pump({&l.client}, [&] { return l.client.raceNumber() == first + 1; }));
    EXPECT_TRUE(l.client.backToLobby(first)); // the race screen of the first race leaves
    EXPECT_TRUE(l.client.takeRaceStart());    // and the menus start the second
    EXPECT_FALSE(l.client.backToLobby(first + 1));
}

// Game events left over from a race (a finish sent while the host was
// already going back to the lobby) do not reach the next race.
TEST(NetGameLobby, StaleGameEventsAreDroppedAtTheNextCountdown) {
    Lobby l;
    ASSERT_NO_FATAL_FAILURE(l.open());
    ASSERT_NO_FATAL_FAILURE(l.start());
    l.host.returnToLobby();
    l.client.sendFinish(61000, 1); // sent before the client heard of the return
    ASSERT_TRUE(pump({&l.host, &l.client}, [&] { return l.client.phase() == NetGame::Phase::Lobby; }));
    settle({&l.host, &l.client});
    ASSERT_NO_FATAL_FAILURE(l.readyUp());
    ASSERT_NO_FATAL_FAILURE(l.start());
    EXPECT_TRUE(l.host.takeGameEvents().empty());
    // The new race's events come through.
    l.client.sendCheckpoint(1, 5000);
    std::vector<game::NetGameEvent> got;
    ASSERT_TRUE(pump({&l.host, &l.client}, [&] {
        for (auto& e : l.host.takeGameEvents())
            got.push_back(std::move(e));
        return !got.empty();
    }));
    EXPECT_EQ(got.front().type, net::GameEventType::CheckpointReached);
}

// The driver's transmission choice reaches the network race: MM2's session
// data carries none, and mmGame::Init sets the car's from the player's own
// state. It used to be automatic for everyone.
TEST(NetGameLobby, RaceKeepsTheDriversTransmission) {
    Lobby l;
    ASSERT_NO_FATAL_FAILURE(l.open());
    EXPECT_TRUE(l.client.raceConfig().automatic);
    game::NetCar manual = l.client.localCar();
    manual.automatic = false;
    l.client.setLocalCar(manual);
    EXPECT_FALSE(l.client.raceConfig().automatic);
    EXPECT_TRUE(l.host.raceConfig().automatic); // each machine its own
    l.host.setLocalCar({"vpcop", 0, 0, false});
    EXPECT_FALSE(l.host.raceConfig().automatic);
}
