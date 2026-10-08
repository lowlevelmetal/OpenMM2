# MM2 -> OpenMM2: props-fx

Audited from MM2Recomp (midtown2.exe build 3393) on 2026-10-08.

Summary: 169 reachable functions in 24 classes; ported 126 (of which newly
ported 1, and 6 more fixed in this pass), replaced 0, not needed 43, open 0.
The list's 14 unreachable functions have rows too (not needed).

Scope: breakable props (the dgBanger* family, path sets, street rules, road
decals), particles (asParticles, asBirthRule, asLineSparks), car shards
(fxShard*), texel damage (fxTexelDamage), wheel particles (vehWheelPtx) and
the car parts that come off (vehBreakable*). The first audit verified the
OpenMM2 side of these (docs/parity/camera-props.md for src/game/bangers,
docs/parity/rendering-fx.md for src/game/fx); this pass started from MM2's
functions and their callers. "Not needed" covers constructors, destructors
and list plumbing that C++ value types replace, editor code, and code the
game cannot reach with retail data (the reason is given).

Fixed or newly ported in this pass:

- **Race restart resets the props** (`mmGame::Reset` ->
  `lvlLevel::ResetInstances`, which resets every instance and then
  `dgBangerManager::Reset` and `dgBangerActiveManager::Reset`). OpenMM2's
  Restart put the cars back but left every knocked-over prop lying where it
  fell; RaceScreen now calls `BangerSet::reset` first.
- **Repaired cars lose their ejected parts** (`vehBreakableMgr::Reset`,
  from `vehCarModel::ClearDamage` / `Reset`: each breakable back on, and
  `Detach` of the hit banger it became, so the part lying in the street
  disappears). Ported as `BangerSet::detachHit`
  (`dgHitBangerInstance::Detach`), `BangerSet::ejectPart` returning the hit
  instance (vehBreakable +0x44) and `VehicleRenderer::detach` /
  `reattachAll` remembering and detaching it.
- **Lamp glows while a prop is held**: `dgBangerInstance::DrawGlow` tests
  lvlInstance flag 1, which stays set while an active holds a standing prop
  (OpenMM2 tested "not active").
- **Debris sheet**: `dgBangerActive::Attach` clamps TexNumber - 1 to 0..20
  (a negative TexNumber takes fxpt1; 0 has none); a prop without a sheet
  only resets the particles and keeps the rule and sheet the active last
  had, which goes on spewing at its own Position (around the world origin).
  OpenMM2 went on spewing but stopped drawing them.
- **Banger birth rules** are read with `asBirthRule::Load`, which takes only
  the 24 fields `dgBangerData::Save` writes: LifeVar, Damp, DampVar,
  Height, Intensity and Color keep their defaults (no retail file has them).
- **Room lists newest first** (`lvlLevel::MoveToRoom` puts an instance at
  the head of its room's list; OpenMM2 appended).

Tests: `tests/game/test_parity_props_fx.cpp`, with the first audit's
`test_bangers.cpp`, `test_parity_camera_props_bangers.cpp`, `test_fx.cpp`
and `test_parity_rendering_fx.cpp`.

## Where props come from (spawns)

`cityLevel::Load` places, in this order: the street rules
(`cityPropulator` over every road of `lvlAiMap` whose first room has a prop
rule, `ResetRandomSeed` per road), the banger records of `<map>.inst` and
`<map>_ai.inst` with the bangers each record's PKG "xrefs" place
(`lvlLevel::LoadInstances`), `city/<map>/props.pathset`, the road decals of
`city/<map>/decals.pathset`, and the race's own path set
`race/<map>/<dgGameModeNames[mode] with the race index>.pathset`
(`cityLevel::LoadPathSet` -> `dgPath::Enumerate` -> `cityLevel::LoadPath`
-> `cityLevel::LoadProp` -> `dgUnhitBangerInstance::RequestBanger`, room
from `FindRoomId` of the placement point). Each placed prop is a
`dgUnhitYBangerInstance` (rotation about Y only) or
`dgUnhitMtxBangerInstance` (full matrix: .inst records not in the compact
form, line-strip paths, xrefs). Car parts become bangers through
`vehBreakableMgr::Eject`. OpenMM2: `bangers::placeCityProps` (same order)
and `BangerSet::add`; verified in the first audit, see camera-props.md.
The gizmos (`gizBridgeMgr`, `gizFerryMgr`, `gizParkedCarMgr`, ...) also
read path sets and banger data (CollisionType 0x30 on the bridges and
ferries: lvlInstance flag 0x20); they belong to world-objects.

## asBirthRule

How particles are born: `tune/effects/*.asBirthRule` (the wheel rules, rain),
the `BirthRule` block of every dgBangerData, and vehCarDamage's engine smoke
rule. OpenMM2: `fx::BirthRule`, `loadBirthRule`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `asBirthRule::asBirthRule` | ported | `fx::BirthRule` defaults | Verified (first audit): Life, Mass, Radius, Damp, Intensity 1, Gravity −9.8, colour −1. |
| `asBirthRule::FileIO` | ported | `fx::loadBirthRule` | By field name, as datParser does for `.asBirthRule` files. |
| `asBirthRule::Load` | ported (fixed) | `bangers::parseBangerData` | Positional reader of dgBangerData's rule: only Position, PositionVar, Velocity, VelocityVar, Life, Mass, MassVar, Radius, RadiusVar, Drag, DragVar, DRadius, DRadiusVar, DAlpha, DAlphaVar, DRotation, DRotationVar, InitialBlast, SpewRate, SpewTimeLimit, Gravity, TexFrameStart, TexFrameEnd, BirthFlags. The six other fields now keep their defaults (OpenMM2 read them by name; no retail file has them). |
| `asBirthRule::InitSpark` | ported | `fx::ParticleSystem::initSpark` | Verified (first audit), random draw order. |
| `asBirthRule::Copy` | ported | `fx::VehicleEffects::VehicleEffects` | Copies every FileIO field (0x18..0xAC). Its one caller, `mmGame::InitWeather` in rain, copies the splash wheel rule over the smoke one. |
| `asBirthRule::GetDirName`, `GetClassName` | ported | `fx::EffectLibrary::load` | "tune/effects" and the "asBirthRule" type of the files. |
| `asBirthRule::Save`, `Indent` | not needed | — | Editor: only `dgBangerData::Save` calls them. |
| `asBirthRule::~asBirthRule`, `` `scalar_deleting_destructor' `` | not needed | — | Value type. |

## asParticles

A pool of particles drawn as camera-facing cards. MM2 has one per car for
wheel dust (`vehWheelPtx`, 128), one per car for damage and exhaust smoke
(`vehCarDamage`, 64), one per banger active for debris (64), and the rain
(200). `aiVehicleActive` constructs two that nothing ever uses
(`aiVehicleActive::UpdateDamage` is empty). OpenMM2: `fx::ParticleSystem`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `asParticles::asParticles` | ported | `fx::ParticleSystem` defaults | Intensity 1, no wind, Reset. |
| `asParticles::Init` | ported | `ParticleSystem::init` | Count (quartered only in software rendering), frame grid. |
| `asParticles::Reset` | ported | `ParticleSystem::reset` | Count, birth matrix and elapsed time to 0; the rule and the spew fraction stay. |
| `asParticles::Blast` | ported | `ParticleSystem::blast` | Re-checked: capped by the pool, positions through the birth matrix. |
| `asParticles::Update` | ported | `ParticleSystem::update` | Re-checked against the decompile: spew while elapsed < SpewTimeLimit (or 0), death by life, y < −50 or alpha 0 with the last particle swapped in and processed in the same pass, quadratic drag with gravity, alpha/rotation by ftol(Δ × 60 dt), radius, stop-at-height (8), bounce (2), frame cycling (4). |
| `asParticles::SetTexture` (gfxTexture) | ported | the texture passed to `ParticleRenderer::draw` | The sheet each owner draws with. |
| `asParticles::SetTexture` (name) | ported | `fx::Weather` (rain sheet) | Only the rain uses it; it also empties the pool (count 0), which is empty then anyway. |
| `asParticles::Cull` | ported | `ParticleRenderer::draw` | asMeshCardInfo::DrawShadows then Draw (rendering-fx). |
| `asParticles::~asParticles`, `` `scalar_deleting_destructor' `` | not needed | — | Value type. |

## asLineSparks

Up to 64 sparks per car, drawn as lines, from `vehCarDamage::ApplyImpact`
above 15 mph. OpenMM2: `fx::LineSparks`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `asLineSparks::Init` | ported | `LineSparks::LineSparks`, `SparkLut::load` | `Init(64, "spark")` from `vehCarDamage::Init`: gravity −20, 6..7 m/s across and 4..5 along the normal, trail −0.036, step 1/30 s, age −650/s, birth box 0.1, ground 0, colours from asSparkLut "spark". |
| `asLineSparks::RadialBlast` | ported | `LineSparks::radialBlast` | Fixed in the first audit (axes). |
| `asLineSparks::Update()` | ported | `LineSparks::update` | Accumulates to 1/30 s. |
| `asLineSparks::Update(float)` | ported | `LineSparks::step` | Re-checked: a dead spark takes the last one's position, row, age and velocity and is processed again. |
| `asLineSparks::Draw` | ported | `LineSparks::draw` | Registered from `vehCar::PostUpdate` while sparks live (the global switch is always on). |
| `asLineSparks::asLineSparks`, `~asLineSparks`, `` `scalar_deleting_destructor' `` | not needed | — | Reference count of the shared spark tables. |

## vehWheelPtx

Dust, dirt, grass, leaves, smoke, snow, splash and rock thrown off sliding
wheels: per wheel, the two `ptxindex` slots of its ground material
(city/materials.mtl) each blast `InitialBlast × seconds × load` particles
while the wheel's slide exceeds the slot's threshold. Every vehCar (player,
opponents, police, network cars) has one. OpenMM2: `fx::VehicleEffects`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `vehWheelPtx::vehWheelPtx` | ported | `VehicleEffects::VehicleEffects` | ConstructClass, Reset. |
| `vehWheelPtx::ConstructClass` | ported | `EffectLibrary::load`, `wheelRule`, `wheelRuleName` | Eight rules shared by every car, loaded once from tune/effects (no per-car variant: the argument is always null). |
| `vehWheelPtx::Init` | ported | `VehicleEffects::VehicleEffects` | 128 particles of ptx_wheel, 8 × 8 frames; no texture switches the effect off (retail has it). |
| `vehWheelPtx::Reset` | ported | `VehicleEffects::reset` | Only the two spew fractions. |
| `vehWheelPtx::Update` | ported | `VehicleEffects::step` | Four wheels, then asParticles::Update. The particles' intensity is `cityLevel`'s lighting intensity at the car (`vehCar::PostUpdate`), always 1. |
| `vehWheelPtx::UpdateWheel` | ported | `VehicleEffects::step` | Both slots of the wheel's material, indices 0..7. |
| `vehWheelPtx::Blast` | ported | `VehicleEffects::blastWheel` | Re-checked against the asm: count = InitialBlast × seconds × load + fraction, blast when ≥ 1. MM2 writes the position into the shared rule and restores velocity and radius; OpenMM2 blasts a copy (same particles). |
| `vehWheelPtx::DestroyClass`, `~vehWheelPtx`, `` `scalar_deleting_destructor' `` | not needed | — | Frees the shared rules. |

## fxShard, fxShardManager

Sixteen paint-coloured triangles per car thrown from hard impacts
(`vehCarDamage::ApplyImpact`: impact above 500 and speed above 5 m/s,
impact / 300 shards, at most 2). OpenMM2: `fx::Shards`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `fxShardManager::fxShardManager` | ported | `fx::Shards` constants | 500, 300, 0.3, 0.3, 0.2, spin 10.8 (× 1..5), 5 m/s. |
| `fxShardManager::Init` | ported | `fx::Shards` (16), `VehicleFxSetup::shardTextures` | `Init(16, paint job's shaders, shader count)` from `vehCarDamage::Init`, for every car. |
| `fxShardManager::EmitShards` | ported | `Shards::emit` | |
| `fxShardManager::EmitShard` | ported | `Shards::emitOne` | Re-checked: draw order side, up, back, axis (−0.5, 0, −0.5 offsets), spin; ring of 16. |
| `fxShardManager::Update` | ported | `Shards::update` | |
| `fxShardManager::Draw` | ported | `Shards::draw`, `materialFor` | Material index restarts after 16 / count shards. With fewer than four materials MM2 reads the next paint jobs' shaders; OpenMM2 draws those shards untextured (first audit). |
| `fxShard::fxShard` | ported | `Shards::Shard` defaults | Age FLT_MAX (dead), identity frame, u/v 0.5. |
| `fxShard::AddShard` | ported | `Shards::emitOne` | Keeps the previous rotation, as MM2. |
| `fxShard::Update` | ported | `Shards::update` | Gravity 20, rotation about the axis, 1.8 s. |
| `fxShard::Draw` | ported | `Shards::draw` | draw_textured_tri with a 0.3 patch. |
| `fxShard::~fxShard`, `fxShardManager::~fxShardManager`, `` `scalar_deleting_destructor' `` | not needed | — | |
| `fxShardManager::EmitAllShards`, `GetInstance`, `SetShader`, `` fxShard::`vector_deleting_destructor' `` | not needed | — | Not reachable. |

## fxTexelDamage

Damage painted into a private copy of the car body's textures
(rendering-fx's `game::TexelDamage`, verified in the first audit).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `fxTexelDamage::Init` | ported | `TexelDamage::TexelDamage` | From `vehCarModel::Init` at the high LOD. |
| `fxTexelDamage::ApplyDamage` | ported | `TexelDamage::apply` | Once per frame at the first damaging impact (`vehCarDamage::Update`). |
| `fxTexelDamage::Reset` | ported | `TexelDamage::reset` | `vehCarModel::ClearDamage`. |
| `fxTexelDamage::Kill`, `fxTexelDamage`, `~fxTexelDamage` | ported | `TexelDamage` constructor / destructor | |

## vehBreakable, vehBreakableMgr

The car parts that come off. Every `vehCarModel` has two managers: A
(BREAK0..3, BREAK01/12/23/03 and the paint job's VARIANT part) ejects the
part nearest an impact of 10000 or more (`vehCarDamage::ApplyImpact` ->
`vehBreakableMgr::Impact`); B (wheels, hubs, fenders, engine) loses parts
once when the car is wrecked (`vehCarModel::EjectOneshot`, vehicle-physics).
A breakable exists when its part has a high-LOD mesh; its matrix is the
part's pivot. An ejected part becomes a hit banger thrown at speed ± 1
(4 for A, 1.3 × the car's speed for B) with a spin of 2 ± 1, and
`vehBreakableMgr::Reset` (`vehCarModel::ClearDamage` / `Reset`: repair,
respawn, regeneration, restart) puts every part back and detaches the hit
banger each one became. Traffic cars have one manager too
(`aiVehicleInstance`: threshold 2500, speed 11 ± 1, spin 4 ± 1, BREAK0..3
only), but no retail traffic model has BREAK meshes, so it never ejects.
OpenMM2: `VehicleRenderer` (the breakable list and its attached state),
RaceScreen's `breakParts` and `BangerSet::ejectPart`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `vehBreakableMgr::vehBreakableMgr` | ported | RaceScreen `breakParts` (10000), `BangerSet` `kEject*` | Threshold 10000, speed 4 ± 1, spin 2 ± 1. |
| `vehBreakableMgr::Init` | ported | `breakParts` (`CarSim::modelMatrix`) | The car's matrix the parts move with. |
| `vehBreakableMgr::Create`, `Add`, `vehBreakable::vehBreakable`, `vehBreakable::Add` | ported | `VehicleRenderer::nearestBreakable`, `wreckParts` | The candidate lists in MM2's order; a part counts when its high-LOD mesh exists. OpenMM2 leaves a part on when it has no banger data (`AddBangerDataEntry` −1 in MM2; only vpdb731 and vplafrance, which are not in the car list, lack it). |
| `vehBreakableMgr::Get` | ported | `VehicleRenderer::wreckParts` | Parts by id bit (wheel k = bit 2k, hub k = 2k + 1, fenders 8, 9, engine 10). |
| `vehBreakableMgr::Impact` | ported | `breakParts`, `VehicleRenderer::nearestBreakable` | Nearest attached pivot by squared distance under 100000, at value ≥ 10000. MM2 ejects inside the impact callback (mid-sample); OpenMM2 after the frame, with the frame's last car matrix. |
| `vehBreakableMgr::Eject` | ported (fixed) | `BangerSet::ejectPart`, `VehicleRenderer::detach` | Verified in the first audit (random order, momentum, angular impulse). `ejectPart` now returns the hit instance and the renderer keeps it (vehBreakable +0x44). |
| `vehBreakableMgr::EjectAll` | ported | `VehicleRenderer::wreckParts` (above 100 mph) | Every part of manager B in list order. |
| `vehBreakableMgr::Reset` | ported (new) | `VehicleRenderer::reattachAll`, `BangerSet::detachHit` | Parts back on and each ejected part's hit banger detached (it disappears). MM2 keeps the instance's address: if the ring has since reused it, whatever prop it now holds goes (kept). OpenMM2 detaches in ejection order rather than list order (only the active pool's order differs). |
| `vehBreakableMgr::Draw` | ported | `VehicleRenderer` part drawing | Attached parts at their LOD. |
| `vehBreakableMgr::Update`, `DrawCityLit`, `~vehBreakableMgr` | not needed | — | Not reachable (and empty). |

## dgBangerData, dgBangerDataManager

A banger's tuning (tune/banger/*.dgBangerData) and its bound. OpenMM2:
`bangers::BangerData`, `BangerDataLibrary`, `BangerSet::boundsOf`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `dgBangerData::dgBangerData` | ported | `BangerData` defaults | First audit. |
| `dgBangerData::Load` | ported | `parseBangerData` | First audit (positional reader, same result on all 995 retail files); the rule as above. |
| `dgBangerData::InitBound`, `AdjustPrim` | ported | `BangerSet::boundsOf` | First audit. |
| `dgBangerData::GetDirName`, `GetClassName` | ported | `BangerDataLibrary` | tune/banger, .dgBangerData. |
| `dgBangerData::Save` | not needed | — | Editor (asNode::Save). |
| `dgBangerData::AdjustBound`, `LoadEntry` | not needed | — | Not reachable. |
| `dgBangerData::~dgBangerData`, `` `scalar_deleting_destructor' `` | not needed | — | |
| `dgBangerDataManager::dgBangerDataManager` | ported | `BangerDataLibrary`, `EffectLibrary::bangerSheet`, `BangerSet` age constants | The 20 debris sheets fxpt1..fxpt20 (retail has 1-14 and 16), the age mode (off after `mmGame::Init`), and the glass rule node (glass props, not needed). |
| `dgBangerDataManager::AddBangerDataEntry` | ported | `BangerDataLibrary::find`, `part` | First audit. |
| `dgBangerDataManager::GetClassName`, `~dgBangerDataManager`, `` `scalar_deleting_destructor' `` | not needed | — | |
| `dgBangerDataManager::ChangeData` | not needed | — | Not reachable. |

## dgBangerInstance, dgUnhitBangerInstance, dgUnhitYBangerInstance, dgUnhitMtxBangerInstance, dgHitBangerInstance

A placed prop (unhit: standing where the city put it, flag 1) and the ring
of 40 hit instances that take a prop's place when it breaks loose or a car
part is ejected. OpenMM2: `BangerSet` and its `Prop` instances.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `dgUnhitBangerInstance::RequestBanger` | ported | `placeCityProps`, `placePathSet` (banger data only), `BangerSet::add` | Y or Mtx form; glass props need BillFlags 0x100 (never in retail; the `-andyglasshack` switch). |
| `dgUnhitBangerInstance::Init` | ported | `BangerSet::add` | CG offset, variant; flags 0x80 (glows), 0x40 (shadow unless BillFlags 0x10), 0x20 (CollisionType 0x20: gizmos only). |
| `dgUnhitBangerInstance::InitBreakables` | ported | `BangerSet::add`, `BangerDataLibrary::part` | BREAKnn data and geometry. |
| `dgUnhitBangerInstance::Impact` | ported | `BangerSet::unhitImpact` | First audit (asm-checked). |
| `dgUnhitBangerInstance::Reset` | ported (fixed) | `BangerSet::reset` | Was never reached: RaceScreen's Restart now calls it (`mmGame::Reset` -> `lvlLevel::ResetInstances`). |
| `dgUnhitBangerInstance::InitBound`, `ImpactCB` | not needed | — | Empty (`true` / nothing). |
| `dgUnhitBangerInstance::dgUnhitBangerInstance` | not needed | — | Flags 0x12 for the glass instance's base. |
| `dgUnhitYBangerInstance::GetMatrix`, `SetMatrix`, `GetPosition`, `SizeOf` | ported | `BangerSet::add` (Y form), `Prop::matrix` | Rows (c, 0, s), (0, 1, 0), (−s, 0, c). |
| `dgUnhitMtxBangerInstance::GetMatrix`, `SetMatrix`, `GetPosition`, `SizeOf` | ported | `Prop::matrix` | Full matrix. |
| `dgHitBangerInstance::dgHitBangerInstance` | ported | `BangerSet::getBanger` | Flags 0x12, identity. |
| `dgHitBangerInstance::Detach` | ported (fixed) | `BangerSet::detachHit` | Now a function of its own: the active (if any) DetachMe, then off its room. Called by the physics manager outside the active rooms and by `vehBreakableMgr::Reset`. |
| `dgHitBangerInstance::GetMatrix`, `SetMatrix`, `GetPosition`, `SizeOf` | ported | `Prop::matrix`, `Instance::matrix` | |
| `dgHitBangerInstance::~dgHitBangerInstance` | not needed | — | |
| `` dgHitBangerInstance::`vector_deleting_destructor' ``, `dgUnhitBangerInstance::~dgUnhitBangerInstance`, `dgUnhitYBangerInstance::~dgUnhitYBangerInstance` | not needed | — | Not reachable. |
| `dgBangerInstance::GetBound` | ported | `Prop::bound`, `boundsOf` | First audit. |
| `dgBangerInstance::GetEntity`, `AttachEntity` | ported | `Prop::entity`, `BangerSet::attachEntity` | |
| `dgBangerInstance::GetVelocity` | ported | `BangerSet::activeAttach` | The active's velocity (zero after Zero). |
| `dgBangerInstance::GetData` | ported | `Instance::data` | |
| `dgBangerInstance::SetVariant` | ported | `variantOf` | |
| `dgBangerInstance::Draw` | ported | `BangerSet::draw` | Unlit (BillFlags 0x80) and trees as the first audit verified. Its LOD bump for lvlInstance flag 4 is for gizInstance and waypoints: no banger has the flag. |
| `dgBangerInstance::DrawTree` | ported | `BangerSet::draw` (trees) | |
| `dgBangerInstance::DrawGlow` | ported (fixed) | `BangerSet::draw` (glows), `BangerSet::standing` | Flag 1, not "no active": a lamp an active holds still glows. Night only (`mmGame::InitWeather` -> `dgBangerManager::InitGlow` at time of day 3). |
| `dgBangerInstance::DrawShadowMap` | not needed | — | Draws a SHADOW mesh; no retail banger PKG has one. |
| `dgBangerInstance::DrawShadow`, `DrawReflected`, `SetupGfxLights` | ported | `BangerSet::draw` | Empty in MM2 (no shadow, no reflection, no local lights). |
| `dgBangerInstance::ComputeLod` | not needed | — | Answers 0; banger LODs come from `lvlInstance::IsVisible` (`objectLod`). |

## dgBangerManager, dgBangerActive, dgBangerActiveManager

The ring of 40 hit instances (`dgBangerManager::Init(40)` in mmGame's
setup) and the pool of 32 rigid bodies that simulate props while they move.
A prop an active holds is simulated until it sleeps (speed² 0.1, spin² 0.5),
falls below −100 or its room is left; its CollisionType decides what it
collides with; its debris (InitialBlast particles, then SpewRate) is born
when the active attaches. OpenMM2: `BangerSet`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `dgBangerManager::dgBangerManager`, `Init` | ported | `BangerSet` (`kMaxHit`, `getBanger`) | 40, made as first used. |
| `dgBangerManager::GetBanger` | ported | `BangerSet::getBanger` | Slot 0 handed out twice after each wrap (kept). |
| `dgBangerManager::InitGlow` | ported | `BangerSet::DrawParams::glows`, "s_yel_glow" | |
| `dgBangerManager::Reset` | ported | `BangerSet::reset` | From `lvlLevel::ResetInstances`; now reached on restart. |
| `dgBangerManager::~dgBangerManager`, `` `scalar_deleting_destructor' `` | not needed | — | |
| `dgBangerActive::dgBangerActive` | ported | `BangerSet::BangerSet` | |
| `dgBangerActive::Attach` | ported (fixed) | `BangerSet::activeAttach`, `EffectLibrary::bangerSheetNumber` | Sheet = TexNumber − 1 clamped to 0..20 (negative: fxpt1; 21 and up read past MM2's table: none); without a sheet only the particles reset and the last rule and sheet stay (they went before); a stationary rule on a prop no longer standing gets no rule (unreachable: no retail rule is stationary). |
| `dgBangerActive::Detach`, `DetachMe` | ported | `activeDetach`, `detachMe` | |
| `dgBangerActive::Update` | ported | `Active::beforeIntegrate` / `afterIntegrate` / `afterCollisions`, `directUpdate` | |
| `dgBangerActive::PostUpdate` | ported | `BangerSet::update` | Detach when asleep or below −100, else the debris updates. |
| `dgBangerActive::GetICS` | ported | `phys::Body::ics` | |
| `dgBangerActive::GetInst` | not needed | — | Answers null. |
| `dgBangerActive::~dgBangerActive`, `` `scalar_deleting_destructor' `` | not needed | — | |
| `dgBangerActiveManager::dgBangerActiveManager` | ported | `BangerSet::BangerSet` | |
| `dgBangerActiveManager::GetActive`, `Attach`, `Detach` | ported | `activeOf`, `managerAttach`, `managerDetach` | |
| `dgBangerActiveManager::Update` | ported | `BangerSet::update`, `declare` | First audit; the age branch re-checked. |
| `dgBangerActiveManager::Reset` | ported | `BangerSet::reset` | |
| `dgBangerActiveManager::~dgBangerActiveManager`, `` `scalar_deleting_destructor' `` | not needed | — | |

## dgPath, dgPathSet, cityPropulator, dgRoadDecalInstance

Path sets (`.pathset`, "PTH1") place props along points and lines; the
propulator places street props from propdefs.csv / proprules.csv; road
decals are path strips drawn on the street. OpenMM2:
`bangers::PropPlacement`, `bangers::RoadDecals`, `city::PathSet`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `dgPathSet::Load` | ported | `city::parsePathSet`, `placeCityProps` | "PTH1" check, path count; no file, no props. |
| `dgPath::Load` | ported | `city::parsePathSet`, `decodePathPlacement` | First audit. |
| `dgPath::Enumerate` | ported | `placePathSet` | First audit (asm-checked). |
| `dgPath::dgPath` | ported | `PathPlacement` defaults | Type 2, spacing 5 (Load always sets both). |
| `dgPath::SetName`, `~dgPath`, `dgPathSet::dgPathSet`, `~dgPathSet`, `Kill`, `` `scalar_deleting_destructor' `` | not needed | — | |
| `cityPropulator::Load` | ported | `placeCityProps` | Both CSV files or no street props. |
| `cityPropulator::LookupRule` | ported | `parsePropRules`, `placeStreetProps` | |
| `cityPropulator::Propulate` | ported | `placeStreetProps` | First audit. |
| `cityPropulator::cityPropulator`, `~cityPropulator` | not needed | — | Keep the SDL pointer, free the tables. |
| `dgRoadDecalInstance::dgRoadDecalInstance` | ported | `RoadDecals::load` | Paths of 3 points or more. |
| `dgRoadDecalInstance::DrawShadow` | ported | `RoadDecals::draw` | Deviation (first audit): drawn every frame, not per visible room. |
| `dgRoadDecalInstance::Draw`, `GetMatrix`, `SetMatrix`, `GetPosition`, `SizeOf` | ported | `RoadDecals` | Empty, null, nothing, the first point. |
| `dgRoadDecalInstance::~dgRoadDecalInstance` | not needed | — | Not reachable. |

## ptxGlass::ptxShard

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `ptxGlass::ptxShard::SetCentroidFromVerts` | not needed | — | Called only by `ptxGlass::AddShards`, from `dgGlassInstance::Impact`: glass props need BillFlags 0x100, which no retail banger data sets. ptxGlass belongs to world-objects. |

## For other subsystems

- **vehicle-physics** (`vehCarModel::InitBreakable` / `EjectOneshot`):
  `VehicleRenderer::wreckParts` takes a wheel, hub, fender or the engine
  when its pivot exists; MM2 makes the breakable when the part's high-LOD
  mesh exists (`vehCarModel::InitBreakable`), whatever the pivot.
- **world-objects**: `dgBangerInstance::Draw` raises the LOD by one for
  lvlInstance flag 4 (set by `gizInstance` and `mmWaypointInstance`);
  CollisionType 0x20 (bridges, ferries) sets lvlInstance flag 0x20.
- **city-render**: `lvlLevel::MoveToRoom` keeps the city's static
  instances (flag 0x400) at the tail of the room's list, after the movable
  instances, each new one before the previous one; CityLevel lists the
  statics in load order before the banger source.
- **ai**: `aiVehicleActive`'s two particle systems and birth rules are
  never used.
