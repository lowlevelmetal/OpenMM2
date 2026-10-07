# In-game sound effects

`src/audio/game` plays the car, city and voice sounds. MM2 drives them from
CSV tables in `MM2AUD.AR`; the code ports MM2's own audio classes (from the
build 3393 reference) and reads those tables. "MM2 (`Class::Method`)" below
names the original function a behaviour was taken from; "inferred" marks what
is still OpenMM2's reading and should be compared with the original game.

Music (DirectMusic) is separate: see `docs/music.md`.

## Volume, pan and pitch units

Every volume in the tables, and every volume the code computes, is an Angel
volume `v` in 0..1. `audSound::SetVolume` passes `(v - 1) * 10000` to
DirectSound, so `v` is linear in decibels: 1 = 0 dB, 0.9 = -10 dB, 0.25 = -75 dB
(MM2 `audSound::SetVolume`; `ageVolumeToGain`). This is why the tables use
values such as 0.91 / 0.93.

Pans are Angel pans in -1..1. `audSound::SetPan` passes `pan * 10000` to
DirectSound, which attenuates the far channel by `|pan| * 100` dB and leaves
the near one at full level. The mixer attenuates the far channel linearly, so
`agePanToMixer` converts: a pan of 0.2 puts the far channel at -20 dB (MM2
`audSound::SetPan`). Values outside -1..1 are rejected by DirectSound and leave
the previous pan (this matters for the rain's thunder, below).

Pitch is a multiple of the sample rate; `audSound::SetPitch` clamps the
resulting rate to 100..100000 Hz and nothing else (MM2; MM1 also clamped the
multiplier to 0..10).

## Positioned sounds (`Object3D`)

MM2 does not use DirectSound3D for game sounds. Every positioned object (other
cars, police, ambient traffic, city emitters, creatures) plays 2D buffers whose
volume, pan and frequency it computes itself, and only a few objects sound at
a time. Ported from MM2 `Aud3DObject` and `Aud3DObjectManager`:

| Behaviour | Evidence |
|---|---|
| Attenuation = `1 - (d² - min²) / (max² - min²)`, a volume multiplier (so linear in dB against the squared distance); full inside the minimum distance | MM2 (`Aud3DObject::CalcPercentToMaxDist2`, `CalculateAttenuation`) |
| Pan = 0.2 × x / (&#124;dx&#124; + &#124;dy&#124; + &#124;dz&#124;), x in listener space; 0 inside the minimum distance | MM2 (`CalcSinglePlayerPan`) |
| Doppler = `1 + (pseudo distance closed since the last update) * factor * dt`, factor `1 / 56.7166` (`2 / 56.7166` for ambient traffic). The `* dt` makes the shift tiny; ported as is | MM2 (`CalculateDoppler`; `vehCarAudio` / `aiAmbientVehicleAudio` statics) |
| An object starts sounding within its maximum distance and stops past it; a police car with its siren on keeps its slot and its last attenuation at any distance | MM2 (`WithinMaxDistance`, `PastMaxDistance`, flag +0x49 set by `vehPoliceCarAudio::StartSiren`) |
| Slots: four in single player, one held for good by the player's car (non-positioned objects get +1000000 priority), so three for everything else. A newcomer takes the slot of the farthest object of no higher priority if its priority is higher, or equal and it is closer | MM2 (`mmPlayer::Init` creates `Aud3DObjectManager(4)`; `Add`, `FindGreatestDistance`, `GetPriority`) |
| Priorities: cars 9, police 7 (10 with the siren on), ambient traffic 8, city emitters from their file (12 in every retail file, so they win over cars) | MM2 (`vehCarAudio::Init`, `vehPoliceCarAudio::Init` / `StartSiren` / `StopSiren`, `aiAmbientVehicleAudio::Init`, `Aud3DAmbientObject::Load`) |
| Drop-offs: cars and police 0..150 m, ambient traffic 0..100 m, pedestrians 0..40 m, city emitters from their file | MM2 (`SetDropOffs` callers) |
| `vehCarAudio`'s 25 m "amplification" multiplies by 1 + 0.01 × a speed field nothing sets, i.e. by 1 | MM2 (`vehCarAudio::UpdateAudio3D`); not ported |

## Inputs from the game (`CarAudioInputs`)

| Field | Meaning |
|---|---|
| `rpm`, `idleRpm`, `engineRunning` | engine speed; never below `idleRpm` while running (the physics reports 0 RPM at rest) |
| `speed` | m/s, `vehCarSim` speed |
| `gear` | -1 reverse, 0 neutral, 1.. forward (MM2's gear index is this + 1) |
| `wheels[4]` | `onGround`; `slip`, vehWheel's skid amount (MM2 +0x220: the slip percentage of the sliding direction, 0 while gripping; the app passes max(&#124;lat&#124;, &#124;long&#124;) slip, **inferred** mapping); `surface`, the material's `sound:` value; `suspensionSpeed`; `brakeCoef` (tuning) |
| `impacts` | `force` = &#124;x&#124; + &#124;y&#124; + &#124;z&#124; of the impulse vector (`impactStrength`), `audioId` = impact table index |
| `horn`, `siren`, `wrecked` | buttons and AI state |
| `tireWobble`, `wheelRadius` | (damage - MedDamage) / (MaxDamage - MedDamage); rear left wheel radius |
| `groundBelow` | player only: distance to the ground below, for the airborne flag |
| `transform`, `velocity` | positioned cars |
| `inTunnel` | the listener's tunnel echo state (the app does not detect tunnels yet) |

## Cars

| Behaviour | Evidence |
|---|---|
| Sound class by `shared/vehtypes.csv`: semi or bus (vpcentury, vpbus, vpddbus) → semi sounds; police (vpcop, vpsemi, vpeagle) → siren; "Always nitro" (FALSE in retail) would give every other car a nitro sound. The car table's "flags" and "Num Engine Samples" columns are read and discarded | MM2 (`vehCarAudioContainer::RegisterTypes`, constructor; `vehCarAudio::Load`) |
| Engine row volume: min volume at or below the fade-in start and at or past the fade-out end (so a sample keeps playing at its minimum volume, e.g. -45 dB for 0.55), linear up to max over the fade-in, max between, linear down over the fade-out | MM2 (`vehEngineSampleWrapper::CalculateVolume`) |
| Engine row pitch: min up to the shift start, max from the shift end, `min + rpm * (max - min) / (end - start)` between — the slope multiplies the whole RPM, so the pitch jumps at the start of the range (and past max pitch just before its end) unless the range starts at 0 | MM2 (`CalculatePitch`, `ParseCSVBuffer`) |
| A sample stops while its table volume is below 0.25; positioned cars then scale volume by attenuation and pitch by doppler | MM2 (`vehEngineSampleWrapper::UpdateRPM`) |
| Clutch sample whenever the gear changes into or out of reverse (including the first update in reverse); positioned cars have none | MM2 (`vehCarAudio::UpdateGear`, `Load`) |
| Horn: loops while held, from the start; positioned cars only have one when they are network players (container mode 0), AI cars (mode 1) none | MM2 (`vehCarAudio::PlayHorn` / `StopHorn`, `vehCarAudioContainer::Init`) |
| A car with siren lights (every police-list car: vpcop, vpsemi) toggles its siren with the horn button and never plays its horn, so the fire truck's FIRETRUCKSIREN horn is unused and it sounds the city's police siren | MM2 (`mmGame::UpdateHorn`, `vehCarAudioContainer::PlayHorn`, `vehCarModel::InitSirenLight`) |
| Engine silenced after a damage out (`silenceEngine`) | MM2 (`vehEngineAudio::Silence`, `mmSingleRace` / `mmSingleBlitz`) |
| Semi reverse beeper loops in reverse. The air brake hisses once when the truck is stopped (≤ 0.04 m/s) and "braking" — which `vehSurfaceAudio::IsBrakeing` reads as both front wheels' BrakeCoef > 0.5, a tuning value. Every retail semi has 0.5, so it never sounds. The latch is global to all semis, and the player's air brake plays at the reverse volume | MM2 (`vehSemiCarAudio::UpdateReverse`, `UpdateAirBlow`, `SetNon3DParams`) |
| Opponents and police use the same surface, suspension and tyre wobble tables as the player (`aud/cardata/player`), impacts from `aud/cardata/opponent` | MM2 (`vehCarAudio::Init`) |

## Surfaces, skids, suspension, wobble

| Behaviour | Evidence |
|---|---|
| Tables: `default_surfacewet.csv` in rain, `default_surfacedry.csv` otherwise (snow included); `default_surfaceice.csv` is never loaded | MM2 (`vehCarAudio::Init`; weather 3 = rain) |
| Surface index = the wheel material's `sound:` value (city/materials.mtl: road 0, water 1, grass 2; cobblestone is 0) | MM2 (`vehWheel::GetSurfaceSound`, `lvlMaterial::Load`) |
| The surface follows the front left wheel unless either front wheel is still on the current one; in tunnels the table's tunnel entry | MM2 (`vehSurfaceAudio::SurfaceChanged`, `UpdateSurface`, echo flag) |
| Rolling sound: speed > 2 m/s, two or more wheels down, no skid sample of the current surface playing; volume and pitch linear in speed up to max speed | MM2 (`UpdateSurface`, `vehSurfaceAudioData::UpdateSurface`) |
| Skid: the largest wheel skid amount; any value above 0 counts (no speed threshold); every skid sample whose range holds it plays (bounds inclusive, so two at a boundary), the rest stop; volume = min + slip × (max - min) | MM2 (`UpdateSkid`, `vehSurfaceAudioData::UpdateSkid`) |
| Suspension thump: the average compression speed of all four wheels ≥ &#124;min velocity&#124;, two wheels down, not already playing; volume = clamp(speed / divisor, min, max) | MM2 (`UpdateSuspension`, `LoadSuspension`) |
| Tyre wobble: once damage is more than 5% past MedDamage, one thump per revolution of the rear left wheel; volume = clamp(damage fraction, min, max), pitch = clamp(speed / divisor, min, max) (player only; positioned cars keep their pitch) | MM2 (`UpdateTireWobble`, `LoadTireWobble`) |
| Airborne (the music's "big air"): no wheel down and ground 3–33 m below; cleared when a wheel lands (player only) | MM2 (`vehSurfaceAudio::UpdateAir`, `vehCarAudio::IsAirBorne`) |

## Impacts

| Behaviour | Evidence |
|---|---|
| Banger entries are indexed by their order in `default_impacts.csv`, not by the ID column; out of range (and "no banger", 1000) is WALL | MM2 (`AudImpact::GetAudImpactDataPtr`, `ReadCSV`; `vehCarDamage::ApplyImpact`) |
| Strength = &#124;x&#124; + &#124;y&#124; + &#124;z&#124; of the impulse | MM2 (`vehCarDamage::ApplyImpact`, `aiVehicleActive`) |
| Every sample whose force range holds the strength (inclusive) and is not playing plays; volume = min + force × (max - min) / (max force - min force) — the whole force, so hard hits can exceed the max volume | MM2 (`AudImpactData::Play`, `PlaySample`) |
| Positioned cars: the last banger's playing samples follow the car's attenuation and pan | MM2 (`AudImpact::UpdateAttenuation`) |

## Police sirens

| Behaviour | Evidence |
|---|---|
| Table: `aud/cardata/player/<city>policesiren.csv` for the player and AI police alike; `opponent/policesiren.csv` is unused; the explosion plays at volume 1 (the table's 0.95 is not read) | MM2 (`mmGame` / `vehCarAudioContainer::SetSirenCSVName`, `vehPoliceCarAudio::Load`) |
| Sample i loops until its current (play time, next) entry runs out, then the next sample starts and sample i's entry index advances, wrapping: a fixed sequence, not random | MM2 (`FluctuateSiren`) |
| Positioned: volume = max(table volume × attenuation, 0.75), so a siren is heard at -25 dB anywhere; frequency = doppler; the engine stops while the siren is on | MM2 (`UpdateSiren`, `StartSiren`, `vehPoliceCarAudio::UpdateAudio3D`) |
| A cop destroyed in pursuit: explosion (volume max(attenuation, 0.85) while the siren runs), the siren drops to half pitch for 1 s, 0.45 to 1.75 s, then halves in volume each update and stops — but `StopSiren` follows at once, so in practice the siren just stops | MM2 (`PlayExplosion`, `DamageSiren`, `UpdateExplosion`, `aiPoliceOfficer::PerpEscapes`) |
| Cops pursuing the player are counted (for the cop chase music); an exploding cop is subtracted twice; constructing a police car audio resets the count | MM2 (`s_iNumCopsPursuingPlayer`) |

## Ambient traffic

| Behaviour | Evidence |
|---|---|
| Engine: speed bands (inclusive) except the last while holding or gaining speed; the last band (0..500 in every file) while slowing; a drop of more than 4 m/s per update (a crash) decays the pitch from the previous speed by 5% per update; no band → pitch unchanged; the pitch applies one update late | MM2 (`aiAmbientVehicleAudio::UpdateAudio`, `aiEngineAudio::CalculatePitch`, `UpdateDoppler`) |
| Horn on a near miss: a random pattern, but half the time none; not while a pattern sounds | MM2 (`vehHornAudio::PlayAvoidance`, called by `aiGoalAvoidPlayer`) |
| Horn after a hit ≥ "min stuck horn impact force": the last pattern (a 3 s blast) one time in four | MM2 (`vehHornAudio::PlayImpact`, `DAT_005cfbc8` = 7.5 of 10) |
| Patterns: (play, pause) pairs; a play time of 0 sounds for one update | MM2 (`vehHornAudioTiming::Update`) |
| Engine/horn files `aud/cardata/ambient/<type>_engine.csv`, plural model names mapped to the singular files (`va_sedans_s` → `va_sedan_s`), else `default_*` | file lookup MM2 (`aiEngineAudio::Load`, `vehHornAudio::Load`); the name mapping **inferred** |

## City ambience (`<city>ambientcontainer.csv`)

| Behaviour | Evidence |
|---|---|
| Each listed set is one positioned object; with VECTORPOINTS it sounds from the point with the smallest &#124;dx&#124; + &#124;dy&#124; + &#124;dz&#124; to the listener, chosen while it has no slot | MM2 (`Aud3DAmbObjContainer::Init`, `Aud3DObject::SetClosestPositionPtr`) |
| Audible area: 0 everywhere, 1 only underground (tube voices), 2 only above ground (buoy seals) | MM2 (`Aud3DAmbientObject::Update`, `UpdateAudio`) |
| Type 0 positional loop; 1 one-shot every [low, high] s at a random volume (0.75–1 × table) and random pan, not attenuated; 2 positional one-shot every [low, high] s; 3 positional on request. One-shot timers start at 0 (the first plays at once); low = high uses that value | MM2 (`UpdateSoundData`, `UpdateLoop`, `UpdateOneShot`, `PendOneShot`, `PlayOneShot`) |
| "min/max speed" filter the attached object's speed (0 for the container's sets) | MM2 (`Aud3DAmbientObject::Update`) |
| The container (and the DirectMusic ambient segment) is only loaded when the "Ambient" option is on, which the audio options make exclusive with music; OpenMM2 plays both | MM2 (`mmPlayer::Init`, `AudioOptions::ToggleMusic` / `ToggleAmbient`); deliberate difference |
| `playAt` / `setLoop` for bridges, subway, ferry and pedestrians approximate `mmBridgeAudio`, `aiSubwayAudio`, `aiCableCarAudio` (their owners are not wired up yet) | **inferred** |
| `mmAmbientAudio` (a "walla" loop) is never constructed in MM2 | MM2 (no caller of the constructor); not ported |

## Rain

| Behaviour | Evidence |
|---|---|
| Only in rain (weather 3); exterior loop, or interior while the camera is in the car; starts at 0.82 / 0.85; after a shelter (tunnel) 0.76 / 0.83; sheltered 0.65 / 0 | MM2 (`mmRainAudio::mmRainAudio`, `ShelterOn`, `ShelterOff`, `SetInterior`) |
| Thunder only at night: flash at 13 s, a clap at 15 s (pan -0.2), a second clap 1 s later (pan 0.2, 0.8 pitch), then the cycle restarts. Sheltered: 0.85 and centred; `ShelterOff`'s ±20 pans are rejected, so after the first tunnel the claps stay centred | MM2 (`mmRainAudio::Update`, `ShelterOn` / `Off`) |

## Announcer

| Behaviour | Evidence |
|---|---|
| Announcer: uniform from 1 to "Num announcers"; in SF a 3 becomes 4 (as3 does not exist, so as4 is twice as likely) | MM2 (`mmRaceSpeech::LoadCityInfo`) |
| Per race: the mode's table; weather pre-race lines for clear, cloudy, fog, rain (none in snow); time of day for morning, noon, night, and evening only in clear weather; the car's pre-race and results lines; final lap; cruise or race damage | MM2 (`mmSpeechContainer::InitRace`, `mmPlayer::InitSpeechAudio`) |
| Table rows: a header sets where its event's lines start; PRERACE / UNLOCKRACE / UNLOCKVEHICLE take every following row as a range, every other event only the first row | MM2 (`mmRaceSpeech::LoadGroup`, `SetReadState`) |
| Line in a row: NN uniform in (add, end]; the number used last in that row moves up one, wrapping to 1 | MM2 (`AudSpeechData::GetRandomName`) |
| Pre-race: r uniform in 0..11.5; ≤ 7.5 a random row of the mode's pre-race range, ≤ 8.5 time of day, ≤ 9.5 weather (nothing if that table was not loaded), else the car's line (else the mode's); it starts 1.5 s later | MM2 (`mmRaceSpeech::PlayPreRace`) |
| Results: 1st win; a place above half the field mid; else poor. Win/mid use the car's line when r > 5 of 10, poor when r > 8; without a "mid" table (blitz) nothing is said | MM2 (`PlayResults`, `PlayResultsWin` / `Mid` / `Poor`) |
| Final checkpoint, final lap, damage penalty, race progress and results cut off the line playing; unlock lines wait 0.1 s behind it; one queued line at a time | MM2 (`AudSpeech::Play`, `PutInQueue`, `Update`, `Stop`) |
| File names `<announcer><prefix>NN` | verified against the sample names |
| Cops & Robbers: cut the line, start after 0.01 s; a random row of the event. MM2's grouping (a fourth "num used" column choosing a random window) is not ported | delays MM2 (`mmCNRSpeech::Play`); grouping **inferred** |
| Crash Course: pre-race after 1.5 s, results at once; the first row of the event | delays MM2 (`mmCCSpeech`); rows **inferred**; checkpoint lines (`cc_cpoint_*info.csv`) not implemented |

## Creature voices

| Behaviour | Evidence |
|---|---|
| Lines are said only when the AI reports a near miss (ambient car honking at the player, pedestrian diving); each eligible block queues one of its lines half of the time, avoiding the line any creature said last; a queued line plays within 50 m, or is dropped after 5 s | MM2 (`AudCreature::PlayAvoidance`, `AudCreatureAvoid::QueuePlay`, `Update`, `Play`) |
| A block is eligible unless its speed was in range for less than "min time in range" while out of it for more than "max time out of range"; the speed is the player's | MM2 (`AudCreatureAvoid::IsEligible`, `InSpeedRange`; `aiMap` passes the player's speed) |
| Impact lines: a hit at least "min impact force", the line after its delay, at most once a minute across all creatures (the clock starts at 0, so none in the first minute) | MM2 (`AudCreatureImpact::QueuePlay`, `Update`, `UpdateStatics`; `DAT_005d24c4` = 60) |

## Not implemented

* The tunnel echo (`EchoOn`: per-sample delay, 0.96 attenuation; music echo)
  and EAX: the mixer has no effects yet. The tunnel flag itself (surface
  table entry, rain shelter, ambience areas) is an input the app does not set
  yet.
* `vehNitroCarAudio` (only used if vehtypes.csv says "Always nitro").
* Crash Course checkpoint location lines (`cc_cpoint_*info.csv`).

## Data quirks

* `skidflagstone` is referenced by the surface tables but the file is
  `skidflagstone1`; like the original's loader, OpenMM2 leaves it silent.
* `engineparamsplay.csv` / `engineparamsopp.csv` have binary garbage in their
  name column (tuning leftovers) and are not used.
* No `racelaps01NN` lines exist for the final lap table's `RACELAPS01,10,8`.
* Several engines (fire truck, 4x4) exceed full scale when their samples
  overlap at high table volume, more so now that samples keep their minimum
  volume outside their fades; DirectSound's mixer would have saturated too.

## Tools

`mm2tool carsound <game-source> <car> [--wav out.wav] [--play]` renders an
800 → max → 800 RPM sweep, a skid and a hard impact through the mixer and
prints per-second peak / RMS levels.
