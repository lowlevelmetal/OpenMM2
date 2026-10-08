#pragma once

#include "core/Math.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>

namespace mm2::asset {

// geometry/<model>_<part>.mtx: placement of a model part (wheels, headlights,
// dashboard needles, breakables, trailer hitch) or of a whole city object
// ("<model>_(null).mtx"). Twelve floats; see docs/formats/mtx.md.
//
// A part's mesh vertices are stored relative to its own origin; `origin` is
// where that origin sits in model space (a wheel centre, a lamp position) or,
// for "(null)" city objects, in world space. There is no rotation.
// min/max/center describe the mesh's box, and occur in two flavours in the
// retail files: relative to the part (center is then usually zero) or already
// offset by `origin` (center == origin). halfExtent() is the same either way.
//
// MM2 reads the 48 bytes with GetPivot straight into a Matrix34 (rows m0 =
// min, m1 = max, m2 = center, m3 = origin) and each caller picks what it
// needs: most take only the position row, vehWheel::Init the box
// (radius |max.y - min.y| / 2, width max.x - min.x), and some (vehAxle,
// vehSuspension, dgBangerData) use the rows as a matrix.
struct Mtx {
    Vec3 min;
    Vec3 max;
    Vec3 center; // centre of [min, max] in the same space
    Vec3 origin; // pivot: where the part's local origin sits in model space

    // Half-size of the part, independent of which space min/max are in.
    Vec3 halfExtent() const { return (max - min) * 0.5f; }
};

std::optional<Mtx> parseMtx(std::span<const std::byte> data, std::string* error = nullptr);

} // namespace mm2::asset
