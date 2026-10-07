#pragma once

// The AI layer of a session: road network, traffic lights, ambient traffic
// and pedestrians, stepped at a fixed rate.
//
// MM1 advanced its AI once per rendered frame with the frame's clamped
// duration (asSimulation, 0.0001..0.25 s). OpenMM2 steps at a fixed 30 Hz
// (MM1's default asSimulation rate) so traffic is deterministic and frame
// rate independent; reaction delays counted in updates by the original
// ("react ticks") are therefore counted in 1/30 s steps.

#include "ai/Pedestrians.h"
#include "ai/RoadNetwork.h"
#include "ai/Traffic.h"
#include "ai/TrafficLights.h"
#include "ai/VehicleData.h"
#include "city/CityData.h"
#include "vfs/Vfs.h"

#include <memory>
#include <string>
#include <vector>

namespace mm2::ai {

inline constexpr float kAiStepSeconds = 1.0f / 30.0f;

struct Settings {
    float trafficDensity = 1.0f;    // menu "Traffic Density" (0 none .. 1 full)
    float pedestrianDensity = 1.0f; // menu "Pedestrian Density"
    int maxCars = 64;
    int maxPeds = 48;
    std::uint64_t seed = 1;
};

// A traffic light pole for rendering (red/amber/green glow parts of the model).
struct Signal {
    std::string model; // e.g. "sp_traflitsingle_l"
    Mat34 transform;   // glows (+Z of the model) face the approaching traffic
    LightState state = LightState::Red;
};

class World {
public:
    // `config` is the race's AI map (race/<dir>/<race>.aimap); null uses the
    // city's cruise map (roam.aimap).
    static std::unique_ptr<World> create(const city::CityData& city, const vfs::Vfs& vfs,
                                         const Settings& settings, const city::AiMapConfig* config = nullptr,
                                         std::string* error = nullptr);

    // Advances by `dt` seconds in fixed steps (at most 8 per call).
    void update(float dt, const Vec3& playerPos, const Vec3& playerVel);
    // One fixed step.
    void step(const Vec3& playerPos, const Vec3& playerVel);

    const RoadNetwork& network() const { return *m_network; }
    TrafficLights& lights() { return m_lights; }
    Traffic& traffic() { return *m_traffic; }
    Pedestrians& pedestrians() { return *m_peds; }

    const std::vector<AmbientCar>& cars() const { return m_traffic->cars(); }
    const std::vector<Pedestrian>& peds() const { return m_peds->peds(); }
    const std::vector<Signal>& signals() const { return m_signals; }

private:
    World() = default;
    void updateSignals();

    std::unique_ptr<RoadNetwork> m_network;
    TrafficLights m_lights;
    std::unique_ptr<Traffic> m_traffic;
    std::unique_ptr<Pedestrians> m_peds;
    std::vector<Signal> m_signals;
    float m_accumulator = 0.0f;
};

// Vehicle types for ambient traffic of a city when the AI map does not list
// them: every tune/vehicle/va_*.aivehicledata whose model suffix is the
// city's ("_l" London, "_f" San Francisco) or shared ("_s"), excluding rail
// vehicles. Inferred from the file naming.
std::vector<city::AiAmbientType> defaultAmbientTypes(const vfs::Vfs& vfs, const city::CityData& city);

// Loads the pedestrian types (anim/<type>.csv and .shaders) present in the data.
std::vector<PedTypeInfo> loadPedTypes(const vfs::Vfs& vfs);

} // namespace mm2::ai
