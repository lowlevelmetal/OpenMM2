#include "city/RoomLocator.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mm2::city {
namespace {

bool pointInPolygon(const std::vector<Vec2>& poly, float x, float z) {
    bool inside = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Vec2 a = poly[i], b = poly[j];
        if ((a.y > z) != (b.y > z) && x < (b.x - a.x) * (z - a.y) / (b.y - a.y) + a.x)
            inside = !inside;
    }
    return inside;
}

} // namespace

RoomLocator::RoomLocator(const Psdl& psdl, float cellSize) : m_cell(cellSize) {
    m_rooms.resize(psdl.rooms.size());
    float minX = std::numeric_limits<float>::max(), minZ = minX;
    float maxX = std::numeric_limits<float>::lowest(), maxZ = maxX;
    for (std::size_t r = 1; r < psdl.rooms.size(); ++r) {
        auto& shape = m_rooms[r];
        float floor = std::numeric_limits<float>::max();
        for (const auto& p : psdl.rooms[r].perimeter) {
            if (p.vertex >= psdl.vertices.size())
                continue;
            const Vec3 v = psdl.vertices[p.vertex];
            shape.polygon.push_back({v.x, v.z});
            floor = std::min(floor, v.y);
        }
        if (shape.polygon.size() < 3) {
            shape.polygon.clear();
            continue;
        }
        shape.floorY = floor;
        shape.minX = shape.maxX = shape.polygon[0].x;
        shape.minZ = shape.maxZ = shape.polygon[0].y;
        for (const auto& v : shape.polygon) {
            shape.minX = std::min(shape.minX, v.x);
            shape.maxX = std::max(shape.maxX, v.x);
            shape.minZ = std::min(shape.minZ, v.y);
            shape.maxZ = std::max(shape.maxZ, v.y);
        }
        minX = std::min(minX, shape.minX);
        minZ = std::min(minZ, shape.minZ);
        maxX = std::max(maxX, shape.maxX);
        maxZ = std::max(maxZ, shape.maxZ);
    }
    if (minX > maxX)
        return;
    m_originX = minX;
    m_originZ = minZ;
    m_cols = static_cast<int>((maxX - minX) / m_cell) + 1;
    m_rows = static_cast<int>((maxZ - minZ) / m_cell) + 1;
    m_grid.resize(static_cast<std::size_t>(m_cols) * m_rows);
    for (std::size_t r = 1; r < m_rooms.size(); ++r) {
        const auto& s = m_rooms[r];
        if (s.polygon.empty())
            continue;
        const int c0 = static_cast<int>((s.minX - m_originX) / m_cell), c1 = static_cast<int>((s.maxX - m_originX) / m_cell);
        const int r0 = static_cast<int>((s.minZ - m_originZ) / m_cell), r1 = static_cast<int>((s.maxZ - m_originZ) / m_cell);
        for (int z = r0; z <= r1; ++z)
            for (int x = c0; x <= c1; ++x)
                m_grid[static_cast<std::size_t>(z) * m_cols + x].push_back(static_cast<int>(r));
    }
}

int RoomLocator::find(const Vec3& p) const {
    if (m_grid.empty())
        return 0;
    const int cx = static_cast<int>(std::floor((p.x - m_originX) / m_cell));
    const int cz = static_cast<int>(std::floor((p.z - m_originZ) / m_cell));
    if (cx < 0 || cz < 0 || cx >= m_cols || cz >= m_rows)
        return 0;
    int best = 0;
    float bestFloor = std::numeric_limits<float>::lowest();
    int fallback = 0;
    float fallbackFloor = std::numeric_limits<float>::max();
    for (int r : m_grid[static_cast<std::size_t>(cz) * m_cols + cx]) {
        const auto& s = m_rooms[static_cast<std::size_t>(r)];
        if (p.x < s.minX || p.x > s.maxX || p.z < s.minZ || p.z > s.maxZ || !pointInPolygon(s.polygon, p.x, p.z))
            continue;
        if (s.floorY <= p.y + 1.0f) {
            if (s.floorY > bestFloor) {
                bestFloor = s.floorY;
                best = r;
            }
        } else if (s.floorY < fallbackFloor) {
            fallbackFloor = s.floorY;
            fallback = r;
        }
    }
    return best ? best : fallback;
}

} // namespace mm2::city
