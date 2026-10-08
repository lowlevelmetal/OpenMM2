#pragma once

// MM2's colliders (phColliderBase / phCollider / phColliderJointed): a bound
// placed in the world by a matrix, optionally driven by a rigid body, with the
// per-sample bookkeeping the collision routines read. Ported from the code of
// midtown2.exe build 3393. See docs/physics.md, "Collision".

#include "core/Math.h"

#include <cfloat>

namespace mm2::phys {

class Bound;
class Body;
class InertialCS;
class Joint;
struct Impact;
class Collider;

// The receiver of a collider's impacts (phColliderBase's impact datCallback,
// called with the impulse the collider took and the impact).
class ImpactHandler {
public:
    virtual ~ImpactHandler() = default;
    virtual void onImpact(Collider& self, const Impact& impact, const Vec3& impulse) = 0;
};

class Collider {
public:
    // phCollider::Init variants: a bound at `matrix` driven by `ics` (null
    // for kinematic colliders), then phColliderBase::Reset.
    void init(const Bound* b, const Mat34* m, InertialCS* inertia);
    // phCollider::Init(bound, matrix): a collider without a body (the level's
    // and the static instances' temporary ones), which counts as not moving
    // at all (maxMoved 0, barelyMoved set).
    void initStatic(const Bound* b, const Mat34* m);
    // phColliderBase::Reset / phCollider::Reset: last matrix = current.
    void reset();
    // phColliderBase::UpdateMtx (dgPhysManager, after each sample's
    // collisions): applies the pending push (MoveICS), remembers the matrix
    // and which collider pushed hardest.
    void updateMtx();
    // phColliderBase::CalcMaxMoved: an upper bound on how far any point of
    // the bound moves in a sample; sets barelyMoved. MM2 calls it only when a
    // traffic car leaves its rail (aiVehicleActive::Attach).
    void calcMaxMoved(float dt);
    // phColliderBase::CopyLastMatrix: the matrix at the start of the sample,
    // advanced by the ICS's last push when `pusher` pushed hardest last time.
    Mat34 copyLastMatrix(const void* pusher) const;

    // phColliderBase::GetLocalVelocity: the ICS's velocity at a world point
    // (0 without an ICS).
    Vec3 localVelocity(const Vec3& position) const;
    // phColliderBase::GetInvMassMatrix / phColliderJointed::GetInvMassMatrix:
    // the inverse mass matrix at a world point, through the joint while it
    // holds; zero without an ICS.
    void invMassMatrix(const Vec3& position, Mat34& out) const;
    // phColliderBase::Impact (and phColliderJointed::Impact, which forwards
    // to it): adds the impulse (and its angular part about the ICS position)
    // to the ICS accumulators, folds the push into the pending push, records
    // the hardest push and calls the handler. MM2's bound-callback branch
    // (phColliderBase::CallBoundCallback) is never taken: nothing calls
    // phColliderBase::SetBoundCB.
    void impact(const Impact& impact, const Vec3& impulse, const Vec3& push);

    // The identity of the collider for the push bookkeeping (MM2 uses the
    // collider's InstanceData pointer; the temporary colliders of static
    // instances share theirs).
    const void* key() const { return m_key ? m_key : this; }
    void setKey(const void* key) { m_key = key; }

    const Bound* bound = nullptr;
    InertialCS* ics = nullptr;
    const Mat34* matrix = nullptr;   // the bound's world matrix
    Mat34 lastMatrix;                // the matrix at the end of the previous sample
    bool justReset = true;
    bool barelyMoved = false;
    float maxMoved = FLT_MAX;
    float maxPush2 = 0;              // |push|^2 of the hardest push this sample
    const void* maxPusher = nullptr; // key of the collider that gave it
    const void* lastMaxPusher = nullptr; // the same for the previous sample
    // The instance data's collider id: the id AudImpact plays (vehCar::SetColliderID, the
    // banger's AudioId); 0 for the world and cars.
    int id = 0;
    ImpactHandler* handler = nullptr; // phColliderBase::SetImpactCB
    const Joint* joint = nullptr;     // phColliderJointed's joint (phColliderJointed::Attach)
    Body* body = nullptr;             // the simulated body owning the collider (dgPhysEntity), if any
    bool active = true;               // ColliderIsActive without an ICS

private:
    const void* m_key = nullptr;
};

} // namespace mm2::phys
