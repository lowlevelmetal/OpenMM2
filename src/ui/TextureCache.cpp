#include "ui/TextureCache.h"

#include "asset/Image.h"
#include "core/Log.h"
#include "core/StringUtil.h"

namespace mm2::ui {

TextureCache::TextureCache(render::Device& device, const vfs::Vfs& vfs) : m_device(device), m_vfs(vfs) {}

TextureCache::~TextureCache() { clear(); }

void TextureCache::clear() {
    for (auto& [path, t] : m_textures)
        if (t.handle)
            m_device.destroyTexture(t.handle);
    m_textures.clear();
}

const UiTexture& TextureCache::get(std::string_view path) {
    const std::string key = str::normalizeVirtualPath(path);
    if (auto it = m_textures.find(key); it != m_textures.end())
        return it->second;
    UiTexture tex;
    if (auto bytes = m_vfs.readAll(key)) {
        std::string error;
        if (auto image = asset::decodeImageFile(key, *bytes, &error); image && !image->empty()) {
            const auto& level = image->levels[0];
            render::TextureDesc desc;
            desc.width = level.width;
            desc.height = level.height;
            desc.debugName = key;
            const render::TextureData data{level.rgba.data(), 0};
            tex.handle = m_device.createTexture(desc, std::span(&data, 1));
            tex.width = level.width;
            tex.height = level.height;
        } else {
            log::warn("ui: cannot decode {}: {}", key, error);
        }
    } else {
        log::warn("ui: missing image {}", key);
    }
    return m_textures.emplace(key, tex).first->second;
}

} // namespace mm2::ui
