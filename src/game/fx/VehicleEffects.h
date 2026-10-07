/*
    OpenMM2 - per-vehicle effects: skid marks, surface particles, damage smoke.
    Particle accumulation ported from Open1560 (mmcar/wheel.cpp
    mmWheel::GenerateSkidParticles), Copyright (C) 2020 Brick, GPL-3.0-or-later.
*/
#pragma once

#include "game/fx/EffectLibrary.h"
#include "game/fx/ParticleRenderer.h"
#include "game/fx/Particles.h"
#include "game/fx/SkidMarks.h"

#include <array>
#include <memory>

namespace mm2::game::fx {

struct WheelFxInput {
    SkidInput skid;
    // Surface particle selection from the ground material (city/materials.mtl
    // ptxindex / ptxthreshold); -1 = none.
    int ptxIndex[2] = {-1, -1};
    float ptxThreshold[2] = {0.25f, 0.5f};
};

struct VehicleFxInput {
    std::array<WheelFxInput, 4> wheels;
    Mat34 body;          // car model matrix (for damage smoke)
    float damage01 = 0.0f; // vehCarDamage fraction (CarDamage::damage)
};

class VehicleEffects {
public:
    // MM1 PtxMaxSkidCount = PtxFrameRate * maxskid (default 1).
    static constexpr float kMaxSkidCount = 30.0f;

    // `damageRule`: the particle fields of tune/vehicle/<car>.vehCarDamage
    // (optional), with its SmokeOffset/SmokeOffset2 emitter positions.
    VehicleEffects(const EffectLibrary& library, const BirthRule* damageRule = nullptr,
                   Vec3 smokeOffset = {}, std::optional<Vec3> smokeOffset2 = {});

    void reset();
    void update(float dt, const VehicleFxInput& in);
    // Skid marks and particles; call inside the scene pass.
    void draw(render::Device& device, TextureLibrary& textures, ParticleRenderer& cards, SkidRenderer& skids,
              const Mat34& cameraBasis);

    float particleMultiplier = 1.0f; // ParticleMultiplier (graphics option)
    const std::array<SkidTrail, 4>& trails() const { return m_trails; }
    int liveParticles() const;

private:
    std::array<SkidTrail, 4> m_trails{SkidTrail(64), SkidTrail(64), SkidTrail(64), SkidTrail(64)};
    std::array<float, 4> m_particleCount{};
    // One system per surface effect (ptxindex 0..8), drawing from ptx_wheel.
    std::array<ParticleSystem, 9> m_surface;
    std::array<BirthRule, 9> m_surfaceRules;
    std::array<Mat34, 4> m_emitters;
    ParticleSystem m_smoke;
    BirthRule m_smokeRule;
    bool m_hasSmoke = false;
    Vec3 m_smokeOffset;
    std::optional<Vec3> m_smokeOffset2;
    Mat34 m_smokeMatrix;
};

} // namespace mm2::game::fx
