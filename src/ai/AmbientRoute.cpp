// Ambient traffic route choice after MM2's aiMap::ChooseNext*Link family
// (build 3393); see AmbientRoute.h.
#include "ai/AmbientRoute.h"

#include <cmath>
#include <limits>
#include <vector>
#include <algorithm>

namespace mm2::ai {
namespace {

struct Ctx {
    const RoadNetwork& net;
    const RailLink& from;
    Random& rng;
    RailLink& next;
    int node = -1;  // arrival intersection
    int index = -1; // the car's road in its path list
};

const PathInfo& pathOf(const RoadNetwork& net, int p) {
    return net.paths()[static_cast<std::size_t>(p)];
}

int pathCount(const Ctx& c) {
    return static_cast<int>(c.net.intersections()[static_cast<std::size_t>(c.node)].paths.size());
}

int pathAt(const Ctx& c, int i) {
    return c.net.intersections()[static_cast<std::size_t>(c.node)].paths[static_cast<std::size_t>(i)];
}

// Direction of travel leaving the intersection along `path`: +1 when the
// intersection is the path's ends[1] (direction +1 starts there).
int leavingDir(const Ctx& c, int path) {
    return pathOf(c.net, path).intersection[1] == c.node ? 1 : -1;
}

int laneCount(const RoadNetwork& net, int path, int dir) {
    return static_cast<int>(pathOf(net, path).lanesOf(dir).size());
}

bool available(const Ctx& c, int path) {
    return (pathOf(c.net, path).flagsOf(leavingDir(c, path)) & 1) == 0;
}

// Lane row `lane` of (path, dir) as MM2 indexes its vertex rows: the row
// after the last lane is the side's sidewalk line.
const Polyline* row(const RoadNetwork& net, int path, int dir, int lane) {
    const auto& lanes = pathOf(net, path).lanesOf(dir);
    if (lane >= 0 && static_cast<std::size_t>(lane) < lanes.size())
        return &net.lanes()[static_cast<std::size_t>(lanes[static_cast<std::size_t>(lane)])].line;
    if (static_cast<std::size_t>(lane) == lanes.size()) {
        const int side = dir == 1 ? 1 : 0;
        for (int w : pathOf(net, path).sidewalks)
            if (net.sidewalks()[static_cast<std::size_t>(w)].side == side)
                return &net.sidewalks()[static_cast<std::size_t>(w)].centre;
    }
    return nullptr;
}

// The car's lane end and the road's left vector there (xAxis at the last
// section for direction +1, -xAxis at the first for -1).
void endFrame(const Ctx& c, Vec3& end, Vec3& left) {
    const PathInfo& p = pathOf(c.net, c.from.path);
    const Polyline* line = row(c.net, c.from.path, c.from.dir, c.from.lane);
    end = line && !line->points.empty() ? line->points.back() : p.centreEnd;
    if (p.xAxis.empty()) {
        left = {};
        return;
    }
    left = c.from.dir == 1 ? p.xAxis.back() : -p.xAxis.front();
}

void take(Ctx& c, int path, int lane) {
    c.next.path = path;
    c.next.dir = leavingDir(c, path);
    c.next.lane = lane;
}

// The second road of a left/right-or-straight pair qualifies when its start
// lies within 2 m of the car's lane line on the near side (lateral offset
// along the road's left vector).
float lateralOfStart(const Ctx& c, int path) {
    int lane = c.from.lane;
    const int count = laneCount(c.net, path, leavingDir(c, path));
    if (count < lane) // MM2 clamps only when the lane is beyond the count
        lane = count - 1;
    const Polyline* line = row(c.net, path, leavingDir(c, path), lane);
    if (!line || line->points.empty())
        return 0.0f;
    Vec3 end, left;
    endFrame(c, end, left);
    const Vec3 d = line->points.front() - end;
    return d.x * left.x + d.z * left.z;
}

bool chooseNextRightLink(Ctx& c) {
    const int n = pathCount(c);
    int i = c.index;
    for (int k = 1; k < n; ++k) {
        if (++i >= n)
            i -= n;
        const int path = pathAt(c, i);
        if (available(c, path)) {
            take(c, path, laneCount(c.net, path, leavingDir(c, path)) - 1);
            return true;
        }
    }
    return false;
}

// ChooseNextLeftStraightLink (step -1, the left road or the next one when it
// is not more than 2 m to the right; next lane 0) and
// ChooseNextRightStraightLink (step +1, the right road or the next one when
// it is not more than 2 m to the left; the last lane).
bool chooseTurnOrStraight(Ctx& c, int step) {
    const int n = pathCount(c);
    int i = c.index;
    int found = 0;
    int pick[2] = {-1, -1};
    int k = 1;
    for (; k < n; ++k) {
        i += step;
        if (i < 0)
            i += n;
        if (i >= n)
            i -= n;
        if (available(c, pathAt(c, i))) {
            pick[0] = pathAt(c, i);
            found = 1;
            ++k;
            break;
        }
    }
    for (; found == 1 && k < n; ++k) {
        i += step;
        if (i < 0)
            i += n;
        if (i >= n)
            i -= n;
        const int path = pathAt(c, i);
        if (!available(c, path))
            continue;
        const float lateral = lateralOfStart(c, path);
        if (step < 0 ? lateral > -2.0f : lateral < 2.0f) {
            pick[1] = path;
            found = 2;
        }
        break;
    }
    if (found == 0)
        return false;
    const int choice = static_cast<int>(c.rng.frand() * static_cast<float>(found));
    const int path = pick[choice];
    take(c, path, step < 0 ? 0 : laneCount(c.net, path, leavingDir(c, path)) - 1);
    return true;
}

int numAvailSinks(const Ctx& c) {
    int count = 0;
    for (int path : c.net.intersections()[static_cast<std::size_t>(c.node)].paths) {
        if (path == c.from.path)
            continue;
        const PathInfo& p = pathOf(c.net, path);
        std::uint16_t flags;
        if (p.intersection[1] == c.node) {
            flags = p.sideFlags[1];
        } else {
            if (p.sideLanes[0].empty())
                continue;
            flags = p.sideFlags[0];
        }
        if ((flags & 1) == 0)
            ++count;
    }
    return count;
}

bool chooseNextStraightLink(Ctx& c) {
    const int n = pathCount(c);
    int i = c.index;
    if (numAvailSinks(c) < 3) {
        // Roads with more than two lanes leaving: one is taken; several, the
        // one whose far end lies nearest the car's line.
        std::vector<int> wide;
        for (int k = 1; k < n; ++k) {
            if (++i >= n)
                i -= n;
            const int path = pathAt(c, i);
            if (available(c, path) && laneCount(c.net, path, leavingDir(c, path)) > 2)
                wide.push_back(path);
        }
        if (wide.empty())
            return chooseNextRightLink(c);
        int best = 0;
        if (wide.size() > 1) {
            Vec3 end, left;
            endFrame(c, end, left);
            float bestOffset = 999999.0f;
            for (std::size_t w = 0; w < wide.size(); ++w) {
                const int dir = leavingDir(c, wide[w]);
                const Polyline* line = row(c.net, wide[w], dir, laneCount(c.net, wide[w], dir) - 1);
                if (!line || line->points.empty())
                    continue;
                const Vec3 d = line->points.back() - end;
                const float offset = std::abs(d.x * left.x + d.z * left.z);
                if (offset < bestOffset) {
                    bestOffset = offset;
                    best = static_cast<int>(w);
                }
            }
        }
        // MM2 keeps the lane number unclamped; retail roads never need it.
        const int path = wide[static_cast<std::size_t>(best)];
        take(c, path, std::min(c.from.lane, laneCount(c.net, path, leavingDir(c, path)) - 1));
        return true;
    }
    // At least three ways out: the second open road round to the right.
    int seen = 0;
    for (int k = 1; k < n; ++k) {
        if (++i >= n)
            i -= n;
        const int path = pathAt(c, i);
        if (available(c, path) && ++seen == 2) {
            take(c, path, std::min(c.from.lane, laneCount(c.net, path, leavingDir(c, path)) - 1));
            return true;
        }
    }
    return true; // MM2 returns success and keeps the previous next road
}

bool chooseStraightLinkAt4Way(Ctx& c) {
    const int path = pathAt(c, (c.index + 2) % pathCount(c));
    take(c, path, std::min(c.from.lane, laneCount(c.net, path, leavingDir(c, path)) - 1));
    return true;
}

bool chooseNextRandomLink(Ctx& c) {
    const int n = pathCount(c);
    int i = c.index;
    std::vector<int> open;
    for (int k = 1; k < n; ++k) {
        if (++i >= n)
            i -= n;
        const int path = pathAt(c, i);
        if (!available(c, path))
            continue;
        if (pathOf(c.net, path).freeway()) {
            take(c, path, laneCount(c.net, path, leavingDir(c, path)) - 1);
            return true;
        }
        open.push_back(path);
    }
    if (open.empty())
        return false;
    const int path =
        open[static_cast<std::size_t>(c.rng.frand() * static_cast<float>(open.size()))];
    const int dir = leavingDir(c, path);
    const TurnType turn = solveTurnType(c.net, c.from, path, dir);
    take(c, path, turn != TurnType::Right ? 0 : laneCount(c.net, path, dir) - 1);
    return true;
}

bool byLane(Ctx& c) {
    const int count = laneCount(c.net, c.from.path, c.from.dir);
    if (c.from.lane == 0)
        return chooseTurnOrStraight(c, -1);
    if (c.from.lane == count - 1)
        return chooseTurnOrStraight(c, +1);
    return chooseNextStraightLink(c);
}

bool chooseNextFreewayLink(Ctx& c) {
    const int n = pathCount(c);
    int i = c.index;
    for (int k = 1; k < n; ++k) {
        if (++i >= n)
            i -= n;
        const int path = pathAt(c, i);
        if (available(c, path) && pathOf(c.net, path).freeway()) {
            take(c, path, std::min(c.from.lane, laneCount(c.net, path, leavingDir(c, path)) - 1));
            return true;
        }
    }
    return byLane(c);
}

bool chooseNextRightStraightFreewayLink(Ctx& c) {
    // MM2 makes a choice here and then overwrites it with the lane rule.
    if (static_cast<int>(c.rng.frand() * 2.0f) == 1)
        chooseNextFreewayLink(c);
    else
        chooseNextRightLink(c);
    return byLane(c);
}

} // namespace

int arrivalIntersection(const RoadNetwork& net, int path, int dir) {
    if (path < 0 || static_cast<std::size_t>(path) >= net.paths().size())
        return -1;
    return pathOf(net, path).intersection[dir == 1 ? 0 : 1];
}

TurnType solveTurnType(const RoadNetwork& net, const RailLink& from, int nextPath, int nextDir) {
    const city::AiMap* map = net.source();
    if (!map || nextPath < 0 || from.path < 0)
        return TurnType::Straight;
    const city::AiPath& to = map->paths[static_cast<std::size_t>(nextPath)];
    const city::AiPath& cur = map->paths[static_cast<std::size_t>(from.path)];
    if (to.center.size() < 2 || cur.xAxis.empty() || cur.zAxis.empty())
        return TurnType::Straight;
    // The next road's first segment in its direction of travel.
    const std::size_t n = to.center.size();
    const Vec3 v = nextDir == 1 ? to.center[1] - to.center[0] : to.center[n - 2] - to.center[n - 1];
    // The current road's right and forward vectors at its arrival end.
    Vec3 right, forward;
    if (from.dir == 1) {
        right = -cur.xAxis.back();
        forward = -cur.zAxis.back();
    } else {
        right = cur.xAxis.front();
        forward = cur.zAxis.front();
    }
    const float angle = std::atan2(v.dot(right), v.dot(forward));
    if (angle > 0.5f)
        return TurnType::Right;
    if (angle < -0.5f)
        return TurnType::Left;
    return TurnType::Straight;
}

bool chooseNextLaneLink(const RoadNetwork& net, const RailLink& from, Random& rng, RailLink& next) {
    next = {};
    if (from.path < 0 || static_cast<std::size_t>(from.path) >= net.paths().size())
        return false;
    const PathInfo& p = pathOf(net, from.path);
    const int end = from.dir == 1 ? 0 : 1;
    Ctx c{net, from, rng, next, p.intersection[end], p.roadIndex[end]};
    if (c.node < 0 || c.index < 0 || pathCount(c) == 0)
        return false;
    bool ok;
    const std::uint16_t endFlags = p.endFlags[end];
    if (endFlags == 3) {
        ok = chooseStraightLinkAt4Way(c);
    } else if (endFlags == 1) {
        ok = chooseNextRightLink(c);
    } else if (p.freeway()) {
        const int count = laneCount(net, from.path, from.dir);
        ok = from.lane == count - 1 ? chooseNextRightStraightFreewayLink(c) : chooseNextFreewayLink(c);
    } else {
        const int count = laneCount(net, from.path, from.dir);
        if (count == 1)
            ok = chooseNextRandomLink(c);
        else if (count == 2)
            ok = from.lane == 0 ? chooseTurnOrStraight(c, -1) : chooseTurnOrStraight(c, +1);
        else
            ok = byLane(c);
    }
    if (!ok || next.path < 0 || next.lane < 0) {
        next = {};
        return false;
    }
    return true;
}

} // namespace mm2::ai
