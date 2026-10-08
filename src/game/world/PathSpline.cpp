// gizPathspline, ported from the code of midtown2.exe build 3393 (MM2Recomp;
// documentation only).
#include "game/world/PathSpline.h"

#include <cmath>
#include <utility>

namespace mm2::game::world {
namespace {

// Matrix44::Hermite, which the gizPathspline constructor sets up: row r
// gives the coefficient of t^(3-r) as weights of (p0, p1, t0, t1).
constexpr float kHermite[4][4] = {
    {2.0f, -2.0f, 1.0f, 1.0f},
    {-3.0f, 3.0f, -2.0f, -1.0f},
    {0.0f, 0.0f, 1.0f, 0.0f},
    {1.0f, 0.0f, 0.0f, 0.0f},
};

// The x and y rows of gizPathspline::Compute, summed in its order (the
// asm's: p1, t0, t1, then p0 for three coefficients; t1, t0, p1, p0 for the
// t^2 one).
Vec4 coefficientsXY(float p0, float p1, float t0, float t1) {
    const auto& h = kHermite;
    return {((p1 * h[0][1] + t0 * h[0][2]) + t1 * h[0][3]) + h[0][0] * p0,
            ((h[1][3] * t1 + h[1][2] * t0) + h[1][1] * p1) + h[1][0] * p0,
            ((h[2][1] * p1 + t0 * h[2][2]) + t1 * h[2][3]) + h[2][0] * p0,
            ((p1 * h[3][1] + t0 * h[3][2]) + t1 * h[3][3]) + h[3][0] * p0};
}

// The z row: the t^3 coefficient through Vector4::Dot (p0, p1, t0, t1 in
// turn), the others as above.
Vec4 coefficientsZ(float p0, float p1, float t0, float t1) {
    const auto& h = kHermite;
    Vec4 k = coefficientsXY(p0, p1, t0, t1);
    k.x = ((p0 * h[0][0] + p1 * h[0][1]) + t0 * h[0][2]) + t1 * h[0][3];
    return k;
}

float distance(const Vec3& a, const Vec3& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

} // namespace

void PathSpline::init(std::vector<Vec3> points, float speed) {
    m_points = std::move(points);
    m_speed = speed;
    reset();
}

void PathSpline::reset() {
    m_x = m_y = m_z = {};
    m_time = 0.0f;
    m_length = 1.0f;
    // OpenMM2: MM2 sets up any path; one with fewer than two points would
    // divide by a zero length (no retail path has one).
    if (!usable())
        return;
    m_index = vertexCount() - 1;
    incrementPath();
}

void PathSpline::compute(const Vec3& p0, const Vec3& p1, const Vec3& t0, const Vec3& t1) {
    m_x = coefficientsXY(p0.x, p1.x, t0.x, t1.x);
    m_y = coefficientsXY(p0.y, p1.y, t0.y, t1.y);
    m_z = coefficientsZ(p0.z, p1.z, t0.z, t1.z);
}

void PathSpline::solve(Vec3& position, Vec3& direction, float t) const {
    position = {((t * m_x.x + m_x.y) * t + m_x.z) * t + m_x.w, ((t * m_y.x + m_y.y) * t + m_y.z) * t + m_y.w,
                ((t * m_z.x + m_z.y) * t + m_z.z) * t + m_z.w};
    const float t2 = t * t * 3.0f;
    const float t1 = t + t;
    direction = {t2 * m_x.x + t1 * m_x.y + m_x.z, t2 * m_y.x + t1 * m_y.y + m_y.z, t2 * m_z.x + t1 * m_z.y + m_z.z};
}

void PathSpline::update(Vec3& position, Vec3& direction, float dt) {
    m_time = dt + m_time;
    updateRatio(position, direction, currentRatio());
}

void PathSpline::updateRatio(Vec3& position, Vec3& direction, float ratio) {
    if (!usable())
        return;
    if (vertexCount() == 2) {
        position = m_points[0];
        direction = m_points[1] - m_points[0];
        return;
    }
    if (1.0f < ratio) {
        incrementPath();
        ratio = ratio - 1.0f;
        m_time = (ratio * m_length) / m_speed;
    }
    if (ratio < 0.0f) {
        decrementPath();
        ratio = ratio + 1.0f;
        m_time = (ratio * m_length) / m_speed;
    }
    solve(position, direction, ratio);
}

void PathSpline::computePath(int before, int to, int after) {
    const Vec3 p0 = vertex(m_index);
    const Vec3 p1 = vertex(to);
    const Vec3& b = vertex(before);
    const Vec3& a = vertex(after);
    const Vec3 t0{(p1.x - b.x) * 0.5f, (p1.y - b.y) * 0.5f, (p1.z - b.z) * 0.5f};
    const Vec3 t1{(a.x - p0.x) * 0.5f, (a.y - p0.y) * 0.5f, (a.z - p0.z) * 0.5f};
    compute(p0, p1, t0, t1);
    Vec3 middle, tangent;
    solve(middle, tangent, 0.5f);
    m_next = to;
    m_length = distance(p0, middle) + distance(middle, p1);
}

void PathSpline::incrementPath() {
    const int n = vertexCount();
    const int from = m_index;
    m_index = from + 1;
    if (m_index == n)
        m_index = 0;
    int to = m_index + 1;
    if (to == n)
        to = 0;
    int after = to + 1;
    if (after == n)
        after = 0;
    computePath(from, to, after);
}

void PathSpline::decrementPath() {
    const int n = vertexCount();
    const int from = m_index;
    if (from == 0)
        m_index = n;
    m_index = m_index - 1;
    int before = m_index;
    if (before == 0)
        before = n;
    int after = from + 1;
    if (after == n)
        after = 0;
    computePath(before - 1, from, after);
}

} // namespace mm2::game::world
