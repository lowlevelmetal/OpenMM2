#pragma once

#include "asset/Image.h"
#include "render/Device.h"
#include "vfs/Vfs.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mm2::game {

// A world texture on the GPU. Game images keep their file row order (bottom
// row first), which is what the game's texture coordinates expect, so no
// flipping happens anywhere in 3D rendering.
struct WorldTexture {
    render::TextureHandle handle;
    // The address modes come from the .tex flag word (0x1 clamps U, 0x10000
    // clamps V, as gfxRenderState::DoFlush applies the texture environment).
    render::SamplerDesc sampler;
    std::uint32_t width = 0, height = 0;
    std::uint32_t flags = 0;    // .tex flag word (MM2's texture environment)
    bool translucent = false;   // some texels have alpha < 255
};

// Loads textures by base name ("cw_apt_brk"), looking for texture/<name>.tex
// and then .tga (gfxLoadImageAll's order for the formats the 3D world uses).
// A missing texture with frames texture/<name>-0001... is a texture movie
// (gfxGetTextureMovie), cycling at the rate tune/<name>.movie gives.
class TextureLibrary {
public:
    TextureLibrary(render::Device& device, const vfs::Vfs& vfs);
    ~TextureLibrary();
    TextureLibrary(const TextureLibrary&) = delete;
    TextureLibrary& operator=(const TextureLibrary&) = delete;

    // gfxGetTexture(name, mipmaps). Null when the texture does not exist
    // (logged once). Without `mipmaps` the texture has its top level only
    // (asParticles::SetTexture by name asks for none).
    const WorldTexture* get(std::string_view name, bool mipmaps = true);

    // MM2's texture variant handler (InstallTextureVariantHandler), active
    // in game: in rain a texture loads as "<name>_fa" when that exists (wet
    // roads and decals); at night as "<name>_ni" (lit windows, shop fronts,
    // lamps), and every other texture is darkened (each channel halved on
    // every mip level). Changing the variants reloads the textures.
    void setVariants(bool night, bool rain);
    void setNight(bool night) { setVariants(night, m_rain); }
    bool night() const { return m_night; }
    bool rain() const { return m_rain; }
    // Advances animated textures to `time` seconds.
    void update(double time);

    // The pixels `name` loads as (variants and night darkening applied), for
    // textures that are modified at run time (fxTexelDamage).
    std::optional<asset::Image> image(std::string_view name);
    // Adds a texture made elsewhere under `name` (kept across variant
    // changes); the library destroys it.
    const WorldTexture* adopt(const std::string& name, const WorldTexture& texture);
    void release(const std::string& name);

    std::size_t loadedCount() const { return m_textures.size(); }

private:
    struct Animation {
        std::vector<WorldTexture> frames;
        float rate = 30.0f;
        WorldTexture current;
    };
    std::optional<WorldTexture> load(const std::string& name, bool darken, bool mipmaps);
    // The variant to load for `name` and whether to darken it.
    std::optional<WorldTexture> loadVariant(const std::string& name, bool mipmaps);
    void clear();

    render::Device& m_device;
    const vfs::Vfs& m_vfs;
    std::unordered_map<std::string, std::optional<WorldTexture>> m_textures;
    std::unordered_map<std::string, Animation> m_animations;
    std::unordered_map<std::string, WorldTexture> m_adopted;
    bool m_night = false;
    bool m_rain = false;
};

} // namespace mm2::game
