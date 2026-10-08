#pragma once

// The rooms cityLevel::Draw lists for a view, from which cityLevel::DrawRooms
// draws the dynamic objects (see docs/parity/mm2/city-render.md).

#include "city/RoomLocator.h"
#include "core/Math.h"

#include <cstddef>
#include <limits>
#include <vector>

namespace mm2::game {

// cityLevel::DrawRooms draws a dynamic object (a car, a traffic car, a
// pedestrian, a signal, a prop) from the room lvlLevel::MoveToRoom keeps it
// in, and only while cityLevel::Draw lists that room for the view:
// cityLevel_drawObjects with the room's distance (its sphere's depth minus
// its radius) at most NoDraw, cityLevel_drawShadows and cityLevel_drawLights
// (shadows and glows, which do not ask the object's own IsVisible) under
// NoDraw. The objects keep their rooms as MM2 does: cityLevel::FindRoomId of
// their position from the room they were in (findRoom), each update.
//
// CityRenderer::draw lists the rooms of every view it draws; the renderers
// that draw after it in the same view read them. Until a city view has been
// listed (no city, tools, tests) every object passes.
class RoomVisibility {
public:
    // Which of DrawRooms' passes draw an object of a room.
    struct Passes {
        bool objects = true;         // cityLevel_drawObjects
        bool shadowsAndGlows = true; // cityLevel_drawShadows, cityLevel_drawLights
    };

    void begin(const city::RoomLocator* locator, std::size_t rooms, float noDraw) {
        m_locator = locator;
        m_distance.assign(rooms, kNotListed);
        m_noDraw = noDraw;
        m_active = true;
    }
    void list(int room, float distance) {
        if (room >= 0 && static_cast<std::size_t>(room) < m_distance.size())
            m_distance[static_cast<std::size_t>(room)] = distance;
    }

    bool active() const { return m_active; }

    Passes passes(int room) const {
        if (!m_active)
            return {};
        const float d = room >= 0 && static_cast<std::size_t>(room) < m_distance.size()
                            ? m_distance[static_cast<std::size_t>(room)]
                            : kNotListed;
        return {d <= m_noDraw, d < m_noDraw};
    }

    // cityLevel::FindRoomId: the room containing `position`, searched from
    // `hint` (the object's previous room); 0 outside every room, where
    // MoveToRoom leaves the object in room 0, which no view lists.
    int findRoom(const Vec3& position, int hint) const { return m_locator ? m_locator->find(position, hint) : 0; }

private:
    static constexpr float kNotListed = std::numeric_limits<float>::infinity();
    const city::RoomLocator* m_locator = nullptr;
    std::vector<float> m_distance;
    float m_noDraw = 0.0f;
    bool m_active = false;
};

} // namespace mm2::game
