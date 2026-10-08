# Parity audit

Every source file is classified by whether it reproduces the original game,
and every function in a file that does is checked against MM2's own code
(MM2Recomp: the symbol-named disassembly and Ghidra project of the
unprotected `midtown2.exe` build 3393, see CLAUDE.md). This directory holds
the result: one record per audit area, listing each function, the MM2
function(s) it reproduces and the verdict.

## Classes

- **P**: reproduces original game behaviour. Every function is mapped to the
  MM2 function(s) it reproduces and compared operation by operation
  (constants, operation order, 32-bit float math, branch conditions, update
  order, defaults).
- **F**: reads an original data format. Checked against the MM2 loader that
  reads the same data, and against the retail data.
- **M**: mixed. The area record splits it per function into P/F work and
  OpenMM2-only work.
- **O**: OpenMM2-only, with no counterpart in the original: the platform
  layer, the Vulkan/OpenGL backends, the UDP/ENet network transport with
  UPnP/NAT-PMP/LAN discovery, game-source detection (disc, ISO, install), the
  setup screen and installer, the video decoders (the Indeo codec is not part
  of `midtown2.exe`) and the tools. These are not audited against MM2, but
  must not change game behaviour.

## Verdicts

Each function in a P, F or M file gets one verdict in its area record:

- **verified**: same behaviour as the cited MM2 function(s).
- **fixed**: differed; corrected (the record says what changed).
- **deviation**: differs on purpose (for example a modern convenience or a
  platform need); the record gives the reason.
- **inferred**: no MM2 counterpart found, or the behaviour lives outside
  `midtown2.exe` (DirectX, DirectMusic, Windows); the record says what it is
  based on.
- **open**: differs and is not fixed yet; the record says why.
- **openmm2**: OpenMM2-only code inside a P or M file (glue, rendering
  backends, networking).

Records describe the original in words and cite its function names (and
struct offsets where they identify a field). They never contain MM2Recomp's
generated assembly or C.

## Areas

| Area | Record | Scope |
| --- | --- | --- |
| phys-core | [phys-core.md](phys-core.md) | rigid bodies, world, collision response, joints, materials, `core/Math` |
| phys-bounds | [phys-bounds.md](phys-bounds.md) | collision bounds and the `.bnd` format |
| vehicle | [vehicle.md](vehicle.md) | `vehCarSim` and its parts, the player's car |
| ai-vehicles | [ai-vehicles.md](ai-vehicles.md) | traffic, opponents, police, road network, traffic bodies |
| ai-ambient-city | [ai-ambient-city.md](ai-ambient-city.md) | pedestrians, ambient routes, traffic lights, the city formats and PSDL collision |
| session | [session.md](session.md) | game modes, race rules, HUD, multiplayer rules, the race loop |
| camera-props | [camera-props.md](camera-props.md) | cameras, props (bangers), the vehicle catalog, profiles |
| rendering-fx | [rendering-fx.md](rendering-fx.md) | city/vehicle/AI rendering, textures, effects, render-state semantics, the intro |
| audio | [audio.md](audio.md) | game audio, voices, ambience, music direction |
| frontend-ui | [frontend-ui.md](frontend-ui.md) | menus, widgets, options |
| formats | [formats.md](formats.md) | asset and data formats, archives |
| OpenMM2-only | [openmm2-only.md](openmm2-only.md) | check that O files carry no game behaviour |

## Results (2026-10-08)

Every area was audited, most in two passes (the second pass fixed what the
first left open and wired features across areas). Rows per record, as each
record's summary counts them:

| Area | Rows | Verified | Fixed | Deviation | Inferred | Open | OpenMM2 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| phys-core | 206 | 75 | 96 | 9 | 10 | 1 | 15 |
| phys-bounds | 159 | 101 | 36 | 13 | 1 | 0 | 8 |
| vehicle | 141 | 76 | 42 | 6 | 2 | 1 | 14 |
| ai-vehicles | 170 | 71 | 67 | 3 | 2 | 1 | 26 |
| ai-ambient-city | 236 | 116 | 61 | 13 | 22 | 4 | 20 |
| session | 205 | 89 | 86 | 6 | 4 | 1 | 19 |
| camera-props | 195 | 107 | 57 | 11 | 2 | 0 | 18 |
| rendering-fx | 197 | 67 | 94 | 9 | 3 | 4 | 20 |
| audio | 265 | 69 | 155 | 11 | 10 | 0 | 20 |
| frontend-ui | 151 | 80 | 39 | 11 | 3 | 2 | 16 |
| formats | 119 | 23 | 44 | 11 | 7 | 0 | 34 |
| OpenMM2-only | 34 | 6 | 8 | 5 | 4 | 1 | 10 |
| **Total** | **2078** | **880** | **785** | **108** | **70** | **15** | **220** |

The open rows and each record's "Missing" table list what still differs
from MM2 and what porting it needs. The larger missing features are the
in-race options pages (PUOptions), force feedback, rebinding non-keyboard
controllers, the Cops and Robbers roster, traffic turn signals and
ambient traffic movers without a body.

## Manifest

Lines as of the start of the audit (files the audit added: as of when
they were added). Classes as above; `-` means OpenMM2-only with no audit
area. The OpenMM2-only check reclassified the
files whose area reads `openmm2-only` from O to M; their game behaviour is
audited in [openmm2-only.md](openmm2-only.md).

| File | Lines | Class | Area |
| --- | ---: | --- | --- |
| `src/ai/AmbientRoute.cpp` | 369 | P | ai-ambient-city |
| `src/ai/AmbientRoute.h` | 42 | P | ai-ambient-city |
| `src/ai/Course.cpp` | 898 | P | ai-vehicles |
| `src/ai/Course.h` | 190 | P | ai-vehicles |
| `src/ai/Driving.cpp` | 1081 | P | ai-vehicles |
| `src/ai/Driving.h` | 297 | P | ai-vehicles |
| `src/ai/Opponent.cpp` | 280 | P | ai-vehicles |
| `src/ai/Opponent.h` | 164 | P | ai-vehicles |
| `src/ai/Pedestrians.cpp` | 1095 | P | ai-ambient-city |
| `src/ai/Pedestrians.h` | 206 | P | ai-ambient-city |
| `src/ai/PlayerCar.h` | 26 | P | vehicle |
| `src/ai/Police.cpp` | 543 | P | ai-vehicles |
| `src/ai/Police.h` | 203 | P | ai-vehicles |
| `src/ai/Random.h` | 30 | P | ai-ambient-city |
| `src/ai/RoadNetwork.cpp` | 310 | P | ai-vehicles |
| `src/ai/RoadNetwork.h` | 169 | P | ai-vehicles |
| `src/ai/Traffic.cpp` | 1882 | P | ai-vehicles |
| `src/ai/Traffic.h` | 297 | P | ai-vehicles |
| `src/ai/TrafficLights.cpp` | 145 | P | ai-ambient-city |
| `src/ai/TrafficLights.h` | 89 | P | ai-ambient-city |
| `src/ai/VehicleControl.h` | 51 | P | ai-vehicles |
| `src/ai/VehicleData.cpp` | 53 | P | ai-vehicles |
| `src/ai/VehicleData.h` | 46 | P | ai-vehicles |
| `src/ai/World.cpp` | 238 | P | ai-ambient-city |
| `src/ai/World.h` | 113 | P | ai-ambient-city |
| `src/app/App.cpp` | 216 | M | openmm2-only |
| `src/app/App.h` | 11 | O | - |
| `src/app/CommandLine.cpp` | 111 | O | - |
| `src/app/CommandLine.h` | 38 | O | - |
| `src/app/Context.cpp` | 61 | M | openmm2-only |
| `src/app/Context.h` | 103 | O | - |
| `src/app/Controls.cpp` | 105 | P | session |
| `src/app/Controls.h` | 104 | P | session |
| `src/app/GameData.cpp` | 153 | O | - |
| `src/app/GameData.h` | 37 | O | - |
| `src/app/IntroScreen.cpp` | 233 | M | rendering-fx |
| `src/app/IntroScreen.h` | 14 | M | rendering-fx |
| `src/app/RaceScreen.cpp` | 1528 | M | session |
| `src/app/Screens.h` | 24 | O | - |
| `src/app/Settings.cpp` | 81 | M | frontend-ui |
| `src/app/Settings.h` | 63 | M | frontend-ui |
| `src/app/SetupScreen.cpp` | 248 | O | - |
| `src/app/frontend/Frontend.h` | 244 | P | frontend-ui |
| `src/app/frontend/FrontendScreen.cpp` | 871 | P | frontend-ui |
| `src/app/frontend/PagesCrash.cpp` | 233 | P | frontend-ui |
| `src/app/frontend/PagesMain.cpp` | 481 | P | frontend-ui |
| `src/app/frontend/PagesMulti.cpp` | 1078 | P | frontend-ui |
| `src/app/frontend/PagesOptions.cpp` | 998 | P | frontend-ui |
| `src/app/frontend/PagesRace.cpp` | 587 | P | frontend-ui |
| `src/app/frontend/PagesResults.cpp` | 155 | P | frontend-ui |
| `src/app/main.cpp` | 104 | O | - |
| `src/asset/Bound.cpp` | 304 | F | phys-bounds |
| `src/asset/Bound.h` | 100 | F | phys-bounds |
| `src/asset/Image.cpp` | 389 | F | formats |
| `src/asset/Image.h` | 100 | F | formats |
| `src/asset/Mtx.cpp` | 24 | F | formats |
| `src/asset/Mtx.h` | 34 | F | formats |
| `src/asset/Ped.cpp` | 842 | F | formats |
| `src/asset/Ped.h` | 238 | F | formats |
| `src/asset/Pkg.cpp` | 370 | F | formats |
| `src/asset/Pkg.h` | 134 | F | formats |
| `src/asset/Reader.h` | 94 | F | formats |
| `src/asset/VehicleModel.cpp` | 73 | F | formats |
| `src/asset/VehicleModel.h` | 59 | F | formats |
| `src/audio/AngelRandom.cpp` | 90 | P | audio |
| `src/audio/AngelRandom.h` | 44 | P | audio |
| `src/audio/AngelUnits.cpp` | 33 | P | audio |
| `src/audio/AngelUnits.h` | 32 | P | audio |
| `src/audio/AudioDevice.cpp` | 75 | O | - |
| `src/audio/AudioDevice.h` | 38 | O | - |
| `src/audio/EchoEffect.cpp` | 209 | P | audio |
| `src/audio/EchoEffect.h` | 112 | P | audio |
| `src/audio/Mixer.cpp` | 316 | M | audio |
| `src/audio/Mixer.h` | 135 | M | audio |
| `src/audio/Music.cpp` | 767 | M | audio |
| `src/audio/Music.h` | 270 | M | audio |
| `src/audio/MusicDirector.cpp` | 129 | M | audio |
| `src/audio/MusicDirector.h` | 75 | M | audio |
| `src/audio/MusicMotif.c` | 153 | M | audio |
| `src/audio/MusicMotif.h` | 44 | M | audio |
| `src/audio/SoundBank.cpp` | 70 | F | audio |
| `src/audio/SoundBank.h` | 45 | F | audio |
| `src/audio/TextFields.cpp` | 101 | F | audio |
| `src/audio/TextFields.h` | 34 | F | audio |
| `src/audio/Wav.cpp` | 100 | F | audio |
| `src/audio/Wav.h` | 27 | F | audio |
| `src/audio/game/Ambience.cpp` | 390 | P | audio |
| `src/audio/game/Ambience.h` | 118 | P | audio |
| `src/audio/game/AudioTables.cpp` | 541 | P | audio |
| `src/audio/game/AudioTables.h` | 297 | P | audio |
| `src/audio/game/CarAudio.cpp` | 1127 | P | audio |
| `src/audio/game/CarAudio.h` | 426 | P | audio |
| `src/audio/game/Object3D.cpp` | 127 | P | audio |
| `src/audio/game/Object3D.h` | 122 | P | audio |
| `src/audio/game/PedAudio.cpp` | 298 | P | audio |
| `src/audio/game/PedAudio.h` | 98 | P | audio |
| `src/audio/game/SoundSlot.cpp` | 128 | P | audio |
| `src/audio/game/SoundSlot.h` | 70 | P | audio |
| `src/audio/game/Voices.cpp` | 523 | P | audio |
| `src/audio/game/Voices.h` | 166 | P | audio |
| `src/city/AiMap.cpp` | 179 | P | ai-ambient-city |
| `src/city/AiMap.h` | 96 | P | ai-ambient-city |
| `src/city/CityData.cpp` | 232 | F | ai-ambient-city |
| `src/city/CityData.h` | 72 | F | ai-ambient-city |
| `src/city/CityMesh.cpp` | 586 | P | ai-ambient-city |
| `src/city/CityMesh.h` | 70 | P | ai-ambient-city |
| `src/city/Environment.cpp` | 236 | F | ai-ambient-city |
| `src/city/Environment.h` | 101 | F | ai-ambient-city |
| `src/city/Inst.cpp` | 46 | F | ai-ambient-city |
| `src/city/Inst.h` | 29 | F | ai-ambient-city |
| `src/city/PathSet.cpp` | 45 | F | ai-ambient-city |
| `src/city/PathSet.h` | 36 | F | ai-ambient-city |
| `src/city/Psdl.cpp` | 320 | F | ai-ambient-city |
| `src/city/Psdl.h` | 178 | F | ai-ambient-city |
| `src/city/Pvs.cpp` | 84 | F | ai-ambient-city |
| `src/city/Pvs.h` | 35 | F | ai-ambient-city |
| `src/city/Race.cpp` | 337 | F | ai-ambient-city |
| `src/city/Race.h` | 143 | F | ai-ambient-city |
| `src/city/Reader.h` | 99 | F | ai-ambient-city |
| `src/city/RoomInfo.cpp` | 89 | P | ai-ambient-city |
| `src/city/RoomInfo.h` | 58 | P | ai-ambient-city |
| `src/city/RoomLocator.cpp` | 101 | P | ai-ambient-city |
| `src/city/RoomLocator.h` | 34 | P | ai-ambient-city |
| `src/city/SdlCollect.cpp` | 1141 | P | ai-ambient-city |
| `src/city/SdlCollect.h` | 127 | P | ai-ambient-city |
| `src/city/SdlDraw.cpp` | 1264 | P | rendering-fx |
| `src/city/SdlDraw.h` | 89 | P | rendering-fx |
| `src/core/File.cpp` | 184 | O | - |
| `src/core/File.h` | 108 | O | - |
| `src/core/Ini.cpp` | 189 | O | - |
| `src/core/Ini.h` | 57 | O | - |
| `src/core/Log.cpp` | 107 | O | - |
| `src/core/Log.h` | 55 | O | - |
| `src/core/Math.cpp` | 195 | M | phys-core |
| `src/core/Math.h` | 257 | M | phys-core |
| `src/core/Paths.cpp` | 92 | O | - |
| `src/core/Paths.h` | 24 | O | - |
| `src/core/StringUtil.cpp` | 135 | M | openmm2-only |
| `src/core/StringUtil.h` | 35 | M | openmm2-only |
| `src/data/CNumbers.h` | 128 | F | formats |
| `src/data/DatFile.cpp` | 275 | F | formats |
| `src/data/DatFile.h` | 63 | F | formats |
| `src/data/PeResources.cpp` | 200 | F | formats |
| `src/data/PeResources.h` | 30 | F | formats |
| `src/data/TextTables.cpp` | 117 | F | formats |
| `src/data/TextTables.h` | 54 | F | formats |
| `src/game/AiRenderer.cpp` | 185 | P | rendering-fx |
| `src/game/AiRenderer.h` | 65 | P | rendering-fx |
| `src/game/CamCar.cpp` | 169 | P | camera-props |
| `src/game/CamCar.h` | 149 | P | camera-props |
| `src/game/CamMath.cpp` | 185 | P | camera-props |
| `src/game/CamMath.h` | 77 | P | camera-props |
| `src/game/CamMirror.cpp` | 79 | P | camera-props |
| `src/game/CamMirror.h` | 86 | P | camera-props |
| `src/game/CamParams.cpp` | 151 | P | camera-props |
| `src/game/CamParams.h` | 111 | P | camera-props |
| `src/game/CamPlayer.cpp` | 316 | P | camera-props |
| `src/game/CamPlayer.h` | 144 | P | camera-props |
| `src/game/CamPov.cpp` | 41 | P | camera-props |
| `src/game/CamPov.h` | 45 | P | camera-props |
| `src/game/CamRace.cpp` | 72 | P | camera-props |
| `src/game/CamRace.h` | 71 | P | camera-props |
| `src/game/CamTrack.cpp` | 429 | P | camera-props |
| `src/game/CamTrack.h` | 78 | P | camera-props |
| `src/game/CamView.cpp` | 238 | P | camera-props |
| `src/game/CamView.h` | 97 | P | camera-props |
| `src/game/Camera.cpp` | 52 | P | camera-props |
| `src/game/Camera.h` | 39 | P | camera-props |
| `src/game/Catalog.cpp` | 117 | P | camera-props |
| `src/game/Catalog.h` | 70 | P | camera-props |
| `src/game/CityLevel.cpp` | 449 | P | rendering-fx |
| `src/game/CityLevel.h` | 105 | P | rendering-fx |
| `src/game/CityRenderer.cpp` | 391 | P | rendering-fx |
| `src/game/CityRenderer.h` | 137 | P | rendering-fx |
| `src/game/MeshDraw.cpp` | 88 | P | rendering-fx |
| `src/game/MeshDraw.h` | 53 | P | rendering-fx |
| `src/game/ModelLibrary.cpp` | 131 | P | rendering-fx |
| `src/game/ModelLibrary.h` | 68 | P | rendering-fx |
| `src/game/PlayerVehicle.cpp` | 200 | P | vehicle |
| `src/game/PlayerVehicle.h` | 57 | P | vehicle |
| `src/game/Profile.cpp` | 527 | P | camera-props |
| `src/game/Profile.h` | 212 | P | camera-props |
| `src/game/RaceConfig.h` | 80 | P | session |
| `src/game/Strings.cpp` | 38 | P | session |
| `src/game/Strings.h` | 43 | P | session |
| `src/game/TexelDamage.cpp` | 168 | P | rendering-fx |
| `src/game/TexelDamage.h` | 65 | P | rendering-fx |
| `src/game/TextureLibrary.cpp` | 210 | P | rendering-fx |
| `src/game/TextureLibrary.h` | 81 | P | rendering-fx |
| `src/game/TrafficBodies.cpp` | 645 | P | ai-vehicles |
| `src/game/TrafficBodies.h` | 128 | P | ai-vehicles |
| `src/game/VehicleRenderer.cpp` | 404 | P | rendering-fx |
| `src/game/VehicleRenderer.h` | 134 | P | rendering-fx |
| `src/game/bangers/BangerData.cpp` | 100 | P | camera-props |
| `src/game/bangers/BangerData.h` | 83 | P | camera-props |
| `src/game/bangers/BangerSet.cpp` | 1047 | P | camera-props |
| `src/game/bangers/BangerSet.h` | 181 | P | camera-props |
| `src/game/bangers/PropPlacement.cpp` | 327 | P | camera-props |
| `src/game/bangers/PropPlacement.h` | 84 | P | camera-props |
| `src/game/bangers/RoadDecals.cpp` | 76 | P | camera-props |
| `src/game/bangers/RoadDecals.h` | 38 | P | camera-props |
| `src/game/fx/BirthRule.cpp` | 62 | P | rendering-fx |
| `src/game/fx/BirthRule.h` | 68 | P | rendering-fx |
| `src/game/fx/EffectLibrary.cpp` | 60 | P | rendering-fx |
| `src/game/fx/EffectLibrary.h` | 54 | P | rendering-fx |
| `src/game/fx/LensFlares.cpp` | 159 | P | rendering-fx |
| `src/game/fx/LensFlares.h` | 61 | P | rendering-fx |
| `src/game/fx/LineSparks.cpp` | 143 | P | rendering-fx |
| `src/game/fx/LineSparks.h` | 63 | P | rendering-fx |
| `src/game/fx/ParticleRenderer.cpp` | 126 | P | rendering-fx |
| `src/game/fx/ParticleRenderer.h` | 55 | P | rendering-fx |
| `src/game/fx/Particles.cpp` | 167 | P | rendering-fx |
| `src/game/fx/Particles.h` | 113 | P | rendering-fx |
| `src/game/fx/Random.h` | 35 | P | rendering-fx |
| `src/game/fx/Shards.cpp` | 105 | P | rendering-fx |
| `src/game/fx/Shards.h` | 52 | P | rendering-fx |
| `src/game/fx/SkidMarks.cpp` | 140 | P | rendering-fx |
| `src/game/fx/SkidMarks.h` | 85 | P | rendering-fx |
| `src/game/fx/VehicleEffects.cpp` | 215 | P | rendering-fx |
| `src/game/fx/VehicleEffects.h` | 102 | P | rendering-fx |
| `src/game/fx/Weather.cpp` | 37 | P | rendering-fx |
| `src/game/fx/Weather.h` | 44 | P | rendering-fx |
| `src/game/net/NetGame.cpp` | 659 | M | session |
| `src/game/net/NetGame.h` | 229 | M | session |
| `src/game/session/CopsAndRobbers.cpp` | 252 | P | session |
| `src/game/session/CopsAndRobbers.h` | 130 | P | session |
| `src/game/session/Gate.cpp` | 92 | P | session |
| `src/game/session/Gate.h` | 44 | P | session |
| `src/game/session/Hud.cpp` | 916 | P | session |
| `src/game/session/Hud.h` | 244 | P | session |
| `src/game/session/RaceSetup.cpp` | 296 | P | session |
| `src/game/session/RaceSetup.h` | 105 | P | session |
| `src/game/session/Session.cpp` | 1321 | P | session |
| `src/game/session/Session.h` | 269 | P | session |
| `src/game/session/Types.h` | 115 | P | session |
| `src/game/world/Gizmos.cpp` | 885 | P | mm2-world-objects |
| `src/game/world/Gizmos.h` | 289 | P | mm2-world-objects |
| `src/game/world/PathSpline.cpp` | 146 | P | mm2-world-objects |
| `src/game/world/PathSpline.h` | 72 | P | mm2-world-objects |
| `src/net/BitStream.cpp` | 243 | O | - |
| `src/net/BitStream.h` | 200 | O | - |
| `src/net/ClockSync.cpp` | 21 | O | - |
| `src/net/ClockSync.h` | 46 | O | - |
| `src/net/Discovery.cpp` | 400 | O | - |
| `src/net/Discovery.h` | 159 | O | - |
| `src/net/NatPmp.cpp` | 171 | O | - |
| `src/net/NatPmp.h` | 84 | O | - |
| `src/net/NatPmpBackend.cpp` | 279 | O | - |
| `src/net/Net.cpp` | 113 | O | - |
| `src/net/Net.h` | 62 | O | - |
| `src/net/PortMapper.cpp` | 324 | O | - |
| `src/net/PortMapper.h` | 164 | O | - |
| `src/net/Protocol.cpp` | 31 | O | - |
| `src/net/Protocol.h` | 587 | O | - |
| `src/net/Session.cpp` | 942 | O | - |
| `src/net/Session.h` | 253 | O | - |
| `src/net/Sha256.cpp` | 98 | O | - |
| `src/net/Sha256.h` | 37 | O | - |
| `src/net/Snapshot.cpp` | 94 | O | - |
| `src/net/Snapshot.h` | 106 | O | - |
| `src/net/Transport.cpp` | 270 | O | - |
| `src/net/Transport.h` | 109 | O | - |
| `src/net/UpnpBackend.cpp` | 190 | O | - |
| `src/phys/AgeMath.cpp` | 241 | P | phys-core |
| `src/phys/AgeMath.h` | 77 | P | phys-core |
| `src/phys/Bound.cpp` | 879 | P | phys-bounds |
| `src/phys/Bound.h` | 413 | P | phys-bounds |
| `src/phys/BoundBox.cpp` | 1622 | P | phys-bounds |
| `src/phys/BoundCollision.cpp` | 93 | P | phys-bounds |
| `src/phys/BoundHotdog.cpp` | 834 | P | phys-bounds |
| `src/phys/BoundPolygonal.cpp` | 1523 | P | phys-bounds |
| `src/phys/BoundSphere.cpp` | 130 | P | phys-bounds |
| `src/phys/BoundTerrain.cpp` | 1121 | P | phys-bounds |
| `src/phys/Collider.cpp` | 118 | P | phys-core |
| `src/phys/Collider.h` | 87 | P | phys-core |
| `src/phys/Collision.cpp` | 187 | P | phys-core |
| `src/phys/Collision.h` | 147 | P | phys-core |
| `src/phys/Constants.h` | 46 | P | phys-core |
| `src/phys/Geometry.cpp` | 872 | P | phys-core |
| `src/phys/Geometry.h` | 103 | P | phys-core |
| `src/phys/Impact.cpp` | 490 | P | phys-core |
| `src/phys/Impact.h` | 104 | P | phys-core |
| `src/phys/InertialCS.cpp` | 481 | P | phys-core |
| `src/phys/InertialCS.h` | 155 | P | phys-core |
| `src/phys/Joint.cpp` | 81 | P | phys-core |
| `src/phys/Joint.h` | 67 | P | phys-core |
| `src/phys/Level.h` | 134 | P | phys-core |
| `src/phys/Material.cpp` | 157 | P | phys-core |
| `src/phys/Material.h` | 66 | P | phys-core |
| `src/phys/PolygonSoup.cpp` | 230 | P | phys-core |
| `src/phys/PolygonSoup.h` | 109 | P | phys-core |
| `src/phys/Sleep.cpp` | 128 | P | phys-core |
| `src/phys/Sleep.h` | 48 | P | phys-core |
| `src/phys/TrailerJoint.cpp` | 675 | P | phys-core |
| `src/phys/TrailerJoint.h` | 139 | P | phys-core |
| `src/phys/World.cpp` | 498 | P | phys-core |
| `src/phys/World.h` | 232 | P | phys-core |
| `src/phys/vehicle/Aero.cpp` | 57 | P | vehicle |
| `src/phys/vehicle/Aero.h` | 22 | P | vehicle |
| `src/phys/vehicle/CarSim.cpp` | 487 | P | vehicle |
| `src/phys/vehicle/CarSim.h` | 232 | P | vehicle |
| `src/phys/vehicle/Controls.cpp` | 31 | P | vehicle |
| `src/phys/vehicle/Controls.h` | 32 | P | vehicle |
| `src/phys/vehicle/Drivetrain.cpp` | 218 | P | vehicle |
| `src/phys/vehicle/Drivetrain.h` | 71 | P | vehicle |
| `src/phys/vehicle/Engine.cpp` | 133 | P | vehicle |
| `src/phys/vehicle/Engine.h` | 66 | P | vehicle |
| `src/phys/vehicle/Gyro.cpp` | 42 | P | vehicle |
| `src/phys/vehicle/Gyro.h` | 34 | P | vehicle |
| `src/phys/vehicle/Splash.cpp` | 68 | P | vehicle |
| `src/phys/vehicle/Splash.h` | 39 | P | vehicle |
| `src/phys/vehicle/Stuck.cpp` | 107 | P | vehicle |
| `src/phys/vehicle/Stuck.h` | 49 | P | vehicle |
| `src/phys/vehicle/Trailer.cpp` | 238 | P | vehicle |
| `src/phys/vehicle/Trailer.h` | 116 | P | vehicle |
| `src/phys/vehicle/Transmission.cpp` | 154 | P | vehicle |
| `src/phys/vehicle/Transmission.h` | 66 | P | vehicle |
| `src/phys/vehicle/TuneParams.cpp` | 209 | P | vehicle |
| `src/phys/vehicle/TuneParams.h` | 188 | P | vehicle |
| `src/phys/vehicle/VehicleBody.h` | 37 | P | vehicle |
| `src/phys/vehicle/VehicleGeometry.cpp` | 28 | P | vehicle |
| `src/phys/vehicle/VehicleGeometry.h` | 56 | P | vehicle |
| `src/phys/vehicle/Wheel.cpp` | 657 | P | vehicle |
| `src/phys/vehicle/Wheel.h` | 176 | P | vehicle |
| `src/platform/Clock.cpp` | 41 | M | openmm2-only |
| `src/platform/Clock.h` | 36 | M | openmm2-only |
| `src/platform/Dialogs.cpp` | 137 | O | - |
| `src/platform/Dialogs.h` | 42 | O | - |
| `src/platform/ImGuiPlatform.cpp` | 27 | O | - |
| `src/platform/ImGuiPlatform.h` | 26 | O | - |
| `src/platform/Input.cpp` | 230 | M | openmm2-only |
| `src/platform/Input.h` | 131 | M | openmm2-only |
| `src/platform/Platform.cpp` | 100 | O | - |
| `src/platform/Platform.h` | 54 | O | - |
| `src/platform/Window.cpp` | 257 | O | - |
| `src/platform/Window.h` | 104 | O | - |
| `src/render/Device.h` | 127 | O | - |
| `src/render/DisplaySettings.cpp` | 135 | O | - |
| `src/render/DisplaySettings.h` | 69 | O | - |
| `src/render/GpuConstants.h` | 85 | O | - |
| `src/render/HandleTable.h` | 58 | O | - |
| `src/render/ImGuiRenderer.cpp` | 165 | O | - |
| `src/render/ImGuiRenderer.h` | 31 | O | - |
| `src/render/ImageUtil.cpp` | 94 | M | openmm2-only |
| `src/render/ImageUtil.h` | 29 | M | openmm2-only |
| `src/render/Overlay2D.cpp` | 101 | O | - |
| `src/render/Overlay2D.h` | 57 | O | - |
| `src/render/Projection.cpp` | 75 | M | openmm2-only |
| `src/render/Projection.h` | 50 | M | openmm2-only |
| `src/render/Renderer.cpp` | 116 | O | - |
| `src/render/Renderer.h` | 40 | O | - |
| `src/render/ShaderBlobs.h` | 29 | O | - |
| `src/render/Types.cpp` | 25 | M | rendering-fx |
| `src/render/Types.h` | 273 | M | rendering-fx |
| `src/render/opengl/GlApi.cpp` | 28 | O | - |
| `src/render/opengl/GlApi.h` | 119 | O | - |
| `src/render/opengl/GlDevice.cpp` | 972 | O | - |
| `src/render/shaders/composite.frag` | 9 | M | rendering-fx |
| `src/render/shaders/composite.vert` | 13 | M | rendering-fx |
| `src/render/shaders/mesh.frag` | 57 | M | rendering-fx |
| `src/render/shaders/mesh.vert` | 77 | M | rendering-fx |
| `src/render/shaders/overlay.frag` | 25 | M | rendering-fx |
| `src/render/shaders/overlay.vert` | 26 | M | rendering-fx |
| `src/render/shaders/prelude_gl.glsl` | 12 | M | rendering-fx |
| `src/render/shaders/prelude_vk.glsl` | 12 | M | rendering-fx |
| `src/render/vulkan/VmaImpl.cpp` | 8 | O | - |
| `src/render/vulkan/VulkanDevice.cpp` | 2279 | O | - |
| `src/ui/Font.cpp` | 331 | M | frontend-ui |
| `src/ui/Font.h` | 129 | M | frontend-ui |
| `src/ui/MenuLayout.cpp` | 108 | M | frontend-ui |
| `src/ui/MenuLayout.h` | 61 | M | frontend-ui |
| `src/ui/Text.cpp` | 103 | M | frontend-ui |
| `src/ui/Text.h` | 52 | M | frontend-ui |
| `src/ui/TextureCache.cpp` | 56 | M | frontend-ui |
| `src/ui/TextureCache.h` | 64 | M | frontend-ui |
| `src/ui/Widgets.cpp` | 920 | M | frontend-ui |
| `src/ui/Widgets.h` | 429 | M | frontend-ui |
| `src/vfs/DaveArchive.cpp` | 163 | M | formats |
| `src/vfs/DaveArchive.h` | 63 | M | formats |
| `src/vfs/DirectoryFs.cpp` | 63 | O | - |
| `src/vfs/DirectoryFs.h` | 37 | O | - |
| `src/vfs/FileSystem.h` | 35 | O | - |
| `src/vfs/GameSource.cpp` | 283 | M | openmm2-only |
| `src/vfs/GameSource.h` | 66 | M | openmm2-only |
| `src/vfs/IsoImage.cpp` | 290 | O | - |
| `src/vfs/IsoImage.h` | 56 | O | - |
| `src/vfs/Vfs.cpp` | 62 | M | formats |
| `src/vfs/Vfs.h` | 43 | M | formats |
| `src/video/Avi.cpp` | 178 | O | - |
| `src/video/Avi.h` | 59 | O | - |
| `src/video/Indeo5.cpp` | 1593 | O | - |
| `src/video/Indeo5.h` | 52 | O | - |
| `src/video/IndeoTables.inc` | 548 | O | - |
| `src/video/Movie.cpp` | 70 | O | - |
| `src/video/Movie.h` | 44 | O | - |
| `tools/introplay/main.cpp` | 269 | O | - |
| `tools/mm2tool/Command.h` | 24 | O | - |
| `tools/mm2tool/Common.cpp` | 96 | O | - |
| `tools/mm2tool/Common.h` | 24 | O | - |
| `tools/mm2tool/cmd_ai.cpp` | 445 | O | - |
| `tools/mm2tool/cmd_asset.cpp` | 491 | O | - |
| `tools/mm2tool/cmd_audio.cpp` | 48 | O | - |
| `tools/mm2tool/cmd_city.cpp` | 408 | O | - |
| `tools/mm2tool/cmd_data.cpp` | 88 | O | - |
| `tools/mm2tool/cmd_fx.cpp` | 239 | O | - |
| `tools/mm2tool/cmd_gameaudio.cpp` | 169 | O | - |
| `tools/mm2tool/cmd_music.cpp` | 307 | O | - |
| `tools/mm2tool/cmd_ped.cpp` | 386 | O | - |
| `tools/mm2tool/cmd_phys.cpp` | 441 | O | - |
| `tools/mm2tool/cmd_vfs.cpp` | 98 | O | - |
| `tools/mm2tool/main.cpp` | 51 | O | - |
| `tools/netprobe/main.cpp` | 304 | O | - |
| `tools/rendertest/main.cpp` | 530 | O | - |
