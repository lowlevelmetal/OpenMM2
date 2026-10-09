// Loopback tests: a host and one or more clients in the same process talk
// over real UDP sockets on 127.0.0.1.
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
    const E* find(std::function<bool(const E&)> pred = nullptr) const {
        for (const auto& e : events)
            if (const auto* p = std::get_if<E>(&e); p && (!pred || pred(*p)))
                return p;
        return nullptr;
    }
    template <class E>
    int count() const {
        int n = 0;
        for (const auto& e : events)
            n += std::holds_alternative<E>(e);
        return n;
    }
};

// Pumps every peer until `done` or the timeout.
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

HostParams hostParams() {
    HostParams h;
    h.bind = Address::loopback(0); // ephemeral port
    h.advertiseOnLan = false;
    h.player.name = "Host";
    h.player.car = "vpcop";
    h.settings.name = "Test";
    return h;
}

JoinParams joinParams(const Peer& host, const std::string& name) {
    JoinParams j;
    j.host = Address::loopback(host.session->port());
    j.player.name = name;
    j.player.car = "vpbug";
    j.player.color = 2;
    return j;
}

bool joined(const Peer& p) { return p.find<ev::JoinAccepted>() != nullptr; }

} // namespace

TEST(Session, JoinLobbyChatAndLeave) {
    Peer host, client;
    ASSERT_TRUE(host.session->host(hostParams()));
    ASSERT_NE(host.session->port(), 0);
    ASSERT_TRUE(client.session->join(joinParams(host, "Alice")));
    ASSERT_TRUE(pumpUntil({&host, &client}, [&] { return joined(client) && host.find<ev::PlayerJoined>(); }));

    EXPECT_EQ(client.session->state(), Session::State::Active);
    EXPECT_EQ(client.session->localId(), 1);
    ASSERT_EQ(client.session->players().size(), 2u);
    EXPECT_EQ(client.session->players()[0].name, "Host");
    EXPECT_TRUE(client.session->players()[0].host);
    EXPECT_EQ(client.session->settings().name, "Test");
    ASSERT_EQ(host.session->players().size(), 2u);
    EXPECT_EQ(host.session->player(1)->car, "vpbug");
    EXPECT_EQ(host.session->player(1)->color, 2);

    // Chat both ways; the client's own message is echoed back by the host.
    client.session->sendChat("hi from alice");
    host.session->sendChat("welcome");
    ASSERT_TRUE(pumpUntil({&host, &client}, [&] {
        return client.count<ev::Chat>() == 2 && host.count<ev::Chat>() == 2;
    }));
    EXPECT_TRUE(client.find<ev::Chat>([](const ev::Chat& c) { return c.from == 1 && c.text == "hi from alice"; }));
    EXPECT_TRUE(host.find<ev::Chat>([](const ev::Chat& c) { return c.from == 0 && c.text == "welcome"; }));

    // Car/colour/ready changes go through the host.
    client.session->setLocalPlayer("vppanoz", 5, 1);
    client.session->setReady(true);
    ASSERT_TRUE(pumpUntil({&host, &client}, [&] {
        const PlayerInfo* p = host.session->player(1);
        const PlayerInfo* q = client.session->player(1);
        return p && p->ready && p->car == "vppanoz" && q && q->ready && q->color == 5;
    }));

    // Settings propagate.
    SessionSettings s = host.session->settings();
    s.city = "sf";
    s.mode = GameMode::Checkpoint;
    host.session->updateSettings(s);
    ASSERT_TRUE(pumpUntil({&host, &client}, [&] { return client.session->settings().city == "sf"; }));
    EXPECT_EQ(client.session->settings().mode, GameMode::Checkpoint);

    // Client leaves: host sees PlayerLeft(Left).
    client.session->leave();
    ASSERT_TRUE(pumpUntil({&host}, [&] { return host.find<ev::PlayerLeft>(); }));
    EXPECT_EQ(host.find<ev::PlayerLeft>()->reason, DisconnectReason::Left);
    EXPECT_EQ(host.session->players().size(), 1u);
}

TEST(Session, DuplicateNamesAreMadeUnique) {
    Peer host, a, b;
    ASSERT_TRUE(host.session->host(hostParams()));
    ASSERT_TRUE(a.session->join(joinParams(host, "Sam")));
    ASSERT_TRUE(b.session->join(joinParams(host, "sam")));
    ASSERT_TRUE(pumpUntil({&host, &a, &b}, [&] { return joined(a) && joined(b) && host.session->players().size() == 3; }));
    EXPECT_NE(host.session->player(a.session->localId())->name, host.session->player(b.session->localId())->name);
    // Each client learns about the other.
    ASSERT_TRUE(pumpUntil({&host, &a, &b}, [&] { return a.session->players().size() == 3; }));
}

TEST(Session, PasswordProtection) {
    HostParams hp = hostParams();
    hp.password = "hunter2";
    Peer host;
    ASSERT_TRUE(host.session->host(hp));
    EXPECT_TRUE(host.session->settings().hasPassword);

    Peer wrong;
    JoinParams jp = joinParams(host, "Mallory");
    jp.password = "guess";
    ASSERT_TRUE(wrong.session->join(jp));
    ASSERT_TRUE(pumpUntil({&host, &wrong}, [&] { return wrong.find<ev::JoinFailed>(); }));
    EXPECT_EQ(wrong.find<ev::JoinFailed>()->reason, DisconnectReason::BadPassword);

    Peer none;
    ASSERT_TRUE(none.session->join(joinParams(host, "Nobody")));
    ASSERT_TRUE(pumpUntil({&host, &none}, [&] { return none.find<ev::JoinFailed>(); }));
    EXPECT_EQ(none.find<ev::JoinFailed>()->reason, DisconnectReason::BadPassword);

    Peer right;
    jp.player.name = "Bob";
    jp.password = "hunter2";
    ASSERT_TRUE(right.session->join(jp));
    ASSERT_TRUE(pumpUntil({&host, &right}, [&] { return joined(right); }));
    EXPECT_EQ(host.session->players().size(), 2u);
}

TEST(Session, FullSessionRejects) {
    HostParams hp = hostParams();
    hp.settings.maxPlayers = 2;
    Peer host, a, b;
    ASSERT_TRUE(host.session->host(hp));
    ASSERT_TRUE(a.session->join(joinParams(host, "A")));
    ASSERT_TRUE(pumpUntil({&host, &a}, [&] { return joined(a); }));
    ASSERT_TRUE(b.session->join(joinParams(host, "B")));
    ASSERT_TRUE(pumpUntil({&host, &a, &b}, [&] { return b.find<ev::JoinFailed>(); }));
    EXPECT_EQ(b.find<ev::JoinFailed>()->reason, DisconnectReason::ServerFull);
}

TEST(Session, VersionMismatchIsRejectedAtConnect) {
    Peer host;
    ASSERT_TRUE(host.session->host(hostParams()));
    Transport t;
    ASSERT_TRUE(t.startClient());
    t.connect(Address::loopback(host.session->port()), (std::uint32_t{kProtocolMagic} << 16) | (kProtocolVersion + 1));
    std::vector<TransportEvent> events;
    bool disconnected = false;
    std::uint32_t data = 0;
    pumpUntil({&host}, [&] {
        t.service(events);
        for (const auto& e : events)
            if (e.type == TransportEvent::Type::Disconnected) {
                disconnected = true;
                data = e.data;
            }
        events.clear();
        return disconnected;
    });
    EXPECT_TRUE(disconnected);
    EXPECT_EQ(data, static_cast<std::uint32_t>(DisconnectReason::VersionMismatch));
    EXPECT_EQ(host.session->players().size(), 1u);
}

TEST(Session, UnreachableHostTimesOut) {
    SessionConfig cfg = fastConfig();
    cfg.connectTimeoutMs = 500;
    Session s(cfg);
    // Grab a free port and close it again so nothing listens there.
    std::uint16_t port = 0;
    {
        Transport t;
        ASSERT_TRUE(t.listen(Address::loopback(0)));
        port = t.port();
    }
    ASSERT_TRUE(s.join({Address::loopback(port), {}, {}}));
    const auto start = std::chrono::steady_clock::now();
    std::vector<SessionEvent> events;
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(5) && events.empty()) {
        s.update();
        events = s.takeEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_EQ(events.size(), 1u);
    const auto* failed = std::get_if<ev::JoinFailed>(&events[0]);
    ASSERT_TRUE(failed);
    EXPECT_EQ(failed->reason, DisconnectReason::JoinTimeout);
    EXPECT_EQ(s.state(), Session::State::Closed);
}

TEST(Session, CountdownSnapshotsAndEvents) {
    Peer host, a, b;
    ASSERT_TRUE(host.session->host(hostParams()));
    ASSERT_TRUE(a.session->join(joinParams(host, "A")));
    ASSERT_TRUE(b.session->join(joinParams(host, "B")));
    ASSERT_TRUE(pumpUntil({&host, &a, &b}, [&] {
        return joined(a) && joined(b) && a.session->players().size() == 3 && b.session->players().size() == 3;
    }));
    // Let clock sync converge.
    ASSERT_TRUE(pumpUntil({&host, &a, &b}, [] { return false; }, 300) == false);
    EXPECT_TRUE(a.session->clockSynced());
    EXPECT_NEAR(static_cast<double>(a.session->time()), static_cast<double>(host.session->time()), 20.0);

    host.session->startRace(300);
    ASSERT_TRUE(pumpUntil({&host, &a, &b}, [&] {
        for (Peer* p : {&host, &a, &b})
            p->session->reportLoaded(); // nothing to load
        return host.find<ev::GameStarted>() && a.find<ev::GameStarted>() && b.find<ev::GameStarted>();
    }));
    EXPECT_TRUE(a.find<ev::RaceLoading>());
    EXPECT_EQ(a.session->phase(), SessionPhase::InGame);

    // A drives along +X; B and the host see it through the interpolation buffer.
    const std::uint8_t aId = a.session->localId();
    const auto driveStart = std::chrono::steady_clock::now();
    auto drive = [&] {
        const float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - driveStart).count();
        VehicleSnapshot s;
        s.position = {10.0f * t, 0, 0};
        s.linearVelocity = {10.0f, 0, 0};
        s.controls.throttle = 1.0f;
        a.session->submitLocalState(s);
    };
    VehicleSnapshot seen;
    ASSERT_TRUE(pumpUntil({&host, &a, &b}, [&] {
        drive();
        const float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - driveStart).count();
        return t > 0.5f && b.session->sampleRemote(aId, seen) == SnapshotBuffer::Result::Interpolated;
    }));
    const float now = std::chrono::duration<float>(std::chrono::steady_clock::now() - driveStart).count();
    // Shown ~interpolation delay (50 ms) plus transit behind real time.
    EXPECT_GT(seen.position.x, 10.0f * (now - 0.3f));
    EXPECT_LT(seen.position.x, 10.0f * now + 0.1f);
    EXPECT_NEAR(seen.controls.throttle, 1.0f, 0.01f);
    VehicleSnapshot hostView;
    EXPECT_NE(host.session->sampleRemote(aId, hostView), SnapshotBuffer::Result::Empty);
    // A never receives its own state back.
    VehicleSnapshot self;
    EXPECT_EQ(a.session->sampleRemote(aId, self), SnapshotBuffer::Result::Empty);

    // Game events: broadcast from A reaches host and B, not A. The host's
    // filter can drop events.
    host.session->setEventFilter([](std::uint8_t, GameEventMsg& m) { return m.type != 999; });
    a.session->sendGameEvent(999, {}); // filtered
    a.session->sendGameEvent(static_cast<std::uint16_t>(GameEventType::CheckpointReached),
                             encodePayload(CheckpointEvent{3, 12345}));
    ASSERT_TRUE(pumpUntil({&host, &a, &b}, [&] { return host.find<ev::GameEvent>() && b.find<ev::GameEvent>(); }));
    const auto* ge = b.find<ev::GameEvent>();
    EXPECT_EQ(ge->from, aId);
    CheckpointEvent cp;
    ASSERT_TRUE(decodePayload(ge->payload, cp));
    EXPECT_EQ(cp.index, 3);
    EXPECT_EQ(host.count<ev::GameEvent>(), 1);
    EXPECT_EQ(a.count<ev::GameEvent>(), 0);

    // Targeted event from the host to B only.
    host.session->sendGameEvent(static_cast<std::uint16_t>(GameEventType::Custom), {std::byte{7}},
                                b.session->localId());
    ASSERT_TRUE(pumpUntil({&host, &a, &b}, [&] { return b.count<ev::GameEvent>() == 2; }));
    EXPECT_EQ(a.count<ev::GameEvent>(), 0);

    // Back to the lobby: ready flags reset everywhere.
    host.session->returnToLobby();
    ASSERT_TRUE(pumpUntil({&host, &a, &b}, [&] { return b.find<ev::ReturnedToLobby>(); }));
    EXPECT_EQ(b.session->phase(), SessionPhase::Lobby);
}

TEST(Session, KickAndHostShutdown) {
    Peer host, a, b;
    ASSERT_TRUE(host.session->host(hostParams()));
    ASSERT_TRUE(a.session->join(joinParams(host, "A")));
    ASSERT_TRUE(b.session->join(joinParams(host, "B")));
    ASSERT_TRUE(pumpUntil({&host, &a, &b}, [&] { return joined(a) && joined(b) && b.session->players().size() == 3; }));

    host.session->kick(a.session->localId(), "spawn camping");
    ASSERT_TRUE(pumpUntil({&host, &a, &b}, [&] {
        return a.find<ev::Disconnected>() && b.find<ev::PlayerLeft>();
    }));
    EXPECT_EQ(a.find<ev::Disconnected>()->reason, DisconnectReason::Kicked);
    EXPECT_NE(a.find<ev::Disconnected>()->message.find("spawn camping"), std::string::npos);
    EXPECT_EQ(b.find<ev::PlayerLeft>()->reason, DisconnectReason::Kicked);
    EXPECT_EQ(host.session->players().size(), 2u);

    host.session->leave();
    ASSERT_TRUE(pumpUntil({&b}, [&] { return b.find<ev::Disconnected>(); }));
    EXPECT_EQ(b.find<ev::Disconnected>()->reason, DisconnectReason::HostShutdown);
}

TEST(Session, JoinInProgressPolicy) {
    Peer host, late;
    ASSERT_TRUE(host.session->host(hostParams()));
    host.session->startRace(0);
    host.session->reportLoaded(); // alone: the start follows at once
    ASSERT_TRUE(pumpUntil({&host}, [&] { return host.find<ev::GameStarted>(); }));
    ASSERT_TRUE(late.session->join(joinParams(host, "Late")));
    ASSERT_TRUE(pumpUntil({&host, &late}, [&] { return late.find<ev::JoinFailed>(); }));
    EXPECT_EQ(late.find<ev::JoinFailed>()->reason, DisconnectReason::GameInProgress);

    SessionSettings s = host.session->settings();
    s.allowJoinInProgress = true;
    host.session->updateSettings(s);
    Peer late2;
    ASSERT_TRUE(late2.session->join(joinParams(host, "Late2")));
    ASSERT_TRUE(pumpUntil({&host, &late2}, [&] { return joined(late2) && late2.find<ev::GameStarted>(); }));
    EXPECT_EQ(late2.session->phase(), SessionPhase::InGame);
}
