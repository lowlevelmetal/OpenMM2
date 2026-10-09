#include "app/DrawTrace.h"

#include "core/Log.h"

#include <cstdlib>

namespace mm2::app {

std::unique_ptr<DrawTrace> DrawTrace::fromEnvironment() {
    const char* path = std::getenv("OPENMM2_DEBUG_DRAW_TRACE");
    if (!path || !*path)
        return nullptr;
    std::FILE* f = std::fopen(path, "w");
    if (!f) {
        log::warn("race: cannot write the draw trace {}", path);
        return nullptr;
    }
    std::fprintf(f, "# frame dt steps alpha car(x y z) point(x y z) segment speed other(x y z)\n");
    return std::make_unique<DrawTrace>(f);
}

void DrawTrace::frame(double dt, int steps, float alpha, const Mat34& camera, const Mat34& car, float speed,
                      const std::optional<Mat34>& other) {
    // The roadside point stands 40 m ahead of the car and 6 m to its right
    // until it is behind the camera or far from it; a new point starts a new
    // segment (the analysis differences positions within one).
    const Vec3 ahead = car.m3 - car.m2 * 40.0f + car.m0 * 6.0f;
    if (m_point) {
        const Vec3 v = camera.untransform(*m_point);
        if (v.z > -2.0f || v.mag() > 80.0f)
            m_point.reset();
    }
    if (!m_point) {
        m_point = ahead;
        ++m_segment;
    }
    const Vec3 c = camera.untransform(car.m3);
    const Vec3 p = camera.untransform(*m_point);
    const Vec3 o = other ? camera.untransform(other->m3) : Vec3{};
    std::fprintf(m_file.get(), "%ld %.6f %d %.4f %.5f %.5f %.5f %.5f %.5f %.5f %d %.3f %d %.5f %.5f %.5f\n",
                 m_frame++, dt, steps, alpha, c.x, c.y, c.z, p.x, p.y, p.z, m_segment, speed,
                 other ? 1 : 0, o.x, o.y, o.z);
}

} // namespace mm2::app
