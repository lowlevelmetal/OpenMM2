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
