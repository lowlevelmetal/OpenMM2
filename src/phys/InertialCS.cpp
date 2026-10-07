// Port of asInertialCS from Open1560 (https://github.com/0x1F9F1/Open1560),
// GPL-3.0: code/midtown/game.asm, Midtown Madness 1 beta build 1560.
// Operation order and float32 arithmetic follow the original.

#include "phys/InertialCS.h"

#include "phys/AgeMath.h"
#include "phys/Joint3Dof.h"

namespace mm2::phys {

InertialCS::InertialCS() {
    // asInertialCS::asInertialCS: SetDensity(1, 1, 1, 1) then Zero().
    setMass(1.0f, 1.0f, 1.0f, 1000.0f);
    zero();
}

void InertialCS::setMass(float sizeX, float sizeY, float sizeZ, float m) {
    size = {sizeX, sizeY, sizeZ};
    mass = m;
    invMass = 1.0f / m;
    const float x2 = sizeX * sizeX, y2 = sizeY * sizeY, z2 = sizeZ * sizeZ;
    const float k = m / 12.0f;
    inertia = {(y2 + z2) * k, (x2 + z2) * k, (x2 + y2) * k};
    invInertia = {1.0f / inertia.x, 1.0f / inertia.y, 1.0f / inertia.z};
}

void InertialCS::clearAccumulators() {
    linearForce = {};
    angularTorque = {};
    linearImpulse = {};
    angularImpulse = {};
    linearPush = {};
    framePush = {};
    turnForce = {};
}

void InertialCS::zero() {
    matrix = Mat34::identity();
    linearMomentum = {};
    angularMomentum = {};
    linearVelocity = {};
    angularVelocity = {};
    clearAccumulators();
    numImpulses = 0;
    if (state != Off)
        state = Awake;
    counter = 0.0f;
}

void InertialCS::place(const Mat34& m) {
    zero();
    matrix = m;
    frameVelocity = {};
}

void InertialCS::update(float dt, float invDt) {
    if (constraints == kConstrainAll) {
        clearAccumulators();
        return;
    }
    if (constraints & kConstrainLink)
        return;
    finishForces(dt, invDt);
    finishUpdate(dt);
}

void InertialCS::finishForces(float dt, float invDt) {
    if (numImpulses != 0) {
        const float inv = 1.0f / static_cast<float>(numImpulses);
        linearImpulse = {linearImpulse.x * inv, inv * linearImpulse.y, inv * linearImpulse.z};
        angularImpulse = {angularImpulse.x * inv, inv * angularImpulse.y, inv * angularImpulse.z};
        numImpulses = 0;
    }

    // FrameVelocity = (LinearPush + FramePush) / dt + LinearVelocity.
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

    applyForce({gravity.x * mass, gravity.y * mass, gravity.z * mass});
    if (constraints & kConstrainAll)
        doConstrain();
}

void InertialCS::finishUpdate(float dt) {
    if (state == Asleep || (constraints & kConstrainZeroDof))
        return;

    Vec3& p = linearMomentum;
    Vec3& L = angularMomentum;
    matrix.m3 = {linearPush.x + matrix.m3.x, linearPush.y + matrix.m3.y, linearPush.z + matrix.m3.z};
    p = {linearImpulse.x + p.x, linearImpulse.y + p.y, linearImpulse.z + p.z};
    L = {L.x + angularImpulse.x, angularImpulse.y + L.y, angularImpulse.z + L.z};

    const Vec3 fdt{linearForce.x * dt, linearForce.y * dt, linearForce.z * dt};
    p = {fdt.x + p.x, fdt.y + p.y, fdt.z + p.z};
    linearVelocity = {invMass * p.x, p.y * invMass, p.z * invMass};
    const Vec3 vdt{linearVelocity.x * dt, linearVelocity.y * dt, linearVelocity.z * dt};
    matrix.m3 = {vdt.x + matrix.m3.x, vdt.y + matrix.m3.y, vdt.z + matrix.m3.z};

    const Vec3 tdt{angularTorque.x * dt, angularTorque.y * dt, angularTorque.z * dt};
    L = {tdt.x + L.x, tdt.y + L.y, tdt.z + L.z};

    // Angular momentum in body space.
    const Mat34& m = matrix;
    float lx = (L.y * m.m0.y + L.z * m.m0.z) + m.m0.x * L.x;
    float ly = (L.y * m.m1.y + m.m1.z * L.z) + m.m1.x * L.x;
    float lz = (L.y * m.m2.y + L.z * m.m2.z) + m.m2.x * L.x;

    if (limitAngVelocity) {
        float scale = 1.0f;
        const float wx = invInertia.x * lx;
        if (wx > maxAngVelocity)
            scale = maxAngVelocity / wx;
        else if (wx < -maxAngVelocity)
            scale = -(maxAngVelocity / wx);
        const float wyU = invInertia.y * ly;
        const float wy = scale * wyU;
        if (wy > maxAngVelocity)
            scale = maxAngVelocity / wyU;
        else if (wy < -maxAngVelocity)
            scale = -(maxAngVelocity / wyU);
        const float wzU = invInertia.z * lz;
        const float wz = scale * wzU;
        if (wz > maxAngVelocity)
            scale = maxAngVelocity / wzU;
        else if (wz < -maxAngVelocity)
            scale = -(maxAngVelocity / wzU);
        lx = lx * scale;
        ly = scale * ly;
        lz = scale * lz;
        L = {L.x * scale, scale * L.y, scale * L.z};
    }

    // World angular velocity: sum over body axes of InvInertia_i * (row_i * l_i).
    Vec3 w{invInertia.x * (m.m0.x * lx), invInertia.x * (m.m0.y * lx), invInertia.x * (m.m0.z * lx)};
    const Vec3 b{invInertia.y * (m.m1.x * ly), invInertia.y * (m.m1.y * ly), invInertia.y * (m.m1.z * ly)};
    w = {b.x + w.x, b.y + w.y, b.z + w.z};
    const Vec3 c{invInertia.z * (m.m2.x * lz), invInertia.z * (m.m2.y * lz), invInertia.z * (m.m2.z * lz)};
    w = {c.x + w.x, c.y + w.y, c.z + w.z};
    angularVelocity = w;

    const Vec3 rot{w.x * dt + turnForce.x, turnForce.y + w.y * dt, turnForce.z + w.z * dt};
    const float a2 = (rot.y * rot.y + rot.z * rot.z) + rot.x * rot.x;
    if (a2 != 0.0f) {
        const float angle = age::invSqrtFast(a2) * a2;
        const float inv = 1.0f / angle;
        age::rotate(matrix, {inv * rot.x, inv * rot.y, inv * rot.z}, angle);
        // OpenMM2 guard (not in the original): re-orthonormalise only if the
        // basis has drifted measurably.
        const float d = matrix.m0.mag2() - 1.0f;
        if (d > 1e-3f || d < -1e-3f)
            matrix.normalize();
    }
    clearAccumulators();
}

void InertialCS::moveICS() {
    if (numImpulses == 0)
        return;
    matrix.m3 = {linearPush.x + matrix.m3.x, linearPush.y + matrix.m3.y, linearPush.z + matrix.m3.z};
    framePush = {framePush.x + linearPush.x, linearPush.y + framePush.y, linearPush.z + framePush.z};
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

void InertialCS::applyPush(const Vec3& push) {
    const float d = (linearPush.x * push.x + push.z * linearPush.z) + linearPush.y * push.y;
    if (d < 0.0f) {
        linearPush = {linearPush.x + push.x, linearPush.y + push.y, push.z + linearPush.z};
        return;
    }
    const float p2 = (push.y * push.y + push.z * push.z) + push.x * push.x;
    if (p2 <= 0.0f)
        return;
    const float k = d / p2;
    // Part of the new push already covered by the pending one.
    const Vec3 delta{k * push.x - push.x, k * push.y - push.y, k * push.z - push.z};
    const float s = (delta.z * push.z + delta.y * push.y) + delta.x * push.x;
    if (s < 0.0f)
        linearPush = {linearPush.x - delta.x, linearPush.y - delta.y, linearPush.z - delta.z};
}

Vec3 InertialCS::getVelocity(const Vec3* pos) const {
    if (!pos)
        return linearVelocity;
    const Vec3 r{pos->x - matrix.m3.x, pos->y - matrix.m3.y, pos->z - matrix.m3.z};
    const Vec3& w = angularVelocity;
    return {(w.y * r.z - w.z * r.y) + linearVelocity.x, (w.z * r.x - w.x * r.z) + linearVelocity.y,
            (w.x * r.y - w.y * r.x) + linearVelocity.z};
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

void InertialCS::getCMatrix(Mat34& out, const Vec3& pos) const {
    if (joint && (constraints & kConstrainLink) && !joint->isBroken())
        joint->getCMatrix(this, out, pos);
    else
        calcCMatrix(out, pos);
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
