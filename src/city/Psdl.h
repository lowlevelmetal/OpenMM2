#pragma once

// PSDL ("PSD0") city street description: city/<map>.psdl.
// Format notes: docs/formats/psdl.md. Everything structural here was verified
// by parsing every room of every retail PSDL to the exact end of its
// attribute list; semantics marked "inferred" in the doc are best guesses.

#include "core/Math.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mm2::city {

// Room flags (byte per room). Names follow mm2hook's lvlRoomInfo flags.
// Observed: 0x04 on every building block (mm2hook calls it Water; in PSDL it
// marks plain/building rooms), 0x08 roads, 0x10 intersections, 0x02 below
// ground, 0x40 "warp" (tunnels/bridges that need special room lookup).
namespace RoomFlag {
inline constexpr std::uint8_t UnhitBanger = 0x01;
inline constexpr std::uint8_t Subterranean = 0x02;
inline constexpr std::uint8_t Standard = 0x04;
inline constexpr std::uint8_t Road = 0x08;
inline constexpr std::uint8_t Intersection = 0x10;
inline constexpr std::uint8_t SpecialBound = 0x20;
inline constexpr std::uint8_t Warp = 0x40;
inline constexpr std::uint8_t Instance = 0x80;
} // namespace RoomFlag

// Attribute type: bits 3..6 of the attribute header word.
enum class PsdlAttrType : std::uint8_t {
    RoadStrip = 0,        // sections of 4 vertices: outer L, curb L, curb R, outer R
    SidewalkStrip = 1,    // pairs (curb at road level, outer edge at sidewalk level)
    RectangleStrip = 2,   // pairs forming a quad strip
    Sliver = 3,           // thin wall: top height, texture density (both height-table indices), 2 vertices
    Crosswalk = 4,        // 4 vertices
    RoadTriangleFan = 5,  // n vertices, fan
    TriangleFan = 6,      // n vertices, fan
    FacadeBound = 7,      // invisible building wall (collision/lighting)
    DividedRoadStrip = 8, // sections of 6 vertices with a median
    Tunnel = 9,           // walls/ceiling/railings for the next road attribute
    Texture = 10,         // selects the texture group for following attributes
    Facade = 11,          // textured building wall
    RoofTriangleFan = 12, // flat roof at a height
};
inline constexpr int kPsdlAttrTypeCount = 13;
const char* psdlAttrTypeName(PsdlAttrType t);

// One decoded attribute. `args` holds the words that follow the header (and
// the explicit count word, when the subtype is 0), exactly as stored.
struct PsdlAttribute {
    PsdlAttrType type{};
    std::uint8_t subtype = 0; // low 3 bits of the header word
    bool last = false;        // 0x80: last geometry attribute of the room
    std::vector<std::uint16_t> args;

    // --- typed views (valid only for the matching type) ---

    // Texture: the texture *group* base index (value - 1), or -1 for "none".
    int textureBase() const;

    // Facade: heights are indices into Psdl::heights (absolute Y).
    std::uint16_t facadeBottom() const { return args[0]; }
    std::uint16_t facadeTop() const { return args[1]; }
    std::int16_t facadeURepeat() const { return static_cast<std::int16_t>(args[2]); }
    std::int16_t facadeVRepeat() const { return static_cast<std::int16_t>(args[3]); }
    // Facade, FacadeBound, Sliver: the wall's two ground vertices.
    std::uint16_t wallLeft() const { return args[args.size() - 2]; }
    std::uint16_t wallRight() const { return args[args.size() - 1]; }

    // Sliver: top height index and texture tiling.
    std::uint16_t sliverTop() const { return args[0]; }
    std::uint16_t sliverTextureScale() const { return args[1]; }

    // FacadeBound: lighting/sun angle (0..63, inferred) and top height index.
    std::uint16_t facadeBoundAngle() const { return args[0]; }
    std::uint16_t facadeBoundTop() const { return args[1]; }

    // Roof: height index, then the polygon vertex indices.
    std::uint16_t roofHeight() const { return args[0]; }

    // Strips/fans/roofs/crosswalks: the vertex index list (count word and the
    // roof height stripped). Divided roads: the 6-per-section list.
    std::span<const std::uint16_t> vertices() const;

    // DividedRoadStrip header.
    std::uint8_t dividerFlags() const { return static_cast<std::uint8_t>(dividedHeader()[0] & 0xFF); }
    // 1 = flat, 2 = elevated (curbed), 3 = wedged (jersey barrier). Inferred.
    int dividerType() const { return dividerFlags() & 0x03; }
    bool dividerCapStart() const { return (dividerFlags() & 0x40) != 0; }
    bool dividerCapEnd() const { return (dividerFlags() & 0x80) != 0; }
    // 1-based texture value of the divider (see docs; side = value-1, top = value).
    std::uint8_t dividerTexture() const { return static_cast<std::uint8_t>(dividedHeader()[0] >> 8); }
    // Divider height in metres (stored as 8.8 fixed point).
    float dividerHeight() const { return dividedHeader()[1] / 256.0f; }

    // Tunnel header: flags, then two heights in 8.8 fixed point; junction
    // tunnels (subtype 0) append per-perimeter-edge bit masks.
    std::uint16_t tunnelFlags() const { return tunnelWords()[0]; }
    float tunnelHeight1() const { return tunnelWords()[1] / 256.0f; }
    float tunnelHeight2() const { return tunnelWords()[2] / 256.0f; }
    bool tunnelIsJunction() const { return subtype == 0; }
    std::span<const std::uint16_t> tunnelEdgeMasks() const { return tunnelWords().subspan(3); }

private:
    std::span<const std::uint16_t> counted() const; // args without the explicit count word
    std::span<const std::uint16_t> dividedHeader() const { return counted().first(2); }
    std::span<const std::uint16_t> tunnelWords() const { return counted(); }
};

struct PsdlPerimeterPoint {
    std::uint16_t vertex = 0;
    std::uint16_t neighbor = 0; // room across the edge to the next point; 0 = none
};

struct PsdlRoom {
    std::vector<PsdlPerimeterPoint> perimeter;
    std::vector<PsdlAttribute> attributes;
    std::uint8_t flags = 0;
    std::uint8_t propRule = 0; // street prop placement rule (inferred)
};

// Road ("path") between two intersections, in the trailing section of the
// file. These are the roads the AI map (.bai) was generated from: the counts
// match and each .bai path lists the same rooms.
struct PsdlRoad {
    // The road's flag word (lvlAiRoad +0): bit 1 blocked (lvlAiMap::IsBlocked),
    // 2 closed to pedestrians (IsPedBlocked), 3 divided (IsDivided), 4 alley
    // (IsAlley), 5 freeway (IsFreeway); bits 10-11 and 16-17 the intersection
    // type at either end (GetIntersectionType 0 and 1). Other bits unread by
    // lvlAiMap (OpenMM2's prop placement reads bit 6).
    std::uint32_t flags = 0;
    // One value per lane on each side (the counts are lvlAiMap::GetNumLanes'
    // lane counts); fractions in (0,1), use inferred.
    std::vector<float> leftValues, rightValues;
    // Per end, the stop light / sign type (lvlAiRoad +10/+11;
    // lvlAiMap::GetStopLightType masks it with 0x15 and GetStopLightName picks
    // the prop by it).
    std::array<std::uint8_t, 2> stopLights{};
    // Corner vertices of the intersections at each end (4 each).
    std::array<std::uint16_t, 4> startCrossroads{};
    std::array<std::uint16_t, 4> endCrossroads{};
    // Rooms along the road. Some SF roads store negated ids (-408 for room
    // 408); the .bai lists the same rooms un-negated. Meaning of the sign is
    // unknown (possibly reversed orientation); use roomId() for the room.
    std::vector<std::int16_t> rooms;
    static std::uint16_t roomId(std::int16_t r) { return static_cast<std::uint16_t>(r < 0 ? -r : r); }
};

struct Psdl {
    std::uint32_t version = 0;
    std::vector<Vec3> vertices;
    std::vector<float> heights;
    std::vector<std::string> textures; // empty string = unused slot
    // rooms[0] is a dummy so room ids index directly; real rooms are 1..N.
    std::vector<PsdlRoom> rooms;
    // First room that is not a building block (rooms [1, this) are blocks).
    std::uint32_t firstRoadRoom = 0;
    Aabb bounds;
    Vec3 sphereCenter;
    float sphereRadius = 0;
    std::vector<PsdlRoad> roads;

    std::size_t roomCount() const { return rooms.size(); }
    const std::string* texture(int index) const {
        return index >= 0 && static_cast<std::size_t>(index) < textures.size() ? &textures[index] : nullptr;
    }
};

std::optional<Psdl> parsePsdl(std::span<const std::byte> data, std::string* error = nullptr);

// Decodes one room's attribute word list. Returns false if an attribute runs
// past the end of the list.
bool decodePsdlAttributes(std::span<const std::uint16_t> words, std::vector<PsdlAttribute>& out,
                          std::string* error = nullptr);

// Structural checks (indices in range, etc.). Returns human-readable problems.
std::vector<std::string> validatePsdl(const Psdl& psdl);

} // namespace mm2::city
