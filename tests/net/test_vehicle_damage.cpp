// A car's damage on the wire (net/VehicleDamage.h) and the knocked traffic
// cars' wheels and the police's damage in AmbientState.
#include "net/AmbientState.h"
#include "net/VehicleDamage.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using namespace mm2;
using namespace mm2::net;

namespace {

VehicleDamageEvent sampleEvent() {
    VehicleDamageEvent e;
    e.subject = kDamageOwnCar;
    e.epoch = 3;
    e.time = 123456;
    e.first = 40;
    for (int i = 0; i < 5; ++i)
        e.patches.push_back({static_cast<std::uint8_t>(i * 10), {0.8f - 0.1f * i, 0.5f, -2.1f + 0.3f * i},
                             0xDA6Au + static_cast<std::uint32_t>(i) * 7919u});
    e.parts = (1u << 0) | (1u << 9) | (1u << 13);
    e.partsDelay = 33;
    DamageImpact m;
    m.delay = 12;
    m.point = {-0.9f, 0.4f, 1.7f};
    m.normal = Vec3{0.6f, 0.0f, 0.8f};
    m.total = 4321.0f;
    m.speed = 27.5f;
    m.sound = 12000.0f;
    m.audioId = 7;
    e.impacts.push_back(m);
    return e;
}

} // namespace

TEST(VehicleDamage, RoundTripsWithinItsQuantization) {
    const VehicleDamageEvent e = sampleEvent();
    VehicleDamageEvent d;
    ASSERT_TRUE(decodePayload(encodePayload(e), d));
    EXPECT_EQ(d.subject, e.subject);
    EXPECT_EQ(d.epoch, e.epoch);
    EXPECT_EQ(d.time, e.time);
    EXPECT_EQ(d.first, e.first);
    ASSERT_EQ(d.patches.size(), e.patches.size());
    for (std::size_t i = 0; i < e.patches.size(); ++i) {
        EXPECT_EQ(d.patches[i].delay, e.patches[i].delay);
        EXPECT_EQ(d.patches[i].seed, e.patches[i].seed);
        EXPECT_NEAR(d.patches[i].point.x, e.patches[i].point.x, 0.0003f);
        EXPECT_NEAR(d.patches[i].point.z, e.patches[i].point.z, 0.0003f);
    }
    EXPECT_EQ(d.parts, e.parts);
    EXPECT_EQ(d.partsDelay, e.partsDelay);
    ASSERT_EQ(d.impacts.size(), 1u);
    const DamageImpact& m = d.impacts[0];
    EXPECT_EQ(m.delay, 12);
    EXPECT_NEAR(m.point.z, 1.7f, 0.005f);
    EXPECT_NEAR(m.normal.x, 0.6f, 0.01f);
    EXPECT_NEAR(m.total, 4321.0f, 4321.0f * 0.02f); // log scale: 1.4% steps
    EXPECT_NEAR(m.speed, 27.5f, 0.15f);
    EXPECT_NEAR(m.sound, 12000.0f, 12000.0f * 0.02f);
    EXPECT_EQ(m.audioId, 7);
}

// The owner paints at the point as it arrives, so both machines run the
// texel damage on the same numbers.
TEST(VehicleDamage, AQuantizedPointArrivesExactly) {
    const Vec3 points[] = {{0.123456f, 0.98765f, -1.8765f}, {-7.99f, 7.99f, 0.0f}, {12.0f, -30.0f, 0.5f}};
    for (const Vec3& p : points) {
        VehicleDamageEvent e;
        e.patches.push_back({0, quantizeDamagePoint(p), 1});
        VehicleDamageEvent d;
        ASSERT_TRUE(decodePayload(encodePayload(e), d));
        EXPECT_EQ(d.patches[0].point.x, e.patches[0].point.x);
        EXPECT_EQ(d.patches[0].point.y, e.patches[0].point.y);
        EXPECT_EQ(d.patches[0].point.z, e.patches[0].point.z);
        EXPECT_LE(std::abs(d.patches[0].point.y), kDamagePointRange);
    }
}

// Whatever the sender's numbers, what is read is finite and in range.
TEST(VehicleDamage, NonFiniteValuesArriveFiniteAndInRange) {
    constexpr float nan = std::numeric_limits<float>::quiet_NaN();
    constexpr float inf = std::numeric_limits<float>::infinity();
    VehicleDamageEvent e;
    e.patches.push_back({0, {nan, inf, -inf}, 0});
    DamageImpact m;
    m.point = {nan, 1e9f, -inf};
    m.normal = {nan, nan, nan};
    m.total = inf;
    m.speed = nan;
    m.sound = -5.0f;
    m.audioId = 60000;
    e.impacts.push_back(m);
    e.parts = 0xFFFFFFFFu;
    VehicleDamageEvent d;
    ASSERT_TRUE(decodePayload(encodePayload(e), d));
    for (float v : {d.patches[0].point.x, d.patches[0].point.y, d.patches[0].point.z}) {
        EXPECT_TRUE(std::isfinite(v));
        EXPECT_LE(std::abs(v), kDamagePointRange);
    }
    const DamageImpact& r = d.impacts[0];
    for (float v :
         {r.point.x, r.point.y, r.point.z, r.normal.x, r.normal.y, r.normal.z, r.total, r.speed, r.sound})
        EXPECT_TRUE(std::isfinite(v));
    EXPECT_GE(r.total, 0.0f);
    EXPECT_GE(r.sound, 0.0f);
    EXPECT_LE(r.speed, kDamageMaxSpeed);
    EXPECT_LE(r.audioId, 1000);
    EXPECT_EQ(d.parts, (1u << kDamagePartCount) - 1u);
}

// A record holds at most kMaxDamageRecord patches: an event reaching past it
// does not decode, nor does a cut one.
TEST(VehicleDamage, MalformedEventsDoNotDecode) {
    VehicleDamageEvent e = sampleEvent();
    e.first = static_cast<std::uint16_t>(kMaxDamageRecord - 2);
    VehicleDamageEvent d;
    EXPECT_FALSE(decodePayload(encodePayload(e), d));
    e = sampleEvent();
    auto bytes = encodePayload(e);
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        const std::vector<std::byte> cut(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(n));
        EXPECT_FALSE(decodePayload(cut, d)) << n;
    }
}

// The police's damage level travels at 10 bits (the smoke's four levels
// change at quarters of it); a knocked car with a body carries its wheels,
// one bit says so, a car on its rail or a police car has neither.
TEST(AmbientState, PoliceDamageAndKnockedCarsWheelsRoundTrip) {
    AmbientStateMsg msg;
    msg.time = 5000;
    msg.setOrigin({100.0f, 10.0f, -50.0f});
    AmbientEntity police;
    police.id = 401;
    police.kind = AmbientKind::Police;
    police.position = {110.0f, 10.0f, -40.0f};
    police.damage = 0.2507f;
    msg.entities.push_back(police);
    AmbientEntity knocked;
    knocked.id = 12;
    knocked.position = {90.0f, 11.0f, -60.0f};
    knocked.flags = kAmbientOffRail;
    knocked.wheels = true;
    knocked.wheelOffsets = {Vec3{0.01f, 0.12f, -0.02f}, Vec3{-0.03f, -0.07f, 0.0f}, Vec3{0.0f, 0.3f, 0.05f},
                            Vec3{0.2f, -0.9f, -0.2f}};
    msg.entities.push_back(knocked);
    AmbientEntity rail;
    rail.id = 13;
    rail.position = {95.0f, 10.0f, -45.0f};
    rail.wheels = true; // on its rail: not sent
    msg.entities.push_back(rail);

    AmbientStateMsg out;
    ASSERT_TRUE(decodeMessage(encodeMessage(msg), out));
    ASSERT_EQ(out.entities.size(), 3u);
    EXPECT_NEAR(out.entities[0].damage, 0.2507f, 0.0005f);
    EXPECT_GT(std::ceil(out.entities[0].damage * 4.0f), 1.0f); // still the second smoke level
    EXPECT_FALSE(out.entities[0].wheels);
    ASSERT_TRUE(out.entities[1].wheels);
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_NEAR(out.entities[1].wheelOffsets[i].x, knocked.wheelOffsets[i].x, 0.005f);
        EXPECT_NEAR(out.entities[1].wheelOffsets[i].y, knocked.wheelOffsets[i].y, 0.005f);
        EXPECT_NEAR(out.entities[1].wheelOffsets[i].z, knocked.wheelOffsets[i].z, 0.005f);
    }
    EXPECT_FALSE(out.entities[2].wheels);
    // The wheels' cost: one bit for any knocked car, 80 more with a body.
    AmbientEntity bare = knocked;
    bare.wheels = false;
    EXPECT_EQ(ambientEntityBits(knocked), ambientEntityBits(bare) + 80);
    AmbientEntity onRail = rail;
    onRail.wheels = false;
    EXPECT_EQ(ambientEntityBits(rail), ambientEntityBits(onRail));
}
