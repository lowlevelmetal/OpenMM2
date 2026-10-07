# Particle effects

Code: `src/game/fx/` — `BirthRule`, `Particles` (asParticles, FixedTicker),
`ParticleRenderer` (asMeshCardInfo cards), `EffectLibrary`, `SkidMarks`
(lvlTrackManager), `VehicleEffects` (vehWheelPtx, vehCar::UpdateTrack,
vehCarDamage smoke), `Weather`, `Random`. Tool: `mm2tool fxsim <source>
<rule|banger:model> <outdir>` renders side-view PNG frames.

Everything below follows MM2's own code (build 3393, the functions named);
the structure was first ported from MM1 (Open1560 `mmeffects/ptx.cpp`), which
differs in many details (noted where it matters).

## Timing

MM2 updates its effects once per frame with the frame time, and several
rules count updates rather than seconds (frame cycling, DAlpha, DRotation and
DRadius are "per 1/60 s" through `ftol(delta × 60 × dt)`, damage smoke is one
puff per update). OpenMM2 runs them at a fixed 60 Hz (`FixedTicker`, at most
8 updates per frame), which is the original at 60 fps — the same rate the
physics reproduces (`World::advanceFixed`).

## Particles (asBirthRule, asParticles, asMeshCardInfo)

* **RNG**: `irand()` = MSVC LCG (`seed × 214013 + 2531011`, 15 bits),
  `frand() = irand() × 2⁻¹⁵`. OpenMM2 gives each system its own seed (the
  original shared one global).
* **asBirthRule defaults** (constructor): Life, Mass, Radius, Damp,
  Intensity 1, Gravity −9.8, Color −1 (white), everything else 0.
* **asBirthRule::InitSpark**, in the original's draw order: velocity x, y,
  z = base + (frand − 0.5) × var; 1 / mass; life; drag; damp; the frame
  (TexFrameStart with flag 4, else `irand() % (end − start + 1) + start`);
  radius; position x, y, z; colour; gravity; dRadius; dAlpha; rotation 0;
  dRotation; the particle keeps the rule's Height and the low byte of
  BirthFlags. The file's Color is 0xAABBGGRR and becomes the vertex colour
  0xAARRGGBB (splash `-331546` is pale blue, smoke `-251989786` bluish white
  at alpha 240).
* **asParticles::Blast** transforms the birth position by the system's matrix
  when it has one (not the velocity); the count is capped by the pool.
* **asParticles::Update**: spew `SpewRate × dt` (fraction kept) until
  SpewTimeLimit; per particle: life −= dt, die at life ≤ 0, y < −50 or alpha
  0; acceleration `−|v + wind| × Drag × (v + wind) / mass` plus gravity (the
  wind is zero; there is no wind density, so Drag always acts — MM1 needed a
  density); v += a dt, then p += v dt; the colour is rebuilt from the birth
  colour times the system's intensity (1; `cityLevel::GetLightingIntensity`
  answers 1); alpha += ftol(DAlpha × 60 dt) (0 when it would go negative);
  rotation += ftol(DRotation × 60 dt); radius += 60 dt × DRadius. There is no
  damping in flight.
* **BirthFlags** (low byte, per particle): 2 bounce — below `radius + Height`
  the particle is put on Height and, if falling, its velocity is multiplied
  by Damp with y reversed; 4 cycle — one frame per update through
  [start, end); 8 stop — below Height the particle stops on it and dies next
  update (rain; MM1 turned it into a splash frame); 0x10 shadow — see below.
  1 is only read by banger code; 0x20 and 0x40 (bangers set 0x70) are read by
  nothing.
* **Cards** (asMeshCardInfo::Init/Draw): quad (±1, ±1) × radius, 32
  precomputed rotations picked by `rotation & 31` (MM1 used `>> 2`), facing
  the camera; frame f of a W×H sheet at column f % W, row f / W.
* **Shadows** (asMeshCardInfo::DrawShadows, flag 0x10): a flat card on the
  Height plane while the particle is more than 1 cm above it, drawn before
  the cards, colour halved with red and blue swapped (as the original does).

## Wheel particles (vehWheelPtx)

One system per car: 128 particles from `texture/ptx_wheel` (8×8 frames).
The rules are `tune/effects/<name>.asbirthrule` by the ptxindex values of
`city/materials.mtl` (`vehWheelPtx::PtxName`): 0 dirt, 1 dust, 2 grass,
3 leaf, 4 smoke, 5 snow, 6 splash, 7 rock. In rain the smoke rule is replaced
by a copy of splash (`mmGame::InitWeather`).

Each update, for each wheel on a material, both ptxindex slots blast when the
wheel's slide exceeds that slot's ptxthreshold (`vehWheelPtx::UpdateWheel`;
both can fire together):

* load factor s = (suspension force / static load + 1) / 4 below the static
  load, else min(1, ((force − load) / (spring × SuspensionLimit) + 1) / 2);
* radius = s × tyre width × Radius / 2 (RadiusVar still applies);
* position = contact point + sign(−wheel spin) × wheel radius along the
  contact frame's back axis (the side the tread leaves the ground), lifted
  by the radius;
* velocity = (lateral speed × Velocity.x, |spin| × radius × Velocity.y,
  −slip speed × Velocity.z) in the contact frame (right, normal, back);
* count: s × InitialBlast × dt, accumulated per slot (shared by the four
  wheels) — InitialBlast is the rate per second.

The rule files' Position is an editor leftover; Blast overwrites it.

## Tyre tracks (lvlTrackManager, vehCar::UpdateTrack)

Four tracks per car (one per wheel), 64 vertex pairs each, laid while the
wheel's skid flag is set (vehWheel: slide > 0.5), its material is not
`water` and the car's room lacks the runtime flag 0x10 (set by `gizBridge` on
the opening bridges, which OpenMM2 does not have yet).
The pair is the contact point ∓ half the tyre width along the wheel
matrix's axle. The first pair waits until the wheel has moved 10 cm; while
the wheel keeps its direction (dot ≥ 0.99) and stays within 10 m of the last
fixed pair, the newest pair follows the wheel; otherwise a pair is added. A
pair's v is its distance from the last fixed pair in tyre widths (0 starts a
strip). Strips are drawn with `texture/tire_track.tga` (dark tread, alpha up
to 25%; vehCar::Init sets its clamp-U flag), u across, white vertex colour,
alpha blended, no depth writes. MM2 (`vehCar::DrawTracks`) also turns the
depth test off and draws the tracks right after the static city, before cars;
OpenMM2 keeps the depth test with a depth bias because its draw order
differs.

## Damage and exhaust smoke (vehCarDamage)

64 particles from `texture/fxpt8` (2×2). The rule (vehCarDamage::Init's
EngineSmokeRule: Velocity (0, 1, 0), VelocityVar (1, 2, 1), Life 0.8 ± 0.4,
Mass 0.2, Drag 1, Radius 0.3 ± 0.1, DAlpha −15, DRadius 0.03, Gravity 3) is
overwritten by the particle fields of `tune/vehicle/<car>.vehCarDamage`
(vehCarDamage::FileIO includes the rule's). MM2 shares one rule between all
cars, so the last car loaded wins; OpenMM2 keeps one per car.

Each update (`vehCarDamage::Update`): level = ceil(4 × clamp((damage −
MedDamage) / (MaxDamage − MedDamage))); at levels 1–4 the frame is 1, 0, 3, 2
and one puff is spewed (ParticleMultiplier 1) at SmokeOffset, or — with a
SmokeOffset2 — at a random point between the two, or alternately at each with
DoublePivot. Positions and the velocity are in the car's body (centre of
gravity) frame (`vehCarDamage::SpewSmoke`). After the particle update the
`exhaust0`/`exhaust1` pivots (`geometry/<car>_exhaust<N>.mtx`) spew
clamp((rpm − 2000) / (MaxRPM − 2000)) puffs per update whatever the damage,
with the frame last chosen.

## Rain (weather 3)

200 particles of `tune/rain.asbirthrule` (spewed by the rule, 200/s) from
`texture/ptx_rain` (4×4 frames, a random one each), born around camera-space
(0, 10, −10) — set in `cityLevel::DrawRooms` — stopping below Height 0
(flag 8). No rain is drawn while the camera is underground (PSDL room flag
0x02); MM2 also hides it in rooms with a landmark when a 100 m probe upwards
hits something (not ported). MM2's weather is 0–3 (clear, cloudy, fog,
rain); it has no snow.
OpenMM2's snow option draws MM1's leftover `tune/snow.asbirthrule` (frames
5–7) from 6 m above and 6 m ahead of the camera (OpenMM2 addition).

## Data

| Data | Use |
|---|---|
| `tune/effects/*.asbirthrule` | wheel rules by name (above); `default` and `engine smoke rule` are editor files MM2 does not load |
| `city/materials.mtl` `ptxindex a b`, `ptxthreshold ta tb` | the two wheel rules of a surface and their slide thresholds (lvlMaterial; defaults −1/−1, 0.25/0.5) |
| `texture/ptx_wheel.tex` | 8×8 sheet: dirt 0–1, dust 2–5, grass 6–9, leaves 10–13, smoke 14, splash 16–21, snow 23–24 |
| `tune/rain.asbirthrule`, `texture/ptx_rain` | rain |
| `texture/fxpt<N>` | banger debris, 2×2 |
| `tune/vehicle/*.vehCarDamage` | smoke rule fields, SmokeOffset/SmokeOffset2/DoublePivot |
| `texture/tire_track.tga` | tyre tracks |

## Impacts (vehCarDamage::ApplyImpact)

An impact the car reports counts when its impulse, scaled by the other
body's share of the two masses, exceeds ImpactThreshold, at 10 mph or more
unless the other party is a vehicle. Then:

* **Sparks** (`asLineSparks`, `fx/LineSparks`), above 15 mph: ftol(16 ×
  impact × frame seconds) of them, at most 64 per car, born within 5 cm of
  the contact, flying 4–5 m/s along the contact normal and 6–7 m/s across it.
  They update in steps of at least 1/30 s: gravity 20 m/s², a bounce off
  y = 0 keeping 80%, and an age byte falling 650 per second from 192–255
  that picks the colour column of `texture/spark.tga` (8×8 ramps, 24-bit
  pixels with alpha 0x80; asSparkLut). Drawn as lines from just behind the
  previous position to the new one.
* **Shards** (`fxShardManager`, `fx/Shards`): above impact 500 and 5 m/s,
  impact / 300 of them (at most 2), from a ring of 16 per car: velocity
  (±0.3, 0.15–0.3, 0.02–0.2) × car speed sideways/up/back in the body frame,
  tumbling at 10.8–54 rad/s about a random axis, falling at 20 m/s², for
  1.8 s. Each is a 0.1 m right triangle showing a random 0.3 × 0.3 patch of
  the paint job's material of its index, both sides drawn.
* **Texel damage** at the first such impact point of the frame (see
  rendering.md).
* **Parts breaking off** (`vehBreakableMgr::Impact`): with an impact of
  10000 or more the attached breakable part (BREAK0–3, BREAK01/12/23/03, the
  paint job's VARIANT) whose pivot is nearest the impact flies off as a
  banger (bangers.md).
* **Wrecked cars** (`vehCarModel::EjectOneshot`, once when the damage
  reaches MaxDamage): above 100 mph every wheel, hub and fender (and the
  engine), above 75 mph two random wheel/hub pairs and a fender, above
  50 mph one pair, thrown at 1.3 times the car's speed.

Ejected parts take their physics from `tune/banger/<car>_<part>.dgBangerData`
(`vehBreakableMgr::Create`). Retail data has these for every car's wheels and
for some cars' BREAK parts, sirens and fenders, but none for hubs. MM2 still
registers a part without a file (`dgBangerDataManager::AddBangerDataEntry`
returns no entry); OpenMM2 leaves such parts on the car (inferred: what MM2
does when ejecting them is not known).
