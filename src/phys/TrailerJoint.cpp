// dgTrailerJoint from Midtown Madness 2 (Init, Reset, SetPosition,
// SetCosFreeLean, SetRotate1/2, SetFrictionLean/Roll, SetLeanLimit,
// SetRollLimit (both), SetRestOrientation, SetRestOrientMat (both),
// SetForceLimit, SetJointForceFlag, Update, MoveICS, Break/UnbreakJoint,
// IsBroken, DoJointTorque, DoJointLimits, both ComputeInvMassMatrix
// overloads, FileIO), verified against the build 3393 code (MM2Recomp).
// It descends from MM1's Joint3Dof (Open1560); MM2 changed the hitch
// handling (FreeRange, FreeLean, FreeRoll, no integration of its own) and
// the units (ForceLimit). Operation order and float32 arithmetic follow the
// original; where the x87 code keeps intermediates in extended precision
// (acos, atan2) the result is computed in double and rounded.

#include "phys/TrailerJoint.h"

#include "phys/AgeMath.h"
#include "phys/InertialCS.h"
#include "phys/vehicle/TuneParams.h"

#include <cmath>

namespace mm2::phys {
namespace {

constexpr float kPi = 3.1415927f;
// The restoring spring per unit of effective inertia (40 / pi) and the
// damping constant 2 sqrt(40 / pi) (a static initialiser in the original).
constexpr float kJointSpring = 12.732395f;
const float kJointDamp = [] {
    const float s = std::sqrt(kJointSpring);
    return s + s;
}();
constexpr float kJointConst = 10.0f;
// ForceLimit is in units of 10 kN.
constexpr float kForceLimitUnit = 10000.0f;

Vec3 sub(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

// Vector3::Dot in MM2's summation order.
float dotZYX(const Vec3& a, const Vec3& b) {
    return (a.z * b.z + a.y * b.y) + a.x * b.x;
}

// Vector3::Cross: a x b.
Vec3 cross(const Vec3& a, const Vec3& b) {
    return {b.z * a.y - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - b.x * a.y};
}

// v * M (3x3), the order of the original's vector-matrix helper.
Vec3 mulRow(const Vec3& v, const Mat34& m) {
    return {(m.m1.x * v.y + m.m2.x * v.z) + v.x * m.m0.x, (m.m0.y * v.x + m.m1.y * v.y) + m.m2.y * v.z,
            (m.m0.z * v.x + m.m1.z * v.y) + m.m2.z * v.z};
}

Mat34 rotation3x3(const Mat34& m) {
    Mat34 r = m;
    r.m3 = {};
    return r;
}

// Matrix34::MakeScale.
Mat34 diag(const Vec3& d) {
    Mat34 m;
    m.m0 = {d.x, 0.0f, 0.0f};
    m.m1 = {0.0f, d.y, 0.0f};
    m.m2 = {0.0f, 0.0f, d.z};
    m.m3 = {};
    return m;
}

// The tractor's hitch point in world space, in the order of
// dgTrailerJoint::Update (and Reset).
Vec3 hitchPoint(const Mat34& m, const Vec3& o) {
    return {((m.m1.x * o.y + m.m2.x * o.z) + o.x * m.m0.x) + m.m3.x,
            ((m.m1.y * o.y + m.m2.y * o.z) + m.m0.y * o.x) + m.m3.y,
            ((m.m1.z * o.y + m.m2.z * o.z) + m.m0.z * o.x) + m.m3.z};
}

// The trailer's hitch point, which dgTrailerJoint::Update sums in another
// order.
Vec3 trailerHitchPoint(const Mat34& m, const Vec3& o) {
    return {((m.m1.x * o.y + m.m2.x * o.z) + m.m0.x * o.x) + m.m3.x,
            ((m.m0.y * o.x + m.m1.y * o.y) + m.m2.y * o.z) + m.m3.y,
            ((m.m0.z * o.x + m.m1.z * o.y) + m.m2.z * o.z) + m.m3.z};
}

} // namespace

void TrailerJoint::init(const TrailerJointParams& p, InertialCS* tractor, InertialCS* trailer,
                        const Vec3& carHitch, const Vec3& trailerHitch) {
    // The constructor's and Init's defaults (TrailerJointParams holds the
    // same values), the force flag from them, then the file's fields. The
    // file's JointStatus replaces the flags whole; MM2 does not recompute
    // the force flag after loading.
    forceLimit = 0.0f;
    rollElasticity = 0.0f;
    status = kForce;
    leanLimit = kPi;
    negativeRollLimit = -0.3f;
    positiveRollLimit = 0.3f;
    leanElasticity = 1.0f;
    frictionLean = {2.0f, 2.0f, 0.9f};
    frictionRoll = {2.0f, 0.1f, 2.0f};
    restOrient1 = Mat34::identity();
    restOrient2 = Mat34::identity();
    setJointForceFlag();
    freeRange = 0.15f;
    freeLean = 0.1f;
    setCosFreeLean();
    freeRoll = 0.1f;

    // dgTrailerJoint::FileIO.
    forceLimit = p.forceLimit;
    status = p.jointStatus;
    frictionLean = {p.restoreForceLean, p.dampConstLean, p.dampLinearLean};
    frictionRoll = {p.restoreForceRoll, p.dampConstRoll, p.dampLinearRoll};
    leanLimit = p.leanLimit;
    leanElasticity = p.limitElasticityLean;
    rollElasticity = p.limitElasticityRoll;
    freeRange = p.freeRange;
    freeLean = p.freeLean;
    freeRoll = p.freeRoll;
    setCosFreeLean();

    Joint::init(tractor, trailer, carHitch, trailerHitch);
    lean = leanRate = roll = rollRate = 0.0f;
    jointForce = gap = {};
}

void TrailerJoint::reset() {
    unbreakJoint();
    Joint::reset();
    position = hitchPoint(ics1->matrix, offset1);
}

void TrailerJoint::setPosition(const Vec3& pos) {
    for (auto [ics, o] : {std::pair{ics1, offset1}, std::pair{ics2, offset2}}) {
        const Mat34& m = ics->matrix;
        const Vec3 r{(m.m2.x * o.z + m.m1.x * o.y) + m.m0.x * o.x, (m.m0.y * o.x + m.m2.y * o.z) + m.m1.y * o.y,
                     (m.m0.z * o.x + m.m2.z * o.z) + m.m1.z * o.y};
        ics->matrix.m3 = {pos.x - r.x, pos.y - r.y, pos.z - r.z};
    }
    position = pos;
}

void TrailerJoint::setCosFreeLean() {
    cosFreeLean = std::cos(freeLean);
}

void TrailerJoint::setRotate1(const Mat34& m) {
    ics1->matrix = m;
}

void TrailerJoint::setRotate2(const Mat34& m) {
    ics2->matrix = m;
}

void TrailerJoint::setFrictionLean(float restore, float dampConst, float dampLinear) {
    frictionLean = {restore, dampConst, dampLinear};
    setJointForceFlag();
}

void TrailerJoint::setFrictionRoll(float restore, float dampConst, float dampLinear) {
    frictionRoll = {restore, dampConst, dampLinear};
    setJointForceFlag();
}

void TrailerJoint::setLeanLimit(float limit, float elasticity) {
    leanLimit = limit;
    leanElasticity = elasticity;
    setJointForceFlag();
}

void TrailerJoint::setRollLimit(float limit, float elasticity) {
    setRollLimit(-limit, limit, elasticity);
}

void TrailerJoint::setRollLimit(float negativeLimit, float positiveLimit, float elasticity) {
    negativeRollLimit = negativeLimit;
    positiveRollLimit = positiveLimit;
    rollElasticity = elasticity;
    setJointForceFlag();
}

void TrailerJoint::setRestOrientation() {
    Mat34 m = rotation3x3(ics1->matrix);
    age::dot3x3TransposeInPlace(m, ics2->matrix);
    setRestOrientMat(m);
}

void TrailerJoint::setRestOrientMat(const Mat34& m) {
    restOrient2 = rotation3x3(m);
    restOrient1 = Mat34::identity();
}

void TrailerJoint::setRestOrientMat(const Mat34& m1, const Mat34& m2) {
    restOrient1 = rotation3x3(m1);
    restOrient2 = rotation3x3(m2);
}

void TrailerJoint::setJointForceFlag() {
    const bool any = frictionLean.x != 0.0f || frictionLean.y != 0.0f || frictionLean.z != 0.0f ||
                     frictionRoll.x != 0.0f || frictionRoll.y != 0.0f || frictionRoll.z != 0.0f ||
                     leanLimit < kPi;
    if (any)
        status |= kForce;
    else
        status &= ~kForce;
}

void TrailerJoint::moveICS() {
    ics1->moveICS();
    ics2->moveICS();
}

void TrailerJoint::update(float dt, float invDt) {
    if (isBroken())
        return;
    // (The original also breaks the joint on Ctrl+B, a debug key.)
    InertialCS& a = *ics1;
    InertialCS& b = *ics2;
    const Mat34 r1 = rotation3x3(a.matrix);
    const Mat34 r2 = rotation3x3(b.matrix);
    const Mat34 r1t = age::transpose(r1);
    const Mat34 r2t = age::transpose(r2);

    // K = (C1 + C2)^-1 at the joint point.
    Mat34 c1, c2;
    a.calcCMatrix(c1, position);
    b.calcCMatrix(c2, position);
    const Mat34 k = age::inverse(age::add3x3(c1, c2));

    const Vec3 rel1 = sub(position, a.matrix.m3);
    const Vec3 rel2 = sub(position, b.matrix.m3);
    const Mat34 x1 = age::crossProdMatrix(rel1);
    const Mat34 x2 = age::crossProdMatrix(rel2);

    // World inertia tensors and their inverses.
    const Mat34 iw1 = age::dot3x3(age::dot3x3(r1t, diag(a.inertia)), r1);
    const Mat34 iw1Inv = age::inverse(iw1);
    const Mat34 iw2 = age::dot3x3(age::dot3x3(r2t, diag(b.inertia)), r2);
    const Mat34 iw2Inv = age::inverse(iw2);

    float leanAngle = 0.0f, leanAngleRate = 0.0f, rollAngle = 0.0f, rollAngleRate = 0.0f;
    Vec3 leanAxis{1.0f, 0.0f, 0.0f};
    Mat34 bm = r2;
    if (status & kForce) {
        bm = age::dot3x3(restOrient2, r2);
        const Mat34 am = age::dot3x3(restOrient1, r1);
        doJointTorque(am, age::transpose(am), bm, age::transpose(bm), leanAngle, leanAngleRate, leanAxis,
                      rollAngle, rollAngleRate);
    }

    // This sample's forces and torques so far (wheels, aero, the restoring
    // torque; gravity arrives with the next sample and accelerates both
    // joint points alike).
    const Vec3 f1 = a.getForce(invDt);
    const Vec3 t1 = a.getTorque(invDt);
    const Vec3 f2 = b.getForce(invDt);
    const Vec3 t2 = b.getTorque(invDt);

    // Free acceleration of the trailer's joint point: F/m + w x (w x r) +
    // alpha x r, alpha = Iw^-1 (T - w x Iw w).
    Vec3 acc;
    {
        const float m = b.invMass;
        const Vec3& w = b.angularVelocity;
        const Vec3& r = rel2;
        const Vec3 lin{f2.x * m, f2.y * m, f2.z * m};
        const Vec3 u{r.z * w.y - r.y * w.z, r.x * w.z - r.z * w.x, r.y * w.x - r.x * w.y};
        const Vec3 c{u.z * w.y - u.y * w.z, u.x * w.z - u.z * w.x, u.y * w.x - u.x * w.y};
        acc = {c.x + lin.x, lin.y + c.y, lin.z + c.z};
        const Mat34& i = iw2;
        const Vec3 l{(i.m2.x * w.z + i.m0.x * w.x) + i.m1.x * w.y,
                     (i.m2.y * w.z + i.m0.y * w.x) + i.m1.y * w.y,
                     (i.m2.z * w.z + i.m0.z * w.x) + i.m1.z * w.y};
        const Vec3 g{l.z * w.y - l.y * w.z, l.x * w.z - l.z * w.x, l.y * w.x - l.x * w.y};
        const Vec3 tau{t2.x - g.x, t2.y - g.y, t2.z - g.z};
        const Mat34& ii = iw2Inv;
        const Vec3 al{(tau.z * ii.m2.x + tau.y * ii.m1.x) + ii.m0.x * tau.x,
                      (tau.z * ii.m2.y + tau.y * ii.m1.y) + ii.m0.y * tau.x,
                      (tau.z * ii.m2.z + tau.y * ii.m1.z) + ii.m0.z * tau.x};
        const Mat34& x = x2;
        const Vec3 s{(al.z * x.m2.x + al.y * x.m1.x) + x.m0.x * al.x,
                     (al.z * x.m2.y + al.y * x.m1.y) + x.m0.y * al.x,
                     (al.z * x.m2.z + al.y * x.m1.z) + x.m0.z * al.x};
        acc = {s.x + acc.x, acc.y + s.y, acc.z + s.z};
    }
    // Less the tractor's.
    {
        const float m = a.invMass;
        acc = {acc.x - f1.x * m, acc.y - f1.y * m, acc.z - f1.z * m};
        const Vec3& w = a.angularVelocity;
        const Vec3& r = rel1;
        const Vec3 u{r.z * w.y - r.y * w.z, r.x * w.z - r.z * w.x, r.y * w.x - r.x * w.y};
        const Vec3 c{u.z * w.y - u.y * w.z, u.x * w.z - u.z * w.x, u.y * w.x - u.x * w.y};
        acc = {acc.x - c.x, acc.y - c.y, acc.z - c.z};
        const Mat34& i = iw1;
        const Vec3 l{(i.m1.x * w.y + i.m0.x * w.x) + i.m2.x * w.z,
                     (i.m0.y * w.x + i.m1.y * w.y) + i.m2.y * w.z,
                     (i.m0.z * w.x + i.m1.z * w.y) + i.m2.z * w.z};
        const Vec3 g{l.z * w.y - l.y * w.z, l.x * w.z - l.z * w.x, l.y * w.x - l.x * w.y};
        const Vec3 tau{t1.x - g.x, t1.y - g.y, t1.z - g.z};
        const Mat34& ii = iw1Inv;
        const Vec3 al{(tau.z * ii.m2.x + tau.y * ii.m1.x) + ii.m0.x * tau.x,
                      (tau.z * ii.m2.y + tau.y * ii.m1.y) + ii.m0.y * tau.x,
                      (tau.z * ii.m2.z + tau.y * ii.m1.z) + ii.m0.z * tau.x};
        const Mat34& x = x1;
        const Vec3 s{(al.z * x.m2.x + al.y * x.m1.x) + x.m0.x * al.x,
                     (al.z * x.m2.y + al.y * x.m1.y) + x.m0.y * al.x,
                     (al.z * x.m2.z + al.y * x.m1.z) + x.m0.z * al.x};
        acc = {acc.x - s.x, acc.y - s.y, acc.z - s.z};
    }
    // Plus a third of the relative velocity of the joint points per sample.
    {
        const Vec3 v1 = a.getVelocity(&position);
        const Vec3 v2 = b.getVelocity(&position);
        const float kv = invDt * 0.33333334f;
        const Vec3 corr{kv * (v2.x - v1.x), (v2.y - v1.y) * kv, (v2.z - v1.z) * kv};
        acc = {corr.x + acc.x, acc.y + corr.y, acc.z + corr.z};
    }
    Vec3 force{(acc.y * k.m1.x + acc.z * k.m2.x) + k.m0.x * acc.x,
               (acc.y * k.m1.y + acc.z * k.m2.y) + k.m0.y * acc.x,
               (acc.z * k.m2.z + k.m0.z * acc.x) + acc.y * k.m1.z};

    if (status & kForce) {
        bool limit = false;
        float leanErr = 0.0f;
        if (leanLimit <= leanAngle) {
            leanErr = (leanAngle - leanLimit) * 3.0f + leanAngleRate;
            if (0.0f < leanErr)
                limit = true;
            else
                leanErr = 0.0f;
        }
        float rollErr = 0.0f;
        if (positiveRollLimit < rollAngle) {
            rollErr = (rollAngle - positiveRollLimit) * 3.0f + rollAngleRate;
            if (!(0.0f < rollErr))
                rollErr = 0.0f;
            else
                limit = true;
        } else if (rollAngle < negativeRollLimit) {
            rollErr = (rollAngle - negativeRollLimit) * 3.0f + rollAngleRate;
            if (rollErr < 0.0f)
                limit = true;
            else
                rollErr = 0.0f;
        }
        if (limit)
            doJointLimits(leanErr, leanAxis, rollErr, bm.m2, force, k, iw1Inv, iw2Inv, x1, x2, dt, invDt);
    }

    if (0.0f < forceLimit) {
        const float l = forceLimit * kForceLimitUnit;
        if (l * l < (force.x * force.x + force.z * force.z) + force.y * force.y)
            breakJoint();
    }

    // Torques of the joint force about each centre of mass.
    {
        const Vec3 ta = cross(sub(position, a.matrix.m3), force);
        a.angularTorque = {a.angularTorque.x + ta.x, a.angularTorque.y + ta.y, a.angularTorque.z + ta.z};
        const Vec3 tb = cross(sub(b.matrix.m3, position), force);
        b.angularTorque = {b.angularTorque.x + tb.x, b.angularTorque.y + tb.y, b.angularTorque.z + tb.z};
    }

    // The linear force turns by half of the sample's mean rotation (and is
    // scaled by 2 sin(theta / 2) / theta).
    {
        const float h = dt * 0.5f;
        const Vec3& w1 = a.angularVelocity;
        const Vec3& w2 = b.angularVelocity;
        const Vec3 theta{h * (w1.x + w2.x), (w1.y + w2.y) * h, (w1.z + w2.z) * h};
        const float theta2 = (theta.x * theta.x + theta.z * theta.z) + theta.y * theta.y;
        if (static_cast<double>(theta2) > 1e-05) {
            const float m = std::sqrt(theta2);
            const float invM = 1.0f / m;
            const Vec3 n{theta.x * invM, theta.y * invM, theta.z * invM};
            const float halfAngle = m * 0.5f;
            // MM2 applies the rotation with Matrix34::RotateUnitAxis (this =
            // this * R) to a matrix that still holds the trailer's inverse
            // mass matrix C2 from the top of Update (MM1 built it with
            // RotateAbs). The part of the force perpendicular to the
            // rotation axis therefore comes out multiplied by C2, of the
            // order of 1e-3 / kg: while the pair turns faster than about
            // 0.2 rad/s, the joint transmits essentially only the force
            // along the turning axis, and the FreeRange correction below
            // keeps the hitch together. (mm2ForceRotation = false applies
            // the plain rotation instead.)
            Mat34 rot = age::makeRotateUnitAxis(n, halfAngle);
            if (mm2ForceRotation) {
                rot = c2;
                age::rotateUnitAxis(rot, n, halfAngle);
            }
            const float d = (n.x * force.x + n.z * force.z) + n.y * force.y;
            const Vec3 par{n.x * d, n.y * d, d * n.z};
            const Vec3 perp{force.x - par.x, force.y - par.y, force.z - par.z};
            const Vec3 turned{(rot.m0.x * perp.x + perp.z * rot.m2.x) + perp.y * rot.m1.x,
                              (rot.m0.y * perp.x + perp.z * rot.m2.y) + perp.y * rot.m1.y,
                              (rot.m0.z * perp.x + perp.z * rot.m2.z) + perp.y * rot.m1.z};
            float factor;
            if (theta2 < 1.0f) {
                factor = 1.0f - theta2 * 0.041666668f;
            } else {
                // fsin's wide result times 1/m, rounded once.
                const float s =
                    static_cast<float>(std::sin(static_cast<double>(halfAngle)) * static_cast<double>(invM));
                factor = s + s;
            }
            force = {factor * (turned.x + par.x), factor * (turned.y + par.y), factor * (par.z + turned.z)};
        }
    }
    jointForce = force;
    a.linearForce = {a.linearForce.x + force.x, a.linearForce.y + force.y, a.linearForce.z + force.z};
    b.linearForce = {b.linearForce.x - force.x, b.linearForce.y - force.y, b.linearForce.z - force.z};

    // The joint point follows the middle of the two hitch points; past
    // FreeRange both bodies move half the excess towards each other.
    const Vec3 p1 = hitchPoint(a.matrix, offset1);
    const Vec3 p2 = trailerHitchPoint(b.matrix, offset2);
    position = {(p2.x + p1.x) * 0.5f, (p2.y + p1.y) * 0.5f, (p2.z + p1.z) * 0.5f};
    gap = sub(p2, p1);
    lean = leanAngle;
    leanRate = leanAngleRate;
    roll = rollAngle;
    rollRate = rollAngleRate;
    const float gap2 = age::mag2(gap);
    if (gap2 <= freeRange * freeRange)
        return;
    const float len = std::sqrt(gap2);
    const float s = (len - freeRange) * 0.5f;
    const Vec3 move{gap.x / len * s, gap.y / len * s, gap.z / len * s};
    a.matrix.m3 = {a.matrix.m3.x + move.x, a.matrix.m3.y + move.y, a.matrix.m3.z + move.z};
    b.matrix.m3 = {b.matrix.m3.x - move.x, b.matrix.m3.y - move.y, b.matrix.m3.z - move.z};
}

void TrailerJoint::doJointTorque(const Mat34& a, const Mat34& at, const Mat34& b, const Mat34& bt,
                                 float& leanOut, float& leanRateOut, Vec3& axisOut, float& rollOut,
                                 float& rollRateOut) {
    // The trailer's joint axis (B's Z row) in the tractor's joint frame.
    const Vec3& bz = b.m2;
    const Vec3 bInA{(at.m2.x * bz.z + at.m1.x * bz.y) + bz.x * at.m0.x,
                    (at.m2.y * bz.z + at.m0.y * bz.x) + at.m1.y * bz.y,
                    (at.m2.z * bz.z + at.m0.z * bz.x) + at.m1.z * bz.y};
    // The lean axis in the tractor's frame. The original leaves it unset
    // when the lean is inside FreeLean; it is then only read by the roll
    // torque, which Update never enables (see below).
    Vec3 a0{1.0f, 0.0f, 0.0f};
    Vec3 t{};
    if (std::abs(bInA.z) < cosFreeLean) {
        if (!(bInA.z < 1.0f))
            leanOut = 0.0f;
        else if (!(bInA.z > -1.0f))
            leanOut = kPi;
        else
            leanOut = static_cast<float>(std::acos(static_cast<double>(bInA.z)));
        if (static_cast<double>(leanOut) > 1e-05) {
            a0 = {bInA.y, -bInA.x, 0.0f};
            const float s = age::invMag(a0);
            a0 = {s * a0.x, a0.y * s, a0.z * s};
        }
        axisOut = {(a0.z * a.m2.x + a0.y * a.m1.x) + a0.x * a.m0.x,
                   (a0.z * a.m2.y + a0.y * a.m1.y) + a0.x * a.m0.y,
                   (a0.z * a.m2.z + a0.y * a.m1.z) + a0.x * a.m0.z};
        const Vec3& axis = axisOut;

        const Vec3& w1 = ics1->angularVelocity;
        const Vec3& w2 = ics2->angularVelocity;
        const Vec3 dw{w2.x - w1.x, w2.y - w1.y, w2.z - w1.z};
        leanRateOut = -((dw.x * axis.x + dw.y * axis.y) + dw.z * axis.z);
        const float rr = -((dw.y * bz.y + dw.z * bz.z) + dw.x * bz.x);
        rollRateOut = rr;
        // The relative angular velocity less its part about the trailer's
        // joint axis.
        const Vec3 lv{rr * bz.x + dw.x, dw.y + rr * bz.y, dw.z + rr * bz.z};

        // Effective inertia of the pair about the hitch.
        const float m1 = std::sqrt((offset1.x * offset1.x + offset1.y * offset1.y) + offset1.z * offset1.z);
        const float m2 = std::sqrt((offset2.x * offset2.x + offset2.y * offset2.y) + offset2.z * offset2.z);
        const float l1 = m1 <= 0.0f ? 1.0f : m1;
        const float l2 = m2 <= 0.0f ? 1.0f : m2;
        const float eff = (l2 * l1) / (ics1->invMass * m2 + ics2->invMass * m1);
        const float kS = kJointSpring * eff;
        const float kC = eff * kJointConst;

        Vec3 lvn{};
        if (static_cast<double>((lv.x * lv.x + lv.z * lv.z) + lv.y * lv.y) > 1e-12) {
            const float s = age::invMag(lv);
            lvn = {s * lv.x, lv.y * s, lv.z * s};
        }
        const float cTerm = kC * frictionLean.y;
        const Vec3 c{lvn.x * cTerm, lvn.y * cTerm, lvn.z * cTerm};
        const float sTerm = (frictionLean.x * leanOut) * kS;
        t = {sTerm * axis.x - c.x, sTerm * axis.y - c.y, sTerm * axis.z - c.z};
        // MM2 also computes -(DampLinearLean * 2 sqrt(40/pi) * eff) * lv but
        // never adds it to the torque, so the lean has no linear damping.
    }

    // Roll about the trailer's joint axis. Update passes a roll of 0 and
    // FreeRoll is positive in every file, so in MM2 this never runs: the
    // relative roll is free and the roll limits never engage.
    if (freeRoll < std::abs(rollOut) &&
        (frictionRoll.x != 0.0f || frictionRoll.y != 0.0f || frictionRoll.z != 0.0f)) {
        const Vec3& axis = axisOut;
        const Vec3 av{(bt.m2.x * axis.z + bt.m1.x * axis.y) + bt.m0.x * axis.x,
                      (bt.m2.y * axis.z + bt.m1.y * axis.y) + bt.m0.y * axis.x,
                      (bt.m2.z * axis.z + bt.m1.z * axis.y) + bt.m0.z * axis.x};
        const Vec3 c{a0.z * bInA.y - a0.y * bInA.z, a0.x * bInA.z - a0.z * bInA.x,
                     a0.y * bInA.x - a0.x * bInA.y};
        const float num = (av.x * c.x + c.z * av.z) + c.y * av.y;
        const float den = (av.x * a0.x + av.z * a0.z) + av.y * a0.y;
        rollOut = static_cast<float>(std::atan2(static_cast<double>(num), static_cast<double>(den)));
        const float kInv = 1.0f / (ics2->invInertia.z + ics1->invInertia.z);
        const float kS2 = kInv * kJointSpring;
        const float kD2 = kInv * kJointDamp;
        const float kC2 = kInv * kJointConst;
        const float rr = rollRateOut;
        float sgn = 0.0f;
        if (static_cast<double>(rr) > 1e-05)
            sgn = 1.0f;
        else if (static_cast<double>(rr) < -1e-05)
            sgn = -1.0f;
        const float tr =
            (frictionRoll.z * rr * kD2 + frictionRoll.y * sgn * kC2) + rollOut * frictionRoll.x * kS2;
        t = {tr * bz.x + t.x, tr * bz.y + t.y, tr * bz.z + t.z};
    }
    ics1->applyTorque({-t.x, -t.y, -t.z});
    ics2->applyTorque(t);
}

void TrailerJoint::doJointLimits(float leanErr, const Vec3& leanAxis, float rollErr, const Vec3& rollAxis,
                                 Vec3& force, const Mat34& k, const Mat34& iw1Inv, const Mat34& iw2Inv,
                                 const Mat34& x1, const Mat34& x2, float dt, float invDt) {
    InertialCS& a = *ics1;
    InertialCS& b = *ics2;
    // S couples an angular impulse to the joint points; M is the matrix of
    // the bodies' relative angular response to an angular impulse.
    const Mat34 s = age::add3x3(age::dot3x3(iw2Inv, x2), age::dot3x3(iw1Inv, x1));
    const Mat34 sk = age::dot3x3(s, k);
    Mat34 m = age::dot3x3Transpose(sk, age::transpose(s));
    m = age::add3x3(age::add3x3(m, iw1Inv), iw2Inv);

    // The angular momentum each body gains this sample, the joint force's
    // included, and the resulting relative turn rate.
    const Vec3 j{dt * force.x, dt * force.y, dt * force.z};
    const Vec3 ta = a.getTorque(invDt);
    const Vec3 tb = b.getTorque(invDt);
    const Vec3 ca = cross(sub(position, a.matrix.m3), j);
    const Vec3 h1{ca.x + ta.x * dt, ta.y * dt + ca.y, ta.z * dt + ca.z};
    const Vec3 cb = cross(sub(position, b.matrix.m3), j);
    const Vec3 h2{tb.x * dt - cb.x, tb.y * dt - cb.y, tb.z * dt - cb.z};
    const Vec3 dw1{(h1.x * iw1Inv.m0.x + h1.z * iw1Inv.m2.x) + h1.y * iw1Inv.m1.x,
                   (h1.z * iw1Inv.m2.y + h1.y * iw1Inv.m1.y) + h1.x * iw1Inv.m0.y,
                   (h1.z * iw1Inv.m2.z + h1.y * iw1Inv.m1.z) + h1.x * iw1Inv.m0.z};
    const Vec3 dw2{(h2.x * iw2Inv.m0.x + h2.z * iw2Inv.m2.x) + h2.y * iw2Inv.m1.x,
                   (h2.z * iw2Inv.m2.y + h2.y * iw2Inv.m1.y) + h2.x * iw2Inv.m0.y,
                   (h2.z * iw2Inv.m2.z + h2.y * iw2Inv.m1.z) + h2.x * iw2Inv.m0.z};
    const Vec3 dw{dw1.x - dw2.x, dw1.y - dw2.y, dw1.z - dw2.z};

    Vec3 l{};
    if (leanErr != 0.0f) {
        const float d = (dw.z * leanAxis.z + dw.y * leanAxis.y) + dw.x * leanAxis.x;
        if (static_cast<double>(d) > 0.0)
            leanErr = d + leanErr;
        const Vec3 v{(m.m2.x * leanAxis.z + m.m1.x * leanAxis.y) + m.m0.x * leanAxis.x,
                     (m.m2.y * leanAxis.z + m.m1.y * leanAxis.y) + m.m0.y * leanAxis.x,
                     (m.m2.z * leanAxis.z + m.m1.z * leanAxis.y) + m.m0.z * leanAxis.x};
        float q = dotZYX(leanAxis, v);
        if (q != 0.0f)
            q = leanErr / q;
        const float f = (leanElasticity + 1.0f) * q;
        l = {f * leanAxis.x, f * leanAxis.y, f * leanAxis.z};
    }
    if (rollErr != 0.0f) {
        const float p = dotZYX(dw, rollAxis);
        if ((rollErr > 0.0f && p > 0.0f) || (!(rollErr > 0.0f) && p < 0.0f))
            rollErr = p + rollErr;
        float q = dotZYX(rollAxis, mulRow(rollAxis, m));
        if (q != 0.0f)
            q = rollErr / q;
        const float f = (rollElasticity + 1.0f) * q;
        l = {l.x + f * rollAxis.x, l.y + f * rollAxis.y, l.z + f * rollAxis.z};
    }
    a.applyAngImpulse({-l.x, -l.y, -l.z});
    b.applyAngImpulse(l);
    // The joint force changes with the impulse.
    Vec3 li = mulRow(l, s);
    li = {li.x * invDt, li.y * invDt, li.z * invDt};
    const Vec3 extra = mulRow(li, k);
    force = {force.x + extra.x, force.y + extra.y, force.z + extra.z};
}

namespace {

// How an impulse at the joint (lever `xj`) moves the point `pos` of `ics`:
// Xj^T R^T diag(InvInertia) R Xp + InvMass * I.
Mat34 jointCoupling(const InertialCS& ics, const Mat34& xj, const Vec3& pos) {
    const Mat34 xp = age::crossProdMatrix(sub(pos, ics.matrix.m3));
    Mat34 d = age::dot3x3(rotation3x3(ics.matrix), xp);
    const Vec3& ii = ics.invInertia;
    d.m0 = {ii.x * d.m0.x, d.m0.y * ii.x, d.m0.z * ii.x};
    d.m1 = {ii.y * d.m1.x, ii.y * d.m1.y, d.m1.z * ii.y};
    d.m2 = {ii.z * d.m2.x, d.m2.y * ii.z, ii.z * d.m2.z};
    const Mat34 t = age::dot3x3(age::transpose(rotation3x3(ics.matrix)), d);
    Mat34 c = age::dot3x3(age::transpose(xj), t);
    c.m0.x = ics.invMass + c.m0.x;
    c.m1.y = ics.invMass + c.m1.y;
    c.m2.z = ics.invMass + c.m2.z;
    c.m3 = {};
    return c;
}

// out -= S^T K S.
void subtractJointPart(Mat34& out, const Mat34& k, const Mat34& s) {
    const Mat34 ks = age::dot3x3(k, s);
    const Mat34 corr = age::dot3x3(age::transpose(s), ks);
    out.m0 = {out.m0.x - corr.m0.x, out.m0.y - corr.m0.y, out.m0.z - corr.m0.z};
    out.m1 = {out.m1.x - corr.m1.x, out.m1.y - corr.m1.y, out.m1.z - corr.m1.z};
    out.m2 = {out.m2.x - corr.m2.x, out.m2.y - corr.m2.y, out.m2.z - corr.m2.z};
    out.m3 = {};
}

} // namespace

void TrailerJoint::computeInvMassMatrix(const InertialCS* ics, Mat34& out, const Vec3& pos) const {
    ics->calcCMatrix(out, pos);
    Mat34 c1, c2;
    ics1->calcCMatrix(c1, position);
    ics2->calcCMatrix(c2, position);
    const Mat34 k = age::inverse(age::add3x3(c1, c2));
    const Mat34 xj = age::crossProdMatrix(sub(position, ics->matrix.m3));
    subtractJointPart(out, k, jointCoupling(*ics, xj, pos));
}

void TrailerJoint::computeInvMassMatrix(const InertialCS* a, const InertialCS* b, Mat34& out,
                                        const Vec3& pos) const {
    a->calcCMatrix(out, pos);
    Mat34 cb;
    b->calcCMatrix(cb, pos);
    out = age::add3x3(cb, out);
    out.m3 = {};
    Mat34 c1, c2;
    a->calcCMatrix(c1, position);
    b->calcCMatrix(c2, position);
    const Mat34 k = age::inverse(age::add3x3(c1, c2));
    const Mat34 xj = age::crossProdMatrix(sub(position, a->matrix.m3));
    Mat34 s = age::add3x3(jointCoupling(*b, xj, pos), jointCoupling(*a, xj, pos));
    s.m3 = {};
    subtractJointPart(out, k, s);
}

} // namespace mm2::phys
