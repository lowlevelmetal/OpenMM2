#pragma once

#include "core/Math.h"

namespace mm2::phys {

class InertialCS;

// phJoint (Midtown Madness 2, verified against the build 3393 code): a ball
// joint between two rigid bodies, the base of dgTrailerJoint (TrailerJoint).
//
// The joint point is `offset1` in body 1's frame and `offset2` in body 2's
// (both relative to each body's centre of mass, i.e. in InertialCS space).
// update() is the generic joint step: the inverse mass matrix at the joint,
// a force that cancels the relative acceleration and the whole relative
// velocity of the two joint points in one sample, and a push that closes
// the gap. MM2's trailers use TrailerJoint::update instead; nothing in the
// game runs this generic step, which is ported for completeness.
class Joint {
public:
    Joint() = default;
    virtual ~Joint() = default;
    Joint(const Joint&) = delete;
    Joint& operator=(const Joint&) = delete;

    // phJoint::Init: links the bodies; offset2 is where body 1's joint point
    // currently lies in body 2's frame.
    void init(InertialCS* ics1, InertialCS* ics2, const Vec3& offset1);
    void init(InertialCS* ics1, InertialCS* ics2, const Vec3& offset1, const Vec3& offset2);
    // phJoint::Reset: the joint point goes back to where Init put it.
    void reset();
    // phJoint::Update: computeInvMassMatrix, computeJointForce,
    // computeJointPush.
    void update(float invDt);
    // phJoint::ComputeInvMassMatrix(): the joint point from body 1, and
    // (C1 + C2)^-T there (C = InertialCS::calcCMatrix).
    void computeInvMassMatrix();
    // phJoint::ComputeJointForce: f = (dv * invDt + a2 - a1) * M at the joint
    // point (v, a: the bodies' point velocities and accelerations), applied
    // +f to body 1 and -f to body 2 with their torques.
    void computeJointForce(float invDt);
    // phJoint::ComputeJointPush: pushes both bodies along the gap between
    // body 2's joint point and the joint, each by m1 / (m1 + m2) of it.
    void computeJointPush();
    // phJoint::GetInvMassMatrix.
    const Mat34& invMassMatrix() const { return m_invMassMatrix; }

    // phJoint::IsBroken (a plain phJoint never breaks).
    virtual bool isBroken() const { return false; }
    // phJoint::ComputeInvMassMatrix(ics, out, pos): the inverse mass matrix
    // at `pos` of `ics` (one of the two bodies) as seen through the joint
    // (phColliderJointed::GetInvMassMatrix asks the joint for it). phJoint's
    // version leaves `out` unchanged.
    virtual void computeInvMassMatrix(const InertialCS* ics, Mat34& out, const Vec3& pos) const;

    InertialCS* ics1 = nullptr;
    InertialCS* ics2 = nullptr;
    Vec3 offset1;
    Vec3 offset2;
    Vec3 position;        // the joint point (world)
    Vec3 initialPosition; // where Init put it (phJoint::Reset returns there)

    // OpenMM2 (network prediction): the inverse mass matrix as the last
    // update left it, saved and put back with the joint's other state.
    void setInvMassMatrix(const Mat34& m) { m_invMassMatrix = m; }

protected:
    Mat34 m_invMassMatrix;
};

} // namespace mm2::phys
