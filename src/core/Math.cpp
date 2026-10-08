#include "core/Math.h"

namespace mm2 {

// Rotation matrices are the transposes of the usual column-vector forms
// because of the row-vector convention; positive angles rotate
// counter-clockwise when looking down the axis toward the origin.

namespace {

// cos and sin as the x87 computes them (wide), rounded to float.
float cosf32(float a) {
    return static_cast<float>(std::cos(static_cast<double>(a)));
}
float sinf32(float a) {
    return static_cast<float>(std::sin(static_cast<double>(a)));
}

} // namespace

Mat34 Mat34::rotationX(float a) {
    const float c = cosf32(a), s = sinf32(a);
    Mat34 m;
    m.m1 = {0, c, s};
    m.m2 = {0, -s, c};
    return m;
}

Mat34 Mat34::rotationY(float a) {
    const float c = cosf32(a), s = sinf32(a);
    Mat34 m;
    m.m0 = {c, 0, -s};
    m.m2 = {s, 0, c};
    return m;
}

Mat34 Mat34::rotationZ(float a) {
    const float c = cosf32(a), s = sinf32(a);
    Mat34 m;
    m.m0 = {c, s, 0};
    m.m1 = {-s, c, 0};
    return m;
}

Mat34 Mat34::rotationAxis(const Vec3& k, float angle) {
    // Matrix34::MakeRotateUnitAxis: the transpose of Rodrigues' rotation. The
    // diagonal adds the cosine as fcos left it (wider than a float); every
    // product before it rounds to float.
    const double c = std::cos(static_cast<double>(angle));
    const float s = sinf32(angle);
    const float t = static_cast<float>(1.0 - c);
    const auto diagonal = [&](float v) { return static_cast<float>(static_cast<double>((v * v) * t) + c); };
    Mat34 m;
    m.m0 = {diagonal(k.x), (k.y * k.x) * t + s * k.z, (k.z * k.x) * t - s * k.y};
    m.m1 = {(k.y * k.x) * t - s * k.z, diagonal(k.y), (k.z * k.y) * t + s * k.x};
    m.m2 = {(k.z * k.x) * t + s * k.y, (k.z * k.y) * t - s * k.x, diagonal(k.z)};
    return m;
}

Mat34 Mat34::inverse() const {
    const float a = m0.x, b = m0.y, c = m0.z;
    const float d = m1.x, e = m1.y, f = m1.z;
    const float g = m2.x, h = m2.y, i = m2.z;
    const float A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    const float det = a * A + b * B + c * C;
    if (std::abs(det) < 1e-12f)
        return identity();
    const float inv = 1.0f / det;
    Mat34 r;
    r.m0 = {A * inv, -(b * i - c * h) * inv, (b * f - c * e) * inv};
    r.m1 = {B * inv, (a * i - c * g) * inv, -(a * f - c * d) * inv};
    r.m2 = {C * inv, -(a * h - b * g) * inv, (a * e - b * d) * inv};
    r.m3 = -r.transformDir(m3);
    return r;
}

void Mat34::normalize() {
    // Matrix34::Normalize, in its summation order (the first length sums z,
    // y, x; the others x, y, z).
    m0 = {m2.z * m1.y - m2.y * m1.z, m2.x * m1.z - m2.z * m1.x, m2.y * m1.x - m1.y * m2.x};
    const float l0 = (m0.z * m0.z + m0.y * m0.y) + m0.x * m0.x;
    const float s0 = l0 == 0.0f ? 0.0f : 1.0f / std::sqrt(l0);
    m0 = {s0 * m0.x, s0 * m0.y, s0 * m0.z};
    m1 = {m2.y * m0.z - m0.y * m2.z, m0.x * m2.z - m0.z * m2.x, m0.y * m2.x - m0.x * m2.y};
    m1 = m1 * m1.invMag();
    m2 = m2 * m2.invMag();
}

Mat44 Mat44::perspective(float fovY, float aspect, float zNear, float zFar, bool zeroToOne) {
    const float f = 1.0f / std::tan(fovY * 0.5f);
    Mat44 r;
    r.m[0][0] = f / aspect;
    r.m[1][1] = f;
    r.m[2][3] = -1.0f;
    r.m[3][3] = 0.0f;
    if (zeroToOne) {
        r.m[2][2] = zFar / (zNear - zFar);
        r.m[3][2] = zNear * zFar / (zNear - zFar);
    } else {
        r.m[2][2] = (zFar + zNear) / (zNear - zFar);
        r.m[3][2] = 2.0f * zFar * zNear / (zNear - zFar);
    }
    return r;
}

Mat44 Mat44::perspectiveReversedInfinite(float fovY, float aspect, float zNear) {
    const float f = 1.0f / std::tan(fovY * 0.5f);
    Mat44 r;
    r.m[0][0] = f / aspect;
    r.m[1][1] = f;
    r.m[2][2] = 0.0f;
    r.m[2][3] = -1.0f;
    r.m[3][2] = zNear;
    r.m[3][3] = 0.0f;
    return r;
}

Mat44 Mat44::orthographic(float left, float right, float bottom, float top, float zNear, float zFar,
                          bool zeroToOne) {
    Mat44 r;
    r.m[0][0] = 2.0f / (right - left);
    r.m[1][1] = 2.0f / (top - bottom);
    r.m[3][0] = -(right + left) / (right - left);
    r.m[3][1] = -(top + bottom) / (top - bottom);
    if (zeroToOne) {
        r.m[2][2] = 1.0f / (zNear - zFar);
        r.m[3][2] = zNear / (zNear - zFar);
    } else {
        r.m[2][2] = 2.0f / (zNear - zFar);
        r.m[3][2] = (zFar + zNear) / (zNear - zFar);
    }
    return r;
}

Quat Quat::fromAxisAngle(const Vec3& axis, float angle) {
    const float s = std::sin(angle * 0.5f);
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(angle * 0.5f)};
}

Quat Quat::fromMatrix(const Mat34& m) {
    // m is the transpose of the column-vector rotation R; R[i][j] = m.row(j)[i].
    const float trace = m.m0.x + m.m1.y + m.m2.z;
    Quat q;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (m.m1.z - m.m2.y) / s;
        q.y = (m.m2.x - m.m0.z) / s;
        q.z = (m.m0.y - m.m1.x) / s;
    } else if (m.m0.x > m.m1.y && m.m0.x > m.m2.z) {
        const float s = std::sqrt(1.0f + m.m0.x - m.m1.y - m.m2.z) * 2.0f;
        q.w = (m.m1.z - m.m2.y) / s;
        q.x = 0.25f * s;
        q.y = (m.m1.x + m.m0.y) / s;
        q.z = (m.m2.x + m.m0.z) / s;
    } else if (m.m1.y > m.m2.z) {
        const float s = std::sqrt(1.0f + m.m1.y - m.m0.x - m.m2.z) * 2.0f;
        q.w = (m.m2.x - m.m0.z) / s;
        q.x = (m.m1.x + m.m0.y) / s;
        q.y = 0.25f * s;
        q.z = (m.m2.y + m.m1.z) / s;
    } else {
        const float s = std::sqrt(1.0f + m.m2.z - m.m0.x - m.m1.y) * 2.0f;
        q.w = (m.m0.y - m.m1.x) / s;
        q.x = (m.m2.x + m.m0.z) / s;
        q.y = (m.m2.y + m.m1.z) / s;
        q.z = 0.25f * s;
    }
    return q.normalized();
}

Mat34 Quat::toMatrix(const Vec3& position) const {
    const float xx = x * x, yy = y * y, zz = z * z;
    const float xy = x * y, xz = x * z, yz = y * z;
    const float wx = w * x, wy = w * y, wz = w * z;
    Mat34 m;
    m.m0 = {1 - 2 * (yy + zz), 2 * (xy + wz), 2 * (xz - wy)};
    m.m1 = {2 * (xy - wz), 1 - 2 * (xx + zz), 2 * (yz + wx)};
    m.m2 = {2 * (xz + wy), 2 * (yz - wx), 1 - 2 * (xx + yy)};
    m.m3 = position;
    return m;
}

Quat Quat::operator*(const Quat& o) const {
    return {w * o.x + x * o.w + y * o.z - z * o.y, w * o.y - x * o.z + y * o.w + z * o.x,
            w * o.z + x * o.y - y * o.x + z * o.w, w * o.w - x * o.x - y * o.y - z * o.z};
}

Quat Quat::normalized() const {
    const float m = std::sqrt(x * x + y * y + z * z + w * w);
    if (m == 0.0f)
        return {};
    const float inv = 1.0f / m;
    return {x * inv, y * inv, z * inv, w * inv};
}

Quat Quat::slerp(const Quat& a, const Quat& b, float t) {
    float cosom = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    Quat end = b;
    if (cosom < 0.0f) {
        cosom = -cosom;
        end = {-b.x, -b.y, -b.z, -b.w};
    }
    float s0, s1;
    if (cosom > 0.9995f) {
        s0 = 1.0f - t;
        s1 = t;
    } else {
        const float omega = std::acos(cosom);
        const float sinom = std::sin(omega);
        s0 = std::sin((1.0f - t) * omega) / sinom;
        s1 = std::sin(t * omega) / sinom;
    }
    return Quat{a.x * s0 + end.x * s1, a.y * s0 + end.y * s1, a.z * s0 + end.z * s1, a.w * s0 + end.w * s1}
        .normalized();
}

} // namespace mm2
