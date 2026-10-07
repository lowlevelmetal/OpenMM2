# AI: road network, traffic lights, ambient traffic, pedestrians

Module `mm2_ai` (`src/ai`). Phase 1 covers the living city: the road network
built from the AI map, traffic lights, ambient (rail) traffic and
pedestrians. Opponents and police (phase 2) will drive physics cars through
`ai/VehicleControl.h` once `src/phys` is in place.

Sources: MM1's AI from Open1560 (`code/midtown/mmai/*.h` for class layouts,
`code/midtown/game.asm` for the routines it has not rewritten; GPL-3.0), the
retail MM2 data, and mm2hook's MM2 `aiPath` layout (documentation only).
Nothing comes from the MM2 executable.

Evidence levels: **ported** = translated from MM1's code with its constants;
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
directly, as MM1's AI wrote `mmCarSim::Steering` / `Brakes` /
`Engine.Throttle`, so they obey exactly the player's car simulation.
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

Code: `ai/Course` (driving lines), `ai/Driving` (shared goals),
`ai/Opponent`, `ai/Police`. Ported from MM1 (Open1560 `code/midtown/mmai`
and `game.asm`, GPL-3.0, Copyright (C) 2020 Brick); constants are decoded
from the x87 code where named. Everything not listed as ported is an
OpenMM2 reconstruction (**inferred**).

### Data

* `race/<city>/<race>-<a|p>-<n>.opp`: row 0 is the car's grid place; the
  other rows lie on AI-map intersections (0–12 m from a centre on every
  London and SF file), and consecutive ones are joined by a single road in
  94 % (London) / 89 % (SF) of the pairs, i.e. MM1's waypoint lists. The
  last row is the finish line when it is not on an intersection (circuits:
  on the start road, ahead of the staggered grid). The car starts on the
  road from row 1 to row 2 (MM1 `aiGoalFollowWayPts::Reset` starts at the
  first waypoint equal to the intersection the car is heading for).
* `.aimap [Opponent]`: `car path.opp 0.86 0 50.0 0.7 1 1 1 1 0 1.0`. The
  first number is read as MM1's `MaxThrottle` (0.84–1.0, amateur lower than
  professional; **inferred**); the rest are unused.
* `.aimap [Police]` (MM2 layout, the header comment is MM1's): `vpcop x y z
  heading 0 15 0.5 50`. Only the position and heading are used; the four
  numbers are unknown (the first is 0 everywhere, read as "parked").

### Driving line (`Course`)

The waypoint intersections are joined by their roads (centre lines, with
the curbs of each section as the edges); across intersections the line is a
chord. Gaps between non-adjacent waypoints are filled with the shortest
route that avoids roads the line uses elsewhere (otherwise SF circuit7
doubles back along its next road); a start or finish off the waypoint roads
is joined along its own road (**inferred**: MM1's `LocateWayPtFromRoad` is
not decoded). Bends closer than 25 m (within 40 m) are merged into turns.

### Opponent (`aiVehicleOpponent`)

| Behaviour | Status |
|---|---|
| Goals: FollowWayPts, then Stop once finished, Backup while backing up | ported |
| Steering = clamp(atan2(d·m0, d·−m2), −1, 1) towards the target point | ported |
| Realism 0 unless the car hit the player; yaw momentum × 0.1 when within 0.1 rad of the target | ported (per-frame factors scaled by 30·dt as Open1560 does; full realism for 2 s after touching the player is **inferred**) |
| Damage: wrecked → no inputs, momentum × 0.95; circuits repair after 5 s | ported |
| Laps: loop the waypoints, then stop within 10 m of the last row (`PlanRoute`, flt_61BC94) | ported |
| `CalcSpeed`: for the next turn of deflection d (or the one after when \|d\| ≤ 0.5): R = max(W − sign·side, 0.5), r = R / (1 − sin((π − \|d\|)/2)), vmax = √(23.76 r), braking from r·cos(h) before it; brake only when (v − vmax)/(a·T) > 0.7, then angular momentum × 0.85; else MaxThrottle | ported; `a` is the car's measured braking deceleration instead of 23.76 (**inferred**, see below); full brake when already inside a turn too fast and W = narrowest road around the turn are **inferred** |
| Target point TargetPtOffset ahead (7–20 m, flt_61B26C/70), at DistToSide across | ported limits; speed term 7 + v²·0.488/23.76 **inferred** |
| DistToSide = the car's place across the road, inside the curbs less half width + 1.5 m | ported idea (DetermineOppMapComponent not decoded); looking ahead for narrowing roads **inferred** |
| Obstacles: blocked lateral ranges of cars ahead → nearest free gap, else follow (`DetectCollision`/`AvoidCollision`/`AddToBlockedRange`; skipped for vpsemi) | structure ported, details **inferred**; a stopped car is crept up to and nudged |
| `aiStuck` (0.3 s, 0.6 m, 1.0 m, 1.0 rad/s; Pegged = throttle > ¾ MaxThrottle, \|steer\| < 0.5) → full lock + throttle | ported |
| vehStuck fires → `aiGoalBackup`: reverse with steering −angle·20/7, throttle clamp(\|angle\|, 0.1, 0.85) for 3 s (5 s above 2 m/s), then brake to < 2 m/s | ported (aims at the line 8 m ahead instead of the nearest path vertex) |
| No progress for 10 s, or fallen 15 m below the line → put back on the line, further along each time it recurs, clear of cars and walls | **inferred** (OpenMM2 recovery) |
| Target behind a wall or median → pulled closer (needs `settings.world`) | **inferred** |
| Rubber-banding | none: MM1 has no code that scales opponents by position |

**Braking capability.** The opponents' `*_opp.vehCarSim` tunes are MM1-format
files. In the current physics they accelerate and brake far more weakly
than the player tunes (vpcoop_opp: 0→9 m/s in 9 s and 30→1 m/s in 16.6 s,
against 3 s and 0.8 s for vpcoop), so MM1's assumption of 23.76 m/s² of
braking sent them into corners far too fast. `BrakeMeter` measures what the
car achieves under braking and CalcSpeed uses that. If the physics port of
MM1-format tunes changes, this adapts automatically.

### Police (`aiVehiclePolice`, `aiGoalChase`, `aiPoliceForce`)

| Behaviour | Status |
|---|---|
| A suspect within 75 m (sym_63A3C8 = 75²), not a cop: hit me → chase; else if in view (±90°, line of sight 3.5 m above both cars) and speeding (> 70 mph when the cop's road has 4 lanes a side, else > 40 mph), collided this frame, or off the road → chase. The player is checked before the opponents | ported (`Context`, `Fov`, `Speeding`, `Collision`, `OffRoad` meaning **inferred**; "4 lanes" read per side) |
| MM1 also chases a stopped player in view | ported but off by default (`chaseStoppedPlayer`) — not confirmed for MM2 |
| `aiPoliceForce`: ≤ 3 cops per suspect, ≤ 3 suspects; State 3 (close in) for the nearest pursuer within 25 m, else 4 | ported |
| Follow at the suspect's speed, +10 m/s beyond 20 m; CopSpeedBoost 1.01 (throttle 1, < 50 m/s), CopSteerBoost1 0.5 (\|angle\| < 0.05), CopBrakeBoost 0.95 per 30 Hz frame | ported |
| Wrecked cop gives up | ported |
| Parked at the post until a suspect appears; road route (shortest path, replanned every 1.5 s) while far; straight at the suspect when within 40 m and in sight; the closing-in cop rams; escape beyond 150 m or after 5 s out of sight; then drive back and park | **inferred** (the 1000-line chase update, its Push/Block/Barricade behaviours and MM2's police numbers are not decoded) |
| Siren while chasing | ported (MM1 starts it with the chase) |
| Cruise cop density: round(count × density) posts, evenly spaced | **inferred** |

### Evidence (retail data, `test_game`)

* London circuit0, amateur, 7 opponents, 3 laps: all finish in 57–60 s;
  flying laps 12.5–15.5 s on a 412–449 m line (27–35 m/s), none more than
  1 m beyond a curb for longer than 0.7 s, no resets.
* London race0 with full ambient traffic (solid traffic bodies): all 4
  finish in 48.4–49.5 s over 945–973 m, no backups or resets.
* A car put nose-first against a wall: backs up after 3.2 s, is put back on
  its line at 10 s, and is 150 m along it at 30 s.
* A player-tuned car passing a parked London cop at up to 26 m/s: the chase
  starts at 6.8 s (reason: speeding); the suspect slows to 8 m/s at 10 s and
  the cop is within 8 m at 16.8 s, closing in. At 13 m/s no chase starts.
* Sweep of every circuit and checkpoint race, both cities and difficulties,
  one lap, all opponents (`OPENMM2_AI_SWEEP=1`): 487 of 517 opponents
  finish. Nearly all others end wrecked (point-to-point opponents stay
  wrecked in MM1), mostly at SF hairpins and jumps.

`OPENMM2_AI_TRAILS=<dir>` writes top-down plots of these runs (see the
header of `tests/game/test_opponent_race.cpp`).

### In the race (`app/RaceScreen`)

* Cars: opponents load the `_opp` tune, police the `_cop` tune when one
  exists (only `vpcop_cop.vehcarsim` in the retail data), otherwise the base
  tune. Cops stay at their posts for the whole race.
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
