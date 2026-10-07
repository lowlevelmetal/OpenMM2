#pragma once

// Checkpoint gate tests: MM2's mmWaypointObject (CalculateGatePoints,
// LineIntersect, PlaneHit, RadiusHit) and the car segments mmWaypoints
// feeds them (Update / ClearWaypoint for the player, AIWPHit / AnyWPHits for
// opponents).

#include "game/session/Types.h"

namespace mm2::game::session {

// mmWaypointObject::CalculateGatePoints: position +- radius * (cos h, sin h)
// on (x, z), h in degrees.
void calculateGatePoints(const Vec3& position, float headingDeg, float radius, Vec2& a, Vec2& b);

// mmWaypointObject::LineIntersect: the intersection of the two (infinite)
// lines through p1-p2 and q1-q2, computed in slope / intercept form, must lie
// inside both segments' bounding boxes grown by `tolerance` metres. A
// zero-length segment acts as a vertical line through its point.
bool lineIntersect(Vec2 p1, Vec2 p2, Vec2 q1, Vec2 q2, float tolerance);

// mmWaypointObject::PlaneHit: the gate crosses the segment p1-p2 (with
// size.x of slack), the car's vertical axis +- size.y (slack size.x; a point
// for a level car) or its lateral axis +- size.x (no slack).
bool planeHit(const Checkpoint& cp, const Mat34& car, Vec2 p1, Vec2 p2, const Vec3& size);

// mmWaypointObject::RadiusHit: within the checkpoint radius (3D).
bool radiusHit(const Checkpoint& cp, const Vec3& position);

// The player's test (mmWaypoints::Update): the segment from the car's nose
// to 2 m behind its tail, half the InertiaBox width as slack.
bool playerGateHit(const Checkpoint& cp, const Mat34& car, const Vec3& inertiaBox);

// An opponent's test (mmWaypoints::AIWPHit, AnyWPHits): the segment one
// InertiaBox length either side of the car, with the InertiaBox scaled by 5
// as the size (the scale is the 5.0 mmSingleCircuit / mmSingleRace pass).
bool aiGateHit(const Checkpoint& cp, const Mat34& car, const Vec3& inertiaBox);

// mmWaypoints::AnyWPHits only tests checkpoints within 50 m of the car.
constexpr float kAnyWaypointRange = 50.0f;

Checkpoint makeCheckpoint(const Vec3& position, float headingDeg, float radius);

} // namespace mm2::game::session
