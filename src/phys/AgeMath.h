#pragma once

// Exact ports of Angel engine (vector7) math routines whose rounding affects
// the simulation. Ported from Open1560's game.asm (GPL-3.0), Midtown Madness 1
// beta build 1560. Generic vector algebra uses mm2::Vec3/Mat34 directly.

#include "core/Math.h"

namespace mm2::phys::age {

// The original runs the x87 FPU in single precision (Direct3D's default), so
// each operation rounds to float. An operation with a double constant
// (`fmul qword ptr [...]`) is exact in double and then rounded to float;
// these helpers express that.
inline float mulD(float a, double b) {
    return static_cast<float>(static_cast<double>(a) * b);
}
inline float addD(float a, double b) {
    return static_cast<float>(static_cast<double>(a) + b);
}
inline float subD(float a, double b) {
    return static_cast<float>(static_cast<double>(a) - b);
}
inline float rsubD(double a, float b) {
    return static_cast<float>(a - static_cast<double>(b));
}

// ?invsqrtf_fast@@YAMM@Z: 1/sqrt(x) from a 256-entry mantissa seed table and
// two Newton-Raphson steps. Used by asInertialCS::FinishUpdate for the
// rotation angle, so it is ported bit for bit.
float invSqrtFast(float x);

// ?Rotate@Matrix34@@QAEXABVVector3@@M@Z: rotates the three basis rows of m
// (not m3) by angle radians about the world axis, i.e. row' = row * R.
// Axis-aligned axes take the original's dedicated paths; anything else goes
// through ArbitraryRotation + Dot3x3. MathSpeed is 0 in the original, so
// exact cos/sin are used.
void rotate(Mat34& m, const Vec3& axis, float angle);

// ?ArbitraryRotation@Matrix34@@AAEXABVVector3@@M@Z (3x3 part).
Mat34 arbitraryRotation(const Vec3& axis, float angle);

// ?RotateAbs@Matrix34@@QAEXABVVector3@@M@Z: sets the 3x3 part of m to the
// rotation about axis by angle (m3 untouched).
void rotateAbs(Mat34& m, const Vec3& axis, float angle);

// Vector3::Mag / InvMag / operator^ (dot) in the original's summation order.
inline float mag2(const Vec3& v) {
    return (v.y * v.y + v.z * v.z) + v.x * v.x;
}
float mag(const Vec3& v);
float invMag(const Vec3& v);
inline float dot(const Vec3& a, const Vec3& b) {
    return (a.x * b.x + a.y * b.y) + a.z * b.z;
}

// Matrix34 helpers (row-vector convention, see core/Math.h).
// Transpose: 3x3 transposed, m3 copied unchanged.
Mat34 transpose(const Mat34& m);
// Inverse: general 3x4 inverse; returns m unchanged if singular (the original
// prints a warning).
Mat34 inverse(const Mat34& m);
// Dot3x3: 3x3 product a * b (m3 of the result is left as `keepM3`).
Mat34 dot3x3(const Mat34& a, const Mat34& b, const Vec3& keepM3 = {});
// Dot: affine product a * b.
Mat34 dot(const Mat34& a, const Mat34& b);
// CrossProdMatrix: p * M == p x v; m3 = 0.
Mat34 crossProdMatrix(const Vec3& v);
// Elementwise 3x3 sum a + b (m3 taken from b).
Mat34 add3x3(const Mat34& a, const Mat34& b);

} // namespace mm2::phys::age
