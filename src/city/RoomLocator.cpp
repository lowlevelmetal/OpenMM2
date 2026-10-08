// Room lookup after MM2's cityLevel (build 3393, MM2Recomp; documentation
// only): FindRoomId, FullProbe, InitFullProbe, IsInRoomCheckWarps, AddWarp
// and the room height spans and warps of cityLevel::Load.
#include "city/RoomLocator.h"

#include "core/StringUtil.h"

#include <algorithm>
#include <cmath>

namespace mm2::city {
namespace {

constexpr int kProbeCells = 64; // cityLevel::Load: InitFullProbe(64, 64)
constexpr std::size_t kMaxRoomsPerCell = 0x80;

// cityLevel::Load samples each warp room's perimeter edges at these
// fractions (besides both ends) against the other rooms.
constexpr float kWarpEdgeSamples[] = {0.33f, 0.66f, 0.5f, 0.16f, 0.86f};

// x87 ftol: truncation towards zero.
int ftol(float v) {
    return static_cast<int>(v);
}

} // namespace

RoomLocator::RoomLocator(const Psdl& psdl, std::string_view cityName) {
    m_points = psdl.vertices;
    const auto& verts = m_points;
    m_rooms.resize(psdl.rooms.size());
    for (std::size_t r = 1; r < psdl.rooms.size(); ++r) {
        const PsdlRoom& src = psdl.rooms[r];
        Room& room = m_rooms[r];
        room.flags = src.flags;
        for (const auto& p : src.perimeter) {
            if (p.vertex >= verts.size())
                continue; // OpenMM2 guard: retail perimeters are in range
            room.perimeter.push_back(p.vertex);
            room.neighbors.push_back(p.neighbor);
        }
        if (room.perimeter.empty())
            continue;
        // cityLevel::Load: the room's height span (lvlRoomInfo MinY/MaxY).
        if (room.flags & RoomFlag::Subterranean) {
            room.minY = -1000.0f;
            room.maxY = verts[room.perimeter[0]].y;
            for (std::size_t k = 1; k < room.perimeter.size(); ++k)
                if (verts[room.perimeter[k]].y > room.maxY)
                    room.maxY = verts[room.perimeter[k]].y;
            if (room.flags & RoomFlag::Standard) {
                room.maxY += 7.0f;
            } else {
                // A tunnel (after an optional texture attribute) adds its
                // height (the attribute's fourth word, 8.8 fixed point).
                std::size_t a = 0;
                if (a < src.attributes.size() && src.attributes[a].type == PsdlAttrType::Texture)
                    ++a;
                if (a < src.attributes.size() && src.attributes[a].type == PsdlAttrType::Tunnel &&
                    src.attributes[a].args.size() > 2)
                    room.maxY += static_cast<float>(src.attributes[a].args[2]) * 0.00390625f;
                else
                    room.maxY += 7.0f;
            }
        } else {
            room.maxY = 1000.0f;
            room.minY = verts[room.perimeter[0]].y;
            for (std::size_t k = 1; k < room.perimeter.size(); ++k)
                if (verts[room.perimeter[k]].y < room.minY)
                    room.minY = verts[room.perimeter[k]].y;
            room.minY -= 1.0f;
        }
    }

    // cityLevel::InitFullProbe(64, 64): a grid over the extent of every
    // room's perimeter; each room is listed (in id order) in the cells its
    // perimeter's bounds overlap.
    float minX = 1e6f, minZ = 1e6f, maxX = -1e6f, maxZ = -1e6f;
    for (std::size_t r = 1; r < m_rooms.size(); ++r)
        for (auto v : m_rooms[r].perimeter) {
            const Vec3& p = verts[v];
            if (p.x < minX)
                minX = p.x;
            if (p.z < minZ)
                minZ = p.z;
            if (maxX < p.x)
                maxX = p.x;
            if (maxZ < p.z)
                maxZ = p.z;
        }
    m_originX = minX;
    m_originZ = minZ;
    m_scaleX = static_cast<float>(kProbeCells) / (maxX - minX);
    m_scaleZ = static_cast<float>(kProbeCells) / (maxZ - minZ);
    m_cells.assign(static_cast<std::size_t>(kProbeCells * kProbeCells), {});
    for (std::size_t r = 1; r < m_rooms.size(); ++r) {
        float rMinX = 1e6f, rMinZ = 1e6f, rMaxX = -1e6f, rMaxZ = -1e6f;
        for (auto v : m_rooms[r].perimeter) {
            const Vec3& p = verts[v];
            if (p.x < rMinX)
                rMinX = p.x;
            if (p.z < rMinZ)
                rMinZ = p.z;
            if (rMaxX < p.x)
                rMaxX = p.x;
            if (rMaxZ < p.z)
                rMaxZ = p.z;
        }
        const int x0 = ftol((rMinX - m_originX) * m_scaleX);
        const int z0 = ftol((rMinZ - m_originZ) * m_scaleZ);
        const int x1 = ftol(std::ceil((rMaxX - m_originX) * m_scaleX));
        const int z1 = ftol(std::ceil((rMaxZ - m_originZ) * m_scaleZ));
        for (int z = z0; z < z1; ++z)
            for (int x = x0; x < x1; ++x) {
                // OpenMM2 guard: MM2 writes past its grid (and quits on a
                // full cell); retail rooms stay inside.
                if (x < 0 || z < 0 || x >= kProbeCells || z >= kProbeCells)
                    continue;
                auto& cell = m_cells[static_cast<std::size_t>(z * kProbeCells + x)];
                if (cell.size() < kMaxRoomsPerCell)
                    cell.push_back(static_cast<int>(r));
            }
    }

    // Warps (cityLevel::Load, AddWarp): an ordinary room containing either
    // end or one of the sample points of any perimeter edge of a warp room
    // gets that warp room.
    auto addWarp = [&](std::size_t room, int warp) {
        if (room < m_rooms.size() && std::ranges::find(m_rooms[room].warps, warp) == m_rooms[room].warps.end())
            m_rooms[room].warps.push_back(warp);
    };
    for (std::size_t w = 1; w < m_rooms.size(); ++w) {
        const Room& warp = m_rooms[w];
        if (!(warp.flags & RoomFlag::Warp))
            continue;
        const std::size_t n = warp.perimeter.size();
        for (std::size_t i = 0; i < n; ++i) {
            const Vec3& a = verts[warp.perimeter[i]];
            const Vec3& b = verts[warp.perimeter[(i + 1) % n]];
            std::vector<Vec2> points = {{a.x, a.z}, {b.x, b.z}};
            for (float t : kWarpEdgeSamples)
                points.push_back({(b.x - a.x) * t + a.x, (b.z - a.z) * t + a.z});
            for (std::size_t j = 1; j < m_rooms.size(); ++j) {
                if (m_rooms[j].flags & RoomFlag::Warp)
                    continue;
                for (const Vec2& p : points)
                    if (pointInPerimeter(m_rooms[j], p.x, p.y)) {
                        addWarp(j, static_cast<int>(w));
                        break;
                    }
            }
        }
    }
    // MM2's hand-made London warps (the city name contains "london").
    if (str::lower(cityName).find("london") != std::string::npos)
        for (int room : {597, 599, 600, 1297})
            addWarp(static_cast<std::size_t>(room), 382);
}

// sdlPage16::PointInPerimeter: crossings of the perimeter edges to the
// right of the point (a repeated vertex is stepped over).
bool RoomLocator::pointInPerimeter(const Room& r, float x, float z) const {
    const auto& verts = m_points;
    const int n = static_cast<int>(r.perimeter.size());
    bool inside = false;
    int prev = n - 1;
    for (int i = 0; i < n; ++i) {
        if (r.perimeter[static_cast<std::size_t>(prev)] == r.perimeter[static_cast<std::size_t>(i)]) {
            if (i == n - 1)
                return inside;
            ++i;
        }
        const Vec3& a = verts[r.perimeter[static_cast<std::size_t>(i)]];
        const Vec3& b = verts[r.perimeter[static_cast<std::size_t>(prev)]];
        if (((a.z <= z && z < b.z) || (b.z <= z && z < a.z)) && x < (b.x - a.x) * (z - a.z) / (b.z - a.z) + a.x)
            inside = !inside;
        prev = i;
    }
    return inside;
}

// FindRoomId's test of one room: inside its perimeter and, for a warp room,
// within its height span; for an ordinary room, not inside one of its warps
// (cityLevel::IsInRoomCheckWarps).
bool RoomLocator::inRoom(int room, const Vec3& p) const {
    const Room& r = m_rooms[static_cast<std::size_t>(room)];
    if (r.perimeter.empty() || !pointInPerimeter(r, p.x, p.z))
        return false;
    if (r.flags & RoomFlag::Warp)
        return r.minY <= p.y && p.y <= r.maxY;
    for (int w : r.warps) {
        const Room& warp = m_rooms[static_cast<std::size_t>(w)];
        if (warp.minY <= p.y && p.y <= warp.maxY && pointInPerimeter(warp, p.x, p.z))
            return false;
    }
    return true;
}

// cityLevel::FullProbe: the last room of the position's grid cell that holds
// it.
int RoomLocator::fullProbe(const Vec3& p) const {
    if (m_cells.empty())
        return 0;
    const int x = ftol((p.x - m_originX) * m_scaleX);
    const int z = ftol((p.z - m_originZ) * m_scaleZ);
    if (x < 0 || x >= kProbeCells || z < 0 || z >= kProbeCells)
        return 0;
    int found = 0;
    for (int room : m_cells[static_cast<std::size_t>(z * kProbeCells + x)])
        if (inRoom(room, p))
            found = room;
    return found;
}

int RoomLocator::find(const Vec3& p, int hint) const {
    if (hint > 0 && static_cast<std::size_t>(hint) < m_rooms.size()) {
        if (inRoom(hint, p))
            return hint;
        // Its neighbours across the perimeter edges, each once.
        std::vector<int> seen{hint};
        int best = 0;
        for (int nb : m_rooms[static_cast<std::size_t>(hint)].neighbors) {
            if (nb == 0 || static_cast<std::size_t>(nb) >= m_rooms.size() || std::ranges::find(seen, nb) != seen.end())
                continue;
            if (inRoom(nb, p) && best <= nb)
                best = nb;
            seen.push_back(nb);
        }
        if (best != 0)
            return best;
    }
    return fullProbe(p);
}

} // namespace mm2::city
