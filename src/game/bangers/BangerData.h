/*
    OpenMM2 - breakable/knock-over props ("bangers"): their tuning data.
    Field meanings from Open1560's mmBangerData (code/midtown/mmbangers/data.h,
    Copyright (C) 2020 Brick, GPL-3.0-or-later) and MM2's tune/banger files;
    mm2hook's dgBangerData layout was used as documentation only.
*/
#pragma once

#include "game/fx/BirthRule.h"
#include "vfs/Vfs.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mm2::game::bangers {

// tune/banger/<model>.dgBangerData. Breakable parts are separate entries
// named "<model>_break01", "<model>_break02" ...
struct BangerData {
    std::string name;
    Vec3 size{1, 1, 1};  // collision/inertia box
    Vec3 cg;             // centre of gravity above the model's ground origin; meshes are centred on it
    std::vector<Vec3> glowOffsets; // light glows relative to the CG (NumGlows / GlowOffset)
    float mass = 1.0f;
    float elasticity = 0.5f;
    float friction = 0.9f;
    float impulseLimit2 = 0.0f; // square of the largest impulse a hit transfers to the prop
    float yRadius = 0.0f;
    int spinAxis = 0;
    int flash = 0;
    int numParts = 0;
    int texNumber = 0;   // particle sheet texture/fxpt<N> for the debris, 0 = none
    int billFlags = 0;
    int colliderId = 0;
    int collisionPrim = 1; // 1 box, 2 cylinder (YRadius), 0 none (inferred)
    int collisionType = 16;
    int audioId = 0;
    std::optional<fx::BirthRule> birthRule;
};

std::optional<BangerData> parseBangerData(std::string_view name, std::string_view text, std::string* error = nullptr);

// Every tune/banger entry, loaded on demand.
class BangerDataLibrary {
public:
    explicit BangerDataLibrary(const vfs::Vfs& vfs);

    // Null when the model has no banger data.
    const BangerData* find(std::string_view model) const;
    // Part i (0-based) of a breakable banger: "<model>_break%02d".
    const BangerData* part(std::string_view model, int i) const;
    bool has(std::string_view model) const;
    std::size_t available() const { return m_names.size(); }
    std::vector<std::string> names() const;

private:
    const vfs::Vfs& m_vfs;
    std::map<std::string, std::string, std::less<>> m_names; // lower-case name -> path
    mutable std::map<std::string, std::optional<BangerData>, std::less<>> m_cache;
};

} // namespace mm2::game::bangers
