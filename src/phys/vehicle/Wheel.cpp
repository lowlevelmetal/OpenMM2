// vehWheel from Midtown Madness 2, verified against the build 3393 code
// (MM2Recomp): ComputeConstants, SetNormalLoad, ComputeFriction, SetInputs,
// CalcSuspensionForce, ComputeDwtdw, GetBumpDisplacement, Update and the
// GetVisualDisp* helpers. Operation order and float32 arithmetic follow the
// original. See docs/physics.md.

#include "phys/vehicle/Wheel.h"

#include "phys/AgeMath.h"
#include "phys/InertialCS.h"
#include "phys/World.h"

#include <cmath>
#include <cstdint>

namespace mm2::phys {
namespace {

// The gravity vehWheel uses for its static loads (_DAT_005c6c1c).
constexpr float kWheelGravity = -19.6f;
// Probe start above the top of the suspension travel.
constexpr float kProbeAbove = 0.3f;
// Tyre displacement relaxation per metre of wheel travel while sliding.
constexpr float kRelaxRate = 0.1f;

float signOf(float v) {
    return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f);
}

std::uint32_t g_randSeed = 1;

// Matrix34::MakeRotateY (3x3 only).
void makeRotateY(Mat34& m, float a) {
    const float c = std::cos(a);
    const float s = std::sin(a);
    m.m0 = {c, 0.0f, -s};
    m.m1 = {0.0f, 1.0f, 0.0f};
    m.m2 = {s, 0.0f, c};
}

// Matrix34::Rotate about a world-space axis (this = this * R).
void rotateAbout(Mat34& m, const Vec3& axis, float angle) {
    const Mat34 r = age::arbitraryRotation(axis, angle);
    m.m0 = r.transformDir(m.m0);
    m.m1 = r.transformDir(m.m1);
    m.m2 = r.transformDir(m.m2);
}

} // namespace

float physFrand() {
    // irand: MSVC rand(); frand = irand * 2^-15.
    g_randSeed = g_randSeed * 214013u + 2531011u;
    const int r = static_cast<int>((g_randSeed >> 16) & 0x7FFFu);
    return static_cast<float>(r) * 3.0517578e-05f;
}

void Wheel::init(const WheelParams& p, const WheelGeometry& g, float m, bool withCar, float cgz, int wheelFlags) {
    *this = Wheel{};
    params = p;
    flags = wheelFlags;
    center = g.center;
    radius = g.radius;
    width = g.width;
    mass = m;
    hasCar = withCar;
    cgZ = cgz;
    computeConstants();
    reset();
}

void Wheel::copyVars(const Wheel& o) {
    params = o.params;
    computeConstants();
}

void Wheel::computeConstants() {
    invOptSlip2 = 1.0f / (params.optimumSlipPercent * params.optimumSlipPercent);
    const float z = center.z;
    if (hasCar) {
        setNormalLoad(-(kWheelGravity * mass) * (std::abs(z - cgZ) / (std::abs(z) + std::abs(z))) * 0.5f);
        return;
    }
    const float az = std::abs(z);
    setNormalLoad((-(mass * kWheelGravity) * az * 0.5f) / (az + az));
}

void Wheel::setNormalLoad(float load) {
    normalLoad = load;
    if (params.suspensionFactor < 0.75f)
        params.suspensionFactor = 0.75f;
    const float k = 1.0f / ((params.suspensionLimit + params.suspensionExtent) * params.suspensionExtent);
    spring = (params.suspensionFactor * params.suspensionExtent + params.suspensionLimit) * k * load;
    progressive = ((params.suspensionFactor - 1.0f) * k * load) / spring;
    const float d = std::sqrt(spring * load) * params.suspensionDampCoef;
    damping = d + d;
    const float massShare = load / kWheelGravity; // negative
    stiffLong = (load + load) / params.tireDispLimitLong;
    const float dl = std::sqrt(stiffLong * -massShare) * params.tireDampCoefLong;
    dampLong = dl + dl;
    stiffLat = (load + load) / params.tireDispLimitLat;
    const float dt = std::sqrt(stiffLat * -massShare) * params.tireDampCoefLat;
    dampLat = dt + dt;
    const float brake = params.staticFric * radius * load;
    maxBrakeTorque = brake * params.brakeCoef;
    maxHandbrakeTorque = brake * params.handbrakeCoef;
}

void Wheel::reset() {
    suspension = 0.0f;
    suspensionVelocity = 0.0f;
    contactStiffness = 0.0f;
    rotationSpeed = 0.0f;
    brakeTorque = 0.0f;
    steerAngle = 0.0f;
    bumpPhase = 0.0f;
    rotation = 0.0f;
    currentTireDispLat = 0.0f;
    currentTireDispLong = 0.0f;
    latSlipPercent = 0.0f;
    longSlipPercent = 0.0f;
    tireResistance = 0.0f;
    tireGripLat = 0.0f;
    tireGripLong = 0.0f;
    suspensionForce = 0.0f;
    currentLoad = 0.0f;
    contactFrame = Mat34::identity();
    slide = 0.0f;
    skidding = false;
    onGround = false;
    hit = false;
    latVelocity = fwdVelocity = normalVelocity = slipVelocity = 0.0f;
    bump = 0.0f;
    drag = 0.0f;
    friction = 1.0f;
    depth = 0.0f;
    bumpHeight = 0.0f;
    bumpWidth = 0.0f;
    camber = 0.0f;
    wobble = 0.0f;
    matrix = Mat34::identity();
    matrix.m3 = center;
}

void Wheel::setInputs(float steer, float brake, float handbrake) {
    const float a = -(steer * params.steeringLimit);
    steerAngle = (1.0f - a * params.steeringOffset * signOf(center.x)) * a;
    brakeTorque = handbrake * maxHandbrakeTorque + brake * maxBrakeTorque;
    if (flags & kFixed)
        brakeTorque = params.staticFric * currentLoad * friction * radius;
}

float Wheel::computeFriction(float slip, float& slideOut) const {
    const float s = std::abs(slip);
    const float fs = params.staticFric * friction;
    const float fl = params.slidingFric * friction;
    const float opt = params.optimumSlipPercent;
    const float f = ((opt + opt) - s) * invOptSlip2 * fs * s;
    if (s <= opt) {
        slideOut = (s * 0.5f) / opt;
        return f;
    }
    if (fl < f) {
        slideOut = ((fs - f) + (f - fl) * 0.5f) / (fs - fl);
        return f;
    }
    slideOut = 1.0f;
    return fl;
}

void Wheel::calcSuspensionForce(float disp, bool contact, float cosNormal, const WheelEnv& env) {
    const float prev = suspension;
    bottomedOut = false;
    const float droop = -params.suspensionExtent;
    bool atDroop;
    if (contact) {
        suspension = disp;
        atDroop = !(droop <= disp);
        if (atDroop)
            suspension = droop;
    } else {
        suspension = droop;
        atDroop = true;
    }
    float rate = (suspension - prev) * env.invDt;
    if (rate < -10.0f)
        rate = -10.0f;
    else if (10.0f <= rate)
        rate = 10.0f;
    suspensionVelocity = rate;
    const float prog = progressive * suspension + 1.0f;
    const float f = (rate * damping + suspension * spring) * prog + normalLoad;
    suspensionForce = f;
    if (f < 0.0f) {
        // The wheel lifts off: relax the travel without pulling the body down.
        suspensionForce = 0.0f;
        const float d =
            (prev * damping - env.dt * spring * params.suspensionExtent) / (env.dt * spring + damping);
        suspension = d;
        contactStiffness = 0.0f;
        suspensionVelocity = (d - prev) * env.invDt;
        return;
    }
    bool bottom = false;
    float impulseForce = 0.0f;
    float bottomStiffness = 0.0f;
    if (suspension <= params.suspensionLimit) {
        if (droop <= suspension) {
            if (!atDroop) {
                contactStiffness = prog * damping + (env.dt * spring) / cosNormal;
                return;
            }
            contactStiffness = 0.0f;
            return;
        }
        suspension = droop;
    } else {
        // Bottomed out: stop the closing velocity at the contact
        // (phImpact::CalcCollisionNoFriction) and push the overlap out.
        const Vec3& n = contactFrame.m1;
        float j = 0.0f;
        if (normalVelocity < 0.0f) {
            Mat34 c;
            env.ics->calcCMatrix(c, intersection.position);
            const Vec3 cn = c.transformDir(n);
            j = -(normalVelocity / cn.dot(n));
        }
        impulseForce = env.invDt * j * 0.25f;
        bottomStiffness = -(impulseForce / normalVelocity);
        const float over = (suspension - params.suspensionLimit) * cosNormal;
        env.ics->applyPush({over * n.x, over * n.y, over * n.z});
        bottomedOut = true;
        suspension = params.suspensionLimit;
        bottom = true;
    }
    rate = (suspension - prev) * env.invDt;
    if (rate < -10.0f)
        rate = -10.0f;
    else if (10.0f < rate)
        rate = 10.0f;
    suspensionVelocity = rate;
    suspensionForce = (rate * damping + suspension * spring) * (progressive * suspension + 1.0f) + normalLoad;
    contactStiffness = 0.0f;
    if (bottom) {
        contactStiffness = bottomStiffness;
        suspensionForce = impulseForce + suspensionForce;
    }
}

float Wheel::bumpDisplacement(float speed, float dt) {
    if (!material || material->height == 0.0f)
        return 0.0f;
    bumpPhase = (physFrand() + 0.618f) * dt * speed + bumpPhase;
    bumpPhase = std::fmod(bumpPhase, material->width);
    const float b = std::sin((bumpPhase * 6.2831855f) / material->width) * material->height;
    return speed < 1.0f ? b * speed : b;
}

void Wheel::noContact() {
    hit = false;
}

float Wheel::computeDwtdw(float net, const WheelEnv& env) {
    // The steered wheel pivots about its inner edge.
    Mat34 m;
    if (params.steeringLimit == 0.0f) {
        m = Mat34::identity();
        m.m3 = center;
    } else {
        const float off = signOf(center.x) * width * 0.5f;
        m.m3 = center;
        makeRotateY(m, steerAngle);
        m.m3.x = m.m3.x - off;
        m.m3 = {off * m.m0.x + m.m3.x, off * m.m0.y + m.m3.y, off * m.m0.z + m.m3.z};
    }
    matrix = Mat34::mul(m, *env.frame);

    const Vec3& up = matrix.m1;
    const float above = params.suspensionLimit + kProbeAbove;
    const float below = params.suspensionExtent + radius;
    const Vec3 top{above * up.x + matrix.m3.x, above * up.y + matrix.m3.y, above * up.z + matrix.m3.z};
    const Vec3 bottom{matrix.m3.x - below * up.x, matrix.m3.y - below * up.y, matrix.m3.z - below * up.z};
    const float segLen = params.suspensionExtent + radius + above;

    hit = false;
    float upDotN = 0.0f;
    bool wall = false;
    RayHit isect;
    if (env.ground && env.ground->probe(top, bottom, isect)) {
        material = &env.ground->material(isect.material);
        // OpenMM2: deep water (materials.mtl depth >= 1, e.g. deepwater 100)
        // carries no wheel (inferred; see docs/physics.md).
        hit = material->depth < 1.0f;
    }
    if (hit) {
        const Vec3& n = isect.normal;
        upDotN = up.x * n.x + up.y * n.y + up.z * n.z;
        const Vec3& r0 = matrix.m0;
        Vec3 back{n.z * r0.y - n.y * r0.z, r0.z * n.x - r0.x * n.z, r0.x * n.y - r0.y * n.x};
        const float b2 = back.z * back.z + back.y * back.y + back.x * back.x;
        if (upDotN < 0.02f || b2 < 0.02f) {
            hit = false;
        } else {
            const float inv = 1.0f / std::sqrt(b2);
            back = {inv * back.x, inv * back.y, inv * back.z};
            contactFrame.m3 = isect.position;
            contactFrame.m1 = n;
            contactFrame.m2 = back;
            contactFrame.m0 = {back.z * n.y - n.z * back.y, back.x * n.z - back.z * n.x,
                               back.y * n.x - back.x * n.y};
            wall = !(0.001f <= std::abs(n.y));
            intersection = isect;
        }
    }
    if (!hit) {
        calcSuspensionForce(-params.suspensionExtent, false, 0.0f, env);
        currentLoad = suspensionForce;
        if (0.0f < net)
            return -1e10f;
        return 0.0f <= net ? -0.0f : 1e10f;
    }

    const Vec3 v = env.ics->filteredVelocity(intersection.position, env.invDt);
    const Mat34& cf = contactFrame;
    latVelocity = v.x * cf.m0.x + v.y * cf.m0.y + v.z * cf.m0.z;
    fwdVelocity = -cf.m2.x * v.x + -cf.m2.y * v.y + -cf.m2.z * v.z;
    normalVelocity = v.x * cf.m1.x + v.y * cf.m1.y + v.z * cf.m1.z;
    slipVelocity = (flags & kFixed) ? fwdVelocity : rotationSpeed * radius + fwdVelocity;

    // Surface: bumps, drag, friction, sinking into soft ground while skidding.
    bump = bumpDisplacement(std::sqrt(latVelocity * latVelocity + fwdVelocity * fwdVelocity), env.dt);
    drag = material->drag;
    friction = material->friction;
    const float sinkTo = skidding ? material->depth : 0.0f;
    const float sinkRate = std::abs(rotationSpeed) * radius * kRelaxRate;
    if (depth < sinkTo) {
        depth = sinkRate * env.dt + depth;
        if (sinkTo < depth)
            depth = sinkTo;
    } else if (sinkTo < depth) {
        depth = depth - sinkRate * env.dt;
        if (depth < sinkTo)
            depth = sinkTo;
    }
    bumpHeight = material->height;
    bumpWidth = material->width;
    if (wall)
        friction = 0.0f;
    friction = env.weatherFriction * friction;
    if (env.hasCar && friction < 1.0f) {
        if (1.0f <= env.carFrictionHandling)
            friction = (1.0f / env.carFrictionHandling) * friction;
        else
            friction = (1.0f - env.carFrictionHandling) * (1.0f - friction) + friction;
    }

    calcSuspensionForce((above + radius) - ((segLen * intersection.t - bump) + depth), true, upDotN, env);
    currentLoad = suspensionForce;

    // Breakpoint: the wheel speed where the long slip reaches the optimum.
    // (The original also computes a tyre slope here and then discards it.)
    const float opt = params.optimumSlipPercent;
    const float w0 = -(fwdVelocity / radius);
    const float lo = (1.0f - opt) * w0;
    const float hi = (opt + 1.0f) * w0;
    float c = longSlipPercent;
    if (c < -opt)
        c = -opt;
    else if (opt < c)
        c = opt;
    const float t = (1.0f - c) * w0;
    float inner, outer;
    if (lo <= hi) {
        inner = w0 < t ? w0 : t;
        outer = w0 <= t ? t : w0;
    } else {
        inner = t < w0 ? w0 : t;
        outer = t <= w0 ? t : w0;
    }
    const float rot = rotationSpeed;
    if (0.0f <= net) {
        if (fwdVelocity <= 0.0f) {
            if (outer <= rot)
                return outer;
            if (lo <= rot)
                return lo;
        } else {
            if (inner <= rot)
                return inner;
            if (hi <= rot)
                return hi;
        }
        return -1e11f;
    }
    if (fwdVelocity <= 0.0f) {
        if (rot <= inner)
            return inner;
        if (rot <= hi)
            return hi;
    } else {
        if (rot <= outer)
            return outer;
        if (rot <= lo)
            return lo;
    }
    return 1e11f;
}

void Wheel::update(const WheelEnv& env) {
    InertialCS& ics = *env.ics;
    const float dt = env.dt;
    if (!hit) {
        onGround = false;
        slide = 0.0f;
        skidding = false;
        currentTireDispLat = 0.0f;
        currentTireDispLong = 0.0f;
        tireResistance = 0.0f;
        tireGripLat = 0.0f;
        tireGripLong = 0.0f;
    } else {
        onGround = true;
        position = intersection.position;
        const Mat34& cf = contactFrame;
        const Vec3& n = cf.m1;
        if (0.0f < contactStiffness) {
            // The suspension's stiffness for the body's implicit step.
            const float c = contactStiffness;
            const Vec3 cn{c * n.x, c * n.y, c * n.z};
            Mat34 k;
            k.m0 = {cn.x * n.x, cn.x * n.y, cn.x * n.z};
            k.m1 = {cn.x * n.y, cn.y * n.y, cn.y * n.z};
            k.m2 = {cn.x * n.z, cn.y * n.z, cn.z * n.z};
            k.m3 = {};
            Vec3 f;
            if (params.suspensionLimit <= suspension) {
                f = {};
            } else {
                const float s = spring * suspensionVelocity * dt;
                f = {s * n.x, s * n.y, s * n.z};
            }
            ics.applyContactForce(f, intersection.position, k);
        }

        const float wr = rotationSpeed * radius;
        if (latVelocity == 0.0f)
            latSlipPercent = 0.0f;
        else if (std::abs(fwdVelocity) < std::abs(latVelocity))
            latSlipPercent = signOf(latVelocity);
        else
            latSlipPercent = latVelocity / std::abs(fwdVelocity);
        slipVelocity = (flags & kFixed) ? fwdVelocity : wr + fwdVelocity;
        if (slipVelocity == 0.0f)
            longSlipPercent = 0.0f;
        else if (std::abs(fwdVelocity) < std::abs(slipVelocity))
            longSlipPercent = signOf(slipVelocity);
        else
            longSlipPercent = slipVelocity / std::abs(fwdVelocity);

        // Displacement at which the force reaches one unit of friction.
        const float unitLong = (signOf(slipVelocity) * currentLoad) / stiffLong;
        const float unitLat = (signOf(latVelocity) * currentLoad) / stiffLat;
        const float stepLat = dt * latVelocity;
        const float stepLong = dt * slipVelocity;
        const float peak = params.staticFric * friction;
        const float opt = params.optimumSlipPercent;

        // Longitudinal friction and displacement limit.
        float slideLong = 0.0f;
        float muLong = peak;
        float limitLong = peak * unitLong;
        bool computeLong = std::abs(longSlipPercent) <= opt;
        if (!computeLong) {
            if (stepLong <= 0.0f)
                computeLong = currentTireDispLong <= limitLong || stepLong < limitLong - currentTireDispLong;
            else
                computeLong = limitLong <= currentTireDispLong || limitLong - currentTireDispLong < stepLong;
        }
        if (computeLong) {
            muLong = computeFriction(longSlipPercent, slideLong);
            limitLong = muLong * unitLong;
        }
        // Lateral.
        float slideLat = 0.0f;
        float muLat = peak;
        float limitLat = peak * unitLat;
        bool computeLat = std::abs(latSlipPercent) <= opt;
        if (!computeLat) {
            if (stepLat <= 0.0f)
                computeLat = currentTireDispLat <= limitLat || stepLat < limitLat - currentTireDispLat;
            else
                computeLat = limitLat <= currentTireDispLat || limitLat - currentTireDispLat < stepLat;
        }
        if (computeLat) {
            muLat = computeFriction(latSlipPercent, slideLat);
            limitLat = muLat * unitLat;
        }

        // Combined slip: the direction slipping more sets the friction.
        float maxSlip = std::abs(longSlipPercent);
        bool longDominates = true;
        if (maxSlip < latSlipPercent) {
            maxSlip = latSlipPercent;
            longDominates = false;
        } else if (maxSlip < -latSlipPercent) {
            maxSlip = -latSlipPercent;
            longDominates = false;
        }
        float mu = peak;
        if (opt <= maxSlip) {
            if (longDominates) {
                mu = muLong;
                if (muLong < muLat)
                    limitLat = muLong * unitLat;
            } else {
                mu = muLat;
                if (muLat < muLong)
                    limitLong = muLat * unitLong;
            }
        }

        // Advance the displacements; past the limit they relax towards it.
        const float relax = std::abs(wr) * dt * kRelaxRate;
        float rateLong = stepLong;
        bool gripLong;
        {
            float d = stepLong + currentTireDispLong;
            if (stepLong < 0.0f) {
                if (limitLong > d) {
                    d = relax + currentTireDispLong;
                    if (limitLong < d)
                        d = limitLong;
                    gripLong = false;
                    rateLong = 0.0f;
                } else {
                    gripLong = true;
                }
            } else if (limitLong < d) {
                d = currentTireDispLong - relax;
                if (d < limitLong)
                    d = limitLong;
                gripLong = false;
                rateLong = 0.0f;
            } else {
                gripLong = true;
            }
            currentTireDispLong = d;
        }
        float rateLat = stepLat;
        bool gripLat;
        {
            float d = stepLat + currentTireDispLat;
            if (stepLat < 0.0f) {
                if (limitLat > d) {
                    d = relax + currentTireDispLat;
                    if (limitLat < d)
                        d = limitLat;
                    gripLat = false;
                    rateLat = 0.0f;
                } else {
                    gripLat = true;
                }
            } else if (limitLat < d) {
                d = currentTireDispLat - relax;
                if (d < limitLat)
                    d = limitLat;
                gripLat = false;
                rateLat = 0.0f;
            } else {
                gripLat = true;
            }
            currentTireDispLat = d;
        }
        tireGripLat = -(currentTireDispLat * stiffLat) - rateLat * env.invDt * dampLat;
        tireGripLong = -(stiffLong * currentTireDispLong) - rateLong * env.invDt * dampLong;

        // Friction circle.
        const float f2 = tireGripLong * tireGripLong + tireGripLat * tireGripLat;
        const float limit2 = mu * currentLoad * mu * currentLoad;
        if (limit2 < f2) {
            if ((!gripLong && opt < std::abs(longSlipPercent)) || (!gripLat && opt < std::abs(latSlipPercent))) {
                // Sliding: the force opposes the slip velocity.
                const float k =
                    -std::sqrt(limit2 / (latVelocity * latVelocity + slipVelocity * slipVelocity));
                tireGripLat = k * latVelocity;
                tireGripLong = k * slipVelocity;
            } else {
                tireGripLat = std::sqrt(limit2 / f2) * tireGripLat;
                tireGripLong = std::sqrt(limit2 / f2) * tireGripLong;
            }
        }
        if (gripLong)
            slide = gripLat ? 0.0f : slideLat;
        else if (gripLat)
            slide = slideLong;
        else
            slide = slideLat <= slideLong ? slideLong : slideLat;
        skidding = 0.5f < slide;
        tireResistance = tireGripLong * radius;

        // Tyre forces, surface drag (quadratic in the contact velocity, scaled
        // by TireDragCoef and the material's drag; sinking adds to it) and the
        // suspension force.
        const float dragLat =
            -(std::abs(latVelocity) * params.tireDragCoefLat * currentLoad * latVelocity * drag);
        const float dragLong = -((depth + 1.0f) * -(std::abs(fwdVelocity) * params.tireDragCoefLong *
                                                    currentLoad * drag * fwdVelocity));
        const float back = -tireGripLong;
        const Vec3 force{dragLong * cf.m2.x + dragLat * cf.m0.x + back * cf.m2.x + tireGripLat * cf.m0.x +
                             suspensionForce * cf.m1.x,
                         dragLong * cf.m2.y + dragLat * cf.m0.y + back * cf.m2.y + tireGripLat * cf.m0.y +
                             suspensionForce * cf.m1.y,
                         dragLong * cf.m2.z + dragLat * cf.m0.z + back * cf.m2.z + tireGripLat * cf.m0.z +
                             suspensionForce * cf.m1.z};
        ics.applyForce(force, intersection.position);
    }

    // Visual state: camber, spin, the wheel lowered by its displacement less
    // the tyre squash.
    if (0.0f < params.camberLimit)
        camber = signOf(center.x) * suspension * params.camberLimit;
    rotation = dt * rotationSpeed + rotation;
    const float d = suspension - visualDispVert();
    matrix.m3 = {d * matrix.m1.x + matrix.m3.x, d * matrix.m1.y + matrix.m3.y, d * matrix.m1.z + matrix.m3.z};
    rotateAbout(matrix, matrix.m0, rotation);
    if (wobble != 0.0f)
        rotateAbout(matrix, matrix.m2, wobble);
}

float Wheel::visualDispVert() const {
    const float d = (radius * 0.05f * suspensionForce) / normalLoad;
    if (d < 0.0f)
        return 0.0f;
    const float m = radius * 0.3f;
    return d <= m ? d : m;
}

float Wheel::visualDispLat() const {
    const float l = params.tireDispLimitLat;
    return currentTireDispLat < -l ? -l : (l < currentTireDispLat ? l : currentTireDispLat);
}

float Wheel::visualDispLong() const {
    const float l = params.tireDispLimitLong;
    return currentTireDispLong < -l ? -l : (l < currentTireDispLong ? l : currentTireDispLong);
}

} // namespace mm2::phys
