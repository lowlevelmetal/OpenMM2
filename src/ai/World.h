#pragma once

// The AI layer of a session: road network, traffic lights, ambient traffic
// and pedestrians, stepped at a fixed rate.
//
// MM2 advances its AI once per rendered frame with the frame's duration
// (aiMap::Update). OpenMM2 steps at a fixed 30 Hz so traffic is
// deterministic and frame rate independent; reaction delays counted in
// updates by the original ("react ticks") are therefore counted in 1/30 s
// steps. Within a step the order is MM2's: ambient traffic, pedestrians,
// then the traffic light sets (children of aiMap, updated last).

#include "ai/Pedestrians.h"
#include "ai/Random.h"
#include "ai/RoadNetwork.h"
#include "ai/Traffic.h"
#include "ai/TrafficLights.h"
#include "ai/VehicleData.h"
#include "city/CityData.h"
#include "city/RoomLocator.h"
#include "vfs/Vfs.h"

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace mm2::ai {

inline constexpr float kAiStepSeconds = 1.0f / 30.0f;

struct Settings {
    float trafficDensity = 1.0f;    // menu "Traffic Density" (MMSTATE trafficDensity, 0 none .. 1 full)
    float pedestrianDensity = 1.0f; // menu "Pedestrian Density"
    int maxCars = kAmbientPoolSize;
    int maxPeds = -1;        // pedestrian pool; -1: the city's [Ped Pool] (default 100)
    bool winterPeds = false; // snow: the bad-weather pedestrian models (MM2: weather > 2)
    // The traffic and the pedestrians draw from one stream, as from MM2's
    // global seed: aiMap::Init's draws (the ambient pool, then the
    // pedestrians), aiMap::Reset's population and their updates. `random`
    // is that stream when the caller reproduces MM2's set-up order (MM2's
    // global stream as aiMap::Init finds it); null gives the world its own,
    // starting at `seed`. reset() sets it to `seed` (aiMap::Reset's
    // ResetRandomSeed: 1).
    Random* random = nullptr;
    std::uint64_t seed = 1;
};

// A traffic light pole for rendering (aiTrafficLightInstance): draw the
// glow of `state` (RED/YELLOW/GREEN) together with the WALK or NOWALK glow,
// as MM2's DrawGlow does.
struct Signal {
    std::string model; // e.g. "sp_traflitsingle_f"
    Mat34 transform;   // model origin (the pole's base); +Z (the glows) faces the approaching traffic
    // The model's dgBangerData CG. aiTrafficLightInstance::Init places the
    // instance at the base + R * CG (its GetMatrix and GetPosition), and the
    // body mesh is centred on that point; the glow meshes are modelled from
    // the base, so DrawGlow takes R * CG off again.
    Vec3 cg;
    LightState state = LightState::Red;

    // GetMatrix: the instance's frame, at the CG.
    Mat34 frame() const {
        Mat34 m = transform;
        m.m3 = transform.transform(cg);
        return m;
    }
    // GetPosition: the CG in world space.
    Vec3 position() const { return transform.transform(cg); }
};

class World {
public:
    // `config` is the race's AI map (race/<dir>/<race>.aimap, MM2's
    // aiRaceData); null uses the city's cruise map (roam.aimap). The city's
    // own city/<map>.aimap (aiCityData) is read from `vfs`.
    static std::unique_ptr<World> create(const city::CityData& city, const vfs::Vfs& vfs,
                                         const Settings& settings, const city::AiMapConfig* config = nullptr,
                                         std::string* error = nullptr);

    // Advances by `dt` seconds in fixed steps (at most 8 per call).
    void update(float dt, const PlayerCar& player);
    void update(float dt, const Vec3& playerPos, const Vec3& playerVel) {
        update(dt, PlayerCar::at(playerPos, playerVel));
    }
    // One fixed step.
    void step(const PlayerCar& player);
    // aiMap::Reset (mmGame::Init calls it once the AI map has loaded and
    // mmGame::Reset when a race restarts): the random stream back to its
    // seed (ResetRandomSeed), the ambient traffic, the pedestrians and the
    // light sets as at the start; the next step populates the roads round
    // the player, the traffic first, then the pedestrians, before either
    // updates (aiMap::Reset's AdjustAmbients and AdjustPedestrians).
    // (aiMap::Reset also resets the police force and officers and the
    // racers, which the race owns here, and aiVehicleManager,
    // game::TrafficBodies::reset.)
    void reset();
    // aiMap::Update runs the light sets after the racers and the police. A
    // race loop that drives those between update() and the lights sets this
    // and calls updateLights() after them: update() then leaves the lights'
    // steps pending.
    void setLightsDeferred(bool deferred) { m_lightsDeferred = deferred; }
    // Advances the light sets by the steps update() left pending.
    void updateLights();
    void step(const Vec3& playerPos, const Vec3& playerVel) { step(PlayerCar::at(playerPos, playerVel)); }

    // Race opponents' positions: ambient cars are never placed within 50 m
    // of one (aiMap::AdjustAmbients).
    void setOpponents(std::span<const Vec3> positions) { m_traffic->setOpponents(positions); }
    // Collision probe (segment from -> to) for fitting cars and pedestrians
    // to the ground and for pedestrians looking for a wall to run to.
    void setProbe(const Traffic::GroundProbe& probe) {
        m_traffic->setGroundProbe(probe);
        m_peds->setProbe(probe);
    }

    const RoadNetwork& network() const { return *m_network; }
    // The AI drivers' view of the map (rooms, components, obstacle lists).
    MapView& map() { return *m_map; }
    const MapView& map() const { return *m_map; }
    TrafficLights& lights() { return m_lights; }
    Traffic& traffic() { return *m_traffic; }
    Pedestrians& pedestrians() { return *m_peds; }

    const std::vector<AmbientCar>& cars() const { return m_traffic->cars(); }
    // aiVehicleManager::Update's blink counter: trunc(summed time x 16). An
    // ambient car's lit indicators show while bit 3 of (the low byte of its
    // blinkPhase + this) is set (aiVehicleInstance::DrawGlow): on and off for
    // half a second each.
    int blinkClock() const { return static_cast<int>(m_vehicleClock * 16.0f); }
    static bool indicatorsOn(const AmbientCar& car, int clock) { return (((car.blinkPhase & 0xFF) + clock) & 8) != 0; }
    // See Traffic::takeAvoidEvents.
    std::vector<int> takeAvoidEvents() { return m_traffic->takeAvoidEvents(); }
    const std::vector<Pedestrian>& peds() const { return m_peds->peds(); }
    const std::vector<Signal>& signals() const { return m_signals; }
    // PSDL room of a position (0 outside every room), looked up from `hint`
    // (cityLevel::FindRoomId).
    int roomAt(const Vec3& position, int hint = 0) const { return m_rooms ? m_rooms->find(position, hint) : 0; }

private:
    World() = default;
    void updateSignals();

    Random m_ownRandom;              // the stream when the caller shares none
    Random* m_random = &m_ownRandom; // MM2's global seed for the traffic and pedestrians
    std::uint32_t m_resetSeed = 1;   // aiMap::Reset's ResetRandomSeed
    std::unique_ptr<RoadNetwork> m_network;
    std::unique_ptr<city::RoomLocator> m_rooms;
    TrafficLights m_lights;
    std::unique_ptr<Traffic> m_traffic;
    std::unique_ptr<MapView> m_map;
    std::unique_ptr<Pedestrians> m_peds;
    std::vector<Signal> m_signals;
    float m_accumulator = 0.0f;
    float m_vehicleClock = 0.0f; // aiVehicleManager's summed time (never reset)
    bool m_lightsDeferred = false;
    int m_pendingLightSteps = 0;
    int m_playerRoom = 0; // the player's last room, the next lookup's hint
};

// Vehicle types for ambient traffic of a city when no AI map lists them:
// every tune/vehicle/va_*.aivehicledata whose model suffix is the city's
// ("_l" London, "_f" San Francisco) or shared ("_s"), excluding rail
// vehicles. Inferred from the file naming.
std::vector<city::AiAmbientType> defaultAmbientTypes(const vfs::Vfs& vfs, const city::CityData& city);

// Loads the pedestrian types (anim/<type>.csv and .shaders) present in the data.
std::vector<PedTypeInfo> loadPedTypes(const vfs::Vfs& vfs);

// The city's own AI map, city/<map>.aimap (MM2's aiCityData), if present.
std::optional<city::AiMapConfig> loadCityAiConfig(const vfs::Vfs& vfs, const city::CityData& city);

} // namespace mm2::ai
