#pragma once

#include "asset/Mtx.h"
#include "asset/Pkg.h"

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mm2::asset {

// Reads a game file by virtual path (e.g. "geometry/vpbug.pkg").
using ReadFileFn = std::function<std::optional<std::vector<std::byte>>(std::string_view path)>;

// A vehicle (or any multi-part model) together with the pivots of its parts.
//
// Car part names (chunk "<PART>_<LOD>" in the PKG; pivot in
// geometry/<base>_<part>.mtx, lower case). vehCarModel::Init asks for these,
// in this order: body, shadow, hlight, tlight, rlight, slight0, slight1,
// blight, bodydamage, siren0, siren1, decal, driver, shock0-3, arm0-3,
// shaft2, shaft3, axle0, axle1, engine, whl0-3, break0-3, break01, break12,
// break23, break03, hub0-3, trailer_hitch, srn0-3, headlight0/1, fndr0/1,
// whl4, whl5, variant0-9. Notably:
//   BODY            car body
//   SHADOW          ground shadow quad
//   HLIGHT/TLIGHT/RLIGHT/BLIGHT  head/tail/reverse/brake light glows
//   HEADLIGHT0/1    headlight flare sprites (pivot = lamp position)
//   SLIGHT0/1, SIREN0/1, SRN0-3  siren lights (police and emergency cars)
//   WHL0..WHL5      wheels: 0 front left, 1 front right, 2 rear left,
//                   3 rear right, 4/5 an extra rear axle (semi)
//   TWHL0..TWHL5    trailer wheels (in <base>_trailer.pkg)
//   FNDR0/1         fenders that steer with the front wheels
//   BREAK0-3, BREAK01/12/23/03  detachable parts (vehBreakableMgr)
//   TRAILER, TRAILER_HITCH
// Pivot-only names with no mesh: exhaust0/1 (vehCarDamage), trailer_hitch.
// Dashboards are separate models "<base>_dash" with parts dash, roof, wheel
// (steering wheel), speed_needle, tach_needle, damage_needle, gear_indicator.
// This loader takes every part the package has; which ones are drawn is up
// to the renderer.
struct VehicleModel {
    struct Wheel {
        int index = 0;      // N in WHLN
        Vec3 position;      // wheel centre in model space (pivot)
        float radius = 0;   // half the part's height
        float width = 0;    // the part's extent along X
    };

    std::string baseName;
    Pkg pkg;
    // Pivots keyed by lower-case part name ("whl0", "headlight1", ...).
    std::map<std::string, Mtx, std::less<>> pivots;
    std::vector<Wheel> wheels; // sorted by index

    const Mtx* pivot(std::string_view part) const;
    const Wheel* wheel(int index) const;
};

// Loads geometry/<baseName>.pkg and every matching geometry/<baseName>_<part>.mtx.
std::optional<VehicleModel> loadVehicleModel(std::string_view baseName, const ReadFileFn& read,
                                             std::string* error = nullptr);

} // namespace mm2::asset
