#include "net/Discovery.h"
#include "net/NatPmp.h"
#include "net/Protocol.h"
#include "net/Session.h"

#include <gtest/gtest.h>

using namespace mm2;
using namespace mm2::net;

namespace {

template <class M>
M roundTrip(const M& in) {
    const auto packet = encodeMessage(in);
    EXPECT_EQ(peekMessageType(packet), M::kType);
    M out{};
    EXPECT_TRUE(decodeMessage(packet, out));
    return out;
}

SessionSettings sampleSettings() {
    SessionSettings s;
    s.name = "Friday night";
    s.city = "sf";
    s.mode = GameMode::CopsAndRobbers;
    s.raceId = 7;
    s.laps = 5;
    s.timeOfDay = TimeOfDay::Night;
    s.weather = Weather::Rain;
    s.trafficDensity = 80;
    s.pedDensity = 10;
    s.cops = false;
    s.maxPlayers = 8;
    s.hasPassword = true;
    s.allowJoinInProgress = true;
    s.extra = {{"gold", "3"}, {"teams", "auto"}};
    return s;
}

} // namespace

TEST(Protocol, LobbyMessagesRoundTrip) {
    WelcomeMsg w;
    w.yourId = 3;
    w.settings = sampleSettings();
    w.players = {PlayerInfo{0, "Host", "vpcop", 1, 0, true, true, 0}, PlayerInfo{3, "Joe", "vpbug", 2, 1, false, false, 42}};
    w.phase = SessionPhase::Countdown;
    w.hostTime = 123456;
    w.countdownEnd = 130000;
    const auto w2 = roundTrip(w);
    EXPECT_EQ(w2.yourId, 3);
    EXPECT_EQ(w2.settings, w.settings);
    EXPECT_EQ(w2.players, w.players);
    EXPECT_EQ(w2.phase, SessionPhase::Countdown);
    EXPECT_EQ(w2.countdownEnd, 130000u);

    HelloMsg h;
    h.name = "Player";
    h.car = "vppanoz";
    h.color = 3;
    h.build = "OpenMM2 0.1";
    h.passwordProof[0] = std::byte{0xAA};
    h.passwordProof[31] = std::byte{0x55};
    const auto h2 = roundTrip(h);
    EXPECT_EQ(h2.name, "Player");
    EXPECT_EQ(h2.car, "vppanoz");
    EXPECT_EQ(h2.passwordProof, h.passwordProof);

    ChatMsg c{4, "hello world"};
    EXPECT_EQ(roundTrip(c).text, "hello world");
    EXPECT_EQ(roundTrip(RejectMsg{DisconnectReason::BadPassword, "nope"}).reason, DisconnectReason::BadPassword);
    EXPECT_EQ(roundTrip(PlayerLeftMsg{9, DisconnectReason::Kicked}).reason, DisconnectReason::Kicked);
    EXPECT_EQ(roundTrip(TimeResponseMsg{11, 22}).hostTime, 22u);
    EXPECT_EQ(roundTrip(CountdownMsg{5000}).startTime, 5000u);
    roundTrip(ReturnToLobbyMsg{});

    GameEventMsg e;
    e.from = 1;
    e.target = 2;
    e.type = static_cast<std::uint16_t>(GameEventType::CheckpointReached);
    e.time = 99;
    e.payload = encodePayload(CheckpointEvent{4, 61000});
    const auto e2 = roundTrip(e);
    CheckpointEvent cp;
    ASSERT_TRUE(decodePayload(e2.payload, cp));
    EXPECT_EQ(cp.index, 4);
    EXPECT_EQ(cp.raceTime, 61000u);
}

TEST(Protocol, DecodeRejectsMalformedPackets) {
    const auto packet = encodeMessage(ChatMsg{1, "hi"});
    ChatMsg out;
    // Wrong type.
    KickMsg kick;
    EXPECT_FALSE(decodeMessage(packet, kick));
    // Truncated.
    EXPECT_FALSE(decodeMessage(std::span(packet).first(packet.size() - 1), out));
    // Trailing garbage.
    auto padded = packet;
    padded.push_back(std::byte{0});
    EXPECT_FALSE(decodeMessage(padded, out));
    // Empty and unknown type.
    EXPECT_FALSE(peekMessageType({}));
    const std::byte unknown[] = {std::byte{200}};
    EXPECT_FALSE(peekMessageType(unknown));

    // Settings with an impossible player count.
    SettingsMsg s{sampleSettings()};
    s.settings.maxPlayers = 0;
    SettingsMsg s2;
    EXPECT_FALSE(decodeMessage(encodeMessage(s), s2));

    // Too many players claimed.
    BitWriter w;
    w.writeU8(static_cast<std::uint8_t>(MsgType::WorldState));
    w.writeVarU32(1000);
    WorldStateMsg ws;
    EXPECT_FALSE(decodeMessage(w.take(), ws));
}

TEST(Protocol, WorldStateQuantization) {
    VehicleSnapshot v;
    v.time = 777;
    v.position = {-1501.229f, 34.477f, 558.704f};
    v.orientation = Quat::fromAxisAngle(Vec3{0, 1, 0}, 1.2f);
    v.linearVelocity = {12.5f, -0.25f, -40.0f};
    v.angularVelocity = {0.1f, 2.0f, -0.3f};
    v.controls = {-0.5f, 1.0f, 0.0f, 0.0f, 3};
    v.damage = 0.33f;
    v.flags = kVehicleSiren | kVehicleHeadlights;
    WorldStateMsg m;
    m.vehicles = {{2, v}, {5, v}};
    const auto out = roundTrip(m);
    ASSERT_EQ(out.vehicles.size(), 2u);
    const auto& o = out.vehicles[1].second;
    EXPECT_EQ(out.vehicles[1].first, 5);
    EXPECT_EQ(o.time, 777u);
    EXPECT_LT(o.position.dist(v.position), 0.005f);
    EXPECT_LT(o.linearVelocity.dist(v.linearVelocity), 0.01f);
    EXPECT_LT(o.angularVelocity.dist(v.angularVelocity), 0.01f);
    EXPECT_NEAR(o.controls.steering, -0.5f, 0.01f);
    EXPECT_EQ(o.controls.gear, 3);
    EXPECT_NEAR(o.damage, 0.33f, 0.001f);
    EXPECT_EQ(o.flags, v.flags);
    // A full 16-player world state must fit comfortably in one MTU-sized packet.
    WorldStateMsg big;
    for (std::uint8_t i = 0; i < kMaxPlayers; ++i)
        big.vehicles.emplace_back(i, v);
    EXPECT_LT(encodeMessage(big).size(), 1200u);
}

TEST(Protocol, PasswordProof) {
    std::array<std::byte, 16> nonce{};
    nonce[0] = std::byte{1};
    EXPECT_EQ(passwordProof(nonce, ""), (std::array<std::byte, 32>{}));
    const auto a = passwordProof(nonce, "secret");
    EXPECT_EQ(a, passwordProof(nonce, "secret"));
    EXPECT_NE(a, passwordProof(nonce, "Secret"));
    nonce[1] = std::byte{2};
    EXPECT_NE(a, passwordProof(nonce, "secret")); // fresh nonce, different proof
}

TEST(Protocol, LanAdvertRoundTrip) {
    LanAdvert a;
    a.sessionName = "Lobby";
    a.hostName = "Host";
    a.city = "london";
    a.mode = GameMode::Circuit;
    a.players = 3;
    a.maxPlayers = 8;
    a.hasPassword = true;
    a.gamePort = 2310;
    a.build = "OpenMM2";
    std::uint32_t nonce = 0;
    LanAdvert b;
    ASSERT_TRUE(decodeLanAdvert(encodeLanAdvert(1234, a), nonce, b));
    EXPECT_EQ(nonce, 1234u);
    EXPECT_EQ(a, b);

    ASSERT_TRUE(decodeLanQuery(encodeLanQuery(99), nonce));
    EXPECT_EQ(nonce, 99u);
    EXPECT_FALSE(decodeLanQuery(encodeLanAdvert(1, a), nonce)); // wrong magic

    // Newer protocol: still listed, flagged incompatible.
    a.protocolVersion = kProtocolVersion + 1;
    LanAdvert c;
    ASSERT_TRUE(decodeLanAdvert(encodeLanAdvert(0, a), nonce, c));
    EXPECT_EQ(c.protocolVersion, kProtocolVersion + 1);
}

TEST(Protocol, AddressParsing) {
    auto a = Address::parse("192.168.1.20:2300");
    ASSERT_TRUE(a);
    EXPECT_EQ(a->ip, 0xC0A80114u);
    EXPECT_EQ(a->port, 2300);
    EXPECT_EQ(a->toString(), "192.168.1.20:2300");
    EXPECT_EQ(Address::parse("10.0.0.1", 99)->port, 99);
    EXPECT_FALSE(Address::parse("256.1.1.1"));
    EXPECT_FALSE(Address::parse("1.2.3"));
    EXPECT_FALSE(Address::parse("1.2.3.4:70000"));
    EXPECT_FALSE(Address::parse("a.b.c.d"));
    EXPECT_TRUE(Address::parse("10.1.2.3")->isPrivate());
    EXPECT_TRUE(Address::parse("100.64.0.1")->isPrivate()); // CGNAT
    EXPECT_FALSE(Address::parse("8.8.8.8")->isPrivate());
    auto local = Address::resolve("localhost:5");
    ASSERT_TRUE(local);
    EXPECT_EQ(local->ip, 0x7F000001u);
    EXPECT_EQ(local->port, 5);
}

// --- PCP / NAT-PMP wire format (RFC 6887 / RFC 6886) ---------------------------

TEST(NatPmpCodec, PcpMapRequestLayout) {
    natpmp::Nonce nonce{};
    for (std::size_t i = 0; i < nonce.size(); ++i)
        nonce[i] = static_cast<std::byte>(i + 1);
    const auto p = natpmp::encodePcpMap(nonce, 0xC0A80114, 2300, 2301, 0, 3600);
    ASSERT_EQ(p.size(), 60u);
    auto b = [&](std::size_t i) { return std::to_integer<int>(p[i]); };
    EXPECT_EQ(b(0), 2);    // version
    EXPECT_EQ(b(1), 1);    // MAP, request
    EXPECT_EQ(b(4), 0);    // lifetime 3600 = 0x00000E10
    EXPECT_EQ(b(6), 0x0E);
    EXPECT_EQ(b(7), 0x10);
    EXPECT_EQ(b(18), 0xFF); // ::ffff:192.168.1.20
    EXPECT_EQ(b(19), 0xFF);
    EXPECT_EQ(b(20), 192);
    EXPECT_EQ(b(23), 20);
    EXPECT_EQ(b(24), 1); // nonce
    EXPECT_EQ(b(35), 12);
    EXPECT_EQ(b(36), 17); // UDP
    EXPECT_EQ(b(40), 2300 >> 8);
    EXPECT_EQ(b(41), 2300 & 0xFF);
    EXPECT_EQ(b(42), 2301 >> 8);
    EXPECT_EQ(b(43), 2301 & 0xFF);
    EXPECT_EQ(b(54), 0xFF); // suggested external ::ffff:0.0.0.0
    EXPECT_EQ(b(59), 0);
}

TEST(NatPmpCodec, PcpMapResponseDecode) {
    // Build a success response by flipping a request into reply form.
    natpmp::Nonce nonce{};
    nonce[3] = std::byte{9};
    auto p = natpmp::encodePcpMap(nonce, 0x0A000002, 2300, 2300, 0x01020304, 7200);
    p[1] = std::byte{0x81}; // response bit
    p[3] = std::byte{0};    // result SUCCESS
    p[8] = std::byte{0};    // epoch
    p[9] = std::byte{0};
    p[10] = std::byte{0x10};
    p[11] = std::byte{0};
    for (int i = 12; i < 24; ++i)
        p[i] = std::byte{0};
    const auto r = natpmp::decodePcpResponse(p);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->version, 2);
    EXPECT_EQ(r->opcode, natpmp::kPcpMap);
    EXPECT_EQ(r->result, natpmp::kPcpSuccess);
    EXPECT_EQ(r->lifetime, 7200u);
    EXPECT_EQ(r->epoch, 0x1000u);
    EXPECT_EQ(r->nonce, nonce);
    EXPECT_EQ(r->internalPort, 2300);
    EXPECT_EQ(r->externalPort, 2300);
    EXPECT_EQ(r->externalIp, 0x01020304u);

    // NAT-PMP-only router answering a PCP request: 8-byte "unsupported version".
    const std::byte natpmpReply[] = {std::byte{0}, std::byte{0x81}, std::byte{0}, std::byte{1},
                                     std::byte{0}, std::byte{0},    std::byte{0}, std::byte{5}};
    const auto u = natpmp::decodePcpResponse(natpmpReply);
    ASSERT_TRUE(u);
    EXPECT_EQ(u->version, 0);
    EXPECT_EQ(u->result, natpmp::kPcpUnsuppVersion);

    // Requests are not responses.
    EXPECT_FALSE(natpmp::decodePcpResponse(natpmp::encodePcpAnnounce(0x0A000002)));
}

TEST(NatPmpCodec, NatPmpMessages) {
    const auto req = natpmp::encodeNatPmpMapUdp(2300, 2300, 3600);
    ASSERT_EQ(req.size(), 12u);
    EXPECT_EQ(std::to_integer<int>(req[1]), 1); // map UDP
    EXPECT_EQ(natpmp::encodeNatPmpExternalAddress().size(), 2u);

    const std::byte ext[] = {std::byte{0},  std::byte{128}, std::byte{0},  std::byte{0},
                             std::byte{0},  std::byte{0},   std::byte{1},  std::byte{0},
                             std::byte{203}, std::byte{0},  std::byte{113}, std::byte{5}};
    const auto e = natpmp::decodeNatPmpResponse(ext);
    ASSERT_TRUE(e);
    EXPECT_EQ(e->opcode, 0);
    EXPECT_EQ(e->externalIp, 0xCB007105u);
    EXPECT_EQ(e->epoch, 256u);

    const std::byte map[] = {std::byte{0},    std::byte{129},  std::byte{0}, std::byte{0},  std::byte{0}, std::byte{0},
                             std::byte{0},    std::byte{9},    std::byte{0x08}, std::byte{0xFC},
                             std::byte{0x08}, std::byte{0xFD}, std::byte{0},    std::byte{0},
                             std::byte{0x0E}, std::byte{0x10}};
    const auto m = natpmp::decodeNatPmpResponse(map);
    ASSERT_TRUE(m);
    EXPECT_EQ(m->opcode, 1);
    EXPECT_EQ(m->internalPort, 2300);
    EXPECT_EQ(m->externalPort, 2301);
    EXPECT_EQ(m->lifetime, 3600u);

    const std::byte refused[] = {std::byte{0}, std::byte{129}, std::byte{0}, std::byte{2},
                                 std::byte{0}, std::byte{0},   std::byte{0}, std::byte{1}};
    const auto f = natpmp::decodeNatPmpResponse(refused);
    ASSERT_TRUE(f);
    EXPECT_EQ(f->result, 2);
}
