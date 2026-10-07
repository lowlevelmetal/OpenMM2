#pragma once

// Traffic light cycling, after MM2's aiTrafficLightSet (Reset, Update,
// SetFourWay) and aiTrafficLightInstance.
//
// Each intersection with traffic lights owns one set holding a light per
// approaching road end whose rule is "traffic light", in the intersection's
// path order. One light is green at a time: green for 3 s, amber for 3 s,
// then red while the next light turns green. When every approach of the
// intersection has a light, a round ends with an all-red phase for
// pedestrians (6 s: WALK for 3 s, then the don't-walk signal for 3 s) before
// light 0 turns green again. Vehicles only enter on green.

#include "ai/RoadNetwork.h"

#include <cstdint>
#include <vector>

namespace mm2::ai {

// Values as stored by MM2 (aiTrafficLightInstance state).
enum class LightState : std::uint8_t {
    Red = 1,
    Amber = 2,
    Green = 3,
    Walk = 4,    // all red, pedestrians walk
    WalkEnd = 5, // all red, the walk signal off again
};

inline constexpr float kLightCycleSeconds = 6.0f; // aiTrafficLightSet ctor: 40C00000h
inline constexpr float kLightAmberSeconds = 3.0f; // Update: amber within 3 s of the cycle's end

// A light that just turned green, as MM2 reports it to the traffic
// (aiPath::ResetVehicleReactTicks): MM2 passes the intersection's path at the
// *light's* index, so `path` is that list entry (it is the light's own road
// only while every earlier road has a light).
struct GreenEvent {
    int intersection = 0;
    int path = -1;
};

class TrafficLights {
public:
    void build(const RoadNetwork& network);
    void reset();
    // Advances every set by `dt` seconds (aiTrafficLightSet::Update).
    void update(float dt);

    LightState state(int slot) const;
    std::size_t size() const { return m_states.size(); }

    // Debug overrides (aiMap::AllwaysGreen / AllwaysRed).
    void forceAll(LightState state);

    // Lights that turned green in the last update.
    const std::vector<GreenEvent>& newGreens() const { return m_newGreen; }

    // The set of an intersection, for pedestrians (aiPedestrian crossing):
    // whether it has one, its kind, whether it is in the walk phase, and the
    // state of its first light.
    bool hasLights(int intersection) const { return setOf(intersection) != nullptr; }
    LightCycle cycleAt(int intersection) const;
    bool walkPhaseAt(int intersection) const;
    LightState firstLightAt(int intersection) const;

private:
    struct Set {
        int intersection = 0;
        std::vector<int> slots;
        std::vector<int> paths; // the intersection's path list
        LightCycle cycle = LightCycle::Rotate;
        int current = 0;
        bool walkPhase = false;
        float timer = 0.0f;
    };
    void resetSet(Set& set);
    void setState(const Set& set, int light, LightState state);
    int pairedLight(const Set& set) const; // the opposite light of a four-way set
    void turnGreen(Set& set);
    const Set* setOf(int intersection) const;

    std::vector<Set> m_sets;
    std::vector<int> m_setOfNode; // intersection -> index into m_sets, -1 = none
    std::vector<LightState> m_states;
    std::vector<GreenEvent> m_newGreen;
    bool m_forced = false;
};

} // namespace mm2::ai
