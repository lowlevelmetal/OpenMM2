#include "asset/VehicleModel.h"

#include "core/StringUtil.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace mm2::asset {
namespace {

// Pivots that exist without a mesh part of the same name.
constexpr const char* kPivotOnlyParts[] = {"exhaust0", "exhaust1", "trailer_hitch", "headlight0", "headlight1"};

} // namespace

const Mtx* VehicleModel::pivot(std::string_view part) const {
    const auto it = pivots.find(str::lower(part));
    return it == pivots.end() ? nullptr : &it->second;
}

const VehicleModel::Wheel* VehicleModel::wheel(int index) const {
    for (const auto& w : wheels)
        if (w.index == index)
            return &w;
    return nullptr;
}

std::optional<VehicleModel> loadVehicleModel(std::string_view baseName, const ReadFileFn& read, std::string* error) {
    VehicleModel model;
    model.baseName = str::lower(baseName);
    const std::string pkgPath = std::format("geometry/{}.pkg", model.baseName);
    auto bytes = read(pkgPath);
    if (!bytes) {
        if (error)
            *error = std::format("'{}' not found", pkgPath);
        return std::nullopt;
    }
    std::string pkgError;
    auto pkg = parsePkg(*bytes, &pkgError);
    if (!pkg) {
        if (error)
            *error = std::format("{}: {}", pkgPath, pkgError);
        return std::nullopt;
    }
    model.pkg = std::move(*pkg);

    std::vector<std::string> candidates;
    for (const auto& part : model.pkg.parts())
        if (!part.empty())
            candidates.push_back(str::lower(part));
    for (const char* p : kPivotOnlyParts)
        candidates.emplace_back(p);
    for (const auto& part : candidates) {
        if (model.pivots.contains(part))
            continue;
        auto mtxBytes = read(std::format("geometry/{}_{}.mtx", model.baseName, part));
        if (!mtxBytes)
            continue;
        if (auto mtx = parseMtx(*mtxBytes))
            model.pivots.emplace(part, *mtx);
    }

    for (int i = 0; i < 6; ++i) {
        const Mtx* m = model.pivot(std::format("whl{}", i));
        if (!m)
            continue;
        // vehWheel::Init: radius |(max.y - min.y) * 0.5|, width max.x - min.x.
        model.wheels.push_back({i, m->origin, std::abs((m->max.y - m->min.y) * 0.5f), m->max.x - m->min.x});
    }
    return model;
}

} // namespace mm2::asset
