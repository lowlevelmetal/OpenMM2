#pragma once

// The physics world: MM2's dgPhysManager (the per-sample update of every
// simulated body and the collisions between bodies, the city and the
// objects in it), ported from the code of midtown2.exe build 3393. See
// docs/physics.md, "How a sample runs" and "Collision".

#include "phys/Bound.h"
#include "phys/Collider.h"
#include "phys/Collision.h"
#include "phys/Constants.h"
#include "phys/InertialCS.h"
#include "phys/Joint.h"
#include "phys/Level.h"
#include "phys/Material.h"
#include "phys/PolygonSoup.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace mm2::phys {

// Ray/segment queries against the level (the Angel engine's lvlSegment /
// mmIntersection probes used by wheels).
class GroundQuery {
public:
    virtual ~GroundQuery() = default;
    // Nearest hit on the segment a->b.
    virtual bool probe(const Vec3& a, const Vec3& b, RayHit& hit) const = 0;
    virtual const Material& material(int index) const = 0;
};

// Infinite plane y = height (headless testing, simcar).
class FlatGround final : public GroundQuery {
public:
    explicit FlatGround(float height = 0.0f, Material material = {})
        : m_height(height), m_material(material) {}
    bool probe(const Vec3& a, const Vec3& b, RayHit& hit) const override;
    const Material& material(int) const override { return m_material; }

private:
    float m_height;
    Material m_material;
};

class Body;
class World;

// Per-sample hooks, called in the order of the Angel node tree for a car:
//   beforeIntegrate  (mmCarSim::Update: inputs; vehEngine, vehTransmission)
//   InertialCS::update (integrates last sample's forces)
//   afterIntegrate   (LCS children: aero, drivetrains -> wheels, which
//                     accumulate forces for the next sample; mmStuck; a
//                     trailer's joint, which adds its forces last)
//   collisions (impacts reach the body's Collider::handler), the pending
//   push (phColliderBase::UpdateMtx), then afterCollisions.
class BodyController {
public:
    virtual ~BodyController() = default;
    virtual void beforeIntegrate(Body&, float /*dt*/, const World&) {}
    virtual void afterIntegrate(Body&, float dt, const World& world) = 0;
    virtual void afterCollisions(Body&, float /*dt*/, const World&) {}
};

// A simulated body (a mover of dgPhysManager): its rigid body (phInertialCS),
// its collider and bounds, and its place among the level's instances.
class Body : public Instance {
public:
    Body();
    Body(const Body&) = delete;
    Body& operator=(const Body&) = delete;

    InertialCS ics;
    BodyController* controller = nullptr;
    // phColliderJointed::Attach: the joint linking this body to another one
    // (not owned). Bodies sharing an unbroken joint do not collide with each
    // other (dgPhysManager::Update), and impacts see the joint's inverse
    // mass matrix.
    const Joint* joint = nullptr;

    // GetBound(0): the bound the body collides with (not owned; null: the
    // body does not collide). GetBound(1): what spheres and hotdogs use
    // against the city (null: the same).
    const Bound* collisionBound = nullptr;
    const Bound* terrainBound = nullptr;
    // Where the bound's origin sits in the body's (ICS) frame: a car's bound
    // is in model space, whose origin is at CenterOfGravity from the centre
    // of mass (vehCarSim::SetWorldMatrix).
    Vec3 boundOrigin;
    // The bound's world matrix (the collider's matrix): the ICS matrix moved
    // to boundOrigin, refreshed after each integration, so it does not see
    // the sample's push until the next one (as vehCarSim's world matrix).
    Mat34 boundMatrix;
    Collider collider;

    // dgPhysManager mover flags: 2 collide with the city, 8 with the
    // instances of the rooms around, 0x10 with the other movers.
    bool collideTerrain = true;
    bool collideInstances = true;
    bool collideMovers = true;
    // dgPhysManager's PlayerInst: what it hits gets hitByPlayer.
    bool player = false;
    // A body whose motion comes from outside (network cars): not
    // integrated, others collide with it as with an object that does not
    // move (its collider has no ICS).
    bool kinematic = false;

    // Places the body's centre of mass frame and resets its motion and
    // collider (the matrix the next sweep starts from).
    void place(const Mat34& icsMatrix);
    // Refreshes boundMatrix from the ICS.
    void syncBoundMatrix();
    // Points the collider at the bound and matrix (call after changing
    // collisionBound or kinematic).
    void resetCollider();

    // Instance.
    const Bound* bound(int which) const override;
    const Mat34& matrix() const override { return boundMatrix; }
    // The bound's sphere about the bound's origin (lvlInstance::GetRadius
    // returns the geometry's radius; OpenMM2 uses the bound's, inferred).
    float radius() const override;
    Body* entity() override { return this; }

    // The bound's box in the world (for callers that want an extent).
    Aabb aabb() const;
};

class World final : public GroundQuery {
public:
    World();
    explicit World(MaterialTable materials);
    // The helper colliders point into the world: not copyable.
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    void setMaterials(MaterialTable materials) { m_materials = std::move(materials); }
    const MaterialTable& materials() const { return m_materials; }
    // Probe geometry (wheels, line of sight, spawning); finalize()d.
    void setStatic(PolygonSoup soup) { m_static = std::move(soup); }
    const PolygonSoup& staticGeometry() const { return m_static; }
    // The level bodies collide with (rooms, city polygons, instances); not
    // owned. Without one, bodies only collide with each other.
    void setLevel(const Level* level);
    const Level* level() const { return m_level; }

    // Bodies are not owned. Update order is insertion order (deterministic;
    // MM2 updates its movers in the order they were declared each frame).
    // add() sets the body's gravity to (0, -kGravity, 0) unless it was
    // changed from the asInertialCS default. remove() may be called during
    // a step (from impact callbacks); the body must stay alive until the
    // step ends.
    void add(Body* body);
    void remove(Body* body);
    bool contains(const Body* body) const;

    // One simulation sample.
    void step(float dt);
    // Deterministic fixed-step driver: whole samples of `sampleStep`, the
    // remainder carried to the next call. Returns the number of samples.
    int advanceFixed(float frameDelta, float sampleStep = kFixedSampleStep, int maxSamples = 32);
    // The original scheme: n = min(ceil((delta - 0.001) / step), max)
    // samples of delta / n each (frame-rate dependent).
    int advanceOversampled(float frameDelta, float sampleStep = kOversampleStep,
                           int maxSamples = kOversampleMaxSamples);
    float interpolationAlpha(float sampleStep = kFixedSampleStep) const { return m_accumulator / sampleStep; }

    double time() const { return m_time; }

    bool probe(const Vec3& a, const Vec3& b, RayHit& hit) const override;
    const Material& material(int index) const override { return m_materials[index]; }

    // The game's random generator for the simulation (irand / frand). MM2
    // shares one rand() across the whole game; OpenMM2 keeps one stream per
    // world so a race, replay or test is reproducible on its own.
    std::uint32_t* randomSeed() const { return &m_randomSeed; }
    void seedRandom(std::uint32_t seed) { m_randomSeed = seed; }

    // dgPhysManager::NewMover: a body the collisions just set in motion
    // (an attached traffic car or banger) joins the movers; it collides
    // from the next sample on. Called by World itself for instances whose
    // attachEntity() returned a body; owners may call it too.
    void addNewMover(Body* body);

    // Statistics of the last step (debugging).
    struct Stats {
        int terrainImpacts = 0;
        int instanceImpacts = 0;
        int collidables = 0;
    };
    const Stats& stats() const { return m_stats; }

private:
    struct Mover {
        Body* body = nullptr;
        bool fresh = false;   // NewMover's 0x100: no update or collision until the sample ends
        bool removed = false; // removed during a step (dropped when it ends)
        std::vector<Instance*> collidables;
    };
    bool live(const Mover& m) const { return !m.removed; }

    void beginFrame();
    void gatherCollidables(Mover& mover);
    bool trivialCollide(const Instance& a, const Instance& b) const;
    void collideTerrain(Body& body);
    bool collideInstances(Instance& a, Instance& b);

    mutable std::uint32_t m_randomSeed = 1;
    MaterialTable m_materials;
    PolygonSoup m_static;
    const Level* m_level = nullptr;
    std::vector<Mover> m_movers;
    bool m_stepping = false;
    float m_accumulator = 0;
    double m_time = 0;
    Stats m_stats;

    // dgPhysManager's static buffers and helper colliders: the level's
    // collider, the temporary colliders of static instances, the
    // intersection and impact tables.
    LevelBound m_levelBound;
    Mat34 m_identity;
    Collider m_levelCollider;
    Collider m_tempA, m_tempB;
    Mat34 m_tempMatrixA, m_tempMatrixB;
    std::vector<Intersection> m_isectsA, m_isectsB;
    std::vector<Impact> m_impacts;
    std::vector<Instance*> m_roomScratch;
};

} // namespace mm2::phys
