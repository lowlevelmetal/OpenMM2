#pragma once

// Angel engine (AGE) vector and matrix routines used by the car cameras,
// following Midtown Madness 2's own Matrix34 / Vector3 (MM2Recomp, build
// 3393) with the original operation order. AGE ran the x87 FPU at single
// precision (Direct3D 7 sets it), so plain 32-bit float math is the closest
// match; results can still differ in the last bit where x87 kept a sine or
// cosine at extended precision.
//
// Conventions are those of core/Math.h: row vectors, m0/m1/m2 basis rows,
// m3 position, objects face -m2.

#include "core/Math.h"

namespace mm2::game::cam {

inline constexpr float kPi = 3.14159274f;
inline constexpr float kHalfPi = 1.57079637f;
inline constexpr float kQuarterPi = 0.785398185f;
inline constexpr float kTwoPi = 6.28318548f;
inline constexpr float kInvPi = 0.318309873f;
inline constexpr float kDegToRad = 0.0174532924f;

// The world up axis (AGE YAXIS).
inline constexpr Vec3 kYAxis{0.0f, 1.0f, 0.0f};

// Vector3::InvMag: 1 / sqrt((x*x + y*y) + z*z), 0 for a zero vector.
float invMag(const Vec3& v);
// Vector3::Scale
inline Vec3 scaled(const Vec3& v, float s) { return {v.x * s, v.y * s, v.z * s}; }
// Vector3::Angle(this = a, b): angle between two vectors (0 when nearly
// parallel, pi when anti-parallel).
float angle(const Vec3& a, const Vec3& b);

// Matrix34::LookAt(from, to): m2 = normalize(from - to),
// m0 = normalize(YAXIS x m2), m1 = m2 x m0 (not renormalised), m3 = from.
void lookAt(Mat34& m, const Vec3& from, const Vec3& to);

// Matrix34::GetEulers("zxy") / FromEulersZXY: the rotation is
// Rz(e.z) * Rx(e.x) * Ry(e.y) in row-vector order. Only the 3x3 part is read
// or written. Unlike the original (whose asin has no range check), the asin
// argument is clamped to [-1, 1] so a rounding excess cannot produce a NaN.
Vec3 getEulersZXY(const Mat34& m);
void fromEulersZXY(Mat34& m, const Vec3& e);

// Matrix34::MakeRotate(axis, angle): rotation of `angle` about `axis` (3x3
// part of the result; m3 is left as is). An angle of 0 gives the identity;
// an axis along +-X, +-Y or +-Z uses MakeRotateX/Y/Z with the sign folded
// into the angle; any other axis is normalised for MakeRotateUnitAxis.
Mat34 makeRotate(const Vec3& axis, float angle);

// Matrix34::Dot(b): a = a * b, all four rows (m3 also gets b.m3 added).
// Matrix34::Dot3x3(b): only the 3x3 part of a times the 3x3 part of b.
void dot(Mat34& a, const Mat34& b);
void dot3x3(Mat34& a, const Mat34& b);

// Matrix34::Rotate (3x3 only) and RotateFull (also rotates m3 about the
// origin): MakeRotate followed by Dot3x3 / Dot.
void rotate(Mat34& m, const Vec3& axis, float angle);
void rotateFull(Mat34& m, const Vec3& axis, float angle);

// Matrix34::PolarView(distance, azimuth, incline, twist):
// FromEulersZXY(-incline, azimuth, twist), then m3 = distance * m2. The view
// sits `distance` along +m2 from the origin and looks back at it.
void polarView(Mat34& m, float distance, float azimuth, float incline, float twist);

// Vector3::Approach(goal, rate, dt): moves x towards goal.x by rate * dt;
// y only moves once x has arrived, and z once y has. Returns true when all
// three have arrived.
bool approach(Vec3& v, const Vec3& goal, float rate, float dt);

// Converts an Angel CameraFOV (vertical field of view in degrees, as
// gfxViewport::Perspective takes it) to the horizontal field of view in
// radians that it gives on a 4:3 screen (game::Camera::horizontalFov).
float horizontalFov4x3(float verticalDegrees);

} // namespace mm2::game::cam
