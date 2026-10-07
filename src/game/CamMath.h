#pragma once

// Angel engine (AGE) vector and matrix routines used by the car cameras,
// ported from Midtown Madness 1 (Open1560, code/midtown/game.asm; GPL-3.0,
// Copyright (C) Brick) with the original operation order. AGE ran the x87
// FPU at single precision (Direct3D 7 sets it), so plain 32-bit float math
// is the closest match; results can still differ in the last bit where x87
// kept a value on the stack between operations.
//
// Conventions are those of core/Math.h: row vectors, m0/m1/m2 basis rows,
// m3 position, objects face -m2.

#include "core/Math.h"

namespace mm2::game::cam {

inline constexpr float kPi = 3.14159274f;     // flt_620404
inline constexpr float kHalfPi = 1.57079637f; // flt_620528
inline constexpr float kTwoPi = 6.28318548f;  // flt_620534
inline constexpr float kDegToRad = 0.0174532924f; // flt_6204D4

// Vector3::Mag2 / Mag / InvMag: x*x + (y*y + z*z). The original InvMag has no
// zero check (1/0 = inf); this one returns 0 for a zero vector instead.
inline float mag2(const Vec3& v) { return v.x * v.x + (v.y * v.y + v.z * v.z); }
float mag(const Vec3& v);
float invMag(const Vec3& v);
// Vector3::Scale
inline Vec3 scaled(const Vec3& v, float s) { return {v.x * s, v.y * s, v.z * s}; }

// Matrix34::LookAt(from, to): m3 = from, m2 = normalize(from - to),
// m0 = normalize(Y x m2), m1 = normalize(m2 x m0).
void lookAt(Mat34& m, const Vec3& from, const Vec3& to);

// Matrix34::GetEulers("zxy") / FromEulers(e, "zxy"): the rotation is
// Rz(e.z) * Rx(e.x) * Ry(e.y) in row-vector order. Only the 3x3 part is
// read or written.
Vec3 getEulersZXY(const Mat34& m);
void fromEulersZXY(Mat34& m, const Vec3& e);

// Matrix34::RotateFull with an axis-aligned axis: rotates all four rows
// (basis and position) about the world axis. Positive angles follow
// Mat34::rotationX/Y/Z.
void rotateFullX(Mat34& m, float angle);
void rotateFullY(Mat34& m, float angle);
void rotateFullZ(Mat34& m, float angle);
// Matrix34::RotateFull / Rotate with an arbitrary axis (ArbitraryRotation +
// Dot). The axis is normalised when its squared length is outside
// [0.99999, 1.000001], as in the original. `full` also rotates m3.
void rotateAxis(Mat34& m, const Vec3& axis, float angle, bool full);

// Matrix34::PolarView(offz, roty, rotx, rotz): identity, m3.z = offz, then
// RotateFull Z by -rotz, X by -rotx, Y by roty.
void polarView(Mat34& m, float offz, float roty, float rotx, float rotz);

// Vector3::Approach(goal, speed, dt, carry): moves `v` towards `goal` by
// speed * dt (+ *carry). Returns true only when already within 0.01 m.
bool approach(Vec3& v, const Vec3& goal, float speed, float dt, float* carry);

} // namespace mm2::game::cam
