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

struct CityData {
    CityInfo info;
    Psdl psdl;
    std::vector<Instance> instances;   // city/<map>.inst
    std::vector<Instance> aiInstances; // city/<map>_ai.inst (signs, signals)
    std::optional<RoomPvs> pvs;        // city/<map>.cpvs
    std::optional<AiMap> aiMap;        // city/<map>.bai
    std::array<std::optional<LightingDef>, kTimesOfDay * kWeathers> lighting;
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

// Race events of a city, with settings from mm<mode>data.csv.
std::vector<RaceDefinition> listRaces(const vfs::Vfs& vfs, const CityInfo& info);

// Loads a city by map name ("london", "sf") or localized name. Missing
// optional files produce warnings; a missing or corrupt PSDL is an error.
std::optional<CityData> loadCity(const vfs::Vfs& vfs, std::string_view city, std::string* error = nullptr);

} // namespace mm2::city
