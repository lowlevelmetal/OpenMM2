#pragma once

// Static instance placement: city/<map>.inst (buildings, props, landmarks),
// city/<map>_ai.inst and <map>.sdl_ai.inst (traffic signs, lights).
// Format notes: docs/formats/inst.md.

#include "core/Math.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mm2::city {

struct Instance {
    std::uint16_t room = 0;  // PSDL room the instance lives in
    std::uint16_t flags = 0; // lvlInstance flags (meaning mostly unknown)
    std::string name;        // model name, e.g. "cw_apt_baywin03_brkred_3s_4_l"
    Mat34 transform;
    // True when stored in the compact Y-rotation form (lvlFixedRotY): an X
    // axis in the XZ plane (whose length may carry a scale) and a position.
    bool rotY = false;
};

std::optional<std::vector<Instance>> parseInst(std::span<const std::byte> data, std::string* error = nullptr);

} // namespace mm2::city
