#pragma once

#include "city/Psdl.h"
#include "core/Math.h"

#include <vector>

namespace mm2::game {

// Finds the PSDL room containing a world position, using each room's
// perimeter polygon in the XZ plane (a uniform grid narrows the candidates).
// Where rooms overlap (bridges over roads, tunnels), the room whose floor is
// highest but still below the position wins.
class RoomLocator {
public:
    explicit RoomLocator(const city::Psdl& psdl, float cellSize = 32.0f);

    // Room id, or 0 when the position is outside every room.
    int find(const Vec3& position) const;

private:
    struct RoomShape {
        std::vector<Vec2> polygon;
        float minX = 0, minZ = 0, maxX = 0, maxZ = 0;
        float floorY = 0;
    };
    std::vector<RoomShape> m_rooms;
    float m_cell;
    float m_originX = 0, m_originZ = 0;
    int m_cols = 0, m_rows = 0;
    std::vector<std::vector<int>> m_grid;
};

} // namespace mm2::game
