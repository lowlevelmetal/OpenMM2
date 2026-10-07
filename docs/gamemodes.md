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
  scene pass:   world..., hud.drawWorld(*session, camera, ps, steering);
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
