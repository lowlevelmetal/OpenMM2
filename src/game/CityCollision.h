#pragma once

#include "asset/Bound.h"
#include "city/CityData.h"
#include "phys/Bound.h"
#include "phys/Material.h"
#include "vfs/Vfs.h"

#include <functional>
#include <string_view>

namespace mm2::game {

// Static collision for a city: the PSDL street geometry (roads, sidewalks,
// curbs, walls, roofs, the invisible facade bounds) plus the collision bound
// of every placed object (bound/<model>_bound.bbnd, or .bnd).
struct CityCollision {
    phys::MaterialTable materials;
    phys::PolygonSoup soup;
    int streetPolygons = 0;
    int instanceBounds = 0;
    int missingBounds = 0;
};

// Surface materials come from city/materials.mtl, mapped to street textures
// by city/materials.csv; object bounds carry their own material names.
// `isDynamic` marks instances simulated as separate bodies (bangers), which
// are left out of the static geometry.
CityCollision buildCityCollision(const city::CityData& city, const vfs::Vfs& vfs,
                                 const std::function<bool(std::string_view)>& isDynamic = {});

// Converts a parsed bound file into the physics module's polygon soup input,
// registering any of its materials that `table` does not know yet.
phys::BoundGeometry toPhysBound(const asset::BoundGeometry& bound, phys::MaterialTable& table);

} // namespace mm2::game
