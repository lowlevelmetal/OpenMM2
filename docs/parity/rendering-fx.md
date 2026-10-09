# Parity audit: rendering-fx

Audited against MM2Recomp (midtown2.exe build 3393) on 2026-10-07.

Summary: 197 rows (functions; some rows split a constructor's or a draw
function's parts, group small helpers, or cover a shader); verified 67,
fixed 95, deviation 8, inferred 3, open 4, openmm2 20. Missing MM2
behaviour: 7 items. A second pass (2026-10-08) ported the tunnels, the
rear-view mirror, the wide-angle letterbox, emissive materials, the sirens'
lens flares, cloud shadows, the far pedestrians' stick figures, the Texture
Quality limit and the session-long lighting tables.

Scope: the city, car, traffic, pedestrian and signal renderers
(`CityRenderer`, `VehicleRenderer`, `AiRenderer`, `MeshDraw`), the model
and texture libraries, texel damage, the collision view of the city
(`CityLevel`: its instances, materials and room queries), the particle
effects (`src/game/fx`), the render-state semantics the shaders implement
(`src/render/Types`, `src/render/shaders`) and the intro movie. The street
drawing of `sdlPage16::Draw` was ported as part of this audit into
`src/city/SdlDraw` (the city module's owner had finished; its rows are
here). Every renderer decision below was compared with the MM2 function's
decompile, and with the assembly where the decompile lost operands (the
`Vector3` helpers of the divided-road and wedge code, `vehCarDamage`'s smoke
rule, the render-state offsets). Facts checked again and kept: the
render-state byte layout (`RSTATE` + 0x18 Z enable, + 0x1a Z write, + 0x1b
cull, + 0x1d/0x1e alpha reference/function, + 0x25 fog vertex mode; the
default cull mode is clockwise, which OpenMM2 draws as counter-clockwise
front faces), the default alpha test (alpha not 0) and the object passes'
GREATER 100, and that every retail room colour is white because
`cityLevel::Load` rejects the `.lmap` files.

## src/game/CityRenderer.h, CityRenderer.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `lightDirection` | `cityLevel::SetLightDirection` | verified | (cos h cos p, sin p, sin h cos p): the direction the light travels |
| `ambientForQuality` | `cityLevel::SetupLighting`, `cityTimeWeatherLighting::ComputeAmbientLightLevels`, `LoadCityTimeWeatherLighting` | fixed | quality 3 the table's ambient, 0 white, 1 / 2 each channel c + ((255 − c) × 168 / 84 >> 8) of the ambient the table held *before* its .ltNN loaded (the levels are computed first): 0xFF101010 for the first city of a session, the previous city's afterwards (`city::loadCity` keeps the history) |
| `makeEnvironment`: lights | `cityLevel::SetupLighting` | verified | key light from quality 1, fill1 from 2, fill2 from 3 |
| `makeEnvironment`: wall light table | `sdlCommon::UpdateLighting` | verified | 64 facings a = i π / 32 − π / 2: ambient + Σ max(0, n · −L) × colour, clamped per channel |
| `makeEnvironment`: fog, far plane, clear colour | `lvlSky::SetupFog`, `cityLevel::DrawRooms`, `PUGraphics::FixClip` | verified | linear, start min(far − 30, start), end min(far, end); clear colour = fog colour. The table's integer distances (`lvlSky::AutoInit`'s atoi) were fixed on integration by dc8c599 |
| `makeEnvironment`: sky paint job | `lvlSky::AutoInit`, `lvlSky::DrawHat` | verified | time × 4 + weather (snow uses the rain tables: OpenMM2's snow has no MM2 counterpart) |
| `makeEnvironment` without a lighting table | `LoadCityTimeWeatherLighting` | openmm2 | only synthetic cities lack a table now: `city::loadCity` keeps the sixteen tables for the session and loads each .ltNN over its table (`datParser::Load`), so a missing file or field keeps the previous value (the constructor's for the first city) — fixed in the second pass |
| `makeEnvironment`: cloud shadows | `cityLevel::Load` (`vglSetCloudMap`), `mmGame::SetLevelGraphics` | fixed | shadmap_day for times 0 and 1, shadmap_nite for 2 and 3; the flag mask 0 / 4 / 2 by the Cloud Shadows option (second pass) |
| `unpackRgb`, `transformBounds`, `argbToRgba` | — | openmm2 | colour and bounds helpers |
| `sdlTextureName` | `lvlSDL::LoadBinary` | fixed | a PSDL name ending in "-0NNN" is looked up by its base (gfxGetTextureMovie plays the frames) |
| `findFilledLod` | `lvlInstance::GetGeomSet` | fixed | a missing level takes the next less detailed one only (VL → L → M → H); a missing VL draws nothing. The fallback for models without LOD names is OpenMM2 glue |
| `geomRadius` | `lvlInstance::GetGeomSet`, `modGetStatic` | fixed | the farthest vertex from the model origin over the part's levels (was the bounds' half diagonal) |
| constructor: streets | `cityLevel::Load` (`sdlPage16::ComputeBoundSphere`), `sdlPage16::Draw` | fixed | the room spheres and the four levels of `SdlDraw`'s primitives; vertex shading per primitive; one texture slot per PSDL name |
| constructor: tunnels | `sdlPage16::Draw` (Tunnel attributes) | fixed | SdlDraw's tunnels (second pass; the CityMesh stand-in is gone) |
| constructor: texture names | `cityLevel::Load` under `gfxTexReduceSize` | fixed | names the streets', city objects' (with xrefs) and sky's textures while RaceScreen holds the Texture Quality limit (second pass) |
| constructor: objects | `lvlLevel::LoadInstances`, `lvlMultiRoomInstance::Create` | fixed | a collidable (flag 0x2000, not terrain-local) object is listed in the neighbours of its room that its sphere (position, model radius) reaches, not in its own room; reaching none it is never drawn |
| `CityRenderer::setEnvironment` | `sdlPage16::Draw` (vglCurrentColor), `GetShadedColor`, `cityLevel::Load` | fixed | room colour (white), half of it where Draw halves it (curb faces and caps, raised dividers' walls), facades and slivers the light table entry of the room's last FacadeBound (was the wall's rounded facing) |
| `CityRenderer::update` | `lvlSky::Update` | verified | the dome turns at the .sky rate |
| `CityRenderer::drawMesh` | `modStatic::Draw` | verified | through `drawGpuMesh` |
| `CityRenderer::drawSky` | `lvlSky::DrawHat` | verified | at (camera x, camera y × yScale + yOffset, camera z), unlit, unfogged, no depth |
| `CityRenderer::resolve` | `lvlInstance::GetGeomSet` | verified | the model and its radius; loading on first use is OpenMM2's |
| `CityRenderer::drawModel` | `lvlFixedAny::Draw`; xrefs: `lvlLevel::LoadInstances` | open | draws the model's high-to-low set and its cloud pass; PKG xrefs are drawn as static children here, while MM2 turns each into an unhit banger (camera-props; see Missing). No retail city model has the `mask`, `refl`, `nonrandom` or `opaque` parts lvlFixedAny::Draw / DrawReflectedParts / Init use (scanned every geometry/*.pkg) |
| `CityRenderer::drawCloudShadow` | `lvlFixedAny::Draw`, `modStatic::DrawOrthoMapped`, `gfxPacket::OrthoMap` | fixed | packets whose texture has the cloud bit and no alpha format, again with the cloud map at ((y + x), (y + z)) / 128 of their model-space positions, white, alpha blended and tested above 0, fogged (second pass) |
| `CityRenderer::drawInstance` | `lvlInstance::IsVisible`, `lvlMultiRoomInstance::Draw` | fixed | d = view depth − radius against the Object Detail thresholds, no NoDraw limit for static objects; a multi-room object once per frame |
| `CityRenderer::gatherStreets` | `sdlPage16::Draw`, `sdlCommon::BACKFACE` | fixed | road fans, crosswalks and roofs above the camera's height and walls seen from behind are left out. MM2's height is the camera's plus the view matrix's third row times the near distance (deviation of at most the near distance, ignored) |
| `CityRenderer::drawStreets` | `vglBeginBatch`, `vglEndBatch`, `gfxRenderState::DoFlush` | fixed | one batch per texture, opaque formats first then alpha formats (GREATER 100), the textures' own address modes, the default culling (was forced repeat, then no culling); an opaque bucket with the cloud bit drawn again with the cloud map at ((y + x), (z + y)) / 128 in world space (`vglEndBatch`'s second pass, none for alpha textures; second pass) |
| `CityRenderer::draw` | `cityLevel::Draw`, `cityLevel::DrawRooms` | fixed | the camera's room (or the last one found), the CPVS row, each room's sphere tested against the view, the street level of detail from the sphere's depth − radius (camera room: − radius), streets in one batch, then the objects room by room from the end of the list. Without a PVS OpenMM2 tests every room's sphere (MM2 floods out through the neighbours: deviation, an OpenMM2 debug path) |
| `DetailSettings`, `EnvironmentOptions` | `cityLevel::SetObjectDetail`, `PUGraphics` options | openmm2 | option plumbing |

## src/city/SdlDraw.h, SdlDraw.cpp (new, ported in this audit)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Builder::tunnel`, `junctionTunnel` | `sdlPage16::Draw` (Tunnel, ten words) | fixed | second pass, from the assembly: walls on the first mask's perimeter edges (u = max(1, length / height)), inner walls with 0x4000 unless 0x4, the ceiling fan at the highest corner + height from the stored corner backwards (0x8), with 0x4 an apron 1 m below the highest corner, its corners pushed out by 0.333 x height beside a wall (0.25 m elsewhere), and on each walled edge a railing face and top whose ends the second and third masks bevel (x 1.414); nothing at height 0 |
| `Builder::stripTunnel`, `beside` | `sdlPage16::Draw` (Tunnel, other counts) | fixed | second pass, from the assembly: along the next road, divided road or rectangle strip (past a Texture attribute): straight walls (0x1 left first texture, 0x2 right second; 0x4000 both sides) or bulging ones (0x2000, out to the railing line at a quarter and three quarters of the height; caps 0x10/0x20, 0x40/0x80, the left start cap's upper corner at the full height as in MM2); railings (0x4: fourth / fifth texture, end points pushed along the road by 0x200/0x400 and 0x800; MM2's right-hand loop stops before the last section so 0x1000 does nothing there) and the deck between them (sixth texture); a flat (0x8) or arched (0x100: 1.5 m at the quarters, 2 m in the middle) ceiling with ArcMap across. Another next type draws nothing (MM2 would reuse the last stride; no retail data) |
| `Builder::finish`: non-finite corners | Direct3D with infinite coordinates | deviation | a primitive with a non-finite corner (SF's junction railings around duplicated perimeter corners give rail / 0) is dropped; MM2 sends it and Direct3D draws nothing visible |
| `sdlWallMap` | `sdlPage16::WallMap` | fixed | as ArcMap with whole repeats of the given length (second pass) |
| `buildSdlRoomDraw`, `Builder::build` | `sdlPage16::Draw` | fixed | the attribute walk; a Texture value 0 skips the road, sidewalk, rectangle, crosswalk, fan and divided road attributes after it; FacadeBound draws nothing but sets the light index (0 at the start of each room) |
| `Builder::lowDetailRoad` | `sdlPage16::Draw` (road and divided road, levels 0 and 1) | fixed | the group's third texture, ArcMap along the outer left edge over the full width; level 0 every other section (0, 2, … for an odd count; 0, 1, 3, … for an even one), level 1 every section with both edges 0.15 m lower |
| `Builder::sidewalkSide` | `sdlPage16::Draw` (levels 2 and 3) | fixed | the second texture from the outer edge (t 1) to the curb (t 0, raised 0.15 at level 3); at level 3 the half-bright curb face. Drawn only when the first section's outer and curb vertices differ |
| `Builder::road` | `sdlPage16::Draw` (levels 2 and 3) | fixed | the first texture in two strips mirrored about the middle (t 1 at the curbs, 0 at the middle), ArcMap along the left curb over the curb-to-curb width; a road strip's halves meet at the curbs' midpoint, a divided road's at the median edges |
| `Builder::roadStrip` | `sdlPage16::Draw` (type 0) | fixed | sections of four |
| `Builder::sidewalkStrip` | `sdlPage16::Draw` (type 1) | fixed | planar 4 m repeats less the whole repeats at the first curb vertex; level 3: half-bright curb face then the sidewalk from the raised curb; a two-pair strip whose first pair is (0, 0) or (1, 1) is a half-bright end-cap triangle at level 3 only; levels 0–2 flat |
| `Builder::rectangleStrip` | `sdlPage16::Draw` (type 2) | fixed | the first texture, ArcMap across the pairs, every level |
| `Builder::sliver` | `sdlPage16::Draw` (type 3) | fixed | u = floor(flat length × density + 0.5), v = (corner height − top) × density, a fan of 3 or 4 corners by which ends reach the top; the light table shading; every level. The untextured flat-colour variant at level 0 depends on a flag that is never set |
| `Builder::crosswalk` | `sdlPage16::Draw` (type 4) | fixed | the third texture, (1, 0)/(0, 0) on the first pair, v = length / width on the second; only when not above the camera |
| `Builder::fan` | `sdlPage16::Draw` (types 5 and 6) | fixed | planar 8 m repeats less the whole repeats at the first vertex; road fans only when not above the camera |
| `Builder::roof` | `sdlPage16::Draw` (type 12) | fixed | at the stored height, planar 8 m repeats, only when not above the camera; drawn even after a Texture value 0 |
| `Builder::facade` | `sdlPage16::Draw` (type 11) | fixed | the repeats read unsigned, v 0 at the bottom; the light table shading; every level |
| `Builder::dividedRoadStrip` | `sdlPage16::Draw` (type 8) | fixed | sections of six; levels 0/1 as a road from outer edge to outer edge, levels 2/3 sidewalks, lanes and the median |
| `Builder::divider` | `sdlPage16::Draw` (type 8 median) | fixed | flat (1): the divider texture + 1, t up to the height; raised (2): half-bright walls (+0), 45° bevels in and up by the height (+1), the top (+2), caps (+3; the end cap keeps the texture still bound: +3 after a start cap, else +2); wedged (3): sides 0.4 m in and 1 m up (+1), top (+2), caps; any other type draws nothing ("Bad Median Type") |
| `Builder::texture`, `tex`, `vert`, `height`, `at`, `begin`, `vertex`, `endStrip`, `endFan`, `endList`, `finish`, `wallPrimitive` | vgl immediate mode (`vglBegin` types 4/5/6, `vglBindTexture`) | openmm2 | strips and fans become triangle lists with the strip's alternating winding kept; the page's texture table holds none at 0 |
| `sdlArcMap` | `sdlPage16::ArcMap` | fixed | segments along the first vertices, repeats = max(1, floor(length / (width sum / count) + 0.5)), scaled so the longest segment stays under 128, run back and forth; all 0 without width or under 0.1 m |
| `sdlRoomCentroid` | `sdlPage16::GetCentroid` | fixed | area centroid in the ground plane at the mean corner height, a corner repeating its predecessor skipped; the first corner without area |
| `sdlRoomBoundSphere` | `sdlPage16::ComputeBoundSphere` | fixed | the centroid and the farthest perimeter corner |
| `sdlRoomLod` | `cityLevel::DrawRooms` | fixed | beyond 300 / 100 / 50 m levels 0 / 1 / 2, else 3 (sm_SDLVLowThresh, LowThresh, MedThresh, never changed) |
| `sdlBackface` | `sdlCommon::BACKFACE` | fixed | the camera behind the wall's vertical plane, tested in x and z |

## src/game/CityLevel.h, CityLevel.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `toTerrainData`, `rowLength` | — | openmm2 | conversions |
| `modelRadius`, `boxCornerRadius` | `lvlInstance::GetRadius`, `InitBoundTerrainLocal`'s box helper | fixed | an object's sphere radius is its geometry set's (farthest vertex), raised for terrain-local models to the farther of the bound box's corners (was \|centroid\| + bound radius) |
| `loadBoundFile` | `phBoundGeometry::Load`, `LoadBinary` | verified | bound/<name>_bound.bnd or .bbnd |
| `toGeometryData` | `phBoundGeometry::Load` | verified | text quads stay quads |
| `StaticInstance::bound` | `lvlInstance::GetBound` | verified | |
| constructor: materials | `cityLevel::Load`, `lvlMaterialMgr::Load`, `lvlMaterial::lvlMaterial`, `lvlMaterial::Load`, `phMaterialMgr::Load` | fixed | built-in default, then new names of city/materials.mtl, then of the bound files in load order; a text bound's new name is an lvlMaterial (file elasticity, friction, effect, sound with "none" = 0; drag 0, width 1, height 0, depth 0, ptx −1/−1, 0.25/0.5) (width was 0). A binary bound adds a plain phMaterial, but no retail terrain bound introduces a new name (checked over all 184 .ter files' geometry) |
| constructor: texture → material table | `lvlSDL::LoadBinary` | verified | materials.csv names as 1-based manager indices, before any bound adds materials |
| constructor: terrain-local objects | `lvlInstance::InitBoundTerrainLocal`, `phBoundTerrain::Load` | fixed | a .ter that is not version 1.1 or whose polygon count differs from the geometry's is "Malformed terrain": no bound. Listed in their own room only (`lvlLevel::MoveToRoom`), which is an instance room every neighbour returns (were also listed in the rooms their sphere touched) |
| constructor: collidable objects | `lvlMultiRoomInstance::Create` | fixed | scaled bound and normalised matrix; stand-ins in the neighbours the sphere reaches, never the own room, nothing when none (were in the own room too) |
| constructor: flags | `lvlLevel::LoadInstances` (flags 0x130 / 0x110), `lvlMultiRoomInstance::IsCollidable`, `IsTerrainCollidable` | verified | the stand-ins answer not collidable, terrain-collidable once per gather |
| constructor: probe geometry | — | openmm2 | OpenMM2's wheel probe soup |
| `CityLevel::material` | `lvlLevelBound::GetMaterial` | verified | 0 the default, n entry n − 1 |
| `CityLevel::removeSource` | — | openmm2 | |
| `CityLevel::findRoom` | `cityLevel::FindRoomId`, `FullProbe`, `IsInRoomCheckWarps` | open | RoomLocator's search; MM2 tries the hint, its neighbours and the warps first with per-room height ranges (see Missing; RoomLocator takes a hint on integration) |
| `CityLevel::touchedNeighbors`, `cityTouchedNeighbors` | `cityLevel::GetTouchedNeighbors`, `GetTouchedNeighborsR` | verified | each neighbour once, instance rooms whatever the sphere, the edge test in the ground plane; now a free function the renderer shares |
| `CityLevel::collect` | `lvlSDL::CollidePolyToLevel` | verified | `sdlPage16::Collect` of each room into one 256-polygon buffer with one resume state; overflow logged |
| `CityLevel::instances` | `lvlLevel` room lists | verified | |

## src/game/AiRenderer.h, AiRenderer.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `AiRenderer::AiRenderer`, `carModel`, `pedType` | — | openmm2 | caches |
| `AiRenderer::draw`: cars | `aiVehicleInstance::Draw`, `aiVehicleInstance::SetColor` | fixed | the traffic path of VehicleRenderer, Object Detail thresholds (was an invented draw distance), paint trunc(frand × (count − 1)) |
| `AiRenderer::draw`: pedestrians | `aiPedestrianInstance::Draw` | fixed | nothing beyond NoDraw (was a fixed distance) |
| `AiRenderer::draw`: signals | `aiTrafficLightInstance::Draw` | fixed | `lvlInstance::IsVisible` with the Object Detail thresholds |
| `AiRenderer::draw`: room visibility | `cityLevel::DrawRooms` | fixed | MM2 draws traffic, pedestrians and signals from the visible rooms' lists; ported in the MM2-side audit (`RoomVisibility`, see mm2/city-render.md) |
| `AiRenderer::drawPed` | `pedAnimationInstance::Draw`, `modModel::Draw` | fixed | posed with the root drift taken out; the default culling (was none; the retail meshes face out counter-clockwise, tested) |
| `AiRenderer::drawSkeleton` | `aiPedestrianInstance::Draw`, `pedAnimation::DrawSkeleton`, the pedestrian type loader's .rays reading | fixed | second pass: beyond 35 m, for each bone with a start width its position raised by the offset, a quad to its parent across the modelview's first row taken in the pedestrian's space (round 3, frames: was the camera's right axis), coloured trunc(255 x diffuse) of the variant's shader its row names; untextured, unlit, both sides |
| `AiRenderer::drawSignal` | `aiTrafficLightInstance::Draw`, `DrawGlow` | fixed | the first shader set; the light's glow and the walk signal both or neither, added, unfogged, default alpha test, only within NoDraw |

## src/game/VehicleRenderer.h, VehicleRenderer.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `argb`, `rotateUpTo` | `Matrix34::RotateTo` | verified | colour packing; the shadow's tilt |
| constructor | `vehCarModel::Init`, `GetSurfaceColor`, `InitSirenLight` | verified | radius from `geomRadius` (fixed), headlight and siren lights, fender offset (+2.5 cm) |
| `VehicleRenderer::setPaintjob` | `vehCarModel::Init`, `fxTexelDamage::Init` | verified | paint job modulo the shader sets (fixed in ModelLibrary) |
| `VehicleRenderer::applyDamage`, `resetDamage` | `fxTexelDamage::ApplyDamage`, `Reset`, `vehCarModel::ClearDamage` | verified | |
| `VehicleRenderer::nearestBreakable` | `vehBreakableMgr::Impact` | verified | the attached part (break0–3, 01, 12, 23, 03, the paint job's variant, in that list order) whose pivot is nearest, ties to the first |
| `VehicleRenderer::wreckParts` | `vehCarModel::EjectOneshot` | verified | once; > 100 mph every wheel, hub, fender and the engine in registration order; > 75 two random wheel/hub pairs and a fender; > 50 one pair. MM2 registers a part only with geometry; OpenMM2 requires its pivot (no difference in retail). The parts' speed (1.3 × the car's) is the session's |
| `VehicleRenderer::reattachAll`, `materialTextures`, `paintjob` | `vehCarModel::ClearDamage`, `fxShardManager::Init` | verified | |
| `VehicleRenderer::lodFor` | `lvlInstance::IsVisible` | verified | dynamic objects end at NoDraw |
| `VehicleRenderer::setLightGlowScales` | `ltLight` glow globals (`vehSiren::vehSiren`, `aiVehicleManager::Init`) | fixed | 0.2 / 0.95 in single player; the session sets 0.2 / 0.6 when sirens are built later |
| `VehicleRenderer::setTraffic` | `aiVehicleInstance::Draw` | fixed | no texel damage |
| `VehicleRenderer::drawPart` | `lvlInstance::GetGeomSet`, `modStatic::Draw` | fixed | less detailed fill only |
| `VehicleRenderer::drawReflection` | `modStatic::DrawEnvMapped`, `modShader::BeginEnvMap`, `cityLevel::GetEnvMap` | fixed | world-space normals (u 0.5 + 0.5x, v 0.5 − 0.5y), ambient grey ftol(power × 255), added, fogged |
| `VehicleRenderer::draw` | `vehCarModel::Draw`, `DrawShadow`, `DrawGlow` | verified | |
| `VehicleRenderer::drawCar` | `vehCarModel::Draw`, `vehBreakableMgr::Draw` | fixed | VL body only with the stored shaders; panoz gt paint 4 alpha reference 0; the extra `_dmg` loop removed |
| `VehicleRenderer::wheelMatrix` | `vehCarModel::Draw` | fixed | WHL4/WHL5 at WHL2/WHL3's matrix moved 2.2 wheel radii back along the body |
| `VehicleRenderer::drawTraffic` | `aiVehicleInstance::Draw` | fixed | the colour's shaders, BREAK0–3 at H, reflection and wheels only at H (LAME_WHEELS off), no steering; with an aiVehicleActive the vehWheelCheaps' matrices (round 3, frames) |
| `VehicleRenderer::shadowMatrix`, `drawShadow` | `vehCarModel::DrawShadow`, `lvlInstance::DrawPhysics` | fixed | default alpha test (was GREATER 100) |
| `VehicleRenderer::addLightGlow`, `drawGlows` | `vehCarModel::DrawGlow`, `DrawHeadlights`, `ltLight::DrawGlow`, `aiVehicleInstance::DrawGlow` | fixed | default alpha test; traffic: TLIGHT while braking and with the light flag, one white headlight pair pulled 0.2 m to the camera |
| headlight sweep with the siren | `vehCarModel::DrawHeadlights` | fixed | ±42.4 rad/s sweep of the two world-space directions from wherever they point (round 3, frames); MM2 also turns them for the mirror's view (deviation: once a frame) |
| suspension and engine parts | `vehSuspension::Update`, `vehCarModel::Init` (shock0–3, arm0–3, shaft2/3, axle0/1, engine) | deviation | not drawn; no retail vehicle model has any of these parts (scanned every geometry/v*.pkg) |
| traffic turn signals | `aiVehicleInstance::DrawGlow` (SLIGHT0/1) | open | see Missing (ai-vehicles) |
| `VehicleRenderer::setLensFlareTarget`, siren flares in `drawGlows` | `vehSiren::Init`, `vehSiren::Draw` | fixed | second pass: one ltLensFlare(20) per car with sirens; each siren light's flares queued with ltLight::ComputeIntensity(eye, 0.05) |

## src/game/MeshDraw.h, MeshDraw.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `drawGpuMesh` | `modStatic::Draw`, `modShader::Load`, `gfxRenderState::DoFlush` | fixed | untextured materials halved at night only when lit; alpha blending (and test) when the diffuse alpha is not 1 or the texture's format has alpha (was by texels); the default culling |
| `drawGpuMesh`: emissive colour | `modShader` (the compact third colour), Direct3D lighting | fixed | second pass: added to the lit colour before the per-vertex clamp (DrawConstants::emissive, mesh.vert) |
| `ObjectDetail::forLevel` | `cityLevel::SetObjectDetail` | verified | Med 20/30/40/70, Low 70/90/100/130, VLow 150/175/200/200, NoDraw 200/250/300/300 |
| `objectLod` | `lvlInstance::IsVisible` | verified | strictly beyond each threshold |
| `viewDepth` | `lvlInstance::IsVisible` (`gfxViewport` depth) | verified | |

## src/game/ModelLibrary.h, ModelLibrary.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `GpuModel::find` | — | openmm2 | nearest-LOD lookup for OpenMM2 tools; renderers use `findFilledLod` |
| `GpuModel::materials` | `vehCarModel::Init`, `lvlSky::Init` | fixed | paint job modulo the number of shader sets |
| `ModelLibrary::add`: second texture coordinates | `gfxPacket::OrthoMap` | fixed | second pass: uv1 holds the cloud map coordinates ((y + x), (y + z)) / 128 of the model-space position |
| `ModelLibrary::get`, `add`, destructor | `modGetStatic` | verified | the radius per mesh (farthest vertex); materials as the reader snapped them (the renderer no longer re-rounds) |
| `argbToRgba`, `lodRank` | — | openmm2 | |

## src/game/TextureLibrary.h, TextureLibrary.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `darkenImage` | the variant handler's darkening (`InstallTextureVariantHandler`) | verified | each channel halved on every level |
| `TextureLibrary::get` | `gfxGetTexture`, `gfxGetTextureMovie` | fixed | the plain texture first, else frames 1–128 at the .movie rate (30 without it); with or without mipmaps |
| `TextureLibrary::loadVariant` | `InstallTextureVariantHandler` | fixed | `_fa` in rain and `_ni` at night tried for every name |
| `TextureLibrary::readImage` | `gfxLoadImageAll`, `gfxLoadTexImage`, `gfxLoadTargaImage`, `gfxLoadBmpImage`, JPEG | fixed | .tex → .tga → .bmp → jpg/<name>.jpg, falling through on failure; Targa, BMP and JPEG flipped to bottom-up; alpha by format (formats 2, 6, 14, 16, 18, 32-bit Targa) |
| `TextureLibrary::load` | `gfxGetTexture`, `gfxImage::GenerateMipmaps`, `gfxTexture::Create` | fixed | the file's levels exactly; a square Targa a full 2×2 chain, a JPEG one generated level; clamp U 0x1, clamp V 0x10000, else repeat |
| `TextureLibrary::update` | `gfxTextureMovie::Update`, `UpdateAll` | verified | frame from the accumulated game time × rate; MM2 steps a float timer per frame (deviation in rounding only) |
| `TextureLibrary::image`, `adopt`, `release`, `clear`, `setVariants` | — | openmm2 | cache management; `setVariants` is the variant handler's switch |
| `TextureLibrary::setSizeLimit`, `declare`, the limit in `load` | `cityLevel::Load` (`gfxTexReduceSize` = 32 << gfxTextureQuality while the city loads), `gfxDefaultPrepareImage`, `gfxImage::Halve` | fixed | second pass: a texture named under a limit keeps it whenever it loads; top mip levels dropped, or a single level halved keeping every other texel of every other row (counted from the loader's top row), until both sides fit. Props placed from pathsets are not named (deviation) |
| `TextureLibrary::cloudMap` | `vglSetCloudMap` | fixed | second pass: the image through the variant handler, every texel black with its alpha inverted, no mipmaps |

## src/game/TexelDamage.h, TexelDamage.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| constructor | `fxTexelDamage::Init`, `gfxTexture::Clone` | fixed | private single-level copies of the clean textures (were mipmapped); a `_dmg` material without its clean texture starts damaged; address modes from the clean texture |
| `TexelDamage::stamp` | `fxTexelDamage::ApplyDamage` | verified | the 7-row small or large patch, clipped, no wrap |
| `TexelDamage::apply` | `fxTexelDamage::ApplyDamage` | verified | triangles with a corner within the radius, a random barycentric point, the patch size by irand & 1 |
| `TexelDamage::upload` | `gfxTexture` lock/unlock | fixed | level 0 only |
| `TexelDamage::reset`, destructor | `fxTexelDamage::Reset` | verified | |

## src/game/fx/BirthRule.h, BirthRule.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `BirthRule` defaults | `asBirthRule::asBirthRule` | verified | Life, Mass, Radius, Damp, Intensity 1, Gravity −9.8, Color −1 |
| `loadBirthRule` | `asBirthRule::FileIO` | verified | field names and the 0xAABBGGRR colour |
| `parseBirthRuleFile` | `asBirthRule::Load` (datParser) | verified | |

## src/game/fx/EffectLibrary.h, EffectLibrary.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `EffectLibrary::load` | `vehWheelPtx::Init`, `mmGame::InitWeather` | verified | tune/effects/<name>.asbirthrule by name; rain |
| `EffectLibrary::rule`, `wheelRule` | — | openmm2 | lookups |
| `EffectLibrary::wheelRuleName` | `vehWheelPtx::PtxName` | verified | dirt, dust, grass, leaf, smoke, snow, splash, rock |
| `wheelSheet`, `rainSheet` | `vehWheelPtx::Init`, `mmGame::InitWeather` | verified | ptx_wheel 8×8, ptx_rain 4×4 |

## src/game/fx/Particles.h, Particles.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `ParticleSystem::init` | `asParticles::Init` | fixed | the spew fraction starts at 0 |
| `ParticleSystem::reset` | `asParticles::Reset` | fixed | drops the birth matrix, keeps the spew fraction |
| `ParticleSystem::initSpark` | `asBirthRule::InitSpark` | verified | the original's draw order |
| `ParticleSystem::blast` | `asParticles::Blast` | verified | position through the system's matrix, capped by the pool |
| `ParticleSystem::update` | `asParticles::Update` | verified | spew, drag from \|v + wind\|, alpha/rotation/radius steps through ftol(× 60 dt), the BirthFlags |
| `FixedTicker::advance` | — | deviation | effects step at a fixed 60 Hz (at most 8 steps a frame); MM2 steps once per frame with the frame time, identical at 60 fps |

## src/game/fx/ParticleRenderer.h, ParticleRenderer.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `ParticleRenderer::ParticleRenderer` | `asMeshCardInfo::Init` | verified | 32 precomputed rotations |
| `ParticleRenderer::add`, `cardVertex` | `asMeshCardInfo::Draw` | verified | rotation & 31, frame f at column f % W, row f / W |
| `shadowColor` | `asMeshCardInfo::DrawShadows` | verified | halved with red and blue swapped |
| `ParticleRenderer::draw` | `asMeshCardInfo::DrawShadows`, `Draw` | verified | shadows first, flag 0x10, above the Height plane by 1 cm |
| `ParticleRenderer::flush`, `begin`, `CardStyle` | lvlLevel's late callbacks after `cityLevel::DrawRooms`' glow pass | fixed | no fog (was fogged), alpha blended, default alpha test, no depth writes |

## src/game/fx/LineSparks.h, LineSparks.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `SparkLut::builtin`, `load` | `asSparkLut` | verified | 8×8 ramps of spark.tga, alpha 0x80 |
| `LineSparks::radialBlast` | `asLineSparks::RadialBlast` | fixed | t = normal × Y (× X near vertical), b = t × normal (both were negated) |
| `LineSparks::update`, `step` | `asLineSparks::Update` | verified | steps of at least 1/30 s, gravity 20, 80% bounce, age −650/s |
| `LineSparks::draw` | `asLineSparks::Draw` | fixed | vertex colour only, no fog |
| `argbToRgba` | — | openmm2 | |

## src/game/fx/Shards.h, Shards.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Shards::reset`, `live` | `fxShardManager::Reset` | verified | |
| `Shards::emit`, `emitOne` | `fxShardManager::EmitShards`, `Emit` | verified | above impact 500 and 5 m/s, impact / 300 (at most 2), ring of 16 |
| `Shards::update` | `fxShardManager::Update` | verified | gravity 20, 1.8 s |
| `Shards::materialFor` | `fxShardManager::Draw` | fixed | the material index restarts at 0 after 16 / count shards |
| `Shards::draw` | `fxShardManager::Draw` | fixed | alpha test reference 1, alpha blended, no depth writes, culling off, the texture's own sampler, no fog |

## src/game/fx/SkidMarks.h, SkidMarks.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `SkidTrack::SkidTrack`, `setWidth`, `reset`, `push` | `lvlTrackManager::Init`, `Reset` | verified | 64 pairs |
| `SkidTrack::update` | `lvlTrackManager::Update`, `vehCar::UpdateTrack` | verified | 10 cm start, 0.99 direction test, 10 m limit, v in tyre widths |
| `SkidRenderer::draw` | `vehCar::DrawTracks`, `lvlTrackManager::Draw` | fixed | the default culling (tracks laid in reverse face down and are culled; inferred from the cull state and pair order). Depth: deviation, see below |
| `SkidRenderer::draw` depth | `vehCar::DrawTracks` | deviation | MM2 turns the depth test off and relies on drawing right after the static city; OpenMM2 keeps the depth test with a bias because its draw order differs |

## src/game/fx/VehicleEffects.h, VehicleEffects.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `VehicleFxSetup::engineSmokeDefaults` | `vehCarDamage::Init` | verified | |
| `VehicleEffects::engineSmokeRule`, constructor | `vehCarDamage::vehCarDamage`, `Init`, `FileIO` | fixed | one shared EngineSmokeRule: each car resets it and loads its file over it, so all cars use the last loaded car's (was one per car); in rain the wheel smoke rule is a copy of splash (`mmGame::InitWeather`) |
| `VehicleEffects::reset` | `vehCar::Reset`, `vehWheelPtx::Reset`, `vehCarDamage::ClearDamage` | fixed | tracks, wheel fractions, smoke and its fraction and frame, impacts; particles, sparks and shards in flight carry on |
| `VehicleEffects::impact` | `vehCarDamage::ApplyImpact` | fixed | sparks above 15 mph from the running total (was the latest value); shards; first damage point; impact list |
| `VehicleEffects::takeImpacts`, `takeDamagePoint`, `update` | — | openmm2 | hand-off to the session; the fixed-step driver |
| `VehicleEffects::blastWheel` | `vehWheelPtx::UpdateWheel`, `Blast` | verified | load factor, position at the tread's leaving edge, velocity in the contact frame, InitialBlast per second |
| `VehicleEffects::spewSmoke` | `vehCarDamage::SpewSmoke` | verified | the rule's velocity turned by the car, position the offset through the car matrix |
| `VehicleEffects::step` | `vehCar::Update`, `vehWheelPtx::Update`, `vehCarDamage::Update` | verified | four tracks, both ptx slots, smoke level ceil(4 × fraction) with frames 1, 0, 3, 2, exhaust puffs above 2000 rpm |
| `VehicleEffects::draw` | `vehCar::DrawTracks`, `asParticles::Draw`, `asLineSparks::Draw`, `fxShardManager::Draw` | verified | |

## src/game/fx/Weather.h, Weather.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Weather::Weather` | `mmGame::InitWeather` | verified | 200 particles of tune/rain |
| `Weather::update` (rain) | `cityLevel::DrawRooms` | verified | born at camera-space (0, 10, −10) |
| `Weather::update` (snow) | — | openmm2 | OpenMM2's snow option |
| `Weather::draw` | `asParticles::SetTexture` → `gfxGetTexture` | fixed | ptx_rain without mipmaps |
| rain under landmarks | `cityLevel::DrawRooms` | open | see Missing (session) |

## src/game/fx/Random.h

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Rand::irand`, `frand` | `irand`, `frand` | verified | seed × 214013 + 2531011, 15 bits; × 2⁻¹⁵ |
| one generator per subsystem | `gRandSeed`, `DisableGlobalSeed`/`EnableGlobalSeed` | deviation | MM2 shares one global seed (effects switch to a secondary one); OpenMM2 seeds each subsystem so simulations stay deterministic independently |

## src/game/fx/LensFlares.h, LensFlares.cpp (new, second pass)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `LensFlare::LensFlare` | `ltLensFlare::ltLensFlare`, `ltFlare::ltFlare`, `ltFlare::Random` | fixed | random colours (frand + 1) / 2, k = (1.5 frand)^2 along the centre line (sign random), brightness min(1, 0.25 / k), half size 0.1 sqrt(k), reach 1.5 + 0.5 frand; flare 0 on the light (0.3), flare 1 mirrored (0.25) |
| `LensFlare::draw` | `ltLensFlare::Draw` | fixed | drawn when intensity x 0.5 reaches 0.05 and the light is within twice the half height of the centre (fading from half of it); colour light x flare x min(1, intensity x brightness). OpenMM2 keeps the cards square at any aspect (MM2's flare viewport is a fixed 4:3 ortho; deviation only away from 4:3) |
| `spotIntensity` | `ltLight::ComputeIntensity` | fixed | 25 / d^2 x cos^3, nothing behind the beam, less the threshold, at least 0 |
| `drawLensFlares` | `ltLensFlare::DrawBegin`, `DrawEnd` | fixed | added, unlit, no depth, texture lt_flare; the mirror view draws none (MM2 would draw its flares at full-screen positions) |
| `Rand` for the flares | MM2's global frand | deviation | as the other effects |

## src/app/RaceScreen.cpp hooks (session's file; second pass)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `drawLevel` | `lvlLevel::Draw` (cityLevel::Draw) | openmm2 | the world drawing moved out of `drawScene` so the main view and the mirror share it |
| `drawMirror` | `mmMirror::Cull`, `mmMirror::Reset` | fixed | the inset (RearViewMirror::viewport) cleared to black, the level from the mirror frame on the player's car, `Perspective(Fov, Aspect 2, NearClip, FarClip)`, winding swapped, the player's car hidden (its trailer stays). The on/off switch and the profile flag are the session's |
| `drawScene`: letterbox | `mmPlayer::SetWideFOV` | fixed | in wide-angle mode the scene clears black and the level draws in the band trunc(0.18 h) down, trunc(0.66 h) tall, at that band's aspect |
| `drawScene`: lens flares | `vehSiren::Draw` | fixed | flares queued while the level draws, added over it after |
| Texture Quality, Cloud Shadows | `mmGame::SetLevelGraphics`, `cityLevel::Load` | fixed | the [Graphics] TextureQuality limit around CityRenderer's construction; CloudShadows into EnvironmentOptions |

## src/render/Types.h, Types.cpp (M)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `PipelineState::key`, the Device types | — | openmm2 | Vulkan/OpenGL pipeline plumbing |
| `BlendMode` | `SetBlendSet` | verified | Alpha = set 0 (SRCALPHA/INVSRCALPHA), Additive = set 2 (SRCALPHA/ONE), Add = set 7 (ONE/ONE); Opaque, Modulate and Premultiplied serve OpenMM2's compositing |
| `CompareOp` default LessEqual | `gfxRenderState` ZFUNC | verified | |
| `CullMode`, `FrontFace` | `gfxRenderState` cull (offset 0x1b, default clockwise from `rglOpenPipe`) | verified | counter-clockwise front faces with back culling reproduce Direct3D's clockwise culling |
| `Device::setFrontFaceFlipped`, `effectiveState` (Device.h, both backends) | `mmMirror::Cull`'s swapped cull mode | fixed | second pass: swaps every draw's winding while a mirrored view draws |
| `DrawConstants::emissive`, `GpuDrawConstants` (112 bytes) | Direct3D material emissive | fixed | second pass |

## src/render/shaders (M)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `mesh.vert`: lighting | Direct3D 7 fixed-function lighting | inferred | ambient + Σ max(0, N · −L) × colour, clamped per vertex (DirectX behaviour, outside midtown2.exe) |
| `mesh.vert`: environment map | `modShader::BeginEnvMap` (camera-space normals back to world space) | fixed | u = 0.5 + 0.5 x, v = 0.5 − 0.5 y of the world-space normal |
| `mesh.vert`: fog | `gfxRenderState` fog vertex mode (offset 0x25, linear) | inferred | linear per-vertex fog by view depth (Direct3D's vertex fog) |
| `mesh.frag`: alpha test, texture stages | `gfxRenderState::DoFlush` (ALPHAREF/ALPHAFUNC, texture stage modulate) | inferred | discard when alpha is below the reference: GREATER 100 as at least 101/255, the default NOTEQUAL 0 as at least 1/255; stages modulate as Direct3D's do |
| `composite.*`, `overlay.*`, `prelude_*.glsl` | — | openmm2 | presentation, 2D overlay and backend preludes |

## src/app/IntroScreen.h, IntroScreen.cpp (M)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `IntroScreen` constructor, `decodeLoop`, `PcmStream` | `ebolaPlayMovie` (MCI playback of LOGOS.AVI) | openmm2 | OpenMM2 decodes the AVI itself (the Indeo codec is not part of midtown2.exe) |
| `IntroScreen::drawOverlay` | `ebolaPlayMovie` | fixed | the movie at its own size centred in the 640 × 480 screen (was stretched) |
| `IntroScreen::update`, `skipPressed` | `ebolaPlayMovie` | fixed | Esc, Space or the left button skip it, polled every 250 ms (was any key or button); paused while the window is inactive |
| playing in a window | `ebolaPlayMovie` (skipped with -nomovie or in a window) | deviation | OpenMM2 plays the intro in windowed mode too |
| `IntroScreen::finish` | — | openmm2 | screen hand-off |

## Missing

| MM2 | What it does | Status |
| --- | --- | --- |
| `lvlLevel::LoadInstances` xrefs | each PKG xref of an instance becomes an unhit banger (`dgUnhitBangerInstance::RequestBanger`) at the xref matrix times the instance's (rows renormalised outside 0.97–1.03, rejected when a row is zero or not orthogonal within 0.01), placed in the room FindRoomId gives | open for camera-props (BangerSet): 33 retail models have xrefs (tower lights, trees, awnings, doors); CityRenderer draws them as static children until BangerSet places them |
| `aiVehicleInstance::DrawGlow` (SLIGHT0/1) | traffic turn signals blinking on a frame counter | open for ai-vehicles (the turn state) |
| `cityLevel::FindRoomId`, `FullProbe`, `IsInRoomCheckWarps` | room search from a hint, its neighbours and warp links, with per-room height ranges | CityRenderer passes the hint on integration; `CityLevel::findRoom` still uses RoomLocator's search |
| `cityLevel::Draw` without a PVS | flood fill from the camera's room through neighbours and warps | deviation: OpenMM2 tests every room's sphere (debug path) |
| `gfxTexture::sm_LOD` | Direct3D texture-manager residency limit per street level | not ported: no lasting visual effect |
| `lvlFixedAny` `mask` / `refl` / `nonrandom` / `opaque` parts, `DrawReflectedParts` | extra parts drawn with the model, in the reflected-parts pass, and widening the radius | no retail city model has them (scanned) |
| Rain under landmarks | a 100 m probe upwards in landmark rooms hides the rain | open for session |
