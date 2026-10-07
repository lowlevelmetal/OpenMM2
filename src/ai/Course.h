#pragma once

// Driving lines through the road network for AI racers and police: MM1's
// aiGoalFollowWayPts waypoint route (a list of aiMap intersections driven
// along the roads that join them, aiRailSet) rebuilt as one polyline with the
// road edges along it.
//
// MM2's race/<city>/*.opp files keep MM1's idea: row 0 is the car's grid
// place, every other row lies on an intersection of the AI map (measured: the
// rows after the first are within 0-12 m of an intersection centre on every
// London and San Francisco file), and consecutive intersections are joined by
// one road in 94 % (London) / 89 % (SF) of the pairs. Circuits repeat the
// grid place as the last row. The car starts on the road from row 1's
// intersection to row 2's (MM1 aiGoalFollowWayPts::Reset picks the first
// waypoint equal to the intersection the car is heading for).
//
// Open1560 - An Open Source Re-Implementation of Midtown Madness 1 Beta
// Copyright (C) 2020 Brick. GPL-3.0-or-later; OpenMM2 port under the same licence.

#include "ai/RoadNetwork.h"
#include "city/Race.h"
#include "core/Math.h"

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

// A bend of the driving line (the turns aiGoalFollowWayPts::CalcSpeed brakes
// for). Consecutive bends of the same direction closer than 25 m are merged.
struct CourseTurn {
    float s = 0.0f;          // arc length of the turn's middle
    float deflection = 0.0f; // radians, positive = right
    float halfWidth = 6.0f;  // centre line to curb at the turn
};

// Points along the line: road edges and the road they belong to.
struct CoursePoint {
    float left = 6.0f;  // distance from the line to the left curb (m)
    float right = 6.0f; // to the right curb
    int path = -1;      // -1 = across an intersection
    int lanes = 1;      // most lanes on one side of that road
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
    static std::optional<Course> fromOpponentPath(const RoadNetwork& net, std::span<const city::OpponentPoint> rows,
                                                  bool circuit, std::string* error = nullptr);

    bool loop() const { return m_loop; }
    float length() const { return m_line.length; } // one lap for loops
    float startDistance() const { return m_startS; }
    // Arc length of the finish: the end of a point-to-point line; on loops
    // the finish row (the start when there is none).
    float finishDistance() const { return m_finishS; }
    // Distance from the start to the finish driving `laps` laps (loops).
    float raceDistance(int laps) const;
    float wrap(float s) const;

    Vec3 pointAt(float s, Vec3* direction = nullptr) const;
    // Arc length of the point of the line closest to `p` (XZ) within
    // `window` m of `hint` (loops wrap), with the lateral offset (+ right of
    // the driving direction) and XZ distance.
    float locate(const Vec3& p, float hint, float window, float* lateral = nullptr, float* distance = nullptr) const;
    float locate(const Vec3& p, float* lateral = nullptr, float* distance = nullptr) const;
    // Road edges at `s` (distances from the line to the left and right curb).
    void edges(float s, float& left, float& right) const;
    // Narrowest edges over [s, s + distance] (merge in before the road narrows).
    void edgesAhead(float s, float distance, float& left, float& right) const;
    int lanesAt(float s) const;

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
RoadSpot locateOnRoads(const RoadNetwork& net, const Vec3& p);

// Road edges of a path at section `k`: distances from the centre line to the
// left and right curbs (left = the .bai left side, +x of the section frame).
void pathCurbs(const city::AiPath& path, std::size_t k, float& left, float& right);

} // namespace mm2::ai
