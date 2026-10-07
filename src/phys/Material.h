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

// Parses every "mtl <name> { ... }" block in `text`. Unknown keys are
// ignored. Returns std::nullopt (and sets `error`) on malformed input.
std::optional<std::vector<Material>> parseMaterials(std::string_view text, std::string* error = nullptr);

// Material lookup by case-insensitive name. Index 0 is always "_default";
// unknown names (and "none", which city/materials.csv uses for most
// textures) resolve to it.
class MaterialTable {
public:
    MaterialTable();

    // Adds or replaces materials by name.
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
