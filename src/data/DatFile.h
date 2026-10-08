#pragma once

#include "core/Math.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mm2::data {

// Angel "type: a" text data files (tune/*.vehCarSim, *.camTrackCS, ...), read
// the way MM2's datParser::Load / datParser::Read and datAsciiTokenizer do:
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
// The file must start with the seven bytes "type: a". Tokens are separated by
// spaces, tabs, line breaks and NULs; ';' starts a comment and a token that
// starts with '"' runs to the next '"'. The first token is the class name,
// then come the fields up to the matching '}'. Every field name is one token:
// MM2 registers names such as "Approach Rate" (mmHudMap) that can never match
// a token, so those fields are never read. A field's values are the number
// tokens that follow it (they may continue over several lines, as in
// Trans GearRatios); a token is a number when it starts with a digit, '-' or
// '.', and its value is atof's/atoi's reading of it ("1.#QNAN0" is 1).
// Other tokens on the field's line are kept as strings (labels such as
// "Aero asAero :075abc8c {", which MM2 skips). A field followed by '{' is a
// nested block. When a field appears twice the last one counts, as MM2
// assigns each in turn. Lookups are case-sensitive, as in the original.
class DatNode {
public:
    std::string name;
    std::vector<double> numbers;          // atof value of each number token
    std::vector<std::string> numberTexts; // the number tokens as written (for atoi)
    std::vector<std::string> strings;
    std::vector<DatNode> children;
    bool isBlock = false;

    // The last child called `key`, or nullptr.
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
    std::string type; // "a" for ASCII; binary ("type: b") files are rejected
    DatNode root;     // root.children holds the class block

    // The class block (e.g. "vehCarSim"), or nullptr.
    const DatNode* top() const { return root.children.empty() ? nullptr : &root.children.front(); }
};

// Returns std::nullopt (and sets `error`) on malformed input: a binary file,
// or a block without its closing '}' (on which MM2 itself never returns).
std::optional<DatFile> parseDat(std::string_view text, std::string* error = nullptr);

} // namespace mm2::data
