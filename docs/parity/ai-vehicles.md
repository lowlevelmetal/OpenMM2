# Parity audit: ai-vehicles

Audited against MM2Recomp (midtown2.exe build 3393) on 2026-10-07.

Scope: `src/ai/Course.*`, `Driving.*`, `Opponent.*`, `Police.*`,
`RoadNetwork.*`, `Traffic.*`, `VehicleControl.h`, `VehicleData.*`,
`src/game/TrafficBodies.*` (all class P).

Summary: 158 rows (a row may cover several helpers of one MM2 function);
verified 76, fixed 37, open 9, inferred 11, deviation 5, openmm2 20. Eight
MM2 functions or groups with no OpenMM2 counterpart are listed under Missing.
In the opponent sweep (`OPENMM2_AI_SWEEP=1`) 517 of 517 opponents finish
(516 before this audit).

Two structural differences frame the whole area and are recorded once here
rather than on every row:

- **Courses instead of aiPath windows.** MM2's aiVehiclePhysics plans on a
  window of three aiPath road segments (the road the car is on and the next
  two of its waypoint list), indexing vertices, lane/curb vertex arrays and
  per-road sharp-turn circles; OpenMM2 plans on an `ai::Course`, the same
  roads joined into one polyline with the curbs, section frames and aiPath
  flags along it. Everything that reads MM2's window (CalcRoadTarget and its
  helpers, the bend part of CalcRoadSpeed, IsTargetBlocked's per-vertex
  obstacle lists) is an equivalent on the course, not a port; the rows say
  where the rules match and where they still differ (open).
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
| `PhysicsDriver::driveRoute` | `aiVehiclePhysics::DriveRoute` | fixed | Wreck branch no longer clears the driver's own throttle/brake/steering (MM2 writes only the car's inputs; police read the throttle value). Repair after 5 s kept (game time, MM2 real time: deviation). A car given no course goes to Shortcut, standing in for RegisterRoute. |
| `PhysicsDriver::initForward` | `aiVehiclePhysics::InitForward` | fixed | Handbrake kept, aiStuck state cleared; only reverse goes to first gear, written without a shift (was: reverse and neutral through setDrive). |
| `PhysicsDriver::initShortcut` | `aiVehiclePhysics::InitShortcut` | fixed | New (was inline): aiStuck state cleared, same gear rule. |
| `PhysicsDriver::handleStuck` | Forward / Shortcut opening | fixed | vehStuck pegged: Backup with both momenta cleared. aiStuck stuck: throttle 1, steering 1, brakes 0 written to the car only (the driver's values untouched), handbrake kept. |
| `PhysicsDriver::forward` | `aiVehiclePhysics::Forward` | fixed | The invented switch to Shortcut 10 m beyond the curb is gone (MM2 never leaves Forward for Shortcut on its own). Past-destination braking, gains, handbrake over 30 m/s, CarFrictionHandling 2 when the player touches: verified. The undrivable branch is in Opponent. |
| `PhysicsDriver::initBackup` | `aiVehiclePhysics::InitBackup` | verified | Target = first point of the best route; reverse selected once. |
| `PhysicsDriver::backup` | `aiVehiclePhysics::Backup` | fixed | No longer reselects reverse every frame; writes the car's inputs without touching the driver's values. 65 frames kept as 65.5/30 s (deviation: time-based). Snap-turn about m1 within 0.1 rad: verified. |
| `PhysicsDriver::finishedBackingUp` | `aiVehiclePhysics::FinishedBackingUp` | fixed | Throttle 0 / brakes 1 written to the car only; Shortcut without a course; momenta x 0.25. |
| `PhysicsDriver::shortcut` | `aiVehiclePhysics::Shortcut`, `SolveShortcutTargetPoint` | open | Verified: same opening as Forward (vehStuck pegged to Backup, aiStuck pegged writes), gain 1.33 clamped to 0.75, the destination less the stop distance (XZ length, 3D lerp) once no waypoint is left, 1 m up, then CalcSpeed. Differs: MM2 aims at the current waypoint's intersection centre and, once MapComponent puts the car in that intersection, advances the waypoint (re-forming its road window, wrapping laps) and returns to Forward; OpenMM2 aims at the next course leg end more than 5 m ahead and stays in Shortcut. Only the police reach Shortcut in OpenMM2, and they choose the state every frame, so a cop that reaches an intersection drives Forward on the next frame either way. A port needs the waypoint index on the driver (see LocateWayPt / PlanRoute under Missing). |
| `PhysicsDriver::stop` | `aiVehiclePhysics::Stop` | verified | Handbrake now kept (apply). |
| `PhysicsDriver::mirror` | `aiVehiclePhysics::Mirror` | fixed | Target speed now vehCarSim Speed (was |velocity|), heading from the target's -m2 in XZ; 0.5 throttle, 0.3 brake dead band. |
| `PhysicsDriver::calcSpeed` | `aiVehiclePhysics::CalcSpeed` | fixed | Corner speed evaluated as sqrt(tan * 10 * 1.2 * 19.8) in MM2's order (was tan * 10 then x 23.76 in float). |
| `PhysicsDriver::calcRoadSpeed` | `aiVehiclePhysics::CalcRoadSpeed` | verified | Destination part (70.7 m, 0.014 x d threshold, full brakes within 2.5 m). The bend part is `turnBrake`. |
| `turnBrake` (room) | `aiPath::CalcRoadTurns` | fixed | The room is clamped to [3, 2 x road limit - 1.5] as CalcRoadTurns does (was >= 0.5). |
| `turnBrake` (which bends) | CalcRoadSpeed bends, `CalcTurnIntersection`, `CheckDistance` | open | MM2 brakes separately for each road's own sharp turns (aiPath turn tables) and for the two window intersection turns (CalcTurnIntersection radii, alley halving only for those), with the distance measured along the section axis; OpenMM2 merges course bends and halves the room into any alley. Needs the per-road window (see `courseTarget`). |
| `PhysicsDriver::planRoutes` | `CalcRoute`, `DetermineBestRoute` | verified | Least final angle; sidewalk routes first when preferred, then routes with a way round every obstacle, then all. |
| `PhysicsDriver::finishRoute` | `ContinueCheck` (save) | verified | Flags: sidewalk node, no-way-round node. |
| `PhysicsDriver::roadState` | `aiPath::IsPosOnRoad` | inferred | Margin is now RSideDistance (was half width). MM2 asks the road the obstacle is on; OpenMM2 the course's road at the point. |
| `PhysicsDriver::roadTarget` | `CalcRoadTarget` call | fixed | Passes LSide/RSide (were half width twice). |
| `courseTarget` (both) | `aiVehiclePhysics::CalcRoadTarget`, `CalcDestinationTarget`, `SetTargetPtToDestination` | open | Same rule: the window of directions between the curb points (left curb + LSide + 1 m, right curb + RSide + 1 m toward the centre; the centre line a curb on divided roads), narrowing vertex by vertex; a bend ends the walk at the inside curb point; the car's place across the road (0x9738, clamped to the half width less RSide + 1) kept on straights. Differences: MM2 clamps the forward term to 1 m only for the car's own node (OpenMM2 for every node), replaces lateral terms under 0.01 m by -1, measures in the frame of the node's own road section, and at a window road's end with a turn over 0.7 rad targets CalcTurnIntersection's turn point (OpenMM2 crosses intersections by a chord). A port needs MM2's per-road windows and turn circles. |
| `PhysicsDriver::blocking` | `aiVehiclePhysics::IsTargetBlocked` | inferred | Classes and flags verified (traffic, props (not modelled), players, racers only past waypoint 2, police never). MM2 searches the window roads' per-vertex obstacle lists up to the target vertex + 3 and stops at the first vertex with a hit; OpenMM2 tests every tracked car (with a 6 m height and reach reject). Extra length 2 x (front + back), width left + right: verified. |
| `PhysicsDriver::enumRoutes` | `EnumRoutes`, `CalcObstacleAvoidPoints`, `EnumTargets`, `SaveTarget`, `ContinueCheck` | fixed | EnumTargets no longer accepts a point the first obstacle still blocks; the gap before a further ambient car (more than 15 m on, nearer the car, within the look-ahead, ahead along the road) is taken as MM2 does; avoid points saved 1 m above the point, the first one's angle from the car's centre. Ten-level recursion, +-1.57 road test, 40 nodes, 10 routes extended: verified. Open: MM2's sharp-turn node state (no branching after a sharp-turn node, racers ignored there) has no course equivalent. |
| `yawInPlace` | `Matrix34::Rotate` (aiStuck) | verified | |
| `blocked`, `placeOnCourse` | none | openmm2 | Opponent recovery helpers (see Opponent). |

## Opponents (`ai/Opponent`, aiRouteRacer / aiRaceData)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `OpponentSettings::fromData` | `aiRaceData::aiRaceData` [Opponent], `aiRouteRacer::DriveRoute` → `RegisterRoute` | verified | Field order MaxThrottle, flag, look-ahead, brake threshold, traffic, props, players, racers, sidewalk preference, corner factor; defaults 1, 0, 50, 0.7, 1, 1, 1, 1, 0, 1. Repair flag = circuits (game mode 3). |
| `Opponent::Opponent` | `aiRouteRacer::Init` | inferred | vehStuck 0.5 s verified. Waypoints become course legs (see Course). |
| `Opponent::~Opponent` | none | openmm2 | |
| `Opponent::create` | `aiRouteRacer::Init` | inferred | Waypoint = nearest intersection (MM2: the first intersection component of the row's PSDL room). |
| `Opponent::reset` | `aiRouteRacer::Reset` | verified | State and last state reset, aiVehiclePhysics::Reset. |
| Opponent start state | `aiRouteRacer::DriveRoute` → `RegisterRoute` (once, when the racer's state changes after Reset) | open | MM2 maps the racer's position (MapComponent) and starts it in Shortcut when it is on no road or intersection; OpenMM2 racers always start in Forward. Porting it needs the Shortcut waypoint advance above, or a racer starting off the roads would never return to Forward. |
| `Opponent::finish` | `aiRouteRacer::Finished` (read by the game) | openmm2 | MM2's Finished is a game query; the driver ignores it. |
| `Opponent::onImpact` | (player contact, lvlInstance flag 0x8000) | verified | |
| `Opponent::lapsDone`, `remainingDistance`, `trackProgress` | none | openmm2 | Progress on the course. |
| `Opponent::waypointsPassed` | aiVehiclePhysics waypoint index (0x967a) | inferred | From course progress; MM2 advances it in PlanRoute when the car enters the waypoint intersection, back to 1 each lap. |
| `Opponent::update` | `aiRouteRacer::Update`, `DriveRoute`, `Disabled` | fixed | Held: MM2's undrivable car (mmGameSingle::DisableRacers → vehCar::SetDrivable(0, 1)) revs in neutral with the brakes on (Forward: throttle 1, steering 0 with the front-left wheel down; vehCar::PreUpdate: brakes 1, neutral), into first gear on release (SetForward); was throttle 0 + handbrake. Disabled (below y -200, checked after driving as MM2 does) stops all driving (MM2 only sets Stop without calling DriveRoute; was braking). Finishing no longer switches to Stop: the car drives on to its destination, where CalcRoadSpeed holds it. |
| `kFinishRadius` auto-finish | none | openmm2 | Marks a racer finished within 10 m of its destination for tools/tests; changes no driving. |
| Opponent recovery (reset after 10 s without progress or 15 m below the line) | none | deviation | Not in MM2. Kept because OpenMM2's course planner is an approximation of MM2's (CalcRoadTarget open) and can strand a car MM2 would not; it does not trigger while the car progresses. |
| `speedLimit` hook | none | openmm2 | Scripted test cars only. |
| DeclareMover LOD (opponents beyond 200 m of a player declared differently) | `aiRouteRacer::Update` | open | dgPhysManager mover levels are not modelled (phys-core). |

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
| `PoliceCar::PoliceCar` | `aiPoliceOfficer::Init` | verified | vehStuck 0.75 s; Stop. Siren audio settings are the audio area's. |
| `PoliceCar::~PoliceCar` | none | openmm2 | |
| `PoliceCar::reset` | `aiPoliceOfficer::Reset` | fixed | Now registers the post route's settings (destination speed 0, 5 m short, corner factor 2) and keeps the last suspect. |
| `PoliceCar::onImpact` | (player contact flag) | verified | |
| `PoliceCar::inView` | `aiPoliceOfficer::Fov` | verified | ICS frame, +-1.57 rad. |
| `PoliceCar::detect` | `aiPoliceOfficer::DetectPerpetrator` | fixed | Records the state it starts from (the siren restarts on the next chase). As coded, an opponent is registered as the pursuer of the cop's last suspect, so the force calls the cop's pursuit "not pursued"; ported. 75 m (3D), opponents only with the player within range (XZ), frand <= chance, losers ignored until Reset: verified. MM2 draws from the global frand stream; OpenMM2 from a per-cop stream (deviation). |
| `PoliceCar::acquire` | DetectPerpetrator's hit branch | verified | Forward, follow, component of the suspect, FollowPerpetrator. |
| `PoliceCar::escape` | `aiPoliceOfficer::PerpEscapes` | verified | Out of the force, state 0, Stop. Audio (explosion, siren) is the audio area's. |
| `PoliceCar::setRouteParams` | the officers' RegisterRoute arguments | verified | MaxThrottle 1, threshold 0.7, look-ahead 75, traffic/props/racers avoided, players not. |
| `PoliceCar::routeTo` | `aiMap::CalcRoute` + `RegisterRoute` | fixed | Waypoints now from MM2's CalcRoute (below), recomputed every frame, the course rebuilt when they change; RegisterRoute's Forward / Shortcut choice (no road or intersection under the cop) made every frame. Was: nearest intersection to the goal, course every second. |
| `PoliceCar::context` | (waypoint count restarts with every RegisterRoute) | verified | A cop never passes waypoint 1, so never steers round racers. |
| `PoliceCar::follow` | `aiPoliceOfficer::FollowPerpetrator` | verified | Siren on state change; destination speed = suspect Speed + distance - 12.5, 5 m short, corner factor 2. |
| `PoliceCar::apprehend` | `aiPoliceOfficer::ApprehendPerpetrator` | verified | One frand drawn on entry, Block chosen; Push and Barricade never chosen in this build. |
| `PoliceCar::block` | `aiPoliceOfficer::Block` | fixed | Suspect's bound box (back max z, left -min x) and the cop's RSideDistance (were half sizes). The "more than 20 m behind on a road" case reads the suspect's component id where its type was meant and asks the road numbered by the type, with a three-way on-road comparison; ported as coded. 12 m ahead, +25 m/s from behind, mirror within 3 m (XZ), back to block when the suspect gets ahead: verified. |
| `PoliceCar::update` | `aiPoliceOfficer::Update` | fixed | The pursuit state keeps 5 (not pursued), which apprehends. Follow overrides, chase distance (XZ), wreck, x 1.03 boost on the throttle value under 50 m/s, mirror on state 1 / sub-state 7, fall below -200: verified. Inactive sessions: openmm2. |
| room flag 4 drop-out | `aiPoliceOfficer::Update` | open | PSDL room flags are not available to the AI here. |
| DeclareMover LOD (cops beyond 250 m not simulated) | `aiPoliceOfficer::Update` | open | Mover levels not modelled (phys-core). |
| `PoliceSquad::add`, `update`, `reset`, `anySiren` | aiMap's officer loop | openmm2 | Container. |
| `PoliceSquad::countForDensity` | `aiMap::Init` | verified | trunc(count x clamp(density, 0, 1)). |

## Course and road components (`ai/Course`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `curbOffset`, `pathCurbs`, `pathOuterEdges` | aiPath curb / edge vertex rows | verified | Curb after lanes, trams, trains and sidewalk centre; edge after the curb. |
| `pathOnRoadLimits` | `aiPath::IsPosOnRoad` limits | verified | Right side layout params[2n-1] / params[2n+1]; lane-less sides as documented. |
| `lanesOf`, `pathLength`, `directPath` | `aiMap::DetRdSegBetweenInts` | verified | First road joining the two intersections (OpenMM2 the shortest of several: same on retail data, inferred). |
| `nearestIntersection` | waypoint lookup | inferred | MM2: the intersection component of the point's PSDL room. |
| `findRoute` | none | deviation | Fills gaps between non-adjacent .opp waypoints (MM2 has none on retail data). |
| `locateOnRoads` | `aiMap::MapComponent` (geometry) | inferred | |
| `Course::build`, `fromOpponentPath`, `alongRoad` | `RegisterRoute`'s road window | deviation | The course is OpenMM2's structural substitute for MM2's three-road window (see top). |
| `Course::raceDistance`, `wrap`, `segmentAt`, `pointAt`, `edges`, `onRoadLimits`, `vertexCount`, `vertexRight`, `vertexAfter`, `nextVertex`, `edgesAhead`, `lanesAt`, `locate` | none | openmm2 | Course geometry. |
| `Course::findTurns` | `aiPath` sharp turns, `CalcTurnIntersection` | deviation | Merged course bends stand in for MM2's per-road turn tables and intersection turns (see `turnBrake`). |
| `posOnRoad` | `aiPath::IsPosOnRoad` | inferred | New. Lateral from the road's centre line (MM2: at the first vertex ahead of the point). |
| `componentsAt` | `aiMap::PositionToAIMapComp` | inferred | New. Intersection first, else up to five roads within twice their half width; MM2 reads the point's PSDL room components. |
| `mapComponent` | `aiMap::MapComponent` | inferred | New. Intersection first, else a road with IsPosOnRoad < 3; none keeps the old id. |
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
| `railPosition` | `aiRailSet::CalcRailPosition` | verified | Height from aiPath::CenterPosition. |
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
| `okayToEnter` | `aiGoalRandomDrive::OkayToEnterIntersection` | verified | Always-green/red debug switches not modelled. |
| `upcomingAccident` | `aiGoalRandomDrive::UpcomingAccident` | inferred | MM2 scans the obstacle lists of the intersection and the next road for InAccident; OpenMM2 by rail registration. |
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
| `resetRegainRail` | `aiGoalRegainRail::Reset` | open | Third regain within 1 m parks; curve and speed verified. MM2 first maps the car onto the road or intersection under it (switching road, predicting the intersection path, parking on closed or wrong-way freeway sides); OpenMM2 keeps its road and lane. |
| `updateRegainRail` | `aiGoalRegainRail::Update` | verified | |
| `impact` | `aiVehicleAmbient::Impact(1)` | verified | |
| `detach` | `aiVehicleAmbient::Impact(0)` | verified | Third attempt parks. |
| `setPhysicalTransform` | `aiGoalCollision::Update` | verified | |
| `release`, `accidentAt` | none | openmm2 | Game hooks. |
| `step` | `aiMap::Update` (ambient part), `aiPath::UpdateAmbients`, `aiVehicleAmbient::Update` | fixed | Lists now walked live: the car behind is taken before each update and the walk stops after as many cars as the list holds, so a car leaving its list makes the last car wait a frame (was: all cars collected first). Green-light restarts verified. |
| `updateCar` | `aiVehicleAmbient::Update` | verified | New (split out). Goal switch, tyre rotation wrap 6.28. |
| `publish` | none | openmm2 | |
| `PlayerCar::at` | none | openmm2 | Tool helper. |
| Physics mover declarations while avoiding / regaining / colliding | `aiGoalAvoidPlayer::Update`, `aiGoalRegainRail::Update`, `aiGoalCollision::Update` (DeclareMover) | open | The physics manager's mover levels are not modelled (phys-core). |

## Road network (`ai/RoadNetwork`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Polyline::finalize`, `pointAt`, `project` | none | openmm2 | |
| `RoadNetwork::build` | `aiMap::ReadBinary`, `aiPath::ReverseDirection`, the speed-limit loop of aiMap's init, `aiIntersection::NumSources` / `NumSinks`, aiTrafficLightSet ctor | verified | Reverse only roads whose first side has lanes; exception limit, else city limit (+ 12.5 on freeways); sources and sinks as coded. |
| `RoadNetwork::lane`, `exits`, `sidewalksAt` | none | openmm2 | Lookups. |

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
| `aiVehiclePhysics::CalcSharpTurnTarget`, `SaveTurnTarget`, `InitRoadTurns`, `CalcRoadTurns`, `CalcTurnIntersection`, `InSharpTurn` | Turn circles through tight intersection turns and road sharp turns: route points along the circle, the turn radius/setback for braking, and the "in a sharp turn" node state that stops obstacle branching | open: needs MM2's per-road windows on the course (a re-architecture of the planner) |
| `aiVehiclePhysics::CalcCurrent/NextMaxWidthAdjustment`, `CalcCurrent/NextRdOffset` | Lateral offsets used by CalcRoadTarget / CalcTurnIntersection to keep the car's side of the road through turns | open (with the above) |
| `aiVehiclePhysics::LocateWayPtFromInt`, `LocateWayPtFromRoad`, `PlanRoute`, `DestMapComponent`, `CurrentRoadIdx` | Waypoint progress, the road window shift, and the destination's component | inferred in Forward (course progress stands in); open for Shortcut, whose waypoint advance needs the index (see `PhysicsDriver::shortcut`) |
| `aiVehiclePhysics::StopRoadTraffic`, `aiMap::StopRoadTraffic`, `aiIntersection::StopSources` | Racers stop ambient traffic sources at the race's first intersection and around themselves | open: ambient traffic does not yet look at racers (affects starts in traffic) |
| `dgPhysManager::DeclareMover` levels for AI cars | Distance-based simulation level of opponents (200 m), police (200/250 m, none beyond) and ambient cars off their rails | open (phys-core) |
| `aiRouteRacer::Finished` | The game's finish-line test for opponents | session area |
| `aiPoliceOfficer::Push`, `Barricade` | Apprehend behaviours | never chosen in this build (ApprehendPerpetrator always sets Block) |
| `aiGoalAvoidPlayer` road re-mapping via `aiMap::DetermineRoadPosInfo` | The avoiding car's road position from its room's component | inferred (lane projection) |

## Outside this area

- `src/app/RaceScreen.cpp` (session): tracked cars now come from
  `ai::trackedCar` / `ai::trackedAmbient` (the player flagged), a three-line
  change.
- `tests/game/test_opponent_race.cpp`: same helpers; the circuit test checks
  that finished racers come to rest instead of a Stop mode; the traffic race
  allows 6 s beyond the sidewalk after a collision.
- For phys-core: AI mover declaration levels (above).
- For session: `aiRouteRacer::Finished` is the opponents' finish test; opponents
  keep driving after it.
- For audio: police siren / explosion calls in PerpEscapes and StartSiren, the
  revving of held racers now reaches the engine (throttle 1 in neutral).
- For ai-ambient-city: racers stopping traffic (StopRoadTraffic,
  StopSources); the PlayerCar position the traffic reads should be the
  player's ICS position (aiVehiclePlayer::Position) for parity.
