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
// implicitly:
//     w' = w + dt * (-net) / (I + dt * D)
// where net = engine torque * gear ratio - sum of tyre resistance, adjusted by
// the brakes (which cannot reverse the wheel), D = 300 (+ tyre slopes, which
// are zero in this engine build) and I = an effective inertia. If w' crosses
// the wheel-speed limit reported by Wheel::computeDwtdw, the step stops there
// and continues past it (piecewise-linear solve). Finally the wheel speed is
// limited by the engine's MaxRPM in the current gear and the engine speed is
// set from it.
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

private:
    std::array<Wheel*, 4> m_wheels{};
    int m_numWheels = 0;
    Engine* m_engine = nullptr;
    Transmission* m_trans = nullptr;
    Engine* m_lastEngine = nullptr;
    Transmission* m_lastTrans = nullptr;
};

} // namespace mm2::phys
