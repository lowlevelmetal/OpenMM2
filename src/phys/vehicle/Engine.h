#pragma once

#include "phys/vehicle/TuneParams.h"

namespace mm2::phys {

class Transmission;
class Drivetrain;

// vehEngine, ported from Midtown Madness 1's mmEngine (Open1560 game.asm).
//
// The torque curve is the classic power curve P(x) = Pmax (x + x^2 - x^3),
// x = w / w_opt, i.e. T = Pmax/w_opt (1 + x - x^2), written as
// (phi*w_opt - w)(w_opt/phi + w) * Pmax / w_opt^3 with phi = (sqrt5+1)/2
// (PullUpModifier / BreakDownModifier). Above OptRPM it is additionally
// tapered by (MaxRPM + w - 2 OptRPM)(MaxRPM - w)/(MaxRPM - OptRPM)^2, reaching
// zero at MaxRPM. MM2's vehEngine stores the same sqrt(5)+-1 constants but has
// no TorqueDropoff member, and MM2 tunes such as vpdb7 (UpshiftBias 0.002)
// only shift if the engine can reach MaxRPM, so by default the taper is off
// and the curve runs to the rev limit (mm1TorqueTaper restores MM1).
class Engine {
public:
    void configure(const EngineParams& p);
    void reset();

    // mmEngine::ComputeConstants.
    void computeConstants();
    // At the current rotationSpeed.
    float calcTorqueAtFullThrottle() const;
    float calcTorqueAtZeroThrottle() const;
    float calcTorque(float throttle) const;

    // mmEngine::Update. `primary` is the engine-driven drivetrain; it is
    // detached while the transmission is in neutral.
    void update(float dt, Transmission& trans, Drivetrain& primary);

    // --- Tune (mmEngine / vehEngine fields) ---
    float maxHorsePower = 370.0f;
    float optRPM = 5800.0f;
    float maxRPM = 8500.0f;
    float gcl = 0.25f; // gear change lag: no torque for this long after a shift
    float hpScale = 1.0f;
    bool mm1TorqueTaper = false;
    float idleRPM = 750.0f; // MM2 field; not used by the ported physics (see docs)
    // MM2 Engine.AngInertia; replaces MM1's car mass * 0.001 as the engine's
    // inertia while the clutch is open.
    float angInertia = 1.0f;

    // --- Constants ---
    float maxRotationSpeed = 0;
    float optRotationSpeed = 0;
    float torqueDropoff = 0;
    float torqueCoef = 0;        // Pmax / w_opt^3 ("MaxTorque" in Open1560's header)
    float pullUpModifier = 0;    // (sqrt5 + 1) / 2
    float breakDownModifier = 0; // (sqrt5 - 1) / 2

    // --- State ---
    float prevGearRPM = 0;
    float gearChangeTime = 0;
    bool changingGear = false;
    float throttle = 0;
    float maxThrottle = 1.0f; // field_60: mmStuck::Pegged compares against 0.75 of this
    float rotationSpeed = 0;  // rad/s
    float rpm = 0;
    float horsepower = 0;
    float torque = 0;
};

} // namespace mm2::phys
