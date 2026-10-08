/*
    OpenMM2 - particle systems (asParticles).
    First ported from Open1560 (code/midtown/mmeffects/ptx.cpp),
    Copyright (C) 2020 Brick, GPL-3.0-or-later; the update and birth now
    follow MM2's asParticles::Update/Blast and asBirthRule::InitSpark.
*/
#pragma once

#include "game/fx/BirthRule.h"
#include "game/fx/Random.h"

#include <cstdint>
#include <span>
#include <vector>

namespace mm2::game::fx {

// asSparkInfo: per-particle simulation state.
struct SparkInfo {
    Vec3 velocity;
    float life = 0.0f;
    float invMass = 0.0f; // the rule's mass is stored inverted
    float drag = 0.0f;
    float damp = 1.0f;
    float gravity = 0.0f;
    float dRadius = 0.0f;
    std::int8_t dAlpha = 0;
    std::int8_t dRotation = 0;
    std::uint8_t frameStart = 0, frameEnd = 0;
    // Birth colour (red, green, blue) and the current alpha.
    std::uint8_t red = 255, green = 255, blue = 255, alpha = 255;
};

// asSparkPos: per-particle render state.
struct SparkPos {
    std::int8_t frame = 0;
    std::int8_t rotation = 0; // 32 units per turn (asMeshCardInfo masks it with 31)
    std::uint8_t flags = 0;   // low byte of the rule's BirthFlags
    float radius = 0.0f;
    std::uint32_t color = 0xFFFFFFFFu; // 0xAARRGGBB
    float height = 0.0f;               // the rule's Height (bounce/stop/shadow plane)
    Vec3 position;
};

// asParticles: a pool of particles born by BirthRules.
//
// update() is one MM2 update with the frame time `dt`. Several rules work per
// update rather than per second (frame cycling advances one frame, alpha and
// rotation change by ftol(delta * 60 * dt)), so owners call it at a fixed
// 60 Hz (see FixedTicker): exactly the original running at 60 fps, the rate
// OpenMM2's physics reproduces too.
class ParticleSystem {
public:
    // asParticles::Init(count, framesWide, framesHigh, ...). The original
    // quartered the count in software rendering only.
    void init(int maxParticles, int framesWide, int framesHigh);
    // asParticles::Reset: drops every particle and the birth matrix.
    void reset();

    // Rule spewed by update() (asParticles' own rule); blasts may name another.
    void setBirthRule(const BirthRule* rule) { m_rule = rule; }
    const BirthRule* birthRule() const { return m_rule; }
    // Blast positions are transformed by this matrix when set (the particle
    // velocities are not).
    void setMatrix(const Mat34* m) { m_matrix = m; }

    // asParticles::Blast: emits up to `count` particles from `rule` (or the
    // system's rule), as many as fit.
    void blast(int count, const BirthRule* rule = nullptr);
    // asParticles::Update.
    void update(float dt);

    // Scales the birth colours every update (asParticles' intensity, 1 by
    // default; cityLevel::GetLightingIntensity always answers 1).
    float intensity = 1.0f;
    // Added to the particle velocity before drag (zero in MM2).
    Vec3 wind;

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

// Runs per-update effect rules at a fixed rate whatever the frame rate:
// advance() returns how many kStep updates are due.
class FixedTicker {
public:
    static constexpr float kStep = 1.0f / 60.0f;
    // At most `maxSteps` per call (a long stall does not replay seconds of effects).
    int advance(float dt, int maxSteps = 8);
    void reset() { m_accumulator = 0.0f; }

private:
    float m_accumulator = 0.0f;
};

} // namespace mm2::game::fx
