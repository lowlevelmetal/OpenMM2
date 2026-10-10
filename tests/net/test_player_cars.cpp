// The host-simulated players' cars on the wire (net/PlayerCarState.h): the
// clients' inputs and the host's states, their encodings, what a receiver
// refuses, mutated messages, and their delivery through real sessions.
#include "net/PlayerCarState.h"
#include "net/Session.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <random>
#include <thread>

using namespace mm2;
using namespace mm2::net;

namespace {

using Bytes = std::vector<std::byte>;

// This file's block of ports, below 49152 (see tests/game/test_traffic_net.cpp).
std::uint16_t testPort(int k) {
    static const auto base = static_cast<std::uint16_t>(
        21000 + (std::chrono::steady_clock::now().time_since_epoch().count() / 1000) % 1000 * 4);
    return static_cast<std::uint16_t>(base + k);
}

PlayerInputMsg sampleInput() {
    PlayerInputMsg m;
    m.first = 1234;
    CarInputFrame f;
    f.steering = -127;
    f.throttle = 255;
    f.flags = kInputAutomatic | kInputAutoReverse;
    for (int i = 0; i < 20; ++i) {
        if (i == 5)
            f.events = kInputShiftUp;
        else
            f.events = 0;
        if (i == 9) {
            f.brake = 200;
            f.steering = 64;
        }
        if (i == 15) {
            f.extraMass = 227;
            f.throttleCap = 191;
        }
        m.frames.push_back(f);
    }
    m.commands.push_back({1240, CarCommandKind::RespawnAt, {-1150.5f, 112.25f, 163.0f}, 3.1f});
    m.commands.push_back({1250, CarCommandKind::ClearDamage, {}, 0.0f});
    return m;
}

OwnCarState sampleOwn() {
    OwnCarState o;
    o.matrix = Mat34::rotationY(0.7f);
    o.matrix.m3 = {-1149.94531f, 111.900002f, 163.194f};
    o.linearMomentum = {1234.5678f, -9.87f, 0.001f};
    o.angularMomentum = {0.1f, 1500.25f, -3.0f};
    o.linearVelocity = {1.2345678f, -0.00987f, 0.000001f};
    o.angularVelocity = {0.0001f, 0.5f, -0.003f};
    o.lastPush = {0.0f, 0.002f, 0.0f};
    for (std::size_t i = 0; i < 4; ++i)
        o.wheels[i] = {-30.5f - static_cast<float>(i), 12.0f, 0.05f, -0.2f, 0.001f, -0.002f};
    o.engineSpeed = 420.0f;
    o.gearChangeTime = 0.1f;
    o.prevGearRpm = 5000.0f;
    o.changingGear = true;
    o.gear = 4;
    o.automatic = false;
    o.timeInGear = 2.5f;
    o.drivetrainSpeed = {-30.0f, -31.0f, -32.0f};
    o.damage = 1234.0f;
    o.stuckState = 3;
    o.stuckActive = true;
    o.stuckTime = 0.4f;
    o.stuckPosition = {-1150.0f, 112.0f, 160.0f};
    o.random = 0xDEADBEEF;
    o.swapThrottle = true;
    o.held = false;
    o.resets = 7;
    o.force = {-13.98f, 22884.0f, -51.13f};
    o.torque = {-461.3f, -16.6f, 419.6f};
    o.tireResistance = {4.77f, -0.42f, -2320.78f, 0.0f};
    o.contact = true;
    o.linearImpulse = {1500.0f, 0.0f, -20.0f};
    o.angularImpulse = {0.0f, 300.0f, 0.0f};
    o.linearPush = {0.001f, 0.0f, 0.002f};
    o.turnForce = {0.0f, 0.0001f, 0.0f};
    o.framePush = {0.003f, 0.0f, 0.0f};
    o.hasBound = true;
    o.bound = Mat34::rotationY(0.69f);
    o.bound.m3 = {-1149.9f, 112.2f, 163.1f};
    o.pusher = 3;
    return o;
}

CarStatesMsg sampleStates(std::size_t cars, std::size_t near = 0) {
    CarStatesMsg m;
    m.time = 98765;
    m.ack = 4321;
    m.waiting = -2;
    m.hasOwn = true;
    m.own = sampleOwn();
    for (std::size_t i = 0; i < cars; ++i) {
        VehicleSnapshot s;
        s.time = m.time;
        s.position = {100.0f + static_cast<float>(i), 5.0f, -20.0f};
        s.orientation = Quat::fromAxisAngle(Vec3{0, 1, 0}, 0.3f * static_cast<float>(i));
        s.linearVelocity = {10.0f, 0.0f, 1.0f};
        s.controls.throttle = 1.0f;
        s.damage = 0.5f;
        m.cars.emplace_back(static_cast<std::uint8_t>(i), s);
    }
    for (std::size_t i = 0; i < near; ++i) {
        NearCarState n;
        n.id = static_cast<std::uint8_t>(i);
        n.state = sampleOwn();
        n.state.matrix.m3.x += 3.0f * static_cast<float>(i);
        n.input = sampleInput().frames[9];
        const auto frames = sampleInput().frames;
        n.upcoming.assign(frames.begin(), frames.begin() + 6);
        m.nearIds.push_back(n.id);
        m.near.push_back(n);
    }
    return m;
}

bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

void expectSameOwn(const OwnCarState& a, const OwnCarState& b) {
    const auto vec = [](const Vec3& x, const Vec3& y) {
        return sameBits(x.x, y.x) && sameBits(x.y, y.y) && sameBits(x.z, y.z);
    };
    EXPECT_TRUE(vec(a.matrix.m0, b.matrix.m0) && vec(a.matrix.m1, b.matrix.m1));
    EXPECT_TRUE(vec(a.matrix.m2, b.matrix.m2) && vec(a.matrix.m3, b.matrix.m3));
    EXPECT_TRUE(vec(a.linearMomentum, b.linearMomentum) && vec(a.angularMomentum, b.angularMomentum));
    EXPECT_TRUE(vec(a.linearVelocity, b.linearVelocity) && vec(a.angularVelocity, b.angularVelocity));
    EXPECT_TRUE(vec(a.lastPush, b.lastPush) && vec(a.stuckPosition, b.stuckPosition));
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(sameBits(a.wheels[i].rotationSpeed, b.wheels[i].rotationSpeed));
        EXPECT_TRUE(sameBits(a.wheels[i].tireDispLong, b.wheels[i].tireDispLong));
    }
    EXPECT_EQ(a.gear, b.gear);
    EXPECT_EQ(a.automatic, b.automatic);
    EXPECT_EQ(a.changingGear, b.changingGear);
    EXPECT_TRUE(sameBits(a.engineSpeed, b.engineSpeed));
    EXPECT_TRUE(sameBits(a.damage, b.damage));
    EXPECT_EQ(a.drivetrainSpeed, b.drivetrainSpeed);
    EXPECT_EQ(a.stuckState, b.stuckState);
    EXPECT_EQ(a.random, b.random);
    EXPECT_EQ(a.swapThrottle, b.swapThrottle);
    EXPECT_EQ(a.held, b.held);
    EXPECT_EQ(a.resets, b.resets);
    EXPECT_TRUE(vec(a.force, b.force) && vec(a.torque, b.torque));
    for (std::size_t i = 0; i < 4; ++i)
        EXPECT_TRUE(sameBits(a.tireResistance[i], b.tireResistance[i]));
    EXPECT_EQ(a.contact, b.contact);
    EXPECT_TRUE(vec(a.linearImpulse, b.linearImpulse) && vec(a.angularImpulse, b.angularImpulse));
    EXPECT_TRUE(vec(a.linearPush, b.linearPush) && vec(a.turnForce, b.turnForce) &&
                vec(a.framePush, b.framePush));
    EXPECT_EQ(a.hasBound, b.hasBound);
    EXPECT_TRUE(vec(a.bound.m0, b.bound.m0) && vec(a.bound.m1, b.bound.m1) && vec(a.bound.m2, b.bound.m2) &&
                vec(a.bound.m3, b.bound.m3));
    EXPECT_EQ(a.pusher, b.pusher);
}

// What a decoded message may hold, whatever arrived.
void checkInput(const PlayerInputMsg& m) {
    ASSERT_GE(m.frames.size(), 1u);
    ASSERT_LE(m.frames.size(), kMaxInputFrames);
    ASSERT_GE(m.first, 1u);
    ASSERT_LE(m.commands.size(), kMaxCarCommands);
    for (const auto& f : m.frames) {
        ASSERT_EQ(f.flags & ~kCarInputFlagMask, 0);
        ASSERT_EQ(f.events & ~kCarInputEventMask, 0);
        ASSERT_LE(f.extraMass, kMaxExtraMass);
    }
    for (const auto& c : m.commands) {
        ASSERT_LE(c.kind, CarCommandKind::Last);
        for (float v : {c.position.x, c.position.y, c.position.z}) {
            ASSERT_TRUE(std::isfinite(v));
            ASSERT_LE(std::abs(v), kMaxCommandCoordinate);
        }
        ASSERT_LE(std::abs(c.rotation), kMaxCommandAngle);
    }
}

// What a sample hands the next and a contact's bookkeeping (protocol 17).
void checkCarry(const OwnCarState& c) {
    const auto within = [](const Vec3& v, float r) {
        return std::abs(v.x) <= r && std::abs(v.y) <= r && std::abs(v.z) <= r;
    };
    for (const Vec3& v : {c.force, c.torque, c.linearImpulse, c.angularImpulse, c.linearPush, c.turnForce,
                          c.framePush})
        ASSERT_TRUE(within(v, kOwnStateMaxValue));
    for (float r : c.tireResistance)
        ASSERT_LE(std::abs(r), kOwnStateMaxValue);
    ASSERT_TRUE(within(c.bound.m0, 2.0f) && within(c.bound.m1, 2.0f) && within(c.bound.m2, 2.0f));
    ASSERT_TRUE(within(c.bound.m3, kOwnStateMaxCoordinate));
}

void checkStates(const CarStatesMsg& m) {
    ASSERT_LE(m.cars.size(), kMaxPlayers);
    ASSERT_LE(std::abs(m.waiting), kMaxReportedWaiting);
    ASSERT_LE(m.nearIds.size(), kMaxNearCars);
    if (!m.hasOwn)
        return;
    const OwnCarState& o = m.own;
    checkCarry(o);
    for (const Vec3& v : {o.matrix.m0, o.matrix.m1, o.matrix.m2})
        ASSERT_TRUE(std::abs(v.x) <= 2.0f && std::abs(v.y) <= 2.0f && std::abs(v.z) <= 2.0f);
    for (const Vec3& v : {o.matrix.m3, o.stuckPosition})
        ASSERT_TRUE(std::abs(v.x) <= kOwnStateMaxCoordinate && std::abs(v.y) <= kOwnStateMaxCoordinate &&
                    std::abs(v.z) <= kOwnStateMaxCoordinate);
    for (const Vec3& v :
         {o.linearMomentum, o.angularMomentum, o.linearVelocity, o.angularVelocity, o.lastPush})
        ASSERT_TRUE(std::isfinite(v.x) && std::abs(v.y) <= kOwnStateMaxValue && std::isfinite(v.z));
    ASSERT_GE(o.gear, 0);
    ASSERT_LE(o.gear, 7);
    ASSERT_GE(o.stuckState, 0);
    ASSERT_LE(o.stuckState, 4);
    ASSERT_LE(m.near.size(), kMaxNearCars);
    for (const auto& n : m.near) {
        ASSERT_EQ(n.input.events, 0); // never repeated
        ASSERT_EQ(n.input.flags & ~kCarInputFlagMask, 0);
        ASSERT_LE(n.input.extraMass, kMaxExtraMass);
        const OwnCarState& c = n.state;
        for (const Vec3& v : {c.matrix.m0, c.matrix.m1, c.matrix.m2})
            ASSERT_TRUE(std::abs(v.x) <= 2.0f && std::abs(v.y) <= 2.0f && std::abs(v.z) <= 2.0f);
        ASSERT_TRUE(std::abs(c.matrix.m3.x) <= kOwnStateMaxCoordinate &&
                    std::abs(c.matrix.m3.y) <= kOwnStateMaxCoordinate &&
                    std::abs(c.matrix.m3.z) <= kOwnStateMaxCoordinate);
        for (const Vec3& v : {c.linearMomentum, c.angularMomentum, c.linearVelocity, c.angularVelocity})
            ASSERT_TRUE(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z));
        checkCarry(c);
        ASSERT_LE(n.upcoming.size(), kMaxUpcomingInputs);
        for (const auto& u : n.upcoming) {
            ASSERT_EQ(u.flags & ~kCarInputFlagMask, 0);
            ASSERT_EQ(u.events & ~kCarInputEventMask, 0);
            ASSERT_LE(u.extraMass, kMaxExtraMass);
        }
    }
}

Bytes mutate(const std::vector<Bytes>& corpus, std::mt19937& rng) {
    auto pick = [&](std::size_t n) { return std::uniform_int_distribution<std::size_t>(0, n - 1)(rng); };
    Bytes b = corpus[pick(corpus.size())];
    const int rounds = 1 + static_cast<int>(pick(4));
    for (int r = 0; r < rounds; ++r) {
        switch (pick(5)) {
        case 0:
            for (std::size_t i = 0, n = 1 + pick(8); i < n && !b.empty(); ++i)
                b[pick(b.size())] ^= static_cast<std::byte>(1u << pick(8));
            break;
        case 1:
            for (std::size_t i = 0, n = 1 + pick(4); i < n && !b.empty(); ++i)
                b[pick(b.size())] = static_cast<std::byte>(rng() & 0xFF);
            break;
        case 2:
            if (!b.empty())
                b.resize(pick(b.size()));
            break;
        case 3:
            for (std::size_t i = 0, n = 1 + pick(32); i < n; ++i)
                b.push_back(static_cast<std::byte>(rng() & 0xFF));
            break;
        default:
            b.resize(pick(400));
            for (auto& x : b)
                x = static_cast<std::byte>(rng() & 0xFF);
            if (!b.empty())
                b[0] = static_cast<std::byte>(pick(2) ? MsgType::PlayerInput : MsgType::CarStates);
            break;
        }
    }
    return b;
}

bool pumpUntil(std::initializer_list<Session*> sessions, const std::function<bool()>& done, int ms = 3000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        for (Session* s : sessions)
            s->update();
        if (done())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

// A host and a joined, clock-synced client.
struct Pair {
    Session host, client;
    explicit Pair(int k) {
        HostParams h;
        h.bind = Address::loopback(testPort(k));
        h.advertiseOnLan = false;
        h.player.name = "Host";
        EXPECT_TRUE(host.host(h));
        JoinParams j;
        j.host = Address::loopback(host.port());
        j.player.name = "Client";
        EXPECT_TRUE(client.join(j));
        EXPECT_TRUE(pumpUntil({&host, &client}, [&] {
            return client.clockSynced() && client.players().size() == 2 && host.players().size() == 2;
        }));
        pumpUntil({&host, &client}, [] { return false; }, 300);
    }
    void race() {
        host.startRace(0);
        EXPECT_TRUE(pumpUntil({&host, &client}, [&] {
            return host.phase() == SessionPhase::Countdown && client.phase() == SessionPhase::Countdown;
        }));
    }
};

} // namespace

TEST(PlayerCars, InputsRoundTrip) {
    const PlayerInputMsg m = sampleInput();
    const auto bytes = encodeMessage(m);
    PlayerInputMsg out;
    ASSERT_TRUE(decodeMessage(bytes, out));
    EXPECT_EQ(out.first, m.first);
    EXPECT_EQ(out.frames, m.frames);
    EXPECT_EQ(out.commands, m.commands);
}

TEST(PlayerCars, RepeatedInputsCostABitEach) {
    // A second of samples (60) with nothing changing after the first.
    PlayerInputMsg m;
    m.first = 1;
    CarInputFrame f;
    f.throttle = 255;
    f.steering = 30;
    f.flags = kInputAutomatic;
    m.frames.assign(kMaxInputFrames, f);
    const auto bytes = encodeMessage(m);
    EXPECT_LE(bytes.size(), 20u);
    PlayerInputMsg out;
    ASSERT_TRUE(decodeMessage(bytes, out));
    EXPECT_EQ(out.frames, m.frames);
}

TEST(PlayerCars, MalformedInputsAreRefused) {
    PlayerInputMsg out;
    auto bad = sampleInput();
    bad.first = 0; // samples count from 1
    EXPECT_FALSE(decodeMessage(encodeMessage(bad), out));
    bad = sampleInput();
    bad.first = 0xFFFFFFF0u; // the numbers would wrap
    EXPECT_FALSE(decodeMessage(encodeMessage(bad), out));
    bad = sampleInput();
    bad.commands[0].position.y = 1.0e9f; // far outside any city
    EXPECT_FALSE(decodeMessage(encodeMessage(bad), out));
    bad = sampleInput();
    bad.commands[0].rotation = 1000.0f;
    EXPECT_FALSE(decodeMessage(encodeMessage(bad), out));
    auto bytes = encodeMessage(sampleInput());
    bytes.push_back(std::byte{0x55}); // trailing bytes
    EXPECT_FALSE(decodeMessage(bytes, out));
    bytes = encodeMessage(sampleInput());
    bytes.resize(bytes.size() / 2); // cut short
    EXPECT_FALSE(decodeMessage(bytes, out));
}

TEST(PlayerCars, StatesRoundTripTheOwnCarBitForBit) {
    const CarStatesMsg m = sampleStates(kMaxPlayers - 1);
    const auto bytes = encodeMessage(m);
    // Fits a packet below the ENet MTU with 15 other cars.
    EXPECT_LT(bytes.size(), 1250u);
    CarStatesMsg out;
    ASSERT_TRUE(decodeMessage(bytes, out));
    EXPECT_EQ(out.time, m.time);
    EXPECT_EQ(out.ack, m.ack);
    EXPECT_EQ(out.waiting, m.waiting);
    ASSERT_TRUE(out.hasOwn);
    expectSameOwn(out.own, m.own);
    ASSERT_EQ(out.cars.size(), m.cars.size());
    EXPECT_NEAR(out.cars[3].second.position.x, 103.0f, 0.01f);
}

TEST(PlayerCars, NearCarsTravelInFullWithoutTheirKeys) {
    // Eight players: seven other cars, three of them near, in full with the
    // inputs the host holds for them, in a contact.
    CarStatesMsg m = sampleStates(kMaxPlayers / 2 - 1, kMaxNearCars);
    m.near[1].input.events = kInputShiftUp; // a key: applied once on the host, never sent on
    const auto bytes = encodeMessage(m);
    CarStatesMsg out;
    ASSERT_TRUE(decodeMessage(bytes, out));
    ASSERT_EQ(out.nearIds, m.nearIds);
    ASSERT_EQ(out.near.size(), kMaxNearCars);
    for (std::size_t i = 0; i < kMaxNearCars; ++i) {
        EXPECT_EQ(out.near[i].id, m.near[i].id);
        expectSameOwn(out.near[i].state, m.near[i].state);
        CarInputFrame expected = m.near[i].input;
        expected.events = 0;
        EXPECT_EQ(out.near[i].input, expected);
        EXPECT_EQ(out.near[i].upcoming, m.near[i].upcoming); // keys and all: each applied once, in turn
    }
    // The host puts in full as many as keep it in one ENet packet
    // (kMaxCarStatesBytes), taking them in turn: one in a contact with
    // eight players.
    CarStatesMsg one = m;
    one.near.resize(1);
    EXPECT_LE(encodeMessage(one).size(), kMaxCarStatesBytes);
    // More than kMaxNearCars: the first ones only.
    m.near.push_back(m.near[0]);
    m.nearIds.push_back(7);
    ASSERT_TRUE(decodeMessage(encodeMessage(m), out));
    EXPECT_EQ(out.near.size(), kMaxNearCars);
    EXPECT_EQ(out.nearIds.size(), kMaxNearCars);
    // A near car out of range refuses the message.
    m = sampleStates(1, 2);
    m.near[1].state.matrix.m3.y = -1.0e7f;
    EXPECT_FALSE(decodeMessage(encodeMessage(m), out));
}

TEST(PlayerCars, StatesOutOfRangeAreRefused) {
    CarStatesMsg out;
    auto bad = sampleStates(2);
    bad.own.matrix.m0.x = 3.0f; // not a rotation
    EXPECT_FALSE(decodeMessage(encodeMessage(bad), out));
    bad = sampleStates(2);
    bad.own.matrix.m3.z = 1.0e6f;
    EXPECT_FALSE(decodeMessage(encodeMessage(bad), out));
    bad = sampleStates(2);
    bad.own.linearVelocity.y = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(decodeMessage(encodeMessage(bad), out));
    bad = sampleStates(2);
    bad.own.wheels[2].rotationSpeed = 1.0e20f;
    EXPECT_FALSE(decodeMessage(encodeMessage(bad), out));
    bad = sampleStates(2);
    bad.own.framePush.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(decodeMessage(encodeMessage(bad), out));
    bad = sampleStates(2);
    bad.own.bound.m1.y = 5.0f; // not a rotation
    EXPECT_FALSE(decodeMessage(encodeMessage(bad), out));
    // Out of a contact the impulses and pushes do not travel.
    auto calm = sampleStates(2);
    calm.own.contact = false;
    calm.own.hasBound = false;
    ASSERT_TRUE(decodeMessage(encodeMessage(calm), out));
    EXPECT_EQ(out.own.linearImpulse, Vec3{});
    EXPECT_LT(encodeMessage(calm).size(), encodeMessage(sampleStates(2)).size());
    bad = sampleStates(2);
    bad.waiting = 1000; // clamped to the range, not refused
    ASSERT_TRUE(decodeMessage(encodeMessage(bad), out));
    EXPECT_EQ(out.waiting, kMaxReportedWaiting);
}

TEST(PlayerCars, DecodersSurviveMutatedMessages) {
    const std::vector<Bytes> corpus{encodeMessage(sampleInput()), encodeMessage(sampleStates(3)),
                                    encodeMessage(sampleStates(0)), encodeMessage(sampleStates(2, 2))};
    std::mt19937 rng(20261009);
    for (int i = 0; i < 40000; ++i) {
        const Bytes b = mutate(corpus, rng);
        if (PlayerInputMsg m; decodeMessage(b, m))
            checkInput(m);
        if (CarStatesMsg m; decodeMessage(b, m))
            checkStates(m);
        if (HasFatalFailure()) {
            ADD_FAILURE() << "iteration " << i;
            return;
        }
    }
}

TEST(PlayerCars, InputsAndStatesTravelDuringARaceOnly) {
    Pair p(0);
    // In the lobby nothing is taken.
    p.client.sendPlayerInput(sampleInput());
    p.host.sendCarStates(p.client.localId(), sampleStates(1));
    pumpUntil({&p.host, &p.client}, [] { return false; }, 200);
    EXPECT_TRUE(p.host.takePlayerInputs().empty());
    EXPECT_TRUE(p.client.takeOwnCarStates().empty());

    p.race();
    p.client.sendPlayerInput(sampleInput());
    std::vector<Session::ReceivedInput> inputs;
    ASSERT_TRUE(pumpUntil({&p.host, &p.client}, [&] {
        for (auto& r : p.host.takePlayerInputs())
            inputs.push_back(std::move(r));
        return !inputs.empty();
    }));
    EXPECT_EQ(inputs[0].player, p.client.localId());
    EXPECT_EQ(inputs[0].msg.frames, sampleInput().frames);

    CarStatesMsg states = sampleStates(0);
    states.time = p.host.time();
    VehicleSnapshot hostCar;
    hostCar.position = {10.0f, 2.0f, -5.0f};
    hostCar.orientation = Quat::fromAxisAngle(Vec3{0, 1, 0}, 0.0f);
    states.cars.emplace_back(kHostPlayerId, hostCar);
    states.cars.emplace_back(p.client.localId(), hostCar); // its own: not a remote car
    EXPECT_GT(p.host.sendCarStates(p.client.localId(), states), 0u);
    std::vector<Session::OwnCarUpdate> own;
    ASSERT_TRUE(pumpUntil({&p.host, &p.client}, [&] {
        for (auto& u : p.client.takeOwnCarStates())
            own.push_back(u);
        return !own.empty();
    }));
    EXPECT_EQ(own[0].ack, 4321u);
    EXPECT_EQ(own[0].time, states.time);
    expectSameOwn(own[0].own, sampleOwn());
    EXPECT_TRUE(own[0].near.empty());
    VehicleSnapshot s;
    EXPECT_NE(p.client.sampleRemoteAt(kHostPlayerId, states.time, s), SnapshotBuffer::Result::Empty);
    EXPECT_NEAR(s.position.x, 10.0f, 0.01f);
    EXPECT_EQ(p.client.sampleRemoteAt(p.client.localId(), states.time, s), SnapshotBuffer::Result::Empty);
}

TEST(PlayerCars, AClientTakesNoNearStateOfItsOwnCarOrOfACarTwice) {
    Pair p(3);
    p.race();
    CarStatesMsg states = sampleStates(0, 2);
    states.time = p.host.time();
    states.nearIds = {kHostPlayerId, 5};
    states.near[0].id = kHostPlayerId;
    states.near[1].id = kHostPlayerId; // twice: both refused
    p.host.sendCarStates(p.client.localId(), states);
    std::vector<Session::OwnCarUpdate> own;
    ASSERT_TRUE(pumpUntil({&p.host, &p.client}, [&] {
        for (auto& u : p.client.takeOwnCarStates())
            own.push_back(u);
        return !own.empty();
    }));
    EXPECT_TRUE(own[0].near.empty());
    EXPECT_EQ(own[0].nearIds, (std::vector<std::uint8_t>{kHostPlayerId, 5}));
    states.nearIds = {p.client.localId(), kHostPlayerId}; // its own car: refused
    states.near[0].id = p.client.localId();               // so is its state; the host's once: taken
    p.host.sendCarStates(p.client.localId(), states);
    own.clear();
    ASSERT_TRUE(pumpUntil({&p.host, &p.client}, [&] {
        for (auto& u : p.client.takeOwnCarStates())
            own.push_back(u);
        return !own.empty();
    }));
    ASSERT_EQ(own[0].near.size(), 1u);
    EXPECT_EQ(own[0].near[0].id, kHostPlayerId);
    EXPECT_EQ(own[0].nearIds, (std::vector<std::uint8_t>{kHostPlayerId}));
    // A state in full of a car not among the near ones: refused.
    states.nearIds = {5};
    states.near.resize(1);
    states.near[0].id = kHostPlayerId;
    p.host.sendCarStates(p.client.localId(), states);
    own.clear();
    ASSERT_TRUE(pumpUntil({&p.host, &p.client}, [&] {
        for (auto& u : p.client.takeOwnCarStates())
            own.push_back(u);
        return !own.empty();
    }));
    EXPECT_TRUE(own[0].near.empty());
}

TEST(PlayerCars, AFloodOfInputsKeepsToTheBudget) {
    Pair p(1);
    p.race();
    for (int i = 0; i < 1000; ++i)
        p.client.sendPlayerInput(sampleInput());
    std::size_t taken = 0;
    pumpUntil({&p.host, &p.client}, [&] {
        taken += p.host.takePlayerInputs().size();
        return false;
    }, 400);
    EXPECT_GT(taken, 0u);
    EXPECT_LE(taken, 260u); // the burst (180) and 0.4 s of the rate
}

TEST(PlayerCars, MutatedTrafficLeavesBothSidesWithinLimits) {
    Pair p(2);
    p.race();
    // The client sends mutated inputs, and a host-like peer is not needed:
    // the host's states reach the client through the same session.
    const std::vector<Bytes> corpus{encodeMessage(sampleInput()), encodeMessage(sampleStates(3)),
                                    encodeMessage(sampleStates(2, 2))};
    std::mt19937 rng(91);
    for (int i = 0; i < 2000; ++i) {
        const Bytes b = mutate(corpus, rng);
        PlayerInputMsg in;
        if (decodeMessage(b, in))
            p.client.sendPlayerInput(in);
        CarStatesMsg st;
        if (decodeMessage(b, st))
            p.host.sendCarStates(p.client.localId(), st);
        if (i % 50 == 0) {
            p.host.update();
            p.client.update();
            for (const auto& r : p.host.takePlayerInputs())
                checkInput(r.msg);
            for (const auto& u : p.client.takeOwnCarStates())
                if (u.hasOwn)
                    checkStates({u.time, u.ack, u.waiting, true, u.own, {}, u.nearIds, u.near});
        }
    }
    EXPECT_EQ(p.host.state(), Session::State::Active);
    EXPECT_EQ(p.client.state(), Session::State::Active);
}
