#pragma once

// Lens flares (ltLensFlare, ltFlare; MM2 build 3393): the police sirens'
// flares, drawn after the 3D scene as screen-space cards.

#include "core/Math.h"
#include "game/fx/Random.h"
#include "render/Device.h"

#include <cstdint>
#include <span>
#include <vector>

namespace mm2::game {
class TextureLibrary;
}

namespace mm2::game::fx {

// One flare card queued for drawing, in normalised device coordinates.
struct LensFlareQuad {
    Vec2 min, max;
    std::uint32_t argb = 0;
};

class LensFlare {
public:
    // ltLensFlare(count): `count` flares from ltFlare::Random, then the first
    // on the light itself (0.3 across) and the second mirrored through the
    // screen's centre (0.25).
    LensFlare(int count, Rand& rng);

    // ltLensFlare::Draw: the cards for a light at `position` (world) of
    // `color` with ltLight::ComputeIntensity's `intensity`, seen through
    // `viewProj` on a screen of `aspect` (width / height).
    void draw(const Vec3& position, const Vec3& color, float intensity, const Mat44& viewProj, float aspect,
              std::vector<LensFlareQuad>& out) const;

    struct Flare {
        float r = 1.0f, g = 1.0f, b = 1.0f;
        float brightness = 1.0f; // colour scale, at most 1
        float along = 1.0f;      // position along the line through the centre
        float size = 0.1f;       // half size
        float reach = 0.0f;      // drawn while the light is this close to the centre
    };
    const std::vector<Flare>& flares() const { return m_flares; }

private:
    std::vector<Flare> m_flares;
};

// ltLight::ComputeIntensity of a spot light (intensity 25, exponent 3, the
// cars' ltLights) at `position` shining along `direction`, seen from `eye`,
// less `threshold` (0 below it).
float spotIntensity(const Vec3& position, const Vec3& direction, const Vec3& eye, float threshold);

// ltLensFlare::DrawBegin / ltLensFlare::DrawEnd: the queued cards added (ONE/ONE) over the
// whole screen with texture lt_flare, unlit, without depth.
void drawLensFlares(render::Device& device, TextureLibrary& textures, std::span<const LensFlareQuad> quads);

} // namespace mm2::game::fx
