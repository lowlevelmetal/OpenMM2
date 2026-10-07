#include "data/TextTables.h"

#include "core/StringUtil.h"

namespace mm2::data {

std::vector<std::string_view> splitLines(std::string_view text) {
    if (text.starts_with("\xEF\xBB\xBF"))
        text.remove_prefix(3);
    std::vector<std::string_view> lines = str::split(text, '\n');
    for (auto& l : lines)
        if (!l.empty() && l.back() == '\r')
            l.remove_suffix(1);
    if (!lines.empty() && lines.back().empty())
        lines.pop_back();
    return lines;
}

KeyValueFile KeyValueFile::parse(std::string_view text) {
    KeyValueFile kv;
    for (std::string_view line : splitLines(text)) {
        const auto eq = line.find('=');
        if (eq == std::string_view::npos)
            continue;
        kv.m_entries.emplace_back(std::string(str::trim(line.substr(0, eq))),
                                  std::string(str::trim(line.substr(eq + 1))));
    }
    return kv;
}

std::optional<std::string> KeyValueFile::get(std::string_view key) const {
    for (const auto& [k, v] : m_entries)
        if (str::iequals(k, key))
            return v;
    return std::nullopt;
}

std::string KeyValueFile::getString(std::string_view key, std::string_view fallback) const {
    auto v = get(key);
    return v ? *v : std::string(fallback);
}

int KeyValueFile::getInt(std::string_view key, int fallback) const {
    auto v = get(key);
    if (!v)
        return fallback;
    if (auto i = str::parseInt(*v))
        return static_cast<int>(*i);
    if (auto d = str::parseDouble(*v))
        return static_cast<int>(*d);
    return fallback;
}

float KeyValueFile::getFloat(std::string_view key, float fallback) const {
    auto v = get(key);
    if (!v)
        return fallback;
    auto d = str::parseDouble(*v);
    return d ? static_cast<float>(*d) : fallback;
}

std::vector<std::string> KeyValueFile::getList(std::string_view key) const {
    std::vector<std::string> out;
    auto v = get(key);
    if (!v || v->empty())
        return out;
    for (auto part : str::split(*v, '|'))
        out.emplace_back(str::trim(part));
    return out;
}

CsvTable CsvTable::parse(std::string_view text, bool hasHeader) {
    CsvTable t;
    bool first = true;
    for (std::string_view line : splitLines(text)) {
        if (str::trim(line).empty())
            continue;
        std::vector<std::string> cells;
        for (auto c : str::split(line, ','))
            cells.emplace_back(str::trim(c));
        if (first && hasHeader)
            t.m_header = std::move(cells);
        else
            t.m_rows.push_back(std::move(cells));
        first = false;
    }
    return t;
}

int CsvTable::column(std::string_view name) const {
    for (std::size_t i = 0; i < m_header.size(); ++i)
        if (str::iequals(m_header[i], name))
            return static_cast<int>(i);
    return -1;
}

std::string_view CsvTable::cell(std::size_t row, std::size_t col) const {
    if (row >= m_rows.size() || col >= m_rows[row].size())
        return {};
    return m_rows[row][col];
}

float CsvTable::cellFloat(std::size_t row, std::size_t col, float fallback) const {
    auto d = str::parseDouble(cell(row, col));
    return d ? static_cast<float>(*d) : fallback;
}

int CsvTable::cellInt(std::size_t row, std::size_t col, int fallback) const {
    const auto c = cell(row, col);
    if (auto i = str::parseInt(c))
        return static_cast<int>(*i);
    if (auto d = str::parseDouble(c))
        return static_cast<int>(*d);
    return fallback;
}

} // namespace mm2::data
