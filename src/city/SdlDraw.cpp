#include "city/SdlDraw.h"

#include <cmath>

// Port of sdlPage16::Draw and its helpers (sdlPage16::ArcMap, GetCentroid,
// ComputeBoundSphere, sdlCommon::BACKFACE, the level choice in
// cityLevel::DrawRooms) from midtown2.exe build 3393, documented by the
// MM2Recomp disassembly. The operation order and the 32-bit float math
// follow the original (the x87 runs in single precision).
//
// sdlPage16::Draw sends immediate-mode strips and fans through vgl; here
// each becomes a primitive whose triangles go into the room's index list.
// Draw is called with the level of detail; a primitive that every level
// draws the same way is built once and listed in each level.

namespace mm2::city {
namespace {

// sdlPage16::Draw: the curb line stands this high above the road at the top
// level of detail, and level 1 sinks the road's outer edges by as much.
constexpr float kCurbRise = 0.15f;
// Sidewalk strips repeat their texture every 4 m, fans and roofs every 8 m.
constexpr float kSidewalkRepeat = 0.25f;
constexpr float kFanRepeat = 0.125f;
// Wedged dividers: the top edges are this far in from the median's edges
// and this high.
constexpr float kWedgeInset = 0.4f;
constexpr float kWedgeHeight = 1.0f;
// cityLevel's street level of detail thresholds (sm_SDLVLowThresh,
// sm_SDLLowThresh, sm_SDLMedThresh), never changed at run time.
constexpr float kVLowThresh = 300.0f;
constexpr float kLowThresh = 100.0f;
constexpr float kMedThresh = 50.0f;

// Divider types (the low six bits of the divided road's first word).
constexpr int kDividerFlat = 1;
constexpr int kDividerRaised = 2;
constexpr int kDividerWedged = 3;

constexpr std::uint8_t kAllLods = 0x0F;
constexpr std::uint8_t lodBit(int lod) {
    return static_cast<std::uint8_t>(1u << lod);
}

float dist(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Vector3::FlatDist.
float flatDist(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

Vec3 raised(Vec3 p, float rise) {
    p.y = p.y + rise;
    return p;
}

// Vector3::InvMag then Scale.
Vec3 normalizedOrZero(const Vec3& v) {
    const float m2 = v.x * v.x + v.y * v.y + v.z * v.z;
    if (m2 == 0.0f)
        return {};
    const float inv = 1.0f / std::sqrt(m2);
    return {v.x * inv, v.y * inv, v.z * inv};
}

// The midpoint the road's two halves meet at: (b - a) * 0.5 + a.
Vec3 midpoint(const Vec3& a, const Vec3& b) {
    return {(b.x - a.x) * 0.5f + a.x, (b.y - a.y) * 0.5f + a.y, (b.z - a.z) * 0.5f + a.z};
}

class Builder {
public:
    Builder(const Psdl& p, std::size_t room, SdlRoomDraw& out) : m_p(p), m_room(room), m_out(out) {}

    void build() {
        for (const auto& a : m_p.rooms[m_room].attributes) {
            switch (a.type) {
            case PsdlAttrType::RoadStrip:
                if (!m_skip)
                    roadStrip(a);
                break;
            case PsdlAttrType::SidewalkStrip:
                if (!m_skip)
                    sidewalkStrip(a);
                break;
            case PsdlAttrType::RectangleStrip:
                if (!m_skip)
                    rectangleStrip(a);
                break;
            case PsdlAttrType::Sliver:
                sliver(a);
                break;
            case PsdlAttrType::Crosswalk:
                if (!m_skip)
                    crosswalk(a);
                break;
            case PsdlAttrType::RoadTriangleFan:
                if (!m_skip)
                    fan(a, true);
                break;
            case PsdlAttrType::TriangleFan:
                if (!m_skip)
                    fan(a, false);
                break;
            case PsdlAttrType::FacadeBound:
                // Draws nothing; the following facades and slivers take
                // its light table entry.
                if (!a.args.empty())
                    m_light = static_cast<std::uint8_t>(a.facadeBoundAngle() & 63);
                break;
            case PsdlAttrType::DividedRoadStrip:
                if (!m_skip)
                    dividedRoadStrip(a);
                break;
            case PsdlAttrType::Tunnel:
                break; // not ported (see SdlDraw.h)
            case PsdlAttrType::Texture:
                // The texture group (its value, 0 = none). With none, the
                // road, sidewalk, rectangle, crosswalk, fan and divided road
                // attributes after it are not drawn.
                m_value = a.textureBase() + 1;
                m_skip = m_value == 0;
                break;
            case PsdlAttrType::Facade:
                facade(a);
                break;
            case PsdlAttrType::RoofTriangleFan:
                roof(a);
                break;
            }
        }
    }

private:
    // The page's texture table holds "none" at 0 and texture n at n + 1;
    // Draw binds the entry of the current value plus an offset.
    int texture(int value, int offset) const {
        const int t = value - 1 + offset;
        return t >= 0 && t < static_cast<int>(m_p.textures.size()) ? t : -1;
    }
    int tex(int offset) const { return texture(m_value, offset); }

    Vec3 vert(std::uint16_t i) const { return i < m_p.vertices.size() ? m_p.vertices[i] : Vec3{}; }
    float height(std::uint16_t i) const { return i < m_p.heights.size() ? m_p.heights[i] : 0.0f; }
    static std::uint16_t at(std::span<const std::uint16_t> v, std::size_t i) {
        return i < v.size() ? v[i] : 0;
    }

    // ---- primitives ---------------------------------------------------------

    void begin(int texture, SdlShade shade = SdlShade::Room) {
        m_prim = {};
        m_prim.texture = texture;
        m_prim.shade = shade;
        m_first = static_cast<std::uint32_t>(m_out.vertices.size());
    }
    void vertex(const Vec3& p, float s, float t) { m_out.vertices.push_back({p, {s, t}}); }

    std::uint32_t emitted() const { return static_cast<std::uint32_t>(m_out.vertices.size()) - m_first; }

    // vglBegin(D3DPT_TRIANGLESTRIP).
    void endStrip(std::uint8_t lods) {
        const std::uint32_t n = emitted();
        m_prim.firstIndex = static_cast<std::uint32_t>(m_out.indices.size());
        for (std::uint32_t i = 0; i + 2 < n; ++i) {
            if (i & 1)
                m_out.indices.insert(m_out.indices.end(), {m_first + i + 1, m_first + i, m_first + i + 2});
            else
                m_out.indices.insert(m_out.indices.end(), {m_first + i, m_first + i + 1, m_first + i + 2});
        }
        finish(lods);
    }
    // vglBegin(D3DPT_TRIANGLEFAN).
    void endFan(std::uint8_t lods) {
        const std::uint32_t n = emitted();
        m_prim.firstIndex = static_cast<std::uint32_t>(m_out.indices.size());
        for (std::uint32_t i = 1; i + 1 < n; ++i)
            m_out.indices.insert(m_out.indices.end(), {m_first, m_first + i, m_first + i + 1});
        finish(lods);
    }
    // vglBegin(D3DPT_TRIANGLELIST).
    void endList(std::uint8_t lods) {
        const std::uint32_t n = emitted();
        m_prim.firstIndex = static_cast<std::uint32_t>(m_out.indices.size());
        for (std::uint32_t i = 0; i + 2 < n; i += 3)
            m_out.indices.insert(m_out.indices.end(), {m_first + i, m_first + i + 1, m_first + i + 2});
        finish(lods);
    }
    void finish(std::uint8_t lods) {
        m_prim.indexCount = static_cast<std::uint32_t>(m_out.indices.size()) - m_prim.firstIndex;
        if (m_prim.indexCount == 0)
            return;
        for (int l = 0; l < 4; ++l)
            if (lods & lodBit(l))
                m_out.lods[static_cast<std::size_t>(l)].push_back(m_prim);
    }

    // ---- attributes ---------------------------------------------------------

    // A strip from the outer left to the outer right edge with the road's
    // low-detail texture: level 0 every other section, level 1 every
    // section with both edges sunk by the curb height.
    void lowDetailRoad(std::span<const std::uint16_t> v, int stride, int n, int right) {
        const auto s = sdlArcMap(m_p, v, stride, n, right);
        const bool odd = n % 2 == 1;
        const bool many = n > 2;
        begin(tex(2));
        for (int i = 0; i < n; ++i) {
            const auto k = static_cast<std::size_t>(i);
            vertex(vert(at(v, k * stride)), s[k], 0.0f);
            vertex(vert(at(v, k * stride + right)), s[k], 1.0f);
            if (many && (odd || i != 0))
                ++i;
        }
        endStrip(lodBit(0));

        begin(tex(2));
        for (int i = 0; i < n; ++i) {
            const auto k = static_cast<std::size_t>(i);
            vertex(raised(vert(at(v, k * stride)), -kCurbRise), s[k], 0.0f);
            vertex(raised(vert(at(v, k * stride + right)), -kCurbRise), s[k], 1.0f);
        }
        endStrip(lodBit(1));
    }

    // Levels 2 and 3: a sidewalk from the outer edge `outer` to the curb
    // `curb` (the curb raised at level 3), and at level 3 the half-bright
    // curb face. `leftSide` is the order Draw emits them in.
    void sidewalkSide(std::span<const std::uint16_t> v, int stride, int n, int outer, int curb, bool leftSide,
                      const std::vector<float>& s, int lod) {
        const float rise = lod == 3 ? kCurbRise : 0.0f;
        begin(tex(1));
        for (int i = 0; i < n; ++i) {
            const auto k = static_cast<std::size_t>(i) * static_cast<std::size_t>(stride);
            const auto si = s[static_cast<std::size_t>(i)];
            if (leftSide) {
                vertex(vert(at(v, k + outer)), si, 1.0f);
                vertex(raised(vert(at(v, k + curb)), rise), si, 0.0f);
            } else {
                vertex(raised(vert(at(v, k + curb)), rise), si, 0.0f);
                vertex(vert(at(v, k + outer)), si, 1.0f);
            }
        }
        endStrip(lodBit(lod));
        if (lod != 3)
            return;
        begin(tex(1), SdlShade::HalfRoom);
        for (int i = 0; i < n; ++i) {
            const auto k = static_cast<std::size_t>(i) * static_cast<std::size_t>(stride);
            const auto si = s[static_cast<std::size_t>(i)];
            const Vec3 c = vert(at(v, k + curb));
            if (leftSide) {
                vertex(raised(c, kCurbRise), si, 0.0f);
                vertex(c, si, 0.0f);
            } else {
                vertex(c, si, 0.0f);
                vertex(raised(c, kCurbRise), si, 0.0f);
            }
        }
        endStrip(lodBit(lod));
    }

    void roadStrip(const PsdlAttribute& a) {
        // Sections of four: outer left, curb left, curb right, outer right.
        const auto v = a.vertices();
        const int n = static_cast<int>(v.size() / 4);
        if (n <= 0)
            return;
        lowDetailRoad(v, 4, n, 3);
        for (int lod = 2; lod <= 3; ++lod) {
            if (v[0] != v[1])
                sidewalkSide(v, 4, n, 0, 1, true, sdlArcMap(m_p, v, 4, n, 1), lod);
            if (v[2] != v[3])
                sidewalkSide(v, 4, n, 3, 2, false, sdlArcMap(m_p, v.subspan(2), 4, n, 1), lod);
            road(v, 4, n, 1, 2, 2, 2, true, lod);
        }
    }

    // The road between the curbs at levels 2 and 3, in two halves (the
    // texture mirrored about the line between them): left `l` to `lm`, and
    // `rm` to right `r`; a road strip's halves meet at the midpoint of its
    // curbs (`split`).
    void road(std::span<const std::uint16_t> v, int stride, int n, int l, int lm, int rm, int r, bool split,
              int lod) {
        const auto s = sdlArcMap(m_p, v.subspan(static_cast<std::size_t>(l)), stride, n, 1);
        auto point = [&](int i, int column) {
            return vert(at(v, static_cast<std::size_t>(i) * static_cast<std::size_t>(stride) +
                                  static_cast<std::size_t>(column)));
        };
        begin(tex(0));
        for (int i = 0; i < n; ++i) {
            const Vec3 a = point(i, l);
            const Vec3 b = split ? midpoint(a, point(i, r)) : point(i, lm);
            vertex(a, s[static_cast<std::size_t>(i)], 1.0f);
            vertex(b, s[static_cast<std::size_t>(i)], 0.0f);
        }
        endStrip(lodBit(lod));
        begin(tex(0));
        for (int i = 0; i < n; ++i) {
            const Vec3 b = point(i, r);
            const Vec3 a = split ? midpoint(point(i, l), b) : point(i, rm);
            vertex(a, s[static_cast<std::size_t>(i)], 0.0f);
            vertex(b, s[static_cast<std::size_t>(i)], 1.0f);
        }
        endStrip(lodBit(lod));
    }

    void sidewalkStrip(const PsdlAttribute& a) {
        // Pairs: curb (road level), outer edge.
        const auto v = a.vertices();
        const int n = static_cast<int>(v.size() / 2);
        if (n <= 0)
            return;
        auto uv = [](const Vec3& p, float fx, float fz) {
            return Vec2{p.x * kSidewalkRepeat - fx, p.z * kSidewalkRepeat - fz};
        };
        // A first pair (0, 0) or (1, 1) of a two-pair strip marks a curb end
        // cap: a half-bright triangle closing the raised curb at level 3.
        const bool cap = n == 2 && v[0] == v[1] && v[0] < 2;
        if (cap) {
            const Vec3 c = vert(v[2]), o = vert(v[3]);
            const float fx = std::floor(c.x * kSidewalkRepeat), fz = std::floor(c.z * kSidewalkRepeat);
            const Vec2 cu = uv(c, fx, fz), ou = uv(o, fx, fz);
            begin(tex(1), SdlShade::HalfRoom);
            if (v[0] == 0) {
                vertex(raised(c, kCurbRise), cu.x, cu.y);
                vertex(o, ou.x, ou.y);
                vertex(c, cu.x, cu.y);
            } else {
                vertex(c, cu.x, cu.y);
                vertex(o, ou.x, ou.y);
                vertex(raised(c, kCurbRise), cu.x, cu.y);
            }
            endList(lodBit(3));
            return;
        }
        const Vec3 c0 = vert(v[0]);
        const float fx = std::floor(c0.x * kSidewalkRepeat), fz = std::floor(c0.z * kSidewalkRepeat);
        // Level 3: the curb face, then the sidewalk from the raised curb.
        begin(tex(1), SdlShade::HalfRoom);
        for (int i = 0; i < n; ++i) {
            const Vec3 c = vert(at(v, 2 * static_cast<std::size_t>(i)));
            const Vec2 cu = uv(c, fx, fz);
            vertex(c, cu.x, cu.y);
            vertex(raised(c, kCurbRise), cu.x, cu.y);
        }
        endStrip(lodBit(3));
        begin(tex(1));
        for (int i = 0; i < n; ++i) {
            const Vec3 c = vert(at(v, 2 * static_cast<std::size_t>(i)));
            const Vec3 o = vert(at(v, 2 * static_cast<std::size_t>(i) + 1));
            const Vec2 cu = uv(c, fx, fz), ou = uv(o, fx, fz);
            vertex(raised(c, kCurbRise), cu.x, cu.y);
            vertex(o, ou.x, ou.y);
        }
        endStrip(lodBit(3));
        // Levels 0 to 2: flat.
        begin(tex(1));
        for (int i = 0; i < n; ++i) {
            const Vec3 c = vert(at(v, 2 * static_cast<std::size_t>(i)));
            const Vec3 o = vert(at(v, 2 * static_cast<std::size_t>(i) + 1));
            const Vec2 cu = uv(c, fx, fz), ou = uv(o, fx, fz);
            vertex(c, cu.x, cu.y);
            vertex(o, ou.x, ou.y);
        }
        endStrip(lodBit(0) | lodBit(1) | lodBit(2));
    }

    void rectangleStrip(const PsdlAttribute& a) {
        const auto v = a.vertices();
        const int n = static_cast<int>(v.size() / 2);
        if (n <= 0)
            return;
        const auto s = sdlArcMap(m_p, v, 2, n, 1);
        begin(tex(0));
        for (int i = 0; i < n; ++i) {
            const auto k = 2 * static_cast<std::size_t>(i);
            vertex(vert(at(v, k)), s[static_cast<std::size_t>(i)], 0.0f);
            vertex(vert(at(v, k + 1)), s[static_cast<std::size_t>(i)], 1.0f);
        }
        endStrip(kAllLods);
    }

    void wallPrimitive(const Vec3& a, const Vec3& b) {
        m_prim.light = m_light;
        m_prim.wall = true;
        m_prim.wall0 = a;
        m_prim.wall1 = b;
    }

    void sliver(const PsdlAttribute& a) {
        if (a.args.size() < 4)
            return;
        const Vec3 p0 = vert(a.wallLeft()), p1 = vert(a.wallRight());
        const float top = height(a.sliverTop());
        const float density = height(a.sliverTextureScale());
        const float u = std::floor(flatDist(p1, p0) * density + 0.5f);
        const float v0 = (p0.y - top) * density;
        const float v1 = (p1.y - top) * density;
        begin(tex(0), SdlShade::Wall);
        wallPrimitive(p0, p1);
        if (v0 == 0.0f) {
            if (v1 == 0.0f)
                return;
            vertex(p0, 0.0f, 0.0f);
            vertex(p1, u, v1);
            vertex({p1.x, top, p1.z}, u, 0.0f);
        } else if (v1 != 0.0f) {
            vertex(p0, 0.0f, v0);
            vertex(p1, u, v1);
            vertex({p1.x, top, p1.z}, u, 0.0f);
            vertex({p0.x, top, p0.z}, 0.0f, 0.0f);
        } else {
            vertex(p0, 0.0f, v0);
            vertex(p1, u, 0.0f);
            vertex({p0.x, top, p0.z}, 0.0f, 0.0f);
        }
        endFan(kAllLods);
    }

    void crosswalk(const PsdlAttribute& a) {
        const auto v = a.vertices();
        if (v.size() < 4)
            return;
        const Vec3 c0 = vert(v[0]), c1 = vert(v[1]), c2 = vert(v[2]), c3 = vert(v[3]);
        // The stripes repeat along the crossing by its length over its width.
        const float width = v[0] == v[1] ? dist(c2, c3) : dist(c0, c1);
        const float repeat = dist(c0, c2) / width;
        begin(tex(2));
        m_prim.belowCamera = true;
        m_prim.height = c0.y;
        vertex(c1, 0.0f, 0.0f);
        vertex(c0, 1.0f, 0.0f);
        vertex(c3, 0.0f, repeat);
        vertex(c2, 1.0f, repeat);
        endStrip(kAllLods);
    }

    void fan(const PsdlAttribute& a, bool road) {
        const auto v = a.vertices();
        if (v.size() < 3)
            return;
        const Vec3 first = vert(v[0]);
        const float fx = std::floor(first.x * kFanRepeat), fz = std::floor(first.z * kFanRepeat);
        begin(tex(0));
        if (road) {
            m_prim.belowCamera = true;
            m_prim.height = first.y;
        }
        for (const auto i : v) {
            const Vec3 p = vert(i);
            vertex(p, p.x * kFanRepeat - fx, p.z * kFanRepeat - fz);
        }
        endFan(kAllLods);
    }

    void roof(const PsdlAttribute& a) {
        const auto v = a.vertices();
        if (a.args.empty() || v.size() < 3)
            return;
        const float y = height(a.roofHeight());
        const Vec3 first = vert(v[0]);
        const float fx = std::floor(first.x * kFanRepeat), fz = std::floor(first.z * kFanRepeat);
        begin(tex(0));
        m_prim.belowCamera = true;
        m_prim.height = y;
        for (const auto i : v) {
            const Vec3 p = vert(i);
            vertex({p.x, y, p.z}, p.x * kFanRepeat - fx, p.z * kFanRepeat - fz);
        }
        endFan(kAllLods);
    }

    void facade(const PsdlAttribute& a) {
        if (a.args.size() < 6)
            return;
        const Vec3 p0 = vert(a.wallLeft()), p1 = vert(a.wallRight());
        const float bottom = height(a.facadeBottom()), top = height(a.facadeTop());
        // The repeats are read unsigned; v is 0 at the bottom.
        const auto u = static_cast<float>(a.args[2]);
        const auto vr = static_cast<float>(a.args[3]);
        begin(tex(0), SdlShade::Wall);
        wallPrimitive(p0, p1);
        vertex({p0.x, bottom, p0.z}, 0.0f, 0.0f);
        vertex({p1.x, bottom, p1.z}, u, 0.0f);
        vertex({p1.x, top, p1.z}, u, vr);
        vertex({p0.x, top, p0.z}, 0.0f, vr);
        endFan(kAllLods);
    }

    void dividedRoadStrip(const PsdlAttribute& a) {
        // Sections of six: outer left, curb left, median left, median right,
        // curb right, outer right.
        const auto v = a.vertices();
        const int n = static_cast<int>(v.size() / 6);
        if (n <= 0)
            return;
        lowDetailRoad(v, 6, n, 5);
        for (int lod = 2; lod <= 3; ++lod) {
            if (v[0] != v[1])
                sidewalkSide(v, 6, n, 0, 1, true, sdlArcMap(m_p, v, 6, n, 1), lod);
            if (v[4] != v[5])
                sidewalkSide(v, 6, n, 5, 4, false, sdlArcMap(m_p, v.subspan(4), 6, n, 1), lod);
            road(v, 6, n, 1, 2, 3, 4, false, lod);
        }
        divider(a, v, n);
    }

    // The median between the lanes (levels 2 and 3 only).
    void divider(const PsdlAttribute& a, std::span<const std::uint16_t> v, int n) {
        const std::uint8_t lods = lodBit(2) | lodBit(3);
        const int type = a.dividerFlags() & 0x3F;
        const bool capStart = a.dividerCapStart();
        const bool capEnd = a.dividerCapEnd();
        const int value = a.dividerTexture();
        const float h = a.dividerHeight(); // 8.8 fixed point
        const auto s = sdlArcMap(m_p, v.subspan(2), 6, n, 1);
        auto ml = [&](int i) { return vert(at(v, static_cast<std::size_t>(i) * 6 + 2)); };
        auto mr = [&](int i) { return vert(at(v, static_cast<std::size_t>(i) * 6 + 3)); };
        auto si = [&](int i) { return s[static_cast<std::size_t>(i)]; };
        const int last = n - 1;
        if (type == kDividerFlat) {
            begin(texture(value, 1));
            for (int i = 0; i < n; ++i) {
                vertex(ml(i), si(i), 0.0f);
                vertex(mr(i), si(i), h);
            }
            endStrip(lods);
        } else if (type == kDividerRaised) {
            // Two half-bright walls, two bevels and the top.
            begin(texture(value, 0), SdlShade::HalfRoom);
            for (int i = 0; i < n; ++i) {
                vertex(ml(i), si(i), 0.0f);
                vertex(raised(ml(i), h), si(i), h);
            }
            endStrip(lods);
            begin(texture(value, 0), SdlShade::HalfRoom);
            for (int i = 0; i < n; ++i) {
                vertex(raised(mr(i), h), si(i), 0.0f);
                vertex(mr(i), si(i), 1.0f);
            }
            endStrip(lods);
            // The bevels go in by the height from the top edges and up by it.
            begin(texture(value, 1));
            for (int i = 0; i < n; ++i) {
                const Vec3 d = normalizedOrZero(ml(i) - mr(i));
                const Vec3 p = ml(i);
                vertex(raised(p, h), si(i), 0.0f);
                vertex({-h * d.x + p.x, d.y * -h + h + p.y, d.z * -h + p.z}, si(i), 1.0f);
            }
            endStrip(lods);
            begin(texture(value, 1));
            for (int i = 0; i < n; ++i) {
                const Vec3 d = normalizedOrZero(ml(i) - mr(i));
                const Vec3 p = mr(i);
                vertex({h * d.x + p.x, d.y * h + h + p.y, d.z * h + p.z}, si(i), 1.0f);
                vertex(raised(p, h), si(i), 0.0f);
            }
            endStrip(lods);
            begin(texture(value, 2));
            for (int i = 0; i < n; ++i) {
                const Vec3 d = normalizedOrZero(ml(i) - mr(i));
                const float x = -h * d.x, y = h + -h * d.y, z = -h * d.z;
                const Vec3 l = ml(i), r = mr(i);
                vertex({x + l.x, y + l.y, z + l.z}, si(i), 0.0f);
                vertex({-x + r.x, y + r.y, -z + r.z}, si(i), 1.0f);
            }
            endStrip(lods);
            if (capStart) {
                begin(texture(value, 3));
                vertex(ml(0), 0.0f, 0.0f);
                vertex(mr(0), 1.0f, 0.0f);
                vertex(raised(ml(0), h), 1.0f, 1.0f);
                vertex(raised(mr(0), h), 0.0f, 1.0f);
                endStrip(lods);
            }
            if (capEnd) {
                // Drawn with the texture still bound: the start cap's, else the top's.
                begin(texture(value, capStart ? 3 : 2));
                vertex(mr(last), 0.0f, 0.0f);
                vertex(ml(last), 1.0f, 0.0f);
                vertex(raised(mr(last), h), 1.0f, 1.0f);
                vertex(raised(ml(last), h), 0.0f, 1.0f);
                endStrip(lods);
            }
        } else if (type == kDividerWedged) {
            // Sides slanting in to a narrower top a fixed height up.
            auto inset = [](const Vec3& from, const Vec3& to, float k) {
                Vec3 d = normalizedOrZero(from - to);
                d = {d.x * k, d.y * k, d.z * k};
                d.y = d.y + kWedgeHeight;
                return d;
            };
            auto add = [](const Vec3& p, const Vec3& d) { return Vec3{p.x + d.x, p.y + d.y, p.z + d.z}; };
            auto mirrored = [](const Vec3& d) { return Vec3{-d.x, d.y, -d.z}; };
            begin(texture(value, 1));
            for (int i = 0; i < n; ++i) {
                vertex(ml(i), si(i), 0.0f);
                vertex(add(ml(i), inset(ml(i), mr(i), -kWedgeInset)), si(i), 1.0f);
            }
            endStrip(lods);
            begin(texture(value, 1));
            for (int i = 0; i < n; ++i) {
                vertex(add(mr(i), inset(ml(i), mr(i), kWedgeInset)), si(i), 1.0f);
                vertex(mr(i), si(i), 0.0f);
            }
            endStrip(lods);
            begin(texture(value, 2));
            for (int i = 0; i < n; ++i) {
                const Vec3 d = inset(ml(i), mr(i), -kWedgeInset);
                vertex(add(ml(i), d), si(i), 0.0f);
                vertex(add(mr(i), mirrored(d)), si(i), 1.0f);
            }
            endStrip(lods);
            if (capStart) {
                const Vec3 d = inset(ml(0), mr(0), -kWedgeInset);
                begin(texture(value, 3));
                vertex(ml(0), 0.0f, 0.0f);
                vertex(mr(0), 1.0f, 0.0f);
                vertex(add(ml(0), d), 1.0f, 1.0f);
                vertex(add(mr(0), mirrored(d)), 0.0f, 1.0f);
                endStrip(lods);
            }
            if (capEnd) {
                const Vec3 d = inset(mr(last), ml(last), -kWedgeInset);
                begin(texture(value, capStart ? 3 : 2));
                vertex(mr(last), 0.0f, 0.0f);
                vertex(ml(last), 1.0f, 0.0f);
                vertex(add(mr(last), d), 1.0f, 1.0f);
                vertex(add(ml(last), mirrored(d)), 0.0f, 1.0f);
                endStrip(lods);
            }
        }
        // Any other type: "Bad Median Type", nothing drawn.
    }

    const Psdl& m_p;
    std::size_t m_room;
    SdlRoomDraw& m_out;
    SdlPrimitive m_prim;
    std::uint32_t m_first = 0;
    int m_value = 0;          // the Texture attribute's value (0 before any)
    bool m_skip = false;      // the value is 0 ("none")
    std::uint8_t m_light = 0; // the last FacadeBound's light table entry
};

} // namespace

std::vector<float> sdlArcMap(const Psdl& psdl, std::span<const std::uint16_t> indices, int stride, int count,
                             int column) {
    std::vector<float> s(static_cast<std::size_t>(count > 0 ? count : 1), 0.0f);
    auto vert = [&](std::size_t k) {
        const std::uint16_t i = k < indices.size() ? indices[k] : 0;
        return i < psdl.vertices.size() ? psdl.vertices[i] : Vec3{};
    };
    const auto st = static_cast<std::size_t>(stride);
    float width = 0.0f, length = 0.0f, longest = 1.0f;
    for (int i = 1; i < count; ++i) {
        const auto prev = static_cast<std::size_t>(i - 1) * st;
        const Vec3 a = vert(prev), c = vert(prev + static_cast<std::size_t>(column));
        const Vec3 b = vert(static_cast<std::size_t>(i) * st);
        const float dx = a.x - c.x, dy = a.y - c.y;
        width = std::sqrt(dx * dx + dy * dy + (a.z - c.z) * (a.z - c.z)) + width;
        const float seg =
            std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
        s[static_cast<std::size_t>(i)] = seg;
        length = seg + length;
        if (longest <= seg)
            longest = seg;
    }
    if (count > 1 && width != 0.0f && 0.1 <= static_cast<double>(length)) {
        float repeats = std::floor(length / (width / static_cast<float>(count)) + 0.5f);
        if (repeats < 1.0f)
            repeats = 1.0f;
        float scale = (1.0f / length) * repeats;
        if (128.0f < scale * longest)
            scale = 127.0f / longest;
        float run = 0.0f;
        for (int i = 1; i < count; ++i) {
            auto& v = s[static_cast<std::size_t>(i)];
            if (0.0f < run)
                run = run - v;
            else
                run = run + v;
            v = run * scale;
        }
        return s;
    }
    std::fill(s.begin(), s.end(), 0.0f);
    return s;
}

Vec3 sdlRoomCentroid(const Psdl& psdl, std::size_t room) {
    if (room >= psdl.rooms.size() || psdl.rooms[room].perimeter.empty())
        return {};
    const auto& per = psdl.rooms[room].perimeter;
    auto vert = [&](std::uint16_t i) { return i < psdl.vertices.size() ? psdl.vertices[i] : Vec3{}; };
    const std::size_t n = per.size();
    float area = 0.0f, cx = 0.0f, sy = 0.0f, cz = 0.0f;
    int count = 0;
    std::size_t i = 0;
    std::uint16_t prev = per[n - 1].vertex;
    do {
        // A corner repeating the one before it is skipped.
        if (per[i].vertex == prev) {
            if (i == n - 1)
                break;
            ++i;
        }
        ++count;
        const std::uint16_t cur = per[i].vertex;
        ++i;
        const Vec3 p = vert(cur), q = vert(prev);
        const float cross = p.z * q.x - p.x * q.z;
        area = cross + area;
        cx = (p.x + q.x) * cross + cx;
        sy = sy + q.y;
        cz = (p.z + q.z) * cross + cz;
        prev = cur;
    } while (i < n);
    if (area != 0.0f) {
        const float f = 1.0f / (area * 3.0f);
        return {f * cx, sy / static_cast<float>(count), f * cz};
    }
    return vert(per[0].vertex);
}

void sdlRoomBoundSphere(const Psdl& psdl, std::size_t room, Vec3& centre, float& radius) {
    centre = sdlRoomCentroid(psdl, room);
    float r2 = 0.0f;
    if (room < psdl.rooms.size()) {
        for (const auto& pt : psdl.rooms[room].perimeter) {
            const Vec3 p = pt.vertex < psdl.vertices.size() ? psdl.vertices[pt.vertex] : Vec3{};
            const float dx = p.x - centre.x, dy = p.y - centre.y, dz = p.z - centre.z;
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (r2 < d2)
                r2 = d2;
        }
    }
    radius = std::sqrt(r2);
}

int sdlRoomLod(float distance) {
    if (kVLowThresh < distance)
        return 0;
    if (kLowThresh < distance)
        return 1;
    if (kMedThresh < distance)
        return 2;
    return 3;
}

bool sdlBackface(const Vec3& eye, const Vec3& a, const Vec3& b) {
    return (eye.z - a.z) * (b.x - a.x) + (eye.x - a.x) * (a.z - b.z) < 0.0f;
}

SdlRoomDraw buildSdlRoomDraw(const Psdl& psdl, std::size_t room) {
    SdlRoomDraw out;
    if (room == 0 || room >= psdl.rooms.size())
        return out;
    Builder(psdl, room, out).build();
    return out;
}

} // namespace mm2::city
