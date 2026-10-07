# Game modes, race rules and HUD

Code: `src/game/session/` (`Session`, `RaceSetup`, `Gate`, `CopsAndRobbers`,
`Hud`). Tests: `tests/game/test_session.cpp`, `tests/game/test_session_hud.cpp`.

Sources, in order of weight:

* **Ported**: MM1's `mmGame`, `mmSingleBlitz`, `mmSingleCircuit`,
  `mmGameSingle`, `mmWaypoints`, `mmArrow`, `RadialGauge`, `mmMultiCR` from
  Open1560's `game.asm` / C++ (GPL-3.0). Timings, gate geometry, hit tests,
  arrow placement and the win rule come from there.
* **Verified from MM2 data**: race help pictures (`jpg/race_*.jpg`,
  `lon_cc*.jpg`, `sf_cc*.jpg`), the string table, the race files and models.
* **Inferred**: anything MM2 added (Checkpoint race details, Crash Course,
  the HUD layout). Marked below.

## Integration (what the race screen does)

```cpp
SessionOptions opts;
opts.scoringBias = catalog.vehicle(config.vehicle)->scoringBias;   // tune/<car>.info
auto session = Session::create(config, cityData, vfs, strings, &error, opts);
car.reset(session->playerSpawn());
for (i : session->opponents())  spawn AI car i.vehicle at i.spawn, driving i.path
for (p : session->police())     spawn police (crash course chasers, race cops)
session->start();

each frame (dt):
  PlayerState ps;                 // see below
  session->update(dt, ps, opponentStates, policeStates);
  if (session->playerHeld())      full brake, ignore throttle
  if (!session->racersReleased()) hold the AI racers
  for (Event e : session->takeEvents())
      Respawn       -> car.reset(session->respawnTransform())
      CountdownReady/Set/Go, CheckpointCleared, FinishActivated, LapCompleted,
      FinalLap, TimerWarning, PlayerFinished, TimeUp, HitWater ... -> sounds, voice
  music.setState(session->musicHint())  // Racing / Idle / Results
  scene pass:   world..., hud.drawWorld(*session, camera, ps, steering, blips);
                hud.drawMap(*session, ps, blips, dt);          // last in the pass
  overlay pass: hud.drawOverlay(overlay, text, uiTextures, *session, ps);
  if (session->finished())  -> makeFrontendScreen(ctx, session->result())
```

`PlayerState` fields the screen fills every frame:

| Field | Source |
|---|---|
| `transform` | `CarSim::modelMatrix()` |
| `velocity`, `speedMph` | car body velocity, `CarSim::speedMph()` |
| `rpm`, `maxRpm` | `engine.rpm`, `engine.maxRPM` |
| `gear`, `automatic` | `trans.getCurrentGear()` (-1 R, 0 N, 1..), config |
| `throttle` | accelerator pedal as pressed (used for the false start) |
| `damage01`, `wrecked` | `damage.damage`, `damage.wrecked()` |
| `inWater` | car below `city.water->height` inside one of `city.water->rooms` |
| `vehicleImpacts`, `objectImpacts` | running counts from `CarSim::onImpactCallback` (impact with `other != nullptr` is a vehicle) |

`OpponentState` per opponent / police car: transform, velocity, damage, wrecked.

## Common rules (ported)

* **Countdown** (`mmSingleBlitz::UpdateGame` states 0-2): "Ready..." for
  1.25 s, "Set..." for 1.25 s, then "Go!" releases the racers and starts the
  clocks. Cruise has no countdown.
* **False start** (inferred trigger): pressing the throttle during the
  countdown in a mode whose string group has the penalty line (Circuit;
  Checkpoint borrows it) shows "Wait...5 second penalty!" and keeps the
  player braked for 5 s after "Go!".
* **Gates** (`CalculateGatePoints`, `LineIntersect`, `WPHit`): a waypoint
  is a segment `position ± radius·(cos h, sin h)` across the road (h = the
  CSV heading). The player hits it when the gate crosses the path driven
  since the last update or the car's long or lateral axis. AI cars hit it
  within 5 m of the centre or by crossing it (`AnyAIWPHit`). Jumps of more
  than 50 m in one update (respawns) are not tested.
* **Start**: the first waypoint, facing its heading. The driving direction
  of heading h is `(sin h, 0, -cos h)` (verified against every race's first
  two waypoints). Opponents start at the first point of their `.opp` line.
* **Winning** (`ProgressCheck`): within `MustPlace` (3, `tune/<city>.cinfo`)
  for amateurs, first place for professionals.
* **Score** (`CalculateRaceScore`): 50 / 25 / 10 points for 1st / 2nd / 3rd,
  × the car's `ScoringBias`, × 2 for professionals (the multiplier's meaning
  is inferred).
* **Wrecked**: the race ends ("Game over!").
* **Water** (`mmGame::Update`, `HitWaterTimer`): message at once, respawn
  4 s later at the last cleared checkpoint. London says "More tea, vicar?"
  (string 642), elsewhere "Sleep with the fishes!" (30) (assignment
  inferred). **Falling out of the city** (below y = -50, MM1's limit, or 20 m
  under the PSDL): "That didn't happen!" and an immediate respawn.
* **Post race**: results 5 s after the end (state 4 of `UpdateGame`).

## Modes

| Mode | Rules | Evidence |
|---|---|---|
| Cruise | free roam, no clock | MM1 roam; help text "Red lights are optional!" |
| Blitz | checkpoints in **any order** against the clock (`mmblitzdata.csv` TimeLimit); the finish opens when all are cleared; warnings below 10 s; "Time's up!" ends it | MM1 `mmWaypoints` types 2/3 (`AnyWPHits`); help: "Go through the checkpoints in any order - the arrow will point toward the nearest checkpoint" |
| Checkpoint | checkpoints in any order, then the finish, against opponents; place 1..8 | help: "Go through checkpoints in any order, then beat your opponents to the finish!" |
| Circuit | waypoints in order; crossing waypoint 0 completes a lap; "Final lap!" before the last; lap times | MM1 `mmWaypoints` type 1 |
| Crash Course | lessons of one or two events (`crash<N>data.csv`), see below | inferred |
| Cops & Robbers | multiplayer only; `CopsAndRobbers` holds the rules | MM1 `mmMultiCR` |

The arrow targets the nearest uncleared checkpoint (any-order modes), the
next waypoint (circuit, lessons) or the finish.

`TimeLimit` in the circuit and checkpoint tables is 50 / 40 for every race and
MM1's rules for those modes read no clock: it is not used (inferred).

### Crash Course

Each lesson runs its events in order; an exam's second event starts after
the first is passed, from its own start. Checkpoints are passed in order and
the last one ends the event (inferred). Event types (column `Event`) were
matched to the lesson pictures and to the string groups of the string table:

| Type | Lessons | Rule (inferred) | Strings |
|---|---|---|---|
| 0 Jump | London Leap, River Dancing, Frequent Flyer | checkpoints within the time limit | 205-209 |
| 2 Follow | Follow That Car!, Straight With Chaser, exam parts | stay within 120 m of the lead car until it reaches the destination; "Car escaped!" otherwise | 217-223 |
| 3 Evade | Artful Dodging, The Heat Is On | reach the finish with no chaser (`[Police]` of the lesson's .aimap) within 40 m | 192-198, 652 |
| 4 Minimum speed | Cutting Corners, Turn It Up!, Midterm 1, SF Final | pass every checkpoint at ≥ `extra[0]` mph | 231-240 |
| 5 Clean | Round You'll Go, Keep It Clean | touching another car fails ("You scraped the paint!"); hits are counted | 224-230, 269-270 |
| 7 Course | slaloms, 180s, exams | checkpoints within the time limit | 241-245, 608-609 |
| 8 Destroy | Bonnie & Collide | wreck the target car before it reaches the destination | 616-623 |
| 9 Map | The Knowledge | checkpoints in order | 241-245 |

`extra[2]` = 1 on exactly the events with an AI car (follow / destroy);
`extra[1]` and `extra[3]` are unknown. Passing the last event of lesson 12
shows "You've passed the final. Congratulations!".

### Cops & Robbers (rules only)

Structure of MM1's `mmMultiCR`: whoever carries the gold gets its extra mass
(Gold Mass option), loses it to a car that rams it (impulse threshold
inferred), drops it when wrecked; delivering it scores (robbers to the
hideout, cops to the bank; robber teams: red hideout, blue bank, inferred).
Time limits warn at 20/15/10/5/1 minutes (strings 138-142); point limits end
the game. `race/<city>/multicopwaypoints.csv` is read as bank, hideout, then
gold spots (inferred).

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
| Messages | Gill Sans MT 36 px (string 60), yellow, centred across the screen, word-wrapped, with a (15, 15, 15) shadow offset by 1/18 of the text height. A message box 15 % tall at 80 % of the height, or at 20 % for "upper" messages (`SetMessage` mode 1: countdown, water); MM2's second line sits at 87.5 % / 35 %. `HudMessage::top` selects the upper box | `mmHUD::mmHUD`, `::Update`, `::SetMessage`, `mmTextNode::RenderText` |
| Arrow | `hudarrow01` in every mode (the `_blitz` and `_cc` models are unused); camera space (0, 2.5, −6.1); points at the target taken at the camera's height (the vertical component is kept, the basis is not renormalised); tilted −20° about X; unlit, opaque, no depth test; paint job 1 (yellow) while the target is behind. None in cruise and circuits, nor in follow, destroy and map lessons; off after a Blitz is over. Drawn in the dashboard view too (under the dash) | `mmArrow::mmArrow`, `::Update`, `mmHUD::Init`, `mmSingleStunt::InitHUD`, `mmSingleBlitz::Update` |
| Checkpoint stands | `pt_check` / `pt_finish` (2 × 1 units), rotated by −heading about Y, scaled by (radius, 7.5, radius) and centred 3.75 m above the waypoint, so the arch spans the gate and stands 7.5 m tall; pre-lit (no lighting); crash course stands use paint job 1 (`CCStand`) | `mmWaypointObject::mmWaypointObject`, `mmCheckpointInstance::Init`, `::Draw`, `mmSingleStunt::InitNewEvent` |
| Opponent icons | "Opponent Position" (on for new players): a violet triangle facing the camera, 2 m wide, 4 m tall, tip 4 m above each opponent, drawn over everything. In Blitz the same cards in cyan mark the checkpoints still to clear, 5 m above them | `mmIcons::Cull`, `mmSingleBlitz::InitHUD`, `::Update`, `mmPlayerConfig::DefaultViewSettings` |
| Map: placement | Small: (Pos.x × W, Pos.y × H), size (Size.x × W − 10, Size.y × H − 10); at the left edge for right-hand-drive cars (vehicle Flags 0x40) in the dashboard view. Split: bottom half. Full screen: whole screen. No frame | `mmHudMap::SetMapMode` |
| Map: camera | Perspective, 60° vertical field of view, aspect 1.25 (2.5 split) regardless of the viewport's shape, near 10, far 1600; above the car at an absolute height = the zoom distance. Rotating: the car's heading up; otherwise −Z up and +X right. Zoom and icon size move linearly at (out − in) × Approach Rate per second toward the Map Zoom setting (FS values in full screen) and snap on mode changes. OpenMM2 widens the horizontal field on non-4:3 outputs | `mmHudMap::Cull`, `::SetMapMode` |
| Map: look | Cleared to a hard-coded ocean colour: London (0.92, 0.84, 0.778), elsewhere (0.084, 0.68, 0.92) (the `.mmhudmap` Ocean Color is overwritten); `hudmap_<city>.pkg` unlit | `mmHudMap::Init` |
| Map: icons | Car arrows are flat untextured triangles (0, 0, −1), (±0.7, 0, 1) × icon scale, 15 m above the car: police (red) while chasing, opponents (violet), the player (yellow) over a black one 1.3 times larger; traffic is not shown; `hudmap_tri` is loaded but unused. Waypoints are `hudmap_square` × icon / 7.51, 10 m above: green to clear, yellow the current goal, grey cleared (circuits), the finish dot for the open finish (checkpoint races) or the start line (circuits) | `mmHudMap::DrawIcon`, `DrawColoredTri`, `::DrawPlayer`, `::DrawCops`, `::DrawOpponents`, `::DrawWaypoints`, `::DrawIndicator` |
| Dashboard | `<car>_dash.pkg` in camera space (DashPos, RoofPos), unlit, no depth test, painted dash, roof, gear indicator, speed, tach and damage needles, dash_extra, wheel. Needles turn by −angle about Z around (box centre of `<car>_dash_<part>.mtx` + PivotOffset) and are moved by Speed/Tach/DmgOffset; angle = RotMin + value / max × (RotMax − RotMin), clamped. Speed against 160, rpm against a fixed 8000 with a floor of 800, damage against its maximum. The gear indicator sits at GearPivotOffset with paint job = transmission gear (R, N, One…), also in automatics | `mmDashView::LoadPkg`, `::LoadPivotInfo`, `::Init`, `::Cull`, `RadialGauge::Cull`, `::GetArrowAngle` |
| HUD toggle | Hides the cluster, readouts, messages and arrow; the clock, map, icons and dashboard stay | `mmHUD::Toggle`, `::Disable` |

Defaults for a new player in MM2: map off, rotating map on, zoomed out,
opponent icons on, mirror off (`mmStatePack`,
`mmPlayerConfig::DefaultViewSettings`). OpenMM2 shows the small map by
default because the in-race toggles are not bound yet.

Not implemented, all verified to exist in MM2:

* **Rear-view mirror** ("Rear View Mirror", off by default; `mmMirror`):
  a viewport `Size` (0.3 × 0.16 of the screen) at the top right, one pixel
  from the edges, showing the city from `Position` in car space looking
  backwards, mirrored left-right, with `Fov` / `Aspect` / `NearClip` /
  `FarClip` from `tune/<car>.mmmirror`, and the player's car hidden. Needs a
  second world pass with flipped winding in the race screen.
* The 3D view moving to the top half (split map) or into the small rectangle
  (full-screen map), `mmHudMap::SetMapMode`.
* The mouse steering bar (`mouse_bar` / `mouse_ar`, `mmExternalView::Cull`),
  the CD player display (`mmCDPlayer`), chat lines (`mmHUD::PostChatMessage`)
  and the Cops & Robbers scores (`mmCRHUD`).
* The far LOD of the stands (`pt_*` VL mesh: banner only).

Inferred: the finish line's Blitz icon (shown while the finish is visible);
police on the map are drawn whenever passed (MM2: while in pursuit).
