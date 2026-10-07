#pragma once

// Internal bounds-checked little-endian reader for binary asset formats.

#include "core/File.h"
#include "core/Math.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace mm2::asset::detail {

class Reader {
public:
    explicit Reader(std::span<const std::byte> data, std::size_t pos = 0) : m_data(data), m_pos(pos) {}

    std::size_t pos() const { return m_pos; }
    std::size_t size() const { return m_data.size(); }
    std::size_t remaining() const { return m_pos <= m_data.size() ? m_data.size() - m_pos : 0; }
    bool ok() const { return m_ok; }
    bool has(std::size_t n) const { return m_ok && remaining() >= n; }
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
    std::uint32_t u32() { return read<std::uint32_t>(); }
    float f32() { return read<float>(); }
    Vec2 vec2() {
        const float x = f32();
        return {x, f32()};
    }
    Vec3 vec3() {
        const float x = f32(), y = f32();
        return {x, y, f32()};
    }
    Vec4 vec4() {
        const float x = f32(), y = f32(), z = f32();
        return {x, y, z, f32()};
    }

    // Fixed-size, NUL-padded string field.
    std::string fixedString(std::size_t n) {
        if (!has(n)) {
            m_ok = false;
            return {};
        }
        const char* p = reinterpret_cast<const char*>(m_data.data() + m_pos);
        std::size_t len = 0;
        while (len < n && p[len] != '\0')
            ++len;
        m_pos += n;
        return std::string(p, len);
    }

    std::span<const std::byte> bytes(std::size_t n) {
        if (!has(n)) {
            m_ok = false;
            return {};
        }
        auto s = m_data.subspan(m_pos, n);
        m_pos += n;
        return s;
    }

private:
    std::span<const std::byte> m_data;
    std::size_t m_pos = 0;
    bool m_ok = true;
};

} // namespace mm2::asset::detail
