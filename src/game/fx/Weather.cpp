#include "game/fx/Weather.h"

namespace mm2::game::fx {

Weather::Weather(const EffectLibrary& library, Kind kind) : m_kind(kind) {
    const BirthRule* r = kind == Kind::Rain ? library.rule("rain") : kind == Kind::Snow ? library.rule("snow") : nullptr;
    if (!r)
        return;
    m_rule = *r;
    const ParticleSheet sheet = EffectLibrary::rainSheet();
    // Capacity: one second of spew plus slack.
    m_system.init(static_cast<int>(m_rule.spewRate * (m_rule.life + m_rule.lifeVar)) + 16, sheet.framesWide,
                  sheet.framesHigh, 1.5f);
    m_system.setBirthRule(&m_rule);
    m_system.setMatrix(&m_emitter);
    m_ok = true;
}

void Weather::update(float dt, const Mat34& camera, const std::function<float(const Vec3&)>& ground) {
    if (!m_ok)
        return;
    Vec3 forward = -camera.m2;
    forward.y = 0.0f;
    forward = forward.mag2() > 1e-6f ? forward.normalized() : Vec3{0, 0, -1};
    const bool rain = m_kind == Kind::Rain;
    m_emitter = Mat34::translation(camera.m3 + forward * (rain ? 10.0f : 6.0f) + Vec3{0, rain ? 20.0f : 6.0f, 0});
    m_system.splashHeight = ground;
    m_system.update(dt);
}

void Weather::draw(render::Device& device, TextureLibrary& textures, ParticleRenderer& cards, const Mat34& camera) {
    if (!m_ok || !m_system.count())
        return;
    cards.draw(device, camera, m_system, textures.get(EffectLibrary::rainSheet().texture));
}

} // namespace mm2::game::fx
