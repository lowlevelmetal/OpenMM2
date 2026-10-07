// vehSplash from Midtown Madness 2 (Init, Reset, Activate, Update), verified
// against the build 3393 code (MM2Recomp). See docs/physics.md.

#include "phys/vehicle/Splash.h"

#include "phys/InertialCS.h"

#include <cmath>

namespace mm2::phys {

void Splash::init(const Vec3& min, const Vec3& max) {
    // (The original first fills the points with random directions and then
    // overwrites them with the grid.)
    std::size_t n = 0;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k)
                m_points[n++] = {(max.x - min.x) * static_cast<float>(i) * 0.33333334f + min.x,
                                 (max.y - min.y) * static_cast<float>(j) * 0.33333334f + min.y,
                                 (max.z - min.z) * static_cast<float>(k) * 0.33333334f + min.z};
    reset();
}

void Splash::reset() {
    drag = 0.08f;
    buoyancy = 0.7f;
    m_active = false;
}

void Splash::activate(float level) {
    waterLevel = level;
    m_active = true;
}

void Splash::update(InertialCS& ics, float dt) {
    if (!m_active)
        return;
    const Mat34& m = ics.matrix;
    const float mass = ics.mass;
    const float up = buoyancy;
    for (const Vec3& p : m_points) {
        const Vec3 w = m.transform(p);
        if (waterLevel < w.y)
            continue;
        const Vec3 r = w - m.m3;
        const Vec3& om = ics.angularVelocity;
        Vec3 v{r.z * om.y - r.y * om.z, r.x * om.z - r.z * om.x, r.y * om.x - r.x * om.y};
        v = v + ics.linearVelocity;
        // The original adds the unit vector of the body's world position to
        // the point velocity (a quirk of vehSplash::Update, kept).
        const float p2 = m.m3.mag2();
        const float inv = p2 == 0.0f ? 0.0f : 1.0f / std::sqrt(p2);
        v = v + m.m3 * inv;
        const Vec3 f{-(v.x * drag * mass), mass * up - v.y * drag * mass, -(v.z * drag * mass)};
        ics.applyForce(f, w);
    }
    if (0.4f < buoyancy) {
        float b = buoyancy - dt * 0.03f;
        if (b < 0.4f)
            b = 0.4f;
        else if (0.7f < b)
            b = 0.7f;
        buoyancy = b;
    }
}

} // namespace mm2::phys
