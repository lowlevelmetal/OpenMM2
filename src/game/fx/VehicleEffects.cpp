#include "game/fx/VehicleEffects.h"

#include <algorithm>
#include <cmath>

namespace mm2::game::fx {

VehicleEffects::VehicleEffects(const EffectLibrary& library, const BirthRule* damageRule, Vec3 smokeOffset,
                               std::optional<Vec3> smokeOffset2)
    : m_smokeOffset(smokeOffset), m_smokeOffset2(smokeOffset2) {
    const ParticleSheet sheet = EffectLibrary::wheelSheet();
    for (int i = 0; i < static_cast<int>(m_surface.size()); ++i) {
        auto& sys = m_surface[static_cast<std::size_t>(i)];
        sys.init(32, sheet.framesWide, sheet.framesHigh); // MM1 wheel particle systems hold 32 (x2)
        // The effect files store the editor position where the designer
        // previewed them (e.g. smoke at -1310, 11, -462); emitters place the
        // particles themselves, so the rule's own position is cleared.
        if (const BirthRule* r = library.surfaceRule(i)) {
            m_surfaceRules[static_cast<std::size_t>(i)] = *r;
            m_surfaceRules[static_cast<std::size_t>(i)].position = {};
            sys.setBirthRule(&m_surfaceRules[static_cast<std::size_t>(i)]);
        }
        sys.rng().seed(0x1234u + static_cast<std::uint32_t>(i));
    }
    if (damageRule) {
        // Damage smoke draws from fxpt8 (2x2 smoke puffs), as MM1's car
        // particles did ("fxpt8", 2x2).
        m_smokeRule = *damageRule;
        m_smokeRule.position = {}; // vehCarDamage Position is an editor leftover too
        m_hasSmoke = true;
        m_smoke.init(32, 2, 2);
        m_smoke.setBirthRule(&m_smokeRule);
        m_smoke.setMatrix(&m_smokeMatrix);
    }
}

void VehicleEffects::reset() {
    for (auto& t : m_trails)
        t.reset();
    for (auto& s : m_surface)
        s.reset();
    m_smoke.reset();
    m_particleCount = {};
}

void VehicleEffects::update(float dt, const VehicleFxInput& in) {
    for (std::size_t w = 0; w < 4; ++w) {
        const WheelFxInput& wi = in.wheels[w];
        const bool skidding = m_trails[w].update(dt, wi.skid);

        // Surface rule for the current slip (selection inferred, see EffectLibrary).
        const float slip = std::max(std::abs(wi.skid.longSlip), std::abs(wi.skid.latSlip));
        int index = -1;
        if (wi.ptxIndex[1] >= 0 && slip >= wi.ptxThreshold[1])
            index = wi.ptxIndex[1];
        else if (wi.ptxIndex[0] >= 0 && slip >= wi.ptxThreshold[0])
            index = wi.ptxIndex[0];
        const bool moving = wi.skid.carSpeed > SkidTrail::kSpeedThreshold || wi.skid.wheelSpeed > SkidTrail::kSpeedThreshold;
        const bool emit = wi.skid.onGround && index >= 0 && index < static_cast<int>(m_surface.size()) &&
                          (skidding || (moving && wi.ptxThreshold[0] <= 0.0f && wi.ptxThreshold[1] <= 0.0f));
        if (emit) {
            // mmWheel::GenerateSkidParticles
            const float speed = clampf(wi.skid.carSpeed * 0.1f, 0.1f, 1.0f);
            m_particleCount[w] += dt * particleMultiplier * kMaxSkidCount * std::max(slip * speed, 0.25f);
            const int n = static_cast<int>(m_particleCount[w]);
            if (n > 0) {
                m_particleCount[w] -= static_cast<float>(n);
                auto& sys = m_surface[static_cast<std::size_t>(index)];
                m_emitters[w] = wi.skid.contact;
                sys.setMatrix(&m_emitters[w]);
                sys.blast(n);
                sys.setMatrix(nullptr);
            }
        } else {
            m_particleCount[w] = 0.0f;
        }
    }
    for (auto& s : m_surface)
        s.update(dt);

    if (m_hasSmoke) {
        // Smoke grows with damage (inferred: MM2's vehCarDamage SpewRate is 0
        // in the data, so the rate comes from the code; 0..30 puffs/s here).
        m_smokeMatrix = Mat34::translation(m_smokeOffset) * in.body;
        m_smokeRule.spewRate = in.damage01 > 0.05f ? 30.0f * in.damage01 : 0.0f;
        m_smoke.update(dt);
        if (m_smokeOffset2 && m_smokeRule.spewRate > 0.0f) {
            // Second exhaust/engine pivot (DoublePivot): emit the same amount there.
            const Mat34 second = Mat34::translation(*m_smokeOffset2) * in.body;
            m_smoke.setMatrix(&second);
            m_smoke.blast(static_cast<int>(m_smokeRule.spewRate * dt + 0.5f));
            m_smoke.setMatrix(&m_smokeMatrix);
        }
    }
}

int VehicleEffects::liveParticles() const {
    int n = m_smoke.count();
    for (const auto& s : m_surface)
        n += s.count();
    return n;
}

void VehicleEffects::draw(render::Device& device, TextureLibrary& textures, ParticleRenderer& cards,
                          SkidRenderer& skids, const Mat34& cameraBasis) {
    skids.draw(device, textures, {&m_trails[0], &m_trails[1], &m_trails[2], &m_trails[3]});
    const WorldTexture* wheelTex = textures.get(EffectLibrary::wheelSheet().texture);
    cards.begin();
    for (const auto& s : m_surface)
        for (const auto& p : s.positions())
            cards.add(p, s.framesWide(), s.framesHigh(), cameraBasis);
    cards.flush(device, wheelTex, {});
    if (m_hasSmoke && m_smoke.count())
        cards.draw(device, cameraBasis, m_smoke, textures.get("fxpt8"));
}

} // namespace mm2::game::fx
