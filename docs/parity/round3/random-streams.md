# Round 3: random streams

Audited against MM2Recomp (midtown2.exe build 3393) on 2026-10-08.

Theme: where MM2's randomness decides something the player sees, OpenMM2
should get the same outcome. MM2 draws almost everything from one global
generator and sets it back to 1 at fixed points, so much of what a race
shows at its start is the same every time in MM2; OpenMM2 had given each
subsystem its own stream, so none of it matched.

Summary: 38 consumers checked; verified 5, fixed 14, deviation 12, open 1,
not needed 6. Commits: bf32be5 (the AI's one stream), abed91c (the
set-up order). Tests: `tests/ai/test_parity_random_streams.cpp`,
`tests/game/test_parity_random_streams.cpp`.

## MM2's generators

- `irand()` / `frand()`: the MSVC rand() LCG on the global `gRandSeed`
  (seed × 214013 + 2531011, bits 16..30; frand = irand × 2^-15). The seed
  starts at 0 when the program starts. `ResetRandomSeed` sets it to 1:
  before every road of `cityLevel::Load`'s street-prop loop, at the start of
  `aiMap::Reset`, and in `mmReplayManager` (its constructor and Reset;
  StartReplay and SetReplayInfo save and restore it).
- `DisableGlobalSeed` / `EnableGlobalSeed` swap in `secondarySeed` (also 0 at
  start, never reset) around draws that must not disturb the shared
  sequence: `asBirthRule::InitSpark`, `asLineSparks::RadialBlast`,
  `fxShardManager::EmitShard` (with `fxShard::AddShard`),
  `fxTexelDamage::ApplyDamage`, `vehCarDamage::Update`, the network form of
  `mmGame::RespawnXYZ` and the Cops and Robbers point picker (which seeds
  it from the clock).
- `irand(int)` / `frand(int)`: one LCG step of the argument, no state
  (`aiVehicleInstance::aiVehicleInstance`, on its own address).
- `Random` (Knuth's subtractive generator): the audio, seeded from the
  clock each time (audio record).

OpenMM2: one class for the generator, `ai::Random` (`game::fx::Rand` is the
same class). `RaceScreen` keeps MM2's global stream (`m_random`) through a
race's set-up and hands it to `ai::World`, which draws the traffic and the
pedestrians from it and sets it to 1 at every `reset()`.

## The order of draws

### A race start (single player)

`mmGameManager::mmGameManager` runs the game's `Init` and then
`mmGameManager::Reset`. In `mmGame::Init` (asm order; the decompile is cut
into pieces) the draws on the global stream are:

1. `cityLevel::Load` (the level's vtable slot 2): for every road of
   `lvlAiMap`, `ResetRandomSeed`, then, when the road's first room has a prop
   rule, `cityPropulator::Propulate` with rules `nNNleft` / `nNNright`:
   `lvlSDL::Propulate` walks both sidewalks once per road strip, two frand
   per point (the fraction across the sidewalk, the step along), and the
   placement callback draws irand for the variant when a prop stands. The
   stream is left as the last road's walk leaves it: 1 in London (its last
   road has no props), 2380015889 in San Francisco. The rest of the level
   (instances, path sets, race props) draws nothing.
2. `mmPlayer::Init` -> `vehCar::Init`, 312 draws: `vehCarModel::Init` ->
   `vehSiren::Init` builds the siren's lens flare for every car (the vehSiren
   is created by every `vehCar::Init`, its flare pointer null), twenty
   `ltFlare::Random` of six frand each; then `vehSplash::Init` fills its 4 x 4
   x 4 points with random directions (three frand each), which it overwrites
   at once. A trailer (vehTrailer) draws nothing.
3. `InitGameObjects` (vtable slot 0x44): nothing.
4. `mmGame::InitGizmos`, managers in this order: sailboats
   (`gizSailboatMgr::Init`: per boat `gizInstance::Init`'s irand for the
   paint job, then frand for the speed), bridges and trains (nothing),
   ferries in single player (`gizFerryMgr::ApplyTuning` after all of them:
   one frand each), parked cars in single player and network races
   (`gizParkedCarMgr_EnumeratePath` per point: irand % 3 for the model or
   none, then irand for the paint job when a car stands).
5. `aiMap::Init`: `AddPlayer` (no ambients or pedestrians yet, no draws);
   each racer's `aiRouteRacer::Init` -> `aiVehiclePhysics::Init` ->
   `vehCar::Init` (312 each); hookmen and CTF racers (none in retail
   single-player data); the ambient array of the state's vehicle count
   (`aiVehicleAmbient` -> `aiVehicleSpline` constructor: its `aiRailSet`
   frand, then its own frand), built before the density is looked at, so it
   draws at density 0 too; per car (density above 0) the type (frand against
   the race's, else the city's cumulative list), then `aiVehicleAmbient::Init`:
   the `aiVehicleInstance` constructor's `SetColor`, `SetColor` again (the
   paint kept) and the `aiGoalRandomDrive` constructor (four frand); per
   pedestrian the type (frand x the name count) and `aiPedestrian::Init`'s
   variant (frand); the cable cars (`aiCableCar` array constructors: one
   `aiRailSet` frand each, then `aiCableCar::Init`: one frand each); the
   subways (none: both cities' `[Subway]` is commented out); the police
   force and each officer's `aiPoliceOfficer::Init` -> `vehCar::Init` (312
   each); `aiVehicleManager::Init` (nothing).
6. `aiMap::Reset` (R1): `ResetRandomSeed`; the resets of every list (no
   draws); per player `AdjustAmbients` then `AdjustPedestrians` from room 0
   to the room of the player's reset position at that moment.
7. `InitOtherPlayers` (slot 0x40): in cruise `mmSingleRoam::InitOtherPlayers`
   calls `mmGame::RespawnXYZ`, whose irand picks the start intersection from
   the stream R1 left; the other single-player modes draw nothing here.
8. `mmGameManager::Reset` -> `mmGameSingle::Reset` -> `mmGame::Reset`:
   `lvlLevel::ResetInstances` (nothing), `aiMap::Reset` (R2): seed 1, then
   `AdjustAmbients` and `AdjustPedestrians` round the player's reset position
   (the start); the game's children reset (nothing draws).

Everything the race shows when it starts is decided by steps 1-5 (models,
paints, clothes, parked cars, boats, cable cars, siren flares) and step 8
(where the traffic and pedestrians stand), and none of it depends on the
player.

### A restart

`mmGame::Reset` repeats step 8 only: seed 1, then the same population round
the start. The set-up's outcomes (steps 1-5) stay.

### Where reproducibility ends

From the first frame after R2, `aiMap::Update` draws for the traffic
(`ChooseNext*Link`, `aiGoalAvoidPlayer`), the pedestrians (`Update`,
`WaitCrossStreet`, `Anticipate`, `PickNextRdSeg`) and the police
(`DetectPerpetrator`), interleaved with the player's wheels
(`vehWheel::GetBumpDisplacement` via `vehDrivetrain::Update`), the glows'
flicker as they are drawn (`dgBangerInstance::DrawGlow`, also for the
traffic lights), ejected parts and force feedback. The number of frames and
what is in view depend on the machine and the player, so nothing after R2
is reproducible in practice (MM2's own replays save and restore the seed for
this reason).

## Consumers

### The set-up (steps 1-5)

| MM2 | Draws | OpenMM2 | Verdict | Notes |
| --- | --- | --- | --- | --- |
| `cityLevel::Load` propulator loop, `cityPropulator::Propulate`, `lvlSDL::Propulate`, its callback | seed 1 per road; 2 frand per point, irand per prop | `bangers::placeStreetProps` | verified | Per-road seed 1 and the draw order were already MM2's (camera-props record). |
| (the same) | the stream after the loop | `placeStreetProps` / `placeCityProps` `randomState` | fixed | New: the state the last road leaves (1 when it has no props). Roads without sidewalks are now walked too: MM2 walks them and draws, nothing stands (the X row is zero); still 10,184 street props. |
| `vehCar::Init` -> `vehSiren::Init` -> `ltLensFlare(20)` -> `ltFlare::Random` | 120 frand | `takeVehCarInitDraws`, `VehicleRenderer::setSirenFlares` | fixed | Every car draws them; OpenMM2 drew siren flares from a stream of its own (0x5A1E) and only for cars with sirens. Now the player's, each racer's and each police car's flares come from their place in the stream. |
| `vehCar::Init` -> `vehSplash::Init` | 192 frand | `takeVehCarInitDraws` | fixed | `Splash::init` still skips the overwritten directions (vehicle record); the draws are taken where the car is set up. |
| `gizInstance::Init`, `gizSailboatMgr::Init` | irand, frand per boat | `Gizmos::loadSailboats` | fixed | Draw order was MM2's; the stream started at 1 instead of after the street props and the player's car. |
| `gizFerryMgr::ApplyTuning` | frand per ferry | `Gizmos::loadFerries` | fixed | As above. |
| `gizParkedCarMgr_EnumeratePath` | irand per point, irand per car | `placeParkedCars` | fixed | As above. London now has 488 parked cars (492 from seed 1), San Francisco 102 (90). |
| `aiRouteRacer::Init` -> `vehCar::Init` | 312 per racer | `RaceScreen::loadAi` (the racers' share), `spawnOpponents` (their flares) | fixed | Not taken before, so the traffic started too early in the stream in races. |
| `mcHookman::Init`, `aiCTFRacer::Init` (irand) | — | — | not needed | No hookmen in retail races; the CTF racer count is the state's, always 0. |
| `aiRailSet::aiRailSet`, `aiVehicleSpline::aiVehicleSpline` (the ambient array) | 2 frand per car | `Traffic::init` | fixed | Order was MM2's; now from the shared stream, and at traffic density 0 too (MM2 builds the array before it reads the density; multiplayer cruise sets the density to 0). |
| `aiMap::Init` type pick, `aiVehicleInstance::SetColor` x2, `aiGoalRandomDrive::aiGoalRandomDrive` | 4 frand per car | `Traffic::init`, `pickType` | fixed | Order was MM2's (ai record); now from the shared stream after the gizmos and racers. MM2 skips a car's Init (three draws) when no type's cumulative value exceeds the draw; every retail list ends at 1.0, so it never happens (OpenMM2 keeps the last type). |
| `aiMap::Init` pedestrian type, `aiPedestrian::Init` | 2 frand per pedestrian | `Pedestrians::init` | fixed | Was a stream of its own (seed x 7919 + 1); now after the traffic's. |
| `aiCableCar::aiCableCar` (`aiRailSet`), `aiCableCar::Init` | 1 + 1 frand per car | `CableCars::create` | fixed | The constructors' draws were missing; now taken first, after the pedestrians'. |
| `aiSubway::Init` | frand | — | not needed | No subways in retail data (`[Subway]` commented out in both cities' .aimap); OpenMM2 has no aiSubway. |
| `aiPoliceOfficer::Init` -> `vehCar::Init` | 312 per officer | `RaceScreen::loadEffects` (state), `spawnPolice` (flares) | fixed | Their draws decide nothing but their own siren flares (R1 follows). |
| `aiGoalRandomDrive::aiGoalRandomDrive`'s exceed counter | (not random) | `Traffic::init` | verified | A static that MM2 never resets, but it cycles with period 5 and the pool is 300 cars, so each race starts it at 0 as OpenMM2 does. |
| `aiVehicleInstance::aiVehicleInstance`'s `irand(int)` | stateless | `Traffic::init` (blink phase) | deviation | Already recorded: of the instance's address in MM2, of a stand-in address in OpenMM2. |

### The resets (steps 6-8)

| MM2 | Draws | OpenMM2 | Verdict | Notes |
| --- | --- | --- | --- | --- |
| `aiMap::Reset` `ResetRandomSeed` | seed 1 | `World::reset` | fixed | One seeding of the shared stream (Traffic and Pedestrians seeded their own streams separately, the pedestrians' to 7920). |
| `aiMap::AdjustAmbients` (+ `ChooseNext*Link` per car) at R1/R2 | from 1 | `Traffic::populate` | verified | Same draws from 1 (ai-vehicles record). OpenMM2 populates on the first step after a reset with the player's room then, which is the room of the reset position MM2 uses; now explicitly before any update. |
| `aiMap::AdjustPedestrians` -> `aiPedestrian::Reset(path, side)` | 4 frand per pedestrian placed | `Pedestrians::populate` | fixed | Now continues the stream the traffic's population left, before the traffic or pedestrians update. |
| `mmGame::RespawnXYZ` (single player) | irand per attempt | `RaceSetup` `randomIntersectionStart` | open | Owned by the cruise-start work (not changed here). What it needs: the stream after R1's population round the player's reset position before the start is known, which `ai::World::reset` + `Traffic::populate` / `Pedestrians::populate` on `RaceScreen::m_random` now provide. |
| `mmReplayManager` (`ResetRandomSeed`, save/restore) | — | — | not needed | OpenMM2 has no replays. |
| Network players' `mmNetObject::Init` -> `vehCar::Init` | 312 each | — | not needed | `mmGameMulti::InitOtherPlayers` runs after R1 and R2 reseeds: no effect on anything shown; their flares come from OpenMM2's own flare stream. |

### During play (not reproducible)

| MM2 | OpenMM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `aiMap::ChooseNextRandomLink`, `ChooseNextLeftStraightLink`, `ChooseNextRightStraightLink`, `ChooseNextRightStraightFreewayLink`, `AdjustAmbients` on a room change, `aiGoalAvoidPlayer::AvoidPlayer`, `aiGoalRandomDrive::Reset`, `aiGoalRegainRail::Reset` | `ai/AmbientRoute`, `Traffic` on the AI world's stream | verified | Shared with the pedestrians as in MM2; MM2 interleaves the draws below too, so play-time sequences cannot match. |
| `aiPedestrian::Update`, `WaitCrossStreet`, `Anticipate`, `PickNextRdSeg`, `Wander`, `SolveRoadSegment` | `Pedestrians` on the AI world's stream | verified | As above. |
| `aiPoliceOfficer::DetectPerpetrator`, `ApprehendPerpetrator` | `PoliceSquad` (a stream per officer) | deviation | Play-time; kept apart so a chase does not depend on what the traffic drew. |
| `dgBangerInstance::DrawGlow` (props, traffic lights) | `BangerSet` glow stream | deviation | Drawn per frame for what is in view. |
| `vehWheel::GetBumpDisplacement` (via `vehDrivetrain::Update`) | the wheels' own stream (`phys/vehicle/Wheel`) | deviation | Physics; per sample. |
| `vehCarModel::EjectOneshot`, `vehBreakableMgr::Eject` (also via `aiVehicleActive::Impact`) | RaceScreen and BangerSet eject streams | deviation | On impacts. |
| `mmPlayer::UpdateFF`, `FFImpactCallback` | `ForceFeedback` stream | deviation | Already recorded as inferred own. |
| `mmMultiCR::GetRandomIndex` (global), the Cops and Robbers point picker (secondary, clock-seeded) | `CopsAndRobbers` (its own LCG of the same form, seeded from the race start time) | deviation | Network; MM2's picker is seeded from the clock, so nothing to reproduce. The LCG constants differ from MSVC's (session record). |
| `ptxGlass::AddShards`, `ptxGlass::Update` | — | not needed | No retail banger is glass (BillFlags 0x100). |
| `pedActive::FirstImpactCallback` | — | not needed | No ragdolls exist in MM2 (pedRagdollMgr is never built). |

### The secondary seed

| MM2 | OpenMM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `asBirthRule::InitSpark` | `fx::Particles` (stream per emitter, 0x2A) | deviation | MM2's secondary seed is one stream for all of these, never reset in a session, so its draws depend on every effect since the program started. |
| `asLineSparks::RadialBlast` | `fx::LineSparks` | deviation | As above. |
| `fxShardManager::EmitShard`, `fxShard::AddShard` | `fx::Shards` | deviation | As above. |
| `fxTexelDamage::ApplyDamage` | `TexelDamage` | deviation | As above. |
| `vehCarDamage::Update` | (the damage smoke's particles) | deviation | As above. |

## What changed on screen

London, the same fixed spawn and camera (`OPENMM2_DEBUG_SPAWN`,
`OPENMM2_DEBUG_CAMERA`): the traffic stands in the same places (its
population from seed 1 was already MM2's) but with other models and paints
(the pool's draws now start where MM2's do), the pedestrians stand elsewhere
in other clothes, and the parked cars along the streets are other models in
other paints, some places empty that were taken and the other way round.
The values are pinned by `ParityRandomStreamsRetail.LondonCruiseSetUp`
(the first cars placed round the busiest intersection: 198 va_eurocargo_l,
199 va_eurocargo_l, 200 va_2sitersport_l, with their paints; the first
pedestrians' types and clothing variants; a restart placing them all
again identically).

## Open

- `mmGame::RespawnXYZ`'s cruise start (above): for the cruise-start work.
  MM2 draws it from the stream after R1, whose population is round the
  player's reset position before the start is known (the position
  `mmSingleRoam::InitGameObjects` gives the car from mmGame +0x70).
