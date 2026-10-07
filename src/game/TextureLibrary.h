#pragma once

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
    render::SamplerDesc sampler;
    std::uint32_t width = 0, height = 0;
    std::uint32_t flags = 0;    // .tex flags (asset::TexFlags)
    bool translucent = false;   // some texels have alpha < 255
    bool alphaFlag = false;     // .tex Alpha flag set
};

// Loads textures by base name ("cw_apt_brk"), looking for texture/<name>.tex
// and then .tga. Animated textures (tune/<name>.movie + texture/<name>-NNNN)
// cycle through their frames at the rate given in the .movie file.
class TextureLibrary {
public:
    TextureLibrary(render::Device& device, const vfs::Vfs& vfs);
    ~TextureLibrary();
    TextureLibrary(const TextureLibrary&) = delete;
    TextureLibrary& operator=(const TextureLibrary&) = delete;

    // Null when the texture does not exist (logged once).
    const WorldTexture* get(std::string_view name);

    // At night the game uses "<name>_ni" variants where they exist (401 of
    // the retail textures have one: lit windows, shop fronts, lamps).
    // Inferred from the data; only the night tables enable it.
    void setNight(bool night) { m_night = night; }
    bool night() const { return m_night; }
    // Advances animated textures to `time` seconds.
    void update(double time);

    std::size_t loadedCount() const { return m_textures.size(); }

private:
    struct Animation {
        std::vector<WorldTexture> frames;
        float rate = 30.0f;
        WorldTexture current;
    };
    std::optional<WorldTexture> load(const std::string& name);

    render::Device& m_device;
    const vfs::Vfs& m_vfs;
    std::unordered_map<std::string, std::optional<WorldTexture>> m_textures;
    std::unordered_map<std::string, Animation> m_animations;
    bool m_night = false;
};

} // namespace mm2::game
