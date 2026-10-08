/*
    OpenMM2 - camera-facing particle cards (asMeshCardInfo).
    Card geometry first ported from Open1560 (agiworld/meshrend.cpp),
    Copyright (C) 2020 Brick, GPL-3.0-or-later; follows MM2's
    asMeshCardInfo::Init, Draw and DrawShadows.
*/
#pragma once

#include "game/TextureLibrary.h"
#include "game/fx/Particles.h"
#include "render/Device.h"

#include <array>
#include <vector>

namespace mm2::game::fx {

// The defaults are the render state MM2 draws particles in: asParticles::Cull
// runs from lvlLevel's late draw callbacks, after cityLevel::DrawRooms has
// switched fog off for the glows and left normal alpha blending (with the
// default alpha test, alpha not 0) and no depth writes.
struct CardStyle {
    render::BlendMode blend = render::BlendMode::Alpha;
    bool fog = false;
    Vec4 tint{1, 1, 1, 1};
};

// Draws particles as textured quads facing the camera. Call inside a scene
// pass after Device::setFrameConstants(); geometry goes through per-frame
// transient buffers.
class ParticleRenderer {
public:
    // Rotation steps of a card (asParticles::Init passes 32).
    static constexpr int kRotations = 32;

    ParticleRenderer();

    // `cameraBasis`: the view's world placement (m0 right, m1 up). The
    // texture is a sheet of system.framesWide() x framesHigh() frames, frame
    // 0 at the start of the texture's first stored row. Particles flagged
    // BirthRule::kShadow also get their flat shadow, drawn first
    // (asParticles::Cull).
    void draw(render::Device& device, const Mat34& cameraBasis, const ParticleSystem& system,
              const WorldTexture* texture, const CardStyle& style = {});

    // Lower-level: append cards for arbitrary particles and flush once.
    void begin();
    void add(const SparkPos& p, int framesWide, int framesHigh, const Mat34& cameraBasis);
    void flush(render::Device& device, const WorldTexture* texture, const CardStyle& style);

    int cardsDrawn() const { return m_cardsDrawn; }

private:
    std::array<std::array<Vec2, 4>, kRotations> m_rot{};
    std::vector<render::Vertex3D> m_vertices, m_shadows;
    int m_cardsDrawn = 0;
};

} // namespace mm2::game::fx
