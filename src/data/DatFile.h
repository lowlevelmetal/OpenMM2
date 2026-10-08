#pragma once

#include "core/Math.h"

#include <cstdint>
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
// starts with '"' runs to the next '"' (datBaseTokenizer::GetToken, ported
// with its look-ahead character). The first token is the class name, then
// come the fields up to the matching '}'. Every field name is one token:
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

// One record a class registers in its FileIO (datParser::AddRecord /
// AddParser). The types are MM2's record types, in its numbering.
struct DatRecord {
    enum class Type : std::uint8_t {
        String, // count tokens
        Bool,   // count GetInt != 0
        Byte,   // count GetInt, truncated to a signed char
        Short,  // count GetInt, truncated to a short
        Int,    // count GetInt
        Float,  // count GetFloat
        Vec2,   // count x 2 GetFloat
        Vec3,   // count x 3 GetFloat
        Vec4,   // count x 4 GetFloat
        Parser, // a nested block read with `records`
    };
    std::string name;
    Type type = Type::Float;
    int count = 1;
    std::vector<DatRecord> records; // Parser: the nested class's records
};
using DatSchema = std::vector<DatRecord>;

// Reads every field and block of the file (see DatNode). Returns std::nullopt
// (and sets `error`) on malformed input: a binary file, or a block without
// its closing '}' (on which MM2 itself never returns).
//
// Without the class's record list this cannot tell MM2's known names from
// unknown ones, which MM2 reads differently: an unknown name skips the rest
// of its line, or the block after it when the next token is '{'. So a
// labelled block the class does not register ("AsphaltRule asBirthRule
// :addr {" in a vehCarSim) is a block here, while MM2 skips only its first
// line and reads the lines inside as fields of the outer class, its closing
// brace ending the outer block. Use the schema overload for exact results.
std::optional<DatFile> parseDat(std::string_view text, std::string* error = nullptr);

// Reads the file exactly as datParser::Read does with `schema` registered
// for its class: only registered records appear in the tree, each with the
// values MM2 assigns (in file order; the last assignment of a name counts),
// and unknown names are skipped as MM2 skips them.
std::optional<DatFile> parseDat(std::string_view text, const DatSchema& schema, std::string* error = nullptr);

} // namespace mm2::data
