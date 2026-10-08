#pragma once

// Path sets ("PTH1"): race/<dir>/*.pathset and city/*.pathset. Named point
// lists used to place props along paths (barricades, ferries, bridges,
// parked cars, trains). Format notes: docs/formats/pathset.md.

#include "core/Math.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mm2::city {

struct PathSetPoint {
    Vec3 position;
    // The 4 bytes stored after the position. dgPath::Load reads each point's
    // flags word *before* its position, so this is the next point's flags,
    // and for the last point the path's trailer (type, spacing, 2 unused
    // bytes). See PathSetPath::flags, type and spacing for MM2's reading.
    std::uint32_t extra = 0;
};

struct PathSetPath {
    std::string name;          // model name, may carry a prefix such as "open:"
    std::uint32_t count2 = 0;  // the word after the point count (dgPath +0x2c)
    std::uint32_t unknown = 0; // the first point's flags word (dgPath::Load)
    std::vector<PathSetPoint> points;
    // dgPath::Load: each point's flags word, read before its position.
    std::vector<std::uint32_t> flags;
    // dgPath::Load: the trailer's type byte, and its spacing byte in quarter
    // metres (0 means 5 m).
    std::uint8_t type = 0;
    float spacing = 5.0f;
};

struct PathSet {
    std::uint32_t unknown = 0; // header word after the path count
    std::vector<PathSetPath> paths;
};

std::optional<PathSet> parsePathSet(std::span<const std::byte> data, std::string* error = nullptr);

} // namespace mm2::city
