#pragma once

#include "game/fx/EffectLibrary.h"
#include "game/fx/ParticleRenderer.h"
#include "game/fx/Particles.h"

namespace mm2::game::fx {

// Rain and snow around the camera from tune/rain.asbirthrule and
// tune/snow.asbirthrule, drawn from texture/ptx_rain (4x4 frames; rain uses
// frames 0-15 and splashes, snow frames 5-7 which hold the small flakes).
// Emitter placement relative to the camera is inferred: rain starts 20 m
// above and 10 m ahead of the camera, snow 6 m above and 6 m ahead.
class Weather {
public:
    enum class Kind { None, Rain, Snow };

    Weather(const EffectLibrary& library, Kind kind);
    Kind kind() const { return m_kind; }

    // `ground`: surface height under a point, for rain splashes.
    void update(float dt, const Mat34& camera, const std::function<float(const Vec3&)>& ground = {});
    void draw(render::Device& device, TextureLibrary& textures, ParticleRenderer& cards, const Mat34& camera);
    const ParticleSystem& particles() const { return m_system; }

private:
    Kind m_kind;
    BirthRule m_rule;
    ParticleSystem m_system;
    Mat34 m_emitter;
    bool m_ok = false;
};

} // namespace mm2::game::fx
