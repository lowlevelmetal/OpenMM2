// The damage of the cars drawn from the network (game/net/DamageSync): the
// owner's recorder, the receivers' records (timing, resets, loss, hostile
// input), the same dents on both machines, and its trip through two
// NetGames. Also the knocked traffic cars' wheels (game/net/TrafficSync).
#include "TestData.h"
#include "asset/VehicleModel.h"
#include "core/StringUtil.h"
#include "game/ModelLibrary.h"
#include "game/TextureLibrary.h"
#include "game/VehicleRenderer.h"
#include "game/net/DamageSync.h"
#include "game/net/NetGame.h"
#include "game/net/TrafficSync.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <thread>

using namespace mm2;
using game::DamageRecorder;
using game::DamageReplica;

namespace {

net::DamageImpact sampleImpact() {
    net::DamageImpact m;
    m.point = {0.5f, 0.6f, -1.9f};
    m.normal = {0.0f, 0.0f, 1.0f};
    m.total = 900.0f;
    m.speed = 20.0f;
    m.sound = 5000.0f;
    m.audioId = 3;
    return m;
}

// Encoded and decoded, as the network would hand it over.
net::VehicleDamageEvent overTheWire(const net::VehicleDamageEvent& e) {
    net::VehicleDamageEvent out;
    EXPECT_TRUE(net::decodePayload(net::encodePayload(e), out));
    return out;
}

bool samePoint(const Vec3& a, const Vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

} // namespace

// --- Parts -------------------------------------------------------------------------------

TEST(DamageSync, PartsHaveFixedBits) {
    EXPECT_EQ(game::damagePartIndex("BREAK0"), 0);
    EXPECT_EQ(game::damagePartIndex("break03"), 7);
    EXPECT_EQ(game::damagePartIndex("VARIANT0"), 8);
    EXPECT_EQ(game::damagePartIndex("VARIANT3"), 8);
    EXPECT_EQ(game::damagePartIndex("WHL0"), 9);
    EXPECT_EQ(game::damagePartIndex("HUB3"), 16);
    EXPECT_EQ(game::damagePartIndex("ENGINE"), 19);
    EXPECT_EQ(game::damagePartIndex("VARIANT"), -1);
    EXPECT_EQ(game::damagePartIndex("BODY_H"), -1);
    for (int i = 0; i < net::kDamagePartCount; ++i)
        EXPECT_EQ(game::damagePartIndex(game::damagePartName(i, 2)), i) << i;
    EXPECT_EQ(game::damagePartName(8, 2), "VARIANT2");
    EXPECT_EQ(game::damagePartName(20, 0), "");
    EXPECT_FALSE(game::damagePartIsWreckPart(8));
    EXPECT_TRUE(game::damagePartIsWreckPart(9));
}

// vehCarDamage::Update's level from the replicated fraction: nothing below
// MedDamage, MaxDamage at 1 (the fourth smoke level, a wreck).
TEST(DamageSync, DamageLevelFromTheFraction) {
    phys::CarDamageParams p;
    p.medDamage = 500.0f;
    p.maxDamage = 1500.0f;
    EXPECT_EQ(game::damageFromFraction(p, 0.0f), 0.0f);
    EXPECT_EQ(game::damageFromFraction(p, std::numeric_limits<float>::quiet_NaN()), 0.0f);
    EXPECT_FLOAT_EQ(game::damageFromFraction(p, 0.5f), 1000.0f);
    EXPECT_EQ(game::damageFromFraction(p, 1.0f), 1500.0f);
    EXPECT_EQ(game::damageFromFraction(p, 2.0f), 1500.0f);
}

// --- DamageRecorder ------------------------------------------------------------------------

TEST(DamageSync, RecorderSendsBatchesAtItsInterval) {
    DamageRecorder rec;
    EXPECT_TRUE(rec.take(1000).empty());
    const Vec3 p = rec.patch(5000, {0.3333f, 0.5555f, -1.1111f}, 77);
    EXPECT_TRUE(samePoint(p, net::quantizeDamagePoint({0.3333f, 0.5555f, -1.1111f})));
    rec.patch(5010, {0.1f, 0.2f, 0.3f}, 78);
    rec.impact(5005, sampleImpact());
    rec.part(5012, "BREAK0");
    rec.part(5013, "BREAK0"); // already off
    auto events = rec.take(1000);
    ASSERT_EQ(events.size(), 1u);
    const auto& e = events[0];
    EXPECT_EQ(e.subject, net::kDamageOwnCar);
    EXPECT_EQ(e.epoch, 0);
    EXPECT_EQ(e.time, 5000u);
    EXPECT_EQ(e.first, 0);
    ASSERT_EQ(e.patches.size(), 2u);
    EXPECT_EQ(e.patches[0].delay, 0);
    EXPECT_EQ(e.patches[0].seed, 77u);
    EXPECT_EQ(e.patches[1].delay, 10);
    EXPECT_EQ(e.parts, 1u);
    EXPECT_EQ(e.partsDelay, 12);
    ASSERT_EQ(e.impacts.size(), 1u);
    EXPECT_EQ(e.impacts[0].delay, 5);

    rec.patch(5100, {0.0f, 0.0f, 0.0f}, 79);
    EXPECT_TRUE(rec.take(1050).empty()); // within the 100 ms
    events = rec.take(1100);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].first, 2);
    EXPECT_EQ(events[0].parts, 1u); // every part since the reset, again
    EXPECT_TRUE(rec.take(1300).empty()); // nothing new
    EXPECT_EQ(rec.recorded(), 3u);
}

TEST(DamageSync, RecorderSplitsLongBatchesKeepingTheIndices) {
    DamageRecorder rec;
    for (int i = 0; i < 40; ++i)
        rec.patch(1000 + static_cast<std::uint32_t>(i), {0.01f * static_cast<float>(i), 0.0f, 0.0f},
                  static_cast<std::uint32_t>(i));
    const auto events = rec.take(10);
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[0].first, 0);
    EXPECT_EQ(events[1].first, 16);
    EXPECT_EQ(events[2].first, 32);
    EXPECT_EQ(events[2].patches.size(), 8u);
    EXPECT_EQ(events[2].patches.back().seed, 39u);
    for (const auto& e : events)
        EXPECT_EQ(overTheWire(e).patches.size(), e.patches.size());
}

// A reset sends what came before it, then the new epoch from its time; a
// reset with nothing after it before the next one is superseded by it.
TEST(DamageSync, RecorderStartsANewEpochOnAReset) {
    DamageRecorder rec;
    rec.patch(5000, {0.1f, 0.1f, 0.1f}, 1);
    rec.part(5001, "WHL2");
    rec.reset(5050);
    EXPECT_EQ(rec.epoch(), 1);
    EXPECT_EQ(rec.recorded(), 0u);
    rec.patch(5060, {0.2f, 0.2f, 0.2f}, 2);
    auto events = rec.take(100);
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].epoch, 0);
    EXPECT_EQ(events[0].time, 5000u);
    EXPECT_EQ(events[0].parts, 1u << 11);
    EXPECT_EQ(events[1].epoch, 1);
    EXPECT_EQ(events[1].time, 5050u);
    EXPECT_EQ(events[1].first, 0);
    ASSERT_EQ(events[1].patches.size(), 1u);
    EXPECT_EQ(events[1].patches[0].delay, 10);
    EXPECT_EQ(events[1].parts, 0u);

    rec.reset(6000);
    rec.reset(6100);
    events = rec.take(1000);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].epoch, 3);
    EXPECT_EQ(events[0].time, 6100u);
    EXPECT_TRUE(events[0].patches.empty());
}

// The sender keeps under the host's relay budget: what does not fit waits,
// nothing is lost.
TEST(DamageSync, RecorderKeepsToItsBudget) {
    DamageRecorder::Options o;
    o.intervalMs = 0;
    o.perSecond = 10.0;
    o.burst = 2.0;
    DamageRecorder rec(net::kDamageOwnCar, o);
    for (int i = 0; i < 100; ++i)
        rec.patch(1000, {0.0f, 0.01f * i, 0.0f}, static_cast<std::uint32_t>(i));
    std::vector<net::VehicleDamageEvent> all;
    auto events = rec.take(1000);
    EXPECT_EQ(events.size(), 2u);
    all.insert(all.end(), events.begin(), events.end());
    EXPECT_TRUE(rec.take(1050).empty()); // half a token
    std::uint64_t now = 1100;
    int takes = 0;
    while (all.size() < 7 && takes++ < 100) {
        events = rec.take(now);
        EXPECT_LE(events.size(), 1u);
        all.insert(all.end(), events.begin(), events.end());
        now += 100;
    }
    ASSERT_EQ(all.size(), 7u);
    std::uint32_t next = 0;
    for (const auto& e : all) {
        EXPECT_EQ(e.first, next);
        next += static_cast<std::uint32_t>(e.patches.size());
    }
    EXPECT_EQ(next, 100u);
}

// --- DamageReplica -------------------------------------------------------------------------

// Each entry shows when the car is drawn at the time it happened on its
// owner's machine; the sparks, shards, sound and flying parts while fresh.
TEST(DamageSync, ReplicaAppliesEachEntryAtItsTime) {
    DamageReplica rep;
    net::VehicleDamageEvent e;
    e.time = 10000;
    e.patches = {{0, {0.1f, 0.2f, 0.3f}, 11}, {50, {0.4f, 0.5f, 0.6f}, 12}};
    e.impacts = {sampleImpact()};
    e.impacts[0].delay = 20;
    e.parts = 1u << 9;
    e.partsDelay = 80;
    ASSERT_TRUE(rep.receive(1, overTheWire(e), 10050.0));
    const auto key = DamageReplica::playerKey(1);

    EXPECT_TRUE(rep.advance(key, 9990.0, 10050.0).empty());
    auto a = rep.advance(key, 10000.0, 10060.0);
    EXPECT_TRUE(a.clear); // the first record: from a clean car
    ASSERT_EQ(a.patches.size(), 1u);
    EXPECT_EQ(a.patches[0].seed, 11u);
    EXPECT_TRUE(a.impacts.empty());
    a = rep.advance(key, 10030.0, 10080.0);
    EXPECT_FALSE(a.clear);
    EXPECT_TRUE(a.patches.empty());
    ASSERT_EQ(a.impacts.size(), 1u);
    a = rep.advance(key, 10100.0, 10100.0);
    ASSERT_EQ(a.patches.size(), 1u);
    EXPECT_EQ(a.patches[0].seed, 12u);
    EXPECT_EQ(a.partsOff, 1u << 9);
    EXPECT_TRUE(a.partsFresh);
    EXPECT_TRUE(rep.advance(key, 10200.0, 10200.0).empty());
    EXPECT_EQ(rep.recordSize(key), 2u);

    const auto all = rep.replay(key);
    EXPECT_TRUE(all.clear);
    EXPECT_EQ(all.patches.size(), 2u);
    EXPECT_EQ(all.partsOff, 1u << 9);
    EXPECT_FALSE(all.partsFresh);
    EXPECT_TRUE(all.impacts.empty());
}

// A time the car is never drawn at (a hostile one, or a car not drawn) is
// waited for at most kMaxHoldMs; what was late shows without its effects.
TEST(DamageSync, ReplicaWaitsAtMostTheHoldAndSkipsStaleEffects) {
    DamageReplica rep;
    net::VehicleDamageEvent e;
    e.time = 4000000;
    e.patches = {{0, {0.1f, 0.1f, 0.1f}, 1}};
    e.impacts = {sampleImpact()};
    e.parts = 1u;
    ASSERT_TRUE(rep.receive(2, e, 1000.0));
    const auto key = DamageReplica::playerKey(2);
    EXPECT_TRUE(rep.advance(key, 1000.0, 2999.0).empty());
    auto a = rep.advance(key, 1000.0, 3000.0);
    EXPECT_EQ(a.patches.size(), 1u);
    EXPECT_TRUE(a.impacts.empty());
    EXPECT_EQ(a.partsOff, 1u);
    EXPECT_FALSE(a.partsFresh);

    // Drawn long after its time (the car came into view late).
    e.time = 5000;
    e.epoch = 0;
    e.first = 1;
    e.parts = 3u;
    ASSERT_TRUE(rep.receive(2, e, 5100.0));
    a = rep.advance(key, 7000.0, 5200.0);
    EXPECT_EQ(a.patches.size(), 1u);
    EXPECT_TRUE(a.impacts.empty());
    EXPECT_EQ(a.partsOff, 2u);
    EXPECT_FALSE(a.partsFresh);
}

// A reset (a new epoch) clears the car when it is drawn at the reset's time,
// and only the new epoch's damage is shown after it.
TEST(DamageSync, ReplicaClearsTheCarOnAReset) {
    DamageRecorder rec;
    DamageReplica rep;
    const auto key = DamageReplica::playerKey(3);
    rec.patch(1000, {0.1f, 0.1f, 0.1f}, 1);
    rec.part(1000, "BREAK1");
    for (const auto& e : rec.take(0))
        ASSERT_TRUE(rep.receive(3, overTheWire(e), 1000.0));
    auto a = rep.advance(key, 1100.0, 1100.0);
    EXPECT_EQ(a.patches.size(), 1u);
    EXPECT_EQ(a.partsOff, 2u);

    rec.reset(2000); // mmPlayer::Reset
    rec.patch(2010, {0.2f, 0.2f, 0.2f}, 2);
    for (const auto& e : rec.take(500))
        ASSERT_TRUE(rep.receive(3, overTheWire(e), 2000.0));
    EXPECT_TRUE(rep.advance(key, 1990.0, 2000.0).empty()); // still the old car
    a = rep.advance(key, 2005.0, 2005.0);
    EXPECT_TRUE(a.clear);
    EXPECT_TRUE(a.patches.empty());
    a = rep.advance(key, 2010.0, 2010.0);
    ASSERT_EQ(a.patches.size(), 1u);
    EXPECT_EQ(a.patches[0].seed, 2u);
    const auto all = rep.replay(key);
    EXPECT_EQ(all.patches.size(), 1u);
    EXPECT_EQ(all.partsOff, 0u);
}

// An event lost on the way (the host's budget, say): the parts converge with
// the next one, the patches it held are counted missing, and a car shown
// afresh replays the rest.
TEST(DamageSync, ReplicaConvergesAfterALostEvent) {
    DamageRecorder::Options o;
    o.intervalMs = 0;
    DamageRecorder rec(net::kDamageOwnCar, o);
    DamageReplica rep;
    const auto key = DamageReplica::playerKey(4);
    std::vector<net::VehicleDamageEvent> sent;
    auto batch = [&](std::uint32_t t, const char* part) {
        rec.patch(t, {0.01f * static_cast<float>(t % 100), 0.1f, 0.0f}, t);
        rec.patch(t + 1, {0.02f * static_cast<float>(t % 100), 0.1f, 0.0f}, t + 1);
        rec.part(t, part);
        const auto e = rec.take(t);
        sent.insert(sent.end(), e.begin(), e.end());
    };
    batch(1000, "BREAK0");
    batch(1100, "BREAK1");
    batch(1200, "WHL0");
    ASSERT_EQ(sent.size(), 3u);
    ASSERT_TRUE(rep.receive(4, overTheWire(sent[0]), 1000.0));
    ASSERT_TRUE(rep.receive(4, overTheWire(sent[2]), 1200.0)); // sent[1] lost
    const auto a = rep.advance(key, 1300.0, 1300.0);
    EXPECT_EQ(a.patches.size(), 4u);
    EXPECT_EQ(a.partsOff, (1u << 0) | (1u << 1) | (1u << 9)); // all three parts
    EXPECT_EQ(rep.stats().missed, 2u);
    const auto all = rep.replay(key);
    EXPECT_EQ(all.patches.size(), 4u);
    EXPECT_EQ(all.partsOff, (1u << 0) | (1u << 1) | (1u << 9));
}

// A car not drawn (out of view) takes its events after the hold, so it shows
// them when it comes into view; a player who left is forgotten.
TEST(DamageSync, ReplicaSettlesTheCarsNotDrawn) {
    DamageReplica rep;
    net::VehicleDamageEvent e;
    e.subject = 405;
    e.time = 1000;
    e.patches = {{0, {0.3f, 0.3f, 0.3f}, 9}};
    ASSERT_TRUE(rep.receive(net::kHostPlayerId, e, 1000.0));
    const auto key = DamageReplica::ambientKey(405);
    rep.settle(2500.0);
    EXPECT_EQ(rep.recordSize(key), 0u);
    rep.settle(3000.0);
    EXPECT_EQ(rep.recordSize(key), 1u);
    EXPECT_EQ(rep.replay(key).patches.size(), 1u);
    // Drawn again without being shown afresh: the whole record, then what is
    // new; from then on it is left to advance().
    e.time = 4000;
    e.first = 1;
    ASSERT_TRUE(rep.receive(net::kHostPlayerId, e, 4000.0));
    auto a = rep.advance(key, 4000.0, 4000.0);
    EXPECT_TRUE(a.clear);
    EXPECT_EQ(a.patches.size(), 2u);
    e.time = 5000;
    e.first = 2;
    ASSERT_TRUE(rep.receive(net::kHostPlayerId, e, 5000.0));
    a = rep.advance(key, 5000.0, 5000.0);
    EXPECT_FALSE(a.clear);
    EXPECT_EQ(a.patches.size(), 1u);
    rep.settle(9000.0);
    EXPECT_EQ(rep.recordSize(key), 3u);
    rep.forget(key);
    EXPECT_EQ(rep.recordSize(key), 0u);
    EXPECT_TRUE(rep.replay(key).empty());
}

// Everything a received event says is checked: a police car's damage from
// the host only, ids, counts, indices, finite and in-range values; the
// records and the waiting events are bounded.
TEST(DamageSync, ReplicaRefusesHostileEvents) {
    DamageReplica rep;
    net::VehicleDamageEvent e;
    e.subject = 401;
    EXPECT_FALSE(rep.receive(2, e, 0.0)); // a player claiming a police car
    EXPECT_TRUE(rep.receive(net::kHostPlayerId, e, 0.0));
    e.subject = net::kDamageOwnCar;
    EXPECT_FALSE(rep.receive(net::kInvalidPlayerId, e, 0.0));
    EXPECT_FALSE(rep.receive(static_cast<std::uint8_t>(net::kMaxPlayers), e, 0.0));
    auto bad = e;
    bad.patches = {{0, {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f}, 0}};
    EXPECT_FALSE(rep.receive(1, bad, 0.0));
    bad = e;
    bad.patches = {{0, {0.0f, 100.0f, 0.0f}, 0}};
    EXPECT_FALSE(rep.receive(1, bad, 0.0));
    bad = e;
    bad.impacts = {sampleImpact()};
    bad.impacts[0].normal = {5.0f, 0.0f, 0.0f};
    EXPECT_FALSE(rep.receive(1, bad, 0.0));
    bad.impacts[0] = sampleImpact();
    bad.impacts[0].speed = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(rep.receive(1, bad, 0.0));
    bad = e;
    bad.first = static_cast<std::uint16_t>(net::kMaxDamageRecord);
    bad.patches = {{0, {}, 0}};
    EXPECT_FALSE(rep.receive(1, bad, 0.0));
    bad = e;
    bad.patches.assign(net::kMaxDamagePatches + 1, {});
    EXPECT_FALSE(rep.receive(1, bad, 0.0));
    bad = e;
    bad.parts = 1u << net::kDamagePartCount;
    EXPECT_FALSE(rep.receive(1, bad, 0.0));
    EXPECT_TRUE(rep.receive(1, e, 0.0));

    // At most kMaxRecords cars.
    for (std::uint16_t id = 0; id < net::kMaxAmbientIds; ++id) {
        e.subject = id;
        rep.receive(net::kHostPlayerId, e, 0.0);
    }
    EXPECT_EQ(rep.records(), DamageReplica::kMaxRecords);
    // A flood of events for a car never drawn: bounded, and the oldest are
    // taken at once.
    e.subject = 0;
    e.time = 100000000;
    for (int i = 0; i < 1000; ++i) {
        e.first = static_cast<std::uint16_t>(i % 1000);
        e.patches = {{0, {0.0f, 0.0f, 0.0f}, static_cast<std::uint32_t>(i)}};
        rep.receive(net::kHostPlayerId, e, 0.0);
    }
    rep.settle(1.0);
    EXPECT_LE(rep.recordSize(DamageReplica::ambientKey(0)), 1000u);
    EXPECT_GT(rep.recordSize(DamageReplica::ambientKey(0)), 0u);
}

// The whole trip without a network: what the owner recorded (patches across
// several events, parts, a reset) is what the receiver's record holds.
TEST(DamageSync, RecordReachesTheReceiverIntact) {
    DamageRecorder::Options o;
    o.intervalMs = 50;
    DamageRecorder rec(net::kDamageOwnCar, o);
    DamageReplica rep;
    const auto key = DamageReplica::playerKey(5);
    std::vector<Vec3> painted;
    std::uint32_t t = 1000;
    std::uint64_t now = 0;
    auto pump = [&] {
        for (const auto& e : rec.take(now))
            ASSERT_TRUE(rep.receive(5, overTheWire(e), t));
        rep.advance(key, t, t);
    };
    for (int i = 0; i < 30; ++i) {
        painted.push_back(rec.patch(t, {0.05f * i, 0.4f, -1.0f + 0.03f * i}, 1000u + i));
        if (i == 10)
            rec.part(t, "BREAK2");
        t += 17;
        now += 17;
        pump();
    }
    rec.reset(t);
    painted.clear();
    for (int i = 0; i < 25; ++i) {
        painted.push_back(rec.patch(t, {-0.05f * i, 0.3f, 1.0f}, 2000u + i));
        if (i == 3)
            rec.part(t, "WHL1");
        t += 17;
        now += 17;
        pump();
    }
    now += 1000;
    t += 1000;
    pump();
    const auto all = rep.replay(key);
    ASSERT_EQ(all.patches.size(), painted.size());
    for (std::size_t i = 0; i < painted.size(); ++i) {
        EXPECT_TRUE(samePoint(all.patches[i].point, painted[i])) << i;
        EXPECT_EQ(all.patches[i].seed, 2000u + i);
    }
    EXPECT_EQ(all.partsOff, 1u << 10);
    EXPECT_EQ(rep.stats().missed, 0u);
}

// --- The same dents on both machines (retail data) ---------------------------------------

namespace {

// A device that keeps every texture's latest pixels, by the name it was made
// with.
class TextureDevice final : public render::Device {
public:
    const render::DeviceInfo& info() const override { return m_info; }
    render::TextureHandle createTexture(const render::TextureDesc& d,
                                        std::span<const render::TextureData> data) override {
        const render::TextureHandle h{++m_next};
        m_names[h.id] = d.debugName;
        m_sizes[h.id] = {d.width, d.height};
        if (!data.empty() && data[0].data && d.width && d.height) {
            const auto* p = static_cast<const std::uint8_t*>(data[0].data);
            m_pixels[h.id].assign(p, p + static_cast<std::size_t>(d.width) * d.height * 4);
        }
        return h;
    }
    void updateTexture(render::TextureHandle h, std::uint32_t, const render::Rect& r, const void* data,
                       std::uint32_t) override {
        const auto [w, hgt] = m_sizes[h.id];
        if (r.x == 0 && r.y == 0 && r.width == w && r.height == hgt && data != nullptr) {
            const auto* p = static_cast<const std::uint8_t*>(data);
            m_pixels[h.id].assign(p, p + static_cast<std::size_t>(w) * hgt * 4);
        }
    }
    void destroyTexture(render::TextureHandle) override {}
    render::BufferHandle createBuffer(render::BufferKind, std::size_t, const void*) override {
        return {++m_next};
    }
    void updateBuffer(render::BufferHandle, std::size_t, std::span<const std::byte>) override {}
    void destroyBuffer(render::BufferHandle) override {}
    render::BufferSlice uploadTransient(render::BufferKind, std::span<const std::byte>) override {
        return {{1}, 0};
    }
    void applySettings(const render::DisplaySettings&) override {}
    void notifyResized() override {}
    bool beginFrame() override { return true; }
    render::Extent2D outputExtent() const override { return {640, 480}; }
    render::Extent2D sceneExtent() const override { return {640, 480}; }
    void beginScene(const render::ClearValues&) override {}
    void endScene() override {}
    void beginOverlay(const Vec4&) override {}
    void endOverlay() override {}
    void endFrame() override {}
    void setViewport(const render::Viewport&) override {}
    void setScissor(const render::Rect*) override {}
    void clear(const render::ClearValues&) override {}
    void setFrameConstants(const render::FrameConstants&) override {}
    void draw(const render::DrawCall&) override {}
    void requestCapture() override {}
    bool readCapture(render::Image&) override { return false; }
    void waitIdle() override {}
    const render::FrameStats& stats() const override { return m_stats; }

    // The texel damage copies whose name has `tag` ("<texture>#<tag>#<n>"),
    // by "<texture>#<n>".
    std::map<std::string, std::vector<std::uint8_t>> damageCopies(const std::string& tag) const {
        std::map<std::string, std::vector<std::uint8_t>> out;
        for (const auto& [id, name] : m_names) {
            const auto a = name.find('#');
            const auto b = a == std::string::npos ? a : name.find('#', a + 1);
            if (b == std::string::npos || name.substr(a + 1, b - a - 1) != tag)
                continue;
            const auto it = m_pixels.find(id);
            out[name.substr(0, a) + name.substr(b)] = it == m_pixels.end() ? std::vector<std::uint8_t>{}
                                                                             : it->second;
        }
        return out;
    }

private:
    render::DeviceInfo m_info;
    render::FrameStats m_stats;
    std::uint32_t m_next = 0;
    std::map<std::uint32_t, std::string> m_names;
    std::map<std::uint32_t, std::pair<std::uint32_t, std::uint32_t>> m_sizes;
    std::map<std::uint32_t, std::vector<std::uint8_t>> m_pixels;
};

} // namespace

// The owner paints its car's impacts; another machine replays them from the
// events: the damaged textures come out the same, texel for texel, and a
// reset clears both.
TEST(DamageSync, ReplayedDentsMatchTheOwnersTexelForTexel) {
    MM2_REQUIRE_GAME_DATA();
    const vfs::Vfs& vfs = *test::gameData();
    auto read = [&](std::string_view path) { return vfs.readAll(path); };
    auto model = asset::loadVehicleModel("vpmustang99", read);
    ASSERT_TRUE(model);
    TextureDevice device;
    game::TextureLibrary textures(device, vfs);
    game::ModelLibrary models(device, vfs);
    game::VehicleRenderer owner(device, textures, models, *model, 0);
    game::VehicleRenderer remote(device, textures, models, *model, 0);
    ASSERT_TRUE(owner.hasTexelDamage());
    // The renderers' copies are tagged with the model's name and a serial.
    std::string ownerTag, remoteTag;
    for (int serial = 1; serial < 10000 && remoteTag.empty(); ++serial) {
        const std::string tag = std::format("vpmustang99{}", serial);
        if (!device.damageCopies(tag).empty())
            (ownerTag.empty() ? ownerTag : remoteTag) = tag;
    }
    ASSERT_FALSE(remoteTag.empty());
    const auto clean = device.damageCopies(ownerTag);

    // The owner's side: vehCarDamage::Update's paint with the recorder, at
    // points spread over the high LOD body.
    std::vector<Vec3> body;
    for (const auto& mesh : model->pkg.meshes)
        if (str::iequals(mesh.part, "BODY") && mesh.lod == asset::Lod::High)
            for (const auto& section : mesh.sections)
                for (const auto& packet : section.packets)
                    for (const auto& v : packet.vertices)
                        body.push_back(v.position);
    ASSERT_GT(body.size(), 50u);
    std::vector<Vec3> hits;
    for (std::size_t i = 0; i < 6; ++i)
        hits.push_back(body[i * body.size() / 6] + Vec3{0.01f, -0.02f, 0.015f});
    DamageRecorder rec;
    DamageReplica rep;
    const float radius = 0.4f;
    std::uint32_t t = 1000;
    for (const Vec3& h : hits) {
        const Vec3 p = rec.patch(t, h, owner.texelDamageState());
        owner.applyDamage(p, radius);
        t += 50;
    }
    // Another impact on the receiver's own copy first would change its random
    // state: the patches still come out the owner's.
    const std::uint32_t before = remote.texelDamageState();
    remote.applyDamage(hits[1], radius);
    EXPECT_NE(remote.texelDamageState(), before);
    remote.resetDamage();
    for (const auto& e : rec.take(0))
        ASSERT_TRUE(rep.receive(1, overTheWire(e), t));
    game::DamageTarget target;
    target.renderer = &remote;
    target.texelRadius = radius;
    game::applyDamage(rep.advance(DamageReplica::playerKey(1), t, t), target);

    const auto ownerCopies = device.damageCopies(ownerTag);
    const auto remoteCopies = device.damageCopies(remoteTag);
    ASSERT_EQ(ownerCopies.size(), remoteCopies.size());
    bool dented = false;
    for (const auto& [name, pixels] : ownerCopies) {
        ASSERT_TRUE(remoteCopies.contains(name)) << name;
        EXPECT_TRUE(pixels == remoteCopies.at(name)) << name;
        dented = dented || pixels != clean.at(name);
    }
    EXPECT_TRUE(dented);

    // mmPlayer::Reset: both clean again.
    rec.reset(t);
    owner.resetDamage();
    for (const auto& e : rec.take(1000))
        ASSERT_TRUE(rep.receive(1, overTheWire(e), t));
    game::applyDamage(rep.advance(DamageReplica::playerKey(1), t + 1, t + 1), target);
    EXPECT_TRUE(device.damageCopies(ownerTag) == clean);
    const auto after = device.damageCopies(remoteTag);
    for (const auto& [name, pixels] : clean)
        EXPECT_TRUE(after.at(name) == pixels) << name;
}

// --- Two NetGames ------------------------------------------------------------------------

namespace {

// Each test file has its own block of ports, all below 49152 (see
// test_netgame_sync.cpp).
std::uint16_t damagePort() {
    static const std::uint16_t port = static_cast<std::uint16_t>(
        29000 + (std::chrono::steady_clock::now().time_since_epoch().count() / 1000) % 2000 * 2);
    return port;
}

game::NetOptions netOptions(const std::string& name) {
    game::NetOptions o;
    o.playerName = name;
    o.port = damagePort();
    o.portMapping = false;
    o.discoveryPort = static_cast<std::uint16_t>(damagePort() + 1);
    return o;
}

bool pump(std::initializer_list<game::NetGame*> games, const std::function<bool()>& done,
          int timeoutMs = 4000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < end) {
        for (game::NetGame* g : games)
            g->update();
        if (done())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

} // namespace

// A client's car and the host's police car: each machine's damage reaches
// the other through the session, and a client's claim to a police car's
// damage is refused.
TEST(DamageSync, DamageTravelsBetweenNetGames) {
    game::NetGame host(netOptions("Host"));
    game::NetGame client(netOptions("Client"));
    game::RaceConfig cruise;
    std::string err;
    ASSERT_TRUE(host.host(cruise, {"Damage", "", 2, false}, {"vpbug", 0, 0}, &err)) << err;
    ASSERT_TRUE(client.join(std::format("127.0.0.1:{}", damagePort()), "", {"vpbug", 0, 0}, &err)) << err;
    ASSERT_TRUE(pump({&host, &client}, [&] { return host.players().size() == 2; }));
    host.startRace();
    ASSERT_TRUE(pump({&host, &client}, [&] {
        host.reportLoaded(); // nothing to load
        client.reportLoaded();
        return host.raceStarted() && client.raceStarted();
    }, 10000));
    const std::uint8_t clientId = client.localId();

    game::NetDamage hostSide, clientSide;
    for (int i = 0; i < 20; ++i)
        clientSide.own().patch(host.sessionTime(), {0.1f * i, 0.5f, 0.0f}, static_cast<std::uint32_t>(i));
    clientSide.own().part(host.sessionTime(), "WHL3");
    clientSide.send(client, 0);
    hostSide.police(402).patch(host.sessionTime(), {0.2f, 0.4f, 0.6f}, 99);
    hostSide.police(402).part(host.sessionTime(), "BREAK0");
    hostSide.send(host, 0);
    // A client pretending to be the host's police.
    game::DamageRecorder fake(402);
    fake.patch(0, {}, 1);
    for (const auto& e : fake.take(0))
        client.sendEvent(net::kVehicleDamageEvent, net::encodePayload(e));

    ASSERT_TRUE(pump({&host, &client}, [&] {
        hostSide.receive(host.takeGameEvents(), host.frameTime());
        clientSide.receive(client.takeGameEvents(), client.frameTime());
        hostSide.settle(host.frameTime() + 5000.0);
        clientSide.settle(client.frameTime() + 5000.0);
        return hostSide.replica().recordSize(DamageReplica::playerKey(clientId)) == 20 &&
               clientSide.replica().recordSize(DamageReplica::ambientKey(402)) == 1;
    }));
    EXPECT_EQ(hostSide.replica().replay(DamageReplica::playerKey(clientId)).partsOff, 1u << 12);
    EXPECT_EQ(clientSide.replica().replay(DamageReplica::ambientKey(402)).partsOff, 1u);
    EXPECT_EQ(hostSide.replica().recordSize(DamageReplica::ambientKey(402)), 0u); // the client's claim
    EXPECT_GE(hostSide.replica().stats().refused, 1u);
    EXPECT_EQ(hostSide.stats().receivedEvents, 3u);
}

// --- Knocked traffic cars' wheels ---------------------------------------------------------

TEST(DamageSync, KnockedCarsWheelsAreDrawnWhereTheHostHasThem) {
    ai::VehicleData data;
    data.wheels = {Vec3{-0.8f, 0.3f, -1.3f}, Vec3{0.8f, 0.3f, -1.3f}, Vec3{-0.8f, 0.3f, 1.4f},
                   Vec3{0.8f, 0.3f, 1.4f}, Vec3{-0.8f, 0.3f, 2.4f}, Vec3{0.8f, 0.3f, 2.4f}};
    data.wheelCount = 6;
    data.wheelRadius = 0.32f;
    Mat34 body = Mat34::rotationY(0.7f);
    body.m3 = {100.0f, 5.0f, -30.0f};
    // The host's wheels: the pivots moved by the spring and the tyre.
    const std::array<Vec3, 4> moved = {Vec3{0.01f, 0.08f, -0.02f}, Vec3{-0.02f, 0.11f, 0.0f},
                                       Vec3{0.0f, -0.07f, 0.03f}, Vec3{0.015f, 0.02f, 0.0f}};
    std::array<Mat34, 6> host{};
    for (std::size_t i = 0; i < 4; ++i)
        host[i] = Mat34::mul(Mat34::translation(data.wheels[i] + moved[i]), body);
    const auto offsets = game::trafficWheelOffsets(body, host, data);
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_NEAR(offsets[i].x, moved[i].x, 1e-4f);
        EXPECT_NEAR(offsets[i].y, moved[i].y, 1e-4f);
        EXPECT_NEAR(offsets[i].z, moved[i].z, 1e-4f);
    }
    std::array<Mat34, 6> drawn{};
    std::array<bool, 6> valid{};
    game::trafficWheelMatrices(body, offsets, data, drawn, valid);
    for (std::size_t i = 0; i < 6; ++i)
        EXPECT_TRUE(valid[i]);
    for (std::size_t i = 0; i < 4; ++i)
        EXPECT_LT(drawn[i].m3.dist(host[i].m3), 1e-3f);
    // WHL4 at its pivot raised by WHL2's drawn height less the radius.
    const Vec3 w4 = body.untransform(drawn[4].m3);
    EXPECT_NEAR(w4.y, (data.wheels[2].y + moved[2].y - data.wheelRadius) + data.wheels[4].y, 1e-4f);

    // Through the host's message and the client's interpolation.
    game::SharedCar car;
    car.id = 7;
    car.model = 0;
    car.transform = body;
    car.flags = net::kAmbientOffRail;
    car.velocity = {1.0f, 0.0f, 0.0f};
    car.wheels = true;
    car.wheelOffsets = offsets;
    game::TrafficHost hostSide;
    game::TrafficClient client(4, 0x1234);
    client.receive(hostSide.build({1, body.m3}, std::span(&car, 1), 1000, 0, 0x1234));
    car.wheelOffsets[0].y += 0.1f;
    car.transform.m3.x += 0.05f;
    client.receive(hostSide.build({1, body.m3}, std::span(&car, 1), 1050, 0, 0x1234));
    client.update(1025.0);
    ASSERT_EQ(client.cars().size(), 1u);
    const auto& c = client.cars()[0];
    ASSERT_TRUE(c.wheels);
    EXPECT_NEAR(c.wheelOffsets[0].y, offsets[0].y + 0.05f, 0.01f); // halfway between the two
    // Kept as long as the messages carry them, drawn 100 ms behind.
    for (std::uint32_t t = 1100; t < 3000; t += 50) {
        car.transform.m3.x += 0.05f;
        client.receive(hostSide.build({1, body.m3}, std::span(&car, 1), t, 0, 0x1234));
        client.update(static_cast<double>(t) - 100.0);
        ASSERT_EQ(client.cars().size(), 1u) << t;
        EXPECT_TRUE(client.cars()[0].wheels) << t;
    }
    // Back on its rail (no body): no wheels.
    car.flags = 0;
    car.wheels = false;
    client.receive(hostSide.build({1, body.m3}, std::span(&car, 1), 3000, 0, 0x1234));
    client.update(3000.0);
    ASSERT_EQ(client.cars().size(), 1u);
    EXPECT_FALSE(client.cars()[0].wheels);
}
