#pragma once

// A one-room level for physics tests: fixed world-space polygons (what
// sdlPage16::Collect would hand lvlSDL) and a list of instances.

#include "phys/Level.h"
#include "phys/PolygonSoup.h"

#include <vector>

namespace mm2::phys::fixtures {

class TestLevel final : public Level {
public:
    struct Poly {
        Vec3 corners[4];
        int count = 4;
        Vec3 normal;
        std::uint8_t material = 0;
    };

    // A polygon from 3 or 4 corners, counter-clockwise seen from its front.
    void add(std::initializer_list<Vec3> corners, std::uint8_t material = 0) {
        Poly p;
        int i = 0;
        for (const Vec3& c : corners)
            p.corners[i++] = c;
        p.count = i;
        // phPolygon's normal: (v2 - v1) x (v0 - v1).
        p.normal = (p.corners[2] - p.corners[1]).cross(p.corners[0] - p.corners[1]).normalized();
        p.material = material;
        polys.push_back(p);
    }
    // A square floor at height y.
    void floor(float half, float y = 0.0f, std::uint8_t material = 0) {
        add({{-half, y, -half}, {-half, y, half}, {half, y, half}, {half, y, -half}}, material);
    }
    // The same polygons as a probe soup (for the wheels).
    PolygonSoup soup(const MaterialTable& materials) const {
        PolygonSoup s;
        for (const Poly& p : polys) {
            SoupGeometry g;
            for (int i = 0; i < p.count; ++i)
                g.vertices.push_back(p.corners[i]);
            SoupGeometry::Poly sp;
            sp.v = {0, 1, 2, 3};
            sp.count = static_cast<std::uint8_t>(p.count);
            g.polys.push_back(sp);
            g.materialNames.push_back(materials[p.material].name);
            s.add(g, Mat34::identity(), materials);
        }
        s.finalize(64.0f);
        return s;
    }

    int findRoom(const Vec3&, int) const override { return 1; }
    int touchedNeighbors(int*, int, int, const Vec3&, float) const override { return 0; }
    void collect(const int*, int, const Vec3&, float, LevelBound& out) const override {
        out.clear();
        for (const Poly& p : polys)
            out.addPolygon(p.corners, p.count, p.normal, p.material);
    }
    void instances(int room, std::vector<Instance*>& out) const override {
        if (room == 1)
            out.insert(out.end(), objects.begin(), objects.end());
    }

    std::vector<Poly> polys;
    std::vector<Instance*> objects;
};

// A static object with a bound (lvlInstance).
class TestInstance final : public Instance {
public:
    TestInstance(const Bound* b, const Mat34& m) : m_bound(b), m_matrix(m) { room = 1; }
    const Bound* bound(int) const override { return m_bound; }
    const Mat34& matrix() const override { return m_matrix; }
    float radius() const override { return m_bound->radius + m_bound->centroid.mag(); }

private:
    const Bound* m_bound;
    Mat34 m_matrix;
};

} // namespace mm2::phys::fixtures
