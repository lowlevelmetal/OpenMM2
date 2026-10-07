#pragma once

// Runtime road network for ambient traffic and pedestrians, built from the
// city's AI map (city/<map>.bai, see docs/formats/bai.md and docs/ai.md).
//
// Each .bai path is a road between two intersections with lanes on both
// sides. The files use one convention for both cities (verified on every
// path): right-side lanes are stored in increasing section order and left-side
// lanes in decreasing order, i.e. vehicles keep to the right of the centre
// line. London's AI map adds "[Ambients Drive On The Left] 1"; for it the
// lane polylines are mirrored across each section's centre line, which puts
// traffic on the left while keeping one-way streets (whose lanes are laid out
// symmetrically about the centre) unchanged. The mirroring is inferred.

#include "city/AiMap.h"
#include "city/Race.h"
#include "core/Math.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace mm2::ai {

// Arrival rule at a path end (the path end's "vehicleRule"). The values match
// MM1's aiPath::IntersectionType: 0 stop sign (four-way stop), 1 traffic
// light, 3 no control.
enum class EntryRule : std::uint8_t { StopSign = 0, TrafficLight = 1, Uncontrolled = 3 };

// A polyline with cumulative arc lengths.
struct Polyline {
    std::vector<Vec3> points;
    std::vector<float> distances; // distances[0] = 0
    float length = 0.0f;

    void finalize();
    // Point and unit direction at arc length `s` (clamped to the ends).
    Vec3 pointAt(float s, Vec3* direction = nullptr) const;
    // Arc length of the point on the polyline closest to `p` in XZ, and the
    // XZ distance to it.
    float project(const Vec3& p, float* distance = nullptr) const;
};

// A lane in its direction of travel.
struct Lane {
    int id = 0;
    int path = 0;  // index into RoadNetwork::paths()
    int side = 0;  // 0 left, 1 right (as stored in the .bai)
    int index = 0; // lane number within the side, 0 = nearest the centre line
    Polyline line;
    int fromIntersection = -1;
    int toIntersection = -1;
    EntryRule rule = EntryRule::Uncontrolled; // at toIntersection
    int lightSlot = -1;                       // traffic light slot at toIntersection, -1 = none
    float speedLimit = 15.0f;                 // m/s
};

// Pedestrian walkway along one side of a path.
struct Sidewalk {
    int id = 0;
    int path = 0;
    int side = 0;
    Polyline centre; // walking line (the side's sidewalk polyline), at sidewalk height
    Polyline curb;   // road edge
    Polyline edge;   // building side
    float halfWidth = 1.5f;
    int startIntersection = -1; // at centre.points.front()
    int endIntersection = -1;   // at centre.points.back()
};

struct TrafficLightSite {
    int intersection = 0;
    int lane = -1; // first lane controlled by this light
    Vec3 position; // pole position (path end trafficLightPos)
    Vec3 facing;   // unit direction from the pole towards trafficLightAxis
};

struct Intersection {
    int id = 0;
    int room = 0;
    Vec3 centre;
    std::vector<int> paths;
    std::vector<int> incoming; // lanes arriving here
    std::vector<int> outgoing; // lanes leaving here
    std::vector<int> lights;   // indices into RoadNetwork::lights(), in path list order
    std::vector<int> sidewalks;
};

struct PathInfo {
    int id = 0;
    std::uint16_t flags = 0;
    float halfWidth = 0.0f;
    float speedLimit = 15.0f;
    bool hasException = false; // [Exceptions] entry: density replaces the map density
    float density = 1.0f;
    std::vector<int> lanes;         // all lanes of both sides
    std::vector<int> sidewalks;     // up to two
    int intersection[2] = {-1, -1}; // at center.back() and center.front() (the .bai ends[0], ends[1])
    Vec3 centreStart, centreEnd;
    Aabb bounds;
};

struct NetworkOptions {
    bool driveOnLeft = false;
    float defaultSpeedLimit = 15.0f; // [Speed Limit] of the AI map config
    std::vector<city::AiRoadException> exceptions;
};

class RoadNetwork {
public:
    static RoadNetwork build(const city::AiMap& map, const NetworkOptions& options);

    const std::vector<PathInfo>& paths() const { return m_paths; }
    const std::vector<Lane>& lanes() const { return m_lanes; }
    const std::vector<Sidewalk>& sidewalks() const { return m_sidewalks; }
    const std::vector<Intersection>& intersections() const { return m_intersections; }
    const std::vector<TrafficLightSite>& lights() const { return m_lights; }
    const city::AiMap* source() const { return m_source; }
    bool driveOnLeft() const { return m_driveOnLeft; }

    // Lanes leaving `intersection` other than back along `fromPath` (no U-turns).
    std::vector<int> exits(int intersection, int fromPath) const;
    // Sidewalks meeting `intersection` other than `fromSidewalk`.
    std::vector<int> sidewalksAt(int intersection, int fromSidewalk) const;

private:
    const city::AiMap* m_source = nullptr;
    bool m_driveOnLeft = false;
    std::vector<PathInfo> m_paths;
    std::vector<Lane> m_lanes;
    std::vector<Sidewalk> m_sidewalks;
    std::vector<Intersection> m_intersections;
    std::vector<TrafficLightSite> m_lights;
};

} // namespace mm2::ai
