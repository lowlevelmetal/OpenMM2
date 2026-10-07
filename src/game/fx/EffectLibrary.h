#pragma once

#include "game/fx/BirthRule.h"
#include "vfs/Vfs.h"

#include <map>
#include <string>
#include <string_view>

namespace mm2::game::fx {

// A particle sheet: texture name and its frame grid.
struct ParticleSheet {
    std::string texture;
    int framesWide = 1;
    int framesHigh = 1;
};

// The particle data of the game: birth rules from tune/effects/*.asbirthrule
// (wheel/surface effects and engine smoke) and tune/rain|snow.asbirthrule,
// plus the sheets they draw from.
class EffectLibrary {
public:
    void load(const vfs::Vfs& vfs);

    // Rule by file stem, case-insensitive: "dust", "engine smoke rule",
    // "rain" (tune/rain.asbirthrule) ...
    const BirthRule* rule(std::string_view name) const;
    std::size_t size() const { return m_rules.size(); }

    // vehWheelPtx's rules, by the ptxindex values of city/materials.mtl
    // (vehWheelPtx::PtxName): 0 dirt, 1 dust, 2 grass, 3 leaf, 4 smoke,
    // 5 snow, 6 splash, 7 rock, all from tune/effects.
    static constexpr int kWheelRules = 8;
    const BirthRule* wheelRule(int ptxIndex) const;
    static const char* wheelRuleName(int ptxIndex);

    // texture/ptx_wheel.tex (vehWheelPtx::TexName): an 8x8 grid of frames
    // (vehWheelPtx::Init; the rules' frame ranges land on the matching
    // pictures: dirt 0-1, dust 2-5, grass 6-9, leaves 10-13, smoke 14,
    // splash 16-21, snow 23-24).
    static ParticleSheet wheelSheet() { return {"ptx_wheel", 8, 8}; }
    // texture/ptx_rain: 4x4 frames (the weather particles' Init).
    static ParticleSheet rainSheet() { return {"ptx_rain", 4, 4}; }
    // Banger debris: texture/fxpt<N> (dgBangerData::TexNumber N, 1-based;
    // MM2's dgBangerDataManager keeps 20 such sheets), 2x2 frames
    // (dgBangerActive: asParticles::Init(64, 2, 2)).
    static ParticleSheet bangerSheet(int texNumber) { return {"fxpt" + std::to_string(texNumber), 2, 2}; }

private:
    std::map<std::string, BirthRule, std::less<>> m_rules;
};

} // namespace mm2::game::fx
