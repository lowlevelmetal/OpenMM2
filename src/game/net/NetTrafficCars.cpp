// A shared-traffic client's received cars as TrafficBodies' traffic (OpenMM2
// extra). See NetTrafficCars.h.
#include "game/net/NetTrafficCars.h"

#include <algorithm>
#include <unordered_set>

namespace mm2::game {

void NetTrafficCars::update(std::span<const Received> received, double now, double confirmMs) {
    m_now = now;
    m_cars.clear();
    std::unordered_set<int> present;
    for (const Received& r : received) {
        const ai::AmbientCar& c = r.car;
        present.insert(c.id);
        const bool onRail = c.goal == ai::AmbientGoal::RandomDrive;
        if (const auto it = m_knocks.find(c.id); it != m_knocks.end()) {
            const Knock& k = it->second;
            const double heard = static_cast<double>(r.stateTime);
            if (k.generation != c.spawns) {
                m_knocks.erase(it); // another car in the slot
            } else if (!onRail) {
                // The host has knocked it too: its messages lead from now.
                ++m_stats.confirmed;
                handOver(c.id, k, true);
                m_knocks.erase(it);
                continue;
            } else if (heard > k.time + confirmMs || now > k.time + kMaxLocalMs) {
                // On its rail well after the hit on the host: it was not hit
                // there.
                ++m_stats.withdrawn;
                handOver(c.id, k, false);
                m_knocks.erase(it);
            } else {
                ai::AmbientCar a = c;
                a.transform = k.pose;
                a.goal = ai::AmbientGoal::Collision;
                a.physical = true;
                a.speed = 0.0f;
                m_cars.push_back(a);
                continue;
            }
        }
        // Off its rail on the host: a moving instance (game::TrafficProxies).
        if (onRail)
            m_cars.push_back(c);
    }
    std::erase_if(m_knocks, [&](const auto& e) { return !present.contains(e.first); });
}

void NetTrafficCars::handOver(int id, const Knock& knock, bool confirmed) {
    m_handovers.push_back({id, knock.pose, confirmed});
}

void NetTrafficCars::impact(int carId) {
    const auto it = std::ranges::find_if(m_cars, [carId](const ai::AmbientCar& c) { return c.id == carId; });
    if (it == m_cars.end())
        return;
    m_knocks[carId] = {it->spawns, m_now, it->transform};
    it->physical = true;
    it->goal = ai::AmbientGoal::Collision;
    m_new.push_back(carId);
    ++m_stats.knocks;
}

void NetTrafficCars::detach(int carId, const Mat34& pose, bool) {
    // The body came to rest: the car stays there until the host's messages
    // take it over.
    if (const auto it = m_knocks.find(carId); it != m_knocks.end())
        it->second.pose = pose;
}

void NetTrafficCars::setPhysicalTransform(int carId, const Mat34& transform) {
    if (const auto it = m_knocks.find(carId); it != m_knocks.end())
        it->second.pose = transform;
}

bool NetTrafficCars::attachable(int, bool byPlayer) const { return byPlayer; }

} // namespace mm2::game
