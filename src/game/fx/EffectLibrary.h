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

    // Surface effects named by the ptxindex pairs of city/materials.mtl.
    // The index -> effect mapping is inferred from which materials use which
    // indices (road 4 = tyre smoke, grass 1/2 = dirt/grass, sand 1/5 =
    // dirt/dust, cobblestone 4/7 = smoke/rock, water 6 = splash):
    //   0 default, 1 dirt, 2 grass, 3 leaf, 4 smoke, 5 dust, 6 splash, 7 rock, 8 snow
    const BirthRule* surfaceRule(int ptxIndex) const;
    static const char* surfaceRuleName(int ptxIndex);

    // texture/ptx_wheel.tex: 256x256, an 8x8 grid of 32x32 frames (verified:
    // the rules' frame ranges land on the matching pictures: dirt 0-1, dust
    // 2-5, grass 6-9, leaves 10-13, smoke 14, splash 16-21).
    static ParticleSheet wheelSheet() { return {"ptx_wheel", 8, 8}; }
    // texture/ptx_rain: 4x4 frames (rain uses frames 0-15 with kSplashes).
    static ParticleSheet rainSheet() { return {"ptx_rain", 4, 4}; }
    // Banger debris: texture/fxpt<N> (dgBangerData::TexNumber N, 1-based;
    // MM2's dgBangerDataManager keeps 20 such sheets), 2x2 frames as MM1's
    // mmBangerActive particles (verified visually: coins, trash, papers in
    // quadrants).
    static ParticleSheet bangerSheet(int texNumber) { return {"fxpt" + std::to_string(texNumber), 2, 2}; }

private:
    std::map<std::string, BirthRule, std::less<>> m_rules;
};

} // namespace mm2::game::fx
