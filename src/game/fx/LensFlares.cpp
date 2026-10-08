#include "game/fx/LensFlares.h"

#include "game/TextureLibrary.h"

#include <algorithm>
#include <cmath>

// Port of ltLensFlare, ltFlare and ltLight::ComputeIntensity (MM2 build
// 3393, documented by MM2Recomp), in the original's operation order.

namespace mm2::game::fx {
namespace {

// ltLensFlare's constructor: the brightness that draws at all (intensity x
// scale must reach the minimum), and the screen radii (in units of the
// screen's half height) inside which the flares are full and beyond which
// they vanish.
constexpr float kIntensityScale = 0.5f;
constexpr float kMinimum = 0.05f;
constexpr float kInnerRadius = 0.5f;
constexpr float kOuterRadius = 2.0f;
// The cars' ltLights: intensity 25, spot exponent 3 (ltLight::Default,
// vehSiren::AddLight).
constexpr float kLightIntensity = 25.0f;
constexpr float kSpotExponent = 3.0f;

std::uint32_t toArgb(float r, float g, float b) {
    auto byte = [](float v) { return static_cast<std::uint32_t>(static_cast<int>(v * 255.0f)) & 0xFFu; };
    return 0xFF000000u | byte(r) << 16 | byte(g) << 8 | byte(b);
}

std::uint32_t argbToRgba(std::uint32_t argb) {
    return (argb & 0xFF00FF00u) | ((argb >> 16) & 0xFFu) | ((argb & 0xFFu) << 16);
}

} // namespace

LensFlare::LensFlare(int count, Rand& rng) {
    m_flares.resize(static_cast<std::size_t>(std::max(count, 2)));
    for (auto& f : m_flares) {
        // ltFlare::Random.
        const float a = rng.frand(), b = rng.frand(), c = rng.frand();
        f.r = (b + 1.0f) * 0.5f;
        f.g = (a + 1.0f) * 0.5f;
        f.b = (c + 1.0f) * 0.5f;
        float k = rng.frand();
        k = k * 1.5f * k * 1.5f;
        f.along = k;
        f.brightness = std::min(1.0f, 0.25f / k);
        f.size = std::sqrt(k) * 0.1f;
        f.reach = rng.frand() * 0.5f + 1.5f;
        if (rng.frand() < 0.5f)
            f.along = f.along * -1.0f;
    }
    m_flares[0].along = 1.0f;
    m_flares[0].size = 0.3f;
    m_flares[1].along = -1.0f;
    m_flares[1].size = 0.25f;
}

void LensFlare::draw(const Vec3& position, const Vec3& color, float intensity, const Mat44& viewProj, float aspect,
                     std::vector<LensFlareQuad>& out) const {
    if (!(kMinimum <= intensity * kIntensityScale))
        return;
    const Vec4 clip = viewProj.transform({position.x, position.y, position.z, 1.0f});
    const float invW = 1.0f / clip.w;
    const float y = invW * clip.y;
    if (!(invW * clip.z <= 1.0f))
        return;
    // MM2's flare viewport is an orthographic -1.33..1.33 by -1..1 over the
    // whole screen; OpenMM2 uses the screen's own aspect, so the cards stay
    // square at any resolution (the same at 4:3).
    const float x = invW * clip.x * aspect;
    const float r2 = x * x + y * y;
    if (!(r2 <= kOuterRadius * kOuterRadius))
        return;
    if (kInnerRadius * kInnerRadius < r2) {
        const float fade = (std::sqrt(r2) - kOuterRadius) / (kInnerRadius - kOuterRadius);
        if (fade * intensity * kIntensityScale < kMinimum)
            return;
    }
    for (const Flare& f : m_flares) {
        if (!(r2 <= f.reach * f.reach))
            continue;
        const float x0 = x * f.along - f.size;
        const float y0 = y * f.along - f.size;
        const float side = f.size + f.size;
        const float m = std::min(intensity * f.brightness, 1.0f);
        const float red = color.x * f.r * m, green = f.g * color.y * m, blue = f.b * color.z * m;
        out.push_back({{x0 / aspect, y0}, {(side + x0) / aspect, side + y0}, toArgb(red, green, blue)});
    }
}

float spotIntensity(const Vec3& position, const Vec3& direction, const Vec3& eye, float threshold) {
    const float dx = eye.x - position.x, dy = eye.y - position.y, dz = eye.z - position.z;
    const float d2 = dx * dx + dy * dy + dz * dz;
    if (kLightIntensity < d2 * threshold)
        return 0.0f;
    float value = kLightIntensity / d2;
    const float inv = d2 == 0.0f ? 0.0f : 1.0f / std::sqrt(d2);
    const float c = (dx * direction.x + dy * direction.y + dz * direction.z) * inv;
    if (c < 0.0f)
        return 0.0f;
    value = std::pow(c, kSpotExponent) * value;
    value = value - threshold;
    return value < 0.0f ? 0.0f : value;
}

void drawLensFlares(render::Device& device, TextureLibrary& textures, std::span<const LensFlareQuad> quads) {
    if (quads.empty())
        return;
    std::vector<render::Vertex3D> vertices;
    std::vector<std::uint16_t> indices;
    for (const auto& q : quads) {
        if (vertices.size() + 4 > 65535)
            break;
        const auto base = static_cast<std::uint16_t>(vertices.size());
        auto corner = [&](float x, float y, float u, float v) {
            render::Vertex3D rv{};
            rv.position[0] = x;
            rv.position[1] = y;
            rv.position[2] = 0.5f;
            rv.normal[2] = 1.0f;
            rv.color = argbToRgba(q.argb);
            rv.uv0[0] = rv.uv1[0] = u;
            rv.uv0[1] = rv.uv1[1] = v;
            vertices.push_back(rv);
        };
        corner(q.min.x, q.min.y, 0.0f, 0.0f);
        corner(q.max.x, q.min.y, 1.0f, 0.0f);
        corner(q.max.x, q.max.y, 1.0f, 1.0f);
        corner(q.min.x, q.max.y, 0.0f, 1.0f);
        for (std::uint16_t i : {std::uint16_t(0), std::uint16_t(1), std::uint16_t(2), std::uint16_t(0),
                                std::uint16_t(2), std::uint16_t(3)})
            indices.push_back(static_cast<std::uint16_t>(base + i));
    }
    // Clip space directly: the cards are already in normalised coordinates.
    render::FrameConstants frame;
    frame.view = Mat44::identity();
    frame.proj = Mat44::identity();
    device.setFrameConstants(frame);
    render::DrawCall call;
    call.vertices = device.uploadTransient(render::BufferKind::Vertex, std::span<const render::Vertex3D>(vertices));
    call.indices = device.uploadTransient(render::BufferKind::Index, std::span<const std::uint16_t>(indices));
    call.count = static_cast<std::uint32_t>(indices.size());
    call.constants.world = Mat44::identity();
    call.constants.flags = render::DrawFlag::VertexColor;
    if (const WorldTexture* tex = textures.get("lt_flare")) {
        call.constants.flags |= render::DrawFlag::Texture0;
        call.textures[0] = {tex->handle, tex->sampler};
    }
    call.state.blend = render::BlendMode::Add;
    call.state.depthTest = false;
    call.state.depthWrite = false;
    call.state.cull = render::CullMode::None;
    device.draw(call);
}

} // namespace mm2::game::fx
