#include "ai/MapView.h"

#include "ai/Course.h"
#include "ai/Driving.h"
#include "ai/PathGeometry.h"

namespace mm2::ai {

MapView::MapView(const RoadNetwork& net) : m_net(net) { buildRooms(); }

const city::AiPath* MapView::path(int id) const {
    const city::AiMap* map = m_net.source();
    if (!map || id < 0 || static_cast<std::size_t>(id) >= map->paths.size())
        return nullptr;
    return &map->paths[static_cast<std::size_t>(id)];
}

const PathInfo* MapView::pathInfo(int id) const {
    if (id < 0 || static_cast<std::size_t>(id) >= m_net.paths().size())
        return nullptr;
    return &m_net.paths()[static_cast<std::size_t>(id)];
}

const Intersection* MapView::intersection(int id) const {
    if (id < 0 || static_cast<std::size_t>(id) >= m_net.intersections().size())
        return nullptr;
    return &m_net.intersections()[static_cast<std::size_t>(id)];
}

void MapView::setRoomFinder(RoomFinder finder) {
    m_findRoom = std::move(finder);
    buildRooms();
}

int MapView::findRoom(const Vec3& position, int hint) const {
    if (m_findRoom)
        return m_findRoom(position, hint);
    // OpenMM2, for maps without a PSDL (tests, tools): every intersection and
    // every road is a room of its own, 1 + the intersection's index, then
    // 1 + the number of intersections + the road's id; 0 off them all.
    const RoadSpot spot = locateOnRoads(m_net, position);
    const auto& nodes = m_net.intersections();
    for (std::size_t i = 0; spot.intersection >= 0 && i < nodes.size(); ++i)
        if (nodes[i].id == spot.intersection)
            return 1 + static_cast<int>(i);
    if (spot.path >= 0 && posOnRoad(m_net, spot.path, position, 0.0f) < 3)
        return 1 + static_cast<int>(nodes.size()) + spot.path;
    return 0;
}

const std::vector<RoomComponent>& MapView::components(int room) const {
    static const std::vector<RoomComponent> none;
    if (room < 0 || static_cast<std::size_t>(room) >= m_rooms.size())
        return none;
    return m_rooms[static_cast<std::size_t>(room)];
}

void MapView::buildRooms() {
    m_rooms.clear();
    const city::AiMap* map = m_net.source();
    if (!map)
        return;
    auto add = [&](int room, int id, int type) {
        if (room < 0)
            return;
        if (static_cast<std::size_t>(room) >= m_rooms.size())
            m_rooms.resize(static_cast<std::size_t>(room) + 1);
        auto& list = m_rooms[static_cast<std::size_t>(room)];
        for (const RoomComponent& c : list)
            if (c.type == type && c.id == id)
                return;
        list.push_back({id, type});
    };
    if (!m_findRoom) {
        // The rooms of findRoom's fallback.
        const auto& nodes = m_net.intersections();
        for (std::size_t i = 0; i < nodes.size(); ++i)
            add(1 + static_cast<int>(i), nodes[i].id, kIntersectionComponent);
        for (std::size_t p = 0; p < map->paths.size(); ++p)
            add(1 + static_cast<int>(nodes.size() + p), static_cast<int>(p), kRoadComponent);
        return;
    }
    // aiMap::ReadBinary: each road (aiMap::MapRoadToRooms, type 1) by the
    // rooms of its centre vertices 1 .. n - 2, then each intersection in its
    // own room.
    for (std::size_t p = 0; p < map->paths.size(); ++p) {
        const city::AiPath& path = map->paths[p];
        const int n = static_cast<int>(path.center.size());
        for (int v = 1; v < n - 1; ++v)
            add(findRoom(path.center[static_cast<std::size_t>(v)], 0), static_cast<int>(p), kRoadComponent);
    }
    for (const Intersection& node : m_net.intersections())
        add(node.room, node.id, kIntersectionComponent);
}

int MapView::mapComponent(const Vec3& pos, int& id, int& type, int roomHint) const {
    const int room = findRoom(pos, roomHint);
    const auto& list = components(room);
    for (const RoomComponent& c : list) {
        if (c.type == kRoadComponent || c.type == kIntersectionComponent) {
            type = c.type;
            id = c.id;
            return room;
        }
    }
    for (const RoomComponent& c : list) {
        if (c.type != kShortcutComponent)
            continue;
        if (const city::AiPath* p = path(c.id); p && pathIsPosOnRoad(*p, pos, 0.0f) < 3) {
            type = c.type;
            id = c.id;
            return room;
        }
    }
    type = kNoComponent;
    id = room;
    return room;
}

int MapView::mapComponent(const Vec3& pos, int& id, int& type, int roomHint, int roadHint) const {
    const int room = findRoom(pos, roomHint);
    const auto& list = components(room);
    if (roadHint >= 0) {
        for (int wanted : {static_cast<int>(kRoadComponent), static_cast<int>(kShortcutComponent)}) {
            for (const RoomComponent& c : list) {
                if (c.id == roadHint && c.type == wanted) {
                    type = c.type;
                    id = c.id;
                    return room;
                }
            }
        }
    }
    for (const RoomComponent& c : list) {
        if (c.type == kIntersectionComponent) {
            type = c.type;
            id = c.id;
            return room;
        }
    }
    for (const RoomComponent& c : list) {
        if (c.type != kShortcutComponent && c.type != kRoadComponent)
            continue;
        if (const city::AiPath* p = path(c.id); p && pathIsPosOnRoad(*p, pos, 0.0f) < 3) {
            type = c.type;
            id = c.id;
            return room;
        }
    }
    type = kNoComponent;
    return room;
}

int MapView::coreMapComponent(const Vec3& pos, int& id, int& type, int roomHint, int preferredRoad) const {
    const int room = findRoom(pos, roomHint);
    const auto& list = components(room);
    if (preferredRoad >= 0) {
        for (const RoomComponent& c : list) {
            if (c.id == preferredRoad && c.type == kRoadComponent) {
                type = c.type;
                id = c.id;
                return room;
            }
        }
    }
    for (const RoomComponent& c : list) {
        if (c.type == kIntersectionComponent) {
            type = c.type;
            id = c.id;
            return room;
        }
    }
    for (const RoomComponent& c : list) {
        if (c.type != kRoadComponent)
            continue;
        if (const city::AiPath* p = path(c.id); p && pathIsPosOnRoad(*p, pos, 0.0f) < 3) {
            type = c.type;
            id = c.id;
            return room;
        }
    }
    type = kNoComponent;
    return room;
}

int MapView::roadBetween(int from, int to, bool* forward) const {
    const Intersection* a = intersection(from);
    const Intersection* b = intersection(to);
    if (!a || !b)
        return -1;
    for (int p : a->paths) {
        for (int q : b->paths) {
            if (p != q)
                continue;
            const PathInfo* info = pathInfo(p);
            if (forward)
                *forward = info && info->intersection[1] == from;
            return p;
        }
    }
    return -1;
}

void MapView::trackPlayer(TrackedCar& car) {
    PlayerTrack& t = m_players[car.id];
    int id = 0, type = kNoComponent;
    t.room = mapComponent(car.position, id, type, t.room);
    if (type == kRoadComponent) {
        if (const city::AiPath* p = path(id)) {
            t.road = id;
            t.vert = pathRoadVertice(*p, car.position, 1);
        }
    }
    car.playerRoad = t.road;
    car.playerVert = t.vert;
}

} // namespace mm2::ai
