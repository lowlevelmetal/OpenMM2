#include "TestData.h"
#include "game/fx/BirthRule.h"
#include "game/fx/EffectLibrary.h"
#include "game/fx/Particles.h"
#include "game/fx/Random.h"
#include "game/fx/SkidMarks.h"
#include "game/fx/VehicleEffects.h"
#include "phys/Material.h"

#include <gtest/gtest.h>

using namespace mm2;
using namespace mm2::game::fx;

namespace {
constexpr float kStep = FixedTicker::kStep;
}

TEST(FxRandom, MatchesMsvcRand) {
    // The Angel irand() is the MSVC rand() LCG: seed 1 gives 41, 18467, 6334, 26500.
    Rand r(1);
    EXPECT_EQ(r.irand(), 41);
    EXPECT_EQ(r.irand(), 18467);
    EXPECT_EQ(r.irand(), 6334);
    EXPECT_EQ(r.irand(), 26500);
    Rand f(1);
    EXPECT_FLOAT_EQ(f.frand(), 41.0f / 32768.0f);
}

TEST(FxBirthRule, LoadsAsBirthRuleText) {
    auto r = parseBirthRuleFile("type: a\nasBirthRule {\n  Velocity 0 3 0\n  Life 2\n  SpewRate 8\n"
                                "  TexFrameStart 2\n  TexFrameEnd 5\n  BirthFlags 5\n  Height 2\n  Color -331546\n}\n");
    ASSERT_TRUE(r);
    EXPECT_FLOAT_EQ(r->velocity.y, 3.0f);
    EXPECT_FLOAT_EQ(r->life, 2.0f);
    EXPECT_EQ(r->texFrameEnd, 5);
    EXPECT_EQ(r->birthFlags & BirthRule::kCycleFrames, BirthRule::kCycleFrames);
    EXPECT_FLOAT_EQ(r->height, 2.0f);
    EXPECT_EQ(r->color, 0xFFFAF0E6u); // as stored: 0xAABBGGRR
    EXPECT_FLOAT_EQ(r->gravity, -9.8f); // asBirthRule default kept
    EXPECT_FALSE(parseBirthRuleFile("type: a\nfoo {\n Bar 1\n}\n"));
}

TEST(FxParticles, BlastIsCappedAndParticlesDie) {
    BirthRule r;
    r.life = 0.5f;
    ParticleSystem s;
    s.init(10, 1, 1);
    s.setBirthRule(&r);
    s.blast(25);
    EXPECT_EQ(s.count(), 10);
    for (int i = 0; i < 29; ++i)
        s.update(kStep);
    EXPECT_EQ(s.count(), 10);
    for (int i = 0; i < 2; ++i)
        s.update(kStep);
    EXPECT_EQ(s.count(), 0);
}

TEST(FxParticles, ColourBecomesArgbAndFades) {
    // asBirthRule::InitSpark: the file colour 0xAABBGGRR is drawn as
    // 0xAARRGGBB; DAlpha is per 1/60 s.
    BirthRule r;
    r.life = 10.0f;
    r.gravity = 0.0f;
    r.color = 0xFFFAF0E6u;
    r.dAlpha = -51;
    ParticleSystem s;
    s.init(4, 1, 1);
    s.blast(1, &r);
    EXPECT_EQ(s.positions()[0].color, 0xFFE6F0FAu);
    s.update(kStep);
    EXPECT_EQ(s.positions()[0].color, 0xCCE6F0FAu);
    for (int i = 0; i < 4; ++i)
        s.update(kStep);
    ASSERT_EQ(s.count(), 1);
    EXPECT_EQ(s.positions()[0].color >> 24, 0u);
    s.update(kStep);
    EXPECT_EQ(s.count(), 0); // transparent particles die
}

TEST(FxParticles, DragNeedsNoWindDensity) {
    // asParticles::Update: a = -|v| * Drag * v / Mass, then gravity.
    BirthRule r;
    r.life = 10.0f;
    r.velocity = {10.0f, 0.0f, 0.0f};
    r.drag = 0.1f;
    r.gravity = -9.8f;
    ParticleSystem s;
    s.init(1, 1, 1);
    s.blast(1, &r);
    s.update(kStep);
    EXPECT_NEAR(s.info()[0].velocity.x, 10.0f - 10.0f * kStep, 1e-5f);
    EXPECT_NEAR(s.info()[0].velocity.y, -9.8f * kStep, 1e-5f);
    EXPECT_NEAR(s.positions()[0].position.x, s.info()[0].velocity.x * kStep, 1e-6f);
}

TEST(FxParticles, StopAtHeightAndBounce) {
    BirthRule rain;
    rain.life = 5.0f;
    rain.position = {0, 0.2f, 0};
    rain.velocity = {0, -30.0f, 0};
    rain.birthFlags = BirthRule::kStopAtHeight;
    ParticleSystem s;
    s.init(2, 1, 1);
    s.blast(1, &rain);
    s.update(kStep);
    ASSERT_EQ(s.count(), 1);
    EXPECT_FLOAT_EQ(s.positions()[0].position.y, 0.0f);
    s.update(kStep);
    EXPECT_EQ(s.count(), 0); // stopped particles die on the next update

    BirthRule ball;
    ball.life = 5.0f;
    ball.radius = 0.1f;
    ball.height = 1.0f;
    ball.position = {0, 1.15f, 0};
    ball.velocity = {2.0f, -6.0f, 0};
    ball.damp = 0.5f;
    ball.birthFlags = BirthRule::kBounce;
    s.blast(1, &ball);
    s.update(kStep);
    EXPECT_FLOAT_EQ(s.positions()[0].position.y, 1.0f);
    EXPECT_GT(s.info()[0].velocity.y, 0.0f);
    EXPECT_NEAR(s.info()[0].velocity.x, 1.0f, 1e-6f);
}

TEST(FxParticles, SpewRateAndDeterminism) {
    BirthRule r;
    r.life = 100.0f;
    r.spewRate = 30.0f;
    r.velocityVar = {1, 1, 1};
    auto run = [&] {
        ParticleSystem s;
        s.init(100, 1, 1);
        s.setBirthRule(&r);
        for (int i = 0; i < 60; ++i)
            s.update(kStep);
        return std::vector<SparkPos>(s.positions().begin(), s.positions().end());
    };
    const auto a = run(), b = run();
    EXPECT_NEAR(static_cast<int>(a.size()), 30, 1);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        EXPECT_EQ(a[i].position, b[i].position);
}

TEST(FxParticles, FramesAndRotation) {
    BirthRule r;
    r.life = 10.0f;
    r.birthFlags = BirthRule::kCycleFrames;
    r.texFrameStart = 2;
    r.texFrameEnd = 5;
    r.dRotation = 3;
    ParticleSystem s;
    s.init(1, 8, 8);
    s.setBirthRule(&r);
    s.blast(1);
    EXPECT_EQ(s.positions()[0].frame, 2);
    EXPECT_EQ(s.positions()[0].rotation, 0);
    std::vector<int> frames;
    for (int i = 0; i < 4; ++i) {
        s.update(kStep);
        frames.push_back(s.positions()[0].frame);
    }
    EXPECT_EQ(frames, (std::vector<int>{3, 4, 2, 3})); // one frame per update, range [start, end)
    EXPECT_EQ(s.positions()[0].rotation, 12);

    // Without frame cycling each particle picks irand() % (end - start + 1) + start.
    BirthRule pick;
    pick.texFrameStart = 2;
    pick.texFrameEnd = 5;
    ParticleSystem p;
    p.init(1, 8, 8);
    p.rng().seed(1);
    p.blast(1, &pick);
    Rand ref(1);
    for (int i = 0; i < 7; ++i) // velocity x, y, z, mass, life, drag and damp come first
        ref.frand();
    EXPECT_EQ(p.positions()[0].frame, 2 + ref.irand() % 4);
}

TEST(FxTicker, SixtyHz) {
    FixedTicker t;
    EXPECT_EQ(t.advance(1.0f / 60.0f), 1);
    EXPECT_EQ(t.advance(1.0f / 120.0f), 0);
    EXPECT_EQ(t.advance(1.0f / 120.0f), 1);
    EXPECT_EQ(t.advance(0.05f), 3);
    EXPECT_EQ(t.advance(10.0f), 8); // capped
}

TEST(FxSkids, TrackFollowsTheWheel) {
    SkidTrack t(8);
    t.setWidth(0.2f);
    const Vec3 axle{1, 0, 0};
    // The first pair waits until the wheel has moved 10 cm.
    t.update({0, 0, 0}, axle, true);
    EXPECT_EQ(t.pairCount(), 0u);
    t.update({0, 0, -0.05f}, axle, true);
    EXPECT_EQ(t.pairCount(), 0u);
    t.update({0, 0, -0.5f}, axle, true);
    EXPECT_EQ(t.pairCount(), 2u);
    // Straight on: the newest pair follows the wheel, v in tyre widths.
    for (int i = 1; i <= 10; ++i)
        t.update({0, 0, -0.5f - 0.5f * static_cast<float>(i)}, axle, true);
    EXPECT_EQ(t.pairCount(), 2u);
    int strips = 0;
    t.forEachStrip([&](const std::vector<const SkidTrack::Pair*>& s) {
        ++strips;
        ASSERT_EQ(s.size(), 2u);
        EXPECT_FLOAT_EQ(s[0]->v, 0.0f);
        EXPECT_NEAR(s[1]->v, 5.5f / 0.2f, 1e-3f);
        EXPECT_NEAR(s[1]->left.x, -0.1f, 1e-6f);
        EXPECT_NEAR(s[1]->right.x, 0.1f, 1e-6f);
    });
    EXPECT_EQ(strips, 1);
    // A turn fixes a new pair.
    t.update({1.0f, 0, -6.0f}, axle, true);
    EXPECT_EQ(t.pairCount(), 3u);
    // Lifting ends the strip; the next one starts separately.
    t.update({1.0f, 0, -6.0f}, axle, false);
    t.update({5.0f, 0, -6.0f}, axle, true);
    t.update({5.0f, 0, -7.0f}, axle, true);
    strips = 0;
    t.forEachStrip([&](const auto&) { ++strips; });
    EXPECT_EQ(strips, 2);
    // A full ring drops the oldest pairs.
    for (int i = 0; i < 20; ++i)
        t.update({5.0f + (i % 2 ? 0.5f : -0.5f), 0, -8.0f - static_cast<float>(i)}, axle, true);
    EXPECT_EQ(t.pairCount(), 7u);
}

TEST(FxVehicle, WheelParticlesFollowTheSlide) {
    EffectLibrary lib; // no data: rules keep asBirthRule defaults
    VehicleFxSetup setup;
    VehicleEffects fx(lib, setup);
    phys::CarSim car;
    phys::Material asphalt;
    asphalt.ptxIndex[0] = 4; // smoke
    asphalt.ptxThreshold[0] = 0.25f;
    for (auto& w : car.wheels) {
        w.material = &asphalt;
        w.normalLoad = 4000.0f;
        w.suspensionForce = 4000.0f;
        w.spring = 50000.0f;
        w.contactFrame = Mat34::identity();
        w.width = 0.2f;
        w.radius = 0.3f;
    }
    car.damage.params.medDamage = 1000.0f;
    car.damage.params.maxDamage = 2000.0f;
    fx.update(1.0f, car);
    EXPECT_EQ(fx.wheelParticles().count(), 0); // no slide, no particles
    EXPECT_EQ(fx.smoke().count(), 0);          // no damage, no exhaust pivots
}

TEST(FxRetail, EffectsLoad) {
    MM2_REQUIRE_GAME_DATA();
    EffectLibrary lib;
    lib.load(*test::gameData());
    EXPECT_GE(lib.size(), 10u);
    ASSERT_TRUE(lib.rule("rain"));
    EXPECT_EQ(lib.rule("rain")->birthFlags & BirthRule::kStopAtHeight, BirthRule::kStopAtHeight);
    ASSERT_TRUE(lib.rule("effects/snow"));
    EXPECT_NE(lib.rule("snow")->spewRate, lib.rule("effects/snow")->spewRate); // weather vs effect
    for (int i = 0; i < EffectLibrary::kWheelRules; ++i)
        EXPECT_TRUE(lib.wheelRule(i)) << EffectLibrary::wheelRuleName(i);
    // vehWheelPtx::PtxName order: the frame ranges match the pictures.
    EXPECT_EQ(lib.wheelRule(0)->texFrameEnd, 1);    // dirt 0-1
    EXPECT_EQ(lib.wheelRule(1)->texFrameStart, 2);  // dust 2-5
    EXPECT_EQ(lib.wheelRule(5)->texFrameStart, 23); // snow 23-24
    EXPECT_EQ(lib.wheelRule(6)->texFrameStart, 16); // splash 16-21
}

TEST(FxRetail, SlidingWheelThrowsItsSurface) {
    MM2_REQUIRE_GAME_DATA();
    EffectLibrary lib;
    lib.load(*test::gameData());
    VehicleEffects fx(lib, VehicleFxSetup{});
    phys::CarSim car;
    phys::Material grass;
    grass.ptxIndex[0] = 0; // dirt (InitialBlast 100)
    grass.ptxIndex[1] = 2; // grass (InitialBlast 64)
    grass.ptxThreshold[0] = 0.25f;
    grass.ptxThreshold[1] = 0.5f;
    phys::Wheel& w = car.wheels[0];
    w.material = &grass;
    w.slide = 0.4f; // above the dirt threshold only
    w.normalLoad = 4000.0f;
    w.suspensionForce = 4000.0f; // at the static load: load factor 0.5
    w.spring = 50000.0f;
    w.contactFrame = Mat34::identity();
    w.contactFrame.m3 = {3, 0, 4};
    w.width = 0.2f;
    w.radius = 0.3f;
    w.rotationSpeed = -20.0f;
    car.damage.params.medDamage = 1e6f;
    car.damage.params.maxDamage = 2e6f;
    fx.update(1.0f, car); // 8 updates (at most 8 per frame; the rest is dropped)
    for (int i = 0; i < 7; ++i)
        fx.update(1.0f / 8.0f, car); // 7.5 updates each
    // InitialBlast 100 per second x load 0.5 over 60 updates of 1/60 s.
    EXPECT_NEAR(fx.wheelParticles().count(), 50, 1);
    for (const auto& p : fx.wheelParticles().positions()) {
        EXPECT_LE(p.frame, 1); // dirt frames only
        // Radius: load x width x Radius x 0.5, plus the rule's RadiusVar 0.2.
        EXPECT_NEAR(p.radius, 0.5f * 0.2f * 1.0f * 0.5f, 0.1f + 1e-4f);
    }
}
