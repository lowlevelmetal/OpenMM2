// Port of mmWheel from Open1560 (https://github.com/0x1F9F1/Open1560),
// GPL-3.0: code/midtown/game.asm, Midtown Madness 1 beta build 1560.
// init() maps MM2 tune fields onto MM1's (OpenMM2 adaptation, inferred).

#include "phys/vehicle/Wheel.h"

#include "phys/AgeMath.h"
#include "phys/InertialCS.h"
#include "phys/World.h"

#include <cmath>

namespace mm2::phys {
namespace {

const Vec3 kYAxis{0.0f, 1.0f, 0.0f};
const Vec3 kXAxis{1.0f, 0.0f, 0.0f};
const Vec3 kZAxis{0.0f, 0.0f, 1.0f};

// Moves a tyre displacement by `d` towards `target` without passing it; a
// displacement already beyond the target in the direction of motion stays.
float advanceDisp(float disp, float d, float target) {
    if (d >= 0.0f) {
        if (target <= disp)
            return disp;
        const float cand = d + disp;
        return target < cand ? target : cand;
    }
    const float cand = d + disp;
    if (target <= cand)
        return cand;
    return target < disp ? target : disp;
}

float signOf(float v) {
    return v < 0.0f ? -1.0f : 1.0f;
}

} // namespace

void Wheel::init(const WheelParams& p, const WheelGeometry& g, const Vec3& centerFromCg, float carMass,
                 float gravity, int wheelCount, int wheelFlags) {
    *this = Wheel{};
    flags = wheelFlags;
    numWheels = wheelCount;
    center = centerFromCg;
    radius = g.radius;
    width = g.width;

    // mmWheel::Init: NormalLoad = -(Mass * PHYS.Gravity / NumWheels).
    const float G = -gravity;
    normalLoad = -((carMass * G) / static_cast<float>(numWheels));

    // MM2 -> MM1 mapping (inferred; see docs/physics.md).
    suspensionLimit = p.suspensionLimit;
    suspensionExtent = p.suspensionExtent;
    renderableSuspensionLimit = p.suspensionLimit;
    spring =
        p.suspensionExtent > 0.0f ? p.suspensionFactor * normalLoad / p.suspensionExtent : normalLoad / 0.1f;
    damping = p.suspensionDampCoef * spring;
    steeringRatio = p.steeringLimit;
    brakeRatio = p.brakeCoef;
    handbrakeCoef = p.handbrakeCoef;
    optimumSlipPercent = p.optimumSlipPercent > 0.0f ? p.optimumSlipPercent : 0.14f;
    staticFric = p.staticFric;
    slidingFric = p.slidingFric;
    const float massShare = carMass / static_cast<float>(numWheels);
    rubberSpring = p.tireDispLimitLong > 0.0f ? (staticFric * normalLoad) / p.tireDispLimitLong : 40000.0f;
    rubberSpringLat =
        p.tireDispLimitLat > 0.0f ? (staticFric * normalLoad) / p.tireDispLimitLat : rubberSpring;
    rubberDamp = p.tireDampCoefLong * 2.0f * std::sqrt(rubberSpring * massShare);
    rubberDampLat = p.tireDampCoefLat * 2.0f * std::sqrt(rubberSpringLat * massShare);
    tireDragCoefLong = p.tireDragCoefLong;
    tireDragCoefLat = p.tireDragCoefLat;
    steeringOffset = p.steeringOffset;
    camberLimit = p.camberLimit;
    wobbleLimit = p.wobbleLimit;

    // mmWheel::Init: lateral values default to the longitudinal ones.
    if (rubberSpringLat == 0.0f)
        rubberSpringLat = rubberSpring;
    if (rubberDampLat == 0.0f)
        rubberDampLat = rubberDamp;
    computeConstants();
    reset();
}

void Wheel::computeConstants() {
    // Friction curve -a s^2 + b s with peak StaticFric at s0; U2 is the slip
    // where it has fallen to SlidingFric (larger root).
    const float s0 = optimumSlipPercent;
    const float sf = staticFric * fricMultiplier;
    const float a = sf / (s0 * s0);
    const float b = (sf + sf) / s0;
    const float sl = slidingFric * fricMultiplier;
    const float disc = b * b - (sl * a) * 4.0f;
    const float q = std::sqrt(disc);
    const float m2a = a * -2.0f;
    const float r1 = (q - b) / m2a;
    const float r2 = (-b - q) / m2a;
    unkFriction1 = a;
    unkFriction2 = r1 > r2 ? r1 : r2;
}

void Wheel::reset() {
    tireResistance = 0.0f;
    wobble = 0.0f;
    rollingRotation = 0.0f;
    rotation = 0.0f;
    steering = 0.0f;
    rotationSpeed = 0.0f;
    currentTireDispLong = 0.0f;
    currentTireDispLat = 0.0f;
    suspension = 0.0f;
    longSlipPercent = 0.0f;
    latSlipPercent = 0.0f;
    hit = false;
    onGround = false;
    visualMatrix = Mat34::identity();
    visualMatrix.m3 = center;
}

float Wheel::frictionForSlip(float slip, bool lateral) const {
    const float s = std::abs(slip);
    if (s >= unkFriction2)
        return lateral ? fricMultiplier * slidingFric : slidingFric;
    return s * (((optimumSlipPercent + optimumSlipPercent) - s) * unkFriction1);
}

bool Wheel::probe(const WheelEnv& env) {
    // mmWheel::ComputeDwtdw, first half.
    const float steer = -((steerMultiplier * steeringInput) * steeringRatio);
    const Mat34& M = *env.frame;
    const Vec3 wc = M.transform(center);
    const Vec3& up = M.m1;

    const float above = suspensionLimit + radius;
    const float below = suspensionExtent + radius;
    const Vec3 top{above * up.x + wc.x, above * up.y + wc.y, above * up.z + wc.z};
    const Vec3 bottom{wc.x - below * up.x, wc.y - below * up.y, wc.z - below * up.z};

    hit = false;
    RayHit isect;
    if (env.ground && env.ground->probe(top, bottom, isect)) {
        material = &env.ground->material(isect.material);
        // mmCullCity::IsPolyWater: no support on water. MM2 marks deep water
        // with a large material depth (deepwater: 100).
        hit = material->depth < 1.0f;
    }
    const Vec3& n = isect.normal;
    if (!hit || ((n.y * n.y + n.z * n.z) + n.x * n.x) == 0.0f) {
        hit = false;
        return false;
    }
    intersection = isect;

    Mat34 sm = M;
    age::rotate(sm, kYAxis, steer);

    const InertialCS& ics = *env.ics;
    const Vec3& vel = (flags & kUseLinearVelocity) ? ics.linearVelocity : ics.frameVelocity;
    const Vec3& w = ics.angularVelocity;
    const Vec3 r{isect.position.x - M.m3.x, isect.position.y - M.m3.y, isect.position.z - M.m3.z};
    const Vec3 cv{(r.z * w.y - r.y * w.z) + vel.x, (r.x * w.z - r.z * w.x) + vel.y,
                  (r.y * w.x - r.x * w.y) + vel.z};
    const float vn = (n.x * cv.x + n.z * cv.z) + n.y * cv.y;
    const Vec3 pv{cv.x - vn * n.x, cv.y - n.y * vn, cv.z - n.z * vn};
    contactForwardVel = ((-sm.m2.x) * pv.x + (-sm.m2.y) * pv.y) + (-sm.m2.z) * pv.z;

    // Suspension (MM2: wheels sink by the surface depth; inferred).
    const float segLen = above + below;
    const float dist = isect.t * segLen + material->depth;
    const float oldSusp = suspension;
    suspension = (suspensionLimit - radius * -2.0f) - dist;
    float sv = (suspension - oldSusp) * env.invDt;
    if (sv <= -3.0f)
        sv = -3.0f;
    else if (!(sv < 3.0f))
        sv = 3.0f;
    float load = (spring * suspension + damping * sv) + normalLoad;
    if (load < 0.0f)
        load = 0.0f;
    suspensionVelocity = sv;
    currentLoad = ((n.z * up.z + n.y * up.y) + n.x * up.x) * load;
    // (MM1 also updates BlendedLoad here, used only by the discarded slope.)
    return true;
}

float Wheel::computeLimits(float net, float& A, float& B, float& C, const WheelEnv& env) const {
    // mmWheel::ComputeDwtdw, second half.
    if (!hit) {
        A = 0.0f;
        C = 0.0f;
        B = (net < 0.0f ? -1.0f : 1.0f) * -1e10f;
        return 0.0f;
    }
    const float fwdVel = contactForwardVel;

    // Wheel-speed limit for the drivetrain solver.
    const float s0 = optimumSlipPercent;
    const float rollRot = -(fwdVel / radius);
    const float lo = age::rsubD(1.0, s0) * rollRot;
    const float hi = age::subD(s0, -1.0) * rollRot;
    float c = longSlipPercent;
    if (c <= -s0)
        c = -s0;
    else if (!(c < s0))
        c = s0;
    const float target = age::rsubD(1.0, c) * rollRot;
    float nearV, farV;
    if (lo > hi) {
        nearV = rollRot <= target ? target : rollRot;
        farV = rollRot < target ? rollRot : target;
    } else {
        nearV = rollRot < target ? rollRot : target;
        farV = rollRot <= target ? target : rollRot;
    }
    const float rot = rotationSpeed;
    if (net < 0.0f) {
        if (fwdVel > 0.0f) {
            if (!(farV < rot))
                B = farV;
            else
                B = rot > lo ? 1e11f : lo;
        } else {
            if (!(rot > nearV))
                B = nearV;
            else
                B = rot > hi ? 1e11f : hi;
        }
    } else {
        if (fwdVel > 0.0f) {
            if (!(rot < nearV))
                B = nearV;
            else
                B = rot < hi ? -1e11f : hi;
        } else {
            if (!(farV > rot))
                B = farV;
            else
                B = rot < lo ? -1e11f : lo;
        }
    }
    // Tyre torque slope d(TireResistance)/d(w). MM1 computes
    //     min(R^2 (RubberSpring + RubberDamp / dt),
    //         R^2 / |V| * StaticFric * FricMultiplier * 2 / s0 * BlendedLoad)
    // and then discards it (stores 0 to A and C and returns 0). The first
    // term is a torque per radian, not per rad/s: one sample moves the tyre
    // displacement by R dw dt, so update()'s TireResistance = R (-RubberSpring
    // disp - RubberDamp ddisp/dt) changes by R^2 (RubberSpring dt +
    // RubberDamp) per rad/s while the displacement is below its friction
    // limit. Used as written, the drivetrain's dt D added R^2 (RubberSpring dt
    // + RubberDamp) of inertia per wheel, which does not vanish as dt -> 0
    // (an earlier OpenMM2 did that, and the stiff *_opp tyres launched at a
    // tenth of the expected rate).
    // OpenMM2: the exact per-sample slope, R^2 (RubberSpring dt + RubberDamp).
    // The friction-curve term is not used: with MM1's BlendedLoad for car
    // wheels (0.5 CurrentLoad + 0.5 * 0.25, OtherNormalLoad never being set)
    // it undercuts that slope above walking pace, the step turns partly
    // explicit again and the stiff *_opp tyres chatter at 60 Hz (vppanoz_opp
    // then passes its rev-limited top speed by 40 mph). See docs/physics.md.
    const float slope = ((env.dt * rubberSpring) + rubberDamp) * radius * radius;
    A = slope;
    C = 0.0f;
    return slope;
}

float Wheel::surfaceFriction(const WheelEnv& env) const {
    float f = material ? material->friction : 1.0f;
    if (!env.indoors)
        f = env.weatherFriction * f;
    if (f < 1.0f) {
        if (env.carFrictionHandling < 1.0f)
            f = f - (env.carFrictionHandling - 1.0f) * (1.0f - f);
        else
            f = (1.0f / env.carFrictionHandling) * f;
    }
    return f;
}

float Wheel::predictTireResistance(const WheelEnv& env) const {
    // The longitudinal half of update() at the current wheel speed, without
    // storing anything.
    if (!hit)
        return 0.0f;
    const float fwdVel = contactForwardVel;
    const float slipVel = rotationSpeed * radius + fwdVel;
    float longSlip;
    if (slipVel == 0.0f)
        longSlip = 0.0f;
    else if (!(std::abs(fwdVel) < std::abs(slipVel)))
        longSlip = slipVel / std::abs(fwdVel);
    else
        longSlip = signOf(slipVel);
    const float load = currentLoad > normalLoad ? currentLoad : normalLoad;
    const float muLong = frictionForSlip(longSlip, false) * env.longSlideMultiplier;
    const float longTarget =
        ((((muLong * load) * env.longSlideMultiplier) * surfaceFriction(env)) / rubberSpring) *
        signOf(slipVel);
    const float disp = advanceDisp(currentTireDispLong, env.dt * slipVel, longTarget);
    const float grip = -(disp * rubberSpring) - (env.invDt * (disp - currentTireDispLong)) * rubberDamp;
    return grip * radius;
}

void Wheel::update(InertialCS& ics, const WheelEnv& env) {
    steering = -((steeringInput * steeringRatio) * steerMultiplier);
    const Mat34& M = *env.frame;
    const float dt = env.dt;

    float fwdVel = 0.0f;
    if (hit) {
        const RayHit& isect = intersection;
        position = isect.position;
        onGround = true;
        const Vec3 suspF{ics.matrix.m1.x * currentLoad, ics.matrix.m1.y * currentLoad,
                         ics.matrix.m1.z * currentLoad};

        Mat34 sm = M;
        age::rotate(sm, kYAxis, steering);

        const Vec3& vel = (flags & kUseLinearVelocity) ? ics.linearVelocity : ics.frameVelocity;
        const Vec3& w = ics.angularVelocity;
        const Vec3 r{isect.position.x - M.m3.x, isect.position.y - M.m3.y, isect.position.z - M.m3.z};
        const Vec3 cv{(r.z * w.y - r.y * w.z) + vel.x, (r.x * w.z - w.x * r.z) + vel.y,
                      (w.x * r.y - r.x * w.y) + vel.z};
        rollingRotation = ((cv.x * sm.m2.x + cv.y * sm.m2.y) + cv.z * sm.m2.z) / -radius;

        const Vec3& n = isect.normal;
        const float vn = (n.y * cv.y + n.z * cv.z) + n.x * cv.x;
        const Vec3 pv{cv.x - n.x * vn, cv.y - n.y * vn, cv.z - n.z * vn};
        planeVelocity = pv;

        friction = surfaceFriction(env);

        const float latVel = (pv.x * sm.m0.x + pv.z * sm.m0.z) + pv.y * sm.m0.y;
        const Vec3 fwd{-sm.m2.x, -sm.m2.y, -sm.m2.z};
        fwdVel = (pv.z * fwd.z + pv.y * fwd.y) + pv.x * fwd.x;
        const float slipVel = rotationSpeed * radius + fwdVel;

        if (latVel == 0.0f)
            latSlipPercent = 0.0f;
        else if (std::abs(latVel) <= std::abs(fwdVel))
            latSlipPercent = latVel / std::abs(fwdVel);
        else
            latSlipPercent = signOf(latVel);

        if (slipVel == 0.0f)
            longSlipPercent = 0.0f;
        else if (!(std::abs(fwdVel) < std::abs(slipVel)))
            longSlipPercent = slipVel / std::abs(fwdVel);
        else
            longSlipPercent = signOf(slipVel);

        const float load = currentLoad > normalLoad ? currentLoad : normalLoad;

        const float muLat = frictionForSlip(latSlipPercent, true);
        const float latTarget = (((muLat * load) * friction) / rubberSpringLat) * signOf(latVel);
        const float muLong = frictionForSlip(longSlipPercent, false) * env.longSlideMultiplier;
        const float longTarget =
            ((((muLong * load) * env.longSlideMultiplier) * friction) / rubberSpring) * signOf(slipVel);

        const float dLat = dt * latVel;
        const float dLong = dt * slipVel;
        const float oldLong = currentTireDispLong;
        const float oldLat = currentTireDispLat;

        currentTireDispLat = advanceDisp(currentTireDispLat, dLat, latTarget);
        float v = 0.0f;
        if (signOf(dLat) == signOf(latTarget)) {
            const float at = std::abs(latTarget), ad = std::abs(dLat);
            v = (at < ad ? at : ad) * signOf(latTarget);
        }
        const float realism = env.realism;
        const float latRate = (currentTireDispLat - oldLat) * realism + (1.0f - realism) * v;

        currentTireDispLong = advanceDisp(currentTireDispLong, dLong, longTarget);

        tireGripLat = -(currentTireDispLat * rubberSpringLat) - (env.invDt * latRate) * rubberDampLat;
        tireGripLong = -(currentTireDispLong * rubberSpring) -
                       (env.invDt * (currentTireDispLong - oldLong)) * rubberDamp;
        tireResistance = tireGripLong * radius;

        Vec3 force{(tireGripLat * sm.m0.x - tireGripLong * sm.m2.x) + suspF.x,
                   (tireGripLat * sm.m0.y - tireGripLong * sm.m2.y) + suspF.y,
                   (tireGripLat * sm.m0.z - tireGripLong * sm.m2.z) + suspF.z};

        // MM2 rolling and scrub drag (TireDragCoefLong/Lat, material drag):
        // inferred, applied straight to the body so the wheel spin solver is
        // unaffected. Faded in below 0.5 m/s to avoid chatter at rest.
        if (tireDragCoefLong != 0.0f || tireDragCoefLat != 0.0f || (material && material->drag != 0.0f)) {
            const float matDrag = material ? material->drag : 0.0f;
            const float longDrag = (tireDragCoefLong + matDrag) * load *
                                   clampf(std::abs(fwdVel) * 2.0f, 0.0f, 1.0f) * signOf(fwdVel);
            const float latDrag =
                tireDragCoefLat * load * clampf(std::abs(latVel) * 2.0f, 0.0f, 1.0f) * signOf(latVel);
            force -= fwd * longDrag;
            force -= sm.m0 * latDrag;
        }

        if (static_cast<double>(brakingInput) > 0.5) {
            skidVelocity = cv;
        } else {
            const float lv = (cv.x * sm.m0.x + cv.y * sm.m0.y) + cv.z * sm.m0.z;
            skidVelocity = sm.m0 * lv;
        }
        ics.applyForce(force, isect.position);
    } else {
        onGround = false;
        currentTireDispLat = 0.0f;
        currentTireDispLong = 0.0f;
        suspension = -suspensionExtent;
        tireResistance = 0.0f;
        tireGripLat = 0.0f;
        tireGripLong = 0.0f;
    }

    // Visual matrix (relative to the car's CG frame).
    rotation = dt * rotationSpeed + rotation;
    visualMatrix = Mat34::identity();
    age::rotate(visualMatrix, kZAxis, wobble);
    age::rotate(visualMatrix, kXAxis, rotation);
    if (!(flags & kHandbrake))
        age::rotate(visualMatrix, kYAxis, steering);
    float s = suspension;
    if (s <= -suspensionExtent)
        s = -suspensionExtent;
    else if (!(s < renderableSuspensionLimit))
        s = renderableSuspensionLimit;
    visualMatrix.m3 = {center.x - age::mulD(currentTireDispLat, 0.01), center.y + s,
                       center.z - age::mulD(currentTireDispLong, -0.01)};
}

} // namespace mm2::phys
