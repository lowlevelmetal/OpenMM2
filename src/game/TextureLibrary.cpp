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

// The .tex header's flag word becomes the gfxTexture's texture environment
// (gfxLoadTexImage, gfxTexture::Create). gfxRenderState::DoFlush reads two
// bits of it: 0x1 clamps U and 0x10000 clamps V; everything else repeats.
// (asset::TexFlags calls 0x1 "Alpha" and 0x2/0x4 "WrapU/WrapV"; MM2's
// renderer does not read 0x2, 0x4 or 0x8000.)
constexpr std::uint32_t kTexEnvClampU = 0x1;
constexpr std::uint32_t kTexEnvClampV = 0x10000;

// FUN_00442fb0 of the texture variant handler: every mip level's colour
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
    // The same choice as loadVariant, without uploading.
    const std::string name = str::lower(nameIn);
    auto read = [&](const std::string& n, bool darken) -> std::optional<asset::Image> {
        std::optional<asset::Image> img;
        if (auto bytes = m_vfs.readAll("texture/" + n + ".tex")) {
            if (auto tex = asset::parseTex(*bytes))
                img = std::move(tex->image);
        } else if (auto tga = m_vfs.readAll("texture/" + n + ".tga")) {
            img = asset::decodeTga(*tga);
        }
        if (!img || img->empty())
            return std::nullopt;
        if (darken)
            darkenImage(*img);
        return img;
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

std::optional<WorldTexture> TextureLibrary::load(const std::string& name, bool darken, bool mipmaps) {
    std::optional<asset::Image> image;
    std::uint32_t flags = 0;
    std::string path = "texture/" + name + ".tex";
    if (auto bytes = m_vfs.readAll(path)) {
        std::string error;
        if (auto tex = asset::parseTex(*bytes, &error)) {
            flags = tex->header.flags;
            image = std::move(tex->image);
        } else {
            log::warn("texture {}: {}", path, error);
        }
    } else {
        path = "texture/" + name + ".tga";
        if (auto tga = m_vfs.readAll(path))
            image = asset::decodeTga(*tga);
    }
    if (!image || image->empty())
        return std::nullopt;

    // The mip levels (gfxGetTexture with mipmaps asked for; without, the top
    // level only). A .tex brings its own levels, as many as the file has: a
    // partial chain stays partial and a single level has no mipmaps
    // (gfxLoadTexImage). A Targa gets a full chain generated when it is
    // square and none otherwise (gfxLoadTargaImage creates the levels only
    // for square images, gfxImage::GenerateMipmaps averages 2 x 2 texels).
    // The night darkening comes after, on every level (gfxPrepareImage).
    const std::uint32_t width = image->width(), height = image->height();
    if (!path.ends_with(".tex") && mipmaps && width == height) {
        auto mips = render::buildMipChain(render::Image{width, height, image->levels[0].rgba});
        image->levels.resize(1);
        for (std::size_t i = 1; i < mips.size(); ++i)
            image->levels.push_back({mips[i].width, mips[i].height, std::move(mips[i].pixels)});
    }
    if (darken)
        darkenImage(*image);
    const std::size_t levels = mipmaps ? std::min<std::size_t>(image->levels.size(), render::mipCount(width, height)) : 1;
    std::vector<render::TextureData> data;
    for (std::size_t i = 0; i < levels; ++i)
        data.push_back({image->levels[i].rgba.data(), 0});

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
    t.translucent = image->hasTranslucency();
    // gfxRenderState::DoFlush: the texture environment's clamp bits (a Targa
    // has none, so it repeats).
    t.sampler.addressU = (flags & kTexEnvClampU) ? render::AddressMode::Clamp : render::AddressMode::Wrap;
    t.sampler.addressV = (flags & kTexEnvClampV) ? render::AddressMode::Clamp : render::AddressMode::Wrap;
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
