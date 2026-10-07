#pragma once

#include "phys/vehicle/TuneParams.h"
#include "phys/vehicle/Wheel.h"

#include <array>

namespace mm2::phys {

class Engine;
class Transmission;

// vehDrivetrain, ported from Midtown Madness 1's mmDrivetrain.
//
// A drivetrain is a set of wheels that spin together. The engine-driven one is
// "attached" to the engine and transmission; the others (Freetrain) only feel
// brakes and tyre forces. Each sample it integrates the shared wheel speed
// with one linearly implicit step:
//     w' = w + dt * (-net) / (I + dt * D)
// where net = engine torque * gear ratio - sum of tyre torques
// (TireResistance), adjusted by the brakes (which cannot reverse the wheel),
// D = 300 + the tyres' slopes d(TireResistance)/d(w) and I = an effective
// inertia. If w' crosses the wheel speed B reported by Wheel::computeLimits
// (optimum slip), the step stops there and continues without that wheel's
// slope (piecewise-linear solve). Finally the wheel speed is limited by the
// engine's MaxRPM in the current gear and the engine speed is set from it.
//
// MM1 build 1560 discards the tyre slopes (D = 300) and sums the tyre torques
// of the previous sample, so the wheel and tyre are coupled explicitly. With
// MM2's stiffer tyres that diverges at 60 Hz (vp4x4, several *_opp tunes), so
// OpenMM2 keeps the slopes, with MM1's first slope term corrected by dt, and
// linearises about the tyre torque at the current body velocity
// (Wheel::predictTireResistance), so the slopes stabilise the step without
// acting as extra inertia: in steady acceleration I + dt * 300 resists the
// wheel, as in MM1. mm1ExplicitSpin restores the 1560 behaviour.
class Drivetrain {
public:
    // MM2: AngInertia and BrakeDynamicCoef/BrakeStaticCoef replace MM1's
    // mass-derived constants (dyn_coeff = 2 * mass, stat_coeff = 2.4 * mass,
    // inertia = mass * (0.02 + 0.0002 r^2) attached / mass * 0.01 free).
    // Mapping (inferred, exact for MM1 when AngInertia = 2 * mass and
    // Engine.AngInertia = mass / 1000): dyn = AngInertia * BrakeDynamicCoef,
    // stat = AngInertia * BrakeStaticCoef, inertia = AngInertia * 0.01 +
    // 0.2 * Engine.AngInertia * r^2 attached, AngInertia * 0.005 free.
    void configure(const DrivetrainParams& p);

    bool addWheel(Wheel* w);
    void attach(Engine* engine, Transmission* trans);
    void attach(); // re-attach to the last engine/transmission
    void detach();
    bool attached() const { return m_engine != nullptr; }

    // mmDrivetrain::Update, then the wheels' own updates.
    void update(InertialCS& ics, const WheelEnv& env, float brakes, float handBrake);

    int numWheels() const { return m_numWheels; }
    Wheel* wheel(int i) const { return m_wheels[static_cast<std::size_t>(i)]; }

    float angInertia = 2000.0f;
    float dynCoeff = 2000.0f;
    float statCoeff = 2400.0f;
    float engineAngInertia = 1.0f; // copied from the engine when attached
    // MM1 build 1560: tyre slopes discarded, previous sample's tyre torques
    // (explicit coupling; unstable for stiff tyres at 60 Hz).
    bool mm1ExplicitSpin = false;

private:
    std::array<Wheel*, 4> m_wheels{};
    int m_numWheels = 0;
    Engine* m_engine = nullptr;
    Transmission* m_trans = nullptr;
    Engine* m_lastEngine = nullptr;
    Transmission* m_lastTrans = nullptr;
};

} // namespace mm2::phys
