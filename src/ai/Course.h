#pragma once

// Driving lines through the road network for AI racers and police: the
// waypoint route MM2's aiVehiclePhysics::RegisterRoute is given (a list of
// aiMap intersections driven along the roads that join them,
// aiMap::DetRdSegBetweenInts) rebuilt as one polyline, with the curbs,
// sidewalk edges, section frames and aiPath flags of its roads along it.
//
// race/<city>/*.opp (aiRouteRacer::Init): row 0 is the car's grid place,
// the last row its destination, every row between a waypoint (the
// intersection whose room holds it; measured within 0-12 m of an
// intersection centre on every London and San Francisco file). Consecutive
// waypoints are joined by one road in 94 % (London) / 89 % (SF) of the
// pairs. The car starts on the road from row 1's intersection to row 2's
// (RegisterRoute starts at waypoint 1).
//
// Parts of this file were first ported from Open1560 (MM1's
// aiGoalFollowWayPts). Open1560 - An Open Source Re-Implementation of
// Midtown Madness 1 Beta, Copyright (C) 2020 Brick. GPL-3.0-or-later;
// OpenMM2 port under the same licence.

#include "ai/RoadNetwork.h"
#include "city/Race.h"
#include "core/Math.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mm2::ai {

// One road a course drives along, in its direction of travel.
struct CourseLeg {
    int path = -1;
    bool forward = true; // along increasing section index (towards PathInfo::intersection[0])
    int from = -1;       // intersections
    int to = -1;
    float start = 0.0f; // arc length of the leg's first and last point on the course
    float end = 0.0f;
};

// A bend of the driving line (the turns aiVehiclePhysics::CalcRoadSpeed
// brakes for). Consecutive bends of the same direction closer than 25 m are
// merged.
struct CourseTurn {
    float s = 0.0f;          // arc length of the turn's middle
    float deflection = 0.0f; // radians, positive = right
    float halfWidth = 6.0f;  // centre line to curb at the turn
    bool intoAlley = false;  // the road after it is an alley (aiPath flag 0x2)
};

// Points along the line: road edges and the road they belong to.
struct CoursePoint {
    float left = 6.0f;       // distance from the line to the left curb (m)
    float right = 6.0f;      // to the right curb
    float leftEdge = 9.0f;   // to the left side's outer edge (beyond the sidewalk)
    float rightEdge = 9.0f;  // to the right side's outer edge
    int path = -1;           // -1 = across an intersection
    int lanes = 1;           // most lanes on one side of that road
    std::uint16_t flags = 0; // aiPath flags of that road: 0x1 divided (the centre is a curb), 0x2 alley
    Vec3 across;             // unit right of travel in the road section's frame (zero off the roads)
    // aiPath::IsPosOnRoad: a point this close to the line (either side) is
    // on the road, this close on the sidewalk (see pathOnRoadLimits).
    float onRoad = 5.0f;
    float onSidewalk = 9.0f;
};

class Course {
public:
    // Drives through `intersections` in order. `start` is where the car
    // begins: on the road between the first two intersections when it lies
    // close to it, otherwise the line starts at `start` itself. `finish`
    // ends a point-to-point line; `loop` closes it (circuit laps).
    static std::optional<Course> build(const RoadNetwork& net, std::span<const int> intersections, const Vec3& start,
                                       const std::optional<Vec3>& finish, bool loop, std::string* error = nullptr);

    // From a .opp driving line (see above). `circuit` closes the loop.
    // `waypoints`, when given, are the intersections of the rows between the
    // first and the last as the racer's driver takes them
    // (Opponent::routeFromPath); otherwise each row's nearest intersection.
    static std::optional<Course> fromOpponentPath(const RoadNetwork& net, std::span<const city::OpponentPoint> rows,
                                                  bool circuit, std::string* error = nullptr,
                                                  std::span<const int> waypoints = {});

    // Along one road from `start` to `finish` when both lie on it (no
    // intersection on the way: aiMap::CalcRoute finds no waypoints); else
    // std::nullopt.
    static std::optional<Course> alongRoad(const RoadNetwork& net, const Vec3& start, const Vec3& finish);

    bool loop() const { return m_loop; }
    float length() const { return m_line.length; } // one lap for loops
    float startDistance() const { return m_startS; }
    // Arc length of the finish: the end of a point-to-point line; on loops
    // the finish row (the start when there is none).
    float finishDistance() const { return m_finishS; }
    // Distance from the start to the finish driving `laps` laps (loops).
    float raceDistance(int laps) const;
    // Where the driving line ends: the finish row of a .opp file (MM2's
    // destination), else the line's point at finishDistance().
    Vec3 finishPoint() const { return m_hasFinishPoint ? m_finishPoint : pointAt(m_finishS); }
    float wrap(float s) const;

    Vec3 pointAt(float s, Vec3* direction = nullptr) const;
    // Arc length of the point of the line closest to `p` (XZ) within
    // `window` m of `hint` (loops wrap), with the lateral offset (+ right of
    // the driving direction) and XZ distance.
    float locate(const Vec3& p, float hint, float window, float* lateral = nullptr, float* distance = nullptr) const;
    float locate(const Vec3& p, float* lateral = nullptr, float* distance = nullptr) const;
    // Road edges at `s` (distances from the line to the left and right curb,
    // and optionally to the outer edges beyond the sidewalks).
    void edges(float s, float& left, float& right, float* leftEdge = nullptr, float* rightEdge = nullptr) const;
    // aiPath::IsPosOnRoad's limits at `s` (CoursePoint::onRoad, onSidewalk).
    void onRoadLimits(float s, float& road, float& sidewalk) const;
    // Narrowest edges over [s, s + distance] (merge in before the road narrows).
    void edgesAhead(float s, float distance, float& left, float& right) const;
    int lanesAt(float s) const;

    // The vertices of the line (road sections; intersections are crossed by
    // a chord). Loops: the repeated closing point is not a vertex.
    std::size_t vertexCount() const;
    const Vec3& vertex(std::size_t i) const { return m_line.points[i]; }
    float vertexDistance(std::size_t i) const { return m_line.distances[i]; }
    const CoursePoint& vertexInfo(std::size_t i) const { return m_points[i]; }
    // Unit right of the line at a vertex (across the bisector of its two
    // segments).
    Vec3 vertexRight(std::size_t i) const;
    // The first vertex ahead of arc length `s` (loops wrap; open lines stop
    // at the last), and the one after `i` (`i` itself at the end of an open
    // line).
    std::size_t vertexAfter(float s) const;
    std::size_t nextVertex(std::size_t i) const;

    const Polyline& line() const { return m_line; }
    const std::vector<CoursePoint>& points() const { return m_points; }
    const std::vector<CourseTurn>& turns() const { return m_turns; }
    const std::vector<CourseLeg>& legs() const { return m_legs; }
    const std::vector<int>& intersections() const { return m_intersections; }

private:
    std::size_t segmentAt(float s) const;
    void findTurns();

    Polyline m_line; // loops: the last point repeats the first
    std::vector<CoursePoint> m_points;
    std::vector<CourseTurn> m_turns;
    std::vector<CourseLeg> m_legs;
    std::vector<int> m_intersections;
    bool m_loop = false;
    float m_startS = 0.0f;
    float m_finishS = 0.0f;
    Vec3 m_finishPoint;
    bool m_hasFinishPoint = false;
};

// Shortest route between two intersections along the roads (either
// direction: racers and police ignore one-way rules). Includes both ends;
// empty when unreachable. Roads in `avoid` cost `penalty` metres extra.
std::vector<int> findRoute(const RoadNetwork& net, int from, int to, std::span<const int> avoid = {},
                           float penalty = 1000.0f);
// Intersection whose centre is nearest `p` (XZ), -1 if none.
int nearestIntersection(const RoadNetwork& net, const Vec3& p, float* distance = nullptr);

// Where a point is on the road network.
struct RoadSpot {
    int path = -1;         // nearest road (-1 = none)
    int intersection = -1; // inside this intersection
    float s = 0.0f;        // arc length along the road's centre line (increasing section order)
    float lateral = 0.0f;  // offset from the centre line, + towards the road's left side
    float distance = 1e9f; // XZ distance to the centre line
    bool onRoad = false;   // between the curbs (+1 m) or inside an intersection
};
// `shortcuts` false leaves out the shortcut roads of <city>_sup.bai.
RoadSpot locateOnRoads(const RoadNetwork& net, const Vec3& p, bool shortcuts = true);

// Road edges of a path at section `k`: distances from the centre line to the
// left and right curbs (left = the .bai left side, +x of the section frame).
void pathCurbs(const city::AiPath& path, std::size_t k, float& left, float& right);
// And to the outer edges beyond the sidewalks (at least the curbs).
void pathOuterEdges(const city::AiPath& path, std::size_t k, float& left, float& right);
// aiPath::IsPosOnRoad's limits for a road: a point is on the road while its
// distance from the centre line is under `road`, on the sidewalk under
// `sidewalk` (each less a margin the caller takes off). MM2 reads them from
// the right side's lateral layout (AiRoadSide::params, the boundaries of its
// lanes and sidewalk in turn): with n lanes, road = params[2n - 1] and
// sidewalk = params[2n + 1], for both sides of the road. A side without
// lanes (nine one-way SF alleys) has no road part (MM2 reads the word
// before the layout there, a pointer: a denormal float) and its sidewalk
// ends at params[1]. Layouts that are missing or implausible (none in the
// retail maps; hand-built test maps) fall back to the curbs and the outer
// edges at the middle section.
void pathOnRoadLimits(const city::AiPath& path, float& road, float& sidewalk);

// aiPath::IsPosOnRoad of road `path` for a point: 1 on the road, 2 on the
// sidewalk, 3 beyond, from its distance to the road's centre line (XZ) and
// the limits above, each less `margin`.
int posOnRoad(const RoadNetwork& net, int path, const Vec3& p, float margin);

// A road network component (aiMap): `type` 1 a road (path id), 3 an
// intersection.
struct MapComponentRef {
    int id = -1;
    int type = 0;
};
// aiMap::PositionToAIMapComp: the intersection a point is inside, else the
// roads (up to five) it lies across within twice their half width; empty
// when on none. MM2 asks the components of the point's PSDL room; OpenMM2
// measures the roads (inferred equivalent).
std::vector<MapComponentRef> componentsAt(const RoadNetwork& net, const Vec3& p);
// aiMap::MapComponent: the intersection a point is inside (type 3), else the
// road it is on or beside (type 1); none (type 0) returns `previous`.
int mapComponent(const RoadNetwork& net, const Vec3& p, int previous, int& type);
// aiMap::CalcRoute: the waypoint intersections from `from` to `to`, as the
// police compute them: a shortest route over the intersections where each
// road costs its centre line length, from the start's intersection (or both
// ends of its road at their distances along it) to the goal's intersection
// (or either end of its road). A start on a road is preceded by that road's
// other end, so the first leg is the road the car is on. Empty when both
// lie in the same component.
std::vector<int> calcRoute(const RoadNetwork& net, const Vec3& from, const Vec3& to);

} // namespace mm2::ai
