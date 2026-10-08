#pragma once

// gizPathspline (midtown2.exe build 3393, MM2Recomp; documentation only):
// the spline the moving gizmos follow (ferries, sailboats and the cars of
// the trains). It runs through the points of one path set path as a closed
// loop of cubic Hermite segments with Catmull-Rom tangents, travelled at a
// constant speed in metres per second.

#include "core/Math.h"

#include <vector>

namespace mm2::game::world {

class PathSpline {
public:
    // gizPathspline::Init: the path's points and the speed, then Reset.
    void init(std::vector<Vec3> points, float speed);
    // gizPathspline::Reset: back to the start of the segment from point 0 to
    // point 1 (the segment before it is the one from the last point).
    void reset();
    // gizPathspline::SetSpeed (metres per second).
    void setSpeed(float speed) { m_speed = speed; }
    float speed() const { return m_speed; }

    // gizPathspline::Update: `dt` seconds further along (negative: back),
    // then the position and the (unnormalised) tangent there.
    void update(Vec3& position, Vec3& direction, float dt);
    // gizPathspline::UpdateRatio: the point at `ratio` of the current
    // segment. Past its end (or before its start) the spline moves one
    // segment on (or back) and keeps the time it has left. A path of exactly
    // two points does not move: its first point, facing the second.
    void updateRatio(Vec3& position, Vec3& direction, float ratio);
    // gizPathspline::GetCurrRatio: speed x time / the segment's length.
    float currentRatio() const { return (m_speed * m_time) / m_length; }

    // gizPathspline::GetNumVertex / GetVertex.
    int vertexCount() const { return static_cast<int>(m_points.size()); }
    const Vec3& vertex(int i) const { return m_points[static_cast<std::size_t>(i)]; }
    // The current segment runs from point index() to point next()
    // (gizPathspline +0x3c and +0x40).
    int index() const { return m_index; }
    int next() const { return m_next; }
    // gizPathspline +0x8: the segment's length (its chord through the middle).
    float length() const { return m_length; }
    // gizPathspline +0x0: the seconds spent on the current segment.
    float time() const { return m_time; }

private:
    // gizPathspline::Compute: the cubic's coefficients for the segment from
    // p0 to p1 with tangents t0 and t1 (the Hermite basis, Matrix44::Hermite).
    void compute(const Vec3& p0, const Vec3& p1, const Vec3& t0, const Vec3& t1);
    // gizPathspline::Solve: the point and the tangent at parameter t.
    void solve(Vec3& position, Vec3& direction, float t) const;
    // gizPathspline::ComputePath: the segment from point index() to point
    // `to`, with the Catmull-Rom tangents of `before` and `after`.
    void computePath(int before, int to, int after);
    void incrementPath(); // gizPathspline::IncrementPath
    void decrementPath(); // gizPathspline::DecrementPath
    bool usable() const { return m_points.size() >= 2; }

    std::vector<Vec3> m_points;
    float m_time = 0.0f;   // +0x00
    float m_speed = 1.0f;  // +0x04
    float m_length = 1.0f; // +0x08
    // +0x0c: per axis the cubic a t^3 + b t^2 + c t + d as (a, b, c, d).
    Vec4 m_x, m_y, m_z;
    int m_index = 0; // +0x3c
    int m_next = 0;  // +0x40
};

} // namespace mm2::game::world
