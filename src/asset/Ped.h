#pragma once

// Pedestrian assets (anim/ in MM2CORE.AR). See docs/formats/pedestrians.md.
//
// A pedestrian type ("pedmodel_man", "pedmodel_woman", ...) consists of
//   anim/<type>.skel     text bone hierarchy (19 bones)
//   anim/<type>.mod      text mesh ("version: 1.09"), rigidly skinned: every
//                        vertex belongs to exactly one bone and is stored in
//                        that bone's local space
//   anim/<type>.shaders  clothing colour variants (D3DMATERIAL7 per material)
//   anim/<type>.rays     per-bone and per-variant data, meaning unknown
//   anim/<type>.remap    17 indices, meaning unknown (pedmodel_woman only)
//   anim/<type>.csv      animation state table (names, frames, root motion)
// plus the shared binary animations anim/pedanim_*.anim.

#include "asset/VehicleModel.h" // ReadFileFn
#include "core/Math.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mm2::asset {

// --- Skeleton (anim/*.skel) ---------------------------------------------------
//
//   NumBones 19
//   bone root {
//       offset 0.000570 1.147210 -0.000000
//       bone spine { offset ... bone neck { ... } }
//   }
// Bones are listed depth-first; that order is also the animation channel
// order. `offset` is the bone origin in its parent's (rotated) space.
// crBoneData::Load also accepts optional "rotmin x y z" and "rotmax x y z"
// after the offset (Euler limits for crBoneData::ApplyLimits, default -pi and
// pi); no retail file has them.
struct Skeleton {
    struct Bone {
        std::string name;
        int parent = -1;
        Vec3 offset;
        Vec3 rotMin{-3.14159265f, -3.14159265f, -3.14159265f};
        Vec3 rotMax{3.14159265f, 3.14159265f, 3.14159265f};
    };
    std::vector<Bone> bones;

    int find(std::string_view name) const; // exact name; -1 when absent
};

std::optional<Skeleton> parseSkeleton(std::string_view text, std::string* error = nullptr);

// --- Animation (anim/pedanim_*.anim) -----------------------------------------
//
// 17-byte header, little-endian (crAnimation::LoadAnim):
//   u32 reserved        0 in every retail file (see below)
//   u32 frameCount      1..10000
//   u32 channelCount    1..1000; always 60 = 3 root translation + 19 bones x 3 Euler angles
//   f32 cycleDistance   forward travel of one loop (crAnimation::Normalize), see docs
//   u8  flags           always 1, meaning unknown
// followed by frameCount x channelCount f32. A nonzero first word marks the
// older layout: it is the frame count itself, and the following word is the
// bone count (channels = bones x 3 + 3). Per frame: root translation
// (model space; replaces the root bone's skeleton offset), then one Euler
// vector (radians) per bone in skeleton order, applied with
// matrixFromEulersXZY.
struct PedAnimation {
    std::uint32_t reserved = 0;
    std::uint32_t frameCount = 0;
    std::uint32_t channelCount = 0;
    float cycleDistance = 0.0f;
    std::uint8_t flags = 0;
    std::vector<float> channels; // frameCount * channelCount

    std::size_t boneCount() const { return channelCount >= 3 ? (channelCount - 3) / 3 : 0; }
    Vec3 rootTranslation(std::uint32_t frame) const;
    Vec3 boneRotation(std::uint32_t frame, std::size_t bone) const;
};

std::optional<PedAnimation> parsePedAnimation(std::span<const std::byte> data, std::string* error = nullptr);

// Rotation from Euler angles as built by MM2's Matrix34::FromEulersXZY:
// rotate about X, then Z, then Y (row vectors: M = Rx * Rz * Ry with
// Mat34::rotationX/Y/Z). The translation part is zero.
Mat34 matrixFromEulersXZY(const Vec3& eulers);

// Bone-to-model transforms for a pose, as MM2's crAnimFrame::Pose and
// crBoneData::Transform build them: each bone's local matrix is
// matrixFromEulersXZY(rotation) with the skeleton offset as translation (the
// root's translation comes from the animation), and model = local *
// parentModel. `frame` is 0-based; MM2 poses whole frames, and fractional
// frames here interpolate the channels linearly (an OpenMM2 addition for
// tools; the game passes whole frames). Frames are clamped to the animation.
// With `anim` == nullptr the bind pose (zero rotations, skeleton offsets) is
// produced.
void posePed(const Skeleton& skeleton, const PedAnimation* anim, float frame, std::vector<Mat34>& out);

// --- Mesh (anim/*.mod) ---------------------------------------------------------
//
// Text format "version: 1.09" (modModel::LoadAscii reads 1.08 and 1.09;
// "2.00" is the binary form, not supported here) with header counts, then
//   v x y z | n x y z | c r g b a | t1 u v
// pools (with fewer than two colours MM2 drops per-vertex colour and the
// vertices are white), then materials and geometry in one of two layouts:
//   * packet layout (pedmodel_man/manw): each material has "packets: N" and
//     the packets follow in material order:
//       packet <adjuncts> <triangles> <matrices> [<reskins>] {
//         adj <vertex> <normal> <color> <tex1> <tex2> <matrix slot>
//                                 (no slot when the packet has no matrices)
//         reskin ...              (blend weights; skipped, none in retail files)
//         tri <a> <b> <c>          (indices into the packet's adjuncts)
//         mtx <bone>...            (bone index for each matrix slot)
//       }
//   * flat layout (pedmodel_woman/womanw): each material has "adjuncts: N"
//     and "primitives: P"; global lists of
//       adj <vertex> <normal> <color> <tex1> <tex2>
//       tri <a> <b> <c>            (global adjunct indices)
//     then "mtxv" and "mtxn" give per-bone counts of the vertex and normal
//     pools, which are sorted by bone.
// Positions and normals are local to the vertex's bone.
struct PedMesh {
    struct Vertex {
        Vec3 position; // bone-local
        Vec3 normal;   // bone-local
        Vec2 uv;
        Vec4 color{1, 1, 1, 1};
        std::uint32_t bone = 0;
    };
    struct Material {
        std::string name; // "Businessman1:SKIN"
        Vec3 ambient, diffuse, specular;
        std::string illum; // "diffuse"
        int textureCount = 0;
        std::string texture; // first "texture:" name; no retail material has one
        std::uint32_t firstIndex = 0;
        std::uint32_t indexCount = 0;
    };

    std::string version;
    std::uint32_t boneCount = 0;
    std::vector<Vertex> vertices; // one per adjunct
    std::vector<std::uint32_t> indices; // triangle list, grouped by material
    std::vector<Material> materials;
};

std::optional<PedMesh> parsePedMesh(std::string_view text, std::string* error = nullptr);

// --- Clothing variants (anim/*.shaders) ----------------------------------------
//
// The shader table of a PKG "shaders" chunk (asset::parseShaderTable;
// pedAnimationInstance::Load reads it with modShader::LoadShaderSet):
//   u32 variantCount (low 7 bits; 0x80 = compact byte colours), u32 materialCount
//   variantCount x materialCount entries:
//     u8 nameLength, char name[nameLength] (texture; empty in all retail files)
//     f32 diffuse[4], ambient[4], specular[4], emissive[4], power
// The floats are a Direct3D 7 D3DMATERIAL7, rounded as modShader::Load does
// (see PkgMaterial). Material i of a variant colours material i of the .mod
// (verified: one variant of every type reproduces the .mod's own material
// colours exactly). Without a .shaders file MM2 uses one variant made of the
// .mod's own materials.
struct PedShader {
    std::string texture;
    Vec4 diffuse, ambient, specular, emissive;
    float power = 0.0f;
};

struct PedShaderSet {
    std::uint32_t variantCount = 0;
    std::uint32_t materialCount = 0;
    std::vector<PedShader> shaders; // variant-major

    const PedShader* get(std::uint32_t variant, std::uint32_t material) const;
};

std::optional<PedShaderSet> parsePedShaders(std::span<const std::byte> data, std::string* error = nullptr);

// --- anim/*.rays (pedAnimationInstance::Load) -----------------------------------
//
//   <boneCount>             must equal the skeleton's, else MM2 ignores the file
//   boneCount rows: f32 f32 f32 i32 i32   (two per-bone floats, a third float,
//                                          two per-bone bytes; meaning unknown)
//   one row of boneCount i32 per shader variant (bytes)
struct PedRays {
    struct Row {
        Vec3 values;
        int a = 0;
        int b = 0;
    };
    std::vector<Row> bones;
    std::vector<std::vector<int>> variants;
};

std::optional<PedRays> parsePedRays(std::string_view text, std::string* error = nullptr);

// --- anim/*.remap (meaning unknown): "<count>" then <count> integers -----------
// MM2 never reads this file (there is no "remap" in the executable).
std::optional<std::vector<int>> parsePedRemap(std::string_view text, std::string* error = nullptr);

// --- Animation state table (anim/pedmodel_*.csv) --------------------------------
//
// Columns: anim name, mma name, first frame, last frame, Y AXIS Offset,
// Y AXIS DISTANCE, X AXIS Offset, X AXIS DISTANCE, default next (optional).
// Lines starting with '#' are comments. Transitions are named FROM_TO.
// pedAnimation::Load splits lines with strtok (empty fields between commas
// are skipped) and reads numbers with atoi/atof.
struct PedAnimState {
    std::string name;     // "WALK", "WALK_STAND"
    std::string animFile; // "pedanim_manwalk" -> anim/pedanim_manwalk.anim
    int firstFrame = 1;   // 1-based, inclusive
    int lastFrame = 1;    // may exceed the .anim frame count by one; clamp
    // Root motion, positive forward (-Z) and to the left (-X). The distances
    // match the animations' root translation; the offsets are approximately
    // the start position of the source animation (inferred).
    float forwardOffset = 0.0f;
    float forwardDistance = 0.0f;
    float sideOffset = 0.0f;
    float sideDistance = 0.0f;
    std::string next; // default next state
};

struct PedAnimTable {
    std::vector<PedAnimState> states;
    const PedAnimState* find(std::string_view name) const; // exact name, first match
};

std::optional<PedAnimTable> parsePedAnimTable(std::string_view text, std::string* error = nullptr);

// --- Whole pedestrian type ----------------------------------------------------------
struct PedType {
    std::string name; // "pedmodel_man"
    Skeleton skeleton;
    PedMesh mesh;
    PedShaderSet shaders;
    std::optional<PedRays> rays;
    std::vector<int> remap;
    PedAnimTable table;
    // Animations referenced by the table, keyed by lower-case file name.
    std::map<std::string, PedAnimation, std::less<>> animations;

    // Animation for a state name ("WALK") or an animation file name.
    const PedAnimation* animation(std::string_view stateOrFile) const;
};

// Loads anim/<name>.{skel,mod,csv} (required), .shaders/.rays/.remap
// (optional) and every animation the table references. The animations are
// the files' own data; normalizePedRoots turns them into what the game poses.
std::optional<PedType> loadPedType(std::string_view name, const ReadFileFn& read, std::string* error = nullptr);

// The root translations as MM2 changes them when a pedestrian type loads
// (pedAnimation::Load), so a sequence is posed at the pedestrian's origin and
// the pedestrian itself carries the motion:
//  - crAnimation::GetAnimation(name, normalize) runs crAnimation::Normalize
//    on an animation's first load: frame i's root z gains
//    i * cycleDistance / frameCount (the travel of the loop);
//  - then each table row, in file order, takes m = lastFrame - firstFrame
//    (at most frameCount - 1) and moves the root x and z of frames 0..m by
//    i * (v0 - vm) / m - v0, with v0 and vm read before the change: frame 0
//    and frame m end at 0. Rows sharing an animation adjust it in turn;
//    frames past m are left alone.
// MM2 keeps one copy of an animation for every type that names it (the
// rows of a later type adjust the shared, already adjusted data: a no-op
// for the retail tables up to float rounding); OpenMM2 adjusts each type's
// own copy.
void normalizePedRoots(PedType& type);

// Pedestrian type names present in a list of virtual paths (anim/<type>.mod).
std::vector<std::string> findPedTypes(const std::vector<std::string>& paths);

// Retail files that are broken as shipped (a stray text file named .anim).
bool isKnownBrokenPedAsset(std::string_view path);

} // namespace mm2::asset
