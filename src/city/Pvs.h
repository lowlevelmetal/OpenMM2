#pragma once

// Room-to-room potentially visible sets: city/<map>.cpvs ("PVS0").
// Format notes: docs/formats/cpvs.md.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mm2::city {

class RoomPvs {
public:
    // Number of rooms including the dummy room 0 (matches Psdl::roomCount()).
    std::size_t roomCount() const { return m_roomCount; }

    // Two bits per (from, to) pair; retail files only use 0 and 3.
    int level(std::size_t from, std::size_t to) const;
    bool visible(std::size_t from, std::size_t to) const { return level(from, to) != 0; }
    // Rooms visible from `from` (excluding none-visible ones).
    std::vector<std::uint16_t> visibleFrom(std::size_t from) const;
    bool hasData(std::size_t from) const { return from < m_rows.size() && !m_rows[from].empty(); }

    friend std::optional<RoomPvs> parseCpvs(std::span<const std::byte>, std::string*);

private:
    std::size_t m_roomCount = 0;
    std::vector<std::vector<std::uint8_t>> m_rows; // decompressed, indexed by room
};

std::optional<RoomPvs> parseCpvs(std::span<const std::byte> data, std::string* error = nullptr);

} // namespace mm2::city
