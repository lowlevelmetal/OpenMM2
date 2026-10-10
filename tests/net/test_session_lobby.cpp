// Session phases between races (docs/review/multiplayer-lobby.md): what a
// race leaves behind must not reach the lobby or the next race.
#include "net/Session.h"

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <thread>

using namespace mm2;
using namespace mm2::net;

namespace {

SessionConfig fastConfig() {
    SessionConfig c;
    c.connectTimeoutMs = 3000;
    c.joinTimeoutMs = 3000;
    c.snapshotRateHz = 50;
    c.interpolationDelayMs = 50;
    return c;
}

struct Peer {
    std::unique_ptr<Session> session = std::make_unique<Session>(fastConfig());
    std::vector<SessionEvent> events;

    void pump() {
        session->update();
        for (auto& e : session->takeEvents())
            events.push_back(std::move(e));
    }
    template <class E>
    int count() const {
        int n = 0;
        for (const auto& e : events)
            n += std::holds_alternative<E>(e);
        return n;
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

void settle(std::vector<Peer*> peers, int ms) { pumpUntil(std::move(peers), [] { return false; }, ms); }

HostParams hostParams() {
    HostParams h;
    h.bind = Address::loopback(0); // ephemeral port
    h.advertiseOnLan = false;
    h.player.name = "Host";
    h.settings.name = "Lobby";
    return h;
}

JoinParams joinParams(const Peer& host, const std::string& name) {
    JoinParams j;
    j.host = Address::loopback(host.session->port());
    j.player.name = name;
    return j;
}

// GO DRIVE, and every machine reports the race loaded (there is nothing to
// load) until it has started.
void startRace(Peer& host, Peer& a, Peer& b) {
    host.session->startRace(100);
    pumpUntil({&host, &a, &b}, [&] {
        for (Peer* p : {&host, &a, &b})
            p->session->reportLoaded();
        return host.session->phase() == SessionPhase::InGame;
    });
}

VehicleSnapshot at(float x) {
    VehicleSnapshot s;
    s.position = {x, 0, 0};
    return s;
}

} // namespace

// After a race the client's race screen is gone, but the session still held
// its last state and kept sending it at the snapshot rate through the lobby;
// the host stored it and passed it on, so the next race began with the car
// drawn (and collided with) where it had stopped in the last one.
// Messages of a race that nobody took before it ended (its last frame, or
// one service with the return to the lobby) are not handed to the next race:
// car states and inputs number their samples from 1 every race.
TEST(SessionLobby, NoUntakenRaceMessagesReachTheNextRace) {
    Peer host, a, b;
    ASSERT_TRUE(host.session->host(hostParams()));
    ASSERT_TRUE(a.session->join(joinParams(host, "A")));
    ASSERT_TRUE(b.session->join(joinParams(host, "B")));
    ASSERT_TRUE(pumpUntil({&host, &a, &b}, [&] {
        return a.session->phase() == SessionPhase::Lobby && b.session->players().size() == 3 &&
               a.session->clockSynced() && b.session->clockSynced();
    }));
    const std::uint8_t aId = a.session->localId();
    startRace(host, a, b);
    ASSERT_TRUE(pumpUntil({&host, &a, &b}, [&] { return a.session->phase() == SessionPhase::InGame; }));

    // Late in the race: an input from A and car states for A, never taken.
    PlayerInputMsg input;
    input.first = 5000;
    input.frames.resize(1);
    CarStatesMsg states;
    states.ack = 5000;
    ASSERT_TRUE(pumpUntil({&host, &a, &b}, [&] {
        a.session->sendPlayerInput(input);
        host.session->sendCarStates(aId, states);
        return host.session->playerInputsQueued() > 0 && a.session->ownCarStatesQueued() > 0;
    }));

    host.session->returnToLobby();
    ASSERT_TRUE(pumpUntil({&host, &a, &b}, [&] { return a.session->phase() == SessionPhase::Lobby; }));
    startRace(host, a, b);
    ASSERT_TRUE(pumpUntil({&host, &a, &b}, [&] { return a.session->phase() == SessionPhase::InGame; }));
    EXPECT_TRUE(host.session->takePlayerInputs().empty());
    EXPECT_TRUE(a.session->takeOwnCarStates().empty());
    EXPECT_TRUE(a.session->takePropStates().empty());
    EXPECT_TRUE(a.session->takePropFull().empty());
    EXPECT_TRUE(a.session->takeRulesStates().empty());
}

TEST(SessionLobby, NoVehicleStatesBetweenRaces) {
    Peer host, a, b;
    ASSERT_TRUE(host.session->host(hostParams()));
    ASSERT_TRUE(a.session->join(joinParams(host, "A")));
    ASSERT_TRUE(b.session->join(joinParams(host, "B")));
    ASSERT_TRUE(pumpUntil({&host, &a, &b}, [&] {
        return a.session->phase() == SessionPhase::Lobby && b.session->players().size() == 3 &&
               a.session->clockSynced() && b.session->clockSynced();
    }));
    const std::uint8_t aId = a.session->localId();

    startRace(host, a, b);
    ASSERT_TRUE(pumpUntil({&host, &a, &b}, [&] { return a.session->phase() == SessionPhase::InGame; }));
    VehicleSnapshot seen;
    ASSERT_TRUE(pumpUntil({&host, &a, &b}, [&] {
        a.session->submitLocalState(at(500.0f)); // where A ends the race
        return b.session->sampleRemote(aId, seen) != SnapshotBuffer::Result::Empty;
    }));

    // The host ends the race; nobody submits any more.
    host.session->returnToLobby();
    ASSERT_TRUE(pumpUntil({&host, &a, &b}, [&] {
        return a.session->phase() == SessionPhase::Lobby && b.session->phase() == SessionPhase::Lobby;
    }));
    settle({&host, &a, &b}, 300);
    EXPECT_EQ(host.session->sampleRemote(aId, seen), SnapshotBuffer::Result::Empty);
    EXPECT_EQ(b.session->sampleRemote(aId, seen), SnapshotBuffer::Result::Empty);

    // A state handed over in the lobby (a race screen's last frame) is not sent.
    a.session->submitLocalState(at(600.0f));
    settle({&host, &a, &b}, 200);
    EXPECT_EQ(host.session->sampleRemote(aId, seen), SnapshotBuffer::Result::Empty);

    // The next race starts with nothing from the last one; A's new start
    // place comes through.
    startRace(host, a, b);
    ASSERT_TRUE(pumpUntil({&host, &a, &b}, [&] { return b.session->phase() == SessionPhase::InGame; }));
    settle({&host, &a, &b}, 100);
    EXPECT_EQ(b.session->sampleRemote(aId, seen), SnapshotBuffer::Result::Empty);
    ASSERT_TRUE(pumpUntil({&host, &a, &b}, [&] {
        a.session->submitLocalState(at(-20.0f));
        return b.session->sampleRemote(aId, seen) != SnapshotBuffer::Result::Empty;
    }));
    EXPECT_NEAR(seen.position.x, -20.0f, 0.01f);
}
