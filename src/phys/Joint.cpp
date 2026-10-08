// phJoint from Midtown Madness 2 (Init, Reset, Update, ComputeInvMassMatrix,
// ComputeJointForce, ComputeJointPush, GetInvMassMatrix, IsBroken), verified
// against the build 3393 code (MM2Recomp). Operation order and float32
// arithmetic follow the original.

#include "phys/Joint.h"

#include "phys/AgeMath.h"
#include "phys/InertialCS.h"

namespace mm2::phys {
namespace {

// The joint point o * M + m3 as phJoint::Init(ics, ics, offset) and
// ComputeInvMassMatrix sum it.
Vec3 jointPoint(const Mat34& m, const Vec3& o) {
    return {((m.m1.x * o.y + m.m2.x * o.z) + m.m0.x * o.x) + m.m3.x,
            ((m.m1.y * o.y + m.m0.y * o.x) + m.m2.y * o.z) + m.m3.y,
            ((m.m1.z * o.y + m.m0.z * o.x) + m.m2.z * o.z) + m.m3.z};
}

} // namespace

void Joint::init(InertialCS* a, InertialCS* b, const Vec3& o1) {
    // phJoint::Init(ics, ics, offset): body 1's joint point in body 2's frame.
    const Vec3 p = jointPoint(a->matrix, o1);
    const Vec3 d{p.x - b->matrix.m3.x, p.y - b->matrix.m3.y, p.z - b->matrix.m3.z};
    const Mat34& m = b->matrix;
    const Vec3 o2{(d.z * m.m0.z + d.y * m.m0.y) + d.x * m.m0.x, (d.z * m.m1.z + d.y * m.m1.y) + d.x * m.m1.x,
                  (d.z * m.m2.z + d.y * m.m2.y) + d.x * m.m2.x};
    init(a, b, o1, o2);
}

void Joint::init(InertialCS* a, InertialCS* b, const Vec3& o1, const Vec3& o2) {
    // phJoint::Init(ics, ics, offset, offset).
    ics1 = a;
    ics2 = b;
    offset1 = o1;
    offset2 = o2;
    const Mat34& m = ics1->matrix;
    initialPosition = {((m.m2.x * o1.z + m.m1.x * o1.y) + m.m0.x * o1.x) + m.m3.x,
                       ((m.m2.y * o1.z + m.m0.y * o1.x) + m.m1.y * o1.y) + m.m3.y,
                       ((m.m2.z * o1.z + m.m0.z * o1.x) + m.m1.z * o1.y) + m.m3.z};
    reset();
}

void Joint::reset() {
    m_invMassMatrix = Mat34::identity();
    position = initialPosition;
}

void Joint::update(float invDt) {
    // phJoint::Update calls the three steps through the vtable.
    computeInvMassMatrix();
    computeJointForce(invDt);
    computeJointPush();
}

void Joint::computeInvMassMatrix() {
    position = jointPoint(ics1->matrix, offset1);
    Mat34 c1, c2;
    ics2->calcCMatrix(c2, position);
    ics1->calcCMatrix(c1, position);
    m_invMassMatrix = age::transpose(age::inverse(age::add3x3(c2, c1)));
    m_invMassMatrix.m3 = {};
}

void Joint::computeJointForce(float invDt) {
    const Vec3 v1 = ics1->getVelocity(&position);
    const Vec3 v2 = ics2->getVelocity(&position);
    const Vec3 dv{v2.x - v1.x, v2.y - v1.y, v2.z - v1.z};
    const Vec3 a1 = ics1->localAcceleration(position);
    const Vec3 a2 = ics2->localAcceleration(position);
    const Vec3 t{dv.x * invDt + (a2.x - a1.x), dv.y * invDt + (a2.y - a1.y), dv.z * invDt + (a2.z - a1.z)};
    const Mat34& m = m_invMassMatrix;
    const Vec3 f{(t.z * m.m2.x + t.y * m.m1.x) + t.x * m.m0.x, (t.z * m.m2.y + t.y * m.m1.y) + t.x * m.m0.y,
                 (t.z * m.m2.z + t.y * m.m1.z) + t.x * m.m0.z};
    ics1->applyForce(f, position);
    ics2->applyForce({-f.x, -f.y, -f.z}, position);
}

void Joint::computeJointPush() {
    const Mat34& m = ics2->matrix;
    const Vec3& o = offset2;
    const Vec3 p2{((m.m2.x * o.z + m.m1.x * o.y) + m.m0.x * o.x) + m.m3.x,
                  ((m.m0.y * o.x + m.m2.y * o.z) + m.m1.y * o.y) + m.m3.y,
                  ((m.m0.z * o.x + m.m2.z * o.z) + m.m1.z * o.y) + m.m3.z};
    const Vec3 gap{p2.x - position.x, p2.y - position.y, p2.z - position.z};
    if (gap.x == 0.0f && gap.y == 0.0f && gap.z == 0.0f)
        return;
    // The share is body 1's mass over the total, as the original computes it.
    const float m1 = ics1->mass;
    const float share = (1.0f / (ics2->mass + m1)) * m1;
    ics1->applyPush({share * gap.x, gap.y * share, gap.z * share});
    ics2->applyPush({-gap.x * share, -gap.y * share, -gap.z * share});
}

void Joint::computeInvMassMatrix(const InertialCS*, Mat34&, const Vec3&) const {}

} // namespace mm2::phys
