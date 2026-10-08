// phCollision::TestBoundGeneric (the collider version dgPhysManager calls)
// and phCollision::GetRelDisp, ported from the code of midtown2.exe build
// 3393 (MM2Recomp). See docs/physics.md, "Collision".

#include "phys/Collision.h"

#include "phys/Collider.h"

namespace mm2::phys {

SampleTime& sampleTime() {
    static SampleTime time;
    return time;
}

Vec3 getRelDisp(const Mat34& ref, const Mat34& m, const Mat34& last, const Vec3& relPos) {
    // phCollision::GetRelDisp: the motion of m's origin since `last`'s, taken
    // into m's frame and back out through ref's rotation, plus relPos.
    const float dx = m.m3.x - last.m3.x;
    const float dy = m.m3.y - last.m3.y;
    const float dz = m.m3.z - last.m3.z;
    const float lx = (m.m0.y * dy + dz * m.m0.z) + m.m0.x * dx;
    const float ly = (m.m1.y * dy + dz * m.m1.z) + m.m1.x * dx;
    const float lz = (m.m2.y * dy + dz * m.m2.z) + m.m2.x * dx;
    Vec3 out{(lz * ref.m2.x + ly * ref.m1.x) + lx * ref.m0.x, (lz * ref.m2.y + lx * ref.m0.y) + ly * ref.m1.y,
             (lz * ref.m2.z + lx * ref.m0.z) + ly * ref.m1.z};
    out = {relPos.x + out.x, relPos.y + out.y, relPos.z + out.z};
    return out;
}

int testBoundGeneric(const Bound& a, Collider& ca, const Bound& b, Collider& cb, Intersection* isectsA,
                     Intersection* isectsB, Impact* impacts, int maxIsects, int maxImpacts, const Vec3& relPos) {
    // phCollision::TestBoundGeneric. Each collider's matrix at the start of
    // the sample includes its last push when the other collider was the one
    // that pushed it hardest (CopyLastMatrix).
    const Mat34 lastA = ca.copyLastMatrix(cb.key());
    const Mat34 lastB = cb.copyLastMatrix(ca.key());
    const Mat34& ma = *ca.matrix;
    const Mat34& mb = *cb.matrix;
    const Vec3 neg = -relPos;
    // Vertex sweeps are skipped only when neither collider moved noticeably.
    const bool sweep = !ca.barelyMoved || !cb.barelyMoved;

    auto polyPoly = [&](const BoundPolygonal& pa, const BoundPolygonal& pb) {
        int countA = 0, countB = 0;
        if (testBoundPolyPoly(pa, pb, ma, lastA, mb, lastB, &ca, &cb, isectsA, isectsB, maxIsects, countA, countB,
                              relPos, sweep) == 0)
            return 0;
        return findImpactsPolyToPoly(pa, pb, ma, lastA, mb, lastB, &ca, &cb, isectsA, isectsB, impacts, maxIsects,
                                     maxImpacts, countA, countB);
    };
    auto polyTerrain = [&](const BoundPolygonal& pa, const BoundTerrain& terrain) {
        int countTerrain = 0, countPoly = 0;
        int n;
        if (b.type == BoundType::TerrainLocal)
            n = testBoundTerrainLocalPoly(static_cast<const BoundTerrainLocal&>(terrain), pa, mb, ma, lastA, &ca,
                                          &cb, isectsA, isectsB, maxIsects, countTerrain, countPoly, neg, true);
        else
            n = testBoundTerrainPoly(terrain, pa, ma, lastA, &ca, &cb, isectsA, isectsB, maxIsects, countTerrain,
                                     countPoly, neg, true);
        if (n == 0)
            return 0;
        // The polygonal bound's segments are the "A" list of the impact
        // search, the terrain's edges the "B" list.
        return findImpactsPolyToPoly(pa, terrain, ma, lastA, mb, lastB, &ca, &cb, isectsB, isectsA, impacts,
                                     maxIsects, maxImpacts, countPoly, countTerrain);
    };
    auto polyHotdog = [&](const BoundPolygonal& pa, const BoundHotdog& hb) {
        const Vec3 disp = getRelDisp(mb, lastB, lastA, neg);
        return findImpactsHotdogToPoly(hb, pa, mb, ma, &cb, &ca, impacts, maxImpacts, neg, disp);
    };

    switch (a.type) {
    case BoundType::Sphere: {
        const auto& sa = static_cast<const BoundSphere&>(a);
        switch (b.type) {
        case BoundType::Sphere:
            return findImpactSphereToSphere(sa, static_cast<const BoundSphere&>(b), ma, mb, &ca, &cb, impacts[0],
                                            relPos)
                       ? 1
                       : 0;
        case BoundType::Geometry: {
            const Vec3 disp = getRelDisp(mb, lastB, lastA, neg);
            return findImpactsSphereToPoly(static_cast<const BoundPolygonal&>(b), sa, ma, mb, &ca, &cb, impacts,
                                           maxImpacts, neg, disp);
        }
        case BoundType::Box: {
            const Vec3 disp = getRelDisp(mb, lastB, lastA, neg);
            return findImpactSphereToBox(static_cast<const BoundBox&>(b), sa, ma, mb, &ca, &cb, impacts, neg, disp);
        }
        case BoundType::Terrain: {
            const Vec3 disp{ma.m3.x - lastA.m3.x, ma.m3.y - lastA.m3.y, ma.m3.z - lastA.m3.z};
            return findImpactsSphereToTerrain(static_cast<const BoundTerrain&>(b), sa, ma, mb, &ca, &cb, impacts,
                                              maxImpacts, neg, disp);
        }
        case BoundType::TerrainLocal: {
            // Both vectors go into the terrain's space.
            const Vec3 d{(lastB.m3.x - ma.m3.x) + neg.x, (lastB.m3.y - ma.m3.y) + neg.y,
                         (lastB.m3.z - ma.m3.z) + neg.z};
            const Vec3 localPos{(neg.z * mb.m0.z + neg.y * mb.m0.y) + neg.x * mb.m0.x,
                                (neg.x * mb.m1.x + neg.z * mb.m1.z) + neg.y * mb.m1.y,
                                (neg.x * mb.m2.x + neg.z * mb.m2.z) + neg.y * mb.m2.y};
            const Vec3 localDisp{(d.z * mb.m0.z + d.y * mb.m0.y) + d.x * mb.m0.x,
                                 (d.z * mb.m1.z + d.y * mb.m1.y) + d.x * mb.m1.x,
                                 (d.z * mb.m2.z + d.y * mb.m2.y) + d.x * mb.m2.x};
            return findImpactsSphereToTerrain(static_cast<const BoundTerrain&>(b), sa, ma, mb, &ca, &cb, impacts,
                                              maxImpacts, localPos, localDisp);
        }
        case BoundType::Hotdog:
            return findImpactSphereToHotdog(static_cast<const BoundHotdog&>(b), sa, ma, mb, &ca, &cb, impacts[0],
                                            neg)
                       ? 1
                       : 0;
        default:
            return 0;
        }
    }
    case BoundType::Geometry:
    case BoundType::Box: {
        const auto& pa = static_cast<const BoundPolygonal&>(a);
        switch (b.type) {
        case BoundType::Sphere:
            if (a.type == BoundType::Geometry) {
                const Vec3 disp = getRelDisp(ma, lastA, lastB, relPos);
                return findImpactsSphereToPoly(pa, static_cast<const BoundSphere&>(b), mb, ma, &cb, &ca, impacts,
                                               maxImpacts, relPos, disp);
            } else {
                const Vec3 disp = getRelDisp(ma, lastA, lastB, relPos);
                return findImpactSphereToBox(static_cast<const BoundBox&>(a), static_cast<const BoundSphere&>(b), mb,
                                             ma, &cb, &ca, impacts, relPos, disp);
            }
        case BoundType::Geometry:
            return polyPoly(pa, static_cast<const BoundPolygonal&>(b));
        case BoundType::Box:
            if (a.type == BoundType::Box)
                return findImpactsBoxToBox(static_cast<const BoundBox&>(a), static_cast<const BoundBox&>(b), ma,
                                           lastA, mb, lastB, &ca, &cb, impacts, maxImpacts, relPos);
            return polyPoly(pa, static_cast<const BoundPolygonal&>(b));
        case BoundType::Terrain:
        case BoundType::TerrainLocal:
            return polyTerrain(pa, static_cast<const BoundTerrain&>(b));
        case BoundType::Hotdog:
            return polyHotdog(pa, static_cast<const BoundHotdog&>(b));
        default:
            return 0;
        }
    }
    case BoundType::Hotdog: {
        const auto& ha = static_cast<const BoundHotdog&>(a);
        switch (b.type) {
        case BoundType::Sphere:
            return findImpactSphereToHotdog(ha, static_cast<const BoundSphere&>(b), mb, ma, &cb, &ca, impacts[0],
                                            relPos)
                       ? 1
                       : 0;
        case BoundType::Geometry:
        case BoundType::Box: {
            const Vec3 disp = getRelDisp(ma, lastA, lastB, relPos);
            return findImpactsHotdogToPoly(ha, static_cast<const BoundPolygonal&>(b), ma, mb, &ca, &cb, impacts,
                                           maxImpacts, relPos, disp);
        }
        case BoundType::Terrain: {
            const Vec3 disp{ma.m3.x - lastA.m3.x, ma.m3.y - lastA.m3.y, ma.m3.z - lastA.m3.z};
            return findImpactsHotdogToTerrain(static_cast<const BoundTerrain&>(b), ha, ma, mb, &ca, &cb, impacts,
                                              maxImpacts, neg, disp);
        }
        case BoundType::TerrainLocal: {
            const Vec3 d{(lastB.m3.x - ma.m3.x) + neg.x, (lastB.m3.y - ma.m3.y) + neg.y,
                         (lastB.m3.z - ma.m3.z) + neg.z};
            return findImpactsHotdogToTerrainLocal(static_cast<const BoundTerrainLocal&>(b), ha, ma, mb, &ca, &cb,
                                                   impacts, maxImpacts, neg, d);
        }
        case BoundType::Hotdog:
            return findImpactsHotdogToHotdog(ha, static_cast<const BoundHotdog&>(b), ma, mb, &ca, &cb, impacts,
                                             maxImpacts);
        default:
            return 0;
        }
    }
    default:
        // Terrain and level bounds are only ever B (dgPhysManager tests
        // movers against them), force spheres go through TestBoundForce.
        return 0;
    }
}

} // namespace mm2::phys
