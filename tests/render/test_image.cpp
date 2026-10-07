#include "render/ImageUtil.h"

#include <algorithm>

#include <gtest/gtest.h>


using namespace mm2::render;

namespace {
Image solid(std::uint32_t w, std::uint32_t h, std::uint8_t v) {
    Image img;
    img.width = w;
    img.height = h;
    img.pixels.assign(static_cast<std::size_t>(w) * h * 4, v);
    return img;
}
} // namespace

TEST(ImageUtil, MipChainSizes) {
    EXPECT_EQ(mipCount(1, 1), 1u);
    EXPECT_EQ(mipCount(256, 256), 9u);
    EXPECT_EQ(mipCount(256, 64), 9u);
    EXPECT_EQ(mipCount(3, 5), 3u);
    const auto chain = buildMipChain(solid(16, 4, 100));
    ASSERT_EQ(chain.size(), 5u);
    EXPECT_EQ(chain[1].width, 8u);
    EXPECT_EQ(chain[1].height, 2u);
    EXPECT_EQ(chain[4].width, 1u);
    EXPECT_EQ(chain[4].height, 1u);
    EXPECT_EQ(chain[4].pixels[0], 100);
}

TEST(ImageUtil, DownsampleAverages) {
    Image img = solid(2, 2, 0);
    img.pixels[0] = 255; // red of the top-left texel
    const Image half = downsample(img);
    ASSERT_EQ(half.width, 1u);
    EXPECT_EQ(half.pixels[0], 64); // (255 + 0 + 0 + 0 + 2) / 4
}

TEST(ImageUtil, FlipVertical) {
    Image img = solid(1, 3, 0);
    img.pixels[0] = 1;
    img.pixels[8] = 3;
    flipVertical(img);
    EXPECT_EQ(img.pixels[0], 3);
    EXPECT_EQ(img.pixels[8], 1);
}

TEST(ImageUtil, EncodesValidPng) {
    Image img = solid(7, 5, 128);
    const auto png = encodePng(img);
    ASSERT_GT(png.size(), 8u);
    const std::uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    EXPECT_TRUE(std::equal(sig, sig + 8, png.begin()));
}
