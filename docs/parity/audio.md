# Parity audit: audio

Audited against MM2Recomp (midtown2.exe build 3393) on 2026-10-07; second
pass (tunnel echo, world-owned ambient objects, cable cars, pedestrian voices,
the race-side hooks) on 2026-10-08.

Summary (after the second pass): 265 table rows (a row may cover several
related functions); verified 69, fixed 153, deviation 11, inferred 10,
open 2, openmm2 20. The open rows are the police explosion repeat (waiting on
the ai-vehicles audit) and the ambient traffic, which no traffic car drives
yet.

Scope: `src/audio/game/*` (P, including `PedAudio.*` from the second pass), `src/audio/Mixer.*`, `Music*`,
`MusicDirector.*`, `MusicMotif.*` (M), `src/audio/SoundBank.*`, `Wav.*` (F),
plus the files added by the audit: `src/audio/EchoEffect.*` (the tunnel
echo's duplicate buffer), `src/audio/AngelRandom.*` (MM2's random numbers), `AngelUnits.*` (volume / pan units, moved out of
`game/AudioTables`) and `TextFields.*` (MM2's fgets / strtok / atof reading).
Behaviour is described in `docs/audio.md` and `docs/music.md`.

Cross-cutting findings that changed many rows:

* **Random numbers.** `AudManagerBase::RandomizeNumber` and
  `mmGameMusicData::RandomizeNumber` seed a fresh Angel `Random` (Knuth's
  subtractive generator) with `time(NULL)` on every call and take its first
  number, so all draws within one wall-clock second are equal. Every audio
  draw now goes through `audio/AngelRandom` (the seed source is replaceable
  for tests); the per-object `std::mt19937` generators are gone.
* **The audObject layer.** `AudSoundBase::SetVolume / SetFrequency / SetPan`
  go through `audObject`, which multiplies the volume by the SOUND FX master
  and clamps it to 0..1, clamps the pitch multiplier to 0..2 and the pan to
  -1..1; `PlayLoop` / `PlayOnce` (always called with -1, "leave as is") never
  restart a playing buffer. `SoundSlot` now does all of this.
* **Table reading.** MM2's loaders read lines with `fgets`, cells with
  `strtok` (empty cells collapse) and numbers with `atof` / `atoi` (numeric
  prefix), following each file's line structure. The parsers were rewritten to
  do the same instead of searching for headers and validating numbers.
* **The tunnel echo** (second pass). `mmPlayer::Update` sets audio flag 0x80
  and `Aud3DObjectManager::EchoOn(0.5)` with the player's car in a room
  flagged underground, and every sound object then turns on the echo of its
  samples at the start of its `UpdateAudio`: an `EchoEffect` per sample, a
  duplicate DirectSound buffer that repeats the sample's queued play, stop,
  volume and frequency changes once they are as old as the delay. The mixer
  has effect voices for these duplicates, `SoundSlot` queues its changes like
  `AudSoundBase`, and `Object3DManager::setTunnel` is the race's hook.
  `MMDMusicManager::EchoOn` is never called: the music has no echo.

## SoundSlot.h / SoundSlot.cpp (AudSoundBase, audControl, audObject, audSound)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `clampPitch` | `audSound::SetPitch` | verified | rate × pitch, `__ftol`, unsigned clamp 100..100000 (negative wraps to the top) |
| `SoundSlot::load` | `AudSoundBase::Load`, `Aud3DSampleWrapper::Load` | fixed | the volume now starts at 0 (the sample wrapper sets 0 after loading) and pitch / pan are reset; "NOSOUND" leaves the slot empty |
| `SoundSlot::playLoop` | `AudSoundBase::PlayLoop`, `audSound::SetVolume / SetPitch / Play` | fixed | was: always applied volume and pitch and restarted; now -1 means "leave as is", explicit values go straight to the buffer (no audObject clamps; a volume outside 0..1 is rejected like DirectSound does), a playing buffer is left playing |
| `SoundSlot::playOnce` | `AudSoundBase::PlayOnce`, `audSound::Play` | fixed | was: restarted a playing one-shot; MM2's `audSound::Play` does nothing while the buffer plays (only `Stop` rewinds). Values above -1 go through the clamping setters |
| `SoundSlot::stop` | `AudSoundBase::Stop`, `audSound::Stop` | verified | stop and rewind |
| `SoundSlot::playing` | `AudSoundBase::IsPlaying` | verified | |
| `SoundSlot::setVolume` | `AudSoundBase::SetVolume`, `audObject::SetVolume` | fixed | was: clamped the volume alone and converted it to a gain; now the Angel volume goes to the mixer, which multiplies in the bus master and clamps the product (see `Mixer::computeTargets`) |
| `SoundSlot::setPitch` | `AudSoundBase::SetFrequency`, `audObject::SetPitch` | fixed | the multiplier is now clamped to 0..2 before the rate clamp (ambient engines asking for 2.2 now play at 2) |
| `SoundSlot::setPan` | `AudSoundBase::SetPan`, `audObject::SetPan` | fixed | now clamped to -1..1 (the pan offset at +0x80 is always 0) |
| `SoundSlot::enableEcho` | `AudSoundBase::SetEffect(1)`, `SetEchoEffect` (both), `audFX::EnablePCEcho` | fixed | new: one EchoEffect per sample, made on the first call and kept (with its delay, last volume, pan and position) until the slot is reloaded |
| `SoundSlot::disableEcho` | `AudSoundBase::DisableEffect(1)`, `DisableEchoEffect` | fixed | new |
| `SoundSlot::setEchoDelay / setEchoAttenuation / setEchoFrequency` | `AudSoundBase::SetDelayTime / SetEchoAttenuation / SetEchoFrequency` | fixed | new; SetEchoFrequency only while the echo is on |
| `SoundSlot::updateEcho` | `AudSoundBase::Update`, `UpdateEcho` | fixed | new |
| `SoundSlot::setVolume / setPitch / setPan / playLoop / playOnce / stop` (echo) | `AudSoundBase::SetVolume / SetFrequency / SetPan / PlayLoop / PlayOnce / Stop` | fixed | with the echo on: QueueVolume(master × volume), QueueFrequency(the buffer's Hz), CalculatePan(the unclamped pan), QueuePlay(1 / 0) with the original's position, QueueStop. The Hz is rounded from OpenMM2's stored multiplier (MM2 keeps whole Hz) |
| `SoundSlot::setEmitter` | — | openmm2 | DirectSound3D-style positioning; MM2's game sounds never use it |
| `SoundSlot` move operations, `params`, `start` | — | openmm2 | glue to the mixer |

## EchoEffect.h / EchoEffect.cpp (EchoEffect, audFX, EffectBase)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `EchoEffect::enable` | `EchoEffect::Enable`, `EffectBase::CreateDSoundBuffer`, `audFX::EnablePCEcho` | fixed | new: a stopped effect voice with the original's volume (after the master), pan, pitch and position; the queue size is reset; the rate for SetFrequency is the sample's |
| `EchoEffect::disable` | `EchoEffect::Disable` | fixed | new: queues emptied, the duplicate stopped (it keeps its position) |
| `EchoEffect::setDelayTime` | `EchoEffect::SetDelayTime` | fixed | new: the first call after enable sets the delay and sizes each queue to delay × 180 (truncated to a short); every call queues a play when the original is playing a loop (`OriginalBufferPlaying(1)`) |
| `EchoEffect::Queue::push / ripen`, `update` | `EchoEffect::Update`, `UpdateVolume / UpdatePitch / UpdatePlay / UpdateStop`, `QueueVolume / QueueFrequency / QueuePlay / QueueStop` | fixed | new: each update adds the frame time to every entry's age; once the oldest is as old as the delay it is applied and every entry at least that old is dropped (only the oldest of several applies); a push past the queue size restarts at entry 0. Volume, frequency, then play and stop (stop first when fewer stops than plays are queued) |
| `EchoEffect::queuePlay` | `EchoEffect::QueuePlay` | fixed | new: the duplicate jumps to the original's position when the play is queued |
| `EchoEffect::queueVolume / setVolume` | `EchoEffect::QueueVolume / SetVolume` | fixed | new: ftol(volume × attenuation × 10000 - 10000); DirectSound rejects values outside -10000..0 |
| `EchoEffect::setFrequency` | `EchoEffect::SetFrequency` | fixed | new: sample rate × factor clamped to 100..100000 Hz |
| `EchoEffect::calculatePan` | `EchoEffect::CalculatePan` | fixed | new: -0.25 × pan clamped to -1..1, at once |
| `EchoEffect::stop` | `EchoEffect::Stop`, `audObject::StopPCEchoBuffer` | fixed | new |
| — | `audManager::SetVolAllSounds`, `audControl::SetVolPCEchoBuffers` | deviation | when the SOUND FX slider moves MM2 sets every echo buffer to its control's volume × master; OpenMM2's echoes keep their last queued volume until the next one arrives (within the delay) |

## Object3D.h / Object3D.cpp (Aud3DObject, Aud3DObjectManager)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Audio3D::setDropOffs` | `Aud3DObject::SetDropOffs` | verified | MM2 divides by max² - min² unguarded; OpenMM2 guards a zero range (no retail file has one) |
| `Audio3D::updateDistance` | `Aud3DObject::CalcDistToClosestHeads2` | verified | d², pseudo distance, previous pseudo distance with the -1 sentinel |
| `Audio3D::withinMaxDistance` | `Aud3DObject::WithinMaxDistance` | fixed | now measures the distance itself; on the update an object takes a slot MM2 measures twice (Within + Past), so the doppler shift of that update is 0 |
| `Audio3D::pastMaxDistance` | `Aud3DObject::PastMaxDistance` | fixed | as above; past max keeps the last attenuation; the siren flag keeps the slot |
| `Audio3D::percentToMax` | `Aud3DObject::CalcPercentToMaxDist2` | verified | single-head branch |
| `Audio3D::attenuation` | `Aud3DObject::CalculateAttenuation` | verified | |
| `Audio3D::pan` | `Aud3DObject::CalcSinglePlayerPan` | verified | 0.2 × listener-space x / pseudo distance; 0 inside min; OpenMM2 also returns 0 at zero pseudo distance where MM2 divides by 0. The front/back factor MM2 stores at +8 is read by nothing |
| `Audio3D::doppler` | `Aud3DObject::CalculateDoppler` | verified | approach × factor × frame time + 1 |
| `Audio3D::resetDistance` | `Aud3DObject::Reset` | fixed | no longer called when a slot is lost (MM2's UnAssignSounds keeps the distance history) |
| `Audio3D` defaults | `Aud3DObject::Aud3DObject` | fixed | max² starts at -1 (never in range before SetDropOffs), d² at 1000000, percent at -1 |
| `Object3DManager::Object3DManager` | `Aud3DObjectManager::Aud3DObjectManager(4)`, `mmPlayer::Init` | verified | three slots for positioned objects: the player's car holds the fourth for good (priority + 1000000) |
| `Object3DManager::add` | `Aud3DObjectManager::Add`, `FindUnusedSlot`, `FindGreatestDistance` | verified | first free slot; else the walk for the farthest of no higher priority, replaced if the newcomer's priority is higher, or equal and closer |
| `Object3DManager::remove` | `Aud3DObjectManager::Remove` | verified | |
| `Object3DManager::holds / used / capacity` | `Aud3DObject` +0x44 | verified | |
| `SlotHolder::acquireSlot` | `Aud3DObject::Update` / `AddTo3DMgr` | verified | asks only without a slot and within max distance; without a manager OpenMM2 lets every object sound (tools) |
| `SlotHolder::releaseSlot / hasSlot / setManager` | `Aud3DObject::RemoveFrom3DMgr` | verified | |
| `Object3DManager::echoOn / echoOff / echo / echoDelay` | `Aud3DObjectManager::EchoOn / EchoOff`, +0x24 / +0xa0 | fixed | new |
| `Object3DManager::setTunnel` | `mmPlayer::Update` | fixed | new: EchoOn(0.5) on entering (not again while on), EchoOff on leaving; the race passes its tunnel flag (room flag 0x02 at the player's car) |
| `tunnelEchoDelay` | the EchoOn methods reading +0xa0 | fixed | new; 0.5 s without a manager |
| `Object3DManager::slotOf` | `Aud3DObject` +0x44 | fixed | new: the slot index a creature voice is chosen by |

## AudioTables.h / AudioTables.cpp, TextFields, AngelUnits (the loaders)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `fgetsLines`, `strtokFields`, `crtAtof`, `crtAtoi` | `Stream::fgets`, `strtok`, `atof`, `atoi` | fixed | new: lines cut at the first CR/LF, empty cells skipped, numeric prefixes (no hex in atoi). The old helpers dropped trailing cells, kept empty ones and rejected "0.9x" |
| `ageVolumeToGain` | `audSound::SetVolume` | verified | (v - 1) × 10000 hundredths of a dB |
| `agePanToMixer` | `audSound::SetPan` | verified | far channel at -|pan| × 100 dB |
| `ageMasterVolume` | `AudManager::AssignWaveVolume`, `AudManager::Log`, `DMusicWaveBuffer::SetVolume` | fixed | new: log(200 s) / log(200); the sliders were linear gains |
| `readText` | `datAssetManager::Open` | openmm2 | |
| `parseCarAudio` | `vehCarAudio::Load`, `vehEngineAudio::Load`, `vehEngineSampleWrapper::ParseCSVBuffer` | fixed | was: searched for "Horn wave" / "Engine wave" and required 11 numeric cells. Now line 2 is the horn row, line 3 the engine header, every later line an engine sample; an empty table is allowed. The "Volume Divisor" layout (`ParseCSVBufferOld`) is rejected (see Missing) |
| `ImpactTable::find` | — | openmm2 | tools; MM2 never reads the ID column |
| `ImpactTable::byIndex` | `AudImpact::GetAudImpactDataPtr` | verified | |
| `parseImpactTable` | `AudImpact::ReadCSV`, `AudImpactData::ReadCSV` | fixed | now block by block ("***", header, row, sample header, samples); a file that ends before ENDOFDATA loses the whole table, as in MM2 |
| `SurfaceSoundDef::hasSurfaceSound` | `vehSurfaceAudioData::ParseCSVBuffer` | fixed | "NOSOUND" now matches exact case |
| `SurfaceTable::at` | `vehSurfaceAudio` entry array | verified | |
| `parseSurfaceTable` | `vehSurfaceAudio::LoadCSV`, `vehSurfaceAudioData::ParseCSVBuffer` | fixed | now line 2 tunnel index, then header / row / skid header / `count` skid rows per block; the ice layout (a file MM2 never loads) is no longer special-cased |
| `parseSirenTable` | `vehPoliceCarAudio::Load`, `ReadSirenData`, `ReadSirenPlayInfo` | fixed | now a state machine on exact "Sample name" / "play time": every row after a "play time" header is a step; the explosion volume column is not read |
| `parseSuspension` | `vehSurfaceAudio::LoadSuspension` | fixed | the divisor is stored as 1 / divisor (0 for 0) like MM2; values by atof |
| `parseTireWobble` | `vehSurfaceAudio::LoadTireWobble` | fixed | same |
| `parseSemiData` | `vehSemiCarAudio::Load` | verified | |
| `VehicleTypes::isFreight / isPolice` | `vehCarAudioContainer::IsSemiOrBus / IsPolice` | fixed | now exact case (strcmp) |
| `parseVehicleTypes` | `vehCarAudioContainer::RegisterTypes`, `RegisterSemiNames`, `RegisterPoliceNames` | fixed | now lines 2, 4 and 6; names kept as written; "TRUE" in any case |
| `parseAmbientEngine` | `aiEngineAudio::ReadCSV` | fixed | every line after the third is a band; no band check |
| `parseHorn` | `vehHornAudio::ReadCSV` | verified | "horn play duration" (any case) starts a pattern |
| `parseAmbientSoundSet` | `Aud3DAmbientObject::Load`, `ReadSoundData`, `Aud3DObject::ReadVectorPoints` | fixed | line structure as MM2; VECTORPOINTS exact case, the line after it skipped. A sample type outside 0..3 aborts MM2; OpenMM2 clamps (deviation, malformed data only) |
| `parseAmbientContainer` | `Aud3DAmbObjContainer::Init` | verified | first cell of each line after the header |
| `parseCreatureVoice` | `AudCreature::ReadCSV`, `AudCreatureAvoid::ParseCSVBuffer`, `AudCreatureImpact::ParseCSVBuffer` | fixed | now MM2's block state machine ("min speed" / "min impact force", any case, end the previous block); the last impact block wins |
| `parseNumFileChoices` | `AudCreatureContainer::LoadNumFileChoices` | fixed | new (the voice file count; see Missing for its use) |
| `SpeechRow::header / headerIs / eventName`, `parseSpeechTable` | `mmRaceSpeech::SetReadState`, `locstrnicmp`, `mmCNRSpeech::SetReadState` | fixed | rows kept in order; headers end in "header", event names match as prefixes; the CnR event name is the text before the last space |
| `parseAnnouncerList` | `mmRaceSpeech::LoadCityInfo` | fixed | count on line 2 by atof, prefix on line 4 |

## CarAudio.h / CarAudio.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `impactStrength` | `vehCarDamage::ApplyImpact`, `aiVehicleActive` | fixed | summed |z| + |y| + |x| in MM2's order |
| `surfaceSoundIndex` | `vehWheel::GetSurfaceSound` | fixed | the material's sound as a short (+0x26); only -1 maps to 0, other values pass through; no material is 0 (the race). Second pass: checked against the asm, short truncation added |
| `carAudioPath` | `vehCarAudio::Load` | verified | tools; loading now falls back to default.csv when the car's table does not load either |
| `EngineSound::evaluate` | `vehEngineSampleWrapper::CalculateVolume`, `CalculatePitch`, `ParseCSVBuffer` | fixed | silencing now keeps the table slopes (only min / max become 0) |
| `EngineSound::load` | `vehEngineAudio::AssignSounds` | verified | |
| `EngineSound::update` | `vehEngineSampleWrapper::UpdateRPM(rpm)` | fixed | SetVolume, SetFrequency (clamped), PlayLoop(-1, -1) if not playing |
| `EngineSound::update3D` | `vehEngineSampleWrapper::UpdateRPM(rpm, volume, frequency, pan)` | fixed | as above with attenuation, doppler and pan |
| `EngineSound::silence` | `vehEngineAudio::Silence`, `vehEngineSampleWrapper::Silence` | fixed | see evaluate |
| `EngineSound::stop` | `vehEngineAudio::Stop` | verified | |
| `EngineSound::echoOn / echoOff`, the echo update in `update` / `update3D` | `vehEngineAudio::EchoOn / EchoOff`, `vehEngineSampleWrapper::EchoOn / EchoOff`, the end of `UpdateRPM` (both) | fixed | new: every sample at the manager's delay and 0.96; each sample updates its echo after its play / stop step |
| `CarAudioInputs::rpm` (idle floor removed) | `vehCarAudio::UpdateAudio3D` (+0x2c4) | fixed | OpenMM2 raised the RPM to idle; MM2 passes the engine's RPM as is |
| `SurfaceSounds::skidInRange / skidVolumeFor` | `vehSurfaceAudioData::UpdateSkid` | verified | |
| `SurfaceSounds::surfaceVolumeFor / surfacePitchFor` | `vehSurfaceAudioData::UpdateSurface`, `ParseCSVBuffer` | verified | |
| `SurfaceSounds::load / loadSuspension / loadTireWobble` | `vehSurfaceAudio::LoadCSV`, `AssignSounds`, `vehSurfaceAudioData::AssignSounds` | fixed | NOSOUND exact case; the invented slot priorities removed |
| `SurfaceSounds::surfaceChanged` | `vehSurfaceAudio::SurfaceChanged` | verified | |
| `SurfaceSounds::selectSurface` | `vehSurfaceAudio::UpdateSurface` (both) | verified | |
| `SurfaceSounds::updateSurface` | `vehSurfaceAudio::UpdateSurface`, `UpdateAir`, `vehSurfaceAudioData::UpdateSurface` | fixed | call order SetFrequency / SetVolume / SetPan / PlayLoop(-1, -1); the airborne probe range is now 3 to 33 m (MM2's ten segments start 3 m down) |
| `SurfaceSounds::updateSkid` | `vehSurfaceAudio::UpdateSkid`, `vehSurfaceAudioData::UpdateSkid` | verified | |
| `SurfaceSounds::updateSuspension` | `vehSurfaceAudio::UpdateSuspension` | fixed | volume clamp in MM2's order (low first, then high) and divisor as a stored reciprocal |
| `SurfaceSounds::updateTireWobble` | `vehSurfaceAudio::UpdateTireWobble`, `SetWheelPointers` | fixed | clamps in MM2's order; NaN damage plays nothing |
| `SurfaceSounds::silence` | `vehSurfaceAudio::UnAssignSounds` | fixed | new: a lost slot stops the surface and skids but lets thumps play out |
| `SurfaceSounds::stop / skidPlaying / airborne` | `vehCarAudio::IsAirBorne` | verified | |
| `SurfaceSounds::echoOn / echoOff / updateEcho` | `vehSurfaceAudio::EchoOn / EchoOff / UpdateEcho`, `vehSurfaceAudioData::EchoOn / EchoOff` | fixed | new: every entry's skids, then its surface sample; the update covers the current entry (+0) |
| `SurfaceSounds::setTunnel` | the manager's echo flag read by `UpdateSurface` / `UpdateSkid` | fixed | new: the owner passes inputs' `inTunnel` or the manager's flag |
| `ImpactSounds::volumeFor` | `AudImpactData::PlaySample` | verified | slope × force + min |
| `ImpactSounds::load` | `AudImpactData::ReadCSV`, `AssignSounds` | fixed | the frequency column is set once at assignment (SetFrequency, clamped) |
| `ImpactSounds::play` | `AudImpact::Play`, `AudImpactData::Play / PlaySample` | fixed | an audio id of -1 now plays nothing (was WALL); the raw id is kept |
| `ImpactSounds::updateAttenuation` | `AudImpact::UpdateAttenuation`, `AudImpactData::UpdateAttenuation` | verified | |
| `SirenPlayer::load` | `vehPoliceCarAudio` constructor, `Load`, `AssignSounds` | fixed | explosion volume 1 set at assignment; +0x144 starts at the last sample's volume |
| `SirenPlayer::start` | `vehPoliceCarAudio::StartSiren` | verified | state 1 / 2, count, SetVolume + SetFrequency + PlayLoop only with a slot |
| `SirenPlayer::stop` | `vehPoliceCarAudio::StopSiren` | verified | |
| `SirenPlayer::fluctuate` | `vehPoliceCarAudio::FluctuateSiren` | verified | OpenMM2 keeps an out-of-range "next" inside the table |
| `SirenPlayer::damage` | `vehPoliceCarAudio::DamageSiren` | verified | |
| `SirenPlayer::update` | `vehPoliceCarAudio::UpdateSiren()` | verified | |
| `SirenPlayer::update3D` | `vehPoliceCarAudio::UpdateSiren(v, f, p)`, `UpdateExplosion` | verified | the fading branch (+0x152) is unreachable in MM2 (PerpEscapes sets it, StopSiren clears it at once) and is not ported |
| `SirenPlayer::explode` | `vehPoliceCarAudio::PlayExplosion` | fixed | DamageSiren now gets the car's doppler |
| `SirenPlayer::silence / stopAll` | `vehPoliceCarAudio::UnAssignSounds` | fixed | an explosion now plays out when the slot is lost |
| `SirenPlayer::copsPursuingPlayer / resetPursuitCount` | `vehPoliceCarAudio::GetNumCopsPursuingPlayer` | verified | |
| `SirenPlayer::echoOn / echoOff / updateEcho` | `vehPoliceCarAudio::EchoOn / EchoOff / UpdateEcho` (the siren samples) | fixed | new; the explosion has no echo |
| `PlayerCarAudio::load` | `vehCarAudioContainer` (mode 2), `vehCarAudio::Init / Load / SetNon3DParams`, `vehSemiCarAudio::Init`, `vehPoliceCarAudio::Init`, `mmGame::Init` | fixed | siren table: London's in London, SF's in every other city (was `<city>policesiren`); horn / clutch / semi volumes set like SetNon3DParams |
| `PlayerCarAudio::updateHorn` | `mmGame::UpdateHorn`, `vehCarAudioContainer::PlayHorn / StopHorn` | verified | |
| `PlayerCarAudio::update` | `vehCarAudio::UpdateAudioNon3D`, `vehSemiCarAudio::UpdateAudioNon3D / UpdateReverse / UpdateAirBlow`, `vehPoliceCarAudio::UpdateAudioNon3D` | fixed | MM2's call forms (PlayOnce / PlayLoop with -1); impacts first (they happen in the simulation) |
| `PlayerCarAudio::stop / silenceEngine` | `vehCarAudioContainer::SilenceEngine` | verified | |
| `PlayerCarAudio::updateEchoState / echoOn / echoOff / updateEcho` | `vehCarAudio::UpdateAudio`, `EchoOn / EchoOff / UpdateEcho`, `vehPoliceCarAudio::UpdateAudio / EchoOn / EchoOff / UpdateEcho`, `vehSemiCarAudio::UpdateAudio / EchoOn / EchoOff / UpdateEcho` | fixed | new: sirens or the semi's beeper and air brake first, then engine, surfaces, the horn 0.05 s behind at 0.997 of its rate (SetEchoFrequency), the clutch at the manager's delay. The police and semi variants skip the echo update on the frame the echo comes on. The car reads the manager through `CarAudioOptions::manager` |
| `OpponentCarAudio::load` | `vehCarAudioContainer` (modes 0 / 1), `InitSemi`, `InitPolice` | fixed | positioned semis now get the reverse beeper and air brake; siren table by city as above. The `police` argument (an AI cop whose model is not in the list) is an OpenMM2 addition (deviation) |
| `OpponentCarAudio::update` | `vehCarAudio::UpdateAudio3D` (both), `vehPoliceCarAudio::UpdateAudio3D` (both), `vehSemiCarAudio::UpdateAudio3D`, `aiPoliceOfficer::StartSiren / StopSiren / PerpEscapes` | fixed | StartSiren gets `sirenPursuingPlayer` (MM2 passes `IsPlayer` of the suspect; was always true); a past-max police car with its explosion playing keeps the previous update's values; network horn latched like the container and updated every frame; semi extras |
| `OpponentCarAudio::update` (siren and explosion triggers) | `aiPoliceOfficer::StartSiren`, `StopSiren`, `PerpEscapes` | open | StartSiren / StopSiren follow `CarAudioInputs::siren` edges, as MM2's officer calls them. MM2 calls `PerpEscapes(true)` (PlayExplosion, then StopSiren) on every officer update while officer +0x968a is nonzero, so the explosion replays whenever it has finished; OpenMM2 explodes once on the rising edge of the cop's wreck state while its siren is on. The field's meaning belongs to the ai-vehicles audit; `wrecked` should mirror it |
| `OpponentCarAudio::silence` | `vehCarAudio::UnAssignSounds` (and semi / police) | fixed | the echo goes off first; loops stop, impacts / thumps / explosion play out; the distance history is kept |
| `OpponentCarAudio::updateEchoState / echoOn / echoOff / updateEcho` | as for the player's car | fixed | new: only for a slot holder, before UpdateAudio3D; no clutch sample, a horn only for network cars |
| `OpponentCarAudio::stop` | — | openmm2 | teardown |
| `AmbientCarAudio::pitchFor` | `aiEngineAudio::CalculatePitch` | verified | |
| `AmbientCarAudio::load` | `aiAmbientVehicleAudio::Init`, `LoadEngine`, `LoadHorn`, `LoadImpacts`, `aiEngineAudio::Load`, `vehHornAudio::Load` | fixed | the invented plural → singular name mapping is gone: MM2 opens `<model>_engine`, so `va_sedans_s` uses the default files |
| `AmbientCarAudio::update` | `aiAmbientVehicleAudio::UpdateAudio` (both), `aiEngineAudio::UpdateDoppler`, `vehHornAudio::UpdateDoppler` | fixed | the horn's volume / frequency / pan follow the car only while a pattern runs; the engine through SetVolume / SetFrequency (clamped to 0..2) / PlayLoop(-1, -1) |
| `AmbientCarAudio::honk` | `vehHornAudio::PlayAvoidance`, `aiAmbientVehicleAudio::PlayAvoidanceHorn` | fixed | was RandomizeNumber(2n - 0.01) over all n patterns; MM2 uses the last index (`AllocTiming` stores n - 1), so the stuck blast is never an avoidance honk. Draw from `randomizeNumber` |
| `AmbientCarAudio::impact` | `aiVehicleActive` impact, `vehHornAudio::PlayImpact`, `vehHornAudioTiming::Stop` | fixed | time-seeded draw; the stopped pattern rewinds; uses the stored attenuation |
| `AmbientCarAudio::startPattern` | `vehHornAudioTiming::Play` | fixed | keeps the pattern's own beep index |
| `AmbientCarAudio::updateHorn` | `vehHornAudio::Update`, `vehHornAudioTiming::Update` | verified | |
| `AmbientCarAudio::silence` | `aiAmbientVehicleAudio::UnAssignSounds`, `vehHornAudio::Reset` | fixed | the echo goes off first; Reset idles without rewinding; impacts play out |
| `AmbientCarAudio::echoOn / echoOff` and the echo step of `update` | `aiAmbientVehicleAudio::UpdateAudio / EchoOn / EchoOff / UpdateEcho`, `aiEngineAudio::EchoOn / EchoOff / UpdateEcho`, `vehHornAudio::EchoOn / EchoOff / UpdateEcho` | fixed | new: engine and horn at the manager's delay (the driver's AudCreature is not wired) |
| `AmbientCarAudio::stop` | — | openmm2 | teardown |

## Ambience.h / Ambience.cpp (Aud3DAmbientObject, Aud3DAmbObjContainer, mmBridgeAudio, aiSubwayAudio, aiCableCarAudio, mmRainAudio)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `AmbientObject::nearestPoint` | `Aud3DObject::GetClosestPositionPtr`, `CalcPseudoDistToClosestHead` | verified | |
| `AmbientObject::load` | `Aud3DAmbientObject::Init / Load / ReadSoundData / SetSoundData` | fixed | second pass: one class for every ambient object (was CityAmbience's private set); per-object active flags from the file; sounds on SOUND FX |
| `AmbientObject::interval` | `Aud3DAmbientObject::PendOneShot` | fixed | time-seeded RandomizeNumber(low, high) |
| `AmbientObject::update` | `Aud3DAmbientObject::Update(speed)`, `Aud3DObject::Update` | fixed | the speed is stored for the samples' ranges; the slot request only within the audible area (the point nearest the listener for sets with points) |
| `AmbientObject::updateAudio` | `Aud3DAmbientObject::UpdateAudio` (both) | fixed | second pass: the echo goes on and off with the tunnel; area 1 loses its slot above ground, area 2 underground; the echo update for areas 0 and 1; past max distance the slot goes; attenuation / pan / doppler for positional sets |
| `AmbientObject::updateSoundData` | `Aud3DAmbientObject::UpdateSoundData`, `UpdateDoppler`, `UpdateLoop`, `UpdateOneShot` | fixed | only active samples; PlayLoop(-1, -1) |
| `AmbientObject::playOneShot` (both) | `Aud3DAmbientObject::PlayOneShot` (both) | fixed | random volume and pan from time-seeded draws (equal within a second); types 2 and 3 at the object's values. The public one needs a slot (MM2 has no outside caller) |
| `AmbientObject::activate / deactivate / active` | `Aud3DAmbientObject::ActivateSound / DeactivateSound` | fixed | new: deactivating a loop stops it, a one-shot plays out |
| `AmbientObject::soundIndex` | `Aud3DAmbientObject::GetSoundIndex` | fixed | new: exact-case name |
| `AmbientObject::slotLost / lose` | `Aud3DAmbientObject::UnAssignSounds` (both), `RemoveFrom3DMgr` | fixed | echo off, every playing sample stops; the distance history is kept |
| `AmbientObject::echoOn / echoOff` | `Aud3DAmbientObject::EchoOn / EchoOff / UpdateEcho` (both) | fixed | new |
| `AmbientObject::stop / samplePlaying / position` | — | openmm2 | teardown and queries |
| `CityAmbience::load` | `Aud3DAmbObjContainer::Init`, `FileValid`, `CreateAmbientObject` | verified | |
| `CityAmbience::update` | `Aud3DAmbObjContainer::Update` | verified | every object with speed 0 (`Aud3DObjectManager::Update(0)`) |
| `CityAmbience::set / object / audible / stop` | — | openmm2 | lookups, teardown |
| `BridgeAudio::activate / deactivate` | `mmBridgeAudio::Activate / Deactivate` | fixed | new (replaces the `playAt` stand-in): -1 is samples 0 and 1. gizBridge activates both as the span starts to move and deactivates them when it stops; its simulation is not in OpenMM2 |
| `SubwayAudio::update / activate / deactivate` | `aiSubwayAudio::Update / Activate / Deactivate` | fixed | new (replaces `setLoop`): from 1 m/s sample 0, below sample 1 ("NOTHING" in the retail file), then Update(0). aiSubway / gizTrain are not in OpenMM2 |
| `CableCarAudio::load` | `aiCableCarAudio::aiCableCarAudio / Init`, `aiCableCarAudioData::aiCableCarAudioData / AssignSounds` | fixed | new: CABLECARGOBELL, CABLECARSTOP, CABLECAR, CABLECARSTART, STREETCABLE (assigned, never played); 0..100 m, priority 8 |
| `CableCarAudio::nextState` | `aiCableCarAudioData::UpdateState` | fixed | new: 0.001 / 0.1 / 0.5 m/s |
| `CableCarAudio::update / updatePlay` | `aiCableCarAudio::UpdateAudio` (both), `aiCableCarAudioData::UpdatePlay` | fixed | new: 0.98 × attenuation; the bell rings once per start at that volume without pan or doppler; the previous speed is taken after every update with a slot. aiCableCar is not in OpenMM2 |
| `CableCarAudio::slotLost` | `aiCableCarAudio::UnAssignSounds`, `aiCableCarAudioData::Stop / UnAssignSounds` | fixed | new: the loop and the start sound stop |
| `CableCarAudio::stop` | — | openmm2 | teardown |
| `RainAudio::load` | `mmRainAudio::mmRainAudio` | fixed | sounds on `Bus::Effects` (SOUND FX) instead of the music slider's bus |
| `RainAudio::shelter` | `mmRainAudio::ShelterOn / ShelterOff` | fixed | ShelterOff's ±20 pans are clamped by audObject::SetPan to hard left / right; OpenMM2 had assumed DirectSound rejected them and left the claps centred |
| `RainAudio::update` | `mmRainAudio::Update`, `SetInterior` | verified | the "flash" state machine is MM2's, but nothing in MM2 reads it (no lightning is drawn; the old comment named a nonexistent mmSky::DoFlash). The `Process3D(false)` stop is not modelled (see report). The race now passes the tunnel flag (shelter) and the hood / dash views (interior) |
| `RainAudio::stop` | — | openmm2 | |

## Voices.h / Voices.cpp (AudSpeech, mmRaceSpeech, mmCNRSpeech, mmCCSpeech, AudCreature)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Announcer::load` | `mmRaceSpeech::LoadCityInfo` | fixed | only "sf" and "london" have announcers |
| `Announcer::beginSession / loadCityInfo` | `mmRaceSpeech::LoadCityInfo` | fixed | the announcer is drawn again for every race (MM2 builds a new mmRaceSpeech in InitRace), time-seeded; SF 3 → 4. A minimum install's "announcer 1" is not modelled (inferred: OpenMM2 always reads full data) |
| `Announcer::announcerId` | `mmRaceSpeech` +0x88 | verified | |
| `Announcer::beginRace` | `mmSpeechContainer::InitRace`, `mmPlayer::InitSpeechAudio` | verified | table order and weather / time-of-day rules |
| `Announcer::loadRaceGroup` | `mmRaceSpeech::LoadGroup`, `SetReadState` | fixed | header prefixes, unknown headers keep the state, ranges as floats, the extra queue slots of unlock groups |
| `Announcer::loadVehicleUnlock / loadTextureUnlock` | `mmRaceSpeech::LoadVehicleUnlock / LoadTextureUnlock` | fixed | separated from the play calls like MM2 |
| `Announcer::pickLine` | `AudSpeechData::GetRandomName` | verified | |
| `Announcer::lineName` | `AudSpeechData::GetRandomName / GetName` | verified | two digits below 10 |
| `Announcer::randomGroup` | `mmRaceSpeech::PlayPreRace / PlayUnlockRace / PlayUnlockVehicle` | fixed | time-seeded; an unset (-1, -1) range draws group 0 like MM2 |
| `Announcer::play / putInQueue / start` | `AudSpeech::Play`, `PlayStream`, `PutInQueue` | fixed | the line is now drawn when it starts, not when it is queued; the queue has MM2's slot count |
| `Announcer::playPreRace` | `mmRaceSpeech::PlayPreRace` | fixed | time-seeded draws |
| `Announcer::playFinalCheckpoint / playDamagePenalty / playRaceProgress` | `mmRaceSpeech::PlayFinalCheckPoint / PlayDamagePenalty / PlayRaceProgress` | verified | |
| `Announcer::playFinalLap` | `mmRaceSpeech::PlayFinalLap` | deviation | MM2 stops the line and plays group +0xc4 without checking it was loaded (an out-of-range read); OpenMM2 stops and plays nothing |
| `Announcer::playResults` (both) | `mmRaceSpeech::PlayResults / PlayResultsWin / Mid / Poor` | fixed | time-seeded draw |
| `Announcer::playUnlockRace / playUnlockVehicle` | `mmRaceSpeech::PlayUnlockRace / PlayUnlockVehicle` | fixed | no Stop; queued 0.1 s |
| `Announcer::playUnlockTexture` | `mmRaceSpeech::PlayUnlockTexture` | fixed | new |
| `Announcer::beginCopsAndRobbers` | `mmSpeechContainer::InitCNR`, `mmCNRSpeech::LoadGroup`, `SetReadState` | fixed | SF reads `bullshit.csv` (was `cnrsf.csv`), London `cnrlondon.csv`; the "num used" window is applied at load; names keep their trailing space |
| `Announcer::playCopsAndRobbers` | `mmCNRSpeech::Play(char*)` | fixed | a random group of the event's range (was a random row of a reparsed table) |
| `Announcer::beginCrashCourse` | `mmSpeechContainer::InitCC`, `mmCCSpeech::SetSubPath`, `LoadGroup`, `SetReadState`, `LoadCheckPointIndexInfo` | fixed | new port (was a first-row approximation) |
| `Announcer::playCrashCoursePreRace / CheckPoint / Results / Unlock` | `mmCCSpeech::PlayPreRace / PlayCheckPoint / PlayResults / PlayUnlock` | fixed | new port |
| `Announcer::update` | `AudSpeech::Update` | verified | every slot counts down; the first due one starts when nothing plays |
| `Announcer::speaking / stop / reset` | `AudSpeech::IsPlaying / Stop / EmptyQueue` | verified | |
| `CreatureVoice::advanceClock / resetGlobals` | `AudCreatureImpact::UpdateStatics` | fixed | the shared "last line" values start at 0 (zero-initialised globals), not -1 |
| `CreatureVoice::load` | `AudCreatureAvoid / AudCreatureImpact` copies | fixed | lines are wave sounds on the SOUND FX bus (were on the commentary bus); attenuation 1, pan 0 to start |
| `CreatureVoice::eligible` | `AudCreatureAvoid::IsEligible` | verified | |
| `CreatureVoice::avoid` | `AudCreature::PlayAvoidance`, `AudCreatureAvoid::QueuePlay` | fixed | time-seeded draw |
| `CreatureVoice::impact` | `AudCreature::PlayImpact`, `AudCreatureImpact::QueuePlay` | fixed | time-seeded draw |
| `CreatureVoice::playAvoid / playImpact` | `AudCreatureAvoid::Play`, `AudCreatureImpact::Play` | fixed | second pass: the container is asked for its slot (`UpdateNonVirtual`) and the line waits without one (was: the creature's own drop-off); without a container (tests) nothing is checked |
| `CreatureVoice::update` | `AudCreature::Update`, `AudCreatureAvoid::Update`, `AudCreatureImpact::Update` | fixed | second pass: speed and frame time only; the 50 m test uses the squared distance the container last gave |
| `CreatureVoice::updateAttenuation` | `AudCreature::UpdateAttenuation`, `AudCreatureAvoid / AudCreatureImpact::UpdateAttenuation` | fixed | new (was folded into update with the creature's own position) |
| `CreatureVoice::setOwner / unassign` | `AudCreature::SetAud3DObjectPtr / UnAssignSounds` | fixed | new: queued lines are dropped, lines being said play out |
| `CreatureVoice::speaking` | `AudCreature::IsPlaying`, `AudCreatureAvoid / AudCreatureImpact::SamplePlaying` | fixed | the chosen line of each block and the impact line only (was any line) |
| `CreatureVoice::echoOn / echoOff / updateEcho` | `AudCreature::EchoOn / EchoOff / UpdateEcho`, `AudCreatureAvoid / AudCreatureImpact::EchoOn / EchoOff / UpdateEcho` | fixed | new: every line |
| `CreatureVoice::stop` | — | openmm2 | teardown |

## PedAudio.h / PedAudio.cpp (aiPedAudio, AudCreatureContainer)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `PedestrianAudio::load` | `mmGame::Init`, `aiPedAudio::SetCSVCatString("")`, `LoadNumFemaleChoices / LoadNumMaleChoices`, `AudCreatureContainer::LoadNumFileChoices` | fixed | new: each sex's file number ftol(RandomizeNumber(1, n + 0.25)); 0 when the count file is missing |
| `PedestrianAudio::voiceSet / creature` | `AudCreatureContainer::LoadVoices`, `AudCreature::Load / AddToHash`, the statics' voice table | fixed | new: a file is loaded once, with one AudCreature per manager slot; a missing file is looked for again each time |
| `PedestrianAudio::isWoman` | `aiPedestrian_IsWoman`, `aiPedestrian_AreStringsEqual` | fixed | new: FEMALE, WOMAN, GIRL, Hooker anywhere, ignoring the case of a..z, with the search that does not back up after a mismatch |
| `PedestrianAudio::update` | `aiMap::Update` → `AudCreatureContainer::UpdateStatics / UpdateVoices`, `aiPedestrian::Update` → `AudCreatureContainer::Update`, `aiPedestrian::Wander / Avoid` → `PlayAvoidanceReaction`, `Aud3DObjectManager::Update` → `AudCreatureContainer::UpdateAudio` | fixed | new, in that order. The speed given to the voices is the player's: aiMap reads it through the player vehicle's virtual at +8 (inferred to be its speed) |
| `PedestrianAudio::Container::init` | `aiPedestrian::Init` (audio part), `aiPedAudio::LoadFemaleVoices / LoadMaleVoices("default", true)` | fixed | new: 0..40 m, priority 8, "%s_fpedvoice%s%d" / "%s_mpedvoice%s%d". Re-running it when a pool entry changes model is OpenMM2's |
| `PedestrianAudio::Container::releaseWhenQuiet` | `AudCreatureContainer::Update` | fixed | new |
| `PedestrianAudio::Container::avoid / impact`, `PedestrianAudio::impact` | `AudCreatureContainer::PlayAvoidanceReaction / PlayImpactReaction` | fixed | new; nothing in MM2 calls PlayImpactReaction for pedestrians |
| `PedestrianAudio::Container::updateAudio` | `AudCreatureContainer::UpdateAudio` (both), `EchoOn / EchoOff / UpdateEcho` | fixed | new: the echo, then past 40 m the slot goes, else attenuation, pan and squared distance to the voice (the doppler shift MM2 works out is unused) |
| `PedestrianAudio::Container::requestSlot / assign / unassign / lose` | `Aud3DObject::Update(NonVirtual)`, `AudCreatureContainer::AssignSounds / UnAssignSounds`, `RemoveFrom3DMgr` | fixed | new |
| `PedestrianAudio::stop / audible / speaking` | — | openmm2 | teardown and queries |

## Mixer.h / Mixer.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Mixer::Mixer` (voice count) | `AudManager::Init` (SetMaxConcurrent(1, 32)), `AudManager::SetNumChannels` (empty) | fixed | 32 voices (was 96); SOUND QUALITY's 8 / 16 / 32 channels change nothing in MM2 |
| `Mixer::pickSlot` | `audManager::MoveToActive` | fixed | lowest priority, then the oldest (the loop-before-one-shot preference was invented) |
| `Mixer::computeTargets` (volume) | `audObject::SetVolume`, `AudManager::AssignWaveVolume`, `DMusicWaveBuffer::SetVolume` | fixed | Angel voices: clamp(v × master) to 0..1 then dB; linear voices and streams: the gain of the master |
| `Mixer::computeTargets` (pan) | `audSound::SetPan`, `AudManagerBase::IsStereo` | verified | far channel only; centred in mono |
| `Mixer::computeTargets` (3D path) | — | openmm2 | DirectSound3D model; unused by the game sounds |
| `Mixer::setBusVolume / busVolume` | `AudManager::AssignWaveVolume`, `AudioOptions::SetSFXVolume / SetMusicVolume` | fixed | the slider maps through ageMasterVolume. The intro movie's stream on `Bus::Effects` now follows the same curve (OpenMM2 choice) |
| `Mixer::setStereo / stereo` | `AudioOptions::SetStereoFX`, `AudManagerBase::SetStereoFlag / IsStereo` | fixed | new: mono centres every voice (MM2 skips its pan calls; surround only adds an EAX/3D flag) |
| `Mixer::setBalance` | `MixerCTL::AssignWaveBalance` | inferred | MM2 sets the Windows wave mixer's balance; OpenMM2 attenuates the opposite channel linearly |
| `Mixer::setMasterVolume` | — | openmm2 | OpenMM2's extra master level |
| `Mixer::createEffectVoice / playEffect / haltEffect / position / setPosition` | `EffectBase::CreateDSoundBuffer`, `IDirectSoundBuffer::Play / Stop / GetCurrentPosition / SetCurrentPosition` | fixed | new: effect voices outside the 32-voice limit, never stolen, keeping their position while stopped; a non-looping one stops at 0 at its end (inferred from DirectSound) |
| `Mixer::stopAll` (effect voices) | `audManager::StopAllSounds`, `audControl::StopPCEchoBuffers` | fixed | effect voices stop without being freed |
| `Mixer::busMaster`, `VoiceParams::masterApplied` | `AudManagerBase::GetMasterSFXVolume`, `EchoEffect::QueueVolume` | fixed | new: the echo's volume already includes the master |
| `Mixer::play / stop / stopAll / isPlaying / set* / pauseAll / activeVoices / streams / mix` | DirectSound | openmm2 | the software mixer (resampling, ramps, stream mixing) |
| `Mixer::setDopplerFactor / setRolloffFactor / setListener` | — | openmm2 | DS3D model only |

## Music.h / Music.cpp, MusicMotif (DMusicObject, mmGameMusicData, DirectMusic)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `songFromRow` / `MusicTables::parseRace` | `mmSingleRaceMusicData::LoadMusicSegments`, `LoadMusic` | fixed | strtok cells; every line after the header is a song (GetNumDMusicChoiceGroups counts lines). Column → segment mapping verified (start 0, return 2, idle 4, idle cops 5, cops 1, pause 3, results 6, motif style / name / band) |
| `MusicTables::parseCruise` | `mmSingleRoamMusicData::LoadMusicSegments`, `LoadMusic` | fixed | as above; columns verified (start, return, idle, cops, idle cops, pause, motif) |
| `MusicTables::parseSingle` | `AudioOptions::LoadUIMusicCSV`, `mmGameMusicData::LoadAmbientSFXSegments` | fixed | first cell of line 2 |
| `MusicTables::pickSong` | `mmGameMusicData::RandomizeNumber` | fixed | new: time-seeded (count - 0.01) × u (was a `std::mt19937` draw) |
| `MusicTables::load` | `mmGameMusicData::Load` | verified | file names |
| `MusicLibrary::*` | DirectMusic loader | openmm2 | file resolution for dmusic |
| `MusicEngine::selectSong` | `mmSingleRaceMusicData / mmSingleRoamMusicData::LoadMusic` | fixed | pickSong |
| `MusicEngine::segmentFor` | `MMDMusicManager` indices (+0x24 … +0x44) | verified | IdleCops falls back to Idle when a row has none (never asked in cruise) |
| `MusicEngine::setState` | `DMusicObject::SegmentSwitch`, `StopSegment` | fixed | a stop can now wait for a beat (StopSegment(1)); the Auto timing is OpenMM2's for callers without a director (inferred) |
| `MusicEngine::transitionTo` | `DMusicObject::SegmentSwitch` (both), `PlaySegment`, `SegmentWrapper::Play` | inferred | same-segment no-op is MM2's; the boundary handling is dmusic's (no composer) |
| `MusicEngine::triggerMotif` | `DMusicObject::PlayMotif`, `SegmentWrapper::Play` | fixed | a jump while the motif still plays does nothing (SegmentWrapper::Play returns while playing); SetRepeats(1) |
| `MusicEngine::startMotif / renderMotif`, `MusicMotif.c` | DirectMusic secondary segments | inferred | dmusic has no secondary segments or motifs |
| `MusicEngine::setAmbience` | `mmGameMusicData::LoadAmbientSFX`, `MMDMusicManager::UpdateAmbientSFX` | fixed | plays the city segment; the race now stops it ("") on entering a tunnel (audio flag 0x80, StopSegment(0)) and starts it again outside (PlaySegment). MM2 does this only in its ambient mode (music off), as one DirectMusic object plays either; OpenMM2 plays the ambience on its own stream. "underground" is a tool option MM2 never uses |
| `MusicEngine::playSegment / preload / loadMotifStyle / render / stopAll`, `toString` | — | openmm2 | tools and dmusic glue |
| `MusicPlayer::*` | — | openmm2 | threading, ring buffers |
| `MusicPlayer::startRace` | `mmSingleRaceMusicData::LoadMusic` | fixed | pickSong |

## MusicDirector.h / MusicDirector.cpp (MMDMusicManager, mmGame, mmPopup)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `MusicDirector::segmentSwitch` | `DMusicObject::SegmentSwitch(int)` | verified | same segment: nothing; previous / current |
| `MusicDirector::autoTransition` | `DMusicObject::SegmentSwitch(int, ushort, ulong)` | verified | groove command, DMUS_COMPOSEF_MEASURE (0x20) |
| `MusicDirector::update` | `mmGame::UpdateDMusic`, `StartMusic`, `MMDMusicManager::UpdateSeconds`, `UpdateMusic` | verified | start 1.25 s in (a constant); races block the idle logic; cop count 0 → 1 / 1 → 0; motif on the airborne edge |
| `MusicDirector::matchMusicToPlayerSpeed` | `MMDMusicManager::MatchMusicToPlayerSpeed` | verified | 5 m/s, 5 s, timer starts at 10000; results / pause / idle checks |
| `MusicDirector::raceStarted` | `mmSingleRace / Circuit / Blitz` "Go!" (+0x50 = 0) | verified | |
| `MusicDirector::pause` | `mmPopup::PlayPauseMusic` | verified | |
| `MusicDirector::resume` | `mmPopup::PlayReturnMusic` | fixed | switches to the previous segment whatever the current one (was only from Paused) |
| `MusicDirector::finish` | `mmSingleRace::UpdateGame` StopSegment(0) | verified | the index stays, so idling after the finish brings in the idle segment |
| `MusicDirector::damagedOut` | `mmSingleRace` / `mmSingleBlitz` StopSegment(1) | fixed | new: an ending on the next beat (dmusic: a stop on the beat; inferred). The race calls it instead of `finish` when the session ended in a wreck (`Session::damagedOut`) |
| `MusicDirector::results` | `mmPopup::ShowResults` | fixed | was attributed to ShowRoster and switched even when already in results; now SegmentSwitch's same-segment rule |
| `MusicDirector::takeCommands / takeBigAir` | — | openmm2 | |

## SoundBank.h / SoundBank.cpp, Wav.h / Wav.cpp (formats)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `SoundBank::SoundBank / resolve / get` | `InitAudioManager` (sub-path aud22, ext .22K), `AudSpeech::SetSubPath("aud11\…")` | verified | 22 kHz first; speech exists only at 11 kHz. Falling back to 11 kHz for the four effects without a 22 kHz file is OpenMM2's (deviation: MM2 would play nothing) |
| `SoundBank::setQuality` | `AudioOptions::SetQuality` | deviation | MM2's option never changes the files; `Quality::Low` (11 kHz first) is OpenMM2's and the frontend no longer uses it |
| `decodeWav` | `audSound::CreateSoundBufferFromFile`, `WriteWaveDataToBuffer` | verified | every retail WAV decodes (16-bit mono PCM); the 8/24/32-bit and float paths are OpenMM2 generality |

## Deviations and inferred rows not listed above

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `AngelRandom::seed / number` | `Random::Seed`, `Random::Number` | fixed | new port (x87 product kept in double) |
| `randomizeNumber` (both), `setRandomizeSeedSource` | `AudManagerBase::RandomizeNumber` | fixed | new; the seed source is replaceable for tests (deviation only when replaced) |
| Ambient traffic audio is not driven by the race | `aiAmbientVehicleAudio`, `aiVehicleSpline` | open | see Missing: the city ambience, announcer (session audit) and pedestrian voices are wired; the traffic cars still create no `AmbientCarAudio` |
| `parseAmbientSoundSet` sample type clamp | `Aud3DAmbientObject::UpdateSoundData` (Abortf) | deviation | malformed data only |
| `SirenPlayer::fluctuate` next clamp | `FluctuateSiren` | deviation | malformed data only |
| `Audio3D::pan` zero pseudo distance | `CalcSinglePlayerPan` | deviation | MM2 divides by 0 |
| `OpponentCarAudio::load` `police` flag | `vehCarAudioContainer` | deviation | an AI police car with a non-police model still gets a siren |
| `SlotHolder` without a manager | — | deviation | tools: every object sounds |
| Mixer resampling, ramps, float mixing | DirectSound software mixer | inferred | |
| `Announcer` London Cops & Robbers names with a trailing space | `AudStream::PlayOnce` | inferred | the lines are not found, as the path MM2 builds has the space in it |
| `MusicDirector` composed endings and fills | `IDirectMusicComposer::AutoTransition` | inferred | dmusic has no composer |
| `Music` menu segment timing | MenuManager / UI music | inferred | not traced |
| `MusicEngine::setState(Results)` Beat | `SegmentSwitch(results, END, BEAT)` | inferred | the END embellishment is not rendered |
| `RainAudio` interior flag source | `mmPlayer::SetCamera` | verified | the race passes the hood and dash views (session audit) |
| `Mixer` stream for the DirectMusic ambience on `Bus::Ambient` | `DMusicWaveBuffer::SetVolume` | inferred | follows the music slider (Context sets it) |
| `MusicEngine` synth gain 0.5 | DirectMusic software synth | inferred | |
| `MusicEngine::setAmbience("underground")` | — | deviation | a tool option; MM2 has the segment but never plays it |
| `SoundBank` 11 kHz fallback | `InitAudioManager` | deviation | see SoundBank |
| `Mixer` intro movie on `Bus::Effects` | — | deviation | OpenMM2 routing |

## Missing

| MM2 | What it does | Status |
| --- | --- | --- |
| `aiAmbientVehicleAudio` wiring (`aiVehicleSpline::Init / Update`, `aiGoalAvoidPlayer::Reset` → `PlayAvoidanceHorn` + `PlayAvoidanceReaction`, `aiVehicleActive` impacts → `PlayImpactHorn` / `PlayImpactReaction`, `aiMap` → `UpdateStatics(player speed)`) | Ambient traffic engines, horns and their drivers' voices | open: `AmbientCarAudio` is ported (with its echo) but no traffic car creates one, and the drivers' `AudCreature` containers are not ported (ai-vehicles + session) |
| The owners of `BridgeAudio`, `SubwayAudio`, `CableCarAudio` and the ferry's `AmbientObject` (`gizBridge`, `aiSubway`, `gizTrain`, `aiCableCar`, `gizFerry`) | Drawbridges, tube trains, trains, cable cars and ferries | the audio objects are ported; the simulations that would own and update them are not in OpenMM2 |
| `MMDMusicManager::EchoOn / EchoOff` | An echo on the DirectMusic buffer | never called in build 3393: nothing to port |
| `vehNitroCarAudio` | Nitro sound for every non-semi, non-police car when vehtypes.csv says "Always nitro" (FALSE in retail) | open, no effect on retail data |
| `vehEngineSampleWrapper::ParseCSVBufferOld`, `CalculateVolumeOld` | The "Volume Divisor" engine table layout (volume = rpm / divisor below a cut RPM, divisor / rpm above, clamped) | open, no car table MM2 loads uses it |
| `AudSoundBase` raw `PlayLoop` volume path | `PlayLoop` with an explicit volume bypasses the SOUND FX master | not ported (MM2 always passes -1) |
| `Aud3DObjectManager::Process3D(false)` (results popup, `mmPopup`) | Removes every positioned object from its slot and stops the rain loop while popups show | open: session (popups) |
| `Aud3DObjectManager::QueueInCopVoice / PlayCopVoice` | Called on a damage out; both are empty in build 3393 | nothing to port |
| `mmAmbientAudio` | A city "walla" loop; never constructed | nothing to port |
