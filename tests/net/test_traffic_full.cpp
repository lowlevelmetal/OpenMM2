// The shared traffic's police and knocked cars in full (net/TrafficFull.h,
// protocol 12): their encoding, its size, what a receiver refuses and
// mutated messages.
#include "net/TrafficFull.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <random>

using namespace mm2;
using namespace mm2::net;

namespace {

using Bytes = std::vector<std::byte>;

OwnCarState sampleCop() {
    OwnCarState o;
    o.matrix = Mat34::rotationY(-1.2f);
    o.matrix.m3 = {-1180.25f, 111.5f, 140.125f};
    o.linearMomentum = {-21000.5f, 3.25f, 9000.0f};
    o.angularMomentum = {12.0f, -800.5f, 0.25f};
    o.linearVelocity = {-14.0f, 0.002f, 6.0f};
    o.angularVelocity = {0.01f, -0.4f, 0.0f};
    for (std::size_t i = 0; i < 4; ++i)
        o.wheels[i] = {-40.0f - static_cast<float>(i), 3.0f, 0.04f, 0.1f, -0.003f, 0.002f};
    o.engineSpeed = 500.0f;
    o.gear = 3;
    o.damage = 250.0f;
    o.random = 0x12345678u;
    o.resets = 2;
    o.force = {1.0f, 20000.0f, -3.0f};
    o.contact = true;
    o.linearImpulse = {-900.0f, 0.0f, 12.0f};
    return o;
}

TrafficBodyState sampleBody(bool contact) {
    TrafficBodyState b;
    b.matrix = Mat34::rotationY(0.3f);
    b.matrix.m3 = {-1150.0f, 110.0f, 170.5f};
    b.linearMomentum = {3000.0f, -100.0f, 250.0f};
    b.angularMomentum = {50.0f, 900.0f, -25.0f};
    b.linearVelocity = {2.0f, -0.07f, 0.16f};
    b.angularVelocity = {0.02f, 0.5f, -0.01f};
    b.force = {0.0f, -14000.0f, 0.0f};
    b.torque = {10.0f, 0.0f, -5.0f};
    b.lastPush = {0.0f, 0.001f, 0.0f};
    b.contact = contact;
    if (contact) {
        b.linearImpulse = {800.0f, 0.0f, -40.0f};
        b.angularImpulse = {0.0f, 120.0f, 0.0f};
        b.linearPush = {0.002f, 0.0f, 0.0f};
        b.turnForce = {0.0f, 0.0004f, 0.0f};
        b.framePush = {0.0f, 0.0f, 0.001f};
    }
    for (std::size_t i = 0; i < 4; ++i)
        b.wheels[i] = {0.05f * static_cast<float>(i), -0.3f, 1.25f};
    b.sleepState = 2;
    b.stillUpdates = 17;
    b.dormantUpdates = 1023;
    return b;
}

TrafficFullCar police(std::uint16_t id) {
    TrafficFullCar c;
    c.id = id;
    c.generation = 5;
    c.kind = AmbientKind::Police;
    c.car = sampleCop();
    c.throttle = 0.85f;
    c.brake = 0.0f;
    c.steering = -0.42f;
    c.handBrake = 1.0f;
    c.maxThrottle = 0.9f;
    return c;
}

TrafficFullCar body(std::uint16_t id, bool contact = true) {
    TrafficFullCar c;
    c.id = id;
    c.generation = 7;
    c.kind = AmbientKind::Traffic;
    c.body = sampleBody(contact);
    return c;
}

TrafficFullMsg sample() {
    TrafficFullMsg m;
    m.time = 123456;
    m.cars = {police(401), body(12), body(13, false)};
    return m;
}

bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }
bool sameBits(const Vec3& a, const Vec3& b) {
    return sameBits(a.x, b.x) && sameBits(a.y, b.y) && sameBits(a.z, b.z);
}

void expectSameBody(const TrafficBodyState& a, const TrafficBodyState& b) {
    const auto same = [](const Vec3& x, const Vec3& y) { EXPECT_TRUE(sameBits(x, y)); };
    same(a.matrix.m0, b.matrix.m0);
    same(a.matrix.m1, b.matrix.m1);
    same(a.matrix.m2, b.matrix.m2);
    same(a.matrix.m3, b.matrix.m3);
    same(a.linearMomentum, b.linearMomentum);
    same(a.angularMomentum, b.angularMomentum);
    same(a.linearVelocity, b.linearVelocity);
    same(a.angularVelocity, b.angularVelocity);
    same(a.force, b.force);
    same(a.torque, b.torque);
    same(a.lastPush, b.lastPush);
    same(a.linearImpulse, b.linearImpulse);
    same(a.angularImpulse, b.angularImpulse);
    same(a.linearPush, b.linearPush);
    same(a.turnForce, b.turnForce);
    same(a.framePush, b.framePush);
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(sameBits(a.wheels[i].compression, b.wheels[i].compression));
        EXPECT_TRUE(sameBits(a.wheels[i].lateral, b.wheels[i].lateral));
        EXPECT_TRUE(sameBits(a.wheels[i].longitudinal, b.wheels[i].longitudinal));
    }
    EXPECT_EQ(a.contact, b.contact);
    EXPECT_EQ(a.sleepState, b.sleepState);
    EXPECT_EQ(a.stillUpdates, b.stillUpdates);
    EXPECT_EQ(a.dormantUpdates, b.dormantUpdates);
}

// What a decoded message may hold, whatever arrived.
void checkFull(const TrafficFullMsg& m) {
    ASSERT_LE(m.cars.size(), kMaxTrafficFullCars);
    for (const auto& c : m.cars) {
        ASSERT_LT(c.id, kMaxAmbientIds);
        ASSERT_LT(c.generation, kAmbientGenerations);
        ASSERT_LE(c.kind, AmbientKind::Last);
        if (c.kind == AmbientKind::Police) {
            for (float v : {c.throttle, c.brake, c.steering, c.handBrake})
                ASSERT_LE(std::abs(v), 1.0f);
            ASSERT_LE(std::abs(c.maxThrottle), 10.0f);
            ASSERT_TRUE(std::abs(c.car.matrix.m3.x) <= kOwnStateMaxCoordinate);
            ASSERT_TRUE(std::isfinite(c.car.linearVelocity.y));
            continue;
        }
        const TrafficBodyState& b = c.body;
        for (const Vec3& v : {b.matrix.m0, b.matrix.m1, b.matrix.m2})
            ASSERT_TRUE(std::abs(v.x) <= 2.0f && std::abs(v.y) <= 2.0f && std::abs(v.z) <= 2.0f);
        ASSERT_TRUE(std::abs(b.matrix.m3.x) <= kOwnStateMaxCoordinate &&
                    std::abs(b.matrix.m3.y) <= kOwnStateMaxCoordinate &&
                    std::abs(b.matrix.m3.z) <= kOwnStateMaxCoordinate);
        for (const Vec3& v : {b.linearMomentum, b.angularMomentum, b.linearVelocity, b.angularVelocity,
                              b.force, b.torque, b.lastPush, b.linearImpulse, b.framePush})
            ASSERT_TRUE(std::abs(v.x) <= kOwnStateMaxValue && std::abs(v.y) <= kOwnStateMaxValue &&
                        std::abs(v.z) <= kOwnStateMaxValue);
        ASSERT_TRUE(b.contact || b.linearImpulse == Vec3{});
        for (const auto& w : b.wheels)
            ASSERT_TRUE(std::abs(w.compression) <= 100.0f && std::abs(w.lateral) <= 100.0f &&
                        std::abs(w.longitudinal) <= 100.0f);
        ASSERT_GE(b.sleepState, 0);
        ASSERT_LE(b.sleepState, 2);
        ASSERT_GE(b.stillUpdates, 0);
        ASSERT_LE(b.dormantUpdates, 1023);
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
            b.resize(pick(1300));
            for (auto& x : b)
                x = static_cast<std::byte>(rng() & 0xFF);
            if (!b.empty())
                b[0] = static_cast<std::byte>(MsgType::TrafficFull);
            break;
        }
    }
    return b;
}

} // namespace

TEST(TrafficFull, RoundTripsEveryFieldBitForBit) {
    const TrafficFullMsg m = sample();
    TrafficFullMsg out;
    ASSERT_TRUE(decodeMessage(encodeMessage(m), out));
    EXPECT_EQ(out.time, m.time);
    ASSERT_EQ(out.cars.size(), 3u);
    const TrafficFullCar& cop = out.cars[0];
    EXPECT_EQ(cop.id, 401);
    EXPECT_EQ(cop.generation, 5);
    EXPECT_EQ(cop.kind, AmbientKind::Police);
    EXPECT_TRUE(sameBits(cop.car.matrix.m3, m.cars[0].car.matrix.m3));
    EXPECT_TRUE(sameBits(cop.car.linearMomentum, m.cars[0].car.linearMomentum));
    EXPECT_TRUE(sameBits(cop.car.linearImpulse, m.cars[0].car.linearImpulse));
    EXPECT_EQ(cop.car.random, m.cars[0].car.random);
    EXPECT_EQ(cop.car.gear, m.cars[0].car.gear);
    EXPECT_TRUE(sameBits(cop.throttle, 0.85f) && sameBits(cop.steering, -0.42f));
    EXPECT_TRUE(sameBits(cop.handBrake, 1.0f) && sameBits(cop.maxThrottle, 0.9f));
    for (std::size_t i = 1; i < 3; ++i) {
        EXPECT_EQ(out.cars[i].id, m.cars[i].id);
        EXPECT_EQ(out.cars[i].generation, 7);
        EXPECT_EQ(out.cars[i].kind, AmbientKind::Traffic);
        expectSameBody(out.cars[i].body, m.cars[i].body);
    }
    // Out of a contact the impulses and pushes do not travel.
    EXPECT_EQ(out.cars[2].body.linearImpulse, Vec3{});
    TrafficFullMsg calm;
    calm.cars = {body(1, false)};
    TrafficFullMsg hit;
    hit.cars = {body(1, true)};
    EXPECT_LT(encodeMessage(calm).size(), encodeMessage(hit).size());
}

TEST(TrafficFull, AMessageIsOneDatagram) {
    // The host packs its cars into messages of kTrafficFullBytes at most
    // (one ENet packet, never fragmented): a police car and four bodies in a
    // contact each fit one, two police cars or five bodies at rest too.
    TrafficFullMsg m;
    m.cars = {police(400)};
    for (std::uint16_t i = 0; i < 3; ++i)
        m.cars.push_back(body(i));
    EXPECT_LE(encodeMessage(m).size(), kTrafficFullBytes);
    m.cars = {police(400), police(401)};
    EXPECT_LE(encodeMessage(m).size(), kTrafficFullBytes);
    m.cars.clear();
    for (std::uint16_t i = 0; i < 4; ++i)
        m.cars.push_back(body(i));
    EXPECT_LE(encodeMessage(m).size(), kTrafficFullBytes);
    m.cars.clear();
    for (std::uint16_t i = 0; i < 5; ++i)
        m.cars.push_back(body(i, false));
    EXPECT_LE(encodeMessage(m).size(), kTrafficFullBytes);
    // More than kMaxTrafficFullCars: the first ones only.
    m.cars.clear();
    for (std::uint16_t i = 0; i < kMaxTrafficFullCars + 2; ++i)
        m.cars.push_back(body(i, false));
    TrafficFullMsg out;
    ASSERT_TRUE(decodeMessage(encodeMessage(m), out));
    EXPECT_EQ(out.cars.size(), kMaxTrafficFullCars);
}

TEST(TrafficFull, OutOfRangeStatesAreRefused) {
    TrafficFullMsg out;
    const auto refused = [&](const TrafficFullMsg& m) { return !decodeMessage(encodeMessage(m), out); };
    auto bad = sample();
    bad.cars[0].throttle = 1.5f; // beyond what CarSim::setInputs takes
    EXPECT_TRUE(refused(bad));
    bad = sample();
    bad.cars[0].steering = std::numeric_limits<float>::quiet_NaN();
    EXPECT_TRUE(refused(bad));
    bad = sample();
    bad.cars[0].maxThrottle = 1.0e9f;
    EXPECT_TRUE(refused(bad));
    bad = sample();
    bad.cars[0].car.matrix.m3.y = -1.0e7f; // the police car's own state is checked as a player's
    EXPECT_TRUE(refused(bad));
    bad = sample();
    bad.cars[1].body.matrix.m1.y = 3.0f; // not a rotation
    EXPECT_TRUE(refused(bad));
    bad = sample();
    bad.cars[1].body.matrix.m3.x = 1.0e6f;
    EXPECT_TRUE(refused(bad));
    bad = sample();
    bad.cars[1].body.angularVelocity.z = std::numeric_limits<float>::infinity();
    EXPECT_TRUE(refused(bad));
    bad = sample();
    bad.cars[1].body.framePush.y = 1.0e20f;
    EXPECT_TRUE(refused(bad));
    bad = sample();
    bad.cars[2].body.wheels[3].lateral = 1.0e5f;
    EXPECT_TRUE(refused(bad));
    // phSleep has three states: a fourth is not written (held to the
    // range), and a reader refuses its bits.
    bad = sample();
    bad.cars[2].body.sleepState = 3;
    ASSERT_TRUE(decodeMessage(encodeMessage(bad), out));
    EXPECT_EQ(out.cars[2].body.sleepState, 2);
    // A message cut short is refused.
    auto bytes = encodeMessage(sample());
    bytes.resize(bytes.size() - 20);
    EXPECT_FALSE(decodeMessage(bytes, out));
}

TEST(TrafficFull, DecoderSurvivesMutatedMessages) {
    TrafficFullMsg big;
    big.time = 99;
    for (std::uint16_t i = 0; i < 4; ++i)
        big.cars.push_back(body(static_cast<std::uint16_t>(500 + i)));
    const std::vector<Bytes> corpus{encodeMessage(sample()), encodeMessage(big),
                                    encodeMessage(TrafficFullMsg{7, {police(511)}})};
    std::mt19937 rng(20261012);
    for (int i = 0; i < 40000; ++i) {
        const Bytes b = mutate(corpus, rng);
        if (TrafficFullMsg m; decodeMessage(b, m))
            checkFull(m);
        if (HasFatalFailure()) {
            ADD_FAILURE() << "iteration " << i;
            return;
        }
    }
}
