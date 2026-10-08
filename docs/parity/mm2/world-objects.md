# MM2 -> OpenMM2: world-objects

Audited from MM2Recomp (midtown2.exe build 3393) on 2026-10-08.

Summary: 297 reachable functions in 35 classes; ported 186 (of which newly
ported 123), replaced 30, not needed 75, open 6.

The city's moving and scripted objects. Before this audit OpenMM2 had none of
the gizmos and no cable cars; they now live in `src/game/world/` (Gizmos,
PathSpline, CableCars), driven from `RaceScreen` by small hooks, colliding
through `phys::World` as instances of the level's rooms (`InstanceSource`),
drawn with `MeshDraw`, and sounding through the audio module's
`BridgeAudio`, `SubwayAudio`, `AmbientObject` ("ferry") and `CableCarAudio`.
Tests: `tests/game/test_parity_world_objects.cpp`.

## Where the objects come from (mmGame::InitGizmos, init_gizmo_mgr)

`mmGame::Init` calls `InitGizmos` after the player and before `aiMap::Init`.
Each manager reads a path set `race/<city>/<city>_<kind>_<race>.pathset`
(a race mode, not cruise, when the race has its own: `<race>` is
`dgGameModeNames` with the race index, e.g. `london_bridge_circuit0`,
`london_ferry_crash9`) or else `race/<city>/<city>_<kind>.pathset`; without
either there is no manager. The managers are added in this order as children
of mmGame (so they update before `dgPhysManager::Update` in
`mmGameManager::Update`, and reset with `mmGame::Reset`):

| Kind | Default model | Created | Retail |
| --- | --- | --- | --- |
| sailboat | giz_sailboat01_f | always | London 16 (tugs, water taxis, ducks), SF 16 (sailboats, windsurfers) |
| bridge | giz_bridge01_l | always (none without that model) | London 7 leaves (Tower Bridge and two Waterloo bridges two leaves each, the Tower's drawbridge open), SF 2 (the China Town gate, inactive) |
| train | va_ug_l | always | London 8 tube trains of 3 cars |
| ferry | giz_carferry01_f | single player only | London 6, SF 1 |
| parkedcar | giz_pcar0N_l | single player, and network races (not network cruise or Cops and Robbers) | London 492, SF 90 (seed 1) |

In a network game in London every bridge is set open (type 3) and the bridge
manager reset. `InitGizmos` also adds the player's car and each aiMap
opponent as bridge proximity triggers, but aiMap has no opponents yet
(`aiMap::Clean` zeroed them), so the player is the only trigger. The random
draws (sailboat paint jobs and speeds, ferry speeds, parked cars) come from
MM2's global `rand()`; OpenMM2 gives them their own generator seeded with 1
(inferred: the draw order is MM2's, the global state is not reproducible).

Then `aiMap::Init` creates the cable cars (see aiCableCar) unless the state
pack's EnableCableCars is clear (`mmGameMulti::Init` clears it and
EnableSubways for every network game).

OpenMM2: `game/world/Gizmos.cpp` `gizmoPathSetPath`, `GizmoKinds::forSession`,
`Gizmos::load`, `initGizmos`; `RaceScreen::loadEffects` (after the city's
props), `update` (before the physics step), `drawLevel`, `updateAudio`, and
the race restart.

## _global

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `init_gizmo_mgr` (gizSailboatMgr, gizBridgeMgr, gizTrainMgr, gizFerryMgr, gizParkedCarMgr: 5) | ported (new) | `Gizmos.cpp` `gizmoPathSetPath`, `Gizmos::load`, `initGizmos` | The race's own path set first in a race mode, else the city's; no file, no manager. |
| 23 SEH unwind funclets (`??0`/`??1`/`Init` of aiCableCar, aiCableCarAudio, aiSubway, gizSailboat, gizSailboatMgr, gizBridge, gizBridgeMgr, gizFerry, gizFerryMgr, gizTrain, gizTrainCar, gizTrainMgr, `init_gizmo_mgr`, and `??0vehDrivetrain`) | replaced | C++ unwinding | Compiler-generated exception cleanup. `??0vehDrivetrain@@QAE@XZ_SEH` belongs to the vehicle subsystem. |

## gizPathspline

The spline every moving gizmo follows: a closed loop of cubic Hermite
segments (Matrix44::Hermite) through the path's points with Catmull-Rom
tangents (half the difference of the neighbours; the first segment's
predecessor is the last point), travelled at a speed in m/s: the ratio along
a segment is speed x time / length, the length being the chord through the
segment's middle. Past a segment's end the next one starts with the time
left; going backwards (negative time) the previous one. A path of exactly two
points does not move.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `gizPathspline::gizPathspline`, `~gizPathspline`, `Init`, `Reset` | ported (new) | `PathSpline::init`, `reset` | Speed 1 until set; Reset starts the segment from point 0 to 1. A path of fewer than two points is left still (OpenMM2; MM2 would divide by zero, no retail path has one). |
| `gizPathspline::Compute` | ported (new) | `PathSpline::compute` | Hermite coefficients summed in the asm's order (p1, t0, t1, then p0; the z row's t^3 coefficient through Vector4::Dot); `Matrix44::Hermite` and that `Vector4::Dot` are ported here. |
| `gizPathspline::Solve`, `Update`, `UpdateRatio`, `GetCurrRatio` | ported (new) | `PathSpline::solve`, `update`, `updateRatio`, `currentRatio` | One segment change per call, as coded. |
| `gizPathspline::ComputePath`, `IncrementPath`, `DecrementPath` | ported (new) | `PathSpline::computePath`, `incrementPath`, `decrementPath` | |
| `gizPathspline::GetNumVertex`, `GetVertex`, `SetSpeed` | ported (new) | `PathSpline::vertexCount`, `vertex`, `setSpeed` | |

## gizInstance, gizSailboat, gizSailboatMgr

Boats on spline loops. `gizSailboatMgr::Init` makes one boat per path, its
model the path's name when `geometry/<name>.pkg` exists (giz_tug01_l,
giz_watertaxi01_l, giz_duck_s, giz_sailboard01_f...) else giz_sailboat01_f;
`gizSailboat::Init` is `gizInstance::Init` (geometry, the banger data's CG
height kept to raise the matrix, a random paint job), the spline at 4 m/s,
the matrix at the spline's start (Y up, Z along the tangent,
Matrix34::Normalize) and its room. The manager then sets each speed to the
path's spacing byte +- 1 m/s (frand): ducks 1..3 m/s, windsurfers 29..31.
Each unpaused frame the boat moves along its spline. Boats are drawn as their
room's instances (gizInstance::Draw: lit, the paint job, the LOD
lvlInstance::IsVisible picks) and not collided (no collidable flag).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `gizInstance::gizInstance`, `~gizInstance`, `Init` | ported (new) | `Gizmos::loadSailboats` (`Sailboat`) | Paint job irand() taken modulo the model's paint jobs when drawn. |
| `gizInstance::GetMatrix`, `SetMatrix`, `GetPosition` | ported (new) | `Sailboat::matrix` | SetMatrix raises the position by the banger data's CG height. |
| `gizInstance::Draw` | ported (new) | `Gizmos::draw` | Lit, the paint job; the dgBangerInstance LOD raise for flag 4 does not apply (gizInstance has its own Draw). |
| `gizSailboat::gizSailboat`, `~gizSailboat`, `Init`, `Reset`, `SetSpeed`, `Update` | ported (new) | `Gizmos::loadSailboats`, `reset`, `updateSailboat` | Update is skipped while paused (OpenMM2 does not update paused frames). |
| `gizSailboat::SizeOf` | not needed | - | Allocation size. |
| `gizSailboatMgr::gizSailboatMgr`, `~gizSailboatMgr`, `Init`, `Reset`, `Update` | ported (new) | `Gizmos::loadSailboats`, `reset`, `update` | Speed: the path's spacing +- 1 (the asm confirms the path's +0x34, not the manager's base). |
| `gizSailboatMgr::'scalar_deleting_destructor'` | replaced | C++ delete | |

## gizBridge, gizBridgeMgr

Drawbridges. `gizBridgeMgr::Init` needs geometry/giz_bridge01_l (else no
bridges). Each path gives a leaf at its first point opening away from its
third point (its second on a two-point path), and a path of three points a
second leaf at the third point opening away from the first; the two are
partners. The model is the path name's second token after ':' or '@'
("open:giz_bridge02_l"), or the plain name, when that geometry exists, else
the default; the type is the name's first four characters (case-blind):
"inac" never moves, "time" (also the default) waits 10 s, rises, stays up
10 s and comes down, "prox" rises when a trigger comes within 100 m, "open"
stands open. A leaf turns about its X axis at 0.05 rad/s to 0.471238941 rad
(0.15 pi), pivoting at the hinge with its centre half its banger Size.z
behind it, lowered 0.3 m (`Reposition`); the drawbridge sounds play while it
moves (activated when it starts, deactivated when it stops; a proximity
trigger starts it without activating them, as coded). Init flags the rooms at
the hinge and 5 m above it with level room flag 0x10, where
`vehCar::UpdateTrack` lays no skid marks. Leaves are unhit banger instances
that never break loose (lvlInstance flags 0x132), collided by the physics and
the wheels; the manager draws them by the distance from the camera to the
hinge (high within 200 m, then medium, low, very low, none beyond 800 m).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `gizBridge::gizBridge`, `~gizBridge`, `Init` | ported (new) | `Gizmos::loadBridges`, `makeBody`, `placeBanger` | Init's Reset runs with the constructor's type (timed): the type from the name is set afterwards, so an open leaf opens at the next Reset (`initGizmos` resets, as mmGame::Reset does before the race). |
| `gizBridge::Reset`, `Reposition` | ported (new) | `Gizmos::resetBridgeState`, `repositionBridge` | Reset's Aud3DAmbientObject::Reset is `AmbientObject::reset`. |
| `gizBridge::Update`, `Trigger` | ported (new) | `Gizmos::updateBridge`, `triggerBridge` | Timers in frame seconds. |
| `gizBridge::Cull`, `Draw` | ported (new) | `Gizmos::draw` | Draw is empty in MM2 (the rooms do not draw bridges); Cull is dgBangerInstance::Draw (unlit with BillFlags 0x80). |
| `gizBridge::SizeOf` | not needed | - | |
| `gizBridgeMgr::gizBridgeMgr`, `~gizBridgeMgr`, `Init`, `Reset`, `Update`, `Cull` | ported (new) | `Gizmos::loadBridges`, `reset`, `update`, `draw` | Init's helpers (the leaf frame, the name's model and type) are `bridgeFrame`, `bridgeModel`, `bridgeType`. |
| `gizBridgeMgr::CheckProximity`, `AddProximityTrigger`, `GetBridges` | ported (new) | `Gizmos::update` (`trigger`), `bridges()` | The player's car (its inertial position) is the only trigger, as in MM2. |
| `gizBridgeMgr::'scalar_deleting_destructor'` | replaced | C++ delete | |

## gizTrain, gizTrainCar, gizTrainMgr

London's tube trains (`london_train.pathset`, paths named PATHnn: model
va_ug_l). Each train is three cars on the same spline, set up 0.44 s of
travel at 40 m/s apart. A train waits 10 s in a station, accelerates (its
speed factor rising at 0.51/s) to 40 m/s, runs until its rear car reaches the
third point from the path's end (going back: its other end car within the
first two points), brakes at 0.51/s, and leaves the other way after the next
10 s. The paths are treated as closed loops, so the first and last segments
bulge towards the far end (as in MM2). A car's height runs straight between
its segment's two points (not the spline's), raised by the CG height. The
train's sounds ("subwaycar") follow its middle car, at speed 50 while it
moves and 0 in a station. Cars are unhit banger instances like the bridges,
drawn by their rooms.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `gizTrain::gizTrain`, `~gizTrain`, `Init`, `Reset`, `Update`, `InStation` | ported (new) | `Gizmos::loadTrains`, `resetTrain`, `updateTrain`, `inStation` | |
| `gizTrain::CalcTrainAccel` | ported (new) | `updateTrain` | Only reads the spline's ratio: no effect. |
| `gizTrainCar::gizTrainCar`, `~gizTrainCar`, `Init`, `Reset`, `Update`, `IsFirstStop`, `IsLastStop` | ported (new) | `Gizmos::loadTrains`, `resetTrain`, `updateTrainCar`, `inStation` | |
| `gizTrainCar::SizeOf` | not needed | - | |
| `gizTrainMgr::gizTrainMgr`, `~gizTrainMgr`, `Init`, `Reset`, `Update`, `ApplyTuning` | ported (new) | `Gizmos::loadTrains`, `reset`, `update` | ApplyTuning is empty. |
| `gizTrainMgr::'scalar_deleting_destructor'` | replaced | C++ delete | |

## gizFerry, gizFerryMgr

Ferries (single player only): one per path, model the path's name
(giz_carferry01_l) or giz_carferry01_f, on a spline at 0.75 m/s (the
manager's speed with no variation; frand is drawn anyway), still on a
two-point path. A ferry is a dgUnhitYBangerInstance (only its rotation about
Y is kept), placed with its CG offset at Init and raised by the CG height
each frame; collided like the bridges (the cars can drive on it; MM2 gives it
no velocity). The manager draws them by their distance from the camera, as
the bridges; their "ferry" sounds follow them.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `gizFerry::gizFerry`, `~gizFerry`, `Init`, `Reset`, `Update`, `SetSpeed` | ported (new) | `Gizmos::loadFerries`, `reset`, `updateFerry` | Reset: `AmbientObject::reset`, the room, the spline. |
| `gizFerry::Cull`, `Draw` | ported (new) | `Gizmos::draw` | Draw is empty in MM2. |
| `gizFerry::SizeOf` | not needed | - | |
| `gizFerryMgr::gizFerryMgr`, `~gizFerryMgr`, `Init`, `ApplyTuning`, `Reset`, `Update`, `Cull` | ported (new) | `Gizmos::loadFerries`, `reset`, `update`, `draw` | The manager's pause flag (+0x30) is never set. |
| `gizFerryMgr::'scalar_deleting_destructor'` | replaced | C++ delete | |

## gizParkedCarMgr

Parked cars are ordinary props. Every path is walked as `dgPath::Enumerate`
walks it (type 2 line strips at the path's spacing, at least 5 m); at each
point irand() % 3 picks nothing (0) or giz_pcar01_l / giz_pcar02_l whatever
the city, with the path's frame turned a quarter turn about Y
(`Matrix34::Dot` with `MakeRotateY(pi/2)`, the point's position kept), a
full-matrix unhit banger (`RequestBanger(name, 1)`), paint job irand()
(`SetVariant` takes it modulo the model's variants), in the room found at the
point. The BangerSet does the rest (they can be knocked over).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `gizParkedCarMgr::Init` and the path walk (`gizParkedCarMgr_EnumeratePath`) | ported (new) | `Gizmos.cpp` `placeParkedCars`, `initGizmos` | The walk reuses `bangers::placePathSet` (dgPath::Enumerate) with the spacing raised to 5 m; `Matrix34::Dot`'s three-argument form in its summation order. |
| `gizParkedCarMgr::gizParkedCarMgr`, `~gizParkedCarMgr`, `Reset`, `Update`, `ApplyTuning` | ported (new) | `initGizmos` | Reset, Update and ApplyTuning are empty. |
| `gizParkedCarMgr::'scalar_deleting_destructor'` | replaced | C++ delete | |

## aiCableCar, aiCableCarInstance

San Francisco's cable cars. `aiMap::Init` makes one per intersection that is
a cable-car start (`aiIntersection::IsCableCarStart`: exactly one of its
roads leaves it on a cable-car line, the .bai side's tram polyline: aiPath
+0xdc for direction 1, +0x78 for -1): four in San Francisco, none in London.
`aiCableCar::Init` gives each its instance (BODY, SHADOW and HLIGHT
geometry), an acceleration of 2 frand + 1.5 m/s^2 and its bumper and side
distances from the box of its bound; `DetermineSister` finds the car at the
far end of its line. `aiMap::Update` updates them after the racers, and
`aiMap::Reset` resets them (to their start, at rest).

A car follows its line section by section on aiRailSet XZ Hermite curves
(`SolveRailType`); after the road's last section (shortened by its front
bumper) it turns through the intersection to the next road with a line
(`DetermineNextLink`: back along the road at the end of a line, the other
road where two lines meet, the second one round where four do), the curve's
length the Manhattan distance. `SolveVelocity`: up to 15 m/s at its
acceleration; it brakes to stop 2.5 m short of an obstacle
(`CheckForObstacles`: the player within 30 m, then the vehicles listed in the
obstacle map on its side at the section ahead and, within 30 m of the curve's
end, at the next, blocking the way to that section's line point by
`aiVehicle::IsBlockingTarget` with 30 m of reach, 2 m wide; its corners are
taken in order, so a car facing away is measured at its front), and within
25 m of an intersection it brakes to stop 0.25 m short of it until
`OkayToEnterIntersection` (a road told to always stop never; a four-way stop
once it stands within 1.5 m below 0.5 m/s and its turn comes; a light on
green; no control always). Its height and tilt come from three ground probes
(front left, front right, back left, 5 m above to 5 m below, the wheels'
mask) except where its road and the next are both flat (path flag 8: the
line's first point's height). `UpdateObstacleMap` lists it in the obstacle
map. Its instance is an unhit banger (flags 0x13) whose ImpulseLimit2 of
7.6e9 keeps it standing like a wall; its bound (the banger data's, centred on
the CG) is placed by the car's matrix at the model's origin, so it sits
1.65 m low, as in MM2.

OpenMM2: `game/world/CableCars.cpp`; `RaceScreen` creates them (not in a
network game), updates them after the AI drivers, draws them, plays their
`CableCarAudio`, resets them with the race.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `aiCableCar::aiCableCar`, `~aiCableCar`, `Init` | ported (new) | `CableCars::create` | One frand per car, after the gizmos' draws (aiMap::Init follows InitGizmos). |
| `aiCableCar::DetermineSister` | ported (new) | `CableCars::determineSister` | As coded, the direction followed only changes through IsCableCarStart's outputs and the road it last reported is looked up. |
| `aiCableCar::Reset` | ported (new) | `CableCars::resetCar` | |
| `aiCableCar::Update` | ported (new) | `CableCars::updateCar`, `updateAudio` | The knocked-loose branch (instance flag 1 cleared: the sister stops, the car resets once `aiMap::FindAmbAppRoad` finds no ambient car coming) cannot run: see ImpactCB. |
| `aiCableCar::DetermineNextLink`, `SolveRailType`, `DistanceToIntersection`, `SolveVelocity`, `SolvePositionAndOrientation` | ported (new) | `nextLink`, `solveRailType`, `distanceToIntersection`, `solveVelocity`, `solvePositionAndOrientation` | Leaving an intersection, the stop sign removed from is the road's direction-1 end whatever way the car came, as coded. |
| `aiCableCar::CheckForObstacles` | ported (new) | `checkForObstacles` | Reads the other cable cars from its own map and the ambient cars from `ai::Traffic::roadVehicles` (the traffic's per-section lists, read only), with aiVehicleSpline's bumper and side distances from their VehicleData. |
| `aiCableCar::OkayToEnterIntersection` | ported (new) | `okayToEnterIntersection`, `stopSignOkayToGo`, `removeFromStopSign` | Deviation: the four-way stop queue (aiIntersection::AddToStopSignCntl / StopSignOkayToGo / RemoveFromStopSignCntl) holds the cable cars only (MM2 shares it with the ambient cars). The light is the one of the side's first lane. A road told to always go (aiPath +0x160) has no counterpart in OpenMM2's traffic. |
| `aiCableCar::UpdateObstacleMap` | ported (new) | `updateObstacleMap` | Into the cable cars' own map (see open). |
| `aiCableCar::Type`, `Speed`, `FrontBumperDistance`, `BackBumperDistance`, `LSideDistance`, `RSideDistance`, `Position`, `GetMatrix` | ported (new) | `CableCars::Car`, `matrix`, `speed`, the `TrackedCar` built in `checkForObstacles` | Type 5 (no grouping at stop signs). |
| `aiCableCar::CurrentRoadIdx`, `CurrentRdVert`, `CurrentLane`, `CurrentRoadId` | open | - | The obstacle interface the ambient cars read when they meet a cable car in the obstacle map; open with the traffic side (below). |
| `aiCableCar::DrawId` | not needed | - | Debug drawing (empty). |
| `aiCableCarInstance::GetMatrix`, `SetMatrix`, `GetPosition` | ported (new) | `CableCars::Body::matrix` | The car's matrix. |
| `aiCableCarInstance::Draw` | ported (new) | `CableCars::draw` | The BODY meshes, first paint job, lit, LOD by lvlInstance::IsVisible. |
| `aiCableCarInstance::GetVelocity` | open | - | -m2 x speed; `phys::Instance` has no velocity for static instances (same for the gizmos, which MM2 gives none). |
| `aiCableCarInstance::ImpactCB` | open | - | Keeps the hit banger a car breaks into; with ImpulseLimit2 7.6e9 the break is out of reach, so OpenMM2's `Body` never breaks loose (needs a BangerSet active and the Update branch above). |
| `aiCableCarInstance::SizeOf` | not needed | - | |

## aiCableCarAudio, aiCableCarAudioData, aiSubwayAudio

Ported by the audio area (`audio/game/Ambience.*`); now driven: the cable
cars' `CableCarAudio` (position and speed each frame), the trains'
`SubwayAudio`, the bridges' `BridgeAudio`, the ferries' "ferry"
`AmbientObject`, loaded once each with the race's Object3DManager.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `aiCableCarAudio::aiCableCarAudio`, `~aiCableCarAudio`, `Init`, `AssignSounds`, `UnAssignSounds`, `UpdateAudio` (both), `Reset` | ported | `CableCarAudio::load`, `update`, `slotLost`, `reset`; `CableCars::loadAudio`, `updateAudio`, `resetCar` | |
| `aiCableCarAudioData::aiCableCarAudioData`, `~aiCableCarAudioData`, `AssignSounds`, `UnAssignSounds`, `Stop`, `UpdatePlay`, `UpdateState` | ported | `CableCarAudio` | |
| `aiSubwayAudio::aiSubwayAudio`, `~aiSubwayAudio`, `Activate`, `Deactivate`, `Update` | ported | `SubwayAudio`; `Gizmos::updateAudio` | gizTrain is its only owner in retail (no aiSubway is ever made). |

## aiSubway, aiSubwayInstance

`aiMap::Init` "Create the subways" makes subway trains only when the state
pack's EnableSubways is set (not in network games) and the city's AI map
(aiCityData) names a [Subway] model: `city/london.aimap` has the section
commented out (`#[Subway]` / `#va_ug_l 3`) and no other retail .aimap has
one, so no aiSubway exists in the retail game (London's tube trains are
gizTrains).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `aiSubway` (28: constructor, destructor, Init, Reset, Update, DetermineNextLink, SolvePositionAndOrientation, SolveVelocity, ReverseDirection, OkayToEnterIntersection, DistanceToIntersection, SolveRailType, CurrentRoadIdx, CurrentRdVert, UpdateObstacleMap, ComputeXZCurve, SolveXZCurve, DrawId, Type, Speed, CurrentLane, the bumper and side distances, Position, GetMatrix, CurrentRoadId) | not needed | - | Never instantiated with the retail data (above). |
| `aiSubwayInstance` (7: Draw, GetMatrix, SetMatrix, GetVelocity, GetPosition, ImpactCB, SizeOf) | not needed | - | Same. |

## lvlLandmark, lvlFixedAny, lvlFixedMatrix, lvlFixedRotY

The city's static instances (`lvlLevel::LoadInstances`). A record with
type flag 1 and without the banger flag 2 (OpenMM2's `Instance::flags`
0x100) is an lvlLandmark: a lvlFixedRotY with its own terrain bound, 176 in
London and 88 in San Francisco; its `IsCollidable` answers false, so
`dgPhysManager::GatherCollidables` takes it only for movers colliding with the
city (flag 2), through `IsTerrainCollidable`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `lvlLandmark::Init`, `IsVisible` | ported | `CityLevel`, `CityRenderer` | lvlFixedAny::Init and lvlFixedRotY::IsVisible. |
| `lvlLandmark::IsCollidable` | ported (fixed) | `CityLevel.cpp` (StaticInstance `collidable`) | Was collidable; now false, terrain-collidable stays true. Test `WorldLandmarks.CollideThroughTheCityFlagOnly`. Edit outside this area (CityLevel, one line). |
| `lvlLandmark::IsLandmark`, `SizeOf` | not needed | - | Nothing asks IsLandmark but lvlMultiRoomInstance forwarding it. |
| `lvlFixedAny::Draw`, `Init`, constructor | ported | `CityRenderer::drawModel`, `CityLevel` | Audited by rendering-fx / city-render. |
| `lvlFixedAny::SetVariant` | ported | `CityRenderer` (`staticVariant`, integration branch) | The record's low flag byte picks the shader set (city-render). |
| `lvlFixedAny::DrawShadow`, `DrawShadowMap`, `DrawReflectedParts` | not needed | - | No retail city model has a shadow or reflected part (rendering-fx, city-render). |
| `lvlFixedMatrix::GetMatrix`, `SetMatrix`, `GetPosition`, constructor | ported | `city/Inst.cpp`, `CityLevel` | The record's matrix. |
| `lvlFixedMatrix::IsVisible` | ported | `CityRenderer` (`fixedObjectFacesAway`, integration branch) | city-render. |
| `lvlFixedRotY::GetMatrix`, `SetMatrix`, `GetPosition`, constructor, `IsVisible` | ported | `city/Inst.cpp` (compact form), `CityRenderer` | Rows (c, 0, s), (0, 1, 0), (-s, 0, c); its facing test is gated by `lvlLevel::sm_PhysicsMode` (city-render's record). |
| `lvlFixedMatrix::SizeOf`, `lvlFixedRotY::SizeOf` | not needed | - | |

## lvlSky, lvlTrackManager, ltLight, ltLensFlare, mmBillInstance

Already ported by other areas; this audit checked that the cited ports cover
the reachable functions and named the ones done without a name.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `lvlSky::AutoInit`, `Init`, `SetupFog`, `Update` | ported | `city/Environment.cpp`, `CityRenderer` | rendering-fx, ai-ambient-city. |
| `lvlSky::Draw` | ported | `CityRenderer::drawSky` | Lighting, fog and depth off around DrawHat (named in the comment now). |
| `lvlTrackManager::lvlTrackManager`, `~lvlTrackManager`, `Init`, `Reset`, `Update`, `Draw` | ported | `fx/SkidMarks` (`SkidTrack`, `SkidRenderer`) | rendering-fx. |
| `lvlTrackManager::AddVertex` | ported | `SkidTrack::push` | Named in the comment now: a full ring drops its oldest pair, and the next when it starts a strip. |
| `ltLight::ltLight`, `Default`, `ComputeIntensity`, `DrawGlow` | ported | `fx/LensFlares.cpp`, `VehicleRenderer` | rendering-fx. |
| `ltLight::DrawGlowBegin`, `DrawGlowEnd` | ported | `VehicleRenderer::drawGlows` | Unlit, additive, no depth writes (named in the comment now). |
| `ltLight::~ltLight`, `ShutdownLights` | replaced | `TextureLibrary` | Frees the lt_glow texture. |
| `ltLensFlare::ltLensFlare`, `~ltLensFlare`, `Draw`, `DrawBegin`, `DrawEnd` | ported | `fx/LensFlares` | rendering-fx; DrawEnd named now. |
| `mmBillInstance::mmBillInstance`, `Init`, `Draw` | ported | `session/Hud.cpp` `drawCrObjects` | The Cops and Robbers bases (session). |
| `mmBillInstance::SizeOf` | not needed | - | |

## dgGlassInstance, ptxGlass, ptxGlassBirthRules

Glass props: `dgUnhitBangerInstance::RequestBanger` makes a dgGlassInstance
only for banger data with BillFlags 0x100, which no retail .dgBangerData sets
(the `-andyglasshack` switch would set it for sp_streetlight03_m). The city
load still calls `dgGlassInstance::InitStaticSystems` (two ptxGlass systems
on s_win_trans_01), which nothing then uses.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `dgGlassInstance` (7: constructor, InitStaticSystems, Reset, Impact, Draw, DrawGlow, SizeOf) | not needed | - | No glass prop in retail data. |
| `ptxGlass` (5: Init, AddShards, CreateShards, Update, DrawShards) | not needed | - | Only glass props use it. |
| `ptxGlassBirthRules` (4: FileIO, GetClassName, destructor, deleting destructor) | not needed | - | Same. |

## pedRagdollMgr, Spline

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `pedRagdollMgr` (5: Attach, Reset, Update, destructor, deleting destructor) | not needed | - | Its constructor and Init have no caller, so `pedRagdollMgr::Instance` is never made; `aiPedestrianInstance::AttachEntity` would call Attach, but pedestrians are neither collidable nor bounded (IsCollidable false, GetBound null), so the physics never asks. The ragdoll IK (Quaternion, Matrix34::Interpolate, FromEulersXYZ, ToEulersXZY) goes with it. |
| `Spline` (5: constructor, destructor, deleting destructor, Update, FixTimeStop) | not needed | - | Its only instance is camTrackCS +0x1bc, never initialised (Init and SetGoal have no caller): Update copies no values. |

## For other areas and the coordinator

- Room visibility (`RoomVisibility.h`, cityLevel::DrawRooms): the train
  cars, the sailboats and the cable cars are drawn only from the rooms the
  city lists for the view, from the rooms they are kept in
  (`GizmoBody::room`, `Gizmos::Sailboat::room`, the cable car's body); the
  bridges and the ferries are drawn by their managers' Cull whatever the
  rooms, as in MM2. Parked cars are BangerSet props.
- A race restart resets the gizmos and the cable cars right after the props
  (lvlLevel::ResetInstances), as mmGame::Reset and aiMap::Reset do.
- ai-vehicles: the ambient traffic does not see the cable cars (MM2 lists them
  in the same per-section obstacle lists and at the same four-way stops, and
  the ambient cars read their aiObstacle interface). Porting it needs
  `ai::Traffic` to hold external rail vehicles in its obstacle lists and stop
  queues (open: `aiCableCar::CurrentRoadIdx`, `CurrentRdVert`, `CurrentLane`,
  `CurrentRoadId`).
- phys: static instances have no velocity (`aiCableCarInstance::GetVelocity`).
- Edits outside this area: `BangerSet` gained `boundOf` / `boundRadius`
  (accessors), `CityLevel.cpp` the landmark fix, `RaceScreen.cpp` the hooks,
  comments naming MM2 functions in `SkidMarks.h`, `VehicleRenderer.cpp`,
  `LensFlares.h`, `CityRenderer.cpp`.
