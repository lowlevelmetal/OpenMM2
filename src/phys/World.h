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
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace mm2::phys {

// Ray/segment queries against the level (the Angel engine's lvlSegment /
// mmIntersection probes used by wheels).
class GroundQuery {
public:
    virtual ~GroundQuery() = default;
    // Nearest hit on the segment a->b.
    virtual bool probe(const Vec3& a, const Vec3& b, RayHit& hit) const = 0;
    // The wheels' ground probe (dgPhysManager::Collide with vehWheel's
    // mask); `self` is never hit; `cache` is the wheel's lvlSegmentInfo
    // (null: none). Default: probe().
    virtual bool wheelProbe(const Vec3& a, const Vec3& b, RayHit& hit, const Instance* /*self*/,
                            ProbeCache* /*cache*/) const {
        return probe(a, b, hit);
    }
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
// its collider and bounds, and its place among the level's instances. It
// stands for dgPhysEntity: dgPhysEntity::GetCollider is `collider`,
// dgPhysEntity::PreUpdate the controller's hooks, dgPhysEntity::DetachMe
// and dgPhysEntity::FirstImpactCallback are empty in the base, and
// dgPhysEntity::RequiresTerrainCollision answers true (World::collideTerrain).
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

    // dgPhysManager::DeclareMover's flags: 0x1 the body updates (its
    // controller, its integration and its pending push), 0x2 it collides
    // with the city, 0x8 with the instances of the rooms around, 0x10 with
    // the other movers.
    bool updates = true;
    bool collideTerrain = true;
    bool collideInstances = true;
    bool collideMovers = true;
    // DeclareMover's type: 1 a body that is detached (Instance::detach) and
    // left out for the frame when its room is not one of the active rooms
    // (knocked-over props); 2 a plain mover; 3 and 4 make their room and its
    // neighbours active rooms (opponents near a player, the player); 4 is
    // also PlayerInst.
    int moverType = 2;
    // MM2's owners declare their movers anew every frame (the table is reset
    // after each frame); OpenMM2 keeps the last declaration, and an owner
    // sets `declared` false for a frame in which MM2 would not declare the
    // body (it then neither updates nor collides).
    bool declared = true;
    // dgPhysManager's PlayerInst: what it hits gets hitByPlayer.
    bool player = false;

    // dgPhysManager::DeclareMover(instance, type, flags) for the coming
    // frames: sets moverType and the flags above (type 4 also sets player)
    // and marks the body declared.
    void declare(int type, unsigned flags);
    // A body whose motion comes from outside (network cars): not
    // integrated, others collide with it as with an object that does not
    // move (its collider has no ICS).
    bool kinematic = false;
    // OpenMM2: a kinematic body that does move: what it hits sees this
    // velocity and spin (about the body's centre of mass), while it keeps
    // no momentum of its own (infinite mass).
    bool kinematicMoves = false;
    Vec3 kinematicVelocity, kinematicSpin;
    bool kinematicMotion(Vec3& velocity, Vec3& spin, Vec3& centre) const override;

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

// dgPhysManager (dgPhysManager::dgPhysManager also runs
// phContact::DisableContacts: no contacts, penetration 0).
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
    // remove() may be called during a step (from impact callbacks); the body
    // must stay alive until the step ends.
    //
    // Each frame (advanceFixed / advanceOversampled) takes, as
    // dgPhysManager::DeclareMover and Update do, the declared bodies in
    // order up to 32 (with a level, only those in a room), makes the rooms
    // of the type-3 and type-4 ones and their neighbours active (at most 20
    // rooms), and leaves out and detaches the type-1 bodies outside them.
    void add(Body* body);
    void remove(Body* body);
    bool contains(const Body* body) const;
    // dgPhysManager::DeclareMover of an instance without a body (an ambient
    // car off its rail: aiGoalAvoidPlayer and aiGoalRegainRail declare its
    // aiVehicleInstance (2, 0x0a) each frame, aiGoalCollision a wreck's
    // (2, 0x08)). It takes part in the next frame only, after the bodies:
    // nothing updates it; with 0x2 or 0x8 it gathers the instances of its
    // room and the touched neighbours and collides with them each sample,
    // as a static side (an impact can give it a body, AttachEntity, and
    // break props loose). Its own test against the city (0x2) pits two
    // static colliders against each other, which moves nothing, and is left
    // out. Without a room it is refused (DeclareMover's "not in a room").
    void declareInstance(Instance* instance, int type, unsigned flags);
    // dgPhysManager::IgnoreMover: the body takes no further part in this
    // frame (the props and traffic owners call it when they detach a body).
    void ignoreMover(const Body* body);
    // Whether the body takes part in the current frame.
    bool isActive(const Body* body) const;

    // One simulation sample.
    void step(float dt);
    // Deterministic fixed-step driver: whole samples of `sampleStep`, the
    // remainder carried to the next call. Returns the number of samples.
    int advanceFixed(float frameDelta, float sampleStep = kFixedSampleStep, int maxSamples = 32);
    // The original scheme: n = min(ceil((delta - 0.001) / step), max)
    // samples of delta / n each (frame-rate dependent).
    int advanceOversampled(float frameDelta, float sampleStep = kOversampleStep,
                           int maxSamples = kOversampleMaxSamples);
    // The share of a sample advanceFixed has not run yet: what is drawn
    // between two samples blends the last two states by it
    // (game::StepHistory).
    float interpolationAlpha(float sampleStep = kFixedSampleStep) const { return m_accumulator / sampleStep; }
    // The time (s) advanceFixed(frameDelta) will leave unstepped: how far the
    // simulation will be behind the frame once it has run.
    float remainderAfter(float frameDelta, float sampleStep = kFixedSampleStep, int maxSamples = 32) const;
    float remainder() const { return m_accumulator; }

    double time() const { return m_time; }

    // OpenMM2 presentation: called at the start of every sample, before
    // anything moves, so that the drawing can keep each body's state from
    // before the sample (game::StepHistory). It must not change the world.
    void setStepObserver(std::function<void()> observer) { m_stepObserver = std::move(observer); }
    // OpenMM2 (network races): `before` runs at the start of every sample,
    // after the step observer and before anything moves (the players' cars
    // take their inputs for that sample), `after` at its end (a client keeps
    // its car's state). Unlike the observer they may change the bodies.
    void setSampleHooks(std::function<void()> before, std::function<void()> after) {
        m_beforeSample = std::move(before);
        m_afterSample = std::move(after);
    }

    // OpenMM2 (network prediction): one sample of `bodies` alone (a car and
    // its trailer): their controllers, their integration and their
    // collisions with the city, with the instances around them and with the
    // world's other movers as they stand now. Nothing else moves or changes:
    // what they hit holds still for them (it takes no impulse or push, is
    // not knocked loose and is not marked as hit by the player) and meets
    // them at its own velocity. A network client runs its own car's samples
    // again with it once the host's state has corrected an earlier one.
    void replaySample(std::span<Body* const> bodies, float dt);
    // OpenMM2: the samples replaySample runs from here on are one replay
    // (the props the replayed car has pushed move on through it, until the
    // next replay or sample; see collideHeld).
    void beginReplay();
    // Inside replaySample (the level's sources may show what a replay meets).
    bool replaying() const { return m_replaying; }

    bool probe(const Vec3& a, const Vec3& b, RayHit& hit) const override;
    // dgPhysManager::Collide(segment, mask 0x20) as vehWheel::ComputeDwtdw
    // calls it, with lvlSDL::CollideProbe for the city: the cached polygon
    // of the last probe when the segment still crosses it, else the nearest
    // hit among the level's collision polygons of the segment's start room,
    // end room and up to 10 instance rooms across the start room's perimeter
    // (sdlPage16::CollideSegment, phPolygon::TestSegmentUndirected); then
    // the wheel-collidable instances of the start and end rooms (and of the
    // room a warp room links to) that are nearer (dgPhysManager::
    // CollideProbe, the bound's TestProbe). The hit's material is the World
    // table's entry of the same name. Without a level: probe().
    bool wheelProbe(const Vec3& a, const Vec3& b, RayHit& hit, const Instance* self,
                    ProbeCache* cache) const override;
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
        Body* body = nullptr;         // null: an instance without a body (declareInstance)
        Instance* instance = nullptr; // the mover's instance (the body itself for a body)
        bool transient = false;       // declareInstance: this frame only
        unsigned flags = 0;           // declareInstance's DeclareMover flags
        bool fresh = false;   // NewMover's 0x100: no update or collision until the sample ends
        bool removed = false; // removed during a step (dropped when it ends)
        bool active = true;   // in dgPhysManager's table this frame (declared, within 32, not culled)
        std::vector<Instance*> collidables;
    };
    bool live(const Mover& m) const { return !m.removed; }
    // Taking part this frame; `updating` also has the update flag.
    bool running(const Mover& m) const { return !m.removed && m.active; }
    bool updating(const Mover& m) const { return running(m) && !m.fresh && m.body && m.body->updates; }
    // The mover's DeclareMover flags.
    bool collidesTerrain(const Mover& m) const { return m.body ? m.body->collideTerrain : (m.flags & 0x2) != 0; }
    bool collidesInstances(const Mover& m) const {
        return m.body ? m.body->collideInstances : (m.flags & 0x8) != 0;
    }
    bool collidesMovers(const Mover& m) const { return m.body ? m.body->collideMovers : (m.flags & 0x10) != 0; }

    // The frame's mover bookkeeping (dgPhysManager::ResetTable clears the
    // table, DeclareMover fills it).
    void beginFrame();
    void gatherCollidables(Mover& mover);
    bool trivialCollide(const Instance& a, const Instance& b) const;
    // lvlSDL::CollideProbe: the level part of wheelProbe.
    bool collideLevelProbe(const Segment& seg, Intersection& hit, ProbeCache& cache,
                           const Material*& material) const;
    // sdlPage16::CollideSegment of one room.
    bool collideRoomSegment(int room, const Vec3& centre, float radius, const Segment& seg, Intersection& hit,
                            ProbeCache& cache, const Material*& material) const;
    // dgPhysManager::CollideProbe: the segment against one instance's bound
    // (in its frame), kept when no farther than hit.t.
    bool collideProbe(const Segment& seg, Instance& inst, Intersection& hit, const Material*& material) const;
    void collideTerrain(Body& body);
    bool collideInstances(Instance& a, Instance& b);
    // replaySample's collision of a body with something that holds still.
    bool collideHeld(Body& a, Instance& b);

    mutable std::uint32_t m_randomSeed = 1;
    MaterialTable m_materials;
    PolygonSoup m_static;
    const Level* m_level = nullptr;
    std::vector<Mover> m_movers;
    std::vector<Mover> m_pendingInstances; // declareInstance's, for the next frame
    bool m_stepping = false;
    float m_accumulator = 0;
    double m_time = 0;
    std::function<void()> m_stepObserver;
    std::function<void()> m_beforeSample, m_afterSample;
    Stats m_stats;

    // dgPhysManager's static buffers and helper colliders: the level's
    // collider, the temporary colliders of static instances, the
    // intersection and impact tables.
    LevelBound m_levelBound;
    Mat34 m_identity;
    Collider m_levelCollider;
    Collider m_tempA, m_tempB;
    InertialCS m_heldIcs; // replaySample: what a held body or prop would have (taken by nothing)
    std::vector<const Instance*> m_replayYielded; // replaySample: light bodies the replay has hit
    struct ReplayBody {
        const Instance* instance = nullptr;
        InertialCS ics; // the body the prop takes in the replay, pushed by its hits
    };
    std::vector<ReplayBody> m_replayBodies;
    bool m_replaying = false;
    Mat34 m_tempMatrixA, m_tempMatrixB;
    std::vector<Intersection> m_isectsA, m_isectsB;
    std::vector<Impact> m_impacts;
    std::vector<Instance*> m_roomScratch;
    // dgPhysManager's active rooms (+0x10, at most 20) for this frame.
    std::vector<int> m_activeRooms;
    // The polygons of the room a wheel probe is testing.
    mutable LevelBound m_probeBound;
    mutable std::vector<Instance*> m_probeScratch;
};

} // namespace mm2::phys
