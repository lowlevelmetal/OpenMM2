#pragma once

// Internal bounds-checked little-endian reader for the binary city formats,
// and C-library number parsing for the text ones.
// A failed read sets ok() to false and returns zero; callers check ok() once
// after a block of reads instead of after every field.

#include "core/File.h"
#include "core/Math.h"
#include "core/StringUtil.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mm2::city::detail {

class Reader {
public:
    explicit Reader(std::span<const std::byte> data, std::size_t pos = 0) : m_data(data), m_pos(pos) {}

    std::size_t pos() const { return m_pos; }
    std::size_t size() const { return m_data.size(); }
    std::size_t remaining() const { return m_pos <= m_data.size() ? m_data.size() - m_pos : 0; }
    bool ok() const { return m_ok; }
    bool atEnd() const { return m_ok && m_pos == m_data.size(); }
    bool has(std::size_t n) const { return m_ok && remaining() >= n; }
    void fail() { m_ok = false; }

    void seek(std::size_t pos) {
        if (pos > m_data.size())
            m_ok = false;
        else
            m_pos = pos;
    }
    void skip(std::size_t n) {
        if (!has(n))
            m_ok = false;
        else
            m_pos += n;
    }

    template <class T>
    T read() {
        if (!has(sizeof(T))) {
            m_ok = false;
            return T{};
        }
        T v = loadLE<T>(m_data.data() + m_pos);
        m_pos += sizeof(T);
        return v;
    }
    std::uint8_t u8() { return read<std::uint8_t>(); }
    std::uint16_t u16() { return read<std::uint16_t>(); }
    std::int16_t i16() { return read<std::int16_t>(); }
    std::uint32_t u32() { return read<std::uint32_t>(); }
    float f32() { return read<float>(); }
    Vec3 vec3() {
        const float x = f32(), y = f32();
        return {x, y, f32()};
    }
    bool magic(const char (&tag)[5]) {
        if (!has(4) || std::memcmp(m_data.data() + m_pos, tag, 4) != 0)
            return false;
        m_pos += 4;
        return true;
    }
    // Fixed-size, NUL-padded string field.
    std::string fixedString(std::size_t n) {
        if (!has(n)) {
            m_ok = false;
            return {};
        }
        const char* p = reinterpret_cast<const char*>(m_data.data() + m_pos);
        m_pos += n;
        return std::string(p, strnlen(p, n));
    }
    // Reads `count` elements; refuses absurd counts that cannot fit the data.
    template <class T, class Fn>
    bool readArray(std::size_t count, std::size_t minElemSize, std::vector<T>& out, Fn&& readOne) {
        if (minElemSize && count > remaining() / minElemSize) {
            m_ok = false;
            return false;
        }
        out.clear();
        out.reserve(count);
        for (std::size_t i = 0; i < count && m_ok; ++i)
            out.push_back(readOne());
        return m_ok;
    }

private:
    std::span<const std::byte> m_data;
    std::size_t m_pos = 0;
    bool m_ok = true;
};

// Numbers in the text formats, read the way MM2's loaders read them with the
// C library (atoi, atof, sscanf "%d" / "%f"): leading blanks, an optional
// sign, then the longest decimal prefix; anything after it is ignored, and
// there is no hexadecimal. `length` is how many characters the number took
// (0: none, where atoi/atof give 0 and sscanf fails).
inline double cNumberPrefix(std::string_view s, bool integer, std::size_t* length = nullptr) {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n' || s[i] == '\v' ||
                            s[i] == '\f'))
        ++i;
    std::string digits;
    if (i < s.size() && (s[i] == '+' || s[i] == '-'))
        digits += s[i++];
    std::size_t mantissa = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        digits += s[i++];
        ++mantissa;
    }
    if (!integer && i < s.size() && s[i] == '.') {
        digits += s[i++];
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            digits += s[i++];
            ++mantissa;
        }
    }
    if (mantissa == 0) {
        if (length)
            *length = 0;
        return 0.0;
    }
    if (!integer && i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        std::size_t j = i + 1;
        std::string exponent = "e";
        if (j < s.size() && (s[j] == '+' || s[j] == '-'))
            exponent += s[j++];
        const std::size_t first = j;
        while (j < s.size() && s[j] >= '0' && s[j] <= '9')
            exponent += s[j++];
        if (j > first) {
            digits += exponent;
            i = j;
        }
    }
    if (length)
        *length = i;
    return str::parseDouble(digits).value_or(0.0);
}

// atoi / atof: 0 when there is no number.
inline int cAtoi(std::string_view s) {
    return static_cast<int>(cNumberPrefix(s, true));
}
inline float cAtof(std::string_view s) {
    return static_cast<float>(cNumberPrefix(s, false));
}

// sscanf "%d" / "%f": nothing when there is no number (the target keeps its value).
inline std::optional<int> scanInt(std::string_view s) {
    std::size_t n = 0;
    const double v = cNumberPrefix(s, true, &n);
    return n ? std::optional<int>(static_cast<int>(v)) : std::nullopt;
}
inline std::optional<float> scanFloat(std::string_view s) {
    std::size_t n = 0;
    const double v = cNumberPrefix(s, false, &n);
    return n ? std::optional<float>(static_cast<float>(v)) : std::nullopt;
}

} // namespace mm2::city::detail
