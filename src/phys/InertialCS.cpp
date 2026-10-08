// phInertialCS from Midtown Madness 2 (Init, InitBoxMass, Zero, Freeze,
// ZeroForces, Update, MoveICS, CalcNetPush, CalcNetTurn, ApplyContactForce,
// GetLocalVelocity, GetLocalFilteredVelocity2, GetCMFilteredVelocity,
// GetLocalAcceleration, GetForce, GetTorque, GetInertiaMatrix,
// GetInvMassMatrix) and the weight dgPhysEntity::Update adds, ported from the
// code of midtown2.exe build 3393 (MM2Recomp). Operation order and float32
// arithmetic follow the original.

#include "phys/InertialCS.h"

#include "phys/AgeMath.h"

#include <cmath>

namespace mm2::phys {

InertialCS::InertialCS() {
    init(1.0f, 1.0f, 1.0f, 1.0f);
    zero();
}

void InertialCS::init(float m, float ix, float iy, float iz) {
    // phInertialCS::Init.
    mass = m;
    invMass = m <= 0.0f ? FLT_MAX : 1.0f / m;
    inertia = {ix, iy, iz};
    invInertia = {ix <= 0.0f ? FLT_MAX : 1.0f / ix, iy <= 0.0f ? FLT_MAX : 1.0f / iy,
                  0.0f < iz ? 1.0f / iz : FLT_MAX};
}

void InertialCS::setMass(float sizeX, float sizeY, float sizeZ, float m) {
    // phInertialCS::InitBoxMass.
    size = {sizeX, sizeY, sizeZ};
    constexpr float k = 0.083333336f;
    const float x2 = sizeX * sizeX, y2 = sizeY * sizeY, z2 = sizeZ * sizeZ;
    init(m, (z2 + y2) * m * k, (z2 + x2) * m * k, (y2 + x2) * m * k);
}

void InertialCS::zeroForces() {
    // phInertialCS::ZeroForces.
    linearForce = {};
    angularTorque = {};
    contactForce = {};
    contactTorque = {};
    linearImpulse = {};
    angularImpulse = {};
    lastPush = {linearPush.x + framePush.x, linearPush.y + framePush.y, linearPush.z + framePush.z};
    framePush = {};
    linearPush = {};
    turnForce = {};
    implicitContact = false;
    for (Mat34* m : {&contactK, &contactXK, &contactXKX})
        *m = Mat34{{}, {}, {}, {}};
}

void InertialCS::freeze() {
    // phInertialCS::Freeze.
    linearMomentum = {};
    angularMomentum = {};
    linearVelocity = {};
    angularVelocity = {};
    frameVelocity = {};
    zeroForces();
}

void InertialCS::zero() {
    // phInertialCS::Zero (the active flag is left as it is).
    matrix = Mat34::identity();
    freeze();
    lastPush = {};
}

void InertialCS::place(const Mat34& m) {
    zero();
    matrix = m;
}

void InertialCS::update(float dt, float invDt) {
    finishForces(dt, invDt);
    finishUpdate(dt);
}

void InertialCS::finishForces(float, float invDt) {
    // OpenMM2's FrameVelocity (MM1): this sample's pushes as a velocity on
    // top of the body's.
    Vec3 fv = linearPush;
    fv = {framePush.x + fv.x, framePush.y + fv.y, framePush.z + fv.z};
    fv = {fv.x * invDt, invDt * fv.y, invDt * fv.z};
    frameVelocity = {fv.x + linearVelocity.x, linearVelocity.y + fv.y, linearVelocity.z + fv.z};

    // dgPhysEntity::Update: Mass * gravity joins the force before the
    // entity integrates (MM2 adds it to y only; x and z of `gravity` are 0).
    linearForce = {mass * gravity.x + linearForce.x, mass * gravity.y + linearForce.y,
                   mass * gravity.z + linearForce.z};
    // phInertialCS::Update(): the contact accumulators join this sample's
    // forces.
    linearForce = {contactForce.x + linearForce.x, contactForce.y + linearForce.y,
                   contactForce.z + linearForce.z};
    angularTorque = {contactTorque.x + angularTorque.x, contactTorque.y + angularTorque.y,
                     contactTorque.z + angularTorque.z};
}

void InertialCS::integrateExplicit(float h) {
    // phInertialCS::Update(float) without contact stiffness.
    Vec3& p = linearMomentum;
    Vec3& L = angularMomentum;
    p = {p.x + linearImpulse.x, p.y + linearImpulse.y, p.z + linearImpulse.z};
    p = {h * linearForce.x + p.x, h * linearForce.y + p.y, h * linearForce.z + p.z};
    linearVelocity = {invMass * p.x, invMass * p.y, invMass * p.z};
    L = {angularImpulse.x + L.x, angularImpulse.y + L.y, angularImpulse.z + L.z};
    L = {h * angularTorque.x + L.x, h * angularTorque.y + L.y, h * angularTorque.z + L.z};

    // Angular velocity about the body axes, each limited to MaxAngVelocity
    // (the momentum is rebuilt from the limited velocity).
    const Mat34& m = matrix;
    float wx = ((m.m0.z * L.z + m.m0.y * L.y) + m.m0.x * L.x) * invInertia.x;
    float wy = ((m.m1.z * L.z + m.m1.y * L.y) + m.m1.x * L.x) * invInertia.y;
    float wz = ((m.m2.z * L.z + m.m2.y * L.y) + m.m2.x * L.x) * invInertia.z;
    if (limitAngVelocity) {
        bool clamped = false;
        const auto clampAxis = [&clamped](float& w, float limit) {
            if (w < -limit) {
                clamped = true;
                w = -limit;
            } else if (limit < w) {
                clamped = true;
                w = limit;
            }
        };
        clampAxis(wx, maxAngVelocity.x);
        clampAxis(wy, maxAngVelocity.y);
        clampAxis(wz, maxAngVelocity.z);
        if (clamped) {
            const float lx = wx * inertia.x, ly = wy * inertia.y, lz = wz * inertia.z;
            L = {lx * m.m0.x, lx * m.m0.y, lx * m.m0.z};
            L = {ly * m.m1.x + L.x, ly * m.m1.y + L.y, ly * m.m1.z + L.z};
            L = {lz * m.m2.x + L.x, lz * m.m2.y + L.y, lz * m.m2.z + L.z};
        }
    }
    Vec3 w{wx * m.m0.x, wx * m.m0.y, wx * m.m0.z};
    w = {wy * m.m1.x + w.x, wy * m.m1.y + w.y, wy * m.m1.z + w.z};
    w = {wz * m.m2.x + w.x, wz * m.m2.y + w.y, wz * m.m2.z + w.z};
    angularVelocity = w;
}

void InertialCS::integrateImplicit(float h) {
    // phInertialCS::Update(float) with contact stiffness: backward Euler in
    // the summed stiffness. The linear part goes through
    // M = (I + h/m K)^-1, the angular part solves B dw = rhs with
    // B = Iw + h XKX - (h^2/m) XK M XK^T and rhs = -(h/m) P M XK^T + angular
    // impulse + h torque (P: linear impulse + h force).
    const float k = h * invMass;
    Mat34 mInv = contactK;
    age::scale3x3(mInv, k);
    mInv.m0.x = mInv.m0.x + 1.0f;
    mInv.m1.y = mInv.m1.y + 1.0f;
    mInv.m2.z = mInv.m2.z + 1.0f;
    mInv = age::inverse(mInv);

    Mat34 b = contactXK;
    age::dot3x3InPlace(b, mInv);
    age::dot3x3TransposeInPlace(b, contactXK);
    age::scale3x3(b, -(k * h));
    age::addScaled3x3(b, contactXKX, h);
    const Mat34 inertiaWorld = worldInertia();
    b = age::add3x3(b, inertiaWorld);

    Vec3 pv = linearImpulse;
    pv = {h * linearForce.x + pv.x, h * linearForce.y + pv.y, h * linearForce.z + pv.z};
    const float nk = -k;
    Vec3 rhs{nk * pv.x, nk * pv.y, nk * pv.z};
    rhs = age::dot3x3(rhs, mInv);
    rhs = age::dot3x3Transpose(rhs, contactXK);
    rhs = {angularImpulse.x + rhs.x, angularImpulse.y + rhs.y, angularImpulse.z + rhs.z};
    rhs = {h * angularTorque.x + rhs.x, h * angularTorque.y + rhs.y, h * angularTorque.z + rhs.z};
    const Vec3 dw = age::solveSVD(b, rhs);

    Vec3 u{h * dw.x, h * dw.y, h * dw.z};
    u = age::dot3x3(u, contactXK);
    u = {pv.x + u.x, pv.y + u.y, pv.z + u.z};
    u = {invMass * u.x, invMass * u.y, invMass * u.z};
    u = age::dot3x3(u, mInv);
    linearVelocity = {u.x + linearVelocity.x, u.y + linearVelocity.y, u.z + linearVelocity.z};
    linearMomentum = {mass * linearVelocity.x, mass * linearVelocity.y, mass * linearVelocity.z};
    angularVelocity = {dw.x + angularVelocity.x, dw.y + angularVelocity.y, dw.z + angularVelocity.z};
    // The angular momentum w * Iw (the original's own product order).
    const Vec3& wv = angularVelocity;
    const Mat34& iw = inertiaWorld;
    angularMomentum = {(iw.m1.x * wv.y + iw.m2.x * wv.z) + iw.m0.x * wv.x,
                       (iw.m0.y * wv.x + iw.m1.y * wv.y) + iw.m2.y * wv.z,
                       (iw.m0.z * wv.x + iw.m1.z * wv.y) + iw.m2.z * wv.z};
}

void InertialCS::finishUpdate(float h) {
    // phInertialCS::Update(float): an inactive body (asleep) keeps only the
    // push bookkeeping.
    if (state != Asleep) {
        if (implicitContact)
            integrateImplicit(h);
        else
            integrateExplicit(h);

        // Speed limit, then position and rotation.
        const float v2 = age::mag2(linearVelocity);
        if (maxSpeed * maxSpeed < v2) {
            const float s = (v2 == 0.0f ? 0.0f : 1.0f / std::sqrt(v2)) * maxSpeed;
            linearVelocity = {s * linearVelocity.x, s * linearVelocity.y, s * linearVelocity.z};
            linearMomentum = {s * linearMomentum.x, s * linearMomentum.y, s * linearMomentum.z};
        }
        matrix.m3 = {linearPush.x + matrix.m3.x, linearPush.y + matrix.m3.y, linearPush.z + matrix.m3.z};
        matrix.m3 = {h * linearVelocity.x + matrix.m3.x, h * linearVelocity.y + matrix.m3.y,
                     h * linearVelocity.z + matrix.m3.z};
        // The sample's rotation (and the rotational push) about its axis
        // (Matrix34::RotateUnitAxis).
        Vec3& turn = turnForce;
        turn = {h * angularVelocity.x + turn.x, h * angularVelocity.y + turn.y,
                h * angularVelocity.z + turn.z};
        const float a2 = age::mag2(turn);
        if (1e-15 < static_cast<double>(a2)) {
            const float angle = std::sqrt(a2);
            const float s = 1.0f / angle;
            turn = {s * turn.x, s * turn.y, s * turn.z};
            age::rotateUnitAxis(matrix, turn, angle);
        }
    }
    linearForce = {};
    angularTorque = {};
    linearImpulse = {};
    angularImpulse = {};
    framePush = {linearPush.x + framePush.x, linearPush.y + framePush.y, linearPush.z + framePush.z};
    linearPush = {};
    turnForce = {};
    implicitContact = false;
    for (Mat34* m : {&contactK, &contactXK, &contactXKX})
        *m = Mat34{{}, {}, {}, {}};
    // phInertialCS::Update(): the contact accumulators are spent and this
    // sample's pushes become the last push.
    contactForce = {};
    contactTorque = {};
    lastPush = framePush;
    framePush = {};
}

void InertialCS::moveICS() {
    // phInertialCS::MoveICS.
    matrix.m3 = {linearPush.x + matrix.m3.x, linearPush.y + matrix.m3.y, linearPush.z + matrix.m3.z};
    framePush = {linearPush.x + framePush.x, linearPush.y + framePush.y, linearPush.z + framePush.z};
    linearPush = {};
}

void InertialCS::applyForce(const Vec3& f) {
    linearForce = {linearForce.x + f.x, f.y + linearForce.y, linearForce.z + f.z};
}

void InertialCS::applyForce(const Vec3& f, const Vec3& pos) {
    linearForce = {linearForce.x + f.x, f.y + linearForce.y, linearForce.z + f.z};
    const Vec3 r{pos.x - matrix.m3.x, pos.y - matrix.m3.y, pos.z - matrix.m3.z};
    angularTorque.x = (r.y * f.z - r.z * f.y) + angularTorque.x;
    angularTorque.y = (r.z * f.x - r.x * f.z) + angularTorque.y;
    angularTorque.z = (r.x * f.y - r.y * f.x) + angularTorque.z;
}

void InertialCS::applyTorque(const Vec3& t) {
    angularTorque = {angularTorque.x + t.x, t.y + angularTorque.y, angularTorque.z + t.z};
}

void InertialCS::applyImpulse(const Vec3& j, const Vec3& pos) {
    linearImpulse = {linearImpulse.x + j.x, j.y + linearImpulse.y, linearImpulse.z + j.z};
    const Vec3 r{pos.x - matrix.m3.x, pos.y - matrix.m3.y, pos.z - matrix.m3.z};
    angularImpulse.x = (r.y * j.z - r.z * j.y) + angularImpulse.x;
    angularImpulse.y = (r.z * j.x - r.x * j.z) + angularImpulse.y;
    angularImpulse.z = (r.x * j.y - r.y * j.x) + angularImpulse.z;
}

void InertialCS::applyAngImpulse(const Vec3& j) {
    angularImpulse = {angularImpulse.x + j.x, j.y + angularImpulse.y, angularImpulse.z + j.z};
}

namespace {

// phInertialCS::CalcNetPush / CalcNetTurn (the same code on two fields).
void netPush(Vec3& pending, const Vec3& push) {
    const float d = (pending.y * push.y + pending.z * push.z) + pending.x * push.x;
    if (d < 0.0f) {
        pending = {push.x + pending.x, pending.y + push.y, pending.z + push.z};
        return;
    }
    const float p2 = (push.x * push.x + push.y * push.y) + push.z * push.z;
    if (!(0.0f < p2))
        return;
    const float k = d / p2;
    // Part of the new push not already covered by the pending one.
    const Vec3 delta{k * push.x - push.x, k * push.y - push.y, k * push.z - push.z};
    if ((delta.z * push.z + delta.y * push.y) + delta.x * push.x < 0.0f)
        pending = {pending.x - delta.x, pending.y - delta.y, pending.z - delta.z};
}

} // namespace

void InertialCS::applyPush(const Vec3& push) {
    netPush(linearPush, push);
}

void InertialCS::applyTurn(const Vec3& turn) {
    netPush(turnForce, turn);
}

void InertialCS::applyContactForce(const Vec3& f, const Vec3& pos, const Mat34& k) {
    // phInertialCS::ApplyContactForce.
    implicitContact = true;
    contactForce = {f.x + contactForce.x, f.y + contactForce.y, contactForce.z + f.z};
    const Vec3 r{pos.x - matrix.m3.x, pos.y - matrix.m3.y, pos.z - matrix.m3.z};
    contactTorque = {(r.y * f.z - r.z * f.y) + contactTorque.x, (r.z * f.x - r.x * f.z) + contactTorque.y,
                     (r.x * f.y - r.y * f.x) + contactTorque.z};
    contactK = age::add3x3(contactK, k);
    contactK.m3 = {k.m3.x + contactK.m3.x, k.m3.y + contactK.m3.y, k.m3.z + contactK.m3.z};
    // X K with X = [r]x, then each of its rows a becomes r x a.
    Mat34 xk = age::crossProdMatrix(r);
    age::dot3x3InPlace(xk, k);
    contactXK = age::add3x3(contactXK, xk);
    age::dot3x3CrossProdTranspose(xk, r);
    contactXKX = age::add3x3(contactXKX, xk);
}

Vec3 InertialCS::getVelocity(const Vec3* pos) const {
    // phInertialCS::GetLocalVelocity (without a point: the CG's velocity).
    if (!pos)
        return linearVelocity;
    const Vec3 r{pos->x - matrix.m3.x, pos->y - matrix.m3.y, pos->z - matrix.m3.z};
    const Vec3& w = angularVelocity;
    return {(r.z * w.y - w.z * r.y) + linearVelocity.x, (w.z * r.x - r.z * w.x) + linearVelocity.y,
            (w.x * r.y - w.y * r.x) + linearVelocity.z};
}

Vec3 InertialCS::localAcceleration(const Vec3& pos) const {
    // phInertialCS::GetLocalAcceleration.
    const Vec3 r{pos.x - matrix.m3.x, pos.y - matrix.m3.y, pos.z - matrix.m3.z};
    const Vec3& w = angularVelocity;
    // Centripetal part w x (w x r).
    const Vec3 u{r.z * w.y - r.y * w.z, r.x * w.z - r.z * w.x, r.y * w.x - r.x * w.y};
    Vec3 a{w.y * u.z - u.y * w.z, u.x * w.z - u.z * w.x, u.y * w.x - u.x * w.y};
    a = {invMass * linearForce.x + a.x, invMass * linearForce.y + a.y, invMass * linearForce.z + a.z};
    // Angular acceleration from the torque less the gyroscopic term w x L,
    // through the world inverse inertia R^T diag(1/I) R.
    const Vec3& l = angularMomentum;
    const Vec3 tau{(w.z * l.y - w.y * l.z) + angularTorque.x, (w.x * l.z - l.x * w.z) + angularTorque.y,
                   (l.x * w.y - w.x * l.y) + angularTorque.z};
    const Mat34& m = matrix;
    const float bx = ((tau.z * m.m0.z + tau.y * m.m0.y) + tau.x * m.m0.x) * invInertia.x;
    const float by = ((tau.z * m.m1.z + tau.y * m.m1.y) + tau.x * m.m1.x) * invInertia.y;
    const float bz = ((tau.z * m.m2.z + tau.y * m.m2.y) + tau.x * m.m2.x) * invInertia.z;
    const Vec3 alpha{(bx * m.m0.x + by * m.m1.x) + bz * m.m2.x, (bx * m.m0.y + by * m.m1.y) + bz * m.m2.y,
                     (bx * m.m0.z + by * m.m1.z) + bz * m.m2.z};
    // Tangential part alpha x r.
    return {(alpha.y * r.z - alpha.z * r.y) + a.x, (alpha.z * r.x - r.z * alpha.x) + a.y,
            (r.y * alpha.x - alpha.y * r.x) + a.z};
}

Vec3 InertialCS::getForce(float invDt) const {
    return {invDt * linearImpulse.x + linearForce.x, invDt * linearImpulse.y + linearForce.y,
            invDt * linearImpulse.z + linearForce.z};
}

Vec3 InertialCS::getTorque(float invDt) const {
    return {invDt * angularImpulse.x + angularTorque.x, invDt * angularImpulse.y + angularTorque.y,
            invDt * angularImpulse.z + angularTorque.z};
}

Vec3 InertialCS::filteredVelocity(const Vec3& pos, float invDt) const {
    // phInertialCS::GetLocalFilteredVelocity2.
    Vec3 v = getVelocity(&pos);
    const Vec3& lp = lastPush;
    const float p2 = (lp.z * lp.z + lp.y * lp.y) + lp.x * lp.x;
    if (0.0001f < p2) {
        const float a = ((lp.z * v.z + lp.y * v.y) + lp.x * v.x) * invDt;
        const float b = invDt * invDt * p2;
        float k = invDt;
        if (-a <= b)
            k = -(a / b) * invDt;
        v = {k * lp.x + v.x, k * lp.y + v.y, k * lp.z + v.z};
    }
    return v;
}

Vec3 InertialCS::cmFilteredVelocity(float invDt) const {
    // phInertialCS::GetCMFilteredVelocity.
    return {invDt * lastPush.x + linearVelocity.x, invDt * lastPush.y + linearVelocity.y,
            invDt * lastPush.z + linearVelocity.z};
}

Mat34 InertialCS::worldInertia() const {
    // phInertialCS::GetInertiaMatrix: R^T diag(I), then (in place) times R.
    const Mat34& m = matrix;
    Mat34 t;
    t.m0 = {inertia.x * m.m0.x, m.m1.x * inertia.y, m.m2.x * inertia.z};
    t.m1 = {m.m0.y * inertia.x, m.m1.y * inertia.y, m.m2.y * inertia.z};
    t.m2 = {m.m0.z * inertia.x, m.m1.z * inertia.y, m.m2.z * inertia.z};
    t.m3 = {};
    age::dot3x3InPlace(t, m);
    return t;
}

void InertialCS::calcCMatrix(Mat34& out, const Vec3& pos) const {
    // phInertialCS::GetInvMassMatrix(pos, out).
    const Vec3 r{pos.x - matrix.m3.x, pos.y - matrix.m3.y, pos.z - matrix.m3.z};
    Mat34 rx = matrix;
    age::dot3x3CrossProdMtx(rx, r);
    Mat34 d;
    d.m0 = {rx.m0.x * invInertia.x, rx.m0.y * invInertia.x, rx.m0.z * invInertia.x};
    d.m1 = {rx.m1.x * invInertia.y, rx.m1.y * invInertia.y, rx.m1.z * invInertia.y};
    d.m2 = {rx.m2.x * invInertia.z, rx.m2.y * invInertia.z, rx.m2.z * invInertia.z};
    out = age::dot3x3(age::transpose(rx), d);
    out.m0.x = invMass + out.m0.x;
    out.m1.y = out.m1.y + invMass;
    out.m2.z = out.m2.z + invMass;
    out.m3 = {};
}

} // namespace mm2::phys
