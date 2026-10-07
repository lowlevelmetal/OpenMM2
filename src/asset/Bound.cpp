#include "asset/Bound.h"

#include "asset/Reader.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <format>

namespace mm2::asset {
namespace {

using detail::Reader;

bool fail(std::string* error, std::string msg) {
    if (error)
        *error = std::move(msg);
    return false;
}

constexpr std::uint32_t kMaxBoundVerts = 65536;
constexpr std::uint32_t kMaxBoundPolys = 1u << 20;
constexpr std::uint32_t kMaxBoundMaterials = 256;

// Whitespace tokenizer that remembers line numbers for error messages.
class Tokens {
public:
    explicit Tokens(std::string_view text) : m_text(text) {}

    bool next(std::string_view& tok) {
        while (m_pos < m_text.size() && isSpace(m_text[m_pos])) {
            if (m_text[m_pos] == '\n')
                ++m_line;
            ++m_pos;
        }
        if (m_pos >= m_text.size())
            return false;
        const std::size_t start = m_pos;
        while (m_pos < m_text.size() && !isSpace(m_text[m_pos]))
            ++m_pos;
        tok = m_text.substr(start, m_pos - start);
        return true;
    }
    int line() const { return m_line; }

private:
    static bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
    std::string_view m_text;
    std::size_t m_pos = 0;
    int m_line = 1;
};

} // namespace

Aabb BoundGeometry::bounds() const {
    Aabb b;
    for (const auto& v : vertices)
        b.expand(v);
    return b;
}

std::optional<BoundGeometry> parseBnd(std::string_view text, std::string* error) {
    Tokens t(text);
    BoundGeometry g;
    std::string_view tok;
    auto err = [&](std::string msg) {
        fail(error, std::format("line {}: {}", t.line(), msg));
        return std::nullopt;
    };
    auto number = [&](double& out) {
        std::string_view s;
        if (!t.next(s))
            return false;
        auto v = str::parseDouble(s);
        if (!v)
            return false;
        out = *v;
        return true;
    };
    auto integer = [&](long long& out) {
        std::string_view s;
        if (!t.next(s))
            return false;
        auto v = str::parseInt(s);
        if (!v)
            return false;
        out = *v;
        return true;
    };

    long long declaredVerts = -1, declaredMaterials = -1, declaredPolys = -1;
    while (t.next(tok)) {
        if (tok == "version:" || tok == "verts:" || tok == "materials:" || tok == "edges:" || tok == "polys:") {
            double v = 0;
            if (!number(v))
                return err(std::format("expected a number after '{}'", tok));
            if (tok == "verts:")
                declaredVerts = static_cast<long long>(v);
            else if (tok == "materials:")
                declaredMaterials = static_cast<long long>(v);
            else if (tok == "polys:")
                declaredPolys = static_cast<long long>(v);
            if (declaredVerts > kMaxBoundVerts || declaredPolys > kMaxBoundPolys ||
                declaredMaterials > kMaxBoundMaterials)
                return err("implausible element count");
        } else if (tok == "v") {
            double x, y, z;
            if (!number(x) || !number(y) || !number(z))
                return err("bad vertex");
            g.vertices.push_back({static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)});
        } else if (tok == "mtl") {
            BoundMaterial m;
            std::string_view name, brace;
            if (!t.next(name) || !t.next(brace) || brace != "{")
                return err("bad material header");
            m.name = std::string(name);
            while (true) {
                std::string_view key;
                if (!t.next(key))
                    return err("unterminated material");
                if (key == "}")
                    break;
                std::string_view value;
                if (!t.next(value))
                    return err("material field without value");
                if (key == "elasticity:")
                    m.elasticity = static_cast<float>(str::parseDouble(value).value_or(m.elasticity));
                else if (key == "friction:")
                    m.friction = static_cast<float>(str::parseDouble(value).value_or(m.friction));
                else if (key == "effect:")
                    m.effect = std::string(value);
                else if (key == "sound:")
                    m.sound = std::string(value);
            }
            g.materials.push_back(std::move(m));
        } else if (tok == "tri" || tok == "quad") {
            const int n = tok == "tri" ? 3 : 4;
            BoundPolygon p;
            for (int i = 0; i < n; ++i) {
                long long idx = 0;
                if (!integer(idx) || idx < 0 || idx > 0xFFFF)
                    return err("bad polygon index");
                p.indices[static_cast<std::size_t>(i)] = static_cast<std::uint16_t>(idx);
            }
            long long mtl = 0;
            if (!integer(mtl) || mtl < 0 || mtl > 0xFFFF)
                return err("bad polygon material");
            p.material = static_cast<std::uint16_t>(mtl);
            g.polygons.push_back(p);
        } else if (tok == "edge") {
            long long a = 0, b = 0;
            if (!integer(a) || !integer(b))
                return err("bad edge");
            g.edges.push_back({static_cast<std::uint16_t>(a), static_cast<std::uint16_t>(b)});
        } else {
            return err(std::format("unexpected token '{}'", tok));
        }
    }

    if (declaredVerts >= 0 && static_cast<std::size_t>(declaredVerts) != g.vertices.size())
        return err(std::format("declared {} vertices, found {}", declaredVerts, g.vertices.size()));
    if (declaredPolys >= 0 && static_cast<std::size_t>(declaredPolys) != g.polygons.size())
        return err(std::format("declared {} polygons, found {}", declaredPolys, g.polygons.size()));
    if (g.materials.empty())
        g.materials.emplace_back();
    for (const auto& p : g.polygons) {
        for (int i = 0; i < p.vertexCount(); ++i)
            if (p.indices[static_cast<std::size_t>(i)] >= g.vertices.size())
                return err("polygon index out of range");
        if (p.material >= g.materials.size())
            return err("polygon material out of range");
    }
    return g;
}

std::optional<BoundGeometry> parseBbnd(std::span<const std::byte> data, std::string* error) {
    Reader r(data);
    const std::uint8_t version = r.u8();
    const std::uint32_t vertCount = r.u32();
    const std::uint32_t materialCount = r.u32();
    const std::uint32_t polyCount = r.u32();
    if (!r.ok()) {
        fail(error, "truncated header");
        return std::nullopt;
    }
    if (version != 1) {
        fail(error, std::format("unsupported binary bound version {}", version));
        return std::nullopt;
    }
    if (vertCount > kMaxBoundVerts || polyCount > kMaxBoundPolys || materialCount > kMaxBoundMaterials) {
        fail(error, "implausible element count");
        return std::nullopt;
    }
    const std::size_t expected = 13 + std::size_t{vertCount} * 12 + std::size_t{materialCount} * 104 +
                                 std::size_t{polyCount} * 10;
    if (data.size() != expected) {
        fail(error, std::format("size {} does not match header (expected {})", data.size(), expected));
        return std::nullopt;
    }
    BoundGeometry g;
    g.vertices.resize(vertCount);
    for (auto& v : g.vertices)
        v = r.vec3();
    g.materials.resize(materialCount);
    for (auto& m : g.materials) {
        m.name = r.fixedString(32);
        m.elasticity = r.f32();
        m.friction = r.f32();
        m.effect = r.fixedString(32);
        m.sound = r.fixedString(32);
    }
    g.polygons.resize(polyCount);
    for (auto& p : g.polygons) {
        for (auto& i : p.indices)
            i = r.u16();
        p.material = r.u16();
        for (int i = 0; i < p.vertexCount(); ++i)
            if (p.indices[static_cast<std::size_t>(i)] >= vertCount) {
                fail(error, "polygon index out of range");
                return std::nullopt;
            }
        if (p.material >= std::max<std::uint32_t>(materialCount, 1)) {
            fail(error, "polygon material out of range");
            return std::nullopt;
        }
    }
    if (g.materials.empty())
        g.materials.emplace_back();
    return g;
}

std::optional<TerrainBound> parseTer(std::span<const std::byte> data, std::string* error) {
    Reader r(data);
    TerrainBound t;
    t.version = r.f32();
    const std::uint32_t polyCount = r.u32();
    const std::uint32_t edgeCount = r.u32();
    t.useHotEdges = r.u8() != 0;
    t.size = r.vec3();
    t.widthSections = r.u32();
    t.heightSections = r.u32();
    t.depthSections = r.u32();
    const std::uint32_t sectionCount = r.u32();
    const std::uint32_t refCount = r.u32();
    t.sectionSizeFactors = r.vec3();
    t.min = r.vec3();
    t.max = r.vec3();
    if (!r.ok()) {
        fail(error, "truncated header");
        return std::nullopt;
    }
    if (polyCount > kMaxBoundPolys || edgeCount > kMaxBoundPolys * 4 || sectionCount > (1u << 20) ||
        refCount > (1u << 22) ||
        std::uint64_t{t.widthSections} * t.heightSections * t.depthSections != sectionCount) {
        fail(error, "implausible terrain header");
        return std::nullopt;
    }
    const std::size_t expected = r.pos() + std::size_t{sectionCount} * 4 + std::size_t{refCount} * 2 +
                                 std::size_t{edgeCount} * 4 + std::size_t{polyCount} * 16 +
                                 std::size_t{edgeCount} * 16;
    if (data.size() != expected) {
        fail(error, std::format("size {} does not match header (expected {})", data.size(), expected));
        return std::nullopt;
    }
    t.sectionOffsets.resize(sectionCount);
    for (auto& v : t.sectionOffsets)
        v = r.u16();
    t.sectionCounts.resize(sectionCount);
    for (auto& v : t.sectionCounts)
        v = r.u16();
    t.sectionPolygons.resize(refCount);
    for (auto& v : t.sectionPolygons)
        v = r.u16();
    t.edges.resize(edgeCount);
    for (auto& e : t.edges) {
        e[0] = r.u16();
        e[1] = r.u16();
    }
    t.polygonEdges.resize(polyCount);
    for (auto& pe : t.polygonEdges)
        for (auto& e : pe)
            e = r.u32();
    t.edgeNormals.resize(edgeCount);
    for (auto& n : t.edgeNormals)
        n = r.vec3();
    t.edgeValues.resize(edgeCount);
    for (auto& v : t.edgeValues)
        v = r.f32();
    // Consistency checks on the grid.
    for (std::uint32_t s = 0; s < sectionCount; ++s) {
        if (std::size_t{t.sectionOffsets[s]} + t.sectionCounts[s] > refCount) {
            fail(error, "section polygon list out of range");
            return std::nullopt;
        }
    }
    for (auto p : t.sectionPolygons)
        if (p >= polyCount) {
            fail(error, "section references a polygon out of range");
            return std::nullopt;
        }
    return t;
}

} // namespace mm2::asset
