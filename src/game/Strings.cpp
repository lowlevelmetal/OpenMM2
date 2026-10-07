#include "game/Strings.h"

#include "core/Log.h"

namespace mm2::game {

Strings Strings::load(const vfs::GameSource& source) {
    Strings s;
    std::string error;
    auto file = vfs::openSourceFile(source, vfs::kLanguageModule, &error);
    if (!file) {
        log::warn("strings: {}", error);
        return s;
    }
    auto bytes = file->readAll();
    auto table = data::readPeStringTable(bytes, 0, &error);
    if (!table) {
        log::warn("strings: {}: {}", vfs::kLanguageModule, error);
        return s;
    }
    log::info("strings: {} entries (language 0x{:04x})", table->strings.size(), table->language);
    s.m_table = std::move(*table);
    return s;
}

Strings Strings::fromTable(data::PeStringTable table) {
    Strings s;
    s.m_table = std::move(table);
    return s;
}

std::string Strings::get(std::uint32_t id, std::string_view fallback) const {
    if (const auto* text = m_table.find(id))
        return *text;
    return std::string(fallback);
}

} // namespace mm2::game
