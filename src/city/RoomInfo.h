#pragma once

// The game's own room flags (lvlRoomInfo +0), after midtown2.exe build 3393
// (MM2Recomp, documentation only). cityLevel::Load starts every room's flags
// at 0 and derives them from the PSDL, the city's materials and its .water
// file; lvlLevel::LoadInstances and gizBridge::Init add to them. They are not
// the PSDL's room flags (Psdl.h's RoomFlag), which lvlSDL keeps apart:
// mmPlayer::Update, dgPhysManager::Collide, vehCar and aiPoliceOfficer read
// these.

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace mm2::city {

struct Instance;
struct Psdl;
struct WaterDef;

namespace LevelRoomFlag {
// cityLevel::Load: an intersection that is not a warp room, or a road whose
// first attribute after its texture and tunnel attributes is not a divided
// road and whose tunnel (if any) has none of the header's two low bits.
// Read only by dgPhysManager::CollideTerrain, behind a switch mmGame keeps
// off.
inline constexpr std::uint16_t OpenRoad = 0x01;
// cityLevel::Load: a PSDL subterranean room gets both.
inline constexpr std::uint16_t Subterranean = 0x02;
inline constexpr std::uint16_t Covered = 0x08;
// "Water of Death": the room's first attribute selects a texture whose
// material is lvlMaterialMgr's second entry (deepwater), or the room is
// listed in city/<map>.water. vehCar sinks there below the water level;
// aiPoliceOfficer gives up a pursuit there.
inline constexpr std::uint16_t WaterOfDeath = 0x04;
// gizBridge::Init: the rooms at a bridge's position and 5 m above it (set
// at run time by the bridge gizmo, not here).
inline constexpr std::uint16_t Bridge = 0x10;
// lvlLevel::LoadInstances: the room of an instance with its own terrain
// bound (instance flag 0x100).
inline constexpr std::uint16_t TerrainInstance = 0x20;
// cityLevel::Load: rooms 411, 412, 423 and 625 of a city whose name
// contains "sf", whose wheel probes also test the instances of the room
// dgPhysManager::Collide pairs them with.
inline constexpr std::uint16_t Warp = 0x40;
} // namespace LevelRoomFlag

// The flags cityLevel::Load and lvlLevel::LoadInstances give each room of
// `psdl` (index = room id; room 0 has none). `textureMaterials` is lvlSDL's
// texture -> material table (sdlTextureMaterials), `instances` and
// `aiInstances` the records of city/<map>.inst and city/<map>_ai.inst, and
// `mapName` the city's map name (cityLevel::Load's name).
std::vector<std::uint16_t> levelRoomFlags(const Psdl& psdl, std::span<const std::uint8_t> textureMaterials,
                                          const WaterDef* water, std::span<const Instance> instances,
                                          std::span<const Instance> aiInstances, std::string_view mapName);

} // namespace mm2::city
