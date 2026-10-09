// MM2's impacts and their resolution: phImpactBase, phImpact,
// phContactMgr::CalcImpact (with contacts disabled, as dgPhysManager leaves
// them) and dgImpact, ported from the code of midtown2.exe build 3393
// (MM2Recomp). See docs/physics.md, "Collision".

#include "phys/Impact.h"

#include "phys/AgeMath.h"
#include "phys/Bound.h"
#include "phys/Collider.h"
#include "phys/Geometry.h"
#include "phys/InertialCS.h"
#include "phys/World.h"

#include <cmath>
#include <utility>

namespace mm2::phys {
namespace {

// The inverse mass matrix of a collider at a point (zero without a body).
Mat34 invMass(const Collider* c, const Vec3& position) {
    Mat34 m;
    if (c && c->ics) {
        c->ics->calcCMatrix(m, position);
    } else {
        m.m0 = {};
        m.m1 = {};
        m.m2 = {};
        m.m3 = {};
    }
    return m;
}

// A direction through an inverse mass matrix as CalcCollision transforms it
// for collider A (the original adds the matrix's m3, which GetInvMassMatrix
// leaves at 0).
Vec3 transformed(const Mat34& m, const Vec3& v) {
    return {v.z * m.m2.x + v.y * m.m1.x + m.m0.x * v.x + m.m3.x, v.z * m.m2.y + v.y * m.m1.y + m.m0.y * v.x + m.m3.y,
            v.z * m.m2.z + v.y * m.m1.z + m.m0.z * v.x + m.m3.z};
}

// The same for collider B, through the original's other helper (its own
// summation order).
Vec3 transformedB(const Mat34& m, const Vec3& v) {
    return {((m.m2.x * v.z + m.m1.x * v.y) + m.m0.x * v.x) + m.m3.x,
            ((m.m2.y * v.z + m.m0.y * v.x) + m.m1.y * v.y) + m.m3.y,
            ((m.m2.z * v.z + m.m0.z * v.x) + m.m1.z * v.y) + m.m3.z};
}

Mat34 added(const Mat34& a, const Mat34& b) {
    Mat34 r;
    r.m0 = {a.m0.x + b.m0.x, a.m0.y + b.m0.y, a.m0.z + b.m0.z};
    r.m1 = {a.m1.x + b.m1.x, a.m1.y + b.m1.y, a.m1.z + b.m1.z};
    r.m2 = {a.m2.x + b.m2.x, a.m2.y + b.m2.y, a.m2.z + b.m2.z};
    r.m3 = {a.m3.x + b.m3.x, a.m3.y + b.m3.y, a.m3.z + b.m3.z};
    return r;
}

// The Coulomb limit of phImpact::CalcCollision and dgImpact::CalcCollision:
// when the tangential part of the stopping impulse j exceeds friction times
// its normal part, the impulse is redirected along normal + friction *
// (tangent direction) with the size that stops the normal motion. The two
// originals sum the denominator in different orders (`banger`: dgImpact's).
Vec3 frictionLimited(const Vec3& j, const Vec3& normal, float friction, const Mat34& m, const Vec3& relVel,
                     bool banger = false) {
    const float jn = j.z * normal.z + j.y * normal.y + j.x * normal.x;
    const Vec3 tangent{j.x - jn * normal.x, j.y - jn * normal.y, j.z - jn * normal.z};
    const float tangentMag = std::sqrt(tangent.z * tangent.z + tangent.y * tangent.y + tangent.x * tangent.x);
    if (tangentMag <= jn * friction)
        return j;
    const float k = friction / tangentMag;
    const Vec3 t{tangent.x * k, tangent.y * k, tangent.z * k};
    const Vec3 dir{t.x + normal.x, t.y + normal.y, t.z + normal.z};
    const Vec3 mn = transformed(m, normal);
    const float denom = banger ? (dir.y * mn.y + dir.x * mn.x) + mn.z * dir.z
                               : (mn.z * dir.z + dir.y * mn.y) + dir.x * mn.x;
    if (denom < 1e-5f && -1e-5f < denom)
        return {};
    const float scale = -((normal.z * relVel.z + normal.y * relVel.y + normal.x * relVel.x) / denom);
    return {dir.x * scale, dir.y * scale, dir.z * scale};
}

} // namespace

// --- phImpactBase ------------------------------------------------------------------------

void Impact::swapColliders() {
    // phImpactBase::SwapColliders.
    if (kind > 0)
        kind = 3 - kind;
    std::swap(elementA, elementB);
    std::swap(componentA, componentB);
    std::swap(colliderA, colliderB);
    normal = -normal;
}

void Impact::reset() {
    // phImpactBase::Reset (colliders untouched).
    kind = 0;
    elementA = elementB = 0;
    componentA = componentB = 0;
    position = {};
    normal = {};
    depth = penetration = friction = elasticity = 0.0f;
}

void Impact::startMakingNewImpact(float d, const Vec3& n, const Vec3& p, Collider* a, Collider* b, const Mat34* m,
                                  int elemA, int compA, int compB) {
    // phImpactBase::StartMakingNewImpact: position and normal, moved into the
    // world by m when given.
    if (!m) {
        position = p;
        normal = n;
    } else {
        position = {((m->m2.x * p.z + m->m0.x * p.x) + m->m1.x * p.y) + m->m3.x,
                    ((p.z * m->m2.y + p.y * m->m1.y) + m->m0.y * p.x) + m->m3.y,
                    ((p.z * m->m2.z + p.y * m->m1.z) + m->m0.z * p.x) + m->m3.z};
        normal = {(m->m0.x * n.x + m->m2.x * n.z) + m->m1.x * n.y,
                  (m->m0.y * n.x + n.z * m->m2.y) + n.y * m->m1.y,
                  (m->m0.z * n.x + n.z * m->m2.z) + n.y * m->m1.z};
    }
    depth = d;
    colliderA = a;
    colliderB = b;
    componentA = compA;
    componentB = compB;
    elementA = elemA;
}

void Impact::finishMakingNewImpact(int sphereResult, int elemB, const Bound& boundA, const Bound& boundB,
                                   int elemA) {
    // phImpactBase::FinishMakingNewImpact: the feature the sphere test found
    // (0 a vertex, 1 an edge, 2 the face of the polygon) gives the impact
    // kind: the sphere (A) on a vertex of B is VertexB, on its face VertexA.
    penetration = boundA.penetration < boundB.penetration ? boundA.penetration : boundB.penetration;
    elementA = elemA;
    elementB = elemB;
    if (sphereResult == 0)
        kind = VertexB;
    else if (sphereResult == 1)
        kind = EdgeEdge;
    else if (sphereResult == 2)
        kind = VertexA;
}

void Impact::makeNewImpact(Collider* a, Collider* b, const Vec3& p, const Vec3& n, float d, const Bound& boundA,
                           const Bound& boundB, int k, int elemA, int elemB) {
    // phImpactBase::MakeNewImpact.
    kind = k;
    elementA = elemA;
    elementB = elemB;
    position = p;
    normal = n;
    depth = d;
    friction = boundB.material(0).friction * boundA.material(0).friction;
    elasticity = boundB.material(0).elasticity * boundA.material(0).elasticity;
    penetration = boundA.penetration < boundB.penetration ? boundA.penetration : boundB.penetration;
    colliderA = a;
    colliderB = b;
    componentA = -1;
    componentB = -1;
}

void Impact::cullImpactList(Impact* list, int& count, const Vec3& dir) {
    // phImpactBase::CullImpactList.
    if (count <= 0)
        return;
    const float limit = ((dir.x * dir.x + dir.y * dir.y) + dir.z * dir.z) * 0.01f;
    auto small = [&](const Impact& i) {
        const float d = (i.normal.x * dir.x + i.normal.z * dir.z) + i.normal.y * dir.y;
        return d * d <= limit;
    };
    int first = 0;
    while (small(list[first])) {
        if (++first >= count)
            return; // every normal is perpendicular: keep them all
    }
    if (first > 0) {
        for (int i = first; i < count; ++i)
            list[i - first] = list[i];
        count -= first;
    }
    for (int i = 1; i < count; ++i) {
        if (small(list[i])) {
            for (int k = i; k < count - 1; ++k)
                list[k] = list[k + 1];
            --count;
            --i;
        }
    }
}

bool Impact::addImpactSpherePlaneTest(Impact* list, int& count, const Vec3&, float) {
    // phImpactBase::AddImpactSpherePlaneTest: the impact being added is
    // list[count]. Earlier impacts of the same element whose penetration
    // midpoints lie behind each other's planes overlap it: the deeper one
    // wins.
    const Impact& added = list[count];
    const Vec3 addedNormal = added.normal;
    const float addedDepth = added.depth;
    const float half = addedDepth * 0.5f;
    const Vec3 addedMid{addedNormal.x * half + added.position.x, addedNormal.y * half + added.position.y,
                        addedNormal.z * half + added.position.z};
    const int element = added.elementA;
    const Collider* collider = added.colliderA;
    auto sideOf = [&](const Impact& i, Vec3& n) {
        if (i.colliderA == collider) {
            n = i.normal;
            return i.elementA;
        }
        n = -i.normal;
        return i.elementB;
    };
    auto removeAt = [&](int i) {
        // Shifts every later impact (the one being added included) down.
        for (int k = i; k < count; ++k)
            list[k] = list[k + 1];
        --count;
    };
    for (int i = 0; i < count; ++i) {
        Vec3 n;
        if (sideOf(list[i], n) != element)
            continue;
        const float h = list[i].depth * 0.5f;
        const Vec3 mid{h * n.x + list[i].position.x, n.y * h + list[i].position.y, n.z * h + list[i].position.z};
        if (geom::isPointBehindPlane(addedMid, mid, n, list[i].depth * 0.01f) &&
            geom::isPointBehindPlane(mid, addedMid, addedNormal, addedDepth * 0.01f)) {
            if (addedDepth <= list[i].depth)
                return false;
            removeAt(i);
            --i;
        }
    }
    for (int i = 0; i < count; ++i) {
        Vec3 n;
        if (sideOf(list[i], n) != element)
            continue;
        const float h = list[i].depth * 0.5f;
        const Vec3 mid{h * n.x + list[i].position.x, n.y * h + list[i].position.y, n.z * h + list[i].position.z};
        if (geom::isPointBehindPlane(mid, addedMid, addedNormal, addedDepth * 0.01f)) {
            removeAt(i);
            --i;
        }
    }
    return true;
}

bool Impact::addImpactShaftPlaneTest(Impact* list, int& count, const Vec3& point, float, const Vec3& axis) {
    // phImpactBase::AddImpactShaftPlaneTest: as AddImpactSpherePlaneTest for
    // a hotdog's shaft, comparing only impacts on the same side of `point`
    // along `axis`, with their normals' axial parts removed.
    const Impact& added = list[count];
    const Vec3 addedNormal = added.normal;
    const float addedDepth = added.depth;
    const float half = addedDepth * 0.5f;
    const Vec3 addedMid{addedNormal.x * half + added.position.x, addedNormal.y * half + added.position.y,
                        addedNormal.z * half + added.position.z};
    const Collider* collider = added.colliderA;
    const float addedSide = ((addedMid.y - point.y) * axis.y + (addedMid.x - point.x) * axis.x) +
                            (addedMid.z - point.z) * axis.z;
    auto radialNormal = [&](const Impact& i) {
        Vec3 n = i.colliderA == collider ? i.normal : -i.normal;
        const float along = (n.y * axis.y + n.z * axis.z) + n.x * axis.x;
        if (along != 0.0f) {
            n = {n.x - along * axis.x, n.y - along * axis.y, n.z - along * axis.z};
            const float len2 = (n.z * n.z + n.y * n.y) + n.x * n.x;
            const float scale = len2 == 0.0f ? 0.0f : 1.0f / std::sqrt(len2);
            n = {scale * n.x, scale * n.y, scale * n.z};
        }
        return n;
    };
    auto sameSide = [&](const Impact& i) {
        return 0.0f < (((i.position.y - point.y) * axis.y + (i.position.x - point.x) * axis.x) +
                       (i.position.z - point.z) * axis.z) *
                          addedSide;
    };
    auto removeAt = [&](int i) {
        for (int k = i; k < count; ++k)
            list[k] = list[k + 1];
        --count;
    };
    for (int i = 0; i < count; ++i) {
        const Vec3 n = radialNormal(list[i]);
        if (!sameSide(list[i]))
            continue;
        const float h = list[i].depth * 0.5f;
        const Vec3 mid{h * n.x + list[i].position.x, n.y * h + list[i].position.y, n.z * h + list[i].position.z};
        if (geom::isPointBehindPlane(addedMid, mid, n, list[i].depth * 0.01f)) {
            if (!geom::isPointBehindPlane(mid, addedMid, addedNormal, addedDepth * 0.01f) ||
                addedDepth <= list[i].depth)
                return false;
            removeAt(i);
            --i;
        }
    }
    for (int i = 0; i < count; ++i) {
        const Vec3 n = radialNormal(list[i]);
        if (!sameSide(list[i]))
            continue;
        const float h = list[i].depth * 0.5f;
        const Vec3 mid{h * n.x + list[i].position.x, n.y * h + list[i].position.y, n.z * h + list[i].position.z};
        if (geom::isPointBehindPlane(mid, addedMid, addedNormal, addedDepth * 0.01f)) {
            removeAt(i);
            --i;
        }
    }
    return true;
}

int Impact::impactIsInList(int a, int b, int k, const Impact* list, int count) {
    // phImpactBase::ImpactIsInList.
    for (int i = 0; i < count; ++i) {
        const Impact& im = list[i];
        if (k == 1) {
            if (im.kind == EdgeEdge && im.elementA == a && im.elementB == b)
                return i;
        } else if (k == 0) {
            if ((im.kind == VertexA && im.elementA == a && im.elementB == b) ||
                (im.kind == VertexB && im.elementB == a && im.elementA == b))
                return i;
        } else {
            if ((im.kind == VertexA && im.elementB == a && im.elementA == b) ||
                (im.kind == VertexB && im.elementA == a && im.elementB == b))
                return i;
        }
    }
    return -1;
}

// --- phImpact ----------------------------------------------------------------------------

const Material& Impact::materialOf(const Collider& collider, int component) {
    // phImpact::GetMaterial.
    const Bound& b = *collider.bound;
    if (b.type == BoundType::Terrain || b.type == BoundType::TerrainLocal || b.type == BoundType::Level)
        return b.material(component);
    return b.material(0);
}

void Impact::findFrictionAndElasticity() {
    // phImpact::FindFrictionAndElasticity.
    const Material& a = materialOf(*colliderA, componentA);
    const Material& b = materialOf(*colliderB, componentB);
    friction = b.friction * a.friction;
    const float e = b.elasticity * a.elasticity;
    const float cap = elasticityCap();
    elasticity = e < cap ? e : cap;
}

namespace {
float g_elasticityCap = kElasticityCap;
} // namespace

float elasticityCap() {
    return g_elasticityCap;
}

void setElasticityCap(float cap) {
    g_elasticityCap = cap;
}

void Impact::localVelocities(Vec3& va, Vec3& vb) const {
    // phImpact::GetLocalVelocities (the feature kinds only matter for the
    // flexible bounds' contact displacement, not for velocities).
    va = colliderA->localVelocity(position);
    vb = colliderB->localVelocity(position);
}

void Impact::calcCollision(const Vec3& relVel, float weight, Vec3& pushA, Vec3& pushB, Vec3& impulse) {
    // phImpact::CalcCollision.
    Mat34 ma, mb;
    colliderA->invMassMatrix(position, ma);
    colliderB->invMassMatrix(position, mb);
    findFrictionAndElasticity();
    Vec3 j;
    if (normal.z * relVel.z + normal.y * relVel.y + relVel.x * normal.x <= 0.01f) {
        const Mat34 m = added(ma, mb);
        j = age::solveSVD(m, {-relVel.x, -relVel.y, -relVel.z});
        j = frictionLimited(j, normal, friction, m, relVel);
        const float bounce = elasticity + 1.0f;
        j = {bounce * j.x, j.y * bounce, j.z * bounce};
    }
    const float d = depth - kPenetration;
    if (0.0f < d) {
        const Vec3 an = transformed(ma, normal);
        const float a2 = (an.z * an.z + an.y * an.y) + an.x * an.x;
        const Vec3 bn = transformedB(mb, normal);
        const float b2 = (bn.y * bn.y + bn.z * bn.z) + bn.x * bn.x;
        // Each body's share of the push follows how far a unit push moves
        // it: |Ma n| / (|Ma n| + |Mb n|).
        float shareA, shareB;
        if (b2 < a2) {
            shareA = 1.0f / (std::sqrt(b2 / a2) + 1.0f);
            shareB = 1.0f - shareA;
        } else if (a2 < b2) {
            shareB = 1.0f / (std::sqrt(a2 / b2) + 1.0f);
            shareA = 1.0f - shareB;
        } else {
            shareA = 0.5f;
            shareB = 0.5f;
        }
        const float pa = shareA * d;
        pushA = {pa * normal.x, pa * normal.y, pa * normal.z};
        const float pb = -(shareB * d);
        pushB = {normal.x * pb, normal.y * pb, normal.z * pb};
        // Nothing is pushed downwards: a downward part moves to the other
        // body when that one can move.
        if (pushA.y < 0.0f && 0.0f < b2) {
            pushB.y = pushB.y - pushA.y;
            pushA.y = 0.0f;
        } else if (pushB.y < 0.0f && 0.0f < a2) {
            pushA.y = pushA.y - pushB.y;
            pushB.y = 0.0f;
        }
    } else {
        pushA = {};
        pushB = {};
    }
    impulse = {j.x * weight, j.y * weight, j.z * weight};
}

float calcCollisionNoFriction(const InertialCS& ics, const Vec3& n, float closing, const Vec3& position) {
    // phImpact::CalcCollisionNoFriction.
    if (0.0f <= closing)
        return 0.0f;
    Mat34 m;
    ics.calcCMatrix(m, position);
    const float x = (m.m0.x * n.x + m.m2.x * n.z) + m.m1.x * n.y;
    const float y = (m.m0.y * n.x + m.m2.y * n.z) + m.m1.y * n.y;
    const float z = (m.m0.z * n.x + m.m2.z * n.z) + m.m1.z * n.y;
    return -(closing / ((x * n.x + z * n.z) + y * n.y));
}

void calcImpact(Impact& impact, float weight) {
    // phContactMgr::CalcImpact with contacts disabled: phImpact::Impact
    // hands +impulse and A's push to A, -impulse and B's push to B.
    Vec3 va, vb;
    impact.localVelocities(va, vb);
    const Vec3 rel{va.x - vb.x, va.y - vb.y, va.z - vb.z};
    Vec3 pushA, pushB, impulse;
    impact.calcCollision(rel, weight, pushA, pushB, impulse);
    impact.colliderA->impact(impact, impulse, pushA);
    impact.colliderB->impact(impact, -impulse, pushB);
}

bool calcBangerImpact(Impact& impact, float weight, float impulseLimit2) {
    // dgImpact::CalcImpact / dgImpact::CalcCollision: B is an unhit banger,
    // fixed until the impulse that stops A against it exceeds the banger's
    // limit. Then A takes the limited impulse and both share what stops
    // the remaining relative motion.
    Vec3 va, vb;
    impact.localVelocities(va, vb);
    const Vec3 relVel{va.x - vb.x, va.y - vb.y, va.z - vb.z};
    const Vec3& n = impact.normal;

    Mat34 m = invMass(impact.colliderA, impact.position);
    const Mat34 ma = m;
    Mat34 mb;
    mb.m0 = mb.m1 = mb.m2 = mb.m3 = {};
    Vec3 impulseA;
    Vec3 j;
    bool broke = false;
    impact.findFrictionAndElasticity();
    // OpenMM2: a kinematic body that moves (a network car on this machine)
    // has no ICS in its collider, so no impulse could stop it and the banger
    // would hold forever. One that may (Body::kinematicBreaksBangers: the
    // host's copies of the other players' cars) breaks the banger as a body
    // of its own mass would (Body::ics), keeps its motion and gives the
    // banger all of the relative velocity. MM2's network cars are simulated
    // vehCars.
    const Body* kinematic =
        !impact.colliderA->ics && impact.colliderA->moving ? impact.colliderA->body : nullptr;
    if (kinematic && (!kinematic->kinematicBreaksBangers || !(kinematic->ics.mass > 0.0f)))
        kinematic = nullptr;
    if (n.z * relVel.z + n.y * relVel.y + n.x * relVel.x <= 0.01f) {
        Mat34 test = ma;
        if (kinematic)
            kinematic->ics.calcCMatrix(test, impact.position);
        j = age::solveSVD(test, {-relVel.x, -relVel.y, -relVel.z});
        const float j2 = j.z * j.z + j.y * j.y + j.x * j.x;
        if (impulseLimit2 < j2) {
            const float s = std::sqrt(impulseLimit2 / j2);
            j = {j.x * s, j.y * s, j.z * s};
            // A's velocity after the limited impulse, relative to B.
            const Vec3 after = transformed(test, j);
            Vec3 remaining{after.x + relVel.x, after.y + relVel.y, after.z + relVel.z};
            impulseA = j;
            if (kinematic) {
                remaining = relVel;
                impulseA = {};
            }
            mb = invMass(impact.colliderB, impact.position);
            m = added(m, mb);
            j = age::solveSVD(m, {-remaining.x, -remaining.y, -remaining.z});
            broke = true;
        } else if (kinematic) {
            j = age::solveSVD(ma, {-relVel.x, -relVel.y, -relVel.z});
        }
        j = frictionLimited(j, n, impact.friction, m, relVel, true);
        const float bounce = impact.elasticity + 1.0f;
        j = {bounce * j.x, j.y * bounce, j.z * bounce};
    }
    Vec3 pushA, pushB;
    const float d = impact.depth - kPenetration;
    if (0.0f < d) {
        const Vec3 an = transformed(ma, n);
        const float a = age::mag(an);
        const Vec3 bn = transformedB(mb, n);
        const float b = age::mag(bn);
        float sum = b + a;
        float shareA = a, shareB = b;
        if (sum == 0.0f) {
            sum = 1.0f;
            shareA = shareB = 0.5f;
        }
        const float k = d / sum;
        pushA = n * (k * shareA);
        pushB = n * -(k * shareB);
        if (pushA.y < 0.0f) {
            pushB.y = pushB.y - pushA.y;
            pushA.y = 0.0f;
        } else if (pushB.y < 0.0f) {
            pushA.y = pushA.y - pushB.y;
            pushB.y = 0.0f;
        }
    }
    impulseA = {j.x + impulseA.x, j.y + impulseA.y, j.z + impulseA.z};
    Vec3 impulseB = -j;
    impulseA = {impulseA.x * weight, impulseA.y * weight, impulseA.z * weight};
    impulseB = {impulseB.x * weight, impulseB.y * weight, impulseB.z * weight};
    impact.colliderA->impact(impact, impulseA, pushA);
    impact.colliderB->impact(impact, impulseB, pushB);
    return broke;
}

} // namespace mm2::phys
