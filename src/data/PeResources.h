#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>

namespace mm2::data {

// String table (RT_STRING) of a Windows PE module, e.g. the game's
// MMLANG.DLL, which holds all localized UI text and the font descriptions
// ("Gill Sans MT, 12, 24, 0, 400"). Only the resource section is read.
struct PeStringTable {
    std::map<std::uint32_t, std::string> strings; // id -> UTF-8 text
    std::uint16_t language = 0;                   // LANGID of the table that was read

    const std::string* find(std::uint32_t id) const {
        auto it = strings.find(id);
        return it == strings.end() ? nullptr : &it->second;
    }
};

// Reads the string table. When several languages are present, `preferredLang`
// (a LANGID, 0 = first found) selects one.
std::optional<PeStringTable> readPeStringTable(std::span<const std::byte> image, std::uint16_t preferredLang = 0,
                                               std::string* error = nullptr);

} // namespace mm2::data
