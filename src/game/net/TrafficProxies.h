#pragma once

// The shared traffic of a network cruise in a client's physics world
// (OpenMM2 extra; see TrafficSync.h): each received traffic car is an
// instance of the level with its aiVehicleData box (as the host's rail cars,
// game::TrafficBodies), at its interpolated place and moving at its
// interpolated velocity (phys::Instance::kinematicMotion). The local car
// collides with it as with a moving car of infinite mass; nothing knocks it
// loose here, since only the host simulates the traffic: the host sees the
// client's car hit its own car, and the result comes back with the next
// messages.

#include "ai/Traffic.h"
#include "game/CityLevel.h"
#include "phys/Bound.h"
#include "phys/Level.h"

#include <memory>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mm2::game {

class TrafficProxies final : public InstanceSource {
public:
    // `level` gives the cars their rooms; the level must list instancesIn()
    // (CityLevel::addSource).
    explicit TrafficProxies(const phys::Level* level);
    ~TrafficProxies() override;
    TrafficProxies(const TrafficProxies&) = delete;
    TrafficProxies& operator=(const TrafficProxies&) = delete;

    // The received traffic cars this frame, before the physics step (cars
    // without vehicle data are left out). The ids are the network's.
    void update(std::span<const ai::AmbientCar> cars);
    // The cars the local player's car hit in the last step (the level's
    // hit-by-player mark), cleared.
    std::vector<int> takeHits();

    // InstanceSource.
    void instancesIn(int room, std::vector<phys::Instance*>& out) const override;
    std::size_t size() const { return m_byRoom.size(); }

private:
    class Proxy;
    struct BoundEntry {
        std::unique_ptr<phys::BoundBox> box;
        float radius = 0.0f;
    };
    const BoundEntry& boundFor(const ai::VehicleData& data);

    const phys::Level* m_level;
    std::unordered_map<int, std::unique_ptr<Proxy>> m_proxies;
    std::unordered_map<const ai::VehicleData*, BoundEntry> m_bounds;
    std::vector<std::pair<int, Proxy*>> m_byRoom; // sorted by room
};

} // namespace mm2::game
