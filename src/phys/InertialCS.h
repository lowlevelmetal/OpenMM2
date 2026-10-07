#pragma once

#include "core/Math.h"

#include <cfloat>

namespace mm2::phys {

// Rigid body: MM2's phInertialCS (verified against the build 3393 code,
// see docs/physics.md). The sleep test and constraints are kept from MM1's
// asInertialCS (Open1560 game.asm), which phInertialCS replaced.
//
// The frame `matrix` is centred on the centre of gravity: rows m0..m2 are the
// body axes (right, up, back), m3 the CG in world space.
//
// Forces, torques, impulses and pushes accumulate during a sample and are
// integrated by update(): finishForces (sleep test, gravity) then
// finishUpdate (phInertialCS::Update). Bodies that registered contact
// stiffness this sample (applyContactForce: the car wheels' suspension)
// integrate implicitly against it, as phInertialCS::Update does; that
// includes bodies linked by a TrailerJoint, whose force joins the others.
class InertialCS {
public:
    enum State : int { Off = 0, Awake = 1, Asleep = 2 }; // ICS_STATE_*

    // ICS_CONSTRAIN_* bits.
    static constexpr int kConstrainTX = 0x1, kConstrainTY = 0x2, kConstrainTZ = 0x4;
    static constexpr int kConstrainRX = 0x8, kConstrainRY = 0x10, kConstrainRZ = 0x20;
    static constexpr int kConstrainAll = 0x3F, kConstrainZeroDof = 0x400;

    InertialCS();

    // phInertialCS::InitBoxMass: inertia of a solid box with full extents
    // (sizeX, sizeY, sizeZ): I = (y^2+z^2, x^2+z^2, x^2+y^2) * m * (1/12).
    void setMass(float sizeX, float sizeY, float sizeZ, float mass);

    // phInertialCS::Zero: identity matrix, all motion and accumulators cleared.
    void zero();

    // One physics sample of length `dt` (datTimeManager::Seconds).
    void update(float dt, float invDt);
    void finishForces(float dt, float invDt);
    // phInertialCS::Update(): contact accumulators folded in, Update(dt),
    // then this sample's pushes become lastPush.
    void finishUpdate(float dt);
    // phInertialCS::MoveICS: applies the pending push immediately.
    void moveICS();
    // asInertialCS::DoConstrain (simplified: clears constrained components).
    void doConstrain();

    // Accumulators (world space).
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
    // stiffness K (dF = -K dx, 3x3 in m0..m2). Makes this sample's
    // integration implicit in the summed stiffness.
    void applyContactForce(const Vec3& f, const Vec3& worldPos, const Mat34& k);

    // phInertialCS::GetLocalVelocity: velocity of a world point (or of the CG).
    Vec3 getVelocity(const Vec3* worldPos = nullptr) const;
    // phInertialCS::GetLocalFilteredVelocity2: the point velocity with the
    // part along last sample's push removed (or, moving into the push faster
    // than it, the push rate added).
    Vec3 filteredVelocity(const Vec3& worldPos, float invDt) const;
    // phInertialCS::GetLocalAcceleration: a point's acceleration under this
    // sample's accumulated force and torque (impulses not included):
    // F / m + w x (w x r) + (I^-1 (T - w x L)) x r.
    Vec3 localAcceleration(const Vec3& worldPos) const;
    // phInertialCS::GetForce / GetTorque: the accumulated force (torque)
    // plus the accumulated impulse spread over the sample (impulse * invDt).
    // The ApplyContactForce accumulators are not included.
    Vec3 getForce(float invDt) const;
    Vec3 getTorque(float invDt) const;
    // phInertialCS::GetInertiaMatrix: world inertia tensor R^T diag(I) R.
    Mat34 worldInertia() const;

    // phInertialCS::GetInvMassMatrix (MM1: asInertialCS::CalcCMatrix): the
    // inverse mass matrix C at a world point: an impulse j applied there
    // changes that point's velocity by j * C.
    // C = InvMass * I + (R X)^T diag(InvInertia) (R X), X = [r]x.
    void calcCMatrix(Mat34& out, const Vec3& worldPos) const;

    // --- OpenMM2 additions for the contact solver (not in the original) ---
    // Applies an impulse to momentum immediately and refreshes velocities.
    void applyImpulseNow(const Vec3& j, const Vec3& worldPos);
    Vec3 invInertiaWorld(const Vec3& v) const;
    // 1 / (1/m + dir . ((I^-1 (r x dir)) x r)) at worldPos.
    float effectiveMass(const Vec3& dir, const Vec3& worldPos) const;
    // Places the body (keeps mass), clearing motion.
    void place(const Mat34& m);
    const Vec3& position() const { return matrix.m3; }
    // Per-axis angular velocity limit (phInertialCS +0x30), applied when
    // limitAngVelocity is set.
    void setMaxAngVelocity(float w) { maxAngVelocity = {w, w, w}; }

    // --- State (phInertialCS members) ---
    Mat34 matrix;
    Vec3 size;
    float mass = 1.0f;
    float invMass = 1.0f;
    Vec3 inertia{1, 1, 1};
    Vec3 invInertia{1, 1, 1};
    Vec3 linearMomentum;
    Vec3 angularMomentum;
    Vec3 linearVelocity;
    Vec3 angularVelocity;
    Vec3 frameVelocity; // velocity including this sample's pushes (MM1 FrameVelocity)
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
    // Implicit contact stiffness (phInertialCS +0x120/+0x150/+0x180):
    // sum K, sum X K and sum of X K mapped through r x . (X = [r]x).
    bool implicitContact = false;
    Mat34 contactK;
    Mat34 contactXK;
    Mat34 contactXKX;
    int numImpulses = 0;
    float elasticity = 0.0f;
    float friction = 0.0f;
    Vec3 gravity{0.0f, -10.0f, 0.0f};
    int constraints = 0;
    int state = Off;
    float vel2 = 0.1f;
    float angVel2 = 0.1f;
    float time = 1.0f;
    float counter = 0.0f;
    float forceLimit2 = FLT_MAX;
    float impulseLimit2 = 0.0f;
    float maxSpeed = 500.0f; // phInertialCS +0x2c
    Vec3 maxAngVelocity{3.14159265f * 10.0f, 3.14159265f * 10.0f, 3.14159265f * 10.0f};
    bool limitAngVelocity = false;

private:
    void clearAccumulators();
    void refreshVelocities();
    void integrateExplicit(float dt);
    void integrateImplicit(float dt);
};

} // namespace mm2::phys
