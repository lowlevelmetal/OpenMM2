#include "ai/VehicleData.h"

#include "core/StringUtil.h"
#include "data/DatFile.h"

#include <format>

namespace mm2::ai {

std::optional<VehicleData> loadVehicleData(const vfs::Vfs& vfs, std::string_view model, std::string* error) {
    const std::string path = std::format("tune/vehicle/{}.aivehicledata", str::lower(model));
    auto bytes = vfs.readAll(path);
    if (!bytes) {
        if (error)
            *error = path + " not found";
        return std::nullopt;
    }
    auto dat =
        data::parseDat(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), error);
    if (!dat || !dat->top())
        return std::nullopt;
    const auto& n = *dat->top();
    VehicleData d;
    d.model = str::lower(model);
    n.read("Mass", d.mass);
    n.read("Size", d.size);
    n.read("MaxAng", d.maxAng);
    n.read("Elasticity", d.elasticity);
    n.read("Friction", d.friction);
    n.read("MaxDamage", d.maxDamage);
    n.read("PtxThresh", d.ptxThresh);
    n.read("Spring", d.spring);
    n.read("Damping", d.damping);
    n.read("Limit", d.limit);
    n.read("RubberSpring", d.rubberSpring);
    n.read("RubberDamp", d.rubberDamp);
    n.read("CG", d.cg);
    return d;
}

} // namespace mm2::ai
