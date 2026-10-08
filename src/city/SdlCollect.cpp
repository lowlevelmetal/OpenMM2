#include "city/SdlCollect.h"

#include <cmath>
#include <string>
#include <unordered_map>

// Port of sdlPage16::Collect and the sdlPoly setters it uses (MM2 build 3393,
// documentation from the MM2Recomp disassembly). Operation order and the 32-bit
// float math follow the original, which runs the x87 in single precision. Where
// the original's x87 compares treat NaN specially, the comparisons below are
// written to give the same result (NaN only arises from degenerate geometry,
// e.g. a tunnel section whose two vertices coincide).

namespace mm2::city {
namespace {

// sdlPage16::Collect: curbs and sidewalks stand this high above their road-level vertex.
constexpr float kCurbRise = 0.15f;
// 8.8 fixed point (divider and tunnel heights).
constexpr float kFixed88 = 0.00390625f;
// Tunnel walls are at least this tall.
constexpr float kMinTunnelWallHeight = 3.0f;
// Tunnel railings stand out from the road edge by height1 times this.
constexpr float kRailingOffsetPerHeight = 0.333f;
// Sloped tunnel sides (flag 0x2000) rise to this fraction of the wall height.
constexpr float kSlopeRiseFraction = 0.25f;
// sdlPoly::InitNoArea rejects polygons whose normalized normal is shorter.
constexpr float kMinNormalMag2 = 0.9f;
// sdlPage16::FindBoundingIsoParams scales the sphere radius by a global
// (1.0, and the global enabling it is 1, in the retail executable).
constexpr float kIsoParamRadiusScale = 1.0f;
// Collect skips fans whose material index is 2. With the retail materials.mtl
// that is "deepwater" (the Thames and the bay), so cars fall into deep water.
// The meaning is inferred from the data; the code just compares with 2.
constexpr std::uint8_t kSkippedFanMaterial = 2;

// Tunnel flag bits as Collect reads them (names inferred from their use).
namespace TunnelFlag {
constexpr std::uint16_t Left = 0x0001;
constexpr std::uint16_t Right = 0x0002;
constexpr std::uint16_t Railing = 0x0004;
constexpr std::uint16_t LeftRailingStartCap = 0x0010;
constexpr std::uint16_t LeftRailingEndCap = 0x0020;
constexpr std::uint16_t RightRailingStartCap = 0x0040;
constexpr std::uint16_t RightRailingEndCap = 0x0080;
constexpr std::uint16_t LeftSlopeStartCap = 0x0200;
constexpr std::uint16_t LeftSlopeEndCap = 0x0400;
constexpr std::uint16_t RightSlopeStartCap = 0x0800;
constexpr std::uint16_t RightSlopeEndCap = 0x1000;
constexpr std::uint16_t Sloped = 0x2000;
} // namespace TunnelFlag

// Tunnel attributes: junction walls along the room perimeter, or walls for the
// strip that follows.
constexpr int kJunctionTunnelWords = 10;
constexpr int kStripTunnelWords = 3;

// x87 "not equal": an fcomp that leaves C3 clear. Unordered compares as equal.
bool differs(float a, float b) {
    return a < b || a > b;
}

// Squared distance in the xz plane.
float distXZ2(const Vec3& a, const Vec3& b) {
    const float dz = a.z - b.z;
    const float dx = a.x - b.x;
    return dz * dz + dx * dx;
}

Vec3 raised(Vec3 p, float rise) {
    p.y = p.y + rise;
    return p;
}

int countOf(const PsdlAttribute& a) {
    // Collect: the subtype, or the word after the header when the subtype is 0.
    return a.subtype != 0 ? a.subtype : (a.args.empty() ? 0 : a.args[0]);
}

// The attribute's words after the count word (the header word itself excluded).
std::span<const std::uint16_t> countedWords(const PsdlAttribute& a) {
    std::span<const std::uint16_t> s(a.args);
    if (a.subtype == 0 && a.type != PsdlAttrType::Texture && !s.empty())
        s = s.subspan(1);
    return s;
}

class Collector {
public:
    Collector(const Psdl& psdl, const PsdlRoom& room, const SdlSphere* sphere,
              std::span<const std::uint8_t> materials, SdlPolyBuffer& out, int capacity, bool probeSpecial)
        : m_psdl(psdl), m_room(room), m_sphere(sphere), m_materials(materials), m_out(out),
          m_capacity(capacity), m_special(probeSpecial),
          m_radius2(sphere ? sphere->radius * sphere->radius : 0.0f) {}

    int added() const { return m_added; }
    int texture() const { return m_texture; }
    void setTexture(int value) {
        m_texture = value;
        m_noTexture = value == 0;
    }
    void resumeTexture(int value) { m_texture = value; }

    // One attribute; false when the capacity ran out (Collect returns at once).
    bool attribute(std::size_t index);

private:
    // --- data access (bounds-checked; MM2 reads whatever is there) ---
    Vec3 vertex(std::uint32_t i) const { return i < m_psdl.vertices.size() ? m_psdl.vertices[i] : Vec3{}; }
    Vec3 point(std::uint32_t i) const { return i < m_out.vertices.size() ? m_out.vertices[i] : Vec3{}; }
    float height(std::uint16_t i) const { return i < m_psdl.heights.size() ? m_psdl.heights[i] : 0.0f; }
    static std::uint16_t at(std::span<const std::uint16_t> list, int i) {
        return i >= 0 && static_cast<std::size_t>(i) < list.size() ? list[i] : 0;
    }
    // The page's texture -> material table at texture + offset.
    std::uint8_t materialAt(int index) const {
        return index >= 0 && static_cast<std::size_t>(index) < m_materials.size() ? m_materials[index] : 0;
    }
    std::uint8_t material(int offset = 0) const { return materialAt(m_texture + offset); }

    // Collect decrements its capacity before every polygon it tries.
    bool take() { return --m_capacity >= 0; }

    // --- sphere culling (inlined throughout Collect) ---
    // Before a quad or wall: keep it when the sphere centre is closer to `p`
    // than sqrt(2 (|pq|^2 + r^2)) in the xz plane.
    bool nearPair(const Vec3& p, const Vec3& q) const {
        if (!m_sphere)
            return true;
        const float reach = distXZ2(p, q) + m_radius2;
        return distXZ2(m_sphere->center, p) < reach + reach;
    }
    // Before a triangle: the same with its longest edge, measured from c when
    // |ab| <= |bc| and from a otherwise.
    bool nearTriangle(const Vec3& a, const Vec3& b, const Vec3& c) const {
        if (!m_sphere)
            return true;
        const float ab = distXZ2(a, b);
        const float bc = distXZ2(b, c);
        const float ca = distXZ2(c, a);
        float longest;
        const Vec3* from;
        if (ab > bc) {
            longest = ab > ca ? ab : ca;
            from = &a;
        } else {
            longest = ca > bc ? ca : bc;
            from = &c;
        }
        const float reach = longest + m_radius2;
        return distXZ2(m_sphere->center, *from) < reach + reach;
    }
    // Before a flat fan or roof at height y: the sphere spans y.
    bool spansHeight(float y) const {
        if (!m_sphere)
            return true;
        const float below = (m_sphere->center.y - m_sphere->radius) - y;
        const float above = (m_sphere->center.y + m_sphere->radius) - y;
        return !(below * above >= 0.0f); // x87: "less than 0" includes unordered
    }

    void findBoundingIsoParams(std::span<const std::uint16_t> list, int stride, int count, int& lo,
                               int& hi) const;
    float isoSide(std::span<const std::uint16_t> list, int p, int q) const;

    // --- sdlPoly ---
    std::uint32_t push(const Vec3& p);
    bool initNoArea(std::uint8_t mat, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d);
    bool setQuad(std::uint8_t mat, std::uint32_t v0, float h0, std::uint32_t v1, float h1, std::uint32_t v2,
                 float h2, std::uint32_t v3, float h3);
    bool setFlatQuad(std::uint8_t mat, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d,
                     float y);
    bool setQuad(std::uint8_t mat, Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3);
    bool setTri(std::uint8_t mat, Vec3 p0, Vec3 p1, Vec3 p2);
    bool setFlatTri(std::uint8_t mat, std::uint32_t a, std::uint32_t b, std::uint32_t c, float y);
    bool setWall(std::uint8_t mat, std::uint32_t a, std::uint32_t b, float topA, float topB);
    bool setWall(std::uint8_t mat, Vec3 a, Vec3 b, float topA, float topB);

    // --- shared pieces of the strip attributes ---
    bool leftSidewalk(std::span<const std::uint16_t> list, int i, int stride);
    bool rightSidewalk(std::span<const std::uint16_t> list, int i, int stride, int curb);
    bool roadSurface(std::span<const std::uint16_t> list, int i, int stride, int left);
    bool dividerMedian(std::span<const std::uint16_t> list, int i, std::uint8_t type, int dividerTexture,
                       float dividerHeight);

    // --- attributes ---
    bool roadStrip(const PsdlAttribute& a);
    bool sidewalkStrip(const PsdlAttribute& a);
    bool rectangleStrip(const PsdlAttribute& a);
    bool crosswalk(const PsdlAttribute& a);
    bool fan(const PsdlAttribute& a, bool road);
    bool facadeBound(const PsdlAttribute& a);
    bool dividedRoadStrip(const PsdlAttribute& a);
    bool tunnel(std::size_t index, const PsdlAttribute& a);
    bool tunnelStrip(std::uint16_t flags, float height1, float wallHeight,
                     std::span<const std::uint16_t> list, int stride, int sections);
    bool roof(const PsdlAttribute& a);

    const Psdl& m_psdl;
    const PsdlRoom& m_room;
    const SdlSphere* m_sphere;
    std::span<const std::uint8_t> m_materials;
    SdlPolyBuffer& m_out;
    int m_capacity;
    bool m_special;
    float m_radius2;
    int m_added = 0;
    int m_texture = 0;
    bool m_noTexture = false;
    // The stride of the strip a tunnel describes. Collect only sets it for road,
    // rectangle and divided strips and otherwise keeps the previous tunnel's
    // (uninitialized at entry); 0 here means "never set".
    int m_tunnelStride = 0;
};

// ---------------------------------------------------------------------------
// sdlPage16::FindBoundingIsoParams
// ---------------------------------------------------------------------------

// Signed xz distance of the sphere centre from the line through list[p] and
// list[q] (the strip's cross line at a section).
float Collector::isoSide(std::span<const std::uint16_t> list, int p, int q) const {
    const Vec3 a = vertex(at(list, p));
    const Vec3 b = vertex(at(list, q));
    const float dx = b.x - a.x;
    const float dz = b.z - a.z;
    const float len2 = dz * dz + dx * dx;
    const float inv = differs(len2, 0.0f) ? 1.0f / std::sqrt(len2) : 0.0f;
    return ((a.z - m_sphere->center.z) * dx - (a.x - m_sphere->center.x) * dz) * inv;
}

// Narrows a strip of `count` sections (`stride` list entries each) to the list
// offsets [lo, hi) whose quads can touch the sphere: a binary search for a
// section whose cross line (list[i + a] to list[i + b + stride - 1], a and b
// being lo's and hi's values on entry) passes within the radius, then widening
// to the first sections beyond the radius on either side. Without a sphere, or
// with fewer than 3 sections, every section: [0, (count - 1) * stride).
void Collector::findBoundingIsoParams(std::span<const std::uint16_t> list, int stride, int count, int& lo,
                                      int& hi) const {
    const int a = lo;
    const int b = hi;
    lo = 0;
    hi = (count - 1) * stride;
    if (count < 3 || !m_sphere)
        return;
    const float reach = kIsoParamRadiusScale * m_sphere->radius;
    const float negReach = -reach;
    const auto side = [&](int i) { return isoSide(list, i + a, i + b + stride - 1); };

    int mid;
    for (;;) {
        mid = (count >> 1) * stride + lo;
        const float s = side(mid);
        if (s > negReach) {
            lo = mid;
            if (s < reach)
                break;
            count = (count + 1) >> 1;
        } else {
            hi = mid;
            count = (count >> 1) + 1;
        }
        if (count < 3)
            return;
    }
    if (mid > 0) {
        for (;;) {
            if (side(lo) >= reach)
                break;
            lo -= stride;
            if (lo <= 0)
                break;
        }
    }
    const int end = hi;
    int i = mid;
    while (i < end) {
        if (!(side(i) > negReach))
            break;
        i += stride;
    }
    hi = i;
}

// ---------------------------------------------------------------------------
// sdlPoly
// ---------------------------------------------------------------------------

std::uint32_t Collector::push(const Vec3& p) {
    m_out.vertices.push_back(p);
    --m_out.vertexBudget;
    return static_cast<std::uint32_t>(m_out.vertices.size() - 1);
}

// sdlPoly::InitNoArea: the polygon (a, b, c, d), d == 0 for a triangle, with
// the normal of a, b, c. Rejects (false, nothing added) a polygon whose
// normalized normal has |n|^2 < 0.9, i.e. a degenerate one.
bool Collector::initNoArea(std::uint8_t mat, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                           std::uint32_t d) {
    const Vec3 pa = point(a);
    const Vec3 pb = point(b);
    const Vec3 pc = point(c);
    SdlPoly poly;
    poly.v = {a, b, c, d};
    if (!differs(pa.y, pb.y) && !differs(pb.y, pc.y)) {
        poly.normal = Vec3(0.0f, 1.0f, 0.0f);
    } else {
        const Vec3 n = (pc - pb).cross(pa - pb);
        const float mag2 = n.y * n.y + n.x * n.x + n.z * n.z;
        const float inv = differs(mag2, 0.0f) ? 1.0f / std::sqrt(mag2) : 0.0f;
        poly.normal = Vec3(n.x * inv, n.y * inv, inv * n.z);
        const Vec3& u = poly.normal;
        if (!(u.z * u.z + u.y * u.y + u.x * u.x >= kMinNormalMag2))
            return false;
    }
    poly.material = mat;
    m_out.polys.push_back(poly);
    ++m_added;
    return true;
}

// sdlPoly::SetQuad (index version): the strip quad v0 v1 / v2 v3 (v2, v3 the
// next section), each corner raised by its h when h != 0 (as a new vertex).
// Collapses to a triangle when v1 == v3 or v0 == v2. Winding v0 v1 v3 v2.
bool Collector::setQuad(std::uint8_t mat, std::uint32_t v0, float h0, std::uint32_t v1, float h1,
                        std::uint32_t v2, float h2, std::uint32_t v3, float h3) {
    if (v1 == v3) {
        h3 = h2;
        v3 = v2;
        v2 = 0;
        h2 = 0.0f;
    } else if (v0 == v2) {
        v2 = 0;
        h2 = 0.0f;
    }
    if (differs(h0, 0.0f))
        v0 = push(raised(point(v0), h0));
    if (differs(h1, 0.0f))
        v1 = push(raised(point(v1), h1));
    if (differs(h2, 0.0f))
        v2 = push(raised(point(v2), h2));
    if (differs(h3, 0.0f))
        v3 = push(raised(point(v3), h3));
    return initNoArea(mat, v0, v1, v3, v2);
}

// sdlPoly::SetFlatQuad: like SetQuad with every corner at absolute height y
// (a new vertex for each corner not already there).
bool Collector::setFlatQuad(std::uint8_t mat, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                            std::uint32_t d, float y) {
    const auto flat = [&](std::uint32_t v) {
        Vec3 p = point(v);
        if (!differs(p.y, y))
            return v;
        p.y = y;
        return push(p);
    };
    a = flat(a);
    b = flat(b);
    c = flat(c);
    d = flat(d);
    return initNoArea(mat, a, b, d, c);
}

// sdlPoly::SetQuad (point version): four new vertices, winding p0 p1 p3 p2.
bool Collector::setQuad(std::uint8_t mat, Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3) {
    const std::uint32_t i0 = push(p0);
    const std::uint32_t i1 = push(p1);
    const std::uint32_t i2 = push(p2);
    const std::uint32_t i3 = push(p3);
    return initNoArea(mat, i0, i1, i3, i2);
}

// sdlPoly::SetTri: three new vertices.
bool Collector::setTri(std::uint8_t mat, Vec3 p0, Vec3 p1, Vec3 p2) {
    const std::uint32_t i0 = push(p0);
    const std::uint32_t i1 = push(p1);
    const std::uint32_t i2 = push(p2);
    return initNoArea(mat, i0, i1, i2, 0);
}

// sdlPoly::SetFlatTri: the triangle at absolute height y.
bool Collector::setFlatTri(std::uint8_t mat, std::uint32_t a, std::uint32_t b, std::uint32_t c, float y) {
    const auto flat = [&](std::uint32_t v) {
        Vec3 p = point(v);
        if (!differs(p.y, y))
            return v;
        p.y = y;
        return push(p);
    };
    a = flat(a);
    b = flat(b);
    c = flat(c);
    return initNoArea(mat, a, b, c, 0);
}

// sdlPoly::SetWall (index version): from a to b, up to absolute heights topA
// and topB. Winding a b b' a'.
bool Collector::setWall(std::uint8_t mat, std::uint32_t a, std::uint32_t b, float topA, float topB) {
    Vec3 pa = point(a);
    pa.y = topA;
    const std::uint32_t aTop = push(pa);
    Vec3 pb = point(b);
    pb.y = topB;
    const std::uint32_t bTop = push(pb);
    return initNoArea(mat, a, b, bTop, aTop);
}

// sdlPoly::SetWall (point version): the same with four new vertices.
bool Collector::setWall(std::uint8_t mat, Vec3 a, Vec3 b, float topA, float topB) {
    const std::uint32_t aBottom = push(a);
    a.y = topA;
    const std::uint32_t aTop = push(a);
    const std::uint32_t bBottom = push(b);
    b.y = topB;
    const std::uint32_t bTop = push(b);
    return initNoArea(mat, aBottom, bBottom, bTop, aTop);
}

// ---------------------------------------------------------------------------
// Strip pieces
// ---------------------------------------------------------------------------

// The left sidewalk of a road strip section: outer edge list[i], curb list[i+1]
// (road level), next section at i + stride. Sidewalk texture (+1).
bool Collector::leftSidewalk(std::span<const std::uint16_t> list, int i, int stride) {
    const std::uint16_t outer0 = at(list, i);
    const std::uint16_t curb0 = at(list, i + 1);
    if (outer0 == curb0)
        return true;
    const std::uint16_t outer1 = at(list, i + stride);
    const std::uint16_t curb1 = at(list, i + stride + 1);
    if (m_special) {
        const Vec3 a = vertex(outer0);
        const Vec3 b = raised(vertex(curb0), kCurbRise);
        const Vec3 c = vertex(outer1);
        const Vec3 d = raised(vertex(curb1), kCurbRise);
        if (nearTriangle(a, b, c)) {
            if (!take())
                return false;
            setTri(material(1), a, b, c);
        }
        if (nearTriangle(b, d, c)) {
            if (!take())
                return false;
            setTri(material(1), b, d, c);
        }
    } else if (nearPair(vertex(outer0), vertex(curb1))) {
        if (!take())
            return false;
        setQuad(material(1), outer0, 0.0f, curb0, kCurbRise, outer1, 0.0f, curb1, kCurbRise);
    }
    // The curb face.
    if (nearPair(vertex(curb0), vertex(curb1))) {
        if (!take())
            return false;
        setWall(material(1), curb0, curb1, vertex(curb0).y + kCurbRise, vertex(curb1).y + kCurbRise);
    }
    return true;
}

// The right sidewalk: curb list[i + curb] (road level), outer edge after it.
bool Collector::rightSidewalk(std::span<const std::uint16_t> list, int i, int stride, int curb) {
    const std::uint16_t curb0 = at(list, i + curb);
    const std::uint16_t outer0 = at(list, i + curb + 1);
    if (curb0 == outer0)
        return true;
    const std::uint16_t curb1 = at(list, i + stride + curb);
    const std::uint16_t outer1 = at(list, i + stride + curb + 1);
    if (m_special) {
        const Vec3 a = raised(vertex(curb0), kCurbRise);
        const Vec3 b = vertex(outer0);
        const Vec3 c = raised(vertex(curb1), kCurbRise);
        const Vec3 d = vertex(outer1);
        if (nearTriangle(a, b, c)) {
            if (!take())
                return false;
            setTri(material(1), a, b, c);
        }
        if (nearTriangle(b, d, c)) {
            if (!take())
                return false;
            setTri(material(1), b, d, c);
        }
    } else if (nearPair(vertex(curb0), vertex(outer1))) {
        if (!take())
            return false;
        setQuad(material(1), curb0, kCurbRise, outer0, 0.0f, curb1, kCurbRise, outer1, 0.0f);
    }
    if (nearPair(vertex(curb1), vertex(curb0))) {
        if (!take())
            return false;
        setWall(material(1), curb1, curb0, vertex(curb1).y + kCurbRise, vertex(curb0).y + kCurbRise);
    }
    return true;
}

// A road-level surface quad: list[i + left], list[i + left + 1] and the same
// in the next section. Road texture (+0); in a probed SpecialBound room two
// triangles with the sidewalk texture (+1).
bool Collector::roadSurface(std::span<const std::uint16_t> list, int i, int stride, int left) {
    const std::uint16_t a = at(list, i + left);
    const std::uint16_t b = at(list, i + left + 1);
    const std::uint16_t c = at(list, i + stride + left);
    const std::uint16_t d = at(list, i + stride + left + 1);
    if (m_special) {
        const Vec3 pa = vertex(a), pb = vertex(b), pc = vertex(c), pd = vertex(d);
        if (nearTriangle(pa, pb, pc)) {
            if (!take())
                return false;
            setTri(material(1), pa, pb, pc);
        }
        if (nearTriangle(pb, pd, pc)) {
            if (!take())
                return false;
            setTri(material(1), pb, pd, pc);
        }
    } else if (nearPair(vertex(a), vertex(d))) {
        if (!take())
            return false;
        setQuad(material(0), a, 0.0f, b, 0.0f, c, 0.0f, d, 0.0f);
    }
    return true;
}

// A divided road's median between list[i+2] and list[i+3] (and the next
// section). Type 1 (flags & 0x3f): flat, at road level, divider texture + 1.
// Anything else: walls up `dividerHeight` on both sides (road texture + 1) and
// a top at that height (divider texture + 2). In a probed SpecialBound room the
// top is two road-level triangles with the road texture + 1.
bool Collector::dividerMedian(std::span<const std::uint16_t> list, int i, std::uint8_t type,
                              int dividerTexture, float dividerHeight) {
    const std::uint16_t m0 = at(list, i + 2);
    const std::uint16_t m1 = at(list, i + 3);
    const std::uint16_t n0 = at(list, i + 8);
    const std::uint16_t n1 = at(list, i + 9);
    if (type != 1) {
        if (nearPair(vertex(n0), vertex(m0))) {
            if (!take())
                return false;
            setWall(material(1), n0, m0, dividerHeight + vertex(n0).y, dividerHeight + vertex(m0).y);
        }
        if (nearPair(vertex(m1), vertex(n1))) {
            if (!take())
                return false;
            setWall(material(1), m1, n1, dividerHeight + vertex(m1).y, dividerHeight + vertex(n1).y);
        }
    }
    if (m_special) {
        const Vec3 a = vertex(m0), b = vertex(m1), c = vertex(n0), d = vertex(n1);
        if (nearTriangle(a, b, c)) {
            if (!take())
                return false;
            setTri(material(1), a, b, c);
        }
        if (nearTriangle(b, d, c)) {
            if (!take())
                return false;
            setTri(material(1), b, d, c);
        }
        return true;
    }
    if (nearPair(vertex(m0), vertex(n1))) {
        if (!take())
            return false;
        if (type == 1)
            setQuad(materialAt(dividerTexture + 1), m0, 0.0f, m1, 0.0f, n0, 0.0f, n1, 0.0f);
        else
            setQuad(materialAt(dividerTexture + 2), m0, dividerHeight, m1, dividerHeight, n0, dividerHeight,
                    n1, dividerHeight);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Attributes
// ---------------------------------------------------------------------------

// RoadStrip (4 per section: outer L, curb L, curb R, outer R). Three passes,
// each over the sections FindBoundingIsoParams keeps for its own cross line:
// left sidewalk and curb, road, right sidewalk and curb.
bool Collector::roadStrip(const PsdlAttribute& a) {
    if (m_noTexture)
        return true;
    const auto list = a.vertices();
    const int count = countOf(a);
    int lo = 0, hi = -2;
    findBoundingIsoParams(list, 4, count, lo, hi);
    for (int i = lo; i < hi; i += 4)
        if (!leftSidewalk(list, i, 4))
            return false;
    lo = 1;
    hi = -1;
    findBoundingIsoParams(list, 4, count, lo, hi);
    for (int i = lo; i < hi; i += 4)
        if (!roadSurface(list, i, 4, 1))
            return false;
    lo = 2;
    hi = 0;
    findBoundingIsoParams(list, 4, count, lo, hi);
    for (int i = lo; i < hi; i += 4)
        if (!rightSidewalk(list, i, 4, 2))
            return false;
    return true;
}

// SidewalkStrip: pairs (curb at road level, outer edge). A two-pair strip whose
// first pair is (0, 0) or (1, 1) is a curb end cap instead: one triangle at the
// second pair, facing one way or the other.
bool Collector::sidewalkStrip(const PsdlAttribute& a) {
    if (m_noTexture)
        return true;
    const auto list = a.vertices();
    const int count = countOf(a);
    if (count == 2 && at(list, 0) == at(list, 1) && at(list, 0) < 2) {
        const Vec3 curb = vertex(at(list, 2));
        const Vec3 outer = vertex(at(list, 3));
        const Vec3 top = raised(curb, kCurbRise);
        if (at(list, 0) == 0) {
            if (!nearTriangle(top, outer, curb))
                return true;
            if (!take())
                return false;
            setTri(material(1), top, outer, curb);
        } else {
            if (!nearTriangle(curb, outer, top))
                return true;
            if (!take())
                return false;
            setTri(material(1), curb, outer, top);
        }
        return true;
    }
    for (int i = 0; i + 1 < count; ++i) {
        const std::uint16_t curb0 = at(list, 2 * i);
        const std::uint16_t outer0 = at(list, 2 * i + 1);
        const std::uint16_t curb1 = at(list, 2 * i + 2);
        const std::uint16_t outer1 = at(list, 2 * i + 3);
        if (nearPair(vertex(curb0), vertex(outer1))) {
            if (!take())
                return false;
            setQuad(material(1), curb0, kCurbRise, outer0, 0.0f, curb1, kCurbRise, outer1, 0.0f);
        }
        if (nearPair(vertex(curb1), vertex(curb0))) {
            if (!take())
                return false;
            setWall(material(1), curb1, curb0, vertex(curb1).y + kCurbRise, vertex(curb0).y + kCurbRise);
        }
    }
    return true;
}

// RectangleStrip: pairs forming a quad strip, road texture.
bool Collector::rectangleStrip(const PsdlAttribute& a) {
    if (m_noTexture)
        return true;
    const auto list = a.vertices();
    int lo = 0, hi = 0;
    findBoundingIsoParams(list, 2, countOf(a), lo, hi);
    for (int i = lo; i < hi; i += 2)
        if (!roadSurface(list, i, 2, 0))
            return false;
    return true;
}

// Crosswalk: a flat quad at the height of its first vertex, crosswalk texture (+2).
bool Collector::crosswalk(const PsdlAttribute& a) {
    if (m_noTexture)
        return true;
    const auto list = a.vertices();
    if (!nearPair(vertex(at(list, 1)), vertex(at(list, 2))))
        return true;
    if (!take())
        return false;
    setFlatQuad(material(2), at(list, 1), at(list, 0), at(list, 3), at(list, 2), vertex(at(list, 0)).y);
    return true;
}

// RoadTriangleFan (flat at the height of its first vertex, culled by height
// first) and TriangleFan (the triangles as they are). Both skip material 2.
bool Collector::fan(const PsdlAttribute& a, bool road) {
    if (m_noTexture || material() == kSkippedFanMaterial)
        return true;
    const auto list = a.vertices();
    const int n = countOf(a) + 2;
    const std::uint16_t hub = at(list, 0);
    const float y = road ? vertex(hub).y : 0.0f;
    if (road && !spansHeight(y))
        return true;
    for (int i = 2; i < n; ++i) {
        const std::uint16_t b = at(list, i - 1);
        const std::uint16_t c = at(list, i);
        if (!nearTriangle(vertex(hub), vertex(b), vertex(c)))
            continue;
        if (!take())
            return false;
        if (road)
            setFlatTri(material(), hub, b, c, y);
        else
            initNoArea(material(), hub, b, c, 0);
    }
    return true;
}

// FacadeBound: a wall from the two vertices up to the top height. Drawn or not,
// it always collides (no texture check).
bool Collector::facadeBound(const PsdlAttribute& a) {
    const float top = height(a.facadeBoundTop());
    const std::uint16_t left = a.wallLeft();
    const std::uint16_t right = a.wallRight();
    if (!nearPair(vertex(left), vertex(right)))
        return true;
    if (!take())
        return false;
    setWall(material(), left, right, top, top);
    return true;
}

// DividedRoadStrip (6 per section: outer L, curb L, median L, median R, curb R,
// outer R): left sidewalk; then per section left road, median, right road;
// then right sidewalk.
bool Collector::dividedRoadStrip(const PsdlAttribute& a) {
    if (m_noTexture)
        return true;
    const auto words = countedWords(a);
    const std::uint16_t header = at(words, 0);
    const int dividerTexture = header >> 8;
    const auto type = static_cast<std::uint8_t>(header & 0x3f);
    const float dividerHeight = static_cast<float>(static_cast<int>(at(words, 1))) * kFixed88;
    const auto list = a.vertices();
    const int count = countOf(a);

    int lo = 0, hi = -4;
    findBoundingIsoParams(list, 6, count, lo, hi);
    for (int i = lo; i < hi; i += 6)
        if (!leftSidewalk(list, i, 6))
            return false;
    lo = 1;
    hi = -1;
    findBoundingIsoParams(list, 6, count, lo, hi);
    for (int i = lo; i < hi; i += 6) {
        if (!roadSurface(list, i, 6, 1) || !dividerMedian(list, i, type, dividerTexture, dividerHeight) ||
            !roadSurface(list, i, 6, 3))
            return false;
    }
    lo = 4;
    hi = 0;
    findBoundingIsoParams(list, 6, count, lo, hi);
    for (int i = lo; i < hi; i += 6)
        if (!rightSidewalk(list, i, 6, 4))
            return false;
    return true;
}

// Tunnel: flags, height1, height2 (8.8). Walls are max(height2, 3) tall
// (`wallHeight`) and use the current texture's material. A junction tunnel (10
// words) walls off the room perimeter edges whose bit is set in words 4-5; a
// 3-word tunnel describes the strip after it (after one Texture attribute, if
// there is one). No collision for ceilings.
bool Collector::tunnel(std::size_t index, const PsdlAttribute& a) {
    const auto words = countedWords(a);
    const std::uint16_t flags = at(words, 0);
    const float height1 = static_cast<float>(at(words, 1)) * kFixed88;
    float wallHeight = static_cast<float>(static_cast<int>(at(words, 2))) * kFixed88;
    if (!(wallHeight > kMinTunnelWallHeight))
        wallHeight = kMinTunnelWallHeight;
    const int count = countOf(a);

    if (count == kJunctionTunnelWords) {
        const std::uint32_t edges = static_cast<std::uint32_t>(at(words, 4)) |
                                    (static_cast<std::uint32_t>(at(words, 5)) << 16);
        const auto& perimeter = m_room.perimeter;
        const int n = static_cast<std::uint8_t>(perimeter.size()); // a byte in sdlPage16
        for (int j = 0, prev = n - 1; j < n; prev = j, ++j) {
            if ((edges & (1u << (j & 31))) == 0)
                continue;
            const std::uint16_t here = perimeter[j].vertex;
            const std::uint16_t back = perimeter[prev].vertex;
            if (!nearPair(vertex(here), vertex(back)))
                continue;
            if (!take())
                return false;
            setWall(material(), here, back, wallHeight + vertex(here).y, wallHeight + vertex(back).y);
        }
        return true;
    }
    if (count != kStripTunnelWords)
        return true;

    // The strip this tunnel belongs to. Collect reads the following words
    // directly; past the room's attributes it would read whatever follows.
    std::size_t next = index + 1;
    if (next < m_room.attributes.size() && m_room.attributes[next].type == PsdlAttrType::Texture)
        ++next;
    if (next >= m_room.attributes.size())
        return true; // deviation: MM2 would read past the room's attribute list
    const PsdlAttribute& strip = m_room.attributes[next];
    // Collect reads the count's low byte only when it is a separate word.
    const int sections = strip.subtype != 0 ? strip.subtype : (strip.args.empty() ? 0 : strip.args[0] & 0xFF);
    std::span<const std::uint16_t> list = countedWords(strip);
    switch (strip.type) {
    case PsdlAttrType::RoadStrip:
        m_tunnelStride = 4;
        break;
    case PsdlAttrType::DividedRoadStrip:
        m_tunnelStride = 6;
        list = list.subspan(std::min<std::size_t>(2, list.size()));
        break;
    case PsdlAttrType::RectangleStrip:
        m_tunnelStride = 2;
        break;
    default:
        break; // the previous stride stays (never in retail data)
    }
    if (m_tunnelStride == 0)
        return true; // deviation: MM2 would use an uninitialized stride
    return tunnelStrip(flags, height1, wallHeight, list, m_tunnelStride, sections);
}

bool Collector::tunnelStrip(std::uint16_t flags, float height1, float wallHeight,
                            std::span<const std::uint16_t> list, int stride, int sections) {
    const bool sloped = (flags & TunnelFlag::Sloped) != 0;
    const auto has = [flags](std::uint16_t bits) { return (flags & bits) == bits; };

    // Railing lines: each section's outermost vertex moved out by height1 *
    // 0.333 away from its neighbour (MM2: two 256-entry stack arrays).
    std::vector<Vec3> left, right;
    const auto offsetLine = [&](std::vector<Vec3>& line, int outer, int inner) {
        const float offset = height1 * kRailingOffsetPerHeight;
        line.resize(static_cast<std::size_t>(sections));
        for (int k = 0; k < sections; ++k) {
            const Vec3 p = vertex(at(list, k * stride + outer));
            const Vec3 q = vertex(at(list, k * stride + inner));
            const float dx = p.x - q.x;
            const float dz = p.z - q.z;
            const float f = offset / std::sqrt(dz * dz + dx * dx);
            line[k] = Vec3(p.x - (q.x - p.x) * f, p.y, p.z - (q.z - p.z) * f);
        }
    };
    if (sloped || has(TunnelFlag::Left | TunnelFlag::Railing))
        offsetLine(left, 0, 1);
    if (sloped || has(TunnelFlag::Right | TunnelFlag::Railing))
        offsetLine(right, stride - 1, stride - 2);

    const auto edge = [&](int k, int column) { return at(list, k * stride + column); };

    if (!sloped) {
        // Walls along the strip's outer edges.
        for (int k = 0; k + 1 < sections; ++k) {
            if (flags & TunnelFlag::Left) {
                const std::uint16_t p = edge(k, 0);
                const std::uint16_t q = edge(k + 1, 0);
                if (nearPair(vertex(p), vertex(q))) {
                    if (!take())
                        return false;
                    setWall(material(), p, q, wallHeight + vertex(p).y, wallHeight + vertex(q).y);
                }
            }
            if (flags & TunnelFlag::Right) {
                const std::uint16_t p = edge(k + 1, stride - 1);
                const std::uint16_t q = edge(k, stride - 1);
                if (nearPair(vertex(p), vertex(q))) {
                    if (!take())
                        return false;
                    setWall(material(), p, q, wallHeight + vertex(p).y, wallHeight + vertex(q).y);
                }
            }
        }
        // Railings along the offset lines, with optional end caps back to the
        // road edge. (With no sections MM2's caps would read unset stack
        // entries; nothing here.)
        if (sections <= 0)
            return true;
        if (has(TunnelFlag::Left | TunnelFlag::Railing)) {
            if (flags & TunnelFlag::LeftRailingStartCap) {
                const Vec3 v = vertex(edge(0, 0));
                if (nearPair(left[0], v)) {
                    if (!take())
                        return false;
                    setWall(material(), left[0], v, wallHeight + v.y, wallHeight + left[0].y);
                }
            }
            for (int k = 0; k + 1 < sections; ++k) {
                if (!nearPair(left[k + 1], left[k]))
                    continue;
                if (!take())
                    return false;
                setWall(material(), left[k + 1], left[k], wallHeight + left[k + 1].y, wallHeight + left[k].y);
            }
            if (flags & TunnelFlag::LeftRailingEndCap) {
                const Vec3 v = vertex(edge(sections - 1, 0));
                const Vec3 l = left[static_cast<std::size_t>(sections - 1)];
                if (nearPair(v, l)) {
                    if (!take())
                        return false;
                    setWall(material(), v, l, wallHeight + l.y, wallHeight + v.y);
                }
            }
        }
        if (has(TunnelFlag::Right | TunnelFlag::Railing)) {
            if (flags & TunnelFlag::RightRailingStartCap) {
                const Vec3 v = vertex(edge(0, stride - 1));
                if (nearPair(v, right[0])) {
                    if (!take())
                        return false;
                    setWall(material(), v, right[0], wallHeight + right[0].y, wallHeight + v.y);
                }
            }
            for (int k = 0; k + 1 < sections; ++k) {
                if (!nearPair(right[k], right[k + 1]))
                    continue;
                if (!take())
                    return false;
                setWall(material(), right[k], right[k + 1], wallHeight + right[k].y,
                        wallHeight + right[k + 1].y);
            }
            if (flags & TunnelFlag::RightRailingEndCap) {
                const Vec3 v = vertex(edge(sections - 1, stride - 1));
                const Vec3 r = right[static_cast<std::size_t>(sections - 1)];
                if (nearPair(r, v)) {
                    if (!take())
                        return false;
                    setWall(material(), r, v, wallHeight + v.y, wallHeight + r.y);
                }
            }
        }
        return true;
    }

    // Sloped sides (0x2000): a slope from the road edge up a quarter of the wall
    // height to the offset line, and a wall on the offset line. The start/end
    // cap bits pull the slope's first/last offset point back onto the road edge.
    const float rise = wallHeight * kSlopeRiseFraction;
    if ((flags & TunnelFlag::Left) && sections - 1 > 0) {
        const bool startCap = flags & TunnelFlag::LeftSlopeStartCap;
        const bool endCap = flags & TunnelFlag::LeftSlopeEndCap;
        for (int k = 0; k < sections - 1; ++k) {
            const Vec3 a = vertex(edge(k, 0));
            const Vec3 b = vertex(edge(k + 1, 0));
            const Vec3 p = (k == 0 && startCap) ? a : left[k];
            const Vec3 q = (k == sections - 2 && endCap) ? b : left[k + 1];
            if (nearPair(a, q)) {
                if (!take())
                    return false;
                setQuad(material(), a, b, raised(p, rise), raised(q, rise));
            }
            if (nearPair(p, q)) {
                if (!take())
                    return false;
                setWall(material(), p, q, p.y + wallHeight, q.y + wallHeight);
            }
        }
    }
    if ((flags & TunnelFlag::Right) && sections - 1 > 0) {
        const bool startCap = flags & TunnelFlag::RightSlopeStartCap;
        const bool endCap = flags & TunnelFlag::RightSlopeEndCap;
        for (int k = 0; k < sections - 1; ++k) {
            const Vec3 a = vertex(edge(k, stride - 1));
            const Vec3 b = vertex(edge(k + 1, stride - 1));
            const Vec3 p = (k == 0 && startCap) ? a : right[k];
            const Vec3 q = (k == sections - 2 && endCap) ? b : right[k + 1];
            if (nearPair(b, p)) {
                if (!take())
                    return false;
                setQuad(material(), b, a, raised(q, rise), raised(p, rise));
            }
            if (nearPair(q, p)) {
                if (!take())
                    return false;
                setWall(material(), q, p, q.y + wallHeight, p.y + wallHeight);
            }
        }
    }
    return true;
}

// RoofTriangleFan: a flat fan at its height-table height, culled by height
// first. No texture or material check.
bool Collector::roof(const PsdlAttribute& a) {
    // The height index follows the count word when there is one
    // (PsdlAttribute::roofHeight() reads args[0], the count word, then).
    const float y = height(at(countedWords(a), 0));
    if (!spansHeight(y))
        return true;
    const auto list = a.vertices();
    const int n = countOf(a) + 1;
    const std::uint16_t hub = at(list, 0);
    for (int i = 2; i < n; ++i) {
        const std::uint16_t b = at(list, i - 1);
        const std::uint16_t c = at(list, i);
        if (!nearTriangle(vertex(hub), vertex(b), vertex(c)))
            continue;
        if (!take())
            return false;
        setFlatTri(material(), hub, b, c, y);
    }
    return true;
}

bool Collector::attribute(std::size_t index) {
    const PsdlAttribute& a = m_room.attributes[index];
    switch (a.type) {
    case PsdlAttrType::RoadStrip:
        return roadStrip(a);
    case PsdlAttrType::SidewalkStrip:
        return sidewalkStrip(a);
    case PsdlAttrType::RectangleStrip:
        return rectangleStrip(a);
    case PsdlAttrType::Sliver:
        return true; // no collision
    case PsdlAttrType::Crosswalk:
        return crosswalk(a);
    case PsdlAttrType::RoadTriangleFan:
        return fan(a, true);
    case PsdlAttrType::TriangleFan:
        return fan(a, false);
    case PsdlAttrType::FacadeBound:
        return facadeBound(a);
    case PsdlAttrType::DividedRoadStrip:
        return dividedRoadStrip(a);
    case PsdlAttrType::Tunnel:
        return tunnel(index, a);
    case PsdlAttrType::Texture:
        setTexture(a.textureBase() + 1);
        return true;
    case PsdlAttrType::Facade:
        return true; // no collision (FacadeBound walls stand in for facades)
    case PsdlAttrType::RoofTriangleFan:
        return roof(a);
    }
    return true;
}

} // namespace

void SdlPolyBuffer::reset(const Psdl& psdl) {
    vertices = psdl.vertices;
    psdlVertexCount = psdl.vertices.size();
    polys.clear();
    vertexBudget = kSdlGeneratedVertexBudget;
}

int collectRoomPolygons(const Psdl& psdl, std::size_t room, const SdlSphere* sphere,
                        std::span<const std::uint8_t> textureMaterials, SdlPolyBuffer& out, int capacity,
                        bool* overflow, std::uint32_t* state, std::uint16_t probeRoom) {
    if (room == 0 || room >= psdl.rooms.size())
        return 0; // lvlSDL has no page for room 0
    const PsdlRoom& rm = psdl.rooms[room];
    if (rm.attributes.empty())
        return 0; // sdlPage16 has no attribute list
    if (out.vertices.size() < psdl.vertices.size())
        out.reset(psdl);

    // The probe room's SpecialBound flag (lvlSDL::CollideProbe sets the global).
    const bool special = probeRoom != 0 && probeRoom < psdl.rooms.size() &&
                         (psdl.rooms[probeRoom].flags & RoomFlag::SpecialBound) != 0;
    Collector c(psdl, rm, sphere, textureMaterials, out, capacity, special);

    const std::uint32_t start = state ? *state : 0;
    const std::uint32_t startWord = start >> 11;
    c.resumeTexture(static_cast<int>(start & 0x7ff));

    // Find the attribute at the start offset.
    std::size_t index = 0;
    std::uint32_t word = 0;
    while (index < rm.attributes.size() && word < startWord) {
        word += 1 + static_cast<std::uint32_t>(rm.attributes[index].args.size());
        ++index;
    }
    if (word != startWord || index >= rm.attributes.size())
        return 0; // deviation: MM2 would parse from wherever the state points

    std::uint32_t resume = 0; // Collect's state for an early return
    for (; index < rm.attributes.size(); ++index) {
        const PsdlAttribute& a = rm.attributes[index];
        if (!c.attribute(index)) {
            if (state)
                *state = resume;
            if (overflow)
                *overflow = true;
            return c.added();
        }
        word += 1 + static_cast<std::uint32_t>(a.args.size());
        resume = (word << 11) | static_cast<std::uint32_t>(c.texture());
        if (a.last)
            break; // Collect stops after the last-flagged attribute
    }
    return c.added();
}

std::vector<std::uint8_t> sdlTextureMaterials(const Psdl& psdl,
                                              std::span<const TextureMaterial> textureMaterials,
                                              const std::function<int(std::string_view)>& materialIndex) {
    // lvlSDL::LoadBinary: materials.csv texture -> lvlMaterialMgr index + 1.
    std::unordered_map<std::string, int> byTexture;
    for (const auto& row : textureMaterials) {
        if (row.material == "none")
            continue;
        const int index = materialIndex ? materialIndex(row.material) : 0;
        if (index <= 0)
            continue; // lvlMaterialMgr::Find failed
        byTexture.try_emplace(row.texture, index);
    }
    std::vector<std::uint8_t> table(psdl.textures.size() + 1, 0);
    for (std::size_t i = 0; i < psdl.textures.size(); ++i) {
        std::string name = psdl.textures[i];
        if (name.empty())
            continue;
        // Movie textures ("name-0nnn"): the material belongs to the base name.
        const std::size_t n = name.size();
        if (n >= 5 && name[n - 5] == '-' && name[n - 4] == '0')
            name.resize(n - 5);
        if (const auto it = byTexture.find(name); it != byTexture.end())
            table[i + 1] = static_cast<std::uint8_t>(it->second);
    }
    return table;
}

int sdlMaterialIndex(std::span<const PhysMaterial> materials, std::string_view name) {
    // lvlMaterialMgr: entry 0 is the default material phMaterialMgr's
    // constructor adds (named "default"); lvlMaterialMgr::Load appends each
    // material whose name it cannot Find yet.
    if (name == "default")
        return 1;
    std::vector<std::string_view> added{"default"};
    int index = 1;
    for (const auto& m : materials) {
        bool known = false;
        for (const auto& seen : added)
            known = known || seen == m.name;
        if (known)
            continue;
        added.push_back(m.name);
        ++index;
        if (m.name == name)
            return index;
    }
    return 0;
}

std::vector<std::uint8_t> waterRooms(const Psdl& psdl, std::span<const std::uint8_t> textureMaterials) {
    // cityLevel::Load, per room: when the room's attribute list starts with a
    // Texture attribute, the texture value (subtype << 8 | its word) indexes
    // lvlSDL's texture -> material table; a value of 2 sets lvlRoomInfo flag 4.
    std::vector<std::uint8_t> water(psdl.rooms.size(), 0);
    for (std::size_t room = 1; room < psdl.rooms.size(); ++room) {
        const auto& attrs = psdl.rooms[room].attributes;
        if (attrs.empty() || attrs.front().type != PsdlAttrType::Texture)
            continue;
        const int texture = attrs.front().textureBase() + 1;
        if (texture >= 0 && static_cast<std::size_t>(texture) < textureMaterials.size() &&
            textureMaterials[static_cast<std::size_t>(texture)] == 2)
            water[room] = 1;
    }
    return water;
}

} // namespace mm2::city
