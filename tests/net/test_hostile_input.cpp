// Hostile and malformed network input: a fake host or client built on a raw
// Transport sends what an honest OpenMM2 never would, and the real Session,
// LAN scanner and beacon must stay consistent (docs/review/multiplayer-input.md).
#include "net/Discovery.h"
#include "net/Session.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <format>
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
        return std::ranges::any_of(events, [](const TransportEvent& e) { return e.type == TransportEvent::Type::Connected; });
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
        return client.session->state() == Session::State::Active || client.session->state() == Session::State::Closed;
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

