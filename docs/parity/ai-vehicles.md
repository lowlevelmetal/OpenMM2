# Parity audit: ai-vehicles

Audited against MM2Recomp (midtown2.exe build 3393) on 2026-10-07.

Scope: `src/ai/Course.*`, `Driving.*` (with `DrivingRoute.cpp` and
`DrivingTargets.cpp`), `MapView.*`, `PathGeometry.*`, `Opponent.*`,
`Police.*`, `RoadNetwork.*`, `Traffic.*`, `VehicleControl.h`,
`VehicleData.*`, `src/game/TrafficBodies.*` (all class P).

Summary (after the second pass): 170 rows (a row may cover several helpers
of one MM2 function); verified 73, fixed 63, open 3, inferred 2, deviation 3,
openmm2 26. The Missing table lists 10 MM2 functions or groups, 4 of them
now ported. In the opponent sweep (`OPENMM2_AI_SWEEP=1`) 516 of 517
opponents finish (517 after the first pass, 516 before the audit). The one
that does not, in sf race11 p, is wrecked by landing impacts: every racer
of that 8 km professional race (vppanozgt at 40 - 100 m/s over SF's crests)
collects 330 000 - 420 000 damage against a maximum of 312 500 from about
50 impacts with the terrain at the same places (the first at 40 - 57 m/s
7 s in, the largest near the finish at 93 - 102 m/s), and a point-to-point
race repairs nothing; all six are wrecked, five after the line. The damage
comes from the ported vehCarDamage::Impact on those landings, so whether
MM2's racers wreck there is a matter for the physics (vehicle, phys-core),
not the drivers, which keep MM2's lines; which car crosses first depends on
the run.

Two structural notes frame the whole area and are recorded once here
rather than on every row:

- **The aiPath window (second pass).** MM2's aiVehiclePhysics plans on a
  window of three aiPath roads (the road the car is on and the next two of
  its waypoint intersections), indexing vertices, curb rows and per-road
  sharp-turn circles. The first pass planned on an `ai::Course` (the roads
  joined into one line) instead; the second pass ports the window itself
  (RegisterRoute, PlanRoute, LocateWayPtFromRoad, CalcRoadTarget, the turn
  circles, IsTargetBlocked's obstacle lists, CalcRoadSpeed's turns) on
  `ai::MapView` (aiMap's rooms and components) and `ai::PathGeometry`
  (aiPath's helpers). The course remains, OpenMM2's own, to measure race
  progress and to put a stranded car back on its route.
- **Frame rate.** MM2 runs the AI once per rendered frame and scales nothing
  by time except velocity integration; OpenMM2 runs it per frame at the
  frame time and scales MM2's per-frame factors (momentum x 0.95, x 0.85,
  x 1.03, the 65-frame backup) to the time as for 30 Hz (`perFrame`). The
  ambient traffic runs in fixed 30 Hz steps (World, another area).

## Driving (`ai/Driving`, aiVehiclePhysics / aiStuck / aiVehicle)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `TrackedCar` | `aiVehicle` interface (Position, GetMatrix, Speed, Front/BackBumperDistance, L/RSideDistance) | fixed | Was a model-origin position and symmetric half sizes. Now carries MM2's view: a physics car's ICS position and frame (vehCarSim +0x90, GetMatrix = the ICS matrix), its vehCarSim Speed, its bumper and side distances, the bound box Block reads. |
| `trackedCar` | `aiVehiclePlayer` / `aiVehiclePhysics` accessors | fixed | New. Players: half of vehCarSim Size (= InertiaBox, vehCarSim::Init). AI cars: their bound box from the model origin (aiVehiclePhysics::Init: front -min z, back max z, left -min x, right max x). |
| `trackedAmbient` | `aiVehicleSpline` accessors, `aiVehicleSpline::Init` | fixed | New. AI matrix and speed; bumpers from the aiVehicleData box about CG (GetBound(1) of the instance). |
| `TrackedCar::rightAxis` | GetMatrix m0 | openmm2 | Fallback to the XZ right of `forward` for hand-built entries. |
| `perFrame` | (per-frame factors) | deviation | Frame-rate independence, see above. |
| `headingError` | Forward / Stop / Shortcut / Mirror heading term | fixed | Used 3D dot products; MM2 takes x and z only. |
| `forwardSpeed` | none | openmm2 | Unused helper. |
| `vectorAngle` | `Vector3::Angle` | verified | 0.9999999 and -1 cut-offs. A zero-length vector gives 0 here (MM2 divides by zero). |
| `steeringGain` | Forward / Mirror (x 1.33 x 1.428) | verified | |
| `kAiGrip`, `kSharpTurn` | MM2's AI grip factor 1.2 x 19.8, sharp-turn angle 0.7 | verified | Decoded from the data; the leaked label names were replaced by words. |
| `goesOverSidewalks` | `aiVehiclePhysics::Init` type table, EnumRoutes | verified | Only type 3 (vppanozgt) stays off sidewalks; case-sensitive compare. |
| `configureAiVehStuck` | `aiVehiclePhysics::Init`, `aiPoliceOfficer::Init` | verified | TimeThresh 0.5 (police 0.75), squared PosThresh 1, Rotation 0. MM2 leaves the PosThresh field itself; OpenMM2 also sets it to 1 so a reconfigure keeps the square (same result). |
| `AiStuck::reset` | `aiStuck::Reset` | verified | |
| `AiStuck::clearState` | InitForward / InitShortcut (state only) | fixed | New: MM2 clears aiStuck's state on entering Forward or Shortcut; OpenMM2 did not. |
| `AiStuck::pegged` | `aiStuck::Pegged` | verified | |
| `AiStuck::update` | `aiStuck::Update` | verified | 0.3 s, 0.6 m, 1 m, 1 rad/s (ctor); XZ test for stuck, 3D for moving away; rotate about Y. |
| `corners` | IsBlockingTarget / PreAvoid corners | fixed | Were symmetric half sizes about a flattened heading; now the matrix rows and the four distances. |
| `blockingDistance` | `aiVehicle::IsBlockingTarget` | verified | Corner order FL, FR, BL, BR; width/2 + 1, 0 < along < length + extra, +-0.7 rad. |
| `avoidPoints` | `aiVehicle::PreAvoid` | fixed | Line of sight now normalised in 3D and pushed along (-u.z, u.y, u.x) as MM2 does (was flattened). |
| `aiWrecked` | DriveRoute's damage test | fixed | CurrentDamage > MaxDamage (was >=). |
| `PhysicsDriver::PhysicsDriver` | `aiVehiclePhysics::Init` | fixed | Takes the bumper and side distances from the bound box (were half extents). |
| `PhysicsDriver::reset` | `aiVehiclePhysics::Reset` | verified | MM2's Reset also resets the car (vehCar::Reset); OpenMM2's callers place the car. |
| `PhysicsDriver::apply` | (input writes) | fixed | Wrote handbrake 0 everywhere; MM2 only writes the handbrake in Forward. |
| `PhysicsDriver::applyBrake` | CalcSpeed / CalcRoadSpeed braking | verified | clamp, throttle 0, angular momentum x 0.85. |
| `PhysicsDriver::driveRoute` | `aiVehiclePhysics::DriveRoute` | fixed | Wreck branch no longer clears the driver's own throttle/brake/steering (MM2 writes only the car's inputs; police read the throttle value). Repair after 5 s kept (game time, MM2 real time: deviation). The state is RegisterRoute's (second pass; the first pass stood a course-less car in Shortcut). |
| `PhysicsDriver::initForward` | `aiVehiclePhysics::InitForward` | fixed | Handbrake kept, aiStuck state cleared; only reverse goes to first gear, written without a shift (was: reverse and neutral through setDrive). |
| `PhysicsDriver::initShortcut` | `aiVehiclePhysics::InitShortcut` | fixed | New (was inline): aiStuck state cleared, same gear rule. |
| `PhysicsDriver::handleStuck` | Forward / Shortcut opening | fixed | vehStuck pegged: Backup with both momenta cleared. aiStuck stuck: throttle 1, steering 1, brakes 0 written to the car only (the driver's values untouched), handbrake kept. |
| `PhysicsDriver::forward` | `aiVehiclePhysics::Forward`, `SolveRoadTargetPoint` | fixed | The invented switch to Shortcut 10 m beyond the curb is gone (MM2 never leaves Forward for Shortcut on its own). Second pass: the car's component from aiMap::MapComponent with the road between the last and the next waypoint (or the destination's road) preferred, then SolveRoadTargetPoint (PlanRoute, the turn circles when the window's first road changes, CalcRoute) and CalcSpeed. Past-destination braking (the best route's second node at distance 9999), gains, handbrake over 30 m/s, CarFrictionHandling 2 when the player touches: verified. The undrivable branch: `undrivable` (vehCar drivable flag). |
| `PhysicsDriver::initBackup` | `aiVehiclePhysics::InitBackup` | verified | Target = first point of the best route; reverse selected once. |
| `PhysicsDriver::backup` | `aiVehiclePhysics::Backup` | fixed | No longer reselects reverse every frame; writes the car's inputs without touching the driver's values. 65 frames kept as 65.5/30 s (deviation: time-based). Snap-turn about m1 within 0.1 rad: verified. |
| `PhysicsDriver::finishedBackingUp` | `aiVehiclePhysics::FinishedBackingUp` | fixed | Throttle 0 / brakes 1 written to the car only; Shortcut when the window has no first road; momenta x 0.25. |
| `PhysicsDriver::shortcut` | `aiVehiclePhysics::Shortcut`, `SolveShortcutTargetPoint` | fixed | Second pass. Same opening as Forward (vehStuck pegged to Backup, aiStuck pegged writes), aiMap::MapComponent each frame, gain 1.33 clamped to 0.75, then CalcSpeed. Aims at the current waypoint intersection's centre (1 m up); once the car is in that intersection the window is rebuilt from the waypoints after it, the waypoint advances (laps wrap to 0) and the car returns to Forward. Past the last waypoint: the destination less the stop distance (XZ length, 3D lerp). Was: the next course leg end, staying in Shortcut. |
| `PhysicsDriver::stop` | `aiVehiclePhysics::Stop` | verified | Handbrake now kept (apply). |
| `PhysicsDriver::mirror` | `aiVehiclePhysics::Mirror` | fixed | Target speed now vehCarSim Speed (was |velocity|), heading from the target's -m2 in XZ; 0.5 throttle, 0.3 brake dead band. |
| `PhysicsDriver::calcSpeed` | `aiVehiclePhysics::CalcSpeed` | fixed | Corner speed evaluated as sqrt(tan * 10 * 1.2 * 19.8) in MM2's order (was tan * 10 then x 23.76 in float). |
| `PhysicsDriver::calcRoadSpeed` | `aiVehiclePhysics::CalcRoadSpeed`, `CheckDistance` | fixed | Destination part (70.7 m, 0.014 x d threshold, full brakes within 2.5 m): verified. Second pass, the turn part: the sharp turns of the road the car's node is on (aiPath turn records, within their set-back + the look-ahead, measured along the section axis) and the window's two junction turns (CalcTurnIntersection radii), r = room / (1 - sin((3.14 - a)/2)), v = sqrt(1.2 x 19.8 r) x factor, halved only into an alley (the road after the turn, flag 0x2). Was: merged course bends, any alley. |
| `initRoadTurns`, `calcRoadTurns`, `isSharpTurn`, `sharpTurn`, `sharpTurnVertIndex` (PathGeometry) | `aiPath::InitRoadTurns`, `CalcRoadTurns`, `IsSharpTurn`, `SharpTurn`, `SharpTurnVertIndex` | fixed | Second pass (was the course's merged bends). Turns over 0.7 rad at a vertex, a short section taken with the next; each call fits the circle for whoever asks (shared state, as in MM2); room clamped to [3, 2 x road limit - 1.5]. London 55 / SF 9 turns on the main roads, 67 / 56 on the shortcut roads. The record MM2 counts but never fills (London shortcut 578) is skipped (deviation: MM2 reads stale memory). |
| `PhysicsDriver::calcRoute`, `determineBestRoute` | `aiVehiclePhysics::CalcRoute`, `DetermineBestRoute` | verified | Node 0 at the car (its window slot and vertex: RoadVertice on road 0, then 1 and 2), the turn state, EnumRoutes from node 1. Least final angle; sidewalk routes first when preferred, then routes with a way round every obstacle, then all. |
| `PhysicsDriver::continueCheck`, `saveTarget`, `setTargetPtToDestination`, `nodeTurnAndDistance` | `ContinueCheck`, `SaveTarget`, `SetTargetPtToDestination` | fixed | Second pass on the window: extend while the route is shorter than the look-ahead and fewer than ten routes exist; on the destination's road (or the window's end) on to the destination (distance 9999); else save the route with its sidewalk and no-way-round flags. Avoid points take the slot and vertex of the obstacle's road. |
| `pathIsPosOnRoad` (PathGeometry) | `aiPath::IsPosOnRoad` | fixed | Second pass: the road the obstacle is on (MM2), its lateral at the first section whose outer-edge direction puts the point ahead, the second side's limits params[2L - 1] / params[2L + 1] for both sides. Margin RSideDistance. |
| `pathRoadVertice`, `pathDirection`, `pathIndex`, `pathBoundary`, `pathCenterDist` (PathGeometry) | `aiPath::RoadVertice`, `Direction`, `Index` (centre), the curb and edge rows, `CenterLength` | verified | Second pass. RoadVertice from either end (the vertex ahead, n when past the last). |
| `PhysicsDriver::calcRoadTarget`, `calcDestinationTarget` | `aiVehiclePhysics::CalcRoadTarget`, `CalcDestinationTarget` | fixed | Second pass, ported on the window: the window of directions between the curb points (left curb + LSide + 1 m, right curb + RSide + 1 m toward the centre; the centre line a curb on divided roads), narrowing vertex by vertex in each node road's own section frame, the forward term clamped to 1 m only for the car's own node, lateral terms under 0.01 m replaced by -1; a bend ends the walk at the inside curb point; at a window road's end with a junction turn over 0.7 rad, CalcTurnIntersection's corner; the car's place across the road (0x9738) kept on straights; when the walk runs out, the limit vertex's distance from the curb kept (the four limit rules in MM2's order). Was: the course equivalent with a chord across intersections. |
| `PhysicsDriver::isTargetBlocked`, `obstacleRoadIdx` | `aiVehiclePhysics::IsTargetBlocked`, `aiObstacle::CurrentRoadIdx` | fixed | Second pass: the traffic from the window roads' per-section obstacle lists (both sides, step 0 the intersection a road is entered from) up to the end vertex + 3, stopping at the first step with a hit; the players by the road and vertex aiVehiclePlayer::Update keeps; the other racers by their own window (CurrentRoadIdx), past waypoint 2; police never. Extra length 2 x (front + back), width left + right: verified. Was: every tracked car tested. |
| `PhysicsDriver::enumRoutes`, `calcObstacleAvoidPoints`, `enumTargets` | `EnumRoutes`, `CalcObstacleAvoidPoints`, `EnumTargets` | fixed | EnumTargets no longer accepts a point the first obstacle still blocks; the gap before a further ambient car (more than 15 m on, nearer the car, within the look-ahead, ahead along the road) is taken as MM2 does; avoid points saved 1 m above the point, the first one's angle from the car's centre. Second pass: the sharp-turn node state (a node in a turn circle continues round it, no branching there, racers ignored past a turn target). Ten-level recursion, +-1.57 road test, 40 nodes, 10 routes extended: verified. |
| `PhysicsDriver::registerRoute` | `aiVehiclePhysics::RegisterRoute` | fixed | Second pass. Maps the car (aiMap::MapComponent) and the destination (DestMapComponent: the room's intersection, else a road or shortcut of the room ending at the last waypoint), opens the window from the first waypoints (roads between them, aiMap::DetRdSegBetweenInts), the road offset at the car's vertex; Forward on a road or intersection, Shortcut on neither (a car backing up keeps backing up). Racers register once after Reset, police every frame. |
| `PhysicsDriver::planRoute`, `locateWayPtFromRoad` | `aiVehiclePhysics::PlanRoute`, `LocateWayPtFromRoad` | fixed | Second pass. The waypoint advances when the car is in its intersection (laps wrap to waypoint 1); on a road the window is re-formed: past the current waypoint when the road ends at the next, the road's direction toward the waypoint, the next two roads from the list (round to the list's start on a lap still to drive), the destination's road after the last; a waypoint one road further is reached through either end; "lost" leaves the window. `LocateWayPtFromInt` has no caller in build 3393 and is not ported. |
| `PhysicsDriver::solveRoadTargetPoint`, `solveShortcutTargetPoint` | `SolveRoadTargetPoint`, `SolveShortcutTargetPoint` | fixed | Second pass (see Forward and Shortcut). |
| `PhysicsDriver::initRoadTurns`, `calcRoadTurns`, `calcTurnIntersection` | `aiVehiclePhysics::InitRoadTurns`, `CalcRoadTurns`, `CalcTurnIntersection` | fixed | Second pass. The turn from each window road into the next (angle of the next road's first section in the end frame; over 0.7 rad a turn); its corner where the two inside curbs, moved in by the Rd-Offsets, cross; the room the car's distance across the road from the corner (+1 m beyond the set-back + 12 m), kept between the car's width and what the two roads leave; radius room / (1 - sin((3.14 - a)/2)), set-back, centre, start and end directions. Parallel curbs (a window that doubles back after a skipped waypoint, where MM2 divides by zero) take the next road's curb point (inferred guard). |
| `PhysicsDriver::calcCurrentMaxWidthAdjustment`, `calcCurrentRdOffset`, `calcNextMaxWidthAdjustment`, `calcNextRdOffset`, `laneTrafficIntrusion` | the Max-Width and Rd-Offset helpers | fixed | Second pass: how far cars waiting in the last section's lists reach past the curb (+1.25 m), on the turn's inside or outside by the driving side. |
| `PhysicsDriver::inSharpTurn`, `calcSharpTurnTarget`, `saveTurnTarget` | `InSharpTurn`, `CalcSharpTurnTarget`, `SaveTurnTarget` | fixed | Second pass. A node inside a turn circle (a road's sharp turn, or a window junction turn within radius + 15 m) aims at the arc's start, then steps round the arc (a tenth of its length, even, at least 2 steps) onto the next road. |
| `PhysicsDriver::stopRoadTraffic` | `aiVehiclePhysics::StopRoadTraffic` | fixed | Second pass: each window road's intersection ahead (its end-0 intersection when driven with the vertex order). |
| `PhysicsDriver::currentRoadIdx` | `aiVehiclePhysics::CurrentRoadIdx` | verified | Second pass: only the car's own first road counts, the vertex in its own direction. |
| `PhysicsDriver::resumeRoute` | none | openmm2 | The recovery's re-registration from the waypoint and lap reached. |
| `yawInPlace` | `Matrix34::Rotate` (aiStuck) | verified | |
| `blocked`, `placeOnCourse` | none | openmm2 | Opponent recovery helpers (see Opponent). |

## Map view (`ai/MapView`, aiMap)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `MapView::buildRooms` | `aiMap::ReadBinary`'s room table, `MapRoadToRooms` | fixed | New (second pass). Roads (type 1) by the rooms of centre vertices 1 to n - 2, then intersections (type 3) in their own room, then shortcut roads (type 2) by their centre vertices and every room their curbs cross (about a sample a metre, end rooms excepted). Maps without a PSDL (tests) treat each road and intersection as a room (openmm2). |
| `MapView::mapComponent` (4 and 5 arguments), `coreMapComponent` | `aiMap::MapComponent`, `CoreMapComponent` | fixed | New. The room's first road or intersection (4 arguments); the preferred road, else the room's intersection, else a road or shortcut the point is on or beside (IsPosOnRoad < 3); CoreMapComponent without shortcuts. Rooms from the level's room lookup (cityLevel::FindRoomId). |
| `MapView::roadBetween` | `aiMap::DetRdSegBetweenInts` | verified | New. The first road of `from`'s list that `to` lists; forward when it leaves `from` at vertex 0. |
| `MapView::trackPlayer` | `aiVehiclePlayer::Update` | fixed | New. The player's road and the vertex ahead of it there (RoadVertice), kept while it is in an intersection or off the roads. |

## Opponents (`ai/Opponent`, aiRouteRacer / aiRaceData)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `OpponentSettings::fromData` | `aiRaceData::aiRaceData` [Opponent], `aiRouteRacer::DriveRoute` → `RegisterRoute` | verified | Field order MaxThrottle, flag, look-ahead, brake threshold, traffic, props, players, racers, sidewalk preference, corner factor; defaults 1, 0, 50, 0.7, 1, 1, 1, 1, 0, 1. Repair flag = circuits (game mode 3). |
| `Opponent::Opponent` | `aiRouteRacer::Init` | verified | vehStuck 0.5 s; the driver gets the map, the racer index and the vehicle type. |
| `Opponent::~Opponent` | none | openmm2 | |
| `Opponent::create`, `routeFromPath` | `aiRouteRacer::Init`, `DriveRoute`'s RegisterRoute arguments | fixed | Second pass. Waypoints: the first intersection component of each middle row's PSDL room (was the nearest intersection; a room without one falls back to it, where MM2 quits: openmm2); destination the last row, no heading; laps = the race's in circuits, else 1. The course (progress) is laid through the same waypoints. |
| `Opponent::reset` | `aiRouteRacer::Reset` | verified | State and last state reset, aiVehiclePhysics::Reset. |
| Opponent start state | `aiRouteRacer::DriveRoute` → `RegisterRoute` (once, when the racer's state changes after Reset) | fixed | Second pass. Registered on the first frame after Reset, held or not; Shortcut when the racer is on no road or intersection; then aiIntersection::StopSources(first waypoint, 1). |
| `Opponent::finish` | `aiRouteRacer::Finished` (read by the game) | openmm2 | MM2's Finished is a game query; the driver ignores it. |
| `Opponent::onImpact` | (player contact, lvlInstance flag 0x8000) | verified | |
| `Opponent::lapsDone`, `remainingDistance`, `trackProgress`, `describe`, `setRoute` | none | openmm2 | Progress on the course (searched ahead only while driving forward, so a route doubling back at a shortcut does not pull it back); the racer as an obstacle for the other drivers; a replacement route for tools and tests. |
| `Opponent::update` | `aiRouteRacer::Update`, `DriveRoute`, `Disabled` | fixed | Held: MM2's undrivable car (mmGameSingle::DisableRacers → vehCar::SetDrivable(0, 1), now `CarSim::setDrivable`) revs in neutral with the brakes on (Forward: throttle 1, steering 0 with the front-left wheel down; vehCar::PreUpdate, now `CarSim::preUpdate`: brakes 1, neutral), into first gear on release (SetDrivable(1, 1)); was throttle 0 + handbrake. DisableRacers also turns the car's damage off until EnableRacers (second pass). No AI code uses SetDrivable's modes 2 and 3. Disabled (below y -200, checked after driving as MM2 does) stops all driving (MM2 only sets Stop without calling DriveRoute; was braking). Finishing no longer switches to Stop: the car drives on to its destination, where CalcRoadSpeed holds it. |
| `kFinishRadius` auto-finish | none | openmm2 | Marks a racer finished within 10 m of the end of its course on the last leg, or of its destination past the last waypoint of the last lap, for tools/tests; changes no driving. |
| Opponent recovery (reset after 10 s without progress or 15 m below the line) | none | deviation | Not in MM2. Kept for a car stranded where MM2's would not be (physics differences); it does not trigger while the car progresses. The route is registered again where the car is put, from the waypoint and lap it had reached (`resumeRoute`). |
| `speedLimit` hook | none | openmm2 | Scripted test cars only. |
| DeclareMover LOD (opponents beyond 200 m of a player declared differently), `Opponent::mover`, `nearestPlayer2`, `MoverDeclaration` | `aiRouteRacer::Update` | open | Second pass: the declaration is worked out every frame (within 200 m of any player, 3D: (3, 0x1b); else (2, 0x13) with the race game's switch set, (2, 0x1b) without; `kRaceGameMovers`, inferred from the setup function that sets it) but not applied on this branch: the physics manager's `Body::declare` is on integration (phys-core). |

## Police (`ai/Police`, aiPoliceOfficer / aiPoliceForce / aiMap::CalcRoute)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `PoliceForce::PoliceForce`, `reset` | `aiPoliceForce::aiPoliceForce`, `Reset` | verified | |
| `PoliceForce::findPerp`, `copsOn` | (slot search over the counted suspects) | verified | |
| `PoliceForce::find` | `aiPoliceForce::Find` | openmm2 | Query helper (MM2's Find is buggy and unused by the officers). |
| `PoliceForce::registerPerp` | `aiPoliceForce::RegisterPerp` | fixed | No longer refuses a cop already on the suspect (MM2 appends it again). |
| `PoliceForce::unregisterCop` | `aiPoliceForce::UnRegisterCop` | fixed | No longer moves the last suspect into the freed slot: MM2 empties the slot and drops the count, so a later suspect falls out of the counted ones. |
| `PoliceForce::state` | `aiPoliceForce::State` | fixed | First-cop default when distances tie; 25 m apprehend range verified. |
| `PoliceSettings::fromData` | `aiRaceData` [Police], [CopChaseDistance] | verified | Fields after the heading: unused int, behaviours (bits 1/2/4/8), opponent chance, opponent range; defaults 0, 15, 0.5, 50; chase distance default 250. |
| `PoliceCar::PoliceCar` | `aiPoliceOfficer::Init` | verified | vehStuck 0.75 s; the post route registered as Reset does. Siren audio settings are the audio area's. |
| `PoliceCar::~PoliceCar` | none | openmm2 | |
| `PoliceCar::reset`, `routeToPost` | `aiPoliceOfficer::Reset` | fixed | Registers the post route's settings (destination speed 0, 5 m short, corner factor 2) and keeps the last suspect. Second pass: Stop, then RegisterRoute to the post, which leaves the cop in Forward (Shortcut off the roads) holding itself braked short of the post (was: Stop). |
| `PoliceCar::onImpact` | (player contact flag) | verified | |
| `PoliceCar::inView` | `aiPoliceOfficer::Fov` | verified | ICS frame, +-1.57 rad. |
| `PoliceCar::detect` | `aiPoliceOfficer::DetectPerpetrator` | fixed | Records the state it starts from (the siren restarts on the next chase). As coded, an opponent is registered as the pursuer of the cop's last suspect, so the force calls the cop's pursuit "not pursued"; ported. 75 m (3D), opponents only with the player within range (XZ), frand <= chance, losers ignored until Reset: verified. MM2 draws from the global frand stream; OpenMM2 from a per-cop stream (deviation). |
| `PoliceCar::acquire` | DetectPerpetrator's hit branch | fixed | Forward, follow, the suspect's component, FollowPerpetrator. Second pass: the component from aiMap::MapComponent over the room's components (was the road geometry). |
| `PoliceCar::escape` | `aiPoliceOfficer::PerpEscapes` | verified | Out of the force, state 0, Stop. Audio (explosion, siren) is the audio area's. |
| `PoliceCar::setRouteParams` | the officers' RegisterRoute arguments | verified | MaxThrottle 1, threshold 0.7, look-ahead 75, traffic/props/racers avoided, players not. |
| `PoliceCar::routeTo` | `aiMap::CalcRoute` + `RegisterRoute` | fixed | Waypoints from MM2's CalcRoute (below), registered every frame (second pass: the driver's own window, waypoint 1 every frame, so a cop never steers round racers; was a course rebuilt when the waypoints changed). |
| `PoliceCar::follow` | `aiPoliceOfficer::FollowPerpetrator` | verified | Siren on state change; destination speed = suspect Speed + distance - 12.5, 5 m short, corner factor 2. |
| `PoliceCar::apprehend` | `aiPoliceOfficer::ApprehendPerpetrator` | verified | One frand drawn on entry, Block chosen; Push and Barricade never chosen in this build. |
| `PoliceCar::block` | `aiPoliceOfficer::Block` | fixed | Suspect's bound box (back max z, left -min x) and the cop's RSideDistance (were half sizes). The "more than 20 m behind on a road" case reads the suspect's component id where its type was meant and asks the road numbered by the type, with a three-way on-road comparison (aiPath::IsPosOnRoad, second pass: was the geometric stand-in); ported as coded. 12 m ahead, +25 m/s from behind, mirror within 3 m (XZ), back to block when the suspect gets ahead: verified. |
| `PoliceCar::update` | `aiPoliceOfficer::Update` | fixed | The pursuit state keeps 5 (not pursued), which apprehends. Follow overrides, chase distance (XZ), wreck, x 1.03 boost on the throttle value under 50 m/s, mirror on state 1 / sub-state 7, fall below -200: verified. Second pass: the suspect's component from MapComponent with its last room. Inactive sessions: openmm2. |
| room flag 4 drop-out | `aiPoliceOfficer::Update`, `cityLevel::Load` | fixed | Second pass. A cop whose car is in a room with the game's room flag 0x04 (lvlRoomInfo "water of death", not the PSDL's flag byte) has its suspect escape (PerpEscapes) and is out of action until reset. `PoliceSquad::setRoomFlags` takes the game's room flags; this branch feeds them from `city::waterRooms` (the deep-water texture rule of cityLevel::Load); integration's `CityData::levelRoomFlags` (ai-ambient-city) also has the rooms city/<map>.water lists and replaces it at the reconcile. |
| DeclareMover LOD (cops beyond 250 m not simulated), `PoliceCar::mover` | `aiPoliceOfficer::Update` | open | Second pass: worked out every frame (within 200 m of a player (2, 0x1b); within 250 m (2, 0x13) with the switch set; beyond, or with no player, not declared), not applied on this branch (as above). |
| `PoliceSquad::add`, `update`, `reset`, `anySiren` | aiMap's officer loop | openmm2 | Container. |
| `PoliceSquad::countForDensity` | `aiMap::Init` | verified | trunc(count x clamp(density, 0, 1)). |

## Course and road components (`ai/Course`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `curbOffset`, `pathCurbs`, `pathOuterEdges` | aiPath curb / edge vertex rows | fixed | Curb after the lanes, the sidewalk, trams and trains; edge after the curb. Second pass: counts the side's sidewalks (none on shortcut roads, whose lane rows are zeros). |
| `pathOnRoadLimits` | `aiPath::IsPosOnRoad` limits | verified | Right side layout params[2n-1] / params[2n+1]; lane-less sides as documented. |
| `lanesOf`, `pathLength`, `directPath` | `aiMap::DetRdSegBetweenInts` | fixed | The first road of the first intersection's list joining the two (second pass: was the shortest, which differs once shortcut roads join the lists). |
| `nearestIntersection` | waypoint lookup | openmm2 | The course's waypoints when it has none from the driver (tools); racers use `Opponent::routeFromPath`. |
| `findRoute` | none | deviation | Fills the course between waypoints no road joins (the driver, as MM2's, finds itself lost there; with the shortcut roads few retail waypoint pairs are not joined). |
| `locateOnRoads` | `aiMap::MapComponent` (geometry) | inferred | Shortcut roads optional (the course's start and finish joins leave them out). |
| `Course::build`, `fromOpponentPath`, `alongRoad` | none | openmm2 | Second pass: the course no longer drives the car; it measures progress and places stranded cars. `fromOpponentPath` takes the driver's waypoints. |
| `Course::raceDistance`, `wrap`, `segmentAt`, `pointAt`, `edges`, `onRoadLimits`, `vertexCount`, `vertexRight`, `vertexAfter`, `nextVertex`, `edgesAhead`, `lanesAt`, `locate` | none | openmm2 | Course geometry. |
| `Course::findTurns` | none | openmm2 | Course bends for tools and tests (the driver brakes for aiPath's turns). |
| `posOnRoad` | `aiPath::IsPosOnRoad` | openmm2 | The geometric stand-in from the first pass, now only for maps without a PSDL (MapView's own rooms); the drivers and Block use `pathIsPosOnRoad`. |
| `componentsAt` | `aiMap::PositionToAIMapComp` | inferred | Intersection first, else up to five roads (shortcut roads included) within twice their half width; MM2 reads the point's PSDL room components. |
| `mapComponent` | `aiMap::MapComponent` | openmm2 | The geometric stand-in from the first pass; the drivers use `MapView::mapComponent`. |
| `calcRoute` | `aiMap::CalcRoute` | fixed | New (police routing): Dijkstra over intersections, each road its centre length; start seeding from an intersection (neighbours at their road lengths) or a road (both ends at their distances along it), goal = the goal's intersection or either end of its road, ties to the most recently opened node, the car's road's far end first. Off-road starts use the nearest road's ends at straight-line distance (MM2: the rooms around the point; inferred). |

## Ambient traffic (`ai/Traffic`, aiGoalRandomDrive / aiRailSet / aiVehicleSpline / aiVehicleAmbient / aiGoal*)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `hermite`, `cubic`, `cubicSlope`, `hermitePoint`, `setCurve`, `curvePoint` | `aiRailSet::ComputeXZCurve`, `CalcXZPosition`, `SolveXZCurve` | verified | Heights lerped between curve ends (MM2 solves XZ only and fits heights in the pose solver: inferred equivalent). |
| `manhattanXZ`, `turnLength` | turn length in SolveRailType / Reset | verified | |
| `Traffic::Traffic` | aiVehicleAmbient pool construction, `aiRailSet` ctor, `aiVehicleSpline` ctor/Init, `aiGoalRandomDrive` ctor | fixed | Draw order verified (lane randomness sin(frand 6.2831) 0.5, reaction ticks 8 - trunc(frand x -17) per car; then type, paint, excess 0/8/6/4/2, accel 5 + 3f and separation 0.5 + 2.5f). Added the "vabus" lane randomness -0.5 (dead on retail data). Bumpers from the data box about CG: verified. |
| `pickType` | aiMap ambient type choice | verified | First cumulative probability above frand; race types if any, else the city's. |
| `laneOf`, `laneLength`, `xAxisAt`, `lanePoint` | aiPath lane vertex arrays, `SubSectionLength` | verified | Per-lane cumulative lengths from 0. |
| `subSectionDir`, `entryVector`, `exitVector` | `aiPath::SubSectionDir`, `IntersectionEntryVector`, `IntersectionExitVector` | verified | |
| `entryPoint` | `aiPath::IntersectionEntryPt` | verified | Last segment's length read from the lane-major cumulative table at the lane-0 indices, as coded. |
| `subSectionPoint`, `index`, `subSectionDist` | `aiPath::SubSectionPt`, `Index`, `SubSectionDist` | verified | 1e-5 tolerance. |
| `railPosition` | `aiRailSet::CalcRailPosition`, `CalcRailPosOrient` | verified | Height from aiPath::CenterPosition; with a direction, the curve's tangent there (CalcRailPosOrient, second pass). |
| `queue`, `queueOf`, `pushVehicle`, `popVehicle`, `addVehicle`, `removeVehicle`, `ahead` | `aiPath::PushAmbVehicle`, `PopAmbVehicle`, `AddAmbVehicle`, `RemoveAmbVehicle`, the per-lane links | verified | Vectors stand in for MM2's intrusive per-lane-index links. |
| `resetReactTicks` | `aiVehicleSpline::ResetReactTicks` | verified | |
| `activate`, `clearPath`, `returnToPool` | `aiPath::AddAmbPlayer` / `RemAmbPlayer`, `ClearAmbients`, `aiMap::AddAmbient` | verified | Single player. |
| `placeCar` | AdjustAmbients' placement, `aiVehicleAmbient::Reset` | verified | 50 m (3D) opponent clearance keeps the car in the pool for the next spot. |
| `adjustAmbients` | `aiMap::AdjustAmbients`, `NumCars` | verified | Float totals and carry (the asm shows float stores where the decompile shows ints); 1 + trunc(L d / 8) gaps; exception roads trunc(len x density / 8) per lane with the sin jitter. |
| `populateAll`, `poolFree`, `debug`, `activeCount`, `step(pos, vel)` | none | openmm2 | Tools. |
| `chooseNext` | `aiMap::ChooseNextLaneLink` | verified | (AmbientRoute is ai-ambient-city's.) |
| `speedLimit` | `aiGoalRandomDrive::SpeedLimit` | verified | Freeways +5 m/s per lane from the right. |
| `distanceToIntersection`, `distanceToVehicle` | `aiVehicleSpline::DistanceToIntersection`, `DistanceToVehicle` | verified | Cross-lane length reads resolve to the car's own lane length. Regain rails (MM2 errors) give the straight distance. |
| `resetRandomDrive` | `aiGoalRandomDrive::Reset` | fixed | Now ends with the pose solver as MM2 does (a car placed standing still kept an identity rotation until it moved). |
| `stopSignOkayToGo`, `removeFromStopSign` | `aiIntersection::StopSignOkayToGo`, `AddToStopSignCntl`, `RemoveFromStopSignCntl` | verified | One same-road car joins the first (the loop ends after the first match, as coded). |
| `okayToEnter`, `stopSources`, `alwaysStop` | `aiGoalRandomDrive::OkayToEnterIntersection`, `aiIntersection::StopSources`, aiPath AllwaysStop (+0x162) | fixed | Second pass: a road held by a racer (its end's rule a stop sign or a light) never enters (AllwaysStop is tested first). Always-green debug switch not modelled. |
| `upcomingAccident`, `accidentAt` | `aiGoalRandomDrive::UpcomingAccident`, `aiPedestrian::UpcomingAccident` / `Accident` | fixed | Second pass: on the obstacle map (an InAccident car in the intersection's list or the road's section lists at index 1 / n - 1, as coded). Was: by rail registration, and any car on the road for the pedestrians. |
| `roadCapacity` | `aiPath::RoadCapacity` | verified | TotLength = separation + front + back. |
| `anyVehiclesComingThisWay` | `aiGoalRandomDrive::AnyVehiclesComingThisWay` | verified | Own directions used for the other roads, as coded. |
| `avoidCollision` | `aiGoalRandomDrive::AvoidCollision` | verified | Division by zero kept finite (deviation, documented in code). |
| `solveVelocity` | `aiGoalRandomDrive::SolveVelocity` | fixed | Past a regaining car the distance is to the car beyond but AvoidCollision gets the car directly ahead, as coded. |
| `solveRailType` | `aiGoalRandomDrive::SolveRailType` | verified | |
| `solveLane` | `aiGoalRandomDrive::SolveLane`, `aiPath::NumVehiclesAfterDist` | verified | |
| `changeLanes` | `aiGoalRandomDrive::ChangeLanes` | verified | Back bumper added to the last segment, as coded. |
| `solvePose` | the pose solver (aiGoalRandomDrive / Regain) | verified | Flat roads, section frame beyond 100 m of the player, three-corner ground fit. |
| `updateRandomDrive` | `aiGoalRandomDrive::Update` | fixed | The lane change test no longer requires a lane rail (MM2 does not check). |
| `detectPlayerCollision`, `detectPlayerZoneCollision`, `playerInFront`, `ambientBlockingPlayer` | `aiVehicleSpline::DetectPlayerCollision`, `DetectPlayerZoneCollision`, `IsThePlayerInFrontOfMe`, `IsAmbientBlockingPlayer` | verified | 10 m pieces, 11 m; zone 25 m, 3 m sides. |
| `fitOffRail` | ground fit used by aiGoalAvoidPlayer | verified | |
| `updateAvoidPlayer` | `aiGoalAvoidPlayer::Reset`, `Update`, `AvoidPlayer` | verified | 0.4 m centring band and gain 1.5 (aiMap globals), 0.02 rad clamp, one frand per update (OpenMM2's own stream: deviation). The road position after moving: projection on the lane (MM2 maps the car's room component; inferred). |
| `resetRegainRail`, `roadPosInfo`, `predictIntersectionPath` | `aiGoalRegainRail::Reset`, `aiPath::DetermineRoadPosInfo`, `aiMap::PredictAmbIntersectionPath` / `PredictAmbFreewayIntersectionPath` | fixed | Second pass. MapComponent first (the car's road preferred): on a road, off the old lane lists, the road, side, lane (the side's lateral bounds) and distance under the car, a new next road, parked on a freeway against the next road's way or a side closed to ambients; in an intersection, the road out that best matches its heading (unnormalised for a road leaving from its last vertex, freeways only from a freeway or a one-lane road into one) as the next road, the distance through the turn, the regain shortened to what is left, parked as above; elsewhere parked (on a shortcut road MM2 stops the game: parked, deviation). Third regain within 1 m parks; curve (to the rail point and tangent the regain length on) and speed verified. The next lane is kept within the new next road's lanes (openmm2 guard). Was: kept its road and lane. |
| `updateRegainRail` | `aiGoalRegainRail::Update` | verified | |
| `impact` | `aiVehicleAmbient::Impact(1)` | verified | |
| `detach` | `aiVehicleAmbient::Impact(0)` | verified | Third attempt parks. |
| `setPhysicalTransform` | `aiGoalCollision::Update` | verified | |
| `release`, `accidentAt` | none | openmm2 | Game hooks. |
| `step` | `aiMap::Update` (ambient part), `aiPath::UpdateAmbients`, `aiVehicleAmbient::Update` | fixed | Lists now walked live: the car behind is taken before each update and the walk stops after as many cars as the list holds, so a car leaving its list makes the last car wait a frame (was: all cars collected first). Green-light restarts verified. |
| `updateCar` | `aiVehicleAmbient::Update` | verified | New (split out). Goal switch, tyre rotation wrap 6.28. |
| `publish` | none | openmm2 | |
| `setMap`, `updateObstacleMap`, `roadVehicles`, `intersectionVehicles`, `currentRoadIdx` | `aiVehicleSpline::UpdateObstacleMap`, `CurrentRoadIdx`, aiPath / aiIntersection vehicle lists | fixed | New (second pass). After each update a car is listed in its intersection's list or its road's section list by side (CoreMapComponent with its rail road preferred), as the drivers' IsTargetBlocked and the accident checks read them; CurrentRoadIdx answers 0 off the window, as coded. |
| `PlayerCar::at` | none | openmm2 | Tool helper. |
| Physics mover declarations while avoiding / regaining / colliding | `aiGoalAvoidPlayer::Update`, `aiGoalRegainRail::Update`, `aiGoalCollision::Update` (DeclareMover) | open | The physics manager's mover levels are not modelled (phys-core). |

## Road network (`ai/RoadNetwork`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Polyline::finalize`, `pointAt`, `project` | none | openmm2 | |
| `RoadNetwork::build` | `aiMap::ReadBinary`, `aiPath::ReverseDirection`, `InitRoadTurns`, the speed-limit loop of aiMap's init, `aiIntersection::NumSources` / `NumSinks`, aiTrafficLightSet ctor | verified | Reverse only roads whose first side has lanes; exception limit, else city limit (+ 12.5 on freeways); sources and sinks as coded. Second pass: a loop road's light is its end-1 light (aiTrafficLightSet's constructor and SetFourWay take end 1 when the path's end-1 intersection is theirs; was end 0); each road's sharp turns; shortcut roads flagged, without lanes (their lane rows are zeros; no ambient drives them). |
| `RoadNetwork::lane`, `exits`, `sidewalksAt` | none | openmm2 | Lookups. |
| shortcut roads (`city::parseShortcutBai`, `addShortcuts`) | `aiMap::ReadBinary`, `aiPath::ReadShortcut`, `aiIntersection::AddRoad`, `CreateRoadMap` | fixed | New (second pass; src/city). <city>_sup.bai's roads (70 London, 49 SF) take the next path ids, both sides closed to ambients and pedestrians, and join their end intersections' lists, which are sorted round the centre and re-indexed. MM2 first moves the centre to the room's bound-sphere centre; OpenMM2 keeps the file's (inferred: the main lists are already sorted round it). Racers and police route over them; pedestrians step over them (GetRoadToRight / GetRoadToLeft). Their waypoint pairs: 317 of London's and 328 of SF's are joined only by a shortcut. |

## Vehicle data and physical traffic (`ai/VehicleData`, `game/TrafficBodies`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `loadVehicleData` | `aiVehicleData::FileIO`, `aiVehicleManager::AddVehicleDataEntry` | verified | Same fields; wheel pivots WHL0.. and radius from WHL0's box. Defaults for absent fields are OpenMM2's (MM2 leaves them unset: inferred). MaxAng is stored but read by neither MM2's AI (Attach builds the inertia from Mass and Size only) nor OpenMM2, so va_garbagetruck's "1.#QNAN0", which datParser's atof reads as 1, changes nothing. No other AI vehicle, .aimap or race CSV number differs between a whole-token parse and atof. |
| `VehicleControl.h` (`VehicleControls`, `VehicleState`, `ControlledVehicle`) | none | openmm2 | An unused earlier interface; nothing includes it. |
| `Wheel::init`, `reset`, `update` | `vehWheelCheap::Init`, `Reset`, `Update` | verified | Preload -0.25 x weight, 3 m/s rate clamp, bottomed out under 0.1, grip 0.4 x load x WeatherFriction / RubberSpring. The visual wheel matrix is not kept (rendering). |
| `RailCar::*` | `aiVehicleInstance::GetBound`, `GetMatrix`, `GetPosition`, `GetEntity`, `AttachEntity` | verified | Radius: inferred (sphere round the box). |
| `Active::attach`, `onImpact`, `beforeIntegrate`, `afterIntegrate` | `aiVehicleActive::Attach`, `Impact`, `Update` | verified | Sleep 0.01 / 0.01, gravity -19.6. |
| `TrafficBodies::attach`, `release`, `detach`, `drop`, `beforeStep`, `afterStep` | `aiVehicleManager::Attach`, `Detach`, `Update`, `aiVehicleActive::Detach`, `PostUpdate` | verified | Upright probe +0.5 / -3 m along up, normal . up >= 0.9; y -100; room 0. |
| `boundFor` | `AddVehicleDataEntry` | verified | Box of Size at CG with lvlMaterial defaults (SetFricElas never called). |
| `toLocal`, `signOf`, `deflect`, `keepIntegrating`, `railCar`, `findRailCar`, `instancesIn`, `transformOf`, ctor/dtor | none | openmm2 | Glue. |

## Missing

| MM2 | What it does | Status |
| --- | --- | --- |
| `aiVehiclePhysics::CalcSharpTurnTarget`, `SaveTurnTarget`, `InitRoadTurns`, `CalcRoadTurns`, `CalcTurnIntersection`, `InSharpTurn` | Turn circles through tight intersection turns and road sharp turns | fixed (second pass, see Driving) |
| `aiVehiclePhysics::CalcCurrent/NextMaxWidthAdjustment`, `CalcCurrent/NextRdOffset` | Lateral offsets used by CalcRoadTarget / CalcTurnIntersection | fixed (second pass) |
| `aiVehiclePhysics::LocateWayPtFromInt`, `LocateWayPtFromRoad`, `PlanRoute`, `DestMapComponent`, `CurrentRoadIdx` | Waypoint progress, the road window shift, and the destination's component | fixed (second pass); LocateWayPtFromInt has no caller in build 3393 |
| `aiVehiclePhysics::StopRoadTraffic`, `aiMap::StopRoadTraffic`, `aiIntersection::StopSources` | Racers and police hold the controlled roads of the intersections ahead of them while the ambient traffic updates, racers their first waypoint at the start | fixed (second pass; RaceScreen calls aiMap::StopRoadTraffic round the drivers) |
| `dgPhysManager::DeclareMover` levels for AI cars | Distance-based simulation level of opponents (200 m), police (200/250 m, none beyond), attached ambient cars (aiVehicleManager::Update: (2, 0x1b)) and ambient instances off their rails (aiGoalAvoidPlayer / aiGoalRegainRail (2, 0x0a), aiGoalCollision (2, 0x08)) | open: the racers' and police levels are worked out (`mover()`); applying them and the ambient ones needs integration's `Body::declare` / Instance movers (phys-core) |
| `aiRouteRacer::Finished` | The game's finish-line test for opponents | session area |
| `aiPoliceOfficer::Push`, `Barricade` | Apprehend behaviours | never chosen in this build (ApprehendPerpetrator always sets Block) |
| `aiGoalAvoidPlayer` road re-mapping via `aiPath::DetermineRoadPosInfo` | The avoiding car's road position from its room's component | inferred (lane projection; DetermineRoadPosInfo is now ported for the regain and could serve here) |
| `aiIntersection::CreateRoadMap`'s centre | The intersections a shortcut road joins take their room's bound-sphere centre | open (the file's centre is kept; affects the Shortcut target and the pedestrians' intersections there) |
| Entity-less traffic movers (`aiGoalCollision`, `aiGoalRegainRail` declaring the rail instance, flags 8 and 10) | Ambient cars off their rails that the physics manager moves without a body | open: needs phys::World to take Instance movers without a Body (phys-core) |

## Outside this area

- `src/app/RaceScreen.cpp` (session): tracked cars from `ai::trackedCar` /
  `ai::trackedAmbient` (first pass); second pass: the player's road tracked
  (`MapView::trackPlayer`), racers described to the other drivers,
  aiMap::StopRoadTraffic round the drivers, the opponents and police built
  on `World::map()`, the police room flags, the ambient cars' audio.
- `src/city` (second pass): `<city>_sup.bai` shortcut roads
  (`parseShortcutBai`, `addShortcuts`, sides without sidewalks in the
  parser), `city::waterRooms` (to be replaced by integration's
  `CityData::levelRoomFlags`); `src/game/CityLevel.h` exposes its texture
  material table.
- `src/phys/vehicle/CarSim` (vehicle): `setDrivable` / `preUpdate`.
- `src/ai/Pedestrians`, `World` (ai-ambient-city): the accident query on the
  obstacle lists, the pedestrians stepping over shortcut roads, World owning
  the MapView.
- `tests/game/test_opponent_race.cpp`: the races registered and described as
  RaceScreen does; the chase suspect and the wall test drive registered
  routes.
- For phys-core: the AI mover declarations (`Body::declare`) at the levels
  above; `World::wheelProbe` for TrafficBodies' cheap wheels and Detach
  (integration API, not on this branch); entity-less movers.
- For session: `aiRouteRacer::Finished` is the opponents' finish test;
  opponents keep driving after it.
- For audio: police siren / explosion calls in PerpEscapes and StartSiren;
  officer +0x968a (aiVehiclePhysics +0x9686) is the driver's wrecked flag
  (`PhysicsDriver::wrecked`).
- For ai-ambient-city: the PlayerCar position the traffic reads should be
  the player's ICS position (aiVehiclePlayer::Position) for parity; the
  traffic's ground probes should use `World::wheelProbe`.
