// A network race's start on each machine (game::NetRaceStart, MM2's
// mmMultiRace / mmMultiCircuit / mmMultiBlitz::UpdateGame state 0) and the
// handshake through game::NetGame (docs/multiplayer.md, "Race start").
#include "NetTestPorts.h"
#include "game/Strings.h"
#include "game/net/NetGame.h"
#include "game/net/RaceStart.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <format>
#include <functional>
#include <optional>
#include <thread>

using namespace mm2;
using game::NetGame;
using game::NetRaceStart;

namespace {

using Clock = std::chrono::steady_clock;
constexpr float kFrame = 1.0f / 60.0f;

// Each test file has its own block of ports, all below 49152 (Windows'
// dynamic range) and below Linux's ephemeral range; within the block,
// distinct ports per process for parallel ctest runs.
std::uint16_t basePort() {
    static const std::uint16_t port = static_cast<std::uint16_t>(
        17000 + test::processPortSlot(1500) * 2);
    return port;
}

game::NetOptions options(const std::string& name, std::uint32_t loadWaitMs = 60000) {
    game::NetOptions o;
    o.playerName = name;
    o.port = basePort();
    o.portMapping = false; // never touch a real router from tests
    o.discoveryPort = static_cast<std::uint16_t>(basePort() + 1);
    o.loadWaitMs = loadWaitMs;
    return o;
}

bool pump(std::initializer_list<NetGame*> games, const std::function<bool()>& done, int timeoutMs = 4000) {
    const auto end = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (Clock::now() < end) {
        for (NetGame* g : games)
            g->update();
        if (done())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

game::RaceConfig raceIn(game::GameMode mode) {
    game::RaceConfig c;
    c.mode = mode;
    c.city = "sf";
    c.raceIndex = mode == game::GameMode::Circuit ? 0 : -1;
    return c;
}

// A host and a joined client, ordered to load a race.
struct Pair {
    NetGame host;
    NetGame client;

    explicit Pair(game::GameMode mode, std::uint32_t loadWaitMs = 60000)
        : host(options("Hosty", loadWaitMs)), client(options("Clienty")) {
        std::string err;
        game::NetHostOptions h;
        h.sessionName = "Start test";
        h.advertiseOnLan = false;
        EXPECT_TRUE(host.host(raceIn(mode), h, {"vpbug", 0, 0}, &err)) << err;
        EXPECT_TRUE(client.join(std::format("127.0.0.1:{}", basePort()), "", {"vpcab", 0, 1}, &err)) << err;
        EXPECT_TRUE(pump({&host, &client}, [&] {
            return client.phase() == NetGame::Phase::Lobby && host.players().size() == 2;
        }));
        pump({&host, &client}, [] { return false; }, 400); // the clock sync burst
        host.startRace();
        EXPECT_TRUE(pump({&host, &client}, [&] { return client.phase() == NetGame::Phase::Countdown; }));
        EXPECT_TRUE(host.takeRaceStart());
        EXPECT_TRUE(client.takeRaceStart());
    }
};

// One machine's race after it has loaded: its NetRaceStart once a frame.
struct Machine {
    NetGame& net;
    NetRaceStart start;
    std::optional<Clock::time_point> loadedAt; // the race screen is running from here
    int maxWaitingFor = 0;
    std::optional<double> countdownAt, goAt; // session times
    std::optional<double> beforeCountdown, beforeGo; // the frames before them
    std::optional<double> lastFrame;
    bool everReleasedEarly = false;

    Machine(NetGame& n, game::GameMode mode) : net(n), start(NetRaceStart::kindOf(mode)) {}
    void frame(float dt) {
        if (!loadedAt || Clock::now() < *loadedAt)
            return;
        const auto out = start.update(dt, net);
        maxWaitingFor = std::max(maxWaitingFor, out.waitingFor);
        if (out.countdownBegan) {
            countdownAt = net.frameTime();
            beforeCountdown = lastFrame;
        }
        if (out.went) {
            goAt = net.frameTime();
            beforeGo = lastFrame;
        }
        if (!out.held && !start.gone())
            everReleasedEarly = true;
        lastFrame = net.frameTime();
    }
    // `at` is the first frame of this machine at or after `time` (within the
    // float seconds NetRaceStart counts in), however late the frames come.
    static void expectFirstFrameAt(const std::optional<double>& at, const std::optional<double>& before,
                                   double time) {
        ASSERT_TRUE(at.has_value());
        EXPECT_GE(*at, time - 1.0);
        if (before) {
            EXPECT_LT(*before, time + 1.0);
        }
    }
};

// Runs both machines at about 60 frames a second until `done`.
bool run(Machine& a, Machine& b, const std::function<bool()>& done, int timeoutMs) {
    const auto end = Clock::now() + std::chrono::milliseconds(timeoutMs);
    auto last = Clock::now();
    while (Clock::now() < end) {
        a.net.update();
        b.net.update();
        const auto now = Clock::now();
        const float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        a.frame(dt);
        b.frame(dt);
        if (done())
            return true;
        std::this_thread::sleep_for(std::chrono::microseconds(16600));
    }
    return false;
}

} // namespace

// --- NetRaceStart on its own -----------------------------------------------------------

// mmMultiRace::UpdateGame state 0: 5 s of frames held and silent, RaceReady,
// then "Waiting for N players" while others have not reported.
TEST(NetRaceStart, RaceReportsAfterFiveSecondsThenWaits) {
    NetRaceStart s(NetRaceStart::Kind::Race);
    NetRaceStart::Input in;
    in.othersLoading = 2;
    float t = 0.0f;
    int reports = 0;
    while (t < 5.0f) {
        const auto out = s.update(kFrame, in);
        reports += out.report;
        EXPECT_TRUE(out.held);
        EXPECT_EQ(out.waitingFor, 0) << "nothing is shown before RaceReady";
        EXPECT_FALSE(out.secondsToGo.has_value());
        t += kFrame;
    }
    EXPECT_EQ(reports, 0);
    auto out = s.update(kFrame, in); // +0x408 is past 5.0 now
    for (int i = 0; i < 2 && !out.report; ++i)
        out = s.update(kFrame, in);
    EXPECT_TRUE(out.report);
    EXPECT_EQ(out.waitingFor, 2);
    in.othersLoading = 1;
    out = s.update(kFrame, in);
    EXPECT_FALSE(out.report); // once
    EXPECT_EQ(out.waitingFor, 1);
    in.othersLoading = 0;
    out = s.update(kFrame, in);
    EXPECT_EQ(out.waitingFor, 0);
    EXPECT_TRUE(out.held);
}

// The countdown follows the shared start: Ready... from 2.5 s before it, the
// car released at it.
TEST(NetRaceStart, CountdownFollowsTheSharedStart) {
    NetRaceStart s(NetRaceStart::Kind::CopsAndRobbers);
    EXPECT_EQ(NetRaceStart::countdownSeconds(NetRaceStart::Kind::Race), 2.5f);
    EXPECT_EQ(NetRaceStart::countdownSeconds(NetRaceStart::Kind::CopsAndRobbers), 0.0f);
    NetRaceStart r(NetRaceStart::Kind::Race);
    NetRaceStart::Input in;
    in.startKnown = true; // the start came during the settle: it goes first
    in.secondsToStart = 2.7;
    auto out = r.update(kFrame, in);
    EXPECT_TRUE(out.report);
    ASSERT_TRUE(out.secondsToGo);
    EXPECT_FLOAT_EQ(*out.secondsToGo, 2.7f);
    EXPECT_FALSE(out.countdownBegan);
    EXPECT_TRUE(out.held);
    in.secondsToStart = 2.49;
    out = r.update(kFrame, in);
    EXPECT_TRUE(out.countdownBegan);
    in.secondsToStart = 0.001;
    out = r.update(kFrame, in);
    EXPECT_TRUE(out.held);
    EXPECT_FALSE(out.went);
    in.secondsToStart = -0.002;
    out = r.update(kFrame, in);
    EXPECT_FALSE(out.held);
    EXPECT_TRUE(out.went);
    out = r.update(kFrame, in);
    EXPECT_FALSE(out.went); // once
    EXPECT_FALSE(r.ownCountdown());
}

// A machine loaded after the start (the host stopped waiting for it) counts
// the whole countdown on its own, as MM2 does with a start message that
// waited in its queue (0x20f in state 0).
TEST(NetRaceStart, LoadedAfterTheStartCountsDownOnItsOwn) {
    NetRaceStart r(NetRaceStart::Kind::Race);
    NetRaceStart::Input in;
    in.startKnown = true;
    in.secondsToStart = -12.0;
    auto out = r.update(kFrame, in);
    EXPECT_TRUE(out.report);
    EXPECT_TRUE(r.ownCountdown());
    ASSERT_TRUE(out.secondsToGo);
    EXPECT_FLOAT_EQ(*out.secondsToGo, 2.5f);
    EXPECT_TRUE(out.countdownBegan);
    EXPECT_TRUE(out.held);
    int frames = 1;
    while (out.held && frames < 1000) {
        in.secondsToStart -= kFrame;
        out = r.update(kFrame, in);
        ++frames;
    }
    EXPECT_TRUE(out.went);
    EXPECT_NEAR(static_cast<float>(frames - 1) * kFrame, 2.5f, 1.5f * kFrame);

    // Cops and Robbers has no countdown: it goes at once.
    NetRaceStart cr(NetRaceStart::Kind::CopsAndRobbers);
    out = cr.update(kFrame, in);
    EXPECT_TRUE(out.report);
    EXPECT_TRUE(out.went);
    EXPECT_FALSE(out.held);
}

// mmMultiCR::Init and mmMultiRoam::Init report at the end of the loading;
// a cruise is never held, Cops and Robbers until the start.
TEST(NetRaceStart, CruiseAndCopsAndRobbersReportAtOnce) {
    NetRaceStart cruise(NetRaceStart::Kind::Cruise);
    NetRaceStart cr(NetRaceStart::Kind::CopsAndRobbers);
    NetRaceStart::Input in;
    in.othersLoading = 1;
    auto a = cruise.update(kFrame, in);
    auto b = cr.update(kFrame, in);
    EXPECT_TRUE(a.report);
    EXPECT_FALSE(a.held);
    EXPECT_EQ(a.waitingFor, 0);
    EXPECT_TRUE(b.report);
    EXPECT_TRUE(b.held);
    EXPECT_EQ(b.waitingFor, 1);
    EXPECT_EQ(NetRaceStart::kindOf(game::GameMode::Cruise), NetRaceStart::Kind::Cruise);
    EXPECT_EQ(NetRaceStart::kindOf(game::GameMode::CopsAndRobbers), NetRaceStart::Kind::CopsAndRobbers);
    for (auto m : {game::GameMode::Blitz, game::GameMode::Circuit, game::GameMode::Checkpoint})
        EXPECT_EQ(NetRaceStart::kindOf(m), NetRaceStart::Kind::Race);
}

// Strings 31-37, "Waiting for 1 player" ... "Waiting for 7 players".
TEST(NetRaceStart, WaitingText) {
    const auto none = game::Strings::fromTable({});
    EXPECT_EQ(NetRaceStart::waitingText(none, 1), "Waiting for 1 player");
    EXPECT_EQ(NetRaceStart::waitingText(none, 3), "Waiting for 3 players");
    EXPECT_EQ(NetRaceStart::waitingText(none, 12), "Waiting for 12 players");
    data::PeStringTable table;
    table.strings[31] = "One left";
    table.strings[37] = "Seven left";
    table.strings[38] = "has left the game";
    const auto strings = game::Strings::fromTable(table);
    EXPECT_EQ(NetRaceStart::waitingText(strings, 1), "One left");
    EXPECT_EQ(NetRaceStart::waitingText(strings, 7), "Seven left");
    EXPECT_EQ(NetRaceStart::waitingText(strings, 8), "Waiting for 8 players");
}

// --- Through the network ---------------------------------------------------------------

// A circuit where the client takes a second longer to load: the host waits
// (after its own 5 s it shows "Waiting for 1 player"), and once both have
// reported both count down from the same session time and go together.
TEST(NetGameStart, SlowLoaderIsWaitedForAndBothGoTogether) {
    Pair p(game::GameMode::Circuit);
    Machine host(p.host, game::GameMode::Circuit), client(p.client, game::GameMode::Circuit);
    host.loadedAt = Clock::now();
    client.loadedAt = Clock::now() + std::chrono::milliseconds(1200);
    ASSERT_TRUE(run(host, client, [&] { return host.goAt && client.goAt; }, 15000));
    EXPECT_EQ(host.maxWaitingFor, 1);
    EXPECT_EQ(client.maxWaitingFor, 0); // the host had reported long before
    EXPECT_EQ(p.host.raceStartTime(), p.client.raceStartTime());
    ASSERT_TRUE(host.countdownAt && client.countdownAt);
    // Each machine counts down and goes in its first frame at or after the
    // shared times, on its own session clock (a frame's length under load
    // would not fit a fixed tolerance).
    const double start = p.host.raceStartTime();
    for (const Machine* m : {&host, &client}) {
        Machine::expectFirstFrameAt(m->goAt, m->beforeGo, start);
        Machine::expectFirstFrameAt(m->countdownAt, m->beforeCountdown, start - 2500.0);
    }
    EXPECT_FALSE(host.everReleasedEarly);
    EXPECT_FALSE(client.everReleasedEarly);
}

// A player whose loading never ends: the host starts without it after its
// wait; when it does load it joins the running race on its own countdown,
// and its car shows on the host only from its report (MM2 activates a
// network car on its RaceReady).
TEST(NetGameStart, PlayerStillLoadingAfterTheWaitJoinsTheRunningRace) {
    Pair p(game::GameMode::CopsAndRobbers, 800);
    Machine host(p.host, game::GameMode::CopsAndRobbers), client(p.client, game::GameMode::CopsAndRobbers);
    host.loadedAt = Clock::now();
    // The client is "loading": its race sends nothing yet. The host waits.
    ASSERT_TRUE(run(host, client, [&] { return host.goAt.has_value(); }, 4000));
    EXPECT_FALSE(p.host.playerLoaded(p.client.localId()));
    EXPECT_TRUE(p.client.raceStartKnown()); // its session hears of the start while it loads
    // A state from the client before its report is not shown.
    net::VehicleControls ctl;
    p.client.submitLocalState(Mat34::rotationY(0.0f), {}, {}, ctl, 0.0f, 0);
    pump({&p.host, &p.client}, [] { return false; }, 300);
    auto cars = p.host.remoteCars();
    ASSERT_EQ(cars.size(), 1u);
    EXPECT_FALSE(cars[0].hasState);

    client.loadedAt = Clock::now();
    ASSERT_TRUE(run(host, client, [&] { return client.goAt.has_value(); }, 3000));
    EXPECT_TRUE(client.start.ownCountdown());
    EXPECT_GT(*client.goAt, static_cast<double>(p.client.raceStartTime()));
    ASSERT_TRUE(pump({&p.host, &p.client}, [&] {
        p.client.submitLocalState(Mat34::rotationY(0.0f), {}, {}, ctl, 0.0f, 0);
        cars = p.host.remoteCars();
        return p.host.playerLoaded(p.client.localId()) && cars.size() == 1 && cars[0].hasState;
    }));
}

// A joiner who quits the race (or cannot load it) before reporting no longer
// holds the others' start (MM2: SystemMessage 0x2d in state 0).
TEST(NetGameStart, PlayerLeavingTheRaceReleasesTheStart) {
    Pair p(game::GameMode::CopsAndRobbers);
    p.host.reportLoaded();
    pump({&p.host, &p.client}, [] { return false; }, 200);
    EXPECT_FALSE(p.host.raceStartKnown());
    EXPECT_EQ(p.host.playersLoading(), 1);
    p.client.sendLeftRace();
    ASSERT_TRUE(pump({&p.host, &p.client}, [&] { return p.host.raceStartKnown(); }));
    EXPECT_EQ(p.host.playersLoading(), 0);
}

// Either machine may report first; the start comes with the second report
// and is the same on both, race after race.
TEST(NetGameStart, EitherMachineMayReportFirst) {
    Pair p(game::GameMode::Checkpoint);
    for (int round = 0; round < 2; ++round) {
        NetGame& first = round == 0 ? p.client : p.host;
        NetGame& second = round == 0 ? p.host : p.client;
        first.reportLoaded();
        ASSERT_TRUE(pump({&p.host, &p.client}, [&] {
            return p.host.playerLoaded(first.localId()) && p.client.playerLoaded(first.localId());
        }));
        pump({&p.host, &p.client}, [] { return false; }, 100);
        EXPECT_FALSE(p.host.raceStartKnown());
        EXPECT_FALSE(p.client.raceStartKnown());
        EXPECT_EQ(second.playersLoading(), 0);
        EXPECT_EQ(first.playersLoading(), 1);
        second.reportLoaded();
        ASSERT_TRUE(
            pump({&p.host, &p.client}, [&] { return p.host.raceStarted() && p.client.raceStarted(); }));
        EXPECT_EQ(p.host.raceStartTime(), p.client.raceStartTime());
        EXPECT_EQ(p.host.raceNumber(), p.client.raceNumber());
        // The countdown and a lead after the second report.
        EXPECT_GE(p.host.raceStartTime(), p.host.raceOrderTime() + 2500u + 200u);
        if (round == 0) {
            p.host.returnToLobby();
            ASSERT_TRUE(
                pump({&p.host, &p.client}, [&] { return p.client.phase() == NetGame::Phase::Lobby; }));
            p.host.startRace();
            ASSERT_TRUE(pump({&p.host, &p.client}, [&] { return p.client.takeRaceStart(); }));
            EXPECT_TRUE(p.host.takeRaceStart());
        }
    }
}

// The client reports the race it is loading just as the host has gone back
// to the lobby and ordered the next one: that report does not count for the
// next race, whose start waits for the client's report of it.
TEST(NetGameStart, AReportForAnEarlierRaceDoesNotCount) {
    Pair p(game::GameMode::Checkpoint);
    const std::uint32_t first = p.client.raceNumber();
    p.host.returnToLobby();
    p.host.startRace();
    p.client.reportLoaded(); // still race `first` here
    ASSERT_TRUE(pump({&p.host, &p.client}, [&] { return p.client.raceNumber() == first + 1; }));
    pump({&p.host, &p.client}, [] { return false; }, 150);
    EXPECT_FALSE(p.host.playerLoaded(p.client.localId()));
    EXPECT_TRUE(p.client.backToLobby(first));
    EXPECT_TRUE(p.client.takeRaceStart());
    p.host.reportLoaded();
    pump({&p.host, &p.client}, [] { return false; }, 150);
    EXPECT_FALSE(p.host.raceStartKnown());
    EXPECT_EQ(p.client.playersLoading(), 0);
    p.client.reportLoaded();
    ASSERT_TRUE(pump({&p.host, &p.client}, [&] { return p.client.raceStartKnown(); }));
    EXPECT_EQ(p.host.raceStartTime(), p.client.raceStartTime());
}
