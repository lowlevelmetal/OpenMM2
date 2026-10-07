/*
    OpenMM2 - particle systems (asParticles).
    Ported from Open1560 (code/midtown/mmeffects/ptx.cpp and game.asm
    asBirthRule::InitSpark), Copyright (C) 2020 Brick, GPL-3.0-or-later.
*/
#pragma once

#include "game/fx/BirthRule.h"
#include "game/fx/Random.h"

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace mm2::game::fx {

// asSparkInfo: per-particle simulation state.
struct SparkInfo {
    float life = 0.0f;
    Vec3 velocity;
    float invMass = 0.0f; // MM1 field "Mass" holds 1 / mass
    float drag = 0.0f;
    float damp = 1.0f;
    float gravity = 0.0f;
    float dRadius = 0.0f;
    std::int8_t dAlpha = 0;
    std::int8_t dRotation = 0;
};

// asSparkPos: per-particle render state.
struct SparkPos {
    std::int8_t frame = 0;
    std::int8_t rotation = 0;      // 256 units per turn; cards use (rotation >> 2) & 31
    float radius = 0.0f;
    std::uint32_t color = 0xFFFFFFFFu; // ARGB, alpha in the top byte
    Vec3 position;
};

// asParticles: a pool of particles born by a BirthRule and integrated with
// the original's update (30 Hz frame counting for alpha, rotation and frame
// animation; per-1/30 s damping).
class ParticleSystem {
public:
    // Particle frames run at 30 Hz (PtxFrameRate).
    static constexpr float kFrameRate = 30.0f;

    // `maxParticles` is scaled by `capacityScale` as MM1 does with its
    // "maxptx" command-line parameter (default 2).
    void init(int maxParticles, int framesWide, int framesHigh, float capacityScale = 2.0f);
    void reset();

    void setBirthRule(const BirthRule* rule) { m_rule = rule; }
    const BirthRule* birthRule() const { return m_rule; }
    // Emitter placement for blasts: positions are transformed by it (MM1
    // SetMatrix). Null emits in world space.
    void setMatrix(const Mat34* m) { m_matrix = m; }

    // Emits up to `count` particles from `rule` (or the system's rule).
    void blast(int count, const BirthRule* rule = nullptr);
    // Advances the system: spews by the rule's SpewRate, integrates, ages
    // and kills particles.
    void update(float dt);

    // Wind for drag (MM1 defaults: no wind, density 0, so Drag has no effect
    // unless a density is set).
    Vec3 wind;
    float windDensity = 0.0f;
    // Height of the splash plane for rules with kSplashes. MM1 used y = 0;
    // OpenMM2 lets the owner supply the ground (inferred adaptation for hilly
    // San Francisco).
    std::function<float(const Vec3&)> splashHeight;

    int count() const { return m_count; }
    int capacity() const { return static_cast<int>(m_info.size()); }
    int framesWide() const { return m_framesWide; }
    int framesHigh() const { return m_framesHigh; }
    std::span<const SparkInfo> info() const { return {m_info.data(), static_cast<std::size_t>(m_count)}; }
    std::span<const SparkPos> positions() const { return {m_pos.data(), static_cast<std::size_t>(m_count)}; }
    Rand& rng() { return m_rand; }

private:
    void initSpark(const BirthRule& rule, SparkInfo& info, SparkPos& pos);

    std::vector<SparkInfo> m_info;
    std::vector<SparkPos> m_pos;
    int m_count = 0;
    int m_framesWide = 1, m_framesHigh = 1;
    const BirthRule* m_rule = nullptr;
    const Mat34* m_matrix = nullptr;
    float m_elapsed = 0.0f;
    float m_spewFraction = 0.0f;
    Rand m_rand;
};

} // namespace mm2::game::fx
