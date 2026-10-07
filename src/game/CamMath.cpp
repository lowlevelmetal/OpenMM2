// Angel engine matrix helpers for the cameras, ported from Midtown Madness 2's
// Matrix34 and Vector3 (MM2Recomp, build 3393). The sums keep the original
// association, which differs from element to element in Dot / Dot3x3.
#include "game/CamMath.h"

#include <cmath>

namespace mm2::game::cam {
namespace {

// Matrix34::MakeRotateX / Y / Z.
void makeRotateX(Mat34& r, float a) {
    const float c = std::cos(a), s = std::sin(a);
    r.m0 = {1.0f, 0.0f, 0.0f};
    r.m1 = {0.0f, c, s};
    r.m2 = {0.0f, -s, c};
}

void makeRotateY(Mat34& r, float a) {
    const float c = std::cos(a), s = std::sin(a);
    r.m0 = {c, 0.0f, -s};
    r.m1 = {0.0f, 1.0f, 0.0f};
    r.m2 = {s, 0.0f, c};
}

void makeRotateZ(Mat34& r, float a) {
    const float c = std::cos(a), s = std::sin(a);
    r.m0 = {c, s, 0.0f};
    r.m1 = {-s, c, 0.0f};
    r.m2 = {0.0f, 0.0f, 1.0f};
}

// Matrix34::MakeRotateUnitAxis
void makeRotateUnitAxis(Mat34& r, const Vec3& k, float a) {
    const float c = std::cos(a), s = std::sin(a);
    const float t = 1.0f - c;
    r.m0.x = k.x * k.x * t + c;
    r.m1.y = k.y * k.y * t + c;
    r.m2.z = k.z * k.z * t + c;
    r.m0.y = k.y * k.x * t + s * k.z;
    r.m1.x = k.y * k.x * t - s * k.z;
    r.m0.z = k.z * k.x * t - s * k.y;
    r.m2.x = k.z * k.x * t + s * k.y;
    r.m1.z = k.z * k.y * t + s * k.x;
    r.m2.y = k.z * k.y * t - s * k.x;
}

// The 3x3 rows of a * b with Matrix34::Dot's operation order.
void dotRows(Mat34& a, const Mat34& b) {
    const Vec3 r0 = a.m0, r1 = a.m1, r2 = a.m2;
    a.m0 = {(r0.y * b.m1.x + r0.x * b.m0.x) + r0.z * b.m2.x, (r0.y * b.m1.y + r0.z * b.m2.y) + r0.x * b.m0.y,
            (r0.y * b.m1.z + r0.z * b.m2.z) + r0.x * b.m0.z};
    a.m1 = {(r1.y * b.m1.x + r1.z * b.m2.x) + r1.x * b.m0.x, (r1.y * b.m1.y + r1.z * b.m2.y) + r1.x * b.m0.y,
            (r1.y * b.m1.z + r1.x * b.m0.z) + r1.z * b.m2.z};
    a.m2 = {(r2.y * b.m1.x + r2.x * b.m0.x) + r2.z * b.m2.x, (r2.y * b.m1.y + r2.x * b.m0.y) + r2.z * b.m2.y,
            (r2.z * b.m2.z + r2.y * b.m1.z) + r2.x * b.m0.z};
}

} // namespace

float invMag(const Vec3& v) {
    const float m2 = (v.x * v.x + v.y * v.y) + v.z * v.z;
    return m2 == 0.0f ? 0.0f : 1.0f / std::sqrt(m2);
}

float angle(const Vec3& a, const Vec3& b) {
    const float mb = (b.x * b.x + b.y * b.y) + b.z * b.z;
    const float ma = (a.x * a.x + a.y * a.y) + a.z * a.z;
    const float inv = 1.0f / std::sqrt(mb * ma);
    if (inv == 0.0f)
        return 0.0f;
    const float c = ((b.x * a.x + b.z * a.z) + b.y * a.y) * inv;
    if (c > 0.99999988f)
        return 0.0f;
    if (c < -1.0f)
        return kPi;
    return std::acos(c);
}

void lookAt(Mat34& m, const Vec3& from, const Vec3& to) {
    const Vec3 eye = from; // `from` may be m.m3
    Vec3 b{from.x - to.x, from.y - to.y, from.z - to.z};
    const float bb = (b.x * b.x + b.y * b.y) + b.z * b.z;
    const float sb = bb == 0.0f ? 0.0f : 1.0f / std::sqrt(bb);
    b = {sb * b.x, sb * b.y, sb * b.z};
    // YAXIS x m2 = (m2.z, 0, -m2.x)
    const float nx = -b.x;
    const float aa = nx * nx + b.z * b.z;
    const float sa = aa == 0.0f ? 0.0f : 1.0f / std::sqrt(aa);
    const Vec3 a{b.z * sa, 0.0f, nx * sa};
    m.m0 = a;
    m.m1 = {a.z * b.y - a.y * b.z, a.x * b.z - a.z * b.x, a.y * b.x - a.x * b.y};
    m.m2 = b;
    m.m3 = eye;
}

Vec3 getEulersZXY(const Mat34& m) {
    Vec3 e;
    e.x = std::asin(clampf(-m.m2.y, -1.0f, 1.0f));
    e.y = std::atan2(m.m2.x, m.m2.z);
    e.z = std::atan2(m.m0.y, m.m1.y);
    return e;
}

void fromEulersZXY(Mat34& m, const Vec3& e) {
    const float sx = e.x == 0.0f ? 0.0f : std::sin(e.x), cx = e.x == 0.0f ? 1.0f : std::cos(e.x);
    const float sy = e.y == 0.0f ? 0.0f : std::sin(e.y), cy = e.y == 0.0f ? 1.0f : std::cos(e.y);
    const float sz = e.z == 0.0f ? 0.0f : std::sin(e.z), cz = e.z == 0.0f ? 1.0f : std::cos(e.z);
    const float szsy = sz * sy, czcy = cz * cy;
    const float szcy = sz * cy, czsy = cz * sy;
    m.m0 = {szsy * sx + czcy, sz * cx, szcy * sx - czsy};
    m.m1 = {czsy * sx - szcy, cz * cx, czcy * sx + szsy};
    m.m2 = {sy * cx, -sx, cy * cx};
}

Mat34 makeRotate(const Vec3& k, float a) {
    Mat34 r;
    if (a == 0.0f)
        return r;
    if (k.x == 0.0f) {
        if (k.y == 0.0f) {
            makeRotateZ(r, k.z > 0.0f ? a : -a);
            return r;
        }
        if (k.z == 0.0f) {
            makeRotateY(r, k.y > 0.0f ? a : -a);
            return r;
        }
    } else if (k.y == 0.0f && k.z == 0.0f) {
        makeRotateX(r, k.x > 0.0f ? a : -a);
        return r;
    }
    const float m2 = (k.z * k.z + k.y * k.y) + k.x * k.x;
    const float s = m2 == 0.0f ? 0.0f : 1.0f / std::sqrt(m2);
    makeRotateUnitAxis(r, {s * k.x, s * k.y, s * k.z}, a);
    return r;
}

void dot(Mat34& a, const Mat34& b) {
    const Vec3 p = a.m3;
    dotRows(a, b);
    a.m3 = {((p.y * b.m1.x + p.x * b.m0.x) + p.z * b.m2.x) + b.m3.x,
            ((p.y * b.m1.y + p.x * b.m0.y) + p.z * b.m2.y) + b.m3.y,
            ((p.z * b.m2.z + p.y * b.m1.z) + p.x * b.m0.z) + b.m3.z};
}

void dot3x3(Mat34& a, const Mat34& b) { dotRows(a, b); }

void rotate(Mat34& m, const Vec3& axis, float angle) {
    const Mat34 r = makeRotate(axis, angle);
    dot3x3(m, r);
}

void rotateFull(Mat34& m, const Vec3& axis, float angle) {
    Mat34 r = makeRotate(axis, angle);
    r.m3 = {};
    dot(m, r);
}

void polarView(Mat34& m, float distance, float azimuth, float incline, float twist) {
    fromEulersZXY(m, {-incline, azimuth, twist});
    m.m3 = {distance * m.m2.x, distance * m.m2.y, distance * m.m2.z};
}

bool approach(Vec3& v, const Vec3& goal, float rate, float dt) {
    auto axis = [&](float& value, float target) {
        if (value < target) {
            value = rate * dt + value;
            if (value > target)
                value = target;
        } else if (value > target) {
            value = value - rate * dt;
            if (value < target)
                value = target;
        }
        return value == target;
    };
    return axis(v.x, goal.x) && axis(v.y, goal.y) && axis(v.z, goal.z);
}

float horizontalFov4x3(float verticalDegrees) {
    return 2.0f * std::atan(std::tan(verticalDegrees * 0.5f * kDegToRad) * (4.0f / 3.0f));
}

} // namespace mm2::game::cam
