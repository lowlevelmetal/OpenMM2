#pragma once

#include "asset/Pkg.h"
#include "game/TextureLibrary.h"
#include "game/fx/Random.h"
#include "render/Device.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mm2::game {

// fxTexelDamage: a car body's dents and scrapes painted into copies of its
// textures.
//
// Every body material "<name>" with a "<name>_dmg" texture (or a "_dmg"
// material whose clean "<name>" exists) draws a private copy of the clean
// texture at the high and medium LODs. An impact copies patches of the
// "_dmg" texels into the copy: for every high LOD body triangle with a
// damage texture and a corner within the radius of the impact point, one
// patch around a random point of the triangle (a 7-row blob, small or large
// at random). A "_dmg" material without a clean texture starts damaged.
class TexelDamage {
public:
    // `materials` are the paint job's materials as stored; their texture
    // names are replaced by the private copies (registered in `textures`
    // under unique names, with `tag` to tell cars apart).
    TexelDamage(render::Device& device, TextureLibrary& textures, const asset::PkgMesh& bodyHigh,
                std::vector<asset::PkgMaterial>& materials, const std::string& tag);
    ~TexelDamage();
    TexelDamage(const TexelDamage&) = delete;
    TexelDamage& operator=(const TexelDamage&) = delete;

    bool active() const { return m_active; }
    // fxTexelDamage::ApplyDamage: `point` in the body's model space. With
    // `seed` the patches' random numbers start from that state (OpenMM2: a
    // network car's patches come out as its owner's did).
    void apply(const Vec3& point, float radius, std::optional<std::uint32_t> seed = {});
    // The state the next apply() draws its random numbers from.
    std::uint32_t randomState() const { return m_rand.state(); }
    // fxTexelDamage::Reset: the copies back to clean.
    void reset();

private:
    struct Layer {
        asset::Image clean, damage; // top levels only
        std::vector<std::uint8_t> live;
        std::uint32_t width = 0, height = 0;
        std::string name; // registered name of the copy
        render::TextureHandle handle;
        bool used = false;
    };
    struct Triangle {
        Vec3 p[3];
        Vec2 uv[3];
        std::size_t layer = 0;
    };
    void stamp(Layer& layer, int x, int y, bool large);
    void upload(Layer& layer);

    render::Device& m_device;
    TextureLibrary& m_textures;
    std::vector<Layer> m_layers; // by material index
    std::vector<Triangle> m_triangles;
    fx::Rand m_rand{0xDA6Au};
    bool m_active = false;
};

} // namespace mm2::game
