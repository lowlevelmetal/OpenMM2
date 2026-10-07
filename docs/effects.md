# Particle effects

Code: `src/game/fx/` — `BirthRule`, `Particles` (asParticles),
`ParticleRenderer` (agiMeshSet cards), `EffectLibrary`, `SkidMarks`,
`VehicleEffects`, `Weather`, `Random`. Tool: `mm2tool fxsim <source>
<rule|banger:model> <outdir>` renders side-view PNG frames.

## Ported from MM1 (Open1560 `mmeffects/ptx.cpp`, `game.asm`)

* **RNG**: `irand()` = MSVC LCG (`seed × 214013 + 2531011`, 15 bits),
  `frand() = irand() × 2⁻¹⁵`. OpenMM2 gives each system its own seed (the
  original shared one global).
* **asBirthRule::InitSpark**: velocity and position get `(frand() − 0.5) × var`
  per axis in the original draw order (y, x, z); mass is stored inverted;
  life, drag, damp, radius, dRadius, dAlpha and dRotation get their
  variations; the frame is `TexFrameStart` with `kCycleFrames`, else a random
  frame in `[start, end]`.
* **asParticles::Update**: spew `SpewRate × dt` until `SpewTimeLimit`;
  per particle: life, kill at life < 0 / y < −50 / alpha 0; wind drag
  `(wind − v) |wind − v| × WindDensity × Drag / mass` (density 0 by default),
  gravity, damping, radius growth; 30 Hz frame ticks for alpha, rotation and
  frame cycling; splash rules (`kSplashes`) stop at the ground and advance
  the frame by 4. Damping: the original multiplied by `Damp` once per update
  (≈ 30 Hz); OpenMM2 uses `Damp^(dt × 30)`, identical at 30 Hz and frame-rate
  independent (Open1560 uses a linear approximation).
* **Cards**: quad `(±1, ±1)` scaled by the radius, 32 rotation steps
  (`(rotation >> 2) & 31`), frame `f` of a W×H sheet at column `f % W`, row
  `f / W` from the start of the texture's stored rows.
* **Skid marks** (`mmSkidManager::Update`, `LayTrack`, `mmSkid::AddSkid`):
  speed threshold 3 m/s, slip threshold `SlipPercentThresh` (0.2), `ShouldSkid`
  (speed > 7, throttle > 0.5 or brakes > 0.7), one segment every 0.1 s, 64
  segments per wheel; a segment joins the previous edge to the current
  `±width/2` points.
* **Wheel particles** (`mmWheel::GenerateSkidParticles`): count accumulates
  `dt × multiplier × 30 × max(slip × clamp(speed × 0.1, 0.1, 1), 0.25)`.

## MM2 data

| Data | Use | Evidence |
|---|---|---|
| `tune/effects/*.asbirthrule` | surface and smoke rules | parse; MM2 adds `Height`, `Intensity`, `Color` (initial particle colour) |
| `Position` in effect files | editor leftovers (e.g. smoke at −1310, 11, −462); cleared, emitters place particles | inferred |
| `city/materials.mtl` `ptxindex a b`, `ptxthreshold ta tb` | surface rule above slip `ta` / `tb` | road 4, grass 1/2, sand 1/5, cobblestone 4/7, water −1/6 → indices 0 default, 1 dirt, 2 grass, 3 leaf, 4 smoke, 5 dust, 6 splash, 7 rock, 8 snow (**inferred**) |
| `texture/ptx_wheel.tex` | 8×8 sheet for surface rules | verified: frame ranges land on dirt, dust, grass, leaves, smoke, splash pictures |
| `tune/rain.asbirthrule`, `tune/snow.asbirthrule` | weather around the camera, `texture/ptx_rain` 4×4 (snow uses frames 5–7, the small flakes) | emitter offsets inferred |
| `texture/fxpt<N>` | banger debris, 2×2 | pictures match props |
| `tune/vehicle/*.vehCarDamage` particle fields | damage smoke from `SmokeOffset` (and `SmokeOffset2`), `fxpt8` 2×2; rate 30 × damage per second | rate inferred (SpewRate is 0 in the data) |
| `texture/tire_track.tga` | skid mark texture (MM2 has no MM1 `skid` mesh); v advances one repeat per tyre width | inferred; colours inferred |

Not done: sparks (`spark.tga`, MM1 `asLineSparks`), glass shards, car
breakable parts flying off, exhaust smoke, explosion.
