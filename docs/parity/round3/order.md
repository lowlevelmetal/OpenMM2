# Round 3: order

Audited against MM2Recomp (midtown2.exe build 3393) on 2026-10-08.

The order in which MM2 creates, resets, updates and draws its systems,
compared with OpenMM2's race (`src/app/RaceScreen.cpp` and the session
code). MM2's side was read from the asm (`GameLoop`, `MainPhase`,
`BeginPhase`, `asRoot`, `asNode`, `mmReplayManager`, `mmGameManager`,
`mmGame` and the modes, `asCullManager`, `cityLevel`), with the split
pieces of `mmGame::Init` and `aiMap::Reset` read in place.

Summary: 51 cases checked; verified 35, fixed 10 (six commits), deviation 5
(one frame each, invisible), open 1 (F2's pause, already open in the
game-flow record).

Fixed (one commit each):

- 9d2c313 the menu's Restart and F4 restart the race at the start of the
  next frame (mmReplayManager's reset flag); F4 ends a pause at once.
- d6310f6 the paused frame keeps the camera, the sky, the rain and snow,
  the HUD message timer and the fall and water checks running.
- c124f3c the raw C and V keys of `mmGame::UpdatePaused` are gone: nothing
  in build 3393 calls it.
- 6b444ac with the menu up and the game running, the car is driven by every
  controller but the mouse, whose car keeps its last inputs.
- 7bcdb41 `AudManager::Update` runs twice a frame (GameLoop and asRoot).
- 3798bb8 the horn keeps the state `mmGame::UpdateHorn` last set while the
  menu is up.

Tests: `tests/game/test_parity_order.cpp`,
`tests/audio/test_parity_order.cpp`.

## MM2's tree

`BeginPhase` adds the audio manager (`AudManager`) to `asRoot`, and
`mmInput::AttachToPipe` inserts `mmInput`; the race phase of `MainPhase`
then builds `mmReplayManager`, `mmGameManager` (which builds and initialises
the mode, then resets it) as the replay manager's child, and adds the
replay manager to `asRoot`. `GameLoop(false)` runs the frames.

`asNode::Update` updates every active child (node flag 1) whether or not
`asRoot` is paused; `asNode::UpdatePaused` is only reached from its own
recursion and from `AudManager::Update`, so the game's `UpdatePaused`
slot (`mmGame::UpdatePaused`, `mmHUD::UpdatePaused`, `aiMap::UpdatePaused`)
is never called. The pause is a flag (`asRoot` +0x48) that the nodes test
themselves.

`mmGame`'s children, in update order: the gizmo managers
(`mmGame::InitGizmos`: sailboats, bridges, trains, ferries, parked cars),
then those the mode's `Init` adds: `mmPlayer`, `mmIcons`, the mode's
`mmWaypoints` (races, circuits, Blitz, lessons), `mmHudMap`, `mmHUD`,
`mmPopup` (the network modes add their net objects too). `mmPlayer`'s
camera view (`camViewCS`, +0xe2c) is a child with its active flag cleared:
`mmGameManager::Update` updates it itself, after the physics.

## Load

| Step | MM2 | OpenMM2 | Verdict |
| --- | --- | --- | --- |
| Progress 10 %, audio statics, positions | `mmGame::Init` start | `RaceScreen` ShowLoading state | verified |
| Strings, the driver's settings (controls, audio, view, graphics) | `InitGameStrings`, `PlayerSetState` | `Session::create` strings, `loadProfile` / view settings in `loadVehicle`, options in `loadLevelPart` | verified (the view settings are applied before the cameras are reset, as `PlayerSetState` precedes `mmPlayer::Init`) |
| The mode's player object, the transmission choice | `InitMyPlayer`, `vehTransmission::Automatic` | `loadVehicle` (`trans.automatic`) | verified |
| The city, level graphics | `lvlLevel::Load`, `SetLevelGraphics` | `loadCityPart`, `loadLevelPart` | verified |
| Physics, props, the player's car, cameras, HUD, map, car audio, speech | `dgPhysManager`, `dgBangerManager::Init`, `mmPlayer::Init` | `loadVehicle` (world, car, renderer, cameras, mirror), `loadAudio` | verified |
| Icons | `mmIcons::Init` | `Hud` (loadFinish) | verified (no state of the race depends on it) |
| The start place | the modes' `InitGameObjects` (`SetResetPos`, `vehCar::Reset`) | `RaceSetup::playerPlace`, `setResetPos`, `reset` | verified |
| Gizmos | `InitGizmos`, before `aiMap::Init`: the bridges' triggers loop over the opponent count before the AI map has set it, so only the player triggers them | `loadEffects` (after the AI), the player as the only trigger | verified (world-objects record) |
| The AI map | `aiMap::Init`, `aiMap::Reset`, the racers registered with the map and icons | `loadAi`, `m_ai->reset()`, `spawnOpponents`, `spawnPolice` | verified (ai records) |
| The physics sample (1/35 s, 3 samples), banger data | `mmGame::Init` | phys-core | verified |
| Settling the player, the racers | `InitOtherPlayers` (probe, 0.9 m up), `CollideAIOpponents` | `settleOnGround` in `loadVehicle` (before the AI is loaded), `spawnOpponents` | verified (the probe sees only the level in both: the racers are not on the player's spot) |
| HUD, light, event queue, popup, far clip, results titles, view manager, weather | `InitHUD`, ..., `mmViewMgr::Init`, `InitWeather` | `loadFinish` (HUD, view settings), `loadEffects` (weather) | verified |
| The mode's first Reset | `mmGameManager::mmGameManager` -> `mmGameManager::Reset` -> the mode's Reset (`mmGame::Reset`: props, pre-race speech, `aiMap::Reset` again, `mmPlayer::Reset`, music, elasticity cap; then the state, `SetPreRaceCam`) | `loadFinish`: elasticity cap, `Session::start` (pre-race speech), damage switch, pre-race camera, music | verified; `aiMap::Reset` runs twice at load in MM2 and once in OpenMM2, which only matters to the shared random stream (round-3 random streams) |
| The first frame's time step | `datTimeManager::Update`: the time since the last frame before the load, clamped to 0.1 s | the load's last frame, clamped to 0.1 s | verified |

## Restart

MM2: Restart Race (`mmPopup::Update`, PUMain id 10, and F4 over it) sets
`mmReplayManager` +0x19 and calls `DisablePU(0)`, whose unpause is a
request too (+0x1d); F4 in the game (`mmGame::UpdateDebugInput`) calls
`asRoot::Reset` (the pause ends at once) and sets +0x19. The next frame,
`mmReplayManager::Update` first resets (`mmReplayManager::Reset`: the
global seed back to 1, then `asNode::Reset` down to `mmGameManager::Reset`
and the mode's Reset), then grants the unpause, then records the inputs and
runs the game: the first rules, AI and physics after a restart all start
from the reset state.

The mode's Reset, e.g. `mmSingleRace::Reset`: `DisableRacers`;
`mmGameSingle::Reset` (the audio manager's and `mmInput`'s Reset, the
racers' slots, the results); `mmGame::Reset` (`lvlLevel::ResetInstances`,
`Process3D(1)`, the water timer and race-over flag, the pre-race speech,
`aiMap::Reset`, then the children's Reset: gizmos, `mmPlayer::Reset`,
icons, waypoints, map, HUD, popup; PUMain's replay button, `StartMusic`,
the elasticity cap); then `SwitchState(0)`, `SetPreRaceCam`, `StopTimers`,
`SilenceEngine(0)`. The water and fall handlers call the mode's Reset
directly from `mmGame::Update`, before the AI and the physics of that
frame.

| Case | MM2 | OpenMM2 | Verdict |
| --- | --- | --- | --- |
| When the menu's Restart and F4 act | at the start of the next frame, before anything updates | the session at once, the cars from the Restart event after that frame's physics, so the session's update in between saw the old car | **fixed** (9d2c313): `requestRestart`, `applyRestart` at the top of the next frame, after `mmInput::Update` as in asRoot's order; the frame of the request takes the paused path. Before: restarting with the car at Blitz's first checkpoint showed "Check: 1/4" in the new countdown (old binary, 3 of 4 runs); after: 0/4 in every run |
| F4 during the full-screen map's pause | `asRoot::Reset` ends the pause at once (the map stays up) | the session restarted, the game stayed paused and the cars were not reset until it ran on | **fixed** (9d2c313) |
| When the water and fall handlers restart | in `mmGame::Update`, before that frame's AI and physics | in the session's update after the physics; the Restart event resets everything before the next frame | verified (the next AI and physics both start from the reset) |
| Order inside the reset | props, speech, `aiMap::Reset` (seed 1, police, roads, cable cars, racers, pedestrians, ambient traffic around the player's room, officers), then `mmPlayer::Reset` | props, gizmos, cable cars, cap, player, racers, traffic bodies, ambient AI, police, music, cameras, input, force feedback | verified: no object reads another while resetting; MM2 populates the traffic around the room the car was in before it went back to the start, and its first `aiMap::Update` moves the population to the start's room with the same `AdjustAmbients`, which OpenMM2 does from room 0 at its first step (the draws differ only by stream) |
| `mmInput`'s Reset and the frame's inputs | `mmInput::Update` (asRoot's node) before the reset, the recording after it | `m_gameInput.update`, `applyRestart` (`m_gameInput.reset()`), then `updatePlayer` | verified |

## The frame

MM2, one frame of the race (asRoot not paused, no menu):

1. `GameLoop`: `datTimeManager::Update`, `ioInput::Poll`,
   `gfxPipeline::Manage`, the event handler, `AudManager::Update`.
2. `asRoot::Update`: `AudManager::Update` again (asRoot's first node),
   `mmInput::Update`, `mmReplayManager::Update` (reset and pause requests,
   then the recording of steering, throttle, brakes and handbrake).
3. `mmGameManager::Update`: declares `aiMap` and itself to the cull
   manager, then the tree: `mmGame::Update` = `UpdateDebugInput` (Escape,
   F1, F2, F4, F6, chat), `UpdateGameInput`, the replay data,
   `UpdateSteeringBrakes`; the mode's `UpdateGame`, `mmAmbientAudio`,
   `Aud3DObjectManager::Update`; `mmSpeechContainer::Update`;
   `UpdateDMusic`; the fall and water checks; `DeclareMover` (player, trailer);
   `aiMap::Update`; then the children (gizmos, `mmPlayer::Update`, icons,
   waypoints, map, HUD, popup).
4. `cityLevel::Update` (`lvlSky::Update`, the cloud offset), then
   `dgBangerActiveManager::Update` and `dgPhysManager::Update` (the cars'
   `PostUpdate`: sirens, car audio), `cityLevel::PreDraw` (particles,
   texture movies), the camera (`camViewCS::Update`), the dash, map and
   mirror declared, `asCullManager::Update` (the draw),
   `cityLevel::PostDraw`.

| Case | MM2 | OpenMM2 (`RaceScreen::update`) | Verdict |
| --- | --- | --- | --- |
| The audio manager | `AudManager::Update` twice (GameLoop, asRoot's node): the announcer's queue three times a frame with `mmGame::Update`'s, both pause stops in the first paused frame | once | **fixed** (7bcdb41): `AudioManager::updateFrame` |
| Input before the game | `mmInput::Update`, the recording, `UpdateDebugInput`, `UpdateGameInput`, `UpdateSteeringBrakes` | `m_gameInput.update`, popup / Escape / debug keys, `updateGameInput`, `updatePlayer` | verified |
| The steering filter's speed | set by `mmPlayer::Update` after the recording | `setSpeed` after `steering(dt)` | verified |
| Rules before or after the physics | `UpdateGame` before the AI and the physics, seeing the last frame's physics | `updateSession` after the physics and the camera | verified (session record's one-step offset): OpenMM2's rules at the end of frame N are MM2's at the start of frame N+1, with no input, AI or physics in between; holds, releases ("Go!"), the water handler and the cameras they set take effect in the same physics step and camera update in both. Only the HUD shows a rule's result one frame sooner |
| AI seeing the player | `aiMap::Update` after `UpdateSteeringBrakes`, before the physics: the last step's position, this frame's inputs | `updateAmbient` / `updateAiDrivers` after `updatePlayer`, before the step | verified |
| Ambient, racers, police, cable cars, lights, gizmos | `aiMap::Update` (ambient, pedestrians, racers, police, cable cars, light sets), then the gizmo managers (nodes of `mmGame`) | the same order | verified |
| The finish hold (`mmPlayer` +0x2258) | `mmPlayer::Update`, after `aiMap::Update`, before the physics | `updatePlayer` before the AI | verified (the AI reads positions, not the hold) |
| Active props | `dgBangerActiveManager::Update` (rest check, movers) before the step | `BangerSet::update` after the step, declaring for the next | verified (props-fx record; the same step sees the same movers) |
| The camera | `camViewCS::Update` after the physics | `updateCarCamera` after the step | verified |
| The 3D listener | `Aud3DObjectManager` keeps a pointer to the camera's matrix; its update in `mmGame::Update` and the cars' audio in `PostUpdate` run before the camera moves: last frame's camera | `updateAudio` after the camera | **deviation**: one frame (a sixtieth of a second) of listener lag, inaudible |
| The rain emitter | `asParticles::Update` in `cityLevel::PreDraw`, before the camera | `Weather::update` after the camera | **deviation**: one frame of emitter lag, invisible |
| HUD readouts | the gauges read the car when drawn; the clock digits in `mmHUD::Update`, before its timers update | `m_playerState` after the step | **deviation**: OpenMM2's clock is at most one frame ahead, invisible |
| The map's player arrow | `mmHudMap::Update` in the tree, before the physics | drawn from the state after the step | **deviation**: one frame, invisible |
| Pause and unpause requests (Escape, closing the menu, the full-screen map) | `mmReplayManager` +0x1c / +0x1d, granted at the start of the next frame: the frame of Escape still runs, the frame of closing still pauses | `m_paused` set at once | **deviation**: one frame each way, invisible |

## The paused frame

MM2 with asRoot paused (the menu in single player, the full-screen map,
F2): `AudManager::Update` twice (`AudManagerBase::UpdatePaused`), `mmInput`,
then `mmReplayManager::Update` runs the tree without recording.
`mmGame::Update` still runs its input (without the menu: all of
`UpdateDebugInput` and `UpdateGameInput`), `mmSpeechContainer::Update`,
the fall and water checks (the water timer counts `Seconds`) and its
children: `mmHUD::Update` counts the message and the chat down; the gizmo
managers, `mmTimer` and `mmPlayer`'s rain audio test the pause themselves.
`mmGameManager::Update` skips the props and physics but runs
`cityLevel::Update` (the sky turns), `cityLevel::PreDraw` (the rain and
snow fall, the texture movies run) and the camera.

| Case | MM2 | OpenMM2 before | Verdict |
| --- | --- | --- | --- |
| The camera | updated every frame | frozen; a camera key in the full-screen map changed nothing until the game ran | **fixed** (d6310f6) |
| The sky, the rain and snow | turn and fall | frozen | **fixed** (d6310f6) |
| The HUD message | counts down through the pause | kept its time | **fixed** (d6310f6), `Session::updatePaused`; test `PausedFrameCountsTheMessageDownNotTheRace` |
| The fall and water checks | run (five seconds in the water run out under the menu) | stopped | **fixed** (d6310f6); test `PausedFrameRunsTheWaterAndFallChecks` |
| C and V while paused | `mmGame::UpdatePaused` is never called | taken as raw keys | **fixed** (c124f3c) |
| The rules, clocks, AI, physics, props, gizmos, ambient and 3D audio, music direction | stopped (`UpdateDMusic` tests the pause) | stopped | verified |
| The announcer's queue | `mmGame::Update`'s update only | `m_announcer.update` | verified |
| The texture movies, the chat | run | run | verified |

Checked in the game: a rainy Blitz paused under the menu for 200 frames;
with the fix the camera settles behind the stopped car as camViewCS keeps
updating, where the old build froze it at the moment of the pause.

## The menu up, the game running

A network game, a lost race (`ProcessEscape(0)`) or the chat line.
`mmGame::Update` skips `UpdateDebugInput` and `UpdateGameInput`; while the
game runs and `inputDevice` is above 0 it still calls
`UpdateSteeringBrakes`; it handles F1 and updates the map itself.

| Case | MM2 | OpenMM2 before | Verdict |
| --- | --- | --- | --- |
| The car's inputs | the recorded inputs for the keyboard, joystick, game pad and wheel; the mouse (`inputDevice` 0) leaves the car's last inputs | every pedal and the wheel let go | **fixed** (6b444ac) |
| The game keys | off | off | verified |
| The horn | `UpdateHorn` opens `UpdateGameInput`: the horn's state (+0x274) stays as it was | read every frame | **fixed** (3798bb8): `m_hornHeld`, set in `updateGameInput` |

## Draw order

`asCullManager::Update` clears, draws the 2D background cullables, then for
each camera the 3D cullables in the order they were declared, then the 2D
foreground ones. Declared: `aiMap` (its `Cull` is empty) and
`mmGameManager` (`mmGameManager::Cull`: the full-screen map in map mode 3,
else the letterbox bars; `mmHUD::PostUpdate`; `lvlLevel::Draw`) at the
start of the frame; during the tree the bridges and ferries, the icons
(`mmIcons::Update`), the far checkpoint stands (`mmWaypoints::Update`) and
the HUD's forms (the arrow); after the physics the dash, the small map and
the mirror; the HUD's 2D parts and the popups in the foreground.

| Case | MM2 | OpenMM2 (`drawScene`, `drawOverlay`) | Verdict |
| --- | --- | --- | --- |
| Full-screen map, letterbox, level | `mmGameManager::Cull` first | `drawMap` (full), the band clear, `drawLevel` | verified (hud-views record) |
| Bridges and ferries | after the level | inside `drawLevel` | verified: opaque and depth-tested, the order changes nothing on screen |
| Glows, shadows, rain | inside `lvlLevel::Draw` | inside `drawLevel`, the rain last | verified (rendering-fx record) |
| Stands, icons, arrow, dash | near stands in the level, icons, far stands, arrow, then the dash | stands, icons, arrow, dash (`Hud::drawWorld`) | verified; the far stands' LOD pass is the open `mmWaypoints::Cull` (rendering) |
| Small map, mirror | after the dash | after the 3D view | verified |
| HUD 2D, popup, pointer | foreground | `drawOverlay`: HUD, popup, pointer last | verified |

## End of a race

MM2's modes (e.g. `mmSingleRace::UpdateGame`): the finish stops the timer,
sets the post-race camera, the finish message and standings, stops the
music, registers the finish and saves the driver; a later state locks the
menu and shows the results popup (`mmPopup::ShowResults`) or opens the
menu (`ProcessEscape(0)`) over the running race; leaving it ends the
`GameLoop` (`BeDone`). OpenMM2 does the same up to the results, which are a
frontend page (`leaveRace`): the session record's deviation. Verified
against the session record; nothing new.

## Open

| Item | What it needs |
| --- | --- |
| F2's pause | `mmGame::UpdateDebugInput` F2 pauses through `mmReplayManager` with the HUD off; OpenMM2's F2 is the fly camera (game-flow record) |

## Notes for other areas

- audio: `AudManager::Update` runs twice a frame (`docs/audio.md` and
  `AudioManager.h` now say so; `docs/parity/mm2/audio.md` still says
  "twice as fast"). The audio test `AnnouncerQueueCountsTwiceAFrameWhileRunning`
  checks one `AudioManager::update` per frame, which is still right for
  that function.
- game-flow: `mmGame::UpdatePaused` is unreachable (its row says ported).
