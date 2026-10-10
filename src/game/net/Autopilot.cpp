#include "game/net/Autopilot.h"

#include "ai/Opponent.h"
#include "ai/World.h"
#include "city/Race.h"
#include "core/Libm.h"
#include "core/Log.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace mm2::game {

NetAutopilot::~NetAutopilot() = default;

std::unique_ptr<NetAutopilot> NetAutopilot::create(const city::CityData& city, const vfs::Vfs& vfs,
                                                   const session::RaceSetup& setup, phys::CarSim& car,
                                                   int line, float speedLimit,
                                                   const phys::GroundQuery* ground, std::string* error) {
    auto fail = [&](std::string text) -> std::unique_ptr<NetAutopilot> {
        if (error)
            *error = std::move(text);
        return nullptr;
    };
    if (!setup.race || !setup.aiMap || setup.aiMap->opponents.empty())
        return fail("the race has no opponent lines");
    const auto& lines = setup.aiMap->opponents;
    const auto& o = lines[static_cast<std::size_t>(std::max(0, line)) % lines.size()];
    const std::string& any = !setup.race->aiMap.empty() ? setup.race->aiMap : setup.race->waypoints;
    const std::string file = any.substr(0, any.rfind('/') + 1) + str::lower(o.pathFile);
    const auto bytes = vfs.readAll(file);
    if (!bytes)
        return fail("cannot read " + file);
    const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    auto path = city::parseOpponentPath(text);
    if (!path || path->empty())
        return fail("cannot parse " + file);
    ai::Settings settings;
    settings.trafficDensity = 0.0f;
    settings.pedestrianDensity = 0.0f;
    settings.maxPeds = 0;
    std::unique_ptr<NetAutopilot> pilot(new NetAutopilot);
    pilot->m_ai = ai::World::create(city, vfs, settings, &*setup.aiMap, error);
    if (!pilot->m_ai)
        return nullptr;
    const int laps = std::max(1, setup.laps);
    // The AI takes the car's impact callback for itself; the player's car
    // keeps its own.
    const auto impacts = car.onImpactCallback;
    pilot->m_driver = ai::Opponent::create(pilot->m_ai->map(), car, *path, o.params, laps, 1, 0, error,
                                           ground, setup.config.vehicle);
    car.onImpactCallback = impacts;
    if (!pilot->m_driver)
        return nullptr;
    if (speedLimit > 0.0f)
        pilot->m_driver->setSpeedLimit(speedLimit);
    pilot->m_driver->setResetCar([](const Mat34&) {}); // the car is the player's: no AI resets
    pilot->m_driver->setHeld(true);
    const auto saved = car.saveState();
    pilot->m_driver->reset();
    car.restoreState(saved);
    return pilot;
}

std::unique_ptr<NetAutopilot> NetAutopilot::createFree(const city::CityData& city, const vfs::Vfs& vfs,
                                                       const session::RaceSetup& setup, float speedLimit,
                                                       std::string* error) {
    ai::Settings settings;
    settings.trafficDensity = 0.0f;
    settings.pedestrianDensity = 0.0f;
    settings.maxPeds = 0;
    std::unique_ptr<NetAutopilot> pilot(new NetAutopilot);
    pilot->m_ai = ai::World::create(city, vfs, settings, setup.aiMap ? &*setup.aiMap : nullptr, error);
    if (!pilot->m_ai)
        return nullptr;
    pilot->m_vehicle = setup.config.vehicle;
    pilot->m_speedLimit = speedLimit;
    return pilot;
}

void NetAutopilot::setTarget(phys::CarSim& car, const Vec3& target, const phys::GroundQuery* ground) {
    // A new destination: the racer AI drives the city's roads to it.
    if (m_target && m_target->dist2(target) < 25.0f * 25.0f && m_driver)
        return;
    m_target = target;
    const Vec3& from = car.body.ics.matrix.m3;
    log::info("autopilot: to ({:.0f}, {:.0f}, {:.0f}) from ({:.0f}, {:.0f}, {:.0f})", target.x, target.y,
              target.z, from.x, from.y, from.z);
    std::vector<city::OpponentPoint> path(2);
    path[0].position = car.body.ics.matrix.m3;
    path[1].position = target;
    const auto impacts = car.onImpactCallback;
    m_driver.reset();
    m_driver = ai::Opponent::create(m_ai->map(), car, path, {}, 1, 1, 0, nullptr, ground, m_vehicle);
    car.onImpactCallback = impacts;
    if (!m_driver)
        return;
    if (m_speedLimit > 0.0f)
        m_driver->setSpeedLimit(m_speedLimit);
    m_driver->setResetCar([](const Mat34&) {});
    m_driver->setHeld(false);
    const auto saved = car.saveState();
    m_driver->reset();
    car.restoreState(saved);
}

NetAutopilot::Controls NetAutopilot::drive(float dt, phys::CarSim& car, bool held) {
    if (!m_driver)
        return {};
    const auto saved = car.saveState();
    m_driver->setHeld(held);
    m_driver->update(dt, {});
    Controls c{car.engine.throttle, car.brakes, car.steering,
               car.trans.getCurrentGear() == phys::Transmission::kReverse};
    car.restoreState(saved);
    // The last stretch to a free target (the gold off the road, a base): at
    // it, past where the roads lead.
    const Mat34& m = car.body.ics.matrix;
    if (m_target && !held && m.m3.dist2(*m_target) < 80.0f * 80.0f) {
        const float dx = m_target->x - m.m3.x, dz = m_target->z - m.m3.z;
        const float angle = libm::atan2(dx * m.m0.x + dz * m.m0.z, -(dx * m.m2.x + dz * m.m2.z));
        c.steering = std::clamp(angle * 1.33f, -1.0f, 1.0f);
        const bool behind = std::fabs(angle) > 2.0f;
        const float speed = -car.body.ics.linearVelocity.dot(m.m2);
        c.reverse = behind;
        c.throttle = behind ? 0.5f : (speed < 20.0f ? 1.0f : 0.0f);
        c.brake = 0.0f;
    }
    return c;
}

} // namespace mm2::game
