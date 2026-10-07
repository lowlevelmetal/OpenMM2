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
    std::uint32_t extra = 0; // mm2hook dgPathPoint::unk; often uninitialised
};

struct PathSetPath {
    std::string name;          // model name, may carry a prefix such as "open:"
    std::uint32_t count2 = 0;  // mm2hook NumPoints2
    std::uint32_t unknown = 0; // mm2hook Unk1 (type/spacing packed?), unknown
    std::vector<PathSetPoint> points;
};

struct PathSet {
    std::uint32_t unknown = 0; // header word after the path count
    std::vector<PathSetPath> paths;
};

std::optional<PathSet> parsePathSet(std::span<const std::byte> data, std::string* error = nullptr);

} // namespace mm2::city
