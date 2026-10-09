// Hostile and malformed network input: a fake host or client built on a raw
// Transport sends what an honest OpenMM2 never would, and the real Session,
// LAN scanner and beacon must stay consistent (docs/review/multiplayer-input.md).
#include "net/Discovery.h"
#include "net/PortMapper.h"
#include "net/Session.h"
#include "net/VehicleDamage.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
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

bool waitFor(const std::function<bool()>& step, int timeoutMs = 3000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (step())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

// A Session and the events it reported.
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
    template <class E>
    const E* find() const {
        for (const auto& e : events)
            if (const auto* p = std::get_if<E>(&e))
                return p;
        return nullptr;
    }
};

// One end of a connection driven by hand: sends any bytes it likes.
struct RawEnd {
    Transport transport;
    PeerId peer = kInvalidPeer;
    std::vector<TransportEvent> events;

    void pump() { transport.service(events, 0); }
    template <class M>
    void send(M msg, Channel channel = Channel::Control) {
        transport.send(peer, channel, encodeMessage(std::move(msg)));
    }
    void sendRaw(std::span<const std::byte> bytes, Channel channel = Channel::Control) {
        transport.send(peer, channel, bytes);
    }
    // The first received message of type M (removed from the queue).
    template <class M>
    std::optional<M> take() {
        for (auto it = events.begin(); it != events.end(); ++it) {
            M m;
            if (it->type == TransportEvent::Type::Received && decodeMessage(it->payload, m)) {
                events.erase(it);
                return m;
            }
        }
        return std::nullopt;
    }
    bool connected() const {
        return std::ranges::any_of(events,
                                   [](const TransportEvent& e) { return e.type == TransportEvent::Type::Connected; });
    }
};

HostParams hostParams() {
    HostParams h;
    h.bind = Address::loopback(0);
    h.advertiseOnLan = false;
    h.player.name = "Host";
    h.player.car = "vpcop";
    return h;
}

// A raw client that completed the handshake with `host`; returns its player id.
std::uint8_t rawJoin(Peer& host, RawEnd& raw, HelloMsg hello = {}) {
    EXPECT_TRUE(raw.transport.startClient());
    raw.peer = raw.transport.connect(Address::loopback(host.session->port()));
    std::optional<ChallengeMsg> challenge;
    EXPECT_TRUE(waitFor([&] {
        host.pump();
        raw.pump();
        challenge = raw.take<ChallengeMsg>();
        return challenge.has_value();
    }));
    if (hello.name.empty())
        hello.name = "Raw";
    if (hello.car.empty())
        hello.car = "vpbug";
    raw.send(hello);
    std::optional<WelcomeMsg> welcome;
    EXPECT_TRUE(waitFor([&] {
        host.pump();
        raw.pump();
        welcome = raw.take<WelcomeMsg>();
        return welcome.has_value();
    }));
    return welcome ? welcome->yourId : kInvalidPlayerId;
}

// A raw host listening on loopback that `client` joined; `welcome` is sent
// in answer to the client's Hello.
void rawHost(RawEnd& raw, Peer& client, WelcomeMsg welcome) {
    ASSERT_TRUE(raw.transport.listen(Address::loopback(0)));
    JoinParams jp;
    jp.host = Address::loopback(raw.transport.port());
    jp.player.name = "Victim";
    ASSERT_TRUE(client.session->join(jp));
    ASSERT_TRUE(waitFor([&] {
        client.pump();
        raw.pump();
        for (const auto& e : raw.events)
            if (e.type == TransportEvent::Type::Connected)
                raw.peer = e.peer;
        return raw.peer != kInvalidPeer;
    }));
    raw.send(ChallengeMsg{});
    ASSERT_TRUE(waitFor([&] {
        client.pump();
        raw.pump();
        return raw.take<HelloMsg>().has_value();
    }));
    raw.send(std::move(welcome));
    ASSERT_TRUE(waitFor([&] {
        client.pump();
        raw.pump();
        const auto state = client.session->state();
        return state == Session::State::Active || state == Session::State::Closed;
    }));
}

PlayerInfo playerInfo(std::uint8_t id, std::string name, std::string car = "vpbug") {
    PlayerInfo p;
    p.id = id;
    p.name = std::move(name);
    p.car = std::move(car);
    return p;
}

WelcomeMsg welcomeFor(std::uint8_t you) {
    WelcomeMsg w;
    w.yourId = you;
    w.players = {playerInfo(kHostPlayerId, "Host"), playerInfo(you, "Victim")};
    w.players[0].host = true;
    return w;
}

bool hasControl(std::string_view s) {
    return std::ranges::any_of(s, [](char c) { return static_cast<unsigned char>(c) < 0x20 || c == 0x7F; });
}

} // namespace

// --- Names and text -------------------------------------------------------------------

TEST(HostileInput, SanitizeTextKeepsWellFormedUtf8Only) {
    EXPECT_EQ(sanitizeText("José", 24), "José");
    EXPECT_EQ(sanitizeText("price: 5€", 24), "price: 5€");
    EXPECT_EQ(sanitizeText("  hi\tthere\r\n ", 24), "hi there");
    EXPECT_EQ(sanitizeText("a\x1b[31mb\x7f\x01", 24), "a[31mb");
    EXPECT_EQ(sanitizeText("x\xC2\x85y", 24), "xy");         // C1 control (NEL)
    EXPECT_EQ(sanitizeText("x\xC3", 24), "x");               // truncated sequence
    EXPECT_EQ(sanitizeText("x\xC0\xAFy", 24), "xy");         // overlong '/'
    EXPECT_EQ(sanitizeText("x\xED\xA0\x80y", 24), "xy");     // UTF-16 surrogate
    EXPECT_EQ(sanitizeText("x\xF4\x90\x80\x80y", 24), "xy"); // above U+10FFFF
    EXPECT_EQ(sanitizeText("\xFF\xFE", 24), "");
    // Cut on a character boundary, never inside "é" (2 bytes).
    EXPECT_EQ(sanitizeText("ééé", 5), "éé");
    EXPECT_EQ(sanitizeText("abc", 0), "");
    EXPECT_EQ(sanitizeText("ab cd", 3), "ab");
}

TEST(HostileInput, AssetNamesArePlainBaseNames) {
    for (const char* ok : {"vpbug", "vpmustang99", "sf", "london", "My_Car-2"})
        EXPECT_TRUE(isValidAssetName(ok)) << ok;
    for (const char* bad : {"", "../x", "a/b", "a\\b", "a.b", "x\n", "c:x", " vpbug", "vp bug"})
        EXPECT_FALSE(isValidAssetName(bad)) << bad;
    EXPECT_TRUE(isValidAssetName(std::string(kMaxShortStringLength, 'a')));
    EXPECT_FALSE(isValidAssetName(std::string(kMaxShortStringLength + 1, 'a')));
}

// A name's last character survives the host's cleanup even when it is not
// ASCII ("José", not "Jos").
TEST(HostileInput, NonAsciiNamesAndChatSurviveTheHost) {
    Peer host;
    ASSERT_TRUE(host.session->host(hostParams()));
    Peer client;
    JoinParams jp;
    jp.host = Address::loopback(host.session->port());
    jp.player.name = "José";
    ASSERT_TRUE(client.session->join(jp));
    ASSERT_TRUE(waitFor([&] {
        host.pump();
        client.pump();
        return client.find<ev::JoinAccepted>() != nullptr;
    }));
    ASSERT_NE(host.session->player(client.session->localId()), nullptr);
    EXPECT_EQ(host.session->player(client.session->localId())->name, "José");

    client.session->sendChat("à bientôt");
    ASSERT_TRUE(waitFor([&] {
        host.pump();
        client.pump();
        return host.find<ev::Chat>() != nullptr;
    }));
    EXPECT_EQ(host.find<ev::Chat>()->text, "à bientôt");
}

// Everything text-like that a (modified) host sends is cleaned up by the
// client before the game sees it: names, chat, the session name, a kick
// reason. A city or car name that is not a plain base name never reaches
// the game (a newline in the city would end up in the profile file).
TEST(HostileInput, ClientCleansUpWhatTheHostSends) {
    RawEnd raw;
    Peer client;
    WelcomeMsg w = welcomeFor(1);
    w.settings.name = "Evil\nsession\x1b[31m";
    w.settings.city = "x\n[Races]\nhack=1";
    w.players[0].name = "Bad\nHost\x07";
    w.players[0].car = "../../tune/vpbug";
    rawHost(raw, client, w);
    ASSERT_EQ(client.session->state(), Session::State::Active);

    const auto& s = client.session->settings();
    EXPECT_FALSE(hasControl(s.name)) << s.name;
    EXPECT_FALSE(hasControl(s.city)) << s.city;
    EXPECT_EQ(s.city.find('/'), std::string::npos);
    const PlayerInfo* h = client.session->player(kHostPlayerId);
    ASSERT_NE(h, nullptr);
    EXPECT_FALSE(hasControl(h->name)) << h->name;
    EXPECT_EQ(h->car.find('/'), std::string::npos) << h->car;

    raw.send(ChatMsg{kHostPlayerId, "hi\n\x1b[2Jthere\x7f"});
    PlayerInfo update = playerInfo(5, "New\r\nline", "vpbug\\..\\x");
    raw.send(PlayerJoinedMsg{update});
    SessionSettings later = w.settings;
    later.city = "../city/london";
    raw.send(SettingsMsg{later});
    ASSERT_TRUE(waitFor([&] {
        client.pump();
        raw.pump();
        return client.find<ev::Chat>() && client.session->player(5) && client.find<ev::SettingsChanged>();
    }));
    EXPECT_EQ(client.find<ev::Chat>()->text.find('\n'), std::string::npos);
    EXPECT_FALSE(hasControl(client.find<ev::Chat>()->text));
    EXPECT_FALSE(hasControl(client.session->player(5)->name));
    EXPECT_EQ(client.session->player(5)->car.find('\\'), std::string::npos);
    EXPECT_EQ(client.session->settings().city.find('/'), std::string::npos);

    raw.send(KickMsg{"bye\n\x1b[0m"});
    ASSERT_TRUE(waitFor([&] {
        client.pump();
        raw.pump();
        return client.find<ev::Disconnected>() != nullptr;
    }));
    EXPECT_FALSE(hasControl(client.find<ev::Disconnected>()->message));
}

// A host cannot make a client track more players than the protocol allows
// (each one would get a car in the race).
TEST(HostileInput, ClientKeepsAtMostMaxPlayers) {
    RawEnd raw;
    Peer client;
    rawHost(raw, client, welcomeFor(1));
    ASSERT_EQ(client.session->state(), Session::State::Active);
    for (int id = 2; id < 255; ++id)
        raw.send(PlayerJoinedMsg{playerInfo(static_cast<std::uint8_t>(id), std::format("P{}", id))});
    raw.send(PlayerJoinedMsg{playerInfo(kInvalidPlayerId, "Nobody")});
    raw.send(ChatMsg{kHostPlayerId, "done"});
    ASSERT_TRUE(waitFor([&] {
        client.pump();
        raw.pump();
        return client.find<ev::Chat>() != nullptr;
    }));
    EXPECT_LE(client.session->players().size(), kMaxPlayers);
    EXPECT_EQ(client.session->player(kInvalidPlayerId), nullptr);
}

// The host keeps a player's car a plain base name: anything else in Hello
// gives the default car, and a later request with one keeps the old car.
TEST(HostileInput, HostRejectsCarNamesThatAreNotBaseNames) {
    Peer host;
    ASSERT_TRUE(host.session->host(hostParams()));
    RawEnd raw;
    HelloMsg hello;
    hello.name = "Mallory";
    hello.car = "../../etc/passwd";
    const std::uint8_t id = rawJoin(host, raw, hello);
    ASSERT_NE(id, kInvalidPlayerId);
    const PlayerInfo* p = host.session->player(id);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->car, "vpbug");

    raw.send(PlayerRequestMsg{"vpcoop", 1, 0, false});
    ASSERT_TRUE(waitFor([&] {
        host.pump();
        raw.pump();
        return host.session->player(id)->car == "vpcoop";
    }));
    raw.send(PlayerRequestMsg{"tune\\vpbug", 2, 0, true});
    ASSERT_TRUE(waitFor([&] {
        host.pump();
        raw.pump();
        return host.session->player(id)->ready;
    }));
    EXPECT_EQ(host.session->player(id)->car, "vpcoop");
    EXPECT_EQ(host.session->player(id)->color, 2);
}

// --- Password ---------------------------------------------------------------------------

// Guessing a lobby password online: after five wrong ones an address is
// turned away when it connects, for a minute, the right password included.
TEST(HostileInput, WrongPasswordsLockTheAddressOut) {
    HostParams hp = hostParams();
    hp.password = "hunter2";
    Peer host;
    ASSERT_TRUE(host.session->host(hp));
    auto attempt = [&](const std::string& password) {
        Peer guesser;
        JoinParams jp;
        jp.host = Address::loopback(host.session->port());
        jp.player.name = "Guesser";
        jp.password = password;
        EXPECT_TRUE(guesser.session->join(jp));
        EXPECT_TRUE(waitFor([&] {
            host.pump();
            guesser.pump();
            return guesser.find<ev::JoinFailed>() || guesser.find<ev::JoinAccepted>();
        }));
        const auto* failed = guesser.find<ev::JoinFailed>();
        return failed ? failed->reason : DisconnectReason::None;
    };
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(attempt(std::format("guess{}", i)), DisconnectReason::BadPassword);
    EXPECT_EQ(attempt("hunter2"), DisconnectReason::BadPassword);
    EXPECT_EQ(host.session->players().size(), 1u);
}

// --- The race start ---------------------------------------------------------------------

// A joiner's race reports count only for itself, only for the race the host
// is starting and only once: one in the lobby, for another race number, with
// another player's id, or repeated changes nothing else and does not reach
// the others again.
TEST(HostileInput, HostCountsOnlyAJoinersOwnFirstReportOfTheRace) {
    Peer host;
    ASSERT_TRUE(host.session->host(hostParams()));
    Peer watcher;
    JoinParams jp;
    jp.host = Address::loopback(host.session->port());
    jp.player.name = "Watcher";
    ASSERT_TRUE(watcher.session->join(jp));
    ASSERT_TRUE(waitFor([&] {
        host.pump();
        watcher.pump();
        return watcher.find<ev::JoinAccepted>() != nullptr;
    }));
    RawEnd raw;
    const std::uint8_t id = rawJoin(host, raw);
    ASSERT_NE(id, kInvalidPlayerId);
    const std::uint8_t watcherId = watcher.session->localId();
    auto pumpAll = [&](int ms) {
        waitFor([&] {
            host.pump();
            raw.pump();
            watcher.pump();
            return false;
        }, ms);
    };

    // In the lobby nothing is loading.
    raw.send(RaceLoadedMsg{0, id});
    raw.send(RaceLoadedMsg{1, id});
    pumpAll(100);
    EXPECT_EQ(host.count<ev::PlayerLoaded>(), 0);

    host.session->startRace(0);
    ASSERT_TRUE(waitFor([&] {
        host.pump();
        raw.pump();
        watcher.pump();
        return watcher.session->phase() == SessionPhase::Countdown && raw.take<RaceLoadMsg>().has_value();
    }));
    const std::uint32_t race = host.session->raceNumber();
    raw.send(RaceLoadedMsg{race - 1, id});
    raw.send(RaceLoadedMsg{race + 1, id});
    raw.send(RaceLoadedMsg{0xFFFFFFFFu, id});
    pumpAll(100);
    EXPECT_FALSE(host.session->playerLoaded(id));
    EXPECT_EQ(host.count<ev::PlayerLoaded>(), 0);

    // Another player's id: the host takes the report as the sender's.
    raw.send(RaceLoadedMsg{race, watcherId});
    for (int i = 0; i < 100; ++i)
        raw.send(RaceLoadedMsg{race, id});
    pumpAll(200);
    EXPECT_TRUE(host.session->playerLoaded(id));
    EXPECT_FALSE(host.session->playerLoaded(watcherId));
    EXPECT_EQ(host.count<ev::PlayerLoaded>(), 1);
    EXPECT_EQ(watcher.count<ev::PlayerLoaded>(), 1);
    EXPECT_TRUE(watcher.session->playerLoaded(id));
    // The start still waits for the host and the watcher.
    EXPECT_FALSE(host.session->raceStartKnown());
    host.session->reportLoaded();
    watcher.session->reportLoaded();
    ASSERT_TRUE(waitFor([&] {
        host.pump();
        raw.pump();
        watcher.pump();
        return watcher.session->raceStartKnown();
    }));
    EXPECT_EQ(watcher.session->raceStartTime(), host.session->raceStartTime());
}

// A host's race messages out of turn leave a client's race alone: a start
// or a report in the lobby, an order that does not number a new race, a
// start for another race or a second one, a report for a player the client
// does not know, for itself or for another race.
TEST(HostileInput, ClientIgnoresRaceMessagesOutOfTurn) {
    RawEnd raw;
    Peer client;
    rawHost(raw, client, welcomeFor(1));
    ASSERT_EQ(client.session->state(), Session::State::Active);
    auto pumpBoth = [&](int ms) {
        waitFor([&] {
            client.pump();
            raw.pump();
            return false;
        }, ms);
    };
    raw.send(RaceStartMsg{1, 5000});
    raw.send(RaceLoadedMsg{1, kHostPlayerId});
    pumpBoth(100);
    EXPECT_EQ(client.session->phase(), SessionPhase::Lobby);
    EXPECT_FALSE(client.session->raceStartKnown());
    EXPECT_FALSE(client.session->playerLoaded(kHostPlayerId));

    raw.send(RaceLoadMsg{3, 100});
    raw.send(RaceLoadMsg{2, 100}); // an earlier number
    raw.send(RaceLoadMsg{3, 100}); // the same race again
    raw.send(RaceStartMsg{2, 5000});
    raw.send(RaceLoadedMsg{3, 9});
    raw.send(RaceLoadedMsg{3, 1}); // the client itself: it reports that itself
    raw.send(RaceLoadedMsg{2, kHostPlayerId});
    pumpBoth(150);
    EXPECT_EQ(client.session->phase(), SessionPhase::Countdown);
    EXPECT_EQ(client.session->raceNumber(), 3u);
    EXPECT_EQ(client.count<ev::RaceLoading>(), 1);
    EXPECT_FALSE(client.session->raceStartKnown());
    EXPECT_FALSE(client.session->playerLoaded(9));
    EXPECT_FALSE(client.session->playerLoaded(1));
    EXPECT_FALSE(client.session->playerLoaded(kHostPlayerId));
    // The client's own report still goes out, for race 3.
    client.session->reportLoaded();
    std::optional<RaceLoadedMsg> report;
    ASSERT_TRUE(waitFor([&] {
        client.pump();
        raw.pump();
        report = raw.take<RaceLoadedMsg>();
        return report.has_value();
    }));
    EXPECT_EQ(report->race, 3u);
    EXPECT_EQ(report->player, 1);

    raw.send(RaceLoadedMsg{3, kHostPlayerId});
    raw.send(RaceStartMsg{3, 7000});
    raw.send(RaceStartMsg{3, 9000}); // a second start
    pumpBoth(150);
    EXPECT_TRUE(client.session->playerLoaded(kHostPlayerId));
    EXPECT_EQ(client.session->raceStartTime(), 7000u);
    EXPECT_EQ(client.count<ev::RaceStartSet>(), 1);
}

// A Welcome during a race: the client takes the race, the start and the
// players who have loaded, leaving out ids it does not know.
TEST(HostileInput, WelcomeDuringARaceKeepsOnlyKnownLoadedPlayers) {
    RawEnd raw;
    Peer client;
    WelcomeMsg w = welcomeFor(1);
    w.phase = SessionPhase::Countdown;
    w.race = 2;
    w.raceOrderTime = 50;
    w.loaded = {kHostPlayerId, kHostPlayerId, 7, 200, 1};
    rawHost(raw, client, w);
    ASSERT_EQ(client.session->state(), Session::State::Active);
    client.pump();
    EXPECT_EQ(client.session->raceNumber(), 2u);
    EXPECT_TRUE(client.session->playerLoaded(kHostPlayerId));
    EXPECT_FALSE(client.session->playerLoaded(7));
    EXPECT_FALSE(client.session->playerLoaded(200));
    EXPECT_FALSE(client.session->playerLoaded(1)); // it reports itself
    EXPECT_FALSE(client.session->raceStartKnown());
    EXPECT_EQ(client.count<ev::RaceLoading>(), 1);
}

// --- Floods -----------------------------------------------------------------------------

// A client cannot make the host relay an unbounded stream of chat lines,
// requests and events to everyone else.
TEST(HostileInput, HostRateLimitsWhatItRelays) {
    Peer host;
    ASSERT_TRUE(host.session->host(hostParams()));
    Peer watcher;
    JoinParams jp;
    jp.host = Address::loopback(host.session->port());
    jp.player.name = "Watcher";
    ASSERT_TRUE(watcher.session->join(jp));
    ASSERT_TRUE(waitFor([&] {
        host.pump();
        watcher.pump();
        return watcher.find<ev::JoinAccepted>() != nullptr;
    }));
    RawEnd raw;
    const std::uint8_t id = rawJoin(host, raw);
    ASSERT_NE(id, kInvalidPlayerId);
    const int updatesBefore = watcher.count<ev::PlayerUpdated>();

    for (int i = 0; i < 300; ++i) {
        raw.send(ChatMsg{0, std::format("spam {}", i)});
        raw.send(PlayerRequestMsg{"vpbug", static_cast<std::uint8_t>(i % 8), 0, false});
        GameEventMsg ev;
        ev.type = static_cast<std::uint16_t>(GameEventType::Custom);
        raw.send(ev, Channel::Events);
    }
    waitFor([&] {
        host.pump();
        raw.pump();
        watcher.pump();
        return false;
    }, 600);
    EXPECT_LT(watcher.count<ev::Chat>(), 50);
    EXPECT_LT(watcher.count<ev::PlayerUpdated>() - updatesBefore, 100);
    EXPECT_LT(watcher.count<ev::GameEvent>(), 150);
    EXPECT_GT(watcher.count<ev::Chat>(), 0); // a normal pace still gets through
    EXPECT_GT(watcher.count<ev::GameEvent>(), 0);
    // The player's last request (colour 299 % 8) still reaches everyone.
    EXPECT_EQ(host.session->player(id)->color, 3);
    ASSERT_TRUE(waitFor([&] {
        host.pump();
        raw.pump();
        watcher.pump();
        const PlayerInfo* p = watcher.session->player(id);
        return p && p->color == 3;
    }));
}

// A car's damage events (net/VehicleDamage.h) have a relay budget of their
// own: a flood of them is cut down, and the race events a player sends after
// it still all reach the others.
TEST(HostileInput, HostRateLimitsDamageEventsOnTheirOwn) {
    Peer host;
    ASSERT_TRUE(host.session->host(hostParams()));
    Peer watcher;
    JoinParams jp;
    jp.host = Address::loopback(host.session->port());
    jp.player.name = "Watcher";
    ASSERT_TRUE(watcher.session->join(jp));
    ASSERT_TRUE(waitFor([&] {
        host.pump();
        watcher.pump();
        return watcher.find<ev::JoinAccepted>() != nullptr;
    }));
    RawEnd raw;
    ASSERT_NE(rawJoin(host, raw), kInvalidPlayerId);

    VehicleDamageEvent damage;
    damage.patches = {{0, {0.1f, 0.2f, 0.3f}, 1}};
    GameEventMsg flood;
    flood.type = kVehicleDamageEvent;
    flood.payload = encodePayload(damage);
    for (int i = 0; i < 300; ++i)
        raw.send(flood, Channel::Events);
    GameEventMsg checkpoint;
    checkpoint.type = static_cast<std::uint16_t>(GameEventType::CheckpointReached);
    checkpoint.payload = encodePayload(CheckpointEvent{1, 1000});
    for (int i = 0; i < 10; ++i)
        raw.send(checkpoint, Channel::Events);
    waitFor([&] {
        host.pump();
        raw.pump();
        watcher.pump();
        return false;
    }, 600);
    int damageEvents = 0, raceEvents = 0;
    for (const auto& e : watcher.events)
        if (const auto* g = std::get_if<ev::GameEvent>(&e))
            ++(g->type == kVehicleDamageEvent ? damageEvents : raceEvents);
    EXPECT_GT(damageEvents, 0);
    EXPECT_LT(damageEvents, 60); // a burst of 30 and 15 a second
    EXPECT_EQ(raceEvents, 10);
}

// A peer cannot make the host buffer a huge message (ENet's default limit
// is 32 MiB per packet, allocated when the first fragment arrives).
TEST(HostileInput, OversizedPacketsAreNotDelivered) {
    Transport host;
    ASSERT_TRUE(host.listen(Address::loopback(0)));
    TransportConfig big;
    big.maxPacketSize = 4 * 1024 * 1024;
    Transport client(big);
    ASSERT_TRUE(client.startClient());
    const PeerId toHost = client.connect(Address::loopback(host.port()));
    std::vector<TransportEvent> hostEvents, clientEvents;
    ASSERT_TRUE(waitFor([&] {
        host.service(hostEvents);
        client.service(clientEvents);
        return std::ranges::any_of(clientEvents,
                                   [](const auto& e) { return e.type == TransportEvent::Type::Connected; });
    }));
    const std::vector<std::byte> small(16, std::byte{1});
    const std::vector<std::byte> huge(256 * 1024, std::byte{2});
    ASSERT_TRUE(client.send(toHost, Channel::Control, small));
    ASSERT_TRUE(client.send(toHost, Channel::Events, huge));
    std::size_t largest = 0;
    bool gotSmall = false;
    waitFor([&] {
        host.service(hostEvents);
        client.service(clientEvents);
        for (const auto& e : hostEvents) {
            if (e.type != TransportEvent::Type::Received)
                continue;
            largest = std::max(largest, e.payload.size());
            gotSmall = gotSmall || e.payload.size() == small.size();
        }
        hostEvents.clear();
        return false;
    }, 800);
    EXPECT_TRUE(gotSmall);
    EXPECT_LT(largest, huge.size());
}

// --- LAN discovery ---------------------------------------------------------------------

// Who a beacon answers: this machine and the LAN (private ranges, or a subnet
// of one of its interfaces such as a VPN's), never a public source, nor a
// broadcast, multicast or zero address.
TEST(HostileInput, BeaconAnswersOnlyLanSources) {
    const std::vector<Subnet> subnets = {{0xC0A8010Au, 0xFFFFFF00u},  // 192.168.1.10/24
                                         {0x19010203u, 0xFF000000u}}; // 25.1.2.3/8 (a VPN on a public range)
    auto allowed = [&](std::uint32_t ip, std::uint16_t port = 50000) {
        return answersLanQueryFrom(Address{ip, port}, subnets);
    };
    EXPECT_TRUE(allowed(0x7F000001u));  // 127.0.0.1
    EXPECT_TRUE(allowed(0xC0A80114u));  // 192.168.1.20
    EXPECT_TRUE(allowed(0x0A000005u));  // 10.0.0.5, private elsewhere
    EXPECT_TRUE(allowed(0x64400001u));  // 100.64.0.1 (CGNAT, e.g. Tailscale)
    EXPECT_TRUE(allowed(0x19FFFF01u));  // 25.255.255.1 on the VPN subnet
    EXPECT_FALSE(allowed(0x08080808u)); // 8.8.8.8
    EXPECT_FALSE(allowed(0x1A000001u)); // 26.0.0.1
    EXPECT_FALSE(allowed(0xC0A801FFu)); // 192.168.1.255: the subnet's broadcast
    EXPECT_FALSE(allowed(0xC0A80100u)); // 192.168.1.0
    EXPECT_FALSE(allowed(0x19FFFFFFu)); // the VPN's broadcast
    EXPECT_FALSE(allowed(0xFFFFFFFFu));
    EXPECT_FALSE(allowed(0xE0000001u)); // 224.0.0.1
    EXPECT_FALSE(allowed(0));
    EXPECT_FALSE(allowed(0xC0A80114u, 0)); // no source port to answer
    // This machine's own interfaces count as a LAN.
    const auto local = localSubnets();
    for (const Subnet& s : local) {
        if (~s.mask > 1) {
            EXPECT_TRUE(answersLanQueryFrom(Address{s.ip, 2301}, local));
        }
    }
}

// The beacon answers queries at a bounded rate (it would otherwise reflect up
// to 16 times the traffic it receives to whatever source a query claims).
TEST(HostileInput, BeaconRepliesAreRateLimited) {
    LanBeacon beacon;
    ASSERT_TRUE(beacon.start(0));
    beacon.setAnnounceTargets({});
    beacon.setAnnounceInterval(0);
    LanAdvert advert;
    advert.sessionName = std::string(48, 'x');
    beacon.setAdvert(advert);
    auto sock = UdpSocket::open(Address::loopback(0), false, false);
    ASSERT_TRUE(sock);
    const auto query = encodeLanQuery(1);
    int replies = 0;
    std::array<std::byte, 1500> buf{};
    for (int i = 0; i < 400; ++i) {
        sock->sendTo(Address::loopback(beacon.port()), query);
        if (i % 50 == 49) {
            beacon.update();
            Address from;
            while (sock->receiveFrom(from, buf) > 0)
                ++replies;
        }
    }
    waitFor([&] {
        beacon.update();
        Address from;
        while (sock->receiveFrom(from, buf) > 0)
            ++replies;
        return false;
    }, 200);
    EXPECT_GT(replies, 0);
    EXPECT_LT(replies, 100);
}

// A flood of adverts (spoofed sources and ports) cannot grow the session
// list without bound, and their text is cleaned up.
TEST(HostileInput, ScannerBoundsAndCleansAdverts) {
    LanScanner scanner;
    // Listening on the discovery port lets the test deliver "announcements".
    auto probe = UdpSocket::open(Address::loopback(0), false, false);
    ASSERT_TRUE(probe);
    const std::uint16_t port = probe->localAddress().port;
    probe.reset();
    ASSERT_TRUE(scanner.start(port, /*listenForAnnouncements=*/true));
    scanner.setBroadcast(false);
    auto sock = UdpSocket::open(Address::loopback(0), false, false);
    ASSERT_TRUE(sock);
    for (int i = 0; i < 2000; ++i) {
        LanAdvert a;
        a.sessionName = std::format("Fake\n{}\x1b", i);
        a.hostName = "h\r";
        a.city = "../x";
        a.gamePort = static_cast<std::uint16_t>(1000 + i);
        sock->sendTo(Address::loopback(port), encodeLanAdvert(0, a));
        if (i % 100 == 99)
            scanner.update();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    scanner.update();
    const auto sessions = scanner.sessions();
    EXPECT_GT(sessions.size(), 0u);
    EXPECT_LE(sessions.size(), 64u);
    for (const auto& s : sessions) {
        EXPECT_FALSE(hasControl(s.advert.sessionName));
        EXPECT_FALSE(hasControl(s.advert.hostName));
        EXPECT_EQ(s.advert.city.find('/'), std::string::npos);
    }
}

// --- Port forwarding state file ----------------------------------------------------------

// The crash-safe record is read back with its ports checked: a damaged file
// neither wraps a port around nor names internal port 0, for which a PCP or
// NAT-PMP deletion would remove every mapping of this machine.
TEST(HostileInput, DamagedPortMappingRecordIsIgnored) {
    const auto file = std::filesystem::temp_directory_path() /
                      std::format("openmm2-hostile-portmap-{}.ini",
                                  std::chrono::steady_clock::now().time_since_epoch().count());
    auto write = [&](std::string_view internalPort, std::string_view externalPort) {
        std::ofstream out(file, std::ios::trunc);
        out << "[PortMapping]\nMethod=natpmp\nGateway=192.168.1.1\nInternalIp=192.168.1.20\n"
            << "InternalPort=" << internalPort << "\nExternalPort=" << externalPort << "\nToken=\n";
    };
    write("2300", "2300");
    ASSERT_TRUE(loadMappingRecord(file));
    const std::vector<std::pair<std::string, std::string>> damaged = {
        {"0", "2300"}, {"-1", "2300"}, {"70000", "2300"}, {"2300", "65536"}, {"2300", "0"}, {"x", "2300"}};
    for (const auto& [in, ext] : damaged) {
        write(in, ext);
        EXPECT_FALSE(loadMappingRecord(file)) << in << " " << ext;
    }
    std::filesystem::remove(file);

    // The backend refuses such a deletion itself as well.
    MappingRecord all;
    all.method = MappingMethod::NatPmp;
    all.externalPort = 2300;
    std::string error;
    EXPECT_FALSE(makeNatPmpBackend()->unmap(all, error));
    EXPECT_EQ(error, "no internal port");
}
