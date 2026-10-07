/*
    OpenMM2 - runtime breakable props (bangers), after MM2's dgBangerManager,
    dgUnhitBangerInstance, dgHitBangerInstance, dgBangerActive(Manager) and
    dgImpact::CalcImpact. Structure first ported from Open1560
    (mmbangers/banger.cpp, active.cpp), Copyright (C) 2020 Brick,
    GPL-3.0-or-later.
*/
#pragma once

#include "game/Camera.h"
#include "game/MeshDraw.h"
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
// Unhit props stand still. When a vehicle touches one, dgImpact::CalcImpact
// asks for the impulse J that would stop the car's contact point against an
// immovable prop: up to sqrt(ImpulseLimit2) the prop holds like a wall and
// the car bounces off it; beyond, the car spends that limit on breaking it
// loose and the rest is a normal collision between the two, the prop taking
// its share. The broken prop becomes a hit instance (a ring of 40: the
// oldest knocked-over prop disappears when the ring wraps) or splits into
// its BREAKnn parts, and is simulated by one of 32 actives until it sleeps.
//
// OpenMM2 detects the touch with a box test against each vehicle before the
// physics step instead of through MM2's collision manager, and solves the
// impulses along the contact normal with the car's effective mass and the
// prop's mass (MM2 uses the full 3D impulse with a friction cone).
class BangerSet {
public:
    static constexpr int kMaxActive = 32; // dgBangerActiveManager
    static constexpr int kMaxHit = 40;    // dgBangerManager::Init(40)

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
        ObjectDetail detail;
        // dgBangerManager::InitGlow is called at night only (mmGame::InitWeather).
        bool glows = false;
    };
    // Inside a scene pass after setFrameConstants().
    void draw(render::Device& device, ModelLibrary& models, TextureLibrary& textures, fx::ParticleRenderer& cards,
              const Frustum& frustum, const Camera& camera, const DrawParams& params);

    // Resets every prop to its original place (race restart).
    void reset();

    enum class State : std::uint8_t { Unhit, Active, Hit, Gone };
    struct Instance {
        const BangerData* data = nullptr;
        std::string model;
        int part = -1;   // -1 whole model, k = mesh "BREAK{k+1:02}"
        std::string mesh; // a car part's mesh (ejected parts), else empty
        int paint = 0;
        Mat34 ground;    // placement of the model's ground origin
        Mat34 matrix;    // current frame at the CG (meshes are centred on it)
        State state = State::Unhit;
        bool everHit = false; // a hit instance (in the ring) rather than the original
        int active = -1; // index into the active pool
        int room = 0;
    };
    const std::vector<Instance>& instances() const { return m_instances; }
    std::size_t skipped() const { return m_skipped; } // props without banger data
    int activeCount() const;
    int hitCount() const;

    // Debug/testing: `vehicle` touches instance `i` at `point` with contact
    // normal `n` (from the prop towards the vehicle).
    void impact(std::size_t i, phys::Body& vehicle, const Vec3& point, const Vec3& n);

    // vehBreakableMgr::Eject: a car part flies off as a knocked-over banger.
    // `mesh` is the part of `model` (the car's PKG) to draw with paint job
    // `paint`; `frame` its world placement (the part's pivot). It leaves in
    // a random upward direction at `speed` +- 1 m/s, spinning at 1-3 rad/s.
    void ejectPart(const BangerData& data, const std::string& model, const std::string& mesh, int paint,
                   const Mat34& frame, float speed);

private:
    struct Active;
    using CellKey = std::int64_t;
    CellKey cellOf(const Vec3& p) const;
    void insertCell(std::size_t i);
    void removeCell(std::size_t i);
    phys::Obb obbOf(const Instance& inst) const;
    int attach(std::size_t i, bool blast);
    void detach(int activeIndex);
    void takeRingSlot(std::size_t i);

    const BangerDataLibrary& m_data;
    phys::World* m_world = nullptr;
    std::vector<Instance> m_instances;
    std::vector<Mat34> m_initial;
    std::vector<std::unique_ptr<Active>> m_active; // fixed pool of kMaxActive
    std::vector<int> m_activeList;                 // attached actives in attach order (swap-removed)
    std::vector<std::size_t> m_ring;               // hit instances by ring slot
    std::size_t m_ringNext = 0;
    std::unordered_map<CellKey, std::vector<std::size_t>> m_grid;
    std::vector<CellKey> m_cellOfInstance;
    std::size_t m_skipped = 0;
    fx::FixedTicker m_ticker;
    fx::Rand m_glowRand{1};
    fx::Rand m_ejectRand{0xE7EC7u};
};

} // namespace mm2::game::bangers
