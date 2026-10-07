#pragma once

// MM2's impacts (phImpact / phImpactBase) and their resolution
// (phImpact::CalcCollision, phContactMgr::CalcImpact, dgImpact), ported from
// the code of midtown2.exe build 3393. See docs/physics.md, "Collision".

#include "core/Math.h"

namespace mm2::phys {

class Collider;
class Bound;
class InertialCS;
struct Material;

// phImpact (0x44 bytes): one contact between colliders A and B.
struct Impact {
    // which features touch. VertexA: a vertex (or sphere) of A on a
    // face of B; VertexB the other way round; EdgeEdge two edges.
    enum Kind : int { EdgeEdge = 0, VertexA = 1, VertexB = 2 };
    int kind = EdgeEdge;
    int elementA = 0;    // vertex, edge or polygon index in A's bound (some searches store B's here)
    int elementB = 0;
    int componentA = -1; // material index for terrain and level bounds
    int componentB = -1;
    Vec3 position;
    Vec3 normal;         // unit, pointing from B towards A
    float depth = 0;     // penetration along the normal
    float penetration = 0; // the smaller bound penetration (0 in a race)
    float friction = 0;    // FindFrictionAndElasticity
    float elasticity = 0;
    Collider* colliderA = nullptr;
    Collider* colliderB = nullptr;

    // phImpactBase::SwapColliders: exchanges the A and B sides (VertexA and
    // VertexB swap, the normal flips).
    void swapColliders();
    // phImpactBase::Reset.
    void reset();
    // phImpactBase::StartMakingNewImpact / FinishMakingNewImpact /
    // MakeNewImpact, as the sphere and hotdog tests fill impacts.
    void startMakingNewImpact(float depth, const Vec3& normal, const Vec3& position, Collider* a, Collider* b,
                              const Mat34* m, int elementA, int componentA, int componentB);
    void finishMakingNewImpact(int sphereResult, int elementB, const Bound& boundA, const Bound& boundB,
                               int elementA);
    void makeNewImpact(Collider* a, Collider* b, const Vec3& position, const Vec3& normal, float depth,
                       const Bound& boundA, const Bound& boundB, int kind, int elementA, int elementB);

    // phImpact::GetMaterial: the material of `collider`'s bound at
    // `component` (terrain and level bounds index their materials by
    // component, every other bound uses material 0).
    static const Material& materialOf(const Collider& collider, int component);
    // phImpact::FindFrictionAndElasticity: friction = fA * fB, elasticity =
    // min(eA * eB, the elasticity cap).
    void findFrictionAndElasticity();
    // phImpact::GetLocalVelocities: both colliders' velocities at position.
    void localVelocities(Vec3& va, Vec3& vb) const;

    // phImpact::CalcCollision: the impulse that stops the relative motion
    // `relVel` (A's velocity minus B's) at the contact through both inverse
    // mass matrices, limited by Coulomb friction and multiplied by
    // (1 + elasticity), then scaled by `weight` (1 / the pair's impact count);
    // and the pushes that remove the depth, shared by how far each body
    // moves under a unit push, never pushing a body downwards. Bodies
    // separating faster than 0.01 m/s get no impulse.
    void calcCollision(const Vec3& relVel, float weight, Vec3& pushA, Vec3& pushB, Vec3& impulse);

    // phImpactBase::CullImpactList: drops impacts whose normal is nearly
    // perpendicular to `dir` (dot^2 <= |dir|^2 / 100), keeping at least one.
    static void cullImpactList(Impact* list, int& count, const Vec3& dir);
    // phImpactBase::AddImpactSpherePlaneTest: before list[count] is added,
    // drops earlier impacts of the same element that it covers; false when
    // an earlier impact covers it (it is not to be added).
    static bool addImpactSpherePlaneTest(Impact* list, int& count, const Vec3& center, float radius);
    // phImpactBase::AddImpactShaftPlaneTest: the same for a hotdog's shaft
    // along `axis` through `point`.
    static bool addImpactShaftPlaneTest(Impact* list, int& count, const Vec3& point, float radius,
                                        const Vec3& axis);
    // phImpactBase::ImpactIsInList: the index of the impact of `kind`
    // between elements a and b, or -1. (Declared bool in MM2, which returns
    // the index in eax; callers test its low byte.)
    static int impactIsInList(int a, int b, int kind, const Impact* list, int count);
};

// The elasticity cap of FindFrictionAndElasticity (a global): 1, reset
// by mmGame::Reset; the "/blubber" chat cheat sets 4.
inline constexpr float kElasticityCap = 1.0f;

// phImpact::CalcCollisionNoFriction: the impulse magnitude along `normal`
// that stops a closing speed `closing` (< 0) at `position` of ics; 0 when
// separating.
float calcCollisionNoFriction(const InertialCS& ics, const Vec3& normal, float closing, const Vec3& position);

// phContactMgr::CalcImpact (contacts disabled, as in a race): resolves one
// impact, applying the impulse and pushes to both colliders (which also
// fires their impact callbacks).
void calcImpact(Impact& impact, float weight);

// dgImpact::CalcImpact: resolves an impact against an unhit banger (B). The
// banger absorbs impulses up to sqrt(impulseLimit2); above that it breaks
// loose (returns true) and both bodies take what the breaking costs.
bool calcBangerImpact(Impact& impact, float weight, float impulseLimit2);

} // namespace mm2::phys
