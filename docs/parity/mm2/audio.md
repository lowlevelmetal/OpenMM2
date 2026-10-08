# MM2 -> OpenMM2: audio

Audited from MM2Recomp (midtown2.exe build 3393) on 2026-10-08.

Summary: 1053 reachable functions in 70 classes and the global namespace;
ported 625 (of which newly ported 21), replaced 265, not needed 163, open 0.

The first audit ([../audio.md](../audio.md)) went from OpenMM2's audio code
to MM2. This record goes the other way: every function of MM2's audio
classes the entry point reaches (the coverage tool's list), with what
OpenMM2 does for it. Overloads share a row; a row covers several small
functions when they share a status. Behaviour is described in
[docs/audio.md](../../audio.md) and [docs/music.md](../../music.md).

Statuses: **ported** (OpenMM2 does it; most rows were verified or fixed by
the first audit), **ported (new)** (ported by this audit, with tests in
`tests/audio/test_parity_mm2_audio.cpp`), **replaced** (OpenMM2 does the
job differently on purpose: its software mixer and SDL output instead of
DirectSound, its own DirectMusic renderer, the virtual file system),
**not needed** (code the game never runs, with the reason), **open**
(missing).

What this audit found and ported:

* **Pause.** While the game is paused MM2's audio manager stops every
  wave sound on the first two paused frames (`AudManagerBase::UpdatePaused`);
  OpenMM2 kept the engines, skids and ambience sounding under the popup and
  the full-screen map. (`audio/game/AudioManager`.)
* **The announcer's timing.** The speech container is updated by
  `AudManager::Update` and again by `mmGame::Update`, so the queued delays
  pass twice as fast while the game runs (the pre-race line starts 0.75 s
  after it is queued) and keep counting while it is paused.
* **Resets.** `Aud3DObject::Reset` through the owners' Reset (cars,
  traffic, the world objects) takes the slot away and forgets the distance
  history; OpenMM2 had no such call.
* **The traffic drivers' voices** now belong to their car's audio as in
  `aiAmbientVehicleAudio`: they echo in tunnels and drop their queued lines
  when the car loses its slot.
* **Session sound priority.** The race modes', waypoints' and HUD's
  sounds have audControl priority 0x17.
* **The final checkpoint.** `mmWaypoints::Update` announces it and switches
  the music to the chase segment (also when a circuit's final lap starts);
  the session's events were not consumed. Restarting the race starts the
  music again (`mmGame::Reset` → `StartMusic`).
* **The old "Volume Divisor" engine tables** are read like
  `ParseCSVBufferOld` instead of rejected.

Found to be dead in MM2 (and so not needed): the in-race CD player (inert
with the retail disc in the drive), MIDI, the streamed playlist music,
`vehNitroCarAudio` (its sample never plays), the speech one-shots, the
final-lap / race-progress / unlock-race / Cops & Robbers commentary
(no callers), and the split-screen listeners.


## AudManagerBase, AudManager (the audio manager)

What it is for: the one audio manager MM2 makes (InitAudioManager at
start-up, unless `-noaudio` / `-nosoundfx`): it owns Angel's `audManager`
(32 concurrent wave sounds), DirectSound (`mmDirSnd`), the CD (`CDMan`) and,
during a race with commentary on, the speech container. GameLoop calls
its `Update` once a frame before the game's update. OpenMM2's
`audio::Mixer` (made in `app/App.cpp`) and `audio::AudioDevice` (SDL output)
replace the DirectSound side; the per-frame behaviour is
`audio/game/AudioManager` (new): while the game is paused every sound stops
on the first two paused frames, and while it runs the announcer's queue is
updated here as well as in `mmGame::Update` (twice a frame).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `AudManager::Update` | ported (new) | `audio/game/AudioManager.cpp` `AudioManager::update`; `app/RaceScreen.cpp` at the start of the frame | Paused: the virtual UpdatePaused and nothing else. Running: the paused counter (+0x1e) back to 0, then `mmSpeechContainer::Update`, so with `mmGame::Update`'s own call the announcer's delays pass twice as fast as the clock (the pre-race line starts 0.75 s after it is queued). The `audManager::Update` and `AudMidi::MidiUpdate` calls are the mixer's job / never reached (no MIDI). |
| `AudManagerBase::UpdatePaused`, `AudManagerBase::StopAllSounds` | ported (new) | `AudioManager::update`, `Mixer::stopAll` | While the counter is at most 1 (a constant): `audManager::StopAllSounds` for all five sound types, then count. So every wave sound (engines, skids, sirens, ambience, rain, voices, the announcer's line) stops on two paused frames; the DirectMusic music is not one of them. Test `AudioParityMm2.PausedGameStopsEverySoundOnTwoUpdates`. |
| `AudManagerBase::Update` | not needed | - | The base class's Update; the only manager MM2 makes is an AudManager, whose own Update overrides it. Its extra `AudStreamingMusic::StreamingMusicUpdate` has nothing to play (see AudStreamingMusic). |
| `AudManager::AudManager`, `AudManagerBase::AudManagerBase`, `AudManager::Init`, `AudManager::Enable`, `AudManager::Disable`, `AudManagerBase::Enable`, `AudManagerBase::Disable`, `AudManagerBase::IsEnabled`, `AudManager::DeviceValid`, `AudManager::~AudManager`, `AudManagerBase::~AudManagerBase`, `AudManager::`scalar_deleting_destructor'`, `AudManagerBase::`scalar_deleting_destructor'`, `InitAudioManager`, `KillAudioManager` | replaced | `app/App.cpp` (the Mixer), `audio/AudioDevice.cpp`, `audio/SoundBank.cpp` | Creating and opening the DirectSound device and Angel's audio library. What the game hears from it is kept: 32 concurrent sounds (`Mixer::kMaxVoices`, SetMaxConcurrent(1, 32)), the 22 kHz files (`SoundBank`, SetDefSubPath aud22 / .22K), the SOUND FX volume at full (AssignWaveVolume(1)). InitAudioManager's CD half is in the CD row below. `-noaudio` / `-nosoundfx` are the infrastructure audit's (command-line options). |
| `AudManager::AssignWaveVolume`, `AudManagerBase::GetMasterSFXVolume`, `AudManager::Log` | ported | `audio/AngelUnits.cpp` `ageMasterVolume`, `Mixer::setBusVolume`, `Mixer::busMaster` | The slider's log(200 s) / log(200) master, multiplied into every wave sound's volume (first audit). |
| `AudManager::AssignWaveBalance` | ported | `Mixer::setBalance` | Through `MixerCTL::AssignWaveBalance` to the Windows wave mixer's balance; OpenMM2 attenuates the opposite channel (inferred, first audit). |
| `AudManagerBase::IsStereo`, `AudManagerBase::SetStereoFlag` | ported | `Mixer::setStereo` | STEREO FX: mono skips every SetPan (first audit). |
| `AudManagerBase::RandomizeNumber` | ported | `audio/AngelRandom.cpp` `randomizeNumber` (both) | A fresh time-seeded Random per draw (first audit). |
| `AudManager::SetNumChannels` | ported | `Mixer` (nothing to do) | Empty in build 3393: SOUND QUALITY's 8 / 16 / 32 channels change nothing. |
| `AudManager::MinInstall` | replaced | - | A minimum install reads speech differently (announcer 1); OpenMM2 always reads the full data (first audit). |
| `AudManager::InitSpeech`, `AudManager::GetSpeechContainerPtr`, `AudManager::GetRaceSpeechPtr`, `AudManager::GetCCSpeechPtr` | ported | `app/RaceScreen.cpp` (`m_announcer`, `m_announcerOk`), `audio/game/Voices.cpp` `Announcer` | `mmPlayer::InitSpeechAudio` builds the speech container only with COMMENTARY on (flag 0x400); OpenMM2 loads the Announcer under the same setting. |
| `AudManagerBase::RestartAudio`, `AudManagerBase::ShutDownAudio` | replaced | `app/Context.cpp` (the wave buses at 0) | `mmPlayerConfig::SetAudio` shuts the wave audio down when SOUND FX goes off and restarts it when it comes back; OpenMM2 mutes the wave buses instead. RestartAudio also makes an AudStreamingMusic that never plays. |
| `AudManager::GetMixerPtr`, `AudManager::GetNum3DHalBufs`, `AudManager::GetIDirectSoundInterfacePtr`, `AudManager::GetMaxBitDepth`, `AudManager::Supports16Bit`, `AudManager::GetSamplesPerSecond`, `AudManager::GetActiveDeviceName`, `AudManager::GetDeviceNames`, `AudManager::GetNumDevices` | replaced | `audio/AudioDevice.cpp`, `app/frontend/PagesOptions.cpp` | Device queries for the AUDIO options page and DirectMusic; OpenMM2 opens SDL's default output (48 kHz stereo float) and shows its name. |
| `AudManager::AssignCDBalance`, `AudManager::AssignCDVolume`, `AudManager::CDIsEnabled`, `AudManager::CDIsPlaying`, `AudManager::CheckCDFile`, `AudManager::EnableCD`, `AudManager::GetCDTrackNum`, `AudManager::GetNumCDTracks`, `AudManager::PlayCDTrack`, `AudManager::SetCDPlayMode`, `AudManager::StopCD` | not needed | - | CD audio. CheckCDFile looks for `cdid.txt` on every CD-ROM drive: the retail disc has it at its root and SafeDisc needs the disc in a drive at launch, so `hasMusicCD` is always set in the retail game and the in-race CD player does nothing (see mmCDPlayer). With music on, MainPhase also plays CD track 2 at the splash and `mmGame::StartMusic` stops it; the disc image has only its data track (inferred: an ISO cannot hold audio tracks, and nothing in the data or readme mentions one). OpenMM2 has no CD audio. |

## audManager, audControl, audObject, audSound (Angel's audio library)

What it is for: Angel's sound library under AudSoundBase: one `audControl`
per AudSoundBase with its `audObject`s (DirectSound buffers, streams, CD,
MIDI, mixer objects), kept by `audManager` in inactive and active lists per
sound type, at most 32 active wave controls (MoveToActive stops the oldest
of the lowest priority no higher than the newcomer's). OpenMM2's software
mixer (`audio/Mixer.cpp`) replaces it; the behaviour the game hears (the
voice limit and stealing, the master volume, the clamps of the volume, pan
and pitch setters, play / stop semantics) was ported in the first audit and
is listed here by name.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `audManager::MoveToActive` | ported | `Mixer::pickSlot` | Stops the first (oldest) active control with the lowest priority that is not above the newcomer's. The Mixer takes the lowest priority, then the oldest, without the 'not above' test: they differ only when every busy voice outranks the newcomer, where MM2's candidate is an uninitialised local. The race's mode sounds have priority 0x17 (see AudSoundBase::SetPriority). |
| `audManager::StopAllSounds` | ported | `Mixer::stopAll` | Every voice of a type stops; the echo buffers stop without being freed (audControl::StopPCEchoBuffers). |
| `audManager::SetVolAllSounds` | ported | `Mixer::computeTargets` (the bus master applied at mix time) | Re-applies every sound's volume times the new master at once, echo buffers included; OpenMM2's voices follow the slider at once, its echoes with their next queued change (first audit's deviation). |
| `audManager::SetMaxConcurrent`, `audControl::SetMaxConcurrent` | ported | `Mixer::kMaxVoices` | 32 wave controls (AudManager::Init); an AudSoundBase's own limit is its handle count. |
| `audControl::SetPriority`, `audControl::GetPriority` | ported | `VoiceParams::priority`, `SoundSlot::load(..., priority)` | Clamped to 0..0xffff; game samples keep 0, the race modes', waypoints' and HUD's sounds 0x17 (`kGameSoundPriority`). |
| `audControl::SetVolPCEchoBuffers`, `audControl::StopPCEchoBuffers`, `audObject::StopPCEchoBuffer` | ported | `Mixer::stopAll`, `EchoEffect::stop` | First audit. |
| `audObject::SetVolPCEchoBuffer` | ported | `EchoEffect` (its queued volume) | Part of SetVolAllSounds; see that row. |
| `audObject::SetVolume`, `audObject::SetPitch`, `audObject::SetPan` | ported | `SoundSlot::setVolume / setPitch / setPan` | Volume times the master clamped to 0..1, pitch multiplier 0..2, pan -1..1 (first audit). |
| `audSound::Play`, `audSound::Stop`, `audSound::SetVolume`, `audSound::SetPitch`, `audSound::SetPan` | ported | `SoundSlot::playLoop / playOnce / stop`, `ageVolumeToGain`, `agePanToMixer`, `clampPitch` | A playing buffer is not restarted; Stop rewinds; (v - 1) x 10000 hundredths of a dB; the far channel at -|pan| x 100 dB; 100..100000 Hz (first audit). |
| `audSound::CreateSoundBufferFromFile`, `audSound::WriteWaveDataToBuffer`, `audSound::OpenSoundFile`, `audSound::CloseSoundFile` | ported | `audio/Wav.cpp` `decodeWav`, `SoundBank::get` | Every retail WAV is 16-bit mono PCM (first audit); the ADPCM path is the infrastructure audit's (not needed). |
| `audSound::CreateDuplicateSoundBuffer`, `audObject::CreateDuplicate` | ported | `Mixer::createEffectVoice`, `SoundSlot` | A duplicate buffer: the echo's (EchoEffect) and the extra handles of a multi-handle AudSoundBase; OpenMM2 shares one decoded buffer. |
| `audObject::Update`, `audControl::Update`, `audManager::Update` | replaced | `Mixer::mix` | Re-applies an object's stored volume, pan and pitch each frame when its auto-update flags are set, and moves 3D objects through `audHead`; AudSoundBase clears those flags (AutoUpdateParamsOff) and MM2's game sounds are 2D (Get2DFlags), so for the game's sounds it only refills streams, which the mixer does itself. |
| `audSound::Update`, `audSound::Init`, `audSound::Destroy`, `audSound::GetStatus`, `audSound::PauseResume` | replaced | `Mixer` | DirectSound buffer bookkeeping and stream refills. |
| `audManager::AddControl`, `audManager::AllocControl`, `audManager::AllocFXNode`, `audManager::AllocFileNode`, `audManager::AllocStreamNode`, `audManager::Create`, `audManager::FreeAndDestroyAllSounds`, `audManager::FreeControl`, `audManager::FreeFXNode`, `audManager::FreeFileNode`, `audManager::FreeStreamNode`, `audManager::GetActiveHead`, `audManager::GetMasterVolume`, `audManager::InitControlStruct`, `audManager::IsEnabled`, `audManager::IsSoundFlags`, `audManager::MoveFromActive`, `audManager::RemoveControl`, `audManager::RestartAudio`, `audManager::SetSoundFlags`, `audManager::ShutDownAudio` | replaced | `audio/Mixer.cpp` | The library's lists, node pools and device state; the Mixer's voice table and handles do this. |
| `audControl::Add`, `audControl::Create`, `audControl::Destroy`, `audControl::FindActiveObjectByHandle`, `audControl::FindObjectByHandle`, `audControl::FreeAndDestroy`, `audControl::GetControlFlags`, `audControl::GetHandle`, `audControl::GetManager`, `audControl::GetPosition`, `audControl::GetVolume`, `audControl::Init`, `audControl::MoveFromActive`, `audControl::MoveToActive`, `audControl::Play`, `audControl::Remove`, `audControl::SetControlFlags`, `audControl::SetHandle`, `audControl::SetPan`, `audControl::SetPitch`, `audControl::SetVolume`, `audControl::Stop` | replaced | `audio/Mixer.cpp`, `audio/game/SoundSlot.cpp` | Per-sound control plumbing (handles, activation, the setters forwarded to the objects). |
| `audObject::CreateEmptyObject`, `audObject::CreateFXControl`, `audObject::CreateFromFile`, `audObject::CreateStreamControl`, `audObject::CreateStreamFromFile`, `audObject::Destroy`, `audObject::Get3D`, `audObject::GetBitsPerSample`, `audObject::GetBuffer`, `audObject::GetFile`, `audObject::GetFlags`, `audObject::GetHandle`, `audObject::GetManager`, `audObject::GetNChannels`, `audObject::GetPriority`, `audObject::GetSamplesPerSec`, `audObject::GetStatus`, `audObject::GetStreamControl`, `audObject::GetTotalSize`, `audObject::GetType`, `audObject::GetVolume`, `audObject::Init`, `audObject::PauseResume`, `audObject::Play`, `audObject::RewindFile`, `audObject::Set3DPosition`, `audObject::SetControl`, `audObject::SetFlags`, `audObject::SetHandle`, `audObject::Stop` | replaced | `audio/Mixer.cpp`, `audio/game/SoundSlot.cpp` | Object creation by type and the accessors; Set3DPosition is the unused 3D path. |
| `audHead::GetHandle`, `audHead::GetHeadVolumePanPitch` | not needed | - | The library's own 3D listener model for 3D objects; MM2's sounds are 2D and positioned by Aud3DObject instead. |
| `audSoundBuffer::BytesAdded`, `audSoundBuffer::BytesRemoved`, `audSoundBuffer::Create`, `audSoundBuffer::Destroy`, `audSoundBuffer::GetAdpcmState`, `audSoundBuffer::GetDataPtr`, `audSoundBuffer::GetIndex1`, `audSoundBuffer::GetIndex2`, `audSoundBuffer::GetSize`, `audSoundBuffer::GetType`, `audSoundBuffer::Init`, `audSoundBuffer::SetAdpcmState`, `audSoundBuffer::SetDataPtr`, `audSoundBuffer::SetIndexes`, `audSoundBuffer::SetSize`, `audSoundBuffer::Unused`, `audSoundHeap::Create`, `audSoundHeap::CreateNodeList`, `audSoundHeap::Destroy`, `audSoundHeap::GetSoundBufferList`, `audSoundHeap::ReserveBuffer`, `audMemMgr::audAlloc`, `audMemObj::audMemObjFree` | replaced | `audio/Wav.h` `SoundBuffer` (shared, decoded once) | Buffer and memory pools. |
| `audStream::CloseStream`, `audStream::Create`, `audStream::Destroy`, `audStream::GetBufferSize`, `audStream::GetCurrentCursor`, `audStream::Init`, `audStream::IsFinished`, `audStream::SetBufferSize`, `audStream::Update` | replaced | `SoundBank`, `SoundSlot` (lines decoded whole) | Streamed playback of the announcer's lines from disc. |
| `audList::Count`, `audList::FindObject`, `audList::GetNext`, `audList::GetPrev`, `audList::Init`, `audList::LinkNext`, `audList::LinkPrev`, `audList::SetNext`, `audList::SetPrev`, `audList::Unlink` | replaced | standard containers | Intrusive lists. |
| `audFileSystem::Create`, `audFileSystem::Destroy`, `audFileSystem::FileClose`, `audFileSystem::FileOpenRead`, `audFileSystem::FileRead`, `audFileSystem::FileSeek`, `audFileSystem::GetFileSize`, `audFileSystem::GetName`, `audFileSystem::GetPath`, `audFileSystem::Init`, `audFileSystem::IsOpen`, `audFileSystem::IsPersistent` | replaced | `vfs/` | File access of the library. |
| `audFX::EnablePCEcho` | ported | `EchoEffect::enable` | First audit. |
| `audFX::Create`, `audFX::Destroy` | replaced | `SoundSlot::enableEcho` (the EchoEffect) | The effect node. |
| `audMixer::GetStatus`, `audMixer::Play`, `audMixer::SetPan`, `audMixer::SetPitch`, `audMixer::SetVolume`, `audMixer::Stop`, `audMixer::Update` | not needed | - | A stub object type: every method is empty or returns a constant. |
| `audMIDI::Destroy`, `audMIDI::GetStatus`, `audMIDI::Init`, `audMIDI::PauseResume`, `audMIDI::Play`, `audMIDI::SetPan`, `audMIDI::SetPitch`, `audMIDI::SetVolume`, `audMIDI::Stop`, `audMIDI::Update`, `AudMidi::MidiUpdate`, `AudMidi::MidiZeroPointers`, `AudMidi::~AudMidi`, `AudMidi::`scalar_deleting_destructor'` | not needed | - | MIDI playback; nothing constructs an AudMidi (its constructor has no caller), so the manager's MIDI pointer stays null. MM2's music is DirectMusic. |
| `audRedbook::Destroy`, `audRedbook::GetStatus`, `audRedbook::PauseResume`, `audRedbook::Play`, `audRedbook::SetPan`, `audRedbook::SetPitch`, `audRedbook::SetVolume`, `audRedbook::Stop`, `audRedbook::Update`, `audCD::GetStatus`, `audCD::PauseResume`, `audCD::Play`, `audCD::SetPan`, `audCD::SetPitch`, `audCD::SetVolume`, `audCD::Stop`, `audCD::Update`, `audCDObject::GetCDPosition`, `audCDObject::GetFramesBetween`, `audCDObject::GetTrackInfo`, `audCDObject::Init`, `audCDObject::SetCDPosition`, `CDMan::CDMan`, `CDMan::GetNumTracks`, `CDMan::GetPosition`, `CDMan::Init`, `CDMan::PlayTrack`, `CDMan::Stop`, `CDMan::WindowProc`, `CDMan::~CDMan`, `CDMan::`scalar_deleting_destructor'` | not needed | - | CD audio through MCI: see AudManager's CD row and mmCDPlayer. |

## DirSnd, mmDirSnd, MixerCTL (DirectSound and the Windows mixer)

Opening DirectSound, choosing and rating devices, the primary buffer
format, and the Windows mixer's wave / CD balance. Replaced by the platform
layer's SDL audio output (`audio/AudioDevice.cpp`) and the Mixer.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `DirSnd::ClearDSDeviceList`, `DirSnd::CreatePrimaryInterfaceAndBuffer`, `DirSnd::DirSnd`, `DirSnd::EnumDSDevices`, `DirSnd::GetDeviceNames`, `DirSnd::Init3DListener`, `DirSnd::InitPrimarySoundBuffer`, `DirSnd::IsDSDeviceInList`, `DirSnd::SetDeviceRating`, `DirSnd::SetPrimaryBufferFormat`, `DirSnd::TranslateDSError`, `DirSnd::~DirSnd`, `mmDirSnd::Init`, `mmDirSnd::InitPrimarySoundBuffer`, `mmDirSnd::mmDirSnd` | replaced | `audio/AudioDevice.cpp` | Device enumeration and the primary buffer (22 kHz in MM2; OpenMM2 mixes at 48 kHz and resamples each voice). |
| `MixerCTL::AssignWaveBalance` | ported | `Mixer::setBalance` | Inferred (first audit). |
| `MixerCTL::AssignCDBalance`, `MixerCTL::AssignMixerBalance`, `MixerCTL::GetErrorMessage`, `MixerCTL::SetDeviceNum` | replaced | `Mixer::setBalance` | The Windows mixer lines; the CD line has nothing to play (no CD audio). |

## AudSoundBase, Aud3DSampleWrapper (one game sample)

What it is for: every game sound is an AudSoundBase (one control with one
or more handles of the same sample), loaded from `aud/aud22/<name>.22K`;
the positioned objects share theirs through the 3D manager's sample pool
(`Aud3DObjectManager::AllocateSample`, one Aud3DSampleWrapper per sample
with a copy per 3D slot, assigned to an object while it holds that slot).
OpenMM2's `audio/game/SoundSlot` is one sample with the same play, stop and
setter semantics and the echo; every object owns its SoundSlots instead of
borrowing the pool's copy for its slot, which plays the same.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `AudSoundBase::AudSoundBase`, `AudSoundBase::Load`, `AudSoundBase::PlayLoop`, `AudSoundBase::PlayOnce`, `AudSoundBase::Stop`, `AudSoundBase::IsPlaying`, `AudSoundBase::SetVolume`, `AudSoundBase::SetFrequency`, `AudSoundBase::SetPan` | ported | `audio/game/SoundSlot.cpp` | First audit (the -1 'leave as is' arguments, the audObject clamps, the echo's queued commands). |
| `AudSoundBase::SetEffect`, `AudSoundBase::DisableEffect`, `AudSoundBase::SetEchoEffect`, `AudSoundBase::DisableEchoEffect`, `AudSoundBase::SetDelayTime`, `AudSoundBase::SetEchoAttenuation`, `AudSoundBase::SetEchoFrequency`, `AudSoundBase::Update`, `AudSoundBase::UpdateEcho` | ported | `SoundSlot::enableEcho / disableEcho / setEchoDelay / setEchoAttenuation / setEchoFrequency / updateEcho` | The tunnel echo (first audit's second pass). SetEffect only knows effect 1, the echo. |
| `AudSoundBase::SetPriority` | ported (new) | `SoundSlot.h` `kGameSoundPriority`, `app/RaceScreen.cpp` `playGameSound` | The race modes' sounds (InitGameObjects of every single and multiplayer mode: Startracelow, Startracehigh, Endofracetag, Youlose, Damgelose, Messagenote, Timerwarning), the waypoints' (mmWaypoints::Init / InitStatic: Waypoint, Lastwaypoint) and the HUD's network alert get priority 0x17; OpenMM2 loaded them at 0 like every other sample. |
| `AudSoundBase::ReadyOneShotLayerBuf` | not needed | - | Used by AudImpactData::PlaySample and UpdateTireWobble only when the sample's effect has bit 4 (a layered one-shot), which nothing sets (SetEffect is only called with 1). |
| `AudSoundBase::SetPlayPosition` | not needed | - | Empty in build 3393 (called by vehCarAudio::PlayHorn, vehSemiCarAudio::UpdateReverse and the menu sounds). |
| `AudSoundBase::AutoUpdateParamsOff`, `AudSoundBase::CreateDuplicateObject`, `AudSoundBase::DeallocateStatics`, `AudSoundBase::Get2DFlags`, `AudSoundBase::GetEffect`, `AudSoundBase::GetFreqChange2DFlags`, `AudSoundBase::GetNumSoundHandles`, `AudSoundBase::GetSoft2DFlags`, `AudSoundBase::GetSoundHandleIndex`, `AudSoundBase::SetSoundHandleIndex`, `AudSoundBase::GetSubPathAndExtension`, `AudSoundBase::SetAgeAudioManagerPtr`, `AudSoundBase::SetDefExtension`, `AudSoundBase::SetDefSubPath`, `AudSoundBase::SetExtension`, `AudSoundBase::SetSubPath`, `AudSoundBase::~AudSoundBase`, `AudSoundBase::`scalar_deleting_destructor'` | replaced | `audio/SoundBank.cpp`, `SoundSlot` | Buffer flags, handles and the file's sub-path and extension (aud22 / .22K for effects, aud11 / .11K for speech: `SoundBank`). |
| `Aud3DSampleWrapper::Aud3DSampleWrapper`, `Aud3DSampleWrapper::GetSoundPtr`, `Aud3DSampleWrapper::SetSampleName`, `Aud3DSampleWrapper::~Aud3DSampleWrapper` | replaced | `SoundSlot` (owned by each object) | The pool entry: a copy of the sample per 3D slot, loaded at volume 0 (SoundSlot also starts at 0). |

## EchoEffect (the tunnel echo's duplicate buffer)

A delayed duplicate of a sample that replays its play, stop, volume and
frequency changes; ported in the first audit's second pass
(`audio/EchoEffect.cpp`).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `EchoEffect::EchoEffect`, `EchoEffect::~EchoEffect`, `EchoEffect::Enable`, `EchoEffect::Disable`, `EchoEffect::SetDelayTime`, `EchoEffect::Update`, `EchoEffect::QueuePlay`, `EchoEffect::QueueVolume`, `EchoEffect::QueueFrequency`, `EchoEffect::QueueStop`, `EchoEffect::UpdatePlay`, `EchoEffect::UpdateVolume`, `EchoEffect::UpdatePitch`, `EchoEffect::UpdateStop`, `EchoEffect::SetVolume`, `EchoEffect::SetFrequency`, `EchoEffect::CalculatePan`, `EchoEffect::Stop` | ported | `audio/EchoEffect.cpp` `EchoEffect` | First audit. The pieces the decompiler split off SetFrequency are its DirectSound error messages. |

## Aud3DObject, Aud3DObjectManager (positioned sounds and their slots)

What it is for: MM2 positions its sounds itself. Every positioned object
(other cars, police, traffic, city emitters, world objects, creatures) is an
Aud3DObject measuring its distance to the listener (the camera:
`mmPlayer::Init` sets the manager's left head to the camera's matrix) and
asking the manager for one of four slots (`mmPlayer::Init` makes
`Aud3DObjectManager(4)`; the player's car holds one for good). Only a slot
holder has its samples and is updated (`Aud3DObjectManager::Update`, from
`mmGame::Update` while not paused). Spawning: objects are made by their
owners (cars, traffic, the city container, gizmos); removal: losing the slot
(farther than its maximum distance, or pushed out by a closer or more
important object) stops its loops, and its owner's Reset takes the slot away
and forgets the distance history. OpenMM2: `audio/game/Object3D`
(`Audio3D`, `Object3DManager`, `SlotHolder`), first audit; the Reset and
the full distance reset are new.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `Aud3DObject::Aud3DObject`, `Aud3DObject::SetDropOffs`, `Aud3DObject::SetPositionPtr`, `Aud3DObject::CalcDistToClosestHeads2`, `Aud3DObject::CalcDistToClosestHead2`, `Aud3DObject::CalcDistToHead2`, `Aud3DObject::CalcPseudoDistToClosestHead`, `Aud3DObject::CalcPseudoDistToHead`, `Aud3DObject::GetDistToClosestHead2`, `Aud3DObject::CalcPercentToMaxDist2`, `Aud3DObject::CalculateAttenuation`, `Aud3DObject::CalculatePan`, `Aud3DObject::CalcSinglePlayerPan`, `Aud3DObject::CalculateDoppler`, `Aud3DObject::WithinMaxDistance`, `Aud3DObject::PastMaxDistance`, `Aud3DObject::GetClosestPositionPtr`, `Aud3DObject::SetClosestPositionPtr`, `Aud3DObject::ReadVectorPoints` | ported | `audio/game/Object3D.cpp` `Audio3D`, `AmbientObject::nearestPoint` | First audit; with one listener (no screen splits) the closest-head variants are the single head. |
| `Aud3DObject::Reset` | ported (new) | `Audio3D::reset`; `OpponentCarAudio::reset`, `AmbientCarAudio::reset`, `AmbientObject::reset`, `CableCarAudio::reset` | A positioned slot holder gives its slot up (RemoveFrom3DMgr: UnAssignSounds), d^2 back to 1000000, the pseudo distance and its change to 0, the previous one to -1 (no doppler on the next update), attenuation 0, pan 0, doppler 1. A non-positioned object (the player's car) only gets d^2 0.01. Was only the previous pseudo distance. Tests `ResetForgetsTheDistanceHistory` and the reset tests below. |
| `Aud3DObject::Update`, `Aud3DObject::UpdateNonVirtual`, `Aud3DObject::AddTo3DMgr`, `Aud3DObject::RemoveFrom3DMgr`, `Aud3DObject::GetPriority`, `Aud3DObject::~Aud3DObject` | ported | `SlotHolder::acquireSlot / releaseSlot`, the clients' `slotPriority` | Update asks only while the 3D flag (+0x25, Process3D) is set; UpdateNonVirtual (the creatures' Play) whatever it is. Non-positioned objects get +1000000 priority. |
| `Aud3DObject::Set3D` | ported | `PlayerCarAudio` (not positioned), `Object3DManager::kSinglePlayerSlots` | Set3D(false) is the player's car: d^2 0.01, a slot at once and kept (its priority + 1000000), Set3D's SetNon3DParams. |
| `Aud3DObject::AssignSounds`, `Aud3DObject::UnAssignSounds`, `Aud3DObject::UpdateAudio`, `Aud3DObject::Set3DParams`, `Aud3DObject::SetNon3DParams` | ported | the classes' own versions | Empty base versions; every class overrides them (rows below). |
| `Aud3DObject::CalcMultiPlayerPan`, `Aud3DObjectManager::SetNumScreenSplits`, `Aud3DObjectManager::GetRightHeadPtrPtr` | not needed | - | Split-screen listeners: `mmPlayer::Init` sets 0 splits, so CalculatePan always takes the single-player pan. |
| `Aud3DObjectManager::GetLeftHeadPtrPtr`, `Aud3DObjectManager::SetLeftHeadPtr` | ported | `app/RaceScreen.cpp` (the camera's transform is the listener) | The camera's matrix (mmPlayer::Init). |
| `Aud3DObjectManager::Aud3DObjectManager`, `Aud3DObjectManager::Add`, `Aud3DObjectManager::FindUnusedSlot`, `Aud3DObjectManager::FindGreatestDistance`, `Aud3DObjectManager::Remove`, `Aud3DObjectManager::EchoOn`, `Aud3DObjectManager::EchoOff` | ported | `Object3DManager` (`add`, `remove`, `echoOn`, `echoOff`, `setTunnel`) | First audit. |
| `Aud3DObjectManager::Update` | ported | the objects' own `update` (each runs UpdateAudio while it holds a slot), `CityAmbience::update` | The city container with speed 0, then every slot holder's UpdateAudio while the 3D flag is set, then the cop-voice timer, which nothing starts (QueueInCopVoice is empty). |
| `Aud3DObjectManager::Process3D` | replaced | leaving the race (`app/RaceScreen.cpp` `leaveRace`) | Process3D(false) at the results popup (`mmPopup::ShowResults`) removes every object from its slot and keeps them out, and stops the rain; mmGame::Reset turns it back on. OpenMM2 shows the results as a menu page (the session record's deviation), so leaving the race stops every sound. |
| `Aud3DObjectManager::InitAmbObjContainer` | ported | `CityAmbience::load` from `app/RaceScreen.cpp` | `londonambientcontainer` / `sfambientcontainer`, only with CITY SOUNDS on (flag 0x800), as OpenMM2 loads it. |
| `Aud3DObjectManager::AllocateSample`, `Aud3DObjectManager::GetSample` | replaced | `SoundSlot::load` per object | The sample pool (see Aud3DSampleWrapper). |
| `Aud3DObjectManager::QueueInCopVoice`, `Aud3DObjectManager::PlayCopVoice` | not needed | - | Empty in build 3393 (a damage out calls QueueInCopVoice). |
| `Aud3DObjectManager::~Aud3DObjectManager`, `Aud3DObjectManager::`scalar_deleting_destructor'` | replaced | `Object3DManager` | Teardown. |

## Aud3DAmbientObject, Aud3DAmbObjContainer, mmBridgeAudio, mmRainAudio, mmAmbientAudio

What it is for: Aud3DAmbientObject plays one `aud/ambient/<name>.csv` set
at a position: the city's emitters (one per set the container file lists,
made by `Aud3DObjectManager::InitAmbObjContainer` when the race starts, fixed
at their file's points), and the moving world objects' sounds: drawbridges
(`mmBridgeAudio`, "drawbridge", made by gizBridge), the ferry (gizFerry,
"ferry"), trains (aiSubwayAudio, "subwaycar", aiSubway and gizTrain) and
cable cars (aiCableCarAudio, its own class). Rain (`mmRainAudio`) is made
by `mmPlayer::Init` in rain. OpenMM2: `audio/game/Ambience` (first audit's
second pass). The world objects themselves are being ported by the
world-objects agent; their API is `AmbientObject` / `BridgeAudio` /
`SubwayAudio` / `CableCarAudio`: load once, then each frame `setPosition`
and `update(listener, speed, dt, tunnel)`, `activate` / `deactivate`, and the
new `reset` from the owner's Reset (gizBridge, gizFerry, gizTrain,
aiSubway, aiCableCar), all with the race's `Object3DManager`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `Aud3DAmbientObject::Aud3DAmbientObject`, `Aud3DAmbientObject::Init`, `Aud3DAmbientObject::Load`, `Aud3DAmbientObject::ReadSoundData`, `Aud3DAmbientObject::SetSoundData`, `Aud3DAmbientObject::Update`, `Aud3DAmbientObject::UpdateAudio`, `Aud3DAmbientObject::UpdateSoundData`, `Aud3DAmbientObject::UpdateDoppler`, `Aud3DAmbientObject::UpdateLoop`, `Aud3DAmbientObject::UpdateOneShot`, `Aud3DAmbientObject::PendOneShot`, `Aud3DAmbientObject::PlayOneShot`, `Aud3DAmbientObject::ActivateSound`, `Aud3DAmbientObject::DeactivateSound`, `Aud3DAmbientObject::EchoOn`, `Aud3DAmbientObject::EchoOff`, `Aud3DAmbientObject::UpdateEcho`, `Aud3DAmbientObject::AssignSounds`, `Aud3DAmbientObject::UnAssignSounds`, `Aud3DAmbientObject::~Aud3DAmbientObject` | ported | `audio/game/Ambience.cpp` `AmbientObject`, `audio/game/AudioTables.cpp` `parseAmbientSoundSet` | First audit (both overloads of the paired methods). |
| `Aud3DAmbientObject::Reset` | ported (new) | `AmbientObject::reset` | Aud3DObject::Reset (the owners' Reset): the slot goes (every sample stops), the distance history is forgotten; the active flags stay. Test `AmbientObjectAndCableCarResetGiveUpTheirSlot`. |
| `Aud3DAmbObjContainer::Aud3DAmbObjContainer`, `Aud3DAmbObjContainer::Init`, `Aud3DAmbObjContainer::FileValid`, `Aud3DAmbObjContainer::CreateAmbientObject`, `Aud3DAmbObjContainer::Update`, `Aud3DAmbObjContainer::~Aud3DAmbObjContainer` | ported | `CityAmbience::load / update` | First audit; a set whose file is missing is skipped (FileValid). |
| `mmBridgeAudio::mmBridgeAudio`, `mmBridgeAudio::Activate`, `mmBridgeAudio::Deactivate`, `mmBridgeAudio::~mmBridgeAudio` | ported | `BridgeAudio` | -1 is samples 0 and 1 (first audit). Its owner gizBridge is the world-objects agent's. |
| `mmRainAudio::mmRainAudio`, `mmRainAudio::Update`, `mmRainAudio::SetInterior`, `mmRainAudio::ShelterOn`, `mmRainAudio::ShelterOff`, `mmRainAudio::~mmRainAudio` | ported | `RainAudio` | First audit. Update stops the loop while the 3D flag is off (Process3D: the results), which leaving the race covers. |
| `mmAmbientAudio::Update`, `mmAmbientAudio::~mmAmbientAudio` | not needed | - | A city 'walla' loop; nothing calls its constructor (mmGame::Update's call finds a null pointer). |

## vehCarAudioContainer, vehCarAudio, vehSemiCarAudio, vehPoliceCarAudio, vehNitroCarAudio

What it is for: every vehCar gets a vehCarAudioContainer (vehCar::InitAudio):
the player's car in mode 2 (not positioned), network players' cars in mode 0
(positioned, with a horn) and opponents and police in mode 1 (positioned,
no horn). By `shared/vehtypes.csv` it holds a vehSemiCarAudio (semis and
buses), a vehPoliceCarAudio (police) or a vehCarAudio (everything else; a
vehNitroCarAudio when the file says "Always nitro"). vehCar::PostUpdate
updates it every frame, vehCar::Reset resets it, mmGame::UpdateHorn and
mmNetObject drive the horn and siren, aiPoliceOfficer the AI sirens and the
explosion. OpenMM2: `audio/game/CarAudio` (`PlayerCarAudio` for mode 2,
`OpponentCarAudio` for modes 0 and 1), fed by `app/RaceScreen.cpp`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `vehCarAudioContainer::vehCarAudioContainer`, `vehCarAudioContainer::Init`, `vehCarAudioContainer::InitSemi`, `vehCarAudioContainer::InitPolice`, `vehCarAudioContainer::IsSemiOrBus`, `vehCarAudioContainer::IsPolice`, `vehCarAudioContainer::IsPlayer`, `vehCarAudioContainer::RegisterTypes`, `vehCarAudioContainer::RegisterSemiNames`, `vehCarAudioContainer::RegisterPoliceNames`, `vehCarAudioContainer::SetSirenCSVName`, `vehCarAudioContainer::~vehCarAudioContainer` | ported | `PlayerCarAudio::load`, `OpponentCarAudio::load`, `parseVehicleTypes` | First audit. |
| `vehCarAudioContainer::InitNitro` | not needed | - | Only when vehtypes.csv says "Always nitro" (FALSE in retail); see vehNitroCarAudio. |
| `vehCarAudioContainer::Update`, `vehCarAudio::Update` | ported | `PlayerCarAudio::update`, `OpponentCarAudio::update` (each frame from the race) | vehCar::PostUpdate: the car's Update (position pointer, then Aud3DObject::Update: the slot request); the sound update itself is the 3D manager's UpdateAudio. |
| `vehCarAudioContainer::Reset`, `vehCarAudio::Reset`, `vehSemiCarAudio::Reset`, `vehPoliceCarAudio::Reset` | ported (new) | `OpponentCarAudio::reset`; `app/RaceScreen.cpp` (an opponent's reset callback, the restart, the cops' restart) | All are Aud3DObject::Reset (vehCar::Reset, aiPoliceOfficer::Reset): a positioned car gives up its slot (the loops stop, impacts and an explosion play out) and its distance history; the player's car (not positioned) has nothing audible to reset. Test `OpponentCarResetGivesUpItsSlot`. |
| `vehCarAudioContainer::PlayHorn`, `vehCarAudioContainer::StopHorn`, `vehCarAudioContainer::StartSiren`, `vehCarAudioContainer::StopSiren`, `vehCarAudio::PlayHorn`, `vehCarAudio::StopHorn` | ported | `PlayerCarAudio::updateHorn`, `OpponentCarAudio::update` (the network horn) | mmGame::UpdateHorn: the horn button toggles a police car's siren (StartSiren with 0) instead; mmNetObject::PositionUpdate the network cars'; StopHorn only while latched (first audit). |
| `vehCarAudioContainer::SilenceEngine` | ported | `PlayerCarAudio::silenceEngine` | First audit. |
| `vehCarAudioContainer::IsAirBorne`, `vehCarAudio::IsAirBorne` | ported | `PlayerCarAudio::airborne` | mmGame::UpdateDMusic's big-air motif. |
| `vehCarAudioContainer::GetAudImpactPtr`, `vehCarAudio::GetAudImpactPtr` | ported | `CarAudioInputs::impacts` (the cars' impact callbacks) | vehCarDamage::ApplyImpact plays the car's AudImpact; null while a positioned car has no slot. |
| `vehCarAudioContainer::GetPoliceCarAudioPtr` | ported | `OpponentCarAudio` (its `SirenPlayer`) | aiPoliceOfficer's access to the siren. |
| `vehCarAudioContainer::RemoveNetVehicleAudio`, `vehCarAudio::RemoveFromManager`, `vehSemiCarAudio::RemoveFromManager`, `vehPoliceCarAudio::RemoveFromManager` | ported | `SlotHolder::releaseSlot` (a car's audio is destroyed with it) | mmNetObject::Clear / ReInit: a network car that leaves gives up its slot. |
| `vehCarAudioContainer::InitStatics`, `vehCarAudioContainer::DeallocateStatics`, `vehCarAudio::InitStatics`, `vehCarAudio::DeallocateStatics` | replaced | `loadTable` (tables read per load) | The shared tables' bookkeeping. |
| `vehCarAudio::vehCarAudio`, `vehCarAudio::Init`, `vehCarAudio::Load`, `vehCarAudio::LoadImpacts`, `vehCarAudio::SetNon3DParams`, `vehCarAudio::Set3DParams`, `vehCarAudio::AssignSounds`, `vehCarAudio::UnAssignSounds`, `vehCarAudio::UpdateAudio`, `vehCarAudio::UpdateAudio3D`, `vehCarAudio::UpdateAudioNon3D`, `vehCarAudio::UpdateGear`, `vehCarAudio::EchoOn`, `vehCarAudio::EchoOff`, `vehCarAudio::UpdateEcho`, `vehCarAudio::GetCurrentGear`, `vehCarAudio::GetSpeed`, `vehCarAudio::IsBrakeing`, `vehCarAudio::~vehCarAudio` | ported | `PlayerCarAudio`, `OpponentCarAudio`, `CarAudioInputs` | First audit (both UpdateAudio3D overloads). The gear, speed and brake getters are the race's inputs. |
| `vehCarAudio::SetMinAmpSpeed` | not needed | - | Sets the slope of UpdateAudio3D's 25 m 'amplification' (1 + 0.01 x a speed field); nothing writes that field after Init's 0, so the factor is always 1 (first audit). |
| `vehSemiCarAudio::vehSemiCarAudio`, `vehSemiCarAudio::Init`, `vehSemiCarAudio::Load`, `vehSemiCarAudio::SetNon3DParams`, `vehSemiCarAudio::Set3DParams`, `vehSemiCarAudio::AssignSounds`, `vehSemiCarAudio::UnAssignSounds`, `vehSemiCarAudio::Update`, `vehSemiCarAudio::UpdateAudio`, `vehSemiCarAudio::UpdateAudio3D`, `vehSemiCarAudio::UpdateAudioNon3D`, `vehSemiCarAudio::UpdateReverse`, `vehSemiCarAudio::UpdateAirBlow`, `vehSemiCarAudio::EchoOn`, `vehSemiCarAudio::EchoOff`, `vehSemiCarAudio::UpdateEcho`, `vehSemiCarAudio::~vehSemiCarAudio` | ported | `PlayerCarAudio`, `OpponentCarAudio` (`m_semi`, the beeper and air brake) | First audit. |
| `vehPoliceCarAudio::vehPoliceCarAudio`, `vehPoliceCarAudio::Init`, `vehPoliceCarAudio::Load`, `vehPoliceCarAudio::ReadSirenData`, `vehPoliceCarAudio::ReadSirenPlayInfo`, `vehPoliceCarAudio::DeallocateSirenPlayInfo`, `vehPoliceCarAudio::SetNon3DParams`, `vehPoliceCarAudio::Set3DParams`, `vehPoliceCarAudio::AssignSounds`, `vehPoliceCarAudio::UnAssignSounds`, `vehPoliceCarAudio::Update`, `vehPoliceCarAudio::UpdateAudio`, `vehPoliceCarAudio::UpdateAudio3D`, `vehPoliceCarAudio::UpdateAudioNon3D`, `vehPoliceCarAudio::StartSiren`, `vehPoliceCarAudio::StopSiren`, `vehPoliceCarAudio::FluctuateSiren`, `vehPoliceCarAudio::DamageSiren`, `vehPoliceCarAudio::UpdateSiren`, `vehPoliceCarAudio::PlayExplosion`, `vehPoliceCarAudio::UpdateExplosion`, `vehPoliceCarAudio::ExplosionIsPlaying`, `vehPoliceCarAudio::GetNumCopsPursuingPlayer`, `vehPoliceCarAudio::EchoOn`, `vehPoliceCarAudio::EchoOff`, `vehPoliceCarAudio::UpdateEcho`, `vehPoliceCarAudio::~vehPoliceCarAudio` | ported | `SirenPlayer`, `PlayerCarAudio`, `OpponentCarAudio`, `parseSirenTable` | First audit (both overloads of ReadSirenData, UpdateAudio3D and UpdateSiren). SetNon3DParams: the player's siren samples at their table volumes, the explosion at 1. |
| `vehNitroCarAudio::vehNitroCarAudio`, `vehNitroCarAudio::Init`, `vehNitroCarAudio::Load`, `vehNitroCarAudio::SetNon3DParams`, `vehNitroCarAudio::Set3DParams`, `vehNitroCarAudio::AssignSounds`, `vehNitroCarAudio::UnAssignSounds`, `vehNitroCarAudio::Update`, `vehNitroCarAudio::UpdateAudio`, `vehNitroCarAudio::UpdateAudio3D`, `vehNitroCarAudio::UpdateAudioNon3D`, `vehNitroCarAudio::Reset`, `vehNitroCarAudio::RemoveFromManager`, `vehNitroCarAudio::EchoOn`, `vehNitroCarAudio::EchoOff`, `vehNitroCarAudio::UpdateEcho`, `vehNitroCarAudio::~vehNitroCarAudio` | not needed | - | Made only with "Always nitro" (FALSE in retail), and even then it is a vehCarAudio plus a `<car>_nitro` sample that never plays: `vehCarAudioContainer::PlayNitro` has no caller. The first audit's open row is closed. |

## vehEngineAudio, vehEngineSampleWrapper (engines)

Every engine row of the car's table is a looping sample whose volume and
pitch follow the RPM. OpenMM2: `EngineSound` (first audit); the old
"Volume Divisor" table layout is new.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `vehEngineAudio::vehEngineAudio`, `vehEngineAudio::Load`, `vehEngineAudio::AssignSounds`, `vehEngineAudio::UnAssignSounds`, `vehEngineAudio::UpdateRPM`, `vehEngineAudio::Silence`, `vehEngineAudio::Stop`, `vehEngineAudio::SetPan`, `vehEngineAudio::EchoOn`, `vehEngineAudio::EchoOff`, `vehEngineAudio::~vehEngineAudio` | ported | `EngineSound` (`load`, `update`, `update3D`, `silence`, `stop`, `echoOn`, `echoOff`) | First audit (both UpdateRPM overloads). SetPan sets the player's car's pan (+0x7c, 0 outside split screens): centred. |
| `vehEngineSampleWrapper::vehEngineSampleWrapper`, `vehEngineSampleWrapper::ParseCSVBuffer`, `vehEngineSampleWrapper::CalculateVolume`, `vehEngineSampleWrapper::CalculatePitch`, `vehEngineSampleWrapper::UpdateRPM`, `vehEngineSampleWrapper::Silence`, `vehEngineSampleWrapper::Stop`, `vehEngineSampleWrapper::SetPan`, `vehEngineSampleWrapper::SetSoundPtr`, `vehEngineSampleWrapper::EchoOn`, `vehEngineSampleWrapper::EchoOff`, `vehEngineSampleWrapper::~vehEngineSampleWrapper` | ported | `EngineSound::evaluate`, `parseCarAudio` | First audit (both UpdateRPM overloads). |
| `vehEngineSampleWrapper::ParseCSVBufferOld`, `vehEngineSampleWrapper::CalculateVolumeOld` | ported (new) | `parseCarAudio` (`EngineSampleDef::oldLayout`), `EngineSound::evaluate` | The "Volume Divisor" layout: rpm / divisor below the cut RPM, divisor / rpm from it, clamped to min, then max; the pitch range is never set, so the pitch is the max pitch above 0 RPM (inferred). OpenMM2 rejected such a table; no table MM2 loads has it. Test `OldEngineTableLayout`. |

## vehSurfaceAudio, vehSurfaceAudioData (tyres on surfaces, skids, thumps)

The rolling sound of the surface under the car, skids, suspension thumps
and the tyre wobble. OpenMM2: `SurfaceSounds` (first audit).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `vehSurfaceAudio::vehSurfaceAudio`, `vehSurfaceAudio::LoadCSV`, `vehSurfaceAudio::LoadSuspension`, `vehSurfaceAudio::LoadTireWobble`, `vehSurfaceAudio::AssignSounds`, `vehSurfaceAudio::UnAssignSounds`, `vehSurfaceAudio::SetPositionPtr`, `vehSurfaceAudio::SetDamagePtr`, `vehSurfaceAudio::SetWheelPointers`, `vehSurfaceAudio::SetPan`, `vehSurfaceAudio::Update`, `vehSurfaceAudio::SurfaceChanged`, `vehSurfaceAudio::UpdateSurface`, `vehSurfaceAudio::UpdateSkid`, `vehSurfaceAudio::UpdateSuspension`, `vehSurfaceAudio::UpdateTireWobble`, `vehSurfaceAudio::UpdateAir`, `vehSurfaceAudio::IsBrakeing`, `vehSurfaceAudio::StopSurface`, `vehSurfaceAudio::StopSkid`, `vehSurfaceAudio::EchoOn`, `vehSurfaceAudio::EchoOff`, `vehSurfaceAudio::UpdateEcho`, `vehSurfaceAudio::~vehSurfaceAudio` | ported | `SurfaceSounds`, `CarAudioInputs` (wheels, damage, position) | First audit (both overloads of the updates). The piece the decompiler split off UpdateEcho is the current entry's skids-then-surface echo update, as ported. |
| `vehSurfaceAudioData::vehSurfaceAudioData`, `vehSurfaceAudioData::ParseCSVBuffer`, `vehSurfaceAudioData::AssignSounds`, `vehSurfaceAudioData::UnAssignSounds`, `vehSurfaceAudioData::GetSurfaceSoundPtr`, `vehSurfaceAudioData::SetPan`, `vehSurfaceAudioData::UpdateSurface`, `vehSurfaceAudioData::UpdateSkid`, `vehSurfaceAudioData::SkidPlaying`, `vehSurfaceAudioData::StopSurface`, `vehSurfaceAudioData::StopSkid`, `vehSurfaceAudioData::EchoOn`, `vehSurfaceAudioData::EchoOff`, `vehSurfaceAudioData::~vehSurfaceAudioData` | ported | `SurfaceSounds` (`Entry`), `parseSurfaceTable` | First audit. |

## AudImpact, AudImpactData (collision sounds)

Per-banger impact samples chosen by what was hit and how hard
(`default_impacts.csv`). OpenMM2: `ImpactSounds` (first audit).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `AudImpact::AudImpact`, `AudImpact::Load`, `AudImpact::ReadCSV`, `AudImpact::AddToHash`, `AudImpact::AssignSounds`, `AudImpact::UnAssignSounds`, `AudImpact::Play`, `AudImpact::GetAudImpactDataPtr`, `AudImpact::UpdateAttenuation`, `AudImpact::Set3D`, `AudImpact::SetPan`, `AudImpact::~AudImpact` | ported | `ImpactSounds`, `parseImpactTable`, `loadTable` | First audit (both overloads of the constructor and Play). SetPan for a non-positioned car (Set3D(false)) pans only each banger's first sample (AudImpactData::SetPan never advances): the player's pan is 0, so nothing changes. The pieces split off GetAudImpactDataPtr are the hash table's static set-up. |
| `AudImpact::Update` | not needed | - | Counts updates into two fields and a global that nothing reads. |
| `AudImpactData::AudImpactData`, `AudImpactData::ReadCSV`, `AudImpactData::AssignSounds`, `AudImpactData::UnAssignSounds`, `AudImpactData::Play`, `AudImpactData::PlaySample`, `AudImpactData::UpdateAttenuation`, `AudImpactData::SetPan`, `AudImpactData::~AudImpactData` | ported | `ImpactSounds::play / updateAttenuation` | First audit (both overloads). |

## aiAmbientVehicleAudio, aiEngineAudio, vehHornAudio, vehHornAudioTiming (traffic)

Every ambient traffic car (aiVehicleSpline) has an aiAmbientVehicleAudio:
an engine loop pitched by speed band, horn patterns, impacts and the
driver's voice (an AudCreature from `<type>_ambcarvoice<c>` or the default
file). aiVehicleSpline::Init / Update drive it, aiVehicleSpline::Reset
resets it, aiGoalAvoidPlayer::Reset honks and makes the driver react,
aiVehicleActive's impacts play the impact sounds, horn and reaction.
OpenMM2: `AmbientCarAudio` (first audit), fed by the race; the driver's
voice belongs to the car's audio now (`setVoice`), as in MM2.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `aiAmbientVehicleAudio::aiAmbientVehicleAudio`, `aiAmbientVehicleAudio::Init`, `aiAmbientVehicleAudio::LoadEngine`, `aiAmbientVehicleAudio::LoadHorn`, `aiAmbientVehicleAudio::LoadImpacts`, `aiAmbientVehicleAudio::AssignSounds`, `aiAmbientVehicleAudio::UpdateAudio`, `aiAmbientVehicleAudio::UpdateHorn`, `aiAmbientVehicleAudio::PlayAvoidanceHorn`, `aiAmbientVehicleAudio::PlayImpactHorn`, `aiAmbientVehicleAudio::GetAudImpactPtr`, `aiAmbientVehicleAudio::~aiAmbientVehicleAudio` | ported | `AmbientCarAudio` (`load`, `update`, `honk`, `impact`) | First audit (both UpdateAudio overloads). |
| `aiAmbientVehicleAudio::EchoOn`, `aiAmbientVehicleAudio::EchoOff`, `aiAmbientVehicleAudio::UpdateEcho`, `aiAmbientVehicleAudio::UnAssignSounds` | ported (new) | `AmbientCarAudio::echoOn / echoOff / update / silence` with `setVoice` | The engine and horn were ported; the driver's AudCreature is now included: its lines echo in tunnels with the car, and losing the slot drops its queued lines. Test `AmbientDriverVoiceFollowsItsCar`. |
| `aiAmbientVehicleAudio::PlayAvoidanceReaction`, `aiAmbientVehicleAudio::PlayImpactReaction` | ported (new) | `AmbientCarAudio::avoidReaction / impactReaction` | Only while the car holds its slot (was the race's check); the voice follows the car's attenuation, pan and squared distance inside UpdateAudio, before the impacts'. |
| `aiAmbientVehicleAudio::Reset` | ported (new) | `AmbientCarAudio::reset`; `app/RaceScreen.cpp` (a car back in the pool) | Aud3DObject::Reset and the speed and previous speed (+0x80, +0x84) to 0 (was a full stop that left the speeds and distance history, so a reused car could start with a doppler jump or the crash pitch decay). |
| `aiAmbientVehicleAudio::LoadVoices`, `aiAmbientVehicleAudio::LoadNumVFileChoices`, `aiAmbientVehicleAudio::SetCSVCatString`, `aiAmbientVehicleAudio::UpdateStatics`, `aiAmbientVehicleAudio::UpdateVoices` | ported | `app/RaceScreen.cpp` `ambientVoice`, `updateAmbientAudio` | `<type>_ambcarvoice<c>`, else `default_ambcarvoice<c><n>`, c "_l" in London, "_s" elsewhere, n drawn once per session; every voice and the impact-line clock updated with the player's speed (the clock twice a frame). MM2 keeps one voice per file and 3D slot, handed to the car holding the slot; OpenMM2 gives each car its own, which plays the same except that a car's voice made later starts with empty speed timers (deviation, docs/audio.md). |
| `aiAmbientVehicleAudio::InitStatics`, `aiAmbientVehicleAudio::DeallocateStatics` | replaced | `AmbientCarAudio`, `loadTable` | The shared containers. |
| `aiEngineAudio::aiEngineAudio`, `aiEngineAudio::Load`, `aiEngineAudio::ReadCSV`, `aiEngineAudio::AddToHash`, `aiEngineAudio::AssignSounds`, `aiEngineAudio::UnAssignSounds`, `aiEngineAudio::CalculatePitch`, `aiEngineAudio::UpdateDoppler`, `aiEngineAudio::EchoOn`, `aiEngineAudio::EchoOff`, `aiEngineAudio::UpdateEcho`, `aiEngineAudio::~aiEngineAudio` | ported | `AmbientCarAudio`, `parseAmbientEngine` | First audit (both constructors). The pieces split off the destructor are the hash table's static set-up. |
| `vehHornAudio::vehHornAudio`, `vehHornAudio::Load`, `vehHornAudio::ReadCSV`, `vehHornAudio::AddToHash`, `vehHornAudio::AllocTiming`, `vehHornAudio::AssignSounds`, `vehHornAudio::UnAssignSounds`, `vehHornAudio::Reset`, `vehHornAudio::PlayAvoidance`, `vehHornAudio::PlayImpact`, `vehHornAudio::Update`, `vehHornAudio::UpdateDoppler`, `vehHornAudio::IsPlaying`, `vehHornAudio::GetHornAudioTimingPtr`, `vehHornAudio::GetNumTimings`, `vehHornAudio::EchoOn`, `vehHornAudio::EchoOff`, `vehHornAudio::UpdateEcho`, `vehHornAudio::~vehHornAudio`, `vehHornAudio::`scalar_deleting_destructor'` | ported | `AmbientCarAudio` (`honk`, `impact`, `updateHorn`), `parseHorn` | First audit (both constructors). |
| `vehHornAudioTiming::vehHornAudioTiming`, `vehHornAudioTiming::AllocPlayPause`, `vehHornAudioTiming::Play`, `vehHornAudioTiming::Stop`, `vehHornAudioTiming::Reset`, `vehHornAudioTiming::Update`, `vehHornAudioTiming::~vehHornAudioTiming` | ported | `AmbientCarAudio::startPattern / updateHorn` | First audit (both constructors). |

## AudCreature, AudCreatureAvoid, AudCreatureImpact, AudCreatureContainer, aiPedAudio (voices)

What it is for: the lines drivers and pedestrians say on a near miss or
a hit. A pedestrian is an AudCreatureContainer (aiPedAudio) holding a 3D
slot while it screams; each sex's voice file is drawn when the game starts
(mmGame::Init). OpenMM2: `CreatureVoice` (Voices.cpp) and
`PedestrianAudio` (PedAudio.cpp), first audit; the traffic drivers' voices
are in the traffic section above.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `AudCreature::AudCreature`, `AudCreature::Load`, `AudCreature::ReadCSV`, `AudCreature::AddToHash`, `AudCreature::AssignSounds`, `AudCreature::UnAssignSounds`, `AudCreature::SetAud3DObjectPtr`, `AudCreature::Update`, `AudCreature::UpdateAttenuation`, `AudCreature::PlayAvoidance`, `AudCreature::PlayImpact`, `AudCreature::IsPlaying`, `AudCreature::EchoOn`, `AudCreature::EchoOff`, `AudCreature::UpdateEcho`, `AudCreature::~AudCreature` | ported | `CreatureVoice`, `parseCreatureVoice`, `PedestrianAudio::voiceSet` | First audit (both constructors). The pieces split off UpdateAttenuation are the creature hash table's static set-up. |
| `AudCreatureAvoid::AudCreatureAvoid`, `AudCreatureAvoid::ParseCSVBuffer`, `AudCreatureAvoid::AssignSounds`, `AudCreatureAvoid::UnAssignSounds`, `AudCreatureAvoid::SetAud3DObjectPtr`, `AudCreatureAvoid::Update`, `AudCreatureAvoid::InSpeedRange`, `AudCreatureAvoid::IsEligible`, `AudCreatureAvoid::QueuePlay`, `AudCreatureAvoid::Play`, `AudCreatureAvoid::SamplePlaying`, `AudCreatureAvoid::UpdateAttenuation`, `AudCreatureAvoid::EchoOn`, `AudCreatureAvoid::EchoOff`, `AudCreatureAvoid::UpdateEcho`, `AudCreatureAvoid::~AudCreatureAvoid` | ported | `CreatureVoice` (`Avoid` blocks) | First audit (both constructors). |
| `AudCreatureImpact::AudCreatureImpact`, `AudCreatureImpact::ParseCSVBuffer`, `AudCreatureImpact::AssignSounds`, `AudCreatureImpact::UnAssignSounds`, `AudCreatureImpact::SetAud3DObjectPtr`, `AudCreatureImpact::Update`, `AudCreatureImpact::UpdateStatics`, `AudCreatureImpact::QueuePlay`, `AudCreatureImpact::Play`, `AudCreatureImpact::SamplePlaying`, `AudCreatureImpact::UpdateAttenuation`, `AudCreatureImpact::EchoOn`, `AudCreatureImpact::EchoOff`, `AudCreatureImpact::UpdateEcho`, `AudCreatureImpact::~AudCreatureImpact` | ported | `CreatureVoice` (impact lines, `advanceClock`) | First audit (both constructors). |
| `AudCreatureContainer::AudCreatureContainer`, `AudCreatureContainer::LoadNumFileChoices`, `AudCreatureContainer::LoadVoices`, `AudCreatureContainer::AssignSounds`, `AudCreatureContainer::UnAssignSounds`, `AudCreatureContainer::Update`, `AudCreatureContainer::UpdateAudio`, `AudCreatureContainer::UpdateStatics`, `AudCreatureContainer::UpdateVoices`, `AudCreatureContainer::PlayAvoidanceReaction`, `AudCreatureContainer::Reset`, `AudCreatureContainer::EchoOn`, `AudCreatureContainer::EchoOff`, `AudCreatureContainer::UpdateEcho`, `AudCreatureContainer::DeallocateStatics`, `AudCreatureContainer::~AudCreatureContainer` | ported | `PedestrianAudio` | First audit (both UpdateAudio overloads). |
| `aiPedAudio::aiPedAudio`, `aiPedAudio::SetCSVCatString`, `aiPedAudio::LoadNumFemaleChoices`, `aiPedAudio::LoadNumMaleChoices`, `aiPedAudio::LoadFemaleVoices`, `aiPedAudio::LoadMaleVoices`, `aiPedAudio::~aiPedAudio` | ported | `PedestrianAudio::load`, `Container::init` | First audit: `default_fpedvoice<N>` / `default_mpedvoice<N>`. |

## AudSpeech, AudSpeechData, AudStream, mmSpeechContainer, mmRaceSpeech, mmCNRSpeech, mmCCSpeech (the announcer)

What it is for: the commentary. `mmPlayer::InitSpeechAudio` builds an
mmSpeechContainer for the race (with COMMENTARY on): the race speech
(mmRaceSpeech) in cruise and the races, Cops and Robbers (mmCNRSpeech) or
the crash course (mmCCSpeech), each an AudSpeech with groups of numbered
lines streamed from `aud/aud11`, a small queue of delayed plays and the line
playing. The game's events call the Play methods; the container is updated
by AudManager::Update and by mmGame::Update (twice a frame while running,
once while paused). OpenMM2: `Announcer` (Voices.cpp), first audit; the
update timing is new (`AudioManager`).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmSpeechContainer::Update` | ported (new) | `AudioManager::update` and `app/RaceScreen.cpp` (paused and running) | Updates the three speeches; called by AudManager::Update (while running) and mmGame::Update (always), so the queue's delays run at twice the clock while the game runs and at the clock while it is paused, when a due line starts after the pause's stops. Was once a frame and not while paused. Test `AnnouncerQueueCountsTwiceAFrameWhileRunning`. |
| `mmSpeechContainer::mmSpeechContainer`, `mmSpeechContainer::InitRace`, `mmSpeechContainer::InitCNR`, `mmSpeechContainer::InitCC`, `mmSpeechContainer::GetRaceSpeechPtr`, `mmSpeechContainer::GetCCSpeechPtr`, `mmSpeechContainer::~mmSpeechContainer` | ported | `Announcer::beginRace / beginCopsAndRobbers / beginCrashCourse` | First audit; the modes 0, 1, 3, 4 race speech, 6 crash course, 2 Cops and Robbers. |
| `mmSpeechContainer::Stop` | not needed | - | Only mmGame::UpdateDebugInput (a debug key) calls it. |
| `AudSpeech::AudSpeech`, `AudSpeech::AllocateSpeechData`, `AudSpeech::AllocateQueuePlayData`, `AudSpeech::SetSubPath`, `AudSpeech::SetExtension`, `AudSpeech::Play`, `AudSpeech::PlayStream`, `AudSpeech::PutInQueue`, `AudSpeech::Update`, `AudSpeech::EmptyQueue`, `AudSpeech::Stop`, `AudSpeech::SetVolume`, `AudSpeech::SetStreamVolume`, `AudSpeech::~AudSpeech` | ported | `Announcer` (`play`, `putInQueue`, `start`, `update`, `stop`) | First audit (both Play and PlayStream overloads); the volume is 1 (InitSpeechAudio, InitCC); the lines are the 11 kHz files. |
| `AudSpeech::PlayOneShot`, `AudSpeech::SetOneShotVolume`, `AudSpeechData::GetOneShot`, `AudSpeechData::GetRandomOneShot`, `AudSpeechData::LoadOneShots`, `AudSpeechData::SetVolume` | not needed | - | The one-shot mode of AudSpeech; every speech MM2 builds streams (mmRaceSpeech, mmCCSpeech and InitCNR all pass 1). |
| `AudSpeechData::AudSpeechData`, `AudSpeechData::GetName`, `AudSpeechData::GetRandomName`, `AudSpeechData::~AudSpeechData` | ported | `Announcer::addGroup / pickLine / lineName` | First audit. |
| `AudStream::AudStream`, `AudStream::PlayOnce`, `AudStream::SetVolume`, `AudStream::~AudStream`, `AudStream::`scalar_deleting_destructor'` | ported | `Announcer::start` (`SoundSlot` on the voice bus) | A line whose file does not exist plays nothing (first audit). |
| `mmRaceSpeech::mmRaceSpeech`, `mmRaceSpeech::LoadCityInfo`, `mmRaceSpeech::LoadGroup`, `mmRaceSpeech::SetReadState`, `mmRaceSpeech::locstrnicmp`, `mmRaceSpeech::LoadPreRace`, `mmRaceSpeech::LoadResults`, `mmRaceSpeech::LoadVehicleUnlock`, `mmRaceSpeech::LoadTextureUnlock`, `mmRaceSpeech::PlayPreRace`, `mmRaceSpeech::PlayDamagePenalty`, `mmRaceSpeech::PlayResults`, `mmRaceSpeech::PlayResultsWin`, `mmRaceSpeech::PlayResultsMid`, `mmRaceSpeech::PlayResultsPoor`, `mmRaceSpeech::PlayUnlockVehicle`, `mmRaceSpeech::PlayUnlockTexture`, `mmRaceSpeech::~mmRaceSpeech` | ported | `Announcer`, `parseSpeechTable`, `parseAnnouncerList` | First audit. |
| `mmRaceSpeech::PlayFinalCheckPoint` | ported (new) | `Announcer::playFinalCheckpoint`; `app/RaceScreen.cpp` on the session's FinalCheckpoint event | Ported by the first audit but never called: mmWaypoints::Update calls it when a checkpoint race has only the finish left and when a circuit's final lap reaches its last checkpoint (not in the crash course, which has no race speech). The same moments, and a circuit's final lap, switch the music to the chase segment (`MusicDirector::finalStretch`). |
| `mmRaceSpeech::CheckRaceLoadSanity`, `mmCCSpeech::CheckRaceLoadSanity` | not needed | - | Empty in build 3393. |
| `mmCNRSpeech::mmCNRSpeech`, `mmCNRSpeech::LoadGroup`, `mmCNRSpeech::SetReadState`, `mmCNRSpeech::~mmCNRSpeech` | ported | `Announcer::beginCopsAndRobbers / playCopsAndRobbers` | First audit. |
| `mmCCSpeech::mmCCSpeech`, `mmCCSpeech::SetSubPath`, `mmCCSpeech::LoadGroup`, `mmCCSpeech::SetReadState`, `mmCCSpeech::locstrnicmp`, `mmCCSpeech::LoadCheckPointIndexInfo`, `mmCCSpeech::PlayPreRace`, `mmCCSpeech::PlayCheckPoint`, `mmCCSpeech::PlayResults`, `mmCCSpeech::PlayResultsWin`, `mmCCSpeech::PlayResultsPoor`, `mmCCSpeech::~mmCCSpeech` | ported | `Announcer::beginCrashCourse`, `playCrashCourse*` | First audit. |

## MMDMusicManager, DMusicManager, DMusicObject, DMusicWaveBuffer, SegmentWrapper, AudStreamingMusic (music)

What it is for: the soundtrack is DirectMusic: the UI music in the menus
(mmInterface::PlayUIMusic, AudioOptions::ToggleMusic), and in a race the
song mmGameMusicData picks, its segments switched by
MMDMusicManager / mmGame::UpdateDMusic (start, idle, cops, pause, results)
with the "big air" motif, or with music off the city's ambience segment.
OpenMM2 renders the segments with its own DirectMusic implementation
(`audio/Music.cpp`, `MusicMotif.c`, the dmusic library) on a mixer stream,
and `audio/MusicDirector.cpp` ports the game-side logic (first audit).
AudStreamingMusic is an older streamed-playlist player that never plays.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `MMDMusicManager::Init`, `MMDMusicManager::UpdateSeconds`, `MMDMusicManager::UpdateMusic`, `MMDMusicManager::MatchMusicToPlayerSpeed`, `MMDMusicManager::UpdateAmbientSFX`, `MMDMusicManager::Reset` | ported | `MusicDirector`, `MusicEngine::setAmbience` | First audit; Reset is StartMusic's idle timer at 10000. mmGame::Reset (the race starting over) calls StartMusic again: `MusicDirector::restart` (new; the race had kept whatever segment was playing). |
| `MMDMusicManager::MMDMusicManager`, `MMDMusicManager::~MMDMusicManager` | replaced | `app/Context.cpp` `Context::music` (the MusicPlayer) | Creation and teardown. |
| `DMusicObject::SegmentSwitch`, `DMusicObject::StopSegment`, `DMusicObject::PlayMotif`, `SegmentWrapper::Play` | ported | `MusicDirector::segmentSwitch / autoTransition`, `MusicEngine::setState / triggerMotif` | First audit (both SegmentSwitch overloads). |
| `DMusicObject::PlaySegment`, `DMusicObject::OpenSegmentFile`, `DMusicObject::IsPlaying`, `SegmentWrapper::IsPlaying`, `SegmentWrapper::Stop`, `SegmentWrapper::OpenSegmentFile` | ported | `MusicEngine` (`transitionTo`, `setState`), `MusicLibrary` | Starting a segment at once (StartMusic, ToggleMusic, PlayUIMusic, the ambience), loading it, whether it plays. |
| `DMusicObject::Activate`, `DMusicObject::AllocateMotifs`, `DMusicObject::AssignPChannelBlocks`, `DMusicObject::CleanUpSegmentWrappers`, `DMusicObject::CreateComposer`, `DMusicObject::DMusicObject`, `DMusicObject::FindMSSoftWareSynth`, `DMusicObject::GetDMusicWaveBuffer`, `DMusicObject::HandleNotifications`, `DMusicObject::Init`, `DMusicObject::InitLoader`, `DMusicObject::InitNotificationThread`, `DMusicObject::InitPerformance`, `DMusicObject::InitPort`, `DMusicObject::LoadBand`, `DMusicObject::LoadMotif`, `DMusicObject::PlayBand`, `DMusicObject::ScanDirectory`, `DMusicObject::SetSearchDirectory`, `DMusicObject::~DMusicObject`, `SegmentWrapper::SegmentWrapper`, `SegmentWrapper::CleanUp`, `SegmentWrapper::LoadBand`, `SegmentWrapper::LoadSegmentBands`, `SegmentWrapper::UnloadSegmentBands`, `SegmentWrapper::PlayBand`, `SegmentWrapper::SetLoaderPtr`, `SegmentWrapper::SetPerformancePtr`, `SegmentWrapper::~SegmentWrapper`, `DMusicManager::DMusicManager`, `DMusicManager::Init`, `DMusicManager::Activate`, `DMusicManager::GetDMusicObjectPtr`, `DMusicManager::~DMusicManager`, `DMusicWaveBuffer::Create`, `DMusicWaveBuffer::DMusicWaveBuffer`, `DMusicWaveBuffer::~DMusicWaveBuffer` | replaced | `audio/Music.cpp` (`MusicEngine`, `MusicPlayer`, `MusicLibrary`), `MusicMotif.c`, the dmusic library | DirectMusic's performance, port, software synth, loader, bands, composer and notification thread. DMusicManager::Activate(0/1) is the CD player's way of pausing the music (never in retail). |
| `DMusicManager::SetVolume`, `DMusicWaveBuffer::SetVolume` | ported | `Mixer::setBusVolume(Bus::Music)` | The MUSIC slider's log curve (first audit). |
| `DMusicManager::SetPan`, `DMusicWaveBuffer::SetPan` | replaced | `Mixer::setBalance` | The BALANCE slider on the music buffer; OpenMM2's balance applies to the whole output. |
| `AudStreamingMusic::AudStreamingMusic`, `AudStreamingMusic::StreamingMusicReleaseControl`, `AudStreamingMusic::StreamingMusicUpdate`, `AudStreamingMusic::~AudStreamingMusic`, `AudStreamingMusic::`scalar_deleting_destructor'` | not needed | - | A playlist player (`aud/playlist`, `aud/music`) left from older Angel games: RestartAudio makes one, but nothing calls its Init or Play, so it has no tracks and its update does nothing. |

## AudioOptions (the AUDIO options page)

The frontend's AUDIO page (Options > Audio) and the audio flags it sets
(0x1 SOUND FX, 0x4 MUSIC, 0x400 COMMENTARY, 0x800 CITY SOUNDS, 0x40 / 0x100
STEREO FX, 0x10 / 0x20 SOUND QUALITY). Ported by the frontend-ui audit
(`app/frontend/PagesOptions.cpp` `AudioPage`, `app/Settings`,
`app/Context.cpp` `applyAudioSettings`).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `AudioOptions::AudioOptions`, `AudioOptions::PreSetup`, `AudioOptions::DoneAction`, `AudioOptions::CancelAction`, `AudioOptions::StoreCurrentSetup`, `AudioOptions::ResetDefaultAction`, `AudioOptions::SetAudioState`, `AudioOptions::FocusDescription`, `AudioOptions::~AudioOptions`, `AudioOptions::`scalar_deleting_destructor'` | ported | `AudioPage`, `Settings` | The page, its defaults and DONE / CANCEL. |
| `AudioOptions::SetSoundFX`, `AudioOptions::SetCommentary`, `AudioOptions::SetMusic`, `AudioOptions::SetAmbient`, `AudioOptions::ResetSoundFX`, `AudioOptions::ResetCommentary`, `AudioOptions::ResetMusic`, `AudioOptions::ResetAmbient`, `AudioOptions::ToggleMusic`, `AudioOptions::ToggleAmbient` | ported | `AudioPage` toggles, `Context::applyAudioSettings` | MUSIC and CITY SOUNDS exclude each other (ToggleMusic starts the UI music, ToggleAmbient stops it). |
| `AudioOptions::SetSFXVolume`, `AudioOptions::SetMusicVolume`, `AudioOptions::SetBalance` | ported | `AudioPage` sliders, `Mixer` | Clamped 0..1 (balance -1..1), then the managers' setters. |
| `AudioOptions::SetStereoFX`, `AudioOptions::ResetStereo` | ported | `AudioPage`, `Mixer::setStereo` | Surround plays as stereo. |
| `AudioOptions::SetQuality`, `AudioOptions::ResetSoundQuality` | ported | `AudioPage` (stored only) | The channel count changes nothing (AudManager::SetNumChannels is empty). |
| `AudioOptions::LoadUIMusicCSV` | ported | `MusicTables::parseSingle` | First audit. |
| `AudioOptions::FindDevice`, `AudioOptions::GetCurrentDeviceName`, `AudioOptions::SetDevice` | replaced | `AudioPage` (the SDL device's name) | Choosing a DirectSound device. |

## mmCDPlayer (the in-race CD player)

What it is for: a HUD widget (made by mmHUD) to play the player's own audio
CD in a race, driven by four keys (events 0x1a-0x1d: toggle, start / stop,
previous, next; OpenMM2 binds the keys in `app/Controls.cpp`). Every action
except PrevTrack first checks `hasMusicCD`, which InitAudioManager sets when
any CD-ROM drive holds a disc with `cdid.txt`: the retail disc has it and
SafeDisc requires the disc in a drive to start, so in the retail game the
toggle never shows the widget and the player never plays (PrevTrack only
lowers a track number nothing shows). OpenMM2 does the same: nothing.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmCDPlayer::mmCDPlayer`, `mmCDPlayer::Init`, `mmCDPlayer::Reset`, `mmCDPlayer::Update`, `mmCDPlayer::Cull`, `mmCDPlayer::Toggle`, `mmCDPlayer::PlayStop`, `mmCDPlayer::NextTrack`, `mmCDPlayer::PrevTrack`, `mmCDPlayer::~mmCDPlayer`, `mmCDPlayer::`scalar_deleting_destructor'` | not needed | - | Inert in the retail game (above). Without the game disc (the unprotected build) it would show `cd_bg` with the track number at the top right and play CD tracks through MCI, pausing the DirectMusic music; OpenMM2 has no CD audio. The session record lists the display as open. |

## Global functions

Free functions of the audio code, and the exception-unwind pieces of the
audio classes' constructors and destructors.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `DeallocateAudioData` | replaced | - | Frees the shared containers (engines, creatures, impacts) at the end of a race; OpenMM2's objects own their data (three template instances). |
| `DirectSoundCreate`, `DirectSoundEnumerateA` | replaced | `audio/AudioDevice.cpp` | DirectSound's entry points. |
| `??0AudSoundBase@@QAE@IHF@Z_SEH`, `??0AudSoundBase@@QAE@PAV0@@Z_SEH`, `??0AudioOptions@@QAE@H@Z_SEH`, `??0DMusicObject@@QAE@H@Z_SEH`, `??0PUAudioOptions@@QAE@HMMMM@Z_SEH`, `??0mmRainAudio@@QAE@XZ_SEH`, `??0vehCarAudio@@QAE@PAVvehCarSim@@PAVvehCarDamage@@PBD_N3@Z_SEH`, `??0vehHornAudio@@QAE@PAV0@@Z_SEH`, `??0vehNitroCarAudio@@QAE@PAVvehCarSim@@PAVvehCarDamage@@PAD_N3@Z_SEH`, `??0vehPoliceCarAudio@@QAE@PAVvehCarSim@@PAVvehCarDamage@@PBD2_N@Z_SEH`, `??0vehSemiCarAudio@@QAE@PAVvehCarSim@@PAVvehCarDamage@@PAD_N3@Z_SEH`, `??1AudSoundBase@@UAE@XZ_SEH`, `??1AudStreamingMusic@@UAE@XZ_SEH`, `??1AudioOptions@@UAE@XZ_SEH`, `??1MMDMusicManager@@QAE@XZ_SEH`, `??1vehCarAudio@@UAE@XZ_SEH`, `??1vehPoliceCarAudio@@UAE@XZ_SEH`, `?AllocTiming@vehHornAudio@@QAEXXZ_SEH`, `?AllocateMenuSwitchAudio@MenuManager@@QAEXXZ_SEH`, `?AllocateSounds@UIBMButton@@CAXXZ_SEH`, `?GetSpeed@vehCarAudio@@QAEMXZ_SEH`, `?Init@DMusicManager@@QAEHPAUIDirectSound@@HKHKK@Z_SEH`, `?Init@vehCarAudioContainer@@QAEXPBDPAVvehCarSim@@PAVvehCarDamage@@H@Z_SEH`, `?InitAudio@vehCar@@QAEXPBDH@Z_SEH`, `?InitAudioManager@@YAX_N@Z_SEH`, `?InitLoader@DMusicObject@@AAEHXZ_SEH`, `?InitNitro@vehCarAudioContainer@@QAEXPBDPAVvehCarSim@@PAVvehCarDamage@@H@Z_SEH`, `?InitPolice@vehCarAudioContainer@@QAEXPBDPAVvehCarSim@@PAVvehCarDamage@@H@Z_SEH`, `?InitPort@DMusicObject@@AAEHPAUIDirectSound@@KHKK@Z_SEH`, `?InitSemi@vehCarAudioContainer@@QAEXPBDPAVvehCarSim@@PAVvehCarDamage@@H@Z_SEH`, `?Load@vehEngineAudio@@QAEHPAVStream@@@Z_SEH`, `?LoadAmbientSFX@mmGameMusicData@@AAE_NPAD@Z_SEH`, `?LoadCSV@vehSurfaceAudio@@QAE_NPAD0@Z_SEH`, `?LoadEngine@aiAmbientVehicleAudio@@QAE_NPAD0@Z_SEH`, `?LoadFromFile@CLoader@@AAEJPAU_DMUS_OBJECTDESC@@PAPAUIDirectMusicObject@@@Z_SEH`, `?LoadFromMemory@CLoader@@AAEJPAU_DMUS_OBJECTDESC@@PAPAUIDirectMusicObject@@@Z_SEH`, `?LoadHorn@aiAmbientVehicleAudio@@QAE_NPAD0@Z_SEH`, `?LoadImpacts@aiAmbientVehicleAudio@@QAE_NPAD0@Z_SEH`, `?LoadVoices@aiAmbientVehicleAudio@@QAE_NPAD0_N@Z_SEH`, `?PlayUIMusic@mmInterface@@AAEXXZ_SEH`, `?RestartAudio@AudManagerBase@@QAEXHHH@Z_SEH`, `?ToggleMusic@AudioOptions@@QAEXXZ_SEH` | not needed | - | C++ exception-unwind handlers (labels ending in _SEH): each only destroys the locals of the function it is named after, which is audited with that function (the infrastructure record lists the runtime). |

## For other subsystems

| MM2 | What | For |
| --- | --- | --- |
| `mmHUD::PlayNetAlert` | The HUD's network alert ("Carhorn1double" at 0.85, priority 0x17) when a multiplayer game or system message arrives (`mmMultiRace` / `mmMultiCircuit` / `mmMultiBlitz` / `mmMultiRoam` / `mmMultiCR` `GameMessage` and `SystemMessage`); OpenMM2 plays nothing. The sound slot is `audio::game::SoundSlot` with `kGameSoundPriority` | session (multiplayer messages) |
| `mmWaypoints::Update` | The session raises FinalCheckpoint only for its CheckpointRace / AnyOrderEnd and Circuit rules; MM2's type 5 (in-order) rule has no final checkpoint either, so nothing is missing, but the session should keep raising FinalLap and FinalCheckpoint where it does: the race now plays the line and switches the music on them | session |
| `mmCDPlayer` | The HUD display is listed open in the session record; with the retail disc it never shows (see mmCDPlayer above), so it can be closed as not needed | session |
| `mmCarRoadFF::Update` | Plays a sound while the road force-feedback effect runs; part of force feedback, which OpenMM2 does not have | input / force feedback |
| `-nomusic` | The infrastructure audit mutes the music bus, which also silences the menu music. MM2 only skips the race's song and ambience segment (`mmGameMusicData::Load`): the fix is for the race not to start them (no `startRace` / `setAmbience` and no `MusicDirector`) under `-nomusic`, leaving the bus alone | coordinator (`app/Context.cpp`, `app/RaceScreen.cpp` on integration) |
| `gizBridge`, `gizFerry`, `gizTrain`, `aiSubway`, `aiCableCar` | Their sounds' API: `BridgeAudio`, `AmbientObject` ("ferry"), `SubwayAudio`, `CableCarAudio` (load once with the race's `Object3DManager`; every frame `setPosition` and `update` with the listener, tunnel flag and speed; `activate` / `deactivate` for bridges; `reset` from the owner's Reset) | world objects |
