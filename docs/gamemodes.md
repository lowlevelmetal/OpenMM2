# Game modes, race rules and HUD

Code: `src/game/session/` (`Session`, `RaceSetup`, `Gate`, `CopsAndRobbers`,
`Hud`), the race loop in `src/app/RaceScreen.cpp`, the in-race keys in
`src/app/Controls.{h,cpp}`. Tests: `tests/game/test_session.cpp`,
`tests/game/test_session_hud.cpp`, `tests/game/test_parity_session.cpp`,
`tests/app/test_parity_controls.cpp`. The function-by-function audit is
`docs/parity/session.md`.

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

each frame (dt), in MM2's order:
  player input (mmPlayer), ambient traffic and pedestrians, then the
  opponents and police (aiMap::Update), then the physics step;
  session->update(dt, playerState, opponentStates, policeStates);   // after the step: the rules
                                             // see the cars one physics step late, as MM2's do
  switch (session->playerHold())
      Undrivable  -> vehCar::SetDrivable(0, 1): brakes on, neutral, the throttle revs
                     (before "Go!", wreck penalties, a wreck ending, any multiplayer ending)
      FinishBrake -> mmPlayer +0x2258: brakes on, wheel turned full left (most endings)
      None        -> drives (also after the water)
  hold AI car i unless racersReleased() && opponentActive(i)
  for (Event e : session->takeEvents())
      Respawn              -> car.reset(session->respawnTransform())
      Restart              -> every car back to its start (player, opponents, police)
      DamageReset          -> repair the player's car (CarDamage::reset)
      PlayerDamageLimits   -> player MaxDamage = value, MedDamage = value / 2, ImpactThreshold = 0
      OpponentDamageLimits -> opponent index: MaxDamage = value, MedDamage = value / 2
      OpponentFinished     -> nothing: the game only asks aiRouteRacer::Finished, the car drives on
      Sound                -> the mode's 2D sound (GameSound: play once, loop, stop)
      Speech               -> the announcer (SpeechCue)
      CountdownReady/Set/Go, CheckpointCleared, LapCompleted, TimerWarning, ... -> other listeners
  session->engineSilenced()  -> vehCarAudioContainer::SilenceEngine
  session->damagedOut()      -> the music stops at once (StopSegment(1))
  music.setState(session->musicHint())
  scene pass:   world..., hud.drawWorld(*session, camera, ps, steering, blips);
                hud.drawMap(*session, ps, blips, dt);          // last in the pass
  overlay pass: hud.drawOverlay(overlay, text, uiTextures, *session, ps);
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

Circuits and checkpoint races start the countdown only once the pre-race
camera has finished (`UpdateGame` state 0 waits for mmPlayer +0xE5A; Blitz
does not), and the multiplayer races only after the host's start message
(OpenMM2: 2.5 s before the shared start time). Each countdown line plays
"Startracelow", "Go!" plays "Startracehigh".

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
checkpoint race's waypoint type and so never cycles; the crash course's
any-order events move their target only when it is cleared).

The finish stand (`pt_finish`) is waypoint 0 of a circuit and the last
waypoint of a checkpoint race; Blitz and the crash course use `pt_check`
everywhere (`mmWaypoints::LoadCSV`). Every cleared checkpoint shows the race
time (circuits: the time since the lap started) for 1 s at placement 0,
except in the crash course (`mmHUD::ShowSplitTime` from
`mmWaypoints::DisplayHUDMessage`), and plays "Waypoint" (the crash course
always; races not for the checkpoint that leaves only the finish;
"Lastwaypoint" for a completed lap).

### Start

The player starts on the first waypoint facing its heading
(`mmWaypoints::GetStart` / `GetStartAngle`); the driving direction of
heading h is `(sin h, 0, -cos h)`. Cruise starts 2 m above a random AI
intersection (not the first) whose room's level flags (lvlRoomInfo, not
the PSDL bytes) mark neither subterranean (0x0A), deep water (0x04) nor a
landmark (0x20) and that joins no freeway or alley (`mmGame::RespawnXYZ`;
the path flag bits follow mm2hook's naming), facing -Z. In a multiplayer
race each player takes a slot of `mmGameMulti::StartXYZ`'s grid behind the
start: 2.25 m either side 6 m back, then 4.5 m to the sides and 6 m ahead,
or for cars with a trailer or a radius over 6 m 2.75 / 5.5 m to the sides
16 and 34 m back (the slot is taken as the player's id, inferred).
Multiplayer cruise draws its intersection with the player's id as the
seed. Spawn points drop onto the ground with the wheels' probe. Opponents start on the first
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

OpenMM2 stops checking water and falls once the race is over. A race
or Blitz lost to a wreck silences the engine and stops the music at once
(`SilenceEngine`, `StopSegment(1)`), as the checkpoint race's water does
for the engine; the modes' Reset turns it back on.

### Messages

`mmHUD::SetMessage(text, seconds, flag)` replaces the one message (and
clears the second line); `SetMessage2` sets a line under it that lasts as
long. The flag picks the placement: countdowns, the water lines, the
finish lines ("You Won!", "You finished Nth", lesson passes; "You are a
loaf" is 1 in circuits, 0 in checkpoint races) and most lesson lines use 1;
"Time's up!", "Game over!", penalties, opponents finishing and the lap
lines use 0 (each call is listed with the rule that makes it).
`Session::message()` / `message2()` carry this (`HudMessage::top` = flag 1).
`mmHUD::Update` counts the time down after the frame's `UpdateGame` and
clears both lines once it is below zero.
Times are printed by `GetLocTime` as `M:SS:HH` (hundredths, rounded by
+0.005 then truncated; "  ---  " for none).

### Finish and results

What the game does to the player's car depends on the ending: most set
mmPlayer +0x2258 (brakes on, wheel full left); a wreck that ends a race,
Blitz, jump or course lesson calls `vehCar::SetDrivable(0, 1)` (brakes on,
neutral); the water and the Clean and minimum-speed lessons' wrecks set
neither; the multiplayer modes brake with the throttle tapering off and
then call `SetDrivable(0, 1)` (OpenMM2: undrivable at once). Results
follow 5 s after the end (`UpdateGame` states 4/5 with a 5 s wait); a lost
race or lesson opens the in-race main menu without pausing
(`mmPopup::ProcessEscape(0)`, see below). Multiplayer shows them 3 s
after the finish (Blitz: when the clock would have run out). The race goes
on behind the results in MM2 (opponents still finishing are added); OpenMM2
shows the results as a frontend page, with the opponents that finished by
then.

**Winning** (`mmSingleCircuit::ProgressCheck`, `mmSingleRace::ProgressCheck`):
places 1-3 for amateurs, 1st for professionals, hard-coded; MM2 does not
read `MustPlace` from the `.cinfo`. A Blitz finished in time is won.
**Score** (`mmGame::CalculateRaceScore`): the car's ScoringBias and the race
table's Difficulty column (1 in every retail row), both truncated to
integers, times 50 / 25 / 10 for 1st / 2nd / 3rd (Blitz counts as 1st).
MM2 records the result only when the race was driven with its table
settings (cops, traffic, time of day, weather; circuits also laps and
opponents) and no cheat was used (`RegisterFinish`); OpenMM2's frontend
makes the same check when it records the result after the race
(`Progress::recordable`).

## Modes

| Mode | Rules | Source |
|---|---|---|
| Cruise | free roam, no clock; see the table above | `mmSingleRoam` |
| Blitz | checkpoints in any order against the clock (`mmblitzdata.csv` TimeLimit); beeps once a second below 10 s, continuously below 3 s (`PlayTimerWarning`). At 0 "Time's up!" (161) shows but **the race goes on**: reaching the last checkpoint after that loses ("Time's up!" again), in time wins ("You Won!", 164) | `mmSingleBlitz::UpdateGame` |
| Checkpoint | checkpoints in any order, then the finish, against opponents | `mmSingleRace::UpdateGame` |
| Circuit | waypoints in order; waypoint 0 completes a lap; "Final lap!" (62) or "Lap time" (63) for 1 s with the lap time (`M:SS:HH`) under it; the time limit column is not used | `mmSingleCircuit::UpdateGame`, `mmWaypoints::Update`, `mmHUD::PostLapTime` |
| Crash Course | lessons of one or two events (`crash<N>data.csv`), below | `mmSingleStunt` |
| Multiplayer races | as above, but wrecks cost 5 s; Blitz ends at time-up; the finish shows the player's name over "finished in M:SS:HH" | `mmMultiBlitz`, `mmMultiCircuit`, `mmMultiRace` |
| Cops & Robbers | multiplayer only; `CopsAndRobbers` holds the rules | `mmMultiCR` |

**Opponents** (`UpdateOpponentStatus`, at the end of every `UpdateGame`
from "Go!" on, also after the player's finish): each counts waypoints
passed (from 1): circuits test the next one in order with `AIWPHit`,
checkpoint races the first unpassed one within 50 m (`AnyWPHits`). An
opponent is finished when its AI says so (`aiRouteRacer::Finished`), not by
a gate, and drives on to its destination; the finish order gives the
places, its time is mmHUD's second timer (+0xA24), which the player's finish
does not stop, and while the player races the HUD shows its name (13-20)
over "finished Nth" (21-28) for 5 s with "Messagenote".
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
where the car is: no respawn, no countdown (`InitNewEvent`): state 0
enables its cars and "Go" follows one update later, and the clock restarts
with the event's limit. The "race over" flag the earlier event set stays
for the rest of the lesson (only `mmGame::Reset` clears it): the water no
longer fails it and the Clean lesson's clock is no longer checked.
`aiMap::Init` loads at most OpponentDensity cars, which the crash course
sets to 8 (`CrashCourse::SetEnvironment`); the traffic density is the last
event's AmbDensity. chkflags counts when not 0 (`InitNewEvent`).

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

### Cops & Robbers

`mmMultiCR`, in `CopsAndRobbers` (rules) and the race screen (network,
HUD, car):

* **Start**: no countdown; "Go!" (113) for 2 s at the top with
  "Startracehigh". Players start at random intersections drawn from a
  stream seeded with their id (`mmGame::RespawnXYZ`).
* **Machines**: each machine runs the rules for its own car and tells the
  others (`CopsAndRobbers::updateNetwork` / `receive`): a client that
  reaches free gold asks the host (0x25e) and the gold waits; the host
  grants it (0x25a); the carrier's machine reports a drop (0x259) and a
  delivery (600); the host draws the next set and sends its places (0x261).
  OpenMM2 seeds the first set with the shared start time.

* **Places**: `race/<city>/multicopwaypoints.csv` is a pool of places (at
  least 3; the last row is never picked). Each set, at the start and after
  every delivery, puts the gold, then the bank, then the hideout on random
  different places (`GetNewSet`, `GetRandomPoints`); each pick is an AI
  intersection or a pool row with equal chance. A `<city>sets.csv` with
  fixed sets would take priority; no retail city has one.
* **Teams**: team 0 delivers to the bank (cops; blue in Robber Teams, whose
  bases are `pt_blue` / `pt_red`), team 1 to the hideout (robbers; red).
  Free-For-All puts police cars in team 0.
* **Order** (`mmMultiCR::UpdateGame`): the impact drop (`ImpactCallback`,
  during the physics step), the wreck lock-outs, `UpdateLimit` (time up
  below 0.1 s, then the point limit), `UpdateTimeWarning`, `UpdateGold`
  (the carried gold rides 2 m above the carrier), `UpdateBank` /
  `UpdateHideout`: a delivery's points meet the limit in the next frame.
  Nobody takes the gold in the frame it was knocked loose (each machine
  tests its own car and learns of the drop by message; inferred).
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
  team reaches them ("Time's up!" 118, "Point limit reached" 119); the
  results follow 3 s later with the car braked (state 9, +0x2258).
* **Car**: regeneration while not carrying (`mmPlayer::UpdateRegen`); the
  gold's mass and throttle cap while carrying; a repair at a delivery.
  The throttle cap applies in a forward gear in every network game (1
  unless carrying).
* **HUD**: "You have the Gold!" (115 / 134), "<name> has the Gold!" (135),
  "You dropped the gold!" (112), "<name> dropped the Gold!" (136), "Gold
  delivered!" (117), "<name> delivered the Gold!" (137). The gold spins at
  3 rad/s 1.5 m above its place (`mmPowerupInstance`); the bases are
  billboards 12 x 7.5 x 12 (`mmBillInstance`); the arrow points at the gold,
  or at the carrier's base. `mmCRHUD` (its corner at the top left,
  inferred): in team games "COPS" / "ROBBERS" or "BLUE" / "RED" in blue and
  red (font string 262) with the team totals under them; then the player's
  name (blue, red on team 1) and score, and the roster of the other players
  in their colours with their scores and "$" (string 268) by the gold
  carrier. The numbers are yellow Gill Sans MT (20 and 16 pixels). While
  the player carries the gold a `wpobj_gold` spins (0.05 rad a frame) 5.5 m
  up and 13.1 m ahead of the camera. The time limit's clock is top centre.
  The announcer's Cops & Robbers lines are loaded but build 3393 never
  plays them.

## HUD

Code: `src/game/session/Hud.{h,cpp}`; the pure parts (clock and lap time
formats, gauges, arrow, stands, map camera and rectangle, per-mode rules)
are free functions in `game::session::hud` with tests in
`tests/game/test_session_hud.cpp`. Verified against MM2's own classes
(MM2Recomp, build 3393); the evidence column names them.

**Screen space.** MM2 draws the 2D HUD in screen pixels: bitmaps 1:1 (the
full-size art from 640 pixels wide, the `*_half` art below), fonts at the
second size of their string-table entry (the first below 640;
`mmText::CreateLocFont`), and text nodes placed at fractions of the screen.
OpenMM2 lays the HUD out in the 640×480 virtual space, i.e. as the game looks
at 640×480, and scales it to the window (`HudOptions::pixelSize` picks another
reference resolution, 0.5 = 1280×960). Elements MM2 anchors to the screen
edges use the edges of the whole output (`UiLayout::left/right`), so they
stay in the corners of widescreen windows; fractions of the screen are
fractions of the whole output.

| Element | MM2 behaviour | Evidence |
|---|---|---|
| Instrument cluster | Origin at the left edge, 100 px above the bottom. Painted in order: damage meter, gear, tachometer, speed. Hidden in the dashboard view. `speed.tga`, `mph.tga` and `tacometer ticks_half.tga` are not used by the game | `mmExternalView::ResChange`, `::Cull` |
| Damage meter | At (8, 88): a window as wide as `speed_ticks.tga` (129 px) slides across the 500 px `damage.tga` colour bar, offset = damage / max × (500 − 129); `damage_lable.tga` drawn over it | `mmSlidingGauge::Draw` |
| Tachometer | `speed_ticks.tga` at (8, 41), drawn from the left up to rpm / MaxRPM of its width | `mmLinearGauge::Draw` |
| Gear | `digitac_gear_<x>.tga` at (16, 46): `r` reverse, `p` neutral (the art shows N), `d` any forward gear of an automatic, else the gear number | `mmGearIndicator::Draw` |
| Speed | `digitac_<n>.tga` (41×56) at (19, −14), (19 + w + 1), (19 + 2w + 1); speed truncated; leading zeros not drawn. Always mph (`MetricFactor` 2.23605 is never changed); km/h is an OpenMM2 option | `mmSpeedIndicator::Draw` |
| Clock | Top centre: eight colour-keyed bitmaps `digi_<n>.tga` / `digi_colon.tga` (black is transparent; the grey unlit segments show) reading MM:SS:HH (time + 0.005 s, minutes modulo 100), starting three digits and a colon left of the centre. Count-down in Blitz and the crash course, race time in checkpoint and circuit races (not the lap time), none in cruise and Cops & Robbers, follow lessons, or minimum-speed lessons without a time limit. Stays when the HUD is toggled off | `mmHUD::Update`, `::Cull`, `::Init`, `mmSingleStunt::InitHUD` |
| Place / Check | Labels in light green (0.5, 1, 0.5), numbers white, no shadow, flush with the left edge; label font string 253, numbers 251 (both Gill Sans MT bold, 22 px). Checkpoint races: Place at 3.5 % of the height, Check at 8.5 %; Blitz and the crash course: Check at 3.5 %, hidden in follow and destroy lessons | `mmWPHUD::Init`, `mmSingleRace::InitHUD`, `mmSingleBlitz::InitHUD` |
| Circuit readouts | Place, Check, Lap at 3.5 / 8.5 / 13.5 % (strings 259-261, fonts 258 / 256); one row per completed lap from 18.5 %, 5 % apart: "1." and the lap time as M:SS:HH (`GetLocTime`), the time starting at the width of "10.  " | `mmCircuitHUD::Init`, `::SetLapTime` |
| Clean lessons | "Hit Objects:" at 14 %. The "Hit Vehicles:" line is created at 19 % but never added to the HUD, so it never shows | `mmCollideHUD::Init` |
| Messages | Gill Sans MT 36 px (string 60), yellow, centred across the screen, word-wrapped, with a (15, 15, 15) shadow offset by 1/18 of the text height. A message box 15 % tall at 80 % of the height, or at 20 % for "upper" messages (`SetMessage` mode 1: countdown, water); MM2's second line sits at 87.5 % / 35 %. While the 3D view does not start at the top of the screen (wide angle, full-screen map) both boxes move to 5 % / 10 %. `HudMessage::top` selects the upper box | `mmHUD::mmHUD`, `::Update`, `::SetMessage`, `mmTextNode::RenderText` |
| Arrow | `hudarrow01` in every mode (the `_blitz` and `_cc` models are unused); camera space (0, 2.5, −6.1); points at the target taken at the camera's height (the vertical component is kept, the basis is not renormalised); tilted −20° about X; unlit, opaque, no depth test; paint job 1 (yellow) while the target is behind. None in cruise and circuits, nor in follow, destroy and map lessons; off after a Blitz is over. Drawn in the dashboard view too (under the dash) | `mmArrow::mmArrow`, `::Update`, `mmHUD::Init`, `mmSingleStunt::InitHUD`, `mmSingleBlitz::Update` |
| Checkpoint stands | `pt_check` / `pt_finish` (2 × 1 units), rotated by −heading about Y, scaled by (radius, 7.5, radius) and centred 3.75 m above the waypoint, so the arch spans the gate and stands 7.5 m tall; pre-lit (no lighting); crash course stands use paint job 1 (`CCStand`) | `mmWaypointObject::mmWaypointObject`, `mmCheckpointInstance::Init`, `::Draw`, `mmSingleStunt::InitNewEvent` |
| Opponent icons | "Opponent Position" (on for new players): a violet triangle facing the camera, 2 m wide, 4 m tall, tip 4 m above each opponent, drawn over everything. In checkpoint races and circuits each opponent's place (`UpdateScore`) shows above it as a digit of `opp_icon.tex` on a 4 × 4 m card 13 to 17 m up (the 4 m lift is scaled with the size there); the icons are drawn by place in as many passes as there are opponents, places over 7 in the first, so an opponent placed past the passes is not drawn. Network players: coloured by slot (blue, green, red, yellow, orange, violet, cyan, pink; red / blue by team in Cops & Robbers, the gold carrier marked "$"), their names in that colour with a dark outline (font string 48) over the cars within 300 m. In Blitz cyan cards mark the checkpoints still to clear, growing from 1.9 near the camera to 4.1 at 300 m (their number labels are registered but only network games draw labels); Blitz and the follow and destroy lessons force the icons on | `mmIcons::Cull`, `::RegisterOpponents`, `mmGame::mmGame`, `mmSingleRace::UpdateScore`, `mmGameMulti::RegisterMapNetObjects`, `mmSingleBlitz::InitHUD`, `::Update`, `mmSingleStunt::InitHUD`, `mmPlayerConfig::DefaultViewSettings` |
| Map: placement | Small: (Pos.x × W, Pos.y × H), size (Size.x × W − 10, Size.y × H − 10); at the left edge for right-hand-drive cars (vehicle Flags 0x40) while the dashboard is on. Split: bottom half, the 3D view in the top half. Full screen: whole screen, drawn before the level, the 3D view in (Pos × screen, Size × screen) on black. No frame | `mmHudMap::SetMapMode`, `mmGameManager::Cull` |
| Map: camera | Perspective, 60° vertical field of view, aspect 1.25 (2.5 split) regardless of the viewport's shape, near 10, far 1600; above the car at an absolute height = the zoom distance. Rotating: the car's heading up; otherwise −Z up and +X right. Zoom and icon size move linearly at (out − in) × 1.2 per second toward the Map Zoom setting (FS values in full screen) and snap on mode changes. "Approach Rate" and "Ocean Color" in the `.mmhudmap` are never read (datParser names are one token). OpenMM2 widens the horizontal field on non-4:3 outputs | `mmHudMap::mmHudMap`, `::FileIO`, `::Cull`, `::SetMapMode` |
| Map: look | Cleared to a hard-coded ocean colour: London (0.92, 0.84, 0.778), elsewhere (0.084, 0.68, 0.92) (the `.mmhudmap` Ocean Color is overwritten); `hudmap_<city>.pkg` unlit | `mmHudMap::Init` |
| Map: icons | Car arrows are flat untextured triangles (0, 0, −1), (±0.7, 0, 1) × icon scale, 15 m above the car: police (red) while chasing, opponents (violet; network players IconType slot + 4: red, yellow, orange, violet, cyan, pink, and two colours read past the table for the last two slots), the player (yellow) over a black one 1.3 times larger; traffic is not shown; `hudmap_tri` is loaded but unused. Waypoints are `hudmap_square` × icon / 7.51, 10 m above: green to clear, yellow the current goal, grey cleared (circuits), the finish dot for the open finish (checkpoint races) or the start line (circuits) | `mmHudMap::DrawIcon`, `DrawColoredTri`, `::DrawPlayer`, `::DrawCops`, `::DrawOpponents`, `::DrawWaypoints`, `::DrawIndicator` |
| Dashboard | `<car>_dash.pkg` in camera space (DashPos, RoofPos), unlit, no depth test, painted dash, roof, gear indicator, speed, tach and damage needles, dash_extra, wheel. Needles turn by −angle about Z around (box centre of `<car>_dash_<part>.mtx` + PivotOffset) and are moved by Speed/Tach/DmgOffset; angle = RotMin + value / max × (RotMax − RotMin), clamped. Speed against 160, rpm against a fixed 8000 with a floor of 800, damage against its maximum. The gear indicator sits at GearPivotOffset with paint job = transmission gear (R, N, One…), also in automatics | `mmDashView::LoadPkg`, `::LoadPivotInfo`, `::Init`, `::Cull`, `RadialGauge::Cull`, `::GetArrowAngle` |
| HUD toggle | The "HUD Toggle" key is `mmHUD::ToggleExternalView`: it hides and shows the instrument cluster only. `mmHUD::Disable` (looking around from a point-of-view camera, the in-race menu) hides the dashboard with the readouts, and the messages in single player only | `mmGame::UpdateGameInput`, `mmHUD::ToggleExternalView`, `::Disable` |
| Circuit lap rows | `mmWaypoints::Update` sets the row of the lap being driven every frame, so a live time shows under the completed laps | `mmWaypoints::Update`, `mmCircuitHUD::SetLapTime` |
| Map police | Police cars show on the map only while they pursue | `mmHudMap::DrawCops` |
| Map: Cops & Robbers | After the player: the gold (`GOLD_DOT`), the bank and the hideout (`BANK_DOT`, `HIDEOUT_DOT`), or in Robber Teams the blue and red bases (`BLUE_DOT`, `RED_DOT`) | `mmHudMap::DrawCopsnRobbers`, `::RegisterCopsnRobbers`, `mmMultiCR::InitHUD` |

Defaults for a new player in MM2: map off, rotating map on, zoomed out,
opponent icons on, mirror off (`mmStatePack`,
`mmPlayerConfig::DefaultViewSettings`); OpenMM2 keeps the view settings in
the profile's `[HUD]` section.

## Keys, popup, sounds and the announcer

**Keys** (`src/app/Controls`, `mmInput::SetDefaultConfig`): the race reads
the `[Controls]` `Bind.*` keys over MM2's keyboard defaults, with the slot
order of MM2's input events, and `mmGame::UpdateGameInput`'s actions: map
toggle (`GetNextMapMode`: off, small, split), full-screen map (pauses a
single-player game), map zoom and orientation (with the map on), HUD toggle,
change camera, wide angle, dashboard, transmission, shift up / down,
reverse, next / previous checkpoint (checkpoint races), opponent icons and
the horn. Steer Left wins over Steer Right; keyboard steering goes through
`mmInput::FilterDiscreteSteering`. `[Controls] Controller` picks the
driving inputs (`mmInput::SetDefaultConfig`): the keyboard (and, as an
OpenMM2 extra, any gamepad), the mouse (the cursor across the window
steers, left / right buttons throttle / brakes), a gamepad (stick, buttons
0 / 1 / 3), or a joystick or wheel (X steers, Y forward / back throttle /
brakes). The mouse, joystick and wheel steer through
`mmPlayer::FilterSteering` with the STEERING SENSITIVITY and the
speed-blended curves of mmPlayer; sticks go through the CONTROLLER DEAD
ZONE (default 0.1, rescaled as DirectInput's dead zone does); AUTO REVERSE
reaches the pedal handling.

**Chat** ("Enter Chat Msg", `mmPopup::ProcessChat`, `PUChat`): a line of up
to 40 characters at the bottom left, without pausing. In single player
"/blubber" is the only command (the cheat: elasticity cap 4 until the next
`mmGame::Reset`, elasticity 4 on the player's bound, and no finish is
registered until the game ends). In a network game lines go to the others
("/rc ..." stays local, "/wav ..." is not shown) and show as "name: text"
in five chat lines at 0.65 of the screen, hidden 15 s after the last.

**Network cars** (`mmNetObject`): every other player's car is a body of the
world (kinematic at its interpolated snapshot in OpenMM2; MM2 drives it
with the remote inputs), declared as a type-3 mover, built with the
polygonal bound, towing its trailer except in multiplayer cruise and Cops
& Robbers.

**Popup** (`mmPopup`, `PUMain`, `PUExit`, neither with a title): Escape
opens the in-race main menu (pausing a single-player game; the HUD and map are disabled): Restart
Race / Restart Lesson (read-only in a network game), Options (not ported,
shown disabled), Quit to Race Menu / Back to School, Exit to Windows (asks
first) and Resume Driving; Escape resumes. The popup card is (0.2, 0.1,
0.6, 0.8) of the screen.

**Sounds**: the modes load their 2D sounds in `InitGameObjects`
(Startracelow, Startracehigh, Endofracetag, Youlose, Damgelose, Messagenote,
Timerwarning; the crash course at its own volumes) and play them from
`UpdateGame`: countdown lines, Blitz's warning beats (once a second below
10 s, looping below 3 s, stopped at the finish, time-up and wrecks),
Endofracetag for a win or 1st, Youlose otherwise, Damgelose for a wreck or
the water, Messagenote for circuit penalties and opponents finishing.
While the player's car is in a room flagged 0x02 the audio is in tunnel
mode (`mmPlayer::Update`, flag 0x80): surface sounds take the tunnel entry,
the ambience switches areas and the rain is sheltered; the rain's interior
loop plays for the hood camera and the dashboard (`mmPlayer::SetCamera`).
The city ambience exists only with CITY SOUNDS on (`mmPlayer::Init`).

**Announcer** (COMMENTARY on; `mmPlayer::InitSpeechAudio`,
`mmSpeechContainer`): cruise and the races get the race speech, the crash
course its lesson speech. Build 3393 asks for: the pre-race line at every
`mmGame::Reset` (start, restart, cruise water), Blitz's damage penalty line
on a wreck and its poor results after finishing too late, the results for
the player's place (`mmGameSingle::UpdateRewards`) and the lesson's outcome
(`mmSingleStunt::RegisterFinish`, not after the water). The final lap,
final checkpoint and race progress lines are never asked for. MM2 plays
the results only for a registered finish and an unlock line instead when a
reward unlocks: the race works out the registration and the reward at the
line (on a copy of the driver; the frontend stores the finish when the race
is left) and plays the unlock line (`LoadVehicleUnlock`,
`PlayUnlockVehicle`, or the paint job's) or the results.

**Rain** (`cityLevel::DrawRooms`): none with the camera in a subterranean
room (level flags 0x0A), nor in a landmark room (0x20) with geometry above
the camera (a probe from 100 m up).

Not implemented, all verified to exist in MM2:

* **Rear-view mirror** drawing ("Rear View Mirror", off by default;
  `mmMirror`): a viewport `Size` (0.3 × 0.16 of the screen) at the top
  right, one pixel from the edges, showing the city from `Position` in car
  space looking backwards, mirrored left-right, with `Fov` / `Aspect` /
  `NearClip` / `FarClip` from `tune/<car>.mmmirror`, and the player's car
  hidden. The race toggles it (event 0x1E) and keeps the driver's choice;
  the renderer draws it.
* The CD player display (`mmCDPlayer`): unused in retail (the disc carries
  `cdid.txt`, so the in-race CD player does nothing).
* The far LOD of the stands (`pt_*` VL mesh: banner only).

Inferred: the finish line's Blitz icon (shown while the finish is visible).
