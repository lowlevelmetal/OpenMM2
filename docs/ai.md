# AI: road network, traffic lights, ambient traffic, pedestrians

Module `mm2_ai` (`src/ai`): the living city (road network built from the AI
map, traffic lights, ambient traffic, pedestrians) and the opponents and
police that drive physics cars.

Sources: MM2's own code (build 3393 through MM2Recomp, documentation only;
function names from its linker map), the retail MM2 data, MM1's AI from
Open1560 (GPL-3.0) where MM2 is not yet checked, and mm2hook's `aiPath`
layout (documentation only).

Evidence levels: **MM2** = verified against MM2's code (the function is
named); **ported** = translated from MM1's code with its constants;
**data** = verified on the retail files; **inferred** = reasoned, to be
checked against the original game.

## AI maps

| File | MM2 class | What OpenMM2 reads from it |
|---|---|---|
| `city/<map>.aimap` | `aiCityData` | `[Speed Limit]` (default 15), `[Ambients Drive On The Left]`, `[Ambient Types/Density]`, `[Traffic Lights]` (single and dual light models; default `sp_traflitsingle_f` / `sp_traflitdual_f`), `[Ped Pool]` (default 100), `[GoodWeatherPedName / BadWeatherPedName]` |
| `race/<dir>/<race>.aimap(_p)`, `roam.aimap(_p)` for cruise | `aiRaceData` | `[AmbientLaneChanges]` (default on), `[Ambient Types/Density]` (used when present, else the city's), `[Exceptions]`, police, opponents |

MM2 reads no `[Density]` section: the traffic density is the menu's (or the
race table's), `aiMap::Init`. London's city file names
`sp_traflitsingle_ped_l` for both light models.

## Road network (`RoadNetwork`)

Built from `city/<map>.bai` (`city::AiMap`).

| Finding | Evidence |
|---|---|
| A path has two sides. Direction +1 rides the second side of the file with increasing section index and arrives at `ends[0]` (at `center.back()`); direction -1 rides the first side, whose lane points are stored in their own travel order, and arrives at `ends[1]` | MM2 (`aiPath` helpers), data |
| Per side the polylines are: lane centre lines, the **sidewalk** line, tram and train lines, the **curb**, the **outer edge** (SF path 106: lane 7.5, sidewalk 12.5, tram 2.5, curb 10, edge 15) | MM2 (`aiPath::SidewalkVertice`), data |
| Lane 0 is the leftmost lane in the direction of travel | MM2 (route choice, lane changes) |
| Drive on the left (`aiCityData`): every two-way road is reversed (`aiPath::ReverseDirection`): each direction rides the other side's lane lines, reversed in point order and in lane order, so lane 0 stays the leftmost; lane counts stay with their sides (equal on every London two-way road); one-way roads and sidewalks are unchanged | MM2 |
| Side flags (the side's 5th field): bit 0 = no ambient traffic (empty sides of one-way roads, alleys and some other roads); bit 1 = no pedestrians | MM2 (`aiMap::AdjustAmbients`, `ChooseNext*Link`, `aiPedestrian::PickNextRdSeg`), data |
| Path end `vehicleRule`: 0 stop sign, 1 traffic light, 3 no control (2 "yield" is unsupported) | MM2 (`OkayToEnterIntersection`) |
| Speed limits (`aiMap::Init`): a road listed in the race's `[Exceptions]` gets that entry's limit, zero included; freeways (path flag 0x4) the city limit + 12.5 m/s; other roads the city limit. The `.bai` value is overwritten | MM2 |
| The path list of an intersection is used in file order (MM2 baked it sorted by angle); a path's index in each end's list is stored in the file | MM2 (`aiIntersection::CreateRoadMap` at bake time, `ReadBinary`) |

## Traffic lights (`TrafficLights`, `aiTrafficLightSet`)

| Behaviour | Evidence |
|---|---|
| One set per intersection with any lit approach; one light per path end there with rule 1, in path order | MM2 (ctor, `SetFourWay`) |
| Light 0 green, the others red at the start (`Reset`) | MM2 |
| A light is green for 3 s, amber for 3 s (cycle 6 s), then red while the next turns green; the timer restarts at 0 (the overshoot is dropped) | MM2 (`Update`; 40C00000h, 3.0) |
| When every source road of the intersection has a light (`NumSources`), each round ends with an all-red pedestrian phase of one cycle: state 4 (red + WALK) then, for the last 3 s, state 5 (red + don't walk); then light 0 again | MM2 |
| Four-way sets (4 paths, 4 sources, 4 "sinks") pair opposite lights (i and i + 2) and make traffic go straight on (`ChooseStraightLinkAt4Way`). `NumSinks` counts departing sides whose no-traffic flag is *set*, so no retail intersection qualifies | MM2, data |
| A light turning green restarts the reaction counters of the stopped queue of the intersection's path *at the light's index* (`aiPath::ResetVehicleReactTicks`), which is the light's own road only while every earlier road has a light | MM2 |
| The sets update after the traffic, as children of `aiMap` | MM2 |
| Vehicles enter on green only | MM2 (`OkayToEnterIntersection`) |

Poles (`aiTrafficLightInstance::Init`, `World::signals()`): at
`trafficLightPos`, model X along the unit XZ direction to
`trafficLightAxis` (which points away from the road on retail data), Z =
(-x.z, 0, x.x), so the glows on +Z face the arriving traffic and the SF
models' arms (-X) reach over the road. The dual model for approaches with
two or more lanes. `DrawGlow` draws the light's glow together with the
pedestrian signal (WALK in state 4, NOWALK otherwise) and only when the
model has both; NIGHT glows from the evening on (time of day > 1). Stop
signs are static instances in `city/<map>_ai.inst`, drawn with the city.

## Ambient traffic (`Traffic`, `AmbientRoute`)

### Population (`aiMap::AdjustAmbients`, **MM2**)

* A pool of 300 cars (`MMSTATE`), each with its type (cumulative
  probabilities of `[Ambient Types/Density]`), paint job
  (trunc(frand x (paint jobs - 1)), the last never used), lane randomness
  sin(frand x 6.2831) x 0.5, reaction ticks 8 - trunc(frand x -17), speed
  excess cycling 0, 8, 6, 4, 2 m/s, and acceleration 5 + 3f and separation
  0.5 + 2.5f from one draw f, all fixed for the session. Density 0: no pool.
* Roads are populated per PSDL room: when the player enters a room, the
  roads in its `.bai` list (the first per-room list) that were not in the
  previous room's are populated, and the roads that dropped out return
  their cars to the pool (`aiPath::ClearAmbients`). The first room is
  populated at the start (`aiMap::Reset`).
* Cars per new area: with d = clamp(menu density, 0, 1) x 0.2 and L the new
  roads' usable lane length (centre length - 5 m per lane, open sides
  only, exception roads aside), 1 + trunc(L d / 8) gaps; cars are placed one
  gap apart, carried across lanes and roads, each within its lane and short
  of the line by its front bumper.
* Roads with an `[Exceptions]` entry instead get trunc(length x density / 8)
  cars per lane (not scaled by the menu) at (i + 1/2) spacings, jittered by
  (spacing - 10) x sin(frand x 6.28) / 2.
* No car is placed within 50 m of a race opponent (3D; the player is not
  checked), or on a lane whose next road cannot be chosen.
* A car finishing a turn towards a road that is not populated turns round
  onto the other side of its own road, or, on a one-way road (the first
  side's flag), goes back to the pool.

### Route choice (`aiMap::ChooseNextLaneLink`, **MM2**; `ai/AmbientRoute`)

The choosers walk the arrival intersection's path list from the car's own
road: +1 for the roads to its right, -1 to its left. A road qualifies when
its side leaving the intersection is open to traffic.

* Four-way light (end flags 3): straight on (index + 2), same lane.
* Freeways (flag 0x4): the next freeway round to the right (same lane);
  otherwise by lane as below.
* One lane: any qualifying road (a freeway at once); next lane 0 unless the
  turn is a right turn (`SolveTurnType`), then the last lane.
* Lane 0 (`ChooseNextLeftStraightLink`): the first road to the left, or the
  one after it when its start is not more than 2 m to the right of the
  car's lane line; 50/50; next lane 0.
* The last lane (`ChooseNextRightStraightLink`): the first road to the right
  or the next one if not more than 2 m to the left; 50/50; next lane the
  last.
* Middle lanes (`ChooseNextStraightLink`): with fewer than three ways out,
  the road with more than two lanes leaving (the most straight one when
  several), else the first to the right; otherwise the second open road to
  the right; same lane.
* `SolveTurnType`: the angle of the next road's first segment against the
  arrival heading; over 0.5 rad right, under -0.5 left, else straight. It
  sets the indicators.

### Driving (`aiGoalRandomDrive`, **MM2**)

* Rails: each lane section is a cubic Hermite curve in XZ between the lane
  vertices (moved sideways by the lane randomness), with the section
  directions (wAxis) times the section length as tangents; the road's end
  directions on the first and last sections. The last section stops the
  car's centre with its front bumper at the line. A turn is one Hermite
  curve from there to the next lane's first vertex, both tangents the
  road-end directions scaled by the Manhattan XZ distance, which is also the
  turn's length; the curve parameter is distance / length.
* Pose: on flat roads (path flag 0x8) upright at the road's first centre
  height; with no player within 100 m on a lane section, the road
  section's own frame and the lane's height; otherwise three corners
  probed onto the ground (front left, front right, back left), when the game
  supplies a ground probe (`Traffic::setGroundProbe`). Rail cars have no
  steering angle; the tyres turn by speed x dt, wrapping at 6.28.
* Speed limit: the road's limit + the car's excess (+5 m/s per lane from the
  right on freeways).
* Away from the junction (or in a turn): follow the car ahead within 20 m
  (`AvoidCollision`, once the reaction time has passed), else speed up to
  the limit at the car's acceleration. In a turn the car ahead is the last
  car of the lane being turned into, or a car of the same road turning into
  the same lane.
* Within 25 m of the line (`IntersectionReactDist`) the car may commit
  (`EnterInt`) when `OkayToEnterIntersection` allows, no accident is ahead,
  the next lane has room and no committed car from another road crosses its
  line; otherwise it picks another road.
  * Lights: green only. Stop signs: stopped (< 0.5 m/s) within 1.5 m, then
    first come, first served, together with one waiting car from the same
    road (`aiIntersection::StopSignOkayToGo`). Uncontrolled: always, and
    the stopped cars behind restart their reaction time.
  * Room (`aiPath::RoadCapacity`): the car's length + separation, plus that
    of every car ahead of it bound for the same road, plus the last car's
    back bumper, must fit before that last car's centre.
  * Crossing (`AnyVehiclesComingThisWay`, not on freeways): a committed car
    leading another road's lane whose way (its lane end to its next lane's
    start) passes on both sides of this car's line. As coded, the other
    roads' lanes are read for this car's own directions.
  * Accidents (`UpcomingAccident`): a car out of normal driving (hit,
    regaining its lane, avoiding the player, parked) in the intersection
    or on the next road. MM2 finds them by position in its obstacle map;
    OpenMM2 by the car's road (**inferred** equivalent).
* Not committed: follow the car ahead on the same road (skipping one that is
  regaining its lane), else brake to stop 0.25 m before the line
  (a = -v^2 / 2(d - 0.25)); a car that stopped short (reaction over, at most
  2 m/s, more than 1 m out) creeps on at 2 m/s.
* Committed: follow a car ahead within 20 m when their targets differ,
  else speed up.
* Following (`AvoidCollision`), with u = lead speed - 2.5 (0..999) and gap =
  own front + lead's back bumper: inside the gap stop dead (ease off at
  -v^2/6 behind a lane-changing car); within gap + separation, 0.75 x speed
  and the lead's acceleration (or stop when it is not accelerating); beyond,
  brake to u at gap + separation (2 m sooner when both are turning or both
  changing lanes), or accelerate when slower than u. Target min(u, limit).
* Integration: v += a dt, clamped to the target; a car stopped behind a
  stopped car within 20 m restarts its reaction time.
* Lane changes (`SolveLane`, `ChangeLanes`; `[AmbientLaneChanges]`): once
  per road, after the first quarter of a road longer than 60 m, to the
  neighbouring lane with fewer cars beyond that quarter; a curve from the
  car to the new lane a quarter of the lane plus 30 m on; indicators on.
* Physics hit (`aiVehicleAmbient::Impact`, `aiVehicleManager`,
  `aiVehicleActive`; `game/TrafficBodies`): a moving body touching a rail
  car makes it a rigid body (at most 32; the oldest is let go when full):
  centre of mass at the model origin, the box at CG with Size extents,
  gravity 19.6, friction 1.0 and elasticity 0.5 (the box keeps its default
  material), four `vehWheelCheap` wheels (spring/damper from the
  `.aivehicledata`, locked-wheel rubber grip up to 0.4 N per axis); hazard
  lights on. It stays in its lane list where it was hit, so traffic queues
  behind it. After 15 still physics steps (0.1 m/s, 0.1 rad/s), or below
  y -100, it is handed back: upright on the ground (probe +0.5 to -3 m
  along its up axis, normal . up >= 0.9) and never wrecked, it drives a
  Hermite curve back onto its lane (`aiGoalRegainRail`, 30 m, speeding up to
  the limit); otherwise it stays a wreck for good. A third regain started
  within 1 m of the last one parks it for good. Cars are recycled only with
  their road. The hit impulse is OpenMM2's (closing speed, reduced mass,
  restitution the product of both elasticities, **inferred**).
* The player (`aiGoalAvoidPlayer`): a moving car whose next 30 m of rail (in
  10 m pieces, within its width) holds the player within 25 m, with no car
  ahead nearer, leaves its rail: one chance to honk; it brakes to stop
  short of the player when below its cruise speed, otherwise keeps its
  speed; it steers by heading changes of up to 0.03 rad per update, aiming
  to pass the player (as coded, the yaw turns away from the aim point), and
  never reverses. Once the player is neither in its next 25 m nor in front
  within 25 m, it regains its lane.

**Deviations and approximations** (marked in the code):

* Fixed 30 Hz steps (MM2: per frame); avoid-player steering is per update.
* No terrain probe in tools/tests (no physics world): the heights come from
  the rails.
* Regaining the lane keeps the car's road and lane (MM2 maps it onto the
  road or intersection under it first); a car further than the road's
  half width + 5 m from its lane parks.
* Cars in avoid/regain register on their road for accidents (MM2: by
  position).
* Stop-sign queues drop a recycled car at its own arrival intersection (MM2
  clears end A's for both directions).
* Not done: cable cars and subways (`aiCableCar`, `aiSubway`), the ambient
  horn and voice audio (`AmbientCar::horn` marks the attempt), breakable
  parts and impact sounds of physical traffic, regaining onto another road.

## Pedestrians (`Pedestrians`)

MM2's pedestrians are skeletal and driven by `anim/pedmodel_*.csv`, unlike
MM1's, so little of MM1's code carries over directly.

**Ported constants**:

* lateral spread on the walkway sin(frand·2π)·1.8 (`aiPedestrian::Reset`),
  clamped to the sidewalk width;
* awareness of the player within 35 m (`DetectPlayerAnticipate`, `flt_63936C`);
* collision zone 6 m (`DetectPlayerCollision`, `flt_639360`);
* activity radius 75 m (`aiPedestrian::Update`, 5625 squared);
* turn threshold 0.15 rad (`Wander`).

**Inferred**:

* **Spawning:** 0.5 × density per 10 m of active sidewalk, not within 35 m.
* **Walking:** along the sidewalk line at the WALK state's root-motion speed
  (forward distance / duration, at 20 animation frames per second, which
  gives a normal pace). At corners peds continue on a nearby sidewalk or turn
  round, and occasionally stop for 2–6 s.
* **Reactions:** the player car's straight-line path is projected.
  * Closest approach under 2.5 m within 1.25 s: dive away from the car's
    line, WALK_LDIVE/WALK_RDIVE or ANTIC_LDIVE/ANTIC_RDIVE, then the table's
    chain through ground and get-up states back to STAND.
  * Within 6 m and 3 s: brace (WALK_ANTIC/STAND_ANTIC → ANTIC, facing the
    car), and walk on once it has passed.
* **Root motion:** non-walking states move by the table's forward and side
  distances (side positive = left).

## API

```cpp
#include "ai/World.h"
ai::Settings s;                 // trafficDensity, pedestrianDensity, maxCars (pool, 300), maxPeds, seed
auto world = ai::World::create(city, vfs, s, /*race aimap or nullptr = cruise*/ nullptr, &error);
ai::PlayerCar player;           // matrix, velocity, radius, box size, steering, reverse gear, horn
world->setOpponents(positions); // no ambient car is placed within 50 m of these
world->update(frameDt, player); // fixed 30 Hz steps inside; populates by the player's PSDL room
for (const ai::AmbientCar& c : world->cars())   // model, paint (trunc(paint x (paint jobs - 1))),
    ...;                                         // transform (model origin, faces -Z), speed,
                                                 // tireRotation, braking, signal (incl. hazards),
                                                 // horn (attempt), goal, physical, wreck
for (const ai::Pedestrian& p : world->peds())   // typeName, variant (anim/<type>.shaders),
    ...;                                         // transform, state, animFile, frame -> asset::posePed
for (const ai::Signal& sig : world->signals())  // model, transform, state (light + walk glows)
    ...;
world->traffic().setImpactHandler(...);         // rail car becomes physical when hit (game/TrafficBodies)
world->traffic().setGroundProbe(...);           // terrain fit of cars near the player
```

### Phase 2: driving physics cars

Opponents and police are full physics cars in both games. The drivers
(`ai::Opponent`, `ai::PoliceCar`, below) write a `phys::CarSim`'s inputs
directly, as MM2's aiVehiclePhysics writes vehCarSim Steering / Brakes /
HandBrake / Engine Throttle, so they obey exactly the player's car
simulation.
`ai/VehicleControl.h` (an earlier proposal) is not used by them.

## Verification

`test_ai` (retail tests need `OPENMM2_GAME_DATA`):
- **Synthetic networks** (`test_traffic.cpp`): MM2's lane rules at a
  crossroads (lane 0 left or straight, last lane right or straight, the next
  path in the list is to the right), closed sides never chosen, drive-on-left
  reversal (lane 0 the outer lane on the left), the density of a newly
  populated area (density x 0.2 / 8 per metre), cars queueing at a red light
  and going on green, and no car closer than 4 m behind another round a block.
- **Lights** (`test_ai.cpp`): the 6 s cycle with amber from 3 s and the
  rotation; the all-red WALK / don't-walk phase when every approach has a
  light.
- **Retail cities:** London and SF build, drive on the left only in London,
  every signal model present; two London worlds with the same seed stay
  bit-identical for 120 s; no car enters a light that has been red for over
  2 s; lane deviation within 1.5 m (lane randomness plus the Hermite
  sections).
- `test_game` `TrafficBodies`: a car hit by the player becomes physical, is
  pushed, comes to rest and drives back onto its lane.

`mm2tool aisim <source> <city> [--seconds N] [--drive] [--at x,z] [--png out.png]`
runs the AI headless and plots lanes, sidewalks, signals and trails.
London, 10 minutes, player beside the road at (60, -560): 26 cars, 0
red-light entries, about 95 junction entries per minute throughout.
A player standing in the middle of a junction makes the cars there avoid
him and the roads behind them count as "in accident", as in MM2, so traffic
around him jams. `OPENMM2_AISIM_VERBOSE=1` and `OPENMM2_AISIM_TRACE=<car>`
print diagnostics. `mm2tool aidump <source> <city> [path]` dumps paths,
polylines and ends.

## Opponents and police (phase 2)

Code: `ai/Course` (the roads a car drives), `ai/Driving` (MM2's
`aiVehiclePhysics`, the controller both share: `PhysicsDriver`),
`ai/Opponent` (`aiRouteRacer`), `ai/Police` (`aiPoliceOfficer`,
`aiPoliceForce`). Ported from MM2 build 3393 (MM2Recomp, documentation
only); where OpenMM2 still reconstructs, the table says **inferred**.

MM2 plans on its `aiPath` road segments, three at a time (the road the car
is on and the next two of its waypoint list), with vertex indices and
per-road sharp-turn circles. OpenMM2 plans the same way on an `ai::Course`:
the same roads joined into one line, with the curbs, sidewalk edges, road
section frames and `aiPath` flags along it. Intersections are crossed by a
chord, where MM2 fits a turn circle (`CalcTurnIntersection`,
`CalcSharpTurnTarget`; **not ported**).

### Data

* `race/<city>/<race>-<a|p>-<n>.opp` (`aiRouteRacer::Init`; **MM2**): after
  the header line, row 0 is the car's grid place, its fourth number the
  heading in degrees (reset heading = value × 0.017444445, i.e. 3.14 / 180,
  in `Mat34::rotationY`'s sense; checked against the races' start
  headings); the last row is the destination; every row between is a
  waypoint, the intersection whose room holds it. Circuits drive the
  waypoints `laps` times (`PlanRoute` starts every new lap at waypoint 1).
* `.aimap [Opponent]` (`aiRaceData::aiRaceData`, `"%s %s %f %d %f %f %d
  %d %d %d %d %f"`; **MM2**), e.g. `vpcoop circuit0-a-0.opp 0.86 0 50.0 0.7
  1 1 1 1 0 1.0`: car, path, then `RegisterRoute`'s settings
  (`OpponentSettings::fromData`):

  | # | Meaning | Retail values |
  |---|---|---|
  | 1 | MaxThrottle, written to the car every frame | 0.75 – 1.0 |
  | 2 | a flag `RegisterRoute` stores and never reads | 0 |
  | 3 | look-ahead: the route is planned this far (m) | 50, 99, 101, 150 |
  | 4 | brake threshold (brake when the demand exceeds it) | 0.07 – 1.0, mostly 0.68 – 0.7 |
  | 5 | steer round ambient traffic | 0 / 1 |
  | 6 | steer round props (unbreakable ones; not modelled) | 0 / 1 |
  | 7 | steer round the players | 0 / 1 |
  | 8 | steer round other racers (after the third waypoint of a lap) | 0 / 1 |
  | 9 | prefer routes over the sidewalk | 0 |
  | 10 | corner speed factor | 0.89 – 2.29 (professional races higher) |

  Defaults when missing: 1.0, 0, 50, 0.7, 1, 1, 1, 1, 0, 1.0.
* `.aimap [Police]` (**MM2**): `car x y z heading n behaviours chance
  range`: `heading` in degrees (× −0.017444445), `n` unused, `behaviours`
  bits of apprehend behaviours (15 everywhere; 0 = follow only), `chance`
  of pursuing an opponent (0, 0.5 or 1), `range` the player must be within
  for that (50, 70, 100). Defaults 0, 15, 0.5, 50.
* `.aimap [CopChaseDistance]` (only `sf/crash5`: 150): where a suspect
  escapes; default 250 m (**MM2**).
* MM2 builds every AI car with its base tune (`aiVehiclePhysics::Init` →
  `vehCar::Init(<car>)`); the retail `*_opp` / `*_cop` tunes are MM1
  leftovers (**MM2**).

### The controller (`PhysicsDriver`, `aiVehiclePhysics`)

| Behaviour | Status |
|---|---|
| States Forward, Backup, Shortcut (off the roads: straight for the next waypoint), Stop (brake, steer for the destination) (`DriveRoute`) | MM2; Shortcut's trigger (more than 10 m beyond the curb) **inferred** |
| Steering = clamp(heading error × 1.33 × 1.428, ±1); handbrake over 30 m/s when that exceeds full lock (`Forward`) | MM2 |
| Target = the first point of the best planned route (`SolveRoadTargetPoint`) | MM2 |
| Route points (`CalcRoadTarget`): the farthest point of the road reachable in a straight line between the curbs, each moved in by the car's side distance + 1 m; when the road bends, the curb point at the inside of the bend; on a straight, at the look-ahead distance, keeping the car's place across the road. Points are chained (`EnumRoutes`/`ContinueCheck`) until the look-ahead is covered, 1 m above the road | MM2 (the end of an unbent walk: a point within the window of directions, **inferred** detail) |
| Divided roads (`aiPath` flag 0x1, ten SF roads): the centre line is a curb on the car's side | MM2 |
| Obstacles (`IsTargetBlocked`, `aiVehicle::IsBlockingTarget`): a vehicle whose box corner lies ahead within the way + 2 car lengths, within half the car's width + 1 m and 0.7 rad of the way; the nearest. Classes by the aimap flags; police cars are no obstacle; other racers only after the third waypoint of a lap | MM2 (props not modelled) |
| Going round (`CalcObstacleAvoidPoints`, `aiVehicle::PreAvoid`, `EnumTargets`): the box corners pushed out by the side distance + 2 m; the leftmost and rightmost each start a route if within 1.57 rad of the road and on the road or the sidewalk (not for semis); a further vehicle in the way is passed on the same side (ten deep); no way round keeps the point, marked | MM2 (the recursion's special cases simplified) |
| Best route (`DetermineBestRoute`): least total turning; first among routes over the sidewalk when preferred, then among those with a way round every obstacle | MM2 |
| Corner speed at the first route point (`CalcSpeed`): bend angle a > 0.7 rad → v = sqrt(10 tan((3.14 − a)/2) × 23.76) × factor; brake = (speed − v) / (t × 23.76), t = distance / speed; braking when it exceeds the threshold: brakes clamp(brake), throttle 0, yaw momentum × 0.85 | MM2 |
| Road bends (`CalcRoadSpeed`): turns over 0.7 rad within their set-back + the look-ahead; r = R / (1 − sin((3.14 − d)/2)), v = sqrt(23.76 r) × factor, halved into an alley (`aiPath` flag 0x2); turns already entered are not braked for | MM2 formula; R (room to the inside curb) and the merged turns of the course **inferred** (MM2: `CalcTurnIntersection`, `aiPath::SharpTurnRadius`) |
| Destination (`CalcRoadSpeed`, final approach within 70.7 m): brake to the destination speed at the destination less the stop distance when the demand exceeds 0.014 × distance; within 2.5 m with speed 0 wanted, brakes full on | MM2 |
| Past the destination within 25 m heading its way: brake, steer away (`Forward`) | MM2 |
| Throttle otherwise = MaxThrottle | MM2 |
| CarFrictionHandling 2 while touching the player, else 1 (`Forward`; the 0x8000 instance flag set by `dgPhysManager::CollideInstances`) | MM2 (MM1 switched Realism instead; MM2 has none) |
| vehStuck tuned for AI: TimeThresh 0.5 s (police 0.75), PosThresh 1 m, no rotation recovery (`aiVehiclePhysics::Init`, `aiPoliceOfficer::Init`) | MM2 |
| `aiStuck` (0.3 s, 0.6 m, 1.0 m, 1 rad/s): watches from when vehStuck starts watching; stuck and pegged → steering 1, throttle 1, vehStuck cleared | MM2 |
| vehStuck stuck → Backup with momenta cleared (`Forward`); Backup (`Backup`): reverse at throttle 0.85, steering −2.857 × angle, until within 0.1 rad of the target (then turned onto it) or 65 frames; `FinishedBackingUp`: momenta × 0.25, brakes on, forward | MM2 (65 frames = 66/30 s) |
| Wrecked: no inputs, momentum × 0.95 a frame; circuits repair after 5 s (`DriveRoute`) | MM2 |
| Per-frame factors scaled to the frame time as for 30 Hz | **inferred** (MM2 ran the AI every frame) |

The braking deceleration is MM2's 23.76 m/s² (1.2 × 19.8) throughout; the
earlier `BrakeMeter` fitted to the old physics is gone.

### Opponent (`aiRouteRacer`)

| Behaviour | Status |
|---|---|
| One route for the race: waypoints, destination, laps (`DriveRoute` → `RegisterRoute`) | MM2 |
| Final approach (plan to the destination) past the last waypoint of the last lap | MM2 |
| Fallen below y = −200: disabled, stops (`DriveRoute`, `Disabled`) | MM2 |
| The race result is the game's (finish line); the AI drives on to its destination and stops there | MM2 (`Finished` is only read by the game) |
| No progress for 10 s, or fallen 15 m below the line: put back on the line, further along each time it recurs | **OpenMM2** recovery, not in MM2 |
| Rubber-banding | none in MM2 |

### Police (`aiPoliceOfficer`, `aiPoliceForce`)

| Behaviour | Status |
|---|---|
| At its post braked (Stop) until a suspect appears (`Reset`) | MM2 |
| Detection (`DetectPerpetrator`, `Fov`): the players first, any within 75 m (3D) and within 1.57 rad of the cop's heading. No speeding, collision or line-of-sight test (`Speeding`, `OffRoad`, `IsPerpACop` return 0, `HitMe` is never called) | MM2 |
| Opponents: the same test, while the player is within `range` of the cop, with probability `chance`; one that loses the roll is ignored until the cop resets | MM2 |
| `aiPoliceForce`: at most three cops per suspect and three suspects; the nearest pursuer within 25 m apprehends (`State` 1), the others follow (2) | MM2 |
| Follow only while the player reverses, the cop backs up, it has no apprehend behaviours, or the suspect is under 10 m/s (`Update`) | MM2 |
| Follow (`FollowPerpetrator`): siren on; the road route to the suspect (`aiMap::CalcRoute`), arriving 5 m short at its speed + distance − 12.5 m; corner factor 2.0, look-ahead 75 m, steering round traffic but not the player | MM2; the route rebuilt every second or when the goal moves 15 m (MM2: every frame) **inferred** |
| Apprehend (`ApprehendPerpetrator`, `Block`): to 12 m ahead of the suspect when level with or ahead of it, else beside its tail (more than 20 m behind on a road: the side that is on the road), at its speed (+25 m/s from behind); within 3 m, hold its heading 3 m/s slower (`aiVehiclePhysics::Mirror`) until it gets ahead again | MM2 (`Push` and `Barricade` are never chosen in this build) |
| Full throttle under 50 m/s: momentum × 1.03 a frame (`Update`) | MM2 |
| Escape beyond `[CopChaseDistance]` (XZ), or the cop wrecked: siren off, out of the force, the cop stops where it is and watches again; a wrecked cop is out of action (`PerpEscapes`) | MM2 |
| Fallen below y = −200: back to its post (`Update` → `Reset`) | MM2 |
| A cop in a room with flag 4 drops out | **not ported** |
| Siren while following and apprehending (`StartSiren` / `StopSiren`) | MM2 |
| Cops placed: the first trunc(count × clamp(density, 0, 1)) of the `[Police]` entries (`aiMap::Init`): cruise uses the menu's cop density, races their table's cop count (0 none, 1+ all), the crash course all | MM2 |

### Evidence (retail data, `test_game`, physics as of this commit)

* London circuit0, amateur, 7 opponents, 3 laps: all finish in 49–61 s;
  flying laps 13.6–23 s on a 412–449 m line; the field hugs the inside of
  the loop; no resets.
* London race1 with full ambient traffic (whose racers steer round
  traffic): all 6 finish in 77–108 s over 2.0 km, using the sidewalk to get
  round cars, none beyond it.
* A car put nose-first against a wall: backs up (vehStuck → `Backup`) and
  is 150 m along its line after 13 s.
* A player-flagged car driven past a parked London cop at 13 m/s (lawful):
  pursued from 6.7 s (in view within 75 m), followed to within 15 m once
  it slows; at 15 m/s the nearest cop apprehends and blocks it. A suspect
  70 m behind the cop is not pursued; one moved beyond the chase distance
  escapes (siren off, the cop stops).
* Sweep of every circuit and checkpoint race, both cities and
  difficulties, one lap, all opponents (`OPENMM2_AI_SWEEP=1`): 498 of 517
  opponents finish (488 with the MM1 port). The rest are wrecked
  (point-to-point opponents stay wrecked, as in MM2), mostly SF race11 (a
  crash at about 47 m/s) and race8.

`OPENMM2_AI_TRAILS=<dir>` writes top-down plots of these runs (see the
header of `tests/game/test_opponent_race.cpp`).

### In the race (`app/RaceScreen`)

* Each frame: player input, then every AI driver reads all cars (player id
  0, opponents 1 + their index in `Session::opponents()`, police 100 + n,
  ambient traffic 10000 + id) and writes its car's inputs, then the props
  and solid traffic see every simulated car, then the physics step.
  Opponents are held until `Session::racersReleased()` (and, in the crash
  course, while `opponentActive()` is false); `OpponentFinished` sends them
  to `finish()`.
* Police livery: `vpcop` paint job 0 uses the `vpcop_ca_*` textures and 1
  the `vpcop_ln_*` ones, so London cops get 1 and San Francisco cops 0
  (**inferred** from the texture names).
* Light bar: `SIREN0` and `SIREN1` are additive glow meshes (low detail
  only) drawn alternately, two flashes a second each, while the siren is on
  (rate **inferred**).
* `OPENMM2_DEBUG_AI=1` logs every AI car once a second.
