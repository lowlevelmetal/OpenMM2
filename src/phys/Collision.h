#pragma once

// MM2's narrow phase: the bound-pair tests that turn two placed bounds into
// impacts (phCollision::TestBoundGeneric and the per-type routines it calls),
// ported from the code of midtown2.exe build 3393. Parameter order follows
// the originals; "relPos" is the position of B minus that of A (the
// dgPhysManager hands it down), "relDisp" the relative displacement the
// sphere and hotdog tests use. Matrices place each bound in the world;
// "last" matrices are the colliders' poses at the end of the previous sample
// (Collider::copyLastMatrix). Impact and intersection buffers are caller
// owned; the functions return how many entries they wrote.

#include "phys/Bound.h"
#include "phys/Impact.h"

namespace mm2::phys {

// datTimeManager::Seconds / InvSeconds as the collision routines read them
// (several thresholds are "within 1.5 samples"); World sets them for each
// sample.
struct SampleTime {
    float seconds = 1.0f / 60.0f;
    float invSeconds = 60.0f;
};
SampleTime& sampleTime();

// Buffer sizes dgPhysManager passes (intersections, impacts).
inline constexpr int kMaxIntersections = 100;
inline constexpr int kMaxImpacts = 200;

// phCollision::TestBoundGeneric (collider version): dispatches on the two
// bound types. The impacts name their own colliders: the sphere and hotdog
// routines put the round bound on side A whichever collider it belongs to.
int testBoundGeneric(const Bound& a, Collider& ca, const Bound& b, Collider& cb, Intersection* isectsA,
                     Intersection* isectsB, Impact* impacts, int maxIsects, int maxImpacts, const Vec3& relPos);

// phCollision::GetRelDisp: relPos plus the motion of `last`'s origin
// relative to `m` over the sample, expressed through `ref`'s rotation.
Vec3 getRelDisp(const Mat34& ref, const Mat34& m, const Mat34& last, const Vec3& relPos);

// --- phBoundPolygonal (Collision of polygonal bounds, BoundPolygonal.cpp) ---

// phBoundPolygonal::TestBoundPolyPoly: separating test along relPos (MaxDot
// of A against MinDot of B), then the edges and vertex sweeps of each bound
// against the other's polygons (TestBoundPolyPolyUseDotSmall). isectsA gets
// A's edges piercing B, isectsB B's edges piercing A.
int testBoundPolyPoly(const BoundPolygonal& a, const BoundPolygonal& b, const Mat34& ma, const Mat34& lastA,
                      const Mat34& mb, const Mat34& lastB, Collider* ca, Collider* cb, Intersection* isectsA,
                      Intersection* isectsB, int max, int& countA, int& countB, const Vec3& relPos, bool sweep);
// phBoundPolygonal::TestBoundPolyPolyUseDot / UseDotSmall: `self`'s
// segments (vertex sweeps when `sweep`, then edges) against `other`'s
// polygons, skipping vertices whose projection on `dir` (self's space) is
// below `threshold`. otherLast may be null (no relative sweep). Returns the
// number of intersections written to out (also stored in count).
int testBoundPolyPolyUseDot(const BoundPolygonal& self, const BoundPolygonal& other, Collider* otherCollider,
                            const Mat34& m, const Mat34& last, const Mat34* otherM, const Mat34* otherLast,
                            Intersection* out, int max, int& count, float threshold, const Vec3* dir, bool sweep);
int testBoundPolyPolyUseDotSmall(const BoundPolygonal& self, const BoundPolygonal& other, Collider* otherCollider,
                                 const Mat34& m, const Mat34& last, const Mat34& otherM, const Mat34& otherLast,
                                 Intersection* out, int& count, float threshold, const Vec3& dir, bool sweep);
// phBoundPolygonal::FindImpactsPolyToPoly: adds the interior edges of the
// pierced polygons, moves both intersection lists to the world and builds
// the impacts (FindImpacts).
int findImpactsPolyToPoly(const BoundPolygonal& a, const BoundPolygonal& b, const Mat34& ma, const Mat34& lastA,
                          const Mat34& mb, const Mat34& lastB, Collider* ca, Collider* cb, Intersection* isectsA,
                          Intersection* isectsB, Impact* impacts, int maxIsects, int maxImpacts, int& countA,
                          int& countB);
// phBoundPolygonal::FindImpacts: impacts from vertex sweeps (DoEndPtSearch),
// edge-edge pairs (CheckSaveEdgeEdge) and edges through faces
// (GetCollideEdgePoly), then RetryVertPolyCollide. lastB/mb may be null
// (the level). The other bound (B) may be any type with polygons.
int findImpacts(const BoundPolygonal& a, const Bound& b, const Mat34* ma, const Mat34* lastA, const Mat34* mb,
                const Mat34* lastB, Collider* ca, Collider* cb, Intersection* isectsA, Intersection* isectsB,
                int countA, int countB, Impact* impacts, int maxImpacts);
// phBoundPolygonal::FindImpactsSphereToPoly.
int findImpactsSphereToPoly(const BoundPolygonal& poly, const BoundSphere& sphere, const Mat34& sphereM,
                            const Mat34& polyM, Collider* sphereCollider, Collider* polyCollider, Impact* impacts,
                            int max, const Vec3& relPos, const Vec3& relDisp);

// --- phBoundBox (BoundBox.cpp) ---

// phBoundBox::FindImpactSphereToBox.
int findImpactSphereToBox(const BoundBox& box, const BoundSphere& sphere, const Mat34& sphereM, const Mat34& boxM,
                          Collider* sphereCollider, Collider* boxCollider, Impact* impacts, const Vec3& relPos,
                          const Vec3& relDisp);
// phBoundBox::FindImpactsBoxToBox (FindImpactsBoxToBoxOffset when either
// box has a non-zero centroid).
int findImpactsBoxToBox(const BoundBox& a, const BoundBox& b, const Mat34& ma, const Mat34& lastA, const Mat34& mb,
                        const Mat34& lastB, Collider* ca, Collider* cb, Impact* impacts, int max,
                        const Vec3& relPos);

// --- phBoundSphere / phBoundHotdog (BoundSphere.cpp, BoundHotdog.cpp) ---

// phBoundSphere::FindImpactSphereToSphere.
bool findImpactSphereToSphere(const BoundSphere& a, const BoundSphere& b, const Mat34& ma, const Mat34& mb,
                              Collider* ca, Collider* cb, Impact& impact, const Vec3& relPos);
// phBoundHotdog::FindImpactSphereToHotdog.
bool findImpactSphereToHotdog(const BoundHotdog& hotdog, const BoundSphere& sphere, const Mat34& sphereM,
                              const Mat34& hotdogM, Collider* sphereCollider, Collider* hotdogCollider,
                              Impact& impact, const Vec3& relPos);
// phBoundHotdog::FindImpactsHotdogToPoly.
int findImpactsHotdogToPoly(const BoundHotdog& hotdog, const BoundPolygonal& poly, const Mat34& hotdogM,
                            const Mat34& polyM, Collider* hotdogCollider, Collider* polyCollider, Impact* impacts,
                            int max, const Vec3& relPos, const Vec3& relDisp);
// phBoundHotdog::FindImpactsHotdogToHotdog.
int findImpactsHotdogToHotdog(const BoundHotdog& a, const BoundHotdog& b, const Mat34& ma, const Mat34& mb,
                              Collider* ca, Collider* cb, Impact* impacts, int max);

// --- phBoundTerrain / phBoundTerrainLocal (BoundTerrain.cpp) ---

// phBoundTerrain::TestBoundTerrainPoly: the terrain's edges against the
// polygonal bound (isectsTerrain, TestBoundTerrainEdgesVsPoly) and the
// bound's segments against the terrain's polygons through the grid
// (isectsPoly, TestBoundPolyTerrain, or TestBoundPolyPolyUseDot for bounds
// with 31 or more vertices).
int testBoundTerrainPoly(const BoundTerrain& terrain, const BoundPolygonal& poly, const Mat34& polyM,
                         const Mat34& polyLast, Collider* polyCollider, Collider* terrainCollider,
                         Intersection* isectsTerrain, Intersection* isectsPoly, int max, int& countTerrain,
                         int& countPoly, const Vec3& relPos, bool sweep);
// phBoundTerrainLocal::TestBoundTerrainPoly: the same with the terrain
// placed by terrainM.
int testBoundTerrainLocalPoly(const BoundTerrainLocal& terrain, const BoundPolygonal& poly, const Mat34& terrainM,
                              const Mat34& polyM, const Mat34& polyLast, Collider* polyCollider,
                              Collider* terrainCollider, Intersection* isectsTerrain, Intersection* isectsPoly,
                              int max, int& countTerrain, int& countPoly, const Vec3& relPos, bool sweep);
// phBoundTerrain::FindImpactsSphereToTerrain.
int findImpactsSphereToTerrain(const BoundTerrain& terrain, const BoundSphere& sphere, const Mat34& sphereM,
                               const Mat34& terrainM, Collider* sphereCollider, Collider* terrainCollider,
                               Impact* impacts, int max, const Vec3& relPos, const Vec3& relDisp);
// phBoundTerrain::FindImpactsHotdogToTerrain /
// phBoundTerrainLocal::FindImpactsHotdogToTerrainLocal.
int findImpactsHotdogToTerrain(const BoundTerrain& terrain, const BoundHotdog& hotdog, const Mat34& hotdogM,
                               const Mat34& terrainM, Collider* hotdogCollider, Collider* terrainCollider,
                               Impact* impacts, int max, const Vec3& relPos, const Vec3& relDisp);
int findImpactsHotdogToTerrainLocal(const BoundTerrainLocal& terrain, const BoundHotdog& hotdog,
                                    const Mat34& hotdogM, const Mat34& terrainM, Collider* hotdogCollider,
                                    Collider* terrainCollider, Impact* impacts, int max, const Vec3& relPos,
                                    const Vec3& relDisp);

// --- Intersection lists (BoundPolygonal.cpp) ---

// The list transform of FindImpactsPolyToPoly: segment ends by m, position
// and polygon normal too, except for interior edges, whose edge normal is
// rotated instead.
void toWorldCoords(Intersection* isects, int count, const Mat34& m);

} // namespace mm2::phys
