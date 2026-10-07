/*
    OpenMM2 - runtime breakable props (bangers).
    Behaviour ported from Open1560 (code/midtown/mmbangers/banger.cpp,
    active.cpp: mmUnhitBangerInstance::Impact, mmBangerActive,
    mmBangerActiveManager; impulse limit from game.asm asBound::Impact),
    Copyright (C) 2020 Brick, GPL-3.0-or-later.
*/
#pragma once

#include "game/Camera.h"
#include "game/ModelLibrary.h"
#include "game/TextureLibrary.h"
#include "game/bangers/BangerData.h"
#include "game/bangers/PropPlacement.h"
#include "game/fx/EffectLibrary.h"
#include "game/fx/ParticleRenderer.h"
#include "game/fx/Particles.h"
#include "phys/World.h"
#include "render/Device.h"

#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace mm2::game::bangers {

// All props of a city that can be knocked over.
//
// Unhit props are static and cost nothing. When a vehicle's box touches one
// it is "hit" (mmUnhitBangerInstance::Impact): it becomes a rigid body in the
// physics world, receiving the impulse of the collision capped at
// sqrt(ImpulseLimit2) (asBound::Impact limits the impulse an object can take
// in one collision); the vehicle receives the opposite impulse, so light or
// weak props barely slow it down. Props with breakable parts (NumParts)
// split into their BREAKnn pieces. At most 32 props are simulated at once
// (MAX_ACTIVE_BANGERS); when they come to rest they stay where they fell
// and can be hit again.
//
// OpenMM2 detects the touch with a box test against each vehicle before the
// physics step instead of through the engine's collision manager (not
// ported); the resulting impulses follow the MM1 rules above.
class BangerSet {
public:
    static constexpr int kMaxActive = 32; // MAX_ACTIVE_BANGERS

    explicit BangerSet(const BangerDataLibrary& data);
    ~BangerSet();
    BangerSet(const BangerSet&) = delete;
    BangerSet& operator=(const BangerSet&) = delete;

    // Adds props that have banger data (others are ignored and counted).
    void add(const std::vector<PlacedProp>& props);
    // Physics world that active props join. Must outlive this set (or call
    // setWorld(nullptr) first).
    void setWorld(phys::World* world);

    // Call once per frame before stepping the physics world. `vehicles` are
    // the bodies that can knock props over (player, AI, network cars).
    void update(float dt, std::span<phys::Body* const> vehicles);

    struct DrawParams {
        float lodScale = 1.0f;
        float maxDistance = 400.0f; // e.g. the fog end
        bool night = false;          // draw lamp glows
    };
    // Inside a scene pass after setFrameConstants().
    void draw(render::Device& device, ModelLibrary& models, TextureLibrary& textures, fx::ParticleRenderer& cards,
              const Frustum& frustum, const Camera& camera, const DrawParams& params);

    // Resets every prop to its original place (race restart).
    void reset();

    float particleMultiplier = 1.0f;

    enum class State : std::uint8_t { Unhit, Active, Hit, Gone };
    struct Instance {
        const BangerData* data = nullptr;
        std::string model;
        int part = -1;   // -1 whole model, k = mesh "BREAK{k+1:02}"
        Mat34 ground;    // placement of the model's ground origin
        Mat34 matrix;    // current frame at the CG (meshes are centred on it)
        State state = State::Unhit;
        int active = -1; // index into the active pool
        int room = 0;
    };
    const std::vector<Instance>& instances() const { return m_instances; }
    std::size_t skipped() const { return m_skipped; } // props without banger data
    int activeCount() const;
    int hitCount() const;

    // Debug/testing: hit instance `i` as if `vehicle` touched it with contact
    // normal `n` (from the prop towards the vehicle) at `point`.
    void impact(std::size_t i, phys::Body& vehicle, const Vec3& point, const Vec3& n);

private:
    struct Active;
    using CellKey = std::int64_t;
    CellKey cellOf(const Vec3& p) const;
    void insertCell(std::size_t i);
    void removeCell(std::size_t i);
    phys::Obb obbOf(const Instance& inst) const;
    int activate(std::size_t i, const Vec3& impulse, const Vec3& point, const Vec3& carryVelocity);
    void deactivate(int activeIndex, State newState);

    const BangerDataLibrary& m_data;
    phys::World* m_world = nullptr;
    std::vector<Instance> m_instances;
    std::vector<Mat34> m_initial;
    std::vector<std::unique_ptr<Active>> m_active; // fixed pool of kMaxActive
    std::unordered_map<CellKey, std::vector<std::size_t>> m_grid;
    std::vector<CellKey> m_cellOfInstance;
    std::size_t m_skipped = 0;
    std::uint64_t m_activationSerial = 0;
};

} // namespace mm2::game::bangers
