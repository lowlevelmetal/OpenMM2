#pragma once

#include "core/Math.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mm2::asset {

// Angel PKG model packages (geometry/*.pkg). See docs/formats/pkg.md.
//
// A package is "PKG3" (or the older "PKG2") followed by named FILE chunks:
//   * geometry chunks named "<PART>_<LOD>" (cars: BODY_H, WHL0_M, ...) or
//     just "<LOD>" (buildings and props: H, M, L, VL)
//   * "shaders": the material table, one set per paint job
//   * "offset":  a Vec3: for city objects the world position the mesh was
//     centred on (equal to the object's .mtx origin); in car packages it
//     repeats one part's pivot (an exporter artefact, apparently unused)
//   * "xrefs":   references to other models placed with a matrix
//
// Geometry is stored as Direct3D 7 style indexed triangle lists, one or more
// per material ("section"), with flexible vertex formats.

// Direct3D 7 flexible vertex format bits used by the files.
namespace Fvf {
inline constexpr std::uint32_t PositionMask = 0x00E;
inline constexpr std::uint32_t Xyz = 0x002;
inline constexpr std::uint32_t Normal = 0x010;
inline constexpr std::uint32_t Diffuse = 0x040;
inline constexpr std::uint32_t Specular = 0x080;
inline constexpr std::uint32_t TexCountMask = 0xF00;
inline constexpr std::uint32_t TexCountShift = 8;
} // namespace Fvf

// Size in bytes of one vertex for `fvf`, or 0 if the format is not supported.
std::size_t fvfVertexSize(std::uint32_t fvf);

struct PkgVertex {
    Vec3 position;
    Vec3 normal;                     // zero when the format has no normals
    Vec2 uv;                         // first texture coordinate set; D3D convention (v down)
    // Diffuse colour (ARGB) when present, else white. The file stores it with
    // red in the low byte; modGetStatic swaps red and blue on load.
    std::uint32_t color = 0xFFFFFFFF;
};

// One draw: an indexed primitive list with its own vertex array.
struct PkgPacket {
    // Primitive type as stored; modGetStatic draws it as D3DPRIMITIVETYPE
    // value + 1. Every retail file uses 3, an indexed triangle list (index
    // count is always a multiple of three).
    std::uint32_t primitive = 3;
    std::vector<PkgVertex> vertices;
    std::vector<std::uint16_t> indices;
};

// Packets sharing one material.
struct PkgSection {
    // Index into a paint job's material list. MM2 keeps only the low byte of
    // the stored u32 (modGetStatic).
    std::uint32_t shaderIndex = 0;
    std::vector<PkgPacket> packets;
};

enum class Lod : std::uint8_t { High, Medium, Low, VeryLow, None };

struct PkgMesh {
    std::string name; // chunk name as stored, e.g. "BODY_H", "WHL0_M", "H"
    std::string part; // name without the LOD suffix, e.g. "BODY" ("" for plain "H")
    Lod lod = Lod::None;
    std::uint32_t fvf = 0;
    std::vector<PkgSection> sections;

    std::size_t vertexCount() const;
    std::size_t triangleCount() const;
    Aabb bounds() const;
    // modGetStatic's radius: the largest distance of a vertex from the
    // model's origin (lvlInstance::GetGeomSet takes the largest over a
    // part's levels of detail).
    float radius() const;
};

// A material as modShader::Load builds it. Full materials store a
// D3DMATERIAL7 (diffuse, ambient, specular, emissive, power); MM2 rounds each
// diffuse, specular and emissive component down to a multiple of 1/32 (below
// 0.05 becomes 0, above 0.95 becomes 1). Compact materials store diffuse,
// specular and emissive as bytes (no rounding); light glows (fxltglow) put
// their colour in the emissive slot. Either way MM2 then replaces the
// ambient colour with the diffuse one.
struct PkgMaterial {
    std::string texture; // texture base name (no extension); empty = untextured
    Vec4 diffuse{1, 1, 1, 1};
    Vec4 ambient{1, 1, 1, 1};
    Vec4 specular{0, 0, 0, 0};
    Vec4 emissive{0, 0, 0, 0};
    float shininess = 0;
};

// The shader table shared by a package's "shaders" chunk and
// anim/<pedestrian>.shaders (modShader::LoadShaderSet).
struct ShaderTable {
    // Raw header word: low 7 bits = paint job count, 0x80 = compact (byte
    // colour) materials.
    std::uint32_t type = 0;
    std::uint32_t perPaintjob = 0;
    std::vector<std::vector<PkgMaterial>> paintjobs; // paintjobs[p][shader]
};

// Reads a shader table at the start of `data`; `consumed` receives its size.
std::optional<ShaderTable> parseShaderTable(std::span<const std::byte> data, std::size_t* consumed = nullptr,
                                            std::string* error = nullptr);

struct PkgXref {
    Mat34 transform;
    std::string name; // referenced model, e.g. "sp_light_red_f"
};

struct Pkg {
    int version = 3; // 2 or 3
    std::vector<PkgMesh> meshes;

    // Raw "shaders" header word: low 7 bits = paint job count, 0x80 = compact
    // (byte colour) materials.
    std::uint32_t shaderType = 0;
    std::uint32_t shadersPerPaintjob = 0;
    // paintjobs[p][shaderIndex]. Paint job 0 is the default colour; cars list
    // their colours in the same order as "Colors=" in tune/<car>.info.
    std::vector<std::vector<PkgMaterial>> paintjobs;

    std::optional<Vec3> offset;
    std::vector<PkgXref> xrefs;

    // Non-fatal oddities found while parsing (e.g. wrong totals in a header).
    std::vector<std::string> warnings;

    const PkgMesh* find(std::string_view part, Lod lod) const; // part "" for plain LOD meshes
    const PkgMesh* find(std::string_view name) const;          // exact chunk name, case-insensitive
    // The mesh MM2 uses for `part` at `lod` (lvlInstance::GetGeomSet): a
    // missing level takes the next less detailed one that exists (VL fills L,
    // L fills M, M fills H), never a more detailed one, so a part with only
    // a high LOD has nothing at the lower levels. Parts without a LOD suffix
    // are returned for any level.
    const PkgMesh* findBest(std::string_view part, Lod lod = Lod::High) const;
    // Distinct part names in file order.
    std::vector<std::string> parts() const;
};

// Splits "WHL0_M" into ("WHL0", Medium); "VL" into ("", VeryLow); names
// without a LOD suffix return Lod::None and the full name.
std::pair<std::string, Lod> splitLodName(std::string_view name);
std::string_view lodSuffix(Lod lod);

std::optional<Pkg> parsePkg(std::span<const std::byte> data, std::string* error = nullptr);

// True for the few retail asset files that are broken in the shipped archives
// (and never loaded by the game): geometry/thing.pkg and two zero-byte PKGs.
bool isKnownBrokenRetailAsset(std::string_view path);

} // namespace mm2::asset
