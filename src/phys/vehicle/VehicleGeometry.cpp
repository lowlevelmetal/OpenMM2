#include "phys/vehicle/VehicleGeometry.h"

namespace mm2::phys {

WheelGeometry VehicleGeometry::wheelFromPivot(const Mat34& pivot) {
    WheelGeometry w;
    w.center = pivot.m3;
    w.radius = std::abs(pivot.m1.y - pivot.m0.y) * 0.5f;
    w.width = pivot.m1.x - pivot.m0.x;
    w.present = w.radius > 0.01f;
    return w;
}

VehicleGeometry VehicleGeometry::placeholder() {
    VehicleGeometry g;
    const float x = 0.78f, y = 0.33f, z = 1.25f;
    const Vec3 c[4] = {{-x, y, -z}, {x, y, -z}, {-x, y, z}, {x, y, z}};
    for (int i = 0; i < 4; ++i) {
        g.wheels[static_cast<std::size_t>(i)].center = c[i];
        g.wheels[static_cast<std::size_t>(i)].radius = 0.33f;
        g.wheels[static_cast<std::size_t>(i)].width = 0.25f;
        g.wheels[static_cast<std::size_t>(i)].present = true;
    }
    g.body = Aabb{{-0.9f, 0.25f, -2.2f}, {0.9f, 1.45f, 2.2f}};
    return g;
}

} // namespace mm2::phys
