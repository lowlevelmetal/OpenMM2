#include "net/BitStream.h"

#include <bit>
#include <cmath>
#include <cstring>

namespace mm2::net {

// --- BitWriter ---------------------------------------------------------------

void BitWriter::writeBits(std::uint32_t value, int bits) {
    if (bits < 32)
        value &= (1u << bits) - 1u;
    for (int i = 0; i < bits;) {
        const std::size_t byte = m_bits / 8;
        const int offset = static_cast<int>(m_bits % 8);
        if (byte >= m_data.size())
            m_data.push_back(std::byte{0});
        const int take = std::min(8 - offset, bits - i);
        const std::uint32_t chunk = (value >> i) & ((1u << take) - 1u);
        m_data[byte] |= static_cast<std::byte>(chunk << offset);
        i += take;
        m_bits += static_cast<std::size_t>(take);
    }
}

void BitWriter::writeU64(std::uint64_t v) {
    writeU32(static_cast<std::uint32_t>(v));
    writeU32(static_cast<std::uint32_t>(v >> 32));
}

void BitWriter::writeVarU32(std::uint32_t v) { writeVarU64(v); }

void BitWriter::writeVarU64(std::uint64_t v) {
    do {
        std::uint32_t byte = static_cast<std::uint32_t>(v & 0x7F);
        v >>= 7;
        if (v)
            byte |= 0x80;
        writeBits(byte, 8);
    } while (v);
}

void BitWriter::writeVarS32(std::int32_t v) {
    const auto u = static_cast<std::uint32_t>(v);
    writeVarU32((u << 1) ^ static_cast<std::uint32_t>(v >> 31));
}

void BitWriter::writeF32(float v) { writeU32(std::bit_cast<std::uint32_t>(v)); }

void BitWriter::writeQuantized(float v, float min, float max, int bits) {
    const std::uint32_t steps = bits >= 32 ? 0xFFFFFFFFu : (1u << bits) - 1u;
    if (!(v >= min)) // also catches NaN
        v = min;
    if (v > max)
        v = max;
    const double t = (static_cast<double>(v) - min) / (static_cast<double>(max) - min);
    writeBits(static_cast<std::uint32_t>(std::llround(t * steps)), bits);
}

void BitWriter::writeBytes(std::span<const std::byte> bytes) {
    for (std::byte b : bytes)
        writeU8(std::to_integer<std::uint8_t>(b));
}

void BitWriter::align() {
    const int pad = static_cast<int>((8 - m_bits % 8) % 8);
    if (pad)
        writeBits(0, pad);
}

// --- BitReader ---------------------------------------------------------------

std::uint32_t BitReader::readBits(int bits) {
    if (m_error || bits <= 0 || bits > 32 || m_pos + static_cast<std::size_t>(bits) > m_data.size() * 8) {
        m_error = true;
        return 0;
    }
    std::uint32_t value = 0;
    for (int i = 0; i < bits;) {
        const std::size_t byte = m_pos / 8;
        const int offset = static_cast<int>(m_pos % 8);
        const int take = std::min(8 - offset, bits - i);
        const std::uint32_t chunk = (std::to_integer<std::uint32_t>(m_data[byte]) >> offset) & ((1u << take) - 1u);
        value |= chunk << i;
        i += take;
        m_pos += static_cast<std::size_t>(take);
    }
    return value;
}

std::uint64_t BitReader::readU64() {
    const std::uint64_t lo = readU32();
    const std::uint64_t hi = readU32();
    return lo | (hi << 32);
}

std::uint32_t BitReader::readVarU32() {
    const std::uint64_t v = readVarU64();
    if (v > 0xFFFFFFFFu) {
        m_error = true;
        return 0;
    }
    return static_cast<std::uint32_t>(v);
}

std::uint64_t BitReader::readVarU64() {
    std::uint64_t v = 0;
    for (int shift = 0; shift < 70; shift += 7) {
        const std::uint32_t byte = readBits(8);
        if (m_error)
            return 0;
        if (shift == 63 && (byte & 0x7E)) { // would overflow 64 bits
            m_error = true;
            return 0;
        }
        v |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
        if (!(byte & 0x80))
            return v;
    }
    m_error = true;
    return 0;
}

std::int32_t BitReader::readVarS32() {
    const std::uint32_t u = readVarU32();
    return static_cast<std::int32_t>((u >> 1) ^ (0u - (u & 1u)));
}

float BitReader::readF32() { return std::bit_cast<float>(readU32()); }

float BitReader::readQuantized(float min, float max, int bits) {
    const std::uint32_t steps = bits >= 32 ? 0xFFFFFFFFu : (1u << bits) - 1u;
    const std::uint32_t q = readBits(bits);
    return static_cast<float>(min + (static_cast<double>(max) - min) * (static_cast<double>(q) / steps));
}

bool BitReader::readBytes(std::span<std::byte> out) {
    for (auto& b : out)
        b = static_cast<std::byte>(readU8());
    return ok();
}

void BitReader::align() {
    const int pad = static_cast<int>((8 - m_pos % 8) % 8);
    if (pad)
        readBits(pad);
}

// --- Streams -----------------------------------------------------------------

namespace {

constexpr float kQuatRange = 0.70710678118f; // 1/sqrt(2): bound of the three smallest components

} // namespace

void WriteStream::quat(Quat& q) {
    Quat n = q.normalized();
    const float c[4] = {n.x, n.y, n.z, n.w};
    int largest = 0;
    for (int i = 1; i < 4; ++i)
        if (std::abs(c[i]) > std::abs(c[largest]))
            largest = i;
    const float sign = c[largest] < 0.0f ? -1.0f : 1.0f;
    m_w.writeBits(static_cast<std::uint32_t>(largest), 2);
    for (int i = 0; i < 4; ++i)
        if (i != largest)
            m_w.writeQuantized(c[i] * sign, -kQuatRange, kQuatRange, kQuatComponentBits);
}

void WriteStream::ranged(std::int32_t& v, std::int32_t min, std::int32_t max) {
    const std::int32_t clamped = std::clamp(v, min, max);
    m_w.writeBits(static_cast<std::uint32_t>(clamped - min), bitsRequired(static_cast<std::uint32_t>(max - min)));
}

void WriteStream::string(std::string& s, std::size_t maxLength) {
    const std::size_t len = std::min(s.size(), maxLength);
    m_w.writeVarU32(static_cast<std::uint32_t>(len));
    m_w.writeBytes(std::as_bytes(std::span(s.data(), len)));
}

void WriteStream::bytes(std::vector<std::byte>& b, std::size_t maxLength) {
    const std::size_t len = std::min(b.size(), maxLength);
    m_w.writeVarU32(static_cast<std::uint32_t>(len));
    m_w.writeBytes(std::span(b.data(), len));
}

void ReadStream::f32(float& v) {
    v = m_r.readF32();
    if (!std::isfinite(v)) {
        m_r.fail();
        v = 0.0f;
    }
}

void ReadStream::quat(Quat& q) {
    const int largest = static_cast<int>(m_r.readBits(2));
    float c[4] = {};
    float sum = 0.0f;
    for (int i = 0; i < 4; ++i) {
        if (i == largest)
            continue;
        c[i] = m_r.readQuantized(-kQuatRange, kQuatRange, kQuatComponentBits);
        sum += c[i] * c[i];
    }
    c[largest] = std::sqrt(std::max(0.0f, 1.0f - sum));
    q = Quat{c[0], c[1], c[2], c[3]}.normalized();
}

void ReadStream::ranged(std::int32_t& v, std::int32_t min, std::int32_t max) {
    const std::uint32_t raw = m_r.readBits(bitsRequired(static_cast<std::uint32_t>(max - min)));
    if (raw > static_cast<std::uint32_t>(max - min)) {
        m_r.fail();
        v = min;
        return;
    }
    v = min + static_cast<std::int32_t>(raw);
}

void ReadStream::string(std::string& s, std::size_t maxLength) {
    const std::uint32_t len = m_r.readVarU32();
    if (len > maxLength || len * 8ull > m_r.bitsRemaining()) {
        m_r.fail();
        s.clear();
        return;
    }
    s.resize(len);
    m_r.readBytes(std::as_writable_bytes(std::span(s.data(), s.size())));
}

void ReadStream::bytes(std::vector<std::byte>& b, std::size_t maxLength) {
    const std::uint32_t len = m_r.readVarU32();
    if (len > maxLength || len * 8ull > m_r.bitsRemaining()) {
        m_r.fail();
        b.clear();
        return;
    }
    b.resize(len);
    m_r.readBytes(b);
}

} // namespace mm2::net
