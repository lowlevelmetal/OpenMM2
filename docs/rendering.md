# World rendering

How OpenMM2 draws the city, cars and sky (`src/game/CityRenderer`,
`VehicleRenderer`, `TextureLibrary`, `ModelLibrary`), and the evidence for
each choice. "Verified" means checked against the retail data (usually by
rendering it and looking); "inferred" means a reasoned guess that should be
compared with the original game running.

| Topic | Behaviour | Evidence |
|---|---|---|
| Texture orientation | `.tex` rows are uploaded in file order (bottom row first) and the game's UVs are used unchanged | verified: road markings, facades, car bodies and signs appear upright |
| Texture wrap | models: `.tex` flags 0x2 / 0x4 select repeat in U / V, otherwise clamp; TGA/JPG repeat. Street (PSDL) geometry always repeats | verified: facades repeat their textures vertically although the textures lack flag 0x4; with clamping they smear |
| Mipmaps | file mip levels when the chain is complete, otherwise generated with a box filter | — |
| Animated textures | `tune/<name>.movie` gives `rate N` (frames per second); frames are `texture/<name>-0001.tex` and onwards | verified for s_ocean, s_thames, s_water, s_pond |
| Night textures | at night `<name>_ni` replaces `<name>` when it exists (430 such textures, 401 with a day counterpart: lit windows, shop fronts) | inferred from the data |
| Sky | `city/<map>.sky` names the dome model; its 16 paint jobs are time of day × weather in the same order as the lighting tables (texture letters: a dawn, n noon, d dusk, m midnight; c clear, p partly cloudy, f fog, r rain) | verified by viewing the sky textures |
| Lighting | `city/<map>.ltNN`: key + two fill directional lights and an ambient colour, applied as Direct3D 7-style per-vertex lighting (clamped per vertex) to city geometry and models | the light direction convention (heading about +Y from −Z, negative pitch points down) is inferred; overall brightness at night needs comparison with the original |
| Fog | `city/<map>_fog.csv` row (time × 4 + weather): linear fog colour, start, end; the far plane is the fog end | inferred: the clear colour equals the fog colour |
| Visibility | camera room from the PSDL perimeters; rooms drawn are the CPVS row of that room, or every room within the fog distance when the camera is outside the city or more than 40 m above its room | the 40 m rule is a development convenience |
| City objects | `city/<map>.inst` instances, LOD H < 60 m < M < 130 m < L < 260 m < VL, scaled by the detail setting; PKG xrefs drawn with their parent | LOD distances inferred |
| Car bodies | at the high and medium LODs each body material draws its clean texture (`vpbugyellow_sd` for both `vpbugyellow_sd` and `vpbugyellow_sd_dmg`); the low and very low LODs draw the paint job's materials as stored | MM2 (`vehCarModel::Draw`, `fxTexelDamage::Init`): fxTexelDamage keeps a clean copy per material that impacts patch with `_dmg` texels (not ported yet, see below) |
| Car parts | `vehCarModel::Draw`: body; DECAL (alpha blended); BREAK0-3, BREAK01/12/23/03 and VARIANT<paint> at their pivots; at H the refl_dc reflection pass and the fenders (FNDR0/1 turned with the front wheels, offset from wheel 0's pivot + 2.5 cm, mirrored); wheels and HUB0-3 at the simulation's wheel matrices. VL draws the body only. HLIGHT, SLIGHT, SIREN, SRN and HEADLIGHT meshes are never drawn | MM2 |
| Car reflections | the high LOD body again with `texture/refl_dc` sphere-mapped from the view-space normals, added (ONE/ONE) at the material's power (0.5 for car paint), "Vehicle Reflections" option | MM2 (`modStatic::DrawEnvMapped`, `cityLevel::GetEnvMap`) |
| Car shadow | the high LOD SHADOW mesh turned onto the ground found 1 m above to 1 m (else 5 m) below the car, none on ground steeper than normal.y 0.7; alpha blended, depth tested, no depth writes, pulled forward | MM2 (`vehCarModel::DrawShadow`, `lvlInstance::DrawPhysics`) |
| Car lights | glow pass, unlit (pre-lit parts: white vertices, the black material ignored), added ONE/ONE, no depth writes: TLIGHT and BLIGHT while the brake input is not zero, TLIGHT again with the light flag (evening, night or fog), RLIGHT in reverse | MM2 (`vehCarModel::DrawGlow`, `mmGame::InitWeather`) |
| Headlights | two ltLights at the headlight0/1 pivots in the HEADLIGHT0/1 colours shining forward; each draws `lt_glow` facing the camera, half size 0.2 sqrt(25 cos^3 theta) = cos^1.5 theta metres, colour 0.6 x the light's; with the light flag, or sweeping +-42.4 rad/s while the siren is on | MM2 (`vehCarModel::DrawHeadlights`, `ltLight::DrawGlow`) |
| Police lights | an ltLight per SRN0-3 part (pivot, part colour) whose world-space direction turns 2.5 pi rad/s about Y, a quarter turn apart, drawn as above; the lens flares (`lt_flare`, within ~13 m) are not ported | MM2 (`vehSiren`) |
| Car LOD | lvlInstance::IsVisible: d = view depth of the car - its radius; H up to Med, M up to Low, L up to VLow, VL beyond, none when the car is deeper than NoDraw; thresholds by Object Detail 0-3: Med 20/30/40/70, Low 70/90/100/130, VLow 150/175/200/200, NoDraw 200/250/300/300 m | MM2 (`cityLevel::SetObjectDetail`); the radius is the high LOD body's bounding box half-diagonal (inferred) |

Known differences still to investigate:

* Night brightness of roads and cars.
* Street light glows and flares, headlight beams, water planes
  (`city/<map>.water`), reflections (`refl_*` textures), cloud shadows
  (`shadmap_*`), rain and snow particles.
