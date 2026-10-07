#include "city/CityMesh.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace mm2::city {
namespace {

// Texture offsets within a texture group selected by a Texture attribute.
// Road groups are [road, sidewalk, road LOD]; intersection groups are
// [intersection, sidewalk, crosswalk, intersection LOD] (see docs).
constexpr int kOffsetRoad = 0;
constexpr int kOffsetSidewalk = 1;
constexpr int kOffsetCrosswalk = 2;

// Planar texture scale for fans, roofs and other ground polygons (metres per
// texture repeat). Reconstruction; the original mapping is not known.
constexpr float kPlanarScale = 1.0f / 8.0f;
// Lift for decals drawn on top of another surface (crosswalks, flat medians).
constexpr float kDecalLift = 0.01f;

struct TunnelState {
    std::uint16_t flags = 0;
    float height1 = 0, height2 = 0;
    int texture = -1;
};

class RoomBuilder {
public:
    RoomBuilder(const Psdl& p, std::size_t room, const CityMeshOptions& opt, CityRoomMesh& out)
        : m_p(p), m_room(room), m_opt(opt), m_out(out) {}

    void build() {
        const auto& rm = m_p.rooms[m_room];
        for (const auto& a : rm.attributes) {
            switch (a.type) {
            case PsdlAttrType::Texture:
                m_base = a.textureBase();
                break;
            case PsdlAttrType::RoadStrip:
                roadStrip(a);
                break;
            case PsdlAttrType::SidewalkStrip:
                sidewalkStrip(a);
                break;
            case PsdlAttrType::RectangleStrip:
                rectStrip(a);
                break;
            case PsdlAttrType::Sliver:
                sliver(a);
                break;
            case PsdlAttrType::Crosswalk:
                crosswalk(a);
                break;
            case PsdlAttrType::RoadTriangleFan:
                fan(a, SurfaceKind::Road, tex(kOffsetRoad));
                break;
            case PsdlAttrType::TriangleFan:
                fan(a, SurfaceKind::Ground, tex(kOffsetRoad));
                break;
            case PsdlAttrType::FacadeBound:
                facadeBound(a);
                break;
            case PsdlAttrType::DividedRoadStrip:
                dividedStrip(a);
                break;
            case PsdlAttrType::Tunnel:
                tunnel(a);
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
    // ---- helpers ------------------------------------------------------------

    int tex(int offset) const {
        if (m_base < 0)
            return -1;
        const int t = m_base + offset;
        return t < static_cast<int>(m_p.textures.size()) ? t : -1;
    }

    Vec3 vert(std::uint16_t i) const { return i < m_p.vertices.size() ? m_p.vertices[i] : Vec3{}; }
    float height(std::uint16_t i) const { return i < m_p.heights.size() ? m_p.heights[i] : 0.0f; }

    // Batches are referred to by index: creating one may reallocate the vector.
    using BatchId = int;
    BatchId batch(int texture, SurfaceKind kind) {
        if (texture < 0 && !m_opt.includeUntextured && kind != SurfaceKind::FacadeBound)
            return -1;
        for (std::size_t i = 0; i < m_out.batches.size(); ++i)
            if (m_out.batches[i].texture == texture && m_out.batches[i].kind == kind)
                return static_cast<BatchId>(i);
        m_out.batches.push_back(CityBatch{texture, kind, {}, {}});
        return static_cast<BatchId>(m_out.batches.size() - 1);
    }

    struct V {
        Vec3 p;
        Vec2 uv;
    };

    // Emits a triangle with a flat normal, in the given winding.
    void tri(BatchId id, const V& a, const V& c1, const V& c2) {
        if (id < 0)
            return;
        CityBatch* b = &m_out.batches[static_cast<std::size_t>(id)];
        const Vec3 n = (c1.p - a.p).cross(c2.p - a.p);
        const float len = n.mag();
        if (len < 1e-8f)
            return; // degenerate
        const Vec3 nn = n * (1.0f / len);
        const auto base = static_cast<std::uint32_t>(b->vertices.size());
        for (const V* v : {&a, &c1, &c2}) {
            b->vertices.push_back({v->p, nn, v->uv});
            m_out.bounds.expand(v->p);
        }
        b->indices.insert(b->indices.end(), {base, base + 1, base + 2});
    }

    // Triangle facing up (+Y); flips the winding if needed.
    void triUp(BatchId b, const V& a, const V& c1, const V& c2) {
        const Vec3 n = (c1.p - a.p).cross(c2.p - a.p);
        if (n.y >= 0)
            tri(b, a, c1, c2);
        else
            tri(b, a, c2, c1);
    }

    // Triangle whose front faces along `dir`.
    void triFacing(BatchId b, const V& a, const V& c1, const V& c2, const Vec3& dir) {
        const Vec3 n = (c1.p - a.p).cross(c2.p - a.p);
        if (n.dot(dir) >= 0)
            tri(b, a, c1, c2);
        else
            tri(b, a, c2, c1);
    }

    // Quad a-b-c-d (in order around the edge) facing up / along dir.
    void quadUp(BatchId b, const V& a, const V& c1, const V& c2, const V& d) {
        triUp(b, a, c1, c2);
        triUp(b, a, c2, d);
    }
    void quadFacing(BatchId b, const V& a, const V& c1, const V& c2, const V& d, const Vec3& dir) {
        triFacing(b, a, c1, c2, dir);
        triFacing(b, a, c2, d, dir);
    }

    static Vec2 planarUv(const Vec3& p) { return {p.x * kPlanarScale, p.z * kPlanarScale}; }
    static Vec3 withY(const Vec3& p, float y) { return {p.x, y, p.z}; }

    // Vertical wall between ground points p0 -> p1, from y0 to y1 at each end.
    // Front faces (p1 - p0) x up, which is outward for PSDL facades.
    void wall(BatchId b, const Vec3& p0, const Vec3& p1, float bottom0, float bottom1, float top0, float top1,
              float uRepeat, float vRepeat) {
        const V a{withY(p0, bottom0), {0, vRepeat}};
        const V c1{withY(p1, bottom1), {uRepeat, vRepeat}};
        const V c2{withY(p1, top1), {uRepeat, 0}};
        const V d{withY(p0, top0), {0, 0}};
        const Vec3 outward = (p1 - p0).cross(Vec3::yAxis());
        quadFacing(b, a, c1, c2, d, outward);
    }

    // A strip edge (one vertex per section) and its cumulative length.
    static std::vector<float> cumulative(const std::vector<Vec3>& pts) {
        std::vector<float> out(pts.size(), 0.0f);
        for (std::size_t i = 1; i < pts.size(); ++i)
            out[i] = out[i - 1] + pts[i].dist(pts[i - 1]);
        return out;
    }

    // Sidewalk surface from the curb line (raised to the outer edge's height)
    // to the outer edge, plus the curb face toward the road.
    void sidewalkBand(const std::vector<Vec3>& curb, const std::vector<Vec3>& outer,
                      const std::vector<Vec3>& road) {
        BatchId top = batch(tex(kOffsetSidewalk), SurfaceKind::Sidewalk);
        BatchId face = batch(tex(kOffsetSidewalk), SurfaceKind::Curb);
        std::vector<Vec3> mid(curb.size());
        for (std::size_t i = 0; i < curb.size(); ++i)
            mid[i] = (curb[i] + outer[i]) * 0.5f;
        const auto along = cumulative(mid);
        for (std::size_t i = 0; i + 1 < curb.size(); ++i) {
            const float w = std::max(0.5f, curb[i].dist(withY(outer[i], curb[i].y)));
            const Vec3 r0 = withY(curb[i], outer[i].y), r1 = withY(curb[i + 1], outer[i + 1].y);
            if (curb[i].dist(outer[i]) > 1e-3f || curb[i + 1].dist(outer[i + 1]) > 1e-3f) {
                quadUp(top, {r0, {0, along[i] / w}}, {outer[i], {1, along[i] / w}},
                       {outer[i + 1], {1, along[i + 1] / w}}, {r1, {0, along[i + 1] / w}});
            }
            // Curb faces the road: from the curb toward the opposite side.
            const float h0 = outer[i].y - curb[i].y, h1 = outer[i + 1].y - curb[i + 1].y;
            if (h0 > 1e-3f || h1 > 1e-3f) {
                const Vec3 towardRoad = road[i] - curb[i];
                quadFacing(face, {curb[i], {along[i] / 4, h0 / 4}}, {curb[i + 1], {along[i + 1] / 4, h1 / 4}},
                           {r1, {along[i + 1] / 4, 0}}, {r0, {along[i] / 4, 0}}, withY(towardRoad, 0));
            }
        }
    }

    // Road surface between two edges; u runs 0..1 from `left` to `right`.
    void roadBand(BatchId b, const std::vector<Vec3>& left, const std::vector<Vec3>& right, float u0,
                  float u1, const std::vector<float>& along, float width) {
        for (std::size_t i = 0; i + 1 < left.size(); ++i) {
            quadUp(b, {left[i], {u0, along[i] / width}}, {right[i], {u1, along[i] / width}},
                   {right[i + 1], {u1, along[i + 1] / width}}, {left[i + 1], {u0, along[i + 1] / width}});
        }
    }

    // Walls for a pending Tunnel attribute along the given left/right edges.
    // `left`/`right` hold road-surface points; walls rise from them.
    void tunnelWalls(const std::vector<Vec3>& left, const std::vector<Vec3>& right) {
        if (!m_tunnel)
            return;
        const TunnelState t = *m_tunnel;
        m_tunnel.reset();
        BatchId b = batch(t.texture, SurfaceKind::Tunnel);
        const bool ceiling = (t.flags & 0x0100) != 0;
        const float wallHeight = ceiling ? std::max(t.height1, t.height2) : t.height1;
        const bool doubleSided = !ceiling; // railings and retaining walls
        const auto along = cumulative(left);
        auto edgeWall = [&](const std::vector<Vec3>& edge, const std::vector<Vec3>& other) {
            for (std::size_t i = 0; i + 1 < edge.size(); ++i) {
                const Vec3 inward = withY(other[i] - edge[i], 0);
                const V a{edge[i], {along[i] / 4, wallHeight / 4}};
                const V c1{edge[i + 1], {along[i + 1] / 4, wallHeight / 4}};
                const V c2{edge[i + 1] + Vec3{0, wallHeight, 0}, {along[i + 1] / 4, 0}};
                const V d{edge[i] + Vec3{0, wallHeight, 0}, {along[i] / 4, 0}};
                quadFacing(b, a, c1, c2, d, inward);
                if (doubleSided)
                    quadFacing(b, a, c1, c2, d, -inward);
            }
        };
        if (t.flags & 0x0001)
            edgeWall(left, right);
        if (t.flags & 0x0002)
            edgeWall(right, left);
        if (ceiling) {
            for (std::size_t i = 0; i + 1 < left.size(); ++i) {
                const Vec3 up{0, wallHeight, 0};
                const V a{left[i] + up, planarUv(left[i])}, c1{right[i] + up, planarUv(right[i])};
                const V c2{right[i + 1] + up, planarUv(right[i + 1])},
                    d{left[i + 1] + up, planarUv(left[i + 1])};
                quadFacing(b, a, c1, c2, d, -Vec3::yAxis());
            }
        }
    }

    // ---- attributes ---------------------------------------------------------

    void roadStrip(const PsdlAttribute& a) {
        const auto v = a.vertices();
        const std::size_t n = v.size() / 4;
        if (n < 2)
            return;
        std::vector<Vec3> o1(n), i1(n), i2(n), o2(n);
        for (std::size_t s = 0; s < n; ++s) {
            o1[s] = vert(v[4 * s]);
            i1[s] = vert(v[4 * s + 1]);
            i2[s] = vert(v[4 * s + 2]);
            o2[s] = vert(v[4 * s + 3]);
        }
        std::vector<Vec3> center(n);
        for (std::size_t s = 0; s < n; ++s)
            center[s] = (i1[s] + i2[s]) * 0.5f;
        const auto along = cumulative(center);
        const float width = std::max(1.0f, i1[0].dist(i2[0]));
        roadBand(batch(tex(kOffsetRoad), SurfaceKind::Road), i1, i2, 0.0f, 1.0f, along, width);
        sidewalkBand(i1, o1, i2);
        sidewalkBand(i2, o2, i1);
        tunnelWalls(o1, o2);
    }

    void sidewalkStrip(const PsdlAttribute& a) {
        auto v = a.vertices();
        // A leading (0,0) or (1,1) pair marks a curb end cap (start/end, inferred).
        bool cap = false;
        if (v.size() >= 2 && v[0] == v[1] && v[0] < 2) {
            cap = true;
            v = v.subspan(2);
        }
        const std::size_t n = v.size() / 2;
        std::vector<Vec3> curb(n), outer(n);
        for (std::size_t s = 0; s < n; ++s) {
            curb[s] = vert(v[2 * s]);
            outer[s] = vert(v[2 * s + 1]);
        }
        if (n >= 2) {
            // The road lies away from the outer edge.
            std::vector<Vec3> road(n);
            for (std::size_t s = 0; s < n; ++s)
                road[s] = curb[s] + (curb[s] - outer[s]);
            sidewalkBand(curb, outer, road);
        } else if (n == 1 && cap) {
            // Single-pair cap: close the curb's open end with a vertical face.
            const float h = outer[0].y - curb[0].y;
            if (h > 1e-3f) {
                BatchId face = batch(tex(kOffsetSidewalk), SurfaceKind::Curb);
                const Vec3 r = withY(curb[0], outer[0].y);
                const Vec3 side = (outer[0] - curb[0]).cross(Vec3::yAxis());
                const V p0{curb[0], {0, h / 4}}, p1{withY(outer[0], curb[0].y), {1, h / 4}},
                    p2{outer[0], {1, 0}}, p3{r, {0, 0}};
                quadFacing(face, p0, p1, p2, p3, side);
                quadFacing(face, p0, p1, p2, p3, -side);
            }
        }
    }

    void rectStrip(const PsdlAttribute& a) {
        const auto v = a.vertices();
        const std::size_t n = v.size() / 2;
        if (n < 2)
            return;
        std::vector<Vec3> l(n), r(n), c(n);
        for (std::size_t s = 0; s < n; ++s) {
            l[s] = vert(v[2 * s]);
            r[s] = vert(v[2 * s + 1]);
            c[s] = (l[s] + r[s]) * 0.5f;
        }
        const auto along = cumulative(c);
        const float width = std::max(1.0f, l[0].dist(r[0]));
        const bool road = (m_p.rooms[m_room].flags & (RoomFlag::Road | RoomFlag::Intersection)) != 0;
        roadBand(batch(tex(kOffsetRoad), road ? SurfaceKind::Road : SurfaceKind::Ground), l, r, 0.0f, 1.0f,
                 along, width);
        tunnelWalls(l, r);
    }

    void dividedStrip(const PsdlAttribute& a) {
        const auto v = a.vertices();
        const std::size_t n = v.size() / 6;
        if (n < 2)
            return;
        std::vector<Vec3> o1(n), i1(n), d1(n), d2(n), i2(n), o2(n), center(n);
        for (std::size_t s = 0; s < n; ++s) {
            o1[s] = vert(v[6 * s]);
            i1[s] = vert(v[6 * s + 1]);
            d1[s] = vert(v[6 * s + 2]);
            d2[s] = vert(v[6 * s + 3]);
            i2[s] = vert(v[6 * s + 4]);
            o2[s] = vert(v[6 * s + 5]);
            center[s] = (d1[s] + d2[s]) * 0.5f;
        }
        const auto along = cumulative(center);
        const float width = std::max(1.0f, i1[0].dist(i2[0]));
        BatchId road = batch(tex(kOffsetRoad), SurfaceKind::Road);
        roadBand(road, i1, d1, 0.0f, 0.5f, along, width);
        roadBand(road, d2, i2, 0.5f, 1.0f, along, width);
        sidewalkBand(i1, o1, i2);
        sidewalkBand(i2, o2, i1);

        // Median. Texture value is 1-based like Texture attributes; the side
        // uses value-1 and the top uses value (inferred from texture pairs
        // such as swalk_f / s_grass).
        const int value = a.dividerTexture();
        const int sideTex = value - 1 < static_cast<int>(m_p.textures.size()) ? value - 1 : -1;
        const int topTex = value < static_cast<int>(m_p.textures.size()) ? value : sideTex;
        BatchId top = batch(topTex, SurfaceKind::Divider);
        BatchId side = batch(sideTex, SurfaceKind::Divider);
        const float h = a.dividerHeight();
        const float dw = std::max(0.25f, d1[0].dist(d2[0]));
        for (std::size_t s = 0; s + 1 < n; ++s) {
            const float va = along[s] / dw, vb = along[s + 1] / dw;
            switch (a.dividerType()) {
            case 2: { // elevated: raised top plus two curb faces
                const Vec3 up{0, h, 0};
                quadUp(top, {d1[s] + up, {0, va}}, {d2[s] + up, {1, va}}, {d2[s + 1] + up, {1, vb}},
                       {d1[s + 1] + up, {0, vb}});
                quadFacing(side, {d1[s], {va, h}}, {d1[s + 1], {vb, h}}, {d1[s + 1] + up, {vb, 0}},
                           {d1[s] + up, {va, 0}}, withY(d1[s] - d2[s], 0));
                quadFacing(side, {d2[s], {va, h}}, {d2[s + 1], {vb, h}}, {d2[s + 1] + up, {vb, 0}},
                           {d2[s] + up, {va, 0}}, withY(d2[s] - d1[s], 0));
                break;
            }
            case 3: { // wedged (jersey barrier): two slopes meeting at the top
                const Vec3 ma = (d1[s] + d2[s]) * 0.5f + Vec3{0, h, 0};
                const Vec3 mb = (d1[s + 1] + d2[s + 1]) * 0.5f + Vec3{0, h, 0};
                quadFacing(side, {d1[s], {va, 1}}, {d1[s + 1], {vb, 1}}, {mb, {vb, 0}}, {ma, {va, 0}},
                           withY(d1[s] - d2[s], 0) + Vec3{0, 0.1f, 0});
                quadFacing(side, {d2[s], {va, 1}}, {d2[s + 1], {vb, 1}}, {mb, {vb, 0}}, {ma, {va, 0}},
                           withY(d2[s] - d1[s], 0) + Vec3{0, 0.1f, 0});
                break;
            }
            default: { // flat: painted/grass median at road level
                const Vec3 lift{0, kDecalLift, 0};
                quadUp(top, {d1[s] + lift, {0, va}}, {d2[s] + lift, {1, va}}, {d2[s + 1] + lift, {1, vb}},
                       {d1[s + 1] + lift, {0, vb}});
                break;
            }
            }
        }
        tunnelWalls(o1, o2);
    }

    void fan(const PsdlAttribute& a, SurfaceKind kind, int texture) {
        const auto v = a.vertices();
        if (v.size() < 3)
            return;
        BatchId b = batch(texture, kind);
        const Vec3 p0 = vert(v[0]);
        for (std::size_t i = 1; i + 1 < v.size(); ++i) {
            const Vec3 p1 = vert(v[i]), p2 = vert(v[i + 1]);
            triUp(b, {p0, planarUv(p0)}, {p1, planarUv(p1)}, {p2, planarUv(p2)});
        }
        if (kind == SurfaceKind::Road && m_tunnel)
            junctionWalls();
    }

    // Junction tunnel: walls along the perimeter edges selected by the masks.
    void junctionWalls() {
        const TunnelState t = *m_tunnel;
        m_tunnel.reset();
        const auto& per = m_p.rooms[m_room].perimeter;
        if (per.size() < 2)
            return;
        BatchId b = batch(t.texture, SurfaceKind::Tunnel);
        Vec3 centroid{};
        for (const auto& pt : per)
            centroid += vert(pt.vertex);
        centroid = centroid * (1.0f / static_cast<float>(per.size()));
        const bool ceiling = (t.flags & 0x0100) != 0;
        const float wallHeight = ceiling ? std::max(t.height1, t.height2) : t.height1;
        for (std::size_t e = 0; e < per.size() && e < 16; ++e) {
            if (!(m_tunnelEdgeMask & (1u << e)))
                continue;
            const Vec3 p0 = vert(per[e].vertex), p1 = vert(per[(e + 1) % per.size()].vertex);
            const float len = p0.dist(p1);
            if (len < 1e-3f)
                continue;
            const Vec3 up{0, wallHeight, 0};
            const V a{p0, {0, wallHeight / 4}}, c1{p1, {len / 4, wallHeight / 4}}, c2{p1 + up, {len / 4, 0}},
                d{p0 + up, {0, 0}};
            const Vec3 inward = withY(centroid - (p0 + p1) * 0.5f, 0);
            quadFacing(b, a, c1, c2, d, inward);
            if (!ceiling)
                quadFacing(b, a, c1, c2, d, -inward);
        }
    }

    void crosswalk(const PsdlAttribute& a) {
        const auto v = a.vertices();
        if (v.size() < 4)
            return;
        const Vec3 lift{0, kDecalLift, 0};
        const Vec3 p0 = vert(v[0]) + lift, p1 = vert(v[1]) + lift, p2 = vert(v[2]) + lift,
                   p3 = vert(v[3]) + lift;
        // Two pairs (p0,p1) and (p2,p3) across the crossing.
        quadUp(batch(tex(kOffsetCrosswalk), SurfaceKind::Crosswalk), {p0, {0, 0}}, {p1, {1, 0}}, {p3, {1, 1}},
               {p2, {0, 1}});
    }

    void facade(const PsdlAttribute& a) {
        const Vec3 p0 = vert(a.wallLeft()), p1 = vert(a.wallRight());
        float bottom = height(a.facadeBottom()), top = height(a.facadeTop());
        // Negative repeats are stored for some facades; use the magnitude
        // (whether they mirror the texture is unknown).
        const float u = a.facadeURepeat() ? static_cast<float>(std::abs(a.facadeURepeat())) : 1.0f;
        const float vr = a.facadeVRepeat() ? static_cast<float>(std::abs(a.facadeVRepeat())) : 1.0f;
        if (top < bottom)
            std::swap(top, bottom);
        wall(batch(tex(0), SurfaceKind::Wall), p0, p1, bottom, bottom, top, top, u, vr);
    }

    void sliver(const PsdlAttribute& a) {
        const Vec3 p0 = vert(a.wallLeft()), p1 = vert(a.wallRight());
        const float top = height(a.sliverTop());
        const float len = p0.dist(p1);
        // The second argument is also an index into the height table; the
        // values found there (1/10, 1/8, 1/6, 1/5, 1/4, 1/2, 1) are texture
        // repeats per metre, applied along the wall and up it. Verified by
        // the distribution of values across both cities; the exact mapping
        // (e.g. where the v origin lies) is inferred.
        float density = height(a.sliverTextureScale());
        if (!(density > 0.0f) || density > 4.0f)
            density = 0.25f;
        const float h = std::max(std::abs(top - p0.y), std::abs(top - p1.y));
        wall(batch(tex(0), SurfaceKind::Wall), p0, p1, p0.y, p1.y, top, top, std::max(len * density, 0.01f),
             std::max(h * density, 0.01f));
    }

    void facadeBound(const PsdlAttribute& a) {
        if (!m_opt.includeFacadeBounds)
            return;
        const Vec3 p0 = vert(a.wallLeft()), p1 = vert(a.wallRight());
        const float top = height(a.facadeBoundTop());
        wall(batch(-1, SurfaceKind::FacadeBound), p0, p1, p0.y, p1.y, top, top, 1.0f, 1.0f);
    }

    void roof(const PsdlAttribute& a) {
        const auto v = a.vertices();
        if (v.size() < 3)
            return;
        const float y = height(a.roofHeight());
        BatchId b = batch(tex(0), SurfaceKind::Roof);
        const Vec3 p0 = withY(vert(v[0]), y);
        for (std::size_t i = 1; i + 1 < v.size(); ++i) {
            const Vec3 p1 = withY(vert(v[i]), y), p2 = withY(vert(v[i + 1]), y);
            triUp(b, {p0, planarUv(p0)}, {p1, planarUv(p1)}, {p2, planarUv(p2)});
        }
    }

    void tunnel(const PsdlAttribute& a) {
        if (a.args.size() < (a.subtype == 0 ? 4u : 3u))
            return;
        m_tunnel = TunnelState{a.tunnelFlags(), a.tunnelHeight1(), a.tunnelHeight2(), tex(0)};
        m_tunnelEdgeMask = 0;
        for (std::uint16_t mask : a.tunnelEdgeMasks())
            m_tunnelEdgeMask |= mask;
    }

    const Psdl& m_p;
    std::size_t m_room;
    const CityMeshOptions& m_opt;
    CityRoomMesh& m_out;
    int m_base = -1;
    std::optional<TunnelState> m_tunnel;
    std::uint32_t m_tunnelEdgeMask = 0;
};

} // namespace

const char* surfaceKindName(SurfaceKind k) {
    switch (k) {
    case SurfaceKind::Road:
        return "road";
    case SurfaceKind::Sidewalk:
        return "sidewalk";
    case SurfaceKind::Curb:
        return "curb";
    case SurfaceKind::Crosswalk:
        return "crosswalk";
    case SurfaceKind::Ground:
        return "ground";
    case SurfaceKind::Wall:
        return "wall";
    case SurfaceKind::Roof:
        return "roof";
    case SurfaceKind::Divider:
        return "divider";
    case SurfaceKind::Tunnel:
        return "tunnel";
    case SurfaceKind::FacadeBound:
        return "facadebound";
    }
    return "?";
}

std::size_t CityMesh::triangleCount() const {
    std::size_t n = 0;
    for (const auto& r : rooms)
        for (const auto& b : r.batches)
            n += b.indices.size() / 3;
    return n;
}

std::size_t CityMesh::vertexCount() const {
    std::size_t n = 0;
    for (const auto& r : rooms)
        for (const auto& b : r.batches)
            n += b.vertices.size();
    return n;
}

CityRoomMesh buildRoomMesh(const Psdl& psdl, std::size_t room, const CityMeshOptions& options) {
    CityRoomMesh out;
    if (room == 0 || room >= psdl.rooms.size())
        return out;
    RoomBuilder(psdl, room, options, out).build();
    std::erase_if(out.batches, [](const CityBatch& b) { return b.indices.empty(); });
    return out;
}

CityMesh buildCityMesh(const Psdl& psdl, const CityMeshOptions& options) {
    CityMesh mesh;
    mesh.rooms.resize(psdl.rooms.size());
    for (std::size_t room = 1; room < psdl.rooms.size(); ++room)
        mesh.rooms[room] = buildRoomMesh(psdl, room, options);
    return mesh;
}

} // namespace mm2::city
