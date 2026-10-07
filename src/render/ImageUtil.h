#pragma once

#include "render/Types.h"

#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace mm2::render {

// Encodes an RGBA8 image as PNG (via miniz).
std::vector<std::uint8_t> encodePng(const Image& image);
bool writePng(const std::filesystem::path& path, const Image& image);

// Halves an image with a 2x2 box filter (odd sizes round down, minimum 1).
Image downsample(const Image& image);
// The full mip chain: level 0 is a copy of `image`.
std::vector<Image> buildMipChain(const Image& image);
std::uint32_t mipCount(std::uint32_t width, std::uint32_t height);

// Creates a texture with a full mip chain from an RGBA8 image.
class Device;
TextureHandle createTextureWithMips(Device& device, const Image& image, const std::string& debugName = {});

// Flips an image vertically in place.
void flipVertical(Image& image);

} // namespace mm2::render
