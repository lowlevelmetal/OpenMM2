#include "game/fx/Particles.h"

#include <algorithm>
#include <cmath>

namespace mm2::game::fx {

void ParticleSystem::init(int maxParticles, int framesWide, int framesHigh) {
    const auto capacity = static_cast<std::size_t>(std::max(1, maxParticles));
    m_info.assign(capacity, {});
    m_pos.assign(capacity, {});
    m_framesWide = std::max(1, framesWide);
    m_framesHigh = std::max(1, framesHigh);
    reset();
}

void ParticleSystem::reset() {
    m_count = 0;
    m_elapsed = 0.0f;
    m_spewFraction = 0.0f;
}

// asBirthRule::InitSpark. Every varied value is base + (frand() - 0.5) * var,
// drawn in the original's order.
void ParticleSystem::initSpark(const BirthRule& r, SparkInfo& info, SparkPos& pos) {
    pos.flags = static_cast<std::uint8_t>(r.birthFlags & 0xFF);
    info.velocity.x = (m_rand.frand() - 0.5f) * r.velocityVar.x + r.velocity.x;
    info.velocity.y = (m_rand.frand() - 0.5f) * r.velocityVar.y + r.velocity.y;
    info.velocity.z = (m_rand.frand() - 0.5f) * r.velocityVar.z + r.velocity.z;
    info.invMass = 1.0f / ((m_rand.frand() - 0.5f) * r.massVar + r.mass);
    info.life = (m_rand.frand() - 0.5f) * r.lifeVar + r.life;
    info.drag = (m_rand.frand() - 0.5f) * r.dragVar + r.drag;
    info.damp = (m_rand.frand() - 0.5f) * r.dampVar + r.damp;
    info.frameStart = static_cast<std::uint8_t>(r.texFrameStart);
    info.frameEnd = static_cast<std::uint8_t>(r.texFrameEnd);
    if (r.birthFlags & BirthRule::kCycleFrames) {
        pos.frame = static_cast<std::int8_t>(r.texFrameStart);
    } else {
        // irand() % (end - start + 1) + start. (The original divides by zero
        // when end is start - 1; such a rule starts at its first frame here.)
        const int span = r.texFrameEnd - r.texFrameStart + 1;
        const int pick = span > 0 ? m_rand.irand() % span : 0;
        pos.frame = static_cast<std::int8_t>(static_cast<std::int8_t>(pick) + static_cast<std::int8_t>(r.texFrameStart));
    }
    pos.radius = (m_rand.frand() - 0.5f) * r.radiusVar + r.radius;
    pos.position.x = (m_rand.frand() - 0.5f) * r.positionVar.x + r.position.x;
    pos.position.y = (m_rand.frand() - 0.5f) * r.positionVar.y + r.position.y;
    pos.position.z = (m_rand.frand() - 0.5f) * r.positionVar.z + r.position.z;
    // The file colour 0xAABBGGRR becomes the vertex colour 0xAARRGGBB.
    info.red = static_cast<std::uint8_t>(r.color & 0xFF);
    info.green = static_cast<std::uint8_t>((r.color >> 8) & 0xFF);
    info.blue = static_cast<std::uint8_t>((r.color >> 16) & 0xFF);
    info.alpha = static_cast<std::uint8_t>(r.color >> 24);
    pos.color = (static_cast<std::uint32_t>(info.alpha) << 24) | (static_cast<std::uint32_t>(info.red) << 16) |
                (static_cast<std::uint32_t>(info.green) << 8) | info.blue;
    info.gravity = r.gravity;
    info.dRadius = (m_rand.frand() - 0.5f) * r.dRadiusVar + r.dRadius;
    info.dAlpha = static_cast<std::int8_t>(static_cast<int>(
        static_cast<float>(r.dAlpha) + (m_rand.frand() - 0.5f) * static_cast<float>(r.dAlphaVar)));
    pos.rotation = 0;
    info.dRotation = static_cast<std::int8_t>(static_cast<int>(
        static_cast<float>(r.dRotation) + (m_rand.frand() - 0.5f) * static_cast<float>(r.dRotationVar)));
    pos.height = r.height;
}

void ParticleSystem::blast(int count, const BirthRule* rule) {
    if (!rule)
        rule = m_rule;
    if (!rule || m_info.empty())
        return;
    count = std::min(count, capacity() - m_count);
    for (; count > 0; --count, ++m_count) {
        auto& info = m_info[static_cast<std::size_t>(m_count)];
        auto& pos = m_pos[static_cast<std::size_t>(m_count)];
        initSpark(*rule, info, pos);
        if (m_matrix)
            pos.position = m_matrix->transform(pos.position);
    }
}

void ParticleSystem::update(float dt) {
    // Per-update deltas are scaled by the frame time in 1/60 s.
    const float frames = dt * 60.0f;
    m_elapsed += dt;
    if (m_rule && (m_elapsed < m_rule->spewTimeLimit || m_rule->spewTimeLimit == 0.0f)) {
        const float spew = dt * m_rule->spewRate + m_spewFraction;
        const int n = static_cast<int>(spew);
        m_spewFraction = spew - static_cast<float>(n);
        if (spew >= 1.0f)
            blast(n, nullptr);
    }

    for (int i = 0; i < m_count;) {
        SparkInfo& info = m_info[static_cast<std::size_t>(i)];
        SparkPos& pos = m_pos[static_cast<std::size_t>(i)];
        info.life -= dt;
        if (info.life <= 0.0f || pos.position.y < -50.0f || !(pos.color & 0xFF000000u)) {
            // The last particle takes this slot and is updated next.
            --m_count;
            info = m_info[static_cast<std::size_t>(m_count)];
            pos = m_pos[static_cast<std::size_t>(m_count)];
            continue;
        }

        // Quadratic drag on the velocity (plus the wind offset, zero in MM2),
        // then gravity; the position moves with the new velocity.
        const Vec3 w = info.velocity + wind;
        const float k = -(std::sqrt(w.y * w.y + w.z * w.z + w.x * w.x) * info.drag);
        const float ax = w.x * k * info.invMass;
        const float ay = w.y * k * info.invMass + info.gravity;
        const float az = w.z * k * info.invMass;
        info.velocity.x = ax * dt + info.velocity.x;
        info.velocity.y = ay * dt + info.velocity.y;
        info.velocity.z = az * dt + info.velocity.z;
        pos.position.x = info.velocity.x * dt + pos.position.x;
        pos.position.y = dt * info.velocity.y + pos.position.y;
        pos.position.z = dt * info.velocity.z + pos.position.z;

        auto scaled = [&](std::uint8_t c) {
            return static_cast<std::uint32_t>(static_cast<int>(static_cast<float>(c) * intensity) & 0xFF);
        };
        if (info.dAlpha != 0 && info.alpha > 0) {
            const int d = static_cast<int>(static_cast<float>(info.dAlpha) * frames);
            info.alpha = info.alpha + d < 0 ? std::uint8_t{0} : static_cast<std::uint8_t>(info.alpha + d);
        }
        pos.color = (static_cast<std::uint32_t>(info.alpha) << 24) | (scaled(info.red) << 16) |
                    (scaled(info.green) << 8) | scaled(info.blue);
        pos.rotation = static_cast<std::int8_t>(
            pos.rotation + static_cast<std::int8_t>(static_cast<int>(static_cast<float>(info.dRotation) * frames)));
        pos.radius = frames * info.dRadius + pos.radius;

        if ((pos.flags & BirthRule::kStopAtHeight) && pos.position.y < pos.height) {
            pos.position.y = pos.height;
            info.gravity = 0.0f;
            info.invMass = 0.0f;
            info.velocity = {};
            info.life = 0.0f;
        } else if ((pos.flags & BirthRule::kBounce) && pos.position.y < pos.radius + pos.height) {
            pos.position.y = pos.height;
            if (info.velocity.y < 0.0f) {
                info.velocity.x = info.damp * info.velocity.x;
                info.velocity.y = -(info.damp * info.velocity.y);
                info.velocity.z = info.damp * info.velocity.z;
            }
        }
        if (pos.flags & BirthRule::kCycleFrames) {
            const int start = info.frameStart, end = info.frameEnd;
            if (start < end)
                pos.frame = static_cast<std::int8_t>((pos.frame - start + 1) % (end - start) + start);
        }
        ++i;
    }
}

int FixedTicker::advance(float dt, int maxSteps) {
    m_accumulator += std::max(dt, 0.0f);
    // A hair of slack so a frame of exactly kStep always yields one update.
    int steps = static_cast<int>((m_accumulator + 1e-6f) / kStep);
    m_accumulator = std::max(0.0f, m_accumulator - static_cast<float>(steps) * kStep);
    if (steps > maxSteps) {
        steps = maxSteps;
        m_accumulator = 0.0f;
    }
    return steps;
}

} // namespace mm2::game::fx
