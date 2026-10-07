#pragma once

#include "vfs/Vfs.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mm2::game {

// Player vehicle description from tune/<basename>.info.
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
    float uiDistance = 0.0f;          // showroom camera distance
    float forceFeedbackModifier = 0.0f;
    float roadForceModifier = 0.0f;

    // Flag meanings are inferred from which cars carry them; see docs/game-data.md.
    static constexpr std::uint32_t kFlagCop = 0x08;   // vpcop: siren, Cops & Robbers cop car
    static constexpr std::uint32_t kFlagLarge = 0x10; // buses and trucks
    static constexpr std::uint32_t kFlagBritish = 0x40;
};

// City description from tune/<name>.cinfo.
struct CityInfo {
    std::string name;          // file stem, "london"
    std::string localizedName; // "London"
    std::string mapName;       // city/<mapName>.psdl etc.
    std::string raceDir;       // race/<raceDir>/
    std::vector<std::string> blitzNames;
    std::vector<std::string> circuitNames;
    std::vector<std::string> checkpointNames;
    int mustPlace = 0;   // parsed, but never read by MM2 (pass rules are fixed, see game/Profile.h)
    int unlockGroup = 0; // parsed, but never read by MM2
};

class Catalog {
public:
    // Loads tune/cars.txt + tune/*.info and tune/*.cinfo. Missing or malformed
    // entries are logged and skipped.
    static Catalog load(const vfs::Vfs& vfs);

    const std::vector<VehicleInfo>& vehicles() const { return m_vehicles; }
    const std::vector<CityInfo>& cities() const { return m_cities; }
    const VehicleInfo* vehicle(std::string_view baseName) const;
    const CityInfo* city(std::string_view name) const;

private:
    std::vector<VehicleInfo> m_vehicles; // in tune/cars.txt order
    std::vector<CityInfo> m_cities;
};

std::optional<VehicleInfo> parseVehicleInfo(std::string_view text);
std::optional<CityInfo> parseCityInfo(std::string_view name, std::string_view text);

} // namespace mm2::game
