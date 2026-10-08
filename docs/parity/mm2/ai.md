# MM2 -> OpenMM2: ai

Audited from MM2Recomp (midtown2.exe build 3393) on 2026-10-08.

Summary: 507 reachable functions in 35 classes; ported 408 (of which newly
ported 15, and 9 more fixed in this pass), replaced 6, not needed 92, open 1.

Scope: every ai* class except aiSubway and aiCableCar (world objects), and
lvlAiMap / lvlAiRoad. The first audit verified OpenMM2's side
(docs/parity/ai-vehicles.md, docs/parity/ai-ambient-city.md); this pass
started from MM2's functions and their callers, and looked at how each kind
of AI object is spawned, recycled and reset. "Replaced" here means another
area owns the code (world objects); "not needed" covers constructors,
destructors, exception-unwind tables, debug drawing and replay logging,
empty functions, and code the game cannot reach (the reason is given).

Fixed or newly ported in this pass:

- **Restart resets the AI** (aiMap::Reset from mmGame::Init and
  mmGame::Reset): random seed, roads, intersections, light sets, the ambient
  and pedestrian pools, physical traffic cars, players, police force. Was
  missing: a restart left everything as it was.
- **Traffic indicators and tail lights** (aiVehicleInstance::DrawGlow,
  aiVehicleManager::Update's clock): SLIGHT0 / SLIGHT1 blink once a second
  with each car's own phase; the tail lights follow MM2's rule.
- **Ambient cars off their rails collide** (DeclareMover of the bodiless
  instance from aiGoalAvoidPlayer / aiGoalRegainRail (2, 0x0a) and
  aiGoalCollision for a wreck (2, 0x08)): `phys::World::declareInstance`.
- **The avoiding car's road distance** (aiGoalAvoidPlayer::Update,
  aiMap::MapComponentType, aiMap::DetermineRoadPosInfo, aiPath::RoadDistance).
- **Racers finish at the race's line** (aiRouteRacer::Finished,
  aiMap::SetWaypoints).
- **Placement resets** (aiVehicleAmbient::Reset's goal Inits: a recycled car
  no longer keeps an old regain count); set-up draws in MM2's order; the
  indicators no longer cleared where MM2 leaves them.
- **Intersection prop lists** are empty in play, as aiMap::Reset leaves them.
- **Racers and police steer round props** (aiVehiclePhysics::IsTargetBlocked's
  prop lists, aiBanger::IsBlockingTarget / PreAvoid / CurrentRoadIdx).
- **-pedpool** sets the pedestrian pool (aiCityData).
- **Players' tracked roads** forgotten at a reset (aiVehiclePlayer::Reset).
- OpenMM2's stranded-racer recovery places the car as MM2 places racers
  (vehCarSim::SetResetPos, 0.9 m above the road) and stops once the racer
  has arrived.

Opponent sweep (`OPENMM2_AI_SWEEP=1`): 516 of 517 racers complete their
route (the miss, sf race11 p, as before: landing-damage wrecks); 508 cross
the race's finish line as aiRouteRacer::Finished counts it (london race6 a
and p: 3 each, sf race10 a and p: 1 each stop at a destination short of the
line; the ninth is the sf race11 p wreck).

## aiMap

The AI's root (the global AIMAP, an asNode): it loads the road network
(ReadBinary), creates every AI object of a race (Init), puts them back at a
restart (Reset) and runs them each frame (Update). OpenMM2 splits it into
`ai::World` (roads, rooms, lights, traffic, pedestrians), `ai::MapView` (the
drivers' view of rooms and components) and RaceScreen (racers, police).

**Spawns (aiMap::Init, mmGame::Init).** In order: the vehicle manager and the
players; the city's and the race's AI maps (aiCityData, aiRaceData);
ReadBinary and the routing table; ReverseDirection of every road on a
drive-on-the-left city; the speed limits (the race's exception, else the
city's, + 12.5 on freeways); the racers, trunc of min(the race's opponent
count, the state's opponent count); the hookmen (none in retail races); the
CTF racers (the state's count, always 0); the ambient pool (the state's
MaxAmbientVehicles cars, none at traffic density 0, the density clamped and
x 0.2), each car's type from the race's [Ambient Types] (else the city's) by
frand against the cumulative probabilities (every retail list ends at 1.0, so
a car is never left without a type); the pedestrians (none with the SHOW
PEDESTRIANS option off, in circuits or in software rendering; else
trunc([Ped Pool] x density), -pedpool overriding the pool), type by frand,
variant in aiPedestrian::Init; cable cars and subways (world objects); the
police force and trunc(posts x clamp(cop density)) officers. mmGame::Init
then calls aiMap::Reset at once.

**Reset (aiMap::Reset; mmGame::Init, and mmGame::Reset at a restart, before
the player's own reset).** ResetRandomSeed; the children (aiVehicleManager:
every physical traffic car detached; the light sets); the police force; every
intersection (stop-sign queues, vehicle list and the prop list, so the props
AddBangersToObsMap listed at load are gone for good) and road (lane, vehicle
and pedestrian lists, flags); the players (their room from the reset
position); the racers; every pedestrian and ambient car back in its pool in
index order; then, per player, AdjustAmbients and AdjustPedestrians from room
0 to the room of the player's reset position; the officers; cable cars and
subways. OpenMM2 had no reset at all: a restart left traffic, pedestrians,
lights and physical traffic cars as they were. Now `World::reset`,
`Traffic::reset`, `Pedestrians::reset`, `TrafficBodies::reset`,
`MapView::resetPlayers` and `PoliceSquad::reset` do this at the race start and
at Restart.

**Recycling (aiMap::Update).** Each frame, for each player, the room
(FindRoomId from the last) changing to another non-zero room runs
AdjustAmbients and AdjustPedestrians (roads leaving the player's lists give
their cars and pedestrians back to the pools; new roads are populated); then
the populated roads' ambient cars, the pedestrians, StopRoadTraffic, the
racers, the police, and the children (light sets, the vehicle manager).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `aiMap`, `~aiMap`, `scalar_deleting_destructor`, `Clean` | not needed |  | Construction and freeing; C++ owners (World, RaceScreen) do it. |
| `Init` | ported | `World::create`, `RaceScreen::loadAi`, `spawnOpponents`, `spawnPolice` | Order and counts as above, checked against the asm (the decompile is cut into pieces). The pedestrian rules (option, circuits) are in RaceScreen; -pedpool is now wired (`CommandLine::pedPool`). |
| `ReadBinary`, `MapRoadToRooms` | ported | `city::parseBai`, `MapView::buildRooms` | Verified by the first audit. |
| `InitRouting`, `AddRoutingNode`, `RemoveRoutingNode`, `FindInt` | ported | `ai::calcRoute` (Course.cpp) | CalcRoute's node table and open list; OpenMM2's Dijkstra keeps its own. |
| `CalcRoute` | ported | `ai::calcRoute` | First audit. Its branch for a start inside an intersection with its last argument false (the route then starts with the road aiMap::PredictIntersectionPath predicts) is not ported: the police pass a register left from their caller there, inferred non-zero. |
| `PredictIntersectionPath` | open |  | The road a player in an intersection is taking (by heading, reversed when the car reverses), for aiVehiclePlayer::Reset and that CalcRoute branch. OpenMM2 keeps no road until the player is on one (`MapView::resetPlayers`). Needs the player's reverse flag in TrackedCar; small. |
| `PredictAmbIntersectionPath`, `PredictAmbFreewayIntersectionPath` | ported | `Traffic::predictIntersectionPath` | Both, by its freeway flag (aiGoalRegainRail::Reset). |
| `Reset` | ported (new) | `World::reset`, `Traffic::reset`, `Pedestrians::reset`, `TrafficBodies::reset`, RaceScreen | See above; was missing. |
| `Update` | ported | `World::step`, `RaceScreen::updateAmbient` / `updateAiDrivers` | Order verified; the AI steps at a fixed 30 Hz (deviation recorded by the first audit). |
| `UpdatePaused`, `Cull` | not needed |  | Empty functions. |
| `StopRoadTraffic` | ported | `RaceScreen::stopRoadTraffic` | First audit. |
| `AddPlayer` | ported | `World::step` (first step) | Attaches the player's car and populates its room; aiMap::Reset redoes the population right after. |
| `Player`, `Opponent`, `Police`, `CableCar`, `Path`, `Intersection` | ported | `MapView::path`, `intersection`, RaceScreen lists | Accessors. |
| `AdjustAmbients`, `NumCars`, `AddAmbient`, `RemoveAmbient` | ported | `Traffic::adjustAmbients`, `placeCar`, `returnToPool` | First audit (AdjustAmbients, NumCars, AddAmbient). RemoveAmbient takes a car from the pool list: `placeCar`. |
| `AdjustPedestrians`, `ClearPeds`, `AddPedestrian`, `RemovePedestrian`, `FindPedAppRoad` | ported | `Pedestrians::adjust`, `clearPeds`, `poolAdd`, `poolRemove`, `m_pathActive` | First audit. |
| `FindAmbAppRoad` | ported | `Traffic::solveRailType` (`m_pathActive`) | Whether a road is populated (SolveRailType turns a car round at an unpopulated road; aiCableCar::Update asks too). |
| `MapComponent (both)`, `CoreMapComponent`, `PositionToAIMapComp`, `DetRdSegBetweenInts` | ported | `MapView::mapComponent`, `coreMapComponent`, `roadBetween`, `componentsAt` | First audit. |
| `MapComponentType` | ported (new) | `MapView::mapComponentType` | For aiGoalAvoidPlayer::Update (below). |
| `DetermineRoadPosInfo` | ported (new) | `Traffic::determineRoadPosInfo` | Was cited for aiPath::DetermineRoadPosInfo only; aiMap's version (RoadDistance on lane 0, the lane whose bounds of the car's own road hold the offset, RoadDistance again) is new, for the avoiding car. |
| `ChooseNextLaneLink`, `ChooseNextRandomLink`, `ChooseNextLeftStraightLink`, `ChooseNextRightStraightLink`, `ChooseNextRightLink`, `ChooseNextStraightLink`, `ChooseStraightLinkAt4Way`, `ChooseNextFreewayLink`, `ChooseNextRightStraightFreewayLink` | ported | `ai/AmbientRoute` | First audit (ChooseNextRightStraightLink is `chooseTurnOrStraight` with +2; its name is now in the code). |
| `SetWaypoints` | ported (new) | `Opponent::setFinishLine` | mmSingleRace / mmSingleCircuit::InitGameObjects give it the race's checkpoints (flag 0: the last, the circuit's 1: the first); aiRouteRacer::Finished tests the line. |

## _global (SEH funclets)

Exception-unwind stubs of aiMap's constructor, destructor, Init and ReadBinary.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `??0aiMap@@QAE@XZ_SEH`, `??1aiMap@@UAE@XZ_SEH`, `?Init@aiMap@@QAEXPAD00ABUdgStatePack@@HPAPAVvehCar@@_N@Z_SEH`, `?ReadBinary@aiMap@@QAEXPAD0@Z_SEH` | not needed |  | Structured exception handling tables (C++ unwinding). |

## lvlAiMap / lvlAiRoad

The PSDL's own road table (in the .psdl, not the .bai): cityLevel::Load
reads it (SetRoad per road, GetNumRoads, GetRoom, Delete after the props are
placed) and lvlSDL::IsoLerp uses its sidewalk vertices to place the street
props. Ported with the PSDL parser and the prop placement (props-fx).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `LoadBinary`, `LoadCurrent`, `SetRoad (both)`, `GetRoom`, `GetRoomChop`, `GetNumRoads`, `GetNumRooms`, `GetNumVertexs`, `Delete` | ported | `city::parsePsdl`, `game/bangers/PropPlacement.cpp` | The road records and their rooms; the room chops are the road attribute's per-room sections. |
| `GetSidewalkVertex`, `GetSidewalkVertexMulti`, `GetSidewalkVertexSingle`, `GetVertexSingleCenter`, `IsPedBlocked` | ported | `PropPlacement.cpp` | Street prop placement (props-fx record). |
| `lvlAiRoad::LoadBinary` | ported | `city::parsePsdl` |  |

## aiCityData / aiRaceData

The city's and the race's .aimap files.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `aiCityData::aiCityData` | ported | `city::parseAiMapConfig`, `World::loadCityAiConfig` | First audit; -pedpool (datArgParser after [Ped Pool]) now wired. |
| `aiRaceData::aiRaceData` | ported | `city::parseAiMapConfig`, `OpponentSettings::fromData`, `PoliceSettings::fromData` | First audit. |
| `aiCityData::~aiCityData`, `aiCityData::scalar_deleting_destructor`, `aiRaceData::~aiRaceData`, `aiRaceData::scalar_deleting_destructor` | not needed |  | Destructors. |

## aiPath

A road of the .bai: lanes, sidewalks, the obstacle lists, the population links.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `ReadBinary`, `ReadShortcut`, `ReverseDirection`, `InitRoadTurns`, `CalcRoadTurns`, `IsSharpTurn`, `SharpTurnVertIndex`, `SharpTurnAngle` | ported | `city::parseBai`, `RoadNetwork::build`, `PathGeometry` | First audit. |
| `SharpTurnCenter`, `SharpTurnDir`, `SharpTurnEndDir`, `SharpTurnIntersection`, `SharpTurnRadius`, `SharpTurnSetback`, `SharpTurnStartDir` | ported | `PathGeometry` (`sharpTurn`) | Accessors of the turn records, by direction. |
| `Reset` | ported (new) | `Traffic::reset`, `Pedestrians::reset` | Lane, vehicle and pedestrian lists, AllwaysStop / AllwaysGo, the player masks; not the prop lists. |
| `ResetObstacles`, `AddVehicle`, `RemoveVehicle` | ported | `Traffic::clearPath`, `updateObstacleMap` | The per-section vehicle lists (+0x90 / +0xf4). |
| `AddBanger`, `AddBangersToObsMap` | ported | `Pedestrians::setObstacles` | The per-section prop lists (+0x94 / +0xf8), read by the pedestrians and the drivers. |
| `AddAmbPlayer`, `RemAmbPlayer`, `ClearAmbients`, `AddAmbVehicle`, `PushAmbVehicle`, `PopAmbVehicle`, `RemoveAmbVehicle`, `UpdateAmbients`, `RoadCapacity`, `NumVehiclesAfterDist`, `ResetVehicleReactTicks`, `AllwaysStop` | ported | `Traffic` (`activate`, `clearPath`, queues, `step`, `roadCapacity`, `solveLane`, `resetReactTicks`, `alwaysStop`) | First audit. |
| `AddPedPlayer`, `RemPedPlayer`, `AddPedestrian`, `RemovePedestrian (both)`, `UpdatePedestrians` | ported | `Pedestrians` (`adjust`, `pathAdd`, `pathRemove`, `updateRoad`) | First audit. |
| `SidewalkVertice`, `SidewalkSubSectionLength`, `GetHeading`, `Index (both)`, `SubSectionDir`, `SubSectionPt`, `IntersectionEntryPt`, `IntersectionEntryVector`, `IntersectionExitVector`, `Direction`, `RoadVertice (both)`, `CenterLength`, `CenterPosition`, `IsPosOnRoad`, `DetermineRoadPosInfo` | ported | `Pedestrians`, `Traffic`, `PathGeometry` | First audit. |
| `SubSectionDist`, `SubSectionLength` | ported | `Traffic::subSectionDist`, `laneLength` | Lane lengths between vertices. |
| `RoadDistance` | ported (new) | `Traffic::pathRoadDistance` | The first vertex the position lies before, within the half width + 5 m; for aiMap::DetermineRoadPosInfo. |
| `CenterDist` | ported | `ai::calcRoute` | A position's distance along the centre line (CalcRoute's road start). |
| `CenterIndex`, `HasCableCarLine`, `HasSubwayLine` | replaced | world objects | Used only by aiSubway / aiCableCar and aiIntersection::IsCableCarStart / IsSubwayStart (world-objects area). |
| `aiPath`, `~aiPath` | not needed |  | Construction. |

## aiIntersection

A junction: its roads in order, the stop-sign queues, the obstacle lists and its light set.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `ReadBinary`, `AddRoad`, `CreateRoadMap`, `Path`, `NumSources`, `NumAvailSinks`, `StopSignOkayToGo`, `StopSources`, `AddBanger`, `AddBangersToObsMap` | ported | `city::parseBai`, `RoadNetwork`, `Traffic`, `Pedestrians` | First audit. |
| `NumSinks` | ported | `RoadNetwork::build` | First audit (as coded). |
| `AddToStopSignCntl`, `RemoveFromStopSignCntl`, `RemoveTotalFromStopSignCntl` | ported | `Traffic::okayToEnter`, `removeFromStopSign` | Waiting queue appended at its tail (the decompile shows a stray head write the asm does not have), the allowed list; RemoveTotal (both lists) is what a recycled car gets in `returnToPool`. |
| `AddVehicle`, `RemoveVehicle` | ported | `Traffic::updateObstacleMap` | The vehicle list (+0x24). |
| `Reset` | ported (new) | `Traffic::reset`, `Pedestrians::reset` | Stop-sign queues, vehicle list, the prop list (+0x28) and the light set. |
| `SetFourWay` | ported | `RoadNetwork::build`, `TrafficLights::build` | Clears the prop list and sets the light set four-way (none in retail). |
| `Road` | ported | `RoadNetwork` lookups | Accessor. |
| `IsCableCarStart`, `IsSubwayStart`, `IsSubwayEnd` | replaced | world objects | Cable car and subway placement (world-objects area). |
| `aiIntersection`, `~aiIntersection` | not needed |  | Construction (the light set is an aiMap child). |

## aiTrafficLightSet / aiTrafficLightInstance

The light sets and their poles (first audit: ai-ambient-city).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `aiTrafficLightSet::aiTrafficLightSet`, `Reset`, `SetFourWay`, `Update` | ported | `TrafficLights` | First audit. |
| `aiTrafficLightSet::~aiTrafficLightSet`, `scalar_deleting_destructor` | not needed |  |  |
| `aiTrafficLightInstance::Init`, `Draw`, `DrawGlow` | ported | `World::create`, `AiRenderer::drawSignal` | First audit. |
| `aiTrafficLightInstance::SizeOf` | not needed |  | Instance pool bookkeeping. |

## aiVehicleAmbient / aiVehicleSpline / aiVehicle / aiObstacle

An ambient car: aiVehicleAmbient (its goals) over aiVehicleSpline (the rail,
its instance and audio) over aiVehicle / aiObstacle. One `Traffic::Car` each.

**Spawn.** aiMap::Init builds the pool (constructors of the whole array: lane
randomness, reaction ticks), then per car its type and aiVehicleAmbient::Init
(the instance: an arbitrary 15-bit number irand(int) makes from its own
address, a paint draw; Init's own paint draw, kept; the four goals, the
random-drive goal drawing acceleration and separation). AdjustAmbients places
a car from the pool with aiVehicleAmbient::Reset (aiVehicleSpline::Reset and
each goal's Init: one regain attempt, start point at the origin, base 0,
length 30; tyre rotation 0; the indicators untouched).

**Behaviour.** Update runs the current goal on alternate frames per car (the
update parity), then aiVehicleSpline::Update (tyres, audio position, obstacle
map) and moves the instance between rooms.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `aiVehicleAmbient::Init` | ported | `Traffic::Traffic` | Draw order fixed: the instance ctor draws a paint job that Init draws again (the second is kept). Its 15-bit number is now a stateless hash of the car index (MM2: of its address), not a draw. |
| `aiVehicleAmbient::Reset` | ported | `Traffic::placeCar` | Fixed: the goals' Init values (regain attempts 1, regain start, base and length; the avoid state) and the tyre rotation are reset when a car is placed (a recycled car that had parked after three regains parked again at its first knock); the indicators are left alone as in MM2 (OpenMM2 cleared them). |
| `aiVehicleAmbient::Update`, `Impact` | ported | `Traffic::updateCar`, `impact`, `detach` | First audit. |
| `aiVehicleAmbient::aiVehicleAmbient`, `~aiVehicleAmbient` | ported | `Traffic::Car` defaults |  |
| `aiVehicleAmbient::Type`, `DrawId`, `ReplayDebug` | not needed |  | Type 0 (unused by OpenMM2), debug drawing, replay logging. |
| `aiVehicleSpline::aiVehicleSpline`, `Init`, `Update`, `ResetReactTicks`, `CurrentRoadIdx`, `UpdateObstacleMap`, `DetectPlayerCollision`, `DetectPlayerZoneCollision`, `IsThePlayerInFrontOfMe`, `IsAmbientBlockingPlayer`, `DistanceToVehicle`, `DistanceToIntersection` | ported | `Traffic` | First audit. |
| `aiVehicleSpline::Reset` | ported | `Traffic::placeCar` | With aiVehicleAmbient::Reset (above). |
| `aiVehicleSpline::Position`, `GetMatrix`, `Speed`, `FrontBumperDistance`, `BackBumperDistance`, `LSideDistance`, `RSideDistance`, `CurrentLane`, `CurrentRoadId`, `CurrentRdVert`, `TotLength`, `InAccident` | ported | `ai::trackedAmbient`, `Traffic` (`roadCapacity`, `accidentAt`) | Accessors. |
| `aiVehicleSpline::Impact`, `PlayHorn`, `StopVoice`, `GetAudImpactPtr` | not needed |  | Empty or null in build 3393 (aiVehicleAmbient overrides Impact; the audio has its own objects). |
| `aiVehicleSpline::~aiVehicleSpline`, `Type`, `DrawId`, `ReplayDebug` | not needed |  |  |
| `aiVehicle::IsBlockingTarget`, `PreAvoid` | ported | `ai::blockingDistance`, `avoidPoints` | First audit. |
| `aiVehicle::Init`, `Reset`, `Update` | ported | `Traffic` (car ids, the update parity) | The id, and the alternate-frame update flag. |
| `aiVehicle::aiVehicle`, `~aiVehicle`, `ReplayDebug` | not needed |  |  |
| `aiObstacle::BreakThreshold`, `Drivable`, `InAccident` | ported | defaults of the obstacle kinds | Base answers (1e8, 0, 0) the cars and props override. |

## aiRailSet

The rail an ambient car rides (lane curves, turns, lane changes).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `aiRailSet::aiRailSet` | ported | `Traffic::Traffic` | Lane randomness sin(frand x 6.2831) x 0.5 (first audit); the Hermite basis table it fills is constant. |
| `Reset`, `ComputeXZCurve (both)`, `CalcXZPosition`, `SolveXZCurve`, `CalcRailPosition`, `SolveTurnType` | ported | `Traffic`, `AmbientRoute::solveTurnType` | First audit; Reset is also used by `Traffic::reset`. |
| `CalcRailPosOrient`, `CalcXZPosOrient` | ported | `Traffic::railPosition` (with a direction) | The rail point and its tangent (aiGoalRegainRail::Reset). |
| `~aiRailSet`, `ReplayDebug` | not needed |  |  |

## aiGoal* (the ambient goals)

RandomDrive (on the rail), Collision (hit), RegainRail (back to the lane),
AvoidPlayer (off the rail round the player); parked (6) runs nothing.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `aiGoal::Update` | ported | `Traffic::updateCar` (goal ticks) | The tick counter. |
| `aiGoalRandomDrive: Reset`, `Update`, `SolveVelocity`, `AvoidCollision`, `OkayToEnterIntersection`, `AnyVehiclesComingThisWay`, `UpcomingAccident`, `SolveRailType`, `SolveLane`, `ChangeLanes`, `SpeedLimit` | ported | `Traffic` | First audit. Indicators: set by SolveVelocity (turn) and ChangeLanes, cleared at the end of a turn or lane change; OpenMM2 also cleared them in Reset, which MM2 does not (removed). |
| `aiGoalRandomDrive: aiGoalRandomDrive`, `Init` | ported | `Traffic::Traffic`, `placeCar` | Excess speed, acceleration and separation draw; Init clears acceleration, target, lane change and stop sign. |
| `aiGoalCollision: Reset`, `Update` | ported | `Traffic::updateCar`, `TrafficBodies` | Hazards on; the AI matrix follows the body. |
| `aiGoalCollision::Update (DeclareMover)` | ported (new) | `Traffic::updateCar` (`moverFlags`), `TrafficBodies::beforeStep`, `phys::World::declareInstance` | A wreck (flag 2) is declared (2, 0x08) each frame, so whatever drives into it collides. Was open: phys::World took bodies only. |
| `aiGoalCollision: aiGoalCollision`, `Init` | ported | `Traffic::Car` defaults |  |
| `aiGoalAvoidPlayer: Reset`, `AvoidPlayer` | ported | `Traffic::updateAvoidPlayer` | First audit. |
| `aiGoalAvoidPlayer::Update` | ported | `Traffic::updateAvoidPlayer` | Fixed: the rail distance now comes from the car's room (FindRoomId, MapComponentType): in an intersection the drawn lane's length plus how far past the road's end, else aiMap::DetermineRoadPosInfo on that road or its own (was a projection onto the lane, inferred). New: DeclareMover (2, 0x0a) of the bodiless instance each frame. |
| `aiGoalRegainRail: Reset`, `Update` | ported | `Traffic::resetRegainRail`, `updateRegainRail` | First audit. New: DeclareMover (2, 0x0a) at the end of each update (not when parked by Reset or turning to avoid the player). |
| `aiGoalAvoidPlayer`, `aiGoalRegainRail: aiGoalAvoidPlayer`, `aiGoalRegainRail`, `Init` | ported | `Traffic::placeCar` | Init values at placement (now reset, above). |
| `aiGoalAvoidPlayer`, `aiGoalRandomDrive`, `aiGoalRegainRail: ReplayDebug` | not needed |  | Replay logging. |

## aiVehicleInstance

The ambient car's level instance (drawn, collided; no body until hit).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `aiVehicleInstance::aiVehicleInstance` | ported | `Traffic::Traffic`, `AiRenderer::carModel` | Geometry parts; the arbitrary number (+0x18) for the blink phase (now `AmbientCar::blinkPhase`, a hash of the car index); SetColor; InitBreakable. |
| `SetColor` | ported | `Traffic::Traffic`, `AiRenderer::draw` | trunc(frand x (paint jobs - 1)). |
| `Draw`, `DrawPart`, `DrawShadow` | ported | `AiRenderer::draw`, `VehicleRenderer` | Body, wheels by the rail's tyre rotation, shadow (rendering areas). |
| `DrawGlow` | ported | `AiRenderer::draw`, `VehicleRenderer::drawGlows` | Fixed: TLIGHT while the car decelerates or stands (acceleration below 0 or speed 0; was a 0.5 threshold on both); new: SLIGHT0 / SLIGHT1 for the indicators (bits 1 and 2) while bit 3 of the car's number plus aiVehicleManager's clock (time x 16) is set. Headlight glows as before. |
| `DrawReflected`, `DrawShadowMap` | not needed |  | Empty in build 3393. |
| `GetBound`, `GetMatrix`, `GetPosition`, `GetEntity`, `AttachEntity`, `Detach` | ported | `TrafficBodies::RailCar` | First audit. |
| `InitBreakable`, `Reset` | not needed |  | Traffic breakables (BREAK0-3, threshold 2500): no retail traffic model has BREAK meshes (props-fx). Reset only resets them. |
| `GetData`, `SetMatrix`, `SizeOf` | ported | `AmbientCar::data` | Accessor; SetMatrix stops the game (never called); pool size. |

## aiVehicleManager / aiVehicleActive / aiVehicleData

The 32 bodies traffic cars borrow when hit (first audit: `game/TrafficBodies`).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `aiVehicleManager: Init`, `Attach`, `Detach`, `Update`, `AddVehicleDataEntry` | ported | `TrafficBodies`, `World` (blink clock) | First audit; Update also keeps the indicators' clock (time x 16), now `World::blinkClock`. |
| `aiVehicleManager::Reset` | ported (new) | `TrafficBodies::reset` | Every attached active detached (back to the AI, a wreck if not upright), at aiMap::Reset. |
| `aiVehicleManager: aiVehicleManager`, `~aiVehicleManager`, `scalar_deleting_destructor` | not needed |  |  |
| `aiVehicleActive: Attach`, `Detach`, `Update`, `PostUpdate`, `Impact`, `BottomedOut` | ported | `TrafficBodies::Active` | First audit. |
| `aiVehicleActive::DetachMe` | ported | `TrafficBodies::detach` + `release` |  |
| `aiVehicleActive: Reset`, `UpdateDamage` | ported | `TrafficBodies::Active` (damage 0) | Reset clears the damage, UpdateDamage is empty. |
| `aiVehicleActive::RequiresTerrainCollision` | not needed |  | dgPhysManager::CollideTerrain's branch that asks it is switched off by mmGame::Init (phys-core record). |
| `aiVehicleActive: GetICS`, `GetInst` | ported | `TrafficBodies::Active` | Accessors. |
| `aiVehicleActive: aiVehicleActive`, `~aiVehicleActive`, `scalar_deleting_destructor` | ported | `TrafficBodies::Active` | Sleep thresholds (first audit); the two particle systems and birth rules it builds are never used (props-fx). |
| `aiVehicleData::FileIO` | ported | `ai::loadVehicleData` | First audit. |
| `aiVehicleData: aiVehicleData`, `~aiVehicleData`, `scalar_deleting_destructor`, `GetClassName`, `GetDirName` | not needed |  | Construction and the data-file plumbing names. |

## aiPedestrian / aiPedestrianInstance

Pedestrians (first audit: ai-ambient-city). Spawned by aiMap::Init (pool,
types, variants), placed and recycled with the player's room
(AdjustPedestrians), reset by aiMap::Reset (now ported). The instance has no
bound and is not collidable, so cars never hit pedestrians; it is drawn with
the full model within 35 m (pedAnimationInstance::Draw).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `aiPedestrian: Init`, `Reset (both)`, `Update`, `Wander`, `Anticipate`, `Avoid`, `AvoidPlayer`, `AvoidObstacle`, `AvoidBanger`, `DetectBangerCollision`, `DetectPlayerAnticipate`, `DetectPlayerCollision`, `DetectPlayerForwardCollision`, `TimeToCollision`, `CalcCurve`, `ComputeCurve`, `SolvePosition`, `SolveTargetPoint`, `SolveRoadSegment`, `RoadDistance`, `SetNextRoad`, `GetRoadToRight`, `PickNextRdSeg`, `UpcomingAccident`, `Accident`, `PreCrossStreet`, `WaitCrossStreet`, `CrossStreet` | ported | `ai::Pedestrians` | First audit. The no-argument Reset (the voice) runs from aiMap::Reset: `Pedestrians::reset`. Intersection props: aiMap::Reset empties those lists before play, so `Pedestrians::reset` clears them (they were consulted before). |
| `aiPedestrian::GetRoadToLeft` | ported | `Pedestrians::setNextRoad` | With GetRoadToRight. |
| `aiPedestrian::AvoidPedCollision` | not needed |  | Behind a test that is never true (first audit). |
| `aiPedestrian: aiPedestrian`, `~aiPedestrian` | not needed |  |  |
| `aiPedestrianInstance::Draw` | ported | `AiRenderer::drawPed` | First audit. |
| `aiPedestrianInstance: GetMatrix`, `GetPosition`, `ComputeLod`, `GetVelocity` | ported | `Pedestrian` (published) | Accessors; LOD always 1; velocity zero. |
| `aiPedestrianInstance: GetBound`, `IsCollidable`, `GetEntity`, `AttachEntity`, `Detach`, `SetMatrix`, `DrawShadow`, `DrawShadowMap`, `SizeOf`, `aiPedestrianInstance` | not needed |  | No bound, not collidable (so AttachEntity's ragdoll is unreachable), no shadow; empty or plumbing. |

## aiBanger

A prop as an obstacle (for the pedestrians and the drivers).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `aiBanger: aiBanger`, `IsBlockingTarget`, `Position`, `Radius`, `BreakThreshold`, `Drivable` | ported | `Pedestrians` (`PedObstacle`, `isBlockingTarget`) | First audit (pedestrians). |
| `aiBanger: CurrentRoadIdx`, `CurrentRdVert`, `PreAvoid` | ported (new) | `PhysicsDriver::obstacleRoadIdx`, `ai::avoidPoints`, `PhysicsDriver::propObstacle` | For the drivers (below): on a road the first window slot with its road and the vertex ahead of the prop's centre there; in an intersection the slot after a road arriving there (vertex 1), else slot 0 when the first road leaves it. PreAvoid: the two points square to the line of sight at its radius (YRadius, at most 2) + the clearance. One obstacle per listing, as AddBangersToObsMap makes one aiBanger per list; the position is where the prop was placed (inferred: MM2 reads the instance's current matrix). |
| `aiBanger::Speed` | ported |  | Zero. |

## aiVehiclePhysics / aiStuck

The racers' and police drivers (first audit: ai-vehicles, two passes).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `Init`, `Reset`, `DriveRoute`, `RegisterRoute`, `PlanRoute`, `LocateWayPtFromRoad`, `DestMapComponent`, `CalcRoute`, `DetermineBestRoute`, `ContinueCheck`, `SaveTarget`, `SaveTurnTarget`, `SetTargetPtToDestination`, `EnumRoutes`, `EnumTargets`, `CalcObstacleAvoidPoints`, `CalcRoadTarget`, `CalcDestinationTarget`, `CalcRoadSpeed`, `CheckDistance`, `CalcSpeed`, `InitRoadTurns`, `CalcRoadTurns`, `CalcTurnIntersection`, `InSharpTurn`, `CalcSharpTurnTarget`, `CalcCurrentMaxWidthAdjustment`, `CalcNextMaxWidthAdjustment`, `CalcCurrentRdOffset`, `CalcNextRdOffset`, `SolveRoadTargetPoint`, `SolveShortcutTargetPoint`, `Forward`, `InitForward`, `Backup`, `InitBackup`, `FinishedBackingUp`, `Shortcut`, `InitShortcut`, `Stop`, `Mirror`, `StopRoadTraffic`, `CurrentRoadIdx`, `LSideDistance` | ported | `ai::PhysicsDriver` | First audit. |
| `IsTargetBlocked` | ported | `PhysicsDriver::isTargetBlocked` | Traffic, players and racers verified (first audit). Fixed: its props part was missing (`RouteParams::avoidProps` was read but unused). With avoid-props set (the [Opponent] line's sixth number, the police always) each step tests, after the side-1 cars, the intersection's prop list (empty in play) or the side-1 section list (props whose break threshold is over 250 000), then after the side -1 cars the side -1 list (every prop, as coded, read with the side-1 index) with aiBanger::IsBlockingTarget, kind 5; CalcObstacleAvoidPoints then goes round the prop. The lists are the pedestrians' (`MapView::props`). Racers in the sweep meet props (london circuit3 a: thousands of tests block) and still finish. |
| `CheckForShortcut` | not needed |  | Empty in build 3393. |
| `Position`, `GetMatrix`, `Speed`, `FrontBumperDistance`, `BackBumperDistance`, `RSideDistance`, `CurrentLane`, `CurrentRoadId`, `CurrentRdVert`, `Type` | ported | `ai::trackedCar`, `PhysicsDriver` accessors | Accessors (`frontBumper()` new, for aiRouteRacer::Finished). |
| `aiVehiclePhysics`, `~aiVehiclePhysics`, `DrawId`, `ReplayDebug` | not needed |  |  |
| `aiStuck: aiStuck`, `Reset`, `Update`, `Pegged` | ported | `ai::AiStuck` | First audit. |
| `aiStuck::Init` | ported | `PhysicsDriver::PhysicsDriver` | Binds the car. |
| `aiStuck: ~aiStuck`, `scalar_deleting_destructor` | not needed |  |  |

## aiVehiclePlayer

The player as the AI sees it.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `Update`, `Position`, `Speed`, `CurrentRoadIdx` | ported | `MapView::trackPlayer`, `ai::trackedCar` | First audit. |
| `Reset` | ported | `MapView::resetPlayers` | New: the tracked road is forgotten at aiMap::Reset; the intersection prediction is open (aiMap::PredictIntersectionPath). |
| `Attach`, `GetMatrix`, `FrontBumperDistance`, `BackBumperDistance`, `LSideDistance`, `RSideDistance`, `CurrentLane`, `CurrentRoadId`, `CurrentRdVert`, `Type` | ported | `ai::trackedCar` | Accessors (half the InertiaBox). |
| `aiVehiclePlayer`, `~aiVehiclePlayer`, `DrawId` | not needed |  |  |

## aiRouteRacer / aiRouteNode

The racers (first audit: ai-vehicles).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `Init`, `Reset`, `Update`, `DriveRoute` | ported | `ai::Opponent` | First audit. The racer drives to its .opp destination and stops there (destination speed 0). |
| `Disabled` | ported | `Opponent::update` | Below y -200: Stop without driving. |
| `Finished` | ported | `Opponent::setFinishLine`, `crossedFinishLine`, RaceScreen | Fixed: the game's finish test, against the line aiMap::SetWaypoints set (the race's last checkpoint, a circuit's first): within 30 m (XZ), last lap, last waypoint; 20 - 30 m out it keeps the side of the line, within 20 m it has finished once its front bumper + 1 m is across. OpenMM2 finished a racer within 10 m of its own course's end. Some .opp routes end short of the line (london race6, sf race10: 8 of 517 racers stop up to 7 m before it), so those never finish, as in MM2; OpenMM2's own recovery no longer resets a racer that has arrived. Crash course (mmSingleStunt also asks Finished) gets no line and keeps OpenMM2's arrival test (open: MM2 reads whatever line the last race left). |
| `aiRouteRacer`, `~aiRouteRacer` | not needed |  |  |
| `aiRouteNode: aiRouteNode`, `Reset`, `ReplayDebug` | ported | `PhysicsDriver` route nodes | Zeroed route nodes; logging. |

## aiPoliceForce / aiPoliceOfficer

The police (first audit: ai-vehicles).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `aiPoliceForce: aiPoliceForce`, `RegisterPerp`, `UnRegisterCop`, `State` | ported | `ai::PoliceForce` | First audit. |
| `aiPoliceForce::Reset` | ported | `PoliceForce::reset` | Now reached at a restart (`PoliceSquad::reset` from RaceScreen; was never called there). |
| `aiPoliceForce::Update` | not needed |  | Empty. |
| `aiPoliceForce::~aiPoliceForce` | not needed |  |  |
| `aiPoliceOfficer: Init`, `Reset`, `Update`, `DetectPerpetrator`, `FollowPerpetrator`, `ApprehendPerpetrator`, `Block`, `Push`, `Fov`, `InPersuit`, `PerpEscapes`, `StartSiren` | ported | `ai::PoliceCar` | First audit (Push never chosen). |
| `aiPoliceOfficer::StopSiren` | ported | `PoliceCar::reset` (siren off), police car audio | From Reset; the sound is the audio area's. |
| `aiPoliceOfficer: aiPoliceOfficer`, `~aiPoliceOfficer` | not needed |  |  |

## aiCTFRacer

Capture-the-flag AI racers. aiMap::Init creates trunc(dgStatePack +0x28) of
them and Update / Reset loop over that count; nothing in build 3393 sets the
field off its constructor's 0 (dgStatePack::dgStatePack, mmStatePack's
defaults; no code addresses it), so none ever exists.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `aiCTFRacer`, `~aiCTFRacer`, `vector_deleting_destructor`, `Init`, `Reset`, `Update`, `AquireFlag`, `PreAquireFlag`, `DeliverFlag`, `PreDeliverFlag` | not needed |  | Never instantiated (count 0). |

## Spawns and behaviours checked end to end

- Ambient traffic: pool, types, placement density and spacing, exception
  roads, opponents' 50 m clearance, recycling with the player's room,
  restart (new), wreck flag surviving restarts (as MM2), indicators and
  tail lights (new), off-rail cars colliding as bodiless movers (new), the
  avoid goal's road distance (fixed).
- Pedestrians: pool, types, variants, dealing round new roads, recycling,
  restart (new), no collision with cars (MM2's instance has no bound), the
  intersections' prop lists empty in play (fixed).
- Opponents: count, grid, routes, held start, finish line (fixed), props
  as obstacles (new), stranded-racer recovery placing as MM2 places racers
  (OpenMM2 extra).
- Police: count from density, posts, pursuit, restart resets the force
  (fixed).

## For other areas

- world-objects: ambient traffic and cable cars share the per-section
  vehicle lists and four-way stop queues in MM2 (aiCableCar::CurrentRoadIdx,
  CurrentRdVert, CurrentLane, CurrentRoadId); `ai::Traffic` has no entry for
  external rail vehicles yet (see the final report).
- session: crash course (mmSingleStunt::UpdateChase / UpdateStop) also asks
  aiRouteRacer::Finished, against the line the last race set; OpenMM2 sets
  none there.
- infrastructure: -pedpool is now honoured (it was listed as ignored in
  docs/parity/mm2/infrastructure.md).
- rendering: `VehiclePose::indicators` draws SLIGHT0 / SLIGHT1 for traffic.
- vehicle-physics: racers declare only their car (no trailer) in MM2; no
  retail racer has a trailer, so OpenMM2's racer trailers (none) are moot.
