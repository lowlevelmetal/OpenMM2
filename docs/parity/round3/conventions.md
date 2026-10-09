# Round 3: conventions

Audited against MM2Recomp (midtown2.exe build 3393) on 2026-10-08.

Summary: 104 values; verified 83, fixed 8, deviation 6, inferred 3,
open 0, openmm2 4.

The question for this theme: does every value that crosses from MM2's
data files or code into OpenMM2 keep its meaning at each step: units,
signs, axes, angle conventions (degrees or radians, which way is zero,
which way is positive), speeds (mph or m/s), times (seconds, frames or
milliseconds), colours (byte order, 0..1 or 0..255), densities (fractions
or counts) and the defaults MM2 uses when a value is missing? Each value
was followed from MM2's reader (the FileIO or Load function, decompiled
and, where the decompile loses values, read in the asm) to every place MM2
uses it, and compared with OpenMM2's parser and every OpenMM2 use site,
looking at the seams: where OpenMM2 converts a value, hands it to another
module, keeps it in its own structure or uses it in its own glue. The
earlier records (`docs/parity/*.md` and `docs/parity/mm2/*.md`) compared
most formulas function by function; this record does not repeat them.

Conventions used below: a heading h "faces (sin h, 0, -cos h)" means the
object's forward axis (its -Z, -m2) points that way; `Mat34::rotationY(a)`
is MM2's `Matrix34::MakeRotateY`, rows (cos a, 0, -sin a), (0, 1, 0),
(sin a, 0, cos a), so it faces (-sin a, 0, -cos a).

Screenshots checked (kept under `local/shots/`, not in git): the San
Francisco shop awning on `nw_b4awn_ff01_8_f` before (a flat strip edge-on)
and after (a sloped awning over the shop window) the xref fix, and a
scaled London petrol station before and after the normals fix (7% brighter
walls, nothing else on screen changed).

## Race files and spawns

| Value | MM2: reader and meaning | OpenMM2 | Verdict |
| --- | --- | --- | --- |
| Waypoint columns | `mmPositions::Load`: x, y, z, heading by atof, radius by atoi (0 means 15), column 6 the hit flag | `city::parseWaypoints`, `buildCheckpoints` | verified |
| Waypoint heading | degrees; the users turn by h x -0.017453292, so h faces (sin h, 0, -cos h): 0 is -Z, 90 is +X | `headingDirection`, `Gate::calculateGatePoints` (gate across the heading) | verified |
| Zero heading | `mmWaypoints::LoadCSV` / `ReInit`: a waypoint after the first with heading exactly 0 takes atan2(x - x', z - z') x -57.295776 towards the next one | `headingTowards` | verified |
| Player start angle | the modes' InitGameObjects: `GetStartAngle` x -0.017453292 into vehCarSim +0x250 | `startPlace`, `spawnAt` | verified |
| vehCarSim +0x250 | `vehCarSim::Reset`: identity, the reset position, `Matrix34::Rotate` about +Y (MakeRotateY) by the angle | `CarSim::reset`, `age::makeRotate` | verified |
| Reset angle carried as a matrix | MM2 stores the angle | OpenMM2's spawns are `Mat34`s and `phys::resetRotationOf` takes atan2(m2.x, m2.z) back: the same turn, wrapped into (-pi, pi], within float rounding | openmm2 |
| `.opp` grid row | `aiRouteRacer::Init`: the first row's fourth column (headed `brake`) x 0.017444445 into +0x250, not negated (90 faces -X) | `loadRaceSetup` (`op.spawn`) | verified |
| `.opp` other rows | only x, y, z read; the middle rows become the first intersection of their room, the last the destination | `Opponent::routeFromPath` | verified |
| `[Police]` heading | `aiRaceData::aiRaceData`: heading x -0.017444445 (MM2's own degree factor); `aiPoliceOfficer::Reset` copies it to +0x250 | was -heading x pi / 180 | fixed |
| `[Police]` other columns | `%d %d %f %f`: a mode never read, the behaviour bits (default 15), the chance to pursue an opponent (default 0.5) and the range for it (default 50 m) | `PoliceSettings::fromData` | verified |
| `[Opponent]` numbers | `%f %d %f %f %d %d %d %d %d %f` with defaults 1, 0, 50, 0.7, 1, 1, 1, 1, 0, 1 | `OpponentSettings::fromData` | verified |
| `[CopChaseDistance]` | metres, default 250 | `PoliceSettings::chaseDistance` | verified |
| Race table `Cops` | a count (0 to 8) that `RaceMenuBase::SetStateRace` keeps in the cop density as it is; `aiMap::Init` clamps it when placing posts; `RegisterFinish` compares the setting with the count | was clamped to 0..1 when applied | fixed |
| Race table `Ambient`, `Peds` | floats used as the traffic and pedestrian densities, not clamped | `applyRaceTableDefaults` clamps to 0..1 (retail values are 0 to 0.9) | deviation |
| Race table `TimeofDay`, `Weather` | 0..3, the lighting table index time x 4 + weather | clamped to 0..3 (retail values are in range) | deviation |
| Race table `TimeLimit`, `NumLaps` | seconds (mmPlayer +0xd2c counts down); laps | `RaceSetup::timeLimit`, `laps` | verified |
| Crash course columns | `mmSingleStunt::LoadEventFile`: `cornerspeed` in mph against vehCarSim +0x24c (below 1 means 50), `chkflags`, `numopp`, `TimeLimit` in seconds, `AmbDensity` | `loadRaceSetup` (`LessonEvent`) | verified |
| Multiplayer grid | `mmGameMulti::StartXYZ`: the slot's offset (two tables, long vehicles above radius 6 or towing) turned about +Y by the start angle and added to the start, then `FindGroundPos` | `multiplayerGridOffset`, RaceScreen | verified |
| Cruise start angle | `mmGame::RespawnXYZ`: 0 (facing -Z), 2 m above the intersection | `randomIntersectionStart` (the pick is the random-streams theme's) | verified |
| Water respawn | `mmSingleCircuit` / `mmGameMulti::HitWaterHandler`: the last cleared waypoint, heading x -0.017453292 | `Session::respawnAtLastCheckpoint` | verified |
| Racers' finish line | `aiMap::SetWaypoints`: the point and (0, 0, 1) turned by `Vector3::RotateY`(heading x -0.017453292) | `Opponent::setFinishLine` | verified |
| Multiplayer finish camera | `mmGameMulti::SetFinishCam`: azimuth (heading + 180) x -0.017453292 | `startFinishCamera` | verified |
| Cops and Robbers places | `mmMultiCR::LoadCSV` of `multicopwaypoints.csv`: positions only | `loadCrLocations` | verified |
| Speed for display and rules | vehCarSim +0x24c = speed (m/s) x MetricFactor 2.2360249, the only use of that constant | `CarSim::speedMph` | verified |
| km/h speedometer | MM2 has none | `HudOptions::metric`, off by default | deviation |

## City files

| Value | MM2: reader and meaning | OpenMM2 | Verdict |
| --- | --- | --- | --- |
| `.inst` compact form | `lvlFixedRotY::GetMatrix`: m0 = (a, 0, b), m1 = Y, m2 = (-b, 0, a) | `city::parseInstances` | verified |
| `.inst` full form and flags | four rows; low byte the variant, 0x100 terrain-local, 0x200 banger, 0x2000 multi-room; a banger record keeps its full matrix unless compact | `Inst.cpp`, `CityLevel`, `placeCityProps` | verified |
| `.inst` scale | kept in the matrix, drawn through Direct3D (see the normals row under Renderer) | kept | verified |
| PKG xref bangers | `lvlLevel::LoadInstances`: xref x record, then `RequestBanger(name, 0)`, a dgUnhitYBangerInstance keeping only the turn about Y (the CG offset turned by the whole matrix) | was the full matrix: 68 of 177 retail xrefs are exported Z up and were tipped 90 degrees (SF's shop and Chinatown awnings, Harrods' awning, the Golden Gate, Bay Bridge and tower lights) | fixed |
| Path set type 1 (pairs) | `dgPath::Enumerate`: m0 from point i to i + 1 with y zeroed, m2 = m0 x Y, m1 = m2 x m0, Y-only bangers | `placePathSet` (sets m1 to Y exactly where MM2's cross product can be 1 ulp off) | verified |
| Path set type 2 (strip) | m0 normalised in 3D, m2 and m1 by cross products, not normalised; step L / floor(L / spacing); full matrix | `placePathSet` | verified |
| Path spacing | byte x 0.25 m, 0 means 5 m | `PathSet.cpp` | verified |
| Parked cars | the path matrix times MakeRotateY(pi / 2), position kept | `Gizmos.cpp` | verified |
| Bridge, ferry, train, sailboat frames | bridge Z from the other leaf, X = Y x Z; the movers' spline tangent into m2, then `Matrix34::Normalize` | `Gizmos.cpp` | verified |
| `.bai` section axes | xAxis points left, zAxis back (a left-handed set) | turned into right-handed frames at every use (`Traffic::solvePose`, `World.cpp` light poles) | verified |
| `[Speed Limit]` | m/s (15), freeways + 12.5 | `RoadNetwork::build` | verified |
| `.ltNN` lights | headings and pitches in radians; `SetLightDirection` = (cos h cos p, sin p, sin h cos p), the way the light travels; colours 0..1; `Ambient` an ARGB integer | `city::loadLighting`, `CityRenderer` `lightDirection`, `unpackRgb` | verified |
| `_fog.csv` | R, G, B by atoi (0..255), start and end as shorts; start = min(far - 30, start), end = min(far, end); row = time x 4 + weather | `makeEnvironment` | verified |
| `.sky` | `lvlSky::AutoInit`: model, then height offset a and camera-height scale b (`DrawHat`: camera y x b + a), turn rate c in rad/s (`Update`) | `CityRenderer::drawSky`, `update` | verified |
| `.water` | the level, then room ids flagged water of death | `parseWater`, `RoomInfo` | verified |
| `.lmap` | `cityLevel::Load` takes it only when its count equals lvlSDL's room count (read by `LoadBinary` just before, dummy room 0 included): London has 1341 for 1342, SF 1125 for 1172, so every room is white | every room white | verified |
| `.ext`, `.reset` | no reader in midtown2.exe | parsed, never used | openmm2 |
| PSDL street colours and UVs | room colour, half of it, the wall light table entry (earlier records); UVs sampled with D3D's v = 0 at the first stored row | `CityRenderer`, `TextureLibrary` (see Renderer) | verified |

## Tune files

| Value | MM2: reader and meaning | OpenMM2 | Verdict |
| --- | --- | --- | --- |
| `vehCarSim` and its parts | each class's FileIO and constructor: Mass 2000, InertiaBox (2, 1, 3) full extents, SteeringLimit 0.39 rad, OptimumSlip 0.14, gear speeds in mph turned into ratios with 1609.344 / 60, RPM x 0.10471976 | `phys::loadCarSimParams` | verified |
| `vehGyro`, `vehStuck` | floats; stuck defaults 1.57, 0.39, 0.1, 0.3, 1.25, 1.75 | `loadGyroParams`, `loadStuckParams` | verified |
| `vehCarDamage` | thresholds; SmokeOffset in the body (CG) frame; its rule fields feed one engine-smoke rule shared by every car | `loadCarDamageParams`, `VehicleEffects` | verified |
| `vehTrailer`, `dgTrailerJoint` | trailer mass 3000, box (3, 4, 9); joint defaults from `dgTrailerJoint::Init` | `loadTrailerParams`, `loadTrailerJointParams` | verified |
| `aiVehicleData` | Size a full box, CG the bound offset; the constructor leaves fields unset | `loadVehicleData`; a missing CG (three retail files) is 0 | inferred |
| `.info` | `mmVehInfo::Load` in field order; flag 0x40 right-hand drive moves the small map | `parseVehicleInfo`, `Hud` | verified |
| Camera files | CameraFOV vertical degrees; Pitch radians; AfterLoad's near planes; the track camera's speeds in m/s | `CamParams` | verified |
| CameraFar | one global every file overwrites, then replaced by the far-clip option at race start | each camera keeps its own, the renderer uses the option | verified |
| `.mmmirror` | Position (the frame's m3), Size (screen fractions), Fov vertical degrees, Aspect, NearClip, FarClip; defaults (0, 1.4, -1), (0.3, 0.16), 10, 2, 1.2, 100 | `MirrorParams` | verified |
| `tune/<car>.asnode` | `mmPlayer::Init` names the player node after the car and loads it; `mmPlayer::FileIO` reads the speed bases, the keyboard and game pad rates and the mouse, joystick and wheel curves that `mmPlayer::Update` blends by speed | was never loaded: every car used the constructor's values (vpbug: SpeedBaseHi 44.6 not 100, keyboard DeltaOut 2.573 -> 0.8 not 3.5 -> 2.5) | fixed |
| `.asbirthrule` | named fields, defaults Gravity -9.8 and 1s; Color stored 0xAABBGGRR and turned into 0xAARRGGBB vertex colours | `BirthRule.cpp`, `Particles.cpp` | verified |
| `dgBangerData` fields | read by position in the retail order; NumGlows 1 when absent; Size (0.2, 0.5, 0.2), Mass 50, CollisionType 0x10 | `parseBangerData` (by name; every retail file has the expected order) | verified |
| `dgBangerData` birth rule | `asBirthRule::Load` skips the block's name and reads by position | only a block named `BirthRule` was read: `sp_tree1_s_break06` (`asBirthRule {`) lost its leaves | fixed |
| `dgBangerData` mass guard | none | a mass of 0 or less becomes 1 (no retail file) | deviation |
| `.mmhudmap` | Size and Pos as screen fractions (minus 10 px), ZoomIn; "Approach Rate" and "Ocean Color" contain spaces and are never read | `Hud` | verified |
| `_dash.asnode` | needles turned by -angle about Z, RotMin / RotMax in radians; speed against 160 mph, RPM against 8000 with an 800 floor | `loadDashParams`, `drawDash` | verified |
| `tune/widget.csv` | `WArray::Read`: X and W over the screen width, Y and H over its height (the menus run at 640x480); a table value replaces a nonzero code value | `MenuLayout` | verified |
| `widget.csv` quirks | MM2 keeps the code's X when all four values are within 0.0021 of it, and tests the code value after adding the dialog origin | not reproduced: at most about 1.3 px, and no page passes a zero coordinate inside an offset dialog | inferred |
| `tune/<tex>.movie` | `rate` in frames per second (default 30) | `TextureLibrary` | verified |

## Audio

| Value | MM2: reader and meaning | OpenMM2 | Verdict |
| --- | --- | --- | --- |
| Sound volume | `audSound::SetVolume`: (v - 1) x 10000 hundredths of a dB, after the class master and a 0..1 clamp | `ageVolumeToGain`, `Mixer`, `SoundSlot` | verified |
| Pan sign | `audSound::SetPan`: pan x 10000; positive attenuates the left channel | `agePanToMixer`, `Mixer` | verified |
| Pan source | `Aud3DObject::CalcSinglePlayerPan`: listener-space x / pseudo distance x 0.2; the listener is the camera's matrix | `Audio3D::pan` with `m_camera.transform` | verified |
| Pitch | the wave's own rate x ratio, clamped to 100..100000 Hz | `Mixer`, `SoundSlot::clampPitch` | verified |
| Master sliders | log10(200 s) / log10(200), 0 kept at 0 | `ageMasterVolume` | verified |
| Music and ambience volume | `DMusicWaveBuffer::SetVolume`: clamp, the log mapping, then (x - 1) x 10000 | `Mixer` | verified |
| Drop-off distances | squared min and max; attenuation 1 - p used as a volume | `Audio3D` | verified |
| Speeds into audio | m/s (vehCarSim +0x248, aiVehicleSpline's speed) against the tables' bands; RPM from vehCarSim +0x2c4 | RaceScreen | verified |
| Mixer's own 3D model | none | no game sound passes an emitter | openmm2 |
| Balance | the Windows mixer (outside midtown2.exe) | a linear far-channel cut | inferred |

## HUD, frontend and options

| Value | MM2: reader and meaning | OpenMM2 | Verdict |
| --- | --- | --- | --- |
| HUD map camera | `mmHudMap::Cull`: RotX(-90 degrees) then the heading; screen up is the car's forward when rotating, -Z otherwise; a proper rotation | `mapCamera` | verified |
| HUD map viewport | screen fractions from the top left, 60 degrees vertical, near 10, far 1600, aspect 1.25 (2.5 split) | `Hud` | verified |
| HUD map arrows | tip (0, 0, -1), base (±0.7, 0, 1); colours 0xAARRGGBB; icons upright, 15 m up, scaled | `Hud`, `argbColor`, `mapIconMatrix` | verified |
| Map on non-4:3 screens | fixed 4:3 | widened | deviation |
| Steering sign | left -1, right +1 from input to `vehWheel::SetInputs` (angle -steer x SteeringLimit), the dashboard wheel (-steer x WheelFact about Z) and the mouse arrow | `GameInput`, `Wheel`, `Hud` | verified |
| Clock and lap times | seconds, + 0.005, MM:SS:HH | `clockText`; network times in ms both ways | verified |
| Traffic and pedestrian density | floats 0..1 on 11-step sliders; cruise 0.25 / 0.5 / 1 | `RaceConfig`, `PagesRace` | verified |
| Slider stepping | `UISlider::Action` steps mmSlider's own value, which `SetValue` clamped when `UISlider::Update` took the variable | stepped from the unclamped variable (a count of 3 went to 1, not 1 - step) | fixed |
| Time of day and weather order | morning, noon, evening, night; clear, cloudy, fog (headlights on), rain | `TimeOfDay`, `Weather` | verified |
| Far clip, light quality | 100..1000 m into `gfxViewport::Perspective`; quality 0..3 | `PagesOptions`, RaceScreen | verified |
| Multiplayer densities | the pedestrian density sent as a byte / 255 | OpenMM2's own protocol sends percent | deviation |

## Renderer

| Value | MM2: meaning | OpenMM2 | Verdict |
| --- | --- | --- | --- |
| Camera FOV | vertical degrees (`gfxViewport::Perspective`: y scale 1 / tan(fov / 2), x scale y / aspect) | carried as the 4:3 horizontal FOV and widened (Hor+) | verified at 4:3; the widening is the any-resolution extra (deviation, as recorded in camera-props) |
| Other projections | wide view 70 degrees, dashboard near 0.01, mirror's own Fov / Aspect / clips, HUD map 60 degrees, showroom 0.6 rad turned into degrees | `CamPlayer`, RaceScreen, `Hud`, `Showroom` | verified |
| Depth and handedness | D3D 0..1 depth, a left-handed projection with a z-flipped view | right-handed -Z projection with zero-to-one depth | verified |
| Mirrored frames | `mmMirror::Init` negates m0 and `Cull` swaps the cull mode; no retail `.inst` matrix has a negative determinant | `RearViewMirror`, `setFrontFaceFlipped` | verified |
| Texture rows | v = 0 is the first stored row: `.tex` bottom first, TGA, BMP and JPEG top first | `TextureLibrary::readImage` flips TGA, BMP and JPEG | verified |
| Environment map | camera-space normal back to world space, u = 0.5 + 0.5 x, v = 0.5 - 0.5 y | `mesh.vert` | verified |
| Colour byte order | D3D ARGB; PKG vertex colours red and blue swapped on load | `argbToRgba` at every upload | verified |
| Fog | linear vertex fog on view depth, colour / 255 | `mesh.vert` | verified |
| Per-light ambient and specular | zeroed; never enabled | not modelled | verified |
| Normals of scaled instances | Direct3D 7 fixed function, NORMALIZENORMALS never set: the inverse transpose of the world matrix, not renormalised (Direct3D's rule) | was the forward matrix then normalised: the scaled buildings and props (about 1,500 in London, 2,400 in SF) were lit at unit strength | fixed (the Direct3D rule is inferred) |

## Angles, frames and times derived in code

| Value | MM2: meaning | OpenMM2 | Verdict |
| --- | --- | --- | --- |
| Drawn wheels | the player's from the simulation's wheel matrices; traffic tyres about X by -(distance driven) | `PlayerVehicle`, `AiRenderer` | verified |
| Remote cars' wheels | none (DirectPlay sent no wheel state) | steered -0.5 x steering, spun from the speed | openmm2 |
| Pedestrian facing | `aiPedestrian::Update`: m0 = (-cos h, 0, sin h), m2 = (-sin h, 0, -cos h) | `Pedestrians::frameOf` | verified |
| Traffic heading | `aiGoalAvoidPlayer::Reset`: atan2(m2.x, m2.z) | `Traffic.cpp` | verified |
| Traffic light poles | X towards the light axis, Z = X x Y | `World.cpp` | verified |
| Cable cars | side = (-dz, 0, dx) | `CableCars.cpp` | verified |
| AI backup steering | `aiVehiclePhysics::Backup`: atan2(d . m0, -d . m2), steer x -2.857143 | `Driving.cpp` | verified |
| Stand-in player car | none (OpenMM2's tool and tests) | `ai::PlayerCar::at` faced against its velocity (an extra half turn) | fixed |
| Time units | physics in 1/60 s samples; AI at 30 Hz with its per-frame counts; particles 1/60 s; pedestrian frames 1/30 s | `Constants.h`, `World.h`, `Particles.h`, `LineSparks.cpp`, `CamTrack.cpp` | verified (the fixed step is recorded in phys-core and ai-vehicles) |
| Light direction and wall shades | `cityLevel::SetLightDirection`, `sdlCommon::UpdateLighting` | `CityRenderer` | verified |
| Wheel effect index | materials' `ptxindex` into `vehWheelPtx`'s dirt, dust, grass, leaf, smoke, snow, splash, rock | `EffectLibrary` | verified |

## Fixes

| Commit | What |
| --- | --- |
| dc9f9cb | race police posts turned by heading x -0.017444445 (was pi / 180) |
| 67e02bc | the race table's cop count kept as the cop density; the slider steps from its clamped value |
| ffd80d6 | the police chase tests start the suspect 75 m behind the cop (the 70 m point is just off the road with the corrected heading) |
| 52b505e | `tune/<car>.asnode` loaded as the player's steering tune |
| 3d11dff | PKG xref bangers keep only their turn about Y |
| cc92314 | a banger's birth-rule block read whatever it is called |
| 71c0608 | `ai::PlayerCar::at` faces along its velocity |
| dc95635 | scaled instances lit with Direct3D's unnormalised inverse-transpose normals |
| 186760f | the format docs: waypoint, `.opp` and `[Police]` angles, the cop count, `.sky`, `.lmap`, `.ext`, `.reset`, texture rows |

## Open, and for other areas

Nothing in this theme is left open. For the AI area: for crash courses
`mmSingleStunt::LoadEventFile` also writes clamp(AmbDensity, 0, 1) x 0.2
into aiMap +0x3c, which nothing appears to read (likely dead; not checked
further).
