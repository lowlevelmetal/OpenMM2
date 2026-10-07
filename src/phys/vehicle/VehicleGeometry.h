#pragma once

#include "core/Math.h"

#include <array>
#include <vector>

namespace mm2::phys {

// Geometric inputs for a vehicle simulation, in car model space (the model's
// origin sits near the ground under the body centre; -Z is forward).
// The game fills this from the car's assets:
//   * wheels from the pivot matrices geometry/<car>_whl0..3.mtx: center =
//     row m3, radius = |m1.y - m0.y| / 2, width = m1.x - m0.x (rows m0/m1 are
//     the wheel mesh's bounding box). This mirrors vehWheel::Init as
//     documented by mm2hook. wheelFromPivot() does the conversion.
//   * body from the collision bound bound/<car>_bound.bnd (its box).
struct WheelGeometry {
    Vec3 center;
    float radius = 0.33f;
    float width = 0.25f;
    bool present = false;
};

struct VehicleGeometry {
    // 0 front-left (WHL0), 1 front-right (WHL1), 2 back-left (WHL2),
    // 3 back-right (WHL3).
    std::array<WheelGeometry, 4> wheels;
    // WHL4/WHL5: optional second rear axle (e.g. vpsemi). In the original
    // these follow WHL2/WHL3 visually and are not simulated separately.
    std::array<WheelGeometry, 2> extraWheels;
    // Collision box of the body.
    Aabb body;
    // Optional convex hull points of the body bound (unused for now; the
    // body collides as `body`'s box).
    std::vector<Vec3> hull;

    static WheelGeometry wheelFromPivot(const Mat34& pivot);

    // Generic sedan used when real geometry is not supplied: wheels at
    // x = +-0.78, y = 0.33, z = -1.25 (front) / +1.25 (back), radius 0.33 m,
    // width 0.25 m; body box x +-0.9, y 0.25..1.45, z -2.2..2.2.
    static VehicleGeometry placeholder();
};

} // namespace mm2::phys
