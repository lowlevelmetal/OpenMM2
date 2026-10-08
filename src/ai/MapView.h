#pragma once

// The parts of MM2's aiMap the AI drivers use (build 3393, MM2Recomp; see
// docs/parity/ai-vehicles.md): the roads and intersections by id, the
// components (roads, shortcut roads and intersections) listed per PSDL room,
// and the lookups built on them.
//
// aiMap +0x190 holds one component list per room, built as the map loads:
// aiMap::MapRoadToRooms adds each road (type 1) to the rooms of its centre
// vertices 1 to n - 2, in road order; then every intersection (type 3) is
// added to its own room. A position's components are those of the room
// lvlLevel::FindRoomId finds for it (the game supplies the lookup; without
// one, for maps that have no PSDL, every road and intersection is taken to
// be a room of its own).

#include "ai/RoadNetwork.h"
#include "core/Math.h"

#include <functional>
#include <unordered_map>
#include <vector>

namespace mm2::ai {

class Traffic;
struct TrackedCar;

// Map component types (aiMap::MapComponent).
enum ComponentType : int {
    kNoComponent = 0,
    kRoadComponent = 1,
    kShortcutComponent = 2,
    kIntersectionComponent = 3,
};

struct RoomComponent {
    int id = 0;
    int type = 0;
};

class MapView {
public:
    using RoomFinder = std::function<int(const Vec3& position, int hint)>;

    explicit MapView(const RoadNetwork& net);

    const RoadNetwork& net() const { return m_net; }
    const city::AiMap& map() const { return *m_net.source(); }
    // aiMap::Path / aiMap::Intersection: null outside the map.
    const city::AiPath* path(int id) const;
    const PathInfo* pathInfo(int id) const;
    const Intersection* intersection(int id) const;

    // lvlLevel::FindRoomId; rebuilds the room component lists.
    void setRoomFinder(RoomFinder finder);
    int findRoom(const Vec3& position, int hint) const;
    const std::vector<RoomComponent>& components(int room) const;

    // aiMap::MapComponentType(room, &id): the room's first road or
    // intersection (type 1 or 3, its id), else none with id = the room.
    int mapComponentType(int room, int& id) const;
    // aiMap::MapComponent(pos, &id, &type, room): the room's first road or
    // intersection, else a shortcut road the position is on or next to
    // (IsPosOnRoad < 3), else none with id = the room. Returns the room.
    int mapComponent(const Vec3& pos, int& id, int& type, int roomHint) const;
    // aiMap::MapComponent(pos, &id, &type, room, road): the road `roadHint`
    // (road, then shortcut) when the room lists it, else the room's first
    // intersection, else a road or shortcut the position is on or next to,
    // else none (id left alone). Returns the room.
    int mapComponent(const Vec3& pos, int& id, int& type, int roomHint, int roadHint) const;
    // aiMap::CoreMapComponent: as the above with roads only (no shortcuts).
    int coreMapComponent(const Vec3& pos, int& id, int& type, int roomHint, int preferredRoad) const;

    // aiMap::DetRdSegBetweenInts: the first road of `from`'s list that `to`
    // lists too (`from` itself: its first road), with `forward` set when it
    // leaves `from` at its vertex 0 (from is its end-1 intersection); -1
    // when either is not an intersection.
    int roadBetween(int from, int to, bool* forward) const;

    // The ambient traffic, whose cars are the obstacle lists' entries and
    // whose intersections the racers hold (aiIntersection::StopSources).
    void setTraffic(Traffic* traffic) { m_traffic = traffic; }
    Traffic* traffic() const { return m_traffic; }

    // [Ambients Drive On The Left] of the city.
    bool driveOnLeft() const { return m_net.driveOnLeft(); }

    // aiVehiclePlayer::Update, once a frame for each player: the road the
    // player is on (aiMap::MapComponent) and the vertex ahead of it there,
    // counted from vertex 0; both kept while the player is in an
    // intersection or off the roads. Fills `car`'s playerRoad / playerVert.
    void trackPlayer(TrackedCar& car);

private:
    void buildRooms();

    const RoadNetwork& m_net;
    RoomFinder m_findRoom;
    std::vector<std::vector<RoomComponent>> m_rooms;
    Traffic* m_traffic = nullptr;
    struct PlayerTrack {
        int room = 0;
        int road = -1;
        int vert = 0;
    };
    std::unordered_map<int, PlayerTrack> m_players;
};

} // namespace mm2::ai
