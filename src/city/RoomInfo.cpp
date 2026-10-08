// lvlRoomInfo's flags after cityLevel::Load and lvlLevel::LoadInstances
// (midtown2.exe build 3393, MM2Recomp; documentation only).
#include "city/RoomInfo.h"

#include "city/Environment.h"
#include "city/Inst.h"
#include "city/Psdl.h"

namespace mm2::city {
namespace {

// lvlInstance flag of an instance with its own terrain bound
// (lvlInstance::InitBoundTerrainLocal).
constexpr std::uint16_t kInstTerrainLocal = 0x100;

// lvlSDL::LoadBinary's material value of textures whose materials.csv row
// names lvlMaterialMgr's second entry (deepwater in the retail materials.mtl).
constexpr std::uint8_t kWaterOfDeathMaterial = 2;

// cityLevel::Load's warp rooms, written into the code for a city whose name
// contains "sf".
constexpr std::uint16_t kSfWarpRooms[] = {411, 412, 423, 625};

// cityLevel::Load's OpenRoad test for a road room: the first attribute that
// is neither a texture nor a tunnel decides; a divided road, a warp room or a
// tunnel before it with either of its header's two low bits set clears it.
// (MM2 steps over the texture and tunnel attributes by their usual sizes; a
// room holding nothing else is not marked here.)
bool openRoad(const PsdlRoom& room) {
    const PsdlAttribute* tunnel = nullptr;
    for (const auto& a : room.attributes) {
        if (a.type == PsdlAttrType::Texture)
            continue;
        if (a.type == PsdlAttrType::Tunnel) {
            tunnel = &a;
            continue;
        }
        if (a.type == PsdlAttrType::DividedRoadStrip || (room.flags & RoomFlag::Warp))
            return false;
        return tunnel == nullptr || (tunnel->subtype & 3) == 0;
    }
    return false;
}

} // namespace

std::vector<std::uint16_t> levelRoomFlags(const Psdl& psdl, std::span<const std::uint8_t> textureMaterials,
                                          const WaterDef* water, std::span<const Instance> instances,
                                          std::span<const Instance> aiInstances, std::string_view mapName) {
    std::vector<std::uint16_t> flags(psdl.rooms.size(), 0);
    for (std::size_t r = 1; r < psdl.rooms.size(); ++r) {
        const PsdlRoom& room = psdl.rooms[r];
        std::uint16_t& f = flags[r];
        if (room.flags & RoomFlag::Intersection) {
            if (!(room.flags & RoomFlag::Warp))
                f |= LevelRoomFlag::OpenRoad;
        } else if (room.flags & RoomFlag::Road) {
            if (openRoad(room))
                f |= LevelRoomFlag::OpenRoad;
        }
        if (room.flags & RoomFlag::Subterranean)
            f |= LevelRoomFlag::Subterranean | LevelRoomFlag::Covered;
        // "Room %d has Water of Death(tm) [from SDL]": the first attribute
        // only, and only when it is a texture.
        if (!room.attributes.empty() && room.attributes.front().type == PsdlAttrType::Texture &&
            !room.attributes.front().args.empty()) {
            const std::size_t value = static_cast<std::size_t>(room.attributes.front().textureBase() + 1);
            if (value < textureMaterials.size() && textureMaterials[value] == kWaterOfDeathMaterial)
                f |= LevelRoomFlag::WaterOfDeath;
        }
    }
    if (mapName.find("sf") != std::string_view::npos)
        for (const auto room : kSfWarpRooms)
            if (room < flags.size())
                flags[room] |= LevelRoomFlag::Warp;
    // "Room %d has Water of Death(tm) [from .water file]": ids in range.
    if (water)
        for (const int room : water->rooms)
            if (room > 0 && static_cast<std::size_t>(room) < flags.size())
                flags[static_cast<std::size_t>(room)] |= LevelRoomFlag::WaterOfDeath;
    // lvlLevel::LoadInstances (city/<map>.inst, then city/<map>_ai.inst).
    for (const auto list : {instances, aiInstances})
        for (const auto& inst : list)
            if ((inst.flags & kInstTerrainLocal) && inst.room < flags.size())
                flags[inst.room] |= LevelRoomFlag::TerrainInstance;
    return flags;
}

} // namespace mm2::city
