// Driving lines for AI racers and police: MM2's waypoint routes
// (aiVehiclePhysics::RegisterRoute); see Course.h and docs/ai.md. First ported
// from MM1's aiGoalFollowWayPts (Open1560 game.asm, GPL-3.0).
#include "ai/Course.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace mm2::ai {
namespace {

float xzDist(const Vec3& a, const Vec3& b) {
    return Vec2{a.x - b.x, a.z - b.z}.mag();
}

Vec3 flatUnit(const Vec3& v) {
    const Vec2 f{v.x, v.z};
    const float m = f.mag();
    return m > 1e-6f ? Vec3{f.x / m, 0.0f, f.y / m} : Vec3{0, 0, -1};
}

// Right of a driving direction (Angel: forward -Z, right +X).
Vec3 rightOf(const Vec3& dir) {
    return {-dir.z, 0.0f, dir.x};
}

// Distance from the centre line to one side's curb at section `k`: the
// side's curb polyline (after its lanes, sidewalk, trams and trains), else
// its outermost lane + 2.5 m. With `outer`, the polyline after the curb (the
// outer edge of the sidewalk).
float curbOffset(const city::AiRoadSide& side, const Vec3& centre, const Vec3& xAxis, float fallback,
                 bool outer = false) {
    const std::size_t base =
        static_cast<std::size_t>(side.numLanes + side.numSidewalks + side.numTrams + side.numTrains);
    const std::vector<Vec3>* poly = nullptr;
    float extra = 0.0f;
    const std::size_t which = base + (outer ? 1 : 0);
    if (which < side.polylines.size()) {
        poly = &side.polylines[which];
    } else if (side.numLanes > 0 && static_cast<std::size_t>(side.numLanes) <= side.polylines.size()) {
        poly = &side.polylines[static_cast<std::size_t>(side.numLanes) - 1];
        extra = outer ? 5.0f : 2.5f;
    }
    if (!poly || poly->empty())
        return fallback;
    float best = std::numeric_limits<float>::max();
    Vec3 nearest;
    for (const Vec3& q : *poly) {
        const float d = xzDist(q, centre);
        if (d < best) {
            best = d;
            nearest = q;
        }
    }
    const float offset = std::abs((nearest - centre).dot(xAxis)) + extra;
    return offset > 1.0f ? offset : fallback;
}

int lanesOf(const city::AiPath& p) {
    return std::max<int>(1, std::max(p.left.numLanes, p.right.numLanes));
}

float pathLength(const city::AiPath& p) {
    float len = 0.0f;
    for (std::size_t i = 1; i < p.center.size(); ++i)
        len += xzDist(p.center[i], p.center[i - 1]);
    return len;
}

// Road joining two intersections, -1 if none: the first of `a`'s roads that
// joins `b`, as the racers' driver picks it (aiMap::DetRdSegBetweenInts; a
// shortcut road may come before a main road).
int directPath(const RoadNetwork& net, int a, int b) {
    if (a < 0 || static_cast<std::size_t>(a) >= net.intersections().size())
        return -1;
    for (int p : net.intersections()[static_cast<std::size_t>(a)].paths) {
        if (p < 0 || static_cast<std::size_t>(p) >= net.paths().size())
            continue;
        const PathInfo& info = net.paths()[static_cast<std::size_t>(p)];
        const bool joins = (info.intersection[0] == a && info.intersection[1] == b) ||
                           (info.intersection[1] == a && info.intersection[0] == b);
        if (joins)
            return p;
    }
    return -1;
}

} // namespace

void pathCurbs(const city::AiPath& path, std::size_t k, float& left, float& right) {
    const float fallback = path.halfWidth > 1.0f ? path.halfWidth * 0.75f : 5.0f;
    if (k >= path.center.size() || k >= path.xAxis.size()) {
        left = right = fallback;
        return;
    }
    const Vec3 x = flatUnit(path.xAxis[k]);
    left = curbOffset(path.left, path.center[k], x, fallback);
    right = curbOffset(path.right, path.center[k], x, fallback);
}

void pathOuterEdges(const city::AiPath& path, std::size_t k, float& left, float& right) {
    float curbL, curbR;
    pathCurbs(path, k, curbL, curbR);
    if (k >= path.center.size() || k >= path.xAxis.size()) {
        left = curbL;
        right = curbR;
        return;
    }
    const Vec3 x = flatUnit(path.xAxis[k]);
    left = std::max(curbL, curbOffset(path.left, path.center[k], x, curbL, true));
    right = std::max(curbR, curbOffset(path.right, path.center[k], x, curbR, true));
}

void pathOnRoadLimits(const city::AiPath& path, float& road, float& sidewalk) {
    const auto& layout = path.right.params;
    const int n = path.right.numLanes;
    auto plausible = [](float v) { return std::isfinite(v) && v >= 0.0f && v < 100.0f; };
    if (n == 0 && plausible(layout[1])) {
        road = 0.0f;
        sidewalk = layout[1];
        return;
    }
    const auto roadAt = static_cast<std::size_t>(2 * n - 1);
    const auto walkAt = static_cast<std::size_t>(2 * n + 1);
    if (n > 0 && walkAt < layout.size() && plausible(layout[roadAt]) && plausible(layout[walkAt]) &&
        layout[roadAt] > 0.0f && layout[roadAt] <= layout[walkAt]) {
        road = layout[roadAt];
        sidewalk = layout[walkAt];
        return;
    }
    const std::size_t k = path.center.size() / 2;
    float l, r, el, er;
    pathCurbs(path, k, l, r);
    pathOuterEdges(path, k, el, er);
    road = std::min(l, r);
    sidewalk = std::max(el, er);
}

int nearestIntersection(const RoadNetwork& net, const Vec3& p, float* distance) {
    int best = -1;
    float bestD = std::numeric_limits<float>::max();
    for (const Intersection& n : net.intersections()) {
        if (n.paths.empty())
            continue;
        const float d = xzDist(n.centre, p) + 0.25f * std::abs(n.centre.y - p.y);
        if (d < bestD) {
            bestD = d;
            best = n.id;
        }
    }
    if (distance)
        *distance = bestD;
    return best;
}

std::vector<int> findRoute(const RoadNetwork& net, int from, int to, std::span<const int> avoid, float penalty) {
    const auto& nodes = net.intersections();
    const std::size_t n = nodes.size();
    if (from < 0 || to < 0 || static_cast<std::size_t>(from) >= n || static_cast<std::size_t>(to) >= n)
        return {};
    if (from == to)
        return {from};
    std::vector<float> dist(n, std::numeric_limits<float>::max());
    std::vector<int> prev(n, -1);
    using Item = std::pair<float, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<>> open;
    dist[static_cast<std::size_t>(from)] = 0.0f;
    open.push({0.0f, from});
    while (!open.empty()) {
        const auto [d, u] = open.top();
        open.pop();
        if (d > dist[static_cast<std::size_t>(u)])
            continue;
        if (u == to)
            break;
        for (int p : nodes[static_cast<std::size_t>(u)].paths) {
            if (p < 0 || static_cast<std::size_t>(p) >= net.paths().size())
                continue;
            const PathInfo& info = net.paths()[static_cast<std::size_t>(p)];
            const int v = info.intersection[0] == u ? info.intersection[1] : info.intersection[0];
            if (v < 0 || v == u || (info.intersection[0] != u && info.intersection[1] != u))
                continue;
            // Road length plus the way across both intersections.
            const auto& src = net.source()->paths[static_cast<std::size_t>(p)];
            const Vec3& cu = nodes[static_cast<std::size_t>(u)].centre;
            const Vec3& cv = nodes[static_cast<std::size_t>(v)].centre;
            float w = pathLength(src) + std::min(xzDist(cu, info.centreStart), xzDist(cu, info.centreEnd)) +
                       std::min(xzDist(cv, info.centreStart), xzDist(cv, info.centreEnd));
            if (std::find(avoid.begin(), avoid.end(), p) != avoid.end())
                w += penalty;
            if (d + w < dist[static_cast<std::size_t>(v)]) {
                dist[static_cast<std::size_t>(v)] = d + w;
                prev[static_cast<std::size_t>(v)] = u;
                open.push({d + w, v});
            }
        }
    }
    if (prev[static_cast<std::size_t>(to)] < 0)
        return {};
    std::vector<int> route;
    for (int v = to; v >= 0; v = prev[static_cast<std::size_t>(v)]) {
        route.push_back(v);
        if (v == from)
            break;
    }
    std::reverse(route.begin(), route.end());
    return route;
}

RoadSpot locateOnRoads(const RoadNetwork& net, const Vec3& p, bool shortcuts) {
    RoadSpot spot;
    const city::AiMap* map = net.source();
    if (!map)
        return spot;
    for (std::size_t pi = 0; pi < map->paths.size(); ++pi) {
        if (!shortcuts && map->isShortcut(pi))
            continue;
        const city::AiPath& path = map->paths[pi];
        const Aabb& b = net.paths()[pi].bounds;
        const float margin = path.halfWidth + 5.0f;
        if (p.x < b.min.x - margin || p.x > b.max.x + margin || p.z < b.min.z - margin || p.z > b.max.z + margin)
            continue;
        float s0 = 0.0f;
        for (std::size_t k = 1; k < path.center.size(); ++k) {
            const Vec3& a = path.center[k - 1];
            const Vec3& c = path.center[k];
            const Vec2 ab{c.x - a.x, c.z - a.z};
            const float len2 = ab.mag2();
            const float t = len2 > 1e-6f ? clampf(Vec2{p.x - a.x, p.z - a.z}.dot(ab) / len2, 0.0f, 1.0f) : 0.0f;
            const Vec3 q = lerp(a, c, t);
            const float d = xzDist(q, p) + 0.5f * std::abs(q.y - p.y);
            const float segLen = std::sqrt(len2);
            if (d < spot.distance) {
                const Vec3 x = flatUnit(lerp(path.xAxis[k - 1], path.xAxis[k], t));
                float l0, r0, l1, r1;
                pathCurbs(path, k - 1, l0, r0);
                pathCurbs(path, k, l1, r1);
                spot.path = static_cast<int>(pi);
                spot.distance = d;
                spot.s = s0 + t * segLen;
                spot.lateral = (p - q).dot(x);
                const bool inside = (k > 1 || t > 0.0f) && (k + 1 < path.center.size() || t < 1.0f);
                const float limit = spot.lateral >= 0.0f ? lerp(l0, l1, t) : lerp(r0, r1, t);
                spot.onRoad = inside && std::abs(spot.lateral) <= limit + 1.0f && std::abs(q.y - p.y) < 6.0f;
            }
            s0 += segLen;
        }
    }
    if (spot.onRoad)
        return spot;
    // Inside an intersection: nearer its centre than the ends of its roads.
    for (const Intersection& n : net.intersections()) {
        const float d = xzDist(n.centre, p);
        if (d > 60.0f || std::abs(n.centre.y - p.y) > 6.0f)
            continue;
        float radius = 0.0f;
        for (int pid : n.paths) {
            if (pid < 0 || static_cast<std::size_t>(pid) >= net.paths().size())
                continue;
            const PathInfo& info = net.paths()[static_cast<std::size_t>(pid)];
            radius = std::max(radius, std::min(xzDist(info.centreStart, n.centre), xzDist(info.centreEnd, n.centre)));
        }
        if (d <= radius + 1.0f) {
            spot.intersection = n.id;
            spot.onRoad = true;
            return spot;
        }
    }
    return spot;
}

std::optional<Course> Course::build(const RoadNetwork& net, std::span<const int> intersections, const Vec3& start,
                                    const std::optional<Vec3>& finish, bool loop, std::string* error) {
    auto fail = [&](std::string msg) -> std::optional<Course> {
        if (error)
            *error = std::move(msg);
        return std::nullopt;
    };
    const city::AiMap* map = net.source();
    if (!map)
        return fail("road network has no AI map");
    const int count = static_cast<int>(net.intersections().size());

    // Intersections joined by one road each (waypoints are adjacent; MM2's
    // aiMap::DetRdSegBetweenInts finds no road between others). Gaps are
    // filled with the shortest route that keeps off the roads the course
    // drives elsewhere, so that it does not double back along the next leg
    // (inferred).
    std::vector<int> wanted;
    for (int id : intersections)
        if (id >= 0 && id < count && (wanted.empty() || wanted.back() != id))
            wanted.push_back(id);
    if (loop && wanted.size() > 1 && wanted.front() != wanted.back())
        wanted.push_back(wanted.front());
    std::vector<int> used;
    for (std::size_t i = 0; i + 1 < wanted.size(); ++i)
        if (const int p = directPath(net, wanted[i], wanted[i + 1]); p >= 0)
            used.push_back(p);
    std::vector<int> seq;
    for (int id : wanted) {
        if (seq.empty() || directPath(net, seq.back(), id) >= 0) {
            seq.push_back(id);
            continue;
        }
        std::vector<int> avoid = used;
        if (seq.size() > 1)
            avoid.push_back(directPath(net, seq[seq.size() - 2], seq.back()));
        const auto route = findRoute(net, seq.back(), id, avoid);
        if (route.size() < 2)
            return fail("no road route between intersections");
        for (std::size_t k = 1; k < route.size(); ++k)
            used.push_back(directPath(net, route[k - 1], route[k]));
        seq.insert(seq.end(), route.begin() + 1, route.end());
    }

    // A start or finish off the waypoint roads (police routes, the finish
    // rows of some races) is joined along the road it lies on: the part of
    // that road up to one of its intersections, routed to the waypoints.
    struct Partial {
        int path = -1;
        float s = 0.0f;           // projection along the road (increasing section order)
        bool towardBack = false;  // driving towards center.back() (PathInfo::intersection[0])
    };
    auto onRoad = [&](int path, const Vec3& p) {
        if (path < 0)
            return false;
        const city::AiPath& src = map->paths[static_cast<std::size_t>(path)];
        float best = std::numeric_limits<float>::max(), limit = 0.0f;
        for (std::size_t k = 1; k < src.center.size(); ++k) {
            const Vec2 a{src.center[k - 1].x, src.center[k - 1].z}, b{src.center[k].x, src.center[k].z};
            const Vec2 q{p.x, p.z}, ab = b - a;
            const float t = ab.mag2() > 1e-6f ? clampf((q - a).dot(ab) / ab.mag2(), 0.0f, 1.0f) : 0.0f;
            const float d = (a + ab * t - q).mag();
            if (d < best) {
                best = d;
                float l, r;
                pathCurbs(src, t < 0.5f ? k - 1 : k, l, r);
                limit = std::max(l, r);
            }
        }
        return best <= limit + 4.0f;
    };
    // The end of the road under `p` to join the waypoints at `joinAt`
    // (-1: any), routing to it when it is not that intersection.
    // (The main roads only: a shortcut road passing a grid place would take
    // the course round its far end.)
    auto joinRoad = [&](const Vec3& p, int joinAt, std::vector<int>& route, bool fromRoad) -> std::optional<Partial> {
        const RoadSpot spot = locateOnRoads(net, p, false);
        if (spot.path < 0 || spot.distance > 30.0f)
            return std::nullopt;
        const PathInfo& info = net.paths()[static_cast<std::size_t>(spot.path)];
        const float len = pathLength(map->paths[static_cast<std::size_t>(spot.path)]);
        int end = -1;
        float bestCost = std::numeric_limits<float>::max();
        for (int e = 0; e < 2; ++e) {
            const int id = info.intersection[e];
            if (id < 0)
                continue;
            const float along = e == 0 ? len - spot.s : spot.s;
            float cost = along;
            if (joinAt >= 0 && id != joinAt) {
                const auto r = fromRoad ? findRoute(net, id, joinAt, std::span<const int>(&spot.path, 1))
                                        : findRoute(net, joinAt, id, std::span<const int>(&spot.path, 1));
                if (r.size() < 2)
                    continue;
                cost += xzDist(net.intersections()[static_cast<std::size_t>(id)].centre,
                               net.intersections()[static_cast<std::size_t>(joinAt)].centre) +
                        100.0f * static_cast<float>(r.size());
            }
            if (cost < bestCost) {
                bestCost = cost;
                end = e;
            }
        }
        if (end < 0)
            return std::nullopt;
        const int id = info.intersection[end];
        route.clear();
        if (joinAt >= 0 && id != joinAt)
            route = fromRoad ? findRoute(net, id, joinAt, std::span<const int>(&spot.path, 1))
                             : findRoute(net, joinAt, id, std::span<const int>(&spot.path, 1));
        else
            route = {id};
        return Partial{spot.path, spot.s, fromRoad ? end == 0 : end != 0};
    };

    std::optional<Partial> head, tail;
    const bool startOnFirst = seq.size() >= 2 && onRoad(directPath(net, seq[0], seq[1]), start);
    if (!loop && !startOnFirst) {
        std::vector<int> route;
        head = joinRoad(start, seq.empty() ? -1 : seq.front(), route, true);
        if (head) {
            // route runs from the road's end to the first waypoint.
            seq.insert(seq.begin(), route.begin(), route.end() - (seq.empty() ? 0 : 1));
        }
    }
    if (!loop && finish && !seq.empty()) {
        const bool onLast = seq.size() >= 2 && onRoad(directPath(net, seq[seq.size() - 2], seq.back()), *finish);
        if (!onLast) {
            std::vector<int> route;
            tail = joinRoad(*finish, seq.back(), route, false);
            if (tail && route.size() > 1)
                seq.insert(seq.end(), route.begin() + 1, route.end());
        }
    }

    Course c;
    c.m_loop = loop && seq.size() > 2;
    c.m_intersections = seq;
    std::vector<Vec3>& pts = c.m_line.points;
    auto add = [&](const Vec3& p, const CoursePoint& cp) {
        if (!pts.empty() && xzDist(pts.back(), p) < 1.0f)
            return;
        pts.push_back(p);
        c.m_points.push_back(cp);
    };
    auto addSection = [&](int pathIndex, std::size_t sec, bool forward) {
        const city::AiPath& src = map->paths[static_cast<std::size_t>(pathIndex)];
        float l, r, el, er;
        pathCurbs(src, sec, l, r);
        pathOuterEdges(src, sec, el, er);
        CoursePoint cp;
        cp.left = forward ? l : r;
        cp.right = forward ? r : l;
        cp.leftEdge = forward ? el : er;
        cp.rightEdge = forward ? er : el;
        cp.path = pathIndex;
        cp.lanes = lanesOf(src);
        cp.flags = src.flags;
        pathOnRoadLimits(src, cp.onRoad, cp.onSidewalk);
        // The section's x axis points to the left of travel in increasing
        // section order.
        if (sec < src.xAxis.size())
            cp.across = flatUnit(src.xAxis[sec]) * (forward ? -1.0f : 1.0f);
        add(src.center[sec], cp);
    };
    // Sections of a road strictly beyond / before arc length `s`.
    auto addPartial = [&](const Partial& part, bool fromProjection) {
        const city::AiPath& src = map->paths[static_cast<std::size_t>(part.path)];
        std::vector<float> cum(src.center.size(), 0.0f);
        for (std::size_t k = 1; k < cum.size(); ++k)
            cum[k] = cum[k - 1] + xzDist(src.center[k], src.center[k - 1]);
        const std::size_t n = src.center.size();
        for (std::size_t j = 0; j < n; ++j) {
            const std::size_t sec = part.towardBack ? j : n - 1 - j;
            const bool ahead = part.towardBack ? cum[sec] > part.s + 1.0f : cum[sec] < part.s - 1.0f;
            if (ahead == fromProjection)
                addSection(part.path, sec, part.towardBack);
        }
    };

    if (head) {
        add(start, CoursePoint{});
        addPartial(*head, true);
    } else if (!startOnFirst && !c.m_loop) {
        add(start, CoursePoint{});
    }
    std::vector<std::pair<std::size_t, std::size_t>> legPoints;
    for (std::size_t k = 0; k + 1 < seq.size(); ++k) {
        const int a = seq[k], b = seq[k + 1];
        const int p = directPath(net, a, b);
        if (p < 0)
            return fail("waypoints are not joined by a road");
        const city::AiPath& src = map->paths[static_cast<std::size_t>(p)];
        const bool forward = net.paths()[static_cast<std::size_t>(p)].intersection[1] == a;
        const std::size_t n = src.center.size();
        const std::size_t first = pts.size();
        for (std::size_t j = 0; j < n; ++j)
            addSection(p, forward ? j : n - 1 - j, forward);
        CourseLeg leg;
        leg.path = p;
        leg.forward = forward;
        leg.from = a;
        leg.to = b;
        c.m_legs.push_back(leg);
        legPoints.push_back({std::min(first, pts.size() - 1), pts.size() - 1});
    }
    if (tail)
        addPartial(*tail, false);
    if (finish && !c.m_loop && (pts.empty() || xzDist(*finish, pts.back()) > 3.0f))
        add(*finish, c.m_points.empty() ? CoursePoint{} : c.m_points.back());
    if (pts.empty())
        return fail("empty course");
    // The first point (a start off the roads) takes the edges of the next.
    if (c.m_points.size() > 1 && c.m_points.front().path < 0) {
        c.m_points.front().left = c.m_points[1].left;
        c.m_points.front().right = c.m_points[1].right;
        c.m_points.front().leftEdge = c.m_points[1].leftEdge;
        c.m_points.front().rightEdge = c.m_points[1].rightEdge;
        c.m_points.front().flags = c.m_points[1].flags;
        c.m_points.front().onRoad = c.m_points[1].onRoad;
        c.m_points.front().onSidewalk = c.m_points[1].onSidewalk;
    }
    if (pts.size() == 1) {
        pts.push_back(pts.front() + Vec3{0, 0, -1});
        c.m_points.push_back(c.m_points.front());
    }
    if (c.m_loop && xzDist(pts.front(), pts.back()) > 1e-3f) {
        pts.push_back(pts.front());
        CoursePoint cp = c.m_points.front();
        cp.path = -1;
        c.m_points.push_back(cp);
    }
    // Across intersections the line is a chord between road ends; edges()
    // interpolates the curbs along it.
    c.m_line.finalize();
    for (std::size_t k = 0; k < c.m_legs.size(); ++k) {
        c.m_legs[k].start = c.m_line.distances[std::min(legPoints[k].first, pts.size() - 1)];
        c.m_legs[k].end = c.m_line.distances[std::min(legPoints[k].second, pts.size() - 1)];
    }

    if (startOnFirst) {
        const CourseLeg& leg = c.m_legs.front();
        c.m_startS = c.locate(start, 0.5f * (leg.start + leg.end), 0.5f * (leg.end - leg.start) + 1.0f);
    } else {
        c.m_startS = c.m_loop ? c.locate(start) : 0.0f;
    }
    c.m_finishS = c.m_loop ? c.m_startS : (finish ? c.locate(*finish, c.length(), 60.0f) : c.length());
    if (finish && !c.m_loop) {
        c.m_finishPoint = *finish;
        c.m_hasFinishPoint = true;
    }
    c.findTurns();
    return c;
}

std::optional<Course> Course::fromOpponentPath(const RoadNetwork& net, std::span<const city::OpponentPoint> rows,
                                               bool circuit, std::string* error,
                                               std::span<const int> waypoints) {
    if (rows.size() < 2) {
        if (error)
            *error = "driving line has fewer than two points";
        return std::nullopt;
    }
    const Vec3 start = rows.front().position;
    // The last row is the finish line when it is not on an intersection
    // (circuits: on the start road, ahead of the staggered grid; races:
    // e.g. london race0 ends 12 m past an intersection and 25+ m from the
    // centre is treated as "not on it"), else the last waypoint.
    std::vector<int> ids;
    std::optional<Vec3> finish;
    for (std::size_t i = 1; i < rows.size(); ++i) {
        float d = 0.0f;
        int id = nearestIntersection(net, rows[i].position, &d);
        if (i + 1 == rows.size()) {
            finish = rows[i].position;
            if (d > 20.0f)
                break;
        } else if (i - 1 < waypoints.size()) {
            id = waypoints[i - 1];
        }
        ids.push_back(id);
    }
    auto course = build(net, ids, start, circuit ? std::nullopt : finish, circuit, error);
    if (course && circuit && finish) {
        // Where the race ends on the loop: the finish row's arc length.
        float lateral = 0.0f, dist = 0.0f;
        const float s = course->locate(*finish, course->m_startS, 60.0f, &lateral, &dist);
        if (dist < 25.0f) {
            course->m_finishS = s;
            // aiRouteRacer::Init: the last row is the destination.
            course->m_finishPoint = *finish;
            course->m_hasFinishPoint = true;
        }
    }
    return course;
}

std::optional<Course> Course::alongRoad(const RoadNetwork& net, const Vec3& start, const Vec3& finish) {
    const city::AiMap* map = net.source();
    if (!map)
        return std::nullopt;
    const RoadSpot a = locateOnRoads(net, start);
    const RoadSpot b = locateOnRoads(net, finish);
    if (a.path < 0 || a.path != b.path || a.intersection >= 0 || b.intersection >= 0 || a.distance > 30.0f ||
        b.distance > 30.0f)
        return std::nullopt;
    const city::AiPath& src = map->paths[static_cast<std::size_t>(a.path)];
    const bool forward = b.s >= a.s; // along increasing section order
    std::vector<float> cum(src.center.size(), 0.0f);
    for (std::size_t k = 1; k < cum.size(); ++k)
        cum[k] = cum[k - 1] + xzDist(src.center[k], src.center[k - 1]);
    Course c;
    auto point = [&](std::size_t sec) {
        float l, r, el, er;
        pathCurbs(src, sec, l, r);
        pathOuterEdges(src, sec, el, er);
        CoursePoint cp;
        cp.left = forward ? l : r;
        cp.right = forward ? r : l;
        cp.leftEdge = forward ? el : er;
        cp.rightEdge = forward ? er : el;
        cp.path = a.path;
        cp.lanes = lanesOf(src);
        cp.flags = src.flags;
        pathOnRoadLimits(src, cp.onRoad, cp.onSidewalk);
        if (sec < src.xAxis.size())
            cp.across = flatUnit(src.xAxis[sec]) * (forward ? -1.0f : 1.0f);
        return cp;
    };
    auto add = [&](const Vec3& p, const CoursePoint& cp) {
        if (!c.m_line.points.empty() && xzDist(c.m_line.points.back(), p) < 1.0f)
            return;
        c.m_line.points.push_back(p);
        c.m_points.push_back(cp);
    };
    const std::size_t n = src.center.size();
    auto nearest = [&](float s) {
        std::size_t best = 0;
        for (std::size_t k = 1; k < n; ++k)
            if (std::abs(cum[k] - s) < std::abs(cum[best] - s))
                best = k;
        return best;
    };
    add(start, point(nearest(a.s)));
    for (std::size_t j = 0; j < n; ++j) {
        const std::size_t sec = forward ? j : n - 1 - j;
        const bool between = forward ? (cum[sec] > a.s + 1.0f && cum[sec] < b.s - 1.0f)
                                     : (cum[sec] < a.s - 1.0f && cum[sec] > b.s + 1.0f);
        if (between)
            add(src.center[sec], point(sec));
    }
    add(finish, point(nearest(b.s)));
    if (c.m_line.points.size() < 2) {
        c.m_line.points.push_back(start + Vec3{0, 0, -1});
        c.m_points.push_back(c.m_points.front());
    }
    c.m_line.finalize();
    c.m_intersections = {};
    c.m_startS = 0.0f;
    c.m_finishS = c.m_line.length;
    c.m_finishPoint = finish;
    c.m_hasFinishPoint = true;
    c.findTurns();
    return c;
}

float Course::raceDistance(int laps) const {
    float d = m_finishS - m_startS;
    if (!m_loop)
        return d;
    const float len = m_line.length;
    if (d > 0.5f * len)
        d -= len;
    else if (d < -0.5f * len)
        d += len;
    return static_cast<float>(std::max(laps, 1)) * len + d;
}

float Course::wrap(float s) const {
    const float len = m_line.length;
    if (!m_loop || len <= 0.0f)
        return clampf(s, 0.0f, len);
    s = std::fmod(s, len);
    return s < 0.0f ? s + len : s;
}

std::size_t Course::segmentAt(float s) const {
    const auto& d = m_line.distances;
    if (d.size() < 2)
        return 0;
    auto it = std::upper_bound(d.begin(), d.end(), s);
    std::size_t i = it == d.begin() ? 0 : static_cast<std::size_t>(std::distance(d.begin(), it)) - 1;
    return std::min(i, d.size() - 2);
}

Vec3 Course::pointAt(float s, Vec3* direction) const {
    return m_line.pointAt(wrap(s), direction);
}

void Course::edges(float s, float& left, float& right, float* leftEdge, float* rightEdge) const {
    s = wrap(s);
    const std::size_t i = segmentAt(s);
    if (m_points.size() < 2) {
        left = right = m_points.empty() ? 6.0f : m_points[0].left;
        if (leftEdge)
            *leftEdge = m_points.empty() ? 9.0f : m_points[0].leftEdge;
        if (rightEdge)
            *rightEdge = m_points.empty() ? 9.0f : m_points[0].rightEdge;
        return;
    }
    const float seg = m_line.distances[i + 1] - m_line.distances[i];
    const float t = seg > 1e-6f ? clampf((s - m_line.distances[i]) / seg, 0.0f, 1.0f) : 0.0f;
    left = lerp(m_points[i].left, m_points[i + 1].left, t);
    right = lerp(m_points[i].right, m_points[i + 1].right, t);
    if (leftEdge)
        *leftEdge = lerp(m_points[i].leftEdge, m_points[i + 1].leftEdge, t);
    if (rightEdge)
        *rightEdge = lerp(m_points[i].rightEdge, m_points[i + 1].rightEdge, t);
}

void Course::onRoadLimits(float s, float& road, float& sidewalk) const {
    s = wrap(s);
    if (m_points.size() < 2) {
        road = m_points.empty() ? 5.0f : m_points[0].onRoad;
        sidewalk = m_points.empty() ? 9.0f : m_points[0].onSidewalk;
        return;
    }
    const std::size_t i = segmentAt(s);
    const float seg = m_line.distances[i + 1] - m_line.distances[i];
    const float t = seg > 1e-6f ? clampf((s - m_line.distances[i]) / seg, 0.0f, 1.0f) : 0.0f;
    road = lerp(m_points[i].onRoad, m_points[i + 1].onRoad, t);
    sidewalk = lerp(m_points[i].onSidewalk, m_points[i + 1].onSidewalk, t);
}

std::size_t Course::vertexCount() const {
    const std::size_t n = m_line.points.size();
    return m_loop && n > 1 ? n - 1 : n;
}

Vec3 Course::vertexRight(std::size_t i) const {
    const auto& pts = m_line.points;
    const std::size_t n = vertexCount();
    if (pts.size() < 2)
        return {1.0f, 0.0f, 0.0f};
    // Road sections: the section's own frame (aiPath's x axis).
    if (i < m_points.size() && m_points[i].path >= 0 && m_points[i].across.mag2() > 0.5f)
        return m_points[i].across;
    // The directions of the segments into and out of the vertex.
    Vec3 in, out;
    if (i + 1 < pts.size())
        out = flatUnit(pts[i + 1] - pts[i]);
    if (i > 0)
        in = flatUnit(pts[i] - pts[i - 1]);
    else if (m_loop)
        in = flatUnit(pts[i] - pts[n - 1]);
    if (i + 1 >= pts.size())
        out = in;
    if (i == 0 && !m_loop)
        in = out;
    Vec3 d = flatUnit(in + out);
    if (d.mag2() < 0.5f)
        d = out;
    return rightOf(d);
}

std::size_t Course::vertexAfter(float s) const {
    const auto& d = m_line.distances;
    const std::size_t n = vertexCount();
    if (n == 0)
        return 0;
    s = wrap(s);
    const auto it = std::upper_bound(d.begin(), d.begin() + static_cast<std::ptrdiff_t>(n), s);
    if (it == d.begin() + static_cast<std::ptrdiff_t>(n))
        return m_loop ? 0 : n - 1;
    return static_cast<std::size_t>(std::distance(d.begin(), it));
}

std::size_t Course::nextVertex(std::size_t i) const {
    const std::size_t n = vertexCount();
    if (i + 1 < n)
        return i + 1;
    return m_loop ? 0 : i;
}

void Course::edgesAhead(float s, float distance, float& left, float& right) const {
    edges(s, left, right);
    const int steps = std::max(1, static_cast<int>(distance / 4.0f));
    for (int i = 1; i <= steps; ++i) {
        float l, r;
        edges(s + distance * static_cast<float>(i) / static_cast<float>(steps), l, r);
        left = std::min(left, l);
        right = std::min(right, r);
    }
}

int Course::lanesAt(float s) const {
    if (m_points.empty())
        return 1;
    const std::size_t i = segmentAt(wrap(s));
    return std::max(m_points[i].lanes, m_points[std::min(i + 1, m_points.size() - 1)].lanes);
}

float Course::locate(const Vec3& p, float hint, float window, float* lateral, float* distance) const {
    if (!std::isfinite(p.x) || !std::isfinite(p.z) || !std::isfinite(hint)) {
        // A car the physics lost: nowhere on the course.
        if (lateral)
            *lateral = 0.0f;
        if (distance)
            *distance = std::numeric_limits<float>::max();
        return std::isfinite(hint) ? wrap(hint) : 0.0f;
    }
    const auto& pts = m_line.points;
    const auto& d = m_line.distances;
    const float len = m_line.length;
    float best = std::numeric_limits<float>::max(), bestS = wrap(hint), bestLat = 0.0f;
    bool found = false;
    hint = wrap(hint);
    const int copies = m_loop ? 1 : 0;
    for (std::size_t i = 1; i < pts.size(); ++i) {
        const float s0 = d[i - 1], s1 = d[i];
        if (s1 - s0 < 1e-6f)
            continue;
        for (int k = -copies; k <= copies; ++k) {
            const float lo = std::max(s0, hint - window + k * len);
            const float hi = std::min(s1, hint + window + k * len);
            if (lo > hi)
                continue;
            const Vec2 a{pts[i - 1].x, pts[i - 1].z}, b{pts[i].x, pts[i].z}, q{p.x, p.z};
            const Vec2 ab = b - a;
            float t = clampf((q - a).dot(ab) / ab.mag2(), 0.0f, 1.0f);
            t = clampf(t, (lo - s0) / (s1 - s0), (hi - s0) / (s1 - s0));
            const Vec2 c = a + ab * t;
            const float dist = (c - q).mag();
            if (dist < best) {
                best = dist;
                bestS = s0 + t * (s1 - s0);
                const Vec3 dir = flatUnit(pts[i] - pts[i - 1]);
                bestLat = (Vec3{q.x - c.x, 0.0f, q.y - c.y}).dot(rightOf(dir));
                found = true;
            }
        }
    }
    if (!found)
        return locate(p, lateral, distance);
    if (lateral)
        *lateral = bestLat;
    if (distance)
        *distance = best;
    return bestS;
}

float Course::locate(const Vec3& p, float* lateral, float* distance) const {
    return locate(p, 0.5f * m_line.length, m_line.length + 1.0f, lateral, distance);
}

void Course::findTurns() {
    m_turns.clear();
    const auto& pts = m_line.points;
    const std::size_t n = pts.size();
    if (n < 3)
        return;
    // Vertex deflections (+ right). Loops: the last point repeats the first.
    struct Bend {
        float s, angle;
        std::size_t vertex;
    };
    std::vector<Bend> bends;
    for (std::size_t i = m_loop ? 0 : 1; i + 1 < n; ++i) {
        const std::size_t prev = i == 0 ? n - 2 : i - 1;
        const Vec3 a = flatUnit(pts[i] - pts[prev]);
        const Vec3 b = flatUnit(pts[i + 1] - pts[i]);
        const float angle = std::atan2(a.x * b.z - a.z * b.x, a.x * b.x + a.z * b.z);
        if (std::abs(angle) > 0.03f)
            bends.push_back({m_line.distances[i], angle, i});
    }
    std::vector<Bend> cluster;
    auto flush = [&] {
        if (cluster.empty())
            return;
        float sum = 0.0f, weighted = 0.0f;
        for (const Bend& b : cluster) {
            sum += b.angle;
            weighted += b.s * std::abs(b.angle);
        }
        float absSum = 0.0f;
        for (const Bend& b : cluster)
            absSum += std::abs(b.angle);
        if (std::abs(sum) >= 0.1f) {
            CourseTurn t;
            t.deflection = sum;
            t.s = weighted / absSum;
            // The narrower of the roads into and out of the turn.
            t.halfWidth = std::numeric_limits<float>::max();
            for (float at : {cluster.front().s - 10.0f, t.s, cluster.back().s + 10.0f}) {
                float l, r;
                edges(at, l, r);
                t.halfWidth = std::min(t.halfWidth, 0.5f * (l + r));
            }
            // The road the turn leads into (the first road vertex after it).
            for (std::size_t k = cluster.back().vertex + 1; k < m_points.size(); ++k) {
                if (m_points[k].path >= 0) {
                    t.intoAlley = (m_points[k].flags & 0x2) != 0;
                    break;
                }
            }
            m_turns.push_back(t);
        }
        cluster.clear();
    };
    for (const Bend& b : bends) {
        if (!cluster.empty() &&
            (signf(b.angle) != signf(cluster.back().angle) || b.s - cluster.back().s > 25.0f ||
             b.s - cluster.front().s > 40.0f))
            flush();
        cluster.push_back(b);
    }
    flush();
    // Loops: a turn split by the closing point is one turn.
    if (m_loop && m_turns.size() > 1) {
        CourseTurn& first = m_turns.front();
        const CourseTurn& last = m_turns.back();
        const float len = m_line.length;
        if (signf(first.deflection) == signf(last.deflection) && first.s + len - last.s <= 25.0f) {
            const float w0 = std::abs(first.deflection), w1 = std::abs(last.deflection);
            float s = (first.s * w0 + (last.s - len) * w1) / (w0 + w1);
            first.s = s < 0.0f ? s + len : s;
            first.deflection += last.deflection;
            first.halfWidth = std::min(first.halfWidth, last.halfWidth);
            m_turns.pop_back();
            std::sort(m_turns.begin(), m_turns.end(),
                      [](const CourseTurn& a, const CourseTurn& b) { return a.s < b.s; });
        }
    }
}

// --- aiMap components and routes ---------------------------------------------------

namespace {

// The point of a road's centre line nearest `p` (XZ): arc length from
// center.front(), lateral offset (+ towards the road's x axis) and the
// projection's place on its segment (0..1 of the whole road, -1/+1 beyond an
// end).
struct CentreSpot {
    float s = 0.0f;
    float lateral = 0.0f;
    float distance = std::numeric_limits<float>::max();
    float dy = 0.0f;
    bool inside = false; // the projection falls within the road's length
};

CentreSpot centreSpot(const city::AiPath& path, const Vec3& p) {
    CentreSpot out;
    float s0 = 0.0f;
    for (std::size_t k = 1; k < path.center.size(); ++k) {
        const Vec3& a = path.center[k - 1];
        const Vec3& c = path.center[k];
        const Vec2 ab{c.x - a.x, c.z - a.z};
        const float len2 = ab.mag2();
        const float raw = len2 > 1e-6f ? Vec2{p.x - a.x, p.z - a.z}.dot(ab) / len2 : 0.0f;
        const float t = clampf(raw, 0.0f, 1.0f);
        const Vec3 q = lerp(a, c, t);
        const float d = xzDist(q, p);
        if (d < out.distance) {
            Vec3 x{1, 0, 0};
            if (k - 1 < path.xAxis.size())
                x = flatUnit(lerp(path.xAxis[k - 1], path.xAxis[std::min(k, path.xAxis.size() - 1)], t));
            out.distance = d;
            out.s = s0 + t * std::sqrt(len2);
            out.lateral = (p - q).dot(x);
            out.dy = p.y - q.y;
            out.inside = (k > 1 || raw >= 0.0f) && (k + 1 < path.center.size() || raw <= 1.0f);
        }
        s0 += std::sqrt(len2);
    }
    return out;
}

} // namespace

int posOnRoad(const RoadNetwork& net, int path, const Vec3& p, float margin) {
    const city::AiMap* map = net.source();
    if (!map || path < 0 || static_cast<std::size_t>(path) >= map->paths.size())
        return 3;
    const city::AiPath& src = map->paths[static_cast<std::size_t>(path)];
    float road = 0.0f, sidewalk = 0.0f;
    pathOnRoadLimits(src, road, sidewalk);
    const float a = std::abs(centreSpot(src, p).lateral);
    if (a < road - margin)
        return 1;
    if (a < sidewalk - margin)
        return 2;
    return 3;
}

std::vector<MapComponentRef> componentsAt(const RoadNetwork& net, const Vec3& p) {
    std::vector<MapComponentRef> out;
    const city::AiMap* map = net.source();
    if (!map)
        return out;
    // An intersection takes the point first.
    const RoadSpot spot = locateOnRoads(net, p);
    if (spot.intersection >= 0) {
        out.push_back({spot.intersection, 3});
        return out;
    }
    // Then every road (up to five) the point lies across, within twice the
    // road's half width of its centre line (PositionToAIMapComp, which asks
    // the roads of the point's room).
    for (std::size_t pi = 0; pi < map->paths.size() && out.size() < 5; ++pi) {
        const city::AiPath& path = map->paths[pi];
        const Aabb& b = net.paths()[pi].bounds;
        const float margin = 2.0f * path.halfWidth + 1.0f;
        if (p.x < b.min.x - margin || p.x > b.max.x + margin || p.z < b.min.z - margin ||
            p.z > b.max.z + margin)
            continue;
        const CentreSpot c = centreSpot(path, p);
        if (c.inside && std::abs(c.lateral) < path.halfWidth + path.halfWidth && std::abs(c.dy) < 6.0f)
            out.push_back({static_cast<int>(pi), 1});
    }
    return out;
}

int mapComponent(const RoadNetwork& net, const Vec3& p, int previous, int& type) {
    // aiMap::MapComponent: an intersection, else a road the point is on or
    // beside (IsPosOnRoad below 3, no margin); none leaves the id as it was.
    const RoadSpot spot = locateOnRoads(net, p);
    if (spot.intersection >= 0) {
        type = 3;
        return spot.intersection;
    }
    if (spot.path >= 0 && posOnRoad(net, spot.path, p, 0.0f) < 3) {
        type = 1;
        return spot.path;
    }
    type = 0;
    return previous;
}

std::vector<int> calcRoute(const RoadNetwork& net, const Vec3& from, const Vec3& to) {
    // aiMap::CalcRoute (as the police call it): a shortest route over the
    // intersections, every road costing its centre line length.
    const city::AiMap* map = net.source();
    const auto& nodes = net.intersections();
    const auto& paths = net.paths();
    if (!map || nodes.empty())
        return {};
    const std::vector<MapComponentRef> starts = componentsAt(net, from);
    const std::vector<MapComponentRef> goals = componentsAt(net, to);
    // Both in the same component: no waypoints.
    for (const MapComponentRef& a : starts)
        for (const MapComponentRef& b : goals)
            if (a.id == b.id && a.type == b.type)
                return {};
    constexpr int kNone = 9999;
    constexpr float kFar = 9999999.0f;
    const std::size_t n = nodes.size();
    std::vector<float> cost(n, kFar);
    std::vector<int> pred(n, kNone);
    std::vector<int> open; // most recently added first (aiMap::AddRoutingNode)
    auto add = [&](int id) {
        if (std::find(open.begin(), open.end(), id) == open.end())
            open.insert(open.begin(), id);
    };
    auto centreLength = [&](int path) { return paths[static_cast<std::size_t>(path)].centreLength; };
    auto seed = [&](int id, float c, int previous) {
        if (id < 0)
            return;
        cost[static_cast<std::size_t>(id)] = c;
        pred[static_cast<std::size_t>(id)] = previous;
        add(id);
    };
    // Off the roads (type 0): the ends of the nearest road, by straight-line
    // distance (MM2: the intersections, else the road ends, of the rooms
    // round the point).
    auto nearestRoadEnds = [&](const Vec3& p) {
        std::vector<int> ends;
        const RoadSpot spot = locateOnRoads(net, p);
        if (spot.path >= 0)
            for (int e : paths[static_cast<std::size_t>(spot.path)].intersection)
                if (e >= 0)
                    ends.push_back(e);
        return ends;
    };
    int marker = kNone; // the start's predecessor mark
    int count = 0;
    const MapComponentRef start = starts.empty() ? MapComponentRef{-1, 0} : starts.front();
    if (start.type == 0) {
        for (int e : nearestRoadEnds(from))
            seed(e, xzDist(from, nodes[static_cast<std::size_t>(e)].centre), kNone);
    } else if (start.type == 3) {
        // From an intersection: its neighbours at their road lengths.
        marker = start.id;
        count = 1;
        for (int pid : nodes[static_cast<std::size_t>(start.id)].paths) {
            if (pid < 0 || static_cast<std::size_t>(pid) >= paths.size())
                continue;
            const PathInfo& info = paths[static_cast<std::size_t>(pid)];
            const int other = info.intersection[1] == start.id ? info.intersection[0] : info.intersection[1];
            seed(other, centreLength(pid), start.id);
        }
        cost[static_cast<std::size_t>(start.id)] = 0.0f;
    } else {
        // On a road: each end at the distance along the centre line.
        count = 1;
        const PathInfo& info = paths[static_cast<std::size_t>(start.id)];
        const float along = centreSpot(map->paths[static_cast<std::size_t>(start.id)], from).s;
        seed(info.intersection[0], centreLength(start.id) - along, kNone);
        seed(info.intersection[1], along, kNone);
    }
    // The goals: the goal's intersection, or both ends of its road.
    std::vector<int> targets;
    for (const MapComponentRef& g : goals) {
        if (g.type == 3) {
            targets.push_back(g.id);
        } else {
            const PathInfo& info = paths[static_cast<std::size_t>(g.id)];
            targets.push_back(info.intersection[1]);
            targets.push_back(info.intersection[0]);
        }
    }
    if (goals.empty())
        targets = nearestRoadEnds(to);
    if (std::find(targets.begin(), targets.end(), marker) != targets.end())
        return {};
    int found = -1;
    while (!open.empty()) {
        int best = -1;
        float least = kFar;
        for (int id : open) {
            if (cost[static_cast<std::size_t>(id)] < least) {
                least = cost[static_cast<std::size_t>(id)];
                best = id;
            }
        }
        if (best < 0)
            break;
        if (std::find(targets.begin(), targets.end(), best) != targets.end()) {
            found = best;
            break;
        }
        std::erase(open, best);
        for (int pid : nodes[static_cast<std::size_t>(best)].paths) {
            if (pid < 0 || static_cast<std::size_t>(pid) >= paths.size())
                continue;
            const PathInfo& info = paths[static_cast<std::size_t>(pid)];
            const int other = info.intersection[1] == best ? info.intersection[0] : info.intersection[1];
            if (other < 0)
                continue;
            const float c = centreLength(pid) + cost[static_cast<std::size_t>(best)];
            if (c < cost[static_cast<std::size_t>(other)]) {
                cost[static_cast<std::size_t>(other)] = c;
                add(other);
                pred[static_cast<std::size_t>(other)] = best;
            }
        }
    }
    if (found < 0)
        return {};
    std::vector<int> route;
    for (int id = found; id != marker && id != kNone; id = pred[static_cast<std::size_t>(id)])
        route.push_back(id);
    if (count == 1) {
        if (start.type == 3) {
            route.push_back(start.id);
        } else {
            // From a road: its end that the route does not leave by comes
            // first, so that the first leg is the car's own road.
            const PathInfo& info = paths[static_cast<std::size_t>(start.id)];
            const int first = route.back();
            route.push_back(first == info.intersection[0] ? info.intersection[1] : info.intersection[0]);
        }
    }
    std::reverse(route.begin(), route.end());
    return route;
}

} // namespace mm2::ai
