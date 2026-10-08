#pragma once

// City environment files: lighting, fog, sky, water, extents, reset points,
// per-room ambient colours and physics materials.
// Format notes: docs/formats/environment.md.

#include "core/Math.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mm2::city {

inline constexpr int kTimesOfDay = 4; // morning, noon, evening, night
inline constexpr int kWeathers = 4;   // clear, cloudy, foggy, rainy
// city/<map>.ltNN and <map>_fog.csv rows are indexed timeOfDay * 4 + weather.
constexpr int lightingIndex(int timeOfDay, int weather) {
    return timeOfDay * kWeathers + weather;
}

// city/<map>.lt00 .. .lt15 ("type: a" files, block named e.g. "clear-morning").
// Headings/pitches are radians; colours are 0..1.
struct LightingDef {
    std::string name;
    float keyHeading = 0, keyPitch = 0;
    Vec3 keyColor;
    float fill1Heading = 0, fill1Pitch = 0;
    Vec3 fill1Color;
    float fill2Heading = 0, fill2Pitch = 0;
    Vec3 fill2Color;
    std::uint32_t ambient = 0; // packed ARGB (stored as a signed int)
};
std::optional<LightingDef> parseLighting(std::string_view text, std::string* error = nullptr);

// city/<map>_fog.csv
struct FogDef {
    std::uint8_t r = 0, g = 0, b = 0;
    float start = 0, end = 0;
    std::string description; // ignored by the game per the header
};
std::optional<std::vector<FogDef>> parseFogTable(std::string_view text, std::string* error = nullptr);

// city/<map>.sky: "<model> <a> <b> <c>" (sky dome model and three parameters,
// meaning unknown; retail values 0 0.95 0.005).
struct SkyDef {
    std::string model;
    std::vector<float> params;
};
std::optional<SkyDef> parseSky(std::string_view text, std::string* error = nullptr);

// city/<map>.water: water plane height, then room ids (inferred: rooms that
// use the water plane).
struct WaterDef {
    float height = 0;
    std::vector<int> rooms;
};
std::optional<WaterDef> parseWater(std::string_view text, std::string* error = nullptr);

// city/<map>.ext: "minX minZ maxX maxZ" world rectangle (likely the HUD map extent).
struct MapExtent {
    float minX = 0, minZ = 0, maxX = 0, maxZ = 0;
};
std::optional<MapExtent> parseExtent(std::string_view text, std::string* error = nullptr);

// city/<map>.reset: "x y z  [# comment]" positions (fall-through recovery points).
struct ResetPoint {
    Vec3 position;
    std::string comment;
};
std::vector<ResetPoint> parseResetPoints(std::string_view text);

// city/<map>.lmap ("LMP0"): one packed ARGB ambient colour per room.
std::optional<std::vector<std::uint32_t>> parseLightMap(std::span<const std::byte> data,
                                                        std::string* error = nullptr);

// city/materials.mtl: physics materials ("mtl name { key: values }").
// Defaults are lvlMaterial's constructor's (what a block that ends after
// its sound or depth leaves).
struct PhysMaterial {
    std::string name;
    float elasticity = 0.5f, friction = 1.0f, drag = 0;
    float width = 1.0f, height = 0, depth = 0;
    std::string effect;
    int sound = 0;
    std::array<int, 2> ptxIndex{-1, -1};
    std::array<float, 2> ptxThreshold{0.25f, 0.5f};
};
std::optional<std::vector<PhysMaterial>> parseMaterialLibrary(std::string_view text,
                                                              std::string* error = nullptr);

// city/materials.csv: texture name -> physics material name ("none" = default).
struct TextureMaterial {
    std::string texture;
    std::string material;
};
std::vector<TextureMaterial> parseTextureMaterials(std::string_view text);

} // namespace mm2::city
