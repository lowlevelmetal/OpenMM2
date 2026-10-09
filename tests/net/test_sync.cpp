// In-race synchronisation: snapshot time stamps, the playout delay, the
// client's session clock, and what a session takes in during a race.
#include "net/ClockSync.h"
#include "net/Session.h"
#include "net/Snapshot.h"

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <thread>

using namespace mm2;
using namespace mm2::net;

namespace {

VehicleSnapshot at(std::uint32_t timeMs, float x = 0.0f) {
    VehicleSnapshot s;
    s.time = timeMs;
    s.position = {x, 0, 0};
    return s;
}

// Snapshots every 50 ms from t = 1000, each arriving `late` ms after its stamp.
void feed(SnapshotBuffer& b, int count, double late, std::uint32_t start = 1000, std::uint32_t every = 50) {
    for (int i = 0; i < count; ++i) {
        const auto t = start + static_cast<std::uint32_t>(i) * every;
        b.push(at(t), t + late);
    }
}

} // namespace

TEST(PlayoutDelay, CoversLatenessPlusTheGap) {
    SnapshotBuffer b;
    EXPECT_LT(b.requiredDelay(), 0.0); // nothing measured yet
    feed(b, 20, 80.0);
    // Until the next snapshot arrives the newest one is 50 + 80 ms old.
    EXPECT_DOUBLE_EQ(b.requiredDelay(), 130.0);
}

TEST(PlayoutDelay, FollowsTheLatestSnapshotAndForgetsOldOnes) {
    SnapshotBuffer b;
    feed(b, 20, 80.0);
    // One snapshot 150 ms late raises what is needed...
    b.push(at(2000), 2150.0);
    EXPECT_DOUBLE_EQ(b.requiredDelay(), 150.0 + 50.0);
    // ...until it is older than the window.
    for (std::uint32_t t = 2050; t <= 2200 + SnapshotBuffer::kDelayWindowMs; t += 50)
        b.push(at(t), t + 30.0);
    EXPECT_DOUBLE_EQ(b.requiredDelay(), 80.0);
}

TEST(PlayoutDelay, ALostSnapshotIsBridgedNotWaitedFor) {
    SnapshotBuffer b;
    feed(b, 19, 40.0); // up to 1900
    // 1950 never arrives: the 100 ms gap counts as 1.5 times the usual 50.
    b.push(at(2000), 2040.0);
    EXPECT_DOUBLE_EQ(b.requiredDelay(), 40.0 + 75.0);
}

TEST(PlayoutDelay, OutOfOrderSnapshotsDoNotCount) {
    SnapshotBuffer b;
    feed(b, 10, 20.0); // up to 1450
    b.push(at(1425), 1900.0); // an old one, very late: nothing waited for it
    EXPECT_DOUBLE_EQ(b.requiredDelay(), 70.0);
    EXPECT_EQ(b.size(), 11u);
}

TEST(SlewedClock, StepsOutsideARaceAndSlewsInOne) {
    SlewedClock c;
    EXPECT_FALSE(c.valid());
    c.update(0.0, 1000.0, true); // the first estimate is taken at once
    EXPECT_DOUBLE_EQ(c.offset(), 1000.0);
    c.update(100.0, 1040.0, false); // lobby: at once
    EXPECT_DOUBLE_EQ(c.offset(), 1040.0);
    // Racing: 20 ms of correction at 5% takes 400 ms; from update to update
    // the clock never runs backwards and never jumps.
    c.update(200.0, 1020.0, true);
    EXPECT_DOUBLE_EQ(c.offset(), 1035.0); // 100 ms at 5%
    double shownBefore = 200.0 + c.offset();
    for (double t = 210.0; t <= 700.0; t += 10.0) {
        c.update(t, 1020.0, true);
        const double shown = t + c.offset();
        EXPECT_GT(shown, shownBefore);
        EXPECT_LE(shownBefore + 10.0 - shown, 0.5 + 1e-9);
        shownBefore = shown;
    }
    EXPECT_DOUBLE_EQ(c.offset(), 1020.0);
    // A huge error is stepped even in a race.
    c.update(710.0, 5000.0, true);
    EXPECT_DOUBLE_EQ(c.offset(), 5000.0);
}

TEST(ClockSync, KeepsSubMillisecondPrecision) {
    ClockSync c;
    c.addSample(10.25, 1000.5, 10.75);
    EXPECT_DOUBLE_EQ(c.rtt(), 0.5);
    EXPECT_DOUBLE_EQ(c.offset(), 1000.5 + 0.25 - 10.75);
}

// --- Sessions on loopback -----------------------------------------------------------------

namespace {

SessionConfig syncConfig() {
    SessionConfig c;
    c.connectTimeoutMs = 3000;
    c.joinTimeoutMs = 3000;
    c.snapshotRateHz = 50;
    return c;
}

struct Peer {
    std::unique_ptr<Session> session = std::make_unique<Session>(syncConfig());
    std::vector<SessionEvent> events;
    void pump() {
        session->update();
        for (auto& e : session->takeEvents())
            events.push_back(std::move(e));
    }
    template <class E>
    bool has() const {
        for (const auto& e : events)
            if (std::holds_alternative<E>(e))
                return true;
        return false;
    }
};

bool pumpUntil(std::vector<Peer*> peers, const std::function<bool()>& done, int timeoutMs = 3000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        for (Peer* p : peers)
            p->pump();
        if (done())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

void pumpFor(std::vector<Peer*> peers, int ms) { pumpUntil(std::move(peers), [] { return false; }, ms); }

// A host and one client, joined and clock-synced.
void connect(Peer& host, Peer& client) {
    HostParams h;
    h.bind = Address::loopback(0);
    h.advertiseOnLan = false;
    h.player.name = "Host";
    ASSERT_TRUE(host.session->host(h));
    JoinParams j;
    j.host = Address::loopback(host.session->port());
    j.player.name = "Client";
    ASSERT_TRUE(client.session->join(j));
    ASSERT_TRUE(pumpUntil({&host, &client}, [&] { return client.has<ev::JoinAccepted>(); }));
    pumpFor({&host, &client}, 400); // the clock sync burst
}

void startRace(Peer& host, Peer& client) {
    host.session->startCountdown(100);
    ASSERT_TRUE(pumpUntil({&host, &client}, [&] { return client.has<ev::GameStarted>(); }));
}

} // namespace

TEST(SessionSync, StatesCarryTheTimeTheyWereSimulatedAt) {
    Peer host, client;
    connect(host, client);
    startRace(host, client);
    // The host's car as it stood 12.5 ms before now.
    const double simulated = host.session->timeMs() - 12.5;
    VehicleSnapshot s;
    s.position = {3, 0, 0};
    host.session->submitLocalState(s, simulated);
    VehicleSnapshot seen;
    ASSERT_TRUE(pumpUntil({&host, &client}, [&] {
        return client.session->sampleRemoteAt(0, simulated, seen) != SnapshotBuffer::Result::Empty;
    }));
    EXPECT_EQ(seen.time, static_cast<std::uint32_t>(std::llround(simulated)));
}

TEST(SessionSync, NothingIsReplicatedInTheLobby) {
    Peer host, client;
    connect(host, client);
    const std::uint8_t id = client.session->localId();
    VehicleSnapshot s;
    client.session->submitLocalState(s);
    host.session->submitLocalState(s);
    pumpFor({&host, &client}, 200);
    VehicleSnapshot out;
    EXPECT_EQ(host.session->sampleRemote(id, out), SnapshotBuffer::Result::Empty);
    EXPECT_EQ(client.session->sampleRemote(0, out), SnapshotBuffer::Result::Empty);
}

TEST(SessionSync, StatesStampedFarFromTheClockAreDropped) {
    Peer host, client;
    connect(host, client);
    startRace(host, client);
    const std::uint8_t id = client.session->localId();
    VehicleSnapshot s;
    // Ten seconds ahead: neither taken in nor relayed.
    client.session->submitLocalState(s, client.session->timeMs() + 10000.0);
    pumpFor({&host, &client}, 200);
    VehicleSnapshot out;
    EXPECT_EQ(host.session->sampleRemote(id, out), SnapshotBuffer::Result::Empty);
    // A sane one is.
    client.session->submitLocalState(s);
    ASSERT_TRUE(pumpUntil({&host, &client}, [&] {
        return host.session->sampleRemote(id, out) != SnapshotBuffer::Result::Empty;
    }));
}

TEST(SessionSync, PlayoutDelayFollowsTheArrivals) {
    Peer host, client;
    connect(host, client);
    startRace(host, client);
    const std::uint8_t id = client.session->localId();
    EXPECT_DOUBLE_EQ(host.session->playoutDelay(id), syncConfig().interpolationDelayMs);
    // On loopback snapshots arrive within a frame: the delay stays at its
    // least, and the car is drawn from between two snapshots.
    VehicleSnapshot out;
    const auto start = std::chrono::steady_clock::now();
    ASSERT_TRUE(pumpUntil(
        {&host, &client},
        [&] {
            VehicleSnapshot s;
            s.position = {10.0f * std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count(), 0, 0};
            s.linearVelocity = {10, 0, 0};
            client.session->submitLocalState(s);
            return std::chrono::steady_clock::now() - start > std::chrono::milliseconds(600) &&
                   host.session->sampleRemote(id, out) == SnapshotBuffer::Result::Interpolated;
        },
        3000));
    EXPECT_GE(host.session->playoutDelay(id), syncConfig().interpolationDelayMs);
    EXPECT_LT(host.session->playoutDelay(id), 200.0);
}
