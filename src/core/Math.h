#pragma once

// Vector/matrix types following the conventions of the Angel Game Engine
// (AGE), which Midtown Madness 2 is built on:
//
//   * Right-handed, Y up. Objects face -Z ("m2" of a matrix is the back axis).
//   * Row vectors: a point is transformed as p' = p * M. Matrix34 stores the
//     three basis rows (m0 = right, m1 = up, m2 = back) and the position m3.
//   * Matrix44 is row-major with the same row-vector convention (Direct3D
//     style). Upload it as-is to shaders that compute `v * M`, or transposed
//     for `M * v`.
//   * 32-bit floats everywhere, as in the original, so that integrating the
//     simulation in the same order produces the same rounding behaviour.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace mm2 {

inline constexpr float kPi = std::numbers::pi_v<float>;
inline constexpr float kTwoPi = 2.0f * kPi;
inline constexpr float kHalfPi = 0.5f * kPi;
inline constexpr float kDegToRad = kPi / 180.0f;
inline constexpr float kRadToDeg = 180.0f / kPi;

constexpr float sq(float v) { return v * v; }
constexpr float lerp(float a, float b, float t) { return a + (b - a) * t; }
constexpr float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
constexpr float signf(float v) { return v < 0.0f ? -1.0f : 1.0f; }

struct Vec2 {
    float x = 0, y = 0;

    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}

    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator-() const { return {-x, -y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }
    constexpr Vec2& operator+=(Vec2 o) { return *this = *this + o; }
    constexpr Vec2& operator-=(Vec2 o) { return *this = *this - o; }
    constexpr Vec2& operator*=(float s) { return *this = *this * s; }
    constexpr bool operator==(const Vec2&) const = default;

    constexpr float dot(Vec2 o) const { return x * o.x + y * o.y; }
    constexpr float cross(Vec2 o) const { return x * o.y - y * o.x; }
    constexpr float mag2() const { return dot(*this); }
    float mag() const { return std::sqrt(mag2()); }
};

struct Vec3 {
    float x = 0, y = 0, z = 0;

    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    constexpr float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
    constexpr float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }

    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    // AGE divides by multiplying with the reciprocal; keep that for rounding parity.
    constexpr Vec3 operator/(float s) const {
        const float inv = 1.0f / s;
        return {x * inv, y * inv, z * inv};
    }
    constexpr Vec3& operator+=(const Vec3& o) { return *this = *this + o; }
    constexpr Vec3& operator-=(const Vec3& o) { return *this = *this - o; }
    constexpr Vec3& operator*=(float s) { return *this = *this * s; }
    constexpr bool operator==(const Vec3&) const = default;

    constexpr float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    constexpr Vec3 cross(const Vec3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    constexpr Vec3 mul(const Vec3& o) const { return {x * o.x, y * o.y, z * o.z}; }
    constexpr float mag2() const { return dot(*this); }
    float mag() const { return std::sqrt(mag2()); }
    float invMag() const {
        const float m = mag();
        return m != 0.0f ? 1.0f / m : 0.0f;
    }
    Vec3 normalized() const { return *this * invMag(); }
    float dist(const Vec3& o) const { return (*this - o).mag(); }
    constexpr float dist2(const Vec3& o) const { return (*this - o).mag2(); }

    static constexpr Vec3 zero() { return {0, 0, 0}; }
    static constexpr Vec3 xAxis() { return {1, 0, 0}; }
    static constexpr Vec3 yAxis() { return {0, 1, 0}; }
    static constexpr Vec3 zAxis() { return {0, 0, 1}; }
};

constexpr Vec3 operator*(float s, const Vec3& v) { return v * s; }
constexpr Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
constexpr Vec3 vmin(const Vec3& a, const Vec3& b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}
constexpr Vec3 vmax(const Vec3& a, const Vec3& b) {
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;

    constexpr Vec4() = default;
    constexpr Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    constexpr Vec4(const Vec3& v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}

    constexpr Vec3 xyz() const { return {x, y, z}; }
    constexpr Vec4 operator+(const Vec4& o) const { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
    constexpr Vec4 operator-(const Vec4& o) const { return {x - o.x, y - o.y, z - o.z, w - o.w}; }
    constexpr Vec4 operator*(float s) const { return {x * s, y * s, z * s, w * s}; }
    constexpr float dot(const Vec4& o) const { return x * o.x + y * o.y + z * o.z + w * o.w; }
    constexpr bool operator==(const Vec4&) const = default;
};

// Affine transform: rotation/scale rows m0..m2 plus translation m3.
struct Mat34 {
    Vec3 m0{1, 0, 0};
    Vec3 m1{0, 1, 0};
    Vec3 m2{0, 0, 1};
    Vec3 m3{0, 0, 0};

    static constexpr Mat34 identity() { return {}; }
    static constexpr Mat34 translation(const Vec3& p) {
        Mat34 m;
        m.m3 = p;
        return m;
    }
    static Mat34 rotationX(float a);
    static Mat34 rotationY(float a);
    static Mat34 rotationZ(float a);
    // Rotation of `angle` radians about the unit axis `axis`.
    static Mat34 rotationAxis(const Vec3& axis, float angle);

    constexpr Vec3& row(int i) { return i == 0 ? m0 : (i == 1 ? m1 : (i == 2 ? m2 : m3)); }
    constexpr const Vec3& row(int i) const { return i == 0 ? m0 : (i == 1 ? m1 : (i == 2 ? m2 : m3)); }

    // p * M (point, includes translation). AGE: Vector3::Dot.
    constexpr Vec3 transform(const Vec3& p) const { return m0 * p.x + m1 * p.y + m2 * p.z + m3; }
    // v * M3x3 (direction). AGE: Vector3::Dot3x3.
    constexpr Vec3 transformDir(const Vec3& v) const { return m0 * v.x + m1 * v.y + m2 * v.z; }
    // v * transpose(M3x3): world direction into local space for orthonormal M.
    // AGE: Vector3::Dot3x3Transpose.
    constexpr Vec3 untransformDir(const Vec3& v) const { return {v.dot(m0), v.dot(m1), v.dot(m2)}; }
    // Inverse transform of a point for orthonormal M.
    constexpr Vec3 untransform(const Vec3& p) const { return untransformDir(p - m3); }

    // this = a * b (apply a, then b). AGE: Matrix34::Dot.
    static constexpr Mat34 mul(const Mat34& a, const Mat34& b) {
        return {b.transformDir(a.m0), b.transformDir(a.m1), b.transformDir(a.m2), b.transform(a.m3)};
    }
    constexpr Mat34 operator*(const Mat34& b) const { return mul(*this, b); }

    // Inverse for orthonormal rotation part (rigid transforms).
    constexpr Mat34 fastInverse() const {
        Mat34 r;
        r.m0 = {m0.x, m1.x, m2.x};
        r.m1 = {m0.y, m1.y, m2.y};
        r.m2 = {m0.z, m1.z, m2.z};
        r.m3 = -r.transformDir(m3);
        return r;
    }
    // General inverse (handles scale/shear). Returns identity if singular.
    Mat34 inverse() const;

    // Re-orthonormalize the basis (Gram-Schmidt, keeping m1 up as primary).
    void normalize();
};

// Row-major 4x4 matrix, row-vector convention (p' = p * M).
struct Mat44 {
    float m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};

    static constexpr Mat44 identity() { return {}; }
    static constexpr Mat44 fromMat34(const Mat34& a) {
        Mat44 r;
        for (int i = 0; i < 4; ++i) {
            const Vec3& v = a.row(i);
            r.m[i][0] = v.x;
            r.m[i][1] = v.y;
            r.m[i][2] = v.z;
            r.m[i][3] = i == 3 ? 1.0f : 0.0f;
        }
        return r;
    }
    static constexpr Mat44 mul(const Mat44& a, const Mat44& b) {
        Mat44 r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                float s = 0;
                for (int k = 0; k < 4; ++k)
                    s += a.m[i][k] * b.m[k][j];
                r.m[i][j] = s;
            }
        return r;
    }
    constexpr Mat44 operator*(const Mat44& b) const { return mul(*this, b); }
    constexpr Mat44 transposed() const {
        Mat44 r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                r.m[i][j] = m[j][i];
        return r;
    }
    constexpr Vec4 transform(const Vec4& v) const {
        return {v.x * m[0][0] + v.y * m[1][0] + v.z * m[2][0] + v.w * m[3][0],
                v.x * m[0][1] + v.y * m[1][1] + v.z * m[2][1] + v.w * m[3][1],
                v.x * m[0][2] + v.y * m[1][2] + v.z * m[2][2] + v.w * m[3][2],
                v.x * m[0][3] + v.y * m[1][3] + v.z * m[2][3] + v.w * m[3][3]};
    }

    // Right-handed perspective looking down -Z. `zeroToOne` selects Vulkan/D3D
    // depth range [0,1] vs OpenGL [-1,1]. `fovY` in radians.
    static Mat44 perspective(float fovY, float aspect, float zNear, float zFar, bool zeroToOne);
    // Reversed-Z infinite perspective (depth 1 at near, 0 at infinity), [0,1] range.
    static Mat44 perspectiveReversedInfinite(float fovY, float aspect, float zNear);
    static Mat44 orthographic(float left, float right, float bottom, float top, float zNear, float zFar,
                              bool zeroToOne);
};

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;

    static Quat fromAxisAngle(const Vec3& axis, float angle);
    static Quat fromMatrix(const Mat34& m);
    Mat34 toMatrix(const Vec3& position = {}) const;
    Quat operator*(const Quat& o) const;
    Quat normalized() const;
    static Quat slerp(const Quat& a, const Quat& b, float t);
};

// Axis-aligned bounding box.
struct Aabb {
    Vec3 min{1e30f, 1e30f, 1e30f};
    Vec3 max{-1e30f, -1e30f, -1e30f};

    constexpr bool valid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
    constexpr void expand(const Vec3& p) {
        min = vmin(min, p);
        max = vmax(max, p);
    }
    constexpr void expand(const Aabb& b) {
        if (b.valid()) {
            expand(b.min);
            expand(b.max);
        }
    }
    constexpr Vec3 center() const { return (min + max) * 0.5f; }
    constexpr Vec3 extent() const { return (max - min) * 0.5f; }
};

} // namespace mm2
