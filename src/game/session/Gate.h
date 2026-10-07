#pragma once

// Checkpoint gate tests, ported from MM1's mmWaypoints (Open1560
// game.asm: CalculateGatePoints, LineIntersect, WPHit).
//
// Open1560 - An Open Source Re-Implementation of Midtown Madness 1 Beta
// Copyright (C) 2020 Brick. GPL-3.0-or-later; OpenMM2 port under the same licence.

#include "game/session/Types.h"

namespace mm2::game::session {

// Gate end points: position +- width * (cos h, sin h) on (x, z), h in radians.
void calculateGatePoints(const Vec3& position, float headingRad, float width, Vec2& a, Vec2& b);

// Segment p1-p2 against segment q1-q2 with `tolerance` metres of slack on the
// bounding-box checks (mmWaypoints::LineIntersect).
bool lineIntersect(Vec2 p1, Vec2 p2, Vec2 q1, Vec2 q2, float tolerance);

// Car footprint used by the hit test (mmWaypoints' "Size": half width,
// half length).
struct CarExtent {
    float halfWidth = 1.0f;
    float halfLength = 2.2f;
};

// WPHit: the gate is hit when it crosses the car's path since the last
// update, its longitudinal axis or its lateral axis.
bool gateHit(const Checkpoint& cp, const Vec3& previousPosition, const Mat34& car, const CarExtent& extent = {});

// AI test (AnyAIWPHit): the car's centre is within `radius` of the waypoint,
// or it crossed the gate.
bool aiGateHit(const Checkpoint& cp, const Vec3& previousPosition, const Vec3& position, float radius);

Checkpoint makeCheckpoint(const Vec3& position, float headingDeg, float radius);

} // namespace mm2::game::session
