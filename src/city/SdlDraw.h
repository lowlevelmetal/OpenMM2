#pragma once

// What sdlPage16::Draw draws for a PSDL room, at each of its four levels of
// detail, as triangle lists the renderer can batch by texture the way
// vglBeginBatch / vglEndBatch do. Ported from midtown2.exe build 3393
// (MM2Recomp); see docs/formats/psdl.md, "Drawing".
//
// Tunnel attributes are drawn at every level: a junction's walls along the
// room's masked perimeter edges, a strip tunnel's along the next road,
// divided road or rectangle strip.

#include "city/Psdl.h"
#include "core/Math.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace mm2::city {

// How sdlPage16::Draw colours what it draws (vglCurrentColor).
enum class SdlShade : std::uint8_t {
    Room,     // the room colour (cityLevel's per-room colour)
    HalfRoom, // half of it: curb faces and curb end caps
    Wall,     // the wall light table entry shaded by the room colour (GetShadedColor)
};

struct SdlDrawVertex {
    Vec3 position;
    Vec2 uv;
};

// One vglBegin / vglEnd primitive, as a triangle list.
struct SdlPrimitive {
    int texture = -1; // index into Psdl::textures, -1: none (untextured)
    SdlShade shade = SdlShade::Room;
    std::uint8_t light = 0; // SdlShade::Wall: sdlCommon's light table index
    // Facades and slivers are skipped while the camera is behind the wall
    // running from wall0 to wall1 (sdlCommon::BACKFACE).
    bool wall = false;
    Vec3 wall0, wall1;
    // Road fans, crosswalks and roofs are skipped while `height` is above
    // the camera's height.
    bool belowCamera = false;
    float height = 0.0f;
    std::uint32_t firstIndex = 0, indexCount = 0; // into SdlRoomDraw::indices
};

struct SdlRoomDraw {
    std::vector<SdlDrawVertex> vertices;
    std::vector<std::uint32_t> indices; // triangle lists of room-local vertex indices
    // What sdlPage16::Draw draws at each level of detail (0 the lowest, 3
    // the highest). A primitive that does not depend on the level is listed
    // in several lists with the same index range.
    std::array<std::vector<SdlPrimitive>, 4> lods;
};

SdlRoomDraw buildSdlRoomDraw(const Psdl& psdl, std::size_t room);

// sdlPage16::ArcMap: texture coordinates along a strip of `count` sections
// of `stride` indices (starting at `indices`): the distance along the
// sections' first vertices, scaled to a whole number of repeats of the
// strip's average width (first vertex to the one `column` further), and run
// back and forth.
std::vector<float> sdlArcMap(const Psdl& psdl, std::span<const std::uint16_t> indices, int stride, int count,
                             int column);

// sdlPage16::WallMap: as ArcMap, but whole repeats of `repeatLength` along
// the sections' first vertices (none when it is 0 or the strip is shorter
// than 0.1 m).
std::vector<float> sdlWallMap(const Psdl& psdl, std::span<const std::uint16_t> indices, float repeatLength,
                              int count, int stride);

// sdlPage16::GetCentroid: the area centroid of the room's perimeter in the
// ground plane, at the mean height of its corners.
Vec3 sdlRoomCentroid(const Psdl& psdl, std::size_t room);
// sdlPage16::ComputeBoundSphere (cityLevel::Load): the centroid and the
// distance of the farthest perimeter corner.
void sdlRoomBoundSphere(const Psdl& psdl, std::size_t room, Vec3& centre, float& radius);
// cityLevel::DrawRooms: the street level of detail of a room whose bounding
// sphere's view depth minus its radius is `distance` (the camera's room uses
// minus its radius).
int sdlRoomLod(float distance);
// sdlCommon::BACKFACE: whether the camera at `eye` is behind the wall from
// `a` to `b` (tested in the ground plane).
bool sdlBackface(const Vec3& eye, const Vec3& a, const Vec3& b);

} // namespace mm2::city
