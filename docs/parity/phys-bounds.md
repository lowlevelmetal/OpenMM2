# Parity audit: phys-bounds

Audited against MM2Recomp (midtown2.exe build 3393) on 2026-10-07.

Summary: 159 functions (a row may group small helpers or overloads);
verified 98, fixed 35, deviation 13, inferred 6, open 0, openmm2 7.
Missing: 1 open, 1 deviation, the rest unreached in midtown2.exe.

Scope: `src/asset/Bound.{h,cpp}` (F), `src/phys/Bound.{h,cpp}`,
`BoundBox.cpp`, `BoundCollision.cpp`, `BoundHotdog.cpp`,
`BoundPolygonal.cpp`, `BoundSphere.cpp`, `BoundTerrain.cpp` (P), and
`docs/formats/bnd.md`. Every float-heavy routine was compared with the x87
disassembly, not only the Ghidra listing: the listing prints x87 sums in a
different order from the code (and in a few places misprints a comparison),
and most "fixed" rows below are sums put back in the code's order. Such a fix
changes only rounding; the rows say when a fix changes behaviour.

Conventions used in the notes: "sum order" is the order the original adds
the three products of a dot product or of one component of a matrix
product. "Guard" is a bounds check OpenMM2 adds where the original writes
past a buffer or reads stale memory; the result is identical whenever the
original is well defined.

## src/asset/Bound.h, src/asset/Bound.cpp (F)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `BoundMaterial` defaults | `phMaterial::phMaterial` | verified | Elasticity 0.1, friction 0.5, the values a material starts from before its keys are read. |
| `BoundPolygon::isQuad`, `vertexCount` | `phPolygon` (fourth index 0 = triangle) | verified | `CalculateNormal`, `ComputeEdgeNormalCross` and the segment tests all test the fourth index. |
| `BoundGeometry::bounds` | — | openmm2 | Box for the tools. |
| `Tokens` | `datAsciiTokenizer` | inferred | Whitespace-separated tokens; no retail bound file has comments. |
| `parseBnd` | `phBoundGeometry::Load`, `lvlMaterial::Load` | verified | Same result for all 525 retail files. The game reads the header tokens in a fixed order and requires version 1.01; the parser accepts any order and does not check the version (every retail file is 1.01 with the game's order). The optional `drag:`..`ptxthreshold:` material keys are skipped (no bound file has them). Out-of-range indices fail instead of being undefined. |
| `parseBbnd` | `phBoundGeometry::LoadBinary`, `phMaterial::LoadBinary` | verified | Layout (u8 version, three u32 counts, vertices, 104-byte materials, 10-byte polygons). The game does not check the version byte or the size; the parser rejects both (all 324 retail files are version 1 and exact). |
| `parseTer` | `phBoundTerrain::Load` | verified | Field order and sizes match the reads. The game requires version 1.1 and the geometry's polygon count; the count is checked in `makeTerrainBound`, the version nowhere (every retail file is 1.1, see Missing). |
| `TerrainBound::sectionIndex`, `sectionList` | the section tables `phBoundTerrain::Load` reads | inferred | File-level helpers for the tools; the game's queries (`BoundTerrain.cpp`) index sections by x and z only. |

## src/phys/Bound.h, src/phys/Bound.cpp (P)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `BoundType` | the type argument of `phBound::phBound` | verified | Sphere 0, geometry 1, box 2, terrain 4, local terrain 5, hotdog 6, level (`sdlCommon`) 7. |
| `BoundType::ForceSphere` | — | inferred | 3 is the only unused number; no such class in build 3393. |
| `kPenetration`, `kBarelyMovedDistance` | `phBoundCollision::SetPenetration`, `phContact::DisableContacts` | verified | Start at 0.035 / 0.0105 (0.3 of it); `dgPhysManager`'s constructor calls `DisableContacts`, which sets 0 for the session. |
| `Segment::calculateInfo` | `lvlSegment::CalculateInfo` | fixed | Squared length summed z, y, x (was x, y, z). |
| `IntersectionPoint::transform` | `phIntersectionPoint::Transform` | fixed | Position x/y summed x, z, y then the origin; z summed z, y, x; the normal the same without the origin. |
| `Intersection`, `IntersectionPoint` | `phIntersection`, `phIntersectionPoint` | verified | Fields the collision code reads and writes. |
| `Polygon::initTriangle`, `initQuad` | `phPolygon::InitTriangle`, `InitQuad` | verified | |
| `Polygon::calculateNormal` | `phPolygon::CalculateNormal` | fixed | Squared length summed y, x, z; the quad's second triangle y, z, x. The original keeps the material index in the low byte of the area float; OpenMM2 keeps it in its own field (nothing reads the area). |
| `Polygon::computeEdgeNormalCross` | `phPolygon::ComputeEdgeNormalCross` | fixed | Squared length summed x, y, z (was z, y, x). Side order and normal × side verified. |
| `Polygon::testSegmentDirected` | `phPolygon::TestSegmentDirected` | fixed | Plane distances summed z, x, y; edge tests through `Vector4::Dot3` (z, y, x). Branches, t limit and outputs verified. |
| `Polygon::testSegmentUndirected` | `phPolygon::TestSegmentUndirected` | fixed | Plane distances summed y, x, z; edge dots z, y, x. Reject window `rejectFrom < t < rejectTo` and depth/bInside choice verified. |
| `Polygon::detectSegmentDirected`, `detectSegmentUndirected` | `phPolygon::DetectSegmentDirected`, `DetectSegmentUndirected` | fixed | Edge dots z, y, x. |
| `segEdgeCheckDirected`, `segEdgeCheckUndirected` | `phPolygon::SegEdgeCheckDirected`, `SegEdgeCheckUndirected` | fixed | Side and edge-normal dots z, y, x. |
| `outsideDirected`, `outsideUndirected` | the inline edge tests of the four routines above | fixed | Same dot order. |
| `cross`, `signBit`, `dot3`, `at`, `sqrtf32` | `Vector4::Cross`, `Vector4::Dot3` | openmm2 | Helpers; `dot3` is `Vector4::Dot3`'s order. |
| `backupDispByPenetration` | `phBoundPolygonal::BackupDispByPenetration` | fixed | Squared length summed z, y, x (no effect while the penetration is 0). |
| `backupAByPenetration` | `phBoundPolygonal::BackupAbyPenetration` | verified | The original has its own body and reads the pair's penetration from a global set by the poly-poly tests; it is 0 in a race, as `kPenetration`. |
| `defaultBoundMaterial` | `lvlMaterial::lvlMaterial` | fixed | Elasticity 0.5, friction 1, width 1, thresholds 0.25 / 0.5, name "default"; now also phMaterial's sound index −1 (was 0). |
| `embeddedBoundMaterial` | `phMaterial::phMaterial` | fixed | New: the material a plain `phBoundSphere` / `phBoundHotdog` embeds (elasticity 0.1, friction 0.5). |
| `Bound::Bound` | `phBound::phBound` | verified | Gravity 1, no offset, penetration from the session global. |
| `Bound::makeOwnMaterial` | `dgBoundBox`/`dgBoundGeometry`/`dgBoundSphere`/`dgBoundHotdog` constructors | verified | Each allocates its own `lvlMaterial`. |
| `Bound::material` | `phBound::GetFricElas`'s fallback to the manager default | verified | A bound with no materials yields the default lvlMaterial. |
| `Bound::numMaterials` | `phBound::GetNumMaterials`, `dg*::GetNumMaterials` | verified | 0, or 1 with an own material. |
| `Bound::setFriction`, `setElasticity` | `dg*::SetFriction`, `SetElasticity` (`phBound`'s are no-ops) | verified | Friction at +0x2c, elasticity at +0x28 of the material. |
| `Bound::vertex` | `phBound::GetVertex` | verified | A dummy point. |
| `Bound::testEdge`, `testProbe` (base) | — | inferred | The base never answers; every bound type overrides them. |
| `Bound::testSegment` | `phBound::TestSegment` | verified | Probe → TestProbe with maxT 2 (needs 1 slot), edge → TestEdge (needs 2), AI segments → 0. |
| `Bound::calculateSphereFromBoundingBox` | `phBound::CalculateSphereFromBoundingBox` | fixed | Radius summed z, y, x. |
| `Bound::setOffset` | `phBound::SetOffset` | verified | Never clears the offset flag. |
| `Bound::center` | `phBound::GetCenter` (the Vector3-out overload) | fixed | Sums in the original's order (no OpenMM2 caller yet). |
| `Bound::setPenetration` | `phBound::SetPenetration` | verified | Smallest extent, 0.001 floor. |
| `Bound::isPolygonal` | — | openmm2 | Type test for the dispatcher. |
| `BoundPolygonal::vertex` | `phBoundPolygonal::GetVertex` | verified | |
| `BoundPolygonal::maxDot`, `minDot` | `phBoundPolygonal::MaxDot`, `MinDot` | fixed | Local direction rows summed y, z, x; vertex dots z, y, x; origin term y, z, x. |
| `BoundPolygonal::testProbe` | `phBoundPolygonal::TestProbe` | verified | Last polygon first, maxT shrinking. |
| `BoundPolygonal::testEdge` | `phBoundPolygonal::TestEdge` | verified | Entry/exit tie rules and the count capped at 2. |
| `BoundGeometry::material` | `phBoundGeometry::GetMaterial` | verified | Index into the table; an out-of-range index falls back to the default (the original reads past the table). |
| `BoundGeometry::numMaterials`, `edgeCosine`, `edgeNormal` | `phBound::GetNumMaterials`, `phBoundGeometry::GetEdgeCosine`, `GetEdgeNormal` | verified | |
| `BoundGeometry::postLoadCompute` | `phBoundGeometry::PostLoadCompute` | verified | |
| `edgeInList` | `phBoundGeometry::EdgeInList` | verified | |
| `BoundGeometry::computeEdges` | `phBoundGeometry::ComputeEdges` | verified | (v[n−1], v0) first, declared edges kept in front. |
| `BoundGeometry::computeEdgeNums` | `phBoundGeometry::ComputeEdgeNums` | verified | Missing edge → 0. |
| `BoundGeometry::computeEdgeNormals` | `phBoundGeometry::ComputeEdgeNormals`, `ReComputeEdgeNormals` | fixed | Squared lengths summed x, y, z; convexity and cosine dots z, y, x. The comment said the first face found each way is used; it is the last one before both sides have been seen (the code already did that). Constants 1e-6, 2.0, −1 verified. |
| `BoundGeometry::setQuickTestInfo` | `phBoundGeometry::SetQuickTestInfo` | verified | |
| `BoundGeometry::shiftCentroid` | `phBoundGeometry::ShiftCentroid` | verified | |
| `loadPolygons` | `phBoundGeometry::Load` (polygon part) | verified | A text quad whose fourth index is 0 starts at its second index; the material byte kept. |
| `validGeometry` | `phBoundGeometry::Load` (zero counts) | verified | Also rejects out-of-range indices (guard). |
| `makeGeometryBound` | `phBoundGeometry::Load` + `PostLoadCompute` | deviation | Edges a text file declares are not passed on (`GeometryData` has none); no retail file declares any. |
| `makeTerrainBound` | `phBoundTerrain::Load` | verified | Geometry without computed edges, then the .ter's edges, low 16 bits of each polygon's edge numbers, normals, cosines and grid; the box from the vertices; a mismatching polygon count drops the bound as `lvlInstance::InitBoundTerrain(Local)` does. |
| `BoundBox::unitCorners`, `faceNormals`, `edgeNormalTable`, `kBoxEdges`, `kBoxFaces`, `kBoxFaceEdges` | phBoundBox's tables (static initialisers and rdata) | verified | All values checked; edge normals ±sqrt(1/2) as 0x3f3504f3. |
| `BoundBox::BoundBox()` | `phBoundBox::phBoundBox()` | verified | Unit box, face edge numbers, material 0, the six default materials (same lookups as none). |
| `BoundBox::BoundBox(const Vec3&)` | `phBoundBox::phBoundBox(const Vector3&)` | deviation | The original's sized constructor leaves the polygons' edge numbers 0, but nothing in midtown2.exe calls it; OpenMM2's is the default constructor plus SetSize, which is what the game's callers do. |
| `BoundBox::edgeCosine` | `phBoundBox::GetEdgeCosine` | verified | 0.7853982 (π/4) for every edge, as the original. |
| `BoundBox::edgeNormal` | `phBoundBox::GetEdgeNormal` | verified | |
| `BoundBox::setSize`, `shiftCentroid` | `phBoundBox::SetSize`, `ShiftCentroid` | verified | |
| `BoundBox::setQuickTestInfo` | `phBoundBox::SetQuickTestInfo` | fixed | Radius summed x, y, z (was z, y, x). |
| `BoundSphere::BoundSphere` | `phBoundSphere::phBoundSphere(float)` | verified | The default argument stands in for `phBoundSphere()`, which leaves the box and bounding radius 0 until SetRadius; OpenMM2 never builds a sphere without a radius. |
| `BoundSphere::material`, `numMaterials` | `phBoundSphere::GetMaterial`, `dgBoundSphere::GetMaterial` | fixed | One material: the embedded phMaterial (0.1 / 0.5), or the dg sphere's own; was the manager default (0.5 / 1). |
| `BoundSphere::setRadius` | `phBoundSphere::SetRadius` | verified | |
| `BoundHotdog::BoundHotdog` | `phBoundHotdog::phBoundHotdog(float, float)` | verified | |
| `BoundHotdog::material`, `numMaterials` | `phBoundHotdog::GetMaterial`, `dgBoundHotdog::GetMaterial` | fixed | As the sphere's. |
| `BoundHotdog::setSize`, `calculateBoundingBox` | `phBoundHotdog::SetSize`, `CalculateBoundingBox` | verified | |
| `TerrainGrid`, `TerrainData`, `BoundTerrain::polyTouched` | phBoundTerrain's fields | verified | One byte per polygon where the original keeps a bit. |

## src/phys/BoundSphere.cpp (P)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `invSqrtOrZero` | inline | verified | |
| `setIntersection` | `phIntersectionPoint::Set` + polygon 0 | verified | |
| `BoundSphere::testEdge` | `phBoundSphere::TestEdge` | verified | Reads the bounding radius (+0x34) as the original; squared lengths z, y, x; bInside = start outside. The `max < 2` check cannot fire (TestSegment needs 2 slots). |
| `BoundSphere::testProbe` | `phBoundSphere::TestProbe` | verified | t0 ≤ maxT (NaN accepted, as the x87 compare). |
| `findImpactSphereToSphere` | `phBoundSphere::FindImpactSphereToSphere` | verified | Offset rotations, reach² ≤ dist² rejects, normal, depth, position from A's origin, sum orders from the asm. |

## src/phys/BoundCollision.cpp (P)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `noOverlap` | `phBoundCollision::testNoOverlap` | verified | |
| `segSegDistNorm` | `phBoundCollision::SegSegDistNorm` | fixed | The second crossing's two `Vector3::Dot` calls summed z, y, x (were x, y, z). Normal, distance, first crossing and `operator/` (multiply by the reciprocal) verified. |
| `getDisp` | `phBoundCollision::GetDisp` | verified | |

## src/phys/BoundBox.cpp (P)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `kFaceVertices`, `kFaceEdges`, `kEdgeVertices`, `kEdgeFaces`, `kCornerEdges`, `kCornerBySign` | phBoundBox's rdata tables | verified | |
| `kCornerSigns` | the ±1 corner table filled by a static initialiser | verified | Comment updated (was described as inferred). |
| `SumOrder`, `dot`, `rowSum`, `rotate`, `place` | — | openmm2 | Sum-order helpers. |
| `clipSlab`, `clipToBox` | `phBoundBox::TestEdge` / `TestProbeSlave` slab clip | verified | Every `<`/`<=` against 0 and 1 from the asm (the listing misprints the x exit test). |
| `fillCrossing` | TestEdge / TestProbeSlave output | verified | |
| `BoundBox::testEdge` | `phBoundBox::TestEdge` | verified | The `count < max` check cannot fire through TestSegment. |
| `BoundBox::testProbe` | `phBoundBox::TestProbe` + `TestProbeSlave` | verified | `tEnter <= maxT` (the listing prints `<`). |
| `findImpactSphereToBox` | `phBoundBox::FindImpactSphereToBox` | verified | All classification branches and sum orders; the `outside == 0` return covers a path the original leaves undefined and r > 0 never reaches. |
| `findFaceDots` | `phBoundBox::FindFaceDots` | verified | ±0.035f thresholds; NaN rows not reproduced (file header). |
| `removeFaceDotZero`, `checkFourFaceDotPattern`, `removeFifthFaceDotZero` | `RemoveFaceDotZero`, `CheckFourFaceDotPattern`, `RemoveFifthFaceDotZero` | verified | Includes the original's negative-index write (equivalent). |
| `classifyRotation` | the zero-count switch of FindImpactsBoxToBox(Offset) | verified | |
| `makeTransformedCorners`, `addEdgeChecks`, `avoidEdgeChecks`, `useThisImpact` | `MakeTransformedCorners`, `AddEdgeChecks`, `AvoidEdgeChecks` (both), `UseThisImpact` | verified | 1.5 × Seconds limit (MM2's own unit mix). |
| `ConvexPolyIntersect::run`, `stateOutside`, `stateUInside`, `stateCrossing`, `stateTouching`, `precomputeRays`, `advanceV`, `getvHeadOut`, `getuHeadOut`, `getuTailOut`, `recordNoIsect`, `recordInteriorCollides` | `phConvexPoly::ConvexPolyIntersect` and helpers | verified | |
| `recordEE`, `recordTail` | `phConvexPoly::RecordEE`, `RecordTail`/`RecordUTail`/`RecordVTail` | deviation | Guard: records capped at 8 (the caller's buffer); the original overwrites its stack past that. |
| `projectOnFace` | `Vector3::GetVector2` | verified | |
| `faceNormalToWorld`, `facePointToWorld` | inline in BoxToBoxFaceImpacts(Offset) | verified | |
| `writeImpact`, `boxCorners` | — | openmm2 | Glue; the stored fields match. |
| `boxToBoxFaceImpacts` | `BoxToBoxFaceImpacts`, `BoxToBoxFaceImpactsOffset` | verified | |
| `faceFaceImpacts` | face-face branch of the same | deviation | Records of the no-intersection type are dropped; the original duplicates stale values depending on a pointer byte (undefined). |
| `edgeFaceImpacts` | edge-face branch of the same | deviation | When t1 ≤ t0 OpenMM2 sets the impact's colliders; the original leaves them stale. |
| `boxEdgeImpacts` | edge pass of FindImpactsBoxToBox(Offset) | verified | |
| `findImpactsBoxToBoxOffset`, `findImpactsBoxToBox` | `FindImpactsBoxToBoxOffset`, `FindImpactsBoxToBox` | verified | |

## src/phys/BoundHotdog.cpp (P)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `invSqrtOrZero`, `rowDot`, `transformXZY` | inline | verified | `rowDot` is `Vector3::Dot`'s order. |
| `isInsideHotdog` | `phBoundHotdog::IsInsideHotdog` | verified | |
| `findHotdogIsectNormal` | `phBoundHotdog::FindHotdogIsectNormal` | verified | |
| `segmentToHotdogIntersections` | `phBoundHotdog::SegmentToHotdogIntersections` | verified | Including the original's shaft-range quirk (centred y in [−h, 0]). |
| `setIntersection` | `phIntersectionPoint::Set` + feature | verified | |
| `BoundHotdog::testEdge` | `phBoundHotdog::TestEdge` | verified | The second crossing's doubled-b position quirk is kept; the `max < 2` check cannot fire. |
| `BoundHotdog::testProbe` | `phBoundHotdog::TestProbe` | verified | |
| `findImpactSphereToHotdog` | `phBoundHotdog::FindImpactSphereToHotdog` | verified | |
| `findImpactsHotdogToPoly` | `phBoundHotdog::FindImpactsHotdogToPoly` | deviation | Guard on the impact table in pass 4 (dgPhysManager's table holds 200); all four passes verified. |
| `findImpactsHotdogToHotdog` | `phBoundHotdog::FindImpactsHotdogToHotdog` | verified | 0.001225 parallel test, the "AddScaled" helper that subtracts. |

## src/phys/BoundPolygonal.cpp (P)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `xform` | `Matrix34::Transform4` | verified | Used for the current poses of UseDotSmall and the level. |
| `rotateZyx` (was `xformDir`) | the edge-normal transforms | fixed | Summed z, y, x. |
| `relative` | `Matrix34::FastInverse` + `Matrix34::Dot` | verified | |
| `dot` | `Vector3::Dot` | fixed | Summed z, y, x (was x, y, z). |
| `materialOf` | FindImpacts' material read | verified | |
| `DispSegment`, `EdgeSegment`, `Scratch` | `phBoundPolygonal::DispSegment`, `Segment`, the static buffers | inferred | Same contents and initial values (0xffff, 2.0, −1.0). |
| `getAllSegments` | `phBoundPolygonal::GetAllSegments` | fixed | Threshold dot and previous-pose transform in the original's orders. |
| `collidePolygon` | per-polygon pass of `TestBoundPolyPolyUseDotSmall` and `lvlSDL::CollidePolyToLevel` | fixed | Plane distances summed z, y, x. −1.5 × penetration backup, entry/exit rules verified. |
| `writeIntersections` | output loops of the same | fixed | Edge normals summed z, y, x. The capacity check is a guard. |
| `makeBsInside` | `phBoundPolygonal::MakeBsInside` | verified | 0xfe07 mask, 1.03 pairing. |
| `resetVertNeedsH` (both) | `phBoundPolygonal::ResetVertNeedsH` (both) | verified | |
| `checkSaveEdgeEdge` | `phBoundPolygonal::CheckSaveEdgeEdge` | fixed | "along" summed z, y, x. 1.2 and 4.0 verified. |
| `getCollideEdgePoly` | `phBoundPolygonal::GetCollideEdgePoly` | fixed | Every inline transform and dot in the original's order (velocity, point, edge direction, slide, edge distances, approach, the chosen edge's ends, the cosine test). 0.25881904, 0.01, 1e-6, 1.5 × Seconds verified. |
| `doEndPtSearch` | `phBoundPolygonal::DoEndPtSearch` | fixed | Divides only by a positive approach speed (the listing prints `0 <=`; at rest with no depth OpenMM2 got 0/0); approach summed x, z, y. The `left` check is a guard. 0.9, 1e-5, 0.001 verified. |
| `retryVertPolyCollide` | `phBoundPolygonal::RetryVertPolyCollide` | fixed | Squared edge length summed z, y, x; parameters renamed to the vertex's and the face's colliders (see findImpacts). 0.75, 0.04 verified. |
| `writeEdgeEdgeImpact` | FindImpacts' edge-edge writer | verified | |
| `pairedWithNext` | `phBoundPolygonal::GetNextEdgeIsect` (inlined) | verified | |
| `addInteriorEdges` | `phBoundPolygonal::AddInteriorEdges` | verified | |
| `toWorldCoords` | FindImpactsPolyToPoly's per-intersection transform | fixed | Points, face normals and interior edge normals each in their own order. |
| `testBoundPolyPoly` | `phBoundPolygonal::TestBoundPolyPoly` | fixed | Thresholds' origin terms summed y, z, x. |
| `testBoundPolyPolyUseDotSmall` | `phBoundPolygonal::TestBoundPolyPolyUseDotSmall` | deviation | Verified; the 100-entry cap is a guard (the original has none). |
| `testBoundPolyPolyUseDot` | `TestBoundPolyPolyUseDot`, `RewindSegments`, `GetNextSegment` (UseDot variants) | fixed | Threshold dot, current pose, previous pose, and each end of an edge placed on demand, each in the original's order; edge normals z, y, x. Comment on the slot check corrected (the original's TestSegment refuses too). |
| `findImpactsPolyToPoly` | `phBoundPolygonal::FindImpactsPolyToPoly` | verified | |
| `findImpacts` | `phBoundPolygonal::FindImpacts` | fixed | **Behaviour:** B's leftover vertices go to RetryVertPolyCollide as (B, A), so the impact keeps collider A = A; OpenMM2 passed (A, B) and swapped the colliders against the elements and normal (impulse reversed; terrain material on the wrong side). Edge-edge approach summed z, y, x. |
| `findImpactsSphereToPoly` | `phBoundPolygonal::FindImpactsSphereToPoly` | fixed | Centre and offset dots, offset rotation, probe test and fallback depth in the original's orders. |
| `LevelBound::clear`, `addPolygon` | `sdlPoly::InitNoArea` (edge normals) | inferred | Corners copied per polygon instead of shared; the level's edges are never used. |
| `LevelBound::vertex` | `lvlSDL::GetVertex` | verified | |
| `LevelBound::material` | `lvlLevelBound::GetMaterial` | verified | |
| `Level::material` | — | openmm2 | Fallback for a level without a table. |
| `collidePolyToLevel` | `lvlSDL::CollidePolyToLevel` | fixed | Previous pose and the "near the plane" test in the original's orders. The `max` cap is a guard. |
| `findLevelImpacts` | the level impact search `dgPhysManager::CollideTerrain` calls | verified | Friction 1 / elasticity 0.25 placeholders, colliderA = level for retries. |

## src/phys/BoundTerrain.cpp (P)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `ftol`, `equalOrUnordered`, `clampCell`, `dot3` | CRT float-to-int, x87 compares, inline clamps, `Vector3::Dot` | verified | |
| `clearPolyTouched` | `phBoundTerrain::ClearPolyTouched` | verified | |
| `touchPolygon`, `addSection` | the touched-bit and section-list code of every walk | deviation | Guards on indices; identical otherwise. |
| `sectionPolygons` | a section's offset/count lookup | openmm2 | Clamps the list. |
| `forEachNewPolygon` | the shared section/polygon walk | verified | |
| `sectionsNearSphere` | `phBoundTerrain::InitPolyIterator(const Vector3&, float)` | verified | |
| `calculateBuckets` | `phBoundTerrain::CalculateBuckets` | verified | 0.0001 pull-in, NaN branches, all four walk branches. |
| `sectionsAlongSegment` | `phBoundTerrain::InitPolyIterator(const phSegment&)` | verified | |
| `testBoundTerrainEdgesVsPoly` | `phBoundTerrain::TestBoundTerrainEdgesVsPoly` | deviation | Stops when fewer than 2 slots remain (the original overflows); no retail terrain has hot edges. |
| `testBoundPolyTerrain` | `phBoundTerrain::TestBoundPolyTerrain` | deviation | Capacity guards; all sums and rules verified. |
| `hotdogToTerrain` | the shared body of the two hotdog-terrain searches | deviation | `max <= 0` returns 0 (the original writes one impact). |
| `BoundTerrain::testEdge` | `phBoundTerrain::TestEdge` | deviation | Writes only the slots `max` allows and keeps their other fields; the original copies a whole stack intersection (stale fields included). |
| `BoundTerrain::testProbe` | `phBoundTerrain::TestProbe` | verified | |
| `testBoundTerrainPoly` | `phBoundTerrain::TestBoundTerrainPoly` | verified | < 31 vertices → TestBoundPolyTerrain, else UseDot with −FLT_MAX. |
| `testBoundTerrainLocalPoly` | `phBoundTerrainLocal::TestBoundTerrainPoly` | verified | |
| `findImpactsSphereToTerrain` | `phBoundTerrain::FindImpactsSphereToTerrain` | deviation | `max <= 0` guard; verified otherwise. |
| `findImpactsHotdogToTerrain`, `findImpactsHotdogToTerrainLocal` | `phBoundTerrain::FindImpactsHotdogToTerrain`, `phBoundTerrainLocal::FindImpactsHotdogToTerrainLocal` | verified | |

## Missing

| MM2 | What it does | Status |
| --- | --- | --- |
| `phBoundTerrain::Load`'s version check | A .ter that is not version 1.1 falls back to a plain geometry bound and the instance is dropped | open (low): every retail file is 1.1; needs `TerrainData` to carry the version from `CityLevel`'s glue. |
| `phBoundGeometry::Load`'s declared edges | `edge a b` lines become the first edges | deviation: parsed but not handed to phys; no retail file declares any. |
| `phBound::TestSegmentPoint`, `TestProbePoint` / `TestAIPoint` of every bound type | point-only and AI segment tests | not needed: reached only through `phColliderBase::TestSegmentPoint`, which nothing in midtown2.exe calls. |
| `phBound::TestSphere` (both) and the per-type `TestSphere` | deepest contact of a query sphere | not needed: `dgPhysManager::TestSphere` is a stub returning false. |
| `phBound::CenterBound`, `phBoundPolygonal::CenterBound` | move the vertices by −centroid and clear the offset | not needed: no caller found (vtable slot 0). |
| `phBound::GetFricElas` (both), `SetFlexibility` | pair friction/elasticity from intersections; flexibility flag | not needed: no callers. |
| `phBoundGeometry::Init`, `ScaleSize`, `CalculatePolyNormals`, `OverlapRegion`, `GhostSection` | allocation, per-axis scaling, ghost bounds | not needed: no callers (`lvlInstance`'s ghost-bound methods are unreferenced). |
| `phBoundBox::CreateOffset`, `ScaleSize`, `Load`, `ProbeVsBox`, `VerifyFaceDotPattern` | offset/scale/text loader, static probe, pattern check | not needed: no callers. |
| `phBoundSphere::CreateOffset`, `ShiftCentroid`, `ScaleRadius`, `Load` | offset (the sphere radius becomes the farthest box corner's distance), scaling, text loader | not needed: only CreateOffset calls ShiftCentroid, and nothing calls CreateOffset. |
| `phBoundHotdog::Load`, `SetBoundingBox`, `ScaleBoundingBox`, `CreateOffset`, `ShiftCentroid` | text loader, sizing, offset | not needed: no callers. |
| `phBoundPolygonal::GetNextSegment` / `RewindSegments` (plain variants) | segment walk of `lvlLevelBound::TrivialCollideBoxToLevel` | not needed: no call site found. |
| `phBoundTerrain::PackNormal`, `UnpackNormal`, `Save` | tool code | not needed. |
| `phPolygon::Rotate` | rotate a polygon's indices | not needed: no callers. |
