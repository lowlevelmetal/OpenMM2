#include "game/TextureLibrary.h"

#include "asset/Image.h"
#include "core/Log.h"
#include "core/StringUtil.h"
#include "data/TextTables.h"
#include "render/ImageUtil.h"

#include <algorithm>
#include <format>

namespace mm2::game {

TextureLibrary::TextureLibrary(render::Device& device, const vfs::Vfs& vfs) : m_device(device), m_vfs(vfs) {}

TextureLibrary::~TextureLibrary() { clear(); }

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

std::optional<WorldTexture> TextureLibrary::loadVariant(const std::string& name) {
    if (m_rain && !str::iendsWith(name, "_fa"))
        if (auto t = load(name + "_fa", m_night))
            return t;
    if (m_night && !str::iendsWith(name, "_ni"))
        if (auto t = load(name + "_ni", false))
            return t;
    return load(name, m_night && !str::iendsWith(name, "_ni"));
}

std::optional<WorldTexture> TextureLibrary::load(const std::string& name, bool darken) {
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
    if (darken)
        for (auto& level : image->levels)
            for (std::size_t i = 0; i + 3 < level.rgba.size(); i += 4) {
                level.rgba[i] >>= 1;
                level.rgba[i + 1] >>= 1;
                level.rgba[i + 2] >>= 1;
            }

    // Use the file's mip levels when the chain is complete, else build one.
    const auto& top = image->levels[0];
    const std::uint32_t fullChain = render::mipCount(top.width, top.height);
    std::vector<render::Image> mips;
    if (image->levels.size() >= fullChain) {
        for (std::size_t i = 0; i < fullChain; ++i)
            mips.push_back({image->levels[i].width, image->levels[i].height, image->levels[i].rgba});
    } else {
        mips = render::buildMipChain(render::Image{top.width, top.height, top.rgba});
    }
    std::vector<render::TextureData> data;
    for (const auto& m : mips)
        data.push_back({m.pixels.data(), 0});

    WorldTexture t;
    render::TextureDesc desc;
    desc.width = top.width;
    desc.height = top.height;
    desc.mipLevels = static_cast<std::uint32_t>(mips.size());
    desc.debugName = name;
    t.handle = m_device.createTexture(desc, data);
    t.width = top.width;
    t.height = top.height;
    t.flags = flags;
    t.translucent = image->hasTranslucency();
    t.alphaFlag = (flags & asset::TexFlags::Alpha) != 0;
    // Wrap flags apply to .tex files; everything else repeats.
    const bool isTex = path.ends_with(".tex");
    t.sampler.addressU = !isTex || (flags & asset::TexFlags::WrapU) ? render::AddressMode::Wrap : render::AddressMode::Clamp;
    t.sampler.addressV = !isTex || (flags & asset::TexFlags::WrapV) ? render::AddressMode::Wrap : render::AddressMode::Clamp;
    t.sampler.filter = render::Filter::Trilinear;
    return t;
}

const WorldTexture* TextureLibrary::get(std::string_view nameIn) {
    if (nameIn.empty())
        return nullptr;
    const std::string name = str::lower(nameIn);
    if (auto it = m_animations.find(name); it != m_animations.end())
        return &it->second.current;
    if (auto it = m_textures.find(name); it != m_textures.end())
        return it->second ? &*it->second : nullptr;

    // Animated texture? The PSDL names a single frame ("s_thames-0009"); the
    // whole sequence plays when tune/<base>.movie exists.
    if (name.size() > 5 && name[name.size() - 5] == '-' &&
        std::all_of(name.end() - 4, name.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        const std::string base = name.substr(0, name.size() - 5);
        if (m_vfs.exists("tune/" + base + ".movie"))
            if (const WorldTexture* anim = get(base))
                return anim;
    }
    if (auto movie = m_vfs.readAll("tune/" + name + ".movie")) {
        Animation anim;
        const std::string_view text(reinterpret_cast<const char*>(movie->data()), movie->size());
        for (auto line : data::splitLines(text)) {
            const auto parts = str::split(str::trim(line), ' ');
            if (parts.size() >= 2 && str::iequals(parts[0], "rate"))
                anim.rate = static_cast<float>(str::parseDouble(parts.back()).value_or(30.0));
        }
        for (int i = 1; i < 1000; ++i) {
            auto frame = loadVariant(std::format("{}-{:04}", name, i));
            if (!frame)
                break;
            anim.frames.push_back(*frame);
        }
        if (!anim.frames.empty()) {
            anim.current = anim.frames.front();
            auto& stored = m_animations[name] = std::move(anim);
            return &stored.current;
        }
    }

    auto t = loadVariant(name);
    if (!t)
        log::debug("texture '{}' not found", name);
    auto& slot = m_textures[name] = t;
    return slot ? &*slot : nullptr;
}

void TextureLibrary::update(double time) {
    for (auto& [name, a] : m_animations) {
        const auto frame = static_cast<std::size_t>(time * a.rate) % a.frames.size();
        a.current = a.frames[frame];
    }
}

} // namespace mm2::game
