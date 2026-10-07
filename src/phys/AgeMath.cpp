#include "phys/AgeMath.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

// Ported from Open1560 (https://github.com/0x1F9F1/Open1560), GPL-3.0:
// code/midtown/game.asm, Midtown Madness 1 beta build 1560.

namespace mm2::phys::age {
namespace {

// byte_65A1F8: invsqrtf_fast seed table, indexed by bits 16..23 of the input.
constexpr std::uint8_t kInvSqrtSeed[256] = {
    0x6A, 0x68, 0x67, 0x66, 0x64, 0x63, 0x62, 0x60, 0x5F, 0x5E, 0x5C, 0x5B, 0x5A, 0x59, 0x57, 0x56,
    0x55, 0x54, 0x53, 0x52, 0x50, 0x4F, 0x4E, 0x4D, 0x4C, 0x4B, 0x4A, 0x49, 0x48, 0x47, 0x46, 0x45,
    0x44, 0x43, 0x42, 0x41, 0x40, 0x3F, 0x3E, 0x3D, 0x3C, 0x3B, 0x3A, 0x39, 0x38, 0x37, 0x36, 0x35,
    0x34, 0x34, 0x33, 0x32, 0x31, 0x30, 0x2F, 0x2F, 0x2E, 0x2D, 0x2C, 0x2B, 0x2A, 0x2A, 0x29, 0x28,
    0x27, 0x27, 0x26, 0x25, 0x24, 0x24, 0x23, 0x22, 0x21, 0x21, 0x20, 0x1F, 0x1F, 0x1E, 0x1D, 0x1C,
    0x1C, 0x1B, 0x1A, 0x1A, 0x19, 0x18, 0x18, 0x17, 0x16, 0x16, 0x15, 0x15, 0x14, 0x13, 0x13, 0x12,
    0x11, 0x11, 0x10, 0x10, 0x0F, 0x0E, 0x0E, 0x0D, 0x0D, 0x0C, 0x0C, 0x0B, 0x0A, 0x0A, 0x09, 0x09,
    0x08, 0x08, 0x07, 0x07, 0x06, 0x05, 0x05, 0x04, 0x04, 0x03, 0x03, 0x02, 0x02, 0x01, 0x01, 0x00,
    0xFF, 0xFE, 0xFC, 0xFA, 0xF8, 0xF6, 0xF4, 0xF2, 0xF0, 0xEF, 0xED, 0xEB, 0xE9, 0xE8, 0xE6, 0xE4,
    0xE2, 0xE1, 0xDF, 0xDE, 0xDC, 0xDA, 0xD9, 0xD7, 0xD6, 0xD4, 0xD3, 0xD1, 0xD0, 0xCE, 0xCD, 0xCB,
    0xCA, 0xC8, 0xC7, 0xC5, 0xC4, 0xC3, 0xC1, 0xC0, 0xBF, 0xBD, 0xBC, 0xBB, 0xB9, 0xB8, 0xB7, 0xB6,
    0xB4, 0xB3, 0xB2, 0xB1, 0xB0, 0xAE, 0xAD, 0xAC, 0xAB, 0xAA, 0xA8, 0xA7, 0xA6, 0xA5, 0xA4, 0xA3,
    0xA2, 0xA1, 0xA0, 0x9F, 0x9E, 0x9C, 0x9B, 0x9A, 0x99, 0x98, 0x97, 0x96, 0x95, 0x94, 0x93, 0x92,
    0x91, 0x90, 0x8F, 0x8F, 0x8E, 0x8D, 0x8C, 0x8B, 0x8A, 0x89, 0x88, 0x87, 0x86, 0x85, 0x85, 0x84,
    0x83, 0x82, 0x81, 0x80, 0x7F, 0x7F, 0x7E, 0x7D, 0x7C, 0x7B, 0x7A, 0x7A, 0x79, 0x78, 0x77, 0x76,
    0x76, 0x75, 0x74, 0x73, 0x73, 0x72, 0x71, 0x70, 0x70, 0x6F, 0x6E, 0x6D, 0x6D, 0x6C, 0x6B, 0x6A};

} // namespace

float invSqrtFast(float x) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(x);
    const float half = x * 0.5f;
    std::uint32_t seed = (0x5F000000u - (((bits >> 23) & 0xFFu) << 22)) & 0xFF800000u;
    seed |= static_cast<std::uint32_t>(kInvSqrtSeed[(bits >> 16) & 0xFFu]) << 15;
    float r = x == 0.0f ? 0.0f : std::bit_cast<float>(seed);
    // Two Newton steps, in the original's operation order.
    r = (1.5f - half * (r * r)) * r;
    r = r * (1.5f - (half * r) * r);
    return r;
}

Mat34 arbitraryRotation(const Vec3& axisIn, float angle) {
    Vec3 k = axisIn;
    const float len2 = (k.y * k.y + k.z * k.z) + k.x * k.x;
    Mat34 r;
    if (len2 < 1e-11f)
        return r;
    if (len2 > 1.0000010f || len2 < 0.99999f) {
        const float inv = 1.0f / std::sqrt(len2);
        k = {k.x * inv, k.y * inv, k.z * inv};
    }
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const float omc = 1.0f - c;
    const float x = k.x, y = k.y, z = k.z;
    const float omcY = omc * y;
    const float omcZ = omc * z;
    const float xy = x * omcY;
    const float xz = x * omcZ;
    const float yz = y * omcZ;
    r.m0 = {(omc * x) * x + c, xy + z * s, xz - y * s};
    r.m1 = {xy - z * s, y * omcY + c, x * s + yz};
    r.m2 = {xz + y * s, yz - x * s, z * omcZ + c};
    return r;
}

void rotate(Mat34& m, const Vec3& axis, float angle) {
    if (angle == 0.0f)
        return;
    const auto rowsXY = [&](float c, float s) {
        // Z axis: x' = c*x - s*y, y' = s*x + c*y.
        for (Vec3* r : {&m.m0, &m.m1, &m.m2}) {
            const float x = r->x, y = r->y;
            r->x = x * c - s * y;
            r->y = s * x + c * y;
        }
    };
    if (axis.z == 0.0f && axis.y == 0.0f) {
        if (axis.x == 0.0f)
            return;
        const float c = std::cos(angle);
        float s = std::sin(angle);
        if (axis.x < 0.0f)
            s = -s;
        // X axis: y' = c*y - s*z, z' = c*z + s*y.
        for (Vec3* r : {&m.m0, &m.m1, &m.m2}) {
            const float y = r->y, z = r->z;
            r->y = c * y - s * z;
            r->z = c * z + y * s;
        }
        return;
    }
    if (axis.z == 0.0f && axis.x == 0.0f) {
        const float c = std::cos(angle);
        float s = std::sin(angle);
        if (axis.y < 0.0f)
            s = -s;
        // Y axis: x' = s*z + c*x, z' = c*z - s*x.
        for (Vec3* r : {&m.m0, &m.m1, &m.m2}) {
            const float x = r->x, z = r->z;
            r->x = s * z + x * c;
            r->z = c * z - x * s;
        }
        return;
    }
    if (axis.x == 0.0f && axis.y == 0.0f) {
        const float c = std::cos(angle);
        float s = std::sin(angle);
        if (axis.z < 0.0f)
            s = -s;
        rowsXY(c, s);
        return;
    }
    const Mat34 r = arbitraryRotation(axis, angle);
    m.m0 = r.transformDir(m.m0);
    m.m1 = r.transformDir(m.m1);
    m.m2 = r.transformDir(m.m2);
}

void rotateAbs(Mat34& m, const Vec3& axis, float angle) {
    const Vec3 m3 = m.m3;
    m = Mat34::identity();
    if (angle != 0.0f)
        rotate(m, axis, angle);
    m.m3 = m3;
}

float mag(const Vec3& v) {
    return std::sqrt(mag2(v));
}
float invMag(const Vec3& v) {
    return 1.0f / std::sqrt(mag2(v));
}

Mat34 transpose(const Mat34& m) {
    Mat34 r;
    r.m0 = {m.m0.x, m.m1.x, m.m2.x};
    r.m1 = {m.m0.y, m.m1.y, m.m2.y};
    r.m2 = {m.m0.z, m.m1.z, m.m2.z};
    r.m3 = m.m3;
    return r;
}

Mat34 inverse(const Mat34& a) {
    // ?Inverse@Matrix34@@QBE?AV1@XZ, cofactors in the original's order.
    const float c10 = a.m1.x * a.m2.z - a.m2.x * a.m1.z;
    const float c00 = a.m2.z * a.m1.y - a.m1.z * a.m2.y;
    const float c20 = a.m1.x * a.m2.y - a.m2.x * a.m1.y;
    const float det = (a.m0.x * c00 - c10 * a.m0.y) + c20 * a.m0.z;
    if (det == 0.0f)
        return a;
    const float inv = 1.0f / det;
    Mat34 r;
    r.m0.x = inv * c00;
    r.m1.x = -(inv * c10);
    r.m2.x = inv * c20;
    r.m3.x = -(((r.m0.x * a.m3.x) + r.m1.x * a.m3.y) + r.m2.x * a.m3.z);
    r.m0.y = -((a.m2.z * a.m0.y - a.m2.y * a.m0.z) * inv);
    r.m1.y = (a.m0.x * a.m2.z - a.m2.x * a.m0.z) * inv;
    r.m2.y = -((a.m0.x * a.m2.y - a.m2.x * a.m0.y) * inv);
    r.m3.y = -(((r.m0.y * a.m3.x) + r.m1.y * a.m3.y) + r.m2.y * a.m3.z);
    r.m1.z = -((a.m0.x * a.m1.z - a.m1.x * a.m0.z) * inv);
    r.m0.z = (a.m1.z * a.m0.y - a.m1.y * a.m0.z) * inv;
    r.m2.z = (a.m0.x * a.m1.y - a.m1.x * a.m0.y) * inv;
    r.m3.z = -(((r.m0.z * a.m3.x) + r.m1.z * a.m3.y) + r.m2.z * a.m3.z);
    return r;
}

Mat34 dot3x3(const Mat34& a, const Mat34& b, const Vec3& keepM3) {
    Mat34 r;
    r.m0 = b.transformDir(a.m0);
    r.m1 = b.transformDir(a.m1);
    r.m2 = b.transformDir(a.m2);
    r.m3 = keepM3;
    return r;
}

Mat34 dot(const Mat34& a, const Mat34& b) {
    return Mat34::mul(a, b);
}

Mat34 crossProdMatrix(const Vec3& v) {
    Mat34 r;
    r.m0 = {0.0f, -v.z, v.y};
    r.m1 = {v.z, 0.0f, -v.x};
    r.m2 = {-v.y, v.x, 0.0f};
    r.m3 = {};
    return r;
}

Mat34 add3x3(const Mat34& a, const Mat34& b) {
    Mat34 r;
    r.m0 = {a.m0.x + b.m0.x, a.m0.y + b.m0.y, a.m0.z + b.m0.z};
    r.m1 = {a.m1.x + b.m1.x, a.m1.y + b.m1.y, a.m1.z + b.m1.z};
    r.m2 = {a.m2.x + b.m2.x, a.m2.y + b.m2.y, a.m2.z + b.m2.z};
    r.m3 = b.m3;
    return r;
}

Vec3 solveSVD(const Mat34& b, const Vec3& rhs) {
    float a[3][4] = {{b.m0.x, b.m1.x, b.m2.x, rhs.x}, {b.m0.y, b.m1.y, b.m2.y, rhs.y}, {b.m0.z, b.m1.z, b.m2.z, rhs.z}};
    float scale = 0.0f;
    for (auto& row : a)
        for (int c = 0; c < 3; ++c)
            scale = std::max(scale, std::abs(row[c]));
    const float eps = scale * 1e-6f;
    int pivotCol[3] = {-1, -1, -1};
    bool used[3] = {};
    for (int col = 0; col < 3; ++col) {
        int best = -1;
        float bestAbs = eps;
        for (int r = 0; r < 3; ++r)
            if (!used[r] && std::abs(a[r][col]) > bestAbs) {
                best = r;
                bestAbs = std::abs(a[r][col]);
            }
        if (best < 0)
            continue;
        used[best] = true;
        pivotCol[col] = best;
        for (int r = 0; r < 3; ++r) {
            if (r == best)
                continue;
            const float f = a[r][col] / a[best][col];
            for (int c = col; c < 4; ++c)
                a[r][c] -= f * a[best][c];
        }
    }
    float x[3] = {};
    for (int col = 0; col < 3; ++col)
        if (pivotCol[col] >= 0)
            x[col] = a[pivotCol[col]][3] / a[pivotCol[col]][col];
    return {x[0], x[1], x[2]};
}

} // namespace mm2::phys::age
