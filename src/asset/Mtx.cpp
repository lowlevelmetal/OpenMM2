#include "asset/Mtx.h"

#include "asset/Reader.h"

#include <format>

namespace mm2::asset {

std::optional<Mtx> parseMtx(std::span<const std::byte> data, std::string* error) {
    // GetPivot reads the first 48 bytes; anything after is ignored. A shorter
    // file would leave part of MM2's matrix unset and is rejected here.
    if (data.size() < 48) {
        if (error)
            *error = std::format("expected 48 bytes, got {}", data.size());
        return std::nullopt;
    }
    detail::Reader r(data);
    Mtx m;
    m.min = r.vec3();
    m.max = r.vec3();
    m.center = r.vec3();
    m.origin = r.vec3();
    return m;
}

} // namespace mm2::asset
