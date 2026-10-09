#pragma once

// Development aid for automated network races (OPENMM2_DEBUG_AUTOPILOT,
// docs/multiplayer.md "Automation"): the player's car driven by MM2's racer
// AI (ai::Opponent, aiRouteRacer) along one of the race's opponent lines,
// through the car's inputs only. The AI's frame runs on the car and the car
// is put back as it was (the AI also nudges its car's momentum and gear,
// which a network car must not take outside its inputs); what it asked of
// the throttle, brakes and wheel is what the player's controls send. A
// network race has no AI map of its own (mmGameMulti's race modes skip
// aiMap::Init), so the autopilot loads one, with no traffic.

#include "city/CityData.h"
#include "game/session/RaceSetup.h"
#include "phys/vehicle/CarSim.h"
#include "vfs/Vfs.h"

#include <memory>
#include <string>

namespace mm2::ai {
class Opponent;
class World;
} // namespace mm2::ai

namespace mm2::phys {
class GroundQuery;
}

namespace mm2::game {

class NetAutopilot {
public:
    // The race's opponent `line` (0-based, wrapped to the lines it has) at
    // up to `speedLimit` m/s (0: the AI's own).
    static std::unique_ptr<NetAutopilot> create(const city::CityData& city, const vfs::Vfs& vfs,
                                                const session::RaceSetup& setup, phys::CarSim& car, int line,
                                                float speedLimit, const phys::GroundQuery* ground,
                                                std::string* error = nullptr);
    ~NetAutopilot();
    NetAutopilot(const NetAutopilot&) = delete;
    NetAutopilot& operator=(const NetAutopilot&) = delete;

    struct Controls {
        float throttle = 0.0f, brake = 0.0f, steering = 0.0f;
    };
    // The AI's frame for the car (held: on the grid).
    Controls drive(float dt, phys::CarSim& car, bool held);

private:
    NetAutopilot() = default;
    std::unique_ptr<ai::World> m_ai;
    std::unique_ptr<ai::Opponent> m_driver;
};

} // namespace mm2::game
