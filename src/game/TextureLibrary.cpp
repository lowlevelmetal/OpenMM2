#include "game/TextureLibrary.h"

#include "asset/Image.h"
#include "core/Log.h"
#include "core/StringUtil.h"
#include "data/TextTables.h"
#include "render/ImageUtil.h"

#include <algorithm>
#include <format>

namespace mm2::game {
namespace {

// The texture variant handler's darkening helper (InstallTextureVariantHandler's
// image hook): every mip level's colour
// channels halved.
void darkenImage(asset::Image& image) {
    for (auto& level : image.levels)
        for (std::size_t i = 0; i + 3 < level.rgba.size(); i += 4) {
            level.rgba[i] >>= 1;
            level.rgba[i + 1] >>= 1;
            level.rgba[i + 2] >>= 1;
        }
}

} // namespace

TextureLibrary::TextureLibrary(render::Device& device, const vfs::Vfs& vfs) : m_device(device), m_vfs(vfs) {}

TextureLibrary::~TextureLibrary() {
    clear();
    for (auto& [name, t] : m_adopted)
        m_device.destroyTexture(t.handle);
}

const WorldTexture* TextureLibrary::adopt(const std::string& name, const WorldTexture& texture) {
    release(name);
    return &(m_adopted[str::lower(name)] = texture);
}

void TextureLibrary::release(const std::string& name) {
    if (auto it = m_adopted.find(str::lower(name)); it != m_adopted.end()) {
        m_device.destroyTexture(it->second.handle);
        m_adopted.erase(it);
    }
}

std::optional<asset::Image> TextureLibrary::image(std::string_view nameIn) {
    // The same choice as loadVariant, without uploading (top level only).
    const std::string name = str::lower(nameIn);
    auto read = [&](const std::string& n, bool darken) -> std::optional<asset::Image> {
        auto loaded = readImage(n, false);
        if (!loaded)
            return std::nullopt;
        if (darken)
            darkenImage(loaded->image);
        return std::move(loaded->image);
    };
    if (m_rain)
        if (auto t = read(name + "_fa", m_night))
            return t;
    if (m_night)
        if (auto t = read(name + "_ni", false))
            return t;
    return read(name, m_night);
}

void TextureLibrary::clear() {
    for (auto& [name, t] : m_textures)
        if (t && t->handle)
            m_device.destroyTexture(t->handle);
    for (auto& [name, a] : m_animations)
        for (auto& f : a.frames)
            m_device.destroyTexture(f.handle);
    m_textures.clear();
    m_animations.clear();
}

void TextureLibrary::setVariants(bool night, bool rain) {
    if (night == m_night && rain == m_rain)
        return;
    m_night = night;
    m_rain = rain;
    clear();
}

// InstallTextureVariantHandler's gfxLoadImage wrapper: in rain (weather 3)
// "<name>_fa" first; at night (time 3) "<name>_ni" next, which is not
// darkened; else the texture itself. The gfxPrepareImage wrapper then halves
// what was loaded at night unless it came from the "_ni" lookup. A name that
// already ends in "_ni" is darkened like any other (its "_ni_ni" lookup fails).
std::optional<WorldTexture> TextureLibrary::loadVariant(const std::string& name, bool mipmaps) {
    if (m_rain)
        if (auto t = load(name + "_fa", m_night, mipmaps))
            return t;
    if (m_night)
        if (auto t = load(name + "_ni", false, mipmaps))
            return t;
    return load(name, m_night, mipmaps);
}

std::optional<TextureLibrary::LoadedImage> TextureLibrary::readImage(const std::string& name, bool mipmaps) const {
    // gfxLoadImageAll: texture/<name>.tex, then .tga, then .bmp (a file that
    // fails to load falls through to the next), then jpg/<name>.jpg.
    LoadedImage out;
    const std::string base = "texture/" + name;
    if (auto bytes = m_vfs.readAll(base + ".tex")) {
        std::string error;
        if (auto tex = asset::parseTex(*bytes, &error)) {
            // gfxLoadTexImage: the file's levels as stored (bottom row first,
            // the way the game's UVs expect), the flag word as the texture
            // environment; PA8, P8A8, PA4, ARGB1555 and RGBA8888 have alpha.
            out.image = std::move(tex->image);
            out.flags = tex->header.flags;
            const auto format = static_cast<std::uint16_t>(tex->header.format);
            out.alphaFormat = format == 2 || format == 6 || format == 14 || format == 16 || format == 18;
            if (!out.image.empty())
                return out;
        } else {
            log::warn("texture {}.tex: {}", base, error);
        }
    }
    // The other loaders store the picture's top row first (gfxLoadTargaImage
    // and gfxLoadBmpImage write bottom-up files upwards; JPEG is top-down),
    // the opposite of asset::Image's row order, so their rows are flipped.
    auto flipped = [](asset::Image img) {
        for (auto& level : img.levels) {
            const std::size_t row = std::size_t{level.width} * 4;
            for (std::uint32_t y = 0; y < level.height / 2; ++y)
                std::swap_ranges(level.rgba.begin() + static_cast<std::ptrdiff_t>(y * row),
                                 level.rgba.begin() + static_cast<std::ptrdiff_t>((y + 1) * row),
                                 level.rgba.begin() + static_cast<std::ptrdiff_t>((level.height - 1 - y) * row));
        }
        return img;
    };
    if (auto bytes = m_vfs.readAll(base + ".tga")) {
        if (auto img = asset::decodeTga(*bytes); img && !img->empty()) {
            out.image = flipped(std::move(*img));
            // 32-bit Targas become RGBA8888 images, others RGB888.
            out.alphaFormat = bytes->size() > 16 && std::to_integer<int>((*bytes)[16]) == 32;
            // gfxLoadTargaImage: a square image gets a full generated chain.
            out.generatedLevels = mipmaps && out.image.width() == out.image.height() ? 99 : 0;
            return out;
        }
    }
    if (auto bytes = m_vfs.readAll(base + ".bmp")) {
        if (auto img = asset::decodeImageFile(base + ".bmp", *bytes); img && !img->empty()) {
            out.image = flipped(std::move(*img));
            return out;
        }
    }
    if (auto bytes = m_vfs.readAll("jpg/" + name + ".jpg")) {
        if (auto img = asset::decodeImageFile("jpg/" + name + ".jpg", *bytes); img && !img->empty()) {
            out.image = flipped(std::move(*img));
            // gfxLoadJPEGImage: one generated level below the picture when
            // mipmaps are asked for.
            out.generatedLevels = mipmaps ? 1 : 0;
            return out;
        }
    }
    return std::nullopt;
}

std::optional<WorldTexture> TextureLibrary::load(const std::string& name, bool darken, bool mipmaps) {
    auto loaded = readImage(name, mipmaps);
    if (!loaded)
        return std::nullopt;
    asset::Image& image = loaded->image;
    const std::uint32_t flags = loaded->flags;

    // The mip levels (gfxGetTexture with mipmaps asked for; without, the top
    // level only). A .tex brings its own levels, as many as the file has: a
    // partial chain stays partial and a single level has no mipmaps
    // (gfxLoadTexImage). Other formats get generated levels averaging 2 x 2
    // texels (gfxImage::GenerateMipmaps): a full chain for a square Targa,
    // one level for a JPEG. The night darkening comes after, on every level
    // (gfxPrepareImage).
    const std::uint32_t width = image.width(), height = image.height();
    if (loaded->generatedLevels > 0) {
        auto mips = render::buildMipChain(render::Image{width, height, image.levels[0].rgba});
        image.levels.resize(1);
        for (std::size_t i = 1; i < mips.size() && i <= loaded->generatedLevels; ++i)
            image.levels.push_back({mips[i].width, mips[i].height, std::move(mips[i].pixels)});
    }
    if (darken)
        darkenImage(image);
    const std::size_t levels = mipmaps ? std::min<std::size_t>(image.levels.size(), render::mipCount(width, height)) : 1;
    std::vector<render::TextureData> data;
    for (std::size_t i = 0; i < levels; ++i)
        data.push_back({image.levels[i].rgba.data(), 0});

    WorldTexture t;
    render::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.mipLevels = static_cast<std::uint32_t>(levels);
    desc.debugName = name;
    t.handle = m_device.createTexture(desc, data);
    t.width = width;
    t.height = height;
    t.flags = flags;
    t.translucent = image.hasTranslucency();
    t.alphaFormat = loaded->alphaFormat;
    // gfxRenderState::DoFlush: textures repeat unless their .tex flags clamp
    // U (0x1) or V (0x10000); other image types carry no flags and repeat.
    using render::AddressMode;
    t.sampler.addressU = (flags & asset::TexFlags::ClampU) ? AddressMode::Clamp : AddressMode::Wrap;
    t.sampler.addressV = (flags & asset::TexFlags::ClampV) ? AddressMode::Clamp : AddressMode::Wrap;
    t.sampler.filter = render::Filter::Trilinear;
    return t;
}

const WorldTexture* TextureLibrary::get(std::string_view nameIn, bool mipmaps) {
    if (nameIn.empty())
        return nullptr;
    const std::string name = str::lower(nameIn);
    if (auto it = m_adopted.find(name); it != m_adopted.end())
        return &it->second;
    if (auto it = m_animations.find(name); it != m_animations.end())
        return &it->second.current;
    const std::string key = mipmaps ? name : name + "#nomip";
    if (auto it = m_textures.find(key); it != m_textures.end())
        return it->second ? &*it->second : nullptr;

    // gfxGetTextureMovie (models' shaders and the city's textures): the
    // texture itself when it exists; otherwise the frames "<name>-0001",
    // "<name>-0002", ... (at most 128) cycle at tune/<name>.movie's "rate"
    // (frames per second; 30 without the file).
    auto t = loadVariant(name, mipmaps);
    if (!t && mipmaps) {
        Animation anim;
        for (int i = 1; i <= 128; ++i) {
            auto frame = loadVariant(std::format("{}-{:04}", name, i), true);
            if (!frame)
                break;
            anim.frames.push_back(*frame);
        }
        if (!anim.frames.empty()) {
            if (auto movie = m_vfs.readAll("tune/" + name + ".movie")) {
                const std::string_view text(reinterpret_cast<const char*>(movie->data()), movie->size());
                for (auto line : data::splitLines(text)) {
                    const auto parts = str::split(str::trim(line), ' ');
                    if (parts.size() >= 2 && str::iequals(parts[0], "rate"))
                        anim.rate = static_cast<float>(str::parseDouble(parts.back()).value_or(30.0));
                }
            }
            anim.current = anim.frames.front();
            auto& stored = m_animations[name] = std::move(anim);
            return &stored.current;
        }
    }
    if (!t)
        log::debug("texture '{}' not found", name);
    auto& slot = m_textures[key] = t;
    return slot ? &*slot : nullptr;
}

void TextureLibrary::update(double time) {
    for (auto& [name, a] : m_animations) {
        const auto frame = static_cast<std::size_t>(time * a.rate) % a.frames.size();
        a.current = a.frames[frame];
    }
}

} // namespace mm2::game
