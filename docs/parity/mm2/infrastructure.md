# MM2 -> OpenMM2: infrastructure

Audited from MM2Recomp (midtown2.exe build 3393) on 2026-10-08.

Summary: 2424 reachable functions in 68 classes and the global namespace;
ported 219 (of which newly ported 7), replaced 1188, not needed 639,
open 0, handed over 378.

"Handed over" is this record's extra status: the function belongs to a
game subsystem the coverage tool did not route it to (an unnamed piece of
a named function, or a small class of another subsystem), so its row
belongs in that subsystem's record; the sections below say which.

Infrastructure is everything the coverage tool did not assign to a game
subsystem: the C runtime and compiler support, libjpeg and zlib, Direct3D
and DirectDraw (AgeDevice, gfxPipeline, gfxPacket, gfxRenderState,
gfxViewport), DirectPlay (asNetwork), DirectMusic's loader streams, the
Angel engine's data layer (streams, archives, tokenizers, the tuning
parser, hash tables, memory), the scene graph (asNode, asRoot,
asCullManager) and the Windows glue. OpenMM2 replaces this plumbing on
purpose: `render/` (Vulkan/OpenGL), `net/` (UDP/ENet), `audio/`,
`vfs/` and `data/`, `platform/` (SDL3) and `core/`. The audit's real
work was to find the game behaviour that sits in this plumbing: the
original's command-line options, the frame clock, the random generators,
small game classes the tool left here, and 622 unnamed functions, most of
which are pieces of named game functions.

How each function was classified: the class's purpose from its methods
and callers in the asm call graph (built as `coverage.py` builds it, with
callers and fall-through predecessors); the global names by what they are
(runtime, libjpeg, zlib, exception handling, engine helpers); each
unnamed function by the named function it belongs to (its fall-through
predecessor, or its only caller), read in the decompile where it does
anything of note.

## Command-line options (datArgParser)

`datArgParser::Init` stores every "-name" with the words after it, up to
the next one that starts with '-' and a non-digit (so "-5" is a value);
"-name=value" makes the part after '=' the first value; a repeated name
keeps its first occurrence (later ones are skipped with their values).
`datArgParser::Get` answers "present", or converts value n with atoi /
atof, or returns it as a string; the table is case-sensitive. These are
every option build 3393 reads (each `datArgParser::Get` call):

| Option | Read by | What it does in MM2 | OpenMM2 |
| --- | --- | --- | --- |
| `-nomovie` | `Main` | Skips LOGOS.AVI. The movie also never plays in a window (`Main` checks inWindow). | ported (new): skips the intro, as `--skip-intro`. |
| `-window`, `-max`, `-fs`, `-fullscreen` | `gfxPipeline::SetRes` | Checked in that order: a window; a window the size of the screen at 0,0; full screen. A window (-window or -max) also means no logo movie. | ported (new): windowed; borderless (stands in for -max); full screen (borderless). -window and -max skip the intro. |
| `-width <px>`, `-height <px>` | `gfxPipeline::SetRes` | The resolution (ignored by -max). | ported (new): the window size. |
| `-novblank` | `gfxPipeline::SetRes` | Presents without waiting for the vertical blank. | ported (new): vsync off. |
| `-noaudio` | `InitAudioManager`, `mmGameMusicData::Load`, `mmPlayer::InitSpeechAudio` | No audio manager (so no sound at all), no music, no speech. | ported (new): master volume 0 for the run. |
| `-nosoundfx` | `InitAudioManager` | Leaves AudManager uninitialised: no sound effects, and (inferred: DirectMusic is started on AudManager's DirectSound, `MMDMusicManager::Init`) no music either. | ported (new): as -noaudio. |
| `-nomusic` | `mmGameMusicData::Load` | The race loads neither its music nor the city's ambience segment. The menu music (`mmInterface::PlayUIMusic`) is not affected. | ported (new): the race starts neither the song, the music director nor the ambience segment; the menu music plays on. |
| `-nospeech` | `mmPlayer::InitSpeechAudio` | No announcer. | ported (new): the voice bus (only the announcer plays on it) is muted. |
| `-tune_car` | `mmPlayer::Init` | Without it, a player driving vpcop gets vpmustang99's vehCarSim (the cop car drives like the Mustang); with it, vpcop keeps its own (for tuning the cop). | The swap is ported (`game/PlayerVehicle.cpp`); the option is a development switch: not needed. |
| `-tune_ai` | `mmGame::Init` (passed as `aiMap::Init`'s last argument) | Development switch for AI tuning. | not needed. |
| `-level <city>`, `-car <vehicle>` | `Main`, `mmStatePack::SetDefaults` | The state pack's default city and car ("sf" and "vpcoop" when absent). | not needed: OpenMM2 opens the menus on the profile's last city and car; `--quickstart <city>` is its development shortcut. |
| `-pedpool <n>` | `aiCityData::aiCityData` | Overrides the city's [Ped Pool] (pedestrian pool, default 100). | ported: `CommandLine::pedPool` sets `ai::World`'s pedestrian pool (the AI audit, 48895c4). |
| `-pvs <name>` | `cityLevel::Load` | Reads city/<name>.cpvs instead of the city's own PVS. | not needed. |
| `-texframeskip <n>` | `gfxGetTextureMovie` | Texture movies use every n-th frame (name-0001, then 1 + n, ...; default 1). | not needed. |
| `-nomipmap` | `gfxRenderState::Init` | Turns the mip filter off. | not needed. |
| `-andyglasshack` | `dgUnhitBangerInstance::RequestBanger` | Makes sp_streetlight03_m a glass banger (data flag 0x100). | not needed (development hack). |
| `-nolockcheck` | `Main` | MM2 creates a marker file at start and deletes it at exit; when the marker is already there (the last run crashed) it deletes it, opens Trouble.rtf and quits. The option skips that check. | not needed. |
| `-nolog`, `-nan`, `-checkalloc`, `-logopen` | `Main`, `asRoot::Init`, `zipAutoInit`, `zipMultiAutoInit` | Logging, the NaN floating-point trap, heap checking, logging every archive file opened. | not needed. |
| `-archive [file]` | `Main`, `zipAutoInit` | Mounts one archive instead of every *.ar. | not needed. |
| `-ime`, `-noime` | `Main` | Forces the IME on, or skips setting it up (Japanese, Chinese and Korean systems). | replaced (SDL text input). |
| `-config`, `-blade` | `gfxAutoDetect` | Re-detect the devices instead of loading the saved choice. | replaced. |
| `-ref`, `-blade`, `-bladed`, `-swage`, `-sw`, `-sysmem`, `-triple`, `-nomultitexture`, `-nomt`, `-nohwtnl`, `-tex32`, `-primary`, `-display <n>`, `-single`, `-cdepth <n>`, `-zdepth <n>` | `gfxPipeline::SetRes` | Direct3D device (reference, software rasterisers), system-memory surfaces, triple or single buffering, multitexture, hardware T&L, 32-bit textures, adapter, colour and depth bits. | replaced. |
| `-nativevb`, `-nonativevb` | `gfxPipeline::BeginGfx3D` | Device vertex buffers. | replaced. |

OpenMM2 keeps its own "--" options and now also accepts these: the
honoured ones as above, the rest logged at start-up as having no
equivalent, and any other "-word" logged as unknown and ignored (the
original stores any word and never asks for it). Tests:
`tests/app/test_parity_infrastructure.cpp`.

## datTimeManager

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `datTimeManager::Update` | ported | `platform/Clock.cpp` `FrameClock::tick` | Measures the frame (Timer ticks), ActualSeconds = the measured time; ElapsedTime accumulates the measured, unclamped time (also while paused: the clock runs every frame); in real-time mode (Mode 0, SampleStep 0) Seconds = the measured time; the first frame of the process uses SampleStep; Seconds is held to ClampMin 0.0001 .. ClampMax 0.1 and InvSeconds = 1 / Seconds. The first-frame rule is not reproduced (openmm2-only record); datReplay recording and the per-frame debug message are not needed. |
| `datTimeManager::RealTime` | ported | `FrameClock` (real time) | MainPhase calls it with 0: FPS 60, Mode 0, SampleStep 0. |
| `datTimeManager::SetTempOverSampling` | ported | `phys/World.cpp` `World::advanceOversampled` | Seconds / n for each physics sample (phys-core record). |

ElapsedTime, the unclamped clock, is read by `AboutMenu` (1.5 s steps),
`asViewCS` (a sine wobble), `mmPowerupInstance` (spins at 3 rad/s),
`mmPlayer` (+0x2388 timer), `phSleep`, `Spline`, `mmReplayManager` and
`mmInterface` (player tag ids). OpenMM2 advances those with the clamped
frame time, which differs only on frames longer than 0.1 s and while
paused (handed to hud-views, vehicle-physics, world-objects and
frontend with those classes).

## Random generators

MM2 has two generators:

- **irand / frand** (the MSVC rand() LCG: seed × 214013 + 2531011,
  bits 16..30; frand = irand × 2^-15) on one global seed, gRandSeed.
  ResetRandomSeed sets it to 1 in `aiMap::Reset` (every race start and
  reset), in `cityLevel::Load` just before the propulator places the
  street props (so the props come out the same every time) and in
  `mmReplayManager`. Its users: the AI (`aiMap` link choices and
  AdjustAmbients, `aiPedestrian` Init, Reset, Update, Anticipate,
  WaitCrossStreet and PickNextRdSeg, `aiPoliceOfficer` detection and
  apprehension, `aiGoalAvoidPlayer`, `aiGoalRandomDrive`, `aiRailSet`,
  `aiVehicleSpline`, `aiVehicleInstance::SetColor`, `aiCTFRacer::Init`,
  `aiMap::Init`'s ambient set-up), the world objects (`aiCableCar::Init`,
  `aiSubway::Init`, `gizInstance::Init`, the parked cars'
  `gizParkedCarMgr_EnumeratePath`), the props and effects
  (`cityPropulator::Propulate`, `asBirthRule::InitSpark`,
  `asLineSparks::RadialBlast`, `fxShard`, `fxShardManager::EmitShard`,
  `fxTexelDamage::ApplyDamage`, `dgBangerInstance::DrawGlow`,
  `ltFlare::Random`), the vehicle (`vehCarModel::EjectOneshot`,
  `vehCarDamage::Update`) and the game (`mmGame::RespawnXYZ`,
  `mmMultiCR::GetRandomIndex` and GetRandomPoints).
- DisableGlobalSeed / EnableGlobalSeed swap in a second seed (and switch
  off the LogRandomCalls debug hook) around the draws that must not
  disturb the shared sequence: particle births, line sparks, shards,
  texel damage, vehCarDamage::Update, mmGame::RespawnXYZ and the Cops and
  Robbers random points (whose helper at 0x424cd0 seeds the second
  sequence from the clock). The shared sequence stays the same on every
  machine of a network race whatever effects each one draws.
- `irand(int)` and `frand(int)` are a stateless hash of their argument
  (one LCG step); only `aiVehicleInstance::aiVehicleInstance` uses it, on
  its own address, for the instance's arbitrary 15-bit number (lvlInstance
  +0x18; handed to ai).
- **Random** (Knuth's subtractive generator, 55 terms, `Random::Seed`,
  `Random::Number`, `Random::Normal` unused): the audio's
  RandomizeNumber helpers (`AudManagerBase`, `mmGameMusicData`,
  `vehPoliceCarAudio`) and `Aud3DObjectManager::CatName`, each seeding a
  fresh generator from the clock.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `irand`, `frand` | ported | `ai/Random.h`, `game/fx/Random.h`, `phys/World.h`, `phys/vehicle/Wheel.h` | Same LCG and scaling. Deviation (documented in `ai/Random.h`): each OpenMM2 subsystem has its own generator, so identical inputs replay identically per subsystem, while MM2's systems share one sequence and their interleaving changes every draw. OpenMM2's per-subsystem split plays the role of the second seed. |
| `ResetRandomSeed` | ported | `game/bangers/PropPlacement.cpp` (seed 1 before placing props); the AI generators are seeded per session | |
| `EnableGlobalSeed`, `DisableGlobalSeed` | ported | the effects' own generators (`game/fx`) | |
| `Random::Seed`, `Random::Number` | ported | `audio/AngelRandom.cpp` | Audio record. |

## Game behaviour found here, for other subsystems

| What | Subsystem | MM2 | OpenMM2 |
| --- | --- | --- | --- |
| The menu pointer | frontend | `sfPointer::ResChange`, `Update`, `Cull`: texture/midcursor.tga, colour keyed, drawn at the mouse (held 4 pixels inside the screen) in full screen only, not while the IME composes; in a window (-window) Windows' cursor shows. | ported (new): `app/frontend/MenuPointer.cpp`, drawn last by `Frontend::draw` when borderless or full screen, with the system cursor hidden. OpenMM2 always showed the system cursor. |
| Start-up graphics defaults | frontend | `AutoDetect`: far clip 400/600/800/1000 m, object detail and cloud shadows by CPU MHz (less 150 for software rendering), lighting quality 1-3, sky, environment maps and texture quality by video memory and RAM. | deviation already recorded by frontend-ui (top tier always). |
| Hookmen | ai | `mcHookman` (circuit, hideout and return driving), created per the [Hookmen] count of the race's .aimap. | not needed: no retail file asks for one. |
| Unclamped ElapsedTime | hud-views, world-objects, vehicle-physics, frontend | See datTimeManager. | differs only on long frames and while paused. |
| The game's update order | game-flow | `asNode::Update` runs the children in the order they were added; `mmGameManager::Update` declares the AI map and itself to the cull manager, updates its children, the level, then (unpaused) the active bangers and the physics, then the level's post-update, the camera, the clear colour, the HUD and the cull pass. | session record (RaceScreen's order). |
| Pause | game-flow | `asRoot` pause flag and deferred pause; `asNode::UpdatePaused` keeps updating the children of nodes flagged 0x400. | session record. |
| Split functions | all | The unnamed pieces of named functions listed at the end (for example the rest of `Main`, `mmGame::Init`, `cityLevel::Load`, `aiMap::Init`, `aiMap::Reset`, `aiMap::Update`). | Their subsystems' records. |
| Small classes | see the class table | `dgImpact`, `dgStatePack`, `lvlSegmentInfo`, `mmCCData`, `mmRewardRecord`, `Quaternion` (ragdoll IK and net prediction), `TerrainContact`. | handed over. |
| ADPCM | audio | `adpcm_decoder` for compressed .wav data. | not needed: all 2503 retail .wav files are PCM (checked in the data). |

## Classes

One row per class, or per group of methods where a class splits. Every method is named in the function index at the end.

| MM2 class | Functions | Status | OpenMM2 | Notes |
| --- | ---: | --- | --- | --- |
| `AgeDevice` | 50 | replaced | `render/` Vulkan and OpenGL backends | The Angel engine's IDirect3DDevice7 wrapper (state blocks, primitives, transforms, lights, sphere visibility). OpenMM2 renders through its own device layer. |
| `asCullable` | 1 | replaced | `render/` draw lists | Base Cull of everything drawn; the screens draw explicitly. |
| `asCullManager` | 10 | replaced | `Screen::drawScene` / `drawOverlay` | Per frame: clear to the clear colour (0xff001e3c after Reset; mmGameManager::Update sets the race's; MenuManager black), the declared 2D background bitmaps, each declared camera's 3D cullables between DrawBegin/DrawEnd, then the 2D foreground; a frame flagged by the replay manager draws nothing. OpenMM2 keeps the same layering (scene, then overlay). |
| `asFileIO` | 11 | replaced | `data::DatFile` | asNode text I/O (the .asnode, .camtrackcs, .campovcs tuning files are read by the data parser); saving is not needed. |
| `asMeshCardInfo` | 3 | ported | `game/fx/ParticleRenderer.cpp` | Particle cards and their shadows; cited, belongs to props-fx. |
| `asNetObject` | 4 | replaced | `net/` (snapshots) | DirectPlay-replicated scene object. |
| `asNetwork` | 51 | replaced | `net/` UDP/ENet transport, lobby, LAN discovery | DirectPlay 4: sessions, players, lobby (Zone) launch, modems, system messages. The game messages it carries are audited with their senders (mmGameMulti, mmMulti*). |
| `asNode` (the rest) | 22 | replaced | explicit update order in `RaceScreen` / screens | The scene graph: Update calls every active child (flag 1) in child order; UpdatePaused calls UpdatePaused, except children of a node with flag 0x400, which keep updating; Reset and ResChange recurse. OpenMM2 calls its systems directly in mmGameManager::Update's order (session record). |
| `asNode` (Load) | 1 | ported | `data::DatFile` readers | asNode::Load reads a node's tuning file. |
| `asRoot` | 6 | replaced | the pause state of `RaceScreen` and the session | The root of the scene graph and the pause flag (+0x48: IsPaused, SetPause, TogglePause; Reset clears it and the deferred pause +0x49, which Update turns into a pause after the tree has updated). Update also arms the NaN trap of -nan. Who pauses (mmPopup, mmGameManager::ForcePopupUI, mmGame's constructor, the replay manager) is game-flow's. |
| `asSparkLut` | 2 | ported | `game/fx/LineSparks.cpp` | Spark colour table from a texture (at most 256 pixels, power of two); props-fx. |
| `asUnderlay` | 5 | ported | `app/frontend` page backgrounds | Camera underlay bitmap: MenuManager::SetBackgroundImage (asCamera::SetUnderlay); the frontend draws its backgrounds. |
| `Base` | 2 | not needed | - | Compiler-generated destructors of the root class. |
| `CArrayList` | 3 | ported | `ai/RoadNetwork.cpp` (.bai) | aiMap::ReadBinary's arrays; ai. |
| `CFileStream` | 24 | replaced | `audio/` music player, `vfs/` | DirectMusic loader IStream over Angel streams (DMusic's CLoader). |
| `CLoader` | 21 | replaced | `audio/` music player | IDirectMusicLoader implementation (object cache, search directory). |
| `CMemStream` | 22 | replaced | `audio/` music player | IStream over memory for DirectMusic. |
| `datArgParser` (Get, Init) | 5 | ported (new) | `app/CommandLine.cpp` `parseCommandLine` | Init: "-name" or "-name=value", values up to the next "-<non-digit>", the first of a repeated name kept; Get: present / int / float / string. OpenMM2 now accepts the original's options (section above). |
| `datArgParser` (Kill) | 1 | not needed | - | Kill frees the table. |
| `datAsciiTokenizer` (the rest) | 11 | ported | `data/Tokenizer` | Reading ASCII tuning data (formats record). |
| `datAsciiTokenizer` (Put, PutDelimiter) | 7 | not needed | - | Put*/PutDelimiter write ASCII data (saving tuning files: editor only). |
| `datAssetManager` | 9 | replaced | `vfs/` (`Vfs`, `GameSource`) | Path building (folder + name + extension), Exists, Open, EnumFiles over the archives. |
| `datBaseTokenizer` (the rest) | 9 | ported | `data/Tokenizer` | Token reading, comments, push-back (formats record). |
| `datBaseTokenizer` (Put) | 1 | not needed | - | Writing. |
| `datBinTokenizer` (the rest) | 11 | ported | `data/Tokenizer` (binary) | Binary tuning data (formats record). |
| `datBinTokenizer` (Put, PutDelimiter) | 7 | not needed | - | Writing. |
| `datCallback` | 7 | replaced | `std::function` | Angel callback object (member function + argument). |
| `datMemStream` | 2 | replaced | `std::span` readers |  |
| `datMultiTokenizer` (GetReadTokenizer, datMultiTokenizer) | 2 | ported | `data/` | Picks the ASCII or binary tokenizer by the file's header. |
| `datMultiTokenizer` (GetWriteTokenizer) | 1 | not needed | - | Writing. |
| `datOutput` | 2 | not needed | - | Message-box hooks of the logger. |
| `datParser` (AddParser, AddRecord, AddValue, Load, Read, datParser) | 8 | ported | `data/DatFile` | Tuning file records (formats record). |
| `datParser` (Indent, Save, Write, ~datParser) | 6 | not needed | - | Save/Write/Indent and the destructor: writing tuning files. |
| `datParserRecord` | 1 | ported | `data/DatFile` |  |
| `datRefCount` | 5 | replaced | C++ ownership |  |
| `datReplay` | 2 | not needed | - | Records or plays back each frame's time for debug replays (datTimeManager::Update); never enabled in the shipped game. |
| `datStack` | 3 | not needed | - | Stack tracebacks from the linker map (crash logging). |
| `datTimeManager` | 3 | ported | `platform/Clock`, `phys/World` | Section above. |
| `datTokenizer` | 1 | ported | `data/Tokenizer` |  |
| `dgImpact` | 2 | handed over | vehicle-physics | Cited (`phys/`). |
| `dgStatePack` | 2 | handed over | game-flow | Base of mmStatePack (the game state block). |
| `dgTreeRenderer` | 3 | ported | `game/bangers/BangerSet.cpp` | Trees drawn after the other props (camera-props record); props-fx. |
| `EffectBase` | 2 | ported | `audio/EchoEffect.cpp` | The echo effect's buffer; audio. |
| `FixedHashEntry` | 2 | replaced | standard containers |  |
| `gfxLight` | 1 | replaced | `render/` lighting | Default light values. |
| `gfxMaterial` | 1 | replaced | `render/` materials | Default material values. |
| `gfxPacket` (the rest) | 19 | replaced | `render/` vertex buffers | Vertex packets of the static meshes. |
| `gfxPacket` (OrthoMap) | 1 | ported | `game/` renderers | Cited by the rendering record. |
| `gfxPipeline` (the rest) | 26 | replaced | `render/`, `platform/` | DirectDraw/Direct3D set-up, frames, scenes, clears, window procedure, adapters. |
| `gfxPipeline` (CopyBitmap, Manage, SetRes, gfxWindowProc) | 4 | ported | `app/App.cpp`, `render/` | Cited: inactive-window behaviour, bitmaps, resolution (openmm2-only record). |
| `gfxRenderState` (the rest) | 14 | replaced | `render/` | Device state cache. |
| `gfxRenderState` (DoFlush, SetCamera) | 3 | ported | `render/`, `game/` renderers | Cited (render-state semantics, rendering record). |
| `gfxVertexBuffer` | 3 | replaced | `render/` |  |
| `gfxViewport` (DoFlush, Ortho, ResetWindow, gfxViewport) | 4 | replaced | `render/` |  |
| `gfxViewport` (IsSphereVisible, Perspective, SetWindow) | 3 | ported | `render/`, `game/` | Cited (culling and projection). |
| `HashTable` | 18 | replaced | standard containers | String-keyed table (the argument table, banger data, textures, movies...). |
| `ltFlare` | 2 | ported | `game/fx/LensFlares.cpp` | Cited; rendering. |
| `lvlSegment` | 1 | ported | `phys/World.cpp` | Cited; vehicle-physics. |
| `lvlSegmentInfo` | 1 | handed over | city-render / ai | AllocateState for the segment probes of wheels, pedestrians, splines, subway and cable cars. |
| `MArray` | 5 | ported | `ui/MenuLayout` | Menu data file (frontend). |
| `mcHookman` | 8 | not needed | - | An AI driver that drives a circuit, runs to a hideout and returns (DriveCircuit, DriveToHideout, ReturnToCircuit). aiMap creates one per [Hookmen] count of the race's .aimap file; the only retail file with the section (race/sf/roam.aimap and its _p copy) gives 0, so none ever exists. |
| `memMemoryAllocator` | 13 | replaced | C++ allocator |  |
| `memSafeHeap` | 7 | replaced | C++ allocator |  |
| `mmAccelCompute` | 2 | replaced | `net/Snapshot.cpp` | Acceleration estimate for mmNetObject::SetPositionData (remote car prediction). |
| `mmCCData` | 2 | handed over | game-flow | Crash course data block of mmSingleStunt. |
| `mmRewardRecord` | 2 | handed over | frontend / game-flow | mmRewardList::Init's records (rewards, `game/Profile`). |
| `mmSlidingGauge` | 2 | ported | `game/session/Hud` | Cited (session record); hud-views. |
| `netScoreInfo` | 1 | not needed | - | Score block sent to the Zone lobby (SendLobbyResults). |
| `parCsvFile` | 6 | ported | `data/` CSV readers | Cited. |
| `Quaternion` | 2 | handed over | world-objects (ragdolls) / game-flow (net) | Matrix34::Interpolate: the ragdoll IK solvers (crArmData, crLegData, crSpineData, crHeadData) and mmNetObject::Predict; OpenMM2's network prediction has its own slerp (`net/Snapshot.cpp`). |
| `Random` | 2 | ported | `audio/AngelRandom` | Section above. |
| `sfPointer` (Cull, ResChange) | 2 | ported (new) | `app/frontend/MenuPointer.cpp` `drawMenuPointer` (the menus, and the race's popups from `RaceScreen::drawPopup`) | The menu pointer: ResChange loads texture/midcursor.tga and limits its corner to the screen less 4 pixels; Update declares it only when not in a window (and not while the IME composes); Cull copies it colour keyed with its top-left corner at the mouse. OpenMM2 now draws it on top of the menus and hides the system cursor when the window is borderless or full screen; in a decorated window the system cursor shows, as Windows' did under -window. The IME condition is not reproduced. |
| `sfPointer` (GetPointerHeight, WaitForRelease) | 2 | handed over | frontend | GetPointerHeight (0.05 of the screen) is read by UIBMButton::Update; WaitForRelease (ignore the mouse until the button is released) by UICWArray::AcceptCapture. |
| `sfPointer` (Init, 'scalar_deleting_destructor', sfPointer, ~sfPointer) | 4 | replaced | `app/frontend` | Construction and field defaults of the pointer node. |
| `sfPointer` (Update) | 1 | ported | `ui/Widgets.cpp` | Mouse hits (frontend record). |
| `Stream` (the rest) | 11 | replaced | `vfs/` | Buffered file streams, archive streams, open-file dump. |
| `Stream` (GetCh, Open, Read, Seek) | 5 | ported | `vfs/`, `core/` readers | Cited. |
| `string` (Contains, Init, SubString, operator+=, operator=, string) | 9 | replaced | `std::string` |  |
| `string` (NumSubStrings) | 1 | ported | `core/StringUtil` | Cited. |
| `TerrainContact` | 1 | handed over | vehicle-physics | phInertialCS::Update's contact record. |
| `Timer` | 3 | replaced | `platform/Clock` | Tick counter (QueryPerformanceCounter or timeGetTime). |
| `winDispatchable` | 3 | replaced | `platform/` events | Window message dispatch base. |
| `zipFile` | 15 | ported | `vfs/DaveArchive`, `vfs/GameSource` | Cited (formats and openmm2-only records); zipCreate/zipWrite/zipClose are for writing (not needed). |
| `zipHandle` | 2 | ported | `vfs/DaveArchive` | Cited. |

## Global functions

Grouped by what they are; the names are listed in full.

| Group | Functions | Status | OpenMM2 | Notes and names |
| --- | ---: | --- | --- | --- |
| Random generators | 6 | ported | `ai/Random.h`, `game/fx/Random.h`, `phys/World.h` | Section above. Names: DisableGlobalSeed, EnableGlobalSeed, frand, irand, ResetRandomSeed. |
| Collision geometry helpers | 20 | ported | `phys/Geometry.cpp` | Segment, line and box intersection helpers of the bounds (cited). Names: AddIntersection, DistanceLineToLine, DistanceLineToPoint, DistanceParallelLineToLine, FindImpactEdgeToShaft, FindImpactPolygonToSphere, FindTValueSegToOrigin, FindTValueSegToPoint, FindTValuesLineToBoxFace, FindTValuesLineToLine, FindTValuesSegToSeg, IsPointBehindPlane, IsPointInBox, IsPointNearPlane, OrderIntersections, SegmentToBoxIntersections, SegmentToHemisphereIntersections, SegmentToSphereIntersections, SegmentToUprightCylIsects. |
| Math helpers | 7 | ported | `core/Math` | Vector and matrix helpers: Convert (Matrix34 to Matrix44 for the device), `operator*` (scalar times Vector3), __vectorDotMatrix; Lerp and Max (psdl tunnel junctions, `city/SdlDraw.cpp`); max and min (VehicleSelectBase::AssignVehicleStats, frontend). Names: __vectorDotMatrix, Convert, Lerp, Max, max, min, operator*. |
| Asset loaders | 32 | ported | `game/TextureLibrary.cpp`, `asset/Image.cpp`, `asset/Pkg`, `asset/Mtx` | Textures, bitmaps, texture movies, the .tex formats (all eight LoadTEX_ decoders, `TexFormat`), the texture quality reduction, JPEG/TGA/BMP/raw loading, models and pivots (cited). Names: CleanName, GetPivot, gfxDefaultPrepareImage, gfxFreeBitmap, gfxFreeTexture, gfxGetBitmap, gfxGetTexture, gfxGetTextureMovie, gfxLoadBmpImage, gfxLoadImageAll, gfxLoadJPEGImage, gfxLoadJPEGImageCB, gfxLoadRawImage, gfxLoadTargaImage, gfxLoadTexImage, gfxSetTexReduceSize, InstallJPEGSupport, LoadTEX_ARGB1555, LoadTEX_Invalid, LoadTEX_P4, LoadTEX_P8, LoadTEX_P8A8, LoadTEX_PA4, LoadTEX_PA8, LoadTEX_RGB888, LoadTEX_RGBA8888, modGetModel, modGetStatic, nodeGetBitmap, ParseCSVLine, texImage_CheckRes. |
| Other game globals | 12 | ported | see notes | GetLocTime (`game/session` formatTime: M:SS:HH); AngelReadString and MyLoadStringA (`game::Strings`, the PE string table); aiPedestrian_IsWoman and aiPedestrian_AreStringsEqual (`audio/game/PedAudio`); LoadCityCB, isCityInfoFile, LoadVehListCB and isVehInfoFile (`game/Catalog.cpp`: every tune/*.cinfo and tune/*.info); zipMultiAutoInit (`vfs/GameSource.cpp`: archive order); fgets and fscanf on streams (the CSV and .info readers). Names: aiPedestrian_AreStringsEqual, aiPedestrian_IsWoman, AngelReadString, fgets, fscanf, GetLocTime, isCityInfoFile, isVehInfoFile, LoadCityCB, LoadVehListCB, MyLoadStringA, zipMultiAutoInit. |
| Game globals of other subsystems | 7 | handed over | see notes | gizParkedCarMgr_EnumeratePath (world-objects: parked cars); UpdateCrc (frontend: the CRC of player, city and record files and mmVehInfo::ComputeTuningCRC); LoadDlgReplayCB and isReplayFile (game-flow: replays); AngelReadKeyString and ConvertDItoString (input-ff: key and button names); the mangled mmInterface::HOFInitRecords label (frontend: hall of fame). Names: ?HOFInitRecords@mmInterface@@AAEXHPAD@, AngelReadKeyString, ConvertDItoString, gizParkedCarMgr_EnumeratePath, isReplayFile, LoadDlgReplayCB, UpdateCrc. |
| Graphics device set-up | 22 | replaced | `render/DisplaySettings`, `app/frontend` (graphics page) | Device enumeration and the start-up graphics defaults. AutoDetect picks far clip, object detail, cloud shadows, lighting quality, sky, environment maps and texture quality from ComputeCpuSpeed's MHz and the video memory; OpenMM2 always takes the top tier (frontend record, deviation). Names: _CreateAgeDevice, AutoDetect, AutoDetectCallback, ComputeCpuSpeed, DDEnumProc, DeviceCallback, EnumAllSurfCallback, gfxApplySettings, gfxAutoDetect, gfxCreateFont, gfxFVFOffset, gfxFVFSize, gfxLoadSettings, gfxLoadVideoDatabse, gfxPipeline_SetLostCallback, gfxReleaseFont, gfxSafeMode, gfxSaveSettings, InitDirectDraw, MultiMonCallback, ResCallback, SetupResChoices. |
| Debug drawing | 7 | not needed | - | Debug drawing (axes, spheres, world matrix) and the glow particle helper (tglDrawParticle: the glow cards of dgBangerInstance::DrawGlow and ltLight::DrawGlow are drawn by OpenMM2's renderers). Names: rglDrawAxis, rglDrawSphere, rglEnableDisable, rglIsEnabled, rglWorldIdentity, rglWorldMatrix, tglDrawParticle. |
| Logging and errors | 24 | replaced | `core/Log` | Logging, assertions and fatal errors (Quitf/Abortf end the game; OpenMM2 parsers return errors instead), formatting, crash dumps, allocation logging, the NaN trap (-nan). Names: Abortf, ageDebug, DebugLog, DebugLogShutdown, DefaultPrinter, DefaultPrintString, Displayf, DumpStackTraceback, EnableNanSignal, ErrorDisplay, Errorf, FloatDump, formatf, gfxDebugf, HeapAssert, HexDump, InitMap, log_alloc, LogStackTraceback, Messagef, Printf, Quitf, StringDuplicate, Warningf. |
| Stream helpers | 2 | replaced | `vfs/` readers | The Angel stream printf and seek helpers (fseek: music data, player records, replays; fprintf: saving). Names: fprintf, fseek. |
| Windows glue | 37 | replaced | `platform/` (SDL3) | Windows start-up and glue: the CPU timer, the single-instance mutex, the memory and disk space warnings, the registry, the input window procedure and its focus handlers, the event queue callbacks, the IME, DirectInput creation and enumeration, file enumeration, the joystick control panel and help launchers (CalibrateWatcher, HelpWatcher: not needed). Names: ArchInit, CalibrateWatcher, CheckDiskSpace, CheckGlobalMemory, coreRawEnumFiles, CreateGameMutex, DecodeDIErrorMFlag, diInit, DirectInputCreateA, GetMidtownRegString, glb_4BAF0F, glb_4BAF68, glb_4BAFBD, glb_4BB003, glb_4BB05A, HelpWatcher, ImmAssociateContext, ImmDestroyContext, ImmGetContext, ImmGetDefaultIMEWnd, ImmNotifyIME, ImmSetCompositionWindow, inputEnumDeviceProc, inputEnumEffectTypeProc, InputWindowProc, IO_EVENT_CHAR, IO_EVENT_CREATE, IO_EVENT_DESTROY, IO_EVENT_INPUT, IO_EVENT_KEYDOWN, IO_EVENT_LBUTTONUP, IO_EVENT_MBUTTONDOWN, IO_EVENT_RBUTTONDOWN, IO_EVENT_RBUTTONUP, KeyCodeToEventCode, ReleaseWindowFocus, SetWindowFocus. |
| DirectPlay glue | 5 | replaced | `net/` | DirectPlay creation and enumeration callbacks. Names: DirectPlayCreate, EnumConnectionsCallback, EnumModemAddress, EnumPlayersCallback, EnumSessionCallback. |
| Audio plumbing | 8 | replaced | `audio/` (mixer, `vfs/` streams) | Streaming file access of the audio library, DirectSound device enumeration, the DirectMusic notification thread, the IMA ADPCM decoder (no retail .wav uses it: all 2503 are PCM). Names: adpcm_decoder, Aud_Stream_Close, Aud_Stream_Open, Aud_Stream_Read, Aud_Stream_Seek, Aud_Stream_Size, DSEnumProc, ThreadProc. |
| Single-archive mode | 1 | not needed | - | zipAutoInit: -archive <file> mounts a single archive instead of every *.ar (development). Names: zipAutoInit. |
| Empty functions | 3 | not needed | - | Empty functions (mmVehicleForm::Cull, mmCNRSpeech::SetReadState and Abortf hooks). Names: nullsub_56, nullsub_70, nullsub_76. |
| C runtime | 352 | replaced | the platform C/C++ runtime | Microsoft C runtime (startup, heap, stdio, strings, multibyte, locale, maths, float conversion, environment, spawn, memcpy/memmove internals). Names: $done$19155, $I10_OUTPUT, $NORMAL_STATE$1535, __87except, ___add_12, ___addl, ___crtCompareStringA, ___crtGetEnvironmentStringsA, ___crtGetStringTypeA, ___crtLCMapStringA, ___crtMessageBoxA, ___crtsetenv, ___dtold, ___from_strstr_to_strchr, ___initmbctable, ___ld12mul, ___loctotime_t, ___mtold12, ___multtenpow12, ___sbh_alloc_block, ___sbh_alloc_new_group, ___sbh_alloc_new_region, ___sbh_find_block, ___sbh_free_block, ___sbh_heap_init, ___sbh_resize_block, ___setargv, ___shl_12, ___shr_12, ___strgtold12, ___tzset, ___wtomb_environ, __abstract_cw, __access, __allmul, __alloc_osfhnd, __alloca_probe, __allshl, __amsg_exit, __aulldiv, __aullrem, __callnewh, __cenvarg, __cfltcvt, __cfltcvt_init, __cftoe, __cftoe_g, __cftof, __cftof_g, __cftog, __chsize, __CIacos, __CIasin, __CIfmod, __cinit, __cintrindisp2, __CIpow, __close, __clrfp, __ComputeCpuSpeed, __control87, __controlfp, __CopyMan, __copysign, __cropzeros, __ctrlfp, __cwild, __d_inttype, __decomp, __dosmaperr, __dospawn, __errcode, __exit, __fassign, __FF_MSGBANNER, __filbuf, __FillZeroMan, __fload_withFB, __flsbuf, __fltin, __fltout, __forcdecpt, __fpclass, __fptostr, __fptrap, __free_osfhnd, __freebuf, __frnd, __fsopen, __ftbuf, __ftol, __get_fname, __get_osfhandle, __getbuf, __getpath, __getstream, __handle_qnan1, __heap_alloc, __heap_init, __hextodec, __hw_cw, __IncMan, __initterm, __input, __ioinit, __isatty, __isctype, __isindst, __ismbblead, __IsZeroMan, __itoa, __ld12cvt, __lseek, __math_exit, __mbschr, __mbscmp, __mbsdec, __mbsicmp, __mbsnbcpy, __mbsnbicoll, __mbspbrk, __mbsrchr, __mkdir, __ms_p5_mp_test_fdiv, __ms_p5_test_fdiv, __msize, __nh_malloc, __NMSG_WRITE, __onexit, __openfile, __output, __positive, __powhlp, __read, __RoundMan, __set_errno, __set_exp, __set_osfhnd, __set_statfp, __setargv, __setdefaultprecision, __setenvp, __setmbcp, __setmode, __shift, __ShrMan, __sopen, __spawnve, __spawnvpe, __sptype, __startOneArgErrorHandling, __startTwoArgErrorHandling, __statfp, __stbuf, __strcmpi, __strdup, __strupr, __trandisp2, __tzset, __umatherr, __un_inc, __unlink, __whiteout, __wincmdln, __write, __ZeroTail, _abort, _atexit, _atof, _atoi, _atol, _bsearch, _calloc, _ceil, _CPtoLCID, _exit, _fclose, _fgets, _floor, _flush, _fopen, _fprintf, _free, _getenv, _gmtime, _isspace, _localtime, _main, _malloc, _mbstowcs, _mbtowc, _memcpy, _memset, _modf, _printf, _qsort, _realloc, _remove, _sprintf, _sscanf, _strchr, _strcmp, _strlen, _strncmp, _strncnt, _strncpy, _strpbrk, _strrchr, _strstr, _strtok, _system, _time, _tolower, _ungetc, _vsprintf, _wcscat, _wcscmp, _wcscpy, _wcslen, _wcstombs, _wctomb, add, aftercopy, align_dest, at_done, checkinexact, checkrng, chk_null, chk_null2, cintrinexit, comexecmd, compare_loop, copy_environ, copy_tail_loop, cvtdate, cwdefault, dest_align_loop, dest_align_loop_end, dodwords, doexit, done, done2, doneeq, donene, dopartial, doword, dstdone, dstnext, empty_str2, fast_error_exit, fill_dwords_with_EOS, fill_tail, fill_tail_end, fill_tail_end1, fill_tail_zero_bytes, fill_with_EOS_dwords, fill_with_EOS_loop, find, findenv, findnext, finish, finish_loop, first_char_found, firstbig, get_int64_arg, get_int_arg, get_short_arg, getSystemCP, hard, haveerror, haveoverflow, haveunderflow, in_loop, inRange, lastbig, lastpage, LeadDown1, LeadDown2, LeadDown3, LeadUp1, LeadUp2, LeadUp3, listdone, listnext, loop_start, main_loop, match, MORE32, movretval, not_found, notclocale, nullsub_21, nullsub_22, nullsub_222, okay, operator_delete, operator_new, parse_cmdline, probepages, restoreCW, resume2, RETZERO, sAcquireBuffer, save2arg, setcw, setSBCS, setSBUpLow, setUnicodeMode, shortsort, sIsBuffer, sort, sqrtf, src_misaligned, start, strchr_call, swap, tail_loop_start, toend, TrailDown0, TrailDown1, TrailDown2, TrailDown3, TrailUp0, TrailUp1, TrailUp2, TrailUp3, two_first_chars_equal, UnwindDown0, UnwindDown1, UnwindDown2, UnwindDown3, UnwindDown4, UnwindDown5, UnwindDown6, UnwindDown7, UnwindUp0, UnwindUp1, UnwindUp2, UnwindUp3, UnwindUp4, UnwindUp5, UnwindUp6, UnwindUp7, wcsncnt, wpmax, wpmin, write_char, write_multi_char, write_string, x_ismbbtype, xtoa. |
| libjpeg | 130 | replaced | OpenMM2's JPEG decoder (`asset/`) | IJG libjpeg 6 decompressor (the menu and loading .jpg files). Names: access_virt_barray, access_virt_sarray, alloc_barray, alloc_funny_pointers, alloc_large, alloc_sarray, alloc_small, build_ycc_rgb_table, consume_markers, decode_mcu, decompress_onepass, default_decompress_parms, do_barray_io, do_sarray_io, dummy_consume_data, emit_message, error_exit, examine_app0, examine_app14, fill_input_buffer, finish_input_pass, finish_output_pass, first_marker, format_message, free_pool, fullsize_upsample, get_dht, get_dqt, get_dri, get_interesting_appn, get_sof, get_soi, get_sos, gray_rgb_convert, grayscale_convert, h2v1_fancy_upsample, h2v1_upsample, h2v2_fancy_upsample, h2v2_upsample, init_source, initial_setup, int_upsample, jcopy_sample_rows, jdiv_round_up, jinit_color_deconverter, jinit_d_coef_controller, jinit_d_main_controller, jinit_d_post_controller, jinit_huff_decoder, jinit_input_controller, jinit_inverse_dct, jinit_marker_reader, jinit_master_decompress, jinit_memory_mgr, jinit_upsampler, jpeg_abort, jpeg_alloc_huff_table, jpeg_alloc_quant_table, jpeg_calc_output_dimensions, jpeg_consume_input, jpeg_CreateDecompress, jpeg_destroy, jpeg_destroy_decompress, jpeg_fill_bit_buffer, jpeg_finish_decompress, jpeg_free_large, jpeg_free_small, jpeg_get_large, jpeg_get_small, jpeg_huff_decode, jpeg_idct_float, jpeg_idct_ifast, jpeg_idct_islow, jpeg_make_d_derived_tbl, jpeg_mem_available, jpeg_mem_init, jpeg_mem_term, jpeg_open_backing_store, jpeg_read_header, jpeg_read_scanlines, jpeg_resync_to_restart, jpeg_start_decompress, jpeg_std_error, jpeg_stdio_src, jround_up, jzero_far, latch_quant_tables, main_loop_entrance, make_funny_pointers, master_selection, next_marker, noop_upsample, null_convert, out_of_memory, output_message, output_pass_setup, per_scan_setup, post_process_1pass, prepare_for_output_pass, prepare_range_limit_table, process_data_context_main, process_data_simple_main, process_restart, read_markers, read_restart_marker, realize_virt_arrays, request_virt_barray, request_virt_sarray, reset_error_mgr, reset_input_controller, reset_marker_reader, self_destruct, sep_upsample, set_bottom_pointers, set_wraparound_pointers, skip_input_data, skip_variable, start_iMCU_row, start_input_pass, start_output_pass, start_pass, start_pass_dcolor, start_pass_dpost, start_pass_huff_decoder, start_pass_main, start_pass_upsample, term_source, use_merged_upsample, ycc_rgb_convert, ycck_cmyk_convert. |
| zlib | 19 | replaced | miniz (`vfs/DaveArchive`) | zlib inflate for PKZIP add-on archives (zipFile). Names: adler32, inflate, inflate_blocks, inflate_blocks_free, inflate_blocks_new, inflate_blocks_reset, inflate_codes, inflate_codes_free, inflate_codes_new, inflate_fast, inflate_flush, inflate_trees_bits, inflate_trees_dynamic, inflate_trees_fixed, inflateEnd, inflateInit2_, inflateReset, zcalloc, zcfree. |
| Exception handling | 543 | not needed | - | C++ exception handling: the per-function unwind handlers (labels ending in _SEH, Unwind@address, SEH_address) and the runtime's frame handler, catch, unwind and translator machinery. Each _SEH handler only destroys the locals of the function it is named after, which is audited with that function. Names: 480 per-function handlers; runtime: $ExceptionContinuation$16667, $ReturnPoint$16567, AdjustPointer, BuildCatchObject, CW_is_restored, CallCatchBlock, CatchGuardHandler, CatchIt, ExceptMain, FindHandler, FindHandlerForForeignException, RtlUnwind, TranslatorGuardHandler, TypeMatch, _CallCatchBlock2, _CallSETranslator, _ContinueErrorHandling, _GetRangeOfTrysToCheck, _JumpToContinuation, _NLG_Continue, _UnwindNestedFrames, _ValidateExecute, _ValidateRead, _ValidateWrite, __ArrayUnwind, __CallSettingFrame@12, __CxxUnhandledExceptionFilter, __NLG_Dispatch, __NLG_Go, __NLG_Notify, __NLG_Notify1, __NLG_Return, __NLG_Return2, __XcptFilter, ___CxxFrameHandler, ___FrameUnwindToState, ___InternalCxxFrameHandler, __abnormal_termination, __except1, __global_unwind2, __handle_exc, __local_unwind2, __purecall, __raise_exc, __unwind_handler, _raise, 'eh_vector_constructor_iterator', 'eh_vector_destructor_iterator', 'vcall'{80,{flat}}'_}', 'vcall'{84,{flat}}'_}', 'vector_constructor_iterator', gu_return, lh_bagit, lh_continue, lh_dismiss, lh_return, lh_top, lh_unwinding, lu_done, lu_top, siglookup, uh_return, xcptlookup. |

## Unnamed functions

The decompiler splits a function where it believes a call does not return (Displayf, Quitf, Errorf, the exception helpers), and the rest of the function appears as a separate unnamed function; switch tables and small inlined helpers (static initialisers of global tables, vector set/negate) appear the same way. Each is listed by address with the named function it belongs to: the fall-through predecessor, or its only caller. Those of other subsystems are handed over: their records must read these pieces with the function, since the decompile of the named function stops where the piece starts.

| Belongs to | Subsystem | Status | Addresses |
| --- | --- | --- | --- |
| `Main` | game-flow | handed over | 0x4012b2, 0x40136c, 0x401496, 0x4014f2 |
| `mmGame::Init` | game-flow | handed over | 0x41280c, 0x412822, 0x412853, 0x41297a, 0x4129b1, 0x4131ee, switch table 412eb4 |
| `mmGame::UpdateDebugInput` | game-flow | handed over | 0x414442 |
| `mmGame::UpdatePaused` | game-flow | handed over | 0x414582 |
| `mmGameManager::mmGameManager` | game-flow | handed over | 0x402c46, 0x402df1, switch table 402b0e, switch table 402c92 |
| `mmGameMulti::BeDone` | game-flow | handed over | 0x43a4e5, 0x43a51c, 0x43a546, 0x43a55c |
| `mmGameMulti::GameMessageCB` | game-flow | handed over | 0x439c90, 0x43a144, 0x43a19a, 0x43a1f1, 0x43a341, 0x43dc90 |
| `mmGameMulti::RegisterMapNetObjects` | game-flow | handed over | 0x43ae96 |
| `mmGameMulti::SendLobbyResults`, `mmMultiCR::SendLobbyResults` | game-flow | handed over | 0x572b90 |
| `mmGameMulti::SystemMessageCB` | game-flow | handed over | 0x43989b, 0x439ace |
| `mmGameMulti::Update` | game-flow | handed over | 0x43a942 |
| `mmMultiCR::DisplayTimeWarning` | game-flow | handed over | 0x426ef5 |
| `mmMultiCR::GameMessage` | game-flow | handed over | 0x426c66 |
| `mmMultiCR::GetRandomPoints` | game-flow | handed over | 0x424cd0 |
| `mmMultiCR::LoadSets` | game-flow | handed over | 0x42484a |
| `mmMultiCR::UpdateGame` | game-flow | handed over | 0x425361 |
| `mmMultiCircuit::GameMessage` | game-flow | handed over | 0x4234b6 |
| `mmMultiCircuit::GameMessage`, `mmMultiCircuit::UpdateGame` | game-flow | handed over | 0x423500, 0x423530 |
| `mmMultiCircuit::Init` | game-flow | handed over | 0x421d80 |
| `mmMultiCircuit::UpdateGame` | game-flow | handed over | 0x422d69 |
| `mmMultiCircuit::mmMultiCircuit`, `mmMultiCircuit::~mmMultiCircuit` | game-flow | handed over | 0x423670 |
| `mmMultiRace::GameMessage` | game-flow | handed over | 0x429a95, 0x429b8e |
| `mmMultiRoam::UpdateGame` | game-flow | handed over | 0x427e81 |
| `mmNetObject::mmNetObject` | game-flow | handed over | 0x43ddd0 |
| `mmPopup::ChatCB` | game-flow | handed over | 0x42b54c |
| `mmSingleBlitz::UpdateGame` | game-flow | handed over | 0x41bd89 |
| `mmSingleCircuit::UpdateGame` | game-flow | handed over | 0x41d461 |
| `mmSingleStunt::UpdateCorner` | game-flow | handed over | 0x418fe9 |
| `mmSingleStunt::UpdateEvade` | game-flow | handed over | 0x417012 |
| `mmSingleStunt::UpdateJump` | game-flow | handed over | 0x417ae5 |
| `mmSingleStunt::UpdateStop` | game-flow | handed over | 0x419c71 |
| `mmWaypoints::DisplayHUDMessage`, `mmWaypoints::Reset`, `mmWaypoints::Update` | game-flow | handed over | 0x4361d0, 0x43625d |
| `mmWaypoints::LoadCSV` | game-flow | handed over | 0x434cf0, 0x434f48, 0x4351a2 |
| `netZoneScore::~netZoneScore` | game-flow | handed over | 0x572bc0 |
| `aiCTFRacer::Update` | ai | handed over | 0x554605 |
| `aiGoalAvoidPlayer::AvoidPlayer` | ai | handed over | 0x5690c0 |
| `aiGoalAvoidPlayer::Update` | ai | handed over | 0x56b07b |
| `aiGoalRandomDrive::OkayToEnterIntersection` | ai | handed over | 0x56d85a, 0x56d882, 0x56d88d |
| `aiGoalRandomDrive::Reset`, `aiGoalRandomDrive::Update`, `aiGoalRegainRail::Update` | ai | handed over | 0x5686e0 |
| `aiGoalRandomDrive::SolveRailType` | ai | handed over | 0x56ddf7 |
| `aiGoalRegainRail::Reset` | ai | handed over | 0x56b831 |
| `aiIntersection::RemoveFromStopSignCntl` | ai | handed over | 0x54a434 |
| `aiMap::Init` | ai | handed over | 0x534fed, 0x535126, 0x535140, 0x5351e3 |
| `aiMap::RemoveAmbient` | ai | handed over | 0x53a64b |
| `aiMap::RemovePedestrian` | ai | handed over | 0x539db6 |
| `aiMap::Reset` | ai | handed over | 0x536a65, 0x536b99, 0x536c21, 0x536dd9 |
| `aiMap::Update` | ai | handed over | 0x5370aa, 0x53716a, 0x53721c, 0x5372b2 |
| `aiPath::AddAmbPlayer` | ai | handed over | 0x547af5 |
| `aiPath::AddPedPlayer` | ai | handed over | 0x547bb5 |
| `aiPath::RemAmbPlayer` | ai | handed over | 0x547a91 |
| `aiPath::RemPedPlayer` | ai | handed over | 0x547b51 |
| `aiPath::RemovePedestrian` | ai | handed over | 0x549956 |
| `aiPath::UpdateAmbients` | ai | handed over | 0x544075, 0x544119 |
| `aiPath::UpdatePedestrians` | ai | handed over | 0x5441c9 |
| `aiPoliceOfficer::Push` | ai | handed over | 0x53e74e |
| `aiRouteRacer::Init` | ai | handed over | 0x53d336, 0x53d35f |
| `aiStuck::'scalar_deleting_destructor'` | ai | handed over | 0x56fca0, 0x56fcb0, 0x56fcc0 |
| `aiStuck::aiStuck` | ai | handed over | 0x56f9d0 |
| `aiTrafficLightInstance::DrawGlow` | ai | handed over | 0x53cd09 |
| `aiVehicleAmbient::Type` | ai | handed over | 0x551d00, 0x551d20, 0x551d30, 0x551d50, 0x551d70, 0x551d80 |
| `aiVehicleAmbient::Update` | ai | handed over | 0x5516b2, 0x551712 |
| `aiVehiclePhysics::CalcRoadTarget` | ai | handed over | 0x567c70, 0x567ce0, 0x567cf0, 0x567d00, 0x567d10, 0x567d30, 0x567d50, 0x567d70, 0x567d90, 0x567da0, 0x567dc0 |
| `aiVehiclePhysics::LocateWayPtFromRoad` | ai | handed over | 0x55d896 |
| `aiVehicleSpline::DistanceToIntersection` | ai | handed over | 0x56a2e0 |
| `aiVehicleSpline::DistanceToVehicle` | ai | handed over | 0x56a002, 0x56a0d1, 0x56a194, 0x56a26a |
| `Spline::'scalar_deleting_destructor'` | world-objects | handed over | 0x5232a0, 0x5232b0, 0x5232c0, 0x5232e0, 0x5232f0, 0x523300 |
| `aiCableCar::DistanceToIntersection` | world-objects | handed over | 0x540bac |
| `aiCableCar::OkayToEnterIntersection` | world-objects | handed over | 0x540b51, 0x540b66, 0x540b71 |
| `aiCableCar::SolveRailType` | world-objects | handed over | 0x540c25 |
| `aiSubway::DistanceToIntersection` | world-objects | handed over | 0x542b0f |
| `aiSubway::SolveRailType` | world-objects | handed over | 0x542bf6 |
| `gizBridgeMgr::Init` | world-objects | handed over | 0x577d10, 0x577e30, 0x577ee0 |
| `ptxGlass::CreateShards` | world-objects | handed over | 0x462063, 0x46207f, 0x4620bd |
| `Vector3::GetVector2` | vehicle-physics | handed over | 0x4c013a |
| `dgPhysManager::CollideTerrain` | vehicle-physics | handed over | 0x469ea1, 0x46ac70, 0x46b2d0, 0x46b390 |
| `dgTrailerJoint::DoJointLimits`, `phInertialCS::Update` | vehicle-physics | handed over | 0x479620, 0x479690, 0x479830, 0x479970, 0x479bc0, 0x479d60 |
| `phBoundPolygonal::FindImpactsPolyToPoly` | vehicle-physics | handed over | 0x489ed0 |
| `phCollision::TestBoundForce` | vehicle-physics | handed over | 0x475b5a |
| `vehEngine::Update` | vehicle-physics | handed over | 0x4d9067 |
| `vehSplash::vehSplash` | vehicle-physics | handed over | 0x4d6a60 |
| `vehStuck::Init`, `vehStuck::vehStuck` | vehicle-physics | handed over | 0x4d6040 |
| `cityPropulator::Propulate` | props-fx | handed over | 0x45d3b0, 0x45d460 |
| `dgBangerData::AdjustPrim` | props-fx | handed over | 0x440650, 0x440670, 0x440680, 0x4406a0, 0x4406c0, 0x4406d0 |
| `dgBangerData::InitBound` | props-fx | handed over | 0x4413b5 |
| `fxTexelDamage::ApplyDamage` | props-fx | handed over | 0x5923c0, 0x592860 |
| `InstallTextureVariantHandler` | city-render | handed over | 0x443020 |
| `cityLevel::GetVisitList` | city-render | handed over | 0x4471d0 |
| `cityLevel::InitFullProbe` | city-render | handed over | 0x4464a6, 0x44680a |
| `cityLevel::Load` | city-render | handed over | 0x444078, 0x44468a, 0x44477c, 0x4447d6, 0x444b08, 0x444c6e, 0x444e98, 0x44507a |
| `cityLevel::SetObjectDetail` | city-render | handed over | 0x443f16 |
| `crAnimFrame::~crAnimFrame` | city-render | handed over | 0x57de20 |
| `gfxBitmap::Load` | city-render | handed over | 0x4ae750, 0x4ae7c0, 0x4ae840 |
| `gfxBitmap::Load`, `gfxTexture::Load` | city-render | handed over | 0x4ad530, 0x4ad570, 0x4ad5d0 |
| `gfxPrepareImage` | city-render | handed over | 0x442fb0 |
| `gfxTexture::Create` | city-render | handed over | 0x4acff0 |
| `gfxTexture::GetColor` | city-render | handed over | 0x45d1a0, 0x45d1d0, 0x45d1e0 |
| `gfxTexture::InitCache` | city-render | handed over | 0x4ad93d, 0x4ada20, 0x4ada56, 0x4adab8 |
| `gfxTexture::Load` | city-render | handed over | 0x4ad450, 0x4ad490, 0x4ad4f0, 0x4ad630, 0x4ad660, 0x4ad6a0, 0x4ad6c0, 0x4ad6f0 |
| `lvlInstance::GetLightInfo` | city-render | handed over | 0x4630d0, 0x4630f0, 0x463100 |
| `lvlInstance::InitBoundTerrainLocal` | city-render | handed over | 0x464140 |
| `lvlMultiRoomInstance::Create` | city-render | handed over | 0x467f34 |
| `lvlSDL::Enumerate` | city-render | handed over | 0x45bf05 |
| `modShader::Load` | city-render | handed over | 0x4a3e90, 0x4a3ef0 |
| `pedAnimation::Load` | city-render | handed over | 0x57ab00, 0x57ab20, 0x57ab30 |
| `pedAnimationInstance::Draw` | city-render | handed over | 0x57b410 |
| vglBegin and 4 more callers | city-render | handed over | 0x4a5620, 0x4a5780, 0x4a5910, 0x4a59b0 |
| `asCamera::'vector_deleting_destructor'` | hud-views | handed over | 0x4a32b0, 0x4a32c0, 0x4a32d0 |
| `asViewCS::Update` | hud-views | handed over | 0x596562 |
| `mmHUD::Init` | hud-views | handed over | 0x42d8f5 |
| `mmPositions::Load` | hud-views | handed over | 0x52a2b0 |
| `mmViewMgr::SetViewSetting` | hud-views | handed over | 0x4320bd |
| `ControlSetup::ActivateDeviceOptions` | frontend | handed over | 0x502342 |
| `ControlSetup::ControlSetup` | frontend | handed over | 0x534420 |
| `ControlSetup::LaunchJoyCpl` | frontend | handed over | 0x5025db, 0x50260c |
| `Dialog_Eject::BootButtonCB` | frontend | handed over | 0x4f95cf |
| `Dialog_Replay::DeleteCB` | frontend | handed over | 0x4fa9d8 |
| `Dialog_Serial::BuildComs` | frontend | handed over | 0x4fe45a |
| `GraphicsOptions::GraphicsOptions` | frontend | handed over | 0x4f4f30 |
| `GraphicsOptions::GraphicsOptions`, `GraphicsOptions::SetResolution` | frontend | handed over | 0x4f4b60 |
| `MainMenu::'scalar_deleting_destructor'` | frontend | handed over | 0x506af0, 0x506b00, 0x506b10, 0x506b40, 0x506b50, 0x506b60, 0x506b90, 0x506ba0, 0x506bb0 |
| `MenuManager::GetFGColor` | frontend | handed over | 0x4e4f10 |
| `MenuManager::GetFont` | frontend | handed over | 0x4e4d3d |
| `MenuManager::ScanGlobalKeys` | frontend | handed over | 0x4e5755 |
| `NetSelectMenu::BuildComs` | frontend | handed over | 0x504cbe |
| `NetSelectMenu::NetNameCB` | frontend | handed over | 0x505024 |
| `PUGraphics::PUGraphics` | frontend | handed over | 0x509f50 |
| `PURoster::BootButtonCB` | frontend | handed over | 0x50aaa2 |
| `RaceMenuBase::ChangeLocalVals` | frontend | handed over | 0x5088a1 |
| `RaceMenuBase::CityChange` | frontend | handed over | 0x508372 |
| `RaceMenuBase::SetRW` | frontend | handed over | 0x50829d |
| `RaceMenuBase::SetStateRace` | frontend | handed over | 0x508c7a |
| `UICWArray::Init` | frontend | handed over | 0x5ac9f5 |
| `UICompositeScroll::Action` | frontend | handed over | 0x4ebcde |
| `UICompositeScroll::Init` | frontend | handed over | 0x5ac735 |
| `UITextDropdown::CaptureAction` | frontend | handed over | 0x4e847d |
| `mmInterface::GetSessionData` | frontend | handed over | 0x411642 |
| `mmInterface::InitLobby` | frontend | handed over | 0x4100ac |
| `mmInterface::LobbySwitch` | frontend | handed over | 0x40d78a |
| `mmInterface::MessageCallback`, `mmInterface::MessageCallback2`, `mmInterface::Update` | frontend | handed over | 0x411dd0 |
| `mmInterface::MessageCallback2` | frontend | handed over | 0x409d6b, 0x409e03, 0x409f8e, 0x40a0d6, switch table 409d5a |
| `mmInterface::MultiStartGame` | frontend | handed over | 0x410881, 0x410898 |
| `mmInterface::PlayerFillRecords` | frontend | handed over | 0x40f6fa |
| `mmInterface::PlayerRemove` | frontend | handed over | 0x40dc15 |
| `mmInterface::Switch` | frontend | handed over | 0x40d3f9 |
| `mmInterface::Update` | frontend | handed over | 0x40b2a9, 0x40b887 |
| `mmInterface::UpdateLobby` | frontend | handed over | 0x40cdbd |
| `mmInterface::mmInterface` | frontend | handed over | 0x412490 |
| `mmInterface_IsAutodialEnabled` | frontend | handed over | 0x40c0cb |
| `mmPlayerData::LoadBinary` | frontend | handed over | 0x52807e |
| `uiNavBar::Minimize` | frontend | handed over | 0x4e647c |
| `eqEventMonitor::Keyboard` | input-ff | handed over | 0x4a21fe |
| `eqEventMonitor::Mouse` | input-ff | handed over | 0x4a2183 |
| `ioMouse::Update` | input-ff | handed over | 0x4bb480, 0x4bb4a0, 0x4bb4b0 |
| `mmCollideFF::Init` | input-ff | handed over | 0x531747, 0x53175c, 0x531771, 0x53178d |
| `mmIODev::GetComponentType` | input-ff | handed over | 0x52f936 |
| `mmIODev::GetDescription` | input-ff | handed over | 0x52f8a6 |
| `mmInput::Init` | input-ff | handed over | 0x52c552 |
| `mmInput::PollStates` | input-ff | handed over | 0x52caff |
| `mmInput::ProcessJoyEvents` | input-ff | handed over | 0x52d4f5 |
| `mmInput::SetDefaultConfig` | input-ff | handed over | 0x52c361 |
| `mmJoyMan::Init` | input-ff | handed over | 0x52fd18 |
| `mmJoystick::Init` | input-ff | handed over | 0x530585, 0x5305a7 |
| `mmJoystick::PrintDeviceCaps` | input-ff | handed over | 0x530f21, 0x530f32, 0x530f43, 0x530f6e, 0x530f84, 0x531012, 0x531024, 0x531036, 0x531048, 0x53105a, 0x53106c, 0x53107e, 0x531090, 0x5310a2, 0x5310b4, 0x5310ba |
| `mmJoystick::ResetAxisCapture` | input-ff | handed over | 0x530b97 |
| `mmJoystick::SetDeadZone` | input-ff | handed over | 0x5307c2, 0x5307f1 |
| `mmJoystick::inputPrepareDevice` | input-ff | handed over | 0x530623, 0x53066c, 0x5306c1, 0x530704 |
| `mmMouseSteerBar::'scalar_deleting_destructor'` | input-ff | handed over | 0x534440 |
| `AudCreature::UpdateAttenuation` | audio | handed over | 0x512a20, 0x512a40, 0x512a50 |
| `AudImpact::GetAudImpactDataPtr` | audio | handed over | 0x511330, 0x511350, 0x511360 |
| `DMusicObject::CreateComposer` | audio | handed over | 0x516e10 |
| `DMusicObject::FindMSSoftWareSynth` | audio | handed over | 0x5165e2 |
| `DirSnd::SetDeviceRating` | audio | handed over | 0x5a54d7 |
| `EchoEffect::SetFrequency` | audio | handed over | 0x5a36c8, 0x5a36e3, 0x5a36fe, 0x5a3719 |
| `MixerCTL::GetErrorMessage` | audio | handed over | 0x51cb75 |
| `aiEngineAudio::~aiEngineAudio` | audio | handed over | 0x4da890, 0x4da8b0, 0x4da8c0 |
| `audMIDI::GetStatus` | audio | handed over | 0x5a484d |
| `audManager::AllocControl` | audio | handed over | 0x5a1d06 |
| `audManager::FreeControl` | audio | handed over | 0x5a1f7e |
| `audManager::MoveToActive` | audio | handed over | 0x5a1541 |
| `audManager::SetMaxConcurrent` | audio | handed over | 0x5a1ba5 |
| `audManager::StopAllSounds` | audio | handed over | 0x5a17f6 |
| `audManager::Update` | audio | handed over | 0x5a1146 |
| `audObject::CreateEmptyObject` | audio | handed over | 0x5a2441 |
| `vehHornAudio::UnAssignSounds` | audio | handed over | 0x4db390, 0x4db3b0, 0x4db3c0 |
| `vehSurfaceAudio::UpdateEcho` | audio | handed over | 0x4e0700 |
| `IO_EVENT_INPUT`, `IO_EVENT_KEYDOWN`, `eqEventHandler::Update` | input-ff | handed over | 0x4a1bd6 |
| `RadialGauge::Cull and 35 more callers` | infrastructure (math) | ported | 0x43dd30 |
| `aiVehiclePhysics::CalcRoadTarget`, `dgTrailerJoint::DoJointLimits` | infrastructure (math) | ported | 0x567ca0 |
| `dgImpact::CalcCollision`, `dgImpact::CalcCollision`, `phImpact::CalcCollision` | infrastructure (math) | ported | 0x46c3c0 |
| `dgImpact::CalcCollision`, `phContactMgr::Calc2ImpactsFixed` | infrastructure (math) | ported | 0x46c3a0 |
| `lvlMaterial::Load and 4 more callers` | infrastructure (C runtime) | replaced | 0x5a8a00 |
| (no caller but the exception tables) | infrastructure (exceptions) | not needed | 0x4124c0, 0x41ff10, 0x526500 |
| $I10_OUTPUT and 10 more callers | infrastructure | replaced | 0x58b8b0 |
| $I10_OUTPUT and 11 more callers | infrastructure | replaced | 0x58b8c0 |
| `AgeDevice::Clear` | infrastructure | replaced | 0x4ba350 |
| `AgeDevice::Initialize` | infrastructure | replaced | 0x4ba830 |
| `AgeDevice::Initialize and 3 more callers` | infrastructure | replaced | 0x4b74a0, 0x4b76a0, 0x4b78c0, 0x4b7ae0, 0x4b7d00, 0x4b7f20, 0x4b8140, 0x4b8360, 0x4b8580 |
| `AgeDevice::SetRenderState`, `AgeDevice::SetTexture`, `AgeDevice::SetTextureStageState` | infrastructure | replaced | 0x4b7440, 0x4b87a0, 0x4b88b0, 0x4b89c0, 0x4b8a40, 0x4b8ac0, 0x4b8b50, 0x4b9a80, 0x4b9bc0, 0x4b9c20 |
| `AgeDevice::SetRenderTarget` | infrastructure | replaced | 0x4ba4d0, 0x4ba510 |
| `AngelReadString` | infrastructure | ported | 0x534820, 0x534830, 0x534840 |
| `AutoDetect` | infrastructure | replaced | 0x4f4051, 0x4f41f1, 0x4f4200 |
| `AutoDetectCallback` | infrastructure | replaced | 0x4ac051, 0x4ac267, 0x4ac2a0, 0x4ac30f |
| `BuildCatchObject` | infrastructure | not needed | 0x581a72, 0x581a79 |
| BuildCatchObject and 4 more callers | infrastructure | not needed | 0x5859d2, 0x585a16 |
| BuildCatchObject and 7 more callers | infrastructure | not needed | 0x58596c, 0x5859ad |
| `CalibrateWatcher` | infrastructure | not needed | 0x502680, 0x5026b3, 0x5026d3 |
| `CallCatchBlock` | infrastructure | not needed | 0x585654 |
| `CallCatchBlock`, `FindHandler` | infrastructure | not needed | 0x581a6b, 0x585888 |
| `CheckGlobalMemory` | infrastructure | replaced | 0x402206 |
| `DebugLog` | infrastructure | not needed | 0x4c7d57, 0x4c7d78, 0x4c7dc3, 0x4c7dd8, 0x4c7e26 |
| `DecodeDIErrorMFlag` | infrastructure | replaced | 0x53145e |
| `DeviceCallback` | infrastructure | replaced | 0x4ac3ef, 0x4ac426, 0x4ac44d, 0x4ac51c |
| `DumpStackTraceback` | infrastructure | not needed | 0x4c7f96 |
| `EnumPlayersCallback` | infrastructure | replaced | 0x572717 |
| `EnumSessionCallback` | infrastructure | replaced | 0x572621 |
| `ErrorDisplay` | infrastructure | replaced | 0x51710f, 0x517125, 0x517143, 0x517161, 0x51717f, 0x51719d, 0x5171bb, 0x5171d9, 0x5171f7, 0x517215, 0x517233, 0x517251, 0x51726f, 0x51728d, 0x5172ab, 0x5172c9, 0x5172e7, 0x517305, 0x517323, 0x517341, 0x51735f, 0x51737d |
| `ExceptMain` | infrastructure | not needed | 0x402318 |
| `HeapAssert` | infrastructure | not needed | 0x576fe7 |
| `HelpWatcher` | infrastructure | not needed | 0x4e6044, 0x4e6068, 0x4e60b2, 0x4e60d2 |
| `InputWindowProc` | infrastructure | replaced | 0x4bb0c5 |
| InputWindowProc and 4 more callers | infrastructure | replaced | 0x4bb100 |
| LeadDown1 and 3 more callers | infrastructure | replaced | 0x58c8ee, 0x58c9a1 |
| LeadDown1 and 4 more callers | infrastructure | not needed | 0x58ca0e |
| LeadUp1 and 4 more callers | infrastructure | not needed | 0x58c876 |
| `LogStackTraceback` | infrastructure | not needed | 0x4c7f3c |
| `ResCallback` | infrastructure | replaced | 0x4ac765, 0x4ac7a6, 0x4ac7dc |
| `SetupResChoices` | infrastructure | replaced | 0x4f4da5 |
| `Stream::DumpOpenFiles` | infrastructure | not needed | 0x4c9983, 0x4c99a2 |
| `Timer::QuickTicks`, `Timer::Timer` | infrastructure | replaced | 0x4c7830 |
| `Timer::Timer` | infrastructure | replaced | 0x4c794d |
| `TrailUp3` | infrastructure | replaced | 0x58c8f9 |
| `TranslatorGuardHandler` | infrastructure | not needed | 0x581cb9 |
| `__87except`, `__umatherr` | infrastructure | replaced | 0x58e52b |
| `__ArrayUnwind` | infrastructure | not needed | 0x582161 |
| __CIacos and 3 more callers | infrastructure | replaced | 0x586fbc |
| __CIacos and 5 more callers | infrastructure | replaced | 0x586fa5, 0x58702e |
| `__CIacos`, `__CIasin` | infrastructure | replaced | 0x587018 |
| `__CIacos`, `_acos` | infrastructure | replaced | 0x582d8d |
| `__CIasin`, `_asin` | infrastructure | replaced | 0x58356d |
| `__CIpow`, `_pow` | infrastructure | replaced | 0x5821b2, 0x5821e1, 0x58222a, 0x582252, 0x582385, 0x586f90, 0x587079 |
| `__ComputeCpuSpeed` | infrastructure | replaced | 0x4c943b, 0x4c9460 |
| `___FrameUnwindToState` | infrastructure | not needed | 0x5854e0 |
| `___crtCompareStringA` | infrastructure | replaced | 0x58ea17, 0x58ea82, 0x58eac7 |
| `___crtGetEnvironmentStringsA`, `__cenvarg`, `_realloc` | infrastructure | replaced | 0x58db90, 0x58dc29, 0x58dc69, 0x58dcd6, 0x58dd0d, 0x58dd4e, 0x58dd59, 0x58dda5, 0x58de01, 0x58de6e, 0x58de95, switch table 58dbc5, switch table 58dd47 |
| `___crtGetStringTypeA` | infrastructure | replaced | 0x58bfe5 |
| `___crtLCMapStringA` | infrastructure | replaced | 0x58bdb5, 0x58be68 |
| `__cinit` | infrastructure | replaced | 0x5815a8 |
| `__fassign` | infrastructure | replaced | 0x58c479, 0x58c509, 0x58c564 |
| `__fassign`, `__fltin` | infrastructure | replaced | 0x58c463 |
| `__input`, `__whiteout` | infrastructure | replaced | 0x5895ff |
| `_memcpy` | infrastructure | replaced | 0x58c809 |
| 'eh_vector_constructor_iterator' | infrastructure | not needed | 0x58258c |
| 'eh_vector_destructor_iterator' | infrastructure | not needed | 0x582103 |
| `asFileIO::GetClassName` | infrastructure | replaced | 0x5979e0, 0x597a00, 0x597a10 |
| `asNetwork::CloseSession` | infrastructure | replaced | 0x57156e |
| `asNetwork::CreatePlayer` | infrastructure | replaced | 0x5709a4 |
| `asNetwork::CreateSession` | infrastructure | replaced | 0x57114a |
| `asNetwork::HandleSysMessage` | infrastructure | replaced | 0x571de1, 0x571e02, 0x571e34, 0x571e4d, 0x571e66, 0x571eb9, 0x571ec7, 0x571ed5, 0x571ee3, 0x571f11, 0x571f27, 0x571f3d, 0x571f63, 0x571f79, 0x571f8f, switch table 571e8f |
| `asNetwork::InitializeLobby` | infrastructure | replaced | 0x56fe7b |
| `asNetwork::JoinLobbySession` | infrastructure | replaced | 0x56ff03, 0x5700c9 |
| `asNetwork::JoinSession` | infrastructure | replaced | 0x572777 |
| `asNetwork::SealSession` | infrastructure | replaced | 0x5716eb |
| `asNetwork::Send` | infrastructure | replaced | 0x5723e0 |
| `asNetwork::SetProtocol` | infrastructure | replaced | 0x570657, 0x570741, 0x570857 |
| `asNetwork::SetSessionData` | infrastructure | replaced | 0x5719ed |
| `asNetwork::StopSessionsAsynch` | infrastructure | replaced | 0x571b84 |
| `asNetwork::UnSealSession` | infrastructure | replaced | 0x5717eb |
| `consume_markers`, `jinit_input_controller` | infrastructure | replaced | 0x498850 |
| `datAssetManager::EnumFiles` | infrastructure | replaced | 0x4c5b60, 0x4c5b80, 0x4c5b90 |
| `datParser::Write` | infrastructure | not needed | 0x4a8199 |
| `datStack::DoTraceback` | infrastructure | not needed | 0x4c758c, 0x4c759e |
| `error_exit` | infrastructure | replaced | 0x4986ad |
| `fast_error_exit` | infrastructure | replaced | 0x58209a |
| `gfxAutoDetect` | infrastructure | replaced | 0x4abec4, 0x4abf06 |
| `gfxLoadTexImage` | infrastructure | ported | 0x4b0b6d |
| `gfxLoadVideoDatabse` | infrastructure | replaced | 0x4ac697 |
| `gfxPacket::Draw`, `gfxPipeline::DrawIdxVB` | infrastructure | replaced | 0x4b73c0 |
| `gfxPacket::DrawList` | infrastructure | replaced | 0x4b48a0, 0x4b48c0, 0x4b48d0 |
| `gfxPacket::FreeAllVertexBuffers` | infrastructure | replaced | 0x4b4650 |
| `gfxPacket::RestoreAllVertexBuffers` | infrastructure | replaced | 0x4b46a6 |
| `gfxPipeline::BeginFrame` | infrastructure | replaced | 0x4aa213 |
| `gfxPipeline::ForceSetViewport` | infrastructure | replaced | 0x4b2f90, 0x4b2fb0, 0x4b2fc0, 0x4b2fe0, 0x4b3000, 0x4b3010 |
| `gfxPipeline::RenderIdx` | infrastructure | replaced | 0x4b6100, 0x4b61b0, 0x4b6260, 0x4b63b0, 0x4b6440, 0x4b6480, 0x4b64b0, 0x4b6540, 0x4b6820, 0x4b6950, 0x4b69e0, 0x4b6b10, 0x4b6ba0, 0x4b6cd0, 0x4b6d60, 0x4b6e90, 0x4b6f20, 0x4b7050, 0x4b70e0, 0x4b7210, 0x4b72a0 |
| `gfxPipeline::gfxWindowProc` | infrastructure | replaced | 0x4a8a01, 0x4a8a1a, 0x4a8a40 |
| `gfxReleaseFont` | infrastructure | replaced | 0x4aeb60 |
| `gfxRenderState::SetBlendSet` | infrastructure | replaced | 0x4b2cc0 |
| `inflate` | infrastructure | replaced | 0x574236 |
| `inflate_trees_bits`, `inflate_trees_dynamic` | infrastructure | replaced | 0x575c00 |
| `inputEnumEffectTypeProc` | infrastructure | replaced | 0x53152e |
| `jinit_color_deconverter` | infrastructure | replaced | 0x49dec9 |
| `jpeg_consume_input` | infrastructure | replaced | 0x497f52 |
| `mcHookman::Init` | infrastructure | not needed | 0x54a8c9, 0x54a8f7 |
| `memMemoryAllocator::DisplayUsed` | infrastructure | not needed | 0x5771d0 |
| `modGetStatic` | infrastructure | ported | 0x4a5310 |
| `rglEnableDisable` | infrastructure | not needed | 0x4a5fe6 |
| `start` | infrastructure | replaced | 0x582033 |
| `zipFile::Init` | infrastructure | ported | 0x5734f4, 0x57368d, 0x5736be, 0x5738ef, 0x573922, 0x5739e0 |
| `zipFile::Init`, `zipFile::Open` | infrastructure | ported | 0x573a20 |
| `zipMultiAutoInit` | infrastructure | ported | 0x572f30 |

## Function index

Every reachable named method of the classes above, for the coverage tool (`Class::Method`).

- AgeDevice: `AgeDevice::AddRef`, `AgeDevice::ApplyStateBlock`, `AgeDevice::BeginScene`, `AgeDevice::BeginStateBlock`, `AgeDevice::CaptureStateBlock`, `AgeDevice::Clear`, `AgeDevice::ComputeSphereVisibility`, `AgeDevice::CreateStateBlock`, `AgeDevice::DeleteStateBlock`, `AgeDevice::DrawIndexedPrimitive`, `AgeDevice::DrawIndexedPrimitiveStrided`, `AgeDevice::DrawIndexedPrimitiveVB`, `AgeDevice::DrawPrimitive`, `AgeDevice::DrawPrimitiveStrided`, `AgeDevice::DrawPrimitiveVB`, `AgeDevice::EndScene`, `AgeDevice::EndStateBlock`, `AgeDevice::EnumTextureFormats`, `AgeDevice::GetCaps`, `AgeDevice::GetClipPlane`, `AgeDevice::GetClipStatus`, `AgeDevice::GetDirect3D`, `AgeDevice::GetInfo`, `AgeDevice::GetLight`, `AgeDevice::GetLightEnable`, `AgeDevice::GetMaterial`, `AgeDevice::GetRenderState`, `AgeDevice::GetRenderTarget`, `AgeDevice::GetTexture`, `AgeDevice::GetTextureStageState`, `AgeDevice::GetTransform`, `AgeDevice::GetViewport`, `AgeDevice::Initialize`, `AgeDevice::LightEnable`, `AgeDevice::Load`, `AgeDevice::MultiplyTransform`, `AgeDevice::PreLoad`, `AgeDevice::QueryInterface`, `AgeDevice::Release`, `AgeDevice::SetClipPlane`, `AgeDevice::SetClipStatus`, `AgeDevice::SetLight`, `AgeDevice::SetMaterial`, `AgeDevice::SetRenderState`, `AgeDevice::SetRenderTarget`, `AgeDevice::SetTexture`, `AgeDevice::SetTextureStageState`, `AgeDevice::SetTransform`, `AgeDevice::SetViewport`, `AgeDevice::ValidateDevice`
- asCullable: `asCullable::Cull`
- asCullManager: `asCullManager::DeclareBitmap`, `asCullManager::DeclareCamera`, `asCullManager::DeclareCullable`, `asCullManager::DeclareCullable2D`, `asCullManager::DeclareCullable2DFG`, `asCullManager::Reset`, `asCullManager::Update`, `asCullManager::'vector_deleting_destructor'`, `asCullManager::asCullManager`, `asCullManager::~asCullManager`
- asFileIO: `asFileIO::AfterLoad`, `asFileIO::BeforeSave`, `asFileIO::FileIO`, `asFileIO::GetClassName`, `asFileIO::GetDirName`, `asFileIO::Load`, `asFileIO::Save`, `asFileIO::SetName`, `asFileIO::'scalar_deleting_destructor'`, `asFileIO::asFileIO`, `asFileIO::~asFileIO`
- asMeshCardInfo: `asMeshCardInfo::Draw`, `asMeshCardInfo::DrawShadows`, `asMeshCardInfo::Init`
- asNetObject: `asNetObject::Update`, `asNetObject::'scalar_deleting_destructor'`, `asNetObject::asNetObject`, `asNetObject::~asNetObject`
- asNetwork: `asNetwork::BootPlayer`, `asNetwork::CloseSession`, `asNetwork::CreateInterface`, `asNetwork::CreatePlayer`, `asNetwork::CreateSession`, `asNetwork::Deallocate`, `asNetwork::DestroyPlayer`, `asNetwork::Disconnect`, `asNetwork::GetEnumPlayer`, `asNetwork::GetEnumPlayerData`, `asNetwork::GetEnumSession`, `asNetwork::GetEnumSessionLock`, `asNetwork::GetGameVersion`, `asNetwork::GetNetworkCaps`, `asNetwork::GetNumModems`, `asNetwork::GetNumPlayers`, `asNetwork::GetNumSessions`, `asNetwork::GetPlayerData`, `asNetwork::GetPlayerID`, `asNetwork::GetPlayerName`, `asNetwork::GetPlayers`, `asNetwork::GetProtocols`, `asNetwork::GetSessionData`, `asNetwork::GetSessionsAsynch`, `asNetwork::GetSessionsSynch`, `asNetwork::GetTime`, `asNetwork::HandleAppMessage`, `asNetwork::HandleSysMessage`, `asNetwork::Initialize`, `asNetwork::InitializeLobby`, `asNetwork::JoinLobbySession`, `asNetwork::JoinSession`, `asNetwork::Logout`, `asNetwork::PollLobby`, `asNetwork::QueryModems`, `asNetwork::SealSession`, `asNetwork::Send`, `asNetwork::SetEnumSessionLock`, `asNetwork::SetPlayerData`, `asNetwork::SetProtocol`, `asNetwork::SetSessionData`, `asNetwork::SetTime`, `asNetwork::StopSessionsAsynch`, `asNetwork::UnSealSession`, `asNetwork::Update`, `asNetwork::WaitForLobbyConnection`, `asNetwork::asNetwork`, `asNetwork::~asNetwork`
- asNode: `asNode::AddChild`, `asNode::AfterLoad`, `asNode::BeforeSave`, `asNode::FileIO`, `asNode::GetChild`, `asNode::GetClassName`, `asNode::GetClassNameA`, `asNode::GetDirName`, `asNode::InsertChild`, `asNode::NumChildren`, `asNode::RemoveAllChildren`, `asNode::RemoveChild`, `asNode::ResChange`, `asNode::Reset`, `asNode::Save`, `asNode::SetName`, `asNode::Update`, `asNode::UpdatePaused`, `asNode::'vector_deleting_destructor'`, `asNode::asNode`, `asNode::~asNode`, `asNode::Load`
- asRoot: `asRoot::Init`, `asRoot::IsPaused`, `asRoot::Reset`, `asRoot::SetPause`, `asRoot::TogglePause`, `asRoot::Update`
- asSparkLut: `asSparkLut::Get`, `asSparkLut::Init`
- asUnderlay: `asUnderlay::Cull`, `asUnderlay::SetBitmap`, `asUnderlay::'scalar_deleting_destructor'`, `asUnderlay::asUnderlay`, `asUnderlay::~asUnderlay`
- Base: `Base::'scalar_deleting_destructor'`, `Base::~Base`
- CArrayList: `CArrayList::CArrayList`, `CArrayList::ReadBinary`, `CArrayList::~CArrayList`
- CFileStream: `CFileStream::AddRef`, `CFileStream::AddRef'adjustor{4}'`, `CFileStream::CFileStream`, `CFileStream::Clone`, `CFileStream::Close`, `CFileStream::Commit`, `CFileStream::CopyTo`, `CFileStream::GetLoader`, `CFileStream::GetNextPtr`, `CFileStream::LockRegion`, `CFileStream::Open`, `CFileStream::QueryInterface`, `CFileStream::QueryInterface'adjustor{4}'`, `CFileStream::Read`, `CFileStream::Release`, `CFileStream::Release'adjustor{4}'`, `CFileStream::Revert`, `CFileStream::Seek`, `CFileStream::SetNextPtr`, `CFileStream::SetSize`, `CFileStream::Stat`, `CFileStream::UnlockRegion`, `CFileStream::Write`, `CFileStream::~CFileStream`
- CLoader: `CLoader::AddCFileStreamToList`, `CLoader::AddRef`, `CLoader::AddRefP`, `CLoader::CLoader`, `CLoader::CacheObject`, `CLoader::ClearCache`, `CLoader::DestroyCFileStreamList`, `CLoader::EnableCache`, `CLoader::EnumObject`, `CLoader::GetObjectA`, `CLoader::Init`, `CLoader::LoadFromFile`, `CLoader::LoadFromMemory`, `CLoader::QueryInterface`, `CLoader::Release`, `CLoader::ReleaseObject`, `CLoader::ReleaseP`, `CLoader::ScanDirectory`, `CLoader::SetObject`, `CLoader::SetSearchDirectory`, `CLoader::~CLoader`
- CMemStream: `CMemStream::AddRef`, `CMemStream::AddRef'adjustor{4}'`, `CMemStream::CMemStream`, `CMemStream::Clone`, `CMemStream::Close`, `CMemStream::Commit`, `CMemStream::CopyTo`, `CMemStream::GetLoader`, `CMemStream::LockRegion`, `CMemStream::Open`, `CMemStream::QueryInterface`, `CMemStream::QueryInterface'adjustor{4}'`, `CMemStream::Read`, `CMemStream::Release`, `CMemStream::Release'adjustor{4}'`, `CMemStream::Revert`, `CMemStream::Seek`, `CMemStream::SetSize`, `CMemStream::Stat`, `CMemStream::UnlockRegion`, `CMemStream::Write`, `CMemStream::~CMemStream`
- datArgParser: `datArgParser::Get`, `datArgParser::Init`, `datArgParser::Kill`
- datAsciiTokenizer: `datAsciiTokenizer::GetDelimiter`, `datAsciiTokenizer::GetFloat`, `datAsciiTokenizer::GetInt`, `datAsciiTokenizer::GetVector`, `datAsciiTokenizer::MatchFloat`, `datAsciiTokenizer::MatchInt`, `datAsciiTokenizer::MatchVector`, `datAsciiTokenizer::Put`, `datAsciiTokenizer::PutDelimiter`
- datAssetManager: `datAssetManager::Create`, `datAssetManager::EnumFiles`, `datAssetManager::Exists`, `datAssetManager::FullPath`, `datAssetManager::Open`, `datAssetManager::SetPath`
- datBaseTokenizer: `datBaseTokenizer::CheckToken`, `datBaseTokenizer::GetToken`, `datBaseTokenizer::GetTokenCh`, `datBaseTokenizer::IgnoreToken`, `datBaseTokenizer::Init`, `datBaseTokenizer::MatchToken`, `datBaseTokenizer::PushBack`, `datBaseTokenizer::SkipComment`, `datBaseTokenizer::SkipToEndOfLine`, `datBaseTokenizer::Put`
- datBinTokenizer: `datBinTokenizer::GetDelimiter`, `datBinTokenizer::GetFloat`, `datBinTokenizer::GetInt`, `datBinTokenizer::GetVector`, `datBinTokenizer::MatchFloat`, `datBinTokenizer::MatchInt`, `datBinTokenizer::MatchVector`, `datBinTokenizer::Put`, `datBinTokenizer::PutDelimiter`
- datCallback: `datCallback::Call`, `datCallback::datCallback`
- datMemStream: `datMemStream::Read`, `datMemStream::Write`
- datMultiTokenizer: `datMultiTokenizer::GetReadTokenizer`, `datMultiTokenizer::datMultiTokenizer`, `datMultiTokenizer::GetWriteTokenizer`
- datOutput: `datOutput::CallAfterMsgBoxFunction`, `datOutput::CallBeforeMsgBoxFunction`
- datParser: `datParser::AddParser`, `datParser::AddRecord`, `datParser::AddValue`, `datParser::Load`, `datParser::Read`, `datParser::datParser`, `datParser::Indent`, `datParser::Save`, `datParser::Write`, `datParser::~datParser`
- datParserRecord: `datParserRecord::~datParserRecord`
- datRefCount: `datRefCount::DecRef`, `datRefCount::IncRef`, `datRefCount::'scalar_deleting_destructor'`, `datRefCount::datRefCount`, `datRefCount::~datRefCount`
- datReplay: `datReplay::GetInt`, `datReplay::RecordInt`
- datStack: `datStack::DoTraceback`, `datStack::LookupAddress`, `datStack::Traceback`
- datTimeManager: `datTimeManager::RealTime`, `datTimeManager::SetTempOverSampling`, `datTimeManager::Update`
- datTokenizer: `datTokenizer::datTokenizer`
- dgImpact: `dgImpact::CalcCollision`, `dgImpact::CalcImpact`
- dgStatePack: `dgStatePack::dgStatePack`, `dgStatePack::~dgStatePack`
- dgTreeRenderer: `dgTreeRenderer::AddTree`, `dgTreeRenderer::RenderTrees`, `dgTreeRenderer::dgTreeRenderer`
- EffectBase: `EffectBase::CreateDSoundBuffer`, `EffectBase::OriginalBufferPlaying`
- FixedHashEntry: `FixedHashEntry::FixedHashEntry`, `FixedHashEntry::~FixedHashEntry`
- gfxLight: `gfxLight::Reset`
- gfxMaterial: `gfxMaterial::Reset`
- gfxPacket: `gfxPacket::AllocateVertexBuffer`, `gfxPacket::AutoSetPacking`, `gfxPacket::BeginRef`, `gfxPacket::DoLock`, `gfxPacket::DoUnlock`, `gfxPacket::Draw`, `gfxPacket::DrawList`, `gfxPacket::FreeAllVertexBuffers`, `gfxPacket::GetPosition`, `gfxPacket::GetTexCoord`, `gfxPacket::GetTri`, `gfxPacket::MakeList`, `gfxPacket::Persist`, `gfxPacket::ReserveVertexBuffer`, `gfxPacket::RestoreAllVertexBuffers`, `gfxPacket::SetDefaultPacking`, `gfxPacket::SetPacking`, `gfxPacket::gfxPacket`, `gfxPacket::OrthoMap`
- gfxPipeline: `gfxPipeline::BeginFrame`, `gfxPipeline::BeginGfx2D`, `gfxPipeline::BeginGfx3D`, `gfxPipeline::BeginInternal`, `gfxPipeline::BeginScene`, `gfxPipeline::Clear`, `gfxPipeline::ClearRect`, `gfxPipeline::CopyClippedBitmap`, `gfxPipeline::CreateViewport`, `gfxPipeline::EndFrame`, `gfxPipeline::EndGfx`, `gfxPipeline::EndGfx2D`, `gfxPipeline::EndGfx3D`, `gfxPipeline::EndInternal`, `gfxPipeline::EndScene`, `gfxPipeline::EnumDDAdapters`, `gfxPipeline::ForceSetViewport`, `gfxPipeline::GetWidth`, `gfxPipeline::Render`, `gfxPipeline::RenderIdx`, `gfxPipeline::SetTitle`, `gfxPipeline::SetWindow`, `gfxPipeline::gfxEnumTexs`, `gfxPipeline::gfxEnumZ`, `gfxPipeline::gfxWindowCreate`, `gfxPipeline::CopyBitmap`, `gfxPipeline::Manage`, `gfxPipeline::SetRes`, `gfxPipeline::gfxWindowProc`
- gfxRenderState: `gfxRenderState::CheckSet`, `gfxRenderState::Default`, `gfxRenderState::DisableAllLights`, `gfxRenderState::GetLight`, `gfxRenderState::Init`, `gfxRenderState::LerpRGBA`, `gfxRenderState::LightEnable`, `gfxRenderState::Regenerate`, `gfxRenderState::SetBlendSet`, `gfxRenderState::SetCard`, `gfxRenderState::SetLight`, `gfxRenderState::SetTexMatrix`, `gfxRenderState::SetTexSource`, `gfxRenderState::SetTexTransform`, `gfxRenderState::DoFlush`, `gfxRenderState::SetCamera`
- gfxVertexBuffer: `gfxVertexBuffer::KillAll`, `gfxVertexBuffer::RestoreAll`, `gfxVertexBuffer::~gfxVertexBuffer`
- gfxViewport: `gfxViewport::DoFlush`, `gfxViewport::Ortho`, `gfxViewport::ResetWindow`, `gfxViewport::gfxViewport`, `gfxViewport::IsSphereVisible`, `gfxViewport::Perspective`, `gfxViewport::SetWindow`
- HashTable: `HashTable::Access`, `HashTable::AccessData`, `HashTable::AccessName`, `HashTable::ComputePrime`, `HashTable::Delete`, `HashTable::GetEmptySlot`, `HashTable::GetEntry`, `HashTable::Hash`, `HashTable::HashTable`, `HashTable::InitCommon`, `HashTable::InitFixed`, `HashTable::Insert`, `HashTable::Kill`, `HashTable::KillAll`, `HashTable::MakePermanent`, `HashTable::Recompute`, `HashTable::~HashTable`
- ltFlare: `ltFlare::Random`, `ltFlare::ltFlare`
- lvlSegment: `lvlSegment::Set`
- lvlSegmentInfo: `lvlSegmentInfo::AllocateState`
- MArray: `MArray::AddMenuData`, `MArray::Init`, `MArray::MArray`, `MArray::Read`, `MArray::~MArray`
- mcHookman: `mcHookman::DriveCircuit`, `mcHookman::DriveToHideout`, `mcHookman::Init`, `mcHookman::Reset`, `mcHookman::ReturnToCircuit`, `mcHookman::Update`, `mcHookman::mcHookman`, `mcHookman::~mcHookman`
- memMemoryAllocator: `memMemoryAllocator::Allocate`, `memMemoryAllocator::DisplayUsed`, `memMemoryAllocator::FindHeap`, `memMemoryAllocator::Free`, `memMemoryAllocator::GetStats`, `memMemoryAllocator::Init`, `memMemoryAllocator::Kill`, `memMemoryAllocator::Link`, `memMemoryAllocator::SanityCheck`, `memMemoryAllocator::Unlink`, `memMemoryAllocator::VerifyBlock`, `memMemoryAllocator::memMemoryAllocator`, `memMemoryAllocator::~memMemoryAllocator`
- memSafeHeap: `memSafeHeap::Activate`, `memSafeHeap::Deactivate`, `memSafeHeap::Init`, `memSafeHeap::Kill`, `memSafeHeap::Restart`, `memSafeHeap::memSafeHeap`, `memSafeHeap::~memSafeHeap`
- mmAccelCompute: `mmAccelCompute::Init`, `mmAccelCompute::SetLatest`
- mmCCData: `mmCCData::mmCCData`, `mmCCData::~mmCCData`
- mmRewardRecord: `mmRewardRecord::mmRewardRecord`, `mmRewardRecord::~mmRewardRecord`
- mmSlidingGauge: `mmSlidingGauge::Draw`, `mmSlidingGauge::Init`
- netScoreInfo: `netScoreInfo::netScoreInfo`
- parCsvFile: `parCsvFile::GetColumn`, `parCsvFile::GetFloat`, `parCsvFile::GetInt`, `parCsvFile::GetRow`, `parCsvFile::Kill`, `parCsvFile::Load`
- Quaternion: `Quaternion::FromMatrix`, `Quaternion::Slerp`
- Random: `Random::Number`, `Random::Seed`
- sfPointer: `sfPointer::Cull`, `sfPointer::ResChange`, `sfPointer::GetPointerHeight`, `sfPointer::WaitForRelease`, `sfPointer::Init`, `sfPointer::'scalar_deleting_destructor'`, `sfPointer::sfPointer`, `sfPointer::~sfPointer`, `sfPointer::Update`
- Stream: `Stream::AllocStream`, `Stream::Close`, `Stream::Create`, `Stream::DumpOpenFiles`, `Stream::Flush`, `Stream::PreLoad`, `Stream::PutCh`, `Stream::Size`, `Stream::Tell`, `Stream::Write`, `Stream::GetCh`, `Stream::Open`, `Stream::Read`, `Stream::Seek`
- string: `string::Contains`, `string::Init`, `string::SubString`, `string::operator+=`, `string::operator=`, `string::string`, `string::NumSubStrings`
- TerrainContact: `phInertialCS::TerrainContact::TerrainContact`
- Timer: `Timer::QuickTicks`, `Timer::Ticks`, `Timer::Timer`
- winDispatchable: `winDispatchable::'scalar_deleting_destructor'`, `winDispatchable::winDispatchable`, `winDispatchable::~winDispatchable`
- zipFile: `zipFile::EnumFiles`, `zipFile::Init`, `zipFile::Open`, `zipFile::internalRead`, `zipFile::internalSeek`, `zipFile::zipClose`, `zipFile::zipCreate`, `zipFile::zipEnumFiles`, `zipFile::zipFile`, `zipFile::zipOpen`, `zipFile::zipRead`, `zipFile::zipSeek`, `zipFile::zipSize`, `zipFile::zipWrite`, `zipFile::~zipFile`
- zipHandle: `zipHandle::Read`, `zipHandle::Seek`
