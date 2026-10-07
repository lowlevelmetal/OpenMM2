#pragma once

#include "core/Math.h"

namespace mm2::phys {

class InertialCS;

// Joint3Dof: the three-degree-of-freedom joint (ball joint with rotational
// friction and limits) that connects a tractor and its trailer. Ported from
// Midtown Madness 1 (Open1560 game.asm, "Joint3Dof"); MM2 calls the same
// mechanism dgTrailerJoint (phJoint) and tunes it from .dgTrailerJoint files.
//
// While linked, both bodies skip their own integration (InertialCS::update
// returns early on kConstrainLink). update() integrates them together each
// sample:
//   1. FinishForces on both bodies.
//   2. Rotational friction/restoring torques (doJointTorque): "lean" is the
//      angle between the bodies' joint axes (Orientation2 * trailer vs
//      Orientation1 * tractor; with the rest orientation used for trailers
//      these are the two up axes, i.e. relative tilt) and "roll" the
//      rotation about the trailer's joint axis (the hitch's yaw angle).
//   3. A constraint force at the joint that makes both bodies' joint points
//      accelerate together, plus a 0.33 * relative-velocity / dt correction.
//   4. Lean/roll limits (doJointLimits) as angular impulses.
//   5. FinishUpdate on both bodies, then the joint point is set from the
//      tractor and the trailer moved onto it (setPosition).
class Joint3Dof {
public:
    // JointFlags.
    static constexpr int kBroken = 1;
    static constexpr int kForce = 2; // friction or lean limit active

    Joint3Dof() { init(); }

    // Joint3Dof::Init: no friction, limits +-pi, elasticity 1, identity
    // rest orientations.
    void init();
    // Joint3Dof::InitJoint3Dof: links two bodies at offsets relative to each
    // body's centre of gravity (body space) and places them (setPosition with
    // the first body's position).
    void initJoint3Dof(InertialCS* ics1, const Vec3& offset1, InertialCS* ics2, const Vec3& offset2);
    // Joint3Dof::SetPosition: puts the joint at a world point and moves both
    // bodies so that their offsets land on it.
    void setPosition(const Vec3& pos);
    void setFrictionLean(float restore, float dampConst, float dampLinear);
    void setFrictionRoll(float restore, float dampConst, float dampLinear);
    void setLeanLimit(float limit, float elasticity);
    void setRollLimit(float negativeLimit, float positiveLimit, float elasticity);
    // Orientation2 = m, Orientation1 = identity.
    void setRestOrientMat(const Mat34& m);
    void setRestOrientMat(const Mat34& m1, const Mat34& m2);
    void setJointForceFlag();
    void breakJoint();
    void unbreakJoint();
    bool isBroken() const { return (jointFlags & kBroken) != 0; }

    // Joint3Dof::Update for one sample of `dt` seconds (the original reads
    // ARTSPTR->seconds_ / inv_seconds_).
    void update(float dt, float invDt);

    // Joint3Dof::GetCMatrix: the collision matrix at the world point `pos`
    // of `ics` (one of the two bodies) through the joint: an impulse j there
    // changes that point's velocity by j * out. C(pos) - Sᵀ K S with
    // K = (C1 + C2)⁻¹ at the joint and S the coupling between the joint and
    // `pos`.
    void getCMatrix(const InertialCS* ics, Mat34& out, const Vec3& pos) const;
    // The two-body overload: the matrix for an impulse between the point
    // `pos` of `a` and of `b` (collisions between the linked bodies, which
    // OpenMM2's World does not run yet).
    void getCMatrix(const InertialCS* a, const InertialCS* b, Mat34& out, const Vec3& pos) const;

    InertialCS* ics1 = nullptr;
    InertialCS* ics2 = nullptr;
    Vec3 offset1;
    Vec3 offset2;
    Vec3 position;
    float forceLimit = 0.0f; // 0 = unbreakable
    int jointFlags = 0;
    Mat34 orientation1;
    Mat34 orientation2;
    Vec3 frictionLean; // restore, constant damping, linear damping
    Vec3 frictionRoll;
    float leanLimit1 = 0.0f; // lean limit
    float rollLimit1 = 0.0f; // negative roll limit
    float rollLimit2 = 0.0f; // positive roll limit
    float leanLimit2 = 0.0f; // lean limit elasticity
    float rollLimit3 = 0.0f; // roll limit elasticity

    // MM2 dgTrailerJoint FreeLean / FreeRoll (inferred): angles of free play
    // within which the restoring spring does nothing.
    float freeLean = 0.0f;
    float freeRoll = 0.0f;

    // Diagnostics from the last update (the original keeps the hitch
    // separation in the global ?discrepancy@@3VVector3@@A).
    Vec3 discrepancy;
    float lean = 0.0f;
    float leanRate = 0.0f;
    float roll = 0.0f;
    float rollRate = 0.0f;
    Vec3 jointForce;

private:
    // (C1 + C2)⁻¹ at the joint position.
    Mat34 jointK() const;
    void doJointTorque(const Mat34& a, const Mat34& at, const Mat34& b, const Mat34& bt, float& leanOut,
                       float& leanRateOut, Vec3& axisOut, float& rollOut, float& rollRateOut);
    void doJointLimits(float leanErr, const Vec3& leanAxis, float rollErr, const Vec3& rollAxis, Vec3& force,
                       const Mat34& k, const Mat34& iw1Inv, const Mat34& iw2Inv, const Mat34& x1,
                       const Mat34& x2, float dt, float invDt);
};

} // namespace mm2::phys
