// The host's props on the wire (net/PropState.h): the ring's state and the
// knocks event.

#include "net/PropState.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace mm2;
using namespace mm2::net;

namespace {

PropSlot flying(std::uint8_t slot) {
    PropSlot p;
    p.slot = slot;
    p.generation = 5;
    p.what.source = PropSource::PropPart;
    p.what.prop = 7067;
    p.what.part = 2;
    p.moving = true;
    p.position = {-3000.25f, 120.5f, 4095.75f};
    p.orientation = Quat::fromAxisAngle(Vec3{0.0f, 0.6f, 0.8f}, 1.1f);
    p.velocity = {12.0f, -30.0f, 100.0f};
    p.angularVelocity = {-40.0f, 3.0f, 0.5f};
    return p;
}

} // namespace

TEST(PropState, RoundTripsEverySourceAndState) {
    PropStateMsg m;
    m.time = 123456;
    m.catalog = 0xF5672068u;
    m.slots.push_back(flying(0));
    PropSlot rest = flying(1);
    rest.what = {PropSource::Prop, 32767, 0, PropOwner::Player, 0, 0};
    rest.moving = false;
    rest.velocity = rest.angularVelocity = {};
    m.slots.push_back(rest);
    PropSlot part = flying(2);
    part.what = {PropSource::CarPart, 0, 19, PropOwner::Catalog, 63, 15};
    m.slots.push_back(part);
    PropSlot still; // after a gap, the ring's last slot
    still.slot = 255;
    still.generation = 15;
    still.hasState = false;
    m.slots.push_back(still);

    const auto bytes = encodeMessage(m);
    PropStateMsg back;
    ASSERT_TRUE(decodeMessage(bytes, back));
    EXPECT_EQ(back.time, m.time);
    EXPECT_EQ(back.catalog, m.catalog);
    ASSERT_EQ(back.slots.size(), 4u);
    for (std::size_t i = 0; i < back.slots.size(); ++i) {
        const PropSlot& a = m.slots[i];
        const PropSlot& b = back.slots[i];
        EXPECT_EQ(b.slot, a.slot);
        EXPECT_EQ(b.generation, a.generation);
        EXPECT_EQ(b.hasState, a.hasState);
        if (!a.hasState)
            continue;
        EXPECT_TRUE(b.what == a.what) << i;
        EXPECT_EQ(b.moving, a.moving);
        // 22 bits over +-8 km: 4 mm.
        EXPECT_NEAR(b.position.x, a.position.x, 0.004f);
        EXPECT_NEAR(b.position.y, a.position.y, 0.004f);
        EXPECT_NEAR(b.position.z, a.position.z, 0.004f);
        const Quat& qa = a.orientation;
        const Quat& qb = b.orientation;
        const float dot = qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w;
        EXPECT_NEAR(std::abs(dot), 1.0f, 2e-3f);
        if (a.moving) {
            EXPECT_NEAR(b.velocity.z, a.velocity.z, 0.04f);
            EXPECT_NEAR(b.angularVelocity.x, a.angularVelocity.x, 0.04f);
        } else {
            EXPECT_EQ(b.velocity.mag2(), 0.0f);
        }
    }
}

// The busiest message, every slot of MM2's 40-slot ring (dgBangerManager::
// Init(40)) flying, still fits one unfragmented datagram.
TEST(PropState, AFullRingFitsOneDatagram) {
    PropStateMsg m;
    for (std::uint8_t k = 0; k < 40; ++k)
        m.slots.push_back(flying(k));
    EXPECT_LT(encodeMessage(m).size(), 1100u);
    std::size_t bits = 0;
    std::int32_t previous = -1;
    for (const auto& p : m.slots) {
        bits += propSlotBits(p, previous);
        previous = p.slot;
    }
    EXPECT_LE(bits, 40u * 8u * 26u); // 26 bytes a flying piece at most
}

// A grown ring (a pile-up, net::kMaxPropSlots) listed in full: a slot after
// the one before it costs a bit for its number.
TEST(PropState, AGrownRingListsCheaply) {
    PropStateMsg m;
    for (int k = 0; k < static_cast<int>(kMaxPropSlots); ++k) {
        PropSlot p;
        p.slot = static_cast<std::uint8_t>(k);
        p.generation = static_cast<std::uint8_t>(k % kPropGenerations);
        p.hasState = false;
        m.slots.push_back(p);
    }
    const auto bytes = encodeMessage(m);
    EXPECT_LE(bytes.size(), 14u + kMaxPropSlots * 6u / 8u + 1u); // 6 bits a slot
    PropStateMsg back;
    ASSERT_TRUE(decodeMessage(bytes, back));
    ASSERT_EQ(back.slots.size(), kMaxPropSlots);
    EXPECT_EQ(back.slots[255].slot, 255);
    EXPECT_EQ(back.slots[255].generation, 255 % kPropGenerations);
    EXPECT_EQ(propSlotBits(m.slots[1], 0), 6u);
    EXPECT_EQ(propSlotBits(m.slots[1]), 14u);
}

// The slots ascend: a repeated or earlier slot is refused (a writer cannot
// write one either).
TEST(PropState, RefusesSlotsOutOfOrder) {
    PropStateMsg m;
    m.slots.push_back(flying(5));
    m.slots.push_back(flying(3));
    {
        WriteStream s;
        std::uint8_t type = static_cast<std::uint8_t>(MsgType::PropState);
        s.u8(type);
        EXPECT_FALSE(serialize(s, m));
    }
    // By hand: slot 5, then slot 5 again (not "the next one").
    WriteStream s;
    std::uint8_t type = static_cast<std::uint8_t>(MsgType::PropState);
    std::uint32_t time = 1, catalog = 2, count = 2;
    s.u8(type);
    s.u32(time);
    s.u32(catalog);
    s.varU32(count);
    for (int i = 0; i < 2; ++i) {
        PropSlot p;
        p.slot = 5;
        p.hasState = false;
        bool next = false;
        std::int32_t slot = 5, generation = 0;
        s.boolean(next);
        s.ranged(slot, 0, static_cast<std::int32_t>(kMaxPropSlots) - 1);
        s.ranged(generation, 0, static_cast<std::int32_t>(kPropGenerations) - 1);
        s.boolean(p.hasState);
    }
    PropStateMsg back;
    EXPECT_FALSE(decodeMessage(s.writer().take(), back));
}

TEST(PropState, RefusesMalformedMessages) {
    PropStateMsg m;
    m.slots.push_back(flying(0));
    auto bytes = encodeMessage(m);
    PropStateMsg back;
    // Every truncation fails.
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        const std::vector<std::byte> cut(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(n));
        EXPECT_FALSE(decodeMessage(cut, back)) << n;
    }
    // A count beyond the ring.
    WriteStream s;
    std::uint8_t type = static_cast<std::uint8_t>(MsgType::PropState);
    std::uint32_t time = 1, catalog = 2, count = kMaxPropSlots + 1;
    s.u8(type);
    s.u32(time);
    s.u32(catalog);
    s.varU32(count);
    EXPECT_FALSE(decodeMessage(s.writer().take(), back));
}

TEST(PropState, KnocksEventRoundTripsAndFitsAPayload) {
    PropKnocksEvent e;
    e.time = 50000;
    for (std::uint16_t i = 0; i < 250; ++i)
        e.knocks.push_back({static_cast<std::uint16_t>(32767 - i), static_cast<std::uint16_t>(i * 200)});
    const auto live = encodePayload(e);
    EXPECT_LE(live.size(), kMaxEventPayload); // what PropHost puts in one event
    PropKnocksEvent back;
    ASSERT_TRUE(decodePayload(live, back));
    ASSERT_EQ(back.knocks.size(), e.knocks.size());
    EXPECT_EQ(back.time, e.time);
    EXPECT_FALSE(back.catchUp);
    EXPECT_EQ(back.knocks[249].prop, 32767 - 249);
    EXPECT_EQ(back.knocks[249].delay, 249 * 200);

    PropKnocksEvent all;
    all.catchUp = true;
    for (std::uint16_t i = 0; i < 500; ++i)
        all.knocks.push_back({i, 77}); // a catch-up has no delays
    const auto bytes = encodePayload(all);
    EXPECT_LE(bytes.size(), kMaxEventPayload);
    ASSERT_TRUE(decodePayload(bytes, back));
    EXPECT_TRUE(back.catchUp);
    ASSERT_EQ(back.knocks.size(), 500u);
    EXPECT_EQ(back.knocks[499].prop, 499);
    EXPECT_EQ(back.knocks[499].delay, 0);

    // More knocks than an event may hold are refused.
    WriteStream s;
    std::uint32_t time = 1, count = kMaxPropKnocksPerEvent + 1;
    bool catchUp = true;
    s.u32(time);
    s.boolean(catchUp);
    s.varU32(count);
    EXPECT_FALSE(decodePayload(s.writer().take(), back));
}
