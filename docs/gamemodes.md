# Game modes, race rules and HUD

Code: `src/game/session/` (`Session`, `RaceSetup`, `Gate`, `CopsAndRobbers`,
`Hud`). Tests: `tests/game/test_session.cpp`, `tests/game/test_session_hud.cpp`.

Sources, in order of weight:

* **MM2** (build 3393, via MM2Recomp): `mmGame`, `mmGameSingle`,
  `mmSingleRoam`, `mmSingleBlitz`, `mmSingleCircuit`, `mmSingleRace`
  (checkpoint race), `mmSingleStunt` (crash course), `mmGameMulti` and the
  `mmMulti*` modes, `mmMultiCR`, `mmWaypoints`, `mmWaypointObject`,
  `mmPositions`, `mmTimer`, `mmPlayer`; the HUD section names its own
  (`mmHUD` and its parts). Rules below name the function they come from.
* **MM2 data**: the string table, the race files, race help pictures
  (`jpg/race_*.jpg`, `lon_cc*.jpg`, `sf_cc*.jpg`).
* **Inferred**: marked as such.

## Integration (what the race screen does)

```cpp
SessionOptions opts;
opts.scoringBias = catalog.vehicle(config.vehicle)->scoringBias;   // tune/<car>.info
opts.lineOfSight = [&](Vec3 a, Vec3 b) { return !world.probe(a, b, hit); };
auto session = Session::create(config, cityData, vfs, strings, &error, opts);
car.reset(session->playerSpawn());
for (i : session->opponents())  spawn AI car i.vehicle at i.spawn, driving i.path
for (p : session->police())     spawn police
session->start();

each frame (dt):
  session->update(dt, playerState, opponentStates, policeStates);
  if (session->playerHeld())                 full brake, ignore throttle
  hold AI car i unless racersReleased() && opponentActive(i)
  for (Event e : session->takeEvents())
      Respawn              -> car.reset(session->respawnTransform())
      Restart              -> every car back to its start (player, opponents, police)
      DamageReset          -> repair the player's car (CarDamage::reset)
      PlayerDamageLimits   -> player MaxDamage = value, MedDamage = value / 2, ImpactThreshold = 0
      OpponentDamageLimits -> opponent index: MaxDamage = value, MedDamage = value / 2
      OpponentFinished     -> the AI car stops (aiGoalStop)
      CountdownReady/Set/Go, CheckpointCleared, LapCompleted, TimerWarning, ... -> sounds, voice
  music.setState(session->musicHint())
  if (session->finished())  -> makeFrontendScreen(ctx, session->result())
```

`PlayerState`: transform, velocity, speed, rpm, gear, throttle, damage,
`wrecked` (`mmPlayer::IsMaxDamaged`: CurrentDamage > MaxDamage), `inWater`,
impact counts, and the tune's `inertiaBox` (the checkpoint test's car size).
`OpponentState` per opponent / police car: transform, velocity, damage,
`currentDamage`, `inertiaBox`, `finished` (the AI reached the end of its
route, `aiRouteRacer::Finished`) and, for police, `pursuing` (chasing the
player, `aiPoliceOfficer::InPersuit`).

## Common rules

### Countdown

Each message is shown for 1.25 s (refreshed every frame); the first one
stays until 1.25 s of a wait are left, the second for 1.25 s, then "Go!"
releases the racers (`mmGameSingle::EnableRacers`) and starts the clocks.

| Mode | Wait | First / second / go (string ids) | Source |
|---|---|---|---|
| Blitz | 5 s (3.75 s + 1.25 s) | 158 / 159 / 160 | `mmSingleBlitz::UpdateGame` |
| Circuit | 2.5 s | 165 / 166 / 167 | `mmSingleCircuit::UpdateGame` |
| Checkpoint | 2.5 s | 179 / 180 / 181 | `mmSingleRace::UpdateGame` |
| Multiplayer Blitz / Circuit / Checkpoint | 2.5 s | 89-91 / 101-103 / 144-146 | `mmMulti*::UpdateGame` (after the host's start signal) |
| Cruise | none ("Go!" 154 in multiplayer) | | `mmSingleRoam`, `mmMultiRoam` |
| Crash course | see below | | `mmSingleStunt::Update*` |

There is no false start rule: the "Wait...5 second penalty" lines are wreck
penalties (below).

### Checkpoints

A waypoint list (`<race>waypoints.csv`, lesson `.csv`) is read by
`mmPositions::Load`: x, y, z, heading, radius (read with `atoi`, so
`12.9` is 12; 0 means 15 m), a hit flag (column 6) and editor columns.
`mmWaypoints::LoadCSV` / `ReInit` give a waypoint whose heading is exactly 0
the heading towards the next waypoint (from the second waypoint on; the
start keeps its own, and only circuits turn the last one towards the
first).

The gate is the segment `position ± radius·(cos h, sin h)` on (x, z)
(`mmWaypointObject::CalculateGatePoints`). `mmWaypointObject::LineIntersect`
intersects two lines in slope/intercept form (a segment with no x extent is
a vertical line through its first point; parallel lines never meet) and
accepts the point when it lies in both segments' bounding boxes grown by a
tolerance. `PlaneHit` tests, against the gate:

1. a segment given by the caller, tolerance `size.x`;
2. the car's vertical axis ± `size.y` (a point for a level car), tolerance `size.x`;
3. the car's lateral axis ± `size.x`, no tolerance.

The player (`mmWaypoints::Update` / `ClearWaypoint`) uses the segment from
the nose to 2 m behind the tail with `size` = (InertiaBox width / 2, height,
length / 2): the gate counts about 2.5 m before the nose reaches it. An
opponent (`mmWaypoints::AIWPHit`, `AnyWPHits`) uses one InertiaBox length
either side of the centre with the InertiaBox ×5 as `size`, so a 2 m wide
car hits a gate from 12-13 m away. `RadiusHit` (crash course jumps,
waypoints with the hit flag) is a 3D distance below the radius. There is no
"path since the last frame" test.

How the checkpoints are used (`mmWaypoints` type):

| Type | Modes | Rule | Shown |
|---|---|---|---|
| 1 | Circuit | in order from waypoint 1; waypoint 0 ends a lap; laps done = finished | all; a gate passed hides until the lap ends (`ResetAllTags`) |
| 2 | Checkpoint race | any order (`ClearWaypoint`: the first uncleared one hit); the finish (last) only counts after all others | all but the start; the finish appears when the others are cleared |
| 3 | Blitz | any order; the race ends when **all** are cleared (no separate finish; the last row is a checkpoint like the others) | all but the start |
| 4 | Crash course jump | as 2; waypoints with the hit flag clear by radius | all but the start |
| 5 | Other lessons | strictly in order; passing the last ends the event | all but the start |

The HUD counts against waypoints - 1 in every mode (`mmWPHUD::Init` from
`mmSingleBlitz` / `mmSingleRace::InitHUD`, `mmCircuitHUD::SetWPCleared`);
a checkpoint race's finish counts as the last one.
The lesson column "chkflags" bit 0 shows only the current target. The
arrow points at the current target (`mmWaypoints::SetArrow`); in any-order
modes it changes only when a checkpoint is cleared
(`GetClosestWaypoint`: the nearest shown, uncleared one, by 3D distance).
Checkpoint races can cycle the target (`GetNextWaypoint` /
`GetLastWaypoint`, `Session::cycleTarget`; Blitz's input handler tests the
checkpoint race's waypoint type and so never cycles).

### Start

The player starts on the first waypoint facing its heading
(`mmWaypoints::GetStart` / `GetStartAngle`); the driving direction of
heading h is `(sin h, 0, -cos h)`. Cruise starts 2 m above a random AI
intersection (not the first) that is not in an underground, road or
building room and joins no freeway or alley (`mmGame::RespawnXYZ`; the path
flag bits follow mm2hook's naming), facing -Z. Opponents start on the first
row of their `.opp` line, turned by its fourth column (degrees × 0.017444445,
not negated as the player's start angle is; `aiRouteRacer::Init`); the last
row is where the AI finishes.

### Water, falling out of the city, wrecks

`mmGame::Update`: in the water, the message is "More tea, vicar?" (642) in
London, "Sleep with the fishes!" (643) in San Francisco, 30 elsewhere (3 s);
after 5 s in the water (the time adds up across dips) the mode's
`HitWaterHandler` runs. Below y = -50 the mode's `DropThruCityHandler` runs
(single player: `mmGame::Reset`, the race starts over; multiplayer: as the
water). String 29 ("That didn't happen!") only goes to a debug print.

| Mode | Water (after 5 s) | Wrecked (`IsMaxDamaged`) |
|---|---|---|
| Cruise | the game restarts (`mmSingleRoam::HitWaterHandler`) | held 3 s, then repaired (`mmSingleRoam::UpdateGame`) |
| Blitz | race lost, menu 0.5 s later | race lost: "Game over!" (162) |
| Checkpoint | race lost, menu 0.5 s later | race lost: "Game over!" (182) |
| Circuit | back at the last checkpoint cleared, facing its heading | held 5 s with "Wait...5 second penalty" (168), then repaired |
| Crash course | event failed (unless already over) | per event, below |
| Multiplayer | back at the last checkpoint (cruise: the start) | held 5 s (92 / 104 / 147 / 155), then repaired |

OpenMM2 stops checking water and falls once the race is over.

### Messages

`mmHUD::SetMessage(text, seconds, flag)` replaces the one message (and
clears the second line); `SetMessage2` sets a line under it that lasts as
long. The flag picks the placement: countdowns, the water lines, the
finish lines ("You Won!", "You finished Nth", lesson passes; "You are a
loaf" is 1 in circuits, 0 in checkpoint races) and most lesson lines use 1;
"Time's up!", "Game over!", penalties, opponents finishing and the lap
lines use 0 (each call is listed with the rule that makes it).
`Session::message()` / `message2()` carry this (`HudMessage::top` = flag 1).
Times are printed by `GetLocTime` as `M:SS:HH` (hundredths, rounded by
+0.005 then truncated; "  ---  " for none).

### Finish and results

The car brakes once the race is over (`mmPlayer::Update`, +0x2258). Results
follow 5 s after the end (`UpdateGame` states 4/5 with a 5 s wait); a lost
race goes to the menu instead in the original. Multiplayer shows them 3 s
after the finish (Blitz: when the clock would have run out).

**Winning** (`mmSingleCircuit::ProgressCheck`, `mmSingleRace::ProgressCheck`):
places 1-3 for amateurs, 1st for professionals, hard-coded; MM2 does not
read `MustPlace` from the `.cinfo`. A Blitz finished in time is won.
**Score** (`mmGame::CalculateRaceScore`): the car's ScoringBias and the race
table's Difficulty column (1 in every retail row), both truncated to
integers, times 50 / 25 / 10 for 1st / 2nd / 3rd (Blitz counts as 1st).
MM2 records the result only when the race was driven with its table
settings (cops, traffic, time of day, weather; circuits also laps and
opponents) and no cheat was used (`RegisterFinish`); OpenMM2 leaves that to
the profile (not done yet).

## Modes

| Mode | Rules | Source |
|---|---|---|
| Cruise | free roam, no clock; see the table above | `mmSingleRoam` |
| Blitz | checkpoints in any order against the clock (`mmblitzdata.csv` TimeLimit); beeps once a second below 10 s, continuously below 3 s (`PlayTimerWarning`). At 0 "Time's up!" (161) shows but **the race goes on**: reaching the last checkpoint after that loses ("Time's up!" again), in time wins ("You Won!", 164) | `mmSingleBlitz::UpdateGame` |
| Checkpoint | checkpoints in any order, then the finish, against opponents | `mmSingleRace::UpdateGame` |
| Circuit | waypoints in order; waypoint 0 completes a lap; "Final lap!" (62) or "Lap time" (63) for 1 s with the lap time (`M:SS:HH`) under it; the time limit column is not used | `mmSingleCircuit::UpdateGame`, `mmWaypoints::Update`, `mmHUD::PostLapTime` |
| Crash Course | lessons of one or two events (`crash<N>data.csv`), below | `mmSingleStunt` |
| Multiplayer races | as above, but wrecks cost 5 s; Blitz ends at time-up | `mmMultiBlitz`, `mmMultiCircuit`, `mmMultiRace` |
| Cops & Robbers | multiplayer only; `CopsAndRobbers` holds the rules | `mmMultiCR` |

**Opponents** (`UpdateOpponentStatus`): each counts waypoints passed (from
1): circuits test the next one in order with `AIWPHit`, checkpoint races
the first unpassed one within 50 m (`AnyWPHits`). An opponent is finished
when its AI says so (`aiRouteRacer::Finished`), not by a gate; the finish
order gives the places, and while the player races the HUD shows its name
(13-20) over "finished Nth" (21-28) for 5 s.
**Place** (`UpdateScore`): 1 + the opponents that have passed more
waypoints, have finished, or have passed as many and are nearer the
player's current target than the player (3D).

### Crash Course

`mmSingleStunt::LoadEventFile` reads `crash<N>data.csv` (one row per
event): file, Event, Checkpoints (0 = no checkpoints), TimeLimit,
AmbDensity, then three extras, named "cornerspeed, chkflags, numopp" in the
header of `crash6data.csv`: the minimum speed (MinimumSpeed events use 50
below 1), checkpoint flags (bit 0: only the next checkpoint is shown) and
the number of AI cars the event uses. A fourth extra is not read. Each
event's AI cars follow those of the earlier events (`GetOpponentIndex`);
"Go" enables cars from that offset up to the event's count
(`EnableRacers`; with the retail data that is always the first `numopp`).
Police from the lesson's `.aimap` drive in every event.

Countdown (first event): the lesson's name (string 532 + 13 for San
Francisco + lesson) on the upper line until 1.25 s have passed, then the
event's first line for 3.75 s, its second for 1.25 s and its "go" line
(Course and Map: 1.25 s each). A later event of an exam starts at once
where the car is: no respawn, no countdown (`InitNewEvent`), and the clock
restarts with the event's limit.

| Type | Update | Countdown lines | Rule | Pass | Fail |
|---|---|---|---|---|---|
| 0 Jump | `UpdateJump` | 206 / 207 / 208 | checkpoints in any order (type 4), the last one last, against the clock | 209 | time 614, wreck 615 |
| 1 Collide | `UpdateCollide` | 210 first / 211 / 212 / 213 | in order against the clock; a wreck costs 5 s (216); nothing is recorded; not in the retail data | (215) | time 214 |
| 2 Follow | `UpdateChase` | 218 / 219 / 220 | when the car arrives (its AI finishes, or the checkpoints end) the player must be within 10 m; no clock; a wreck waits 3 s and repairs | 221 | farther than 100 m: 222 + 223 |
| 3 Evade | `UpdateEvade` | 193 / 194 / 195 (later event: 652, 3 s) | checkpoints in order against the clock; at the end no cop may be pursuing in sight within 200 m (`CheckCopPursuit`: 3.5 m above both cars); a wreck waits 3 s and repairs | 196 | pursued 197, time 198 |
| 4 MinimumSpeed | `UpdateCorner` | 232 ("Maintain %.0f", 2.5 s) / none / 233 | reach the speed before the first checkpoint, then never stay below it for more than 1 s; clock only when TimeLimit is not 0; pursuit as Evade | 234 | not up to speed 235 + 236, too slow 238 + 239, wreck 240, time 244, pursued 197 |
| 5 Clean | `UpdateFrogger` | 225 / 226 / 227 | from the second line MaxDamage is 10 (MedDamage 5, ImpactThreshold 0): any damage wrecks the car | 229 (1.25 s, results after 3 s) | wreck 230, time 228 |
| 6 Acceleration | `UpdateAccel` | 199 first / 200 / 201 / 202 | in order against the clock; result 2 s after the end; not in the retail data | 204 | time 203 |
| 7 Course, 9 Map | `UpdateBlitz` | 241 / 242 / 243 | checkpoints in order against the clock | 609 | time 244, wreck 245 |
| 8 Destroy | `UpdateStop` | 617 / 618 / 619 | at "Go" the target's MaxDamage becomes 150000 (MedDamage 75000); wreck it (CurrentDamage ≥ 150000) before its AI reaches the end; a wreck waits 3 s and repairs | 620 | arrived 621 + 622, time 244 |

The speed is compared in the player's display units in the original
(speed × MetricFactor); OpenMM2 uses mph (inferred equivalent with
imperial units). Only the last event's pass is recorded
(`RegisterFinish(1)`); string 610 ("You've passed the final") is not used
by the game. Falling into the water fails the event 0.5 s later.

### Cops & Robbers (rules only)

`mmMultiCR`, in `CopsAndRobbers`:

* **Places**: `race/<city>/multicopwaypoints.csv` is a pool of places (at
  least 3; the last row is never picked). Each set, at the start and after
  every delivery, puts the gold, then the bank, then the hideout on random
  different places (`GetNewSet`, `GetRandomPoints`); each pick is an AI
  intersection or a pool row with equal chance. A `<city>sets.csv` with
  fixed sets would take priority; no retail city has one.
* **Teams**: team 0 delivers to the bank (cops; blue in Robber Teams, whose
  bases are `pt_blue` / `pt_red`), team 1 to the hideout (robbers; red).
  Free-For-All puts police cars in team 0.
* **Gold**: picked up within 5 m (+25 points); the carrier gets the gold's
  mass (0 / 100 / 200 kg for the three gold weight options) and, above
  first gear, a throttle cap of 1 / 0.9 / 0.81 (`FondleCarMass`,
  `mmGame::UpdateSteeringBrakes`). A hit of impulse 250 or more from
  another player's car makes the carrier drop it where it is (on a road;
  elsewhere it returns to the set's place); the carrier cannot take it back
  for 2 s. A wrecked carrier drops it and sits out 5 s ("Wait...5 second
  penalty!", 114); water or falling out sends it back to its place.
* **Delivery**: within 12 m of the team's base, +100 points, the car is
  repaired, and a new set follows.
* **Scores and limits**: team scores are the sum of the members'. Time
  limits warn as 20, 15, 10, 5 and 1 minutes are passed (138-142) and end
  below 0.1 s; point limits end the game when a player (Free-For-All) or a
  team reaches them.

## HUD

Art is the full-size HUD set drawn at half size into the 640×480 UI space,
as if authored for 1280×960 (the `*_half` variants are exactly half). Layout
is **inferred** from the art and `tune/<city>.mmhudmap`:

| Element | Art | Placement |
|---|---|---|
| Instrument cluster | `speed.tga`; speed in `digitac_<n>_half.tga` (fits the left window), gear `digitac_gear_<r/n/d/1-8>.tga` (fits the right window exactly), tachometer LEDs `tacometer ticks_half.tga` stretched into the top slot up to rpm/MaxRPM, `mph.tga` (stored upside down) | bottom left, at the screen edge |
| Damage meter | `damage_lable.tga`, `damage.tga` cropped by damage | above the cluster |
| Clock | `digi_<n>.tga`, `digi_colon.tga`, hundredths `digi_<n>_half.tga` | top right: remaining time (Blitz, lessons), lap time (circuit), race time |
| Place / Check / Lap | strings 254/255/259-261, fonts 251/252 | top left |
| Messages | fonts 562 (upper line) and 564 (centre line) | centre |
| Arrow | `hudarrow01` (green), `hudarrow_blitz01` (red), `hudarrow_cc01` (violet); paint job 1 (yellow) when the target is behind | MM1 `mmArrow`: camera space (0, 2, -6.1), turned toward the target, tilted 20°, 50 % transparent, drawn over the scene |
| Checkpoint stands | `pt_check`, `pt_finish` (paint job 1 = crash course `CCStand`): an arch of two posts and a CHECKPOINT / FINISH banner, 2×1 units | one per gate, scaled by the waypoint radius to span it, banner toward the approaching driver (inferred; MM1's `mmWaypointInstance` likewise carries the radius). Drawn lit and back-face culled: the banner's two sides are coplanar |
| Map | `hudmap_<city>.pkg` (flat world-space mesh with the map textures), `hudmap_square` icons (paint jobs: red, blue, green, grey, yellow, finish, gold, bank, hideout), `hudmap_tri` car arrows | `mmhudmap` Pos/Size (fraction of the screen), Ocean Color background, zoom between ZoomInDist/ZoomOutDist easing at Approach Rate, icon size IconScale (read as metres), full-screen variants *FS |
| Dashboard | `<car>_dash.pkg` parts dash / roof / wheel / needles, `tune/<car>_dash.asnode` | camera space; needles turn `RotMin + value/max·(RotMax-RotMin)` (MM1 `RadialGauge`); pivots inferred |

HUD fonts: the string table's font entries carry two sizes; the first is
used as the cell height at 640×480 (the second would fill the screen).
