#include "city/PathSet.h"

#include "city/Reader.h"

#include <format>

namespace mm2::city {

std::optional<PathSet> parsePathSet(std::span<const std::byte> data, std::string* error) {
    auto fail = [&](std::string msg) -> std::optional<PathSet> {
        if (error)
            *error = std::move(msg);
        return std::nullopt;
    };
    detail::Reader r(data);
    if (!r.magic("PTH1"))
        return fail("not a path set (missing PTH1)");
    PathSet set;
    const std::uint32_t count = r.u32();
    set.unknown = r.u32();
    if (!r.ok() || count > r.remaining() / 44)
        return fail("bad path count");
    set.paths.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto& path = set.paths[i];
        path.name = r.fixedString(32);
        const std::uint32_t numPoints = r.u32();
        path.count2 = r.u32();
        path.unknown = r.u32();
        if (!r.ok() || numPoints > r.remaining() / 16)
            return fail(std::format("path {} ('{}'): bad point count", i, path.name));
        path.points.resize(numPoints);
        for (auto& pt : path.points) {
            pt.position = r.vec3();
            pt.extra = r.u32();
        }
    }
    if (!r.ok())
        return fail("truncated path set");
    if (!r.atEnd())
        return fail(std::format("{} unexpected trailing bytes", r.remaining()));
    return set;
}

} // namespace mm2::city
