#include "game/fx/LineSparks.h"

#include <bit>
#include <cmath>

namespace mm2::game::fx {
namespace {

std::uint32_t argbToRgba(std::uint32_t argb) {
    return (argb & 0xFF00FF00u) | ((argb >> 16) & 0xFFu) | ((argb & 0xFFu) << 16);
}

} // namespace

SparkLut SparkLut::builtin() {
    // asSparkLut's table when spark.tga is missing: four ramps of eight.
    SparkLut l;
    l.colors = {0xFF330000, 0xFF4C0000, 0xFF660000, 0xFF7F0000, 0xFF990000, 0xFFB20000, 0xFFCC0000, 0xFFE50000,
                0xFF330019, 0xFF4C0019, 0xFF660019, 0xFF7F1933, 0xFF99334C, 0xFFB24C7F, 0xFFCC66B2, 0xFFE599CC,
                0xFF330019, 0xFF4C0019, 0xFF660019, 0xFF7F3333, 0xFF994C66, 0xFFB2667F, 0xFFCC7F99, 0xFFE599B2,
                0xFF4C0000, 0xFF660000, 0xFF7F0000, 0xFF990000, 0xFFB20000, 0xFFCC4C4C, 0xFFE59999, 0xFFFFFFFF};
    l.shift = 5;
    l.rows = 4;
    return l;
}

SparkLut SparkLut::load(const vfs::Vfs& vfs, std::string_view texture) {
    const auto bytes = vfs.readAll("texture/" + std::string(texture) + ".tga");
    if (!bytes || bytes->size() < 18)
        return builtin();
    const auto* d = reinterpret_cast<const std::uint8_t*>(bytes->data());
    if (d[0] != 0 || d[1] != 0 || d[2] != 2)
        return builtin();
    const unsigned w = d[12] | (d[13] << 8), h = d[14] | (d[15] << 8), bpp = d[16];
    if (!std::has_single_bit(w) || !std::has_single_bit(h) || w * h > 256 || (bpp != 24 && bpp != 32))
        return builtin();
    const std::size_t stride = bpp / 8;
    if (bytes->size() < 18 + w * h * stride)
        return builtin();
    SparkLut l;
    l.rows = static_cast<int>(h);
    l.shift = 0;
    for (unsigned width = w; width < 256; width <<= 1)
        ++l.shift;
    // Pixels in file order; 24-bit ones get alpha 0x80.
    for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; ++i) {
        const std::uint8_t* p = d + 18 + i * stride;
        const std::uint32_t a = stride == 4 ? p[3] : 0x80u;
        l.colors.push_back((a << 24) | (static_cast<std::uint32_t>(p[2]) << 16) |
                           (static_cast<std::uint32_t>(p[1]) << 8) | p[0]);
    }
    return l;
}

LineSparks::LineSparks(SparkLut lut) : m_lut(std::move(lut)), m_sparks(kMax) {}

void LineSparks::radialBlast(int count, const Vec3& position, const Vec3& normal) {
    // Two axes across the normal.
    const Vec3 axis = std::abs(normal.y) >= 0.95f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    const Vec3 t = axis.cross(normal);
    const Vec3 b = t.cross(normal);
    for (; count > 0; --count) {
        if (m_count >= kMax)
            continue;
        Spark& s = m_sparks[static_cast<std::size_t>(m_count)];
        const float rx = m_rand.frand(), rz = m_rand.frand(), ry = m_rand.frand();
        // Born within 5 cm of the impact (0.1 wide, up to 0.1 along the normal).
        Vec3 local{(rx - 0.5f) * 0.1f, ry * 0.1f, (rz - 0.5f) * 0.1f};
        s.position = s.tail = local + position;
        const float len2 = local.mag2();
        local = len2 == 0.0f ? Vec3{} : local * (1.0f / std::sqrt(len2));
        const float across = m_rand.frand() * (7.0f - 6.0f) + 6.0f;
        const float along = m_rand.frand() * (5.0f - 4.0f) + 4.0f;
        local = {local.x * across, local.y * along, local.z * across};
        s.velocity = t * local.x + normal * local.y + b * local.z;
        const int rowMask = (m_lut.rows - 1) << (8 - m_lut.shift);
        s.row = static_cast<std::uint8_t>(m_rand.irand() & rowMask);
        s.age = static_cast<std::uint8_t>((m_rand.irand() & 0x3F) - 0x40);
        ++m_count;
    }
}

void LineSparks::update(float dt) {
    m_accumulator += dt;
    if (m_accumulator >= 1.0f / 30.0f) {
        step(m_accumulator);
        m_accumulator = 0.0f;
    }
}

// asLineSparks::Update(float): age by 650 per second (a 0-255 byte), fall
// at 20 m/s^2, bounce off y = 0 losing 20%; the line runs from just behind
// the old position to the new one.
void LineSparks::step(float dt) {
    const float trail = dt * -0.036f;
    const int ageStep = static_cast<int>(dt * -650.0f);
    for (int i = 0; i < m_count; ++i) {
        Spark& s = m_sparks[static_cast<std::size_t>(i)];
        const int age = static_cast<int>(s.age) + ageStep;
        if (age < 0) {
            s = m_sparks[static_cast<std::size_t>(--m_count)];
            --i;
            continue;
        }
        s.age = static_cast<std::uint8_t>(age);
        const std::size_t index = static_cast<std::size_t>((s.age >> m_lut.shift) + s.row);
        s.color = index < m_lut.colors.size() ? m_lut.colors[index] : 0xFFFFFFFFu;
        s.tail = s.velocity * trail + s.position;
        s.velocity.y += dt * -20.0f;
        s.position += s.velocity * dt;
        if (s.position.y < 0.0f && s.velocity.y < 0.0f)
            s.velocity.y *= -0.8f;
    }
}

void LineSparks::draw(render::Device& device) const {
    if (!m_count)
        return;
    std::vector<render::Vertex3D> vertices;
    for (const auto& s : sparks()) {
        for (const Vec3* p : {&s.position, &s.tail}) {
            render::Vertex3D v{};
            v.position[0] = p->x;
            v.position[1] = p->y;
            v.position[2] = p->z;
            v.normal[1] = 1.0f;
            v.color = argbToRgba(s.color);
            vertices.push_back(v);
        }
    }
    render::DrawCall call;
    call.vertices = device.uploadTransient(render::BufferKind::Vertex, std::span<const render::Vertex3D>(vertices));
    call.count = static_cast<std::uint32_t>(vertices.size());
    call.constants.world = Mat44::identity();
    call.constants.flags = render::DrawFlag::VertexColor | render::DrawFlag::Fog;
    call.state.topology = render::Topology::LineList;
    call.state.blend = render::BlendMode::Alpha;
    call.state.depthWrite = false;
    call.state.cull = render::CullMode::None;
    device.draw(call);
}

} // namespace mm2::game::fx
