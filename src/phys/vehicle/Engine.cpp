// vehEngine from Midtown Madness 2 (ComputeConstants, CalcTorqueAtFullThrottle,
// CalcTorqueAtZeroThrottle, CalcTorque, Update), verified against the build
// 3393 code (MM2Recomp). See docs/physics.md.

#include "phys/vehicle/Engine.h"

#include "phys/AgeMath.h"
#include "phys/InertialCS.h"
#include "phys/vehicle/Drivetrain.h"
#include "phys/vehicle/Transmission.h"

namespace mm2::phys {
namespace {
constexpr float kRpmToRad = 0.10471976f;
constexpr float kRadToRpm = 9.549296f;
constexpr float kWattsToHp = 0.0013404826f;
} // namespace

void Engine::configure(const EngineParams& p) {
    maxHorsePower = p.maxHorsePower;
    idleRPM = p.idleRPM;
    optRPM = p.optRPM;
    maxRPM = p.maxRPM;
    gcl = p.gcl;
    angInertia = p.angInertia;
    reset();
    computeConstants();
}

void Engine::reset() {
    // vehEngine::Reset: idle, mid gear change, full throttle limit.
    rpm = idleRPM;
    prevGearRPM = idleRPM;
    rotationSpeed = 0.0f;
    horsepower = 0.0f;
    torque = 0.0f;
    throttle = 0.0f;
    changingGear = true;
    gearChangeTime = gcl;
    hpScale = 1.0f;
    maxThrottle = 1.0f;
}

void Engine::computeConstants() {
    maxRotationSpeed = maxRPM * kRpmToRad;
    const float wOpt = optRPM * kRpmToRad;
    optRotationSpeed = wOpt;
    idleRotationSpeed = idleRPM * kRpmToRad;
    const float span = maxRPM * kRpmToRad - wOpt;
    taperCoef = 1.0f / (span * span);
    torqueCoef = (hpScale * maxHorsePower * 746.0f) / (wOpt * wOpt * wOpt);
}

float Engine::calcTorqueAtFullThrottle(float w) const {
    const float o = optRotationSpeed;
    if (w <= o)
        return (kPhi * o - w) * (kInvPhi * o + w) * torqueCoef;
    if (w <= maxRotationSpeed) {
        // Multiplied in vehEngine::CalcTorqueAtFullThrottle's order.
        const float taper = (w + maxRotationSpeed) - (o + o);
        return (((taper * (kInvPhi * o + w)) * (kPhi * o - w)) * (maxRotationSpeed - w)) * torqueCoef * taperCoef;
    }
    return 0.0f;
}

float Engine::calcTorqueAtZeroThrottle() const {
    return ((idleRotationSpeed - rotationSpeed) * ((hpScale * maxHorsePower * 746.0f) / optRotationSpeed) * 0.75f) /
           (optRotationSpeed - idleRotationSpeed);
}

float Engine::calcTorque(float t) const {
    return calcTorqueAtFullThrottle(rotationSpeed) * t + (1.0f - t) * calcTorqueAtZeroThrottle();
}

void Engine::update(float dt, Transmission& trans, Drivetrain& primary, InertialCS& ics, int drivetrainType) {
    if (trans.gearChanged && !changingGear) {
        prevGearRPM = rpm;
        gearChangeTime = gcl;
        changingGear = true;
    }
    torque = calcTorque(throttle);

    // Clutch.
    const float ratio = trans.currentRatio();
    if (ratio == 0.0f || rotationSpeed < idleRotationSpeed) {
        if (primary.attached())
            primary.detach();
    } else if (ratio != 0.0f && idleRotationSpeed + idleRotationSpeed < rotationSpeed && !primary.attached()) {
        primary.attach();
    }
    if (!primary.attached()) {
        // Free revving.
        float w = (torque / angInertia) * dt + rotationSpeed;
        rotationSpeed = w;
        if (w < 0.0f)
            w = 0.0f;
        else if (maxRotationSpeed < w)
            w = maxRotationSpeed;
        rotationSpeed = w;
    }

    rpm = rotationSpeed * kRadToRpm;
    if (changingGear) {
        if (gearChangeTime <= 0.0f) {
            changingGear = false;
            trans.gearChanged = false;
            rotationSpeed = rpm * kRpmToRad;
        } else {
            // Gear change lag: no torque, displayed RPM blended.
            torque = 0.0f;
            rpm = (prevGearRPM * gearChangeTime + (gcl - gearChangeTime) * rpm) / gcl;
            gearChangeTime = gearChangeTime - dt;
        }
    }
    horsepower = torque * rotationSpeed * kWattsToHp;

    // Reaction torque: revving in neutral rocks the car about the engine's axis.
    if (trans.currentGear == Transmission::kNeutral) {
        const float t = (torque / calcTorqueAtFullThrottle(optRotationSpeed)) * angInertia;
        Vec3 axis;
        // The frame the torque is given in: the body's, or the engine pivot
        // rocked by 0.05 t about its own Z axis and carried into the world
        // (vehEngine::Update keeps that matrix for drawing too).
        Mat34 frame = ics.matrix;
        if (pivot) {
            const float k = -(t * angInertia);
            axis = {k * pivot->m2.x, k * pivot->m2.y, k * pivot->m2.z};
            Mat34 rocked = *pivot;
            age::rotate(rocked, pivot->m2, t * 0.05f); // Matrix34::Rotate
            frame = Mat34::mul(rocked, ics.matrix);
        } else if (drivetrainType == 1) {
            axis = {t * angInertia, 0.0f, 0.0f};
        } else {
            axis = {0.0f, 0.0f, -(t * angInertia)};
        }
        const Vec3 l{axis.x * ics.inertia.x, axis.y * ics.inertia.y, axis.z * ics.inertia.z};
        // Summed in vehEngine::Update's order.
        ics.applyTorque({(l.z * frame.m2.x + l.y * frame.m1.x) + l.x * frame.m0.x,
                         (l.z * frame.m2.y + l.y * frame.m1.y) + l.x * frame.m0.y,
                         (l.z * frame.m2.z + l.y * frame.m1.z) + l.x * frame.m0.z});
    }
}

} // namespace mm2::phys
