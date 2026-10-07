# World rendering

How OpenMM2 draws the city, cars and sky (`src/game/CityRenderer`,
`VehicleRenderer`, `TextureLibrary`, `ModelLibrary`, `MeshDraw`), and the
evidence for each choice. "MM2 (`Function`)" means ported from the original's
code (build 3393); "verified" means checked against the retail data (usually
by rendering it and looking); "inferred" means a reasoned guess that should
be compared with the original game running. OpenMM2 keeps its own renderer
(Vulkan/OpenGL, any resolution); what is ported are the original's
decisions: levels of detail, distances, textures, states and order.

## Textures

| Topic | Behaviour | Evidence |
|---|---|---|
| Orientation | `.tex` rows are uploaded in file order (bottom row first) and the game's UVs are used unchanged | verified: road markings, facades, car bodies and signs appear upright |
| Wrap | models: `.tex` flags 0x2 / 0x4 select repeat in U / V, otherwise clamp; TGA/JPG repeat. Street (PSDL) geometry always repeats | verified: facades repeat their textures vertically although the textures lack flag 0x4; with clamping they smear |
| Mipmaps | file mip levels when the chain is complete, otherwise generated with a box filter | — |
| Animated textures | `tune/<name>.movie` gives `rate N` (frames per second); frames are `texture/<name>-0001.tex` and onwards | verified for s_ocean, s_thames, s_water, s_pond |
| Variants | in game every texture load goes through the variant handler: in rain `<name>_fa` when it exists (82 wet roads and decals), at night (time 3 only) `<name>_ni` when it exists (lit windows, shop fronts, lamps); at night every texture that is not a `_ni` one is darkened, each channel halved on every mip level (cars, sky and `_fa` textures included) | MM2 (`InstallTextureVariantHandler`: the `gfxLoadImage`/`gfxPrepareImage` wrappers) |
| Untextured materials | at night their diffuse colour is halved | MM2 (`modShader::Load`) |
| Alpha | everything is drawn with alpha test GREATER 100; materials and textures with alpha blend and test together | MM2 (`cityLevel::DrawRooms` render states; ALPHABLENDENABLE and ALPHATESTENABLE share a byte) |

## Environment

| Topic | Behaviour | Evidence |
|---|---|---|
| Lighting tables | `city/<map>.ltNN` (time × 4 + weather): key, fill1 and fill2 lights (heading, pitch, colour) and an ambient colour | MM2 (`cityTimeWeatherLighting`) |
| Light direction | the direction a light travels: (cos h cos p, sin p, sin h cos p); vertices take N · −L | MM2 (`cityLevel::SetLightDirection`; the old inferred convention was 90° off) |
| Light quality | the Lighting option (0–1 → quality 0–3, default 3) turns on the key light from quality 1, fill1 from 2, fill2 from 3; the ambient is the file's at quality 3, 33% / 66% of the way to white at 2 / 1, white at 0 | MM2 (`cityLevel::SetupLighting`, `ComputeAmbientLightLevels`; the slider mapping is inferred). MM2 computes the reduced levels before reading the file (from the defaults on the first race); OpenMM2 uses the file's ambient |
| Instance lighting | Direct3D 7-style per-vertex lighting (clamped per vertex) with those lights | MM2. MM2 also tints the directional lights of each room's instances by the room colour (`SetupPerRoomLighting`); every room is white in retail (below), so this has no effect |
| Street geometry | unlit: roads, sidewalks, roofs, fans and ground take the room colour, curbs half of it, facades and slivers the wall light table entry of their facing (ambient + Σ max(0, n · −L) × light colour for 64 horizontal directions), shaded by the room colour | MM2 (`sdlPage16::Draw`, `sdlCommon::UpdateLighting`). MM2 stores each facade's table index in the preceding FacadeBound attribute; OpenMM2 takes it from the wall's normal (inferred to match) |
| Room colours | `city/<map>.lmap` holds one per room, but cityLevel::Load rejects it unless its count equals the PSDL room count including room 0 (London 1340 vs 1341, SF 1125 vs 1171), so in retail every room is white | MM2 (`cityLevel::Load`) |
| Fog | `city/<map>_fog.csv` row (time × 4 + weather): linear fog colour, start = min(far − 30, start), end = min(far, end); the clear colour is the fog colour | MM2 (`lvlSky::SetupFog`, `cityLevel::DrawRooms`) |
| Far plane | the Far Clip option (100–1000 m); OpenMM2's Visibility slider maps 0–1 to that range | MM2 (`PUGraphics::FixClip`); the slider mapping is inferred |
| Sky | `city/<map>.sky` "model yOffset yScale speed" (retail `sky_dome_l 0 0.95 0.005`): the dome at (camera x, camera y × yScale + yOffset, camera z), turning about Y at `speed` rad/s, unlit, unfogged, no depth; its 16 paint jobs are time of day × weather (texture letters: a dawn, n noon, d dusk, m midnight; c clear, p partly cloudy, f fog, r rain) | MM2 (`lvlSky::AutoInit`, `Update`, `DrawHat`); paint jobs verified by viewing the sky textures |
| Light flag | evening, night or fog: car lights on (see below) | MM2 (`mmGame::InitWeather`) |
| Lamp glows | at night only (time 3) | MM2 (`mmGame::InitWeather` → `dgBangerManager::InitGlow`), see bangers.md |

## City

| Topic | Behaviour | Evidence |
|---|---|---|
| Camera room | the PSDL room under the camera; outside every room the last one found stays | MM2 (`cityLevel::Draw`, sm_LastPvsRoom; MM2 tests ordinary rooms in XZ only) |
| Visibility | the CPVS row of the camera room; OpenMM2 falls back to every room within the far plane before the camera has ever been inside the city or without a PVS | MM2 (PVS path); the fallback is an OpenMM2 debug convenience |
| City objects | `city/<map>.inst` instances, LOD by lvlInstance::IsVisible (below) with no distance limit beyond the far plane; a missing LOD takes the next less detailed one (VL → L → M → H) and a missing VL draws nothing; PKG xrefs drawn with their parent | MM2 (`lvlInstance::IsVisible`, `GetGeomSet`); the radius is the model's bounding box half-diagonal (inferred) |
| Object LOD | d = view depth − radius: H up to Med, M up to Low, L up to VLow, VL beyond; dynamic objects (cars, bangers) are not drawn deeper than NoDraw. Object Detail 0–3: Med 20/30/40/70, Low 70/90/100/130, VLow 150/175/200/200, NoDraw 200/250/300/300 m | MM2 (`cityLevel::SetObjectDetail`) |

Not ported yet: street LODs (`sm_SDLMedThresh` 50, `LowThresh` 100,
`VLowThresh` 300 m: the low-detail road strips with the `*_lo` textures and
no curbs), cloud shadows (`shadmap_day`/`shadmap_nite` in a second pass with
UVs (x + y, y + z) / 128), the `<name>_refl` reflection parts, the room
flood fill used without a PVS, and per-room instance lists (MM2 draws an
object once from every room it touches).

## Cars

| Topic | Behaviour | Evidence |
|---|---|---|
| Car bodies | at the high and medium LODs each body material draws its clean texture (`vpbugyellow_sd` for both `vpbugyellow_sd` and `vpbugyellow_sd_dmg`); the low and very low LODs draw the paint job's materials as stored | MM2 (`vehCarModel::Draw`, `fxTexelDamage::Init`): fxTexelDamage keeps a clean copy per material that impacts patch with `_dmg` texels (not ported yet) |
| Car parts | `vehCarModel::Draw`: body; DECAL (alpha blended); BREAK0-3, BREAK01/12/23/03 and VARIANT<paint> at their pivots; at H the refl_dc reflection pass and the fenders (FNDR0/1 turned with the front wheels, offset from wheel 0's pivot + 2.5 cm, mirrored); wheels and HUB0-3 at the simulation's wheel matrices. VL draws the body only. HLIGHT, SLIGHT, SIREN, SRN and HEADLIGHT meshes are never drawn | MM2 |
| Car reflections | the high LOD body again with `texture/refl_dc` sphere-mapped from the view-space normals, added (ONE/ONE) at the material's power (0.5 for car paint), "Vehicle Reflections" option | MM2 (`modStatic::DrawEnvMapped`, `cityLevel::GetEnvMap`) |
| Car shadow | the high LOD SHADOW mesh turned onto the ground found 1 m above to 1 m (else 5 m) below the car, none on ground steeper than normal.y 0.7; alpha blended, depth tested, no depth writes, pulled forward | MM2 (`vehCarModel::DrawShadow`, `lvlInstance::DrawPhysics`) |
| Car lights | glow pass, unlit (pre-lit parts: white vertices, the black material ignored), added ONE/ONE, no depth writes: TLIGHT and BLIGHT while the brake input is not zero, TLIGHT again with the light flag (evening, night or fog), RLIGHT in reverse | MM2 (`vehCarModel::DrawGlow`, `mmGame::InitWeather`) |
| Headlights | two ltLights at the headlight0/1 pivots in the HEADLIGHT0/1 colours shining forward; each draws `lt_glow` facing the camera, half size 0.2 sqrt(25 cos^3 theta) = cos^1.5 theta metres, colour 0.6 x the light's; with the light flag, or sweeping +-42.4 rad/s while the siren is on | MM2 (`vehCarModel::DrawHeadlights`, `ltLight::DrawGlow`) |
| Police lights | an ltLight per SRN0-3 part (pivot, part colour) whose world-space direction turns 2.5 pi rad/s about Y, a quarter turn apart, drawn as above; the lens flares (`lt_flare`, within ~13 m) are not ported | MM2 (`vehSiren`) |
| Car LOD | as objects (above), culled beyond NoDraw | MM2 (`lvlInstance::IsVisible`); the radius is the high LOD body's bounding box half-diagonal (inferred) |

Not ported yet: texel damage (`fxTexelDamage::ApplyDamage` copies 7-row
patches of `_dmg` texels around random points of the triangles within
TextelDamageRadius of an impact), suspension and engine parts (SHOCK, ARM,
SHAFT, AXLE, ENGINE need the suspension matrices), parts breaking off
(`vehBreakableMgr`), glass shards (`fxShardManager`) and sparks
(`asLineSparks`). Traffic cars (`aiVehicleInstance`) use the same light
rules here; MM2 draws their turn signals (SLIGHT0/1) and a shared headlight
glow, not ported.
