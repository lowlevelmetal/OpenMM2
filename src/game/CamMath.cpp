// Angel engine matrix helpers for the cameras.
// Ported from Open1560 (code/midtown/game.asm, Midtown Madness 1 build 1560),
// GPL-3.0, Copyright (C) Brick. See CamMath.h.
#include "game/CamMath.h"

#include <cmath>

namespace mm2::game::cam {
namespace {

// ArbitraryRotation: transpose of the Rodrigues matrix (row-vector form),
// general-case operation order. The original has separate paths when one axis
// component is exactly zero; they compute the same values with the zero
// terms dropped.
Mat34 arbitraryRotation(Vec3 k, float angle) {
    Mat34 r;
    const float m2 = mag2(k);
    if (m2 < 1e-11f) // flt_621BDC
        return r;    // identity
    if (m2 > 1.00000095f || m2 < 0.999989986f) { // flt_621BE0 / flt_621BE4
        const float inv = 1.0f / std::sqrt(m2);
        k = {k.x * inv, k.y * inv, k.z * inv};
    }
    const float c = std::cos(angle), s = std::sin(angle);
    const float t = 1.0f - c;
    const float ty = t * k.y, tz = t * k.z;
    const float xty = k.x * ty, xtz = k.x * tz, ytz = k.y * tz;
    r.m0 = {t * k.x * k.x + c, xty + k.z * s, xtz - k.y * s};
    r.m1 = {xty - k.z * s, k.y * ty + c, ytz + k.x * s};
    r.m2 = {xtz + k.y * s, ytz - k.x * s, k.z * tz + c};
    return r;
}

// Matrix34::Dot(a, b) with b.m3 == 0: every row (including m3) times b's 3x3.
Vec3 rowTimes(const Vec3& v, const Mat34& b) { return b.m0 * v.x + b.m1 * v.y + b.m2 * v.z; }

} // namespace

float mag(const Vec3& v) { return std::sqrt(mag2(v)); }

float invMag(const Vec3& v) {
    const float m2 = mag2(v);
    return m2 > 0.0f ? 1.0f / std::sqrt(m2) : 0.0f;
}

void lookAt(Mat34& m, const Vec3& from, const Vec3& to) {
    m.m3 = from;
    const Vec3 d = from - to;
    const float s = invMag(d);
    m.m2 = {s * d.x, s * d.y, s * d.z};
    // YAXIS x m2 with YAXIS = (0, 1, 0), written out as in the original.
    constexpr float yx = 0.0f, yy = 1.0f, yz = 0.0f;
    const Vec3& b = m.m2;
    const Vec3 side{yy * b.z - yz * b.y, yz * b.x - yx * b.z, yx * b.y - yy * b.x};
    const float s0 = invMag(side);
    m.m0 = {s0 * side.x, s0 * side.y, s0 * side.z};
    const Vec3& a = m.m0;
    const Vec3 up{a.z * b.y - b.z * a.y, a.x * b.z - b.x * a.z, b.x * a.y - a.x * b.y};
    const float s1 = invMag(up);
    m.m1 = {s1 * up.x, s1 * up.y, s1 * up.z};
}

Vec3 getEulersZXY(const Mat34& m) {
    Vec3 e;
    // z = atan2(m01, m11), y = atan2(m20, m22), x = asin(-m21) (clamped).
    e.z = (m.m1.y == 0.0f && m.m0.y == 0.0f) ? 0.0f : std::atan2(m.m0.y, m.m1.y);
    e.y = (m.m2.z == 0.0f && m.m2.x == 0.0f) ? 0.0f : std::atan2(m.m2.x, m.m2.z);
    const float sx = -m.m2.y;
    if (sx < -1.0f)
        e.x = -kHalfPi;
    else if (sx > 1.0f)
        e.x = kHalfPi;
    else
        e.x = std::asin(sx);
    return e;
}

void fromEulersZXY(Mat34& m, const Vec3& e) {
    const float sx = e.x == 0.0f ? 0.0f : std::sin(e.x), cx = e.x == 0.0f ? 1.0f : std::cos(e.x);
    const float sy = e.y == 0.0f ? 0.0f : std::sin(e.y), cy = e.y == 0.0f ? 1.0f : std::cos(e.y);
    const float sz = e.z == 0.0f ? 0.0f : std::sin(e.z), cz = e.z == 0.0f ? 1.0f : std::cos(e.z);
    m.m0 = {sx * (sz * sy) + cz * cy, cx * sz, sx * (cy * sz) - cz * sy};
    m.m1 = {sx * (cz * sy) - cy * sz, cz * cx, sx * (cz * cy) + sz * sy};
    m.m2 = {cx * sy, -sx, cy * cx};
}

void rotateFullX(Mat34& m, float angle) {
    if (angle == 0.0f)
        return;
    const float c = std::cos(angle), s = std::sin(angle);
    for (int i = 0; i < 4; ++i) {
        Vec3& r = m.row(i);
        const float y = r.y, z = r.z;
        r.y = c * y - s * z;
        r.z = c * z + s * y;
    }
}

void rotateFullY(Mat34& m, float angle) {
    if (angle == 0.0f)
        return;
    const float c = std::cos(angle), s = std::sin(angle);
    for (int i = 0; i < 4; ++i) {
        Vec3& r = m.row(i);
        const float x = r.x, z = r.z;
        r.x = s * z + c * x;
        r.z = c * z - s * x;
    }
}

void rotateFullZ(Mat34& m, float angle) {
    if (angle == 0.0f)
        return;
    const float c = std::cos(angle), s = std::sin(angle);
    for (int i = 0; i < 4; ++i) {
        Vec3& r = m.row(i);
        const float x = r.x, y = r.y;
        r.x = c * x - s * y;
        r.y = c * y + s * x;
    }
}

void rotateAxis(Mat34& m, const Vec3& axis, float angle, bool full) {
    if (angle == 0.0f)
        return;
    const Mat34 r = arbitraryRotation(axis, angle);
    m.m0 = rowTimes(m.m0, r);
    m.m1 = rowTimes(m.m1, r);
    m.m2 = rowTimes(m.m2, r);
    if (full)
        m.m3 = rowTimes(m.m3, r);
}

void polarView(Mat34& m, float offz, float roty, float rotx, float rotz) {
    m = Mat34::identity();
    m.m3.z += offz;
    rotateFullZ(m, -rotz);
    rotateFullX(m, -rotx);
    rotateFullY(m, roty);
}

bool approach(Vec3& v, const Vec3& goal, float speed, float dt, float* carry) {
    const Vec3 d = goal - v;
    const float dist2 = d.x * d.x + (d.y * d.y + d.z * d.z);
    if (!(dist2 > 1e-4f)) { // flt_621B74
        v = goal;
        if (carry)
            *carry = dt * speed;
        return true;
    }
    float step = dt * speed;
    if (carry)
        step = *carry + step;
    if (step * step < dist2) {
        const float k = step / std::sqrt(dist2);
        v = {k * d.x + v.x, k * d.y + v.y, k * d.z + v.z};
        if (carry)
            *carry = 0.0f;
        return false;
    }
    v = goal;
    if (carry)
        *carry = step - std::sqrt(dist2);
    return false;
}

} // namespace mm2::game::cam
