#pragma once

#include "ai/World.h"
#include "phys/World.h"

#include <functional>
#include <memory>
#include <span>
#include <unordered_map>

namespace mm2::game {

// Ambient traffic in the physics world. Cars on their rails are not
// simulated; when a vehicle body touches one it becomes a rigid body (MM1:
// aiVehicleAmbient -> aiVehicleActive on impact), takes the collision impulse
// and is left to the simulation. Physical cars far from the player go back
// to the AI pool.
class TrafficBodies {
public:
    TrafficBodies(ai::World& ai, phys::World& world);
    ~TrafficBodies();
    TrafficBodies(const TrafficBodies&) = delete;
    TrafficBodies& operator=(const TrafficBodies&) = delete;

    // Before the physics step: activates rail cars touched by `vehicles`.
    void beforeStep(std::span<phys::Body* const> vehicles);
    // After the step: releases physical cars far from `playerPos`.
    void afterStep(const Vec3& playerPos);

    // World transform (model origin) of a physical car, or null when the car
    // is on its rail (use the AI transform).
    const Mat34* transformOf(int carId) const;
    std::size_t activeCount() const { return m_active.size(); }

private:
    struct Active {
        std::unique_ptr<phys::Body> body;
        Vec3 cg;          // model space
        Mat34 model;      // updated after each step
    };
    void onImpact(ai::AmbientCar& car, const Vec3& impulse);

    ai::World& m_ai;
    phys::World& m_world;
    std::unordered_map<int, Active> m_active;
    // Contact found in beforeStep(), consumed by the impact handler.
    struct PendingHit {
        phys::Body* vehicle = nullptr;
        Vec3 point;
    } m_pending;
};

} // namespace mm2::game
