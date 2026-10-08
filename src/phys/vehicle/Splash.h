#pragma once

#include "core/Math.h"

#include <array>

namespace mm2::phys {

class InertialCS;

// vehSplash (Midtown Madness 2), verified against the build 3393 code: what
// keeps a car afloat after it drives into water. A 4 x 4 x 4 grid of points
// spans the car's InertiaBox around its model origin; every point below the
// water level pushes the body up with Mass * buoyancy and drags it with
// -0.08 * Mass * the point's velocity. The buoyancy starts at 0.7 per point
// (64 of them against gravity's 19.6) and sinks towards 0.4 at 0.03 per
// second, so a car first bobs high and then settles lower.
class Splash {
public:
    // vehSplash::Init: points from `min` to `max` in the body's frame.
    void init(const Vec3& min, const Vec3& max);
    // vehSplash::Reset (vehSplash::vehSplash runs it once; the members start
    // at its values).
    void reset();
    // vehSplash::Activate: the car is in a water room below its level.
    void activate(float waterLevel);
    // vehCar::Reset: clears the active flag only (the buoyancy stays).
    void deactivate() { m_active = false; }
    bool active() const { return m_active; }
    // vehSplash::Update: forces for the next sample.
    void update(InertialCS& ics, float dt);

    float buoyancy = 0.7f; // per point, times the mass
    float drag = 0.08f;
    float waterLevel = 0.0f;

private:
    std::array<Vec3, 64> m_points{};
    bool m_active = false;
};

} // namespace mm2::phys
