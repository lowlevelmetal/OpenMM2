#pragma once

#include "phys/Bound.h"
#include "phys/Collide.h"
#include "phys/Constants.h"
#include "phys/InertialCS.h"
#include "phys/Joint3Dof.h"
#include "phys/Material.h"

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

struct Impact {
    Body* other = nullptr; // nullptr for static geometry
    int material = 0;      // static surface material
    Vec3 point;
    Vec3 normal;       // points towards the body receiving the report
    float impulse = 0; // magnitude of the normal impulse (N s)
    float speed = 0;   // closing speed along the normal before impact (m/s)
};

// Per-sample hooks, called in the order of the Angel node tree for a car:
//   beforeIntegrate  (mmCarSim::Update: inputs; vehEngine, vehTransmission)
//   InertialCS::update (integrates last sample's forces; linked bodies are
//                     integrated by their Joint3Dof::update right after)
//   afterIntegrate   (LCS children: aero, drivetrains -> wheels, which
//                     accumulate forces for the next sample; mmStuck)
//   collisions, then onImpact / afterCollisions.
class BodyController {
public:
    virtual ~BodyController() = default;
    virtual void beforeIntegrate(Body&, float /*dt*/, const World&) {}
    virtual void afterIntegrate(Body&, float dt, const World& world) = 0;
    virtual void onImpact(Body&, const Impact&) {}
    virtual void afterCollisions(Body&, float /*dt*/, const World&) {}
};

// Collision shape, in body space relative to the centre of gravity.
struct Shape {
    enum class Kind { Box, Sphere } kind = Kind::Box;
    Vec3 offset;
    Vec3 half{1, 1, 1};
    float radius = 1.0f;
};

class Body {
public:
    InertialCS ics;
    Shape shape;
    // Impact parameters (vehCarSim BoundElasticity / BoundFriction, copied
    // into ICS.Elasticity / ICS.Friction by RestoreImpactParams).
    float elasticity() const { return ics.elasticity; }
    float friction() const { return ics.friction; }
    BodyController* controller = nullptr;
    bool collideStatic = true;
    bool collideBodies = true;

    Obb obb() const;
    Aabb aabb() const;

private:
    friend class World;
    Vec3 m_prevPosition;
};

class World final : public GroundQuery {
public:
    World() = default;
    explicit World(MaterialTable materials) : m_materials(std::move(materials)) {}

    void setMaterials(MaterialTable materials) { m_materials = std::move(materials); }
    const MaterialTable& materials() const { return m_materials; }
    // Static level geometry (finalize() must have been called).
    void setStatic(PolygonSoup soup) { m_static = std::move(soup); }
    const PolygonSoup& staticGeometry() const { return m_static; }

    // Bodies are not owned. Update order is insertion order (deterministic).
    // add() sets the body's gravity to (0, -kGravity, 0) unless it was
    // changed from the asInertialCS default.
    void add(Body* body);
    void remove(Body* body);
    // Joints are not owned. A linked body skips its own integration
    // (InertialCS::update); the joint integrates both of its bodies right
    // after the free bodies, in insertion order. Bodies sharing an unbroken
    // joint do not collide with each other.
    void add(Joint3Dof* joint);
    void remove(Joint3Dof* joint);

    // One simulation sample.
    void step(float dt);
    // Deterministic fixed-step driver: whole samples of `sampleStep`, the
    // remainder carried to the next call. Returns the number of samples.
    int advanceFixed(float frameDelta, float sampleStep = kFixedSampleStep, int maxSamples = 32);
    // The original Angel scheme: n = min(floor(delta / step) + 1, max)
    // samples of delta / n each (frame-rate dependent).
    int advanceOversampled(float frameDelta, float sampleStep = kOversampleStep,
                           int maxSamples = kOversampleMaxSamples);
    float interpolationAlpha(float sampleStep = kFixedSampleStep) const { return m_accumulator / sampleStep; }

    double time() const { return m_time; }

    bool probe(const Vec3& a, const Vec3& b, RayHit& hit) const override;
    const Material& material(int index) const override { return m_materials[index]; }

    int solverIterations = 8;
    // Contacts closing slower than this (m/s) do not bounce.
    float restitutionThreshold = 1.0f;
    // Fraction of penetration corrected per sample, and allowed slop (m).
    float pushFactor = 0.8f;
    float pushSlop = 0.005f;

private:
    struct ContactPoint {
        Body* a;
        Body* b; // nullptr = static
        Contact c;
        float bounce;
        float friction;
        float jn = 0;
        float jt1 = 0, jt2 = 0;
        Vec3 t1, t2;
        float massN = 0, massT1 = 0, massT2 = 0;
        float approach = 0;
    };

    void collide(std::vector<ContactPoint>& contacts);
    void solve(std::vector<ContactPoint>& contacts);
    void pushApart(std::vector<ContactPoint>& contacts);
    void report(std::vector<ContactPoint>& contacts);
    void continuous(Body& body);

    MaterialTable m_materials;
    PolygonSoup m_static;
    std::vector<Body*> m_bodies;
    std::vector<Joint3Dof*> m_joints;
    std::vector<ContactPoint> m_contacts;
    std::vector<std::uint32_t> m_query;
    float m_accumulator = 0;
    double m_time = 0;
};

} // namespace mm2::phys
