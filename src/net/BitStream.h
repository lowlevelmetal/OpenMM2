#pragma once

// Bit-packed serialization for network messages.
//
// BitWriter/BitReader do the packing. WriteStream/ReadStream wrap them with an
// identical method set taking references, so a message describes its layout
// once:
//
//   template <class S> bool serialize(S& s, Chat& m) {
//       s.u8(m.from);
//       s.string(m.text, kMaxChatLength);
//       return s.ok();
//   }
//
// Readers never read past the buffer: on overrun (or on any value outside the
// range the writer could have produced) the stream enters a sticky error
// state, further reads return zero, and ok() is false.

#include "core/Math.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mm2::net {

class BitWriter {
public:
    BitWriter() { m_data.reserve(128); }

    void writeBits(std::uint32_t value, int bits); // 1..32 bits, LSB first
    void writeBool(bool v) { writeBits(v ? 1u : 0u, 1); }
    void writeU8(std::uint8_t v) { writeBits(v, 8); }
    void writeU16(std::uint16_t v) { writeBits(v, 16); }
    void writeU32(std::uint32_t v) { writeBits(v, 32); }
    void writeU64(std::uint64_t v);
    void writeVarU32(std::uint32_t v); // LEB128, 1-5 bytes
    void writeVarU64(std::uint64_t v); // LEB128, 1-10 bytes
    void writeVarS32(std::int32_t v);  // zig-zag + LEB128
    void writeF32(float v);
    // Maps [min, max] onto `bits` bits (value is clamped first).
    void writeQuantized(float v, float min, float max, int bits);
    void writeBytes(std::span<const std::byte> bytes);
    // Pads with zero bits to the next byte boundary.
    void align();

    std::size_t bitCount() const { return m_bits; }
    std::size_t byteCount() const { return (m_bits + 7) / 8; }
    // The packed bytes (the last byte is zero-padded).
    std::span<const std::byte> data() const { return {m_data.data(), byteCount()}; }
    std::vector<std::byte> take() {
        m_data.resize(byteCount());
        m_bits = 0;
        return std::move(m_data);
    }

private:
    std::vector<std::byte> m_data;
    std::size_t m_bits = 0;
};

class BitReader {
public:
    explicit BitReader(std::span<const std::byte> data) : m_data(data) {}

    std::uint32_t readBits(int bits);
    bool readBool() { return readBits(1) != 0; }
    std::uint8_t readU8() { return static_cast<std::uint8_t>(readBits(8)); }
    std::uint16_t readU16() { return static_cast<std::uint16_t>(readBits(16)); }
    std::uint32_t readU32() { return readBits(32); }
    std::uint64_t readU64();
    std::uint32_t readVarU32();
    std::uint64_t readVarU64();
    std::int32_t readVarS32();
    float readF32();
    float readQuantized(float min, float max, int bits);
    bool readBytes(std::span<std::byte> out);
    void align();

    bool ok() const { return !m_error; }
    void fail() { m_error = true; }
    std::size_t bitsRemaining() const { return m_error ? 0 : m_data.size() * 8 - m_pos; }

private:
    std::span<const std::byte> m_data;
    std::size_t m_pos = 0;
    bool m_error = false;
};

// Quantization helpers shared by both streams.
inline constexpr int kQuatComponentBits = 10;

class WriteStream {
public:
    static constexpr bool kWriting = true;
    static constexpr bool kReading = false;

    BitWriter& writer() { return m_w; }
    bool ok() const { return true; }
    bool fail() { return false; } // writing never fails; validation happens before sending

    void bits(std::uint32_t& v, int n) { m_w.writeBits(v, n); }
    void boolean(bool& v) { m_w.writeBool(v); }
    void u8(std::uint8_t& v) { m_w.writeU8(v); }
    void u16(std::uint16_t& v) { m_w.writeU16(v); }
    void u32(std::uint32_t& v) { m_w.writeU32(v); }
    void u64(std::uint64_t& v) { m_w.writeU64(v); }
    void varU32(std::uint32_t& v) { m_w.writeVarU32(v); }
    void varS32(std::int32_t& v) { m_w.writeVarS32(v); }
    void f32(float& v) { m_w.writeF32(v); }
    void quantized(float& v, float min, float max, int n) { m_w.writeQuantized(v, min, max, n); }
    void vec3(Vec3& v) {
        f32(v.x);
        f32(v.y);
        f32(v.z);
    }
    void vec3Quantized(Vec3& v, float range, int n) {
        quantized(v.x, -range, range, n);
        quantized(v.y, -range, range, n);
        quantized(v.z, -range, range, n);
    }
    // Unit quaternion, "smallest three": 2-bit index + 3 x kQuatComponentBits.
    void quat(Quat& q);
    // Integer in [min, max] using the fewest bits.
    void ranged(std::int32_t& v, std::int32_t min, std::int32_t max);
    void string(std::string& s, std::size_t maxLength);
    void bytes(std::vector<std::byte>& b, std::size_t maxLength);
    template <class E>
    void enumeration(E& e, E maxValue) {
        auto v = static_cast<std::int32_t>(e);
        ranged(v, 0, static_cast<std::int32_t>(maxValue));
    }

private:
    BitWriter m_w;
};

class ReadStream {
public:
    static constexpr bool kWriting = false;
    static constexpr bool kReading = true;

    explicit ReadStream(std::span<const std::byte> data) : m_r(data) {}

    BitReader& reader() { return m_r; }
    bool ok() const { return m_r.ok(); }
    bool fail() {
        m_r.fail();
        return false;
    }

    void bits(std::uint32_t& v, int n) { v = m_r.readBits(n); }
    void boolean(bool& v) { v = m_r.readBool(); }
    void u8(std::uint8_t& v) { v = m_r.readU8(); }
    void u16(std::uint16_t& v) { v = m_r.readU16(); }
    void u32(std::uint32_t& v) { v = m_r.readU32(); }
    void u64(std::uint64_t& v) { v = m_r.readU64(); }
    void varU32(std::uint32_t& v) { v = m_r.readVarU32(); }
    void varS32(std::int32_t& v) { v = m_r.readVarS32(); }
    void f32(float& v);
    void quantized(float& v, float min, float max, int n) { v = m_r.readQuantized(min, max, n); }
    void vec3(Vec3& v) {
        f32(v.x);
        f32(v.y);
        f32(v.z);
    }
    void vec3Quantized(Vec3& v, float range, int n) {
        quantized(v.x, -range, range, n);
        quantized(v.y, -range, range, n);
        quantized(v.z, -range, range, n);
    }
    void quat(Quat& q);
    void ranged(std::int32_t& v, std::int32_t min, std::int32_t max);
    void string(std::string& s, std::size_t maxLength);
    void bytes(std::vector<std::byte>& b, std::size_t maxLength);
    template <class E>
    void enumeration(E& e, E maxValue) {
        std::int32_t v = 0;
        ranged(v, 0, static_cast<std::int32_t>(maxValue));
        e = static_cast<E>(v);
    }

private:
    BitReader m_r;
};

// Number of bits needed to store values 0..maxValue.
constexpr int bitsRequired(std::uint32_t maxValue) {
    int n = 0;
    while (maxValue) {
        ++n;
        maxValue >>= 1;
    }
    return n == 0 ? 1 : n;
}

} // namespace mm2::net
