// Port of Joint3Dof from Open1560 (https://github.com/0x1F9F1/Open1560),
// GPL-3.0: code/midtown/game.asm, Midtown Madness 1 beta build 1560
// (Init, InitJoint3Dof, SetPosition, SetFriction*, Set*Limit,
// SetRestOrientMat, SetJointForceFlag, Break/UnbreakJoint, Update,
// DoJointTorque, DoJointLimits, GetCMatrix). Operation order and
// float32 arithmetic follow the original. FreeLean/FreeRoll are MM2
// additions (inferred).

#include "phys/Joint3Dof.h"

#include "phys/AgeMath.h"
#include "phys/InertialCS.h"

#include <cmath>

namespace mm2::phys {
namespace {

constexpr float kPi = 3.1415927f;
// flt_61FA90 (= 40 / pi) and the static initialiser _$E404:
// flt_719110 = 2 * sqrt(flt_61FA90).
constexpr float kJointSpring = 12.732395f;
const float kJointDamp = [] {
    const float s = std::sqrt(kJointSpring);
    return s + s;
}();
constexpr float kJointConst = 10.0f;

Vec3 sub(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

// w x r in the original's operand order (Joint3Dof::Update).
Vec3 crossWR(const Vec3& w, const Vec3& r) {
    return {r.z * w.y - r.y * w.z, r.x * w.z - w.x * r.z, w.x * r.y - r.x * w.y};
}
// w x u (second cross of the centripetal term).
Vec3 crossWU(const Vec3& w, const Vec3& u) {
    return {u.z * w.y - u.y * w.z, u.x * w.z - w.x * u.z, w.x * u.y - u.x * w.y};
}
// w x L (gyroscopic term).
Vec3 crossWL(const Vec3& w, const Vec3& l) {
    return {l.z * w.y - l.y * w.z, l.x * w.z - w.x * l.z, w.x * l.y - l.x * w.y};
}
// r x f.
Vec3 crossRF(const Vec3& r, const Vec3& f) {
    return {r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x};
}

Mat34 diag(const Vec3& d) {
    Mat34 m;
    m.m0 = {d.x, 0.0f, 0.0f};
    m.m1 = {0.0f, d.y, 0.0f};
    m.m2 = {0.0f, 0.0f, d.z};
    m.m3 = {};
    return m;
}

float freePlay(float angle, float free) {
    if (free <= 0.0f)
        return angle;
    if (angle > free)
        return angle - free;
    if (angle < -free)
        return angle + free;
    return 0.0f;
}

} // namespace

void Joint3Dof::init() {
    leanLimit1 = kPi;
    rollLimit2 = kPi;
    forceLimit = 0.0f;
    leanLimit2 = 1.0f;
    rollLimit3 = 1.0f;
    jointFlags = 0;
    rollLimit1 = -kPi;
    frictionLean = {};
    frictionRoll = {};
    orientation1 = Mat34::identity();
    orientation2 = Mat34::identity();
}

void Joint3Dof::initJoint3Dof(InertialCS* a, const Vec3& o1, InertialCS* b, const Vec3& o2) {
    ics1 = a;
    ics2 = b;
    offset1 = o1;
    offset2 = o2;
    setPosition(a->matrix.m3);
    ics1->constraints = InertialCS::kConstrainLink;
    ics1->joint = this;
    ics2->constraints = InertialCS::kConstrainLink;
    ics2->joint = this;
}

void Joint3Dof::setPosition(const Vec3& pos) {
    const Vec3 p = pos;
    ics1->matrix.m3 = p;
    ics1->matrix.m3 = ics1->matrix.transform({-offset1.x, -offset1.y, -offset1.z});
    ics2->matrix.m3 = p;
    ics2->matrix.m3 = ics2->matrix.transform({-offset2.x, -offset2.y, -offset2.z});
    position = p;
}

void Joint3Dof::setFrictionLean(float restore, float dampConst, float dampLinear) {
    frictionLean = {restore, dampConst, dampLinear};
    setJointForceFlag();
}

void Joint3Dof::setFrictionRoll(float restore, float dampConst, float dampLinear) {
    frictionRoll = {restore, dampConst, dampLinear};
    setJointForceFlag();
}

void Joint3Dof::setLeanLimit(float limit, float elasticity) {
    leanLimit1 = limit;
    leanLimit2 = elasticity;
    setJointForceFlag();
}

void Joint3Dof::setRollLimit(float negativeLimit, float positiveLimit, float elasticity) {
    rollLimit1 = negativeLimit;
    rollLimit2 = positiveLimit;
    rollLimit3 = elasticity;
    setJointForceFlag();
}

void Joint3Dof::setRestOrientMat(const Mat34& m) {
    orientation2 = m;
    orientation2.m3 = {};
    orientation1 = Mat34::identity();
}

void Joint3Dof::setRestOrientMat(const Mat34& m1, const Mat34& m2) {
    orientation1 = m1;
    orientation1.m3 = {};
    orientation2 = m2;
    orientation2.m3 = {};
}

void Joint3Dof::setJointForceFlag() {
    const bool any = frictionLean.x != 0.0f || frictionLean.y != 0.0f || frictionLean.z != 0.0f ||
                     frictionRoll.x != 0.0f || frictionRoll.y != 0.0f || frictionRoll.z != 0.0f ||
                     leanLimit1 < kPi;
    if (any)
        jointFlags |= kForce;
    else
        jointFlags &= ~kForce;
}

void Joint3Dof::breakJoint() {
    jointFlags |= kBroken;
    ics1->constraints &= ~InertialCS::kConstrainLink;
    ics2->constraints &= ~InertialCS::kConstrainLink;
}

void Joint3Dof::unbreakJoint() {
    jointFlags &= ~kBroken;
    ics1->constraints |= InertialCS::kConstrainLink;
    ics2->constraints |= InertialCS::kConstrainLink;
}

void Joint3Dof::update(float dt, float invDt) {
    if (jointFlags & kBroken)
        return;
    InertialCS& a = *ics1;
    InertialCS& b = *ics2;

    Mat34 r1 = a.matrix;
    r1.m3 = {};
    Mat34 r2 = b.matrix;
    r2.m3 = {};
    const Mat34 r1t = age::transpose(r1);
    const Mat34 r2t = age::transpose(r2);

    a.finishForces(dt, invDt);
    b.finishForces(dt, invDt);

    Mat34 c1, c2;
    a.calcCMatrix(c1, position);
    b.calcCMatrix(c2, position);
    const Mat34 k = age::inverse(age::add3x3(c2, c1));

    const Vec3 rel1 = sub(position, a.matrix.m3);
    const Vec3 rel2 = sub(position, b.matrix.m3);
    const Mat34 x1 = age::crossProdMatrix(rel1);
    const Mat34 x2 = age::crossProdMatrix(rel2);

    const Mat34 iw1 = age::dot(age::dot(r1t, diag(a.inertia)), r1);
    const Mat34 iw1Inv = age::inverse(iw1);
    const Mat34 iw2 = age::dot(age::dot(r2t, diag(b.inertia)), r2);
    const Mat34 iw2Inv = age::inverse(iw2);

    float leanAngle = 0.0f, leanAngleRate = 0.0f, rollAngle = 0.0f, rollAngleRate = 0.0f;
    Vec3 leanAxis;
    Mat34 bm;
    if (jointFlags & kForce) {
        bm = age::dot3x3(orientation2, r2);
        const Mat34 am = age::dot3x3(orientation1, r1);
        doJointTorque(am, age::transpose(am), bm, age::transpose(bm), leanAngle, leanAngleRate, leanAxis,
                      rollAngle, rollAngleRate);
    }

    // Forces and torques acting this sample, impulses included.
    auto scaled = [](const Vec3& v, float s) { return Vec3{v.x * s, v.y * s, v.z * s}; };
    auto added = [](const Vec3& base, const Vec3& extra) {
        return Vec3{base.x + extra.x, base.y + extra.y, base.z + extra.z};
    };
    const Vec3 f1 = added(a.linearForce, scaled(a.linearImpulse, invDt));
    const Vec3 t1 = added(a.angularTorque, scaled(a.angularImpulse, invDt));
    const Vec3 f2 = added(b.linearForce, scaled(b.linearImpulse, invDt));
    const Vec3 t2 = added(b.angularTorque, scaled(b.angularImpulse, invDt));

    // Acceleration of body 2's joint point without the joint.
    Vec3 acc{b.invMass * f2.x, b.invMass * f2.y, b.invMass * f2.z};
    {
        const Vec3& w = b.angularVelocity;
        const Vec3 c = crossWU(w, crossWR(w, rel2));
        acc = {c.x + acc.x, c.y + acc.y, c.z + acc.z};
        const Vec3 l = iw2.transform(w);
        const Vec3 g = crossWL(w, l);
        const Vec3 tau{t2.x - g.x, t2.y - g.y, t2.z - g.z};
        const Vec3 alpha = iw2Inv.transform(tau);
        const Vec3 at = x2.transform(alpha);
        acc = {at.x + acc.x, at.y + acc.y, at.z + acc.z};
    }
    // Minus body 1's.
    {
        const Vec3 a1{a.invMass * f1.x, a.invMass * f1.y, a.invMass * f1.z};
        acc = {acc.x - a1.x, acc.y - a1.y, acc.z - a1.z};
        const Vec3& w = a.angularVelocity;
        const Vec3 c = crossWU(w, crossWR(w, rel1));
        acc = {acc.x - c.x, acc.y - c.y, acc.z - c.z};
        const Vec3 l = iw1.transform(w);
        const Vec3 g = crossWL(w, l);
        const Vec3 tau{t1.x - g.x, t1.y - g.y, t1.z - g.z};
        const Vec3 alpha = iw1Inv.transform(tau);
        const Vec3 at = x1.transform(alpha);
        acc = {acc.x - at.x, acc.y - at.y, acc.z - at.z};
    }
    // Velocity drift correction.
    {
        const Vec3 v1 = a.getVelocity(&position);
        const Vec3 v2 = b.getVelocity(&position);
        const Vec3 dv{v2.x - v1.x, v2.y - v1.y, v2.z - v1.z};
        const Vec3 corr{dv.x * 0.33f, dv.y * 0.33f, dv.z * 0.33f};
        const Vec3 corr2{invDt * corr.x, invDt * corr.y, invDt * corr.z};
        acc = {corr2.x + acc.x, corr2.y + acc.y, corr2.z + acc.z};
    }
    Vec3 force = k.transform(acc);

    if (jointFlags & kForce) {
        float leanErr = 0.0f, rollErr = 0.0f;
        bool limit = false;
        if (!(leanAngle < leanLimit1)) {
            leanErr = leanAngleRate - (leanAngle - leanLimit1) * -3.0f;
            if (leanErr > 0.0f)
                limit = true;
            else
                leanErr = 0.0f;
        }
        if (rollAngle > rollLimit2) {
            rollErr = rollAngleRate - (rollAngle - rollLimit2) * -3.0f;
            if (rollErr > 0.0f)
                limit = true;
            else
                rollErr = 0.0f;
        } else if (rollAngle < rollLimit1) {
            rollErr = rollAngleRate - (rollAngle - rollLimit1) * -3.0f;
            if (rollErr < 0.0f)
                limit = true;
            else
                rollErr = 0.0f;
        }
        if (limit)
            doJointLimits(leanErr, leanAxis, rollErr, bm.m2, force, k, iw1Inv, iw2Inv, x1, x2, dt, invDt);
    }

    if (static_cast<double>(forceLimit) > 0.0 &&
        forceLimit * forceLimit < (force.y * force.y + force.z * force.z) + force.x * force.x)
        breakJoint();

    // Torques of the joint force about each centre of gravity.
    a.applyTorque(crossRF(sub(position, a.matrix.m3), force));
    {
        const Vec3 r = sub(position, b.matrix.m3);
        b.applyTorque(crossRF({-r.x, -r.y, -r.z}, force));
    }

    // Rotate the force by half of this sample's mean rotation.
    {
        const Vec3& w1 = a.angularVelocity;
        const Vec3& w2 = b.angularVelocity;
        const Vec3 sum{w2.x + w1.x, w1.y + w2.y, w1.z + w2.z};
        const Vec3 half{sum.x * 0.5f, sum.y * 0.5f, sum.z * 0.5f};
        const Vec3 theta{dt * half.x, dt * half.y, dt * half.z};
        const float theta2 = (theta.y * theta.y + theta.z * theta.z) + theta.x * theta.x;
        if (static_cast<double>(theta2) > 1e-05) {
            const float m = std::sqrt(theta2);
            const float invM = 1.0f / m;
            const Vec3 n{theta.x * invM, theta.y * invM, theta.z * invM};
            const float halfAngle = m * 0.5f;
            Mat34 rot;
            age::rotateAbs(rot, n, halfAngle);
            const float d = (n.x * force.x + n.z * force.z) + n.y * force.y;
            const Vec3 par{d * n.x, d * n.y, d * n.z};
            const Vec3 perp{force.x - par.x, force.y - par.y, force.z - par.z};
            force = rot.transformDir(perp);
            force = {force.x + par.x, force.y + par.y, force.z + par.z};
            const float factor = theta2 < 1.0f ? 1.0f - theta2 * 0.041666668f : [&] {
                const float s = std::sin(halfAngle) * invM;
                return s + s;
            }();
            force = {force.x * factor, force.y * factor, force.z * factor};
        }
    }
    jointForce = force;
    a.applyForce(force);
    b.applyForce({-force.x, -force.y, -force.z});

    a.applyPush(b.linearPush);
    b.linearPush = a.linearPush;
    a.finishUpdate(dt);
    b.finishUpdate(dt);

    const Vec3 p1 = a.matrix.transform(offset1);
    const Vec3 p2 = b.matrix.transform(offset2);
    discrepancy = sub(p2, p1);
    // The original computes 0.5 * (p1 + p1): the joint follows the first body
    // and the second is moved onto it.
    const Vec3 twice{p1.x + p1.x, p1.y + p1.y, p1.z + p1.z};
    setPosition({0.5f * twice.x, 0.5f * twice.y, 0.5f * twice.z});

    lean = leanAngle;
    leanRate = leanAngleRate;
    roll = rollAngle;
    rollRate = rollAngleRate;
}

void Joint3Dof::doJointTorque(const Mat34& a, const Mat34& at, const Mat34& b, const Mat34& bt,
                              float& leanOut, float& leanRateOut, Vec3& axisOut, float& rollOut,
                              float& rollRateOut) {
    const Vec3& bz = b.m2;
    const Vec3 bInA = at.transform(bz);
    if (!(bInA.z < 1.0f))
        leanOut = 0.0f;
    else if (!(bInA.z > -1.0f))
        leanOut = kPi;
    else
        leanOut = static_cast<float>(std::acos(static_cast<double>(bInA.z)));

    Vec3 a0;
    if (static_cast<double>(leanOut) > 1e-05) {
        a0 = {bInA.y, -bInA.x, 0.0f};
        const float s = age::invMag(a0);
        a0 = {a0.x * s, a0.y * s, a0.z * s};
    } else {
        a0 = {1.0f, 0.0f, 0.0f};
    }
    axisOut = a.transform(a0);
    const Vec3& axis = axisOut;

    const Vec3& w1 = ics1->angularVelocity;
    const Vec3& w2 = ics2->angularVelocity;
    const Vec3 dw{w2.x - w1.x, w2.y - w1.y, w2.z - w1.z};
    leanRateOut = -(((dw.z * axis.z) + (dw.y * axis.y)) + axis.x * dw.x);
    rollRateOut = -(((bz.z * dw.z) + (bz.y * dw.y)) + bz.x * dw.x);
    const float rr = rollRateOut;
    const Vec3 lv{bz.x * rr + dw.x, bz.y * rr + dw.y, rr * bz.z + dw.z};

    const float m1 = age::mag(offset1);
    const float m2 = age::mag(offset2);
    const float l1 = m1 > 0.0f ? m1 : 1.0f;
    const float l2 = m2 > 0.0f ? m2 : 1.0f;
    const float eff = (l2 * l1) / ((ics1->invMass * m2) + ics2->invMass * m1);
    const float kS = eff * kJointSpring;
    const float kD = eff * kJointDamp;
    const float kC = eff * kJointConst;

    // Constant damping only when some component of the rate is >= 1 rad/s
    // (the original truncates each component to an integer).
    const float s = static_cast<float>(std::abs(static_cast<int>(lv.z))) +
                    (static_cast<float>(std::abs(static_cast<int>(lv.x))) +
                     static_cast<float>(std::abs(static_cast<int>(lv.y))));
    Vec3 lvn;
    if (static_cast<double>(s) > 1e-05) {
        const float im = age::invMag(lv);
        lvn = {lv.x * im, lv.y * im, lv.z * im};
    }

    const float tLean = (frictionLean.x * freePlay(leanOut, freeLean)) * kS;
    const float cTerm = frictionLean.y * kC;
    const float dTerm = frictionLean.z * kD;
    Vec3 t{(tLean * axis.x - cTerm * lvn.x) - dTerm * lv.x, (tLean * axis.y - cTerm * lvn.y) - dTerm * lv.y,
           (tLean * axis.z - cTerm * lvn.z) - dTerm * lv.z};

    rollOut = 0.0f;
    if (frictionRoll.x != 0.0f || frictionRoll.y != 0.0f || frictionRoll.z != 0.0f) {
        const Vec3 av = bt.transform(axis);
        const Vec3 c{bInA.y * a0.z - bInA.z * a0.y, bInA.z * a0.x - bInA.x * a0.z,
                     bInA.x * a0.y - bInA.y * a0.x};
        const float num = ((c.x * av.x) + (c.y * av.y)) + c.z * av.z;
        const float den = ((av.x * a0.x) + (av.y * a0.y)) + av.z * a0.z;
        rollOut = static_cast<float>(std::atan2(static_cast<double>(num), static_cast<double>(den)));
        const float kInv = 1.0f / (ics2->invInertia.z + ics1->invInertia.z);
        const float kS2 = kInv * kJointSpring;
        const float kD2 = kInv * kJointDamp;
        const float kC2 = kInv * kJointConst;
        float sgn;
        if (static_cast<double>(rr) > 1e-05)
            sgn = 1.0f;
        else if (static_cast<double>(rr) < -1e-05)
            sgn = -1.0f;
        else
            sgn = 0.0f;
        const float tr =
            ((freePlay(rollOut, freeRoll) * kS2) * frictionRoll.x + (frictionRoll.z * rr) * kD2) +
            (frictionRoll.y * sgn) * kC2;
        t = {tr * bz.x + t.x, bz.y * tr + t.y, bz.z * tr + t.z};
    }
    ics1->applyTorque({-t.x, -t.y, -t.z});
    ics2->applyTorque(t);
}

void Joint3Dof::doJointLimits(float leanErr, const Vec3& leanAxis, float rollErr, const Vec3& rollAxis,
                              Vec3& force, const Mat34& k, const Mat34& iw1Inv, const Mat34& iw2Inv,
                              const Mat34& x1, const Mat34& x2, float dt, float invDt) {
    InertialCS& a = *ics1;
    InertialCS& b = *ics2;
    const Mat34 s = age::add3x3(age::dot3x3(iw2Inv, x2), age::dot3x3(iw1Inv, x1));
    const Mat34 sk = age::dot3x3(s, k);
    Mat34 m = age::dot3x3(sk, age::transpose(s));
    m = age::add3x3(iw1Inv, m);
    m = age::add3x3(iw2Inv, m);

    const Vec3 j{force.x * dt, force.y * dt, force.z * dt};
    Vec3 h1{a.angularImpulse.x + a.angularTorque.x * dt, a.angularImpulse.y + a.angularTorque.y * dt,
            a.angularImpulse.z + a.angularTorque.z * dt};
    Vec3 h2{b.angularImpulse.x + b.angularTorque.x * dt, b.angularImpulse.y + b.angularTorque.y * dt,
            b.angularImpulse.z + b.angularTorque.z * dt};
    const Vec3 c1 = crossRF(sub(position, a.matrix.m3), j);
    h1 = {c1.x + h1.x, c1.y + h1.y, c1.z + h1.z};
    const Vec3 c2 = crossRF(sub(position, b.matrix.m3), j);
    h2 = {h2.x - c2.x, h2.y - c2.y, h2.z - c2.z};
    const Vec3 w1 = iw1Inv.transform(h1);
    const Vec3 w2 = iw2Inv.transform(h2);
    const Vec3 dw{w1.x - w2.x, w1.y - w2.y, w1.z - w2.z};

    Vec3 l;
    if (leanErr != 0.0f) {
        const float d = ((leanAxis.z * dw.z) + (leanAxis.y * dw.y)) + leanAxis.x * dw.x;
        if (static_cast<double>(d) > 0.0)
            leanErr = d + leanErr;
        const float q = age::dot(leanAxis, m.transform(leanAxis));
        const float sc = q != 0.0f ? leanErr / q : 0.0f;
        const float f = (leanLimit2 - -1.0f) * sc;
        l = {f * leanAxis.x, f * leanAxis.y, f * leanAxis.z};
    }
    if (rollErr != 0.0f) {
        const float p = age::dot(dw, rollAxis);
        if ((rollErr > 0.0f && p > 0.0f) || (!(rollErr > 0.0f) && p < 0.0f))
            rollErr = p + rollErr;
        const float q = age::dot(rollAxis, m.transform(rollAxis));
        const float sc = q != 0.0f ? rollErr / q : 0.0f;
        const float f = (rollLimit3 - -1.0f) * sc;
        l = {l.x + f * rollAxis.x, l.y + f * rollAxis.y, l.z + f * rollAxis.z};
    }
    a.applyAngImpulse({-l.x, -l.y, -l.z});
    b.applyAngImpulse(l);
    const Vec3 li{l.x * invDt, l.y * invDt, l.z * invDt};
    const Vec3 extra = k.transform(s.transform(li));
    force = {force.x + extra.x, force.y + extra.y, force.z + extra.z};
}

namespace {

// The part of GetCMatrix shared by both overloads: how an impulse at the
// joint (lever `xj`) moves the point `pos` of `ics`:
// Xjᵀ Rᵀ diag(InvInertia) R Xp + InvMass * I.
Mat34 jointCoupling(const InertialCS& ics, const Mat34& xj, const Vec3& pos) {
    const Mat34 xp = age::crossProdMatrix(sub(pos, ics.matrix.m3));
    Mat34 d = age::dot3x3(ics.matrix, xp);
    const Vec3& ii = ics.invInertia;
    d.m0 = {ii.x * d.m0.x, ii.x * d.m0.y, ii.x * d.m0.z};
    d.m1 = {ii.y * d.m1.x, ii.y * d.m1.y, ii.y * d.m1.z};
    d.m2 = {ii.z * d.m2.x, ii.z * d.m2.y, ii.z * d.m2.z};
    const Mat34 t = age::dot3x3(age::transpose(ics.matrix), d);
    Mat34 c = age::dot3x3(age::transpose(xj), t);
    c.m0.x = c.m0.x + ics.invMass;
    c.m1.y = c.m1.y + ics.invMass;
    c.m2.z = c.m2.z + ics.invMass;
    return c;
}

// out -= Sᵀ K S (rows), m3 cleared.
void subtractJointPart(Mat34& out, const Mat34& k, const Mat34& s) {
    const Mat34 ks = age::dot3x3(k, s);
    const Mat34 corr = age::dot3x3(age::transpose(s), ks);
    out.m0 = {out.m0.x - corr.m0.x, out.m0.y - corr.m0.y, out.m0.z - corr.m0.z};
    out.m1 = {out.m1.x - corr.m1.x, out.m1.y - corr.m1.y, out.m1.z - corr.m1.z};
    out.m2 = {out.m2.x - corr.m2.x, out.m2.y - corr.m2.y, out.m2.z - corr.m2.z};
    out.m3 = {};
}

} // namespace

Mat34 Joint3Dof::jointK() const {
    Mat34 c1, c2;
    ics1->calcCMatrix(c1, position);
    ics2->calcCMatrix(c2, position);
    return age::inverse(age::add3x3(c2, c1));
}

void Joint3Dof::getCMatrix(const InertialCS* ics, Mat34& out, const Vec3& pos) const {
    ics->calcCMatrix(out, pos);
    const Mat34 k = jointK();
    const Mat34 xj = age::crossProdMatrix(sub(position, ics->matrix.m3));
    subtractJointPart(out, k, jointCoupling(*ics, xj, pos));
}

void Joint3Dof::getCMatrix(const InertialCS* a, const InertialCS* b, Mat34& out, const Vec3& pos) const {
    a->calcCMatrix(out, pos);
    Mat34 cb;
    b->calcCMatrix(cb, pos);
    out = age::add3x3(out, cb);
    out.m3 = {};
    const Mat34 k = jointK();
    // The original builds the joint lever from the first body for both
    // couplings (Position - a's position, also for b).
    const Mat34 xj = age::crossProdMatrix(sub(position, a->matrix.m3));
    const Mat34 ca = jointCoupling(*a, xj, pos);
    const Mat34 cbj = jointCoupling(*b, xj, pos);
    Mat34 s = age::add3x3(ca, cbj);
    s.m3 = {};
    subtractJointPart(out, k, s);
}

} // namespace mm2::phys
