#include "net/BitStream.h"
#include "net/Sha256.h"

#include <gtest/gtest.h>

#include <cmath>
#include <random>

using namespace mm2;
using namespace mm2::net;

TEST(BitStream, RoundTripsMixedWidths) {
    BitWriter w;
    w.writeBits(5, 3);
    w.writeBool(true);
    w.writeU8(0xAB);
    w.writeBits(0x1FFFF, 17);
    w.writeU16(0xBEEF);
    w.writeU32(0xDEADBEEF);
    w.writeU64(0x0123456789ABCDEFull);
    w.writeF32(-3.25f);
    const auto bytes = w.take();

    BitReader r(bytes);
    EXPECT_EQ(r.readBits(3), 5u);
    EXPECT_TRUE(r.readBool());
    EXPECT_EQ(r.readU8(), 0xAB);
    EXPECT_EQ(r.readBits(17), 0x1FFFFu);
    EXPECT_EQ(r.readU16(), 0xBEEF);
    EXPECT_EQ(r.readU32(), 0xDEADBEEFu);
    EXPECT_EQ(r.readU64(), 0x0123456789ABCDEFull);
    EXPECT_EQ(r.readF32(), -3.25f);
    EXPECT_TRUE(r.ok());
    EXPECT_LT(r.bitsRemaining(), 8u);
}

TEST(BitStream, VarIntsCoverFullRange) {
    const std::uint32_t values[] = {0, 1, 127, 128, 16383, 16384, 0x0FFFFFFF, 0xFFFFFFFF};
    const std::int32_t signedValues[] = {0, -1, 1, -64, 64, INT32_MIN, INT32_MAX};
    BitWriter w;
    for (auto v : values)
        w.writeVarU32(v);
    for (auto v : signedValues)
        w.writeVarS32(v);
    w.writeVarU64(~0ull);
    const auto bytes = w.take();
    BitReader r(bytes);
    for (auto v : values)
        EXPECT_EQ(r.readVarU32(), v);
    for (auto v : signedValues)
        EXPECT_EQ(r.readVarS32(), v);
    EXPECT_EQ(r.readVarU64(), ~0ull);
    EXPECT_TRUE(r.ok());
}

TEST(BitStream, OverrunIsStickyAndSafe) {
    const std::byte data[2] = {std::byte{0xFF}, std::byte{0x01}};
    BitReader r(data);
    EXPECT_EQ(r.readBits(12), 0x1FFu);
    EXPECT_EQ(r.readBits(8), 0u); // only 4 bits left
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.readU8(), 0u);
    EXPECT_FALSE(r.ok());
}

TEST(BitStream, RejectsOverlongVarint) {
    std::vector<std::byte> data(11, std::byte{0xFF});
    BitReader r(data);
    r.readVarU64();
    EXPECT_FALSE(r.ok());

    // 5-byte varint encoding a value > 32 bits must fail readVarU32.
    const std::byte big[5] = {std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0x7F}};
    BitReader r2(big);
    r2.readVarU32();
    EXPECT_FALSE(r2.ok());
}

TEST(BitStream, QuantizationErrorIsBounded) {
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-100.0f, 100.0f);
    for (int i = 0; i < 1000; ++i) {
        const float v = dist(rng);
        BitWriter w;
        w.writeQuantized(v, -100.0f, 100.0f, 16);
        const auto bytes = w.take();
        BitReader r(bytes);
        EXPECT_NEAR(r.readQuantized(-100.0f, 100.0f, 16), v, 200.0f / 65535.0f);
    }
    // Clamping and NaN.
    BitWriter w;
    w.writeQuantized(1e9f, -1.0f, 1.0f, 8);
    w.writeQuantized(std::nanf(""), -1.0f, 1.0f, 8);
    const auto bytes = w.take();
    BitReader r(bytes);
    EXPECT_FLOAT_EQ(r.readQuantized(-1.0f, 1.0f, 8), 1.0f);
    EXPECT_FLOAT_EQ(r.readQuantized(-1.0f, 1.0f, 8), -1.0f);
}

TEST(BitStream, QuaternionSmallestThree) {
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (int i = 0; i < 500; ++i) {
        Quat q = Quat{dist(rng), dist(rng), dist(rng), dist(rng)}.normalized();
        WriteStream ws;
        ws.quat(q);
        const auto bytes = ws.writer().take();
        EXPECT_EQ(bytes.size(), 4u); // 2 + 3 * 10 bits
        ReadStream rs(bytes);
        Quat out;
        rs.quat(out);
        ASSERT_TRUE(rs.ok());
        // q and -q are the same rotation.
        const float dot = std::abs(q.x * out.x + q.y * out.y + q.z * out.z + q.w * out.w);
        EXPECT_GT(dot, 0.9999f);
    }
}

TEST(BitStream, StringsAndBytesRespectLimits) {
    WriteStream ws;
    std::string s = "hello";
    std::string longText(300, 'x');
    std::vector<std::byte> blob = {std::byte{1}, std::byte{2}, std::byte{3}};
    ws.string(s, 32);
    ws.string(longText, 10); // truncated on write
    ws.bytes(blob, 16);
    const auto bytes = ws.writer().take();

    ReadStream rs(bytes);
    std::string a, b;
    std::vector<std::byte> c;
    rs.string(a, 32);
    rs.string(b, 10);
    rs.bytes(c, 16);
    EXPECT_TRUE(rs.ok());
    EXPECT_EQ(a, "hello");
    EXPECT_EQ(b, std::string(10, 'x'));
    EXPECT_EQ(c, blob);

    // A reader with a smaller limit than the writer rejects the string.
    ReadStream strict(bytes);
    std::string tooLong;
    strict.string(tooLong, 3);
    EXPECT_FALSE(strict.ok());
}

TEST(BitStream, ClaimedLengthBeyondBufferFails) {
    BitWriter w;
    w.writeVarU32(1000); // claims 1000 bytes, provides none
    const auto bytes = w.take();
    ReadStream rs(bytes);
    std::string s;
    rs.string(s, 2000);
    EXPECT_FALSE(rs.ok());
    EXPECT_TRUE(s.empty());
}

TEST(BitStream, RangedAndEnumValidation) {
    enum class Color : std::uint8_t { Red, Green, Blue, Last = Blue };
    WriteStream ws;
    std::int32_t v = 13;
    ws.ranged(v, 10, 20);
    Color c = Color::Blue;
    ws.enumeration(c, Color::Last);
    // 2 bits can encode 3, which is outside Color's range.
    std::uint32_t bad = 3;
    ws.bits(bad, 2);
    const auto bytes = ws.writer().take();

    ReadStream rs(bytes);
    std::int32_t v2 = 0;
    rs.ranged(v2, 10, 20);
    Color c2 = Color::Red;
    rs.enumeration(c2, Color::Last);
    EXPECT_EQ(v2, 13);
    EXPECT_EQ(c2, Color::Blue);
    Color c3 = Color::Red;
    rs.enumeration(c3, Color::Last);
    EXPECT_FALSE(rs.ok());
}

TEST(Sha256, KnownVectors) {
    auto hex = [](const Sha256::Digest& d) {
        std::string s;
        for (std::byte b : d)
            s += std::format("{:02x}", std::to_integer<unsigned>(b));
        return s;
    };
    EXPECT_EQ(hex(Sha256::hash({})), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    Sha256 h;
    h.update("abc");
    EXPECT_EQ(hex(h.finish()), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    Sha256 h2;
    h2.update("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq");
    EXPECT_EQ(hex(h2.finish()), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // Multi-block input fed in pieces.
    Sha256 h3;
    const std::string million(1000000, 'a');
    for (std::size_t i = 0; i < million.size(); i += 777)
        h3.update(std::string_view(million).substr(i, 777));
    EXPECT_EQ(hex(h3.finish()), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}
