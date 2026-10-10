// A shared-traffic client's received cars as TrafficBodies' traffic (OpenMM2
// extra). See NetTrafficCars.h.
#include "game/net/NetTrafficCars.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace mm2::game {

void NetTrafficCars::update(std::span<const Received> received, double now, double confirmMs) {
    m_now = now;
    m_cars.clear();
    m_standing.clear();
    std::unordered_set<int> present;
    for (const Received& r : received) {
        const ai::AmbientCar& c = r.car;
        present.insert(c.id);
        const bool onRail = c.goal == ai::AmbientGoal::RandomDrive;
        if (r.hostBody) {
            // The host's body in full: simulated here from its states. A car
            // this machine knocked loose goes on as it (the host's state is
            // put in at the next acknowledgement).
            if (const auto it = m_knocks.find(c.id); it != m_knocks.end()) {
                if (!it->second.confirmed)
                    ++m_stats.confirmed;
                m_hostBodies[c.id] = it->second.pose;
                m_knocks.erase(it);
            }
            const auto [body, fresh] = m_hostBodies.try_emplace(c.id, c.transform);
            Knock k;
            k.generation = c.spawns;
            k.pose = body->second;
            keepLocal(c, k);
            continue;
        }
        if (const auto it = m_hostBodies.find(c.id); it != m_hostBodies.end()) {
            // Its full states stopped (at rest on the host, or far): the
            // host's messages lead from here, the drawing blending.
            m_handovers.push_back({c.id, it->second, true});
            m_hostBodies.erase(it);
        }
        if (const auto it = m_knocks.find(c.id); it != m_knocks.end()) {
            Knock& k = it->second;
            const double heard = static_cast<double>(r.stateTime);
            if (!onRail && !k.confirmed && k.generation == c.spawns) {
                k.confirmed = true; // the host has knocked it too
                ++m_stats.confirmed;
            }
            if (k.generation != c.spawns) {
                m_knocks.erase(it); // another car in the slot
            } else if (k.confirmed) {
                // Knocked on the host too. Until its body here comes to rest
                // the local one, which runs the host's physics from the same
                // hit, is the better guess of the two; then (or when the
                // host's car is far from it, or back on its rail) the host's
                // messages lead.
                if (k.resting || onRail || now > k.time + kMaxLocalMs ||
                    c.transform.m3.dist(k.pose.m3) > kDivergeMetres) {
                    handOver(c.id, k, true);
                    m_knocks.erase(it);
                    if (!onRail)
                        continue;
                } else {
                    keepLocal(c, k);
                    continue;
                }
            } else if (heard > k.time + confirmMs || now > k.time + kMaxLocalMs) {
                // On its rail well after the hit on the host: it was not hit
                // there.
                ++m_stats.withdrawn;
                handOver(c.id, k, false);
                m_knocks.erase(it);
            } else {
                keepLocal(c, k);
                continue;
            }
        }
        // Off its rail on the host: a moving instance (game::TrafficProxies);
        // one standing still (a car that came to rest after a knock, without
        // a body on the host) stands here as a rail car does, which this
        // machine's car knocks loose as the host's does (protocol 12; before
        // it met a car of infinite mass).
        if (onRail) {
            m_cars.push_back(c);
        } else if (c.velocity.mag2() < kStandingSpeed * kStandingSpeed &&
                   std::abs(c.speed) < kStandingSpeed) {
            ai::AmbientCar a = c;
            a.physical = false;
            a.speed = 0.0f;
            m_cars.push_back(a);
            m_standing.insert(c.id);
        }
    }
    std::erase_if(m_knocks, [&](const auto& e) { return !present.contains(e.first); });
    std::erase_if(m_hostBodies, [&](const auto& e) { return !present.contains(e.first); });
}

void NetTrafficCars::keepLocal(const ai::AmbientCar& c, const Knock& k) {
    ai::AmbientCar a = c;
    a.transform = k.pose;
    a.goal = ai::AmbientGoal::Collision;
    a.physical = true;
    a.speed = 0.0f;
    m_cars.push_back(a);
}

void NetTrafficCars::handOver(int id, const Knock& knock, bool confirmed) {
    m_handovers.push_back({id, knock.pose, confirmed});
}

void NetTrafficCars::impact(int carId) {
    const auto it = std::ranges::find_if(m_cars, [carId](const ai::AmbientCar& c) { return c.id == carId; });
    if (it == m_cars.end() || m_hostBodies.contains(carId))
        return; // (a body the host sends in full is the host's knock)
    m_knocks[carId] = {it->spawns, m_now, it->transform};
    it->physical = true;
    it->goal = ai::AmbientGoal::Collision;
    m_new.push_back(carId);
    ++m_stats.knocks;
}

void NetTrafficCars::detach(int carId, const Mat34& pose, bool) {
    // The body came to rest: the car stays there until the host's messages
    // take it over.
    if (const auto it = m_knocks.find(carId); it != m_knocks.end()) {
        it->second.pose = pose;
        it->second.resting = true;
    }
    if (const auto it = m_hostBodies.find(carId); it != m_hostBodies.end())
        it->second = pose;
}

void NetTrafficCars::setPhysicalTransform(int carId, const Mat34& transform) {
    if (const auto it = m_knocks.find(carId); it != m_knocks.end())
        it->second.pose = transform;
    if (const auto it = m_hostBodies.find(carId); it != m_hostBodies.end())
        it->second = transform;
}

bool NetTrafficCars::attachable(int, bool byPlayer) const { return byPlayer; }

} // namespace mm2::game
