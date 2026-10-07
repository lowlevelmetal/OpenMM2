# In-game sound effects

`src/audio/game` plays the car, city and voice sounds. MM1 (Open1560's
`game.asm`) hard-coded most of this in its audio classes; MM2 moved the
numbers into CSV tables in `MM2AUD.AR`. The code follows MM1's structure
where it is known and reads MM2's tables. "Ported" below means the behaviour
was read from MM1's code; "inferred" means it is OpenMM2's reading of the MM2
data and should be compared with the original game.

Music (DirectMusic) is separate: see `docs/music.md`.

## Volume units

Every volume in the tables is an Angel volume `v` in 0..1. `SoundObj::SetVolume`
passes `v * 10000 - 10000` to DirectSound, so `v` is linear in decibels:
1 = 0 dB, 0.9 = -10 dB, 0.25 = -75 dB (**ported**; `ageVolumeToGain`). This is
why the tables use values such as 0.91 / 0.93. Pitch is a multiple of the
sample rate, clamped to [0, 10] by `AudSound` and to 100..100000 Hz by
`SoundObj` (**ported**).

## Inputs from the game (`CarAudioInputs`)

| Field | Meaning |
|---|---|
| `rpm`, `idleRpm`, `engineRunning` | engine speed; never below `idleRpm` while running (the physics reports 0 RPM at rest) |
| `throttle`, `brake` | 0..1 pedals (brake drives the freight air brake) |
| `speed` | m/s, magnitude of the car's velocity |
| `gear` | -1 reverse, 0 neutral, 1.. forward |
| `wheels[4]` | `onGround`, `slip` = max(&#124;LatSlipPercent&#124;, &#124;LongSlipPercent&#124;), `surface` = `surfaceSoundIndex(material name, mtl sound)`, `suspensionSpeed` (m/s, compressing > 0) |
| `impacts` | per impact since the last update: `force` = normal impulse (N s), `audioId` = the banger's `AudioId` (0 for walls, ground and cars), `position` |
| `horn`, `siren`, `wrecked`, `tireWobble` | buttons and damage state |
| `transform`, `velocity` | for 3D cars (opponents, police, network) |
| `inTunnel` | selects the tables' tunnel surface entry |

## Ported vs inferred

| Behaviour | Evidence |
|---|---|
| Engine samples stop below volume 0.25 and restart above it | ported (`EngineAudio::UpdateRPM`) |
| Clutch sample plays when the gear jumps directly between reverse and first | ported (`UpdateRPM`, gear stored as gear + 1) |
| Engine row evaluation: silent outside [fade in start, fade out end], volume = min + (max - min) × fade, pitch linear over the pitch shift range | **inferred**; MM1 used two samples with volume = clamp(k × rpm) |
| Freight reverse beeper loops while in reverse; air brake plays once when the vehicle stops (speed ≤ 0.04 m/s) under braking, re-armed when the brake is released | ported (`mmPlayerCarAudio::Update`) |
| Which cars are freight / police | data (`shared/vehtypes.csv`) and flags 2 / 4 / 8 in the car table (**inferred** meaning) |
| Surface loop needs speed > 2 m/s, ≥ 2 wheels down, no skid; it keeps its type while either front wheel still reports it | ported (`mmSurfaceAudio::UpdateSurface`) |
| Surface volume and pitch ramp with speed / max speed | **inferred** (MM1: clamp(k × speed)) |
| Skids need speed > 1 m/s; the sample is chosen by the slippage range containing the slip; volume ramps from min to max skid volume between the first range's start and slip 1 | speed threshold ported (`UpdateSkidClear`), selection and volume **inferred** |
| Snow (`default_surfaceice.csv`) skids picked by speed, triggered above slip 0.25 | **inferred** |
| Tunnel surfaces use the table's "tunnel sound index" entry | **inferred** from the field name |
| Impact samples: every sample whose force range contains the force, skipped while that sample still plays; volume ramps over the force range; "frequency" = pitch | skip-while-playing ported (`mmImpactAudio::PlayWall`), the rest **inferred** |
| Impact strength = &#124;normal · impulse&#124; | ported (`mmCarSim::PlayImpactAudio`) |
| Police siren: sample i plays for its play time, then its "next index" (a random choice among several in the opponent table) | **inferred** (MM1 hard-coded `FluctuateSlowSiren` / `FluctuateFastSiren`) |
| Fire truck: the horn button toggles its siren-loop horn | **inferred** (flag 8, looping `FIRETRUCKSIREN`) |
| Suspension thump and tyre wobble curves | **inferred** (`suspensionaudio.csv`, `tirewobble.csv`; MM1 had `SetWobbleVol` / `SetWobblePitch`) |
| Opponents and police: same tables from `aud/cardata/opponent`, 3D, silenced beyond 150 m | **inferred** distances (`aiAudioManager` gave sounds only to the nearest cars) |
| Ambient traffic engine pitch from the speed band containing the speed; horn patterns of (play, pause) pairs; a hit harder than "min stuck horn impact force" jams the horn | **inferred**. Every engine table ends with a 0..500 band whose purpose is unknown |
| Ambient type lookup `va_sedans_s` → `va_sedan_s_engine.csv` | **inferred** (the audio files use singular names) |
| City ambience (`<city>ambientcontainer.csv`): type 0 loops at the nearest emitter point, types 1 and 2 are one-shots at random intervals [low, high] (1 at a random point in range, 2 at the nearest point), type 3 only on request; audible area 2 = along the polyline | **inferred** |
| Rain: exterior 0.76 / interior 0.83, sheltered 0.65 / 0; thunder every 15 s with a lightning flash at 13 s in storms | ported (`mmRainAudio`) |
| Announcer files `<announcer><prefix>NN` for NN in (add, end]; Cops & Robbers prefixes carry the announcer | verified against the sample names |
| Pre-race line choice: r = U × 0.3; ≤ 0.05 time of day, ≤ 0.1 weather, ≤ 0.15 vehicle, else the mode's lines | ported (`GetRandomPreRace`), except MM1's weather/time-of-day flag test |
| SF lists five announcers but ships `as1`, `as2`, `as4`, `as5`; only shipped ones are picked | data |
| Creature voices fire after the speed has stayed in a range for "min time in range" and re-arm after "max time out of range"; impact lines after their delay | **inferred** |

## Not implemented

* MM1's tunnel echo (`EchoOn`: 0.15 s delay, 0.96 attenuation) and EAX
  reverb: the mixer has no effects yet.
* MM1's second braking sound (`mmPlayerCarAudio` slot +0xF4, braking above
  5 m/s): MM2's tables name no sample for it.
* Crash Course checkpoint location lines (`cc_cpoint_*info.csv`).

## Data quirks

* `skidflagstone` is referenced by the surface tables but the file is
  `skidflagstone1`; like the original's loader, OpenMM2 leaves it silent.
* `engineparamsplay.csv` / `engineparamsopp.csv` have binary garbage in their
  name column (tuning leftovers) and are not used.
* Several engines (fire truck, 4x4) exceed full scale when their samples
  overlap at maximum table volume; the engine bus / master volume sets the
  final level, as DirectSound's mixer would have saturated too.

## Tools

`mm2tool carsound <game-source> <car> [--wav out.wav] [--play]` renders an
800 → max → 800 RPM sweep, a skid and a hard impact through the mixer and
prints per-second peak / RMS levels.
