# Parity audit: camera-props

Audited against MM2Recomp (midtown2.exe build 3393) on 2026-10-07; second
pass on 2026-10-08 (PKG xrefs as bangers, banger movers, the cameras'
probe, the XCams).

Summary: 195 functions; verified 107, fixed 57, deviation 11, inferred 2,
open 0, openmm2 18.

Missing (MM2 code without an OpenMM2 counterpart, listed at the end): 7 open,
3 owned by other areas, 9 not needed.

Scope: the car cameras and the view (`src/game/Cam*`, `Camera.*`), the
rear-view mirror's camera (`CamMirror.*`, added), props (`src/game/bangers`),
the vehicle and city lists (`Catalog.*`) and driver profiles
(`Profile.*`). Rows are per function or closely related group; "fixed"
rows include features that were missing and are now ported. Struct offsets
cite MM2 layouts. Tests: `tests/game/test_parity_camera_props.cpp`,
`test_parity_camera_props_bangers.cpp`, `test_parity_camera_props_profile.cpp`
and the existing `test_camera.cpp`, `test_bangers.cpp`, `test_catalog.cpp`,
`test_profile.cpp`.

## Camera.h / Camera.cpp (render-facing view)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Camera` (`transform`, `horizontalFov`, `nearPlane`, `farPlane`, `view`, `position`, `forward`) | `gfxRenderState::SetCamera`, `gfxViewport::Perspective` | openmm2 | The renderer's view description. The cameras fill it through `CameraView::apply`; RaceScreen replaces `farPlane` with the far clip, as `mmGame::FarClipCB` replaces MM2's camera far global. |
| `Camera::lookAt` | none | openmm2 | Used only by the debug fly and orbit cameras; not `Matrix34::LookAt` (the car cameras use `cam::lookAt`). |
| `Frustum::Frustum`, `intersects`, `intersectsSphere` | none | openmm2 | Renderer culling helper (Vulkan/OpenGL depth range 0..1). |

## CamMath (Angel Matrix34 / Vector3 helpers)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `cam::invMag` | `Vector3::InvMag` | verified | (x²+y²)+z², 0 for a zero vector (association checked in the asm of its inlined copies in `camTrackCS::UpdateHill`). |
| `cam::scaled` | `Vector3::Scale` | verified | |
| `cam::angle` | `Vector3::Angle` | verified | 1/sqrt(|b|²·|a|²), dot > 0.9999999 gives 0, < -1 gives pi; sums checked in the asm. |
| `cam::lookAt` | `Matrix34::LookAt` | verified | m2 = normalised from - to, m0 = normalised YAXIS × m2, m1 = m2 × m0 (not renormalised), m3 = from; asm checked. |
| `cam::getEulersZXY` | `Matrix34::GetEulers("zxy")` | deviation | Same atan2 / asin terms; the asin argument is clamped to [-1, 1] where MM2 could return a NaN from a rounding excess. Every camera call passes "zxy" (checked in the asm). |
| `cam::fromEulersZXY` | `Matrix34::FromEulersZXY` | verified | Zero angles skip sin/cos; products in MM2's order. The x87 keeps sin/cos at extended precision, so the last bit can differ. |
| `cam::makeRotate` (+ `makeRotateX/Y/Z`, `makeRotateUnitAxis`) | `Matrix34::MakeRotate`, `MakeRotateX/Y/Z`, `MakeRotateUnitAxis` | verified | Angle 0 gives the identity; axis-aligned axes fold the sign into the angle; other axes are normalised. |
| `cam::dot`, `cam::dot3x3` (`dotRows`) | `Matrix34::Dot`, `Dot3x3` | verified | Row-vector product; m3 also gets b.m3. Per-element association from the asm (earlier pass). |
| `cam::rotate`, `cam::rotateFull` | `Matrix34::Rotate`, `RotateFull` | verified | MakeRotate then Dot3x3 / Dot with a zero translation. |
| `cam::polarView` | `Matrix34::PolarView` | verified | FromEulersZXY(-incline, azimuth, twist), m3 = distance × m2. |
| `cam::approach` | `Vector3::Approach` | verified | x first; y only once x has arrived, z once y has. |
| `cam::horizontalFov4x3` | none | openmm2 | Converts CameraFOV (vertical, as `gfxViewport::Perspective` takes it) to `Camera`'s 4:3 horizontal FOV for the any-resolution projection. |

## CamParams (tune/camera files)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `BaseCamParams` defaults | `camBaseCS::camBaseCS` | verified | BlendTime 1.2, BlendGoal 1, CameraFOV 50, CameraNear 3. CameraFar 1600 is what every `camTrackCS` / `camPovCS` constructor writes to MM2's far global (its static initial value is 600). |
| `AppCamParams` defaults | `camAppCS::camAppCS` | verified | ApproachOn 0, AppAppOn 0, AppRot / AppXRot / AppYPos 0.5, AppXZPos 0, AppApp 0.1, AppRotMin / AppPosMin 0.5, LookAbove 0, TrackTo (0, 0.8, 0), MaxDist / MinDist 0, LookAt 0.01. |
| `TrackCamParams::TrackCamParams` | `camTrackCS::camTrackCS` | verified | Every field and the collision margin 0.33 compared. |
| `PovCamParams::PovCamParams` | `camPovCS::camPovCS` | verified | Offset (0, 1.6, 0.7), ReverseOffset (0, 1.7, 0.75), AppRot / AppXZPos / AppYPos 28, MinDist 1.74, MaxDist 1.8. |
| `loadBaseCamParams` | `camBaseCS::FileIO` | deviation | CameraFar is kept per camera; MM2 reads it into one global (the last file loaded wins, the car's dash camera) that `mmGame::FarClipCB` then sets from the far clip. RaceScreen draws with the far clip, so the files' values (100 to 1330 in retail) have no effect in either game. |
| `loadAppCamParams` | `camAppCS::FileIO` | verified | |
| `TrackCamParams::load` | `camTrackCS::FileIO`, `AfterLoad` | verified | CameraNear forced to 0.5 after a successful load. |
| `PovCamParams::load` | `camPovCS::FileIO`, `AfterLoad` | verified | CameraNear forced to 0.1. |
| `loadTrackCamParams`, `loadPovCamParams` (`readDat`) | `camCarCS::Init` → `asNode::Load`, `datParser::Read` | verified | A missing file keeps the defaults and skips AfterLoad. Number parsing (coordinator's check): `datParser::Read` converts the tokens after a record's name with its tokenizer, `DatFile` only accepts whole numeric tokens; every value token in the 96 retail camera files is a plain decimal and the int fields hold integers, so both read the same values. |

## CamCar (camBaseCS / camAppCS / camCarCS)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `CameraTarget` | the `vehCarSim` fields the cameras read | fixed | The spin test field was an angular velocity; MM2 reads `vehCarSim +0x60`, the inertial body's angular momentum (`phInertialCS +0x48`). Now `angularMomentum`; RaceScreen passes the body's. Other fields: speed +0x248, steering +0x1554, throttle (engine +0x60), hand brake +0x1550, gear +0x304, wheel contacts and normals; `roomFlags` from the car's room (RaceScreen passes none yet). |
| `CameraTarget::wheelsOnGround` | `vehCarSim::OnGround` | verified | |
| `cameraPanFor` | `mmInput::GetCamPan` | verified | Digital buttons; the analog "Camera Pan" axis is input's. |
| `CameraInput::orbit` | keyboard reads in `camPolarCS::Update` | fixed | Added with the polar camera (Delete / Page Down / End / Home / Page Up / Insert, Shift). |
| `CarCamera::forceMatrixDelta` | `camBaseCS::ForceMatrixDelta` | verified | |
| `CarCamera::apply` | none | openmm2 | Writes one camera to `Camera` (tests). |
| `CarCamera::dApproach` | `camAppCS::DApproach` | verified | Quadratic ramp, AppApp low-pass, sign flip stops. |
| `CarCamera::trackToWorld` | `camAppCS::UpdateApproach` (TrackTo) | verified | Association checked in the asm. |
| `CarCamera::updateMaxDist` | `camAppCS::UpdateMaxDist` | verified | asm checked. |
| `CarCamera::updateApproach` | `camAppCS::UpdateApproach` | verified | Position x, y, z then rotation z, x, y; LookAt blend; ±90° unwrap. |
| `CarCamera::approachIt` | `camAppCS::ApproachIt` | verified | |

## CamTrack (camTrackCS)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `TrackCamera::TrackCamera`, `setParams` | `camTrackCS::camTrackCS`, `camCarCS::Init` | verified | |
| `TrackCamera::reset` | `camTrackCS::Reset` | verified | |
| `TrackCamera::update` | `camTrackCS::Update` | verified | The swing spline update is skipped: nothing calls `SwingToRear`, `Front` or `Rear`. |
| `TrackCamera::updateCar` | `camTrackCS::UpdateCar` | fixed | The "spinning really fast" test compared the angular velocity with 1500; MM2 compares the angular momentum (|L|² > 2.25e6 while off the ground for 0.1 s), which a car tumbling through the air reaches, so the camera keeps its offset instead of following the heading. Reverse swing, hand brake and TrackBreak verified. |
| `TrackCamera::updateHill` | `camTrackCS::UpdateHill` | verified | asm checked, including the tilt < 0.01 branch. |
| `TrackCamera::updateTrack` | `camTrackCS::UpdateTrack` | verified | |
| `TrackCamera::preApproach` | `camTrackCS::PreApproach` | verified | |
| `TrackCamera::minMax` | `camTrackCS::MinMax` | fixed | The algorithm verified. The probe RaceScreen passed was the static polygon soup; MM2 collides through dgPhysManager::Collide with mask 0x20, ignoring the player's car (the city's collision polygons and the rooms' instances flagged 0x20). RaceScreen now passes World::wheelProbe with the player's body as the one never hit. |
| `TrackCamera::collide` | `camTrackCS::Collide` | fixed | Type 1 near-plane corners (tan from `gfxViewport::Perspective`, +0.33), type 2 pull-in, verified; the probe is World::wheelProbe now (see minMax). |
| `setCollideMargin` | `camTrackCS +0x180` (set by `mmPlayer::Update`) | verified | |

## CamPov (camPovCS)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `PovCamera::PovCamera`, `setParams` | `camPovCS::camPovCS` | verified | |
| `PovCamera::reset` | `camPovCS::Reset` | verified | |
| `PovCamera::update` | `camPovCS::Update`, `UpdatePOV` | verified | Pitch about m0, then the pan (or pi in the reverse mode) about m1. |
| `setPan` | `mmGame::UpdateGameInput` (`camPovCS +0x144`) | verified | |
| `setReverseMode` | `camPovCS +0x10C` | verified | No MM2 code sets it. |
| `display` | `mmPlayer::IsPOV`, `vehCarModel::SetVisible` | verified | |

## CamRace (camPreCS, camPointCS, camPolarCS)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `PreCamera::PreCamera` / `Params` | `camPreCS::camPreCS`, `Init` | verified | PolarHeight 2, PolarDistance 22, PolarIncline 1.1, AzimuthOffset 0, BlendTime 3; no file loaded. |
| `PreCamera::makeActive` | `camPreCS::MakeActive` | verified | |
| `PreCamera::update` | `camPreCS::Update` | verified | |
| `PointCamera::PointCamera`, `setPosition`, `setVelocity`, `setMaxDist`, `setMinDist`, `setAppRate` | `camPointCS::camPointCS`, `SetPos`, `SetVel`, `SetMaxDist`, `SetMinDist`, `SetAppRate` | verified | MaxDist 70, near 0.5. |
| `PointCamera::update` | `camPointCS::Update` | verified | Zoom 60 → 25 degrees between 0.3 × MaxDist and MaxDist; asm checked. Sets the perspective even in wide-angle mode, as MM2. |
| `PolarCamera` (all) | `camPolarCS::camPolarCS`, `Update`, `FileIO` defaults | fixed | Was missing; ported for the multiplayer finish camera and the XCams (keyboard orbit, limits 0.5..200 m and ±pi, AzimuthLock). |

## CamView (camViewCS, camTransitionCS)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `CameraView::setCurrent` | `camViewCS::SetCam` | verified | The player's view (+0x48 set) does not set the perspective; MakeActive runs at the next update (needs car input). |
| `CameraView::newCam` | `camViewCS::NewCam`, `camTransitionCS::NextTransition` | verified | |
| `CameraView::reset` | `camViewCS::Reset` | verified | Sets the camera's own perspective even in wide-angle mode (MM2 quirk kept). |
| `CameraView::update` | `camViewCS::Update` | verified | |
| `newTransition`, `startTransition`, `startNextTransition`, `reverseTransition` | `camTransitionCS::NewTransition`, `StartTransition`, `StartNextTransition`, `ReverseTransition` | verified | The target camera's Reset / Update happen at the next update (needs car input), so it is still updated twice that frame. |
| `updateTransition` | `camTransitionCS::Update` | verified | FOV / near blend, curves 1..3, polar blend about each camera's look point. |
| `updateCamera`, `apply`, `matrix`, `setPerspective`, `setWideAngle` | `gfxRenderState::SetCamera`, `gfxViewport::Perspective`, `camViewCS +0x18` | openmm2 | Glue that stands in for the viewport state. |

## CamPlayer (mmPlayer / mmViewMgr camera handling)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `PlayerCameras::load` | `mmPlayer::Init` (camera part) | verified | near, far, ind, pov, dash; narrow screen (< 1.3:1) scales the dash Offset.z by 0.7352941 (RaceScreen does not pass the screen aspect: session). |
| `PlayerCameras::reset` | `mmPlayer::Reset` | verified | |
| `PlayerCameras::update` | `mmGame::UpdateGameInput` (CamPan), `mmPlayer::Update` | fixed | Big vehicles also switch to the _ind camera in flag 0x20 rooms with geometry over the camera (a segment from 100 m above the last rendered camera down to it); that probe was missing. Water camera, pre-race sequence, collision margin verified. |
| `PlayerCameras::toggleCamera` | `mmViewMgr::SetViewSetting(0)` | verified | |
| `PlayerCameras::toggleDashboard`, `setDashboard` | `mmViewMgr::SetViewSetting(6)` | verified | |
| `PlayerCameras::toggleWideAngle` | `mmViewMgr::SetViewSetting(5)` | verified | The map-mode conditions belong to the HUD. |
| `PlayerCameras::toggleXCam`, `setXCamCheat`, `xCam` | `mmViewMgr::SetViewSetting(2)`, `mmPlayer::GetNextCycleXCamIndex`, `GetCurrentXCamIndex`, `SetCamera` group 1, `mmPlayer::Init` (the XCams' settings) | fixed | Was missing: the first press blends (mode 3, 0.8 s) to the current XCam (camPolarCS about the car), turning the dashboard off and remembering it (the dash view's activated flag); the next press returns to the dashboard or the cycled camera. XcamCheat, which would cycle the two XCams, is never set in midtown2.exe. The dashboard key does nothing in an XCam (SetViewSetting(6) returns for group 1). |
| `PlayerCameras::select` | `mmPlayer::SetCamera` | openmm2 | Convenience for menus and tests. |
| `setCamera`, `carCam`, `currentCameraPtr`, `isPov`, `setWideFov`, `view` | `mmPlayer::SetCamera`, `CarCams`, `GetCurrentCameraPtr`, `IsPOV`, `SetWideFOV`, `GetCamera` | verified | Group 1 (the XCams) added with toggleXCam. Rain-audio interior flag in SetCamera is audio's. |
| `PlayerCameras::startPreRace` | `mmPlayer::SetPreRaceCam` | verified | |
| `PlayerCameras::startPostRace` | `mmPlayer::SetPostRaceCam` | verified | MaxDist 25, MinDist 5, AppRate 5, 3.5 m above the far camera. |
| `PlayerCameras::startMultiplayerPostRace` | `mmPlayer::SetMPPostCam` | fixed | Was missing (the doc said nothing calls it; `mmGameMulti::SetFinishCam` does). |
| `PlayerCameras::startWaterCam` | `mmPlayer::Update` (water) | verified | |
| `PlayerCameras::viewSettings`, `setViewSettings` | `mmPlayerConfig::GetViewSettings` / `SetViewSettings` (camera globals) | fixed | Was missing: MM2 keeps the camera, wide angle and dashboard per driver across races. |
| `PlayerCameras::display` | `mmPlayer::IsPOV`, dash view | verified | |
| `viewName`, `camera(View)` | none | openmm2 | |

## CamMirror (mmMirror) — new

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `MirrorParams`, `loadMirrorParams` | `mmMirror::mmMirror`, `FileIO`, `asNode::Load` | fixed | Was missing. tune/<car>.mmmirror (11 cars); plain decimals in all retail files. |
| `RearViewMirror::setParams`, `load`, `localMatrix` | `mmMirror::Init` | fixed | Turned pi about Y, m0 negated (mirrored), Position. |
| `RearViewMirror::worldMatrix` | `mmMirror::Cull` (camera) | fixed | |
| `RearViewMirror::viewport` | `mmMirror::Reset` | fixed | Top right, one pixel in. |
| `enabled`, `setEnabled`, `toggle` | `mmViewMgr::Init`, `SetViewSetting(9)` | fixed | |

## src/game/bangers/BangerData.{h,cpp}

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `BangerData` (defaults) | `dgBangerData::dgBangerData` | verified | Size (0.2, 0.5, 0.2), CG 0, Mass 50, Elasticity 0.5, Friction 0.9, ImpulseLimit2 0, YRadius 0, ColliderId 0, TexNumber 0, BillFlags 0, NumParts 0, AudioId 0, CollisionType 0x10, CollisionPrim 0. |
| `parseBangerData` | `dgBangerData::Load` | verified | MM2 reads the tokens by position in `dgBangerData::Save`'s order (AudioId, Size, CG, optional NumGlows (else 1) and that many GlowOffset, Mass, Elasticity, Friction, ImpulseLimit2, SpinAxis, Flash, NumParts, BirthRule, TexNumber, BillFlags, YRadius, then optional ColliderId, CollisionPrim, CollisionType); OpenMM2 reads by name. Same result on all 995 retail files: every file is in that order, every value token is a plain decimal (so DatFile's whole-token number parsing reads what MM2's tokenizer reads), NumGlows matches the GlowOffset count (974 × 0, 19 × 1; the two files without NumGlows carry one GlowOffset). Names containing `_tree` get BillFlags 0x200 (MM2: `strstr` on the name as given; retail names are lower case). BillFlags 0x100 (pivot from the geometry, glass instance) is not supported: no retail file sets it. |
| `parseBangerData` (mass guard) | — | deviation | A Mass of 0 or less becomes 1 (robustness; MM2 has no guard, no retail file is affected). |
| `BangerDataLibrary::BangerDataLibrary` | `dgBangerDataManager::dgBangerDataManager` | openmm2 | Indexes the tune/banger listing (skipping CVS leftovers `.#…`); MM2 loads entries on demand by name. |
| `BangerDataLibrary::find` | `dgBangerDataManager::AddBangerDataEntry` + `dgBangerData::Load` | verified | Loaded on first request; a failed load is remembered (MM2's second hash table). |
| `BangerDataLibrary::part` | `dgUnhitBangerInstance::InitBreakables` | verified | `<name>_BREAK%02d` from 1 (MM2 then finds part p at entry index + p + 1, which its add order makes the same entries). |
| `BangerDataLibrary::has` / `names` / `available` / `vfs` | — | openmm2 | Lookups for the placement and tools. |

## src/game/bangers/BangerSet.{h,cpp}

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `kAlphaRef`, `kUnlitAlphaRef`, `kTreeAlphaRef` | `cityLevel::DrawRooms`, `dgBangerInstance::Draw`, `dgTreeRenderer::RenderTrees` | verified | Alpha test GREATER 100 (the city's reference), 140 for BillFlags 0x80, 120 for trees. |
| `kSleepSpeed2`, `kSleepSpin2`, `kInertiaRatio`, `kLowestY`, CollisionType bits, `kEject*` | `dgBangerActive::dgBangerActive`, `Attach`, `PostUpdate`, `dgBangerActiveManager::Update`, `vehBreakableMgr::vehBreakableMgr` | verified | phSleep thresholds 0.1 / 0.5 (phSleep::Init's 0.005 / 0.01 replaced), SmoothAngInertia 40, y −100, bits 0x2 / 0x40 / 0x10 / 0x4 in that priority, ejection 4 ± 1 and 2 ± 1. |
| `smoothAngInertia` | `phSleep::SmoothAngInertia` | verified | Largest moment / ratio as the floor, then `phInertialCS::Init`. |
| `inverseInertiaMatrix` | `phInertialCS::GetInverseInertiaMatrix` | verified | Rows of the frame scaled by the inverse moments, then transposed frame · that. |
| `rowTimes` | `dgUnhitBangerInstance::Impact` | verified | Sums z, y, x as the asm does. |
| `variantOf` | `dgBangerInstance::SetVariant` | fixed | New: the variant modulo the geometry's variant count (OpenMM2 clamped, and trees always used paint job 0). |
| `boundsOf` | `dgBangerData::InitBound`, `AdjustPrim`, `dgBangerInstance::GetBound` | verified | CollisionPrim 0 geometry shifted by −CG (box of Size without it), 1 box, 2 hotdog (YRadius, Size.y), 3 sphere; own material with the data's elasticity and friction; GetBound(1) of a non-box is the plain box around it. Other CollisionPrim values get a box (MM2: no bound; no retail file). |
| `DataBounds::radius`, `Prop::radius` | `lvlInstance::GetRadius` | inferred | MM2 returns the geometry set's radius; OpenMM2 the bound's sphere about the CG (only the broad sphere test uses it). |
| `Active::beforeIntegrate` / `afterIntegrate` / `afterCollisions` | `dgBangerActive::Update` (as a mover) | verified | dgPhysEntity::Update's gravity is the world's; phSleep::Update before the integration; SetMatrix, age += seconds, room move. |
| `Prop::bound` | `dgBangerInstance::GetBound`, `dgPhysManager::NewMover` | deviation | An instance that left the rooms has no bound for the rest of the sample, standing in for NewMover taking the broken prop off the lists the movers gathered (phys::World keeps its lists). |
| `Prop::matrix` | `dgUnhitYBangerInstance` / `dgUnhitMtxBangerInstance` / `dgHitBangerInstance::GetMatrix` | verified | |
| `Prop::entity` / `attachEntity` | `dgBangerInstance::GetEntity` / `AttachEntity` | verified | |
| `Prop::isBanger` / `bangerImpulseLimit2` | lvlInstance flag 1, `dgImpact::CalcImpact`'s data read | verified | |
| `Prop::bangerSphere` | `dgPhysManager::TrivialCollideInstances` | verified | Ground point under the CG with YRadius (mmGame::Init turns that test on). |
| `Prop::bangerHit` / `bangerHeld` | `dgUnhitBangerInstance::Impact` / `dgBangerActive::DetachMe` | verified | |
| `BangerSet::BangerSet` | `dgBangerActiveManager::dgBangerActiveManager`, `dgBangerActive::dgBangerActive` | verified | 32 actives, list in index order, asParticles::Init(64, 2, 2). Each active's particles get their own random seed (MM2: the global frand; deviation shared with the effects). |
| `~BangerSet`, `newInstance`, `prop`, `body`, `bound`, `hitCount`, `setWorld`, `instancesIn`, `roomsTracked`, `findRoom`, `inWorld`, `syncActiveList` | — | openmm2 | Glue to phys::World and CityLevel. |
| `moveToRoom` | `lvlLevel::MoveToRoom` | inferred | Appends to the room list (MM2's list order not established). |
| `placeUnroomed` | `cityLevel::LoadPath`, `lvlLevel::LoadInstances` (xrefs) | fixed | Room of the placement point (FindRoomId of the path matrix's position, or of an xref's starting from its record's room); was the CG's. |
| `add` | `dgUnhitBangerInstance::RequestBanger`, `Init`, `SetVariant`, `MoveToRoom` | fixed | The CG offset is turned by the matrix as placed before the Y form keeps m00/m02 (was the reduced matrix; a tilted street prop's CG differed), and the variant is kept. |
| `reset` | `dgBangerActiveManager::Reset`, `dgBangerManager::Reset`, `dgUnhitBangerInstance::Reset` | verified | Same end state (MM2 resets the nodes in tree order, then lvlLevel::ResetInstances). |
| `getBanger` | `dgBangerManager::GetBanger` (`Init(40)`) | verified | Slot 0 twice after a wrap kept; the previous prop's active detached and its room left. OpenMM2 creates the 40 instances as first used. |
| `activeOf` | `dgBangerActiveManager::GetActive` | verified | |
| `managerAttach` | `dgBangerActiveManager::Attach` | verified | Refuses an attached instance; when full, the list's first is detached and reused. |
| `managerDetach` | `dgBangerActiveManager::Detach` | verified | Swap with the last attached. |
| `activeAttach` | `dgBangerActive::Attach` | verified | Not collidable, Zero + instance matrix, InitBoxMass(Mass, Size), collider with GetBound(3) (the data's bound) and ColliderId, momentum from GetVelocity (zero), SmoothAngInertia(40), WakeUp, age 0, debris blast. MM2 writes a stationary rule's position into the shared data's rule and returns before setting the rule when the prop is not standing; no retail rule with a texture is stationary. |
| `activeDetach` | `dgBangerActive::Detach` | verified | IgnoreMover, collidable again, link cleared; OpenMM2 also clears the particles (MM2 stops updating and drawing them; Attach resets them). |
| `detachMe` | `dgBangerActive::DetachMe` | verified | |
| `ActiveBody::detach`, `worldDetach` | `dgHitBangerInstance::Detach` (called by `dgPhysManager::Update` on a type-1 mover outside the active rooms) | fixed | Was missing: the hit instance's active detaches (DetachMe) and the instance leaves its room, so a knocked-over prop still moving vanishes when the cars leave it behind; a prop still standing keeps lvlInstance's empty Detach. World::beginFrame now defers the removals made from inside an owner's detach (phys-core, one change), and RaceScreen declares the player's car as the type-4 mover (mmGame::Update), without which no room would be active. |
| `newMover` | `dgPhysManager::NewMover` | verified | Refused for an instance in no room; collides with everything from the next sample. |
| `unhitImpact` | `dgUnhitBangerInstance::Impact` | verified | Checked against the asm (ground point, part CG, velocity change sums); with parts, data entry +p+1 = BREAK p+1, NewMover with the unhit prop for the first part only, then the prop's active DetachMe. |
| `directUpdate` | `dgBangerActive::Update` + `PostUpdate` (CollisionType 0x2) | verified | No retail data uses 0x2. |
| `declare` | `dgBangerActiveManager::Update` (second loop), `dgPhysManager::DeclareMover` | fixed | Flag priority 0x2, 0x40, 0x10, 0x4 verified; the mover type was not declared (every active was a type-2 mover). Now Body::declare: 0x40 (2, 0x1b), 0x10 (1, 0x1b), 0x4 (1, 0x3), 0x2 updated by the manager, else undeclared; the age mode (dgBangerDataManager +0x2a8a8, off after mmGame::Init) by age: (1, 0x1b) up to the second age, (1, 0x3) up to the first, then the manager's update (`setAgeMode`, ages 6 and 30000 s). |
| `update` | `dgBangerActive::PostUpdate`, `dgBangerActiveManager::Update` (first loop) | deviation | Actives asleep or below −100 detach as in MM2; one whose instance is in no room is detached (MM2 only takes it off the list, leaving the instance linked and uncollidable). Debris at a fixed step (effects). |
| `ejectPart` | `vehBreakableMgr::Eject` | verified | Checked against the asm: frand order y, x, z, sums (z, x, y) and (z, y, x), momentum (not velocity) speed ± 1, angular impulse 2 ± 1. OpenMM2 uses its own random stream. |
| `draw` (props, trees) | `dgBangerInstance::Draw`, `DrawTree`, `dgTreeRenderer::RenderTrees` | fixed | Trees always the high LOD, unlit, reference 120, after the others; unlit props reference 140. Trees now take the prop's variant (was paint job 0). No retail banger PKG has a SHADOW mesh, so DrawShadowMap draws nothing. |
| `draw` (glows) | `dgBangerInstance::DrawGlow`, `cityLevel::DrawRooms` | deviation | Card, size, flicker, colour and standing-only verified. MM2 calls DrawGlow for every instance of a visible room within the NoDraw distance; OpenMM2 only for props that pass their own LOD and frustum test (a lamp's glow can be missing at the edge of the view). |

## src/game/bangers/PropPlacement.{h,cpp}

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `PlacedProp`, `PropDef`, `PropRule` | — | openmm2 | Data carried between placement and BangerSet (`variant` added). |
| `parsePropDefs` | `parCsvFile::Load` / `GetFloat` / `GetInt` / `GetColumn` / `GetRow`, cityPropulator's def reader | fixed | Now read as parCsvFile does (OpenMM2 used CsvTable): at most 16 columns, every line a row (blank lines too), '#' ends a line, 255-character fgets chunks, a cell ends at a comma or a control character (bytes from 0x80 too) and keeps its spaces, numbers by atof / atoi prefix; the row found by the "name" column. Files: a def without files is kept; an empty cell between commas is an empty name that spends a use; the cell after a final comma does not exist (variant count from the last present of file2..file4). A missing column falls back to its usual position and a missing cell keeps the default (MM2 quits or reads a null cell; no retail file has either). Retail: the same 10,184 street props. |
| `parsePropRules` | `parCsvFile`, `cityPropulator::LookupRule` / `Propulate` | fixed | Same parCsvFile reading; a rule found by its "rulename" column; props from the "prop1" column to the last column (16 at most, so 15 props although proprules.csv names 20 columns), stopping at the row's end, empty names skipped (London n11left has one). Blank lines are rows without cells, which never match. |
| `decodePathPlacement` | `dgPath::Load` | fixed | The type byte is kept as read (was folded to 0 above 2); spacing byte × 0.25, 0 → 5 m. |
| `placePathSet` | `dgPath::Enumerate`, `cityLevel::LoadPath`, `LoadProp` | fixed | Type 2 as the asm does: length summed (z, x, y), step length / floor(length / spacing), walk while the spacing is left, position accumulated per step, Z = X × Y and Y = Z × X unnormalised, a prop skipped when a row is zero; types other than 0, 1, 2 place nothing. Types 0 and 1 verified (Y-banger form). Only models with banger data (RequestBanger). |
| `AiRoad::AiRoad` | `lvlAiMap::SetRoad` (bevel off), `LoadCurrent`, `GetNumVertexs`, `GetRoom` | fixed | New port: one road strip / rectangle strip / divided road per room slot, vertex count 2 × sections with the later rooms joining without their first section (was an inferred join that reversed rooms and dropped only repeated sections). |
| `AiRoad::isoLerp` | `lvlSDL::IsoLerp` | fixed | Length summed (z, y, x) (was x, y, z); room of the vertex starting the segment. A zero-length segment hit exactly gives its point (MM2 divides 0 by 0). |
| `AiRoad::sidewalkVertex` | `lvlAiMap::GetSidewalkVertex` | fixed | New: corner cutting by a third, at most 0.1 m (15 m for vertices 1 and n−2), constant 0.333; the current room as MM2 leaves it. |
| `AiRoad::sidewalkVertexMulti` | `lvlAiMap::GetSidewalkVertexMulti` | fixed | New; the 0x400000 curb pull (95% towards the centre) ported though no retail road has it. |
| `AiRoad::sidewalkVertexSingle` | `lvlAiMap::GetSidewalkVertexSingle` | fixed | New; left curb/outer vertices 1/0, right 2/3 (divided 4/5); without sidewalks vertex 0 left, 1 right. |
| `AiRoad::vertexSingleCenter` | `lvlAiMap::GetVertexSingleCenter` | fixed | New (used by the curb pull only). |
| `placeStreetProps` | `cityLevel::Load` (propulate loop), `cityPropulator::Propulate`, `lvlSDL::Propulate`, the propulator's placement callback | fixed | Rule 0 has no props; the left rule stands on the walk with side 1, walked first; X axis normalised with the (z, y, x) sum; maxUse spent per rule side and road even when the chosen file cell is empty; frand / irand order verified. Roads without the sidewalk flag are walked too (round 3: they draw as in MM2, and every candidate is degenerate, so nothing more stands); the stream's state after the last road is reported for the rest of the set-up (`docs/parity/round3/random-streams.md`). MM2 quits on a missing rule or def; OpenMM2 skips it. Retail effect: same 10,184 street props, positions within 7 cm, 10 room changes in London. |
| `racePropsName` | `cityLevel::Load`, `dgGameModeNames` | fixed | New: the race's path set name (roam, race%d, multicop, circuit%d, blitz%d, crash%d). |
| `placeCityProps` | `cityLevel::Load`, `lvlLevel::LoadInstances` | fixed | cityLevel::Load's order (street rules, .inst, _ai.inst, props.pathset, then the race's path set, which was not placed at all); .inst bangers keep the full matrix unless stored in the Y form and take the variant byte; after each record (banger or not) the bangers its geometry's PKG xrefs place (were drawn by CityRenderer as static children instead). Banger records are picked by banger data, MM2 by the record flag 0x200: identical on retail (107 flagged records, all with data, no unflagged record with data). |
| `pkgXrefs` | `lvlInstance::EndGeom` (`modPackage::OpenFile("xrefs")`) | fixed | New: the "xrefs" chunk of geometry/<model>.pkg (named with its NUL; a count, then 80-byte entries). Agrees with asset::parsePkg on all 33 retail models that have one. |
| `placeXrefs` | `lvlLevel::LoadInstances` (xref loop) | fixed | New: xref matrix times the record's (Matrix34::Dot); a zero row or a pair of rows with a dot product above 0.01 drops it ("bad x-ref matrix"), rows outside 0.97..1.03 squared length normalised; only models with banger data (RequestBanger, else "not exported": cl10's trees); full matrix, the record's variant, room found from the placement point starting at the record's room (`PlacedProp::roomHint`). Retail: 65 bangers in London, 368 in San Francisco. |

## src/game/bangers/RoadDecals.{h,cpp}

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `RoadDecals::load` | `dgRoadDecalInstance::dgRoadDecalInstance`, `cityLevel::Load` (decal loop) | verified | Paths of 3 or more points, points + 0.01 y, texture of the path's name, v per pair floor(distance / first width + 0.5). An odd strip's last point takes the last pair's v (MM2 reads past its table; 1 decal per city). |
| `RoadDecals::draw` | `dgRoadDecalInstance::DrawShadow`, `cityLevel::DrawRooms` (shadow pass) | deviation | Strip, u 0/1, v per pair verified. MM2 draws a decal with its room (the room of the midpoint of points 0 and 2) when that room is visible and within the NoDraw distance, in the room's colour; OpenMM2 draws every decal, white (the retail room colour). Render state is rendering-fx's. |
| `RoadDecals::size` / `decals` | — | openmm2 | |

## Vehicle and city lists (`src/game/Catalog.*`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `VehicleInfo` (fields, defaults) | `mmVehInfo::mmVehInfo`, `mmVehInfo::Load` | fixed | UIDist defaulted to 0; MM2 resets it to 6 before reading. ForceFeedbackModifier / RoadForceModifier were read from the file and defaulted to 0; MM2 never reads those lines (Load reads twelve fixed fields, then only UIDist), so the scales mmPlayer::Init copies to the input (mmVehInfo +0x118 / +0x11C) are always the constructor's 1. |
| `VehicleInfo::kFlag*` | readers of mmVehInfo +0xE0 | verified | 0x08 cop (mmMultiCR team), 0x13 big vehicles (mmPlayer::Update cameras), 0x01 / 0x02 (mmMultiRoam spawn), 0x40 (mmHudMap). Comment now cites the readers instead of a missing doc. |
| `parseVehicleInfo` | `mmVehInfo::Load` | fixed | MM2 reads BaseName, Description, Colors, Flags, Order, ScoringBias, UnlockScore, UnlockFlags, Horsepower, Top Speed, Durability, Mass with fscanf in sequence and marks the car invalid (not listed) when one is missing; OpenMM2 accepted a file with only BaseName. Now all twelve are required (in any order; MM2 also needs them in that order, not enforced). Values: every retail number is a plain decimal (some with a trailing tab or CR, trimmed), so `str::parseInt` / `parseDouble` give what fscanf `%d` / `%f` and atof give; atoi/atof prefix parsing of odd values (trailing text, hex) is not reproduced by core's parsers (formats area). BaseName is lowercased and looked up case-insensitively (MM2 strcmp): deviation for case-insensitive game data. Description keeps no trailing CR (MM2's `%[^\n]` keeps it): harmless. Colors stays a `|` list (MM2 keeps the raw string; the vehicle picker splits it). |
| `CityInfo` | `mmCityInfo` (+0x04 LocalizedName, +0x2C MapName, +0x54 RaceDir, +0x8C/+0x90/+0x94 counts) | fixed | see parseCityInfo. MustPlace and UnlockGroup were parsed and kept; mmCityInfo::Load never reads those lines, so they are no longer kept. |
| `parseCityInfo` | `mmCityInfo::Load` | fixed | OpenMM2 trusted the counts and resized the name lists to them; MM2 replaces a non-zero BlitzCount / CircuitCount / CheckpointCount by the number of names (string::NumSubStrings) and ignores the list when the count is 0. The nine fields MM2 reads (LocalizedName, MapName, RaceDir, the three counts, the three name lists) are now required. Retail .cinfo values are plain decimals. |
| `Catalog::load` (vehicles) | `mmVehList::LoadAll`, `mmVehList::Load`, `LoadVehListCB`, `isVehInfoFile` | fixed | The second pass took only tune/vp*.info; MM2's enumeration callback takes every tune/*.info (extension check only) and mmVehList::Load drops a car whose BaseName is already listed. Retail has only vp*.info, so the list is unchanged (20 cars). The built-in order matches MM2's vehLoadNameList table (vp + COOP, bug, cab, caddie, ford, mustang99, cop, bullet, panoz, bus, ddbus, century, coop2k, dune, vwcup, 4x4, auditt, db7, panozgt, semi), checked in the data. Enumeration order after the built-in list is the game data's: inferred. |
| `Catalog::load` (cities) | `mmCityList::LoadAll`, `mmCityList::Load` | fixed | Cities were listed in data order with no dedupe; MM2 loads sf.cinfo first, then every tune/*.cinfo, dropping a city whose RaceDir is already listed. (The game itself lists cities through `city::listCities`, see open items.) |
| `Catalog::vehicle` | `mmVehList::GetVehicleInfo(char*)` | deviation | Case-insensitive; returns null for an unknown name where MM2 returns the default vehicle (SetDefaultVehicle("vpcoop")). Callers fall back themselves. |
| `Catalog::city` | `mmCityList::GetCityInfo(char*)` | fixed | Looked up by file stem; MM2 looks up by RaceDir (case-insensitive). Retail stems equal RaceDir. |
| `Catalog::vehicles`, `cities`, helpers (`asText`, `hasAll`, `namesFor`, `isTuneFile`) | none | openmm2 | accessors and parsing glue |

## Driver profiles, records and unlocks (`src/game/Profile.*`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `RaceRecord` | `mmPlayerRecord` (+0x88 time, +0x8C car, +0xDC score, +0xE0 passed) | verified | |
| `Profile` (fields, defaults) | `mmPlayerData` (+0x88 name, +0xB0 net name, +0x128 difficulty, +0x130 car, +0x180 paint job, +0x184 mode, +0x188 race, +0x18D city), `mmPlayerData::Reset`, `mmInterface::PlayerCreate` | fixed | The last car defaulted to "vpbug", so a profile could not say "no race yet". mmPlayerData::Reset leaves it empty: PlayerFillStats then shows string 64 ("---") as LAST RACE and LAST VEHICLE and PlayerSetState selects vpbug in cruise (PlayerCreate sets the session car to the first listed car, vpcoop, but PlayerSetState, which it calls last, replaces it with vpbug because the driver has no car). The car now defaults to empty. Other defaults verified: paint job 0, net name string 77 "noname" (set by the frontend), mode 0 (cruise), race 0, the current city. `automatic` has no mmPlayerData field (MM2 keeps the transmission choice with the input config): inferred. The tag ID (+0x12C, ties the .rec files to the driver) has no use with INI files. |
| `Profile::hasLastRace`, `selectedVehicle`, `kDefaultVehicle` | `mmInterface::PlayerFillStats` (car empty), `PlayerSetState` (fallback "vpbug", checked in the data) | fixed | Added with the empty default above; the frontend takes the selected car from `selectedVehicle` (one line in FrontendScreen.cpp). |
| `Profile::camera`, `wideAngle`, `dashboard`, `mirror` | `mmPlayerConfig::GetViewSettings` / `SetViewSettings` / `DefaultViewSettings` (+0x7168: camera index, +0x716A wide angle, +0x716B dashboard) | fixed | Missing: MM2 keeps the cycled camera, wide angle and dashboard per driver (mmGame restores them at race start and saves them when the game ends). Added, saved in [Prefs] Camera / WideAngle / Dashboard / Mirror (+0x716C, the mmMirror node's on flag read by mmViewMgr::Init), defaults 0 / off / off / off; the camera number is clamped to 0..2 (mmPlayer::Reset indexes its three cameras with it). Wiring into the race is session's (with PlayerCameras::viewSettings). |
| `modeKey` | `dgGameModeNames` (reward RaceType names), MMSTATE numbers | verified | blitz = 4, circuit = 3, race = checkpoint 1, crash = 6. |
| `Profile::raceKey`, `Profile::record` | `mmPlayerCityRecord::GetRecord`, `GetFileOffset` | deviation | A keyed map instead of per-city binary files with record offsets; same information. |
| `Profile::load` | `mmPlayerData::Load` / `LoadBinary`, `mmPlayerCityRecord::Open` | deviation | OpenMM2's INI format (MM2: players/playerN.sav, players/<city>/playerN.rec, CRC checked). Conversion of earlier OpenMM2 files: openmm2. |
| `Profile::save` | `mmPlayerData::Save` / `SaveBinary`, `mmPlayerCityRecord::Close` | fixed | Times were written to two decimals, so a reloaded best time compared differently from the float MM2 keeps; now written in full (shortest round-trip form). Storage format: deviation. |
| `merge` | `mmPlayerCityRecord::NewRecord` | fixed | OpenMM2 took a finish whole only when no record existed; MM2 does so whenever the stored time is 0, and its passed mask is only ever set, so a pass survives the overwrite. Otherwise: lower time with its car (equal keeps the old car), higher score, passed ORed: verified. |
| `ProfileStore::ProfileStore`, `defaultDir` | `mmPlayerDirectory` (players/players.dir) | openmm2 | user data directory |
| `ProfileStore::list` | `mmPlayerDirectory::LoadBinary`, `GetPlayer` | verified | creation order (AddPlayer appends, RemovePlayer keeps the order); storage: deviation. |
| `ProfileStore::create` | `mmInterface::PlayerCreate`, `mmPlayerDirectory::AddPlayer`, `MakeFileName` | fixed | Trimmed the name; PlayerCreate refuses only an empty string (strlen 0), so " Ace " and a name of spaces are drivers. Now kept as typed; the INI files store names quoted (IniFile strips one pair of quotes after trimming). Limits verified: more than 17 drivers refused, exact case-sensitive duplicate refused (FindPlayer strcmp). File names are the sanitized name instead of playerN (deviation of the storage). |
| `ProfileStore::remove` | `mmInterface::PlayerRemove`, `mmPlayerDirectory::RemovePlayer` | verified | one file instead of .sav/.cfg/.rec files |
| `ProfileStore::lastUsed`, `setLastUsed` | `mmPlayerDirectory::GetLastPlayer`, `SetLastPlayer` | fixed | The name is now written quoted so a driver name with spaces round-trips. |
| `ProfileStore::kMaxDrivers`, `kMaxNameLength` | `mmInterface::PlayerCreate` (more than 17 refused), `Dialog_NewPlayer` text field (18) | verified | |
| `HallEntry` | `mmRecord` (+0x104 passed) | fixed | Entries lacked the passed flag MM2 stores with each race record (mmMiscData::NewRecord's passed argument, the mode's ProgressCheck result) and Dialog_HallOfFame shows (AddRaceRecord style 2 vs 1). Added, saved as a fifth field; filling and showing it is frontend-ui's. |
| `HallOfFame::key` | `mmMiscData::Open` (players/<city>/amateur, pro), `GetFileOffset` | deviation | one INI file keyed by difficulty, city, mode and race |
| `HallOfFame::submit` | `mmMiscData::NewRecord` | verified | Time list: before the first slower or empty slot; score list: before the first lower score; equal values stay ahead; the fifth drops out. MM2 writes only the time into a time slot and only the score into a score slot (the other field is stale, never shown); OpenMM2 stores the whole entry. |
| `HallOfFame::table` | `mmMiscData::GetRecord` | verified | |
| `HallOfFame::load`, `save` | `mmMiscData::Open`, `Init`, `Close` | fixed | Times now in full, the passed flag kept, and entries written quoted so a driver name keeps its spaces; the format itself: deviation. |
| `Reward` | `mmRewardRecord` (variant, car id, mode, race number, message) | verified | |
| `CityProgressInfo` | `mmPlayerCityRecord::InitCityRecord` counts (`mmInterface::PlayerInitStats`) | verified | checkpoint, circuit and blitz counts from the city, 13 lessons. |
| `Progress::Progress` | none | openmm2 | |
| `Progress::load` | `mmRewardList::Init(32)`, `mmRewardList::Load` | fixed | MM2 keeps a row only when its race type is one of dgGameModeNames up to the "%" (roam, race, multicop, circuit, blitz, croam, crash) and its car is in the vehicle list, warning otherwise; OpenMM2 kept every row. Now filtered (with the catalog); skipped rows do not count towards the 32. No retail row is affected. The message ends at the next comma (strtok with ",", checked in the data); MM2 reads each line with fgets into 128 bytes. Retail values are plain ("half", "all", decimal numbers). |
| `Progress::city` | `mmCityList::GetCityInfo` | openmm2 | lookup helper |
| `Progress::raceCount` | `mmPlayerCityRecord::GetNumRaces` | verified | |
| `Progress::passedMask` | `mmPlayerData::GetPassedMask`, `mmPlayerCityRecord::GetPassedMask` | verified | derived from the records' passed flags (MM2 keeps a separate mask that only NewRecord sets; same bits). |
| `Progress::passedCount` | `mmPlayerData::GetNumPassed` | verified | |
| `Progress::openMask` | `mmInterface::CitySetupCB`, `mmPlayerData::ResolveCheckpointProgress`, `ResolveCrashProgress` | fixed | The masks are verified (0x7, 0x3F / 0x1C0 / 0xE00 groups; 0x777, midterms 0x8 / 0x80 / 0x800, all once 0xFFF passed). CitySetupCB only applies them in "sf" and "london"; every other city (and no driver) gets all races open. OpenMM2 applied them in any known city. |
| `Progress::raceOpen` | `mmPlayerData::GetCheckpointProgress` | verified | |
| `Progress::rewardMet` | `mmRewardList::UnlockPlayerRewards` | verified | half: passed >= races / 2 (integer); all: passed == races; a number: that bit. CheckReward's "half" tests passed == races / 2, which gives the same answer for a row that was still locked (passes rise one at a time). |
| `Progress::vehicleUnlocked`, `variantUnlocked` | `mmInterface::PlayerResolveCars`, `UnlockPlayerRewards` (mmVehInfo +0xF4 lock, +0x108 paint job locks) | fixed | PlayerResolveCars clears all locks and then locks only from the sf and london reward tables; OpenMM2 locked from every city's table. |
| `Progress::recordable` | `mmSingleBlitz::ProgressCheck`, `mmSingleRace/Circuit::RegisterFinish`, `mmSingleStunt::RegisterFinish` | verified | Blitz: index < 12 plus cops, traffic, time of day, weather; checkpoint: those four; circuit: also laps and opponents; crash: index <= 12; no pedestrian check. (MM2's cheat flag has no OpenMM2 counterpart.) |
| `Progress::record` | `mmSingle*::RegisterFinish`, `mmPlayerData::RegisterFinish`, `mmGameSingle::UpdateRewards`, `mmRewardList::CheckReward` | verified | Record fields: checkpoint/blitz time, circuit best lap (RegisterLap's minimum), crash 1.0 with score 1; blitz always passed, races by place, lessons by result; the first newly met locked row of the mode is announced. Merge: see `merge`. |
| `Progress::totalScore(city)` | `mmPlayerData::GetTotalScore` | fixed | Summed indices 0..31; MM2 sums each mode's races up to the city's count. The circuit loop's count is the driver's currently open city record's (mmPlayerData +0x27C, confirmed in the asm), not the city just opened: an MM2 quirk OpenMM2 does not reproduce, since which record is open depends on the menu history; London and San Francisco both have 10 circuits, so the totals are the same (deviation within the fix). |
| `Progress::totalScore(profile)` | `mmInterface::PlayerFillStats` | fixed | MM2 adds London and San Francisco only (and shows it only for professionals, which the frontend does); OpenMM2 added every city. |
| `isProgressCity`, `kProgressCities`, `clampEnum`, `asText`, `bit` | none | openmm2 | helpers (the two city names are MM2's, see openMask). |

## Missing

| MM2 | What it does | Status |
| --- | --- | --- |
| `camTrackCS::SwingToRear`, `Front`, `Rear` and the swing spline | Swing the chase camera between front and rear on a spline | not needed: nothing calls them (`UpdateSwing` is empty) |
| `camAICS` | A free camera; `mmPlayer::Init` initialises it but nothing shows it | not needed |
| `camPostCS` | A post-race orbit; only its `MakeActive` runs (from `SetPostRaceCam`), which sets its own fields | not needed: never shown |
| `mmMirror::Cull` (drawing) | Draws the level into the mirror inset (cull winding swapped, player's car hidden) | open (rendering-fx): the camera side is `RearViewMirror`; docs/camera.md lists what the renderer must do |
| `mmViewMgr::SetViewSetting` 1, 3, 4, 7, 8, 10 and the map-mode conditions of 5 | HUD map modes, HUD toggle, external view, map resolution and orientation | session (HUD) |
| `mmGame::UpdateGameInput` (HUD part) | Hides the HUD while looking around in the point-of-view cameras | session (HUD) |
| `mmRainAudio::SetInterior` in `mmPlayer::SetCamera` | Rain sounds from inside in the hood view | audio |
| `dgGlassInstance`, BillFlags 0x100, `-andyglasshack` | Glass props and the pivot read for them | not needed: no retail banger sets 0x100 (the switch is a command-line hack). |
| `dgBangerData::Save`, `LoadEntry`, `ChangeData`, `AdjustBound` | Editor support | not needed for gameplay. |
| `dgBangerInstance::DrawShadowMap` | Draws the geometry's SHADOW mesh in the shadow pass | not needed: banger PKGs have no SHADOW mesh. |
| `dgBangerActiveManager::Update` age mode | Declares by age instead of CollisionType when dgBangerDataManager +0x2a8a8 is set | not needed: mmGame::Init clears it. |
| `mmPlayerConfig` (Load/Save/LoadBinary/SaveBinary, Get/Set Controls, Audio, Graphics, Default*) | Keeps the controls, audio, graphics and HUD options per driver (players/playerN.cfg), applied when the driver is selected and saved at the end of a game. | open (frontend-ui): OpenMM2 keeps these options for all drivers in its settings. View settings now per driver (see Profile::camera). |
| HUD bytes of the view settings (+0x7169 HUD map mode, +0x716D HUD state, +0x716F, +0x7170.. map options) | Restored with the camera choice. | open (session): no HUD-map-mode toggle to keep yet; the mirror byte is now kept (`Profile::mirror`). |
| `mmPlayerData` +0x100 | Last TCP/IP address typed in Dialog_TCPIP, restored for the driver. | open (frontend-ui, multiplayer page). |
| `mmVehInfo::ComputeTuningCRC` / `GetTuningCRC` | CRC of the car's tune/vehicle/<car>.vehcarsim, compared by the multiplayer code (mmInterface, mmGameMulti). | open (session / multiplayer): OpenMM2's own network protocol checks data separately. |
| `mmVehInfo::HasColorVariations` | Colors != "default". | not needed: no retail car uses "default"; the picker counts the colour list. |
| `mmVehList::SetDefaultVehicle` / GetVehicleInfo fallback | Unknown car names resolve to vpcoop. | open, minor: callers handle null. |
| `mmPlayerRecord::ComputeCRC`, `mmPlayerCityRecord::ComputeCRC`, GetRecord's reset of a corrupt record (clearing its pass) | File integrity. | not needed (INI files). |
| `mmRewardList::CheckReward` session unlock | Clears the car's lock flag for the session even if another table's row still asks for it. | open, minor: only differs when two rows name the same car and paint job (none in retail). |

## For other areas

- **session (RaceScreen):** pass the car room's `lvlRoomInfo` flags in
  `CameraTarget::roomFlags` (the _ind camera, the collision margin and the
  multiplayer finish distance depend on them); pass the screen aspect to
  `PlayerCameras::load` (`mmPlayer::Init` moves the dashboard eye on screens
  narrower than 1.3:1); call `startPreRace` again after a Restart (the
  modes' `Reset` calls `SetPreRaceCam`); call `startMultiplayerPostRace` at
  the end of a multiplayer checkpoint race (last waypoint) or circuit (first
  waypoint) with azimuth (heading + 180) x -pi / 180, and fill
  `CameraInput::orbit` from Delete / Page Down / End / Home / Page Up /
  Insert / Shift; apply `Profile::camera` / `wideAngle` / `dashboard` with
  `PlayerCameras::setViewSettings` before the first reset and store
  `viewSettings()` back into the profile when the game ends; create a
  `RearViewMirror` for the player's car, on when `Profile::mirror` is set,
  toggle it with the mirror control (input event 0x1E) and store the state
  back. (Second pass: RaceScreen now gives the cameras World::wheelProbe
  and declares the player's car as the type-4 mover; both one-line changes
  there.)
- **rendering-fx:** draw the mirror (see docs/camera.md, "Rear-view
  mirror") and the wide-angle letterbox (`CameraView::wideAngle`).
- **vehicle:** `mmPlayer::Init` initialises the player's vpcop with
  vpmustang99's `vehCarSim` tuning unless the `-tune_car` argument is given
  (found while auditing the camera part of Init).
- **frontend-ui:** stop storing the first listed car when creating a driver
  (`PagesMain.cpp` and `FrontendScreen.cpp` set `Profile::vehicle`; MM2's
  new driver has no car) and show string 64 ("---") for LAST RACE and LAST
  VEHICLE while `!Profile::hasLastRace()`; let `NewDriverDialog` accept a
  name of spaces (only an empty name is refused); fill `HallEntry::passed`
  in `FrontendScreen::recordResult` (blitz always passed, circuit by place,
  checkpoint by place or already passed) and show passed entries in their
  own style in `RaceRecordsDialog` (`Dialog_HallOfFame` style 2); the
  per-driver options and the last TCP/IP address listed under Missing.
- **ai-vehicles / session:** with the bangers now type-1 movers, only the
  player's room and its neighbours keep knocked-over props simulated; MM2
  also declares opponents within 200 m of a player (aiRouteRacer, type 3)
  and network players (type 3), whose rooms keep props alive too.
- **ai-ambient-city / formats:** `city::parseCityInfo` (the parser the game
  uses for cities) still trusts the race counts; MM2 replaces a non-zero
  count by the number of names and ignores the list when it is 0
  (`mmCityInfo::Load`, fixed in `Catalog`). `city::PsdlRoad::propRule` is
  the high 16 bits of lvlAiRoad's flags, not a prop rule (the per-room rule
  is `PsdlRoom::propRule`). `str::parseInt` / `parseDouble` differ from
  atoi / atof on trailing text and hex; no retail file in this area has
  such a value.
- **rendering-fx (props):** MM2 draws glows and road decals per visible
  room (decals from the room of the midpoint of points 0 and 2, in the
  room's colour). CityRenderer no longer draws PKG xrefs with their parent
  (second pass, one change in `drawModel`): they are bangers now.
