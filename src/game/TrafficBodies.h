#pragma once

#include "ai/World.h"
#include "phys/World.h"

#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace mm2::game {

// Ambient traffic in the physics world, after MM2's aiVehicleManager /
// aiVehicleActive. Cars on their rails are not simulated; a moving body
// touching one turns it into a rigid body (at most 32 at a time; the oldest
// is let go when a 33rd is hit) with four simple wheels (vehWheelCheap).
// Once it has been still for 15 physics steps it is handed back to the AI,
// which drives it back onto its lane when it stands upright on the ground,
// or leaves it as a wreck.
class TrafficBodies {
public:
    TrafficBodies(ai::World& ai, phys::World& world);
    ~TrafficBodies();
    TrafficBodies(const TrafficBodies&) = delete;
    TrafficBodies& operator=(const TrafficBodies&) = delete;

    // Before the physics step: activates rail cars touched by `vehicles` or
    // by physical traffic cars.
    void beforeStep(std::span<phys::Body* const> vehicles);
    // After the step: copies poses to the AI, hands back cars that came to
    // rest or fell out of the world.
    void afterStep(const Vec3& playerPos);

    // World transform (model origin) of a physical car, or null when the car
    // is on its rail (use the AI transform).
    const Mat34* transformOf(int carId) const;
    std::size_t activeCount() const { return m_active.size(); }

private:
    struct Wheel {
        Vec3 pivot;
        float compression = 0.0f;
        float latDeflection = 0.0f, lonDeflection = 0.0f;
    };
    // aiVehicleActive: the body, its wheels and the sleep counter.
    struct Active : phys::BodyController {
        int id = -1;
        const ai::VehicleData* data = nullptr;
        std::unique_ptr<phys::Body> body;
        std::vector<Wheel> wheels;
        float wheelRadius = 0.4f;
        int stillSteps = 0;
        Mat34 model;
        void afterIntegrate(phys::Body& b, float dt, const phys::World& world) override;
    };
    void attach(const ai::AmbientCar& car);
    void detach(std::size_t index);
    bool upright(const Active& a) const;

    ai::World& m_ai;
    phys::World& m_world;
    std::vector<std::unique_ptr<Active>> m_active; // oldest first
};

} // namespace mm2::game
