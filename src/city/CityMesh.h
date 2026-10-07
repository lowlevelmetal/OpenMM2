#pragma once

// Triangle meshes generated from PSDL rooms, grouped per room and texture so
// the renderer can cull by room (with the CPVS) and batch by texture.
//
// How each attribute becomes geometry is documented in docs/formats/psdl.md.
// Geometry positions follow the file exactly; texture coordinates, curb and
// tunnel construction are reconstructions (the original generated them in
// code we cannot read) and are marked as such in the doc.

#include "city/Psdl.h"
#include "core/Math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mm2::city {

// What a batch represents, so renderer and physics can filter.
enum class SurfaceKind : std::uint8_t {
    Road,     // road strips, intersection fans, divided road lanes
    Sidewalk, // raised sidewalk tops
    Curb,     // vertical curb faces
    Crosswalk,
    Ground, // generic fans and rectangle strips (parks, plazas, tunnel floors)
    Wall,   // building facades and slivers
    Roof,
    Divider,     // road medians
    Tunnel,      // tunnel walls/ceilings, bridge railings
    FacadeBound, // invisible building walls (collision only)
};
const char* surfaceKindName(SurfaceKind k);

struct CityVertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv; // origin top-left (Direct3D convention); repeats beyond [0,1]
};

struct CityBatch {
    int texture = -1; // index into Psdl::textures, -1 = untextured
    SurfaceKind kind = SurfaceKind::Ground;
    std::vector<CityVertex> vertices;
    std::vector<std::uint32_t> indices; // triangle list, counter-clockwise front faces
};

struct CityRoomMesh {
    std::vector<CityBatch> batches;
    Aabb bounds;
};

struct CityMesh {
    std::vector<CityRoomMesh> rooms; // indexed by PSDL room id (room 0 empty)

    std::size_t triangleCount() const;
    std::size_t vertexCount() const;
};

struct CityMeshOptions {
    bool includeFacadeBounds = false; // emit invisible FacadeBound walls
    bool includeUntextured = true;    // geometry whose texture slot is "none"
};

CityMesh buildCityMesh(const Psdl& psdl, const CityMeshOptions& options = {});

// Builds the mesh for a single room (used by buildCityMesh).
CityRoomMesh buildRoomMesh(const Psdl& psdl, std::size_t room, const CityMeshOptions& options = {});

} // namespace mm2::city
