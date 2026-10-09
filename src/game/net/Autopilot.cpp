#include "game/net/Autopilot.h"

#include "ai/Opponent.h"
#include "ai/World.h"
#include "city/Race.h"
#include "core/StringUtil.h"

#include <string_view>

namespace mm2::game {

NetAutopilot::~NetAutopilot() = default;

std::unique_ptr<NetAutopilot> NetAutopilot::create(const city::CityData& city, const vfs::Vfs& vfs,
                                                   const session::RaceSetup& setup, phys::CarSim& car,
                                                   int line, float speedLimit, const phys::GroundQuery* ground,
                                                   std::string* error) {
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
    pilot->m_driver = ai::Opponent::create(pilot->m_ai->map(), car, *path, o.params, laps, 1, 0, error, ground,
                                           setup.config.vehicle);
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

NetAutopilot::Controls NetAutopilot::drive(float dt, phys::CarSim& car, bool held) {
    const auto saved = car.saveState();
    m_driver->setHeld(held);
    m_driver->update(dt, {});
    const Controls c{car.engine.throttle, car.brakes, car.steering};
    car.restoreState(saved);
    return c;
}

} // namespace mm2::game
