# MM2 -> OpenMM2: vehicle-physics

Audited from MM2Recomp (midtown2.exe build 3393) on 2026-10-08.

Summary: 736 reachable functions (overloads counted separately) in 64
classes; ported 574 (none newly: every gameplay function already had a
port; this pass fixed the spawn placement of vehCar::Init, vehCar::Reset,
vehCarSim::Init, vehCarSim::SetResetPos and vehCarSim::Reset for every
car, and the wreck parts of vehCarModel::InitBreakable /
vehCarModel::EjectOneshot), replaced 13, not needed 146, open 3 (the
frontend's garage sub-menu and the world-objects' spline basis).

Scope: the `veh*` classes except the audio ones, the `ph*` physics and
bound classes, `dgPhysManager`, `dgPhysEntity`, `dgTrailerJoint`,
`dgImpact`, the `dgBound*` bounds, `Matrix33/34/44`, `Vector2/3/4`,
`Quaternion`, `lvlSegment`, `lvlSegmentInfo`, `phInertialCS::TerrainContact`
and the `Vehicle` menu class the coverage tool files here. The function
list is `local/mm2recomp/coverage/subsystems/vehicle-physics.tsv` plus
those classes' rows in `infrastructure.tsv`. The first audit (records
`docs/parity/vehicle.md`, `phys-core.md`, `phys-bounds.md`) compared
every cited function with the decompile and the x87 assembly; this pass
starts from MM2's list, finds the callers of every reachable function in
the asm and checks that the cited ports cover all of each function's
branches and callers.

Status words: **ported** (OpenMM2 does it; the file and function are
given), **ported (new)** (added by this pass), **replaced** (an OpenMM2
part does the job differently on purpose), **not needed** (the game never
runs it, or it is C++ bookkeeping such as destructors, class names and
file saving), **open** (missing, with what porting needs). "Plumbing"
means a destructor, a scalar deleting destructor, `GetClassName`,
`GetDirName` or `SizeOf`: the C++ objects of OpenMM2 replace them.

## Spawning and placement

Who builds each car, with which arguments to `vehCar::Init(name, paint
job, collider id, polygonal bound, trailer)`, and where `vehCar::Reset`
puts it (`vehCarSim::SetResetPos` stores the position plus
CenterOfGravity, the reset rotation sits next to it at vehCarSim +0x250,
and `vehCarSim::Reset` places the body's centre there turned about Y):

| Car | Built by | `vehCar::Init` | Placed by | OpenMM2 |
| --- | --- | --- | --- | --- |
| Player | `mmPlayer::Init` (`mmPlayer::ReInit` after a car change) | the chosen paint job, id 0, polygonal bound, trailer except in multiplayer cruise and Cops and Robbers (`ReInit`: always) | single player: the modes' `InitGameObjects` set the reset position to the first waypoint as the race file gives it, the rotation to -angle in radians, then `vehCar::Reset`; after the AI map is built, `mmGame::InitOtherPlayers` probes with the wheels' mask from 2 m above the body's centre to 10 m below and, on a hit, sets the reset position 0.9 m above the hit and resets the car again; cruise (its own InitOtherPlayers): `mmGame::RespawnXYZ` (a random intersection, 2 m up); multiplayer races: `mmGameMulti::StartXYZ`, then `mmGame::FindGroundPos` (the wheels' probe from 7.5 m above to 15 m below) | `SimVehicle::loadPlayer`, RaceScreen `loadVehicle`: `setResetPos` + `reset()` and `settleOnGround` (fixed by this pass: OpenMM2 dropped every start from 5 m above to 30 m below, in cruise and for the police too, and put the model origin there instead of the body's centre 0.9 m + CenterOfGravity above the road) |
| Racers | `aiRouteRacer::Init` -> `aiVehiclePhysics::Init` | paint job = racer index & 3, id 0, box bound (the polygonal one when the model is the player's: `vehCarModel::InitBound` builds a bound once per model), trailer allowed | `aiRouteRacer::Init`: the reset position and angle (degrees x 0.01744) from the first row of the racer's route; then `mmGame::CollideAIOpponents` (from InitOtherPlayers) probes from 2 m above the racer's model origin to 10 m below and sets the reset position 0.9 m above the hit | RaceScreen `spawnOpponents` / `loadAiCar`, `settleOnGround` (fixed: MM2's placement and settle, the paint job from the racer's index rather than the count of cars loaded so far) |
| Police | `aiPoliceOfficer::Init` -> `aiVehiclePhysics::Init` | vpcop: paint job 0 when the AI map has cable cars, 1 without (London's livery); any other car: officer index & 3 | `aiPoliceOfficer::Reset`: the post's position and rotation | RaceScreen `spawnPolice`, `ai::PoliceCar::reset` (fixed: MM2's placement, no drop; the paint job rule for other cars) |
| Network cars | `mmMulti*::InitNetworkPlayers` / `mmGameMulti::BootStrapCars` -> `mmNetObject::Init` | the player's paint job, id 1000 + player id, polygonal bound, trailer by mode | `StartXYZ` + `FindGroundPos` + `SetResetPos`; then moved by network updates | replaced: OpenMM2's UDP networking places them from snapshots (`updateRemoteCars`, kinematic bodies) |
| Traffic bodies | `aiVehicleActive` (not a vehCar) | | `aiVehicleActive::Attach` | `game/TrafficBodies.cpp` (ai-vehicles) |

Respawns: `mmSingleCircuit::HitWaterHandler` and
`mmGameMulti::HitWaterHandler` save the reset position and rotation, set
them to the last cleared checkpoint, reset the player and put the saved
values back through `SetResetPos`, which adds CenterOfGravity a second
time: each such respawn moves the start a later restart uses by
CenterOfGravity (`SimVehicle::respawnAt` keeps this). Without waypoints
(`mmGame::HitWaterHandler`) it is a plain `mmPlayer::Reset`. A restart
(`mmGame::Reset`) resets every car at its reset position.

## vehCar

The car as the physics manager and the level see it (a dgPhysEntity with
a phColliderJointed): it owns the vehCarSim, vehCarModel, vehCarDamage,
vehStuck, vehGyro, vehWheelPtx, vehSiren, vehSplash, the four track
managers, the audio container and, for a car with a `trailer_hitch` pivot,
its vehTrailer. OpenMM2 splits it between `game::SimVehicle`
(`game/PlayerVehicle.cpp`), `phys::CarSim` and the race screen's effects,
renderer and audio objects.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `vehCar::vehCar` | ported | `SimVehicle`, `CarSim` members | The parts are members; vehFeedback (force feedback) is the input-ff subsystem's. |
| `vehCar::~vehCar`, `` vehCar::`scalar_deleting_destructor' `` | not needed | | Plumbing. |
| `vehCar::Init` | ported | `SimVehicle::load`, `CarSim::init`, RaceScreen `loadVehicle` / `loadAiCar` | Tune, model, bound, collider, damage, stuck, gyro, trailer (only with a hitch pivot and the trailer flag), splash box. Fixed here: the paint jobs of racers and police (see Spawning). The collider id is `vehCar::SetColliderID`'s row. |
| `vehCar::SetColliderID` | replaced | `Collider::id` (0 for every car) | MM2 gives network cars 1000 + their id; the only readers are AudImpact (any id past the table plays its first entry, as 0 does) and `mmMultiCR::ImpactCallback`, which steals gold only from a hit by an id of 1000 or more: OpenMM2's Cops and Robbers takes the network car from RaceScreen's own list of hits on remote cars. |
| `vehCar::InitAudio` | ported | RaceScreen `loadAudio`, `loadAiCarAudio` | The vehCarAudioContainer is the audio subsystem's (`audio/game/CarAudio`). |
| `vehCar::SetDrivable` | ported | `CarSim::setDrivable`, `SimVehicle::hold` / `drive` | Modes 1 to 3 as `vehCar::PreUpdate` reads them. |
| `vehCar::Reset` | ported | `SimVehicle::reset()`, `CarSim::reset()` | Fixed by this pass: back to the reset position (`vehCarSim::Reset`) instead of a model matrix the caller kept. The track, wheel particle and siren resets are `VehicleEffects::reset` and the renderer's; the audio container's is audio's. |
| `vehCar::ClearDamage` | ported | RaceScreen DamageReset: `CarDamage::reset`, `VehicleRenderer::resetDamage` | vehCarSim's part is empty. |
| `vehCar::PreUpdate` | ported | `CarSim::preUpdate` | The hold modes. Registering the wheel particles' drawable is `VehicleEffects`. |
| `vehCar::UpdateTrack`, `vehCar::DrawTracks` | ported | `fx::VehicleEffects`, `fx::SkidMarks` | No tracks on the "water" material or in a room with lvlRoomInfo flag 0x10. |
| `vehCar::Update` | ported | `CarSim::afterIntegrate` (gyro flags at 0.01, stuck and splash only while drivable, splash activation below the water level, damage), RaceScreen (tracks, wheel particles, room) | |
| `vehCar::PostUpdate` | ported | RaceScreen's draw passes, `fx::VehicleEffects`, `VehicleRenderer`, audio | Draws the tracks, the wheel particles (given the water level at the car), the damage smoke, the sparks (when the sparks global is on), the shards; updates the siren and the audio. |
| `vehCar::GetICS`, `vehCar::GetInst` | ported | `Body::ics`, `VehicleBody` | dgPhysEntity's accessors. |
| `vehCar::RequiresTerrainCollision` | ported | `CarSim::requiresTerrainCollision` | Its caller's branch is never taken in a race (phys-core). |

## vehCarSim

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `vehCarSim::vehCarSim` | ported | `CarSimParams` defaults | Builds ten vehSuspension parts (see vehSuspension). |
| `vehCarSim::~vehCarSim`, `` vehCarSim::`scalar_deleting_destructor' ``, `vehCarSim::GetClassName`, `vehCarSim::GetDirName` | not needed | | Plumbing ("tune/vehicle" is `SimVehicle::load`'s directory). |
| `vehCarSim::Init` | ported | `CarSim::init`, `SimVehicle::load` | Ends with SetResetPos(origin) and Reset: now `CarSim::init` does the same (the body at CenterOfGravity; it used to put the model origin at 0). The ten vehSuspension::Init calls find no pivots (see vehSuspension). |
| `vehCarSim::FileIO` | ported | `loadCarSimParams` | |
| `vehCarSim::ConfigureDrivetrain`, `vehCarSim::ReconfigureDrivetrain`, `vehCarSim::UnconfigureDrivetrain` | ported | `CarSim::init` (drivetrain wiring) | Only `vehCarSim::Init` reaches them (Reconfigure calls Unconfigure, then Configure). |
| `vehCarSim::SetResetPos` | ported | `CarSim::setResetPos`, `SimVehicle::setResetPos` | Fixed by this pass: every caller of the game now uses it (see Spawning). |
| `vehCarSim::Reset` | ported | `CarSim::reset()` | ICS reset, body at the reset position, Matrix34::Rotate about Y by the reset rotation, inputs cleared, RestoreImpactParams. |
| `vehCarSim::RestoreImpactParams` | ported | `CarSim::resetBody` | |
| `vehCarSim::SetWorldMatrix` | ported | `CarSim::modelMatrix` | |
| `vehCarSim::Update` | ported | `CarSim::beforeIntegrate` / `afterIntegrate` | |
| `vehCarSim::OnGround`, `vehCarSim::BottomedOut`, `vehCarSim::GetSSSFactor` | ported | `CarSim::wheelsOnGround`, `bottomedOut`, `sssFactor` | |
| `vehCarSim::ClearDamage` | not needed | | Empty in build 3393. |

## vehWheel, vehWheelCheap

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `vehWheel::vehWheel`, `vehWheel::FileIO`, `vehWheel::CopyVars`, `vehWheel::Init`, `vehWheel::ComputeConstants`, `vehWheel::Reset` | ported | `WheelParams`, `loadWheelParams`, `Wheel::copyVars`, `Wheel::init`, `Wheel::computeConstants`, `Wheel::reset` | The constructor's lvlSegmentInfo::AllocateState is the wheel's `ProbeCache`. |
| `vehWheel::AddNormalLoad`, `vehWheel::SetNormalLoad`, `vehWheel::SetInputs`, `vehWheel::ComputeFriction`, `vehWheel::CalcSuspensionForce`, `vehWheel::GetBumpDisplacement`, `vehWheel::ComputeDwtdw`, `vehWheel::Update`, `vehWheel::GetVisualDispVert` | ported | `phys/vehicle/Wheel.cpp` | As vehicle.md records. |
| `vehWheel::GetSurfaceSound` | ported | `audio/game/CarAudio` | The material's sound index (audio). |
| `vehWheel::~vehWheel`, `` vehWheel::`scalar_deleting_destructor' ``, `vehWheel::GetClassName` | not needed | | Plumbing. |
| `vehWheelCheap::vehWheelCheap`, `vehWheelCheap::Init`, `vehWheelCheap::Reset`, `vehWheelCheap::Update` | ported | `game/TrafficBodies.cpp` | The traffic bodies' wheels (aiVehicleActive); constructor defaults as `TrafficBodies`' wheel constants. Round 3 (frames): the drawing matrix Init and Update leave (+0x128) is kept for aiVehicleInstance::Draw. |
| `vehWheelCheap::~vehWheelCheap`, `` vehWheelCheap::`scalar_deleting_destructor' `` | not needed | | Plumbing. |

## vehDrivetrain, vehEngine, vehTransmission, vehAero, vehAxle

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `vehDrivetrain::vehDrivetrain`, `vehDrivetrain::FileIO`, `vehDrivetrain::CopyVars`, `vehDrivetrain::Init` | ported | `DrivetrainParams`, `loadCarSimParams`, `Drivetrain::configure` | CopyVars also builds the trailer's free drivetrains (`Trailer::init`). |
| `vehDrivetrain::AddWheel`, `vehDrivetrain::Attach`, `vehDrivetrain::Detach`, `vehDrivetrain::Reset`, `vehDrivetrain::Update` | ported | `Drivetrain::addWheel`, `attach`, `detach`, `reset`, `update` | `vehEngine::Update` detaches in neutral and during a gear change. |
| `vehDrivetrain::~vehDrivetrain`, `` vehDrivetrain::`scalar_deleting_destructor' ``, `vehDrivetrain::GetClassName` | not needed | | Plumbing. |
| `vehEngine::vehEngine`, `vehEngine::FileIO`, `vehEngine::Init`, `vehEngine::ComputeConstants`, `vehEngine::Reset` | ported | `EngineParams`, `Engine::configure`, `computeConstants`, `reset` | |
| `vehEngine::CalcTorque`, `vehEngine::CalcTorqueAtFullThrottle`, `vehEngine::CalcTorqueAtZeroThrottle`, `vehEngine::CalcHPAtFullThrottle`, `vehEngine::Update` | ported | `Engine::calcTorque`, `calcTorqueAtFullThrottle`, `calcTorqueAtZeroThrottle`, `calcHPAtFullThrottle`, `update` | CalcHPAtFullThrottle is used by `vehTransmission::ComputeConstants`. |
| `vehEngine::~vehEngine`, `` vehEngine::`scalar_deleting_destructor' ``, `vehEngine::GetClassName` | not needed | | Plumbing. |
| `vehTransmission::vehTransmission`, `vehTransmission::FileIO`, `vehTransmission::Init`, `vehTransmission::ComputeConstants`, `vehTransmission::GearRatioFromMPH`, `vehTransmission::Reset` | ported | `Transmission::configure`, `computeConstants`, `gearRatioFromMph`, `reset` | Init only stores the vehCarSim. |
| `vehTransmission::Update`, `vehTransmission::Upshift`, `vehTransmission::Downshift`, `vehTransmission::SetCurrentGear`, `vehTransmission::Automatic` | ported | `Transmission::update`, `upshift`, `downshift`, `setCurrentGear`, `automatic` | Downshift / Upshift from `mmGame::UpdateGameInput`'s manual keys. |
| `vehTransmission::SetForward`, `vehTransmission::SetNeutral`, `vehTransmission::SetReverse` | ported | `Transmission::setDrive`, `setNeutral`, `setReverse` | Neutral from `vehCar::PreUpdate` / `SetDrivable`; reverse from `mmGame::UpdateSteeringBrakes`. |
| `vehTransmission::~vehTransmission`, `` vehTransmission::`scalar_deleting_destructor' ``, `vehTransmission::GetClassName` | not needed | | Plumbing. |
| `vehAero::vehAero`, `vehAero::FileIO`, `vehAero::Update` | ported | `AeroParams`, `Aero::configure`, `Aero::update` | |
| `vehAero::~vehAero`, `` vehAero::`scalar_deleting_destructor' ``, `vehAero::GetClassName` | not needed | | Plumbing. |
| `vehAxle::vehAxle`, `vehAxle::FileIO`, `vehAxle::Init`, `vehAxle::ComputeConstants`, `vehAxle::Update` | ported | `AxleParams`, the axle part of `CarSim::init`, `CarSim::updateAxles` | No retail car has an axle pivot. |
| `vehAxle::~vehAxle`, `` vehAxle::`scalar_deleting_destructor' ``, `vehAxle::GetClassName` | not needed | | Plumbing. |

## vehGyro, vehStuck, vehSplash, vehSuspension

`vehSuspension` is the visual suspension: vehCarSim builds ten of them
(shock0-3, arm0-3, shaft2-3) and each, with a pivot, would scale its part
along Y from its wheel's travel. No retail vehicle has any of those pivots
(no `geometry/*_shock*`, `*_arm*` or `*_shaft*` .mtx file in the game
data, and no such mesh part), so every `vehSuspension::Init` clears its
active flag and nothing is updated or drawn.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `vehGyro::vehGyro`, `vehGyro::FileIO`, `vehGyro::Init`, `vehGyro::Update` | ported | `GyroParams`, `loadGyroParams` (Init loads `<car>.vehGyro` through the node's file loader), `Gyro::update` | |
| `vehGyro::~vehGyro`, `` vehGyro::`scalar_deleting_destructor' ``, `vehGyro::GetClassName`, `vehGyro::GetDirName` | not needed | | Plumbing. |
| `vehStuck::vehStuck`, `vehStuck::FileIO`, `vehStuck::Init`, `vehStuck::Reset`, `vehStuck::Impact`, `vehStuck::Pegged`, `vehStuck::Update` | ported | `phys/vehicle/Stuck.cpp` | |
| `vehStuck::~vehStuck`, `` vehStuck::`scalar_deleting_destructor' ``, `vehStuck::GetClassName`, `vehStuck::GetDirName` | not needed | | Plumbing. |
| `vehSplash::vehSplash`, `vehSplash::Reset`, `vehSplash::Init`, `vehSplash::Activate`, `vehSplash::Update` | ported | `phys/vehicle/Splash.cpp` | The constructor runs Reset (drag and buoyancy) once. |
| `vehSplash::~vehSplash`, `` vehSplash::`scalar_deleting_destructor' `` | not needed | | Plumbing. |
| `vehSuspension::vehSuspension`, `vehSuspension::Init`, `vehSuspension::Update`, `vehSuspension::Copy` | not needed | | No retail data (above). Init: the pivot, the wheel's travel scale (one over the pivot's Y offset from the wheel centre along its Y axis), the mode (scale or 1 + scale, by whether the pivot's Y axis has |y| of 0.5 or more); Update: the scale from the wheel's suspension minus its visual displacement, then the part's world matrix. |
| `vehSuspension::~vehSuspension`, `` vehSuspension::`scalar_deleting_destructor' ``, `vehSuspension::GetClassName` | not needed | | Plumbing. |

## vehCarDamage

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `vehCarDamage::vehCarDamage`, `vehCarDamage::FileIO`, `vehCarDamage::Init` | ported | `CarDamageParams`, `loadCarDamageParams`, `fx::VehicleEffects` (the smoke and sparks rules) | Init also sets the collider's impact callback (`phColliderBase::SetImpactCB`): `CarSim` is the collider's `ImpactHandler`. |
| `vehCarDamage::Reset`, `vehCarDamage::ClearDamage`, `vehCarDamage::AddDamage`, `vehCarDamage::Update` | ported | `CarDamage::reset`, `addDamage`, `update`; `VehicleEffects` (smoke) | |
| `vehCarDamage::Impact`, `vehCarDamage::InsertImpact`, `vehCarDamage::ApplyImpact` | ported | `CarSim::onImpact`, `insertImpact`, `applyImpact` | |
| `vehCarDamage::GetDamageModifier` | ported | `CarSim::insertImpact` | Always 1 (no class overrides it). |
| `vehCarDamage::SetGameCallback` | ported | `CarSim::onImpactCallback` | Only `mmPlayer::Init` sets one: `mmPlayer::ImpactCallback` (force feedback, then the mode's callback, e.g. `mmMultiCR::ImpactCallback`): RaceScreen's player callback and the session. |
| `vehCarDamage::SpewSmoke` | ported | `fx::VehicleEffects` | (rendering-fx) |
| `vehCarDamage::~vehCarDamage`, `` vehCarDamage::`scalar_deleting_destructor' ``, `vehCarDamage::GetClassName`, `vehCarDamage::GetDirName` | not needed | | Plumbing. |

## vehCarModel, vehBound

The car's level instance: geometry, paint jobs, breakables, lights,
siren, shadow, and the collision bound (`InitBound`). Drawing is
`game::VehicleRenderer` (audited by rendering-fx); the physical parts are
`CarSim` and `VehicleBody`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `vehCarModel::vehCarModel`, `vehCarModel::~vehCarModel` | ported | `VehicleRenderer` | The 0.2 extra-wheel spacing default (+0x2c) is `kExtraWheelSpacing`; the destructor's ltLight shutdown is not needed. |
| `vehCarModel::Init` | ported | `asset::loadVehicleModel`, `VehicleRenderer` | Parts, paint jobs (the variant reduced modulo the model's paint jobs), texel damage, siren lights, headlight lights, fender offset, the two breakable managers, the trailer hitch. |
| `vehCarModel::InitBreakable` | ported | `VehicleRenderer::nearestBreakable`, `wreckParts` | Adds a part to a vehBreakableMgr (props-fx owns vehBreakableMgr) when its high LOD geometry slot is filled; the place is its pivot. Fixed by this pass (from props-fx's finding): `wreckParts` took any part with a pivot, so vpvw_dune (WHL2 / WHL3 pivots, no such meshes) would have thrown off invisible back wheels; it now needs the mesh as `nearestBreakable` does. |
| `vehCarModel::InitBound` | ported | `CarSim::setPolygonalBound` / `buildBound`, `SimVehicle::load` | Polygonal bound for the player and network cars, a dgBoundBox around it otherwise; once per model, so AI cars of the player's model get the polygonal bound (RaceScreen `loadAiCar`). |
| `vehCarModel::InitSirenLight`, `vehCarModel::GetSurfaceColor` | ported | `VehicleRenderer` (siren lights, light colours) | |
| `vehCarModel::GetTrailerHitch` | ported | `SimVehicle::load` (the `trailer_hitch` pivot) | |
| `vehCarModel::Reset`, `vehCarModel::ClearDamage` | ported | `VehicleRenderer::reattachAll`, `resetDamage` | Breakables back on, texel damage cleared, the one-shot eject armed again, the wheels shown. |
| `vehCarModel::EjectOneshot` | ported | `VehicleRenderer::wreckParts`, RaceScreen | |
| `vehCarModel::Draw`, `vehCarModel::DrawPart`, `vehCarModel::DrawShadow`, `vehCarModel::DrawGlow`, `vehCarModel::DrawHeadlights` | ported | `VehicleRenderer::drawCar`, `drawPart`, `drawShadow`, `drawGlows` | Round 3 (frames): DrawHeadlights' sweep directions are kept in world space (fixed). |
| `vehCarModel::DrawShadowMap`, `vehCarModel::DrawReflected` | not needed | | lvlInstance vtable slots 0x34 and 0x3c: no code of build 3393 calls through them (cityLevel draws shadows through slot 0x30). |
| `vehCarModel::SetVisible`, `vehCarModel::GetVisible` | ported | RaceScreen `drawLevel` (`playerBody`) | `mmPlayer::Update` hides the car in the point-of-view cameras; `mmMirror::Cull` shows it for the mirror. |
| `vehCarModel::GetPosition`, `vehCarModel::GetMatrix`, `vehCarModel::SetMatrix`, `vehCarModel::GetVelocity`, `vehCarModel::GetEntity`, `vehCarModel::AttachEntity` | ported | `VehicleBody::position`, `Body::bound` matrix, `Body::ics`, `Body::entity` | lvlInstance's virtuals for the car: the vehCarSim's world matrix and velocity, the vehCar as entity, no attach. |
| `vehCarModel::SizeOf` | not needed | | Plumbing. |
| `vehBound::vehBound`, `vehBound::Init`, `vehBound::SetFriction`, `vehBound::SetElasticity` | ported | `CarSim::buildBound`, `Bound::makeOwnMaterial`, `setFriction`, `setElasticity` | The polygonal bound with one own lvlMaterial for every polygon; friction and elasticity written to it. |

## vehSiren

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `vehSiren::vehSiren`, `vehSiren::Init`, `vehSiren::AddLight`, `vehSiren::Update`, `vehSiren::Draw` | ported | `VehicleRenderer` (siren lights and beams), `fx::LensFlares` | |
| `vehSiren::Reset` | ported | | Empty in build 3393. |
| `vehSiren::~vehSiren` | not needed | | Plumbing. |

## vehTrailer, vehTrailerInstance

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `vehTrailer::vehTrailer`, `vehTrailer::FileIO`, `vehTrailer::Init`, `vehTrailer::Reset`, `vehTrailer::Update`, `vehTrailer::BottomedOut`, `vehTrailer::RequiresTerrainCollision` | ported | `phys/vehicle/Trailer.cpp` | As vehicle.md records (the static load decision is the maintainer's). |
| `vehTrailer::GetICS`, `vehTrailer::GetInst` | ported | `Trailer::body` | |
| `vehTrailer::PostUpdate` | ported | | Empty. |
| `` vehTrailer::Update`adjustor{180}' `` | ported | `Trailer::afterIntegrate` | The same Update through the trailer's second base. |
| `vehTrailer::Load`, `vehTrailer::Save` | not needed | | File loading and saving through the node interface; the tune is read by `loadTrailerParams`. |
| `vehTrailer::~vehTrailer`, `` vehTrailer::`vector_deleting_destructor' ``, `` vehTrailer::`vector_deleting_destructor'`adjustor{180}' ``, `vehTrailer::GetClassName`, `vehTrailer::GetDirName` | not needed | | Plumbing. |
| `vehTrailerInstance::Init` | ported | `SimVehicle::load` (the `<car>_trailer` model), `VehicleRenderer` ("TRAILER" / "TWHL") | |
| `vehTrailerInstance::Draw`, `vehTrailerInstance::DrawShadow` | ported (fixed) | `VehicleRenderer::drawTrailer`, `drawShadow`, `SimVehicle::trailerPose` | The trailer's "shadow" part on the ground; tail lights above 0.1 brake. Round 3 (frames): drawn as vehTrailerInstance does (body alone below H; TLIGHT in the object pass and TWHL0-3 only at H; none of vehCarModel's other parts; no glows), no longer as a car. |
| `vehTrailerInstance::DrawShadowMap` | not needed | | No caller of the slot (as vehCarModel's). |
| `vehTrailerInstance::GetMatrix`, `vehTrailerInstance::SetMatrix`, `vehTrailerInstance::GetPosition`, `vehTrailerInstance::GetVelocity`, `vehTrailerInstance::GetEntity`, `vehTrailerInstance::AttachEntity`, `vehTrailerInstance::GetTrailerHitch` | ported | `Trailer::modelMatrix`, `VehicleBody::position`, `Body`, `Trailer::init` | |
| `vehTrailerInstance::SizeOf` | not needed | | Plumbing. |

## Vehicle

The Garage Menu (UI menu 8), not a vehicle class; the frontend-ui
subsystem's.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `Vehicle::Vehicle`, `Vehicle::PreSetup`, `Vehicle::PostSetup` | ported | `app/frontend/PagesRace.cpp` (Garage Menu) | |
| `Vehicle::SetSubMenu`, `Vehicle::SetSubMenuButtons` | open | | `mmInterface::UpdateLobby` opens the garage from the multiplayer lobby as a sub-menu (lobby widget 100) with its GO button hidden; for the frontend-ui audit to confirm. |
| `Vehicle::~Vehicle`, `` Vehicle::`scalar_deleting_destructor' `` | not needed | | Plumbing. |

## dgPhysEntity, dgPhysManager, dgImpact

dgPhysManager is the per-frame collision and simulation manager: movers
declared each frame (the player type 4, the trailer type 2, racers and
police type 3, network cars type 3, knocked props), gathered against each
other, the instances in their rooms and the city, then integrated in 1/35 s
samples (at most 3). OpenMM2: `phys::World` (records in phys-core.md).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `dgPhysEntity::Update` | ported | `InertialCS::finishForces` (gravity 19.6 along -Y), `World::step` | |
| `dgPhysEntity::GetCollider` | ported | `Body::collider` | |
| `dgPhysEntity::PreUpdate`, `dgPhysEntity::DetachMe`, `dgPhysEntity::FirstImpactCallback` | ported | `BodyController` (empty defaults), `Instance::detach` | Empty in the base; vehCar overrides PreUpdate (`CarSim::preUpdate`), the props and traffic bodies their own (bangers, `TrafficBodies`). |
| `dgPhysEntity::RequiresTerrainCollision` | ported | `World::collideTerrain` | The base answers true; its caller's branch is never taken in a race. |
| `dgPhysEntity::~dgPhysEntity`, `` dgPhysEntity::`scalar_deleting_destructor' `` | not needed | | Plumbing. |
| `dgPhysManager::dgPhysManager` | ported | `World::World` | Builds the contact manager and calls `phContact::DisableContacts` (penetration 0, contacts off for the session). |
| `dgPhysManager::Reset`, `dgPhysManager::ResetTable` | ported | `World::World`, `World::beginFrame` | ResetTable clears the 32 mover slots each frame. |
| `dgPhysManager::DeclareMover`, `dgPhysManager::IgnoreMover`, `dgPhysManager::NewMover` (three) | ported | `Body::declare`, `World::ignoreMover`, `World::addNewMover` | |
| `dgPhysManager::Update`, `dgPhysManager::TrivialCollideInstances`, `dgPhysManager::GatherCollidables`, `dgPhysManager::CollideInstances`, `dgPhysManager::CollideTerrain` | ported | `World::advanceOversampled`, `step`, `trivialCollide`, `gatherCollidables`, `collideInstances`, `collideTerrain` | |
| `dgPhysManager::Collide`, `dgPhysManager::CollideProbe` | ported | `World::wheelProbe`, `World::collideProbe`, `World::probe` | |
| `dgPhysManager::TestSphere` | ported | | A stub returning false in build 3393. |
| `dgPhysManager::CreateInstance`, `dgPhysManager::PromoteInstance`, `dgPhysManager::DemoteInstance`, `dgPhysManager::KillInstance`, `dgPhysManager::DisableInstance`, `dgPhysManager::EnableInstance`, `dgPhysManager::GetCollider`, `dgPhysManager::TestProbe` | not needed | | Empty stubs of the instance-manager interface (null, false or nothing). |
| `dgPhysManager::~dgPhysManager`, `` dgPhysManager::`scalar_deleting_destructor' `` | not needed | | Plumbing. |
| `dgImpact::CalcImpact`, `dgImpact::CalcCollision` | ported | `calcBangerImpact` (`phys/Impact.cpp`) | The banger impact; its contact branch is off (contacts disabled). |

## dgTrailerJoint

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `dgTrailerJoint::dgTrailerJoint`, `dgTrailerJoint::Init`, `dgTrailerJoint::FileIO`, `dgTrailerJoint::Reset` | ported | `TrailerJoint::init`, `loadTrailerJointParams`, `TrailerJoint::reset` | |
| `dgTrailerJoint::Update`, `dgTrailerJoint::DoJointTorque`, `dgTrailerJoint::DoJointLimits`, `dgTrailerJoint::ComputeInvMassMatrix` | ported | `TrailerJoint::update`, `doJointTorque`, `doJointLimits`, `computeInvMassMatrix` | |
| `dgTrailerJoint::SetCosFreeLean`, `dgTrailerJoint::SetJointForceFlag` | ported | `TrailerJoint` setters | |
| `dgTrailerJoint::BreakJoint`, `dgTrailerJoint::UnbreakJoint`, `dgTrailerJoint::IsBroken` | ported | `TrailerJoint`'s broken flag (`update`: Ctrl+B; `reset`) | Bit 0 of the joint flags; `phColliderJointed::GetInvMassMatrix` asks IsBroken. |
| `dgTrailerJoint::Load`, `dgTrailerJoint::Save` | not needed | | Node file I/O; the tune is read by `loadTrailerJointParams`. |
| `dgTrailerJoint::~dgTrailerJoint`, `` dgTrailerJoint::`scalar_deleting_destructor' ``, `dgTrailerJoint::GetClassName`, `dgTrailerJoint::GetDirName` | not needed | | Plumbing. |

## phCollider, phColliderBase, phColliderJointed

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `phCollider::Init` (three), `phCollider::Reset` | ported | `Collider::init`, `initStatic`, `reset` | |
| `phColliderBase::Reset`, `phColliderBase::UpdateMtx`, `phColliderBase::CalcMaxMoved`, `phColliderBase::CopyLastMatrix`, `phColliderBase::GetLocalVelocity`, `phColliderBase::GetInvMassMatrix` (both) | ported | `phys/Collider.cpp` | |
| `phColliderBase::Impact` (both) | ported | `Collider::impact` | The InertialCS accumulation, the hardest push and the impact callback. The bound-callback branch is never taken: nothing calls `phColliderBase::SetBoundCB`, so the callback stays null. |
| `phColliderBase::SetImpactCB` | ported | `Collider::handler` | Set by `vehCarDamage::Init` (`CarSim`) and `aiVehicleActive::Attach` (`TrafficBodies`). |
| `phColliderBase::CallBoundCallback`, `phColliderBase::GetBoundCBImpactInfo` | not needed | | Only reached with a bound callback set (above). |
| `phColliderBase::Contact` (three), `phColliderBase::GetDisp` (both) | not needed | | The contact path (contacts disabled). |
| `phColliderJointed::phColliderJointed`, `phColliderJointed::Attach`, `phColliderJointed::GetInvMassMatrix` (both) | ported | `Collider::joint`, `Collider::invMassMatrix` | The joint's inverse mass matrix unless it is broken. |
| `phColliderJointed::Impact` (both) | ported | `Collider::impact` | Forwards to phColliderBase. |
| `phColliderJointed::Contact` (three) | not needed | | Contacts disabled. |

## phInertialCS, phSleep, phJoint

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `phInertialCS::phInertialCS`, `phInertialCS::Init`, `phInertialCS::InitBoxMass`, `phInertialCS::Reset`, `phInertialCS::Zero`, `phInertialCS::ZeroForces`, `phInertialCS::Freeze` | ported | `phys/InertialCS.cpp` | |
| `phInertialCS::Update` (both), `phInertialCS::MoveICS`, `phInertialCS::CalcNetPush`, `phInertialCS::ApplyContactForce` | ported | `InertialCS::finishUpdate`, `integrateExplicit` / `integrateImplicit`, `moveICS`, `applyPush`, `applyContactForce` | |
| `phInertialCS::TerrainContact::TerrainContact` | ported | `InertialCS::integrateImplicit` | An empty constructor of a matrix temporary in Update's implicit branch. |
| `phInertialCS::GetLocalVelocity`, `phInertialCS::GetLocalFilteredVelocity2`, `phInertialCS::GetCMFilteredVelocity`, `phInertialCS::GetLocalAcceleration`, `phInertialCS::GetForce`, `phInertialCS::GetTorque`, `phInertialCS::GetInertiaMatrix`, `phInertialCS::GetInverseInertiaMatrix`, `phInertialCS::GetInvMassMatrix` (both) | ported | `getVelocity`, `filteredVelocity`, `cmFilteredVelocity`, `localAcceleration`, `getForce`, `getTorque`, `worldInertia`, `calcCMatrix` | GetTorque is read by the trailer joint. |
| `phInertialCS::GetLocalFilteredDisp` | not needed | | Only `phColliderBase::GetDisp` (contacts). |
| `phSleep::phSleep`, `phSleep::Init`, `phSleep::Reset`, `phSleep::WakeUp`, `phSleep::SendToSleep`, `phSleep::SmoothAngInertia`, `phSleep::Update` | ported | `phys/Sleep.cpp`, bangers, `TrafficBodies` | The constructor runs for props, traffic bodies and pedestrian bodies. Update's only use of datTimeManager::ElapsedTime (the unclamped clock that runs on while paused) wakes a sleeper whose wake time has passed; the wake time is FLT_MAX from Init and SendToSleep, and its one setter, `phSleep::WakeUpNextTime`, has no caller, so the clock never matters. The pushes are scaled by the sample's InvSeconds, as `Sleep::update(invDt)`. |
| `phSleep::~phSleep` | not needed | | Plumbing. |
| `phJoint::phJoint`, `phJoint::Init`, `phJoint::Reset`, `phJoint::IsBroken`, `phJoint::GetInvMassMatrix`, `phJoint::ComputeInvMassMatrix` (both), `phJoint::ComputeJointForce`, `phJoint::ComputeJointPush` | ported | `phys/Joint.cpp` | |

## phImpactBase, phImpact, phContact, phContactMgr

`phContact::DisableContacts` (from dgPhysManager's constructor) clears the
contacts flag, its only writer: the contact and held-contact code of
phContactMgr and phContact never runs; every impact goes through
`phContactMgr::CalcImpact`'s impact path.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `phImpactBase::StartMakingNewImpact`, `phImpactBase::FinishMakingNewImpact`, `phImpactBase::MakeNewImpact`, `phImpactBase::SwapColliders`, `phImpactBase::ImpactIsInList`, `phImpactBase::CullImpactList`, `phImpactBase::AddImpactSpherePlaneTest`, `phImpactBase::AddImpactShaftPlaneTest` | ported | `phys/Impact.cpp` | Named `phImpactBase::*` there. |
| `phImpact::CalcCollision`, `phImpact::CalcCollisionNoFriction`, `phImpact::FindFrictionAndElasticity`, `phImpact::GetLocalVelocities`, `phImpact::GetMaterial`, `phImpact::Impact` | ported | `calcCollision`, `calcCollisionNoFriction`, `findFrictionAndElasticity`, `localVelocities`, `materialOf`, `calcImpact` | |
| `phImpact::Contact`, `phImpact::EffectiveMass`, `phImpact::GetRelDisplacement` | not needed | | Contact path. |
| `phContact::DisableContacts` | ported | `kPenetration`, `kBarelyMovedDistance` (`phys/Bound.h`) | |
| `phContact::Init`, `phContact::Set`, `phContact::IsEqual`, `phContact::SwapAB`, `phContact::CalcContactForce` | not needed | | Contacts disabled. |
| `phContactMgr::CalcImpact` | ported | `calcImpact` | |
| `phContactMgr::phContactMgr`, `phContactMgr::~phContactMgr`, `phContactMgr::Reset`, `phContactMgr::ClearContactList`, `phContactMgr::ClearHeldContactTable`, `phContactMgr::Resize`, `phContactMgr::AllocNewContact`, `phContactMgr::SeekContact`, `phContactMgr::CalcContact`, `phContactMgr::CalcContactHash`, `phContactMgr::AddHCEntry`, `phContactMgr::GetCMSeconds`, `phContactMgr::GetCMInvSeconds` | not needed | | The contact tables (200 contacts, 400 hash slots) and their seconds; only the disabled contact path reads them (`dgImpact::CalcImpact` asks GetCMInvSeconds for its contact branch only). |

## lvlSegment, lvlSegmentInfo

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `lvlSegment::Set` | ported | `Segment` (`phys/Bound.h`), `World::wheelProbe` callers | |
| `lvlSegmentInfo::AllocateState` | ported | `ProbeCache` (`phys/Level.h`) | The wheels' (vehWheel's constructor) probe cache; also cable cars, subway, pedestrians and ambient splines (world-objects, ai). |

## phBound and the bound types

Collision bounds of cars (polygonal or box), trailers, props (box,
geometry, sphere, hotdog), the city's instances (geometry, terrain, local
terrain) and the level. OpenMM2: `src/phys/Bound*.cpp`, `asset/Bound.cpp`
(record phys-bounds.md, which also lists the bound methods nothing calls).
The `Test*Point`, `TestAIPoint` and `TestSphere` virtuals are reached only
through `phColliderBase::TestSegmentPoint` (no caller) and the stub
`dgPhysManager::TestSphere`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `phBound::phBound`, `phBound::SetOffset`, `phBound::IsOffset`, `phBound::SetPenetration`, `phBound::GetCenter`, `phBound::GetVertex`, `phBound::GetNumMaterials`, `phBound::CalculateSphereFromBoundingBox`, `phBound::TestSegment` | ported | `Bound` (`phys/Bound.cpp`) | IsOffset is the offset flag every FindImpact* reads. |
| `phBound::SetFriction` (both), `phBound::SetElasticity` (both) | ported | `Bound::setFriction`, `setElasticity` | phBound's are no-ops (the getter overload answers 0.5); the dg bounds write their own material. |
| `phBound::ModifyInvMassMatrix` (both), `phBound::EffectiveMass` | ported | `Collider::invMassMatrix` | phBound's no-ops (EffectiveMass returns its argument); no bound type overrides them. |
| `phBound::CenterBound`, `phBound::TestSphere` (both) | not needed | | No caller of the CenterBound slot; TestSphere as above. |
| `dgBoundBox::dgBoundBox`, `dgBoundGeometry::dgBoundGeometry`, `dgBoundHotdog::dgBoundHotdog`, `dgBoundSphere::dgBoundSphere` | ported | `Bound::makeOwnMaterial` | Each with its own lvlMaterial: car boxes (`vehCarModel::InitBound`), traffic bodies (`aiVehicleManager::AddVehicleDataEntry`), props (`dgBangerData::InitBound`). |
| `dgBoundBox::GetMaterial`, `dgBoundBox::GetNumMaterials`, `dgBoundBox::SetFriction`, `dgBoundBox::SetElasticity`, `dgBoundGeometry::GetMaterial`, `dgBoundGeometry::GetNumMaterials`, `dgBoundGeometry::SetFriction`, `dgBoundGeometry::SetElasticity`, `dgBoundHotdog::GetMaterial`, `dgBoundHotdog::GetNumMaterials`, `dgBoundHotdog::SetFriction`, `dgBoundHotdog::SetElasticity`, `dgBoundSphere::GetMaterial`, `dgBoundSphere::GetNumMaterials`, `dgBoundSphere::SetFriction`, `dgBoundSphere::SetElasticity` | ported | `Bound::material`, `numMaterials`, `setFriction`, `setElasticity` | The bound's own material (1 material). |
| `phBoundBox::phBoundBox`, `phBoundBox::SetSize`, `phBoundBox::SetQuickTestInfo`, `phBoundBox::GetMaterial`, `phBoundBox::GetEdgeCosine`, `phBoundBox::GetEdgeNormal` | ported | `BoundBox` (`phys/Bound.cpp`) | |
| `phBoundBox::TestEdge`, `phBoundBox::TestProbe`, `phBoundBox::TestProbeSlave` | ported | `BoundBox::testEdge`, `testProbe` | |
| `phBoundBox::FindImpactSphereToBox`, `phBoundBox::FindImpactsBoxToBox`, `phBoundBox::FindImpactsBoxToBoxOffset`, `phBoundBox::BoxToBoxFaceImpacts`, `phBoundBox::BoxToBoxFaceImpactsOffset`, `phBoundBox::FindFaceDots`, `phBoundBox::RemoveFaceDotZero`, `phBoundBox::CheckFourFaceDotPattern`, `phBoundBox::RemoveFifthFaceDotZero`, `phBoundBox::MakeTransformedCorners`, `phBoundBox::AddEdgeChecks`, `phBoundBox::AvoidEdgeChecks` (both), `phBoundBox::UseThisImpact` | ported | `phys/BoundBox.cpp` | (`phBoundBox::BoxToBoxFaceImpactsOffset` is `boxToBoxFaceImpacts` with the offset.) |
| `phBoundBox::TestProbePoint`, `phBoundBox::TestAIPoint` | not needed | | Point and AI tests (above). |
| `phBoundBox::~phBoundBox` | not needed | | Plumbing. |
| `phBoundCollision::testNoOverlap`, `phBoundCollision::SegSegDistNorm`, `phBoundCollision::GetDisp`, `phBoundCollision::SetPenetration` | ported | `phys/BoundCollision.cpp`, `kPenetration` | |
| `phBoundGeometry::phBoundGeometry`, `phBoundGeometry::Load`, `phBoundGeometry::LoadBinary`, `phBoundGeometry::PostLoadCompute`, `phBoundGeometry::ComputeEdges`, `phBoundGeometry::ComputeEdgeNums`, `phBoundGeometry::ComputeEdgeNormals`, `phBoundGeometry::ReComputeEdgeNormals`, `phBoundGeometry::EdgeInList`, `phBoundGeometry::SetQuickTestInfo`, `phBoundGeometry::ShiftCentroid` | ported | `asset::parseBnd` / `parseBbnd`, `makeGeometryBound`, `BoundGeometry` | |
| `phBoundGeometry::GetMaterial`, `phBoundGeometry::GetEdgeCosine`, `phBoundGeometry::GetEdgeNormal` | ported | `BoundGeometry::material`, `edgeCosine`, `edgeNormal` | |
| `phBoundGeometry::~phBoundGeometry` | not needed | | Plumbing. |
| `phBoundHotdog::phBoundHotdog`, `phBoundHotdog::SetSize`, `phBoundHotdog::CalculateBoundingBox`, `phBoundHotdog::GetMaterial`, `phBoundHotdog::IsInsideHotdog`, `phBoundHotdog::FindHotdogIsectNormal`, `phBoundHotdog::SegmentToHotdogIntersections`, `phBoundHotdog::TestEdge`, `phBoundHotdog::TestProbe`, `phBoundHotdog::FindImpactSphereToHotdog`, `phBoundHotdog::FindImpactsHotdogToHotdog`, `phBoundHotdog::FindImpactsHotdogToPoly` | ported | `phys/BoundHotdog.cpp`, `BoundHotdog` | |
| `phBoundHotdog::TestProbePoint`, `phBoundHotdog::TestAIPoint`, `phBoundHotdog::TestSphere` | not needed | | Above. |
| `phBoundHotdog::~phBoundHotdog` | not needed | | Plumbing. |
| `phBoundPolygonal::GetVertex`, `phBoundPolygonal::MaxDot`, `phBoundPolygonal::MinDot`, `phBoundPolygonal::TestEdge`, `phBoundPolygonal::TestProbe`, `phBoundPolygonal::BackupAbyPenetration`, `phBoundPolygonal::BackupDispByPenetration` | ported | `BoundPolygonal` (`phys/Bound.cpp`) | |
| `phBoundPolygonal::FindImpacts`, `phBoundPolygonal::FindImpactsPolyToPoly`, `phBoundPolygonal::FindImpactsSphereToPoly`, `phBoundPolygonal::TestBoundPolyPoly`, `phBoundPolygonal::TestBoundPolyPolyUseDot`, `phBoundPolygonal::TestBoundPolyPolyUseDotSmall`, `phBoundPolygonal::GetAllSegments`, `phBoundPolygonal::GetNextSegment`, `phBoundPolygonal::RewindSegments` (both), `phBoundPolygonal::GetNextEdgeIsect`, `phBoundPolygonal::GetCollideEdgePoly`, `phBoundPolygonal::CheckSaveEdgeEdge`, `phBoundPolygonal::DoEndPtSearch`, `phBoundPolygonal::RetryVertPolyCollide`, `phBoundPolygonal::MakeBsInside`, `phBoundPolygonal::ResetVertNeedsH` (both), `phBoundPolygonal::AddInteriorEdges` | ported | `phys/BoundPolygonal.cpp` | RewindSegments' second overload is reached only through UseDot here (`lvlLevelBound::TrivialCollideBoxToLevel` is unreachable). |
| `phBoundPolygonal::CenterBound`, `phBoundPolygonal::TestProbePoint`, `phBoundPolygonal::TestAIPoint`, `phBoundPolygonal::TestSphere` | not needed | | Above. |
| `phBoundSphere::phBoundSphere`, `phBoundSphere::SetRadius`, `phBoundSphere::GetMaterial`, `phBoundSphere::TestEdge`, `phBoundSphere::TestProbe`, `phBoundSphere::FindImpactSphereToSphere` | ported | `phys/BoundSphere.cpp`, `BoundSphere` | |
| `phBoundSphere::TestProbePoint`, `phBoundSphere::TestAIPoint`, `phBoundSphere::TestSphere` (both) | not needed | | Above. |
| `phBoundSphere::~phBoundSphere` | not needed | | Plumbing. |
| `phBoundTerrain::phBoundTerrain`, `phBoundTerrain::Load`, `phBoundTerrain::SetHotEdges`, `phBoundTerrain::PostLoadCompute` | ported | `asset::parseTer`, `makeTerrainBound` | SetHotEdges allocates the hot-edge bits only for a terrain with hot edges (none in the retail data); PostLoadCompute is empty for terrain. |
| `phBoundTerrain::CalculateBuckets`, `phBoundTerrain::ClearPolyTouched`, `phBoundTerrain::InitPolyIterator` (both), `phBoundTerrain::TestEdge`, `phBoundTerrain::TestProbe`, `phBoundTerrain::TestBoundPolyTerrain`, `phBoundTerrain::TestBoundTerrainEdgesVsPoly`, `phBoundTerrain::TestBoundTerrainPoly`, `phBoundTerrain::FindImpactsSphereToTerrain`, `phBoundTerrain::FindImpactsHotdogToTerrain` | ported | `phys/BoundTerrain.cpp` | |
| `phBoundTerrain::TestProbePoint`, `phBoundTerrain::TestAIPoint`, `phBoundTerrain::TestSphere` | not needed | | Above. |
| `phBoundTerrain::~phBoundTerrain` | not needed | | Plumbing. |
| `phBoundTerrainLocal::phBoundTerrainLocal`, `phBoundTerrainLocal::TestBoundTerrainPoly`, `phBoundTerrainLocal::TestBoundTerrainEdgesVsPoly`, `phBoundTerrainLocal::FindImpactsHotdogToTerrainLocal` | ported | `testBoundTerrainLocalPoly`, `findImpactsHotdogToTerrainLocal` | |

## phCollision, phCollisionPrim, phConvexPoly, phPolygon, phIntersection

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `phCollision::TestBoundGeneric`, `phCollision::GetRelDisp` | ported | `testBoundGeneric`, `getRelDisp` (`phys/Collision.cpp`) | |
| `phCollision::TestBoundForce`, `phCollision::SphereApplyCenterForceToSphere`, `phCollision::SphereApplyCenterForceToPoly`, `phCollision::SphereApplyCenterForceToHotdog` | not needed | | Force spheres: `dgPhysManager::CollideInstances` calls TestBoundForce only for a bound of the force-sphere kind, and nothing in build 3393 makes one (phys-core). |
| `phCollisionPrim::SegmentSphereTest` (both), `phCollisionPrim::SphereToPolygonal` | ported | `segmentSphereTest`, `sphereToPolygonal` (`phys/Geometry.cpp`) | |
| `phConvexPoly::ConvexPolyIntersect`, `phConvexPoly::PrecomputeRays`, `phConvexPoly::AdvanceV`, `phConvexPoly::GetvHeadOut`, `phConvexPoly::GetuHeadOut`, `phConvexPoly::GetuTailOut`, `phConvexPoly::RecordNoIsect`, `phConvexPoly::RecordInteriorCollides`, `phConvexPoly::RecordEE`, `phConvexPoly::RecordTail`, `phConvexPoly::RecordUTail`, `phConvexPoly::RecordVTail` | ported | `ConvexPolyIntersect` (`phys/BoundBox.cpp`) | (`phConvexPoly::GetvHeadOut`, `phConvexPoly::GetuHeadOut`, `phConvexPoly::GetuTailOut`, `phConvexPoly::RecordUTail`, `phConvexPoly::RecordVTail`: the run's getters and `recordTail`.) |
| `phPolygon::phPolygon`, `phPolygon::InitTriangle`, `phPolygon::InitQuad`, `phPolygon::CalculateNormal`, `phPolygon::ComputeEdgeNormalCross`, `phPolygon::TestSegmentDirected`, `phPolygon::TestSegmentUndirected`, `phPolygon::DetectSegmentDirected`, `phPolygon::DetectSegmentUndirected`, `phPolygon::SegEdgeCheckDirected`, `phPolygon::SegEdgeCheckUndirected` | ported | `Polygon` (`phys/Bound.cpp`) | |
| `phIntersection::phIntersection`, `phIntersectionPoint::phIntersectionPoint`, `phIntersectionPoint::Set`, `phIntersectionPoint::Transform` | ported | `Intersection`, `IntersectionPoint` (`phys/Bound.h`) | |

## phMaterial, phMaterialMgr

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `phMaterial::phMaterial`, `phMaterial::Load`, `phMaterial::LoadBinary`, `phMaterial::SetName`, `phMaterial::Copy` | ported | `embeddedBoundMaterial`, `parseMaterials`, `asset::parseBbnd` (materials) | Copy is lvlMaterial::Copy's base part. |
| `phMaterial::Save`, `phMaterial::SaveBinary` | not needed | | Saving. |
| `phMaterialMgr::phMaterialMgr`, `phMaterialMgr::CreateInstance`, `phMaterialMgr::Load` (both), `phMaterialMgr::AddToTable`, `phMaterialMgr::Find`, `phMaterialMgr::FindIndexOfName` | ported | `MaterialTable` (`phys/Material.cpp`), `CityLevel` | The bounds' material names resolved through the table. |
| `phMaterialMgr::FindNameOfIndex` | not needed | | Only the Save functions. |
| `phMaterialMgr::~phMaterialMgr` | not needed | | Plumbing. |

## Matrix34, Matrix44, Vector2, Vector3, Vector4, Quaternion

The Angel math helpers. OpenMM2 has `mm2::Vec3` / `Mat34` (`core/Math.h`)
for generic algebra and `phys::age` (`phys/AgeMath.h`) for the routines
whose float rounding the simulation depends on; each game port sums in
the order the original's compiled call site does (phys-core.md compared
every physics caller with the asm; camera-props, ai-vehicles and the city
code did the same for theirs). Helpers named here but used only by code
another subsystem owns are marked with that code's status.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `Matrix34::Identity`, `Matrix34::Identity3x3`, `Matrix34::Zero`, `Matrix34::Set` | ported | `Mat34::identity`, `Mat34{}`, copies | Identity3x3 is MakeRotate's angle-0 case (`age::makeRotate`). |
| `Matrix34::Add` (both), `Matrix34::Subtract`, `Matrix34::Add3x3`, `Matrix34::AddScaled`, `Matrix34::Scale` (both) | ported | `added` (`phys/Impact.cpp`), `age::add3x3`, `age::addScaled3x3`, `age::scale3x3` | Subtract only in the contact displacement (`phColliderBase::GetDisp`, `phBoundCollision::GetDisp`). |
| `Matrix34::Dot` (both), `Matrix34::Dot3x3` (both), `Matrix34::Dot3x3Transpose` (both), `Matrix34::Dot3x3CrossProdMtx`, `Matrix34::Dot3x3CrossProdTranspose` | ported | `age::dot`, `Mat34::mul`, `age::dot3x3`, `dot3x3InPlace`, `dot3x3Transpose`, `dot3x3TransposeInPlace`, `dot3x3CrossProdMtx`, `dot3x3CrossProdTranspose` | |
| `Matrix34::Transform`, `Matrix34::Transform4` | ported | `Mat34::transform`, `xform` (`phys/BoundPolygonal.cpp`) | |
| `Matrix34::Transpose` (both), `Matrix34::Inverse` (both), `Matrix34::FastInverse` (both) | ported | `age::transpose`, `age::inverse`, `Mat34::fastInverse` | |
| `Matrix34::SolveSVD` | ported | `age::solveSVD` | |
| `Matrix34::Normalize` | ported | `Mat34::normalize` | |
| `Matrix34::MakeRotate`, `Matrix34::MakeRotateUnitAxis`, `Matrix34::MakeRotateX`, `Matrix34::MakeRotateY`, `Matrix34::MakeRotateZ`, `Matrix34::Rotate`, `Matrix34::RotateUnitAxis` | ported | `age::makeRotate`, `makeRotateUnitAxis`, `makeRotateX` / `Y` / `Z`, `age::rotate`, `rotateUnitAxis`; `Mat34::rotationX` / `Y` / `Z`, `rotationAxis` | |
| `Matrix34::RotateFull`, `Matrix34::RotateFullUnitAxis`, `Matrix34::RotateY`, `Matrix34::RotateZ` | ported | `cam::rotateFull` (`game/CamTrack.cpp`), the dashboard needles (`session/Hud.cpp`) | Callers: `camTrackCS::UpdateTrack` (camera-props), `RadialGauge::Update` (hud-views), `gizBridge::Reposition` (drawbridges, world-objects), the ragdoll IK solvers, `asDofCS::Update` and `asViewCS::UpdateStereo`, `ptxGlass` (props-fx). |
| `Matrix34::MakeScale` (both) | ported | `TrailerJoint` (`diag`), `Hud` map icons | |
| `Matrix34::LookAt`, `Matrix34::PolarView`, `Matrix34::GetEulers` (both), `Matrix34::FromEulersZXY`, `Matrix34::RotateTo` | ported | `game/CamMath` (`lookAt`, `polarView`, `getEulersZXY`, `fromEulersZXY`), `VehicleRenderer` (`rotateUpTo`) | Cameras and the shadow's DrawPhysics. |
| `Matrix34::FromEulers`, `Matrix34::FromEulersXYZ`, `Matrix34::FromEulersXZY`, `Matrix34::FromEulersYXZ`, `Matrix34::FromEulersYZX`, `Matrix34::FromEulersZYX`, `Matrix34::ToEulersXZY` | ported | `game/CamCar` (`camAppCS::UpdateApproach`), `asset/Ped.cpp` (XZY) | FromEulers dispatches on an order string; the approach camera uses its own order (camera-props). XYZ is also used by `mmNetObject::PositionUpdate` (replaced by OpenMM2's network snapshots) and by the ragdoll IK, ToEulersXZY only by the ragdoll IK (world-objects). |
| `Matrix34::Interpolate`, `Matrix34::FromQuaternion`, `Quaternion::FromMatrix`, `Quaternion::Slerp` | replaced | `net::Snapshot` interpolation (`Quat::slerp`) | Interpolate (quaternion slerp of two frames) serves `mmNetObject::Predict` and the ragdoll IK; `Quaternion::FromMatrix` also `FindHomingAngAccel3D` (no caller reaches it in a race). |
| `Matrix44::Dot` (both), `Matrix44::FastInverse`, `Matrix44::Identity`, `Matrix44::Zero`, `Matrix44::MakeRotX` | replaced | `Mat44` (`core/Math.h`), `render/Projection` | Direct3D render state, viewport and the HUD map's projection (the Vulkan / OpenGL renderers). |
| `Matrix44::Hermite` | open | | The spline basis of `gizPathspline` (ferries, sailboats, trains: world-objects, not in OpenMM2) and `mmNetPath` (replaced by OpenMM2's networking). |
| `Vector2::Vector2`, `Vector2::Mag`, `Vector2::Mag2` | ported | `Vec2` | The street mesh builder (`psdl_draw_tunnel_junction`, `sdlPage16::GetDrawnSDLPrims`; city-render). |
| `Vector3::Add`, `Vector3::Subtract`, `Vector3::Scale` (both), `Vector3::InvScale`, `Vector3::Negate`, `Vector3::Set` (both), `Vector3::Vector3`, `Vector3::operator+=` (both), `Vector3::operator-=` (both), `Vector3::operator-`, `Vector3::operator/`, `Vector3::operator*=`, `Vector3::operator%` | ported | `Vec3` operators | Folded names: `Vector3::operator*=` assigns a scaled copy and `Vector3::SubtractScaled` adds (`Vector3::AddScaled` subtracts in `phBoundHotdog::FindImpactsHotdogToHotdog`); the ports follow the bodies (phys-core, phys-bounds). `operator%` is the cross product (cable cars, subway: world-objects). |
| `Vector3::AddScaled`, `Vector3::SubtractScaled` | ported | `BoundHotdog.cpp`, `InertialCS.cpp` | As above. |
| `Vector3::Dot` (both), `Vector3::Dot3x3`, `Vector3::Dot3x3Transpose`, `Vector3::Cross` (both) | ported | `age::dot`, `age::dot3x3`, `age::dot3x3Transpose`, `Vec3::cross`; `mulRow` (`TrailerJoint.cpp`, `InertialCS.cpp`) | The vector-times-3x3 helper laid out after the unnamed piece at 0x479620 (x summed (m1 y + m2 z) + m0 x, y and z (m0 x + m1 y) + m2 z) is `mulRow`, used by `dgTrailerJoint::DoJointLimits` and `phInertialCS::Update`. |
| `Vector3::Mag`, `Vector3::Mag2`, `Vector3::InvMag`, `Vector3::Normalize`, `Vector3::Dist`, `Vector3::FlatDist`, `Vector3::Lerp`, `Vector3::Angle`, `Vector3::Approach`, `Vector3::IsEqual` | ported | `age::mag`, `mag2`, `invMag`, `Vec3::normalized`, `Vec3::dist`, `lerp`; `cam::approach`, the AI's angle helpers, `city/SdlDraw` | IsEqual: `camAppCS::UpdateMaxDist` (camera-props). |
| `Vector3::RotateY` | ported | `VehicleRenderer` (headlight and siren beams) | Also `aiMap::SetWaypoints` (the ai subsystem's). |
| `Vector3::GetVector2` | ported | `projectOnFace` (`phys/BoundBox.cpp`) | The unnamed piece at 0x4c013a is its six-way switch table. |
| `Vector4::Dot3`, `Vector4::Cross`, `Vector4::Subtract`, `Vector4::Set` | ported | `dot3`, `cross` (`phys/Bound.cpp`) | The polygon segment tests; Set also `gizPathspline::Compute` / `mmNetPath::Compute`. |
| `Vector4::Dot` (both) | replaced | `Mat44::transform`, `render::Projection` | Viewport and light culling; the spline evaluation of `gizPathspline` / `mmNetPath` (above). |

## Unnamed pieces

The infrastructure record hands these pieces of split functions to this
subsystem; each was read with its function.

| Address | Belongs to | Status | Notes |
| --- | --- | --- | --- |
| 0x46ac70 | `dgPhysManager::CollideTerrain` | ported | The polygonal bound against the city (`findLevelImpacts`, phys-bounds). |
| 0x46b2d0, 0x46b390 | `dgPhysManager::CollideTerrain` | not needed | The sphere and the hotdog against the city (through the rooms the bound touches). CollideTerrain swaps a sphere or hotdog for the instance's bound 1 first, and the movers' bound 1 is never one: a prop's (`dgBangerInstance::GetBound(1)`) is a box around its bound, cars and traffic are polygonal (`World::collideTerrain` notes the same). |
| 0x469ea1 | `dgPhysManager`'s scalar deleting destructor | not needed | Misdecoded bytes before the destructor. |
| 0x475b5a | `phCollision::TestBoundForce` | not needed | The force sphere's relative displacement (force spheres do not exist). |
| 0x479620 | `phInertialCS::Update`, `dgTrailerJoint::DoJointLimits` | ported | `mulRow` (above). |
| 0x479690, 0x479830, 0x479970, 0x479bc0, 0x479d60 | phBoundBox's static tables | ported | The ±0.5 unit corners, the face and edge normal tables and the ±1 corner signs (`BoundBox::unitCorners`, `faceNormals`, `edgeNormalTable`, `kCornerSigns`; phys-bounds). |
| 0x489ed0 | `phBoundPolygonal::FindImpactsPolyToPoly` | ported | The per-intersection transform to world space (`toWorldCoords`). |
| 0x4c013a | `Vector3::GetVector2` | ported | Its switch table. |
| 0x4d6040 | `vehStuck::Init`, `vehStuck::vehStuck` | ported | The squares of PosThresh and MoveThresh (`Stuck::configure`). |
| 0x4d6a60 | `vehSplash::vehSplash` | not needed | An empty function the constructor calls. |
| 0x4d9067 | `vehEngine::Update` | ported | The rest of Update: the RPM from the drivetrain, the gear-change lag and clutch (`Engine::update`, compared with the asm by the vehicle audit). |

## For other subsystems

- frontend-ui: `Vehicle::SetSubMenu` (the garage opened from the
  multiplayer lobby without its GO button).
- world-objects: `Matrix44::Hermite` and `Vector4::Dot` for
  `gizPathspline`; the ragdoll IK users of `Matrix34::Interpolate`,
  `FromEulersXYZ` and `ToEulersXZY`; `lvlSegmentInfo::AllocateState` for
  cable cars and the subway.
- game-flow / session: the spawn placement now goes through
  `SimVehicle::setResetPos` / `reset()` / `respawnAt` (RaceScreen
  `loadVehicle`, `loadAiCar`, the Respawn and Restart events);
  `mmPlayer::ReInit` (a car change) and `mmGameMulti`'s restarts
  (`mmMultiRoam::Reset`, `mmMultiCR::Reset`: a new `RespawnXYZ` start) are
  theirs. The network cars' collider id (1000 + id) is replaced by
  RaceScreen's list of hits on remote cars.
- ai-vehicles: `ai::PoliceCar::reset` now uses the post as
  `aiPoliceOfficer::Reset` does; `placeOnCourse` (OpenMM2's repositioning of
  a stuck racer) still places by model matrix.
- props-fx: the one-shot wreck parts need their mesh
  (`VehicleRenderer::wreckParts`), as their finding asked.
