#include "game/bangers/BangerData.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "data/DatFile.h"

#include <format>

namespace mm2::game::bangers {

std::optional<BangerData> parseBangerData(std::string_view name, std::string_view text, std::string* error) {
    auto file = data::parseDat(text, error);
    if (!file || !file->top()) {
        if (error && error->empty())
            *error = "empty file";
        return std::nullopt;
    }
    const data::DatNode& b = *file->top();
    BangerData d;
    d.name = str::lower(name);
    b.read("Size", d.size);
    b.read("CG", d.cg);
    b.read("Mass", d.mass);
    b.read("Elasticity", d.elasticity);
    b.read("Friction", d.friction);
    b.read("ImpulseLimit2", d.impulseLimit2);
    b.read("YRadius", d.yRadius);
    b.read("SpinAxis", d.spinAxis);
    b.read("Flash", d.flash);
    b.read("NumParts", d.numParts);
    b.read("TexNumber", d.texNumber);
    b.read("BillFlags", d.billFlags);
    b.read("ColliderId", d.colliderId);
    b.read("CollisionPrim", d.collisionPrim);
    b.read("CollisionType", d.collisionType);
    b.read("AudioId", d.audioId);
    for (const auto& c : b.children)
        if (c.name == "GlowOffset" && c.numbers.size() >= 3)
            d.glowOffsets.push_back(
                {static_cast<float>(c.numbers[0]), static_cast<float>(c.numbers[1]), static_cast<float>(c.numbers[2])});
    // dgBangerData::Load: NumGlows is optional and 1 without it.
    int numGlows = 1;
    b.read("NumGlows", numGlows);
    d.glowOffsets.resize(static_cast<std::size_t>(std::max(0, std::min<int>(numGlows, static_cast<int>(d.glowOffsets.size())))));
    if (d.name.find("_tree") != std::string::npos)
        d.billFlags |= BangerData::kTree;
    if (const auto* rule = b.child("BirthRule")) {
        fx::BirthRule r;
        if (fx::loadBirthRule(*rule, r))
            d.birthRule = r;
    }
    if (d.mass <= 0.0f)
        d.mass = 1.0f;
    return d;
}

BangerDataLibrary::BangerDataLibrary(const vfs::Vfs& vfs) : m_vfs(vfs) {
    constexpr std::string_view prefix = "tune/banger/", suffix = ".dgbangerdata";
    for (const auto& e : vfs.listFiles()) {
        if (!e.path.starts_with(prefix) || !e.path.ends_with(suffix))
            continue;
        const std::string stem = e.path.substr(prefix.size(), e.path.size() - prefix.size() - suffix.size());
        if (stem.empty() || stem[0] == '.' || stem.find('/') != std::string::npos)
            continue; // CVS leftovers ("tune/banger/.#giz_...")
        m_names.emplace(stem, e.path);
    }
}

bool BangerDataLibrary::has(std::string_view model) const { return m_names.contains(str::lower(model)); }

std::vector<std::string> BangerDataLibrary::names() const {
    std::vector<std::string> out;
    for (const auto& [n, p] : m_names)
        out.push_back(n);
    return out;
}

const BangerData* BangerDataLibrary::find(std::string_view modelIn) const {
    const std::string model = str::lower(modelIn);
    if (auto it = m_cache.find(model); it != m_cache.end())
        return it->second ? &*it->second : nullptr;
    std::optional<BangerData> data;
    if (auto it = m_names.find(model); it != m_names.end()) {
        if (auto bytes = m_vfs.readAll(it->second)) {
            std::string error;
            data = parseBangerData(model, std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()),
                                   &error);
            if (!data)
                log::warn("bangers: {}: {}", it->second, error);
        }
    }
    auto& slot = m_cache[model] = std::move(data);
    return slot ? &*slot : nullptr;
}

const BangerData* BangerDataLibrary::part(std::string_view model, int i) const {
    return find(std::format("{}_break{:02}", str::lower(model), i + 1));
}

} // namespace mm2::game::bangers
