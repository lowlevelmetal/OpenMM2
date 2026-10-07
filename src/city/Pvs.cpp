#include "city/Pvs.h"

#include "city/Reader.h"

#include <format>

namespace mm2::city {

int RoomPvs::level(std::size_t from, std::size_t to) const {
    if (from >= m_rows.size() || to >= m_roomCount)
        return 0;
    const auto& row = m_rows[from];
    const std::size_t byte = to / 4;
    if (byte >= row.size())
        return 0; // trailing zero bytes are not stored
    return (row[byte] >> ((to % 4) * 2)) & 3;
}

std::vector<std::uint16_t> RoomPvs::visibleFrom(std::size_t from) const {
    std::vector<std::uint16_t> out;
    for (std::size_t to = 0; to < m_roomCount; ++to)
        if (visible(from, to))
            out.push_back(static_cast<std::uint16_t>(to));
    return out;
}

std::optional<RoomPvs> parseCpvs(std::span<const std::byte> data, std::string* error) {
    auto fail = [&](std::string msg) -> std::optional<RoomPvs> {
        if (error)
            *error = std::move(msg);
        return std::nullopt;
    };
    detail::Reader r(data);
    if (!r.magic("PVS0"))
        return fail("not a CPVS file (missing PVS0)");
    // Stored value is the PSDL room count + 1; one offset per real room plus
    // an end offset, all relative to the start of the data.
    const std::uint32_t n = r.u32();
    if (!r.ok() || n < 2 || (n - 1) > r.remaining() / 4)
        return fail("corrupt CPVS header");
    const std::size_t numOffsets = n - 1;
    std::vector<std::uint32_t> offsets(numOffsets);
    for (auto& o : offsets)
        o = r.u32();
    const std::size_t base = r.pos();
    const std::size_t dataSize = r.remaining();
    if (offsets.back() != dataSize)
        return fail(std::format("CPVS end offset {} does not match data size {}", offsets.back(), dataSize));

    RoomPvs pvs;
    pvs.m_roomCount = n - 1;
    const std::size_t rowBytes = (pvs.m_roomCount * 2 + 7) / 8;
    pvs.m_rows.resize(pvs.m_roomCount);
    for (std::size_t i = 0; i + 1 < numOffsets; ++i) {
        const std::size_t begin = offsets[i], end = offsets[i + 1];
        if (begin > end || end > dataSize)
            return fail(std::format("CPVS row {} has bad offsets", i + 1));
        auto& row = pvs.m_rows[i + 1]; // first row belongs to room 1
        // RLE: control byte c; c & 0x80 -> (c & 0x7F) + 1 literal bytes,
        // otherwise c copies of the following byte.
        std::size_t p = base + begin;
        const std::size_t e = base + end;
        while (p < e) {
            const auto c = std::to_integer<std::uint8_t>(data[p++]);
            if (c & 0x80) {
                const std::size_t len = (c & 0x7Fu) + 1;
                if (p + len > e)
                    return fail(std::format("CPVS row {} literal overruns", i + 1));
                for (std::size_t k = 0; k < len; ++k)
                    row.push_back(std::to_integer<std::uint8_t>(data[p + k]));
                p += len;
            } else {
                if (p >= e)
                    return fail(std::format("CPVS row {} run overruns", i + 1));
                row.insert(row.end(), c, std::to_integer<std::uint8_t>(data[p++]));
            }
            if (row.size() > rowBytes)
                return fail(std::format("CPVS row {} decompresses past {} bytes", i + 1, rowBytes));
        }
    }
    return pvs;
}

} // namespace mm2::city
