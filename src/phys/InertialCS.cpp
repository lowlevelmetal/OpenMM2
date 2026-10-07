// phInertialCS (Midtown Madness 2): Update, ApplyContactForce, CalcNetPush,
// CalcNetTurn, MoveICS, GetLocalFilteredVelocity2, GetLocalAcceleration,
// GetForce, GetTorque, GetInertiaMatrix, GetInvMassMatrix, InitBoxMass,
// verified against the build 3393 code (MM2Recomp). The sleep test and
// constraints come from MM1's asInertialCS (Open1560,
// https://github.com/0x1F9F1/Open1560, GPL-3.0, code/midtown/game.asm).
// Operation order and float32 arithmetic follow the originals.

#include "phys/InertialCS.h"

#include "phys/AgeMath.h"

#include <algorithm>
#include <cmath>

namespace mm2::phys {
namespace {

Mat34 scaled3x3(const Mat34& m, float s) {
    Mat34 r;
    r.m0 = {m.m0.x * s, m.m0.y * s, m.m0.z * s};
    r.m1 = {m.m1.x * s, m.m1.y * s, m.m1.z * s};
    r.m2 = {m.m2.x * s, m.m2.y * s, m.m2.z * s};
    r.m3 = {};
    return r;
}

// v * A^T (Vector3::Dot3x3Transpose): component i is v . A.row(i).
Vec3 dotTranspose(const Vec3& v, const Mat34& a) {
    return {v.x * a.m0.x + v.y * a.m0.y + v.z * a.m0.z, v.x * a.m1.x + v.y * a.m1.y + v.z * a.m1.z,
            v.x * a.m2.x + v.y * a.m2.y + v.z * a.m2.z};
}

// Solves x * B = b for the symmetric 3x3 B (Matrix34::SolveSVD). Gaussian
// elimination with partial pivoting; a direction with no stiffness at all
// (pivot ~0) gets no change, which is what the original's singular value
// cut-off produces (OpenMM2: the cut-off value itself is not ported).
Vec3 solve3x3(const Mat34& b, const Vec3& rhs) {
    float a[3][4] = {{b.m0.x, b.m1.x, b.m2.x, rhs.x}, {b.m0.y, b.m1.y, b.m2.y, rhs.y}, {b.m0.z, b.m1.z, b.m2.z, rhs.z}};
    float scale = 0.0f;
    for (auto& row : a)
        for (int c = 0; c < 3; ++c)
            scale = std::max(scale, std::abs(row[c]));
    const float eps = scale * 1e-6f;
    int pivotCol[3] = {-1, -1, -1};
    bool used[3] = {};
    for (int col = 0; col < 3; ++col) {
        int best = -1;
        float bestAbs = eps;
        for (int r = 0; r < 3; ++r)
            if (!used[r] && std::abs(a[r][col]) > bestAbs) {
                best = r;
                bestAbs = std::abs(a[r][col]);
            }
        if (best < 0)
            continue;
        used[best] = true;
        pivotCol[col] = best;
        for (int r = 0; r < 3; ++r) {
            if (r == best)
                continue;
            const float f = a[r][col] / a[best][col];
            for (int c = col; c < 4; ++c)
                a[r][c] -= f * a[best][c];
        }
    }
    float x[3] = {};
    for (int col = 0; col < 3; ++col)
        if (pivotCol[col] >= 0)
            x[col] = a[pivotCol[col]][3] / a[pivotCol[col]][col];
    return {x[0], x[1], x[2]};
}

} // namespace

InertialCS::InertialCS() {
    setMass(1.0f, 1.0f, 1.0f, 1000.0f);
    zero();
}

void InertialCS::setMass(float sizeX, float sizeY, float sizeZ, float m) {
    // phInertialCS::InitBoxMass, then Init's reciprocals (FLT_MAX for <= 0).
    size = {sizeX, sizeY, sizeZ};
    constexpr float k = 0.083333336f;
    mass = m;
    invMass = m <= 0.0f ? FLT_MAX : 1.0f / m;
    inertia = {(sizeZ * sizeZ + sizeY * sizeY) * m * k, (sizeZ * sizeZ + sizeX * sizeX) * m * k,
               (sizeY * sizeY + sizeX * sizeX) * m * k};
    invInertia = {inertia.x <= 0.0f ? FLT_MAX : 1.0f / inertia.x, inertia.y <= 0.0f ? FLT_MAX : 1.0f / inertia.y,
                  inertia.z <= 0.0f ? FLT_MAX : 1.0f / inertia.z};
}

void InertialCS::clearAccumulators() {
    linearForce = {};
    angularTorque = {};
    contactForce = {};
    contactTorque = {};
    linearImpulse = {};
    angularImpulse = {};
    linearPush = {};
    turnForce = {};
    implicitContact = false;
    for (Mat34* m : {&contactK, &contactXK, &contactXKX})
        *m = Mat34{{}, {}, {}, {}};
}

void InertialCS::zero() {
    // phInertialCS::Zero: Identity, Freeze (motion and ZeroForces), LastPush 0.
    matrix = Mat34::identity();
    linearMomentum = {};
    angularMomentum = {};
    linearVelocity = {};
    angularVelocity = {};
    frameVelocity = {};
    clearAccumulators();
    framePush = {};
    lastPush = {};
    numImpulses = 0;
    if (state != Off)
        state = Awake;
    counter = 0.0f;
}

void InertialCS::place(const Mat34& m) {
    zero();
    matrix = m;
}

void InertialCS::update(float dt, float invDt) {
    if (constraints == kConstrainAll) {
        clearAccumulators();
        return;
    }
    finishForces(dt, invDt);
    finishUpdate(dt);
}

void InertialCS::finishForces(float dt, float invDt) {
    // phInertialCS adds impulses to momentum whole (MM1 averaged them).
    numImpulses = 0;
    // MM1 FrameVelocity (kept for the game layer): this sample's pushes as a
    // velocity on top of LinearVelocity.
    Vec3 fv = linearPush;
    fv = {framePush.x + fv.x, framePush.y + fv.y, framePush.z + fv.z};
    fv = {fv.x * invDt, invDt * fv.y, invDt * fv.z};
    fv = {fv.x + linearVelocity.x, linearVelocity.y + fv.y, linearVelocity.z + fv.z};
    frameVelocity = fv;

    if (state == Asleep || (constraints & kConstrainZeroDof)) {
        clearAccumulators();
        return;
    }
    if (state == Awake) {
        // MM1 asInertialCS sleep test (MM2 moved sleeping to phSleep).
        bool still = false;
        const float fv2 = (fv.y * fv.y + fv.z * fv.z) + fv.x * fv.x;
        if (fv2 < vel2) {
            const Vec3 ji{invMass * linearImpulse.x, invMass * linearImpulse.y, invMass * linearImpulse.z};
            const float ji2 = (ji.y * ji.y + ji.z * ji.z) + ji.x * ji.x;
            if (ji2 < vel2) {
                const Vec3& w = angularVelocity;
                const float w2 = (w.y * w.y + w.z * w.z) + w.x * w.x;
                if (w2 < angVel2) {
                    still = true;
                    counter = dt + counter;
                    if (counter > time) {
                        state = Asleep;
                        counter = 0.0f;
                        forceLimit2 = FLT_MAX;
                        impulseLimit2 = (mass * mass) * vel2;
                        linearVelocity = {};
                        angularVelocity = {};
                    }
                }
            }
        }
        if (!still)
            counter = 0.0f;
    }
    // dgPhysEntity::Update: Mass * gravity added before the integration.
    applyForce({gravity.x * mass, gravity.y * mass, gravity.z * mass});
    // phInertialCS::Update(): the contact accumulators join this sample's
    // forces.
    linearForce = {linearForce.x + contactForce.x, linearForce.y + contactForce.y, linearForce.z + contactForce.z};
    angularTorque = {angularTorque.x + contactTorque.x, angularTorque.y + contactTorque.y,
                     angularTorque.z + contactTorque.z};
    contactForce = {};
    contactTorque = {};
    if (constraints & kConstrainAll)
        doConstrain();
}

void InertialCS::integrateExplicit(float h) {
    Vec3& p = linearMomentum;
    Vec3& L = angularMomentum;
    p = {p.x + linearImpulse.x, p.y + linearImpulse.y, p.z + linearImpulse.z};
    p = {h * linearForce.x + p.x, h * linearForce.y + p.y, h * linearForce.z + p.z};
    linearVelocity = {invMass * p.x, invMass * p.y, invMass * p.z};
    L = {angularImpulse.x + L.x, angularImpulse.y + L.y, angularImpulse.z + L.z};
    L = {h * angularTorque.x + L.x, h * angularTorque.y + L.y, h * angularTorque.z + L.z};

    // Angular velocity about the body axes, each limited to MaxAngVelocity.
    const Mat34& m = matrix;
    float wx = (m.m0.x * L.x + m.m0.y * L.y + m.m0.z * L.z) * invInertia.x;
    float wy = (m.m1.x * L.x + m.m1.y * L.y + m.m1.z * L.z) * invInertia.y;
    float wz = (m.m2.x * L.x + m.m2.y * L.y + m.m2.z * L.z) * invInertia.z;
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
    // Backward Euler in the contact stiffness: the linear part through
    // M = (I + h/m K)^-1, the angular part by solving the 3x3 system B dw = rhs.
    const float k = h * invMass;
    Mat34 mInv = scaled3x3(contactK, k);
    mInv.m0.x = mInv.m0.x + 1.0f;
    mInv.m1.y = mInv.m1.y + 1.0f;
    mInv.m2.z = mInv.m2.z + 1.0f;
    mInv = age::inverse(mInv);
    mInv.m3 = {};

    Mat34 b = age::dot3x3(contactXK, mInv);
    b = age::dot3x3(b, age::transpose(contactXK));
    b = scaled3x3(b, -(k * h));
    b = age::add3x3(b, scaled3x3(contactXKX, h));
    const Mat34 inertiaWorld = worldInertia();
    b = age::add3x3(b, inertiaWorld);

    Vec3 pv = linearImpulse;
    pv = {h * linearForce.x + pv.x, h * linearForce.y + pv.y, h * linearForce.z + pv.z};
    Vec3 rhs{pv.x * k, pv.y * k, pv.z * k};
    rhs = mInv.transformDir(rhs);
    rhs = dotTranspose(rhs, contactXK);
    rhs = {rhs.x + angularImpulse.x, rhs.y + angularImpulse.y, rhs.z + angularImpulse.z};
    rhs = {h * angularTorque.x + rhs.x, h * angularTorque.y + rhs.y, h * angularTorque.z + rhs.z};
    const Vec3 dw = solve3x3(b, rhs);

    Vec3 u{dw.x * h, dw.y * h, dw.z * h};
    u = contactXK.transformDir(u);
    u = {u.x + pv.x, u.y + pv.y, u.z + pv.z};
    u = {u.x * invMass, u.y * invMass, u.z * invMass};
    u = mInv.transformDir(u);
    linearVelocity = {linearVelocity.x + u.x, linearVelocity.y + u.y, linearVelocity.z + u.z};
    linearMomentum = {linearVelocity.x * mass, linearVelocity.y * mass, linearVelocity.z * mass};
    angularVelocity = {angularVelocity.x + dw.x, angularVelocity.y + dw.y, angularVelocity.z + dw.z};
    angularMomentum = inertiaWorld.transformDir(angularVelocity);
}

void InertialCS::finishUpdate(float h) {
    if (state == Asleep || (constraints & kConstrainZeroDof))
        return;
    // phInertialCS::Update(float): implicit with this sample's contacts,
    // explicit otherwise. MM2 integrates bodies linked by a dgTrailerJoint
    // the same way: the joint only adds forces and torques.
    if (implicitContact)
        integrateImplicit(h);
    else
        integrateExplicit(h);

    // Speed limit, then position and rotation.
    const float v2 = linearVelocity.mag2();
    if (maxSpeed * maxSpeed < v2) {
        const float s = (v2 == 0.0f ? 0.0f : 1.0f / std::sqrt(v2)) * maxSpeed;
        linearVelocity = linearVelocity * s;
        linearMomentum = linearMomentum * s;
    }
    matrix.m3 = {matrix.m3.x + linearPush.x, matrix.m3.y + linearPush.y, matrix.m3.z + linearPush.z};
    matrix.m3 = {h * linearVelocity.x + matrix.m3.x, h * linearVelocity.y + matrix.m3.y,
                 h * linearVelocity.z + matrix.m3.z};
    Vec3 turn = turnForce;
    turn = {h * angularVelocity.x + turn.x, h * angularVelocity.y + turn.y, h * angularVelocity.z + turn.z};
    const float a2 = turn.mag2();
    if (1e-15f < a2) {
        const float angle = std::sqrt(a2);
        const float inv = 1.0f / angle;
        const Mat34 r = age::arbitraryRotation({turn.x * inv, turn.y * inv, turn.z * inv}, angle);
        matrix.m0 = r.transformDir(matrix.m0);
        matrix.m1 = r.transformDir(matrix.m1);
        matrix.m2 = r.transformDir(matrix.m2);
        // OpenMM2 guard (not in the original): re-orthonormalise only if the
        // basis has drifted measurably.
        const float d = matrix.m0.mag2() - 1.0f;
        if (d > 1e-3f || d < -1e-3f)
            matrix.normalize();
    }
    framePush = {framePush.x + linearPush.x, framePush.y + linearPush.y, framePush.z + linearPush.z};
    clearAccumulators();
    lastPush = framePush;
    framePush = {};
}

void InertialCS::moveICS() {
    // phInertialCS::MoveICS.
    matrix.m3 = {linearPush.x + matrix.m3.x, linearPush.y + matrix.m3.y, linearPush.z + matrix.m3.z};
    framePush = {linearPush.x + framePush.x, linearPush.y + framePush.y, linearPush.z + framePush.z};
    linearPush = {};
}

void InertialCS::doConstrain() {
    // Simplified: zero the constrained world-axis components of velocity and
    // momentum (the original projects through the constraint frame).
    if (constraints & kConstrainTX) {
        linearMomentum.x = linearVelocity.x = linearForce.x = 0.0f;
    }
    if (constraints & kConstrainTY) {
        linearMomentum.y = linearVelocity.y = linearForce.y = 0.0f;
    }
    if (constraints & kConstrainTZ) {
        linearMomentum.z = linearVelocity.z = linearForce.z = 0.0f;
    }
    if (constraints & kConstrainRX) {
        angularMomentum.x = angularVelocity.x = angularTorque.x = 0.0f;
    }
    if (constraints & kConstrainRY) {
        angularMomentum.y = angularVelocity.y = angularTorque.y = 0.0f;
    }
    if (constraints & kConstrainRZ) {
        angularMomentum.z = angularVelocity.z = angularTorque.z = 0.0f;
    }
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
// phInertialCS::CalcNetPush / CalcNetTurn.
void netPush(Vec3& pending, const Vec3& push) {
    const float d = (pending.x * push.x + pending.z * push.z) + pending.y * push.y;
    if (d < 0.0f) {
        pending = {push.x + pending.x, pending.y + push.y, pending.z + push.z};
        return;
    }
    const float p2 = (push.z * push.z + push.y * push.y) + push.x * push.x;
    if (!(0.0f < p2))
        return;
    const float k = d / p2;
    // Part of the new push not already covered by the pending one.
    const Vec3 delta{k * push.x - push.x, k * push.y - push.y, k * push.z - push.z};
    if ((delta.x * push.x + delta.y * push.y) + delta.z * push.z < 0.0f)
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
    implicitContact = true;
    contactForce = {f.x + contactForce.x, f.y + contactForce.y, contactForce.z + f.z};
    const Vec3 r{pos.x - matrix.m3.x, pos.y - matrix.m3.y, pos.z - matrix.m3.z};
    contactTorque = {(r.y * f.z - r.z * f.y) + contactTorque.x, (r.z * f.x - r.x * f.z) + contactTorque.y,
                     (r.x * f.y - r.y * f.x) + contactTorque.z};
    contactK = age::add3x3(contactK, k);
    const Mat34 xk = age::dot3x3(age::crossProdMatrix(r), k);
    contactXK = age::add3x3(contactXK, xk);
    // Matrix34::Dot3x3CrossProdTranspose: each row a becomes r x a.
    Mat34 xkx;
    xkx.m0 = r.cross(xk.m0);
    xkx.m1 = r.cross(xk.m1);
    xkx.m2 = r.cross(xk.m2);
    contactXKX = age::add3x3(contactXKX, xkx);
}

Vec3 InertialCS::getVelocity(const Vec3* pos) const {
    if (!pos)
        return linearVelocity;
    const Vec3 r{pos->x - matrix.m3.x, pos->y - matrix.m3.y, pos->z - matrix.m3.z};
    const Vec3& w = angularVelocity;
    return {(r.z * w.y - w.z * r.y) + linearVelocity.x, (w.z * r.x - r.z * w.x) + linearVelocity.y,
            (w.x * r.y - w.y * r.x) + linearVelocity.z};
}

Vec3 InertialCS::localAcceleration(const Vec3& pos) const {
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
    const float bx = (tau.x * m.m0.x + tau.y * m.m0.y + tau.z * m.m0.z) * invInertia.x;
    const float by = (tau.x * m.m1.x + tau.y * m.m1.y + tau.z * m.m1.z) * invInertia.y;
    const float bz = (tau.x * m.m2.x + tau.y * m.m2.y + tau.z * m.m2.z) * invInertia.z;
    const Vec3 alpha{bz * m.m2.x + by * m.m1.x + bx * m.m0.x, bz * m.m2.y + by * m.m1.y + bx * m.m0.y,
                     bz * m.m2.z + by * m.m1.z + bx * m.m0.z};
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
    Vec3 v = getVelocity(&pos);
    const float p2 = lastPush.mag2();
    if (0.0001f < p2) {
        const float a = (lastPush.x * v.x + lastPush.y * v.y + lastPush.z * v.z) * invDt;
        const float b = invDt * invDt * p2;
        float k = invDt;
        if (-a <= b)
            k = -(a / b) * invDt;
        v = {k * lastPush.x + v.x, k * lastPush.y + v.y, k * lastPush.z + v.z};
    }
    return v;
}

Mat34 InertialCS::worldInertia() const {
    const Mat34& m = matrix;
    Mat34 t;
    t.m0 = {inertia.x * m.m0.x, m.m1.x * inertia.y, m.m2.x * inertia.z};
    t.m1 = {m.m0.y * inertia.x, m.m1.y * inertia.y, m.m2.y * inertia.z};
    t.m2 = {m.m0.z * inertia.x, m.m1.z * inertia.y, m.m2.z * inertia.z};
    return age::dot3x3(t, m);
}

void InertialCS::calcCMatrix(Mat34& out, const Vec3& pos) const {
    const Vec3 r{pos.x - matrix.m3.x, pos.y - matrix.m3.y, pos.z - matrix.m3.z};
    const Mat34 x = age::crossProdMatrix(r);
    const Mat34 m1 = age::dot3x3(matrix, x);
    Mat34 d;
    d.m0 = {invInertia.x * m1.m0.x, invInertia.x * m1.m0.y, invInertia.x * m1.m0.z};
    d.m1 = {invInertia.y * m1.m1.x, invInertia.y * m1.m1.y, invInertia.y * m1.m1.z};
    d.m2 = {invInertia.z * m1.m2.x, invInertia.z * m1.m2.y, invInertia.z * m1.m2.z};
    out = age::dot3x3(age::transpose(m1), d);
    out.m0.x = out.m0.x + invMass;
    out.m1.y = out.m1.y + invMass;
    out.m2.z = invMass + out.m2.z;
    out.m3 = {};
}

Vec3 InertialCS::invInertiaWorld(const Vec3& v) const {
    return matrix.transformDir(matrix.untransformDir(v).mul(invInertia));
}

float InertialCS::effectiveMass(const Vec3& dir, const Vec3& worldPos) const {
    const Vec3 r = worldPos - matrix.m3;
    const float k = invMass + dir.dot(invInertiaWorld(r.cross(dir)).cross(r));
    return k > 0.0f ? 1.0f / k : 0.0f;
}

void InertialCS::refreshVelocities() {
    linearVelocity = linearMomentum * invMass;
    angularVelocity = invInertiaWorld(angularMomentum);
}

void InertialCS::applyImpulseNow(const Vec3& j, const Vec3& pos) {
    linearMomentum += j;
    angularMomentum += (pos - matrix.m3).cross(j);
    refreshVelocities();
    if (state == Asleep)
        state = Awake;
}

} // namespace mm2::phys
