#pragma once

// How MM2's audio loaders read their CSV tables: Stream::fgets one line at a
// time, strtok(line, "\r\n") to drop the line end, then strtok(..., ",") for the
// cells and atof / atoi for the numbers (vehCarAudio::Load,
// vehSurfaceAudioData::ParseCSVBuffer, AudImpactData::ReadCSV, ...). Two
// consequences matter for odd entries: strtok skips empty cells, so ",," does
// not leave a blank column, and atof / atoi read the numeric prefix of a cell
// ("0.9x" is 0.9, "abc" is 0) where a strict parser would reject it.

#include <string_view>
#include <vector>

namespace mm2::audio {

// The lines Stream::fgets returns, each cut at its first CR or LF. A final
// line without a newline is a line; nothing after the last newline is not.
std::vector<std::string_view> fgetsLines(std::string_view text);

// strtok(line, delimiters): the non-empty cells in order.
std::vector<std::string_view> strtokFields(std::string_view line, std::string_view delimiters = ",");

// The C runtime's atof / atoi on a cell: leading blanks, an optional sign, then
// as many digits (and, for atof, a decimal point and exponent) as match. A cell
// with no number gives 0; so does a missing cell, where MM2 would pass NULL.
float crtAtof(std::string_view cell);
int crtAtoi(std::string_view cell);

// The i-th cell, or "" when the line has fewer.
inline std::string_view field(const std::vector<std::string_view>& cells, std::size_t i) {
    return i < cells.size() ? cells[i] : std::string_view();
}

} // namespace mm2::audio
