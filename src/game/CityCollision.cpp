#include "game/CityCollision.h"

#include "asset/Bound.h"
#include "city/CityMesh.h"
#include "core/Log.h"
#include "core/StringUtil.h"

#include <unordered_map>

namespace mm2::game {

phys::BoundGeometry toPhysBound(const asset::BoundGeometry& bound, phys::MaterialTable& table) {
    phys::BoundGeometry g;
    g.vertices = bound.vertices;
    for (const auto& m : bound.materials) {
        g.materialNames.push_back(m.name);
        // Bound files declare their surfaces inline; keep the city library's
        // definition when both exist (inferred precedence).
        if (table.find(m.name) < 0 && !str::iequals(m.name, "default") && !str::iequals(m.name, "none")) {
            phys::Material pm;
            pm.name = m.name;
            pm.elasticity = m.elasticity;
            pm.friction = m.friction;
            pm.effect = m.effect;
            table.add(pm);
        }
    }
    if (g.materialNames.empty())
        g.materialNames.push_back("_default");
    for (const auto& p : bound.polygons) {
        phys::BoundGeometry::Poly poly;
        const int n = p.vertexCount();
        for (int i = 0; i < n; ++i)
            poly.v[static_cast<std::size_t>(i)] = p.indices[static_cast<std::size_t>(i)];
        poly.count = static_cast<std::uint8_t>(n);
        poly.material = p.material < g.materialNames.size() ? p.material : 0;
        g.polys.push_back(poly);
    }
    return g;
}

CityCollision buildCityCollision(const city::CityData& city, const vfs::Vfs& vfs,
                                 const std::function<bool(std::string_view)>& isDynamic) {
    CityCollision out;
    if (auto text = vfs.readAll("city/materials.mtl")) {
        std::string error;
        if (auto mats = phys::parseMaterials(std::string_view(reinterpret_cast<const char*>(text->data()), text->size()),
                                             &error))
            out.materials.add(*mats);
        else
            log::warn("collision: city/materials.mtl: {}", error);
    }

    // Street texture -> surface material name (materials.csv).
    std::unordered_map<std::string, std::string> textureMaterial;
    for (const auto& tm : city.textureMaterials)
        textureMaterial[str::lower(tm.texture)] = tm.material;

    // PSDL geometry, one bound per texture/kind batch.
    city::CityMeshOptions opts;
    opts.includeFacadeBounds = true;
    const city::CityMesh mesh = city::buildCityMesh(city.psdl, opts);
    for (const auto& room : mesh.rooms) {
        for (const auto& batch : room.batches) {
            if (batch.indices.empty())
                continue;
            phys::BoundGeometry g;
            std::string material = "_default";
            if (const auto* name = city.psdl.texture(batch.texture); name && !name->empty()) {
                if (auto it = textureMaterial.find(str::lower(*name)); it != textureMaterial.end())
                    material = it->second;
            }
            g.materialNames.push_back(material);
            g.vertices.reserve(batch.vertices.size());
            for (const auto& v : batch.vertices)
                g.vertices.push_back(v.position);
            for (std::size_t i = 0; i + 2 < batch.indices.size(); i += 3) {
                phys::BoundGeometry::Poly p;
                p.v = {batch.indices[i], batch.indices[i + 1], batch.indices[i + 2], 0};
                p.count = 3;
                g.polys.push_back(p);
            }
            out.streetPolygons += static_cast<int>(g.polys.size());
            out.soup.add(g, Mat34::identity(), out.materials);
        }
    }

    // Object bounds, parsed once per model.
    std::unordered_map<std::string, std::optional<phys::BoundGeometry>> cache;
    auto boundFor = [&](const std::string& model) -> const std::optional<phys::BoundGeometry>& {
        const std::string key = str::lower(model);
        if (auto it = cache.find(key); it != cache.end())
            return it->second;
        std::optional<asset::BoundGeometry> parsed;
        if (auto bin = vfs.readAll("bound/" + key + "_bound.bbnd")) {
            parsed = asset::parseBbnd(*bin);
        } else if (auto txt = vfs.readAll("bound/" + key + "_bound.bnd")) {
            parsed = asset::parseBnd(std::string_view(reinterpret_cast<const char*>(txt->data()), txt->size()));
        }
        auto& slot = cache[key];
        if (parsed)
            slot = toPhysBound(*parsed, out.materials);
        return slot;
    };
    for (const auto* list : {&city.instances, &city.aiInstances}) {
        for (const auto& inst : *list) {
            if (isDynamic && isDynamic(inst.name))
                continue;
            const auto& bound = boundFor(inst.name);
            if (!bound) {
                ++out.missingBounds;
                continue;
            }
            out.soup.add(*bound, inst.transform, out.materials);
            ++out.instanceBounds;
        }
    }
    out.soup.finalize();
    log::info("collision: {} street polygons, {} object bounds ({} objects without one), {} total", out.streetPolygons,
              out.instanceBounds, out.missingBounds, out.soup.size());
    return out;
}

} // namespace mm2::game
