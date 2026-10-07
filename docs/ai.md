# AI: road network, traffic lights, ambient traffic, pedestrians

Module `mm2_ai` (`src/ai`). Phase 1 covers the living city: the road network
built from the AI map, traffic lights, ambient (rail) traffic and
pedestrians. Opponents and police (phase 2) will drive physics cars through
`ai/VehicleControl.h` once `src/phys` is in place.

Sources: MM1's AI from Open1560 (`code/midtown/mmai/*.h` for class layouts,
`code/midtown/game.asm` for the routines it has not rewritten; GPL-3.0), the
retail MM2 data, and mm2hook's MM2 `aiPath` layout (documentation only).
The opponents and police (last section) follow MM2's own code (build 3393
through MM2Recomp, documentation only, function names from its linker map).

Evidence levels: **MM2** = verified against MM2's code (the function is
named); **ported** = translated from MM1's code with its constants;
**data** = verified on the retail files; **inferred** = reasoned, to be
checked against the original game.

## Road network (`RoadNetwork`)

Built from `city/<map>.bai` (`city::AiMap`).

| Finding | Evidence |
|---|---|
| Per side, the polylines are: one lane centre line per lane (travel order), trams, trains, then **sidewalk centre** (pedestrian line), **curb** (road edge) and **outer edge** (building side). E.g. London path 88: lanes at 2 and 6 m, sidewalk 9.5, curb 8, edge 11 (`halfWidth` 11). | data |
| Right-side lanes are stored with increasing section index, left-side lanes reversed, in **both** cities: the files describe traffic keeping to the right. | data (every path) |
| London's cruise map has `[Ambients Drive On The Left] 1`. Lanes are then mirrored across the centre line at each section (lateral offset changes sign), keeping their direction. One-way roads lay their lanes out symmetrically about the centre (e.g. ±2, or 6/0/−6), so they keep working. | data (layout); mirroring inferred |
| The flag only appears in `roam.aimap`/`roam.aimap_p`; it is applied city-wide. | inferred |
| A path end's `vehicleRule` takes the values 0, 1, 3: MM1's `aiPath::IntersectionType` 0 stop sign, 1 traffic light, 3 no control (London 45 / 519 / 516 ends; SF 36 / 533 / 189). | data + ported names |
| Right lanes arrive at `ends[0]` (at `center.back()`), left lanes at `ends[1]`. | data |
| Pedestrians stand at the outer edge's height; the sidewalk polyline itself sits halfway up the curb (y 0.07 vs 0.15). | data; use inferred |
| `[Exceptions]` road ids are path ids; their density replaces the map density, their speed limit (if > 0) the default. | inferred |

## Traffic lights (`TrafficLights`) — ported

`aiTrafficLightSet::Reset/Update`: each intersection with lights has one light
per approaching path end with rule 1, in the intersection's path order. Light
0 starts green, the rest red. One light is green at a time for the 10 s cycle
(`41200000h`), turning amber for the last 4 s (`flt_61B474`), then red while
the next light turns green; when a light turns green, the stopped queue
behind it restarts its reaction counters (`aiPath::ResetVehicleReactTicks`).
Vehicles only enter on green.

Poles (`World::signals()`): one per light, at `trafficLightPos`. The vector
`trafficLightAxis − trafficLightPos` points from the pole towards the road
centre (data: on London path 10 it points at the centre line). The models
(`sp_traflitsingle_l/_f`, `sp_traflitdual_f`) extend their arm along −X and
carry RED/YELLOW/GREEN `GLOWDAY`/`GLOWNIGHT` parts facing +Z, so the pole's
−X is that vector. Model choice: `_l` London, `_f` San Francisco; the dual
(arm) model for approaches with two or more lanes where it exists (inferred).
Stop signs are static instances in `city/<map>_ai.inst` (`sp_stop_f`), drawn
with the city.

## Ambient traffic (`Traffic`)

Cars ride their lane polylines and Hermite curves through intersections; they
never touch the physics until hit.

**Ported from MM1** (constants decoded from game.asm):

* Speed control, `aiGoalRandomDrive::SolveVelocity`:
  * within `IntersectionReactDist` (25 m) of the end of the lane, a car
    commits to the junction (`EnterInt`) when `OkayToEnterIntersection`, the
    next road has room and nothing crosses; a car that commits from a stop
    reacts after `TotReactTicks − 3` ticks;
  * otherwise it brakes to stop 0.25 m before the line: a = −v²/(2(d − 0.25));
  * a car stopped short of the line (> 1 m, ≤ 2 m/s, reacted) creeps on at
    2 m/s with a 5 m reaction distance;
  * away from junctions it accelerates at `VehicleAccelFactor` towards
    speed limit + `ExheedLimit`;
  * integration: v += a·dt, clamped to the target (decelerating within
    target + 0.05, or accelerating past it); lane distance += v·dt.
* Car following, `aiGoalRandomDrive::AvoidCollision`, lead within 20 m: with
  gap G = own front bumper + lead's back bumper:
  * inside G, stop dead;
  * within G + `SeparationDist`, match a faster lead, or else slow to
    0.75 × speed and take the lead's (positive) acceleration;
  * beyond that, a = (u² − v²) / (2(d − G − sep [− 2 when both cars are on
    the same rail type])), to arrive at the lead's speed at the gap.
* Intersections, `OkayToEnterIntersection`:
  * light: green only;
  * stop sign: stop (< 0.5 m/s within 1.5 m), take a ticket (the frame
    counter, `aiMap+72h`); the lowest ticket among the lanes' leading cars
    goes, together with cars from the same road;
  * uncontrolled: always.
* Per-car randomisation, in construction order:
  * lateral `LaneRandomness` = sin(frand·2π)·0.5;
  * `TotReactTicks` = 8 − trunc(frand·−17) ∈ [8, 24];
  * `ExheedLimit` from a global counter cycling 0, 8, 6, 4, 2 m/s;
  * `VehicleAccelFactor` = 5 + 3·frand;
  * `SeparationDist` = 0.5 + 2.5·frand;
  * `IntersectionReactDist` = 25.
* Turn curves, `aiRailSet::ComputeXZCurve`/`SolveXZCurve`: a cubic in X and
  Z with the Hermite basis decoded from the matrix initialised in the
  `aiRailSet` constructor (arguments p0, p1, m0, m1).
* Spawning, `aiMap::AdjustAmbients`/`NumCars`:
  * cars per lane = trunc(length · density / 8) when a road comes into range;
  * spacing length/(n + 1), each car jittered by sin(frand·2π)·spacing/2;
  * never within 50 m of the player (`flt_61B238` = 2500);
  * density = road exception or map `[Density]`, times the menu setting;
  * vehicle types by cumulative probability (`[Ambient Types/Density]`).
* Tyre rotation += dt·speed, wrapping at 2π; reaction ticks count updates.
* Player zone: within 25 m (`flt_61BAA4` = 625), a player in the car's path
  is treated as a stopped vehicle ahead, and the car honks when stopped.
  MM1 hands over to `aiGoalAvoidPlayer`.

**Inferred / deviations** (marked in the code):

* Next road: uniform among the roads leaving the intersection, restricted by
  lane discipline after `ChooseNextLeftStraightLink`/`RightStraightLink`/
  `StraightLink`:
  * the lane nearest the centre turns across traffic or goes straight;
  * the outer lane turns away or goes straight;
  * middle lanes go straight;
  * lane position is kept.
* Indicators from the turn angle (> 0.35 rad).
* Turn tangent length = chord between the lane ends.
* Bumper distances = half the AI data's Size.z (the original used the model's
  box).
* Crossing traffic: entry is refused while a car from another road is in the
  first 70 % of its turn, or committed and within 12 m of the junction.
* Room on the next road ("don't block the box"): the whole car plus
  separation + 3 m (+4 m if the last car there is slow).
* Followers also follow a lead that has already committed but is still ahead
  on the lane; MM1 only brakes for the stop line there, which made cars drive
  into slow leads.
* Spawning skips spots within a car length + 1.5 m of another car and the
  last 6 m before the line; MM1's jitter can stack cars.
* The area keeps its density: once a second, a lane with fewer cars than its
  share gets one more, moving, out of sight.
* Gridlock relief: cars stuck for 30 s at least 50 m from the player are
  recycled.
* Fixed 30 Hz step: MM1 used the frame delta.
* Steering angle from the change of heading.
* Physical hand-over hook (`Traffic::impact`, `setImpactHandler`), standing in
  for MM1 `aiVehicleSpline::Impact` → `aiVehicleActive`.

Not done yet: lane changes (`aiGoalRandomDrive::ChangeLanes`), trams and cable
cars on the tram polylines, subways/trains, bridges, the original's audio
hooks (horns are flagged, not played).

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
ai::Settings s;                 // trafficDensity, pedestrianDensity, maxCars, maxPeds, seed
auto world = ai::World::create(city, vfs, s, /*race aimap or nullptr = cruise*/ nullptr, &error);
world->update(frameDt, playerPos, playerVel);   // fixed 30 Hz steps inside
for (const ai::AmbientCar& c : world->cars())   // model, variant (paint job = variant % count),
    ...;                                         // transform (ground contact, faces -Z), speed,
                                                 // tireRotation, steer, braking, signal, horn
for (const ai::Pedestrian& p : world->peds())   // typeName, variant (anim/<type>.shaders),
    ...;                                         // transform, state, animFile, frame -> asset::posePed
for (const ai::Signal& sig : world->signals())  // model, transform, state (draw the matching GLOW part)
    ...;
world->traffic().setImpactHandler(...);         // rail car becomes physical when hit
world->traffic().impact(carId, impulse);
```

### Phase 2: driving physics cars

Opponents and police are full physics cars in both games. The drivers
(`ai::Opponent`, `ai::PoliceCar`, below) write a `phys::CarSim`'s inputs
directly, as MM2's aiVehiclePhysics writes vehCarSim Steering / Brakes /
HandBrake / Engine Throttle, so they obey exactly the player's car
simulation.
`ai/VehicleControl.h` (an earlier proposal) is not used by them.

## Verification

`test_ai` (7 tests; retail tests need `OPENMM2_GAME_DATA`):
- **Synthetic:** polylines, lane directions and drive-on-left mirroring, the
  light cycle (10 s, amber from 6 s, rotation), and cars queueing at a red
  light without crossing.
- **Retail cities:** London and SF both build, with drive-on-left only in
  London, 4 pedestrian types and every signal model present.
- **Determinism:** two London worlds with the same seed stay bit-identical
  for 120 s.
- **Traffic behaviour:** lane deviation ≤ 0.5 m (the lane randomness) and no
  car enters a light that has been red for over 2 s.
- **Pedestrians:** walking peds stay within their sidewalk, and one dives when
  a car drives straight at it.

`mm2tool aisim <source> <city> [--seconds N] [--drive] [--png out.png]` runs
the AI headless and plots lanes, sidewalks, signals and trails. Typical
10-minute runs: 0 red-light entries, under 10 brief car overlaps (crossing
turns), maximum lane deviation 0.50 m, and stable throughput (about 100
junction entries per minute around a stationary player, about 240 when
driving). `OPENMM2_AISIM_VERBOSE=1` and `OPENMM2_AISIM_TRACE=<car>` print
diagnostics. `mm2tool aidump <source> <city> [path]` dumps paths, polylines
and ends.

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
