#include "render/ImageUtil.h"

#include "core/File.h"
#include "render/Device.h"

#include <miniz.h>

#include <algorithm>
#include <cstring>

namespace mm2::render {

std::vector<std::uint8_t> encodePng(const Image& image) {
    std::size_t size = 0;
    void* png = tdefl_write_image_to_png_file_in_memory_ex(image.pixels.data(), static_cast<int>(image.width),
                                                            static_cast<int>(image.height), 4, &size, 6, MZ_FALSE);
    if (!png)
        return {};
    std::vector<std::uint8_t> out(static_cast<std::uint8_t*>(png), static_cast<std::uint8_t*>(png) + size);
    mz_free(png);
    return out;
}

bool writePng(const std::filesystem::path& path, const Image& image) {
    const auto png = encodePng(image);
    if (png.empty())
        return false;
    return file::writeAtomic(path, std::as_bytes(std::span(png)));
}

std::uint32_t mipCount(std::uint32_t width, std::uint32_t height) {
    std::uint32_t n = 1;
    while (width > 1 || height > 1) {
        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);
        ++n;
    }
    return n;
}

Image downsample(const Image& src) {
    Image dst;
    dst.width = std::max(1u, src.width / 2);
    dst.height = std::max(1u, src.height / 2);
    dst.pixels.resize(static_cast<std::size_t>(dst.width) * dst.height * 4);
    for (std::uint32_t y = 0; y < dst.height; ++y) {
        const std::uint32_t y0 = std::min(y * 2, src.height - 1), y1 = std::min(y * 2 + 1, src.height - 1);
        for (std::uint32_t x = 0; x < dst.width; ++x) {
            const std::uint32_t x0 = std::min(x * 2, src.width - 1), x1 = std::min(x * 2 + 1, src.width - 1);
            for (int c = 0; c < 4; ++c) {
                auto at = [&](std::uint32_t px, std::uint32_t py) {
                    return static_cast<unsigned>(src.pixels[(static_cast<std::size_t>(py) * src.width + px) * 4 + c]);
                };
                // gfxImage::GenerateMipmaps: the sum of the 2x2 block shifted
                // right by two (truncated, not rounded).
                dst.pixels[(static_cast<std::size_t>(y) * dst.width + x) * 4 + c] =
                    static_cast<std::uint8_t>((at(x0, y0) + at(x1, y0) + at(x0, y1) + at(x1, y1)) >> 2);
            }
        }
    }
    return dst;
}

std::vector<Image> buildMipChain(const Image& image) {
    std::vector<Image> chain{image};
    while (chain.back().width > 1 || chain.back().height > 1)
        chain.push_back(downsample(chain.back()));
    return chain;
}

TextureHandle createTextureWithMips(Device& device, const Image& image, const std::string& debugName) {
    const auto chain = buildMipChain(image);
    std::vector<TextureData> mips;
    for (const auto& m : chain)
        mips.push_back({m.pixels.data(), 0});
    TextureDesc desc;
    desc.width = image.width;
    desc.height = image.height;
    desc.mipLevels = static_cast<std::uint32_t>(chain.size());
    desc.debugName = debugName;
    return device.createTexture(desc, mips);
}

void flipVertical(Image& image) {
    const std::size_t row = static_cast<std::size_t>(image.width) * 4;
    std::vector<std::uint8_t> tmp(row);
    for (std::uint32_t y = 0; y < image.height / 2; ++y) {
        std::uint8_t* a = image.pixels.data() + y * row;
        std::uint8_t* b = image.pixels.data() + (image.height - 1 - y) * row;
        std::memcpy(tmp.data(), a, row);
        std::memcpy(a, b, row);
        std::memcpy(b, tmp.data(), row);
    }
}

} // namespace mm2::render
