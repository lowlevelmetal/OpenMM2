/*
    OpenMM2 - a car's effects: tyre tracks (vehCar::UpdateTrack), wheel
    particles (vehWheelPtx) and damage/exhaust smoke (vehCarDamage).
*/
#pragma once

#include "game/fx/EffectLibrary.h"
#include "game/fx/ParticleRenderer.h"
#include "game/fx/Particles.h"
#include "game/fx/SkidMarks.h"
#include "phys/vehicle/CarSim.h"

#include <array>
#include <optional>

namespace mm2::game::fx {

struct VehicleFxSetup {
    // The particle fields of tune/vehicle/<car>.vehCarDamage, read over the
    // engine smoke defaults (see engineSmokeDefaults()).
    BirthRule smokeRule = engineSmokeDefaults();
    // geometry/<car>_exhaust0.mtx / _exhaust1.mtx pivots (model space).
    std::array<std::optional<Vec3>, 2> exhaust;
    // mmGame::InitWeather in rain: the road's tyre smoke turns into splash.
    bool rain = false;

    // vehCarDamage::Init's EngineSmokeRule before the car's file is read.
    static BirthRule engineSmokeDefaults();
};

// What the car's surroundings say this frame.
struct VehicleFxContext {
    // vehCar::UpdateTrack lays no tracks while the car's room is an
    // intersection (PSDL room flag 0x10).
    bool tracksAllowed = true;
};

class VehicleEffects {
public:
    // vehWheelPtx: 128 particles from texture/ptx_wheel (8 x 8 frames).
    static constexpr int kWheelParticles = 128;
    // vehCarDamage: 64 smoke particles from texture/fxpt8 (2 x 2 frames).
    static constexpr int kSmokeParticles = 64;

    VehicleEffects(const EffectLibrary& library, const VehicleFxSetup& setup);

    void reset();
    // Runs the original per-frame updates at a fixed 60 Hz (FixedTicker).
    void update(float dt, const phys::CarSim& car, const VehicleFxContext& context = {});
    // Tracks and particles; call inside the scene pass.
    void draw(render::Device& device, TextureLibrary& textures, ParticleRenderer& cards, SkidRenderer& skids,
              const Mat34& cameraBasis);

    const std::array<SkidTrack, 4>& tracks() const { return m_tracks; }
    const ParticleSystem& wheelParticles() const { return m_wheelPtx; }
    const ParticleSystem& smoke() const { return m_smoke; }
    int liveParticles() const { return m_wheelPtx.count() + m_smoke.count(); }

private:
    void step(float dt, const phys::CarSim& car, const VehicleFxContext& context);
    void blastWheel(const phys::Wheel& wheel, float dt, float threshold, int rule, int slot);
    void spewSmoke(const Mat34& car, const Vec3& offset, float amount);

    std::array<SkidTrack, 4> m_tracks;
    ParticleSystem m_wheelPtx;
    std::array<BirthRule, EffectLibrary::kWheelRules> m_wheelRules{};
    std::array<float, 2> m_wheelFraction{}; // one per ptxindex slot, shared by the four wheels
    ParticleSystem m_smoke;
    BirthRule m_smokeRule;
    VehicleFxSetup m_setup;
    float m_smokeFraction = 0.0f;
    int m_nextPivot = 0; // DoublePivot alternation
    FixedTicker m_ticker;
};

} // namespace mm2::game::fx
