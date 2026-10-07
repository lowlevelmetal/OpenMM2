#include "ai/TrafficLights.h"

namespace mm2::ai {

void TrafficLights::build(const RoadNetwork& network) {
    m_sets.clear();
    m_states.assign(network.lights().size(), LightState::Red);
    for (const auto& node : network.intersections()) {
        if (node.lights.empty())
            continue;
        Set set;
        set.slots = node.lights;
        m_sets.push_back(std::move(set));
    }
    reset();
}

void TrafficLights::reset() {
    m_forced = false;
    m_newGreen.clear();
    for (auto& set : m_sets) {
        set.current = 0;
        set.timer = 0.0f;
        // First light green, the rest red (aiTrafficLightSet::Reset).
        for (std::size_t i = 0; i < set.slots.size(); ++i)
            m_states[static_cast<std::size_t>(set.slots[i])] = i == 0 ? LightState::Green : LightState::Red;
    }
}

void TrafficLights::update(float dt) {
    m_newGreen.clear();
    if (m_forced)
        return;
    for (auto& set : m_sets) {
        set.timer += dt;
        if (set.timer > kLightCycleSeconds) {
            m_states[static_cast<std::size_t>(set.slots[static_cast<std::size_t>(set.current)])] =
                LightState::Red;
            ++set.current;
            set.timer = 0.0f;
            if (set.current == static_cast<int>(set.slots.size()))
                set.current = 0;
            const int slot = set.slots[static_cast<std::size_t>(set.current)];
            m_states[static_cast<std::size_t>(slot)] = LightState::Green;
            m_newGreen.push_back(slot);
        } else if (set.timer > kLightCycleSeconds - kLightAmberSeconds) {
            m_states[static_cast<std::size_t>(set.slots[static_cast<std::size_t>(set.current)])] =
                LightState::Amber;
        }
    }
}

LightState TrafficLights::state(int slot) const {
    if (slot < 0 || static_cast<std::size_t>(slot) >= m_states.size())
        return LightState::Green;
    return m_states[static_cast<std::size_t>(slot)];
}

void TrafficLights::forceAll(LightState state) {
    m_forced = true;
    for (auto& s : m_states)
        s = state;
}

} // namespace mm2::ai
