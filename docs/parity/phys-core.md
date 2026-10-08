# Parity audit: phys-core

Audited against MM2Recomp (midtown2.exe build 3393) on 2026-10-07.

Summary: 206 rows (functions that share a verdict are grouped in one row);
verified 75, fixed 96, deviation 9, inferred 10, open 1, openmm2 15. The
Missing table adds 3 open items.

Scope: `src/core/Math`, `src/phys/AgeMath`, `Collider`, `Collision`,
`Constants.h`, `Geometry`, `Impact`, `InertialCS`, `Joint`, `Level.h`,
`Material`, `PolygonSoup`, `Sleep`, `TrailerJoint`, `World`. Every row was
compared with the decompile and, for the float code, the asm (x87 order of
operations; constants decoded from the data sections). "Sum order" below
means the grouping of a three-term float sum ((a + b) + c), which changes
the last bit of the result: the original's compiler grouped every such sum
its own way (the in-place and two-argument overloads of one Matrix34
product differ), and float addition does not associate.

Facts re-checked from the build 3393 code: gravity is MM2's global -19.6
(dgPhysEntity::Update, never written); mmGame::Init sets dgPhysManager's
SampleStep to 1/35 s and MaxSamples to 3 (its constructor's 1/60 and 6 are
overridden), so 60 fps runs one 1/60 s sample per frame; phInertialCS limits
the angular velocity per body axis (constructor 5 rad/s, vehCarSim::Init
4 pi) and the speed at 500; ApplyContactForce makes the integration
implicit. World sets the routines' sample time (datTimeManager::Seconds /
InvSeconds, the "within 1.5 samples" tests of the bound routines) at the
start of each sample, as SetTempOverSampling does.

## core/Math (class M)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `kPi`, `kTwoPi`, `kHalfPi`, `kDegToRad`, `kRadToDeg`, `sq`, `lerp(float)`, `clampf`, `signf` | none | openmm2 | General utilities. |
| `Vec2` | none | openmm2 | UI and rendering. |
| `Vec3` constructors, `operator[]`, `zero`/`xAxis`/`yAxis`/`zAxis`, `mul`, `dist2`, `vmin`, `vmax`, `operator*(float, Vec3)` | none | openmm2 | Utilities. |
| `Vec3::operator+ - unary- *(float) += -= *=` | Vector3::Add, Subtract, Negate, Scale, operator+=, -= | verified | Element-wise. |
| `Vec3::operator/` | Vector3::operator/ | verified | 1 / s, then multiplies. |
| `Vec3::dot` | Vector3::Dot and inline dot products | deviation | Sums x, y, z. MM2 has no single order: most of its dot products are inlined and summed each their own way; the out-of-line Vector3::Dot (z, y, x) is `phys::age::dot`. Ports that must round as the original write the sum out. |
| `Vec3::cross` | Vector3::Cross | verified | Same subtraction order. |
| `Vec3::mag2`, `mag` | Vector3::Mag2, Mag | verified | (x² + y²) + z². |
| `Vec3::invMag`, `normalized` | Vector3::InvMag, Normalize | verified | 0 for a zero vector; scales by the reciprocal. |
| `Vec3::dist` | Vector3::Dist | fixed | Summed (x² + y²) + z²; MM2 sums z², y², x². |
| `lerp(Vec3)` | Vector3::Lerp | verified | a + (b - a) t. |
| `Vec4` | none | openmm2 | Rendering. |
| `Mat34::identity`, `translation`, `row` | Matrix34::Identity | verified | |
| `Mat34::rotationX`, `rotationY`, `rotationZ` | Matrix34::MakeRotateX / Y / Z | verified | Same layout and signs; cos and sin rounded from the wide result as fcos/fsin are. |
| `Mat34::rotationAxis` | Matrix34::MakeRotateUnitAxis | fixed | Was a textbook Rodrigues form; now MM2's: (n·n)(1 − c) + c with the cosine added unrounded, off-diagonals (ny nx)(1 − c) ± s nz and so on. |
| `Mat34::transform` | Vector3::Dot(Vector3, Matrix34), Matrix34::Transform | fixed | Sum order: x is (y + z) + x, y and z are (x + y) + z, then + m3. |
| `Mat34::transformDir` | Vector3::Dot3x3 | fixed | Sum order x (z + y) + x, y (z + x) + y, z (z + x) + y. |
| `Mat34::untransformDir` | Vector3::Dot3x3Transpose | fixed | Sum order as Dot3x3, on the rows. |
| `Mat34::untransform` | none | openmm2 | Composite helper. |
| `Mat34::mul`, `operator*` | Matrix34::Dot (two-argument) | fixed | Each element and the translation now summed in MM2's order. |
| `Mat34::fastInverse` | Matrix34::FastInverse | verified | Transposed rotation, m3 = -((r0 · t)) summed x, y, z; written out now that transformDir follows Dot3x3. |
| `Mat34::inverse` | none | openmm2 | Unused utility (identity when singular); MM2 code uses `age::inverse` (Matrix34::Inverse). |
| `Mat34::normalize` | Matrix34::Normalize | fixed | Kept m1 (up) primary; MM2 keeps m2: m0 = |m1 × m2|, m1 = |m2 × m0|, m2 = |m2|, with its sum orders. |
| `Mat44`, `Quat`, `Aabb` | none | openmm2 | Rendering, network interpolation, bounds boxes. |

## phys/AgeMath

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `age::mag2`, `mag` | Vector3::Mag2, Mag | fixed | Summed y², z², x² (MM1's order); MM2 sums x², y², z². |
| `age::invMag` | Vector3::InvMag | fixed | Same order fix; 0 for a zero vector (was infinity). |
| `age::dot` | Vector3::Dot | fixed | (x + y) + z; MM2 (z + y) + x. |
| `age::dot3x3(Vec3, Mat34)` | Vector3::Dot3x3 | fixed | Added; replaces left-to-right products in the rigid body. |
| `age::dot3x3Transpose(Vec3, Mat34)` | Vector3::Dot3x3Transpose | fixed | Added. |
| `age::makeRotateUnitAxis` | Matrix34::MakeRotateUnitAxis | fixed | Added (moved from TrailerJoint): products rounded to float, the cosine added wide. |
| `age::makeRotate` | Matrix34::MakeRotate | fixed | Added: angle 0 identity; an axis along x, y or z takes MakeRotateX/Y/Z with the axis' sign (a zero axis takes the -z branch, as MM2); otherwise normalised, then MakeRotateUnitAxis. |
| `age::rotate` | Matrix34::Rotate | fixed | Was MM1's axis paths plus ArbitraryRotation; now MakeRotate and the in-place Dot3x3. |
| `age::rotateUnitAxis` | Matrix34::RotateUnitAxis | fixed | Added. |
| `age::arbitraryRotation` | Matrix34::MakeRotate | fixed | MM1's ArbitraryRotation (renormalising only outside 0.99999..1.000001) does not exist in MM2; returns MakeRotate's matrix for its callers (vehicle wheel and axle code). |
| `age::transpose` | Matrix34::Transpose | verified | |
| `age::inverse` | Matrix34::Inverse | fixed | Cofactors and determinant matched; the translation sums were x, y, z, MM2 sums x, z, y. Unchanged when the determinant is 0, as MM2. |
| `age::dot3x3` | Matrix34::Dot3x3 (two-argument) | fixed | Every element summed in MM2's order. |
| `age::dot3x3InPlace` | Matrix34::Dot3x3 (in place) | fixed | Added: the in-place overload groups its sums differently. |
| `age::dot3x3Transpose(Mat34, Mat34)` | Matrix34::Dot3x3Transpose (two-argument) | fixed | Added (replaces TrailerJoint's local version, whose order matched neither overload). |
| `age::dot3x3TransposeInPlace` | Matrix34::Dot3x3Transpose (in place) | fixed | Added. |
| `age::dot` | Matrix34::Dot (two-argument) | fixed | Through `Mat34::mul`. |
| `age::crossProdMatrix` | the cross product matrix MM2 builds inline | verified | p · M = p × v. |
| `age::dot3x3CrossProdMtx` | Matrix34::Dot3x3CrossProdMtx | fixed | Added: each row a becomes a × r. |
| `age::dot3x3CrossProdTranspose` | Matrix34::Dot3x3CrossProdTranspose | fixed | Added: each row a becomes r × a. |
| `age::add3x3` | Matrix34::Add3x3 | verified | Element-wise; m3 now kept from the first operand, as the in-place original. |
| `age::scale3x3`, `age::addScaled3x3` | Matrix34::Scale, AddScaled | fixed | Added. |
| `age::solveSVD` | Matrix34::SolveSVD | fixed | Was Gaussian elimination. MM2: the largest element (strictly larger wins) sets tol = 1e-4 of it (0 → result 0); the cofactor solve of x M = b when \|det\| > tol², \|det\| > (largest cofactor) tol and two cofactor rows exceed tol (rows 2 and 3 when the largest element is in row 1, rows 1 and 3 otherwise); else, if a cofactor exceeds tol, the rank-2 minimum-norm solution (the largest cofactor row as the null direction, the other two rows' 2x2 system in the components other than its largest one, then the part along the cross product of the matching columns removed); else the rank-1 one (b projected on the largest element's row, back through its column). Ported with the asm's sum orders. The inverse mass and implicit matrices the game passes always take the full-rank path. |
| `age::invSqrtFast`, `rotateAbs`, `mulD`/`addD`/`subD`/`rsubD` | none | fixed | Removed: MM1 helpers (invsqrtf_fast, RotateAbs, double constants) that midtown2.exe does not have; nothing used them. |

## phys/InertialCS (phInertialCS)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `InertialCS::InertialCS` | phInertialCS::phInertialCS | fixed | Was mass 1000 with a 1 m box and a 10 pi spin limit applied only on request; MM2: Init(1, 1, 1, 1), speed limit 500, spin limit 5 rad/s per axis, Zero. |
| `init` | phInertialCS::Init | fixed | Added; reciprocals FLT_MAX for values <= 0. |
| `setMass` | phInertialCS::InitBoxMass | verified | (z² + y²) m / 12 and so on, 1/12 = 0.083333336. |
| `zero` | phInertialCS::Zero | fixed | No longer wakes the body (OpenMM2 set state Awake); Identity, Freeze, last push 0. |
| `freeze` | phInertialCS::Freeze | fixed | Added: motion cleared, then ZeroForces. |
| `zeroForces` | phInertialCS::ZeroForces | fixed | Added: accumulators, stiffness matrices and pushes cleared, the pending and frame pushes becoming the last push. |
| `place` | none (owners' Zero then matrix) | inferred | Glue for the owners' Zero-and-place sequence. |
| `update` | dgPhysEntity::Update + phInertialCS::Update() | verified | Weight, then the integration. |
| `finishForces` | dgPhysEntity::Update, phInertialCS::Update() | fixed | MM1's asInertialCS sleep test and constraints removed (MM2 has neither; phSleep decides); weight Mass × gravity as dgPhysEntity adds it; the contact force and torque join. |
| `finishUpdate` | phInertialCS::Update(float), Update() | fixed | An inactive (asleep) body now keeps its push bookkeeping (frame push += push, last push = frame push) instead of returning early, so pushes MoveICS applied while asleep no longer pile up. Rotation by Matrix34::RotateUnitAxis (was MM1's ArbitraryRotation) when the turn's Mag2 exceeds the double 1e-15. OpenMM2's re-orthonormalisation when the basis drifted is removed (MM2 never normalises a body's matrix). |
| `integrateExplicit` | phInertialCS::Update(float), explicit branch | fixed | The spin limit now always applies (MM2 has no switch); the body-axis projections sum (z + y) + x. |
| `integrateImplicit` | phInertialCS::Update(float), implicit branch | fixed | The angular right-hand side is -(h/m) P (I + h/m K)^-1 (X K)^T + angular impulse + h torque: the port had +(h/m) (Ghidra drops the scaled copy's float argument; the asm negates it). The sequence now uses MM2's helpers (in-place Dot3x3 and Dot3x3Transpose, Scale, AddScaled, GetInertiaMatrix, SolveSVD) and the momentum is w · Iw in the original's order. As in MM2, the linear update then adds +h dw (X K), the opposite sign to the one the angular solve assumes. |
| `moveICS` | phInertialCS::MoveICS | verified | |
| `applyForce` (both), `applyTorque`, `applyAngImpulse` | the callers' inline additions | inferred | Stand in for the additions the wheel, engine, aero and joint code make to the force, torque and impulse fields; the vehicle audit checks their call sites. |
| `applyImpulse` | phColliderBase::Impact (its ICS part) | verified | Impulse and r × j. |
| `applyPush`, `applyTurn` | phInertialCS::CalcNetPush, CalcNetTurn | fixed | Same branches; the three sums now (y + z) + x, (x + y) + z and (z + y) + x as the asm. |
| `applyContactForce` | phInertialCS::ApplyContactForce | fixed | X K through the in-place Dot3x3 (was the generic product); the stiffness's translation row summed as MM2. Torque, flags and XKX matched. |
| `getVelocity` | phInertialCS::GetLocalVelocity | verified | |
| `filteredVelocity` | phInertialCS::GetLocalFilteredVelocity2 | fixed | Threshold 0.0001 and branches matched; the push length and projection now summed z, y, x. |
| `cmFilteredVelocity` | phInertialCS::GetCMFilteredVelocity | fixed | Added: velocity + InvSeconds × last push. |
| `localAcceleration` | phInertialCS::GetLocalAcceleration | fixed | Body projections (z + y) + x and the angular acceleration (x + y) + z, as the asm. |
| `getForce`, `getTorque` | phInertialCS::GetForce, GetTorque | verified | |
| `worldInertia` | phInertialCS::GetInertiaMatrix | fixed | R^T diag(I) then the in-place Dot3x3. |
| `calcCMatrix` | phInertialCS::GetInvMassMatrix(pos, out) | fixed | Dot3x3CrossProdMtx, scaled rows, transpose, two-argument Dot3x3, + InvMass on the diagonal. |
| `doConstrain`, `applyImpulseNow`, `invInertiaWorld`, `effectiveMass`, `refreshVelocities` | none | fixed | Removed: MM1 constraints and an unused OpenMM2 contact solver. |
| `position`, `setMaxAngVelocity` | none | openmm2 | Accessors. |
| `limitAngVelocity` | none | deviation | Kept as a test switch, default on: MM2 always limits. |
| `gravity` | MM2's gravity global, dgPhysEntity::Update | inferred | Per-body stand-in for the global the entity adds (-19.6 by default; tests set others). |
| `frameVelocity`, `elasticity`, `friction`, `size` | none | openmm2 | Game-layer velocity (MM1's FrameVelocity); MM1 members nothing reads; InitBoxMass's box. |

## phys/Sleep (phSleep)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Sleep::init` | phSleep::Init | verified | 15 updates, 120 more to dormant, 0.005 and 0.01. (MM2's wake-up time FLT_MAX and callback are not kept; nothing in a race sets them.) |
| `reset`, `wakeUp` | phSleep::Reset, WakeUp | verified | Counters and sums cleared, the body active. |
| `sendToSleep` | phSleep::SendToSleep | fixed | Now phInertialCS::Freeze (clears the pushes, turn, contact accumulators and stiffness too; OpenMM2 cleared only motion, force and impulses). |
| `update` | phSleep::Update | fixed | An asleep body's still updates now call ZeroForces (pushes included); the pushed velocity's length sums z, y, x and the others x, y, z as the asm; 1.8 is a float. |

## phys/Collider (phColliderBase, phCollider, phColliderJointed)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Collider::init` | phCollider::Init (with an ICS) | verified | Then phColliderBase::Reset. MM2's InstanceData key is `key()`. |
| `initStatic` | phCollider::Init(bound, matrix) | fixed | Added: a collider without a body has maxMoved 0 and counts as barely moved (the level's and static instances' temporary colliders; World used `init`). |
| `reset` | phColliderBase::Reset | verified | |
| `updateMtx` | phColliderBase::UpdateMtx | verified | |
| `calcMaxMoved` | phColliderBase::CalcMaxMoved | fixed | Used GetLocalFilteredVelocity2 at the CG; MM2 uses GetCMFilteredVelocity. Sums now as the asm. MM2 calls it only from aiVehicleActive::Attach (see World::step). |
| `copyLastMatrix` | phColliderBase::CopyLastMatrix | verified | |
| `localVelocity` | phColliderBase::GetLocalVelocity | verified | |
| `invMassMatrix` | phColliderJointed / phColliderBase::GetInvMassMatrix | verified | |
| `impact` | phColliderBase::Impact (three-argument) | verified | Push length summed z, y, x; strictly greater wins. |
| `key`, `setKey`, `ImpactHandler` | InstanceData pointer, impact datCallback | inferred | Glue for MM2's key pointer and callback. |

## phys/Collision (phCollision)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `sampleTime` | datTimeManager::Seconds / InvSeconds | inferred | Set by World for each sample. |
| `getRelDisp` | phCollision::GetRelDisp | fixed | Sums now (y + z) + x for the local displacement and (z + y) + x / (z + x) + y for the output, as the asm (the port followed Ghidra's printed order). |
| `testBoundGeneric` | phCollision::TestBoundGeneric (collider version) | fixed | Dispatch, collider sides, relPos signs, GetRelDisp arguments, the sweep flag (off only when both colliders are barely moved) and the terrain tests' swapped lists into FindImpactsPolyToPoly match (checked again in the asm). The sphere-against-local-terrain transforms now sum as the asm. |
| `testBoundGeneric`: local-terrain counts | the same | deviation | MM2 passes reused argument slots (holding pointer values) as the terrain test's counts; OpenMM2 starts them at 0. No retail terrain reaches the difference. |

## phys/Constants.h

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `kGravity` | gravity global (dgPhysEntity::Update) | verified | -19.6, never written. |
| `kFixedSampleStep` | none | deviation | OpenMM2's fixed 1/60 s step (behaves as MM2 at 60 fps). |
| `kOversampleStep`, `kOversampleMaxSamples` | mmGame::Init, dgPhysManager +0x12ac / +0x12a8 | verified | 1/35 s, 3. |
| `kCarMaxAngVelocity` | vehCarSim::Init | verified | 4 pi on each axis. |
| `kMetersPerSecondToMph` | MetricFactor | verified | 2.2360249, never written. |
| `kSleepVel2`, `kSleepAngVel2`, `kSleepTime` | none | fixed | Removed (MM1 asInertialCS defaults). |

## phys/Geometry (vector7 geometry, phCollisionPrim)

All checked operation by operation in the asm, constants decoded.

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `invSqrtOrZero` | the normalise idiom | verified | |
| `distanceParallelLineToLine` | DistanceParallelLineToLine | verified | MM2's unused first direction dropped. |
| `distanceLineToLine` | DistanceLineToLine | verified | Strict parallel test against len2·len1·tolerance, divide by length, sign flip. |
| `tValueSegToOrigin`, `tValueSegToPoint` | FindTValueSegToOrigin, FindTValueSegToPoint | verified | |
| `tValuesLineToLine` | FindTValuesLineToLine | verified | |
| `addIntersection` (both) | AddIntersection (both) | verified | |
| `findImpactPolygonToSphere` | FindImpactPolygonToSphere | verified | |
| `findImpactEdgeToShaft` | FindImpactEdgeToShaft | verified | Tolerance 0.0012217. |
| `isPointBehindPlane`, `isPointNearPlane`, `isPointInBox` | same names | verified | Differ only for NaN input. |
| `distanceLineToPoint` | DistanceLineToPoint | verified | Signed result. |
| `findTValuesSegToSeg` | FindTValuesSegToSeg | verified | All nine end-point cases. |
| `findTValuesLineToBoxFace` | FindTValuesLineToBoxFace | verified | A zero normal reads uninitialised values in MM2; OpenMM2 uses zeros (unreached). |
| `segmentToSphereIntersections`, `segmentToHemisphereIntersections`, `segmentToUprightCylIsects`, `segmentToBoxIntersections`, `orderIntersections` | same names | verified | |
| `sphereToPolygonal` (bound space, world space) | phCollisionPrim::SphereToPolygonal (both) | verified | |
| `segmentToSphere` | phCollisionPrim::SegmentToSphere | verified | |
| `segmentSphereTest` (four) | phCollisionPrim::SegmentSphereTest (four) | verified | Each overload's own sum order. |

`segSegDistNorm` and `getDisp` are declared in Geometry.h but implemented in
BoundCollision.cpp (phys-bounds).

## phys/Impact (phImpactBase, phImpact, phContactMgr::CalcImpact, dgImpact)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Impact` (fields) | phImpact (0x44 bytes) | verified | |
| `swapColliders`, `reset` | phImpactBase::SwapColliders, Reset | verified | |
| `startMakingNewImpact` | phImpactBase::StartMakingNewImpact | fixed | Position and normal transforms now summed as the asm. |
| `finishMakingNewImpact`, `makeNewImpact` | phImpactBase::FinishMakingNewImpact, MakeNewImpact | verified | |
| `cullImpactList` | phImpactBase::CullImpactList | fixed | Logic matched; sums now as the asm. |
| `addImpactSpherePlaneTest` | phImpactBase::AddImpactSpherePlaneTest | verified | |
| `addImpactShaftPlaneTest` | phImpactBase::AddImpactShaftPlaneTest | fixed | Logic matched; side, axial and length sums now as the asm. |
| `impactIsInList` | phImpactBase::ImpactIsInList | verified | Header comment corrected: callers test the sign of the result. |
| `materialOf` | phImpact::GetMaterial | verified | |
| `findFrictionAndElasticity` | phImpact::FindFrictionAndElasticity | fixed | The cap is now the runtime global (`elasticityCap`), not a constant. |
| `elasticityCap`, `setElasticityCap`, `kElasticityCap`, `kBlubberElasticityCap` | the elasticity cap global; mmGame::Reset; mmGame::SendChatMessage | fixed | 1 by default; the "blubber" cheat sets 4. The chat hook itself is open (session area). |
| `localVelocities` | phImpact::GetLocalVelocities | verified | |
| `calcCollision` | phImpact::CalcCollision | fixed | Threshold 0.01, SolveSVD, Coulomb limit (±1e-5), (1 + e), push shares, the downward rule and weighting matched; collider B's normal transform uses MM2's second helper and the lengths sum as the asm. |
| `calcCollisionNoFriction` | phImpact::CalcCollisionNoFriction | fixed | Sums now (x + z) + y as the asm. |
| `calcImpact` | phContactMgr::CalcImpact, phImpact::Impact | verified | Contacts disabled (phContact::DisableContacts in dgPhysManager's constructor). |
| `calcBangerImpact` | dgImpact::CalcImpact, dgImpact::CalcCollision | fixed | Behaviour matched (the other CalcCollision overload has no callers); lengths now Vector3::Mag, B's transform the second helper, the friction denominator dgImpact's order. |
| `invMass`, `transformed`, `added` | GetInvMassMatrix (direct), inline transform, Matrix34::Add | verified | |
| `transformedB` | the second inverse-mass transform helper | fixed | Added. |
| `frictionLimited` | the friction limit in both CalcCollisions | fixed | Takes dgImpact's denominator order for bangers. |

## phys/Joint (phJoint)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Joint::Joint` | phJoint::phJoint | verified | |
| `init` (offset) | phJoint::Init(ics, ics, Vector3) | fixed | Hitch point and body-2 offset sums as the asm. |
| `init` (two offsets) | phJoint::Init(ics, ics, Vector3, Vector3) | fixed | Initial point sums as the asm. |
| `reset`, `update` | phJoint::Reset, Update | verified | Nothing in midtown2.exe calls Update. |
| `computeInvMassMatrix()` | phJoint::ComputeInvMassMatrix() | fixed | (C2 + C1)^-1 transposed matched; joint point sums as the asm. |
| `computeInvMassMatrix(ics, out, pos)` | phJoint::ComputeInvMassMatrix(ics, out, pos) | verified | Empty. |
| `computeJointForce` | phJoint::ComputeJointForce | fixed | t · M summed (z + y) + x. |
| `computeJointPush` | phJoint::ComputeJointPush | fixed | Body-2 hitch sums as the asm. |
| `invMassMatrix`, `isBroken` | phJoint::GetInvMassMatrix, IsBroken | verified | |

## phys/TrailerJoint (dgTrailerJoint)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `init` | dgTrailerJoint::Init, FileIO | verified | Defaults and their order (raw bits), the force flag, the 14 file fields, then phJoint::Init. |
| `reset` | dgTrailerJoint::Reset | fixed | Joint point summed as the asm (the tractor hitch order). |
| `setPosition` | dgTrailerJoint::SetPosition | fixed | Sums as the asm. Never called in MM2. |
| `setCosFreeLean`, `setRotate1`/`2`, `setFrictionLean`/`Roll`, `setLeanLimit`, `setRollLimit` (both), `setRestOrientMat` (both), `setForceLimit`, `setJointForceFlag`, `moveICS`, `breakJoint`, `unbreakJoint`, `isBroken` | same names | verified | |
| `setRestOrientation` | dgTrailerJoint::SetRestOrientation | fixed | The in-place Dot3x3Transpose on the tractor's rotation. Never called in MM2. |
| `update` | dgTrailerJoint::Update | fixed | Every branch, constant and the force rotation through the matrix still holding C2 matched. The trailer's hitch point now sums in its own order, the gap length is Vector3::Mag2, the rotation is RotateUnitAxis (in-place Dot3x3) with MakeRotateUnitAxis's single-precision products, 2 sin(m/2)/m rounds fsin once, and the products go through the exact Matrix34 helpers. |
| `update`: Ctrl+B | dgTrailerJoint::Update | fixed | MM2 breaks the hitch when B is newly pressed with either Ctrl held (ioKeyboard). Wired by the vehicle audit's second pass (27e8faf): `Trailer::breakKeyPressed`, set by RaceScreen, breaks the joint before `joint.update`. |
| `doJointTorque` | dgTrailerJoint::DoJointTorque | fixed | Matched (DampLinearLean built but never added; the roll block never runs with roll 0 and FreeRoll 0.1); lv normalised with Vector3::InvMag's order. |
| `doJointLimits` | dgTrailerJoint::DoJointLimits | fixed | Matched; M through the two-argument Dot3x3Transpose. |
| `computeInvMassMatrix(ics, out, pos)` | dgTrailerJoint::ComputeInvMassMatrix (virtual) | fixed | C(pos) − SᵀKS matched; all products two-argument Dot3x3, now in MM2's order. |
| `computeInvMassMatrix(a, b, out, pos)` | the two-body overload | inferred | Never called; MM2's builds its levers from uninitialised matrices (undefined output). OpenMM2's is a reconstruction. |
| `kJointSpring`, `kJointDamp`, `kJointConst`, `kForceLimitUnit` | 40/pi, 2 sqrt(40/pi), 10, 10000 | verified | |
| `sub`, `dotZYX`, `cross`, `mulRow`, `rotation3x3`, `diag`, `hitchPoint`, `jointCoupling`, `subtractJointPart` | inline code, Vector3::Dot, Cross, Matrix34::MakeScale | verified | |
| `trailerHitchPoint` | dgTrailerJoint::Update | fixed | Added. |
| `mm2ForceRotation` | none | deviation | OpenMM2 switch, default MM2's rotation. |
| `lean`, `leanRate`, `roll`, `rollRate`, `jointForce`, `gap` | none | openmm2 | Diagnostics. |

## phys/Level.h (lvlInstance, lvlLevel, cityLevel as the manager uses them)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Instance::bound`, `matrix`, `position`, `radius`, `entity`, `attachEntity` | lvlInstance::GetBound, GetMatrix, GetPosition, GetRadius, GetEntity, AttachEntity | verified | Interface; GetBound(1) gives spheres and hotdogs their box (dgBangerInstance / lvlInstance::GetBound). |
| `Instance::attached` | dgPhysEntity vtable 0x24 | verified | Called after the impacts of a new mover. |
| `isBanger`, `bangerImpulseLimit2`, `bangerSphere`, `bangerHit`, `bangerHeld` | flag 0x1, dgBangerData ImpulseLimit2 (+0x54), the YRadius sphere, lvlInstance vtable 0x70, dgPhysEntity vtable 0x1c | verified | |
| `collidable`, `terrainCollidable`, `multiRoom`, `hitByPlayer`, `room` | flags 0x10, 0x100, 0x800, 0x8000; room (+6) | verified | |
| `audioId` | the instance data's collider id | inferred | |
| `LevelBound`, `collidePolyToLevel`, `findLevelImpacts`, `Level::material` | lvlLevelBound, lvlSDL::CollidePolyToLevel | verified | Declarations; implemented in BoundPolygonal.cpp (phys-bounds). |
| `Level::findRoom`, `touchedNeighbors`, `collect`, `instances` | lvlLevel::FindRoomId (vtable 0x1c), cityLevel::GetTouchedNeighbors (0x28), sdlPage16::Collect, the room lists | verified | Interface (CityLevel implements). |
| `Instance::wheelCollidable` | lvlInstance flag 0x20 | fixed | Added: the mask every dgPhysManager::Collide caller passes. CityLevel sets it as lvlLevel::LoadInstances does (flags 0x130 for the collidable instances, 0x110 for the terrain-bound ones whose record has 0x400); bangers (0x13) and cars do not carry it. |
| `Level::roomFlags`, `collectProbe` | lvlSDL's room flag bytes; sdlPage16::CollideSegment's Collect loop with the probed room global | fixed | Added for the wheel probe; CityLevel implements both (batches of 256 with the resume state, the room marked for the SpecialBound rule). |
| `ProbeCache` | lvlSegmentInfo with AllocateState's sdlPolyCached | fixed | Added: the rooms of the segment's ends and the cached polygon (InitFromPoly copies its normal, edge normals, material byte and corners; a new state starts stale). The wheel keeps one (vehWheel::Init). |

## phys/Material (phMaterial, lvlMaterial, lvlMaterialMgr)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Material` member defaults | none | deviation | OpenMM2's (test ground). MM2's defaults are `lvlMaterialDefault()`. |
| `lvlMaterialDefault` | lvlMaterial::lvlMaterial (with phMaterial's) | fixed | Added: elasticity 0.5, friction 1, drag 0, width 1, height and depth 0, particles -1/-1, thresholds 0.25/0.5, effect and sound -1, "default". |
| `parseMaterials` | lvlMaterial::Load | fixed | A block starts from lvlMaterialDefault (was 0.9/0.9, width 0, sound 0); sound "none" is 0, otherwise atoi. Field order is free in OpenMM2 (MM2 matches the tokens in order); the retail file reads the same. |
| `Reader`, `toFloat`, `isSpace` | datAsciiTokenizer | inferred | Tokenizer glue. |
| `MaterialTable::MaterialTable` | lvlMaterialMgr::lvlMaterialMgr | fixed | Entry 0 is the built-in default (lvlMaterial's constructor, "default"); was a "_default" with OpenMM2's 0.9/0.9 that materials.mtl's "_default" replaced. MM2 appends "_default" as its own entry, so the wheels on the 3286 city textures that materials.csv maps to `none` (and any unmapped one) get friction 1, not 0.9. |
| `MaterialTable::add` | lvlMaterialMgr::Load | fixed | A known name keeps its first definition (was replaced). |
| `MaterialTable::find`, `resolve`, `operator[]`, `size` | phMaterialMgr::Find (HashTable), lvlMaterialMgr::Lookup | deviation | Case-insensitive (MM2's hash is case-sensitive); every retail name is lower case. Unknown names give entry 0, as an unknown material byte does. |

## phys/PolygonSoup

OpenMM2's probe index: one polygon soup with an XZ grid (the render mesh of
the PSDL with facade bounds, plus the static instances' bounds) answering
"what does this segment hit first" for cameras, spawning, the AI and the
cheap traffic wheels. MM2 answers every such probe with dgPhysManager::
Collide and the mask 0x20 (vehWheel, vehWheelCheap, mmGame::FindGroundPos,
the cameras, aiPedestrian, aiVehicleActive::Detach, the cable cars and
subway...), which `World::wheelProbe` now ports; the wheels of physics cars
and trailers use it.

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `SoupGeometry::bounds`, `SoupPolygon::finalize`, `containsProjected`, `closestPoint`, `PolygonSoup::add` (both), `finalize`, `cellRange`, `query`, `Obb` | none | openmm2 | The spatial index. |
| `segmentPolygon`, `PolygonSoup::raycast`, `World::probe` | dgPhysManager::Collide (mask 0x20) for the callers other than the wheels | open | Nearest hit wins as in MM2. The polygon set differs: the render mesh rather than sdlPage16::Collect's collision polygons, every polygon whose box the segment's box overlaps rather than the rooms of its ends, and the static instances at load rather than the flag-0x20 instances. On the retail cities (20,000 random 2.5 m vertical probes near road vertices per city) the two agree on the height within 1 cm for 95% (London) and 91% (San Francisco) of the hits and on the material for 99.7%; the rest are SpecialBound triangles, terrain-bound instances without the wheel mask, and soup-only hits. The fix is in the callers (cameras, AI, spawning, cheap traffic wheels): call `World::wheelProbe(a, b, hit, self, nullptr)`. |

## phys/World (dgPhysManager, dgPhysEntity)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `GroundQuery`, `FlatGround` | none | openmm2 | Probe interface; a plane for tests and simcar (`GroundQuery::wheelProbe` defaults to `probe`). |
| `wheelProbe` | dgPhysManager::Collide(segment, intersection, room, instance, 0x20, 0) | fixed | Added (the wheels used the soup). lvlSegment::Set (CalculateInfo), the end rooms looked up from the segment info's, t = 2 and no polygon, the level (below), then CollideProbe on the start room's instances with the mask, those of the end room when it differs, and when the start room has the warp flag (0x40) those of the room it leads to (411 → 102, 412 → 122, 423 → 96, 625 → 1). The excluded instance is the car's own (vehWheel passes its vehCarSim's instance; trailer wheels none). The material: the level's by the polygon's byte, or the bound's, resolved by name in the World's table; a hit on the cached polygon alone keeps the material of the probe before, as the wheel's lvlIntersection does. Without a level: `probe`. |
| `collideLevelProbe` | lvlSDL::CollideProbe | fixed | Added. An end in an instance room (0x80) makes the cache stale; a valid cached polygon the segment crosses (TestSegmentUndirected, reject (t, 2)) answers alone, else it goes stale. Sphere: centre (b − a) · 0.5 + a per component, radius (1 / invLength) · 0.51. Rooms: start (if not 0), end (if neither start nor 0), then the start room's perimeter neighbours with flag 0x80, each once, at most 10, skipping start, end and 0. |
| `collideRoomSegment` | sdlPage16::CollideSegment | fixed | Added. Every collected polygon tested with reject (t, 2), so an equal t replaces the earlier hit; the material byte of each accepted one; on a hit the segment's ends and polygon 0 stored and the room's last accepted polygon cached (InitFromPoly), without one the cache goes stale. MM2 tests a room's polygons in batches of 256 and caches the polygon at the kept index of the last batch, which is another polygon when the hit was in an earlier batch; OpenMM2 caches the kept one (deviation, needs over 256 polygons of one room within a wheel probe's sphere). |
| `collideProbe` | dgPhysManager::CollideProbe | fixed | Added. Midpoint (a + b) · 0.5, reach 0.5 / invLength + GetRadius, distance² summed (z, y, x) against reach² (MM2 compares the unrounded x87 values), GetMatrix, the segment into the instance's frame (Vector3::Dot of each row with the end minus the position; vertical false, invLength 0, the kind kept), the bound's TestProbe up to the hit's t, then phIntersectionPoint::Transform, the material byte of the hit polygon when there is one. |
| `Body::Body`, `place`, `resetCollider` | the owners' phCollider::Init + Reset | inferred | Glue. |
| `Body::syncBoundMatrix` | vehCarSim::SetWorldMatrix, the instances' GetMatrix | inferred | The bound's world matrix from the ICS. |
| `Body::bound`, `entity` | lvlInstance::GetBound(0 / 1), GetEntity | verified | |
| `Body::radius` | lvlInstance::GetRadius | fixed | MM2's radius is the largest LOD model's radius (lvlInstance::GetGeomSet). Cars and trailers now carry it (the vehicle audit's `VehicleBody`, 40c6859); traffic overrides it with its own; props take their model's radius from CityLevel/BangerSet. |
| `Body::aabb` | none | openmm2 | |
| `World::World` | dgPhysManager::Reset | fixed | The level's collider is now phCollider::Init(bound, identity): barely moved. |
| `setLevel`, `setStatic`, `setMaterials`, `time`, `stats`, `randomSeed`, `seedRandom`, `interpolationAlpha` | none | openmm2 | |
| `add`, `remove`, `contains` | dgPhysManager::DeclareMover, ResetTable | deviation | A persistent list, where MM2's owners declare their movers anew every frame and the table is reset after it; a body keeps its last declaration (`Body::declare`) and `declared` false stands for a frame without one. `add` no longer sets gravity (the body's default is MM2's). |
| `Body::declare`, `updates`, `moverType`, `declared` | dgPhysManager::DeclareMover | fixed | Added: the type and the flags (0x1 update, 0x2 city, 0x8 objects, 0x10 movers; type 4 is PlayerInst). OpenMM2 had only the three collision flags. |
| `beginFrame` | dgPhysManager::DeclareMover, Update (first loop) | fixed | Was only the clearing of the 0x8000 hit-by-player marks. Now also: the declared bodies in order up to 32 (with a level, those in a room; a body still in room 0 finds its room first, standing in for the owner's MoveToRoom at placement), the rooms of type-3 and type-4 movers and their neighbours (cityLevel::GetNeighbors) active up to 20, and type-1 movers outside them left out of the frame and detached (lvlInstance::Detach). With no active room every type-1 mover goes, as in MM2. |
| `ignoreMover` | dgPhysManager::IgnoreMover | fixed | Added: the mover takes no further part in the frame. |
| `isActive` | none | openmm2 | Query. |
| `addNewMover` | dgPhysManager::NewMover(new, old) | fixed | Flag 0x100 until the sample ends. A body already in the list now starts over as new (MM2 resets its entry to 0x100), and a full table (32) refuses it. MM2 also clears the new mover from the old one's collidables, which the loop has already passed. |
| `Instance::detach`, `Level::neighbors` | lvlInstance::Detach, cityLevel::GetNeighbors | fixed | Added hooks (no-ops by default); CityLevel implements `neighbors` from the PSDL perimeters. |
| `advanceFixed` | none | deviation | OpenMM2's fixed-step driver (one 1/60 s sample per step). |
| `advanceOversampled` | dgPhysManager::Update, datTimeManager::SetTempOverSampling | fixed | n = min(ceil((frame − 0.001) / step), max): a frame under a millisecond now runs no sample (OpenMM2 forced one). |
| `step` | dgPhysManager::Update (one sample) | fixed | Order matched: the entities' updates (weight, phSleep, ICS, the entity's own), GatherCollidables, per mover the city, the later movers (joint check, then TrivialCollideInstances) and the gathered instances, new movers 0x100 → 0x1b, UpdateMtx. Only the movers in this frame's table take part, by their flags (update, UpdateMtx and the hooks with 0x1; gathering and the gathered instances with 0x2 or 0x8; the movers with 0x10). World computed every collider's CalcMaxMoved each sample; MM2 never does (only aiVehicleActive::Attach), so movers keep barely-moved false: removed. The entity PreUpdate/PostUpdate run once per frame in MM2; OpenMM2's hooks run per sample (the same at one sample per frame). |
| `trivialCollide` | dgPhysManager::TrivialCollideInstances | verified | Banger YRadius spheres (enabled by a global mmGame::Init sets) with the other centre's height; sums as the asm. |
| `gatherCollidables` | dgPhysManager::GatherCollidables | verified | Room 0 skipped; rooms touched by the sphere (at most 8); flags 0x18/0x2; multi-room instances only from the mover's room; at most 32. |
| `collideTerrain` | dgPhysManager::CollideTerrain | verified | Sphere and radius of bound 0, bound 1 for spheres and hotdogs (a box for every race object), the last matrix moved by the last push when the level pushed hardest, CollidePolyToLevel with sweeps, weight 1/n. The sphere and hotdog searches against the city (unreached: bound 1 is a box) are not ported. CollideTerrain's branch asking the entity whether it needs the city (vehCarSim / vehTrailer RequiresTerrainCollision) is gated by a global mmGame::Init sets to 0, so in a race MM2 never asks; World does not either. |
| `collideInstances` | dgPhysManager::CollideInstances | verified | Temporary colliders, relPos, player marks, AttachEntity and the collider rewiring, NewMover, phContactMgr::CalcImpact or dgImpact for bangers. Force spheres (phCollision::TestBoundForce) are not ported: none in a race. |

## Missing

| MM2 | What it does | Status |
| --- | --- | --- |
| The owners' DeclareMover calls | Each frame mmGame declares the player type 4 with 0x1b and its trailer type 2 with 0x1b; aiRouteRacer an opponent type 3 with 0x1b within 200 m of a player, else type 2 with 0x13; aiPoliceOfficer type 2 with 0x1b within 200 m, 0x13 to 250 m, nothing beyond; aiVehicleManager its attached traffic type 2 with 0x1b; dgBangerActiveManager a knocked-over prop by its data's flags (0x10: type 1 with 0x1b; 0x4: type 1 with 0x3; 0x40: type 2 with 0x1b; 0x2: not declared, updated directly) or, in its age mode, type 1 with 0x1b, then 0x3, then not declared. | open: World supports it (`Body::declare(type, flags)` each frame, `declared = false` for a frame without a declaration, `Instance::detach`, `World::ignoreMover`); the session/vehicle, ai-vehicles and camera-props owners must call it. 0x13 and 0x1b behave the same in the manager (0x8 only matters without 0x2 and 0x10). |
| dgPhysManager::CollideTerrain for entity-less movers | Movers declared without an entity (traffic in aiGoalCollision / aiGoalRegainRail, flags 8 and 10) collide with the city and attach an entity when they hit it. | open: needs traffic instances declared as movers (ai-vehicles). |
| phCollision::TestBoundForce, SphereApplyCenterForceTo* | Force spheres push bodies without impacts. | inferred unused in a race (no force-sphere bound in the retail data). |
| phInertialCS::UpdateOversample, UpdateOversampleDone, phContactMgr's contact path, phImpact::Contact / EffectiveMass / GetRelDisplacement | Contacts and their oversampling. | Disabled in a race (phContact::DisableContacts). |
| phInertialCS::InitSphere/Cylinder/Hotdog/FromGeometry, AddInertia, SubtractInertia, FindPrincipalAxes, Rotate | Other mass set-ups and inertia algebra. | Not used by race entities (they use InitBoxMass); not ported. |
| phSleep::WakeUpNextTime, SetAsleepCB | Timed wake-up, sleep callback. | Unused in a race. |
| phSleep::SmoothAngInertia | Raises small moments to max / ratio. | Ported in BangerSet.cpp (camera-props). |
| The elasticity cheat | mmGame::SendChatMessage "blubber" sets the cap to 4. | open: `setElasticityCap` exists; the chat hook belongs to the session area. |

## Changes outside the area

- `src/game/CityLevel` (ai-ambient-city): an unmapped or boundless probe
  polygon is "default" (the material manager's built-in entry 0), not
  "_default"; `neighbors` (cityLevel::GetNeighbors from the PSDL
  perimeters), `roomFlags` and `collectProbe` (the wheel probe's
  collection) implemented; the static instances get `wheelCollidable`.
- `src/phys/vehicle/Wheel`, `CarSim.cpp` (vehicle): the wheel probes with
  `wheelProbe(top, bottom, hit, env.self, &probeCache)`; `WheelEnv::self` is
  the car's body (a trailer's wheels pass none, as vehWheel does without a
  vehCarSim); each wheel keeps its `ProbeCache`.

## What other areas must pick up

- The owners' `Body::declare(type, flags)` calls each frame (levels in the
  Missing table) and `declared = false` for a frame without one; bangers
  implement `Instance::detach` (dgHitBangerInstance::Detach) and call
  `World::ignoreMover` when they let a body go.
- The other ground probes (cameras, AI, spawning, vehWheelCheap) are the
  same dgPhysManager::Collide with mask 0x20: `World::wheelProbe(a, b, hit,
  self, nullptr)`.
- `limitAngVelocity = true` in CarSim / Trailer is now the default
  (removed by the vehicle audit's second pass).
- simcar's material fallback names "default" (entry 0) since the vehicle
  audit's second pass.
- The "blubber" chat cheat: `setElasticityCap(kBlubberElasticityCap)`.
- Traffic movers without a body. (`Body::radius` for cars and trailers and
  the trailer's Ctrl+B break are done by the vehicle audit's second pass.)
