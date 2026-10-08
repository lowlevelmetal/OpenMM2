# In-game sound effects

`src/audio/game` plays the car, city and voice sounds. MM2 drives them from
CSV tables in `MM2AUD.AR`; the code ports MM2's own audio classes (from the
build 3393 reference) and reads those tables. "MM2 (`Class::Method`)" below
names the original function a behaviour was taken from; "inferred" marks what
is still OpenMM2's reading and should be compared with the original game.
The function-by-function audit is `docs/parity/audio.md`.

Music (DirectMusic) is separate: see `docs/music.md`.

## Volume, pan and pitch units

Every volume in the tables, and every volume the code computes, is an Angel
volume `v` in 0..1. `audSound::SetVolume` passes `(v - 1) * 10000` to
DirectSound, so `v` is linear in decibels: 1 = 0 dB, 0.9 = -10 dB, 0.25 = -75 dB
(MM2 `audSound::SetVolume`; `ageVolumeToGain`, `audio/AngelUnits.h`). This is
why the tables use values such as 0.91 / 0.93.

The SOUND FX slider `s` becomes a master volume `log(200 s) / log(200)`
(`AudManager::AssignWaveVolume`), which `audObject::SetVolume` multiplies into
every sound's Angel volume before clamping the product to 0..1: a half slider
is -13 dB on a sound at full volume. The MUSIC slider maps the same way onto
the DirectMusic buffer (`DMusicWaveBuffer::SetVolume`). The mixer applies this:
game sounds are "Angel voices" whose bus volume is that slider
(`Mixer::setBusVolume`); wave sounds (effects, engines, the city's wave
ambience, rain, creature voices, commentary) follow SOUND FX, the DirectMusic
streams (soundtrack, city ambience segment) MUSIC.

Pans are Angel pans in -1..1. `audSound::SetPan` passes `pan * 10000` to
DirectSound, which attenuates the far channel by `|pan| * 100` dB and leaves
the near one at full level. The mixer attenuates the far channel linearly, so
`agePanToMixer` converts: a pan of 0.2 puts the far channel at -20 dB (MM2
`audSound::SetPan`). `audObject::SetPan` clamps pans to -1..1 first (this
matters for the rain's thunder, below). With STEREO FX off (`IsStereo`
false) MM2 skips its pan calls, so every sound stays centred
(`Mixer::setStereo(false)`).

Pitch is a multiple of the sample rate. `AudSoundBase::SetFrequency` goes
through `audObject::SetPitch`, which clamps the multiplier to 0..2, and
`audSound::SetPitch` then clamps the rate to 100..100000 Hz (an ambient engine
whose speed band asks for 2.2 plays at 2). `PlayLoop` / `PlayOnce` take a
volume and pitch of -1 ("leave as is"), which is what MM2 always passes; a
buffer already playing is not restarted (`audSound::Play`), and `Stop` rewinds.

At most 32 wave sounds play at once (`AudManager::Init`:
`SetMaxConcurrent(1, 32)`); a new one stops the oldest of no higher priority
(`audManager::MoveToActive`), and all game samples share one priority. The
SOUND QUALITY option's channel count goes to `AudManager::SetNumChannels`,
which does nothing, and MM2 always loads the 22 kHz files (speech only exists
at 11 kHz).

The random numbers of the audio code (horn patterns, one-shot intervals,
announcer lines, creature lines, the song) come from
`AudManagerBase::RandomizeNumber` / `mmGameMusicData::RandomizeNumber`: each
call seeds a fresh Knuth subtractive generator (Angel's `Random`) with
`time(NULL)` and takes its first number, so every draw in the same second is
the same value (`audio/AngelRandom.h`).

## Tables

Every table is read the way MM2's loader reads it (`audio/TextFields.h`):
`fgets` lines cut at the first CR or LF, `strtok` cells (an empty cell is
skipped, so the cells after it shift) and `atof` / `atoi` (a numeric prefix is
read, anything else is 0). The line structure follows each loader: for
example every line after the engine header is an engine sample, every line
after a surface block's skid header up to the block's count is a skid
sample, and `default_impacts.csv` must end with an `ENDOFDATA` block or MM2
drops the whole table (`AudImpact::ReadCSV`).

## Positioned sounds (`Object3D`)

MM2 does not use DirectSound3D for game sounds. Every positioned object (other
cars, police, ambient traffic, city emitters, creatures) plays 2D buffers whose
volume, pan and frequency it computes itself, and only a few objects sound at
a time. Ported from MM2 `Aud3DObject` and `Aud3DObjectManager`:

| Behaviour | Evidence |
|---|---|
| Attenuation = `1 - (d² - min²) / (max² - min²)`, a volume multiplier (so linear in dB against the squared distance); full inside the minimum distance | MM2 (`Aud3DObject::CalcPercentToMaxDist2`, `CalculateAttenuation`) |
| Pan = 0.2 × x / (&#124;dx&#124; + &#124;dy&#124; + &#124;dz&#124;), x in listener space (the camera); 0 inside the minimum distance | MM2 (`CalcSinglePlayerPan`) |
| Doppler = `1 + (pseudo distance closed since the last update) * factor * dt`, factor `1 / 56.7166` (`2 / 56.7166` for ambient traffic). The `* dt` makes the shift tiny; ported as is | MM2 (`CalculateDoppler`; `vehCarAudio` / `aiAmbientVehicleAudio` statics) |
| The distance is measured by `WithinMaxDistance` and again by `PastMaxDistance`, so on the update an object takes a slot the doppler shift is 0 | MM2 (both call `CalcDistToClosestHeads2`) |
| An object starts sounding within its maximum distance and stops past it; a police car with its siren on keeps its slot and its last attenuation at any distance | MM2 (`WithinMaxDistance`, `PastMaxDistance`, flag +0x49 set by `vehPoliceCarAudio::StartSiren`) |
| Slots: four in single player, one held for good by the player's car (non-positioned objects get +1000000 priority), so three for everything else. A newcomer takes the slot of the farthest object of no higher priority if its priority is higher, or equal and it is closer | MM2 (`mmPlayer::Init` creates `Aud3DObjectManager(4)`; `Add`, `FindGreatestDistance`, `GetPriority`) |
| Priorities: cars 9, police 7 (10 with the siren on), ambient traffic 8, city emitters from their file (12 in every retail file, so they win over cars) | MM2 (`vehCarAudio::Init`, `vehPoliceCarAudio::Init` / `StartSiren` / `StopSiren`, `aiAmbientVehicleAudio::Init`, `Aud3DAmbientObject::Load`) |
| Drop-offs: cars and police 0..150 m, ambient traffic 0..100 m, pedestrians 0..40 m, city emitters from their file | MM2 (`SetDropOffs` callers) |
| Losing a slot stops the loops (engine, surface, skids, horn, siren, emitters); impacts, thumps and an explosion play out | MM2 (`UnAssignSounds` of each class) |
| `vehCarAudio`'s 25 m "amplification" multiplies by 1 + 0.01 × a speed field nothing sets, i.e. by 1 | MM2 (`vehCarAudio::UpdateAudio3D`); not ported |

## Inputs from the game (`CarAudioInputs`)

| Field | Meaning |
|---|---|
| `rpm` | the engine's current RPM (vehCarSim +0x2c4), as is |
| `speed` | m/s, `vehCarSim` speed |
| `gear` | -1 reverse, 0 neutral, 1.. forward (MM2's gear index is this + 1) |
| `wheels[4]` | `onGround`; `slip`, vehWheel's skid amount (MM2 +0x220); `surface`, the material's `sound:` value; `suspensionSpeed` (+0x204); `brakeCoef` (tuning) |
| `impacts` | `force` = &#124;z&#124; + &#124;y&#124; + &#124;x&#124; of the impulse vector (`impactStrength`), `audioId` = impact table index (-1: no sound) |
| `horn`, `siren`, `sirenPursuingPlayer`, `wrecked` | buttons and AI state |
| `tireWobble`, `wheelRadius` | (damage - MedDamage) / (MaxDamage - MedDamage); rear left wheel radius |
| `groundBelow` | player only: distance to the first surface 3 to 33 m below, for the airborne flag |
| `transform`, `velocity` | positioned cars |
| `inTunnel` | the listener's tunnel echo state (the app does not detect tunnels yet) |

## Cars

| Behaviour | Evidence |
|---|---|
| Sound class by `shared/vehtypes.csv`: semi or bus (vpcentury, vpbus, vpddbus) → semi sounds; police (vpcop, vpsemi, vpeagle) → siren; "Always nitro" (FALSE in retail) would give every other car a nitro sound. Names compare exactly. The car table's "flags" and "Num Engine Samples" columns are read and discarded; a car without a table uses `default.csv` | MM2 (`vehCarAudioContainer::RegisterTypes`, constructor; `vehCarAudio::Init` / `Load`) |
| Engine row volume: min volume at or below the fade-in start and at or past the fade-out end (so a sample keeps playing at its minimum volume, e.g. -45 dB for 0.55), linear up to max over the fade-in, max between, linear down over the fade-out | MM2 (`vehEngineSampleWrapper::CalculateVolume`) |
| Engine row pitch: min up to the shift start, max from the shift end, `min + rpm * (max - min) / (end - start)` between — the slope multiplies the whole RPM, so the pitch jumps at the start of the range (and past max pitch just before its end) unless the range starts at 0 | MM2 (`CalculatePitch`, `ParseCSVBuffer`) |
| A sample stops while its table volume is below 0.25; positioned cars then scale volume by attenuation and pitch by doppler | MM2 (`vehEngineSampleWrapper::UpdateRPM`) |
| Engine silenced after a damage out: min and max volume become 0 but the fade slopes keep their table values, so a sample in a fade can still be heard (up to max - min) | MM2 (`vehEngineSampleWrapper::Silence`, `vehEngineAudio::Silence`) |
| Clutch sample whenever the gear changes into or out of reverse (including the first update in reverse); positioned cars have none | MM2 (`vehCarAudio::UpdateGear`, `Load`) |
| Horn: loops while held, from the start; positioned cars only have one when they are network players (container mode 0), AI cars (mode 1) none | MM2 (`vehCarAudio::PlayHorn` / `StopHorn`, `vehCarAudioContainer::Init`) |
| A car with siren lights (every police-list car: vpcop, vpsemi) toggles its siren with the horn button and never plays its horn | MM2 (`mmGame::UpdateHorn`, `vehCarAudioContainer::PlayHorn`) |
| Semi reverse beeper loops in reverse. The air brake hisses once when the truck is stopped (≤ 0.04 m/s) and "braking" — which `vehSurfaceAudio::IsBrakeing` reads as both front wheels' BrakeCoef > 0.5, a tuning value. Every retail semi has 0.5, so it never sounds. The latch is global to all semis, and the player's air brake plays at the reverse volume. Positioned semis (opponent buses) have both too, at their own volumes times attenuation and the doppler frequency | MM2 (`vehSemiCarAudio::UpdateReverse`, `UpdateAirBlow`, `SetNon3DParams`, `UpdateAudio3D`) |
| Opponents and police use the same surface, suspension and tyre wobble tables as the player (`aud/cardata/player`), impacts from `aud/cardata/opponent` | MM2 (`vehCarAudio::Init`) |

## Surfaces, skids, suspension, wobble

| Behaviour | Evidence |
|---|---|
| Tables: `default_surfacewet.csv` in rain, `default_surfacedry.csv` otherwise (snow included); `default_surfaceice.csv` is never loaded | MM2 (`vehCarAudio::Init`; weather 3 = rain) |
| Surface index = the wheel material's `sound:` value (city/materials.mtl: road 0, water 1, grass 2; cobblestone is 0; -1 counts as 0). `NOSOUND` (exact case) has no sample | MM2 (`vehWheel::GetSurfaceSound`, `vehSurfaceAudioData::ParseCSVBuffer`) |
| The surface follows the front left wheel unless either front wheel is still on the current one; in tunnels the table's tunnel entry | MM2 (`vehSurfaceAudio::SurfaceChanged`, `UpdateSurface`, echo flag) |
| Rolling sound: speed > 2 m/s, two or more wheels down, no skid sample of the current surface playing; volume and pitch linear in speed up to max speed | MM2 (`UpdateSurface`, `vehSurfaceAudioData::UpdateSurface`) |
| Skid: the largest wheel skid amount; any value above 0 counts (no speed threshold); every skid sample whose range holds it plays (bounds inclusive, so two at a boundary), the rest stop; volume = min + slip × (max - min) | MM2 (`UpdateSkid`, `vehSurfaceAudioData::UpdateSkid`) |
| Suspension thump: the average compression speed of all four wheels ≥ &#124;min velocity&#124;, two wheels down, not already playing; volume = speed / divisor clamped "low first" to min, then max | MM2 (`UpdateSuspension`, `LoadSuspension`) |
| Tyre wobble: once damage is more than 5% past MedDamage, one thump per revolution of the rear left wheel (2π = 6.28318); volume = damage fraction clamped to min / max, pitch = speed / divisor clamped (player only; positioned cars keep their pitch) | MM2 (`UpdateTireWobble`, `LoadTireWobble`, `SetWheelPointers`) |
| Airborne (the music's "big air"): no wheel down and a surface 3–33 m below (ten 3 m probe segments from 3 m down); cleared when a wheel lands (player only) | MM2 (`vehSurfaceAudio::UpdateAir`, `vehCarAudio::IsAirBorne`) |

## Impacts

| Behaviour | Evidence |
|---|---|
| Banger entries are indexed by their order in `default_impacts.csv`, not by the ID column; out of range (and "no banger", 1000) is WALL; -1 plays nothing | MM2 (`AudImpact::Play`, `GetAudImpactDataPtr`, `ReadCSV`; `vehCarDamage::ApplyImpact`) |
| Strength = &#124;z&#124; + &#124;y&#124; + &#124;x&#124; of the impulse | MM2 (`vehCarDamage::ApplyImpact`, `aiVehicleActive`) |
| Every sample whose force range holds the strength (inclusive) and is not playing plays; volume = min + force × (max - min) / (max force - min force) — the whole force, so hard hits can exceed the max volume; the frequency column is set once | MM2 (`AudImpactData::Play`, `PlaySample`, `AssignSounds`) |
| Positioned cars: the last banger's playing samples follow the car's attenuation and pan | MM2 (`AudImpact::UpdateAttenuation`) |

## Police sirens

| Behaviour | Evidence |
|---|---|
| Table: `aud/cardata/player/londonpolicesiren.csv` in London, `sfpolicesiren.csv` in every other city, for the player and AI police alike; `opponent/policesiren.csv` is unused; the explosion plays at volume 1 (the table's 0.95 is not read) | MM2 (`mmGame` / `vehCarAudioContainer::SetSirenCSVName`, `vehPoliceCarAudio::Load`) |
| Sample i loops until its current (play time, next) entry runs out, then the next sample starts and sample i's entry index advances, wrapping: a fixed sequence, not random | MM2 (`FluctuateSiren`) |
| Positioned: volume = max(table volume × attenuation, 0.75), so a siren is heard at -25 dB anywhere; frequency = doppler; the engine stops while the siren is on | MM2 (`UpdateSiren`, `StartSiren`, `vehPoliceCarAudio::UpdateAudio3D`) |
| A cop destroyed in pursuit: explosion at the car's attenuation; the siren drops to half pitch (`DamageSiren`) — but `StopSiren` follows at once, so in practice the siren just stops | MM2 (`PlayExplosion`, `DamageSiren`, `aiPoliceOfficer::PerpEscapes`) |
| Cops pursuing the player are counted (for the cop chase music); a cop chasing someone else is not; an exploding cop is subtracted twice; constructing a police car audio resets the count | MM2 (`s_iNumCopsPursuingPlayer`, `aiPoliceOfficer::StartSiren` passes `IsPlayer`) |

## Ambient traffic

| Behaviour | Evidence |
|---|---|
| Engine: speed bands (inclusive) except the last while holding or gaining speed; the last band (0..500 in every file) while slowing; a drop of more than 4 m/s per update (a crash) decays the pitch from the previous speed by 5% per update; no band → pitch unchanged; the pitch applies one update late | MM2 (`aiAmbientVehicleAudio::UpdateAudio`, `aiEngineAudio::CalculatePitch`, `UpdateDoppler`) |
| Horn on a near miss: RandomizeNumber(2 × last − 0.01) picks one of the patterns before the last (about half the time none); never while a pattern sounds | MM2 (`vehHornAudio::PlayAvoidance`, called by `aiGoalAvoidPlayer`) |
| Horn after a hit ≥ "min stuck horn impact force": the last pattern (a 3 s blast) one time in four | MM2 (`vehHornAudio::PlayImpact`, a constant 7.5 of 10) |
| Patterns: (play, pause) pairs; a play time of 0 sounds for one update; the horn's volume and frequency follow the car only while a pattern runs | MM2 (`vehHornAudioTiming::Update`, `vehHornAudio::UpdateDoppler`) |
| Engine/horn files `aud/cardata/ambient/<model>_engine.csv` / `_horn.csv`, else `default_*`. The sedan model is `va_sedans_s`, so the `va_sedan_s_*` files are never used | MM2 (`aiAmbientVehicleAudio::Init`, `aiEngineAudio::Load`, `vehHornAudio::Load`) |

## City ambience (`<city>ambientcontainer.csv`)

| Behaviour | Evidence |
|---|---|
| Each listed set is one positioned object; with VECTORPOINTS it sounds from the point with the smallest &#124;dx&#124; + &#124;dy&#124; + &#124;dz&#124; to the listener, chosen while it has no slot | MM2 (`Aud3DAmbObjContainer::Init`, `Aud3DObject::SetClosestPositionPtr`) |
| Audible area: 0 everywhere, 1 only underground (tube voices), 2 only above ground (buoy seals) | MM2 (`Aud3DAmbientObject::Update`, `UpdateAudio`) |
| Type 0 positional loop; 1 one-shot every [low, high] s at a random volume (0.75–1 × table) and random pan, not attenuated (volume and pan from the same draw, so they move together); 2 positional one-shot every [low, high] s; 3 positional on request. One-shot timers start at 0 (the first plays at once); low = high uses that value | MM2 (`UpdateSoundData`, `UpdateLoop`, `UpdateOneShot`, `PendOneShot`, `PlayOneShot`) |
| "min/max speed" filter the attached object's speed (0 for the container's sets) | MM2 (`Aud3DAmbientObject::Update`) |
| The container (and the DirectMusic ambient segment) is only loaded when the "Ambient" option is on, which the audio options make exclusive with music; OpenMM2 plays both | MM2 (`mmPlayer::Init`, `AudioOptions::ToggleMusic` / `ToggleAmbient`); deliberate difference |
| `playAt` / `setLoop` for bridges, subway, ferry and pedestrians approximate `mmBridgeAudio`, `aiSubwayAudio`, `aiCableCarAudio`, `aiPedAudio`, which are each a full ambient object with a slot (their owners are not wired up yet) | **open** |
| `mmAmbientAudio` (a "walla" loop) is never constructed in MM2 | MM2 (no caller of the constructor); not ported |

## Rain

| Behaviour | Evidence |
|---|---|
| Only in rain (weather 3); exterior loop, or interior while the camera is a point-of-view camera (the dash view, car view 1, or the bumper view); starts at 0.82 / 0.85; after a shelter (tunnel) 0.76 / 0.83; sheltered 0.65 / 0 | MM2 (`mmRainAudio::mmRainAudio`, `ShelterOn`, `ShelterOff`, `SetInterior`, `mmPlayer::SetCamera`) |
| Thunder only at night: a clap at 15 s (pan -0.2), a second clap 1 s later (pan 0.2, 0.8 pitch), then the cycle restarts. Sheltered: 0.85 and centred; `ShelterOff`'s ±20 pans are clamped, so after the first tunnel the claps are hard left and hard right. A "flash" state set at 13 s is read by nothing: no lightning is drawn | MM2 (`mmRainAudio::Update`, `ShelterOn` / `Off`, `audObject::SetPan`) |

## Announcer

| Behaviour | Evidence |
|---|---|
| Only SF and London have announcers; one is drawn for every race (RandomizeNumber(1, count + 0.99)); in SF a 3 becomes 4 (as3 does not exist, so as4 is twice as likely) | MM2 (`mmSpeechContainer::InitRace`, `mmRaceSpeech::LoadCityInfo`) |
| Per race: the mode's table; weather pre-race lines for clear, cloudy, fog, rain (none in snow); time of day for morning, noon, night, and evening only in clear weather; the car's pre-race and results lines; final lap; cruise or race damage | MM2 (`mmSpeechContainer::InitRace`, `mmPlayer::InitSpeechAudio`) |
| Table rows: a header (a first cell ending in "header") sets where its event's lines start, the event names matching as prefixes; PRERACE / UNLOCKRACE / UNLOCKVEHICLE take every following row as a range, every other event only the first row; an unknown header keeps the state | MM2 (`mmRaceSpeech::LoadGroup`, `SetReadState`) |
| Line in a row: NN = RandomizeNumber(add + 1, end + 0.99); the number used last in that row moves up one, wrapping to 1; the number is drawn when the line starts | MM2 (`AudSpeechData::GetRandomName`, `AudSpeech::PlayStream`) |
| Pre-race: r = RandomizeNumber(11.5); ≤ 7.5 a random row of the mode's pre-race range, ≤ 8.5 time of day, ≤ 9.5 weather (nothing if that table was not loaded), else the car's line (else the mode's); it starts 1.5 s later | MM2 (`mmRaceSpeech::PlayPreRace`) |
| Results: 1st win; a place above half the field mid; else poor. Win/mid use the car's line when r > 5 of 10, poor when r > 8; without a "mid" table (blitz) nothing is said | MM2 (`PlayResults`, `PlayResultsWin` / `Mid` / `Poor`) |
| Final checkpoint, final lap, damage penalty, race progress, texture unlock and results stop the line playing and empty the queue; unlock-race and unlock-vehicle lines wait 0.1 s in the queue. The queue starts with one slot and gains one for every unlock-vehicle line loaded (and every unlock header after the first in a table); a play that finds no free slot is dropped; the first due play starts once nothing is playing | MM2 (`AudSpeech::Play`, `PutInQueue`, `Update`, `Stop`, `AllocateQueuePlayData`) |
| File names `<announcer><prefix>NN` | verified against the sample names |
| Cops & Robbers: `bullshit.csv` in SF, `cnrlondon.csv` elsewhere; an event draws one of its rows, cuts the line playing and starts after 0.01 s; a fourth "num used" column smaller than the row's range picks a random window of that many lines. The London rows' names end in a space ("AL1\AL1ROBROB "), which MM2 keeps, so those files are never found and London's Cops & Robbers commentary is silent | MM2 (`mmSpeechContainer::InitCNR`, `mmCNRSpeech::LoadGroup`, `SetReadState`, `Play`); the missing files **inferred** from `AudStream::PlayOnce`'s path |
| Crash Course: `ccl<lesson>` in London, `ccs<lesson>` elsewhere; the intro after 1.5 s and checkpoint 0's location line after 1.51 s; results at once; lesson 4 adds the checkpoint location lines (`cc_cpoint_waveinfo`, `cc_cpoint_indexinfo`: a fixed line number per checkpoint) | MM2 (`mmSpeechContainer::InitCC`, `mmCCSpeech`) |

## Creature voices

| Behaviour | Evidence |
|---|---|
| Lines are said only when the AI reports a near miss (an ambient car honking at the player, a pedestrian diving); each eligible block queues one of its lines half of the time, avoiding the line any creature said last (that shared "last line" starts at 0); a queued line plays within 50 m, or is dropped after 5 s | MM2 (`AudCreature::PlayAvoidance`, `AudCreatureAvoid::QueuePlay`, `Update`, `Play`) |
| A block is eligible unless its speed was in range for less than "min time in range" while out of it for more than "max time out of range"; the speed is the player's | MM2 (`AudCreatureAvoid::IsEligible`, `InSpeedRange`; `aiMap` passes the player's speed) |
| Impact lines: a hit at least "min impact force", the line after its delay, at most once a minute across all creatures (the clock starts at 0 when the game starts and is never reset) | MM2 (`AudCreatureImpact::QueuePlay`, `Update`, `UpdateStatics`; a constant 60 s) |

## Not implemented

* The tunnel echo (`Aud3DObjectManager::EchoOn(0.5)`: every positioned sound
  plays a delayed copy at 0.96 volume, the horn 0.05 s late at 0.997 pitch;
  `MMDMusicManager::EchoOn` for the music) and EAX. The tunnel flag itself
  (camera in a room flagged underground; surface tunnel entry, rain shelter,
  ambience areas, the music's ambience pause) is an input the app does not
  set yet.
* `vehNitroCarAudio` (only used if vehtypes.csv says "Always nitro").
* The old "Volume Divisor" engine table layout (`ParseCSVBufferOld`); no car
  table MM2 loads uses it.

## Data quirks

* `skidflagstone` is referenced by the surface tables but the file is
  `skidflagstone1`; like the original's loader, OpenMM2 leaves it silent.
* `engineparamsplay.csv` / `engineparamsopp.csv` have binary garbage in their
  name column (tuning leftovers) and are not used.
* No `racelaps01NN` lines exist for the final lap table's `RACELAPS01,10,8`.
* Several engines (fire truck, 4x4) exceed full scale when their samples
  overlap at high table volume, more so now that samples keep their minimum
  volume outside their fades; DirectSound's mixer would have saturated too.
* Four effects exist only at 11 kHz (`austinhigh.`, `mustangstart`,
  `truckstart`, `uireplay`); MM2 would not find them, OpenMM2 falls back to
  the 11 kHz file.

## Tools

`mm2tool carsound <game-source> <car> [--wav out.wav] [--play]` renders an
800 → max → 800 RPM sweep, a skid and a hard impact through the mixer and
prints per-second peak / RMS levels.
