#pragma once

#include "core/Math.h"

#include <cfloat>

namespace mm2::phys {

class Joint3Dof;

// Rigid body: a port of the Angel engine's asInertialCS (Midtown Madness 1,
// Open1560 game.asm). MM2's phInertialCS keeps the same role.
//
// The frame `matrix` is centred on the centre of gravity: rows m0..m2 are the
// body axes (right, up, back), m3 the CG in world space (asLinearCS::Matrix;
// the body is a global CS, so World == Matrix).
//
// Forces, torques, impulses and pushes accumulate during a sample and are
// integrated by update(): FinishForces (sleep test, gravity) then
// FinishUpdate (momentum-based semi-implicit Euler). Member names follow the
// original.
class InertialCS {
public:
    enum State : int { Off = 0, Awake = 1, Asleep = 2 }; // ICS_STATE_*

    // ICS_CONSTRAIN_* bits.
    static constexpr int kConstrainTX = 0x1, kConstrainTY = 0x2, kConstrainTZ = 0x4;
    static constexpr int kConstrainRX = 0x8, kConstrainRY = 0x10, kConstrainRZ = 0x20;
    static constexpr int kConstrainAll = 0x3F, kConstrainLink = 0x40, kConstrainZeroDof = 0x400;

    InertialCS();

    // asInertialCS::SetMass: inertia of a solid box with full extents
    // (sizeX, sizeY, sizeZ): I = m/12 * (y^2+z^2, x^2+z^2, x^2+y^2).
    void setMass(float sizeX, float sizeY, float sizeZ, float mass);

    // asInertialCS::Zero: identity matrix, all motion and accumulators cleared.
    void zero();

    // asInertialCS::Update (one physics sample). `dt` is the sample length
    // (asSimulation seconds_), `invDt` its reciprocal (inv_seconds_).
    void update(float dt, float invDt);
    void finishForces(float dt, float invDt);
    void finishUpdate(float dt);
    // asInertialCS::MoveICS: applies the pending push immediately.
    void moveICS();
    // asInertialCS::DoConstrain (simplified: clears constrained components).
    void doConstrain();

    // Accumulators (world space).
    void applyForce(const Vec3& f);
    void applyForce(const Vec3& f, const Vec3& worldPos);
    void applyTorque(const Vec3& t);
    void applyImpulse(const Vec3& j, const Vec3& worldPos);
    void applyAngImpulse(const Vec3& j);
    // asInertialCS::ApplyPush: positional correction. Pushes in a direction
    // already covered by the pending push only add the missing part.
    void applyPush(const Vec3& push);

    // asInertialCS::GetVelocity: velocity of a world point (or of the CG).
    Vec3 getVelocity(const Vec3* worldPos = nullptr) const;

    // asInertialCS::CalcCMatrix: the 3x3 "collision matrix" C at a world
    // point: an impulse j applied there changes that point's velocity by
    // j * C. C = InvMass * I + (R X)^T diag(InvInertia) (R X), X = [r]x.
    void calcCMatrix(Mat34& out, const Vec3& worldPos) const;
    // asInertialCS::GetCMatrix: as calcCMatrix, but through the joint when
    // the body is linked to another one.
    void getCMatrix(Mat34& out, const Vec3& worldPos) const;

    // --- OpenMM2 additions for the contact solver (not in the original) ---
    // Applies an impulse to momentum immediately and refreshes velocities.
    void applyImpulseNow(const Vec3& j, const Vec3& worldPos);
    Vec3 invInertiaWorld(const Vec3& v) const;
    // 1 / (1/m + dir . ((I^-1 (r x dir)) x r)) at worldPos.
    float effectiveMass(const Vec3& dir, const Vec3& worldPos) const;
    // Places the body (keeps mass), clearing motion.
    void place(const Mat34& m);
    const Vec3& position() const { return matrix.m3; }

    // --- State (asInertialCS members) ---
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
    Vec3 frameVelocity; // velocity including this sample's pushes
    Vec3 linearForce;
    Vec3 angularTorque;
    Vec3 linearImpulse;
    Vec3 angularImpulse;
    Vec3 linearPush;
    Vec3 turnForce; // rotational push (axis * angle)
    Vec3 framePush;
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
    float maxAngVelocity = 3.14159265f * 10.0f;
    bool limitAngVelocity = false;
    Joint3Dof* joint = nullptr; // asInertialCS::Joint (set by Joint3Dof::initJoint3Dof)

private:
    void clearAccumulators();
    void refreshVelocities();
};

} // namespace mm2::phys
