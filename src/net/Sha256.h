#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace mm2::net {

// SHA-256 (FIPS 180-4). Used for the lobby password challenge so passwords
// never cross the network in plain text.
class Sha256 {
public:
    using Digest = std::array<std::byte, 32>;

    Sha256();
    void update(std::span<const std::byte> data);
    void update(std::string_view text) { update(std::as_bytes(std::span(text.data(), text.size()))); }
    Digest finish();

    static Digest hash(std::span<const std::byte> data) {
        Sha256 h;
        h.update(data);
        return h.finish();
    }

private:
    void block(const std::uint8_t* p);

    std::uint32_t m_state[8];
    std::uint8_t m_buffer[64];
    std::uint64_t m_length = 0;
    std::size_t m_fill = 0;
};

} // namespace mm2::net
