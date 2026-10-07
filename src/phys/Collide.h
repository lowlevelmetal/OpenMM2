#pragma once

#include "phys/Bound.h"

namespace mm2::phys {

// A contact between shape A and shape B. `normal` points from B towards A:
// moving A by normal * depth separates the pair.
struct Contact {
    Vec3 point;
    Vec3 normal;
    float depth = 0;
    int material = 0; // surface material of B when B is static geometry
};

inline constexpr int kMaxContacts = 8;

// Box against one convex polygon. One-sided: boxes whose centre lies behind
// the polygon plane do not collide with it (city walls are single polygons;
// continuous collision handles fast crossings).
int collideObbPolygon(const Obb& box, const Polygon& poly, Contact* out, int maxContacts);

// Box against box (separating axis test, contact points from penetrating
// vertices of each box, or the closest points of the crossing edges).
int collideObbObb(const Obb& a, const Obb& b, Contact* out, int maxContacts);

int collideSpherePolygon(const Vec3& center, float radius, const Polygon& poly, Contact* out);
int collideSphereSphere(const Vec3& ca, float ra, const Vec3& cb, float rb, Contact* out);
int collideSphereObb(const Vec3& center, float radius, const Obb& box, Contact* out);

} // namespace mm2::phys
