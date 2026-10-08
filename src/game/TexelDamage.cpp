#include "game/TexelDamage.h"

#include "core/Log.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <format>

namespace mm2::game {
namespace {

// The patch rows (y - 3 .. y + 3) as [x + from, x + to) spans.
constexpr int kSmall[7][2] = {{-2, 3}, {-4, 4}, {-4, 5}, {-5, 4}, {-4, 6}, {-3, 4}, {-2, 3}};
constexpr int kLarge[7][2] = {{-4, 6}, {-8, 8}, {-8, 10}, {-10, 8}, {-8, 12}, {-6, 8}, {-4, 6}};

} // namespace

TexelDamage::TexelDamage(render::Device& device, TextureLibrary& textures, const asset::PkgMesh& bodyHigh,
                         std::vector<asset::PkgMaterial>& materials, const std::string& tag)
    : m_device(device), m_textures(textures) {
    m_layers.resize(materials.size());
    // fxTexelDamage::Init: the materials of the body's high LOD sections.
    for (const auto& section : bodyHigh.sections) {
        const std::size_t s = section.shaderIndex;
        if (s >= materials.size() || m_layers[s].used)
            continue;
        const std::string name = str::lower(materials[s].texture);
        if (name.empty())
            continue;
        std::string cleanName = name, damageName = name + "_dmg";
        if (str::iendsWith(name, "_dmg")) {
            damageName = name;
            cleanName = name.substr(0, name.size() - 4);
        }
        auto damage = m_textures.image(damageName);
        if (!damage)
            continue;
        auto clean = m_textures.image(cleanName);
        if (!clean)
            clean = damage; // only the damaged picture: starts damaged
        if (clean->width() != damage->width() || clean->height() != damage->height()) {
            log::warn("texel damage: {} and {} differ in size", cleanName, damageName);
            continue;
        }
        Layer& layer = m_layers[s];
        layer.used = true;
        layer.width = clean->width();
        layer.height = clean->height();
        layer.clean.levels = {clean->levels[0]};
        layer.damage.levels = {damage->levels[0]};
        layer.live = layer.clean.levels[0].rgba;
        layer.name = std::format("{}#{}#{}", cleanName, tag, s);
        // gfxTexture::Clone: a single-level surface (no mipmaps), copied from
        // the top level.
        const std::vector<render::TextureData> data{{layer.live.data(), 0}};
        render::TextureDesc desc;
        desc.width = layer.width;
        desc.height = layer.height;
        desc.mipLevels = 1;
        desc.debugName = layer.name;
        WorldTexture t;
        t.handle = m_device.createTexture(desc, data);
        t.width = layer.width;
        t.height = layer.height;
        // gfxTexture::Clone keeps the texture environment (address modes and
        // the alpha-format bit) of the texture it copies.
        if (const WorldTexture* base = m_textures.get(cleanName)) {
            t.sampler = base->sampler;
            t.alphaFormat = base->alphaFormat;
        } else if (const WorldTexture* dmg = m_textures.get(damageName)) {
            t.sampler = dmg->sampler;
            t.alphaFormat = dmg->alphaFormat;
        }
        t.translucent = clean->hasTranslucency();
        layer.handle = t.handle;
        m_textures.adopt(layer.name, t);
        materials[s].texture = layer.name;
        m_active = true;
    }
    if (!m_active)
        return;
    // The high LOD body's triangles with a damage texture.
    for (const auto& section : bodyHigh.sections) {
        if (section.shaderIndex >= m_layers.size() || !m_layers[section.shaderIndex].used)
            continue;
        for (const auto& packet : section.packets)
            for (std::size_t i = 0; i + 2 < packet.indices.size(); i += 3) {
                Triangle t;
                t.layer = section.shaderIndex;
                bool ok = true;
                for (int k = 0; k < 3; ++k) {
                    const auto v = packet.indices[i + static_cast<std::size_t>(k)];
                    if (v >= packet.vertices.size()) {
                        ok = false;
                        break;
                    }
                    t.p[k] = packet.vertices[v].position;
                    t.uv[k] = packet.vertices[v].uv;
                }
                if (ok)
                    m_triangles.push_back(t);
            }
    }
}

// fxTexelDamage::~fxTexelDamage (fxTexelDamage::Kill): the private copies go.
TexelDamage::~TexelDamage() {
    for (const auto& layer : m_layers)
        if (layer.used)
            m_textures.release(layer.name);
}

void TexelDamage::stamp(Layer& layer, int x, int y, bool large) {
    const auto& shape = large ? kLarge : kSmall;
    const auto& src = layer.damage.levels[0].rgba;
    for (int r = 0; r < 7; ++r) {
        const int row = y - 3 + r;
        if (row < 0 || row >= static_cast<int>(layer.height))
            continue;
        const int from = std::max(0, x + shape[r][0]);
        const int to = std::min(static_cast<int>(layer.width), x + shape[r][1]);
        if (to <= from)
            continue;
        const std::size_t offset = (static_cast<std::size_t>(row) * layer.width + static_cast<std::size_t>(from)) * 4;
        const std::size_t bytes = static_cast<std::size_t>(to - from) * 4;
        std::copy_n(src.begin() + static_cast<std::ptrdiff_t>(offset), bytes,
                    layer.live.begin() + static_cast<std::ptrdiff_t>(offset));
    }
}

void TexelDamage::upload(Layer& layer) {
    m_device.updateTexture(layer.handle, 0, {0, 0, layer.width, layer.height}, layer.live.data());
}

void TexelDamage::apply(const Vec3& point, float radius) {
    if (!m_active)
        return;
    const float r2 = radius * radius;
    std::vector<bool> dirty(m_layers.size(), false);
    for (const auto& t : m_triangles) {
        if (!(t.p[0].dist2(point) < r2 || t.p[1].dist2(point) < r2 || t.p[2].dist2(point) < r2))
            continue;
        // A random point of the triangle.
        const float a = m_rand.frand(), b = m_rand.frand(), c = m_rand.frand();
        const float n = 1.0f / (c + b + a);
        const float u = n * a * t.uv[0].x + n * b * t.uv[1].x + n * c * t.uv[2].x;
        const float v = n * c * t.uv[2].y + n * b * t.uv[1].y + n * a * t.uv[0].y;
        Layer& layer = m_layers[t.layer];
        const int x = static_cast<int>(static_cast<float>(layer.width) * u);
        const int y = static_cast<int>(static_cast<float>(layer.height) * v);
        stamp(layer, x, y, (m_rand.irand() & 1) == 0);
        dirty[t.layer] = true;
    }
    for (std::size_t i = 0; i < m_layers.size(); ++i)
        if (dirty[i])
            upload(m_layers[i]);
}

void TexelDamage::reset() {
    for (auto& layer : m_layers) {
        if (!layer.used || layer.live == layer.clean.levels[0].rgba)
            continue;
        layer.live = layer.clean.levels[0].rgba;
        upload(layer);
    }
}

} // namespace mm2::game
