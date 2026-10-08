#pragma once

// Everything the game needs about one city, loaded from the mounted game data.

#include "city/AiMap.h"
#include "city/Environment.h"
#include "city/Inst.h"
#include "city/PathSet.h"
#include "city/Psdl.h"
#include "city/Pvs.h"
#include "city/Race.h"
#include "vfs/Vfs.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mm2::city {

// One race event and where its files live (virtual paths; files that a race
// does not have are left empty).
struct RaceDefinition {
    RaceMode mode = RaceMode::Blitz;
    int index = 0;    // N in blitzN / circuitN / raceN / crashN
    std::string name; // localized name from the .cinfo (blitz/circuit/checkpoint)
    std::optional<RaceTableEntry> settings;
    std::string waypoints;      // race/<dir>/<prefix>Nwaypoints.csv
    std::string aiMap;          // race/<dir>/<prefix>N.aimap
    std::string aiMapPro;       // race/<dir>/<prefix>N.aimap_p
    std::string pathSet;        // race/<dir>/<prefix>N.pathset
    std::string crashEvents;    // race/<dir>/crashNdata.csv
    std::string crashEventsPro; // race/<dir>/crashNdata_p.csv
};

// cityTimeWeatherLighting's constructor: ambient 0xFF101010.
inline constexpr std::uint32_t kDefaultLightingAmbient = 0xFF101010u;
constexpr std::array<std::uint32_t, kTimesOfDay * kWeathers> defaultAmbients() {
    std::array<std::uint32_t, kTimesOfDay * kWeathers> a{};
    for (auto& v : a)
        v = kDefaultLightingAmbient;
    return a;
}

struct CityData {
    CityInfo info;
    Psdl psdl;
    std::vector<Instance> instances;   // city/<map>.inst
    std::vector<Instance> aiInstances; // city/<map>_ai.inst (signs, signals)
    std::optional<RoomPvs> pvs;        // city/<map>.cpvs
    std::optional<AiMap> aiMap;        // city/<map>.bai
    std::array<std::optional<LightingDef>, kTimesOfDay * kWeathers> lighting;
    // The ambient each lighting table held before this city's .ltNN files
    // loaded (see loadCity): what MM2 derives the lower light qualities'
    // ambient levels from.
    std::array<std::uint32_t, kTimesOfDay * kWeathers> ambientBeforeLoad = defaultAmbients();
    std::vector<FogDef> fog;
    std::optional<SkyDef> sky;
    std::optional<WaterDef> water;
    std::optional<MapExtent> extent;
    std::vector<ResetPoint> resetPoints;
    std::optional<std::vector<std::uint32_t>> roomColors; // city/<map>.lmap
    std::vector<PhysMaterial> materials;                  // city/materials.mtl
    std::vector<TextureMaterial> textureMaterials;        // city/materials.csv
    std::optional<AiMapConfig> cruise, cruisePro;         // race/<dir>/roam.aimap(_p)
    std::vector<PathSet> cityPathSets;                    // race/<dir>/<map>_*.pathset (bridges, ferries...)
    std::vector<std::string> cityPathSetNames;
    std::vector<RaceDefinition> races;
    std::vector<RaceReward> rewards;
    // Non-fatal problems (missing optional files, parse errors in extras).
    std::vector<std::string> warnings;
};

// All cities defined by tune/*.cinfo, in name order.
std::vector<CityInfo> listCities(const vfs::Vfs& vfs);

// The level's own room flags (lvlRoomInfo's flag word, lvlLevel +8), which
// differ from the PSDL's room flag bytes: cityLevel::Load gives
//   0x01 open streets: intersections that are not warp rooms, and road rooms
//        that are not warp rooms whose geometry (after any texture and tunnel
//        attributes) is not a divided road and whose tunnel, if any, has
//        neither of the subtype bits 0x3;
//   0x0A subterranean rooms (PSDL 0x02);
//   0x04 rooms whose first attribute selects a texture of the "deepwater"
//        material (city/materials.csv; not tested for subterranean rooms that
//        also have PSDL 0x04);
// and lvlLevel::LoadInstances adds
//   0x20 rooms holding an instance with flag 0x100 (the .inst flags word).
// Index = room id (entry 0 is the dummy room).
inline constexpr std::uint16_t kLevelRoomStreet = 0x01;
inline constexpr std::uint16_t kLevelRoomSubterranean = 0x02;
inline constexpr std::uint16_t kLevelRoomWater = 0x04;
inline constexpr std::uint16_t kLevelRoomCovered = 0x08; // set with 0x02
inline constexpr std::uint16_t kLevelRoomLandmark = 0x20;
std::vector<std::uint16_t> levelRoomFlags(const CityData& city);

// Race events of a city, with settings from mm<mode>data.csv.
std::vector<RaceDefinition> listRaces(const vfs::Vfs& vfs, const CityInfo& info);

// Loads a city by map name ("london", "sf") or localized name. Missing
// optional files produce warnings; a missing or corrupt PSDL is an error.
std::optional<CityData> loadCity(const vfs::Vfs& vfs, std::string_view city, std::string* error = nullptr);

} // namespace mm2::city
