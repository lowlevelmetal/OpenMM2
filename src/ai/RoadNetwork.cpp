#include "ai/RoadNetwork.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mm2::ai {

void Polyline::finalize() {
    distances.assign(points.size(), 0.0f);
    for (std::size_t i = 1; i < points.size(); ++i)
        distances[i] = distances[i - 1] + points[i].dist(points[i - 1]);
    length = distances.empty() ? 0.0f : distances.back();
}

Vec3 Polyline::pointAt(float s, Vec3* direction) const {
    if (points.empty())
        return {};
    if (points.size() == 1) {
        if (direction)
            *direction = Vec3::zAxis() * -1.0f;
        return points[0];
    }
    s = clampf(s, 0.0f, length);
    // First segment whose end lies at or beyond s.
    auto it = std::lower_bound(distances.begin() + 1, distances.end(), s);
    std::size_t i = static_cast<std::size_t>(std::distance(distances.begin(), it));
    if (i >= points.size())
        i = points.size() - 1;
    const Vec3& a = points[i - 1];
    const Vec3& b = points[i];
    const float seg = distances[i] - distances[i - 1];
    const float t = seg > 1e-6f ? (s - distances[i - 1]) / seg : 0.0f;
    if (direction) {
        const Vec3 d = b - a;
        *direction = d.mag2() > 1e-12f ? d.normalized() : Vec3{0, 0, -1};
    }
    return lerp(a, b, t);
}

float Polyline::project(const Vec3& p, float* distance) const {
    float best = std::numeric_limits<float>::max(), bestS = 0.0f;
    for (std::size_t i = 1; i < points.size(); ++i) {
        const Vec2 a{points[i - 1].x, points[i - 1].z}, b{points[i].x, points[i].z}, q{p.x, p.z};
        const Vec2 ab = b - a;
        const float len2 = ab.mag2();
        const float t = len2 > 1e-9f ? clampf((q - a).dot(ab) / len2, 0.0f, 1.0f) : 0.0f;
        const float d = (a + ab * t - q).mag();
        if (d < best) {
            best = d;
            bestS = distances[i - 1] + t * (distances[i] - distances[i - 1]);
        }
    }
    if (distance)
        *distance = best;
    return bestS;
}

namespace {

// Mirrors a point across the path's centre line at section `k` (drive on the
// left): the lateral offset along the section's x axis changes sign.
Vec3 mirror(const city::AiPath& path, std::size_t k, const Vec3& p) {
    const Vec3 d = p - path.center[k];
    const float lateral = d.dot(path.xAxis[k]);
    return p - path.xAxis[k] * (2.0f * lateral);
}

} // namespace

RoadNetwork RoadNetwork::build(const city::AiMap& map, const NetworkOptions& options) {
    RoadNetwork net;
    net.m_source = &map;
    net.m_driveOnLeft = options.driveOnLeft;

    net.m_intersections.resize(map.intersections.size());
    for (std::size_t i = 0; i < map.intersections.size(); ++i) {
        auto& n = net.m_intersections[i];
        n.id = static_cast<int>(i);
        n.room = map.intersections[i].room;
        n.centre = map.intersections[i].center;
        for (auto p : map.intersections[i].paths)
            n.paths.push_back(static_cast<int>(p));
    }
    auto validIntersection = [&](std::uint32_t id) {
        return id < net.m_intersections.size() ? static_cast<int>(id) : -1;
    };

    net.m_paths.resize(map.paths.size());
    for (std::size_t p = 0; p < map.paths.size(); ++p) {
        const city::AiPath& src = map.paths[p];
        PathInfo& info = net.m_paths[p];
        info.id = static_cast<int>(p);
        info.flags = src.flags;
        info.halfWidth = src.halfWidth;
        info.speedLimit = options.defaultSpeedLimit;
        for (const auto& e : options.exceptions) {
            if (e.road == static_cast<int>(src.id)) {
                info.hasException = true;
                info.density = e.density;
                if (e.speedLimit > 0.0f)
                    info.speedLimit = e.speedLimit;
            }
        }
        info.intersection[0] = validIntersection(src.ends[0].intersection);
        info.intersection[1] = validIntersection(src.ends[1].intersection);
        if (!src.center.empty()) {
            info.centreStart = src.center.front();
            info.centreEnd = src.center.back();
            for (const auto& c : src.center)
                info.bounds.expand(c);
        }
        const std::size_t sections = src.center.size();

        for (int sideIdx = 0; sideIdx < 2; ++sideIdx) {
            const city::AiRoadSide& side = sideIdx == 0 ? src.left : src.right;
            // Right lanes run with increasing section index and arrive at
            // ends[0]; left lanes run the other way and arrive at ends[1].
            const int arriveEnd = sideIdx == 1 ? 0 : 1;
            const city::AiPathEnd& arrival = src.ends[static_cast<std::size_t>(arriveEnd)];
            for (int l = 0; l < side.numLanes; ++l) {
                if (static_cast<std::size_t>(l) >= side.polylines.size())
                    break;
                const auto& poly = side.polylines[static_cast<std::size_t>(l)];
                Lane lane;
                lane.id = static_cast<int>(net.m_lanes.size());
                lane.path = static_cast<int>(p);
                lane.side = sideIdx;
                lane.index = l;
                // Stored in travel order; when mirroring, point i of a left
                // lane lies at section (sections - 1 - i).
                for (std::size_t i = 0; i < poly.size(); ++i) {
                    Vec3 pt = poly[i];
                    if (options.driveOnLeft && sections == poly.size()) {
                        const std::size_t k = sideIdx == 1 ? i : sections - 1 - i;
                        pt = mirror(src, k, pt);
                    }
                    lane.line.points.push_back(pt);
                }
                lane.line.finalize();
                lane.toIntersection = info.intersection[arriveEnd];
                lane.fromIntersection = info.intersection[1 - arriveEnd];
                lane.rule = static_cast<EntryRule>(arrival.vehicleRule == 1   ? 1
                                                   : arrival.vehicleRule == 0 ? 0
                                                                              : 3);
                lane.speedLimit = info.speedLimit;
                info.lanes.push_back(lane.id);
                net.m_lanes.push_back(std::move(lane));
            }

            // Sidewalk, curb and outer edge follow the lanes, trams and trains.
            const std::size_t base = static_cast<std::size_t>(side.numLanes + side.numTrams + side.numTrains);
            if (side.numSidewalks > 0 && base + 2 < side.polylines.size()) {
                Sidewalk walk;
                walk.id = static_cast<int>(net.m_sidewalks.size());
                walk.path = static_cast<int>(p);
                walk.side = sideIdx;
                walk.centre.points = side.polylines[base];
                walk.curb.points = side.polylines[base + 1];
                walk.edge.points = side.polylines[base + 2];
                // Pedestrians stand on the sidewalk surface: the outer edge
                // height (the centre polyline sits halfway up the curb).
                for (std::size_t i = 0; i < walk.centre.points.size() && i < walk.edge.points.size(); ++i)
                    walk.centre.points[i].y = walk.edge.points[i].y;
                walk.centre.finalize();
                walk.curb.finalize();
                walk.edge.finalize();
                if (!walk.curb.points.empty() && !walk.edge.points.empty()) {
                    const Vec3 a = walk.curb.points.front(), b = walk.edge.points.front();
                    walk.halfWidth = 0.5f * Vec2{a.x - b.x, a.z - b.z}.mag();
                }
                // Points run with increasing section index: front at ends[1].
                walk.startIntersection = info.intersection[1];
                walk.endIntersection = info.intersection[0];
                if (walk.centre.length > 1.0f && walk.halfWidth > 0.25f) {
                    info.sidewalks.push_back(walk.id);
                    net.m_sidewalks.push_back(std::move(walk));
                }
            }
        }
    }

    // Connectivity and traffic light sites.
    for (const Lane& lane : net.m_lanes) {
        if (lane.toIntersection >= 0)
            net.m_intersections[static_cast<std::size_t>(lane.toIntersection)].incoming.push_back(lane.id);
        if (lane.fromIntersection >= 0)
            net.m_intersections[static_cast<std::size_t>(lane.fromIntersection)].outgoing.push_back(lane.id);
    }
    for (const Sidewalk& w : net.m_sidewalks) {
        for (int n : {w.startIntersection, w.endIntersection})
            if (n >= 0)
                net.m_intersections[static_cast<std::size_t>(n)].sidewalks.push_back(w.id);
    }
    for (auto& node : net.m_intersections) {
        // One light per approaching path end with a traffic light (MM1's
        // aiTrafficLightSet has one per "sink" path of IntersectionType 1),
        // in the intersection's path order.
        for (int pathId : node.paths) {
            if (pathId < 0 || static_cast<std::size_t>(pathId) >= map.paths.size())
                continue;
            const auto& src = map.paths[static_cast<std::size_t>(pathId)];
            for (int end = 0; end < 2; ++end) {
                if (net.m_paths[static_cast<std::size_t>(pathId)].intersection[end] != node.id)
                    continue;
                const auto& e = src.ends[static_cast<std::size_t>(end)];
                if (e.vehicleRule != 1)
                    continue;
                TrafficLightSite site;
                site.intersection = node.id;
                site.position = e.trafficLightPos;
                const Vec3 d = e.trafficLightAxis - e.trafficLightPos;
                site.facing = d.mag2() > 1e-8f ? d.normalized() : Vec3{0, 0, 1};
                const int slot = static_cast<int>(net.m_lights.size());
                // Lanes arriving at this end are controlled by this light.
                for (int laneId : net.m_paths[static_cast<std::size_t>(pathId)].lanes) {
                    Lane& lane = net.m_lanes[static_cast<std::size_t>(laneId)];
                    const int arriveEnd = lane.side == 1 ? 0 : 1;
                    if (arriveEnd == end && lane.toIntersection == node.id) {
                        lane.lightSlot = slot;
                        if (site.lane < 0)
                            site.lane = laneId;
                    }
                }
                node.lights.push_back(slot);
                net.m_lights.push_back(site);
            }
        }
    }
    return net;
}

std::vector<int> RoadNetwork::exits(int intersection, int fromPath) const {
    std::vector<int> out;
    if (intersection < 0 || static_cast<std::size_t>(intersection) >= m_intersections.size())
        return out;
    for (int laneId : m_intersections[static_cast<std::size_t>(intersection)].outgoing)
        if (m_lanes[static_cast<std::size_t>(laneId)].path != fromPath)
            out.push_back(laneId);
    return out;
}

std::vector<int> RoadNetwork::sidewalksAt(int intersection, int fromSidewalk) const {
    std::vector<int> out;
    if (intersection < 0 || static_cast<std::size_t>(intersection) >= m_intersections.size())
        return out;
    for (int w : m_intersections[static_cast<std::size_t>(intersection)].sidewalks)
        if (w != fromSidewalk)
            out.push_back(w);
    return out;
}

} // namespace mm2::ai
