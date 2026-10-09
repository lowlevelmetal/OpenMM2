// The shared ambient traffic message (net/AmbientState.h) and its delivery
// over a loopback session.
#include "net/AmbientState.h"
#include "net/Session.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <thread>

using namespace mm2;
using namespace mm2::net;

namespace {

AmbientEntity railCar(std::uint16_t id, const Vec3& pos, float heading, float speed) {
    AmbientEntity e;
    e.id = id;
    e.generation = 5;
    e.kind = AmbientKind::Traffic;
    e.model = 12;
    e.paint = 3;
    e.position = pos;
    e.orientation = Quat::fromAxisAngle({0, 1, 0}, heading);
    e.speed = speed;
    e.flags = kAmbientBrake | kAmbientSignalLeft;
    return e;
}

AmbientEntity copCar(std::uint16_t id, const Vec3& pos) {
    AmbientEntity e;
    e.id = id;
    e.generation = 1;
    e.kind = AmbientKind::Police;
    e.model = 40;
    e.paint = 1;
    e.position = pos;
    e.orientation = Quat::fromAxisAngle(Vec3{0.2f, 1, 0}.normalized(), -2.0f);
    e.velocity = {20.0f, -1.5f, -31.25f};
    e.angularVelocity = {0.5f, -1.25f, 0.0f};
    e.flags = kAmbientSiren | kAmbientPursuit;
    e.target = 3;
    e.damage = 0.4f;
    e.rpm = 5200.0f;
    e.throttle = 1.0f;
    e.gear = 3;
    return e;
}

AmbientStateMsg sampleMessage() {
    AmbientStateMsg m;
    m.time = 98765;
    m.lightSteps = 4321;
    m.catalog = 0xBEEF;
    m.setOrigin({-1501.4f, 34.6f, 558.2f});
    m.entities.push_back(railCar(7, {-1480.31f, 33.02f, 620.77f}, 0.7f, 12.5f));
    m.entities.push_back(copCar(301, {-1600.0f, 40.0f, 500.0f}));
    AmbientEntity knocked = railCar(299, {-1450.0f, 35.0f, 540.0f}, -1.0f, 0.0f);
    knocked.flags = kAmbientOffRail | kAmbientWrecked;
    knocked.velocity = {3.0f, 4.0f, -5.0f};
    knocked.angularVelocity = {1.0f, 2.0f, 3.0f};
    m.entities.push_back(knocked);
    AmbientEntity held;
    held.id = 44;
    held.generation = 2;
    held.hasState = false;
    m.entities.push_back(held);
    return m;
}

} // namespace

TEST(AmbientState, RoundTripKeepsEveryField) {
    const AmbientStateMsg in = sampleMessage();
    const auto packet = encodeMessage(in);
    EXPECT_EQ(peekMessageType(packet), MsgType::AmbientState);
    AmbientStateMsg out;
    ASSERT_TRUE(decodeMessage(packet, out));
    EXPECT_EQ(out.time, 98765u);
    EXPECT_EQ(out.lightSteps, 4321u);
    EXPECT_EQ(out.catalog, 0xBEEF);
    EXPECT_EQ(out.origin[0], -1501);
    EXPECT_EQ(out.origin[1], 35);
    EXPECT_EQ(out.origin[2], 558);
    ASSERT_EQ(out.entities.size(), 4u);
    EXPECT_FALSE(out.entities[3].hasState);
    EXPECT_EQ(out.entities[3].id, 44);
    EXPECT_EQ(out.entities[3].generation, 2);

    const AmbientEntity& rail = out.entities[0];
    EXPECT_EQ(rail.id, 7);
    EXPECT_EQ(rail.generation, 5);
    EXPECT_EQ(rail.kind, AmbientKind::Traffic);
    EXPECT_EQ(rail.model, 12);
    EXPECT_EQ(rail.paint, 3);
    EXPECT_LT(rail.position.dist(in.entities[0].position), 0.04f);
    EXPECT_NEAR(rail.speed, 12.5f, 0.07f);
    // A rail car's velocity is rebuilt along its heading (ai::Traffic's).
    const Vec3 expected = -in.entities[0].orientation.toMatrix().m2 * 12.5f;
    EXPECT_LT(rail.velocity.dist(expected), 0.1f);
    EXPECT_EQ(rail.flags, kAmbientBrake | kAmbientSignalLeft);
    EXPECT_EQ(rail.target, kAmbientNoTarget);

    const AmbientEntity& cop = out.entities[1];
    EXPECT_EQ(cop.id, 301);
    EXPECT_EQ(cop.kind, AmbientKind::Police);
    EXPECT_LT(cop.position.dist(in.entities[1].position), 0.04f);
    EXPECT_LT(cop.velocity.dist(in.entities[1].velocity), 0.05f);
    EXPECT_LT(cop.angularVelocity.dist(in.entities[1].angularVelocity), 0.05f);
    const Mat34 a = cop.orientation.toMatrix(), b = in.entities[1].orientation.toMatrix();
    EXPECT_GT(a.m2.dot(b.m2), 0.9999f);
    EXPECT_EQ(cop.target, 3);
    EXPECT_NEAR(cop.damage, 0.4f, 0.02f);
    EXPECT_NEAR(cop.rpm, 5200.0f, 25.0f);
    EXPECT_NEAR(cop.throttle, 1.0f, 0.001f);
    EXPECT_EQ(cop.gear, 3);

    const AmbientEntity& knocked = out.entities[2];
    EXPECT_TRUE(knocked.fullMotion());
    EXPECT_LT(knocked.velocity.dist({3.0f, 4.0f, -5.0f}), 0.05f);
    EXPECT_LT(knocked.angularVelocity.dist({1.0f, 2.0f, 3.0f}), 0.05f);
}

TEST(AmbientState, OutOfRangeValuesAreClampedNotTrusted) {
    AmbientStateMsg in;
    in.setOrigin({std::numeric_limits<float>::quiet_NaN(), 1e9f, -1e9f});
    EXPECT_EQ(in.origin[0], 0);
    EXPECT_EQ(in.origin[1], 32767);
    EXPECT_EQ(in.origin[2], -32768);
    AmbientEntity e = railCar(1, {}, 0.0f, std::numeric_limits<float>::infinity());
    e.position = {std::numeric_limits<float>::quiet_NaN(), 5000.0f, -5000.0f};
    in.entities.push_back(e);
    AmbientEntity cop = copCar(2, {});
    cop.target = 200; // not a player
    cop.velocity = {std::numeric_limits<float>::quiet_NaN(), 1e6f, -1e6f};
    in.entities.push_back(cop);
    AmbientStateMsg out;
    ASSERT_TRUE(decodeMessage(encodeMessage(in), out));
    for (const auto& o : out.entities) {
        EXPECT_TRUE(std::isfinite(o.position.x) && std::isfinite(o.position.y) &&
                    std::isfinite(o.position.z));
        EXPECT_TRUE(std::isfinite(o.speed));
        EXPECT_LE(std::abs(o.position.x - static_cast<float>(in.origin[0])), kAmbientOffsetRange);
        EXPECT_TRUE(std::isfinite(o.velocity.x) && std::isfinite(o.velocity.y) &&
                    std::isfinite(o.velocity.z));
    }
    EXPECT_NEAR(out.entities[0].speed, kAmbientSpeedRange, 0.001f);
    EXPECT_EQ(out.entities[1].target, kAmbientNoTarget); // no such player: chasing nobody
}

TEST(AmbientState, MalformedMessagesAreRejected) {
    const auto packet = encodeMessage(sampleMessage());
    AmbientStateMsg out;
    // Truncated at every length.
    for (std::size_t n = 0; n < packet.size(); ++n)
        EXPECT_FALSE(decodeMessage(std::span(packet).first(n), out)) << n;
    // Trailing bytes.
    auto padded = packet;
    padded.push_back(std::byte{0xFF});
    EXPECT_FALSE(decodeMessage(padded, out));
    // A count beyond the limit.
    BitWriter w;
    w.writeU8(static_cast<std::uint8_t>(MsgType::AmbientState));
    w.writeU32(1);
    w.writeU32(2);
    w.writeU16(3);
    for (int i = 0; i < 3; ++i)
        w.writeU16(0);
    w.writeVarU32(static_cast<std::uint32_t>(kMaxAmbientPerMessage + 1));
    EXPECT_FALSE(decodeMessage(w.take(), out));
    // A police target that is no player (the 5-bit field's spare values).
    auto copWithTarget = [](std::uint32_t target) {
        BitWriter p;
        p.writeU8(static_cast<std::uint8_t>(MsgType::AmbientState));
        p.writeU32(1);
        p.writeU32(2);
        p.writeU16(3);
        for (int i = 0; i < 3; ++i)
            p.writeU16(0);
        p.writeVarU32(1);
        p.writeBits(4, 9);       // id
        p.writeBits(0, 3);       // generation
        p.writeBits(1, 1);       // with its state
        p.writeBits(1, 1);       // kind: police
        p.writeBits(0, 6);       // model
        p.writeBits(0, 4);       // paint
        p.writeBits(0, 15 + 14 + 15); // position
        p.writeBits(3, 2);       // quaternion: w largest
        p.writeBits(0, 30);
        p.writeU8(0);            // flags
        p.writeBits(0, 3 * 12);  // velocity
        p.writeBits(0, 3 * 11);  // spin
        p.writeBits(target, 5);
        p.writeBits(0, 6 + 8 + 4 + 4); // damage, rpm, throttle, gear
        return p.take();
    };
    AmbientStateMsg cop;
    ASSERT_TRUE(decodeMessage(copWithTarget(2), cop));
    EXPECT_EQ(cop.entities[0].target, 2);
    ASSERT_TRUE(decodeMessage(copWithTarget(static_cast<std::uint32_t>(kMaxPlayers)), cop));
    EXPECT_EQ(cop.entities[0].target, kAmbientNoTarget);
    EXPECT_FALSE(decodeMessage(copWithTarget(20), cop));
    // Random bytes never crash the decoder and never yield an id or model
    // out of range.
    std::uint32_t seed = 12345;
    for (int round = 0; round < 2000; ++round) {
        std::vector<std::byte> junk(1 + (round % 300));
        junk[0] = static_cast<std::byte>(MsgType::AmbientState);
        for (std::size_t i = 1; i < junk.size(); ++i) {
            seed = seed * 1664525u + 1013904223u;
            junk[i] = static_cast<std::byte>(seed >> 24);
        }
        AmbientStateMsg m;
        if (decodeMessage(junk, m)) {
            EXPECT_LE(m.entities.size(), kMaxAmbientPerMessage);
            for (const auto& e : m.entities) {
                EXPECT_LT(e.id, kMaxAmbientIds);
                EXPECT_LT(e.model, kMaxAmbientModels);
                EXPECT_LE(e.paint, kMaxAmbientPaint);
                EXPECT_TRUE(std::isfinite(e.position.x));
            }
        }
    }
}

TEST(AmbientState, SizesFitTheBudget) {
    const AmbientEntity rail = railCar(1, {10, 0, 10}, 0.0f, 10.0f);
    AmbientEntity offRail = rail;
    offRail.flags |= kAmbientOffRail;
    const AmbientEntity cop = copCar(2, {});
    EXPECT_EQ(ambientEntityBits(rail), 119u);
    EXPECT_EQ(ambientEntityBits(offRail), 177u);
    EXPECT_EQ(ambientEntityBits(cop), 204u);
    AmbientEntity held = rail;
    held.hasState = false;
    EXPECT_EQ(ambientEntityBits(held), 13u);
    // 64 rail cars stay under one MTU.
    AmbientStateMsg m;
    for (std::uint16_t i = 0; i < 64; ++i)
        m.entities.push_back(railCar(i, {static_cast<float>(i), 0, 0}, 0.0f, 10.0f));
    EXPECT_LT(encodeMessage(m).size(), 1000u);
}

namespace {

SessionConfig fastConfig() {
    SessionConfig c;
    c.connectTimeoutMs = 3000;
    c.joinTimeoutMs = 3000;
    return c;
}

bool pumpUntil(std::initializer_list<Session*> sessions, const std::function<bool()>& done,
               int timeoutMs = 3000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        for (Session* s : sessions) {
            s->update();
            s->takeEvents();
        }
        if (done())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

} // namespace

TEST(AmbientState, HostSendsEachClientItsOwnState) {
    Session host(fastConfig()), a(fastConfig()), b(fastConfig());
    HostParams hp;
    hp.bind = Address::loopback(0);
    hp.advertiseOnLan = false;
    ASSERT_TRUE(host.host(hp));
    JoinParams jp;
    jp.host = Address::loopback(host.port());
    jp.player.name = "A";
    ASSERT_TRUE(a.join(jp));
    jp.player.name = "B";
    ASSERT_TRUE(b.join(jp));
    ASSERT_TRUE(pumpUntil({&host, &a, &b}, [&] {
        return a.state() == Session::State::Active && b.state() == Session::State::Active &&
               host.players().size() == 3;
    }));
    EXPECT_TRUE(host.settings().sharedTraffic); // on by default
    EXPECT_TRUE(a.settings().sharedTraffic);
    // Not in the lobby: the cars are replicated during a race only.
    EXPECT_EQ(host.sendAmbientState(a.localId(), sampleMessage()), 0u);
    host.startRace(100);
    ASSERT_TRUE(pumpUntil({&host, &a, &b}, [&] {
        for (Session* s : {&host, &a, &b})
            s->reportLoaded(); // nothing to load
        return a.phase() == SessionPhase::InGame && b.phase() == SessionPhase::InGame;
    }));

    AmbientStateMsg forA = sampleMessage();
    forA.time = 1;
    AmbientStateMsg forB = sampleMessage();
    forB.time = 2;
    forB.entities.resize(1);
    EXPECT_GT(host.sendAmbientState(a.localId(), forA), 0u);
    EXPECT_GT(host.sendAmbientState(b.localId(), forB), 0u);
    EXPECT_EQ(host.sendAmbientState(host.localId(), forA), 0u); // never to itself
    EXPECT_EQ(host.sendAmbientState(99, forA), 0u);             // no such player
    EXPECT_EQ(a.sendAmbientState(host.localId(), forA), 0u);    // clients never send it

    std::vector<AmbientStateMsg> gotA, gotB;
    ASSERT_TRUE(pumpUntil({&host, &a, &b}, [&] {
        for (auto& m : a.takeAmbientStates())
            gotA.push_back(std::move(m));
        for (auto& m : b.takeAmbientStates())
            gotB.push_back(std::move(m));
        return !gotA.empty() && !gotB.empty();
    }));
    ASSERT_EQ(gotA.size(), 1u);
    ASSERT_EQ(gotB.size(), 1u);
    EXPECT_EQ(gotA[0].time, 1u);
    EXPECT_EQ(gotA[0].entities.size(), 4u);
    EXPECT_EQ(gotB[0].time, 2u);
    EXPECT_EQ(gotB[0].entities.size(), 1u);

    // The host's setting reaches the clients.
    SessionSettings s = host.settings();
    s.sharedTraffic = false;
    host.updateSettings(s);
    ASSERT_TRUE(pumpUntil({&host, &a, &b},
                          [&] { return !a.settings().sharedTraffic && !b.settings().sharedTraffic; }));
}
