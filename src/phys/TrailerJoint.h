#pragma once

#include "phys/Joint.h"

namespace mm2::phys {

struct TrailerJointParams;

// dgTrailerJoint (Midtown Madness 2, verified against the build 3393 code):
// the hitch between a tractor (body 1) and its trailer (body 2), a ball
// joint with a restoring torque and limits, tuned by tune/vehicle/
// <car>.dgTrailerJoint.
//
// vehTrailer::Update calls update() once per sample, after both bodies were
// integrated and their wheels applied next sample's forces. The joint
// itself only adds forces and torques for the next sample (each body then
// integrates on its own, implicitly with its wheel contacts):
//   1. Restoring torque (doJointTorque, while the force flag is set). With
//      MM2's identity rest orientations the joint axis is each body's Z
//      axis, so "lean" is the angle between the tractor's and the trailer's
//      length (the hitch angle and the relative pitch together). Inside
//      FreeLean nothing acts; beyond it RestoreForceLean pulls the bodies in
//      line and DampConstLean brakes the relative turning at a constant
//      torque. The DampLinearLean term and the roll torque (relative roll
//      about the trailer's Z axis) never act in MM2 (see docs/physics.md).
//   2. The joint force: (free relative acceleration of the two joint
//      points + a third of their relative velocity per sample) through
//      (C1 + C2)^-1, applied +F to the tractor and -F to the trailer, the
//      linear part turned by half the sample's mean rotation. While the
//      pair turns, MM2 turns it with a matrix that still holds the
//      trailer's inverse mass matrix, which all but cancels the force
//      across the turning axis (mm2ForceRotation).
//   3. Lean limit (doJointLimits): an angular impulse stopping the approach
//      to LeanLimit, with LimitElasticityLean.
//   4. Breaks above ForceLimit * 10000 N (0: never).
//   5. The joint point moves to the middle of the two hitch points; when
//      they are more than FreeRange apart, both bodies are moved half the
//      excess towards each other (positions only, not velocities).
class TrailerJoint final : public Joint {
public:
    // Status bits (the JointStatus field).
    static constexpr int kBroken = 1;
    static constexpr int kForce = 2; // restoring torque and limits active

    // dgTrailerJoint::Init: the defaults, then the tune file's values
    // (`params`), then phJoint::Init at the two hitch offsets (`carHitch`
    // in the tractor's InertialCS space, `trailerHitch` in the trailer's).
    void init(const TrailerJointParams& params, InertialCS* tractor, InertialCS* trailer,
              const Vec3& carHitch, const Vec3& trailerHitch);
    // dgTrailerJoint::Reset: unbroken, the joint point on the tractor's hitch.
    void reset();
    // dgTrailerJoint::SetPosition: the joint point at `pos`, both bodies moved
    // so that their hitch points land on it.
    void setPosition(const Vec3& pos);
    // dgTrailerJoint::SetCosFreeLean: cos(FreeLean), the threshold of the
    // restoring torque.
    void setCosFreeLean();
    // dgTrailerJoint::SetRotate1 / SetRotate2: sets a body's whole matrix.
    void setRotate1(const Mat34& m);
    void setRotate2(const Mat34& m);
    void setFrictionLean(float restore, float dampConst, float dampLinear);
    void setFrictionRoll(float restore, float dampConst, float dampLinear);
    void setLeanLimit(float limit, float elasticity);
    // dgTrailerJoint::SetRollLimit: -limit..limit, or explicit bounds.
    void setRollLimit(float limit, float elasticity);
    void setRollLimit(float negativeLimit, float positiveLimit, float elasticity);
    // dgTrailerJoint::SetRestOrientation: the current relative orientation
    // becomes the rest orientation.
    void setRestOrientation();
    // dgTrailerJoint::SetRestOrientMat: rest orientation of body 2 (body 1:
    // identity), or of both.
    void setRestOrientMat(const Mat34& m);
    void setRestOrientMat(const Mat34& m1, const Mat34& m2);
    void setForceLimit(float limit) { forceLimit = limit; }
    // dgTrailerJoint::SetJointForceFlag: kForce while any friction is set or
    // the lean limit is below pi.
    void setJointForceFlag();
    // dgTrailerJoint::MoveICS: applies both bodies' pending pushes.
    void moveICS();
    // dgTrailerJoint::BreakJoint, dgTrailerJoint::UnbreakJoint,
    // dgTrailerJoint::IsBroken: bit 0 of the joint's flags.
    void breakJoint() { status |= kBroken; }
    void unbreakJoint() { status &= ~kBroken; }
    bool isBroken() const override { return (status & kBroken) != 0; }

    // dgTrailerJoint::Update for one sample of `dt` seconds (the trailer's
    // own step; phJoint's generic Joint::update is not used for trailers).
    void update(float dt, float invDt);

    using Joint::computeInvMassMatrix;
    // dgTrailerJoint::ComputeInvMassMatrix(ics, out, pos): the inverse mass
    // matrix at `pos` of `ics` (one of the two bodies) through the joint:
    // C(pos) - S^T K S, K = (C1 + C2)^-1 at the joint and S the coupling of
    // the joint point and `pos`. phColliderJointed::GetInvMassMatrix uses it
    // for collisions of jointed bodies.
    void computeInvMassMatrix(const InertialCS* ics, Mat34& out, const Vec3& pos) const override;
    // The two-body overload (an impulse between the point `pos` of `a` and of
    // `b`). Nothing in MM2 calls it. Like the original, it builds both
    // couplings from body 1's lever arm to the joint.
    void computeInvMassMatrix(const InertialCS* a, const InertialCS* b, Mat34& out, const Vec3& pos) const;

    // --- dgTrailerJoint members ---
    float forceLimit = 0.0f; // ForceLimit, in units of 10000 N (0: never breaks)
    int status = kForce;     // JointStatus
    Mat34 restOrient1;       // body 1's rest orientation
    Mat34 restOrient2;       // body 2's rest orientation
    Vec3 frictionLean;       // RestoreForceLean, DampConstLean, DampLinearLean
    Vec3 frictionRoll;       // RestoreForceRoll, DampConstRoll, DampLinearRoll
    float leanLimit = 0.0f;
    float negativeRollLimit = 0.0f;
    float positiveRollLimit = 0.0f;
    float leanElasticity = 0.0f; // LimitElasticityLean
    float rollElasticity = 0.0f; // LimitElasticityRoll
    float freeRange = 0.0f;      // hitch gap allowed before the bodies are moved (m)
    float freeLean = 0.0f;       // lean below which no restoring torque acts (rad)
    float cosFreeLean = 1.0f;
    float freeRoll = 0.0f; // |roll| above which the roll torque would act (rad)

    // OpenMM2 switch, not an MM2 member: true (the default) reproduces how
    // MM2 turns the joint force (through a matrix that still holds the
    // trailer's inverse mass matrix, see update()); false uses the plain
    // rotation (MM1's Joint3Dof).
    bool mm2ForceRotation = true;

    // --- Diagnostics from the last update (not in the original) ---
    float lean = 0.0f;
    float leanRate = 0.0f;
    float roll = 0.0f;
    float rollRate = 0.0f;
    Vec3 jointForce; // force on the tractor (rotated), N
    Vec3 gap;        // trailer hitch - tractor hitch before the FreeRange correction

private:
    void doJointTorque(const Mat34& a, const Mat34& at, const Mat34& b, const Mat34& bt, float& leanOut,
                       float& leanRateOut, Vec3& axisOut, float& rollOut, float& rollRateOut);
    void doJointLimits(float leanErr, const Vec3& leanAxis, float rollErr, const Vec3& rollAxis, Vec3& force,
                       const Mat34& k, const Mat34& iw1Inv, const Mat34& iw2Inv, const Mat34& x1,
                       const Mat34& x2, float dt, float invDt);
};

} // namespace mm2::phys
