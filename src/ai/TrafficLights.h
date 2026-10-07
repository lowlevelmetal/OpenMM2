#pragma once

// Traffic light cycling, ported from MM1's aiTrafficLightSet (Open1560
// game.asm: aiTrafficLightSet::Reset / ::Update, constructor constants).
//
// Each intersection with traffic lights owns one set holding a light per
// approaching road (one per path end whose rule is "traffic light"). Exactly
// one light of a set is green at a time: it stays green for the cycle (10 s),
// switches to amber for the last 4 s, then turns red and the next light in
// the set turns green. Vehicles only enter on green.

#include "ai/RoadNetwork.h"

#include <cstdint>
#include <vector>

namespace mm2::ai {

// Values as stored by the original (aiTrafficLightInstance state).
enum class LightState : std::uint8_t { Red = 1, Amber = 2, Green = 3 };

inline constexpr float kLightCycleSeconds = 10.0f; // aiTrafficLightSet ctor: 41200000h
inline constexpr float kLightAmberSeconds = 4.0f;  // flt_61B474

class TrafficLights {
public:
    void build(const RoadNetwork& network);
    void reset();
    // `dt` scaled by the AI time scale, as the original multiplies the frame
    // time by aiMap's time factor.
    void update(float dt);

    LightState state(int slot) const;
    std::size_t size() const { return m_states.size(); }

    // Debug overrides (aiMap::AllwaysGreen / AllwaysRed).
    void forceAll(LightState state);

    // Called with an intersection whose current light just turned green, so
    // traffic can restart its reaction counters (aiPath::ResetVehicleReactTicks).
    template <class F>
    void forEachNewGreen(F&& f) {
        for (int slot : m_newGreen)
            f(slot);
    }

private:
    struct Set {
        std::vector<int> slots;
        int current = 0;
        float timer = 0.0f;
    };
    std::vector<Set> m_sets;
    std::vector<LightState> m_states;
    std::vector<int> m_newGreen;
    bool m_forced = false;
};

} // namespace mm2::ai
