#pragma once

#include "core/Math.h"

#include <array>

namespace mm2::game {

// A view in the Angel convention: `transform` is the camera's placement in
// the world (m0 right, m1 up, m2 back, m3 position); it looks down -m2.
struct Camera {
    Mat34 transform;
    // Horizontal field of view on a 4:3 screen, radians; render::computeProjection
    // widens it for other aspect ratios. The car cameras convert their
    // vertical CameraFOV to it (cam::horizontalFov4x3).
    float horizontalFov = 1.2217f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

    Mat44 view() const { return Mat44::fromMat34(transform.fastInverse()); }
    Vec3 position() const { return transform.m3; }
    Vec3 forward() const { return -transform.m2; }

    // Builds a camera at `eye` looking at `target` with world up +Y.
    static Mat34 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up = Vec3::yAxis());
};

// View frustum for culling, from a combined view * projection matrix.
class Frustum {
public:
    explicit Frustum(const Mat44& viewProj);
    bool intersects(const Aabb& box) const;
    bool intersectsSphere(const Vec3& center, float radius) const;

private:
    std::array<Vec4, 6> m_planes; // ax + by + cz + d >= 0 inside
};

} // namespace mm2::game
