// MM2's fgets / strtok / atof reading of the audio tables; see TextFields.h.
#include "audio/TextFields.h"

#include <cstdint>
#include <cstdlib>
#include <string>

namespace mm2::audio {
namespace {

bool isBlank(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }

} // namespace

std::vector<std::string_view> fgetsLines(std::string_view text) {
    std::vector<std::string_view> lines;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        const std::size_t next = end == std::string_view::npos ? text.size() : end + 1;
        std::string_view line = text.substr(pos, next - pos);
        // strtok(line, "\r\n"): the line ends at its first CR or LF.
        if (const auto cut = line.find_first_of("\r\n"); cut != std::string_view::npos)
            line = line.substr(0, cut);
        lines.push_back(line);
        pos = next;
    }
    return lines;
}

std::vector<std::string_view> strtokFields(std::string_view line, std::string_view delimiters) {
    std::vector<std::string_view> cells;
    std::size_t pos = 0;
    while (pos < line.size()) {
        pos = line.find_first_not_of(delimiters, pos);
        if (pos == std::string_view::npos)
            break;
        const std::size_t end = line.find_first_of(delimiters, pos);
        cells.push_back(line.substr(pos, end == std::string_view::npos ? std::string_view::npos : end - pos));
        if (end == std::string_view::npos)
            break;
        pos = end + 1;
    }
    return cells;
}

float crtAtof(std::string_view cell) {
    std::size_t i = 0;
    while (i < cell.size() && isBlank(cell[i]))
        ++i;
    const std::size_t start = i;
    if (i < cell.size() && (cell[i] == '+' || cell[i] == '-'))
        ++i;
    std::size_t digits = 0;
    while (i < cell.size() && isDigit(cell[i])) {
        ++i;
        ++digits;
    }
    if (i < cell.size() && cell[i] == '.') {
        ++i;
        while (i < cell.size() && isDigit(cell[i])) {
            ++i;
            ++digits;
        }
    }
    if (digits == 0)
        return 0.0f;
    // An exponent only counts when digits follow it.
    if (i < cell.size() && (cell[i] == 'e' || cell[i] == 'E')) {
        std::size_t j = i + 1;
        if (j < cell.size() && (cell[j] == '+' || cell[j] == '-'))
            ++j;
        if (j < cell.size() && isDigit(cell[j])) {
            while (j < cell.size() && isDigit(cell[j]))
                ++j;
            i = j;
        }
    }
    // atof returns a double that the loaders store as a float.
    const std::string number(cell.substr(start, i - start));
    return static_cast<float>(std::strtod(number.c_str(), nullptr));
}

int crtAtoi(std::string_view cell) {
    std::size_t i = 0;
    while (i < cell.size() && isBlank(cell[i]))
        ++i;
    bool negative = false;
    if (i < cell.size() && (cell[i] == '+' || cell[i] == '-'))
        negative = cell[i++] == '-';
    // The runtime accumulates in 32 bits without an overflow check.
    std::uint32_t value = 0;
    while (i < cell.size() && isDigit(cell[i]))
        value = value * 10u + static_cast<std::uint32_t>(cell[i++] - '0');
    if (negative)
        value = 0u - value;
    return static_cast<std::int32_t>(value);
}

} // namespace mm2::audio
