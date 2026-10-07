#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

// Little-endian byte buffer builder for synthetic test files.
struct Bytes {
    std::vector<std::byte> data;

    Bytes& u8(std::uint8_t v) {
        data.push_back(std::byte{v});
        return *this;
    }
    Bytes& u16(std::uint16_t v) { return u8(v & 0xFF).u8(v >> 8); }
    Bytes& u32(std::uint32_t v) { return u16(v & 0xFFFF).u16(v >> 16); }
    Bytes& f32(float f) {
        std::uint32_t v;
        std::memcpy(&v, &f, 4);
        return u32(v);
    }
    Bytes& str(std::string_view s) {
        for (char c : s)
            u8(static_cast<std::uint8_t>(c));
        return *this;
    }
    Bytes& fixed(std::string_view s, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i)
            u8(i < s.size() ? static_cast<std::uint8_t>(s[i]) : 0);
        return *this;
    }
    Bytes& append(const Bytes& o) {
        data.insert(data.end(), o.data.begin(), o.data.end());
        return *this;
    }
    std::size_t size() const { return data.size(); }
};
