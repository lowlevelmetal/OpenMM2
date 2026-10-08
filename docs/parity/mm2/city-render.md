# MM2 -> OpenMM2: city-render

Audited from MM2Recomp (midtown2.exe build 3393) on 2026-10-08.

Summary: 354 reachable functions in 44 classes (the free functions counted
as one; the five infrastructure render classes included); ported 233 (of
which newly ported 2 and fixed 11), replaced 68, not needed 53, open 0. One
behaviour of a ported function stays open (the race loading bar, handed to
game-flow; see Open). Two functions of
world-objects' classes that `lvlLevel::LoadInstances` depends on were
ported here as well (`lvlFixedAny::SetVariant`, `lvlFixedMatrix::IsVisible`).

Scope: the level and how the city, its objects and its pedestrians are
loaded and drawn: `cityLevel` (its `Load`, the room visibility of `Draw` and
the passes of `DrawRooms`), `lvlLevel`, `lvlInstance`,
`lvlMultiRoomInstance`, `lvlMaterial` / `lvlMaterialMgr`, `lvlSDL`,
`sdlPage16` and its `psdl_*` cases, the `mod*` model classes, the `gfx*`
texture and image classes, the city lighting tables, `lvlProgress`, the
pedestrian animation (`cr*`, `pedAnimation`, `pedAnimationInstance`) and,
from the infrastructure list, `dgTreeRenderer`, `ltFlare`, `gfxMaterial`,
`gfxLight` and `asMeshCardInfo`. The first audit checked the OpenMM2 side of
this code (records `rendering-fx.md`, `ai-ambient-city.md`, `formats.md`);
this pass starts from MM2's functions. The per-object classes the passes
call (`lvlFixedAny`, `lvlLandmark`, `lvlSky`, `dgBangerInstance`,
`dgRoadDecalInstance`, `vehCarModel`, the AI instances) belong to other
subsystems; where a fix here depends on one of them it is said so.

## What cityLevel::Load spawns, in order

`cityLevel::Load` (with `lvlLevel::LoadInstances` and the helpers it calls)
builds the level in this order; the right-hand column is where OpenMM2 does
the same.

| Step (MM2) | OpenMM2 |
| --- | --- |
| `city/materials.mtl` into `lvlMaterialMgr` | `CityLevel` constructor |
| `lvlSDL::LoadBinary` (city/<map>.psdl), `InitFullProbe(64, 64)` | `city::parsePsdl`, `RoomLocator` |
| city/<map>.cpvs (`sm_EnablePVS` when it loads) | `city::parseCpvs`; `CityRenderer::draw` |
| progress 10 % | open (race loading bar, see lvlProgress) |
| time and weather index (time × 4 + weather), night material factor 0.5 when the time is past 2, cloud map `shadmap_day` / `shadmap_nite`, env map `refl_dc`, `lvlSky::AutoInit`, `LoadCityTimeWeatherLighting`, `sdlCommon::UpdateLighting` | `makeEnvironment`, `TextureLibrary`, `VehicleRenderer::drawReflection`, `city::loadCity` |
| room records: `sdlPage16::ComputeBoundSphere`, the colour from city/<map>.lmap or white, the game's room flags, the subterranean height spans, city/<map>.water | `CityRenderer` constructor, `city::levelRoomFlags`, `RoomLocator`, `parseWater` |
| per-room lighting switched off without a lightmap (every retail city) | not ported: no retail city has a valid lightmap |
| progress 20 %; the warp list and London's four hand warps | `RoomLocator` |
| progress 30 %; `cityPropulator` street props (proprules / propdefs, rules n%02dleft / n%02dright per road) | `bangers::placeStreetProps` (props-fx) |
| progress 40 %; `LoadInstances(city/<map>.inst)` then `(city/<map>_ai.inst)` with the shader-set limit of the game state (99) | `CityRenderer`, `CityLevel`, `bangers::placeCityProps` |
| progress 50 %; `LoadPathSet(city/<map>, props)` | `bangers::placeCityProps` |
| progress 70 %; road decals from city/<map>/decals.pathset (paths of at least 3 points, in the room of the midpoint of their first and third point) | `bangers::RoadDecals` (props-fx) |
| the default cull mode, `vglSetFormat` | render backends |
| weather 3: the rain (tune/rain.asBirthRule, 200 particles of ptx_rain) | `fx::Weather` |
| `LoadPathSet(race/<map>, <mode name><race>)` | `bangers::placeCityProps` (race props) |
| texture size limit restored, `dgGlassInstance::InitStaticSystems`, progress 100 % | `TextureLibrary::setSizeLimit`; glass: world-objects |

What `LoadInstances` makes of a record (header: room in the low 16 bits,
flags in the high 16, the flags' low byte the variant): a banger (0x200,
`dgUnhitBangerInstance::RequestBanger`), an `lvlLandmark` (0x100, terrain
local), a collidable `lvlFixedMatrix` placed by `lvlMultiRoomInstance::Create`
(0x2000), or a plain `lvlFixedMatrix` (instance flags 0x600: visible and
static). Every one gets `SetVariant(low byte)`, then the geometry's xrefs
become bangers in the room `FindRoomId` gives, with the same variant.
`MoveToRoom` puts a static object in front of the room's earlier static
objects and a dynamic one at the head of the list.

## What cityLevel::DrawRooms draws, in order

1. `cityLevel_drawSDL`: the listed rooms' streets in one batch at the level
   of detail of the room's distance (`CityRenderer::draw`, `drawStreets`).
2. The reflected parts of the static objects (`DrawReflectedParts`, lights
   off): no retail city model has a `refl` part (rendering-fx scanned them).
3. `cityLevel_drawStatics`: the static objects, rooms from the last listed
   to the first, each room's statics newest first, `IsVisible` without a
   NoDraw limit (`CityRenderer::draw`, `drawInstance`).
4. `cityLevel_drawShadows`: the layer-0 drawables (`vehCar::DrawTracks`, the
   skid marks), then `DrawShadow` of the instances flagged 0x2000 and then
   0x40 in rooms whose distance is under NoDraw, with the depth range
   squeezed to 0.001..0.999 and no depth writes. City statics have no
   `shadow` part in retail (scanned: every .inst model of both cities), so
   the pass draws road decals and car, trailer and traffic shadows, which
   OpenMM2 draws with each object (`RoadDecals`, `VehicleRenderer`,
   `AiRenderer`; depth bias instead of the depth range).
5. `cityLevel_drawObjects`: the dynamic objects (flag 0x200) of rooms within
   NoDraw, with `IsVisible`'s NoDraw limit; the trees the bangers queue are
   drawn after them (`dgTreeRenderer`, `BangerSet::draw`).
6. `cityLevel_drawLights`: added, unfogged `DrawGlow` of every instance of
   the rooms within NoDraw that hold a glowing one (car lights, signals,
   lamp posts).
7. The layer-1 drawables (smoke, sparks, shards: `VehicleEffects`) and the
   rain, unless the camera's room is subterranean (flags 0x0A) or a landmark
   room with something within 100 m above (`RaceScreen::rainVisible`).

OpenMM2 draws passes 4 to 7 per object kind rather than per room, but with
MM2's room gates (`RoomVisibility`, second pass): `CityRenderer::draw`
records the rooms it lists for the view with their distances, and the cars
(`VehicleRenderer`), traffic, pedestrians and signals (`AiRenderer`) and
props (`BangerSet`) are drawn only from a listed room, the object itself when
the room's distance is at most NoDraw and it passes `IsVisible`, its shadow
and glows when the distance is under NoDraw whatever `IsVisible` says (so a
lamp post's glow or a car's lights can show beyond the object's own NoDraw,
as in MM2). Each object keeps its room as MM2 does: `FindRoomId` of its
position from its last room (`vehCar::Update`, `vehTrailer::Update`,
`mmNetObject::Update`, `aiPedestrian::Update`, the ambient cars after their
spline update and `aiVehicleActive::Update`; the signals once,
`aiTrafficLightSet::SetFourWay`), or the room `BangerSet` keeps for a prop.
An object outside every room is in room 0, which no view lists. The order of
the objects within the passes stays per kind (MM2 goes room by room; it
matters only where translucent objects overlap). Road decals and skid marks
are not gated: decals are static instances and the skid marks a drawable of
the shadow pass.

## cityLevel

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `cityLevel::Load` | ported | `city::loadCity`, `CityRenderer`, `CityLevel`, `RoomLocator`, `bangers::placeCityProps`, `RoadDecals`, `fx::Weather`, `RaceScreen::load` | every step in the table above; the instances of city/<map>_ai.inst now reach `CityRenderer` too (retail: stop signs, all bangers) |
| `cityLevel::LoadPathSet`, `LoadPath`, `LoadProp` | ported | `bangers::placePathSet`, `placeCityProps` | `dgPath::Enumerate` hands each path's props to `LoadPath`, which places them as unhit bangers in the room `FindRoomId` gives (camera-props / props-fx port) |
| `cityLevel::AddWarp` | ported | `RoomLocator::RoomLocator` | a warp is added once per room pair, at the head of the room's list |
| `cityLevel::InitFullProbe`, `FullProbe`, `FindRoomId`, `IsInRoomCheckWarps` | ported | `RoomLocator` | verified by ai-ambient-city |
| `cityLevel::DecompressPvs` | ported | `city::parseCpvs` | |
| `cityLevel::Draw` | ported (fixed) | `CityRenderer::draw` | camera room from the last one, PVS rooms in id order tested against the view; the list now stops at 512 rooms (`kCityMaxDrawnRooms`, the size of MM2's room record array). The room lookup uses the camera position plus the camera's third row times the near distance (OpenMM2 the camera position: a difference of at most the near distance, recorded by rendering-fx) |
| `cityLevel::DrawRooms` | ported | `CityRenderer::draw`, `RaceScreen::drawLevel` | the passes above; the sky is drawn when the draw mask is all ones, which both callers (`mmGameManager::Cull`, `mmMirror::Cull`) pass |
| `cityLevel_drawSDL` | ported | `CityRenderer::gatherStreets`, `drawStreets` | `gfxTexture::sm_LOD` is set per room (texture residency, replaced) |
| `cityLevel_drawStatics` | ported (fixed) | `CityRenderer::draw`, `drawInstance` | rooms from the last listed one; within a room the statics now newest first, as `lvlLevel::MoveToRoom` links them (were oldest first) |
| `cityLevel_drawShadows` | ported (fixed) | `RoadDecals::draw`, `VehicleRenderer::draw` (`drawShadow`), `AiRenderer`, `SkidMarks` | per object kind, gated by the object's room under NoDraw (`RoomVisibility::Passes::shadowsAndGlows`; was drawn with the visible car only); city statics have no shadow geometry in retail |
| `cityLevel_drawObjects` | ported (fixed) | `AiRenderer::draw`, `BangerSet::draw`, `VehicleRenderer::draw`, `RaceScreen::drawLevel`, `RoomVisibility` | dynamic objects from a listed room at most NoDraw away, through `IsVisible` (were culled by the view only) |
| `cityLevel_drawLights` | ported (fixed) | `VehicleRenderer::draw` (`drawGlows`), `AiRenderer::drawSignal`, `BangerSet::draw` | added, unfogged, per object, by the object's room under NoDraw whatever `IsVisible` says (car and prop glows were drawn with the visible object only, the signals' by their own distance) |
| `cityLevel::SetupLighting` | ported | `makeEnvironment` | verified by rendering-fx |
| `cityLevel::SetupPerRoomLighting` | not needed | — | runs only with per-room lighting, which `Load` switches off when the lightmap is missing or rejected (every retail city's room count differs) |
| `cityLevel::SetObjectDetail` | ported | `ObjectDetail::forLevel` | |
| `cityLevel::GetEnvMap` | ported | `VehicleRenderer::drawReflection` | refl_dc, intensity 1, with the Vehicle Reflections option |
| `cityLevel::EnableSky` | ported (new) | `EnvironmentOptions::texturedSky`, `CityRenderer::draw` | `mmGame::SetLevelGraphics` passes the TEXTURED SKY option; off, `lvlSky::Draw` draws nothing and the fog-coloured clear shows. RaceScreen read the option nowhere |
| `cityLevel::GetWaterLevel` | ported | `RaceScreen` water level | the .water height for every room |
| `cityLevel::GetLightingIntensity` | ported | — | always 1; `vehCar::PostUpdate` stores it in the car model (+0x4c) every frame, so OpenMM2's car drawing, which has no such factor, matches |
| `cityLevel::SetPtxHeight` | not needed | — | empty |
| `cityLevel::Update` | ported | `CityRenderer::update` | `lvlSky::Update` and the 1/128 cloud offset |
| `cityLevel::PreDraw` | ported | `fx::Weather::update`, `TextureLibrary::update` | the rain's update and `gfxTextureMovie::UpdateAll` |
| `cityLevel::PostDraw` | not needed | — | empty |
| `cityLevel::GetBoundSphere` | ported | `CityRenderer` room spheres | |
| `cityLevel::GetBound` | ported | `CityLevel` | the level bound is the SDL |
| `cityLevel::GetNeighborCount`, `GetNeighbors` | ported | `CityLevel::neighbors`, `aiMap` routing | |
| `cityLevel::GetTouchedNeighbors`, `GetTouchedNeighborsR` | ported | `cityTouchedNeighbors` | |
| `cityLevel::GetRoomPerimeter`, `GetVisitList`, `Collide` | not needed | — | no caller: no virtual call reaches these slots of the level (searched every call through `lvlLevel::sm_Singleton`); `Collide` returns false |
| `cityLevel::cityLevel`, `~cityLevel`, scalar deleting destructor | replaced | `CityRenderer`, `CityLevel` lifetimes | the constructor sets up the lighting with the default table |

## lvlLevel

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `lvlLevel::LoadInstances` | ported (fixed) | `CityRenderer` constructor, `CityLevel`, `bangers::placeCityProps`, `placeXrefs` | the record kinds above (`staticKind`); the variant byte now picks the static object's shader set (`staticVariant`, see lvlFixedAny::SetVariant below): SF's 2657 storefront facades with a nonzero variant all drew shader set 0 |
| `lvlLevel::MoveToRoom` | ported (fixed) | `CityRenderer` room lists, `CityLevel::instances` | movable instances at the head of a room's list, statics after them, each before the earlier ones. Both drew and gathered the statics oldest first, and `CityLevel` listed them before the movable sources (props, traffic bodies): the collision gather (`World`'s instance list, capped) now takes movers first, then statics newest first, as `dgPhysManager` walks MM2's lists. The sources' order among themselves stays per source (MM2 interleaves them by the time each moved in) |
| `lvlLevel::ResetInstances` | ported (fixed) | `RaceScreen` restart, `BangerSet::reset` | `mmGame::Reset` starts with it: every instance's `Reset` (a no-op for statics, the placed props back up) and the banger managers'. RaceScreen's restart never called `BangerSet::reset`, so knocked-over props stayed down |
| `lvlLevel::RegisterDrawable`, `CallCallbacks`, `ResetCallbacks`, `Update` | ported | `RaceScreen::drawLevel` order | layer 0 (skid marks) with the shadows, layer 1 (effects) after the glows; at most 63 per layer |
| `lvlLevel::ClampToWorld`, `Collide`, `GetBoundSphere`, `GetVisitList`, `GetEnvMap`, `SetObjectDetail` | not needed | — | base-class defaults cityLevel overrides or nothing calls |
| `lvlLevel::lvlLevel`, `~lvlLevel`, scalar deleting destructor | replaced | — | create the material manager and temporary bounds; free the shaders |

## lvlInstance

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `lvlInstance::BeginGeom`, `AddGeom`, `EndGeom`, `GetGeomSet` | ported | `ModelLibrary::get`, `findFilledLod`, `geomRadius`, `placeXrefs` | one geometry set per model name; LOD fill VL → L → M → H; the xrefs table |
| `lvlInstance::AddSphere` | ported | `AiRenderer` | a geometry entry with only a radius: the pedestrians' 2 m |
| `lvlInstance::IsVisible` | ported | `objectLod` | |
| `lvlInstance::GetRadius`, `GetBoundSphere`, `GetBound` | ported | `geomRadius`, `CityLevel` | |
| `lvlInstance::DrawPhysics` | ported | `VehicleRenderer::shadowMatrix` | the ground probe below the object for its shadow |
| `lvlInstance::InitBoundTerrainLocal` | ported | `CityLevel` | |
| `lvlInstance::AttachEntity`, `Detach`, `GetEntity`, `GetVelocity` | ported | `phys::Instance` | physics; verified by phys-core |
| `lvlInstance::SetVariant` | ported | — | the base class ignores the variant; see lvlFixedAny::SetVariant |
| `lvlInstance::DrawShadow`, `DrawGlow`, `DrawReflected`, `DrawReflectedParts`, `Init`, `Reset`, `IsLandmark`, `IsCollidable`, `IsTerrainCollidable` | ported | — | base-class defaults (nothing, false, 0) |
| `lvlInstance::DrawShadowMap` | not needed | — | `DrawRooms` never calls it and `SetShadowBillboardMtx`, which sets its matrix, has no caller |
| `lvlInstance::GetNumLightSources`, `GetLightInfo`, `SetupGfxLights` | not needed | — | no instance class overrides them: no city object carries lights |
| `lvlInstance::CreateTempBounds`, `DeleteTempBounds` | replaced | `phys` bounds | shared scratch bounds |
| `lvlInstance::Optimize`, `PreLoadShader` | replaced | `ModelLibrary` GPU upload | Direct3D packet and texture preparation |
| `lvlInstance::ResetAll`, `ResetInstanceHeap`, `operator new`, `operator delete`, constructor, destructor | replaced | C++ containers | the 2.5 MB instance arena |
| `lvlFixedAny::SetVariant` (world-objects' class) | ported (new) | `CityRenderer::drawModel` | the variant modulo the geometry's shader sets for every static object; the cloud pass keeps set 0 (`DrawOrthoMapped` is handed the first set). Done here because `LoadInstances` is this subsystem's |
| `lvlFixedMatrix::IsVisible` (world-objects' class) | ported (new) | `fixedObjectFacesAway`, `CityRenderer::drawInstance` | in physics mode a static object without instance flag 0x100 (neither a landmark nor collidable) is skipped while (eye − origin) · Z < 0 in the ground plane. `lvlFixedRotY::IsVisible` has the same test but only landmarks use it, and they are exempt |

## lvlMultiRoomInstance

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `lvlMultiRoomInstance::Create` | ported | `CityRenderer` constructor, `CityLevel` | stand-ins in the touched neighbours from the last to the first, the object in room 0; a scaled matrix kept for drawing and normalised for collision |
| `lvlMultiRoomInstance::Draw` | ported | `CityRenderer::drawInstance` | once per frame |
| `lvlMultiRoomInstance::IsCollidable`, `IsTerrainCollidable` | ported | `CityLevel` stand-ins | |
| `lvlMultiRoomInstance::GetPosition`, `GetMatrix`, `SetMatrix`, `GetEntity`, `AttachEntity`, `GetVelocity`, `GetBound`, `IsLandmark`, `SizeOf`, constructor | ported | `CityLevel` stand-ins, `CityRenderer` | forward to the object; the stand-in copies its flags, variant and geometry |

## lvlMaterial, lvlMaterialMgr

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `lvlMaterial::lvlMaterial`, `Load` | ported | `phys::parseMaterials`, `CityLevel` | verified by ai-ambient-city / phys-core |
| `lvlMaterial::Copy` | ported | `CityLevel` materials | |
| `lvlMaterial::Save` | not needed | — | editor |
| `lvlMaterialMgr::lvlMaterialMgr`, `Load`, `Find`, `Lookup` | ported | `CityLevel`, `city::sdlMaterialIndex` | |
| `lvlMaterialMgr::CreateInstance`, `DeleteInstance`, `GetInstance` | replaced | `CityLevel` ownership | |

## lvlSDL, sdlPage16, sdlCommon and the psdl cases

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `lvlSDL::LoadBinary` | ported | `city::parsePsdl`, `sdlTextureMaterials`, `sdlTextureName` | |
| `lvlSDL::Enumerate`, `IsoLerp`, `Propulate` | ported | `bangers::placeStreetProps` | props-fx |
| `sdlPage16::LoadBinary`, constructor, `GetCodedVertex`, `GetFloat`, `GetPerimeterCount`, `GetPerimeterVertexIndex` | ported | `city::parsePsdl`, `decodePsdlAttributes`, `SdlDraw` `Builder::at` / `height` | |
| `sdlPage16::Draw` | ported | `city::buildSdlRoomDraw` | rendering-fx's port; the dispatch below |
| `psdl_draw_road`, `psdl_draw_road_L`, `psdl_draw_road_R`, `psdl_draw_road_low`, `psdl_draw_road_verylow` | ported | `Builder::roadStrip`, `road`, `lowDetailRoad` | type 0 |
| `psdl_draw_sidewalk_L`, `_R`, `_L_shaded`, `_R_shaded` | ported | `Builder::sidewalkSide` | levels 2 / 3 of roads and divided roads |
| `psdl_draw_intersection_sidewalks` | ported | `Builder::sidewalkStrip` | type 1 |
| `psdl_draw_alleys` | ported | `Builder::rectangleStrip` | type 2 |
| `psdl_draw_slivers_thing` | ported | `Builder::sliver` | type 3 |
| `psdl_draw_intersection_crosswalks` | ported | `Builder::crosswalk` | type 4 |
| `psdl_draw_intersection_surfaces`, `psdl_draw_terrain_thing` | ported | `Builder::fan` | types 5 (road fan) and 6 |
| `psdl_set_shade_color` | ported | `Builder::build` (FacadeBound) | type 7: the light index |
| `psdl_draw_median_road` | ported | `Builder::dividedRoadStrip`, `divider` | type 8 |
| `psdl_draw_tunnel_junction` | ported | `Builder::tunnel`, `junctionTunnel`, `stripTunnel` | type 9 |
| `psdl_set_texture` | ported | `Builder::texture` | type 10 |
| `psdl_draw_buildings_thing` | ported | `Builder::facade` | type 11 |
| `psdl_draw_roofs` | ported | `Builder::roof` | type 12 |
| `psdl_invalid_cmd` | replaced | `Builder::build` | MM2 quits on types 13–15; OpenMM2 skips them (malformed data only) |
| `sdlPage16::ArcMap`, `WallMap`, `GetCentroid`, `ComputeBoundSphere`, `GetTexture`, `PointInPerimeter` | ported | `sdlArcMap`, `sdlWallMap`, `sdlRoomCentroid`, `sdlRoomBoundSphere`, `Builder::tex`, `RoomLocator::pointInPerimeter` | |
| `sdlPage16::GetShadedColor` (both) | ported | `CityRenderer::setEnvironment` | the light table entry times the room colour (white) |
| `sdlCommon::BACKFACE`, `UpdateLighting` | ported | `sdlBackface`, `makeEnvironment` | |

## Models: modStatic, modShader, modModel, modPackage

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `modStatic::Draw`, `DrawEnvMapped`, `DrawOrthoMapped` | ported | `drawGpuMesh`, `VehicleRenderer::drawReflection`, `CityRenderer::drawCloudShadow` | |
| `modStatic::GetTriCount`, `GetAdjunctCount` | ported | `asset::parsePkg` | |
| `modStatic::Optimize` | replaced | `ModelLibrary::add` | Direct3D vertex buffers |
| `modShader::Load`, `LoadShaderSet`, `BeginEnvMap` | ported | `asset::parsePkg`, `drawGpuMesh`, `VehicleRenderer::drawReflection` | the shader-set limit is 99 while the city loads and 10000 otherwise: no retail model reaches it |
| `modShader::EndEnvMap` | ported | `VehicleRenderer::drawReflection` | |
| `modShader::AddStaticMaterial`, `PreLoad`, `KillAll` | replaced | render backends, `ModelLibrary` lifetime | |
| `modModel::LoadBinary`, `LoadAscii`, `Draw` | ported | `asset::parsePedMesh`, `AiRenderer::drawPed` | |
| `modPackage::Open`, `OpenFile`, `NextItem`, `CloseFile`, `Close`, `Skip`, `SkipTo`, constructor, destructor | ported | `asset::parsePkg` | OpenMM2 parses every chunk up front (formats) |

## Textures and images: gfxTexture, gfxTextureMovie, gfxImage, gfxBitmap

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `gfxTexture::Load`, `Create` (both), `Clone`, `SetTexEnv` | ported | `TextureLibrary::load`, `TexelDamage` | verified by rendering-fx |
| `gfxTexture::Blit` | ported | `TexelDamage::reset` | |
| `gfxTexture::SetName`, constructor, destructor | replaced | `TextureLibrary` | |
| `gfxTexture::GetColor` | not needed | — | used only by the facade and sliver flat-colour branch of `sdlPage16::Draw`, behind a flag nothing sets |
| `gfxTexture::SetLODs` | not needed | — | Direct3D residency by street level (`sm_LOD`); no lasting visual effect |
| `gfxTexture::PreLoad`, `MarkFirstUse`, `MarkHigherUse`, `EnableCache`, `InitCache`, `ShutdownCache`, `FindEntry`; `gfxTextureCachePool` (`Init`, `AddSlot`, `FindEntry`, `IsCompatibleWith`, constructor, destructor); `gfxTextureCacheEntry` (`Lease`, `Evict`, constructor, destructor) | replaced | render backends | the Direct3D texture cache |
| `gfxTextureMovie::Update`, `UpdateAll`, `AddClient`, `KillAll`, constructor | ported | `TextureLibrary::get`, `update` | frames 1–128 at the .movie rate |
| `gfxPrepareImage`, `gfxImage::Create`, `GenerateMipmaps`, `Halve` | ported | `TextureLibrary::load`, `asset::Image` | |
| `gfxImage::GetDominantColor` | not needed | — | feeds `GetColor` only |
| `gfxImage::Scale`, `GetFont`, destructor | replaced | `ui` (frontend) | loading screens, menus and fonts |
| `gfxBitmap::Load`, `Create` (both), `Clear`, `SetName`, constructor, destructor | replaced | `ui::TextureCache` | 2D bitmaps (frontend) |
| `InstallTextureVariantHandler` | ported | `TextureLibrary::loadVariant`, `darkenImage` | |

## Lighting and immediate mode

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `LoadCityTimeWeatherLighting`, `cityTimeWeatherLighting::FileIO`, `ComputeAmbientLightLevels` | ported | `city::loadCity`, `parseLighting`, `ambientForQuality` | |
| `SetLightDirection` | ported | `lightDirection` | |
| `vglBegin`, `vglEnd`, `vglBindTexture`, `vglBeginBatch`, `vglEndBatch`, `vglSetCloudMap`, `vglSetOffset` | ported | `SdlDraw` `Builder`, `CityRenderer::drawStreets`, `TextureLibrary::cloudMap` | |
| `vglSetFormat`, `vglTexCoord2f`, `vglVertex3f` (both), `vgl_VERTEX_VCT1`, `vgl_VERTEX_VNT1` | replaced | `SdlDraw` vertices, render backends | immediate-mode vertex emission |
| `DrawColoredTri`, `draw_textured_tri` | ported | `session::Hud`, `fx::Shards` | |
| `gfxLight::Reset`, `gfxMaterial::Reset` (infrastructure) | replaced | `render::FrameConstants`, `DrawConstants` | Direct3D light and material structures |

## lvlProgress

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `lvlProgress::SetCallback`, `BeginTask`, `EndTask`, `UpdateTask` | ported | `frontend::LoadingPage` | the frontend's load (frontend-ui) |
| `lvlProgress::UpdateTask` during a race load | open | `RaceScreen` loading screen | `ProgressCB` draws the loading picture and a #0D2CBA bar at (349, 448), percent × 640 / 284 wide, 10 high, at each new percent: `mmGame::Init` 10, `cityLevel::Load` 10, 20, 30, 40, 50, 70, 100, then `aiMap::Init`'s. OpenMM2 loads a race in one frame and shows the picture alone; porting needs the load split into steps across frames |

## Pedestrian animation: cr*, pedAnimation, pedAnimationInstance

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `crAnimation::GetAnimation` | ported (fixed) | `asset::loadPedType`, `asset::normalizePedRoots` | loads an animation once and normalises it on that load |
| `crAnimation::Normalize` | ported (new) | `asset::normalizePedRoots` | frame i's root z gains i × cycle distance / frames (the loop's travel). OpenMM2 took the first-to-last drift out at draw time instead |
| `crAnimation::LoadAnim`, `crAnimFrame::LoadBin`, `Init`, `DeallocateBuffers`, default constructor, constructor, destructor | ported | `asset::parsePedAnimation` | |
| `crAnimation::Subtract`, `crAnimFrame::AddScaled` | not needed | — | `GetAnimation`'s only caller passes no frame to subtract |
| `crAnimation::InitHashTables`, `DeleteAnimTable`, `DeleteChanTable`, constructor, destructor, scalar deleting destructor | replaced | `PedType::animations`, `AiRenderer` cache | MM2 shares one copy per animation name across types (see the notes of `normalizePedRoots`) |
| `crSkeletonData::Load`, `FindBone`, `HowMany`, constructor, destructor; `crBoneData::Load`, `AddChild`, `Transform`, constructor; `crBone` constructor | ported | `asset::parseSkeleton`, `posePed` | |
| `crSkeletonData::InitMirror` | not needed | — | builds the table only `crAnimFrame::Mirror` (no caller) reads |
| `crSkeleton::Init`, `Update`, `Attach`, constructor | ported | `asset::posePed` | the bone-to-model matrices |
| `crKinematics`, `crKinematicsBase` (`Init`, `Reset`, `Update`, `MatchPose`, `SetIKBlend`, `SetLimp`, destructors), `crLegData` (`Init`, `SolveIK`, `SolveLimpIK`, `MatchPose`, `Acosf`), `crSpineData` (`Init`, `SolveIK`, `MatchPose`, destructor), `crBodyData` and `crBodyDataBase` destructors, `pedActive::IsAsleep`, `pedActiveData` destructor | not needed | — | the pedestrian ragdoll's inverse kinematics: only `pedActive` builds them, and `pedRagdollMgr`'s constructor and `Init` have no caller, so no ragdoll exists (reachable only through vtables) |
| `pedAnimation::Load` | ported (fixed) | `asset::parsePedAnimTable`, `asset::normalizePedRoots`, `Pedestrians::fwdSpeed`, `latSpeed` | each row's root x and z of frames 0..m lose frame 0's value and the straight line to frame m (m = last − first, at most frames − 1). OpenMM2 kept frame 0's root and removed the drift over the whole clip, so a pedestrian was drawn off its position by the clip's start: 2.2 m to the side in the dives, 3.1 m ahead in the run-to-walk, 0.28 m in the walk |
| `pedAnimation::Init`, `LookupSequence`, `DrawSkeleton` | ported | `AiRenderer::pedType` (the per-name type cache `Init` sets up), `PedAnimTable::find`, `AiRenderer::drawSkeleton` | |
| `pedAnimationInstance::Draw` | ported (fixed) | `AiRenderer::drawPed` | the current frame posed from the adjusted data; the draw-time drift subtraction is gone |
| `pedAnimationInstance::Load`, `Init`, `Reset` | ported | `AiRenderer::pedType`, `Pedestrians` | |
| `pedAnimationInstance::PreUpdate`, `Update`, `Start`, `VerifySeq` | ported | `Pedestrians::animate`, `startSeq` | 30 frames per second; forward, single-frame and (no retail row) backward sequences; MM2 quits on a sequence out of range, OpenMM2's table lookups guard |

## Infrastructure render classes

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `dgTreeRenderer::dgTreeRenderer`, `AddTree`, `RenderTrees` | ported | `BangerSet::draw` | trees after the other props, alpha reference 120 (camera-props); MM2's queue holds 200 without a check |
| `ltFlare::ltFlare`, `ltFlare::Random` | ported | `fx::LensFlare` | rendering-fx |
| `asMeshCardInfo::Init`, `Draw`, `DrawShadows` | ported | `fx::ParticleRenderer` | rendering-fx |
| `asMeshSetForm` (`SetShape`, `SetZRead`, `SetZWrite`, `EnableAlpha`, `EnableLighting`, `Cull`, `Update`, constructor, destructors) | ported | `session::Hud` | the HUD's 3D meshes: `mmArrow`, the map icons of `mmHudMap::Init`, `mmCRHUD::Init` |

## Open

| MM2 | What it does | What porting needs |
| --- | --- | --- |
| `lvlProgress::UpdateTask` during a race load | the loading bar on the race's loading screen | split `RaceScreen::load` into steps run over several frames, drawing the bar at MM2's percentages (handed to game-flow) |
