#pragma once

#include "core/Math.h"
#include "phys/Constants.h"

#include <cfloat>

namespace mm2::phys {

// Rigid body: MM2's phInertialCS (ported from the code of midtown2.exe build
// 3393, see docs/physics.md).
//
// The frame `matrix` is centred on the centre of gravity: rows m0..m2 are the
// body axes (right, up, back), m3 the CG in world space.
//
// Forces, torques, impulses and pushes accumulate during a sample and are
// integrated by update(): the entity's weight (dgPhysEntity::Update), then
// phInertialCS::Update. Bodies that registered contact stiffness this sample
// (applyContactForce: the car wheels' suspension) integrate implicitly
// against it; the others explicitly. A body is put to sleep by its phSleep
// (Sleep), which makes it inactive: it keeps its push bookkeeping but does
// not integrate.
class InertialCS {
public:
    // Off and Awake: active (phInertialCS +0x8 set); Asleep: inactive, set by
    // phSleep::SendToSleep. (MM1's asInertialCS had a sleep test of its own
    // in state Awake; MM2 moved it to phSleep, so Awake and Off are the same
    // here.)
    enum State : int { Off = 0, Awake = 1, Asleep = 2 };

    // phInertialCS::phInertialCS: Init(1, 1, 1, 1), speed limit 500, the
    // angular velocity limit 5 rad/s per body axis, Zero.
    InertialCS();

    // phInertialCS::Init: mass and principal moments of inertia, and their
    // reciprocals (FLT_MAX for a value <= 0).
    void init(float mass, float ix, float iy, float iz);
    // phInertialCS::InitBoxMass: inertia of a solid box with full extents
    // (sizeX, sizeY, sizeZ): I = (y^2+z^2, x^2+z^2, x^2+y^2) * m * (1/12).
    void setMass(float sizeX, float sizeY, float sizeZ, float mass);

    // phInertialCS::Zero: identity matrix, Freeze, and no last push.
    void zero();
    // phInertialCS::Freeze: no motion, then ZeroForces.
    void freeze();
    // phInertialCS::ZeroForces: every accumulator cleared; the pending and
    // this sample's pushes become the last push.
    void zeroForces();

    // One physics sample of length `dt` (datTimeManager::Seconds): the
    // weight (finishForces), then phInertialCS::Update (finishUpdate).
    void update(float dt, float invDt);
    void finishForces(float dt, float invDt);
    // phInertialCS::Update(): the contact accumulators join this sample's
    // forces, Update(dt) integrates (when active), then this sample's pushes
    // become lastPush.
    void finishUpdate(float dt);
    // phInertialCS::MoveICS: applies the pending push immediately.
    void moveICS();

    // Accumulators (world space). These stand in for the original's inline
    // additions to the phInertialCS fields.
    void applyForce(const Vec3& f);
    void applyForce(const Vec3& f, const Vec3& worldPos);
    void applyTorque(const Vec3& t);
    void applyImpulse(const Vec3& j, const Vec3& worldPos);
    void applyAngImpulse(const Vec3& j);
    // phInertialCS::CalcNetPush: positional correction. Pushes in a direction
    // already covered by the pending push only add the missing part.
    void applyPush(const Vec3& push);
    // phInertialCS::CalcNetTurn: the same for rotation (axis * angle).
    void applyTurn(const Vec3& turn);
    // phInertialCS::ApplyContactForce: a force at a world point plus its
    // stiffness K (dF = -K dv, 3x3 in m0..m2). Makes this sample's
    // integration implicit in the summed stiffness.
    void applyContactForce(const Vec3& f, const Vec3& worldPos, const Mat34& k);

    // phInertialCS::GetLocalVelocity: velocity of a world point (or of the CG).
    Vec3 getVelocity(const Vec3* worldPos = nullptr) const;
    // phInertialCS::GetLocalFilteredVelocity2: the point velocity with the
    // part along last sample's push removed (or, moving into the push faster
    // than it, the push rate added).
    Vec3 filteredVelocity(const Vec3& worldPos, float invDt) const;
    // phInertialCS::GetCMFilteredVelocity: the velocity of the CG plus the
    // last push spread over a sample.
    Vec3 cmFilteredVelocity(float invDt) const;
    // phInertialCS::GetLocalAcceleration: a point's acceleration under this
    // sample's accumulated force and torque (impulses not included):
    // F / m + w x (w x r) + (I^-1 (T - w x L)) x r.
    Vec3 localAcceleration(const Vec3& worldPos) const;
    // phInertialCS::GetForce / phInertialCS::GetTorque: the accumulated force (torque)
    // plus the accumulated impulse spread over the sample (impulse * invDt).
    // The ApplyContactForce accumulators are not included.
    Vec3 getForce(float invDt) const;
    Vec3 getTorque(float invDt) const;
    // phInertialCS::GetInertiaMatrix: world inertia tensor R^T diag(I) R.
    Mat34 worldInertia() const;

    // phInertialCS::GetInvMassMatrix: the inverse mass matrix C at a world
    // point: an impulse j applied there changes that point's velocity by
    // j * C. C = InvMass * I + (R X)^T diag(InvInertia) (R X), X = [r]x.
    void calcCMatrix(Mat34& out, const Vec3& worldPos) const;

    // Places the body (keeps mass), clearing motion (Zero, then the matrix).
    void place(const Mat34& m);
    const Vec3& position() const { return matrix.m3; }
    // The per-axis angular velocity limit (phInertialCS +0x30).
    void setMaxAngVelocity(float w) { maxAngVelocity = {w, w, w}; }

    // --- State (phInertialCS members) ---
    Mat34 matrix;
    Vec3 size; // the box InitBoxMass was given (OpenMM2 bookkeeping)
    float mass = 1.0f;
    float invMass = 1.0f;
    Vec3 inertia{1, 1, 1};
    Vec3 invInertia{1, 1, 1};
    Vec3 linearMomentum;
    Vec3 angularMomentum;
    Vec3 linearVelocity;
    Vec3 angularVelocity;
    Vec3 linearForce;
    Vec3 angularTorque;
    Vec3 contactForce;  // ApplyContactForce force, added to linearForce at update
    Vec3 contactTorque; // its torque (vehEngine's reaction torque also lands here)
    Vec3 linearImpulse;
    Vec3 angularImpulse;
    Vec3 linearPush;
    Vec3 turnForce; // rotational push (axis * angle)
    Vec3 framePush; // pushes applied since the last update
    Vec3 lastPush;  // pushes applied during the previous sample
    // Implicit contact stiffness (phInertialCS +0x11c, +0x120/+0x150/+0x180):
    // sum K, sum X K and sum of X K mapped through r x . (X = [r]x).
    bool implicitContact = false;
    Mat34 contactK;
    Mat34 contactXK;
    Mat34 contactXKX;
    int state = Off;
    float maxSpeed = 500.0f;                // phInertialCS +0x2c
    Vec3 maxAngVelocity{5.0f, 5.0f, 5.0f};  // phInertialCS +0x30, per body axis

    // --- OpenMM2 glue (not phInertialCS members) ---
    // dgPhysEntity::Update's weight: MM2 adds Mass * -19.6 to the force's y
    // for every physics entity (Constants.h kGravity). Tests may change it.
    Vec3 gravity{0.0f, -kGravity, 0.0f};
    // The velocity including this sample's pushes, for the game layer
    // (MM1's FrameVelocity).
    Vec3 frameVelocity;
    // MM2 always applies maxAngVelocity; kept as a switch for tests.
    bool limitAngVelocity = true;
    // MM1 asInertialCS impact parameters, read by nothing (the bounds'
    // materials decide).
    float elasticity = 0.0f;
    float friction = 0.0f;

private:
    void integrateExplicit(float dt);
    void integrateImplicit(float dt);
};

} // namespace mm2::phys
