#pragma once

// The Angel engine's Vector3 / Matrix34 routines whose rounding the
// simulation depends on, ported from midtown2.exe build 3393 (MM2Recomp).
// Each function reproduces the MM2 member named in its comment, summing in
// the original's order: float addition does not associate, and the
// original's compiler grouped every three-term sum its own way (the in-place
// and two-argument overloads of the same product differ). The original runs
// the x87 FPU in single precision (Direct3D's default), so each addition and
// multiplication rounds to float as here; fsin and fcos keep their extended
// result, which MakeRotateUnitAxis adds unrounded. Generic vector algebra
// uses mm2::Vec3 / Mat34 (core/Math.h).

#include "core/Math.h"

namespace mm2::phys::age {

// Vector3::Mag2 / Mag / InvMag (0 for a zero vector) / Dot.
inline float mag2(const Vec3& v) {
    return (v.x * v.x + v.y * v.y) + v.z * v.z;
}
float mag(const Vec3& v);
float invMag(const Vec3& v);
inline float dot(const Vec3& a, const Vec3& b) {
    return (a.z * b.z + a.y * b.y) + a.x * b.x;
}

// Vector3::Dot3x3: v * M (3x3 rows; m3 ignored).
Vec3 dot3x3(const Vec3& v, const Mat34& m);
// Vector3::Dot3x3Transpose: v * M^T (component i is v . M.row(i)).
Vec3 dot3x3Transpose(const Vec3& v, const Mat34& m);

// Matrix34::MakeRotateUnitAxis: the rotation by `angle` radians about the
// unit `axis` (row vectors: p * R; m3 zero).
Mat34 makeRotateUnitAxis(const Vec3& axis, float angle);
// Matrix34::MakeRotate: the same for any axis. Angle 0 gives the identity,
// an axis along x, y or z MakeRotateX/Y/Z (by the sign of the axis; a zero
// axis takes the -z branch, as in the original), any other the normalised
// axis.
Mat34 makeRotate(const Vec3& axis, float angle);
// Matrix34::Rotate / RotateUnitAxis: the basis rows of m rotated, m = m * R
// (m3 untouched).
void rotate(Mat34& m, const Vec3& axis, float angle);
void rotateUnitAxis(Mat34& m, const Vec3& axis, float angle);
// MM1's Matrix34::ArbitraryRotation has no MM2 counterpart; MM2 code builds
// the same matrix with Matrix34::MakeRotate, which this returns.
Mat34 arbitraryRotation(const Vec3& axis, float angle);

// Matrix34::Transpose: the 3x3 part transposed, m3 copied.
Mat34 transpose(const Mat34& m);
// Matrix34::Inverse: the 3x4 inverse; m unchanged when the determinant is 0
// (the original prints a warning).
Mat34 inverse(const Mat34& m);
// Matrix34::Dot3x3(const Matrix34& a, const Matrix34& b): a * b (3x3; m3 of
// the result is `keepM3`, the original leaves the destination's).
Mat34 dot3x3(const Mat34& a, const Mat34& b, const Vec3& keepM3 = {});
// Matrix34::Dot3x3(const Matrix34& b) on a: a = a * b (3x3; a.m3 untouched).
void dot3x3InPlace(Mat34& a, const Mat34& b);
// Matrix34::Dot3x3Transpose(const Matrix34& a, const Matrix34& b): a * b^T
// (m3 zero).
Mat34 dot3x3Transpose(const Mat34& a, const Mat34& b);
// Matrix34::Dot3x3Transpose(const Matrix34& b) on a: a = a * b^T.
void dot3x3TransposeInPlace(Mat34& a, const Mat34& b);
// Matrix34::Dot(const Matrix34& a, const Matrix34& b): the affine a * b.
Mat34 dot(const Mat34& a, const Mat34& b);
// The cross product matrix of v: p * M == p x v; m3 zero.
Mat34 crossProdMatrix(const Vec3& v);
// Matrix34::Dot3x3CrossProdMtx(r): each basis row a becomes a x r
// (m = m * crossProdMatrix(r)).
void dot3x3CrossProdMtx(Mat34& m, const Vec3& r);
// Matrix34::Dot3x3CrossProdTranspose(r): each basis row a becomes r x a.
void dot3x3CrossProdTranspose(Mat34& m, const Vec3& r);
// Matrix34::Add3x3: elementwise 3x3 sum (m3 from a).
Mat34 add3x3(const Mat34& a, const Mat34& b);
// Matrix34::Scale(float): the 3x3 part times s.
void scale3x3(Mat34& m, float s);
// Matrix34::AddScaled(b, s): m += b * s (3x3).
void addScaled3x3(Mat34& m, const Mat34& b, float s);
// Matrix34::SolveSVD: x with x * M = b (row vectors) for the 3x3 part of M.
// Despite its name a cofactor solve: the full inverse when M is well enough
// conditioned (relative to 1e-4 of its largest element), otherwise the
// minimum-norm solution of rank 2 or 1, and 0 for a zero matrix.
Vec3 solveSVD(const Mat34& m, const Vec3& b);

} // namespace mm2::phys::age
