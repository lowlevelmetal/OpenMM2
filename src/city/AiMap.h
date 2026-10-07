#pragma once

// AI road network: city/<map>.bai ("CAI1"). Paths (roads between
// intersections) with lane geometry, intersections, and per-room path lists.
// Format notes: docs/formats/bai.md. The record layout is exact (every retail
// file parses to its last byte); several field meanings are inferred and named
// after mm2hook's in-memory aiPath layout where it matches.

#include "core/Math.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mm2::city {

// One side of a road (the file stores the left side, then the right side).
struct AiRoadSide {
    std::uint16_t numLanes = 0;
    std::uint16_t numTrams = 0;     // SF cable-car tracks
    std::uint16_t numTrains = 0;    // inferred from mm2hook field order
    std::uint16_t numSidewalks = 0; // always 1 in retail data (inferred)
    std::uint16_t roadType = 0;     // mm2hook "AmbientType"; 0..3
    std::uint16_t unknown5 = 0, unknown6 = 0;

    // numLanes + 1 arrays of (sections - 1) cumulative distances, each
    // followed by one extra value (laneEndValues).
    std::vector<std::vector<float>> laneLengths;
    std::vector<float> laneEndValues;
    std::vector<float> laneExtras;  // numLanes values
    std::array<float, 10> params{}; // lateral layout, partly uninitialised in the files
    // (3 + numLanes + numTrams + numTrains) polylines, one point per section.
    std::vector<std::vector<Vec3>> polylines;
};

// Link from a path end to an intersection.
struct AiPathEnd {
    std::uint32_t intersection = 0;
    std::uint16_t unknown1 = 0;    // 0xCDCD (uninitialised) in retail files
    std::uint16_t vehicleRule = 0; // 1 or 3 (stop sign / light?), inferred
    std::uint16_t unknown2 = 0;
    std::uint16_t roadIndex = 0; // index of this path in the intersection's list (inferred)
    std::uint16_t unknown3 = 0;
    Vec3 trafficLightPos;
    Vec3 trafficLightAxis;
};

struct AiPath {
    std::uint16_t id = 0;
    std::uint16_t flags = 0; // mm2hook PathFlags: 0x2 alley, 0x4 freeway, 0x8 ...
    std::vector<std::uint16_t> rooms;
    float halfWidth = 0;
    float speedLimit = 0;
    AiRoadSide left, right;
    std::uint32_t unknown = 0;
    std::vector<float> centerLengths; // sections - 1 cumulative distances
    // Per-section frames along the road centre line (measured on all retail
    // paths): z points back, against increasing section index; x points to
    // the left of that direction, towards the `left` side's polylines; y is
    // up. Note (x, y, z) is therefore left-handed. x is unit length except at
    // a few sharp bends; w is close to -z (likely the unsmoothed segment
    // direction).
    std::vector<Vec3> center, xAxis, yAxis, zAxis, wAxis;
    // ends[0] is the intersection at center.back(), ends[1] at center.front()
    // (measured on 529/540 London and 374/379 SF paths; the rest are loops).
    std::array<AiPathEnd, 2> ends;
    const AiPathEnd& endLink() const { return ends[0]; }
    const AiPathEnd& startLink() const { return ends[1]; }

    std::size_t sectionCount() const { return center.size(); }
};

struct AiIntersection {
    std::uint16_t id = 0;
    std::uint16_t room = 0;
    Vec3 center;
    std::vector<std::uint32_t> paths;
};

struct AiMap {
    std::vector<AiPath> paths;
    std::vector<AiIntersection> intersections;
    // Two per-room lists of path ids (indexed by PSDL room id). The first is
    // wider than the second; likely the ambient-traffic spawn/cull sets.
    std::vector<std::vector<std::uint16_t>> roomPathsNear;
    std::vector<std::vector<std::uint16_t>> roomPathsIn;
};

std::optional<AiMap> parseBai(std::span<const std::byte> data, std::string* error = nullptr);

std::vector<std::string> validateAiMap(const AiMap& map, std::size_t roomCount);

} // namespace mm2::city
