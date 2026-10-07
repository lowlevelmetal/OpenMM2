// Traffic light sets after MM2's aiTrafficLightSet (build 3393); see
// TrafficLights.h and docs/ai.md.
#include "ai/TrafficLights.h"

namespace mm2::ai {

void TrafficLights::build(const RoadNetwork& network) {
    m_sets.clear();
    m_states.assign(network.lights().size(), LightState::Red);
    m_setOfNode.assign(network.intersections().size(), -1);
    for (const auto& node : network.intersections()) {
        if (node.lights.empty())
            continue;
        m_setOfNode[static_cast<std::size_t>(node.id)] = static_cast<int>(m_sets.size());
        Set set;
        set.intersection = node.id;
        set.slots = node.lights;
        set.paths = node.paths;
        set.cycle = node.cycle;
        m_sets.push_back(std::move(set));
    }
    reset();
}

void TrafficLights::setState(const Set& set, int light, LightState state) {
    if (light >= 0 && static_cast<std::size_t>(light) < set.slots.size())
        m_states[static_cast<std::size_t>(set.slots[static_cast<std::size_t>(light)])] = state;
}

int TrafficLights::pairedLight(const Set& set) const {
    return (set.current + 2) % 4;
}

void TrafficLights::resetSet(Set& set) {
    // aiTrafficLightSet::Reset: light 0 green (with light 2 in a four-way
    // set), the others red.
    set.current = 0;
    set.walkPhase = false;
    set.timer = 0.0f;
    for (std::size_t i = 0; i < set.slots.size(); ++i)
        setState(set, static_cast<int>(i), i == 0 ? LightState::Green : LightState::Red);
    if (set.cycle == LightCycle::FourWay)
        setState(set, 2, LightState::Green);
}

void TrafficLights::reset() {
    m_forced = false;
    m_newGreen.clear();
    for (auto& set : m_sets)
        resetSet(set);
}

void TrafficLights::turnGreen(Set& set) {
    // The new green light restarts the stopped queue of the intersection's
    // road at the light's index (aiPath::ResetVehicleReactTicks).
    auto report = [&](int light) {
        setState(set, light, LightState::Green);
        const int path = light >= 0 && static_cast<std::size_t>(light) < set.paths.size()
                             ? set.paths[static_cast<std::size_t>(light)]
                             : -1;
        m_newGreen.push_back({set.intersection, path});
    };
    report(set.current);
    if (set.cycle == LightCycle::FourWay)
        report(pairedLight(set));
}

void TrafficLights::update(float dt) {
    m_newGreen.clear();
    if (m_forced)
        return;
    for (auto& set : m_sets) {
        set.timer += dt;
        if (set.walkPhase) {
            // All red for pedestrians: WALK, then the don't-walk signal for
            // the last 3 s, then the round starts again.
            if (set.timer > kLightCycleSeconds)
                resetSet(set);
            else if (set.timer > kLightCycleSeconds - kLightAmberSeconds)
                for (std::size_t i = 0; i < set.slots.size(); ++i)
                    setState(set, static_cast<int>(i), LightState::WalkEnd);
            continue;
        }
        if (set.timer <= kLightCycleSeconds) {
            if (set.timer > kLightCycleSeconds - kLightAmberSeconds) {
                setState(set, set.current, LightState::Amber);
                if (set.cycle == LightCycle::FourWay)
                    setState(set, pairedLight(set), LightState::Amber);
            }
            continue;
        }
        setState(set, set.current, LightState::Red);
        if (set.cycle == LightCycle::FourWay)
            setState(set, pairedLight(set), LightState::Red);
        ++set.current;
        set.timer = 0.0f; // MM2 drops the overshoot
        if (set.current == static_cast<int>(set.slots.size())) {
            if (set.cycle != LightCycle::Rotate) {
                set.walkPhase = true;
                for (std::size_t i = 0; i < set.slots.size(); ++i)
                    setState(set, static_cast<int>(i), LightState::Walk);
            } else {
                set.current = 0;
            }
        }
        if (!set.walkPhase)
            turnGreen(set);
    }
}

const TrafficLights::Set* TrafficLights::setOf(int intersection) const {
    if (intersection < 0 || static_cast<std::size_t>(intersection) >= m_setOfNode.size())
        return nullptr;
    const int i = m_setOfNode[static_cast<std::size_t>(intersection)];
    return i >= 0 ? &m_sets[static_cast<std::size_t>(i)] : nullptr;
}

LightCycle TrafficLights::cycleAt(int intersection) const {
    const Set* s = setOf(intersection);
    return s ? s->cycle : LightCycle::Rotate;
}

bool TrafficLights::walkPhaseAt(int intersection) const {
    const Set* s = setOf(intersection);
    return s && s->walkPhase;
}

LightState TrafficLights::firstLightAt(int intersection) const {
    const Set* s = setOf(intersection);
    return s && !s->slots.empty() ? m_states[static_cast<std::size_t>(s->slots.front())] : LightState::Red;
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
