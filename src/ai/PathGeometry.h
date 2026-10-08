#pragma once

// MM2's aiPath geometry, as the AI drivers read it (build 3393, MM2Recomp;
// see docs/parity/ai-vehicles.md). A road's arrays come from the .bai
// (city::AiPath): the centre vertices with their section frames, per side
// the lane, sidewalk, tram and train vertices and the curb and outer edge,
// and the lateral layout parameters. MM2 keeps the first side of the file at
// aiPath +0x38 (its curb and outer edge at +0x80) and the second at +0x9c
// (+0xe4); direction +1 (the window's "forward" flag set) runs with the
// vertex index.

#include "city/AiMap.h"
#include "core/Math.h"

#include <span>
#include <vector>

namespace mm2::ai {

// One of a road's sharp turns (aiPath::InitRoadTurns' 0x44-byte record).
// InitRoadTurns finds the turns and their points; aiPath::CalcRoadTurns
// fills in the circle for the car that asks (the records are shared by every
// car, so they hold the values of the last car that computed them, as in
// MM2).
struct SharpTurn {
    int vertex = 0;       // +0x00 the centre vertex the turn starts at
    float angle = 0.0f;   // +0x04 positive: to the right, going with the vertex index
    float dir = 0.0f;     // +0x08 1 for a right turn, -1 for a left one
    float radius = 0.0f;  // +0x0c
    float setback = 0.0f; // +0x10 from the turn point back to where the circle starts
    Vec3 point;           // +0x14 the inside corner of the turn ("intersection")
    Vec3 center;          // +0x20 the circle's centre
    Vec3 startDir;        // +0x2c unit XZ direction from the centre to the circle's start
    Vec3 endDir;          // +0x38 and to its end
};

// The side's curb (`which` 0) or outer sidewalk edge (1): aiPath +0x80 /
// +0xe4, the pair after the lane, sidewalk, tram and train vertices.
const std::vector<Vec3>& pathBoundary(const city::AiRoadSide& side, int which);

// aiPath's cumulative centre distance of vertex `i` (+0x100: the file's
// value before the section lengths, then each section's end).
float pathCenterDist(const city::AiPath& path, int i);
// aiPath::CenterLength: the centre distance from vertex `a` to vertex `b`.
float pathCenterLength(const city::AiPath& path, int a, int b);

// aiPath::RoadVertice(pos, side): the next vertex ahead of `pos` along the
// road, counted in the direction of `side` (1: the vertex index; else from
// the other end); n (or 0 for the other side) when `pos` is past the road's
// end.
int pathRoadVertice(const city::AiPath& path, const Vec3& pos, int side);
// aiPath::RoadVertice(pos, side, start): the first vertex from `start` (in
// the side's numbering) more than 4 m ahead of `pos`, else the above.
int pathRoadVertice(const city::AiPath& path, const Vec3& pos, int side, int start);
// aiPath::Direction: whether a car with matrix `m` faces the way of the
// vertex index.
bool pathDirection(const city::AiPath& path, const Mat34& m);
// aiPath::Index(pos): the first vertex `pos` is behind (n - 1 when it is
// more than the half width off the road there, or behind none).
int pathIndex(const city::AiPath& path, const Vec3& pos);
// aiPath::IsPosOnRoad: 1 on the road, 2 on the sidewalk, 3 off it, with the
// lateral offset from the centre line (positive toward the second side) in
// `lat`. The limits are the second side's (its params[2n - 1] and
// params[2n + 1], n its lanes), less `margin`, on both sides.
int pathIsPosOnRoad(const city::AiPath& path, const Vec3& pos, float margin, float* lat = nullptr);

// aiPath::InitRoadTurns: the road's sharp turns. A turn is a vertex whose
// next section turns more than 0.7 rad; a section shorter than 10 m (but the
// last two) is merged with the following one, the turn then starting at the
// first vertex and its corner where the curbs before and after it (moved
// 1.5 m in) cross.
std::vector<SharpTurn> initRoadTurns(const city::AiPath& path);

// aiPath::CalcRoadTurns for a car at `pos`: each turn's radius (from the
// room R between the car and the inside corner, clamped to [3, 2 x the
// second side's road limit - 1.5]: R / (1 - sin((3.14 - |angle|) / 2))),
// setback, centre and end directions. `forward` is the window's direction
// flag for this road.
void calcRoadTurns(const city::AiPath& path, std::span<SharpTurn> turns, const Vec3& pos, bool forward);

// aiPath::IsSharpTurn: the index (in the direction's order) of the turn at
// vertex `vertex` (counted in the direction of travel), or -1.
int isSharpTurn(const city::AiPath& path, std::span<const SharpTurn> turns, int vertex, bool forward);
// The turn with index `i` in the direction's order (aiPath::SharpTurnAngle,
// SharpTurnDir, SharpTurnRadius, SharpTurnSetback, SharpTurnCenter,
// SharpTurnIntersection, SharpTurnStartDir, SharpTurnEndDir all read it; MM2
// reverses the list for direction -1 but not the values).
const SharpTurn& sharpTurn(std::span<const SharpTurn> turns, int i, bool forward);
// aiPath::SharpTurnVertIndex: its vertex, counted in the direction of travel.
int sharpTurnVertIndex(const city::AiPath& path, std::span<const SharpTurn> turns, int i, bool forward);

} // namespace mm2::ai
