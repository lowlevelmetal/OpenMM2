#pragma once

#include "core/Math.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mm2::data {

// Angel "type: a" text data files (tune/*.vehCarSim, *.camTrackCS, ...):
//
//   type: a
//   vehCarSim {
//     Mass 1000.000000
//     InertiaBox 2.0 2.0 3.0
//     Aero {
//       Drag 0.5
//     }
//   }
//
// A field is one or more identifier words ("Approach Rate" is a single
// field in *.mmHudMap) followed by zero or more numbers, which may continue
// over several lines (Trans GearRatios). Quoted strings are also accepted as
// values. A name followed by '{' opens a nested block. Field lookups are
// case-sensitive, as in the original parser.
class DatNode {
public:
    std::string name;
    std::vector<double> numbers;
    std::vector<std::string> strings;
    std::vector<DatNode> children;
    bool isBlock = false;

    const DatNode* child(std::string_view key) const;

    std::optional<float> getFloat(std::string_view key) const;
    std::optional<int> getInt(std::string_view key) const;
    std::optional<Vec2> getVec2(std::string_view key) const;
    std::optional<Vec3> getVec3(std::string_view key) const;
    std::optional<std::string> getString(std::string_view key) const;
    std::vector<float> getFloats(std::string_view key) const;

    // Convenience: assign `out` only when the field exists. Returns whether it did.
    bool read(std::string_view key, float& out) const;
    bool read(std::string_view key, int& out) const;
    bool read(std::string_view key, Vec2& out) const;
    bool read(std::string_view key, Vec3& out) const;
};

struct DatFile {
    std::string type; // "a" for ASCII; other types are rejected
    DatNode root;     // root.children holds the top-level class blocks

    // The first top-level block (e.g. "vehCarSim"), or nullptr.
    const DatNode* top() const { return root.children.empty() ? nullptr : &root.children.front(); }
};

// Returns std::nullopt (and sets `error`) on malformed input.
std::optional<DatFile> parseDat(std::string_view text, std::string* error = nullptr);

} // namespace mm2::data
