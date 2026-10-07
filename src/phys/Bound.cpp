#include "phys/Bound.h"

#include <algorithm>

namespace mm2::phys {

Aabb BoundGeometry::bounds() const {
    Aabb b;
    for (const auto& v : vertices)
        b.expand(v);
    return b;
}

bool Polygon::finalize() {
    if (count < 3 || count > 4)
        return false;
    // Newell's method copes with slightly non-planar quads.
    Vec3 n;
    for (int i = 0; i < count; ++i) {
        const Vec3& a = v[static_cast<std::size_t>(i)];
        const Vec3& b = v[static_cast<std::size_t>((i + 1) % count)];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    const float len = n.mag();
    if (len < 1e-8f)
        return false;
    normal = n * (1.0f / len);
    Vec3 centroid;
    for (int i = 0; i < count; ++i)
        centroid += v[static_cast<std::size_t>(i)];
    centroid = centroid * (1.0f / static_cast<float>(count));
    d = normal.dot(centroid);
    box = Aabb{};
    for (int i = 0; i < count; ++i) {
        const Vec3& a = v[static_cast<std::size_t>(i)];
        const Vec3& b = v[static_cast<std::size_t>((i + 1) % count)];
        const Vec3 e = b - a;
        Vec3 out = e.cross(normal);
        const float el = out.mag();
        edgeOut[static_cast<std::size_t>(i)] = el > 1e-9f ? out * (1.0f / el) : Vec3{};
        box.expand(a);
    }
    return true;
}

bool Polygon::containsProjected(const Vec3& p, float slack) const {
    for (int i = 0; i < count; ++i) {
        const Vec3& e = edgeOut[static_cast<std::size_t>(i)];
        if ((p - v[static_cast<std::size_t>(i)]).dot(e) > slack)
            return false;
    }
    return true;
}

Vec3 Polygon::closestPoint(const Vec3& p) const {
    const Vec3 onPlane = p - normal * (normal.dot(p) - d);
    if (containsProjected(onPlane))
        return onPlane;
    // Closest point on the boundary.
    Vec3 best;
    float bestD2 = 1e30f;
    for (int i = 0; i < count; ++i) {
        const Vec3& a = v[static_cast<std::size_t>(i)];
        const Vec3& b = v[static_cast<std::size_t>((i + 1) % count)];
        const Vec3 ab = b - a;
        const float l2 = ab.mag2();
        const float t = l2 > 0 ? clampf((p - a).dot(ab) / l2, 0.0f, 1.0f) : 0.0f;
        const Vec3 q = a + ab * t;
        const float d2 = q.dist2(p);
        if (d2 < bestD2) {
            bestD2 = d2;
            best = q;
        }
    }
    return best;
}

bool segmentPolygon(const Vec3& a, const Vec3& b, const Polygon& poly, float& t) {
    const float da = poly.normal.dot(a) - poly.d;
    const float db = poly.normal.dot(b) - poly.d;
    if ((da > 0 && db > 0) || (da < 0 && db < 0) || da == db)
        return false;
    t = da / (da - db);
    const Vec3 p = a + (b - a) * t;
    return poly.containsProjected(p, 1e-4f);
}

void PolygonSoup::add(const BoundGeometry& geom, const Mat34& xform, const MaterialTable& materials) {
    std::vector<int> matIndex(geom.materialNames.size());
    for (std::size_t i = 0; i < matIndex.size(); ++i)
        matIndex[i] = materials.resolve(geom.materialNames[i]);
    for (const auto& p : geom.polys) {
        Polygon poly;
        poly.count = p.count;
        bool ok = p.count >= 3 && p.count <= 4;
        for (int i = 0; ok && i < p.count; ++i) {
            const auto vi = p.v[static_cast<std::size_t>(i)];
            if (vi >= geom.vertices.size())
                ok = false;
            else
                poly.v[static_cast<std::size_t>(i)] = xform.transform(geom.vertices[vi]);
        }
        if (!ok)
            continue;
        poly.material = p.material < matIndex.size() ? matIndex[p.material] : 0;
        add(poly);
    }
}

void PolygonSoup::add(const Polygon& poly) {
    Polygon p = poly;
    if (!p.finalize())
        return;
    m_bounds.expand(p.box);
    m_polys.push_back(p);
}

void PolygonSoup::finalize(float cellSize) {
    m_cell = cellSize;
    m_invCell = 1.0f / cellSize;
    if (m_polys.empty()) {
        m_nx = m_nz = 0;
        m_start.assign(1, 0);
        m_items.clear();
        return;
    }
    m_nx = std::max(1, static_cast<int>(std::ceil((m_bounds.max.x - m_bounds.min.x) * m_invCell)) + 1);
    m_nz = std::max(1, static_cast<int>(std::ceil((m_bounds.max.z - m_bounds.min.z) * m_invCell)) + 1);
    const std::size_t cells = static_cast<std::size_t>(m_nx) * static_cast<std::size_t>(m_nz);
    std::vector<std::uint32_t> counts(cells + 1, 0);
    for (const auto& p : m_polys) {
        int x0, z0, x1, z1;
        cellRange(p.box, x0, z0, x1, z1);
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x)
                ++counts[static_cast<std::size_t>(z) * static_cast<std::size_t>(m_nx) +
                         static_cast<std::size_t>(x)];
    }
    m_start.assign(cells + 1, 0);
    for (std::size_t c = 0; c < cells; ++c)
        m_start[c + 1] = m_start[c] + counts[c];
    m_items.assign(m_start[cells], 0);
    std::vector<std::uint32_t> fill(m_start.begin(), m_start.end() - 1);
    for (std::uint32_t i = 0; i < m_polys.size(); ++i) {
        int x0, z0, x1, z1;
        cellRange(m_polys[i].box, x0, z0, x1, z1);
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x)
                m_items[fill[static_cast<std::size_t>(z) * static_cast<std::size_t>(m_nx) +
                             static_cast<std::size_t>(x)]++] = i;
    }
}

void PolygonSoup::cellRange(const Aabb& box, int& x0, int& z0, int& x1, int& z1) const {
    auto cx = [&](float x) {
        return std::clamp(static_cast<int>(std::floor((x - m_bounds.min.x) * m_invCell)), 0, m_nx - 1);
    };
    auto cz = [&](float z) {
        return std::clamp(static_cast<int>(std::floor((z - m_bounds.min.z) * m_invCell)), 0, m_nz - 1);
    };
    x0 = cx(box.min.x);
    x1 = cx(box.max.x);
    z0 = cz(box.min.z);
    z1 = cz(box.max.z);
}

void PolygonSoup::query(const Aabb& box, std::vector<std::uint32_t>& out) const {
    out.clear();
    if (m_polys.empty() || box.max.x < m_bounds.min.x || box.min.x > m_bounds.max.x ||
        box.max.z < m_bounds.min.z || box.min.z > m_bounds.max.z || box.max.y < m_bounds.min.y ||
        box.min.y > m_bounds.max.y)
        return;
    int x0, z0, x1, z1;
    cellRange(box, x0, z0, x1, z1);
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
            const std::size_t c =
                static_cast<std::size_t>(z) * static_cast<std::size_t>(m_nx) + static_cast<std::size_t>(x);
            for (std::uint32_t k = m_start[c]; k < m_start[c + 1]; ++k) {
                const auto& pb = m_polys[m_items[k]].box;
                if (pb.min.x <= box.max.x && pb.max.x >= box.min.x && pb.min.y <= box.max.y &&
                    pb.max.y >= box.min.y && pb.min.z <= box.max.z && pb.max.z >= box.min.z)
                    out.push_back(m_items[k]);
            }
        }
    std::ranges::sort(out);
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

bool PolygonSoup::raycast(const Vec3& a, const Vec3& b, RayHit& hit) const {
    Aabb box;
    box.expand(a);
    box.expand(b);
    std::vector<std::uint32_t> candidates;
    query(box, candidates);
    bool found = false;
    float best = 2.0f;
    for (std::uint32_t i : candidates) {
        float t;
        if (segmentPolygon(a, b, m_polys[i], t) && t < best) {
            best = t;
            hit.t = t;
            hit.position = a + (b - a) * t;
            hit.normal = m_polys[i].normal;
            hit.material = m_polys[i].material;
            hit.polygon = static_cast<int>(i);
            found = true;
        }
    }
    return found;
}

Vec3 Obb::corner(int i) const {
    return center + axis[0] * ((i & 1) ? half.x : -half.x) + axis[1] * ((i & 2) ? half.y : -half.y) +
           axis[2] * ((i & 4) ? half.z : -half.z);
}

Aabb Obb::aabb() const {
    const Vec3 e{projectRadius({1, 0, 0}), projectRadius({0, 1, 0}), projectRadius({0, 0, 1})};
    return Aabb{center - e, center + e};
}

float Obb::projectRadius(const Vec3& dir) const {
    return std::abs(axis[0].dot(dir)) * half.x + std::abs(axis[1].dot(dir)) * half.y +
           std::abs(axis[2].dot(dir)) * half.z;
}

} // namespace mm2::phys
