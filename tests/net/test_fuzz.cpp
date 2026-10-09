// Fuzz-style robustness: random and mutated messages through every decoder
// and through live Session handlers, with fixed seeds so a failure always
// reproduces. Nothing may crash, read out of bounds (run test_net under
// AddressSanitizer/UBSan to check that) or produce a value outside what the
// writer could have produced. See docs/review/multiplayer-input.md.
#include "net/AmbientState.h"
#include "net/Discovery.h"
#include "net/NatPmp.h"
#include "net/Session.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <functional>
#include <random>
#include <thread>

using namespace mm2;
using namespace mm2::net;

namespace {

using Bytes = std::vector<std::byte>;

Bytes bytesOf(std::initializer_list<int> v) {
    Bytes out;
    for (int b : v)
        out.push_back(static_cast<std::byte>(b));
    return out;
}

VehicleSnapshot sampleSnapshot(std::uint32_t time) {
    VehicleSnapshot s;
    s.time = time;
    s.position = {123.5f, -4.0f, 900.25f};
    s.orientation = Quat::fromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, 0.7f);
    s.linearVelocity = {12.0f, 0.5f, -3.0f};
    s.angularVelocity = {0.1f, 1.5f, 0.0f};
    s.controls = {0.3f, 1.0f, 0.0f, 0.0f, 3};
    s.damage = 0.25f;
    s.flags = kVehicleHeadlights;
    return s;
}

PlayerInfo samplePlayer(std::uint8_t id) {
    PlayerInfo p;
    p.id = id;
    p.name = std::format("Player {}", id);
    p.car = "vpmustang99";
    p.color = 3;
    p.team = 1;
    p.ready = true;
    p.ping = 42;
    return p;
}

SessionSettings sampleSettings() {
    SessionSettings s;
    s.name = "Fuzz session";
    s.city = "sf";
    s.mode = GameMode::CopsAndRobbers;
    s.raceId = 2;
    for (int i = 0; i < 6; ++i)
        s.extra.emplace_back(std::format("key{}", i), std::format("value {}", i * 7));
    return s;
}

// One valid encoding of every message, LAN packet, router reply and event payload.
std::vector<Bytes> corpus() {
    std::vector<Bytes> c;
    c.push_back(encodeMessage(ChallengeMsg{{std::byte{1}, std::byte{2}}, true}));
    HelloMsg hello;
    hello.build = "OpenMM2 test";
    hello.name = "Fuzzer";
    hello.car = "vpbug";
    hello.passwordProof[5] = std::byte{9};
    c.push_back(encodeMessage(hello));
    WelcomeMsg w;
    w.yourId = 3;
    w.settings = sampleSettings();
    for (std::uint8_t i = 0; i < 8; ++i)
        w.players.push_back(samplePlayer(i));
    w.phase = SessionPhase::Countdown;
    w.hostTime = 123456;
    w.countdownEnd = 130000;
    c.push_back(encodeMessage(w));
    c.push_back(encodeMessage(RejectMsg{DisconnectReason::ServerFull, "full"}));
    c.push_back(encodeMessage(PlayerJoinedMsg{samplePlayer(4)}));
    c.push_back(encodeMessage(PlayerLeftMsg{4, DisconnectReason::Timeout}));
    c.push_back(encodeMessage(PlayerUpdateMsg{samplePlayer(2)}));
    c.push_back(encodeMessage(PlayerRequestMsg{"vpcoop", 4, 1, true}));
    c.push_back(encodeMessage(ChatMsg{1, "hello there, é"}));
    c.push_back(encodeMessage(SettingsMsg{sampleSettings()}));
    c.push_back(encodeMessage(CountdownMsg{99999}));
    c.push_back(encodeMessage(ReturnToLobbyMsg{}));
    c.push_back(encodeMessage(KickMsg{"bye"}));
    c.push_back(encodeMessage(TimeRequestMsg{777}));
    c.push_back(encodeMessage(TimeResponseMsg{777, 888}));
    c.push_back(encodeMessage(VehicleStateMsg{sampleSnapshot(5000)}));
    WorldStateMsg world;
    for (std::uint8_t i = 0; i < kMaxPlayers; ++i)
        world.vehicles.emplace_back(i, sampleSnapshot(5000 + i));
    c.push_back(encodeMessage(world));
    GameEventMsg ev;
    ev.from = 1;
    ev.type = static_cast<std::uint16_t>(GameEventType::GoldDropped);
    ev.time = 4242;
    ev.payload = encodePayload(GoldEvent{{1.0f, 2.0f, 3.0f}, 1});
    c.push_back(encodeMessage(ev));
    PlayerPingsMsg pings;
    pings.pings = {{1, 30}, {2, 60}};
    c.push_back(encodeMessage(pings));
    // The shared cruise traffic: a rail car, a knocked car and a police car.
    AmbientStateMsg ambient;
    ambient.time = 5000;
    ambient.lightSteps = 300;
    ambient.catalog = 0x45C1;
    ambient.setOrigin({-1150.0f, 112.0f, 163.0f});
    AmbientEntity car;
    car.id = 12;
    car.generation = 3;
    car.model = 4;
    car.paint = 2;
    car.position = {-1140.0f, 111.0f, 150.0f};
    car.orientation = Quat::fromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, 0.4f);
    car.speed = 11.0f;
    car.flags = kAmbientBrake | kAmbientSignalRight;
    ambient.entities.push_back(car);
    car.id = 13;
    car.flags = kAmbientOffRail | kAmbientWrecked;
    car.velocity = {3.0f, -1.0f, 2.0f};
    car.angularVelocity = {0.5f, 1.0f, -0.5f};
    ambient.entities.push_back(car);
    car.id = 401;
    car.kind = AmbientKind::Police;
    car.flags = kAmbientSiren | kAmbientPursuit;
    car.target = 1;
    car.damage = 0.3f;
    car.rpm = 4500.0f;
    car.throttle = 1.0f;
    car.gear = 3;
    ambient.entities.push_back(car);
    c.push_back(encodeMessage(ambient));

    LanAdvert advert;
    advert.sessionName = "Fuzz";
    advert.hostName = "Host";
    advert.city = "london";
    advert.players = 2;
    advert.maxPlayers = 8;
    advert.build = "OpenMM2";
    c.push_back(encodeLanQuery(0x12345678));
    c.push_back(encodeLanAdvert(0x12345678, advert));

    c.push_back(encodePayload(CheckpointEvent{3, 1000}));
    c.push_back(encodePayload(LapEvent{2, 60000}));
    c.push_back(encodePayload(FinishEvent{90000, 1}));
    c.push_back(encodePayload(CollisionEvent{2, {1, 2, 3}, 50.0f}));
    c.push_back(encodePayload(DamageEvent{0.5f, 3}));

    // A PCP MAP success (60 bytes) and NAT-PMP replies.
    Bytes pcp(60, std::byte{0});
    pcp[0] = std::byte{2};
    pcp[1] = std::byte{0x81};
    pcp[36] = std::byte{17};
    pcp[40] = std::byte{0x08};
    pcp[41] = std::byte{0xFC};
    c.push_back(pcp);
    c.push_back(bytesOf({0, 0x80, 0, 0, 0, 0, 1, 0, 203, 0, 113, 7}));
    c.push_back(bytesOf({0, 0x81, 0, 0, 0, 0, 1, 0, 0x08, 0xFC, 0x08, 0xFC, 0, 0, 0x0E, 0x10}));
    return c;
}

Bytes mutate(const std::vector<Bytes>& corpus, std::mt19937& rng) {
    auto pick = [&](std::size_t n) { return std::uniform_int_distribution<std::size_t>(0, n - 1)(rng); };
    auto randomByte = [&] { return static_cast<std::byte>(rng() & 0xFF); };
    Bytes b = corpus[pick(corpus.size())];
    const int rounds = 1 + static_cast<int>(pick(4));
    for (int r = 0; r < rounds; ++r) {
        switch (pick(8)) {
        case 0: // flip bits
            for (std::size_t i = 0, n = 1 + pick(8); i < n && !b.empty(); ++i)
                b[pick(b.size())] ^= static_cast<std::byte>(1u << pick(8));
            break;
        case 1: // overwrite bytes
            for (std::size_t i = 0, n = 1 + pick(4); i < n && !b.empty(); ++i)
                b[pick(b.size())] = randomByte();
            break;
        case 2: // truncate
            if (!b.empty())
                b.resize(pick(b.size()));
            break;
        case 3: // append
            for (std::size_t i = 0, n = 1 + pick(64); i < n; ++i)
                b.push_back(randomByte());
            break;
        case 4: // insert
            for (std::size_t i = 0, n = 1 + pick(8); i < n; ++i)
                b.insert(b.begin() + static_cast<std::ptrdiff_t>(pick(b.size() + 1)), randomByte());
            break;
        case 5: { // splice with another sample
            const Bytes& o = corpus[pick(corpus.size())];
            if (!b.empty() && !o.empty()) {
                b.resize(pick(b.size()));
                b.insert(b.end(), o.begin() + static_cast<std::ptrdiff_t>(pick(o.size())), o.end());
            }
            break;
        }
        case 6: // another message type
            if (!b.empty())
                b[0] = static_cast<std::byte>(pick(24));
            break;
        default: // pure noise
            b.resize(pick(300));
            for (auto& x : b)
                x = randomByte();
            break;
        }
    }
    return b;
}

void checkSnapshot(const VehicleSnapshot& s) {
    for (float v : {s.position.x, s.position.y, s.position.z, s.linearVelocity.x, s.linearVelocity.y,
                    s.linearVelocity.z, s.angularVelocity.x, s.angularVelocity.y, s.angularVelocity.z})
        ASSERT_TRUE(std::isfinite(v));
    ASSERT_LE(std::abs(s.position.x), kSnapshotPositionRange);
    ASSERT_LE(std::abs(s.linearVelocity.y), kSnapshotVelocityRange);
    ASSERT_LE(std::abs(s.angularVelocity.z), kSnapshotAngularRange);
    const float n = std::sqrt(s.orientation.x * s.orientation.x + s.orientation.y * s.orientation.y +
                              s.orientation.z * s.orientation.z + s.orientation.w * s.orientation.w);
    ASSERT_NEAR(n, 1.0f, 1e-3f);
    ASSERT_GE(s.controls.steering, -1.0f);
    ASSERT_LE(s.controls.steering, 1.0f);
    ASSERT_GE(s.controls.gear, -1);
    ASSERT_LE(s.controls.gear, 14);
    ASSERT_GE(s.damage, 0.0f);
    ASSERT_LE(s.damage, 1.0f);
}

void checkAmbient(const AmbientStateMsg& m) {
    ASSERT_LE(m.entities.size(), kMaxAmbientPerMessage);
    const Vec3 origin = m.originVec();
    for (const AmbientEntity& e : m.entities) {
        ASSERT_LT(e.id, kMaxAmbientIds);
        ASSERT_LT(e.generation, kAmbientGenerations);
        ASSERT_LE(e.kind, AmbientKind::Last);
        ASSERT_LT(e.model, kMaxAmbientModels);
        ASSERT_LE(e.paint, kMaxAmbientPaint);
        for (float v : {e.position.x, e.position.y, e.position.z, e.velocity.x, e.velocity.y, e.velocity.z,
                        e.angularVelocity.x, e.angularVelocity.y, e.angularVelocity.z, e.speed, e.damage, e.rpm,
                        e.throttle})
            ASSERT_TRUE(std::isfinite(v));
        ASSERT_LE(std::abs(e.position.x - origin.x), kAmbientOffsetRange + 0.01f);
        ASSERT_LE(std::abs(e.position.y - origin.y), kAmbientHeightRange + 0.01f);
        ASSERT_LE(std::abs(e.velocity.x), kAmbientVelocityRange + 0.01f);
        ASSERT_TRUE(e.target == kAmbientNoTarget || e.target < kMaxPlayers);
        ASSERT_GE(e.gear, -1);
        ASSERT_LE(e.gear, 8);
        const float n = std::sqrt(e.orientation.x * e.orientation.x + e.orientation.y * e.orientation.y +
                                  e.orientation.z * e.orientation.z + e.orientation.w * e.orientation.w);
        ASSERT_NEAR(n, 1.0f, 1e-3f);
    }
}

void checkPlayer(const PlayerInfo& p) {
    ASSERT_LE(p.name.size(), kMaxNameLength);
    ASSERT_LE(p.car.size(), kMaxShortStringLength);
}

void checkSettings(const SessionSettings& s) {
    ASSERT_LE(s.name.size(), kMaxNameLength * 2);
    ASSERT_LE(s.city.size(), kMaxShortStringLength);
    ASSERT_LE(s.mode, GameMode::Last);
    ASSERT_LE(s.timeOfDay, TimeOfDay::Last);
    ASSERT_LE(s.weather, Weather::Last);
    ASSERT_GE(s.maxPlayers, 1);
    ASSERT_LE(s.maxPlayers, kMaxPlayers);
    ASSERT_LE(s.extra.size(), kMaxExtraSettings);
    for (const auto& [k, v] : s.extra) {
        ASSERT_LE(k.size(), kMaxShortStringLength);
        ASSERT_LE(v.size(), kMaxExtraValueLength);
    }
}

template <class M>
std::optional<M> decoded(std::span<const std::byte> b) {
    M m{};
    if (!decodeMessage(b, m))
        return std::nullopt;
    return m;
}

template <class E>
std::optional<E> payload(std::span<const std::byte> b) {
    E e{};
    if (!decodePayload(b, e))
        return std::nullopt;
    return e;
}

// Every decoder on one buffer; whatever decodes must be within the limits.
void decodeEverything(std::span<const std::byte> b) {
    (void)peekMessageType(b);
    (void)decoded<ChallengeMsg>(b);
    (void)decoded<CountdownMsg>(b);
    (void)decoded<ReturnToLobbyMsg>(b);
    (void)decoded<TimeRequestMsg>(b);
    (void)decoded<TimeResponseMsg>(b);
    if (const auto m = decoded<HelloMsg>(b)) {
        ASSERT_LE(m->build.size(), kMaxShortStringLength);
        ASSERT_LE(m->name.size(), kMaxNameLength);
        ASSERT_LE(m->car.size(), kMaxShortStringLength);
    }
    if (const auto m = decoded<WelcomeMsg>(b)) {
        checkSettings(m->settings);
        ASSERT_LE(m->players.size(), kMaxPlayers);
        for (const auto& p : m->players)
            checkPlayer(p);
        ASSERT_LE(m->phase, SessionPhase::Last);
    }
    if (const auto m = decoded<RejectMsg>(b)) {
        ASSERT_LE(m->reason, DisconnectReason::Last);
        ASSERT_LE(m->message.size(), kMaxReasonLength);
    }
    if (const auto m = decoded<PlayerJoinedMsg>(b)) {
        checkPlayer(m->player);
    }
    if (const auto m = decoded<PlayerLeftMsg>(b)) {
        ASSERT_LE(m->reason, DisconnectReason::Last);
    }
    if (const auto m = decoded<PlayerUpdateMsg>(b)) {
        checkPlayer(m->player);
    }
    if (const auto m = decoded<PlayerRequestMsg>(b)) {
        ASSERT_LE(m->car.size(), kMaxShortStringLength);
    }
    if (const auto m = decoded<ChatMsg>(b)) {
        ASSERT_LE(m->text.size(), kMaxChatLength);
    }
    if (const auto m = decoded<SettingsMsg>(b)) {
        checkSettings(m->settings);
    }
    if (const auto m = decoded<KickMsg>(b)) {
        ASSERT_LE(m->reason.size(), kMaxReasonLength);
    }
    if (const auto m = decoded<VehicleStateMsg>(b)) {
        checkSnapshot(m->state);
    }
    if (const auto m = decoded<WorldStateMsg>(b)) {
        ASSERT_LE(m->vehicles.size(), kMaxPlayers);
        for (const auto& [id, s] : m->vehicles)
            checkSnapshot(s);
    }
    if (const auto m = decoded<GameEventMsg>(b)) {
        ASSERT_LE(m->payload.size(), kMaxEventPayload);
    }
    if (const auto m = decoded<PlayerPingsMsg>(b)) {
        ASSERT_LE(m->pings.size(), kMaxPlayers);
    }
    if (const auto m = decoded<AmbientStateMsg>(b)) {
        checkAmbient(*m);
    }

    std::uint32_t nonce = 0;
    (void)decodeLanQuery(b, nonce);
    if (LanAdvert a; decodeLanAdvert(b, nonce, a)) {
        ASSERT_LE(a.sessionName.size(), kMaxNameLength * 2);
        ASSERT_LE(a.city.size(), kMaxShortStringLength);
        ASSERT_LE(a.mode, GameMode::Last);
    }
    (void)natpmp::decodePcpResponse(b);
    (void)natpmp::decodeNatPmpResponse(b);

    (void)payload<CheckpointEvent>(b);
    (void)payload<LapEvent>(b);
    (void)payload<FinishEvent>(b);
    if (const auto e = payload<GoldEvent>(b)) {
        ASSERT_TRUE(std::isfinite(e->position.x) && std::isfinite(e->position.y) &&
                    std::isfinite(e->position.z));
    }
    if (const auto e = payload<CollisionEvent>(b)) {
        ASSERT_TRUE(std::isfinite(e->impulse) && std::isfinite(e->position.x));
    }
    if (const auto e = payload<DamageEvent>(b)) {
        ASSERT_TRUE(std::isfinite(e->damage));
    }

    // Text cleanup on arbitrary bytes.
    const std::string_view text(reinterpret_cast<const char*>(b.data()), b.size());
    const std::string clean = sanitizeText(text, kMaxChatLength);
    ASSERT_LE(clean.size(), kMaxChatLength);
    ASSERT_EQ(sanitizeText(clean, kMaxChatLength), clean); // idempotent: already clean
}

bool pumpFor(int ms, const std::function<void()>& step) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        step();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

template <class M>
std::optional<M> takeMessage(std::vector<TransportEvent>& events) {
    for (auto it = events.begin(); it != events.end(); ++it) {
        M m;
        if (it->type == TransportEvent::Type::Received && decodeMessage(it->payload, m)) {
            events.erase(it);
            return m;
        }
    }
    return std::nullopt;
}

} // namespace

TEST(Fuzz, DecodersSurviveMutatedMessages) {
    const auto c = corpus();
    for (const auto& b : c)
        decodeEverything(b); // the valid samples themselves
    std::mt19937 rng(20261008);
    for (int i = 0; i < 40000; ++i) {
        const Bytes b = mutate(c, rng);
        decodeEverything(b);
        if (HasFatalFailure()) {
            ADD_FAILURE() << "iteration " << i;
            return;
        }
    }
}

// A host keeps serving its session while a joined client and a peer that
// never said Hello send it mutated messages.
TEST(Fuzz, HostSurvivesMutatedTraffic) {
    SessionConfig cfg;
    cfg.joinTimeoutMs = 60000;
    Session host(cfg);
    HostParams hp;
    hp.bind = Address::loopback(0);
    hp.advertiseOnLan = false;
    hp.player.name = "Host";
    ASSERT_TRUE(host.host(hp));

    // `joined` completes the handshake; `stranger` stays before Hello.
    Transport joined, stranger;
    ASSERT_TRUE(joined.startClient());
    ASSERT_TRUE(stranger.startClient());
    const PeerId toHost = joined.connect(Address::loopback(host.port()));
    const PeerId strangerToHost = stranger.connect(Address::loopback(host.port()));
    std::vector<TransportEvent> events, strangerEvents;
    std::optional<ChallengeMsg> challenge;
    pumpFor(300, [&] {
        host.update();
        joined.service(events);
        stranger.service(strangerEvents);
        if (!challenge)
            challenge = takeMessage<ChallengeMsg>(events);
    });
    ASSERT_TRUE(challenge);
    HelloMsg hello;
    hello.name = "Fuzzer";
    hello.car = "vpbug";
    joined.send(toHost, Channel::Control, encodeMessage(hello));
    pumpFor(200, [&] {
        host.update();
        joined.service(events);
        stranger.service(strangerEvents);
    });
    ASSERT_EQ(host.players().size(), 2u);

    // And raw datagrams at the game port, below ENet (its own headers,
    // checksums-free framing and range-coder decompression).
    auto raw = UdpSocket::open(Address::loopback(0), false, false);
    ASSERT_TRUE(raw);

    const auto c = corpus();
    std::mt19937 rng(4242);
    for (int i = 0; i < 3000; ++i) {
        const Bytes b = mutate(c, rng);
        const auto channel = static_cast<Channel>(rng() % kChannelCount);
        joined.send(toHost, channel, b);
        if (i % 4 == 0)
            stranger.send(strangerToHost, channel, b);
        if (i % 3 == 0)
            raw->sendTo(Address::loopback(host.port()), b);
        if (i % 50 == 0) {
            host.update();
            joined.service(events);
            stranger.service(strangerEvents);
            events.clear();
            strangerEvents.clear();
        }
    }
    pumpFor(300, [&] {
        host.update();
        joined.service(events);
        stranger.service(strangerEvents);
        events.clear();
        strangerEvents.clear();
    });
    EXPECT_EQ(host.state(), Session::State::Active);
    EXPECT_LE(host.players().size(), static_cast<std::size_t>(host.settings().maxPlayers));
    for (const auto& p : host.players()) {
        EXPECT_TRUE(isValidAssetName(p.car)) << p.car;
        EXPECT_EQ(sanitizeText(p.name, kMaxNameLength), p.name);
    }
    for (auto& e : host.takeEvents()) {
        if (const auto* chat = std::get_if<ev::Chat>(&e)) {
            EXPECT_EQ(sanitizeText(chat->text, kMaxChatLength), chat->text);
        }
    }
}

// A client keeps a consistent view while a host sends it mutated messages
// (except the ones that end the session).
TEST(Fuzz, ClientSurvivesMutatedTraffic) {
    Transport fakeHost;
    ASSERT_TRUE(fakeHost.listen(Address::loopback(0)));
    Session client;
    JoinParams jp;
    jp.host = Address::loopback(fakeHost.port());
    jp.player.name = "Victim";
    ASSERT_TRUE(client.join(jp));
    std::vector<TransportEvent> events;
    PeerId peer = kInvalidPeer;
    pumpFor(200, [&] {
        client.update();
        fakeHost.service(events);
        for (const auto& e : events)
            if (e.type == TransportEvent::Type::Connected)
                peer = e.peer;
    });
    ASSERT_NE(peer, kInvalidPeer);
    fakeHost.send(peer, Channel::Control, encodeMessage(ChallengeMsg{}));
    pumpFor(100, [&] {
        client.update();
        fakeHost.service(events);
    });
    WelcomeMsg w;
    w.yourId = 1;
    w.players = {samplePlayer(0), samplePlayer(1)};
    fakeHost.send(peer, Channel::Control, encodeMessage(w));
    pumpFor(100, [&] {
        client.update();
        fakeHost.service(events);
    });
    ASSERT_EQ(client.state(), Session::State::Active);

    const auto c = corpus();
    std::mt19937 rng(777);
    for (int i = 0; i < 3000; ++i) {
        Bytes b = mutate(c, rng);
        if (const auto t = peekMessageType(b); t == MsgType::Kick || t == MsgType::Reject)
            b[0] = static_cast<std::byte>(MsgType::Chat);
        fakeHost.send(peer, static_cast<Channel>(rng() % kChannelCount), b);
        if (i % 50 == 0) {
            client.update();
            fakeHost.service(events);
            events.clear();
        }
    }
    pumpFor(300, [&] {
        client.update();
        fakeHost.service(events);
        events.clear();
    });
    EXPECT_EQ(client.state(), Session::State::Active);
    EXPECT_LE(client.players().size(), kMaxPlayers);
    EXPECT_EQ(client.player(kInvalidPlayerId), nullptr);
    for (const auto& p : client.players()) {
        EXPECT_TRUE(isValidAssetName(p.car)) << p.car;
        EXPECT_EQ(sanitizeText(p.name, kMaxNameLength), p.name);
    }
    const auto& s = client.settings();
    EXPECT_TRUE(s.city.empty() || isValidAssetName(s.city)) << s.city;
    EXPECT_EQ(sanitizeText(s.name, kMaxNameLength * 2), s.name);
    VehicleSnapshot snap;
    for (int id = 0; id < 256; ++id) {
        if (client.sampleRemote(static_cast<std::uint8_t>(id), snap) != SnapshotBuffer::Result::Empty) {
            checkSnapshot(snap);
        }
    }
    // The shared traffic it kept: a bounded queue of messages within limits.
    const auto ambient = client.takeAmbientStates();
    EXPECT_LE(ambient.size(), Session::kMaxQueuedAmbientStates);
    for (const auto& m : ambient)
        checkAmbient(m);
}
