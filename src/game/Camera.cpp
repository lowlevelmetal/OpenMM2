#include "game/Camera.h"

namespace mm2::game {

Mat34 Camera::lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    Mat34 m;
    Vec3 back = (eye - target).normalized();
    if (back.mag2() < 1e-12f)
        back = Vec3::zAxis();
    Vec3 right = up.cross(back);
    if (right.mag2() < 1e-12f)
        right = Vec3::xAxis();
    right = right.normalized();
    m.m0 = right;
    m.m1 = back.cross(right);
    m.m2 = back;
    m.m3 = eye;
    return m;
}

Frustum::Frustum(const Mat44& vp) {
    // Row-vector convention: clip = p * M, so the clip components are the
    // dot products of p with the matrix columns.
    auto col = [&](int c) { return Vec4{vp.m[0][c], vp.m[1][c], vp.m[2][c], vp.m[3][c]}; };
    const Vec4 x = col(0), y = col(1), z = col(2), w = col(3);
    m_planes = {w + x, w - x, w + y, w - y, z, w - z}; // depth range 0..1
    for (auto& p : m_planes) {
        const float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        if (len > 0)
            p = p * (1.0f / len);
    }
}

bool Frustum::intersects(const Aabb& b) const {
    if (!b.valid())
        return false;
    for (const auto& p : m_planes) {
        const Vec3 positive{p.x >= 0 ? b.max.x : b.min.x, p.y >= 0 ? b.max.y : b.min.y, p.z >= 0 ? b.max.z : b.min.z};
        if (p.x * positive.x + p.y * positive.y + p.z * positive.z + p.w < 0)
            return false;
    }
    return true;
}

bool Frustum::intersectsSphere(const Vec3& c, float r) const {
    for (const auto& p : m_planes)
        if (p.x * c.x + p.y * c.y + p.z * c.z + p.w < -r)
            return false;
    return true;
}

} // namespace mm2::game
