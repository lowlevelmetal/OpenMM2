#include "TestData.h"
#include "game/fx/BirthRule.h"
#include "game/fx/EffectLibrary.h"
#include "game/fx/Particles.h"
#include "game/fx/Random.h"
#include "game/fx/SkidMarks.h"

#include <gtest/gtest.h>

using namespace mm2;
using namespace mm2::game::fx;

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
                                "  TexFrameStart 2\n  TexFrameEnd 5\n  BirthFlags 5\n  Color -1\n}\n");
    ASSERT_TRUE(r);
    EXPECT_FLOAT_EQ(r->velocity.y, 3.0f);
    EXPECT_FLOAT_EQ(r->life, 2.0f);
    EXPECT_EQ(r->texFrameEnd, 5);
    EXPECT_EQ(r->birthFlags & BirthRule::kCycleFrames, BirthRule::kCycleFrames);
    EXPECT_EQ(r->color, 0xFFFFFFFFu);
    EXPECT_FLOAT_EQ(r->gravity, -9.8f); // default kept
    EXPECT_FALSE(parseBirthRuleFile("type: a\nfoo {\n Bar 1\n}\n"));
}

TEST(FxParticles, BlastIsCappedAndParticlesDie) {
    BirthRule r;
    r.life = 0.5f;
    ParticleSystem s;
    s.init(10, 1, 1, 1.0f);
    s.setBirthRule(&r);
    s.blast(25);
    EXPECT_EQ(s.count(), 10);
    for (int i = 0; i < 14; ++i)
        s.update(1.0f / 30.0f);
    EXPECT_EQ(s.count(), 10);
    for (int i = 0; i < 4; ++i)
        s.update(1.0f / 30.0f);
    EXPECT_EQ(s.count(), 0);
}

TEST(FxParticles, GravityDampingAndFade) {
    BirthRule r;
    r.life = 10.0f;
    r.gravity = -10.0f;
    r.dAlpha = -51; // 5 frames to transparent
    ParticleSystem s;
    s.init(4, 1, 1, 1.0f);
    s.setBirthRule(&r);
    s.blast(1);
    s.update(1.0f / 30.0f);
    ASSERT_EQ(s.count(), 1);
    EXPECT_LT(s.positions()[0].position.y, 0.0f);
    EXPECT_EQ(s.positions()[0].color >> 24, 255u - 51u);
    for (int i = 0; i < 5; ++i)
        s.update(1.0f / 30.0f);
    EXPECT_EQ(s.count(), 0); // alpha reached 0
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
        for (int i = 0; i < 30; ++i)
            s.update(1.0f / 30.0f);
        return std::vector<SparkPos>(s.positions().begin(), s.positions().end());
    };
    const auto a = run(), b = run();
    EXPECT_NEAR(static_cast<int>(a.size()), 30, 1);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        EXPECT_EQ(a[i].position, b[i].position);
}

TEST(FxParticles, CycleFrames) {
    BirthRule r;
    r.life = 10.0f;
    r.birthFlags = BirthRule::kCycleFrames;
    r.texFrameStart = 2;
    r.texFrameEnd = 5;
    ParticleSystem s;
    s.init(1, 8, 8, 1.0f);
    s.setBirthRule(&r);
    s.blast(1);
    EXPECT_EQ(s.positions()[0].frame, 2);
    std::vector<int> frames;
    for (int i = 0; i < 4; ++i) {
        s.update(1.0f / 30.0f);
        frames.push_back(s.positions()[0].frame);
    }
    EXPECT_EQ(frames, (std::vector<int>{3, 4, 2, 3})); // range [start, end)
}

TEST(FxSkids, LaysTrackWhileSkidding) {
    SkidTrail t(8);
    SkidInput in;
    in.carSpeed = 20.0f;
    in.latSlip = 0.5f;
    in.onGround = true;
    in.shouldSkid = true;
    in.contact = Mat34::identity();
    in.width = 0.3f;
    for (int i = 0; i < 30; ++i) {
        in.contact.m3.z -= 20.0f / 30.0f;
        EXPECT_TRUE(t.update(1.0f / 30.0f, in));
    }
    int used = 0;
    for (const auto& q : t.quads())
        used += q.used;
    EXPECT_GE(used, 6);
    EXPECT_LE(used, 8);
    const auto& q = t.quads()[0];
    EXPECT_NEAR(q.p[0].dist(q.p[1]), 0.3f, 1e-4f);

    // Slow down: no skid, and the next track does not connect to the old one.
    in.carSpeed = 1.0f;
    in.wheelSpeed = 1.0f;
    EXPECT_FALSE(t.update(1.0f / 30.0f, in));
}

TEST(FxRetail, EffectsLoad) {
    MM2_REQUIRE_GAME_DATA();
    EffectLibrary lib;
    lib.load(*test::gameData());
    EXPECT_GE(lib.size(), 10u);
    ASSERT_TRUE(lib.rule("rain"));
    EXPECT_EQ(lib.rule("rain")->birthFlags & BirthRule::kSplashes, BirthRule::kSplashes);
    ASSERT_TRUE(lib.rule("effects/snow"));
    EXPECT_NE(lib.rule("snow")->spewRate, lib.rule("effects/snow")->spewRate); // weather vs effect
    for (int i = 0; i <= 8; ++i)
        EXPECT_TRUE(lib.surfaceRule(i)) << EffectLibrary::surfaceRuleName(i);
    EXPECT_TRUE(lib.rule("engine smoke rule"));
}
