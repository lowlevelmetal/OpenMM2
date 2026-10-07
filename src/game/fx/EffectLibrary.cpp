#include "game/fx/EffectLibrary.h"

#include "core/Log.h"
#include "core/StringUtil.h"

namespace mm2::game::fx {
namespace {

constexpr const char* kWheelRuleNames[] = {"dirt", "dust", "grass", "leaf", "smoke", "snow", "splash", "rock"};

} // namespace

void EffectLibrary::load(const vfs::Vfs& vfs) {
    m_rules.clear();
    for (const auto& e : vfs.listFiles()) {
        if (!e.path.starts_with("tune/") || !e.path.ends_with(".asbirthrule"))
            continue;
        auto bytes = vfs.readAll(e.path);
        if (!bytes)
            continue;
        std::string error;
        auto rule = parseBirthRuleFile(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()),
                                       &error);
        if (!rule) {
            log::warn("fx: {}: {}", e.path, error);
            continue;
        }
        std::string stem = e.path.substr(e.path.rfind('/') + 1);
        stem.resize(stem.size() - std::string_view(".asbirthrule").size());
        // tune/effects/snow and tune/snow both exist: the top-level files are
        // the weather rules and own the plain name; effect rules are always
        // reachable as "effects/<stem>" and by the plain name when no weather
        // rule uses it.
        const bool weather = e.path.find('/', 5) == std::string::npos;
        if (weather) {
            m_rules[stem] = *rule;
        } else {
            m_rules["effects/" + stem] = *rule;
            m_rules.try_emplace(stem, *rule);
        }
    }
    log::debug("fx: {} birth rules", m_rules.size());
}

const BirthRule* EffectLibrary::rule(std::string_view name) const {
    const auto it = m_rules.find(str::lower(name));
    return it == m_rules.end() ? nullptr : &it->second;
}

const char* EffectLibrary::wheelRuleName(int i) {
    static_assert(std::size(kWheelRuleNames) == kWheelRules);
    return i >= 0 && i < kWheelRules ? kWheelRuleNames[i] : nullptr;
}

const BirthRule* EffectLibrary::wheelRule(int i) const {
    const char* name = wheelRuleName(i);
    return name ? rule(std::string("effects/") + name) : nullptr;
}

} // namespace mm2::game::fx
