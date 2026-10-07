#include "AssetTestUtil.h"
#include "asset/Image.h"

#include <gtest/gtest.h>

using namespace mm2;

namespace {

Bytes texHeader(std::uint16_t w, std::uint16_t h, std::uint16_t fmt, std::uint16_t mips, std::uint32_t flags) {
    Bytes b;
    b.u16(w).u16(h).u16(fmt).u16(mips).u16(1).u32(flags);
    return b;
}

} // namespace

TEST(Tex, PalettedBgraPaletteAndMips) {
    Bytes b = texHeader(2, 2, 1, 2, 0x10001);
    // Palette: entry 0 stored B,G,R,A = (0x10,0x20,0x30,0xFF) -> RGB (0x30,0x20,0x10).
    for (int i = 0; i < 256; ++i)
        b.u8(static_cast<std::uint8_t>(i == 0 ? 0x10 : i)).u8(0x20).u8(0x30).u8(0xFF);
    b.u8(0).u8(1).u8(2).u8(3); // 2x2
    b.u8(0);                   // 1x1
    std::string err;
    auto tex = asset::parseTex(b.data, &err);
    ASSERT_TRUE(tex) << err;
    EXPECT_EQ(tex->header.format, asset::TexFormat::P8);
    EXPECT_EQ(tex->header.flags, 0x10001u);
    ASSERT_EQ(tex->image.levels.size(), 2u);
    EXPECT_EQ(tex->image.levels[1].width, 1u);
    const auto& px = tex->image.levels[0].rgba;
    EXPECT_EQ(px[0], 0x30);
    EXPECT_EQ(px[1], 0x20);
    EXPECT_EQ(px[2], 0x10);
    EXPECT_EQ(px[3], 0xFF);
    EXPECT_EQ(px[4], 0x30); // entry 1 stored (1,0x20,0x30) -> R=0x30
    EXPECT_EQ(px[6], 0x01);
    EXPECT_FALSE(tex->image.hasTranslucency());
}

TEST(Tex, TrueColourByteOrder) {
    Bytes rgb = texHeader(1, 1, 17, 1, 0);
    rgb.u8(10).u8(20).u8(30);
    auto t1 = asset::parseTex(rgb.data);
    ASSERT_TRUE(t1);
    EXPECT_EQ(t1->image.levels[0].rgba, (std::vector<std::uint8_t>{10, 20, 30, 255}));

    Bytes rgba = texHeader(1, 1, 18, 1, 0);
    rgba.u8(10).u8(20).u8(30).u8(40);
    auto t2 = asset::parseTex(rgba.data);
    ASSERT_TRUE(t2);
    EXPECT_EQ(t2->image.levels[0].rgba, (std::vector<std::uint8_t>{10, 20, 30, 40}));
    EXPECT_TRUE(t2->image.hasTranslucency());
}

TEST(Tex, RejectsBadInput) {
    std::string err;
    EXPECT_FALSE(asset::parseTex(texHeader(4, 4, 99, 1, 0).data, &err));
    EXPECT_FALSE(asset::parseTex(texHeader(4, 4, 18, 1, 0).data, &err)); // no pixels
    EXPECT_FALSE(asset::parseTex(texHeader(0, 4, 18, 1, 0).data, &err));
    Bytes small;
    small.u16(1);
    EXPECT_FALSE(asset::parseTex(small.data, &err));
}

TEST(Tga, BottomUpOutputFromBothOrigins) {
    // 1x2 image, 24-bit. Stored bottom-up: first row in file is the bottom.
    auto make = [](std::uint8_t descriptor) {
        Bytes b;
        b.u8(0).u8(0).u8(2).u16(0).u16(0).u8(0).u16(0).u16(0).u16(1).u16(2).u8(24).u8(descriptor);
        b.u8(1).u8(2).u8(3);    // first stored pixel (B,G,R)
        b.u8(4).u8(5).u8(6);    // second stored pixel
        return b;
    };
    auto bottomUp = asset::decodeTga(make(0x00).data);
    ASSERT_TRUE(bottomUp);
    // Default origin: first stored row is the bottom -> row 0.
    EXPECT_EQ(bottomUp->levels[0].rgba[0], 3);
    EXPECT_EQ(bottomUp->levels[0].rgba[4], 6);
    auto topDown = asset::decodeTga(make(0x20).data);
    ASSERT_TRUE(topDown);
    EXPECT_EQ(topDown->levels[0].rgba[0], 6);
    EXPECT_EQ(topDown->levels[0].rgba[4], 3);
}

TEST(Tga, RunLengthEncoded) {
    Bytes b;
    b.u8(0).u8(0).u8(10).u16(0).u16(0).u8(0).u16(0).u16(0).u16(3).u16(1).u8(32).u8(8);
    b.u8(0x82).u8(9).u8(8).u8(7).u8(128); // run of 3 identical BGRA pixels
    auto img = asset::decodeTga(b.data);
    ASSERT_TRUE(img);
    EXPECT_EQ(img->levels[0].rgba, (std::vector<std::uint8_t>{7, 8, 9, 128, 7, 8, 9, 128, 7, 8, 9, 128}));
}

TEST(Png, EncodesSignature) {
    asset::Image::Level l{2, 2, std::vector<std::uint8_t>(16, 0x7F)};
    auto png = asset::encodePng(l);
    ASSERT_GT(png.size(), 8u);
    EXPECT_EQ(std::to_integer<int>(png[1]), 'P');
    // Round trip through stb_image (PNG is supported by decodeStb).
    auto img = asset::decodeStb(png);
    ASSERT_TRUE(img);
    EXPECT_EQ(img->levels[0].rgba, l.rgba);
}
