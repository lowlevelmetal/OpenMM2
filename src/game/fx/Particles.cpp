#include "game/fx/Particles.h"

#include <algorithm>
#include <cmath>

namespace mm2::game::fx {

void ParticleSystem::init(int maxParticles, int framesWide, int framesHigh, float capacityScale) {
    const int capacity = std::max(1, static_cast<int>(static_cast<float>(maxParticles) * capacityScale));
    m_info.assign(static_cast<std::size_t>(capacity), {});
    m_pos.assign(static_cast<std::size_t>(capacity), {});
    m_framesWide = std::max(1, framesWide);
    m_framesHigh = std::max(1, framesHigh);
    reset();
}

void ParticleSystem::reset() {
    m_count = 0;
    m_elapsed = 0.0f;
    m_spewFraction = 0.0f;
}

// asBirthRule::InitSpark. The order of the random draws follows the original
// (for each vector: y, then x, then z).
void ParticleSystem::initSpark(const BirthRule& r, SparkInfo& info, SparkPos& pos) {
    {
        const float ry = (m_rand.frand() - 0.5f) * r.velocityVar.y;
        const float rx = (m_rand.frand() - 0.5f) * r.velocityVar.x;
        const float rz = (m_rand.frand() - 0.5f) * r.velocityVar.z;
        info.velocity = {r.velocity.x + rx, r.velocity.y + ry, rz + r.velocity.z};
    }
    const float mass = (m_rand.frand() - 0.5f) * r.massVar + r.mass;
    info.invMass = 1.0f / mass;
    info.life = (m_rand.frand() - 0.5f) * r.lifeVar + r.life;
    info.drag = (m_rand.frand() - 0.5f) * r.dragVar + r.drag;
    info.damp = (m_rand.frand() - 0.5f) * r.dampVar + r.damp;

    if (r.birthFlags & BirthRule::kCycleFrames) {
        pos.frame = static_cast<std::int8_t>(r.texFrameStart);
    } else {
        const float start = static_cast<float>(r.texFrameStart);
        const float span = static_cast<float>(r.texFrameEnd + 1) - start;
        pos.frame = static_cast<std::int8_t>(static_cast<int>(m_rand.frand() * span + start));
    }
    pos.radius = (m_rand.frand() - 0.5f) * r.radiusVar + r.radius;
    {
        const float ry = (m_rand.frand() - 0.5f) * r.positionVar.y;
        const float rx = (m_rand.frand() - 0.5f) * r.positionVar.x;
        const float rz = (m_rand.frand() - 0.5f) * r.positionVar.z;
        pos.position = {r.position.x + rx, r.position.y + ry, rz + r.position.z};
    }
    // MM1 starts every particle white; MM2 rules may carry a Color (inferred use).
    pos.color = r.color;
    // The original leaves the rotation of a reused slot as it was; OpenMM2
    // starts every particle at 0 so runs are reproducible.
    pos.rotation = 0;
    info.gravity = r.gravity;
    info.dRadius = (m_rand.frand() - 0.5f) * r.dRadiusVar + r.dRadius;
    info.dAlpha = static_cast<std::int8_t>(static_cast<int>(
        static_cast<float>(r.dAlpha) + (m_rand.frand() - 0.5f) * static_cast<float>(r.dAlphaVar)));
    info.dRotation = static_cast<std::int8_t>(static_cast<int>(
        static_cast<float>(r.dRotation) + (m_rand.frand() - 0.5f) * static_cast<float>(r.dRotationVar)));
}

void ParticleSystem::blast(int count, const BirthRule* rule) {
    if (!rule)
        rule = m_rule;
    if (!rule || m_info.empty())
        return;
    count = std::min(count, capacity() - m_count);
    for (int i = 0; i < count; ++i, ++m_count) {
        auto& info = m_info[static_cast<std::size_t>(m_count)];
        auto& pos = m_pos[static_cast<std::size_t>(m_count)];
        initSpark(*rule, info, pos);
        if (m_matrix)
            pos.position = m_matrix->transform(pos.position);
    }
}

void ParticleSystem::update(float delta) {
    if (delta <= 0.0f)
        return;
    const float framesDelta = delta * kFrameRate;
    const int oldFrames = static_cast<int>(m_elapsed * kFrameRate);
    m_elapsed += delta;
    const int frames = static_cast<int>(m_elapsed * kFrameRate) - oldFrames;

    if (m_rule && (m_elapsed < m_rule->spewTimeLimit || m_rule->spewTimeLimit == 0.0f)) {
        m_spewFraction += m_rule->spewRate * delta;
        const int spew = static_cast<int>(m_spewFraction);
        m_spewFraction -= static_cast<float>(spew);
        if (spew)
            blast(spew, nullptr);
    }

    for (int i = 0; i < m_count;) {
        SparkInfo& info = m_info[static_cast<std::size_t>(i)];
        SparkPos& pos = m_pos[static_cast<std::size_t>(i)];
        info.life -= delta;
        if (info.life < 0.0f || pos.position.y < -50.0f || !(pos.color & 0xFF000000u)) {
            --m_count;
            info = m_info[static_cast<std::size_t>(m_count)];
            pos = m_pos[static_cast<std::size_t>(m_count)];
            continue;
        }

        Vec3 accel = wind - info.velocity;
        accel *= accel.mag() * windDensity * info.drag * info.invMass;
        accel.y += info.gravity;
        info.velocity += accel * delta;
        // The original multiplied by Damp once per update at its 30 Hz-ish
        // rate; raising it to the elapsed frame count keeps that behaviour
        // at any frame rate (Open1560 uses a linear approximation instead).
        info.velocity *= std::pow(info.damp, framesDelta);

        pos.position += info.velocity * delta;
        pos.radius += info.dRadius * framesDelta;

        if (m_rule && (m_rule->birthFlags & BirthRule::kSplashes)) {
            const float ground = splashHeight ? splashHeight(pos.position) : 0.0f;
            if (pos.position.y < ground) {
                pos.position.y = ground;
                pos.frame = static_cast<std::int8_t>(pos.frame + 4);
                info.gravity = 0.0f;
                info.invMass = 0.0f;
                info.velocity = {};
                info.life = 0.1f;
            }
        }

        if (frames) {
            if (info.dAlpha && pos.color) {
                const int dAlpha = info.dAlpha * frames;
                const int alpha = static_cast<int>(pos.color >> 24);
                if (alpha + dAlpha >= 0)
                    pos.color += static_cast<std::uint32_t>(dAlpha) << 24;
                else
                    pos.color -= static_cast<std::uint32_t>(alpha) << 24;
            }
            pos.rotation = static_cast<std::int8_t>(pos.rotation + static_cast<std::int8_t>(info.dRotation * frames));
            if (m_rule && (m_rule->birthFlags & BirthRule::kCycleFrames)) {
                const int start = m_rule->texFrameStart, end = m_rule->texFrameEnd;
                if (start < end)
                    pos.frame = static_cast<std::int8_t>(start + (pos.frame - start + frames) % (end - start));
            }
        }
        ++i;
    }
}

} // namespace mm2::game::fx
