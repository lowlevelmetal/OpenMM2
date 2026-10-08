#pragma once

#include "vfs/Vfs.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mm2::game {

// Player vehicle description from tune/<basename>.info (mmVehInfo::Load).
// MM2 reads the first twelve fields with fscanf, one after the other, and
// drops a file that lacks one of them; of the remaining lines it only reads
// UIDist.
struct VehicleInfo {
    std::string baseName;    // "vpbug": prefix of the car's geometry, tune and audio files
    std::string description; // showroom name, "VW New Beetle"
    std::vector<std::string> colors;
    std::uint32_t flags = 0;
    int order = -1;
    float scoringBias = 0.0f;
    int unlockScore = 0;
    std::uint32_t unlockFlags = 0;
    // Showroom statistics (display only; the simulation uses .vehCarSim).
    int horsepower = 0;
    int topSpeedMph = 0;
    int durability = 0;
    int massLb = 0;
    float uiDistance = 6.0f; // UIDist: showroom camera distance (6 when absent)
    // The force feedback scales mmPlayer::Init hands to the input. MM2 never
    // reads the ForceFeedbackModifier / RoadForceModifier lines of the .info
    // files, so they keep the mmVehInfo constructor's 1.
    float forceFeedbackModifier = 1.0f;
    float roadForceModifier = 1.0f;

    // Flag meanings come from MM2's readers and which cars carry them: 0x08
    // marks the cop car (mmMultiCR picks the team by it), 0x13 the big
    // vehicles that switch to the _ind camera (mmPlayer::Update), 0x01 / 0x02
    // move the network spawn (mmMultiRoam), 0x40 is read by the HUD map
    // (mmHudMap; set for the British cars).
    static constexpr std::uint32_t kFlagCop = 0x08;   // vpcop: siren, Cops & Robbers cop car
    static constexpr std::uint32_t kFlagLarge = 0x10; // buses and trucks
    static constexpr std::uint32_t kFlagBritish = 0x40;
};

// City description from tune/<name>.cinfo (mmCityInfo::Load). MM2 reads
// nine fields with fscanf in a fixed sequence and drops a file that lacks
// one of them; MustPlace and UnlockGroup are never read.
struct CityInfo {
    std::string name;          // file stem, "london"
    std::string localizedName; // "London"
    std::string mapName;       // city/<mapName>.psdl etc.
    std::string raceDir;       // race/<raceDir>/; MM2 identifies the city by it (mmCityList::GetCityID)
    // A non-zero BlitzCount / CircuitCount / CheckpointCount is replaced by
    // the number of names in the list; a zero count leaves the list empty.
    std::vector<std::string> blitzNames;
    std::vector<std::string> circuitNames;
    std::vector<std::string> checkpointNames;
    int mustPlace = 0;   // parsed, but never read by MM2 (pass rules are fixed, see game/Profile.h)
    int unlockGroup = 0; // parsed, but never read by MM2
};

class Catalog {
public:
    // Loads the vehicles in MM2's order (mmVehList::LoadAll: its built-in
    // list, then every other tune/*.info) and the cities (mmCityList::LoadAll:
    // sf.cinfo, then every other tune/*.cinfo). An entry whose BaseName /
    // RaceDir is already listed is dropped. Missing or malformed entries are
    // logged and skipped.
    static Catalog load(const vfs::Vfs& vfs);

    const std::vector<VehicleInfo>& vehicles() const { return m_vehicles; }
    const std::vector<CityInfo>& cities() const { return m_cities; }
    const VehicleInfo* vehicle(std::string_view baseName) const;
    // By RaceDir, case-insensitively (mmCityList::GetCityInfo).
    const CityInfo* city(std::string_view raceDir) const;

private:
    std::vector<VehicleInfo> m_vehicles; // in MM2's vehicle list order
    std::vector<CityInfo> m_cities;      // in MM2's city list order
};

std::optional<VehicleInfo> parseVehicleInfo(std::string_view text);
std::optional<CityInfo> parseCityInfo(std::string_view name, std::string_view text);

} // namespace mm2::game
