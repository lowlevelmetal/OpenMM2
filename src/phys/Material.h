#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mm2::phys {

// Physical surface material (Angel phMaterial / MM2 lvlMaterial).
//
// Source: city/materials.mtl and the "mtl" blocks of .bnd files:
//
//   mtl grass {
//     elasticity: 0.9   friction: 0.9   effect: none   sound: 2
//     drag: 0.0   width: 0.45   height: 0.04   depth: 0.1
//     ptxindex: 1 2   ptxthreshold: 0.25 0.5
//   }
//
// Field meanings (evidence in docs/physics.md):
//   elasticity, friction  collision restitution / friction (phMaterial)
//   drag                  rolling drag applied to wheels on this surface
//   width, height         wheel bump wavelength (m) and amplitude (m)
//   depth                 how far wheels sink into the surface (m)
//   sound                 surface sound index (0 road, 1 water, 2 grass)
//   ptxindex/threshold    wheel particle effect indices and slip thresholds
// The latter four map to vehWheel::MaterialDrag/Width/Height/Depth.
//
// The member defaults are OpenMM2's (test ground); MM2's material defaults
// are lvlMaterialDefault()'s.
struct Material {
    std::string name = "_default";
    float elasticity = 0.9f;
    float friction = 0.9f;
    std::string effect = "none";
    int sound = 0;
    float drag = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float depth = 0.0f;
    int ptxIndex[2] = {-1, -1};
    float ptxThreshold[2] = {0.25f, 0.5f};
};

// lvlMaterial's constructor (on top of phMaterial's): elasticity 0.5,
// friction 1, drag 0, width 1, height and depth 0, no particle effects
// (thresholds 0.25 / 0.5), effect and sound index -1, named "default". The
// material manager's default material (lvlMaterialMgr's entry 0, which the
// wheels get for polygons without a material) is one, and a block that
// stops early keeps these values for the fields it leaves out.
Material lvlMaterialDefault();

// Parses every "mtl <name> { ... }" block in `text` (lvlMaterial::Load).
// Unknown keys are ignored. Returns std::nullopt (and sets `error`) on
// malformed input.
std::optional<std::vector<Material>> parseMaterials(std::string_view text, std::string* error = nullptr);

// Material lookup by case-insensitive name, standing in for lvlMaterialMgr's
// table: index 0 is the default material (lvlMaterialDefault(), named
// "default"); unknown names (and "none", which city/materials.csv uses for
// most textures) resolve to it.
class MaterialTable {
public:
    MaterialTable();

    // Adds materials by name. As lvlMaterialMgr::Load, a name already in
    // the table keeps its first definition (so a file's "_default" block is
    // an entry of its own, not the default material).
    void add(const Material& m);
    void add(const std::vector<Material>& ms);

    int find(std::string_view name) const;    // -1 if absent
    int resolve(std::string_view name) const; // falls back to 0
    const Material& operator[](int index) const;
    std::size_t size() const { return m_materials.size(); }

private:
    std::vector<Material> m_materials;
};

} // namespace mm2::phys
