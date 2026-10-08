# MM2 -> OpenMM2: game-flow

Audited from MM2Recomp (midtown2.exe build 3393) on 2026-10-08.

Summary: 608 reachable functions (overloads counted separately) in 38
classes and the free functions; ported 381 (of which newly ported 62),
replaced 66, not needed 151, open 10. Constructors, destructors and the
deleting destructors of a class share one row. Some behaviours inside
ported or replaced functions are still open too; every open item is listed
under Open items.

Scope: the game's phases and load screens (Main, MainPhase, GameLoop,
BeginPhase, EndPhase), mmGameManager, mmStatePack, mmGame, mmGameSingle,
every single-player mode (mmSingleRoam, mmSingleRace, mmSingleCircuit,
mmSingleBlitz, mmSingleStunt), the Crash Course page (CrashCourse), the
multiplayer base and modes (mmGameMulti, mmMultiRoam, mmMultiRace,
mmMultiCircuit, mmMultiBlitz, mmMultiCR), mmPlayer, the waypoints
(mmWaypoints, mmWaypointObject, mmWaypointInstance, mmCheckpointInstance),
mmRaceData, the network objects (mmNetObject, mmNetPath, netZoneScore), the
replay manager, mmPowerupInstance, mmPopup, the reward, city and vehicle
lists, and the music data classes. aiCTFRacer and mmCullCity are noted at the
end. The OpenMM2 side is mostly `src/game/session/` (Session, RaceSetup,
CopsAndRobbers, Gate, Types), `src/app/RaceScreen.cpp`,
`src/game/net/NetGame` and the frontend pages; the first audit from
OpenMM2's side is [../session.md](../session.md). Behaviour is described in
[../../gamemodes.md](../../gamemodes.md). Tests:
`tests/game/test_parity_game_flow.cpp` (and the earlier session tests).

The other audits of this pass ported parts of the same functions in
parallel (merged into this branch): the vehicle audit the cars' reset
places (`SimVehicle::setResetPos` / `reset` / `settleOnGround` /
`respawnAt`), the input audit mmReplayManager's input quantisation, the HUD
audit the icons and the map mode, the audio audit the music restart and the
final stretch, the props audit the restart's prop reset. Rows credit them.

## Spawns at a glance

| What | MM2 | OpenMM2 |
| --- | --- | --- |
| Player, Blitz / circuit / checkpoint race / crash course | the modes' `InitGameObjects`: `SetResetPos(GetStart)`, angle `GetStartAngle` x -0.017453292, `vehCar::Reset`; then `mmGame::InitOtherPlayers` (via `mmGameSingle` for the circuit): probe 2 m above the body to 10 m below (`dgPhysManager::Collide`, mask 0x20), reset place 0.9 m above the hit | `RaceSetup::playerPlace` / `playerDrop` (`StartDrop::OnGround`), `SimVehicle::setResetPos`, `reset`, `settleOnGround` |
| Player, cruise | `mmSingleRoam::InitOtherPlayers`: `RespawnXYZ(true, true, false)`, intersection centre + 2 m, angle 0, no probe | `randomIntersectionStart`, `StartDrop::None` |
| Player, multiplayer races | `mmMulti*::InitNetworkPlayers`: `StartXYZ(slot)` from the start (wide grid when the model radius > 6 or a trailer), `FindGroundPos` (7.5 m up, 15 m down), `SetResetPos`, `Reset`; slot = `NetStartArray` (the host's enumeration order at START) | `multiplayerGridOffset(startSlot)`, `findGroundPos`, `StartDrop::FindGround` |
| Player, multiplayer cruise / Cops and Robbers | `RespawnXYZ(true, true, true)`: seeded with the local player id, a counter of draws | `randomIntersectionStart`, seed 1 + player id (deviation: own random stream) |
| Racers | `aiRouteRacer::Init` (first `.opp` row, its 4th column x 0.017444445), then `mmGame::CollideAIOpponents`: the same probe from the model origin, 0.9 m up | `RaceScreen::spawnOpponents` (vehicle audit's settle) |
| Police | `aiPoliceOfficer::Reset`: their post and angle, no probe; how many: trunc(posts x cop density) | `spawnPolice` |
| Multiplayer | no racers or police; the race modes load no AI map at all; cruise and Cops and Robbers keep only pedestrians (`mmGameMulti::Init`) | `loadAi`, `loadRaceSetup` (new) |
| Water respawn | `mmSingleCircuit` / `mmGameMulti::HitWaterHandler`: the last cleared waypoint's place, no probe, the start put back (moving it by CenterOfGravity) | `Session::respawnAtLastCheckpoint`, `SimVehicle::respawnAt` |
| Restart | `mmGame::Reset`: props back (`lvlLevel::ResetInstances`), cars to their reset places, `aiMap::Reset` (traffic, peds, lights, seed 1), music | props, cars, music ported; `aiMap::Reset` of the ambient AI open (ai) |
| Checkpoint stands | `pt_check` / `pt_finish` per waypoint type (`LoadCSV`), (radius, 7.5, radius), raised 3.75 m; lessons (radius, 7.5, 15) | `buildCheckpoints`, `hud::standMatrix` |
| Cops and Robbers places | `GetRandomPoints`: gold, bank, hideout from `multicopwaypoints.csv` rows (never the last) or intersections (never in rooms 0x24); dropped gold on the ground (`FindGround`) | `CopsAndRobbers`, `RaceScreen::setupCopsAndRobbers` |

## Phases and load screens (free functions)

MM2 runs `Main` -> `MainPhase` per phase (frontend or race) -> `BeginPhase`
(engine subsystems, the loading picture and its 10 % bar) -> `GameLoop` ->
`EndPhase`. OpenMM2's App runs one loop over persistent subsystems and
switches screens.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `Main` | replaced | `app/main.cpp`, `App::run`, `CommandLine` | colour-depth, mutex, lock file, EULA, `-level` / `-car` defaults ("sf", "vpcoop"), heap: Windows plumbing; the original's options are parsed by `CommandLine` (infrastructure audit) |
| `MainPhase` | replaced | `App::run` (`ctx.nextScreen`), `FrontendScreen`, `RaceScreen` | frontend phase: the interface, `ShowMain(1)` after a race (the frontend audit's post-race pages); race phase: the replay manager and the game manager; CD track 2 while loading is part of the open `mmCDPlayer` item ([../session.md](../session.md)) |
| `GameLoop` | ported | `App::run` | per frame: time step, input, messages, audio manager, the tree's update, the cull manager |
| `BeginPhase` | replaced | Context / renderer / mixer persist; `RaceScreen` loading state | the visible part is the loading picture and its bar (below) |
| `EndPhase` | replaced | RAII (`RaceScreen`, `FrontendScreen`) | tears down the engine subsystems |
| `GetLoadScreenName` | ported (new) | `RaceScreen::RaceScreen` | `<city>_` + roam / race%d / multicop / circuit%d / blitz%d / crash%d, else "loading"; Cops and Robbers' `multicop` was missing |
| `ProgressCB` | ported (new) | `RaceScreen::loadStep`, `drawOverlay` | race phase: a bar at (0.55 W, 0.896 H), 0.02 H tall, percent x 0.01 x 0.4234375 W long; the race now loads over several frames (city, level, session and car, AI map, effects and cars, HUD) with the bar at 10, 30, 70, 100, then 50, 100 for the AI map (the values after OpenMM2's parts are inferred). The frontend branch (349, 448, percent x 640 / 284) is the frontend audit's; its first 10 % step is missing (frontend) |
| `ProgressRect` | ported (new) | `RaceScreen::drawOverlay` | 0xFF0D2CBA reduced to a 16-bit surface's (8, 44, 184); a 32-bit surface would get white |
| `RestoreFocus` | ported (new) | `Screen::activated`, `RaceScreen::activated` | the surfaces back after another application took the screen: `mmGameManager::ForcePopupUI` in a running single-player game; OpenMM2: the window activated again in a full-screen mode (inferred); the "reloading" bitmap is not needed |
| `GetHostCars`, `IsStock` | not needed | — | `IsStock` always returns 1, so the host's add-on car list is always empty |
| `LogRandomCall` | not needed | — | empty (the replay's random-call log) |
| `ebolaPlayMovie` | ported | `IntroScreen` | rendering-fx record |

## mmGameManager

Builds the game object for the mode (single player: 0 roam, 1 race, 3
circuit, 4 Blitz, 6 crash course; network: 0 roam, 1 race, 2 Cops and
Robbers, 3 circuit, 4 Blitz) after the vehicle and city lists and the
current city's reward locks, then runs the tree each frame.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmGameManager::mmGameManager`, `~mmGameManager`, `` `scalar_deleting_destructor' `` | ported | `Session::create`, `RaceScreen::load` | mode switch; `UnlockPlayerRewards(city)` for the in-game locks: see `mmRewardList` |
| `mmGameManager::Reset` | ported | `Session::start` / `restart` | |
| `mmGameManager::Update` | ported | `RaceScreen::update` / `drawScene` | order: the game, the level, the banger actives and the physics step, cameras, dashboard / map / mirror declarations (session record: frame order) |
| `mmGameManager::Cull` | ported | `RaceScreen::drawScene` | map mode 3 draws only the map; the letterbox skipped in split and full-screen map modes (HUD audit) |
| `mmGameManager::ForcePopupUI` | ported (new) | `RaceScreen::activated` | `ProcessEscape(1)` and a pause |
| `mmGameManager::ForceReplayUI` | not needed | — | only with a replay name, which the retail game never sets |

## mmStatePack

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmStatePack::SetDefaults` | ported | `Settings`, `RaceConfig`, `Hud` / `PlayerCameras` defaults, `Session` (cheat flag) | traffic 0.33, pedestrians 1, cops 1, opponents 8, automatic, noon / clear, view defaults; MM2's default city is "sf" and car "vpcoop" (OpenMM2 starts in London: frontend) |
| `mmStatePack::ParseStateArgs` | not needed | — | retail stub returning false |
| `mmStatePack::mmStatePack`, `~mmStatePack` | not needed | — | `dgStatePack` constructor (NumberOfCTFRacers 0), start slots cleared |

## mmGame

The game base: `Init` (player, level, physics, icons, `InitGameObjects`,
gizmos, `aiMap::Init` + `Reset`, `InitOtherPlayers`, `InitHUD`, popup,
weather), `Update` (input, the mode's rules, audio, music, water and fall,
movers, `aiMap::Update`), `Reset` (a restart).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmGame::mmGame`, `~mmGame`, `` `scalar_deleting_destructor' `` | not needed | — | constants: auto-reverse 0.8 / 5, throttle cap 1, the eight icon colours (`hud::netIconColor`) |
| `mmGame::Init` | ported | `RaceScreen::loadStep` parts | order above; the physics sample 1/35 s, 3 samples (phys-core); the TEXTURED SKY option (integration); the results title is the frontend's |
| `mmGame::InitGameStrings` | ported | `Session::updateOpponents` | "Opponent 1..8" (13-20), "finished 1st..8th" (21-28) |
| `mmGame::InitGizmos` | open | — | world-objects subsystem: sailboat, bridge (proximity triggers, multiplayer London bridges state 3), train, ferry (single player), parked cars (not in multiplayer cruise or Cops and Robbers); `<city>_<type>_<mode><race>.pathset` before `<city>_<type>.pathset` |
| `mmGame::InitWeather` | ported | `RaceScreen::weatherFriction`, `carLights`, effects | lights for evening, night or fog; rain friction 0.75 at night else 0.8 |
| `mmGame::InitOtherPlayers` | ported (new) | `RaceSetup::playerDrop`, `SimVehicle::settleOnGround` (vehicle audit) | from the body 2 m up to 10 m down, 0.9 m above the hit; OpenMM2 put the model origin on the ground (0.7 m lower) |
| `mmGame::CollideAIOpponents` | ported (new) | `RaceScreen::spawnOpponents` | the same probe from each racer's model origin |
| `mmGame::FindGroundPos` | ported (new) | `game::session::findGroundPos` | 7.5 m up to 15 m down, the hit itself or the point; only the multiplayer grids |
| `mmGame::RespawnXYZ` | ported | `randomIntersectionStart` | intersection `irand % (n - 1) + 1`; rejects rooms 0x24, and with its second argument rooms 0x0A and alley paths (0x2), with the first freeway paths (0x4); 2 m up, angle 0; the seeded form (multiplayer): the player id as seed and a counter of draws (deviation: OpenMM2's own stream; MM2's single-player start depends on every earlier draw of the seed-1 stream) |
| `mmGame::Reset` | ported | `Session::restart`, RaceScreen `Restart` | props (props audit), cars to their reset places, water timer, race-over flag, pre-race speech, music restart (audio audit), elasticity cap; `aiMap::Reset` (ambient traffic, pedestrians, lights, seed 1) open: ai subsystem |
| `mmGame::StartMusic`, `UpdateDMusic` | ported | `MusicDirector` | audio record |
| `mmGame::Update` | ported | `RaceScreen::update`, `Session::update` | the water flag is the splash's latched active flag (new: was the depth each frame); water and fall checked after the ending too (new); one-step offset accepted |
| `mmGame::DropThruCityHandler` | ported | `Session::dropThroughCity` | below y = -50: the mode's Reset (single player), the water handler (multiplayer); string 29 only goes to a debug print |
| `mmGame::HitWaterHandler` | ported | RaceScreen `Respawn` | `mmPlayer::Reset`, back to the reset place |
| `mmGame::UpdateDebugInput` | ported (new) | `RaceScreen::update`, `debugKeys` | Escape stops the announcer then `ProcessEscape`; F1 the keymap; F4 restart (single player); F6 roster (network); Ctrl+Alt+Shift+F7 the single-player chat line; F2's pause is not ported (open: OpenMM2's fly camera holds F2) |
| `mmGame::UpdatePaused` | ported | `RaceScreen::updateGameInput` | C / V camera keys; F1 while paused (new); F2 unpause open (above) |
| `mmGame::UpdateHorn` | ported | `PlayerCarAudio::updateHorn` | the player's siren lights (vpcop, vpsemi) are not drawn: open (rendering) |
| `mmGame::UpdateGameInput` | ported | `RaceScreen::updateGameInput` | session record; the CD player events are the open `mmCDPlayer` item |
| `mmGame::UpdateSteeringBrakes` | ported | `ArcadeControls`, `RaceScreen::updatePlayer` | inputs read back from the replay buffer as bytes (input audit's `replayQuantize`); multiplayer forward-gear throttle cap |
| `mmGame::SetIconsState` | ported | `Hud::toggleOpponentIcons` | |
| `mmGame::PlayerSetState` | replaced | settings / profile | |
| `mmGame::BeDone` | replaced | `RaceScreen::leaveRace`, `storeViewSettings` | the driver's view settings stored (also on Exit, new) |
| `mmGame::CalculateRaceScore` | ported | `Session::result` | |
| `mmGame::SendChatMessage` | ported | `RaceScreen::sendChatMessage` | "/blubber" |
| `mmGame::FarClipCB`, `SetLevelGraphics` | ported | `RaceScreen::loadLevelPart` | |
| `mmGame::CycleCam` | not needed | — | only the replay popup calls it |
| `mmGame::NetHost` | replaced | `NetGame::isHost` | |

## mmGameSingle

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmGameSingle::mmGameSingle`, `~mmGameSingle`, `` `scalar_deleting_destructor' `` | not needed | — | |
| `mmGameSingle::Init`, `Update` | ported | (`mmGame::Init`, `Update`) | jumps |
| `mmGameSingle::InitOtherPlayers` | ported (new) | as `mmGame::InitOtherPlayers` | the circuit's |
| `mmGameSingle::Reset` | ported | `Session::resetRace` | racers' slots, results cleared, `mmGame::Reset` |
| `mmGameSingle::EnableRacers` | ported (new) | `Session::go`, `playerDamageEnabled` | the player's damage switched on at "Go!" (new), racers drivable with damage |
| `mmGameSingle::DisableRacers` | ported (new) | `Session::resetRace`, `playerDamageEnabled` | the player's damage off until "Go!" (OpenMM2 left it on) |
| `mmGameSingle::GetWaypoints` | ported | `Session::checkpoints` | |
| `mmGameSingle::HitWaterHandler` | not needed | — | every single mode overrides it |
| `mmGameSingle::UpdateGame`, `UpdateGameInput`, `UpdateDebugKeyInput` | not needed | — | empty |
| `mmGameSingle::UpdateRewards` | ported | `RaceScreen::announceResults` | session record (OpenMM2 falls back to the results line if the unlock line cannot load) |

## mmGameHUD

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmGameHUD::~mmGameHUD`, `` `scalar_deleting_destructor' `` | not needed | — | an empty node |

## mmPlayer

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmPlayer::mmPlayer`, `~mmPlayer`, `` `scalar_deleting_destructor' `` | not needed | — | constants in `Controls` / `CamTrack` |
| `mmPlayer::Init` | ported | `RaceScreen::loadVehicle`, `loadAudio`, `SimVehicle::loadPlayer`, `PlayerCameras::load` | trailer except multiplayer cruise / Cops and Robbers; vpcop on vpmustang99's tune; force feedback by the input audit |
| `mmPlayer::Reset` | ported | `SimVehicle::reset`, `PlayerCameras::reset`, Respawn / Restart | the player's siren and car audio reset: audio |
| `mmPlayer::Update` | ported | `CamPlayer`, `CarSim`, `RaceScreen::updateAudio` | |
| `mmPlayer::ImpactCallback`, `SetGameCallback` | ported | `RaceScreen::playerImpact` | |
| `mmPlayer::InitSpeechAudio` | ported | `Voices` | |
| `mmPlayer::SetPreRaceCam`, `SetPostRaceCam`, `SetMPPostCam` | ported | `PlayerCameras` | which endings set the post-race camera: `Session::postRaceCamera` (new) |
| `mmPlayer::IsPOV`, `SetWideFOV`, `SetCamera`, `GetCamera`, `GetCurrentCameraPtr`, `GetNextCycleCamIndex`, `GetNextCycleXCamIndex`, `GetCurrentGameCamIndex`, `GetCurrentXCamIndex` | ported | `PlayerCameras` | camera-props / HUD records |
| `mmPlayer::SetSteering`, `FilterSteering` | ported | `ArcadeControls`, `AnalogSteering` | |
| `mmPlayer::IsMaxDamaged`, `ResetDamage`, `EnableRegen`, `UpdateRegen` | ported | `CarDamage::maxDamaged`, DamageReset, `m_regen`, `CarSim::regenerate` | |
| `mmPlayer::UpdateHOG` | not needed | — | never fires (vehicle record) |
| `mmPlayer::FFImpactCallback`, `UpdateFF`, `ResetFF` | ported | input audit | force feedback |
| `mmPlayer::FileIO` | ported | defaults | |
| `mmPlayer::AfterLoad`, `BeforeSave` | not needed | — | empty |

## mmSingleRoam (cruise)

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmSingleRoam::mmSingleRoam`, `~mmSingleRoam`, `` `scalar_deleting_destructor' `` | not needed | — | |
| `mmSingleRoam::Init`, `InitMyPlayer`, `InitHUD` | ported | `Session::create`, `RaceScreen::loadVehicle` | |
| `mmSingleRoam::InitGameObjects` | ported | music `startRace(cruise)` | "singleroam"; Messagenote loaded, never played |
| `mmSingleRoam::InitOtherPlayers` | ported (new) | `RaceSetup` (`StartDrop::None`) | 2 m above the intersection, angle 0, no probe (OpenMM2 dropped it onto the ground) |
| `mmSingleRoam::Reset` | ported | `Session::resetRace` | the same start (no new draw) |
| `mmSingleRoam::UpdateGame`, `SwitchState` | ported | `Session::updateRace` | a wreck: undrivable 3 s, then repaired |
| `mmSingleRoam::HitWaterHandler` | ported | `Session::hitWater` -> `restart` | |
| `mmSingleRoam::Update`, `UpdateGameInput`, `UpdateDebugKeyInput` | not needed | — | jump / empty |
| `mmSingleRoam::NextRace` | not needed | — | only the results' Next Race, which cruise never shows |

## mmSingleRace (checkpoint race)

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmSingleRace::mmSingleRace`, `~mmSingleRace`, `` `scalar_deleting_destructor' `` | not needed | — | |
| `mmSingleRace::Init`, `InitMyPlayer`, `InitHUD`, `GetWaypoints`, `Update` | ported | `RaceSetup`, `Hud` | |
| `mmSingleRace::InitGameObjects` | ported | `RaceSetup::playerPlace`, `gameSoundVolume` | start place; `aiMap::SetWaypoints` (ai) |
| `mmSingleRace::InitOtherPlayers` | ported (new) | as `mmGame::InitOtherPlayers` | |
| `mmSingleRace::Reset` | ported | `Session::resetRace`, Restart | pre-race camera, timers, engine un-silenced |
| `mmSingleRace::UpdateGame`, `SwitchState` | ported | `Session::updateCountdown` / `updateRace` | the post-race lock (new, `mmPopup::Lock`), the music stopped only at the finish and the wreck (new) |
| `mmSingleRace::UpdateGameInput` | ported | `Session::cycleTarget` | |
| `mmSingleRace::UpdateOpponentStatus`, `FinishMessage` | ported | `Session::updateOpponents` | |
| `mmSingleRace::UpdateScore` | ported | `Session::updateRank`, `opponentPlace` (HUD audit) | the opponents' icon places |
| `mmSingleRace::HitWaterHandler` | ported | `Session::hitWater` | no post-race camera, music left playing (new); also after the ending (new) |
| `mmSingleRace::RegisterFinish` | replaced | frontend `recordResult` | registered when the race is left (deviation, session record); the Hall of Fame "passed" flag is not set (frontend) |
| `mmSingleRace::ProgressCheck` | ported | `Session::result` | |
| `mmSingleRace::NextRace`, `NextRaceAvailable` | ported | `ResultsPage` | Next needs a driver in MM2; MM2 does not set the pedestrian density (frontend) |
| `mmSingleRace::UpdateDebugKeyInput` | not needed | — | empty |

## mmSingleCircuit

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmSingleCircuit::mmSingleCircuit`, `~mmSingleCircuit`, `` `vector_deleting_destructor' `` | not needed | — | |
| `mmSingleCircuit::Init`, `InitMyPlayer`, `InitHUD`, `GetWaypoints`, `Update` | ported | `RaceSetup`, `Hud` | |
| `mmSingleCircuit::InitGameObjects` | ported | `RaceSetup` | |
| `mmSingleCircuit::InitOtherPlayers` | ported (new) | `mmGameSingle::InitOtherPlayers` | |
| `mmSingleCircuit::Reset` | ported | `Session::resetRace` | |
| `mmSingleCircuit::UpdateGame`, `SwitchState` | ported | `Session::updateRace` | the finish turns the player's damage off (new) |
| `mmSingleCircuit::UpdateOpponentStatus`, `FinishMessage` | ported | `Session::updateOpponents` | no opponent line during the wreck penalty (new) |
| `mmSingleCircuit::UpdateScore` | ported | `Session::updateRank` | the readout's timing quirks after the finish: open (low) |
| `mmSingleCircuit::RegisterLap` | ported | `Session::updateWaypoints` | |
| `mmSingleCircuit::RegisterFinish` | replaced | frontend | as the race |
| `mmSingleCircuit::ProgressCheck`, `NextRace` | ported | `Session::result`, `ResultsPage` | |
| `mmSingleCircuit::HitWaterHandler` | ported | `Session::respawnAtLastCheckpoint`, `SimVehicle::respawnAt` | the map's mode reset (`mmHudMap::Reset`) open (low) |
| `mmSingleCircuit::UpdateGameInput`, `UpdateDebugKeyInput` | not needed | — | empty |

## mmSingleBlitz

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmSingleBlitz::mmSingleBlitz`, `~mmSingleBlitz`, `` `scalar_deleting_destructor' `` | not needed | — | card constants ported |
| `mmSingleBlitz::Init` | ported | `Hud::drawIcons` | the cards' 4-vertex kites (HUD audit) |
| `mmSingleBlitz::InitMyPlayer`, `GetWaypoints`, `Update` | ported | `RaceScreen::loadVehicle`, `Session::checkpoints` | |
| `mmSingleBlitz::InitGameObjects` | ported | `RaceSetup` | |
| `mmSingleBlitz::InitOtherPlayers` | ported (new) | as `mmGame::InitOtherPlayers` | |
| `mmSingleBlitz::InitHUD` | ported | `Hud` | its checkpoint labels are never drawn (labels only in network games; HUD audit) |
| `mmSingleBlitz::Reset` | ported | `Session::resetRace` | |
| `mmSingleBlitz::UpdateGame`, `SwitchState` | ported | `Session::updateRace` | `DeactivateFinish` at the wreck and the late finish (new); the late finish keeps the camera and the music (new) |
| `mmSingleBlitz::PlayTimerWarning` | ported | `Session::timerWarning` | |
| `mmSingleBlitz::FinishMessage`, `ProgressCheck`, `NextRace` | ported | `Session::updateRace`, `Session::result`, `ResultsPage` | |
| `mmSingleBlitz::HitWaterHandler` | ported | `Session::hitWater` | |
| `mmSingleBlitz::RegisterFinish` | replaced | frontend | |
| `mmSingleBlitz::UpdateGameInput` | ported | `cycleTarget` (type 3: nothing) | |
| `mmSingleBlitz::UpdateDebugKeyInput` | not needed | — | |

## mmSingleStunt (crash course lessons)

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmSingleStunt::mmSingleStunt`, `~mmSingleStunt`, `` `vector_deleting_destructor' `` | not needed | — | corner speed 50, grace 1, destroy MaxDamage 150000 |
| `mmSingleStunt::Init`, `InitMyPlayer`, `Update` | ported | `RaceSetup`, `RaceScreen::loadVehicle` | |
| `mmSingleStunt::InitGameObjects`, `LoadEventFile` | ported | `RaceSetup` (lesson events) | the last row's AmbDensity x 0.2 is the traffic |
| `mmSingleStunt::InitNewEvent` | ported | `Session::beginEvent` | the player is not moved; Collide and Accel replay their countdown in a later event (new) |
| `mmSingleStunt::InitOtherPlayers` | ported (new) | `mmGame::InitOtherPlayers` | opponent icon 0 shown for Follow / Accel / Destroy |
| `mmSingleStunt::InitHUD` | ported | `Hud`, `Session::beginEvent` | |
| `mmSingleStunt::Reset` | ported | `Session::resetRace` | |
| `mmSingleStunt::EnableRacers` | ported | `enableLessonOpponents`, `playerDamageEnabled` | a jump lesson is driven without damage (new: UpdateJump never calls EnableRacers) |
| `mmSingleStunt::GetOpponentIndex` | ported | `lessonOpponentOffset` | |
| `mmSingleStunt::UpdateGame`, `UpdateJump`, `UpdateCollide`, `UpdateChase`, `UpdateEvade`, `UpdateCorner`, `UpdateFrogger`, `UpdateAccel`, `UpdateBlitz`, `UpdateStop`, `CheckTimeUp`, `HUDMessage`, `EventSoundCtrl`, `PlayTimerWarning` | ported | `Session::updateLesson` | new: the race-over flag only where they set it, the post-race camera per ending, `DeactivateFinish` on the endings that call it, the wreck pause leaves the car drivable |
| `mmSingleStunt::CheckCopPursuit` | ported | `Session::copPursuit` | "pursuing" is `aiPoliceOfficer::InPersuit` (any chase or a wrecked cop, new) |
| `mmSingleStunt::HitWaterHandler` | ported | `Session::hitWater` | unless race-over is set, also after a failure (new) |
| `mmSingleStunt::RegisterFinish` | replaced | frontend `recordResult` | recorded when leaving; a failed lesson restarted from the menu is not recorded (deviation, session record) |
| `mmSingleStunt::NextRace`, `NextRaceAvailable` | ported | `ResultsPage` | the school car keeps the paint job in MM2 (frontend) |
| `mmSingleStunt::SwitchState` | ported | Session phases | |
| `mmSingleStunt::UpdateGameInput`, `UpdateDebugKeyInput` | not needed | — | empty |

## CrashCourse (the Crash Course page)

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `CrashCourse::CrashCourse`, `~CrashCourse`, `` `scalar_deleting_destructor' `` | ported | `CrashCoursePage` | |
| `CrashCourse::PreSetup`, `GameCallback`, `SetRaceState`, `ChangeLocalVals`, `FocusDescription` | ported | `CrashCoursePage` | |
| `CrashCourse::SetEnvironment` | ported | `applyRaceTableDefaults` | lessons: traffic 0, opponents 8, cops 1 |
| `CrashCourse::SetProgressMask`, `SetCheckpointMask` | ported | `Progress::openMask` | |
| `CrashCourse::SetRaceGrade` | replaced | `CrashCoursePage::drawAbove` | grades from the profile |
| `CrashCourse::IncRaceName`, `DecRaceName`, `SetVehicleNext` | ported | `ui::stepOption`, GO sprite | |

## mmWaypoints, mmWaypointObject, mmWaypointInstance, mmCheckpointInstance

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmWaypoints::mmWaypoints`, `~mmWaypoints`, `` `scalar_deleting_destructor' `` | not needed | — | |
| `mmWaypoints::Init`, `LoadCSV`, `ReInit`, `InitStatic` | ported | `buildCheckpoints`, `loadRaceSetup` | lesson stands 15 deep (new) |
| `mmWaypoints::Reset`, `ResetAllTags` | ported | `Session::resetWaypoints` | |
| `mmWaypoints::Update`, `ClearWaypoint`, `DisplayHUDMessage` | ported | `Session::updateWaypoints`, `displayCleared` | the final stretch's music and line (audio audit); the circuit readout after the final lap (new) |
| `mmWaypoints::AIWPHit`, `AnyWPHits` | ported | `Session::updateOpponents` | |
| `mmWaypoints::SetCurrentGoals`, `GetClosestWaypoint`, `CycleCurrentWaypoint`, `GetNextWaypoint`, `GetLastWaypoint`, `SetArrow`, `GetStart`, `GetStartAngle`, `GetWaypoint`, `GetHeading` | ported | `Session` | |
| `mmWaypoints::DeactivateFinish` | ported (new) | `Session::deactivateFinish` | the last stand hidden at the Blitz wreck / late finish, multiplayer Blitz time-up and the lesson failures that call it |
| `mmWaypoints::GenerateHitRooms`, `GetHitRoom` | not needed | — | the rooms are stored, never read |
| `mmWaypoints::Cull` | open | `Hud::drawStands` | stands in rooms not drawn get the H mesh (VL far away) and the last waypoint is drawn only with its room: rendering |
| `mmWaypointObject::mmWaypointObject`, `~mmWaypointObject`, `` `scalar_deleting_destructor' `` | ported | `makeCheckpoint` | |
| `mmWaypointObject::Activate`, `Deactivate`, `Reset`, `GetDrawFlag`, `GetHitFlag`, `SetHitFlag`, `SetPos` | ported | `Session::Waypoints`, `Checkpoint` | |
| `mmWaypointObject::CalculateGatePoints`, `LineIntersect`, `PlaneHit`, `RadiusHit` | ported | `Gate` | |
| `mmWaypointObject::Move` | ported | `hud::standMatrix` | |
| `mmWaypointObject::SetRadius` | ported (new) | `Checkpoint::standDepth` | only the width changes: lesson stands stay 15 deep |
| `mmWaypointObject::SetHeading` | open | `buildCheckpoints` | MM2 leaves a zero-heading stand it turns 3.75 m lower; no retail race is affected |
| `mmWaypointObject::Update` | not needed | — | the drawn flag for `Cull`; the powerup's room |
| `mmWaypointInstance::mmWaypointInstance`, `~mmWaypointInstance`, `GetMatrix`, `SetMatrix`, `GetPosition` | not needed | — | OpenMM2 recomputes the matrix |
| `mmWaypointInstance::MakeVisible`, `MakeInvisible`, `SetVariant` | ported | `Session::checkpointVisible`, `Hud::drawStands` | |
| `mmCheckpointInstance::mmCheckpointInstance`, `SizeOf` | not needed | — | |
| `mmCheckpointInstance::Init`, `Draw` | ported | `Hud::drawStands` | |

## mmRaceData

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmRaceData::mmRaceData`, `~mmRaceData` | not needed | — | |
| `mmRaceData::Load` | ported | `city::parseRaceTable` | |
| `mmRaceData::GetNumRaces`, `GetNumLaps`, `GetTimeLimit`, `GetNumOpponents`, `GetNumCops`, `GetTimeOfDay`, `GetWeather`, `GetPedDensity`, `GetAmbientDensity`, `GetDifficulty` | ported | `RaceSettings`, `applyRaceTableDefaults` | |

## mmGameMulti

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmGameMulti::mmGameMulti`, `~mmGameMulti`, `` `scalar_deleting_destructor' `` | not needed | — | |
| `mmGameMulti::Init` | ported (new) | `RaceScreen::loadAi`, `loadRaceSetup` | traffic, cop and opponent densities 0 and no rail cars; the race modes clear mmGame's AI flag (no AI map at all) |
| `mmGameMulti::InitGameStrings` | ported (new) | `RaceScreen::updateNetPlayers` | "has left the game" (38); the "Waiting for N players" lines belong to the ready gate (replaced) |
| `mmGameMulti::InitOtherPlayers` | replaced | `RaceScreen::updateRemoteCars` | |
| `mmGameMulti::InitRoster` | ported (new) | `Popup::Roster` | |
| `mmGameMulti::StartXYZ` | ported | `multiplayerGridOffset`, `RaceScreen::startSlot` | slot = the place in the host's player list (new; was the player id); slots 8-15 have no place (OpenMM2 allows 16) |
| `mmGameMulti::SetFinishCam` | ported | `RaceScreen::startFinishCamera` | only at a finish (new) |
| `mmGameMulti::Update` | open | — | the cheater lock (with the tuning CRC below) |
| `mmGameMulti::UpdateGame` | replaced | `NetGame::update`, `sendLocalState` | |
| `mmGameMulti::UpdateScore` | ported (new) | `Session::updateNetRace`, `netRacerPlace` | the standings and each other player's icon rank; waypoint counts as `CheckpointReached` events |
| `mmGameMulti::ClearRank` | not needed | — | the results are a frontend page |
| `mmGameMulti::SortResults`, `UpdateResults` | ported (new) | `Session::addNetResult`, `result` | by time, DNF (24 h) last; the frontend does not show multiplayer results yet (open, frontend) |
| `mmGameMulti::SendFinishReq`, `SendFinishAck` | replaced | `NetGame::sendFinish` (`RaceFinished`) | each machine broadcasts its finish (inferred equivalent) |
| `mmGameMulti::EnableRacers`, `DisableRacers` | ported (new) | `Session::go`, `playerDamageEnabled` | the player's damage off before "Go!" |
| `mmGameMulti::HitWaterHandler` | ported | `Session::hitWater`, `respawnAt` | the map mode reset open (low) |
| `mmGameMulti::SendHitWater` | open | — | the respawned car should snap on the other machines (a flagged snapshot) |
| `mmGameMulti::DropThruCityHandler` | ported | `Session::dropThroughCity` | |
| `mmGameMulti::BeDone` | ported (new) | `RaceScreen::leaveRace` | the host leaving takes everyone to the lobby (`NetGame::returnToLobby` had no caller: one race per session) |
| `mmGameMulti::QuitNetwork` | replaced | `NetGame::leave` | |
| `mmGameMulti::GameMessageCB` | replaced | NetGame events, `RaceScreen::updateNetRace`, `postIncomingChat` | chat with the net alert (new), finishes (new); time, host cars, boot: OpenMM2's own messages |
| `mmGameMulti::SystemMessageCB` | ported (new) | `RaceScreen::updateNetPlayers` | a player leaving; joining in progress and host migration are replaced (OpenMM2 has neither) |
| `mmGameMulti::BootPlayerCB` | ported (new) | `Popup::Roster` -> `NetGame::kick` | |
| `mmGameMulti::RegisterMapNetObjects`, `ActivateMapNetObject`, `DeactivateMapNetObject` | ported (new) | `RaceScreen` blips (HUD audit), places 0 / 10 | the other players on the map and their cards, by start slot |
| `mmGameMulti::GetInactiveNetObjectIndex`, `GetNetObject`, `GetNetObjectIndex`, `ClearNetObjects` | replaced | `m_remotes` | |
| `mmGameMulti::PlayerFinishedLoading`, `PlayerClearLoaded`, `SendRaceReady`, `SendGameSet`, `SendTimeMsg` | replaced | the shared start time, ClockSync | no ready gate (deviation, session record) |
| `mmGameMulti::SendPosition`, `SendMsg` | replaced | snapshots, `sendEvent` | |
| `mmGameMulti::GetCarCRC`, `SendCheater` | open | — | the tuning CRC check (low) |
| `mmGameMulti::BroadCastCarTuning`, `SendCarTuning` | not needed | — | `SendCarTuning` is empty |
| `mmGameMulti::SendHostCars`, `SendLobbyResults`, `SendToChatMessage` | not needed | — | no add-on cars; Zone only; the "/rc" reply |
| `mmGameMulti::ParseChatMessage`, `SendChatMessage` | ported | `sendChatMessage`, `postIncomingChat` | |
| `mmGameMulti::NextRace` | replaced | the lobby | |
| `mmGameMulti::Reset` | replaced | — | no in-race multiplayer restart |
| `mmGameMulti::UpdateDebugKeyInput` | not needed | — | |

## mmMultiRoam, mmMultiRace, mmMultiCircuit, mmMultiBlitz

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmMultiRoam::mmMultiRoam`, `~mmMultiRoam`, `` `scalar_deleting_destructor' ``, `mmMultiRace::mmMultiRace`, `~mmMultiRace`, `` `scalar_deleting_destructor' ``, `mmMultiCircuit::mmMultiCircuit`, `~mmMultiCircuit`, `mmMultiBlitz::mmMultiBlitz`, `~mmMultiBlitz`, `` `scalar_deleting_destructor' `` | not needed | — | timeouts 60 (race) / 120 (circuit) |
| `mmMultiRoam::Init`, `mmMultiRace::Init`, `mmMultiCircuit::Init`, `mmMultiBlitz::Init` | ported (new) | `loadAi`, music, `scaleMass` | no AI map in the races; the circuit's barricades 26 times as heavy (new) |
| `mmMultiRoam::InitMyPlayer`, `mmMultiRace::InitMyPlayer`, `mmMultiCircuit::InitMyPlayer`, `mmMultiBlitz::InitMyPlayer` | ported | `loadVehicle` | |
| `mmMultiRoam::InitNetworkPlayers`, `mmMultiRace::InitNetworkPlayers`, `mmMultiCircuit::InitNetworkPlayers`, `mmMultiBlitz::InitNetworkPlayers` | ported | `loadVehicle` (grid, ground), `updateRemoteCars` | the remote cars are replaced by snapshots |
| `mmMultiRoam::InitGameObjects`, `mmMultiRace::InitGameObjects`, `mmMultiCircuit::InitGameObjects`, `mmMultiBlitz::InitGameObjects` | ported | `RaceSetup`, sounds | |
| `mmMultiRoam::InitHUD`, `mmMultiRace::InitHUD`, `mmMultiCircuit::InitHUD`, `mmMultiBlitz::InitHUD` | ported | `Hud` | the place readout now counts the network players (new); Blitz's place readout and no checkpoint cards (HUD audit) |
| `mmMultiRoam::Reset` | ported | `RaceSetup` start | runs only at the start (a second seeded draw in MM2) |
| `mmMultiRace::Reset`, `mmMultiCircuit::Reset`, `mmMultiBlitz::Reset` | replaced | the lobby | no in-race restart |
| `mmMultiRoam::UpdateGame`, `SwitchState` | ported | `Session::start` / `updateRace` | "Go!" and the car released at once (new: no shared start in cruise) |
| `mmMultiRace::UpdateGame`, `SwitchState`, `mmMultiCircuit::UpdateGame`, `SwitchState`, `mmMultiBlitz::UpdateGame`, `SwitchState` | ported | `Session::updateRace`, `updateNetRace` | countdown, wreck penalty (no warning, finish or clock check during it, new), finish line, the wait for all finishers and the timeout (new); the ready gate and the end taper are replaced (session record) |
| `mmMultiRace::SetTimeoutOn`, `SetTimeoutOff` | ported (new) | `Session::addNetResult`, `updateNetRace` | 60 s from the first finish ("Race over", DNF) |
| `mmMultiBlitz::PlayTimerWarning` | ported | `Session::timerWarning` | |
| `mmMultiRoam::GameMessage`, `mmMultiRace::GameMessage`, `mmMultiCircuit::GameMessage`, `mmMultiBlitz::GameMessage` | ported (new) | `updateNetRace`, `Session::remoteFinished` | the finish lines (host 152 / 110 / 99, client 150 / 107 / 96) with Messagenote; the time-up / restart / host-quit messages are replaced |
| `mmMultiRoam::SystemMessage`, `mmMultiRace::SystemMessage`, `mmMultiCircuit::SystemMessage`, `mmMultiBlitz::SystemMessage` | ported (new) | `updateNetPlayers` | "<name>" / "has left the game" with the net alert |
| `mmMultiRoam::UpdateGameInput`, `mmMultiRace::UpdateGameInput`, `mmMultiCircuit::UpdateGameInput`, `mmMultiBlitz::UpdateGameInput` | ported | `cycleTarget` | |
| `mmMultiRoam::GetWaypoints`, `mmMultiRace::GetWaypoints`, `mmMultiCircuit::GetWaypoints`, `mmMultiBlitz::GetWaypoints` | ported | `Session::checkpoints` | |
| `mmMultiCircuit::UpdateDebugKeyInput` | not needed | — | |

## mmMultiCR (Cops and Robbers) and mmPowerupInstance

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmMultiCR::mmMultiCR`, `~mmMultiCR`, `` `scalar_deleting_destructor' `` | not needed | — | steal threshold 250 |
| `mmMultiCR::Init` | ported | `setupCopsAndRobbers` | the cruise music ("singleroam", new); the first set from the shared seed (deviation) |
| `mmMultiCR::InitMyPlayer` | ported | `crTeam` | regeneration on |
| `mmMultiCR::InitHUD` | ported | `Hud` CR readouts | the per-player roster, the HUD gold indicator and the map's bank / hideout / gold markers are open (HUD) |
| `mmMultiCR::InitGameObjects` | ported | `Hud::drawCrObjects` | |
| `mmMultiCR::InitNetworkPlayers` | ported | `RaceSetup` start | team colours of the network players' icons (HUD audit) |
| `mmMultiCR::Reset` | replaced | the lobby | |
| `mmMultiCR::LoadCSV` | ported | `loadCrLocations` | fewer than three rows: every place an intersection (new; OpenMM2 turned the mode off) |
| `mmMultiCR::LoadSets`, `GetRandomIndex`, `ResetPositions` | not needed | — | `multicopsets.csv` exists in no retail city (ResetPositions would give the bank the hideout's z) |
| `mmMultiCR::GetNewSet`, `GetRandomPoints` | ported | `CopsAndRobbers::newSet`, `randomPoint` | intersections never in rooms 0x24 (new); MM2's picker seeds its own stream from the clock (deviation: the shared seed) |
| `mmMultiCR::UpdateGame`, `SwitchState` | ported | `updateNetwork`, Session | |
| `mmMultiCR::UpdateGold` | ported (new) | `updateNetwork` | the gold's room must be the car's, a host alone cannot take it, the carrier does not see its gold |
| `mmMultiCR::UpdateBank`, `UpdateHideout` | ported (new) | `updateNetwork` | the base's room covered / underground or the car's |
| `mmMultiCR::StealGold`, `OppStealGold` | ported | `take`, `receive` | remote cars' mass is not changed (kinematic) |
| `mmMultiCR::DropGold`, `FindGround` | ported (new) | `drop`, `CrSettings::findGround` | stays unless in deep water; on the ground |
| `mmMultiCR::ImpactCallback` | ported (new) | `playerImpact` | a damaging hit's summed total, from a network car; "You dropped the gold!" only here |
| `mmMultiCR::FondleCarMass` | ported | `fondleMass` | |
| `mmMultiCR::HitWaterHandler`, `DropThruCityHandler` | ported (new) | `m_crWaterHandled` | the forced drop when the handler fires (5 s, or the fall), not on touching the water |
| `mmMultiCR::Score`, `UpdateHUD`, `UpdateLimit`, `UpdateTimeWarning` | ported | `CopsAndRobbers` | the limit line 3 s (new) |
| `mmMultiCR::DisplayTimeWarning` | ported | RaceScreen | 2 s (new) |
| `mmMultiCR::FillResults` | open | — | the results (team rows, players by score) need a multiplayer results page (frontend) |
| `mmMultiCR::GameMessage` | ported | `receive` | the net alert on 0x259 / 600 (new) |
| `mmMultiCR::SystemMessage` | ported (new) | `CopsAndRobbers::playerLeft` | the host drops a leaver's gold where it was |
| `mmMultiCR::SendGameState`, `SendGoldDrop`, `SendGoldAck`, `SendGoldDeliver`, `SendChangeSet`, `SendLimitReached`, `SendTimeWarning` | replaced | `sendCr`, NetGame events | |
| `mmMultiCR::SendLobbyResults` | not needed | — | Zone only |
| `mmMultiCR::UpdateGameInput`, `UpdateDebugKeyInput`, `GetWaypoints` | not needed | — | empty / null |
| `mmPowerupInstance::mmPowerupInstance`, `SizeOf` | not needed | — | |
| `mmPowerupInstance::Init`, `Draw` | ported | `Hud::drawCrObjects` | 3 rad/s spin, 1.5 m up; MM2 spins by the unclamped game clock and draws lit with the level LOD (cosmetic) |

## mmNetObject, mmNetPath, netZoneScore

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmNetObject::mmNetObject`, `~mmNetObject`, `` `scalar_deleting_destructor' ``, `ResetValues`, `Clear`, `Cull` | not needed | — | |
| `mmNetObject::Init`, `ReInit`, `SetCar`, `Set`, `SetActive`, `Activate`, `Deactivate` | replaced | `RaceScreen::updateRemoteCars` | the remote car's audio (`vehCar::InitAudio`) is open |
| `mmNetObject::SetPositionData` | replaced | `sendLocalState` | the siren flag is not sent (open) |
| `mmNetObject::PositionUpdate`, `InputUpdate`, `Predict`, `Update`, `GetPositionApproach` | replaced | snapshots, `updateRemoteCars` | kinematic (deviation, session record); damage, horn and siren of remote cars are open |
| `mmNetPath::mmNetPath`, `~mmNetPath`, `Compute`, `Solve`, `Reset` | replaced | the snapshot interpolation | |
| `netZoneScore::netZoneScore`, `~netZoneScore`, `InitResults`, `SendGameSettings`, `SendGameStart`, `SendGameStartStaging`, `SendGameEnd` | not needed | — | MSN Gaming Zone; its Init has no callers |

## mmPopup

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmPopup::mmPopup`, `~mmPopup`, `` `scalar_deleting_destructor' `` | ported | `RaceScreen::buildPopup` | |
| `mmPopup::IsEnabled` | ported | `m_popup` | |
| `mmPopup::ProcessEscape` | ported | `openPopup` | the pause music (new) |
| `mmPopup::DisablePU` | ported | `closePopup` | the return music where MM2 plays it (new) |
| `mmPopup::ProcessChat`, `ChatCB` | ported | `openChat`, the chat entry | the pause music on opening (new) |
| `mmPopup::ProcessKeymap` | ported (new) | `Popup::Keymap` | F1, the controls list |
| `mmPopup::ShowRoster` | ported (new) | `Popup::Roster` | F6 in a network game |
| `mmPopup::ForceRoster` | replaced | — | from the results page, which is the frontend's |
| `mmPopup::ShowResults` | replaced | frontend `ResultsPage` | the results do not pause in MM2 |
| `mmPopup::Lock`, `Unlock` | ported (new) | `popupLocked` | after a single-player race: Resume off, Escape nothing or the results |
| `mmPopup::Reset` | ported | Restart | unlocks |
| `mmPopup::Update` | ported | `buildPopup`, `updatePopup` | Exit ends the game at once (new: no question); F4 in the menu is not ported (low) |
| `mmPopup::PlayPauseMusic`, `PlayReturnMusic` | ported (new) | `popupMusic` | the song's pause segment and back; CITY SOUNDS' ambience stops and restarts |
| `mmPopup::ShowReplay`, `SaveReplay` | not needed | — | replays cannot be reached |

## mmReplayManager

The retail game cannot play or save replays (the main menu's REPLAY button
is turned off, PUMain has no Instant Replay), but the manager is the root of
the game tree: it records the player's inputs every frame and
`mmGame::UpdateSteeringBrakes` reads them back.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmReplayManager::Update` | ported | `RaceScreen::update` | the recording's quantisation (input audit), the pause and reset requests |
| `mmReplayManager::GetSteering`, `GetThrottle`, `GetBrakes`, `GetHandBrakes` | ported | `controls::replayQuantize` (input audit) | |
| `mmReplayManager::Reset` | ported | Restart | the global seed back to 1 is OpenMM2's deviation (per-subsystem streams) |
| `mmReplayManager::SetData` | not needed | — | read back only in playback |
| `mmReplayManager::GetData`, `EndOfReplay`, `StartReplay`, `ProcessCam`, `LoadReplay`, `LoadReplayDesc`, `SaveReplay`, `SaveReplayDesc`, `SetReplayDesc`, `SetReplayInfo`, `ReadReplayInfo`, `WriteReplayInfo`, `GetReplayInfo`, `Cull` | not needed | — | playback and files, unreachable ("REPLAY18" files of 16-byte frames) |
| `mmReplayManager::mmReplayManager`, `~mmReplayManager`, `` `scalar_deleting_destructor' `` | not needed | — | |

## mmRewardList, mmCityList, mmCityInfo, mmVehList, mmVehInfo, mmInfoBase

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmRewardList::Load`, `Init`, `GetRecord`, `UnlockPlayerRewards` | ported | `city::parseRewards`, `Progress` | in game only the current city's table locks (MM2); OpenMM2 reads sf and london (no retail effect) |
| `mmRewardList::CheckReward` | ported | `Progress::record`, `announceResults` | |
| `mmRewardList::mmRewardList`, `~mmRewardList` | not needed | — | |
| `mmCityInfo::Load` | ported | `parseCityInfo` | |
| `mmCityInfo::mmCityInfo`, `~mmCityInfo`, `` `scalar_deleting_destructor' `` | not needed | — | |
| `mmCityList::Load`, `LoadAll`, `GetCityID`, `GetCityInfo`, `SetCurrentCity` | ported | `Catalog`, frontend | |
| `mmCityList::GetCurrentCity` | replaced | `RaceConfig::city` | |
| `mmCityList::mmCityList`, `~mmCityList`, `` `vector_deleting_destructor' `` | not needed | — | |
| `mmVehInfo::Load`, `mmVehInfo` | ported | `parseVehicleInfo` | |
| `mmVehInfo::ComputeTuningCRC`, `GetTuningCRC` | open | — | the multiplayer car check (with `GetCarCRC`) |
| `mmVehInfo::IsValid` | not needed | — | only its address is tested |
| `mmVehInfo::~mmVehInfo`, `` `scalar_deleting_destructor' `` | not needed | — | |
| `mmVehList::Load`, `LoadAll`, `GetVehicleID`, `GetVehicleInfo` | ported | `Catalog` | an unknown name gives vpcoop in MM2 (camera-props record) |
| `mmVehList::SetDefaultVehicle` | ported | `Catalog` | |
| `mmVehList::mmVehList`, `~mmVehList`, `` `vector_deleting_destructor' `` | not needed | — | |
| `mmInfoBase::FileIO`, `Save`, `SetIOPath` | replaced | `Profile` INI files | |
| `mmInfoBase::mmInfoBase`, `~mmInfoBase`, `` `scalar_deleting_destructor' `` | not needed | — | |

## mmGameMusicData, mmSingleRaceMusicData, mmSingleRoamMusicData

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmGameMusicData::Load`, `LoadAmbientSFX`, `LoadAmbientSFXSegments`, `GetNumDMusicChoiceGroups`, `RandomizeNumber` | ported | `MusicPlayer`, `MusicTables` | audio record (the ambience only with music off, and London's for every city but "sf": audio) |
| `mmGameMusicData::LoadMusic` | not needed | — | the abstract base's |
| `mmSingleRaceMusicData::LoadMusic`, `LoadMusicSegments`, `mmSingleRoamMusicData::LoadMusic`, `LoadMusicSegments` | ported | `parseRace`, `parseCruise` | Cops and Robbers uses the cruise table (new) |

## Related classes

- `aiCTFRacer`: never created in build 3393 (`dgStatePack`'s NumberOfCTFRacers
  stays 0 and `aiMap::CTFOpponent` has no callers): an MM1 leftover, not
  needed.
- `mmCullCity`: no such class in build 3393 (MM1's; MM2 culls through
  `cityLevel`).

## Open items

| MM2 | What it needs |
| --- | --- |
| `mmGame::InitGizmos` | sailboats, bridges, trains, ferries, parked cars (world-objects subsystem) |
| `aiMap::Reset` in `mmGame::Reset` | the ambient traffic, pedestrians and lights back to their start (seed 1) on a restart (ai subsystem: an `ai::World::reset`) |
| F2 pause (`mmGame::UpdateDebugInput`, `UpdatePaused`) | F2 is OpenMM2's fly camera; MM2 pauses a single-player game (with the HUD off) |
| F4 inside the menu (`mmPopup::Update` state 6) | closes the menu and restarts |
| `mmHudMap::Reset` in the water handlers | the map mode back to the driver's |
| `mmSingleCircuit::UpdateScore` readout timing | the place shown only from "Go!" and counting later finishers after the finish |
| `mmGameMulti::SendHitWater` | snap the respawned car on the other machines |
| `mmGameMulti::GetCarCRC`, `SendCheater`, `mmVehInfo::ComputeTuningCRC`, `GetTuningCRC`, `mmGameMulti::Update` | the tuning CRC check and the cheater lock |
| `mmNetObject` audio, damage, horn, siren | remote cars' engine sounds, damage, horn and siren, and the siren flag in the snapshots |
| `mmGameMulti::SortResults` display, `mmMultiCR::FillResults` | a multiplayer results page (frontend) |
| `mmMultiCR::InitHUD` roster, gold indicator, map markers | the HUD audit's area |
| `mmWaypoints::Cull`, `mmWaypointObject::SetHeading` | the stands' LOD and the lowered zero-heading stands (rendering; no retail race affected by the latter) |
| `mmGame::UpdateHorn` siren lights | the player's vpcop / vpsemi siren lights are not drawn (rendering) |

## Notes for other areas

- frontend: the results page's Exit asks first (PUResults' Exit ends the
  game at once); the loading page lacks BeginPhase's first 10 % step;
  multiplayer results (standings with names are in `RaceResult`) and the
  Cops and Robbers results; the Hall of Fame "passed" flag; Next Race (a
  driver needed for checkpoint races, no pedestrian density for checkpoint
  and Blitz, the school car keeps the paint job); MM2's default city is San
  Francisco; the lobby still offers traffic / cops / opponents for
  multiplayer, which the race now ignores.
- ai: `aiMap::Reset` on a restart; `aiRouteRacer::Finished`'s finish line.
- audio: the ambience segment only with music off and London's for every
  city but "sf"; multiplayer modes load none.
- rendering: the stands' LOD; the player's siren lights.
- world-objects: `mmGame::InitGizmos`, and no rail cars in multiplayer
  (`mmGameMulti::Init`).
