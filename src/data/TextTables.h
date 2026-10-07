#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mm2::data {

// "Key=Value" files: tune/*.info (vehicles), tune/*.cinfo (cities).
// Keys may contain spaces ("Top Speed"); lookups are case-insensitive.
// Values are trimmed. Lines without '=' are ignored.
class KeyValueFile {
public:
    static KeyValueFile parse(std::string_view text);

    std::optional<std::string> get(std::string_view key) const;
    std::string getString(std::string_view key, std::string_view fallback = {}) const;
    int getInt(std::string_view key, int fallback = 0) const;
    float getFloat(std::string_view key, float fallback = 0.0f) const;
    // Splits a '|' separated list ("Colors=Yellow|Blue").
    std::vector<std::string> getList(std::string_view key) const;

    const std::vector<std::pair<std::string, std::string>>& entries() const { return m_entries; }

private:
    std::vector<std::pair<std::string, std::string>> m_entries;
};

// Comma separated table with a header row (race/*.csv, tune/menu.csv, aud/*.csv).
// Cells are trimmed; no quoting is used by the game files.
class CsvTable {
public:
    // When `hasHeader` is false, rows() contains every line.
    static CsvTable parse(std::string_view text, bool hasHeader = true);

    const std::vector<std::string>& header() const { return m_header; }
    const std::vector<std::vector<std::string>>& rows() const { return m_rows; }
    // Column index by case-insensitive header name, or -1.
    int column(std::string_view name) const;

    std::string_view cell(std::size_t row, std::size_t col) const;
    float cellFloat(std::size_t row, std::size_t col, float fallback = 0.0f) const;
    int cellInt(std::size_t row, std::size_t col, int fallback = 0) const;

private:
    std::vector<std::string> m_header;
    std::vector<std::vector<std::string>> m_rows;
};

// Splits text into lines, stripping '\r' and dropping a UTF-8 BOM.
std::vector<std::string_view> splitLines(std::string_view text);

} // namespace mm2::data
