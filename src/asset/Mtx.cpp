#include "asset/Mtx.h"

#include "asset/Reader.h"

#include <format>

namespace mm2::asset {

std::optional<Mtx> parseMtx(std::span<const std::byte> data, std::string* error) {
    if (data.size() != 48) {
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
