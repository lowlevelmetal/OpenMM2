# Parity audit: session

Audited against MM2Recomp (midtown2.exe build 3393) on 2026-10-07.

Summary: 205 entries (a row may group closely related functions); verified
87, fixed 89, deviation 5, inferred 4, open 1, openmm2 19 (with round 3's
conventions and cruise-spawn changes). Missing: 17 MM2
features (11 open, 6 deviation).

A second pass (2026-10-08) wired the race-side items the first pass and
the other areas left open; its rows are in "Second pass" below, and the
Missing table is updated.

Scope: the game modes and race rules (`src/game/session/Session`,
`RaceSetup`, `Gate`, `CopsAndRobbers`, `Types`), the in-race HUD
(`src/game/session/Hud`), the race configuration and strings
(`src/game/RaceConfig.h`, `src/game/Strings`), the multiplayer game layer
(`src/game/net/NetGame`), the race loop (`src/app/RaceScreen.cpp`) and the
in-race controls (`src/app/Controls`, new). MM2 classes: `mmGame`,
`mmGameSingle`, `mmGameMulti`, `mmSingleRoam`, `mmSingleBlitz`,
`mmSingleCircuit`, `mmSingleRace`, `mmSingleStunt`, `mmMultiRoam`,
`mmMultiBlitz`, `mmMultiCircuit`, `mmMultiRace`, `mmMultiCR`, `mmWaypoints`,
`mmWaypointObject`, `mmPositions`, `mmHUD`, `mmCircuitHUD`, `mmWPHUD`,
`mmCollideHUD`, `mmExternalView`, `mmDashView`, `mmHudMap`, `mmArrow`,
`mmIcons`, `mmTimer`, `mmPopup`, `PUMain`, `PUExit`, `mmInput`, the parts of
`mmPlayer` that run the race, `mmRaceSpeech` / `mmCCSpeech` call sites.

Behaviour is documented in `docs/gamemodes.md`. Tests:
`tests/game/test_parity_session.cpp`, `tests/app/test_parity_controls.cpp`,
`tests/game/test_session.cpp`, `tests/game/test_session_hud.cpp`.

## Checkpoint gates (`src/game/session/Gate.{h,cpp}`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `calculateGatePoints` | `mmWaypointObject::CalculateGatePoints` | verified | position ± radius·(cos h, sin h) on (x, z), heading in degrees with MM2's conversion |
| `lineIntersect` | `mmWaypointObject::LineIntersect` | verified | slope/intercept form, vertical segment through its first point, parallel lines never meet, bounding boxes grown by the tolerance |
| `planeHit` | `mmWaypointObject::PlaneHit` | verified | the three tests (caller's segment with tolerance size.x, vertical axis ± size.y, lateral axis ± size.x with no tolerance) in MM2's order |
| `radiusHit` | `mmWaypointObject::RadiusHit` | verified | 3D distance below the radius |
| `playerGateHit` | `mmWaypoints::ClearWaypoint` (player test) | verified | nose to 2 m behind the tail, size (InertiaBox width / 2, height, length / 2) |
| `aiGateHit` | `mmWaypoints::AIWPHit`, `AnyWPHits` | verified | one InertiaBox length either side of the centre, InertiaBox × 5 as size |
| `makeCheckpoint` | `mmWaypointObject::mmWaypointObject` | verified | radius 0 → 15 m (`mmPositions` reads it with atoi) |

## Race setup (`src/game/session/RaceSetup.{h,cpp}`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `readText`, `setError` | — | openmm2 | file access glue |
| `raceMode` | `mmGameManager` mode switch | verified | game modes 1 checkpoint, 3 circuit, 4 Blitz, 6 crash course map to the city's race tables |
| `headingTowards` | `mmWaypoints::LoadCSV` / `ReInit` | verified | heading 0 means "towards the next waypoint" |
| `buildCheckpoints` | `mmWaypoints::LoadCSV`, `ReInit` | fixed | the zero-heading rule (start keeps its own, only circuits turn the last towards the first) verified; it no longer marks a finish stand (see `loadRaceSetup`) |
| `loadCheckpoints` | `mmPositions::Load` | verified | x, y, z, heading, radius (atoi), hit flag |
| `headingDirection` | `mmWaypoints::GetStartAngle` users | verified | (sin h, 0, −cos h) |
| `spawnAt` | `mmWaypoints::GetStart`, `GetStartAngle` | verified | rotation by −heading with MM2's degree factor |
| `respawnXYZ` (was `randomIntersectionStart`) | `mmGame::RespawnXYZ` | fixed | round 3 ([cruise-spawn](round3/cruise-spawn.md)): (counter + 1) draws a pick, rooms by FindRoomId of the centre, no retry cap (OpenMM2 only checks that some intersection fits) |
| `cruiseStart`, `respawnCounter` (was `nextRandom`) | `irand`, `mmSingleRoam` / `mmMultiRoam` / `mmMultiCR` start, mmGame's respawn counter | fixed | round 3: MM2's generator; single player from the global stream as aiMap::Reset left it, multiplayer two seeded picks |
| `loadRaceSetup`: race lookup, settings by difficulty | `mmRaceData`, `mmGameSingle::Init` | verified | amateur / professional rows |
| `loadRaceSetup`: finish stand | `mmWaypoints::LoadCSV` | fixed | `pt_finish` is waypoint 0 of a circuit and the last waypoint of a checkpoint race only; Blitz and lessons use `pt_check` everywhere (OpenMM2 marked the last waypoint in every mode) |
| `loadRaceSetup`: time limit and laps | `mmSingleBlitz::InitHUD`, `mmSingleCircuit::Init` | verified | clock in single-player Blitz only; laps from the menu or the table, at least 1 |
| `loadRaceSetup`: AI map | `aiMap::Init` | verified | the race's `.aimap` (`_p` for professionals), `roam.aimap` in cruise |
| `loadRaceSetup`: lesson events | `mmSingleStunt::LoadEventFile`, `InitHUD`, `InitNewEvent` | fixed | chkflags now counts when not 0 (MM2 passes chkflags != 0 to `mmWaypoints::ReInit`; OpenMM2 tested bit 0); cornerspeed below 1 → 50 verified |
| `loadRaceSetup`: opponents | `aiMap::Init`, `CrashCourse::SetEnvironment` | fixed | min(table count, OpponentDensity) racers; the crash course sets OpponentDensity to 8, so lessons load at most eight cars (OpenMM2 loaded every row) |
| `loadRaceSetup`: opponent grid place | `aiRouteRacer::Init` | verified | first `.opp` row, its fourth column in degrees × 0.017444445, not negated |
| `loadRaceSetup`: police | `aiRaceData::aiRaceData`, `aiPoliceOfficer::Reset` | fixed | posts turned by heading x −0.017444445, MM2's own degree factor (round 3, conventions: OpenMM2 used pi / 180); how many are placed is RaceScreen's (cop density) |
| `loadRaceSetup`: player start | `mmWaypoints::GetStart`, the cruise modes' `InitGameObjects` | verified | first waypoint; cruise at the InitGameObjects place until `Session::placeRespawnStart` (round 3); the first Blitz start without an AI map is an OpenMM2 fallback |

## Types, race configuration and strings (`src/game/session/Types.h`, `src/game/RaceConfig.h`, `src/game/Strings.{h,cpp}`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `kDefaultInertiaBox` | `vehCarSim` InertiaBox default | inferred | used only when a car has no tune |
| `PlayerState`, `OpponentState` | `mmPlayer`, `aiVehicleOpponent` fields the rules read | fixed | the rules now get the cars' phInertialCS matrices (vehCarSim +0x6C), as `mmWaypoints::Update` and `AIWPHit` read them, instead of the model matrices |
| `EventType` | — | openmm2 | the session's report to the race screen; `Sound` and `Speech` added for the modes' 2D sounds and the announcer |
| `GameSound`, `gameSoundName` | the modes' `InitGameObjects` (`AudSoundBase::Load`) | fixed | new: Startracelow, Startracehigh, Endofracetag, Youlose, Damgelose, Messagenote, Timerwarning, Waypoint, Lastwaypoint |
| `gameSoundVolume` | the same, `AudSoundBase::SetVolume` | fixed | new: 0.9 / 0.925 per sound, the crash course's own volumes |
| `SpeechCue` | `mmRaceSpeech` / `mmCCSpeech` call sites | fixed | new (see Session) |
| `MusicHint` | `MMDMusicManager` | inferred | the 3 s idle delay is OpenMM2's (audio area) |
| `HudMessage` | `mmHUD::SetMessage`, `SetMessage2` | verified | |
| `RaceConfig` | `mmStatePack` (dgStatePack) | verified | traffic, pedestrian, cop and opponent densities, time of day, weather, laps, difficulty |
| `RaceResult`, `RaceStanding` | `PUResults::AddName`, `mmGame::CalculateRaceScore` | verified | |
| `Strings::load`, `fromTable`, `get` | `AngelReadString` (the PE string table) | verified | |

## Session (`src/game/session/Session.{h,cpp}`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `modeText` | the modes' `UpdateGame` string ids | verified | every id compared per mode (single and multiplayer) |
| `lessonText` | `mmSingleStunt::Update*` countdown strings | verified | |
| `formatTime` | `GetLocTime` | verified | M:SS:HH, +0.005 then truncated, "  ---  " for none |
| `Session::create` | `mmGameManager` / the mode's `Init` | openmm2 | wiring |
| `str`, `setMessage`, `setMessage2` | `AngelReadString`, `mmHUD::SetMessage`, `SetMessage2` | verified | SetMessage clears the second line |
| `currentLesson`, `lessonOpponentOffset` | `mmSingleStunt` event index, `GetOpponentIndex` | verified | |
| `wantsMap` | `mmSingleStunt::UpdateEvade` | fixed | new: the Evade event turns the map on during its first countdown line |
| `opponentActive`, `enableLessonOpponents` | `mmGameSingle::EnableRacers`, `mmSingleStunt::EnableRacers` | verified | offset up to the event's numopp (the original's loop bound) |
| `rule` | `mmWaypoints` types 1-5 | verified | |
| `resetRace` | `mmGame::Reset`, `mmGameSingle::Reset`, the modes' `Reset` | fixed | the minimum-speed state and the race-over flag are reset only here (not per exam event); the timer warning sound is stopped; the engine is un-silenced |
| `beginEvent` | `mmSingleStunt::InitNewEvent`, `InitHUD`, the race modes' `InitGameObjects` | verified | clocks per lesson type |
| `resetWaypoints` | `mmWaypoints::Reset` | verified | target 1, count 1; the finish of a checkpoint race hidden |
| `start` | `mmGame::Reset`, `mmSingleRoam`, `mmMultiRoam::UpdateGame` state 1 | fixed | mmMultiRoam's "Go!" plays Startracehigh; `mmGame::Reset` asks the announcer for the pre-race line |
| `restart` | `mmGame::Reset` | verified | |
| `playerHold`, `playerHeld` | `vehCar::SetDrivable`, mmPlayer +0x2258 | fixed | was "held" for every post-race phase; now per ending: undrivable before "Go!", in wreck penalties, after a race/Blitz/jump/course wreck and any multiplayer ending; +0x2258 after the others; nothing after the water and the Clean and minimum-speed wrecks |
| `updateCountdown` | the modes' `UpdateGame` states 0-2, `mmSingleStunt::Update*` | fixed | circuits and checkpoint races wait for the pre-race camera (mmPlayer +0xE5A); multiplayer waits for the host's start; a later exam event enables its cars in state 0 and says "Go" one update later; sounds |
| `go` | the modes' state 2 → 3 | fixed | starts mmHUD's second timer too; Evade's later-event line (652) |
| `setTarget` | `mmWaypoints::SetCurrentGoals` | verified | |
| `closestTarget` | `mmWaypoints::GetClosestWaypoint` | verified | nearest shown, uncleared, 3D |
| `cycleTarget` | `mmSingleRace` / `mmMultiRace::UpdateGameInput` | fixed | only for waypoint type 2 (OpenMM2 also cycled Blitz and lesson targets) |
| `cycleCurrent` | `mmWaypoints::CycleCurrentWaypoint`, `GetNextWaypoint`, `GetLastWaypoint` | verified | OpenMM2 also stops below three waypoints (MM2 would index past the list) |
| `displayCleared` | `mmWaypoints::DisplayHUDMessage`, `mmHUD::ShowSplitTime` | fixed | every cleared checkpoint shows the split time for 1 s (circuits: since the lap started), not in the crash course; Waypoint / Lastwaypoint sounds |
| `updateWaypoints` | `mmWaypoints::Update`, `ClearWaypoint` | verified | per type; the rules see the waypoints as the previous frame left them (MM2 updates them after `UpdateGame`) |
| `updateOpponents` | `mmSingleRace` / `mmSingleCircuit::UpdateOpponentStatus`, `FinishMessage` | fixed | finish times from mmHUD's +0xA24 timer (the player's finish does not stop it); also runs in the frame the race ends; Messagenote in both modes |
| `updateRank` | `UpdateScore` | verified | |
| `updateHazards` | `mmGame::Update` | verified | below y = −50; 5 s in the water; per-city lines |
| `hitWater` | the modes' `HitWaterHandler`, `mmGameMulti::HitWaterHandler` | fixed | Damgelose; the checkpoint race's handler silences the engine; no hold |
| `dropThroughCity` | `mmGame::DropThruCityHandler`, `mmGameMulti` | verified | |
| `respawnAtLastCheckpoint` | `mmSingleCircuit` / `mmGameMulti::HitWaterHandler` | verified | |
| `updateClock` | `mmTimer::Update` | fixed | adds mmHUD's second timer (+0xA24) beside the race timer (+0xA54) |
| `resetTimerWarning`, `stopTimerWarning`, `timerWarning` | `PlayTimerWarning`, the modes' stop of sound 6 (+0x76E8) | fixed | beats once a second below 10 s, loop below 3 s, stopped where the modes stop it |
| `startPenalty` | the modes' wreck states (5 s, 3 s) | verified | |
| `timeRemaining`, `lapTime` | `mmTimer::GetTime` | verified | |
| `endRace` | the modes' states 4 / 5 | fixed | sets the hold per ending; a finish stops only the race timer, a loss both (`mmHUD::StopTimers`) |
| `playerFinished` | the modes' state 3 finish | fixed | asks for the results line (`mmGameSingle::UpdateRewards`) |
| `updateRace` | `mmSingleBlitz`, `mmSingleCircuit`, `mmSingleRace`, `mmMulti*::UpdateGame` | fixed | multiplayer finish shows the player's name over "finished in"; sounds; Blitz damage penalty and poor results lines; a race or Blitz wreck silences the engine and stops the music |
| `copPursuit` | `mmSingleStunt::CheckCopPursuit` | verified | 200 m, 3.5 m eye height, line of sight |
| `lessonFailed` | the `Update*` failure paths, `RegisterFinish(0)` | fixed | reported once; asks for the lesson's poor results line (not after the water) |
| `lessonPassedOrNext` | the `Update*` pass paths, `InitNewEvent`, `RegisterFinish(1)` | fixed | the race-over flag latches into the next event where the Update* set it (Jump, Evade, MinimumSpeed, Clean, Destroy, Accel; not Course, Map, Follow) |
| `updateLesson` | `mmSingleStunt::UpdateJump` … `UpdateStop` | fixed | MinimumSpeed keeps checking after "not up to speed"; Clean checks the wreck after the finish in the same frame; Jump enables no cars at "Go"; sounds per lesson (EventSoundCtrl) |
| `update`, `tickMessage` | `mmGame::Update` order, `mmHUD::Update` | fixed | the message timer counts down after the rules and clears both lines below zero |
| `updateRules` | the modes' `UpdateGame` | verified | |
| `takeEvents` | — | openmm2 | |
| `checkpointCleared`, `checkpointVisible`, `arrowTarget` | `mmWaypoints` state, `SetArrow` | verified | |
| `checkpointsCleared`, `checkpointsTotal` | `mmWPHUD`, `mmCircuitHUD::SetWPCleared` | fixed | a circuit's readout starts at 1 (`mmWaypoints::Reset`) |
| `lap` | `mmWaypoints` lap count | verified | |
| `musicHint` | `MMDMusicManager` states | inferred | idle after 3 s below 2 mph is OpenMM2's |
| `result` | `PUResults::AddName`, `mmGame::CalculateRaceScore`, `ProgressCheck` | verified | |
| `damagedOut`, `engineSilenced` | `StopSegment(1)`, `vehCarAudioContainer::SilenceEngine` | fixed | new |
| `respawnTransform`, `setPreRaceCamera`, `setStartSignal`, other accessors | — | openmm2 | |

## Cops and Robbers (`src/game/session/CopsAndRobbers.{h,cpp}`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `kGoldMassKg`, `kGoldThrottle`, `carrierExtraMassKg`, `carrierThrottleCap` | `mmMultiCR::FondleCarMass`, `mmGame::UpdateSteeringBrakes` | verified | 0 / 100 / 200 kg; 1 / 0.9 / 0.81 above first gear |
| `kWarningMinutes` | `mmMultiCR::UpdateTimeWarning` | verified | 20, 15, 10, 5, 1 |
| `loadCrLocations` | `mmMultiCR::LoadCSV` | verified | at least three places |
| `CopsAndRobbers` (constructor), `newSet` | `mmMultiCR::Init`, `GetNewSet`, `GetRandomPoints` | verified | gold, then a different bank, then a hideout different from both |
| `randomPoint` | `GetRandomPoints`' picker | deviation | coin flip between an AI intersection and a pool row other than the last; OpenMM2's own random stream (see `nextRandom`) |
| `addCar`, `teamOf`, `deliveryTarget` | `mmMultiCR::SelectTeams`, `UpdateBank`, `UpdateHideout` | verified | team 0 to the bank, team 1 to the hideout |
| `score(team)`, `playerScore`, `score(id, points)` | `mmMultiCR::UpdateHUD`, `Score` | verified | |
| `drop` | `mmMultiCR::DropGold`, `FindGround` | verified | on a road where it is, else back to the set's place |
| `checkLimits` | `mmMultiCR::UpdateLimit` (points) | verified | a player's score in Free-For-All, a team's otherwise |
| `update` | `mmMultiCR::UpdateGame`, `ImpactCallback`, `UpdateLimit`, `UpdateTimeWarning`, `UpdateGold`, `UpdateBank`, `UpdateHideout` | fixed | MM2's order (impact drop, wreck lock-outs, limits, warnings, gold, delivery); the carried gold 2 m above the carrier; no pickup in the drop frame (inferred from the network round trip) |
| `takeEvents` | — | openmm2 | |

## HUD (`src/game/session/Hud.{h,cpp}`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `hud::clockText` | `mmHUD::Update` | verified | |
| `hud::lapTimeText` | `mmCircuitHUD::SetLapTime`, `GetLocTime` | verified | |
| `hud::speedDigits` | `mmSpeedIndicator::Draw` | verified | |
| `hud::gearArt` | `mmGearIndicator::Draw` | verified | |
| `hud::linearGaugeLength` | `mmLinearGauge::Draw` | verified | clamped (MM2 would copy past the bitmap) |
| `hud::slidingGaugeOffset` | `mmSlidingGauge::Draw` | verified | |
| `hud::gaugeAngle` | `RadialGauge::GetArrowAngle` | verified | |
| `hud::arrowPose` | `mmArrow::Update` | verified | |
| `hud::standMatrix` | `mmWaypointObject`, `mmCheckpointInstance::Draw` | verified | |
| `hud::mapCamera`, `hud::mapIconMatrix` | `mmHudMap::Cull`, `DrawIcon` | verified | |
| `hud::mapRect` | `mmHudMap::SetMapMode` | verified | |
| `hud::approach` | `mmHudMap::Cull` | fixed | the rate is the constructor's 1.2: "Approach Rate" is never read |
| `hud::arrowShown`, `clockShown`, `checkReadoutShown` | `mmHUD::Init`, `mmSingleStunt::InitHUD`, `mmSingleBlitz::InitHUD` | verified | |
| `hud::nextMapMode` | `mmHudMap::GetNextMapMode` | fixed | new: off → small → split → off; from full screen, the mode it came from |
| `hud::mapIconColor` | mmHudMap's icon colour table | verified | |
| `hud::loadHudMapParams` | `mmHudMap::FileIO`, `Init` | fixed | "Approach Rate" and "Ocean Color" are not read (datParser names are one token); the ocean colour is set per city |
| `hud::loadDashParams` | `mmDashView::LoadPivotInfo`, `LoadPkg` | verified | |
| `HudOptions` defaults | `mmStatePack`, `mmPlayerConfig::DefaultViewSettings` | fixed | the map starts off for a new player |
| `Hud::Hud`, `preload` | `mmHUD::Init`, `mmDashView::Init` | verified | |
| `Hud::font` | `mmText::CreateLocFont` | verified | second size of the string's font entry |
| `Hud::drawWorld` | `mmHUD::Cull` order | verified | |
| `Hud::drawStands` | `mmCheckpointInstance::Draw` | verified | the far LOD (banner only) is missing |
| `Hud::drawIcons` | `mmIcons::Cull`, `mmSingleBlitz::Update` | fixed | Blitz cards grow from 1.9 to 4.1 over 300 m; icons forced on in Blitz and the follow/destroy lessons |
| `Hud::drawCheckpointLabels` | `mmSingleBlitz::InitHUD` / `Update` (mmIcons labels) | fixed | new: the checkpoint numbers in cyan, font string 48, 7 m above the checkpoint, sorted by depth |
| `Hud::drawArrow` | `mmArrow` | fixed | no longer removed after a lost race (only finished waypoints remove it) |
| `Hud::drawDash` | `mmDashView::Cull`, `RadialGauge::Cull` | fixed | drawn only while the HUD is enabled (`mmHUD::Disable` hides it) |
| `Hud::drawMap` | `mmHudMap::Cull`, `DrawWaypoints`, `DrawPlayer`, `DrawCops`, `DrawOpponents` | fixed | police only while pursuing |
| `Hud::drawTriangle` | `mmHudMap::DrawColoredTri` | verified | |
| `Hud::mapRect`, `toggleFullScreenMap`, `toggleMapZoom`, `toggleMapRotation`, `cycleMap` | `mmViewMgr::SetViewSetting`, `mmHudMap::ToggleMapRes`, `ToggleMapOrient` | fixed | new key handlers; full screen pauses single player (RaceScreen); zoom and orientation only with the map on |
| `Hud::toggleCluster`, `toggleOpponentIcons` | `mmHUD::ToggleExternalView`, `mmGame::SetIconsState` | fixed | the HUD toggle is the instrument cluster only |
| `Hud::drawCluster` | `mmExternalView::Cull` | verified | |
| `Hud::drawClock` | `mmHUD::Cull` digits | verified | |
| `Hud::trackLapTimes` | `mmWaypoints::Update`, `mmCircuitHUD::SetLapTime` | fixed | the lap being driven shows a live time every frame |
| `Hud::drawReadouts` | `mmWPHUD`, `mmCircuitHUD`, `mmCollideHUD` | verified | |
| `Hud::drawMessage` | `mmHUD::Update`, `mmTextNode::RenderText` | fixed | second line at 0.875 / 0.35 in its own 0.075-tall node; messages stay visible in multiplayer with the HUD disabled |
| `Hud::drawOverlay` | `mmHUD::Cull` | fixed | disabled HUD hides dash, cluster, readouts and (single player) messages |
| `Hud::setViewProjection` | — | openmm2 | label projection |
| `argbColor`, `wrapText`, `readDat` | `mmTextNode` (DT_WORDBREAK) | verified | |

## Multiplayer game layer (`src/game/net/NetGame.{h,cpp}`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `toNetMode`, `fromNetMode`, `extra`, `extraInt`, `setExtra`, `reasonText` | — | openmm2 | protocol glue |
| `toSessionSettings`, `fromSessionSettings` | `mmStatePack` / `NetArena` host settings | deviation | densities travel as whole percents; fog shares the protocol's cloudy value with the real weather in an extra field; opponents 0-7 |
| `NetGame` lifetime, `leave`, `update`, `handleEvents`, `phase`, `inSession`, `takeNotice` | `asNetwork` (DirectPlay) | openmm2 | own UDP/ENet transport |
| `startLanScan`, `addScanTarget`, `lanSessions` | DirectPlay enumeration | openmm2 | |
| `localId`, `players`, `player`, `settings`, `pingMs`, `playerName` | `asNetwork::GetPlayerData` | openmm2 | |
| `raceConfig` | `mmMultiCR::InitMyPlayer` | open | Cops vs. Robbers forces the car by team (vpcop / vpmustang99, inferred from the help pictures); MM2 derives the team from the chosen car (team 0 when the car's vehicle flags have 0x08, the police flag). Needs the lobby (frontend-ui) to pick the team from the car |
| `setLocalCar`, `setReady`, `localReady`, `everyoneReady`, `sendChat` | `NetArena` lobby | openmm2 | |
| `setRaceConfig`, `setPassword`, `kick`, `returnToLobby` | `NetArena` host | openmm2 | |
| `setMaxPlayers`, `maxPlayers` | DirectPlay session (8 players) | deviation | OpenMM2 allows 2-16 |
| `setGoldMass`, `goldMass` | `HostRaceMenu::GetGoldMassVal` (cnr_goldMass) | openmm2 | the setting's transport; the default is the host menu's (frontend-ui) |
| `startRace`, `reportLoaded`, `playerLoaded`, `raceStartTime`, `secondsToStart`, `raceStarted` | `mmGameMulti::SendRaceReady`, `mmMultiRace` / `mmMultiCircuit` / `mmMultiBlitz::UpdateGame` state 0, `GameMessage` 0x1f6 / 0x213 / 0x20f, `SystemMessage` 0x2d | fixed | the ready gate (docs/multiplayer.md, "Race start"); deviations: the start is a shared session time, which the countdown follows, instead of the host's start message, and the host stops waiting after 60 s |
| `startPortMapping`, `stopPortMapping`, `portMappingStatus` | — | openmm2 | UPnP / NAT-PMP |
| `submitLocalState`, `remoteCars` | `mmNetObject` | openmm2 | snapshots with interpolation |
| `sendEvent`, `sendCheckpoint`, `sendLap`, `sendFinish`, `sendGold`, `sendCollision`, `sendDamage`, `addSystemLine` | `mmGameMulti::SendMsg`, `mmMultiCR::Send*` | openmm2 | own messages |

## Race loop (`src/app/RaceScreen.cpp`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `RaceScreen`, `~RaceScreen`, `load`, `createSession`, `loadAi`, `loadEffects`, `setupVehicleRenderer`, `drawScene`, `drawOverlay`, `drawDebugUi`, `updateFlyCamera` | — | openmm2 | screen glue; the destructor now destroys the police drivers before their cars (a crash on leaving a race) |
| `update` (frame order) | `mmGameManager::Update`, `mmGame::Update`, `aiMap::Update` | fixed | player input, then the ambient traffic and pedestrians, then the opponents and police, then the physics step (OpenMM2 moved the traffic after the step); the rules after the step (one-step offset as MM2) |
| `spawnPoint`, `loadVehicle` | `mmPlayer::Init`, `mmGame::Init` | fixed | transmission choice and AUTO REVERSE applied; the car's geometry radius; camera aspect for the dashboard eye |
| `loadAiCar` | `vehCar::Init`, `vehCarModel::InitBound` | fixed | AI cars of the player's model get its polygonal bound |
| `waterLevelAt`, `weatherFriction`, `applyEnvironment`, `carLights` | `vehSplash`, `mmGame::InitWeather` | verified | |
| `spawnOpponents` | `aiVehicleOpponent`, `aiRouteRacer::Init` | verified | |
| `spawnPolice` | `aiMap::Init` | fixed | trunc(posts × cop density) in every mode (OpenMM2 had its own count) |
| `trackedCar`, `updateAiDrivers` | `aiMap::Update` (racers, police) | verified | held racers until "Go!" and per lesson event |
| `updateAmbient` | `aiMap::Update` (ambient), `aiVehiclePlayer`, `lvlInstance::GetRadius` | fixed | runs before the racers; the player's radius is vehicle's `VehicleBody::radius` (the BODY geometry set's), which also picks `mmGameMulti::StartXYZ`'s wide grid (over 6 m) |
| `playerState` | `mmPlayer`, `mmPlayer::IsMaxDamaged` | fixed | ICS matrix; wrecked strictly past MaxDamage |
| `updateSession` | the modes' `UpdateGame` integration | fixed | pre-race camera and start signal; post-race camera; Evade's map; lesson icons; restart starts the pre-race camera; damage limits; sounds; announcer; nothing done for an opponent's finish |
| `playGameSound` | `AudSoundBase::PlayOnce`, `PlayLoop`, `Stop` | fixed | new |
| `announce` | `mmRaceSpeech`, `mmCCSpeech` | fixed | new |
| `openPopup`, `closePopup`, `quitToMenu`, `buildPopup`, `updatePopup`, `drawPopup` | `mmPopup::ProcessEscape`, `PUMain`, `PUExit` | fixed | new: the in-race main menu (pause in single player, HUD and map disabled; Restart, Options (disabled), Quit, Exit with confirmation, Resume); a lost race opens it without pausing |
| `updateEffects` | `vehCarModel` effects | verified | (rendering-fx) |
| `loadAudio` | `mmPlayer::Init`, `InitSpeechAudio` | fixed | city ambience only with CITY SOUNDS; the announcer with COMMENTARY |
| `playerImpact` | `mmPlayer::ImpactCallback` | verified | |
| `surfaceWeather`, `carAudioInputs`, `updateAiAudio` | `vehCarAudio` inputs | fixed | the tunnel flag reaches every car's surface sound |
| `updateAudio` | `mmPlayer::Update` audio part | fixed | tunnel flag from the car's room (flag 0x02), rain interior for the hood and dash cameras, engine silenced after a race wreck |
| `sendLocalState`, `drawRemoteCars` | `mmNetObject` | openmm2 | snapshots drawn where they were received (see `updateRemoteCars` below for the bodies and trailers); drawn in the mirror too, from `drawLevel` |
| `updatePlayer` | `mmPlayer::Update`, `mmInput::GetSteering`, `FilterDiscreteSteering`, `vehCar::SetDrivable` | fixed | the player's bindings; Steer Left wins; keyboard filter; dead zone; hold per ending (+0x2258 only where set) |
| `hornDown` | `mmGame::UpdateHorn` | verified | |
| `updateGameInput` | `mmGame::UpdateGameInput`, `mmViewMgr` | fixed | new key handling (map, cluster, cameras, transmission, checkpoints, icons); looking around from a POV camera disables the HUD |
| `loadViewSettings`, `saveViewSettings` | `mmPlayerConfig` view settings | deviation | kept in the profile's `[HUD]` section |
| `cameraTarget`, `updateCarCamera` | `mmPlayer::Update` camera part | fixed | the room flags of the car's room |

## In-race controls (`src/app/Controls.{h,cpp}`, new)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Action`, `kActions`, `actions`, `info` | `mmInput::SetDefaultConfig` | fixed | new: 34 slots in MM2's event order with the keyboard defaults and string ids |
| `bindKey`, `Bindings::load`, `down`, `pressed` | `mmInput` key states, the options page's `[Controls] Bind.*` | fixed | new |
| `Options::load` | `mmInput` / `mmJoystick` options | fixed | new: AutoReverse, DeadZone (0-0.33, default 0.1), Sensitivity, UsePovHat, ForceFeedback |
| `DiscreteSteering::update` | `mmInput::FilterDiscreteSteering` | fixed | removed at the merge: the vehicle area's `phys::SteeringFilter` ports the same filter with the rates mmPlayer::Update sets by speed, and now filters the keys and the dead-zoned stick |
| `applyDeadZone` | `mmJoystick::SetDeadZone` (DIPROP_DEADZONE = option × 10000) | inferred | rescaled as DirectInput applies a dead zone (DirectInput is outside midtown2.exe) |

## Second pass (2026-10-08)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `RaceScreen::loadVehicle` (mover) | `mmGame::Update`'s `dgPhysManager::DeclareMover` | fixed | the player as type 4, 0x1b (its room and the neighbours active, PlayerInst), its trailer as type 2, 0x1b (camera-props also declared the player on integration) |
| `ai::World::setLightsDeferred`, `updateLights` (ai area) | `aiMap::Update` | fixed | the light sets run after the racers and police; no racer or police driver reads a light, so the order is all that changes |
| `RaceScreen::buildPopup`, `drawPopup` | `PUMain` (`CreateTitle(0)`), `PUExit` (`AssignName`) | fixed | no title on either popup |
| `RaceScreen::leaveRace`, `loadProfile` | `mmPlayerConfig::SetViewSettings` / `GetViewSettings` | fixed | the driver's camera, wide angle, dashboard and mirror applied at the start and stored when the race is left |
| `RaceScreen::startFinishCamera` | `mmGameMulti::SetFinishCam`, `mmPlayer::SetMPPostCam` | fixed | the orbit camera on the finish (last waypoint, a circuit's first) at (heading + 180) x -0.017453292; Blitz the post-race camera |
| `RaceScreen::updateCarCamera` (orbit keys, probe) | `camPolarCS::Update`, `dgPhysManager::Collide` (0x20) | fixed | Delete / Page Down / End / Home / Page Up / Insert / Shift; the probe is the wheels' (city and flagged instances, not the player's car) |
| `RaceScreen::updateGameInput` (mirror) | `mmViewMgr::SetViewSetting(9)` (event 0x1E), `mmViewMgr::Init` | fixed | toggles `RearViewMirror`, which `drawMirror` (rendering-fx) draws; off unless the driver's profile had it on |
| `RaceScreen::updateGameInput` (XCam) | `mmGame::UpdateGameInput` event 0x0C, `mmViewMgr::SetViewSetting(0, 2)` | fixed | Thrill Cam calls `PlayerCameras::toggleXCam` (steered by `CameraInput::orbit`); the raw C and V keys taken from `mmGame::UpdatePaused` were removed in round 3's [order](round3/order.md) audit, as nothing in build 3393 calls it (a paused game reads its bound actions through `UpdateGameInput`) |
| `CityData::levelRoomFlags` (ai-ambient-city's) | `cityLevel::Load`, `lvlLevel::LoadInstances` | fixed | lvlRoomInfo's own flags; the session's own copy was dropped at the merge and every reader below uses this one |
| `RaceScreen::cameraTarget` (room flags), `levelRoomFlagsAt`, `m_tunnel` | `mmPlayer::Update` | fixed | the level flags, not the PSDL bytes (the tunnel echo reads `LevelRoomFlag::Subterranean`) |
| `RaceScreen::rainVisible` | `cityLevel::DrawRooms` | fixed | no rain with the camera (main view or mirror, from `drawLevel`) in a subterranean or covered room, nor in a terrain-instance room under geometry (probe from 100 m up) |
| `randomIntersectionStart` | `mmGame::RespawnXYZ` | fixed | rejects `CityData::levelRoomFlags` 0x0A (mmSingleRoam's argument) and 0x24, not PSDL road / building / special-bound rooms |
| `RaceScreen::updateAudio` (tunnel) | `mmPlayer::Update` (audio flag 0x80) | verified | level flag 0x02 at the car |
| `RaceScreen::load` (glow scales), remote car setup | `vehSiren::vehSiren`, `aiVehicleManager::Init` | fixed | 0.2 / 0.95 after the AI is set up, 0.2 / 0.6 once a network car is built |
| engine smoke rule winner | `vehCarDamage::Init` order | verified | the player, then racers, then police, as `mmPlayer::Init` and `aiMap::Init` build them (network cars, which MM2 builds last, have no effects in OpenMM2) |
| ejected parts | `vehCarModel::EjectOneshot` | verified | 1.3 x the car's speed, at CurrentDamage >= MaxDamage |
| `announceResults`, `applyRaceTableDefaults` | `mmGameSingle::UpdateRewards`, the modes' `RegisterFinish` | fixed | a registered finish has the announcer name the unlocked car or paint job (LoadVehicleUnlock / PlayUnlockVehicle, LoadTextureUnlock / PlayUnlockTexture), else the results; computed on a copy of the driver (the frontend stores the finish) |
| `openChat`, `sendChatMessage`, `postIncomingChat`, `Hud::postChat` / `drawChat` | `mmPopup::ProcessChat`, `PUChat`, `mmGame` / `mmGameMulti::SendChatMessage`, `ParseChatMessage`, `mmHUD::PostChatMessage` | fixed | new: the chat line (40 characters, bottom left, no pause), "/blubber" (cheat flag, elasticity cap 4, the player's bound elasticity 4), "/rc" kept local, "/wav" not posted, five chat lines for 15 s; the popup's line height is inferred |
| `cheating`, `RaceResult::cheated` | `bCheating`, `mmStatePack::SetDefaults` | fixed | no finish registered after the cheat until the game ends |
| `Session::start` (Cops and Robbers) | `mmMultiCR::UpdateGame` states 0-2 | fixed | no countdown: "Go!" (113) 2 s at the top with "Startracehigh"; wreck line 114 |
| `controls::Options::controller`, `AnalogSteering`, `readController` | `mmInput::SetDefaultConfig`, `PollContinuous`, `GetThrottleVal` / `GetBrakesVal`, `mmJoystick::GetAxis`, `mmPlayer::FilterSteering`, `mmPlayer::Update` | fixed | the five controllers' driving inputs; the mouse, joystick and wheel steering through FilterSteering with the STEERING SENSITIVITY and the speed blend; the gamepad buttons' mapping to SDL's is inferred; the keyboard keeps OpenMM2's gamepad extra |
| `updateRemoteCars`, `drawRemoteCars` | `mmNetObject::Init`, `Update` | fixed | network cars as kinematic bodies at their snapshots (deviation: MM2 simulates them with the remote inputs), the polygonal bound, a vpcop on the Mustang's tune, trailers except in multiplayer cruise and Cops and Robbers, type-3 movers |
| `multiplayerGridOffset`, spawn | `mmGameMulti::StartXYZ`, `mmGame::RespawnXYZ` (seed), `mmGame::FindGroundPos` | fixed | the eight-slot grids (slot = player id, inferred), the cruise start seeded by the player id, spawns dropped with the wheels' probe |
| `CopsAndRobbers::updateNetwork`, `receive` | `mmMultiCR::UpdateGame`, `UpdateGold`, `UpdateBank`, `UpdateHideout`, `ImpactCallback`, `GameMessage` (0x259, 0x25a, 0x25e, 600, 0x261) | fixed | new: each machine runs its own car, the host grants pickups and draws sets |
| `setupCopsAndRobbers`, `updateCopsAndRobbers`, `fondleMass`, `crTeam` | `mmMultiCR::Init`, `InitMyPlayer`, `FondleCarMass`, `mmPlayer::UpdateRegen`, `mmGame::UpdateSteeringBrakes` | fixed | new: the mode is played (places, teams, messages, mass, throttle cap in every network game, regeneration, repair at delivery, HUD lines, limits, end after 3 s); the shared first set is seeded by the race's order time (OpenMM2) |
| `Hud::drawCrObjects`, `drawCrReadouts`, arrow interest | `mmPowerupInstance::Draw`, `mmBillInstance::Draw`, `mmArrow::SetInterest`, `mmCRHUD::Init` | fixed | new: the spinning gold, the bases' billboards, the team totals ("COPS" / "ROBBERS" or "BLUE" / "RED"); the roster of names and the gold icon are not drawn, and the totals' corner is inferred |
| Cops and Robbers speech | `mmSpeechContainer::InitCNR` | verified | loaded; nothing in build 3393 calls `mmCNRSpeech::Play` |

## Missing

| MM2 | What it does | Status |
| --- | --- | --- |
| `mmCRHUD` roster, gold icon | the player names with their scores and who carries the gold; the 3D gold icon in the corner | open: the team totals are drawn |
| `RegisterFinish` in the race | registering the finish at the line | deviation: the frontend stores it when the race is left (the announcer's choice is made at the line) |
| `mmNetObject::Predict`, `InputUpdate`, `PositionUpdate` | network cars simulated with the remote inputs and pulled toward the received positions | deviation: OpenMM2's snapshots place them kinematically |
| `PUResults` in the race | results over the running race, opponents added as they finish | deviation: the results are a frontend page |
| `PUOptions` pages in the popup | the in-race option pages | open: Options is shown disabled (frontend-ui) |
| `mmHudMap::SetMapMode` 3D view placement | the 3D view moves to the top half / small rectangle | open (rendering-fx) |
| `mmCDPlayer`, mouse steering bar | CD player display, mouse bar (`mouse_bar`, `mouse_ar`) | open |
| HUD bytes of the view settings | map mode, HUD state and map options per driver (mmPlayerConfig +0x7169, +0x716D, +0x7170) | deviation: kept in `[HUD]` of the settings for every driver |
| `mmPlayer::UpdateFF`, `FFImpactCallback` | force feedback | open: no force feedback device layer |
| `mmInput` binding sets for the other controllers | rebinding the mouse, joystick, gamepad and wheel inputs | open: their defaults are used (the options page binds keys only) |
| `Aud3DObjectManager::Process3D(false)` | drops positioned sounds behind the results popup | deviation: leaving the race stops them |
| `mmMulti*` end throttle taper | brakes with the throttle tapering for the wait, then undrivable | deviation: undrivable at once |
| `mmCCSpeech::PlayPreRace`'s first checkpoint line | the lesson's checkpoint line after the pre-race line | open (audio) |
| Stands' far LOD | `pt_*` VL mesh (banner only) | open (rendering-fx) |

## Notes for other areas and the merge

- vehicle: on integration `SimVehicle::hold(pedals)` implements
  `SetDrivable(0, 1)`; RaceScreen's held branch (`playerHeld()`) now also
  covers the undrivable endings, so it should call that. `PlayerState::wrecked`
  should use `CarDamage::maxDamaged()` there (this branch computes the same
  strict test from `maxScaled()`). `CarSim::resetAt` (SetResetPos) is for the
  spawn placement; `SteeringFilter` supersedes `controls::DiscreteSteering`.
- audio: call `MusicDirector::damagedOut()` instead of `finish()` when
  `Session::damagedOut()`; set `CarAudioInputs::sirenPursuingPlayer` from the
  cop's target; the announcer is wired through `Session` Speech events.
- camera-props: `startMultiplayerPostRace` and `CameraInput::orbit` at the
  end of a multiplayer race, the profile's view settings, the mirror (event
  0x1E) and the camera probe's room-instance semantics are not wired on this
  branch (their APIs are on integration).
- ai-vehicles: `ai::Opponent`'s header still says the race screen calls
  `finish()` on OpponentFinished; it no longer does (the game only asks
  `aiRouteRacer::Finished`).
- ai-ambient-city: `ai::World` steps the light sets with the traffic; MM2
  runs them after the racers and police.
- frontend-ui: the lobby should pick the Cops vs. Robbers team from the
  car's police flag (`mmMultiCR::InitMyPlayer`); the options page can use
  `app/Controls` for its action table; the in-race Options pages.
- Merged with the second passes of the other areas: one type-4 player
  declaration and one camera probe (camera-props'); `CityData::levelRoomFlags`
  in place of the session's copy; the XCam keys wired; the mirror drawn by
  rendering-fx's `drawMirror` under the session's toggle and profile flag.
