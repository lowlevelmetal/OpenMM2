#include "phys/Collide.h"

#include <algorithm>

namespace mm2::phys {
namespace {

// Closest points between segments p1-q1 and p2-q2.
void closestSegmentSegment(const Vec3& p1, const Vec3& q1, const Vec3& p2, const Vec3& q2, Vec3& c1,
                           Vec3& c2) {
    const Vec3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
    const float a = d1.dot(d1), e = d2.dot(d2), f = d2.dot(r);
    float s = 0, t = 0;
    if (a <= 1e-12f && e <= 1e-12f) {
        c1 = p1;
        c2 = p2;
        return;
    }
    if (a <= 1e-12f) {
        t = clampf(f / e, 0, 1);
    } else {
        const float c = d1.dot(r);
        if (e <= 1e-12f) {
            s = clampf(-c / a, 0, 1);
        } else {
            const float b = d1.dot(d2);
            const float denom = a * e - b * b;
            s = denom != 0 ? clampf((b * f - c * e) / denom, 0, 1) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0) {
                t = 0;
                s = clampf(-c / a, 0, 1);
            } else if (t > 1) {
                t = 1;
                s = clampf((b - c) / a, 0, 1);
            }
        }
    }
    c1 = p1 + d1 * s;
    c2 = p2 + d2 * t;
}

bool insideObb(const Obb& b, const Vec3& p, float slack) {
    const Vec3 r = p - b.center;
    return std::abs(r.dot(b.axis[0])) <= b.half.x + slack && std::abs(r.dot(b.axis[1])) <= b.half.y + slack &&
           std::abs(r.dot(b.axis[2])) <= b.half.z + slack;
}

// The box edge parallel to axis `i` that is extreme in direction `dir`.
void supportEdge(const Obb& b, int i, const Vec3& dir, Vec3& e0, Vec3& e1) {
    Vec3 mid = b.center;
    for (int k = 0; k < 3; ++k) {
        if (k == i)
            continue;
        const float h = k == 0 ? b.half.x : (k == 1 ? b.half.y : b.half.z);
        mid += b.axis[k] * (b.axis[k].dot(dir) >= 0 ? h : -h);
    }
    const float hi = i == 0 ? b.half.x : (i == 1 ? b.half.y : b.half.z);
    e0 = mid - b.axis[i] * hi;
    e1 = mid + b.axis[i] * hi;
}

struct Axis {
    Vec3 dir; // unit, pointing from B towards A
    float overlap = 1e30f;
    int kind = -1; // 0 = B face / poly normal, 1 = A face, 2 = edge-edge
    int ia = -1, ib = -1;
};

} // namespace

int collideObbPolygon(const Obb& box, const Polygon& poly, Contact* out, int maxContacts) {
    const float centerDist = poly.normal.dot(box.center) - poly.d;
    if (centerDist < 0.0f)
        return 0; // one-sided

    Axis best;
    auto test = [&](Vec3 axis, int kind, int ia, int ib) -> bool {
        const float len = axis.mag();
        if (len < 1e-6f)
            return true;
        axis = axis * (1.0f / len);
        const float bc = axis.dot(box.center);
        const float br = box.projectRadius(axis);
        float pmin = 1e30f, pmax = -1e30f;
        for (int i = 0; i < poly.count; ++i) {
            const float p = axis.dot(poly.v[static_cast<std::size_t>(i)]);
            pmin = std::min(pmin, p);
            pmax = std::max(pmax, p);
        }
        const float o1 = (bc + br) - pmin; // push box towards -axis
        const float o2 = pmax - (bc - br); // push box towards +axis
        if (o1 < 0 || o2 < 0)
            return false;
        float overlap;
        Vec3 dir;
        if (kind == 0) {
            overlap = o2; // only ever push out of the front face
            dir = axis;
        } else if (o2 < o1) {
            overlap = o2;
            dir = axis;
        } else {
            overlap = o1;
            dir = -axis;
        }
        // Prefer face axes over edge axes unless clearly better.
        const float bias = kind == 2 ? 1.05f : (kind == 1 ? 1.02f : 1.0f);
        if (overlap * bias + (kind == 0 ? 0.0f : 1e-3f) < best.overlap) {
            best.overlap = overlap * bias + (kind == 0 ? 0.0f : 1e-3f);
            best.dir = dir;
            best.kind = kind;
            best.ia = ia;
            best.ib = ib;
        }
        return true;
    };

    if (!test(poly.normal, 0, -1, -1))
        return 0;
    for (int i = 0; i < 3; ++i)
        if (!test(box.axis[i], 1, i, -1))
            return 0;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < poly.count; ++j) {
            const Vec3 e =
                poly.v[static_cast<std::size_t>((j + 1) % poly.count)] - poly.v[static_cast<std::size_t>(j)];
            if (!test(box.axis[i].cross(e), 2, i, j))
                return 0;
        }
    if (best.kind < 0)
        return 0;

    int n = 0;
    const Vec3& dir = best.dir;
    if (best.kind == 0) {
        // Box corners below the polygon's plane and over its face.
        for (int c = 0; c < 8 && n < maxContacts; ++c) {
            const Vec3 p = box.corner(c);
            const float s = poly.normal.dot(p) - poly.d;
            if (s < 0 && poly.containsProjected(p, 0.02f)) {
                out[n++] = {p, poly.normal, -s, poly.material};
            }
        }
    }
    if (n == 0 && best.kind != 2) {
        // Polygon vertices inside the box.
        const float boxMin = dir.dot(box.center) - box.projectRadius(dir);
        for (int i = 0; i < poly.count && n < maxContacts; ++i) {
            const Vec3& q = poly.v[static_cast<std::size_t>(i)];
            if (insideObb(box, q, 0.01f)) {
                const float depth = std::min(dir.dot(q) - boxMin, best.overlap);
                if (depth > 0)
                    out[n++] = {q, dir, depth, poly.material};
            }
        }
    }
    if (n == 0) {
        // Edge-edge (or a straddling configuration): one contact between the
        // box's support edge and the nearest polygon edge.
        const int ai = best.ia >= 0 ? best.ia : 0;
        Vec3 e0, e1;
        supportEdge(box, ai, -dir, e0, e1);
        Vec3 bestC1 = box.center, bestC2 = poly.closestPoint(box.center);
        float bestD2 = 1e30f;
        for (int j = 0; j < poly.count; ++j) {
            if (best.kind == 2 && j != best.ib)
                continue;
            Vec3 c1, c2;
            closestSegmentSegment(e0, e1, poly.v[static_cast<std::size_t>(j)],
                                  poly.v[static_cast<std::size_t>((j + 1) % poly.count)], c1, c2);
            const float d2 = c1.dist2(c2);
            if (d2 < bestD2) {
                bestD2 = d2;
                bestC1 = c1;
                bestC2 = c2;
            }
        }
        out[n++] = {(bestC1 + bestC2) * 0.5f, dir, best.overlap, poly.material};
    }
    return n;
}

int collideObbObb(const Obb& a, const Obb& b, Contact* out, int maxContacts) {
    Axis best;
    const Vec3 d = a.center - b.center;
    auto test = [&](Vec3 axis, int kind, int ia, int ib) -> bool {
        const float len = axis.mag();
        if (len < 1e-6f)
            return true;
        axis = axis * (1.0f / len);
        const float dist = d.dot(axis);
        const float overlap = a.projectRadius(axis) + b.projectRadius(axis) - std::abs(dist);
        if (overlap < 0)
            return false;
        const float bias = kind == 2 ? 1.05f : 1.0f;
        if (overlap * bias < best.overlap) {
            best.overlap = overlap * bias;
            best.dir = dist >= 0 ? axis : -axis;
            best.kind = kind;
            best.ia = ia;
            best.ib = ib;
        }
        return true;
    };
    for (int i = 0; i < 3; ++i)
        if (!test(b.axis[i], 0, -1, i))
            return 0;
    for (int i = 0; i < 3; ++i)
        if (!test(a.axis[i], 1, i, -1))
            return 0;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            if (!test(a.axis[i].cross(b.axis[j]), 2, i, j))
                return 0;
    if (best.kind < 0)
        return 0;
    const Vec3& dir = best.dir;
    const float overlap = best.kind == 2 ? best.overlap / 1.05f : best.overlap;

    int n = 0;
    if (best.kind != 2) {
        const float bMax = dir.dot(b.center) + b.projectRadius(dir);
        const float aMin = dir.dot(a.center) - a.projectRadius(dir);
        for (int c = 0; c < 8 && n < maxContacts; ++c) {
            const Vec3 p = a.corner(c);
            if (insideObb(b, p, 0.01f)) {
                const float depth = std::min(bMax - dir.dot(p), overlap);
                if (depth > 0)
                    out[n++] = {p, dir, depth, 0};
            }
        }
        for (int c = 0; c < 8 && n < maxContacts; ++c) {
            const Vec3 p = b.corner(c);
            if (insideObb(a, p, 0.01f)) {
                const float depth = std::min(dir.dot(p) - aMin, overlap);
                if (depth > 0)
                    out[n++] = {p, dir, depth, 0};
            }
        }
    }
    if (n == 0) {
        Vec3 a0, a1, b0, b1;
        const int ia = best.ia >= 0 ? best.ia : 0;
        const int ib = best.ib >= 0 ? best.ib : 0;
        supportEdge(a, ia, -dir, a0, a1);
        supportEdge(b, ib, dir, b0, b1);
        Vec3 c1, c2;
        closestSegmentSegment(a0, a1, b0, b1, c1, c2);
        out[n++] = {(c1 + c2) * 0.5f, dir, overlap, 0};
    }
    return n;
}

int collideSpherePolygon(const Vec3& center, float radius, const Polygon& poly, Contact* out) {
    const float s = poly.normal.dot(center) - poly.d;
    if (s < 0 || s > radius)
        return 0;
    const Vec3 q = poly.closestPoint(center);
    const Vec3 delta = center - q;
    const float dist2 = delta.mag2();
    if (dist2 > radius * radius)
        return 0;
    const float dist = std::sqrt(dist2);
    const Vec3 n = dist > 1e-6f ? delta * (1.0f / dist) : poly.normal;
    out[0] = {q, n, radius - dist, poly.material};
    return 1;
}

int collideSphereSphere(const Vec3& ca, float ra, const Vec3& cb, float rb, Contact* out) {
    const Vec3 delta = ca - cb;
    const float r = ra + rb;
    const float dist2 = delta.mag2();
    if (dist2 > r * r)
        return 0;
    const float dist = std::sqrt(dist2);
    const Vec3 n = dist > 1e-6f ? delta * (1.0f / dist) : Vec3{0, 1, 0};
    out[0] = {cb + n * rb, n, r - dist, 0};
    return 1;
}

int collideSphereObb(const Vec3& center, float radius, const Obb& box, Contact* out) {
    const Vec3 r = center - box.center;
    const float h[3] = {box.half.x, box.half.y, box.half.z};
    float local[3];
    bool inside = true;
    Vec3 q = box.center;
    for (int i = 0; i < 3; ++i) {
        local[i] = r.dot(box.axis[i]);
        const float c = clampf(local[i], -h[i], h[i]);
        if (c != local[i])
            inside = false;
        q += box.axis[i] * c;
    }
    if (inside) {
        int axis = 0;
        float bestPen = 1e30f;
        for (int i = 0; i < 3; ++i) {
            const float pen = h[i] - std::abs(local[i]);
            if (pen < bestPen) {
                bestPen = pen;
                axis = i;
            }
        }
        const Vec3 n = box.axis[axis] * (local[axis] >= 0 ? 1.0f : -1.0f);
        out[0] = {center - n * radius, n, bestPen + radius, 0};
        return 1;
    }
    const Vec3 delta = center - q;
    const float dist2 = delta.mag2();
    if (dist2 > radius * radius)
        return 0;
    const float dist = std::sqrt(dist2);
    const Vec3 n = dist > 1e-6f ? delta * (1.0f / dist) : Vec3{0, 1, 0};
    out[0] = {q, n, radius - dist, 0};
    return 1;
}

} // namespace mm2::phys
