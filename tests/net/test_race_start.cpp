// The race start handshake on loopback sessions (docs/multiplayer.md, "Race
// start"; MM2's RaceReady / start messages, mmMultiRace::UpdateGame state 0):
// the host starts a race once every player still in the session has
// reported it loaded, at a session time every machine shares.
#include "net/Session.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <thread>

using namespace mm2;
using namespace mm2::net;

namespace {

using Clock = std::chrono::steady_clock;

SessionConfig config(std::uint32_t loadWaitMs = 60000) {
    SessionConfig c;
    c.connectTimeoutMs = 3000;
    c.joinTimeoutMs = 3000;
    c.snapshotRateHz = 50;
    c.interpolationDelayMs = 50;
    c.loadWaitMs = loadWaitMs;
    return c;
}

struct Peer {
    std::string name;
    std::unique_ptr<Session> session;
    std::vector<SessionEvent> events;
    std::optional<Clock::time_point> startedAt; // when GameStarted came out

    explicit Peer(std::string n, SessionConfig c = config()) : name(std::move(n)), session(new Session(c)) {}
    void pump() {
        session->update();
        for (auto& e : session->takeEvents()) {
            if (std::holds_alternative<ev::GameStarted>(e) && !startedAt)
                startedAt = Clock::now();
            events.push_back(std::move(e));
        }
    }
    template <class E>
    int count() const {
        int n = 0;
        for (const auto& e : events)
            n += std::holds_alternative<E>(e);
        return n;
    }
    template <class E>
    const E* last() const {
        for (auto it = events.rbegin(); it != events.rend(); ++it)
            if (const auto* p = std::get_if<E>(&*it))
                return p;
        return nullptr;
    }
    void clear() {
        events.clear();
        startedAt.reset();
    }
};

bool pumpUntil(const std::vector<Peer*>& peers, const std::function<bool()>& done, int timeoutMs = 3000) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (Clock::now() < deadline) {
        for (Peer* p : peers)
            p->pump();
        if (done())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

void settle(const std::vector<Peer*>& peers, int ms) { pumpUntil(peers, [] { return false; }, ms); }

bool allIn(const std::vector<Peer*>& peers, SessionPhase phase) {
    return std::ranges::all_of(peers, [&](Peer* p) { return p->session->phase() == phase; });
}

// A host and its joined, clock-synced clients.
struct Table {
    Peer host;
    std::vector<std::unique_ptr<Peer>> clients;

    explicit Table(int n, SessionConfig hostConfig = config()) : host("Host", hostConfig) {
        HostParams h;
        h.bind = Address::loopback(0);
        h.advertiseOnLan = false;
        h.player.name = "Host";
        EXPECT_TRUE(host.session->host(h));
        for (int i = 0; i < n; ++i) {
            auto c = std::make_unique<Peer>(std::string(1, static_cast<char>('A' + i)));
            JoinParams j;
            j.host = Address::loopback(host.session->port());
            j.player.name = c->name;
            EXPECT_TRUE(c->session->join(j));
            clients.push_back(std::move(c));
        }
        EXPECT_TRUE(pumpUntil(all(), [&] {
            return std::ranges::all_of(clients, [&](const auto& c) {
                return c->session->clockSynced() && c->session->players().size() == clients.size() + 1;
            });
        }));
        settle(all(), 300); // the clock sync burst
    }
    std::vector<Peer*> all() {
        std::vector<Peer*> v{&host};
        for (auto& c : clients)
            v.push_back(c.get());
        return v;
    }
    Peer& client(int i) { return *clients[static_cast<std::size_t>(i)]; }
    // GO DRIVE: every machine is loading.
    void order(std::uint32_t countdownMs = 0) {
        for (Peer* p : all())
            p->clear();
        host.session->startRace(countdownMs);
        EXPECT_TRUE(pumpUntil(all(), [&] { return allIn(all(), SessionPhase::Countdown); }));
    }
    bool anyStartKnown() {
        return std::ranges::any_of(all(), [](Peer* p) { return p->session->raceStartKnown(); });
    }
    bool everyStartKnown() {
        return std::ranges::all_of(all(), [](Peer* p) { return p->session->raceStartKnown(); });
    }
    void backToLobby() {
        host.session->returnToLobby();
        EXPECT_TRUE(pumpUntil(all(), [&] { return allIn(all(), SessionPhase::Lobby); }));
    }
};

} // namespace

// Three machines report in every order: nothing starts until the last report,
// then every machine has the host's start time, which lies the countdown and
// a lead ahead; every machine knows who has loaded.
TEST(RaceStart, StartsAfterTheLastReportInEveryOrder) {
    Table t(2);
    std::array<int, 3> order{0, 1, 2};
    std::uint32_t lastRace = 0;
    do {
        constexpr std::uint32_t kCountdown = 300;
        t.order(kCountdown);
        const auto peers = t.all();
        EXPECT_EQ(t.host.session->raceNumber(), lastRace + 1);
        for (Peer* p : peers)
            EXPECT_EQ(p->session->raceNumber(), t.host.session->raceNumber());
        lastRace = t.host.session->raceNumber();
        for (std::size_t k = 0; k < order.size(); ++k) {
            Peer& reporter = *peers[static_cast<std::size_t>(order[k])];
            reporter.session->reportLoaded();
            const std::uint8_t id = reporter.session->localId();
            ASSERT_TRUE(pumpUntil(peers, [&] {
                return std::ranges::all_of(peers, [&](Peer* p) { return p->session->playerLoaded(id); });
            })) << reporter.name;
            if (k + 1 < order.size()) {
                settle(peers, 60);
                EXPECT_FALSE(t.anyStartKnown()) << "started after " << k + 1 << " of 3 reports";
            }
        }
        const std::uint32_t reportedAt = t.host.session->time();
        ASSERT_TRUE(pumpUntil(peers, [&] { return t.everyStartKnown(); }));
        const std::uint32_t start = t.host.session->raceStartTime();
        for (Peer* p : peers) {
            EXPECT_EQ(p->session->raceStartTime(), start) << p->name;
            EXPECT_EQ(p->count<ev::RaceStartSet>(), 1) << p->name;
            EXPECT_EQ(p->count<ev::PlayerLoaded>(), 3) << p->name;
            EXPECT_EQ(p->session->phase(), SessionPhase::Countdown) << p->name;
        }
        // The countdown and a lead of 200 ms (loopback) from the last report.
        EXPECT_GE(start, reportedAt + kCountdown + 200 - 5);
        EXPECT_LE(start, reportedAt + kCountdown + 200 + 50);
        ASSERT_TRUE(pumpUntil(peers, [&] { return allIn(peers, SessionPhase::InGame); }));
        t.backToLobby();
        for (Peer* p : peers) {
            EXPECT_FALSE(p->session->raceStartKnown());
            EXPECT_FALSE(p->session->playerLoaded(p->session->localId()));
        }
    } while (std::next_permutation(order.begin(), order.end()));
    EXPECT_EQ(lastRace, 6u);
}

// Every machine starts the race at the same moment: the start is a session
// time, and each machine's clock follows the host's.
TEST(RaceStart, EveryMachineStartsAtTheSharedTime) {
    Table t(2);
    t.order(500);
    for (Peer* p : t.all())
        p->session->reportLoaded();
    ASSERT_TRUE(pumpUntil(t.all(), [&] {
        return std::ranges::all_of(t.all(), [](Peer* p) { return p->startedAt.has_value(); });
    }));
    for (auto& c : t.clients) {
        using Ms = std::chrono::duration<double, std::milli>;
        const double skew = Ms(*c->startedAt - *t.host.startedAt).count();
        EXPECT_LT(std::abs(skew), 25.0) << c->name << " started " << skew << " ms from the host";
    }
}

// A player who leaves (or is ejected) while the others wait for it no longer
// holds the start (MM2: SystemMessage 0x2d takes it off the count).
TEST(RaceStart, PlayerLeavingWhileTheOthersWaitDoesNotHoldTheStart) {
    Table t(3);
    t.order();
    t.host.session->reportLoaded();
    t.client(0).session->reportLoaded();
    settle(t.all(), 100);
    EXPECT_FALSE(t.anyStartKnown());
    // C is ejected, B leaves: the two who reported start.
    t.host.session->kick(t.client(2).session->localId(), "too slow");
    settle(t.all(), 100);
    EXPECT_FALSE(t.anyStartKnown()); // B is still loading
    t.client(1).session->leave();
    ASSERT_TRUE(pumpUntil({&t.host, &t.client(0)}, [&] {
        return t.host.session->raceStartKnown() && t.client(0).session->raceStartKnown();
    }));
    EXPECT_EQ(t.host.session->raceStartTime(), t.client(0).session->raceStartTime());
    EXPECT_EQ(t.host.session->players().size(), 2u);
}

// A player whose machine stops answering while the others wait is timed out
// by the host, and the start goes on without it.
TEST(RaceStart, PlayerTimingOutWhileTheOthersWaitDoesNotHoldTheStart) {
    SessionConfig hostConfig = config();
    hostConfig.transport.timeoutMinMs = 300;
    hostConfig.transport.timeoutMaxMs = 800;
    Table t(2, hostConfig);
    t.order();
    t.host.session->reportLoaded();
    t.client(0).session->reportLoaded();
    // B freezes: it is not serviced again.
    const std::vector<Peer*> alive{&t.host, &t.client(0)};
    ASSERT_TRUE(pumpUntil(alive, [&] { return t.host.session->raceStartKnown(); }, 6000));
    EXPECT_EQ(t.host.last<ev::PlayerLeft>()->reason, DisconnectReason::Timeout);
    ASSERT_TRUE(pumpUntil(alive, [&] { return t.client(0).session->raceStartKnown(); }));
}

// MM2 waits for every player; OpenMM2's host stops waiting after
// SessionConfig::loadWaitMs. The machine still loading has the start when it
// is ready, and its late report still tells the others it is in the race,
// without changing the start.
TEST(RaceStart, HostStartsWithoutPlayersStillLoadingAfterTheWait) {
    Table t(2, config(500));
    t.order(300);
    const auto ordered = Clock::now();
    t.host.session->reportLoaded();
    t.client(0).session->reportLoaded();
    ASSERT_TRUE(pumpUntil(t.all(), [&] { return t.everyStartKnown(); }));
    const double waited = std::chrono::duration<double, std::milli>(Clock::now() - ordered).count();
    EXPECT_GE(waited, 450.0);
    const std::uint32_t start = t.host.session->raceStartTime();
    // B, still loading, has the start too (its session is serviced while it loads).
    Peer& slow = t.client(1);
    EXPECT_EQ(slow.session->raceStartTime(), start);
    EXPECT_FALSE(slow.session->playerLoaded(slow.session->localId()));
    ASSERT_TRUE(pumpUntil(t.all(), [&] { return slow.session->phase() == SessionPhase::InGame; }));

    // B has loaded after the start.
    slow.session->reportLoaded();
    const std::uint8_t id = slow.session->localId();
    ASSERT_TRUE(pumpUntil(t.all(), [&] {
        return t.host.session->playerLoaded(id) && t.client(0).session->playerLoaded(id);
    }));
    settle(t.all(), 50);
    for (Peer* p : t.all()) {
        EXPECT_EQ(p->session->raceStartTime(), start);
        EXPECT_EQ(p->count<ev::RaceStartSet>(), 1);
    }
}

// A report sent for a race the host has already left (it went back to the
// lobby and ordered the next one meanwhile) does not count for the next
// race, and repeated reports count once.
TEST(RaceStart, ReportsForAnEarlierRaceAndRepeatsAreIgnored) {
    Table t(1);
    Peer& a = t.client(0);
    t.order();
    EXPECT_EQ(a.session->raceNumber(), 1u);
    // The host ends race 1 and orders race 2 before A has heard of either;
    // A reports race 1 loaded.
    t.host.session->returnToLobby();
    t.host.session->startRace(0);
    settle({&t.host}, 50);
    a.session->reportLoaded(); // still race 1 on A
    ASSERT_TRUE(pumpUntil(t.all(), [&] { return a.session->raceNumber() == 2; }));
    settle(t.all(), 100);
    EXPECT_FALSE(t.host.session->playerLoaded(a.session->localId()));
    t.host.session->reportLoaded();
    settle(t.all(), 100);
    EXPECT_FALSE(t.anyStartKnown()); // still waiting for A's report of race 2

    a.session->reportLoaded();
    a.session->reportLoaded(); // a repeat
    ASSERT_TRUE(pumpUntil(t.all(), [&] { return t.everyStartKnown(); }));
    settle(t.all(), 50);
    // Race 2: the host's and A's, once each, on both machines (A also has
    // its own report of race 1).
    auto loadedIn = [](const Peer& p, std::uint32_t race) {
        return std::ranges::count_if(p.events, [&](const SessionEvent& e) {
            const auto* l = std::get_if<ev::PlayerLoaded>(&e);
            return l && l->race == race;
        });
    };
    EXPECT_EQ(loadedIn(t.host, 1), 0);
    EXPECT_EQ(loadedIn(t.host, 2), 2);
    EXPECT_EQ(loadedIn(a, 2), 2);
}

// A host alone starts as soon as it has loaded.
TEST(RaceStart, HostAloneStartsAtOnce) {
    Table t(0);
    t.order();
    EXPECT_FALSE(t.host.session->raceStartKnown());
    t.host.session->reportLoaded();
    EXPECT_TRUE(t.host.session->raceStartKnown());
    t.host.pump();
    const auto* set = t.host.last<ev::RaceStartSet>();
    ASSERT_NE(set, nullptr);
    EXPECT_EQ(set->race, 1u);
}
