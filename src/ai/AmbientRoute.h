#pragma once

// Where an ambient car goes at the end of its road: MM2's
// aiMap::ChooseNextLaneLink and the link choosers it dispatches to
// (ChooseNextRandomLink, ChooseNextLeftStraightLink, ChooseNextRightLink,
// ChooseNextRightStraightLink, ChooseNextStraightLink,
// ChooseStraightLinkAt4Way, ChooseNextFreewayLink,
// ChooseNextRightStraightFreewayLink) and aiRailSet::SolveTurnType.
//
// The choosers walk the arrival intersection's path list from the car's own
// road: one way round for the roads to the right, the other for those to the
// left (MM2 bakes each intersection's list sorted by angle). A road qualifies
// when its side leaving the intersection is open to ambient traffic (side
// flag bit 0 clear). Lane 0 is the leftmost lane: it turns left or goes
// straight, the last lane turns right or goes straight, middle lanes go
// straight, and single-lane roads take any road.

#include "ai/Random.h"
#include "ai/RoadNetwork.h"

namespace mm2::ai {

// A car's position in MM2's terms: road, direction (+1/-1) and lane.
struct RailLink {
    int path = -1;
    int dir = 1;
    int lane = 0;
};

// aiRailSet::SolveTurnType: the angle of the next road's first segment
// against the end of the current one. Values as MM2's.
enum class TurnType : std::uint8_t { Right = 0, Left = 1, Straight = 3 };
TurnType solveTurnType(const RoadNetwork& net, const RailLink& from, int nextPath, int nextDir);

// aiMap::ChooseNextLaneLink for a car on `from` (its drawn lane). Returns
// false when no road qualifies (MM2 then has no next road).
bool chooseNextLaneLink(const RoadNetwork& net, const RailLink& from, Random& rng, RailLink& next);

// The intersection a car on `path` in direction `dir` arrives at.
int arrivalIntersection(const RoadNetwork& net, int path, int dir);

} // namespace mm2::ai
