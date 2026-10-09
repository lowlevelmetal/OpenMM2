# Round 3: cruise spawn

Audited against MM2Recomp (midtown2.exe build 3393) on 2026-10-08.

Summary: 22 rows; verified 5, fixed 11, deviation 4, inferred 1, open 0,
openmm2 1. Missing: 2 (1 not needed, 1 open for the ai area).

The maintainer asked: "when cruising, can you confirm that you spawn in the
correct location?" Before this audit, no: OpenMM2 picked a random AI
intersection at race setup with a glibc-style generator seeded from the
clock, so every cruise started somewhere else, and it judged rooms by the
`.bai` room field. MM2 starts a single-player cruise at the same
intersection every time in a city, because `mmGame::RespawnXYZ` draws it
from the one global random stream right after `aiMap::Reset` set that
stream to seed 1 and drew from it. OpenMM2 now reproduces the whole chain;
with the cruise menu's settings the starts are:

| City | Intersection | Centre | Start (centre + 2 m), facing -Z | Where |
| --- | --- | --- | --- | --- |
| London | 19 | (-542.529, 4.934, -676.292) | (-542.529, 6.934, -676.292) | on the road along the north side of the Marble Arch island, facing across it towards the Underground station entrance; Marble Arch stands 47 m behind and to the left of the car, a red telephone box ahead to the right |
| San Francisco | 121 | (40.306, -0.082, 153.295) | (40.306, 1.918, 153.295) | on the Embarcadero at a light-controlled junction, facing the Ferry Building's clock tower about 160 m ahead, the palm-lined median on the left, the bay on the right, a parking garage 36 m to the left |

`tests/game/test_parity_cruise_spawn.cpp` pins both (retail data).

## How MM2 picks the start

Traced in the asm where the decompile is split or loses values
(`mmGame::Init`, `aiMap::Reset` and its pieces, `mmGame::RespawnXYZ`), with
every call reachable from `aiMap::Reset` followed through the asm call graph
(virtual calls resolved from the vtables and the objects' types) to find
each `irand` / `frand` on the global stream.

1. `mmGame::mmGame` sets the start position (+0x70) to (0, 10, 0);
   `mmGameSingle::mmGameSingle` the start angle (+0x76e4) to 0.
2. `mmGame::Init`: ... `mmPlayer::Init`, `mmIcons::Init`, then the mode's
   `InitGameObjects` (vtable +0x44). `mmSingleRoam::InitGameObjects` calls
   `vehCarSim::SetResetPos((0, 10, 0))`, so the reset position (vehCarSim
   +0x210) is (0, 10, 0) + CenterOfGravity, sets +0x250 to 0 and calls
   `vehCar::Reset`.
3. `InitGizmos` and the rest, then `aiMap::Init` (its draws come before the
   reset and do not matter) and `aiMap::Reset`:
   - `ResetRandomSeed` (the seed becomes 1);
   - `asNode::Reset` (aiVehicleManager), `aiPoliceForce::Reset`,
     `aiIntersection::Reset`, `aiPath::Reset`, `aiVehiclePlayer::Reset`,
     `mcHookman::Reset`, `aiRouteRacer::Reset`, `aiCTFRacer::Reset`,
     `aiPedestrian::Reset()` with `AddPedestrian`, the ambient pool
     (`AddAmbient`, `aiRailSet::Reset`): no draws;
   - for the player: `cityLevel::FindRoomId(reset position, 0)`, then
     `AdjustAmbients(0, room)` (one `ChooseNextLaneLink` per car placed,
     plus one `frand` per car on an [Exceptions] road) and
     `AdjustPedestrians(0, room)` (four `frand`s per pedestrian,
     `aiPedestrian::Reset(path, side)`);
   - `aiPoliceOfficer::Reset`, the cable cars, the subways: no draws.
4. The opponents' registration (`aiMap::Opponent`, `AngelReadString`,
   `mmHudMap` / `mmIcons::RegisterOpponents`) and two
   `dgBangerDataManager::AddBangerDataEntry`: no draws.
5. The mode's `InitOtherPlayers` (vtable +0x40):
   `mmSingleRoam::InitOtherPlayers` calls `RespawnXYZ(start, angle, true,
   true, false)`, `SetResetPos(start)`, +0x250 = angle, `vehCar::Reset`. It
   does not call `mmGame::InitOtherPlayers`, so the car is not dropped onto
   the road: it falls the 2 m.
6. `mmGame::RespawnXYZ`: with no AI map (+0x276 clear) (0, 20, 0), angle 0.
   Otherwise, when in a network session (asNetwork +0x3c) and asked to
   (fifth argument), `DisableGlobalSeed` and the seed set to the local
   player's DirectPlay id (asNetwork +0x20). Then repeatedly: draw
   (counter + 1) numbers `irand() % (intersections - 1) + 1` and keep the
   last (the counter is a .bss global, 0 at start-up); a missing
   intersection draws again; the intersection fails when its room
   (`FindRoomId(centre, 0)`, lvlRoomInfo flags) has 0x24, or 0x0A with the
   fourth argument, or one of its roads (aiPath +0xc) has 0x4 with the third
   argument or 0x2 with the fourth; a failure draws again with no limit. The
   start is the centre (aiIntersection +0x14) 2 m up, the angle 0. In the
   seeded case `EnableGlobalSeed` and the counter becomes (counter + 1) %
   100.

The draws in step 3 depend on the city and the densities: London's room at
the origin is 65 (28 ambient roads, 13 pedestrian roads), San Francisco's
265 (3 ambient roads, none for pedestrians); room 0's lists are empty in
both. With the cruise menu's densities (`RaceMenuBase::SetStateRace`:
traffic 0.5, pedestrians 0.25, so 25 pedestrians) London draws 73 + 100
numbers and San Francisco 21 before the start; any car's CG leaves the
reset position in the same room. The start therefore depends on the
traffic and pedestrian sliders (and the SHOW PEDESTRIANS option, which
creates no pedestrians), as in MM2:

| Traffic | Pedestrians | London draws, start intersection | San Francisco draws, start intersection |
| --- | --- | --- | --- |
| 0 | 0 | 0, 42 | 0, 42 |
| 0 | 0.25 | 100, 256 | 0, 42 |
| 0 | 1 | 400, 129 | 0, 42 |
| 0.5 | 0 | 73, 190 | 21, 121 |
| 0.5 | 0.25 (menu) | 173, 19 | 21, 121 |
| 0.5 | 1 | 473, 42 | 21, 121 |
| 1 | 0 | 145, 81 | 42, 12 |
| 1 | 0.25 | 245, 306 | 42, 12 |
| 1 | 1 | 545, 47 | 42, 12 |

(`--quickstart` uses RaceConfig's own defaults, pedestrians 0.5, and so
starts London elsewhere; the menus apply SetStateRace's.)

Multiplayer cruise and Cops and Robbers: `mmGameMulti::InitOtherPlayers`
calls the mode's `Reset` (`mmMultiRoam::Reset` when its state is 0,
`mmMultiCR::Reset` unless its state is 4, 6 or 7, or 5 with +0xb254
clear), which
calls `RespawnXYZ(true, true, true)`, and then `InitNetworkPlayers`, which
calls it again for the local player and keeps the result (its fall-back to
`StartXYZ` when the result is the origin cannot happen). Each call reseeds
from the player id and advances the counter, so the start is the second
pick, drawn (counter + 2) numbers at a time, and the counter, never reset,
also changes every later single-player start of the run.

## Rows

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `game::session::respawnXYZ` | `mmGame::RespawnXYZ` | fixed | was `randomIntersectionStart`: a glibc-constant LCG, one draw a pick, the `.bai` room field's flags, a 1000-pick cap and a scan fallback. Now (counter + 1) MSVC `irand` draws a pick, the room by `FindRoomId` of the centre (San Francisco's intersection 187 lists room 1078, an open road, but its centre lies in room 265, a terrain instance's: MM2 never starts there), the same flag rules, centre + 2 m, angle 0 |
| `respawnXYZ`: no fitting intersection | `RespawnXYZ`'s endless retry | deviation | MM2 hangs; OpenMM2 first checks that one fits (no other cap, so every pick MM2 returns is the same) and returns nothing |
| `respawnXYZ`: fewer than two intersections, no AI map | `irand() % 0`; (0, 20, 0) | deviation | nothing is returned and `loadRaceSetup` keeps OpenMM2's first-Blitz-start fallback; every retail city has an AI map |
| `respawnCounter` | mmGame's respawn counter (.bss global) | fixed | new: run-long, advanced (mod 100) by every seeded pick, (counter + 1) draws a pick for everyone |
| `cruiseStart` (single player) | `mmSingleRoam::InitOtherPlayers` | fixed | `RespawnXYZ(true, true, false)` on the global stream as `aiMap::Reset` left it |
| `cruiseStart` (multiplayer) | `mmGameMulti::InitOtherPlayers`, `mmMultiRoam::Reset`, `mmMultiCR::Reset`, `InitNetworkPlayers` | fixed | two seeded picks, the second kept, the counter advanced twice (was one pick with the counter ignored) |
| `SessionOptions::seed` | the DirectPlay player id (asNetwork +0x20) | deviation | OpenMM2's transport has no DirectPlay ids: 1 + its network id |
| `mmMultiRoam::Reset`'s state test at the start | +0x270 == 0 | inferred | the game object is fresh heap memory that no constructor clears before `mmGame::Init`; OpenMM2 takes it as 0 (both picks happen) |
| `loadRaceSetup`: the cruise place | `mmSingleRoam::InitGameObjects` ((0, 10, 0) from `mmGame::mmGame`, angle 0), `mmMultiRoam` / `mmMultiCR::InitGameObjects` (the origin) | fixed | `RaceSetup::respawnStart`; the car stands there while the AI resets |
| `Session::placeRespawnStart` | the InitOtherPlayers placement | fixed | new: the pick becomes the player's place, spawn and water respawn |
| `RaceScreen::placeRespawnStart` | `mmGame::Init`'s order | fixed | after `ai::World::reset` (and the racers and police), as InitOtherPlayers follows `aiMap::Reset`; `SimVehicle::setResetPos` + `reset`; the camera follows; `OPENMM2_DEBUG_SPAWN` still wins |
| `ai::World::globalSeedAfterReset` | `aiMap::Reset`'s draws (`ResetRandomSeed`, `AdjustAmbients`, `AdjustPedestrians`) | fixed | new: replays the traffic and pedestrian population for the room of the car's reset position on one MSVC stream from seed 1, then resets the world |
| `Traffic::replayResetPopulation`, `Pedestrians::replayResetPopulation`, `Random::state` | `AdjustAmbients` / `AdjustPedestrians` from room 0 when the map has ambient cars / pedestrians | fixed | new; the populations themselves were audited by the ai areas (draw for draw) |
| `city::addShortcuts`: centre | `aiIntersection::CreateRoadMap` | fixed | a shortcut road's end intersections move to their room's bound-sphere centre (`sdlRoomBoundSphere`; up to 0.21 m in San Francisco) before their road lists are sorted; RespawnXYZ starts there (ai-vehicles' open row, commit ef670f6) |
| C&R picker (`RaceScreen::setupCopsAndRobbers` `randomIntersection`) | `GetRandomPoints`' helper: `RespawnXYZ(false, false, false)` less 2 m | fixed | the room by `FindRoomId`, only 0x24 rejected, no road rules, MSVC draws |
| C&R picker: stream and draws | the host's global stream, (counter + 1) draws | deviation | every OpenMM2 machine draws the first set itself from the shared start time, one draw a pick (the counter is each machine's own, so using it could split the machines) |
| Cruise restart | `mmSingleRoam::Reset` -> `mmGameSingle::Reset` -> `mmGame::Reset` | verified | no new pick: `asNode::Reset` puts the car back at its reset place (`Session::restart`, RaceScreen's Restart); network games have no restart |
| `CarSim::setResetPos` | `vehCarSim::SetResetPos` | verified | the CG added unrotated (+0x210) |
| reset angle | +0x250 written from RespawnXYZ's angle | verified | 0: the body faces -Z (`SimVehicle::setResetPos(pos, 0)`) |
| `CarSim::reset` | `vehCar::Reset`, `vehCarSim::Reset` | verified | phInertialCS at the reset position, `Matrix34::Rotate` about Y by +0x250, `SetWorldMatrix` (model at body + R * CG); test `TheCarStandsOnTheStart` |
| `StartDrop::None` | `mmSingleRoam::InitOtherPlayers` without `mmGame::InitOtherPlayers` | verified | no probe, no 0.9 m settle; the car drops the 2 m |
| `log::info` "race: start at intersection" | none | openmm2 | names the pick in the log |

## Missing

| MM2 | What it does | Status |
| --- | --- | --- |
| `mmGameMulti::SystemMessageCB` (a player joining a running game) | places the newcomer's car with `RespawnXYZ(false, false, false)` on the global stream until its updates arrive | not needed: OpenMM2 has no joining in progress; remote cars appear at their snapshots |
| One random stream for every subsystem | `aiMap::Reset` fills the origin room's roads, `RespawnXYZ` draws, and the first `aiMap::Update` moves the traffic and pedestrians to the start's room from the stream as it then stands | open (ai area): OpenMM2 replays the reset draws only to pick the start; its traffic and pedestrians still populate the start's room from their own streams at the first step (the documented per-subsystem deviation, `ai/Random.h`), so the first cars and pedestrians round the start differ from MM2's |

## Screenshots checked

Taken with `OPENMM2_FRONTEND_SCRIPT="profile:Frames;mode:cruise;city:<map>;vehicle:vpbug;go"`
and `--frames 600 --screenshot` (not committed):

- before (the previous build): London started on a street with a colonnade
  and an Underground sign, San Francisco at a crossing by an office block
  (a different random intersection every run);
- after: London at intersection 19, the Underground station entrance across
  the road ahead, the Marble Arch island behind (a fly-over from
  `OPENMM2_DEBUG_FLY` shows the arch at the island's corner and the car on
  the road north of it); San Francisco at intersection 121 on the
  Embarcadero, the Ferry Building's tower straight ahead, the bay to the
  right. The same start every run.
