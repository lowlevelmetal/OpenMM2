#pragma once

#include "city/Psdl.h"
#include "core/Math.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace mm2::city {

// MM2's room lookup (cityLevel::FindRoomId, FullProbe, InitFullProbe and the
// warps cityLevel::Load sets up): the PSDL room whose perimeter contains a
// position in the XZ plane.
//
// A room flagged Warp (tunnels, bridges) also needs the height inside its
// span (cityLevel::Load: subterranean rooms reach from -1000 to their
// highest perimeter point plus 7 m, or the tunnel's height; other rooms
// from 1 m below their lowest perimeter point to 1000). An ordinary room a
// warp room overlaps lists it as a warp and does not claim positions that
// are inside the warp room.
class RoomLocator {
public:
    // `cityName` is the map name ("london" adds MM2's four hand-made warps).
    explicit RoomLocator(const Psdl& psdl, std::string_view cityName = {});

    // cityLevel::FindRoomId: the `hint` room (the caller's last room) if the
    // position is in it, else the highest-numbered of its neighbours that
    // holds it, else the last room of the 64 x 64 grid cell that holds it
    // (FullProbe); 0 when none does.
    int find(const Vec3& position, int hint = 0) const;

private:
    struct Room {
        std::vector<std::uint16_t> perimeter; // PSDL vertex indices
        std::vector<std::uint16_t> neighbors; // room across each perimeter edge
        std::uint8_t flags = 0;
        float minY = 0.0f, maxY = 0.0f; // lvlRoomInfo +0x20 / +0x24
        std::vector<int> warps;
    };
    bool pointInPerimeter(const Room& r, float x, float z) const;
    bool inRoom(int room, const Vec3& p) const;
    int fullProbe(const Vec3& p) const;

    std::vector<Vec3> m_points; // the PSDL vertices
    std::vector<Room> m_rooms;
    float m_originX = 0, m_originZ = 0, m_scaleX = 0, m_scaleZ = 0;
    std::vector<std::vector<int>> m_cells; // 64 x 64, room ids in ascending order
};

} // namespace mm2::city
