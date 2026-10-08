// MM2's colliders (phColliderBase, phCollider, phColliderJointed), ported
// from the code of midtown2.exe build 3393 (MM2Recomp). See docs/physics.md,
// "Collision".

#include "phys/Collider.h"

#include "phys/Bound.h"
#include "phys/Impact.h"
#include "phys/InertialCS.h"
#include "phys/Joint.h"

#include <cmath>

namespace mm2::phys {

void Collider::init(const Bound* b, const Mat34* m, InertialCS* inertia) {
    // phCollider::Init.
    bound = b;
    matrix = m;
    ics = inertia;
    handler = nullptr;
    joint = nullptr;
    body = nullptr;
    reset();
}

void Collider::initStatic(const Bound* b, const Mat34* m) {
    // phCollider::Init(const phBound*, Matrix34*): Reset, then the collider
    // counts as not moving.
    init(b, m, nullptr);
    maxMoved = 0.0f;
    barelyMoved = true;
}

void Collider::reset() {
    // phColliderBase::Reset.
    lastMatrix = matrix ? *matrix : Mat34::identity();
    justReset = true;
    barelyMoved = false;
    maxMoved = FLT_MAX;
    maxPush2 = 0.0f;
    maxPusher = nullptr;
    lastMaxPusher = nullptr;
}

void Collider::updateMtx() {
    // phColliderBase::UpdateMtx: the pending push moves the body now; the
    // matrix is remembered as it stands (a car's world matrix does not see
    // the push until its next integration).
    if (ics)
        ics->moveICS();
    const void* pusher = maxPusher;
    if (matrix)
        lastMatrix = *matrix;
    lastMaxPusher = pusher;
    maxPush2 = 0.0f;
    maxPusher = nullptr;
    justReset = false;
}

void Collider::calcMaxMoved(float dt) {
    // phColliderBase::CalcMaxMoved: the bound's box swept by the angular
    // velocity, plus the centre's speed with the last push
    // (GetCMFilteredVelocity), over one sample.
    if (!ics) {
        maxMoved = 0.0f;
        barelyMoved = false;
        return;
    }
    const Vec3 w{std::abs(ics->angularVelocity.x), std::abs(ics->angularVelocity.y),
                 std::abs(ics->angularVelocity.z)};
    const Vec3& m = bound->boxMax;
    maxMoved = ((m.z + m.x) * w.y + (m.y + m.x) * w.z) + (m.y + m.z) * w.x;
    const Vec3 v = ics->cmFilteredVelocity(dt > 0.0f ? 1.0f / dt : 0.0f);
    maxMoved = std::sqrt((v.y * v.y + v.z * v.z) + v.x * v.x) + maxMoved;
    maxMoved = maxMoved * dt;
    barelyMoved = maxMoved < kBarelyMovedDistance;
}

Mat34 Collider::copyLastMatrix(const void* pusher) const {
    // phColliderBase::CopyLastMatrix.
    Mat34 m = lastMatrix;
    if (ics && pusher == lastMaxPusher)
        m.m3 = {ics->lastPush.x + m.m3.x, ics->lastPush.y + m.m3.y, ics->lastPush.z + m.m3.z};
    return m;
}

Vec3 Collider::localVelocity(const Vec3& position) const {
    // phColliderBase::GetLocalVelocity.
    if (!ics)
        return {};
    return ics->getVelocity(&position);
}

void Collider::invMassMatrix(const Vec3& position, Mat34& out) const {
    // phColliderJointed::GetInvMassMatrix / phColliderBase::GetInvMassMatrix.
    if (ics && joint && !joint->isBroken()) {
        joint->computeInvMassMatrix(ics, out, position);
        return;
    }
    if (ics) {
        ics->calcCMatrix(out, position);
        return;
    }
    out.m0 = out.m1 = out.m2 = out.m3 = {};
}

void Collider::impact(const Impact& im, const Vec3& impulse, const Vec3& push) {
    // phColliderBase::Impact: the impulse joins the body's accumulators (it
    // acts at the next integration) and the push the pending push; the
    // hardest push and its source are remembered for the next sample's
    // CopyLastMatrix.
    if (ics) {
        ics->applyImpulse(impulse, im.position);
        ics->applyPush(push);
    }
    const Collider* other = im.colliderA == this ? im.colliderB : im.colliderA;
    const float push2 = push.z * push.z + push.y * push.y + push.x * push.x;
    if (maxPush2 < push2) {
        maxPush2 = push2;
        maxPusher = other ? other->key() : nullptr;
    }
    if (handler)
        handler->onImpact(*this, im, impulse);
}

} // namespace mm2::phys
