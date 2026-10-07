#pragma once

#include "phys/Bound.h"
#include "phys/vehicle/TuneParams.h"
#include "phys/vehicle/VehicleGeometry.h"

namespace mm2::phys {

class InertialCS;
class GroundQuery;
struct Material;

// Per-sample inputs a wheel needs from its car.
struct WheelEnv {
    const InertialCS* ics = nullptr;
    // Frame the wheel centre is expressed in (the car's CG frame).
    const Mat34* frame = nullptr;
    const GroundQuery* ground = nullptr;
    float dt = 0;
    float invDt = 0;
    float weatherFriction = 1.0f;     // ?WeatherFriction@@3MA (MM2: 0.8 snow, 0.75 snow at night)
    bool indoors = false;             // room flags & 3: weather does not apply
    float carFrictionHandling = 1.0f; // vehCarSim CarFrictionHandling
    float longSlideMultiplier = 1.0f; // MM1-only field; MM2 has none
    float realism = 1.0f;             // *mmCarSim::Realism (MM1)
};

// vehWheel, ported from Midtown Madness 1's mmWheel (Open1560 game.asm).
//
// Suspension: a probe from SuspensionLimit + Radius above the wheel centre to
// (droop + Radius) below finds the ground. Suspension (compression from rest)
// drives Spring/Damping on top of the static load NormalLoad.
//
// Tyre: a displacement ("rubber") model per direction. Each sample the
// contact patch displacement moves with the slip velocity towards a target
// = friction(slip) * load * Friction / RubberSpring, never past it; the tyre
// force is -RubberSpring * disp - RubberDamp * disp rate. friction(slip) is
// StaticFric * (2s/s0 - s^2/s0^2) up to the point where it falls to
// SlidingFric (mmWheel::ComputeConstants).
//
// MM2 replaced MM1's tuning fields; configure() maps them (inferred, see
// docs/physics.md): Spring = SuspensionFactor * NormalLoad / SuspensionExtent,
// Damping = SuspensionDampCoef * Spring, RubberSpring = StaticFric * NormalLoad
// / TireDispLimit, RubberDamp = TireDampCoef * 2 sqrt(RubberSpring * m/n).
class Wheel {
public:
    enum Flags : int {
        kUseIcsWorld = 1,       // frame from the ICS instead of the car LCS
        kHandbrake = 2,         // back wheel (handbrake acts here)
        kUseLinearVelocity = 4, // driven wheel: ICS LinearVelocity instead of FrameVelocity
    };

    // mmWheel::Init + MM2 parameter mapping. `centerFromCg` is the wheel
    // centre relative to the car's centre of gravity, in car space.
    void init(const WheelParams& p, const WheelGeometry& g, const Vec3& centerFromCg, float carMass,
              float gravity, int numWheels, int flags);
    // mmWheel::ComputeConstants: friction curve coefficients.
    void computeConstants();
    void reset();
    // mmWheel::SetInputs.
    void setInputs(float steer, float brake) {
        steeringInput = steer;
        brakingInput = brake;
    }

    // Friction coefficient for a slip ratio (the curve used by update()).
    float frictionForSlip(float slip, bool lateral) const;

    // mmWheel::ComputeDwtdw, split in two so that the drivetrain can evaluate
    // the tyre torque between the halves (MM1 calls it after summing the
    // torques; nothing in the first half depends on them).
    //
    // probe(): the ground probe, suspension and load, and the contact
    // velocity along the wheel's heading (contactForwardVel). Returns hit.
    bool probe(const WheelEnv& env);
    // computeLimits(): the wheel speed B at which the slip reaches the
    // optimum (the drivetrain's breakpoint), the tyre torque slope
    // d(TireResistance)/d(w) below it (A, also returned) and past it (C = 0).
    // MM1 build 1560 discards the slope (Drivetrain::mm1ExplicitSpin); ours
    // is the exact per-sample slope of the tyre model (see Wheel.cpp).
    float computeLimits(float net, float& A, float& B, float& C, const WheelEnv& env) const;

    // OpenMM2: the TireResistance update() would produce this sample if the
    // wheel kept its current speed (same operations as update(); valid after
    // probe()). The drivetrain's implicit step is linearised about it.
    float predictTireResistance(const WheelEnv& env) const;

    // Surface friction: material x WeatherFriction (outdoors), adjusted by
    // CarFrictionHandling below 1 (mmWheel::Update).
    float surfaceFriction(const WheelEnv& env) const;

    // mmWheel::Update: tyre forces (applied to `ics`) and visual state.
    void update(InertialCS& ics, const WheelEnv& env);

    // --- Tune (MM1 member names; filled by init) ---
    float spring = 40000.0f;
    float damping = 4000.0f;
    float steeringRatio = 0.5f;    // MM2 SteeringLimit
    float brakeRatio = 0.85f;      // MM2 BrakeCoef
    float handbrakeCoef = 1.0f;    // MM2 HandbrakeCoef
    float suspensionLimit = 0.1f;  // compression travel
    float suspensionExtent = 0.1f; // droop travel (MM2; MM1 used SuspensionLimit)
    float renderableSuspensionLimit = 0.1f;
    float rubberSpring = 40000.0f;
    float rubberDamp = 2000.0f;
    float optimumSlipPercent = 0.14f;
    float staticFric = 0.8f;
    float slidingFric = 0.4f;
    float rubberSpringLat = 40000.0f;
    float rubberDampLat = 2000.0f;
    float tireDragCoefLong = 0.0f; // MM2 rolling resistance (inferred use)
    float tireDragCoefLat = 0.0f;
    float steeringOffset = 0.0f; // MM2; visual only (inferred)
    float camberLimit = 0.0f;    // MM2; visual only
    float wobbleLimit = 0.0f;    // MM2; visual only

    // --- Geometry ---
    Vec3 center; // relative to the CG
    float radius = 0.3f;
    float width = 0.1f;
    int flags = 0;
    int numWheels = 4;

    // --- Constants ---
    float normalLoad = 0.0f;   // static load per wheel
    float unkFriction1 = 0.0f; // StaticFric * FricMultiplier / s0^2
    float unkFriction2 = 0.0f; // slip at which the curve reaches SlidingFric
    float fricMultiplier = 1.0f;
    float steerMultiplier = 1.0f;

    // --- State ---
    bool hit = false; // dword168: probe found ground this sample
    RayHit intersection;
    const Material* material = nullptr;
    Vec3 position;
    bool onGround = false;
    float friction = 1.0f;
    float currentTireDispLat = 0.0f;
    float currentTireDispLong = 0.0f;
    float tireGripLat = 0.0f;
    float tireGripLong = 0.0f;
    float tireResistance = 0.0f;
    float rotationSpeed = 0.0f; // rad/s, negative when rolling forward
    float latSlipPercent = 0.0f;
    float longSlipPercent = 0.0f;
    float steeringInput = 0.0f;
    float brakingInput = 0.0f;
    float steering = 0.0f;
    float suspension = 0.0f;         // compression from rest (m)
    float suspensionVelocity = 0.0f; // dword1E4
    float currentLoad = 0.0f;
    float rollingRotation = 0.0f;   // MaybeGrip: forward speed / -radius
    float contactForwardVel = 0.0f; // probe(): contact velocity along the heading
    float rotation = 0.0f;          // accumulated spin (visual)
    float wobble = 0.0f;
    Vec3 planeVelocity; // field_178: contact velocity in the ground plane
    Vec3 skidVelocity;  // field_16C: for skid marks
    Mat34 visualMatrix; // asLinearCS::Matrix, relative to the car frame
};

} // namespace mm2::phys
