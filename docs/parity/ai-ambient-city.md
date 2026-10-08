# Parity audit: ai-ambient-city

Audited against MM2Recomp (midtown2.exe build 3393) on 2026-10-07.

Summary: 227 functions (named entries in the tables; a few constructors
appear in several rows by aspect); verified 118, fixed 50, deviation 13,
inferred 22, open 12, openmm2 12.

## Random (`src/ai/Random.h`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Random::irand` | `irand()` | fixed | Was an xorshift64*. MM2's irand is the MSVC rand() generator: seed = seed x 214013 + 2531011, bits 16..30. |
| `Random::frand` | `frand()` | fixed | Was 24-bit; MM2 is irand() x 3.0517578e-05, so probabilities come in 1/32768 steps. |
| `Random::Random`, `seed` | `ResetRandomSeed` | deviation | MM2 has one global seed (set to 1 by aiMap::Reset) shared by traffic, pedestrians, police, audio and effects; OpenMM2 gives each AI subsystem its own generator (default seed 1) so runs replay deterministically. Exact draw sequences cannot match MM2 anyway, since other subsystems draw from the same global there. |

## Pedestrians (`src/ai/Pedestrians.{h,cpp}`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `kPedAnimFps`, `kPedAwareRadius`, `kPedLookAhead`, `kPedTurnRate`, `kPedMaxLateral`, `kDefaultPedPool` | `pedAnimationInstance::PreUpdate` (30), `aiPedestrian::Update` (1225), `Wander` (6.0), steering (0.15), `CalcCurve` (1.5), `aiCityData` (100) | verified | Constants decoded from the data section. |
| `Pedestrians::Pedestrians` (sequence lookup) | `aiPedestrian::Init` | verified | Same sequence names; LDIVE/RDIVE are looked up by MM2 but absent from every retail table, so leaving them out changes nothing. |
| `Pedestrians::Pedestrians` (sidewalk geometry) | `aiPath::ReadBinary`, `aiPath::ReverseDirection`, `SidewalkVertice` | fixed | Lengths were recomputed in 3D for every road. MM2 uses the file's cumulative lengths, recomputed in 3D only on roads ReverseDirection reverses (drive on the left, two-way); it also needs every row's lengths (GetHeading reads lane 0's), now kept per side after the reversal (lanes swapped and reversed, sidewalk kept). Roads whose sides have different lane counts keep their rows (MM2 would overrun; inferred, none in retail). |
| `Pedestrians::Pedestrians` (pool, types, variants) | `aiMap::Init`, `aiPedestrian::Init`, `aiMap::Reset` | fixed | Count trunc(pool x density), type frand x count, variant trunc(frand x (variants - 1)) verified; the pool is now MM2's list (every pedestrian added in index order, so the last is taken first) instead of a vector stack with the same top. When no listed type loads OpenMM2 uses every type (deviation: MM2 would leave the pedestrians uninitialised). |
| `walk`, `sections`, `sv`, `cumAt`, `axisX`, `axisZ`, `axisW` | `aiPath::SidewalkVertice`, the path's axis arrays | verified | Index clamps are OpenMM2 guards that retail data never hits. |
| `subLength` | `aiPath::SidewalkSubSectionLength` | verified | Negative indices count as 0, as in MM2. |
| `sidewalkIndex` | `aiPath::Index(float,int,int)` | verified | Distance clamped to the row, first vertex within 1e-5. |
| `getHeading` | `aiPath::GetHeading` | fixed | New. MM2 finds the segment on the lengths of row `row` of side `dir` (Reset passes the pedestrian's sidewalk row, Anticipate and AvoidObstacle row 0, the first lane) and returns the heading of side `dir`'s sidewalk segment in direction `dir`; OpenMM2 used its own side's sidewalk always. Rows beyond the side's (MM2 reads past its arrays) use the sidewalk row. |
| `crossedNode` | `aiPedestrian::PreCrossStreet` / `WaitCrossStreet` / `CrossStreet` / `Accident` | fixed | MM2 derives the intersection being crossed from the previous road and direction each time; OpenMM2 stored the node picked at the corner, which went stale after a corner turn-back. |
| `frameCount` | `pedAnimation::Load` | verified | last - first + 1 frames, capped at the .anim's frame count; played from .anim frame 0. |
| `fwdSpeed`, `latSpeed` | `pedAnimation::Load` | verified | CSV distance over (unclamped frames x 0.03333). MM2 divides the CSV's atof double by the float duration; OpenMM2 divides the float-parsed distance (last-ulp differences at most). |
| `startSeq`, `queueSeq` | `pedAnimationInstance::Start`, the queued sequence byte | verified | |
| `pathAdd`, `pathRemove`, `moveToPath` | `aiPath::AddPedestrian`, `RemovePedestrian` | fixed | New: each road keeps its pedestrians in a list with the newest first, as MM2 does; OpenMM2 kept an append-order vector and did not move a pedestrian between roads on AvoidObstacle's corner turn-back. |
| `poolAdd`, `poolRemove` | `aiMap::AddPedestrian`, `RemovePedestrian` | fixed | New: the pool is MM2's list (prepend, remove by search). |
| `calcCurve` | `aiPedestrian::CalcCurve`, `ComputeCurve` | fixed | Cases (last section first, then section 1, interior, corner), offsets, tangents (SubSectionDir = w x len, IntersectionEntry/ExitVector = -z x len) and the corner length verified. The Hermite c0 is now summed in MM2's order ((2p0 + t1) + t0) - 2p1. The corner's zero-length guard is OpenMM2's. |
| `solvePosition` | `aiPedestrian::SolvePosition` | verified | Horner form, x and z only. |
| `solveTargetPoint` | `aiPedestrian::SolveTargetPoint` | verified | Target height = position height. |
| `roadDistance` | `aiPedestrian::RoadDistance` | fixed | Branches verified; the lateral offset taken at a vertex is now a 3D dot (MM2 includes y), and the corner direction is normalised with MM2's summation order. |
| `setNextRoad` | `aiPedestrian::SetNextRoad`, `GetRoadToRight`, `GetRoadToLeft` | fixed | MM2 starts from the road's index at the end it walks towards (by direction); OpenMM2 matched the node, which picks the wrong end on a loop road. MM2 also steps over shortcut roads (ids beyond the regular count); OpenMM2 loads none. |
| `pickNextRoad` | `aiPedestrian::PickNextRdSeg`, `UpcomingAccident` | fixed | Choice irand() % 3 only at sets with a pedestrian phase, 0 on an accident; turn rules and the closed/unpopulated turn-back verified. Now draws irand from MM2's generator. The "no intersection" guard is OpenMM2's. |
| `solveRoadSegment` | `aiPedestrian::SolveRoadSegment` | fixed | Conditions verified; now moves between road lists and keeps the road distance as MM2 does. |
| `steer` | `aiPedestrian::Wander`, `PreCrossStreet`, `WaitCrossStreet`, `CrossStreet` | fixed | The angle is measured in the matrix MM2 rebuilt at the end of the last update, not from the current heading (they differ when the heading changed earlier in the same update); z term summed first as in MM2. |
| `reset` | `aiPedestrian::Reset` | fixed | Distance, lateral offset, index, direction draw, position, curve, invLen, the 5 m ground probe, reaction/crossing resets, start frame and target verified. Heading now from GetHeading with MM2's row/side rule; a missed ground probe keeps the interpolated height (OpenMM2 took the sidewalk line's); the pedestrian joins its road's list. |
| `clearPeds` | `aiMap::ClearPeds` | fixed | Returns the road's pedestrians to the pool head first (newest first), as MM2; OpenMM2 pushed them oldest first. |
| `adjust` | `aiMap::AdjustPedestrians` | fixed | Removal/addition sets verified. MM2 deals from the pool's head, side -1 then side 1 per road, and stops when the pool is empty or the turn comes back to the road where it last placed one; OpenMM2 dealt full laps while anything was placed, so one open road among closed ones took the whole pool instead of one lap. The populated roads now form MM2's list (newest first). |
| `populateAll` | — | openmm2 | Test helper (every road). |
| `forwardCollision` | `aiPedestrian::DetectPlayerForwardCollision` | verified | Reverse gear (gear 0) flips the axes; neutral counts as forward; quarter length to 20 m, half width + 2 m. |
| `anticipateCollision` | `aiPedestrian::DetectPlayerAnticipate` | verified | Up to 35 m, half width + 4 m along -m0 whatever the gear. |
| `playerCollision` | `aiPedestrian::DetectPlayerCollision` | fixed | Uses the matrix of the last update (was the current heading). |
| `wallProbe` | the probe in `Anticipate` / `Avoid` | verified | 2 m road side to 10 m building side, 1 m up. |
| `backupAt` | `aiPedestrian::Anticipate` | verified | Side-signed 0.2 m offset and heading. |
| `avoidObstacle` | `aiPedestrian::AvoidObstacle` | fixed | Side choice and the in-place turn (GetHeading row 0 at the kept distance, offset direction kept) verified. Now: the target is stored; the corner turn-back moves the pedestrian between road lists; MM2 parks the old previous side (an int) in the radius slot and loads it as a float, so +1 aims at the obstacle itself (denormal offset) and -1 makes a NaN that takes heading and position (OpenMM2 hides that pedestrian until its road is cleared, `lost`); angle summed z, y, x. |
| `wander` | `aiPedestrian::Wander`, `AvoidPlayer` | fixed | Entry queues, wall reset, 6 m player check, STAND_WALK queueing and the walk verified; keeps the road distance. Props are not avoided (see Missing). |
| `anticipate` | `aiPedestrian::Anticipate` | fixed | MM2 reacts to a change of reaction only and leaves the crossing state's "last" alone (OpenMM2 also compared/updated the crossing state); turning to run with the car turns round by +3.14 or -3.14 by the heading's sign (was always +3.14) after GetHeading row 0. |
| `avoid` | `aiPedestrian::Avoid` | fixed | Reaction-only entry as above; running at a wall backs up with the unsigned axis (as coded; OpenMM2 used Anticipate's side-signed version); on a corner MM2 neither probes nor clears the wall flag. Dive choice, keep-up scales (5 at the ground dives, 3 after frame 12) and sums verified. |
| `curbPoint`, `crossTargets` | `PreCrossStreet`, `WaitCrossStreet` targets | verified | Near/far curb points 2.5 m into the intersection by the crossing choice and the previous direction/side. |
| `accident` | `aiPedestrian::Accident` | open | MM2 checks the crossed intersection's vehicle list and the per-section vehicle lists of both sides at section 1 (direction +1) or n-1 of the current road; OpenMM2 asks `Traffic::accidentAt(node, path)`, which takes any car on the road. Needs a section-level query in Traffic (ai-vehicles). |
| `abortCrossing` | the abort in `PreCrossStreet` / `WaitCrossStreet` / `CrossStreet` | verified | Back onto the previous road (its prevPath kept), turned round, WALK queued; now through the road lists. |
| `preCross` | `aiPedestrian::PreCrossStreet` | verified | |
| `waitCross` | `aiPedestrian::WaitCrossStreet` | fixed | Reaching the far curb set crossing 1 and returned; MM2 goes on to the light check, so WALK (light 0 state 4 in the walk phase) overrides it with 3. |
| `crossStreet` | `aiPedestrian::CrossStreet` | verified | RUN queued when light 0 shows the don't-walk state (5). |
| `update` | `aiPedestrian::Update` | fixed | Nearest-player distance, BACKUP release, busy states, detection thresholds (0.75 s, 2.3 s, 1 m/s), dispatch, movement (forward then lateral x scale, x dt) and ground handling verified. Added the MM2 order of the matrix rebuild and the `lost` case. (MM2 also hands a pedestrian whose instance has an attached entity to it; nothing attaches one in either game's normal play.) |
| `animate` | `pedAnimationInstance::PreUpdate`, `Update` | fixed | One frame clock shared by all pedestrian updates: each adds dt x 30 and takes the whole frames, the fraction carries to the next pedestrian (OpenMM2 rounded dt x 30 per update). |
| `updateRoad` | `aiPath::UpdatePedestrians` | fixed | New: down the road's list from its head; a pedestrian that moves road carries the walk into its new road's list (those update now, the rest of the old road waits), stopping at the road's head. A step cap is an OpenMM2 guard. |
| `step` | `aiMap::Update`, `aiMap::Reset` | fixed | First room at the start, room changes other than to 0 adjust; now updates the populated roads in MM2's list order (newest first) instead of path index order. |
| `publish`, `activeCount`, `distanceFromSidewalk` | — | openmm2 | Output for the renderer, audio and tests. |

## Ambient routing (`src/ai/AmbientRoute.{h,cpp}`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `pathOf`, `pathCount`, `pathAt`, `leavingDir`, `laneCount`, `available` | `aiIntersection::Path`, the leaving-direction rule (end1 here: +1), side flag bit 0 | verified | |
| `row`, `endFrame` | the lane/sidewalk vertex rows; lane row vertex n-1 and xAxis[n-1] / -xAxis[0] | verified | |
| `take` | the rail's next path/dir/lane | verified | |
| `lateralOfStart` | `ChooseNextLeftStraightLink` / `RightStraightLink` | verified | Lane clamped only when the next road has fewer lanes than the lane number, else the sidewalk row; XZ dot with the arrival left vector. |
| `chooseNextRightLink` | `aiMap::ChooseNextRightLink` | verified | |
| `chooseTurnOrStraight` | `aiMap::ChooseNextLeftStraightLink`, `ChooseNextRightStraightLink` | verified | First open road, then the next one if within 2 m (-2 / +2), frand x found; lane 0 / last. |
| `numAvailSinks` | `aiIntersection::NumAvailSinks` | verified | |
| `chooseNextStraightLink` | `aiMap::ChooseNextStraightLink` | deviation | Rules verified. OpenMM2 clamps the lane in the wide-road branch (MM2 keeps it unclamped), and when the three-way branch finds no second open road MM2 returns success keeping the rail's previous next road; OpenMM2 reports failure. |
| `chooseStraightLinkAt4Way` | `aiMap::ChooseStraightLinkAt4Way` | verified | |
| `chooseNextRandomLink` | `aiMap::ChooseNextRandomLink`, `aiRailSet::SolveTurnType` | verified | First freeway taken at once; frand x count; lane by turn type. |
| `byLane` | the lane dispatch of `ChooseNextLaneLink` / `ChooseNextFreewayLink` | verified | |
| `chooseNextFreewayLink` | `aiMap::ChooseNextFreewayLink` | deviation | OpenMM2 clamps the kept lane to the freeway's lanes; MM2 keeps it unclamped. |
| `chooseNextRightStraightFreewayLink` | `aiMap::ChooseNextRightStraightFreewayLink` | verified | Choice by trunc(2 frand), overwritten by the lane rule, as coded. |
| `arrivalIntersection` | the path's end by direction | verified | |
| `solveTurnType` | `aiRailSet::SolveTurnType` | fixed | Vectors and the 0.5 rad thresholds verified; the dot products are now summed in MM2's order (x, y, z arriving at the end, x, z, y at the start). |
| `chooseNextLaneLink` | `aiMap::ChooseNextLaneLink` | deviation | Dispatch verified; OpenMM2 clears the next link before choosing and reports failure without one (MM2 keeps the previous next road on some failures); range guards are OpenMM2's. |

## Traffic lights (`src/ai/TrafficLights.{h,cpp}`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `LightState`, `kLightCycleSeconds`, `kLightAmberSeconds` | instance state +0x3c, set +0x2c (6.0), 3.0 | verified | |
| `build` | `aiTrafficLightSet::aiTrafficLightSet`, `SetFourWay`, `aiIntersection::NumSources`, `NumSinks` | verified | One light per path end with rule 1 in path order; the cycle type comes from RoadNetwork (ai-vehicles), which counts sources and sinks as MM2 codes them (no retail four-way set). |
| `setState`, `pairedLight` | (current + 2) mod 4 | verified | |
| `resetSet`, `reset` | `aiTrafficLightSet::Reset` | verified | Light 0 green, others red (light 2 also green in a four-way set), timer, current and walk phase 0. |
| `turnGreen` | `aiTrafficLightSet::Update`, `aiPath::ResetVehicleReactTicks` | verified | The event carries the path at the light's index. In a four-way set MM2 passes the current light's direction for the paired road too; the traffic side recomputes it per road (no retail four-way set). |
| `update` | `aiTrafficLightSet::Update` | verified | Amber within 3 s of the 6 s cycle, red and next green with the overshoot dropped, the walk phase (4, then 5 in its last 3 s) for every non-rotating set, then Reset. Advanced by the fixed AI step (see World). |
| `setOf`, `hasLights`, `cycleAt`, `walkPhaseAt`, `firstLightAt` | intersection +0x20, set +0x26, +0x28, light 0 | verified | What PickNextRdSeg, WaitCrossStreet and CrossStreet read. |
| `state` | — | openmm2 | Slot lookup for the renderer (invalid slots read green). |
| `forceAll` | `aiMap::AllwaysGreen`, `AllwaysRed` | deviation | Unused debug helper. MM2's debug toggles set a per-road "always go/stop" flag (`aiPath::AllwaysGo`) read by the traffic, not the light states. |

## World (`src/ai/World.{h,cpp}`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Settings` | dgStatePack (densities, weather) | verified | `winterPeds` = weather > 2 as aiMap::Init picks the bad-weather names. |
| `kAiStepSeconds`, `World::update` | `aiMap::Update` per frame | deviation | OpenMM2 steps the AI at a fixed 30 Hz (at most 8 steps per call) for determinism; MM2 updates once per frame with the frame time. |
| `World::step` | `aiMap::Update` | fixed | Order (ambient traffic, pedestrians, then the light sets as children) verified. The player's room is now looked up from the last room as the hint (MM2 passes aiVehiclePlayer's last room; using the last room found is inferred equivalent); 0 leaves the populations alone. |
| `World::roomAt` | `cityLevel::FindRoomId` | fixed | Takes a hint (see RoomLocator). |
| `World::create` | `aiMap::Init`, `aiCityData`, `aiTrafficLightInstance::Init` | fixed | Pool trunc([Ped Pool] x density), types (race list else city list, good/bad weather), light models (lanes >= 2: the second) and pole matrix verified. [Ped Pool] now comes from the config as sscanf "%d" (a "0" was lost before, see Race); the room locator gets the map name. |
| `World::create` (seeds) | `ResetRandomSeed` | deviation | Traffic seeded with `seed`, pedestrians with `seed x 7919 + 1` (one global in MM2). |
| `World::create` (drive on the left fallback) | `aiCityData` | deviation | With no city AI map MM2 drives on the right; OpenMM2 falls back to the cruise maps' flag (retail city maps have the key). |
| `defaultAmbientTypes`, `citySuffix` | — | deviation | OpenMM2 fallback when no AI map lists ambient types (MM2 would have none). |
| `loadPedTypes` | `pedAnimationInstance::Load` | inferred | Loads every type present; MM2 loads the named types on demand. Same tables. |
| `loadCityAiConfig` | `aiCityData::aiCityData` | verified | city/<map>.aimap. |
| `updateSignals`, `asText` | — | openmm2 | Glue. |

## City formats and lookups (`src/city`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `parseBai`, `readSide`, `readEnd` | `aiMap::ReadBinary`, `aiPath::ReadBinary`, `aiIntersection::ReadBinary`, `CArrayList::ReadBinary` | verified | Same bytes: OpenMM2's "unknown5/6" are the first length of row 0, its laneEndValues/laneExtras the per-row offsets, its "unknown" the first centre length, its roadIndex the end's u32. MM2 reads one tram and one train row whatever the count and lanes + sidewalks rows (OpenMM2 assumes one sidewalk); retail parses to the last byte. The room tables are aiMap +0x174 (ambients) and +0x178 (pedestrians). |
| `validateAiMap` | — | openmm2 | Tool/test checks. |
| `RoomLocator::RoomLocator` | `cityLevel::InitFullProbe(64, 64)`, the room spans and warps of `cityLevel::Load`, `AddWarp` | fixed | Was a 32 m grid picking the highest floor below the position. Now MM2's 64 x 64 grid over the perimeter extent (rooms listed in id order per overlapped cell), MinY/MaxY per room (subterranean: -1000 to the highest perimeter point + 7 or the tunnel height; others: lowest point - 1 to 1000), the warp list (ordinary rooms holding an end or the 0.33/0.66/0.5/0.16/0.86 points of a warp room's edges) and London's four hand-made warps. |
| `RoomLocator::find` | `cityLevel::FindRoomId`, `FullProbe`, `IsInRoomCheckWarps` | fixed | Hint room, then the highest-numbered neighbour, then the last room of the grid cell (truncated cell index); warp rooms need their height span, ordinary rooms give up positions inside their warps. |
| `RoomLocator::pointInPerimeter` | `sdlPage16::PointInPerimeter` | fixed | Half-open crossing test with MM2's repeated-vertex skip (was an equivalent generic test; now the same code). |
| `collectRoomPolygons`, `Collector::attribute` | `sdlPage16::Collect` | verified | Attribute walk, resume state ((word offset << 11) | texture), early return state, capacity count-down before every attempt, the SpecialBound probe rule and the texture/no-texture flag compared case by case with the decompile and asm. Out-of-range reads become 0 and a bad resume state returns nothing (OpenMM2 guards). |
| `Collector::roadStrip`, `sidewalkStrip`, `rectangleStrip`, `crosswalk`, `fan`, `facadeBound`, `dividedRoadStrip`, `roof` | `sdlPage16::Collect` cases 0, 8, 0x10, 0x20, 0x28/0x30, 0x38, 0x40, 0x60 | verified | Vertex columns, 0.15 curb rise, material offsets (+0/+1/+2, divider texture +1/+2), the material-2 fan skip, height culling and the three FindBoundingIsoParams passes match. |
| `Collector::tunnel`, `tunnelStrip` | `sdlPage16::Collect` case 0x48 | verified | Wall height max(h2, 3), railing offset h1 x 0.333, slope rise 0.25, junction edge masks, the following strip's stride; the stride/attribute guards are OpenMM2's (MM2 reads uninitialised or past the list). |
| `Collector::leftSidewalk`, `rightSidewalk`, `roadSurface`, `dividerMedian` | strip passes of `Collect` | verified | |
| `Collector::nearPair`, `nearTriangle`, `spansHeight` | the inlined culling of `Collect` | verified | 2 (d^2 + r^2) reach (computed (longest + r^2) doubled), longest edge from a or c, the height-span sign test with its NaN behaviour. |
| `Collector::findBoundingIsoParams`, `isoSide` | `sdlPage16::FindBoundingIsoParams` | verified | |
| `Collector::initNoArea`, `setQuad`, `setFlatQuad`, `setTri`, `setFlatTri`, `setWall`, `push` | `sdlPoly::InitNoArea`, `SetQuad`, `SetFlatQuad`, `SetTri`, `SetFlatTri`, `SetWall` | verified | |
| `SdlPolyBuffer::reset` | the resets in `lvlSDL::CollidePolyToLevel` / `CollideProbe` | verified | |
| `sdlTextureMaterials` | `lvlSDL::LoadBinary` (materials.csv) | verified | Header skipped, "none" skipped, first row wins, movie "-0nnn" suffix stripped. |
| `sdlMaterialIndex` | `lvlMaterialMgr::Load` order | verified | |
| `buildRoomMesh` / `RoomBuilder::roadStrip` | `sdlPage16::Draw` case 0 | open | MM2 draws four levels of detail (outer-to-outer strip with the LOD texture over every other section; the same with outer edges lowered 0.15 m; sidewalks and road separately; and only at the top level the curb raised 0.15 m with half-bright curb faces) and maps the road with `ArcMap` (t 1 at the curbs, 0 at the centre; s along the strip in whole repeats of about the average width, run back and forth); the builder has one level, curbs raised to the outer height, and u across / v along / width. Porting `sdlPage16::Draw` (immediate mode with vertex colours from `GetShadedColor`) needs LOD and colour support in the renderer (rendering-fx). |
| `RoomBuilder::sidewalkStrip` | `sdlPage16::Draw` case 8 | open | MM2: planar 4 m repeats (x / 4, z / 4 less the whole repeats at the first vertex), curb end caps as half-bright triangles, curb faces half-bright, curb rise only at the top level. |
| `RoomBuilder::rectStrip` | `sdlPage16::Draw` case 0x10 | open | MM2: `ArcMap` s along, t 0/1 across, first texture. |
| `RoomBuilder::sliver` | `sdlPage16::Draw` case 0x18 | open | MM2: u = round(flat length x density), v = (vertex height - top) x density, back-face culled, shaded by the light index of the last FacadeBound; an untextured flat-colour path at level 0. |
| `RoomBuilder::crosswalk` | `sdlPage16::Draw` case 0x20 | open | MM2: (1,0)/(0,0) on the first pair, v = length / width on the second, third texture, drawn only when not above the camera; the 1 cm lift is OpenMM2's (z-fighting). |
| `RoomBuilder::fan` | `sdlPage16::Draw` cases 0x28, 0x30 | verified | Planar 8 m repeats as MM2 (MM2 subtracts the whole repeats at the hub, which wrap addressing ignores). MM2 skips road fans above the camera (a draw-time cull). |
| `RoomBuilder::facadeBound` | `sdlPage16::Draw` case 0x38 | deviation | MM2 draws nothing and takes the attribute's first word as the light index for the following walls; OpenMM2 emits the wall only for the probe soup (`includeFacadeBounds`). |
| `RoomBuilder::dividedStrip` | `sdlPage16::Draw` case 0x40 | open | As road strips (levels of detail, ArcMap), plus the divider's cap flags (header bits 6 and 7) and per-type drawing; the builder's divider shapes are reconstructions. |
| `RoomBuilder::tunnel`, `tunnelWalls`, `junctionWalls` | `sdlPage16::Draw` case 0x48 | open | MM2 draws tunnel walls, railings (0.333 x h1 out), sloped sides at 0.75 / 0.25 of the height and ceilings with their own mapping; the builder's walls are reconstructions. |
| `RoomBuilder::facade` | `sdlPage16::Draw` case 0x58 | open | MM2 reads the repeats unsigned (the builder takes the magnitude of the signed value), maps v 0 at the bottom and the repeat at the top (the builder the reverse), culls back faces and shades by the FacadeBound light. |
| `RoomBuilder::roof` | `sdlPage16::Draw` case 0x60 | verified | Height-table height, planar 8 m repeats; MM2 skips roofs above the camera (draw-time cull). |
| `RoomBuilder` helpers (`batch`, `tri`, `triUp`, `triFacing`, `quadUp`, `quadFacing`, `wall`, `cumulative`, `sidewalkBand`, `roadBand`, `tex`), `surfaceKindName`, `CityMesh::triangleCount`, `vertexCount`, `buildCityMesh` | — | inferred | Mesh assembly for the GPU renderer; MM2 draws immediate-mode strips and fans. Texture groups (`tex`: value - 1 + offset) match `sdlPage16::GetTexture`. |
| `parseCityInfo` | `mmCityInfo::Load` | fixed | Counts read with "%d"; a nonzero count now becomes the number of names (one more than the bars), a zero count leaves the names unread; MustPlace and UnlockGroup (never read by MM2) removed. Key lookup instead of MM2's fixed key order (retail order). |
| `parseRaceTable` | `mmRaceData::Load` | fixed | Column order verified; numbers now atoi/atof prefixes. MM2's strtok skips empty fields (none in retail). |
| `parseWaypoints` | `mmPositions::Load`, `mmWaypoints::LoadCSV` | fixed | atof x, y, z, angle and atoi radius (0 means 15, applied by the session); numbers now atof prefixes. |
| `parseOpponentPath` | `aiRouteRacer::Init` | fixed | MM2 reads x, y, z and the angle (atof) per row; OpenMM2 reads more columns (unused); numbers now atof prefixes. |
| `parseAiMapConfig` | `aiCityData::aiCityData`, `aiRaceData::aiRaceData` | fixed | Values sscanf "%d"/"%f" (prefix, the default stays when nothing parses); the lists MM2 reads take their count and that many entries; value sections ("0" included) are never lists (a 0 value was dropped as an empty list); a negative first ambient probability gives equal shares; [Ped Pool] parsed. Case-insensitive, trimmed headers and skipped comment lines are OpenMM2 leniency (retail is the same either way). |
| `parseCrashEvents` | `mmSingleStunt::LoadEventFile` | verified | File, event, checkpoints (atoi), time limit and density (atof), then a float and two ints; OpenMM2 reads the extras as floats (atof prefixes). |
| `parseRewards` | `mmRewardList::Load` | fixed | Now skips rows starting with '#'; the 32-row cap and race-type matching are applied by its user (game/Profile). |
| `raceModeName`, `raceModePrefix` | the file name formats (`crash%ddata`, ...) | verified | |
| `parseInst` | `lvlLevel::LoadInstances` | verified | Room = the header's low 16 bits, flags the high 16; length byte bit 7 = compact rotation (x axis x and z, m2 = (-z, 0, x)), else 12 floats. |
| `parsePathSet` | `dgPathSet::Load`, `dgPath::Load` | fixed | Positions were right but per point MM2 reads the flags word *before* the position and ends each path with type and spacing bytes (spacing x 0.25, 0 means 5 m); `flags`, `type` and `spacing` now give MM2's reading (the old fields stay for the prop placement code). |
| `parseCpvs`, `RoomPvs::level` | `cityLevel::DecompressPvs` | verified | RLE (c < 0x80: c copies; else c - 0x7f literals), zero-filled to (rooms + 3) / 4 bytes, 2 bits per room. |
| `RoomPvs::visibleFrom`, `hasData` | — | openmm2 | Accessors. |
| `parsePsdl` | `lvlSDL::LoadBinary`, `sdlPage16::LoadBinary`, `lvlAiMap::LoadBinary`, `lvlAiRoad::LoadBinary` | fixed | Layout verified field by field. The road record's first word is one 32-bit flag word (lvlAiMap::IsBlocked, IsPedBlocked, IsDivided, IsAlley, IsFreeway, GetIntersectionType bits), not "flags, unknown, propRule"; the bytes after the lane values are the per-end stop light types (GetStopLightType). Renamed accordingly. |
| `decodePsdlAttributes`, `PsdlAttribute` accessors | the attribute walk of `Collect` / `Draw` | verified | Sizes agree with retail (every room parses to its end). MM2 reads a count word for any attribute with subtype 0 and a third word for a subtype-0 texture whose value has bits 0x700; neither occurs in retail. |
| `psdlAttrTypeName`, `validatePsdl` | — | openmm2 | Tools. |
| `parseLighting` | `cityTimeWeatherLighting::FileIO` | verified | Field names and types. (MM2 computes the derived ambient levels before loading the file; see the notes for rendering-fx.) |
| `parseFogTable` | `lvlSky::AutoInit` (<map>_fog.csv) | fixed | Start and end are atoi in MM2 (were atof); MM2 reads at most 16 rows. |
| `parseSky` | `lvlSky::AutoInit` (.sky) | verified | "%s %f %f %f" from the first line. |
| `parseWater` | `cityLevel::Load` (.water) | verified | Height, then room ids (MM2 marks them HasWater). |
| `parseLightMap` | `cityLevel::Load` (.lmap) | verified | LMP0, count = room count (else MM2 ignores the file). |
| `parseMaterialLibrary` | `lvlMaterial::Load` | verified | Same keys; MM2 reads them in a fixed order (retail order). |
| `parseTextureMaterials` | `lvlSDL::LoadBinary` | verified | |
| `parseExtent`, `parseResetPoints` | — | inferred | No loader for .ext or .reset in midtown2.exe; used by tools only. |
| `listCities` | `mmCityList::LoadAll`, `Load` | inferred | MM2 loads sf.cinfo first, then tune/*.cinfo in enumeration order, keeping the first city per race directory; OpenMM2 sorts by map name and the frontend moves SF first. |
| `listRaces`, `loadCity` | the per-mode file names | inferred | OpenMM2 glue gathering the files MM2's game modes open. |
| `detail::Reader` | `Stream::Read` | verified | Little-endian reads; vec3 x, y, z. |
| `detail::cAtoi`, `cAtof`, `scanInt`, `scanFloat` | `atoi`, `atof`, `sscanf` | fixed | New: C-library prefix parsing for the text formats (strict whole-string parsing rejected trailing text and accepted hex). |

## Missing

| MM2 | What it does | Status |
| --- | --- | --- |
| `aiPath::ReadShortcut` (city/<map>_sup.bai) | Shortcut roads loaded after the regular ones, linked into the intersections (`AddRoad`, `CreateRoadMap`) and rooms; used by opponent routing (`aiMap::CalcRoute`) and skipped by `GetRoadToRight/Left` | open: the parse is small (same path layout), but routing and the road network belong to ai-vehicles |
| `aiPedestrian::DetectBangerCollision`, `AvoidBanger` | Pedestrians step round props on the walkway (and at the corner's intersection) using the per-section and per-intersection obstacle lists `aiPath::AddBangersToObsMap` / `aiIntersection::AddBangersToObsMap` build | open: needs those obstacle maps (props are camera-props, the maps ai-vehicles) |
| `aiPedestrian::DetectPedCollision`, `AvoidPedCollision` | Pedestrian-to-pedestrian avoidance | not needed: DetectPedCollision has no caller, and Wander's call of AvoidPedCollision sits behind a test that is never true |
| `aiPedestrian::Stop`, `Go` | Queue WALK_STAND / STAND_WALK | not needed: no callers |
| `aiMap::AddPedPlayer`, `RemPedPlayer` (per-player bits) | Several players sharing populated roads | not needed for one player; the bookkeeping matches MM2 with player 0 |
| `pedAnimation::DrawSkeleton` (the .rays data) | Stick-figure level of detail for distant pedestrians, drawn from the per-bone widths, offsets and colours in anim/<type>.rays | open for rendering-fx: the AI does not use it |
| `sdlPage16::Draw` | The textured, shaded, four-level PSDL drawing | open (see CityMesh rows): a port needs LOD and vertex colour support in CityRenderer |
| `cityLevel::FindRoomId` hint in the camera's room lookup | `CityRenderer::roomAt` passes no hint | open for rendering-fx: pass the last room |

## Notes for other areas

- ai-vehicles: `Traffic::accidentAt` should check MM2's per-section lists
  (see `accident`); `RoadNetwork` picks end 0 for a light on a loop road
  where `aiTrafficLightSet` uses end 1; shortcut roads (`_sup.bai`).
- phys-core / vehicle / rendering-fx: MM2's wheel probes see the Collect
  polygons, not the render mesh. `lvlSDL::CollideProbe` first retests the
  wheel's cached polygon (`sdlPolyCached`), then collides the segment
  (sphere at its midpoint, radius 0.51 x length) with `sdlPage16::CollideSegment`
  in the start room, the end room and up to ten neighbouring rooms flagged
  Instance (0x80), with the probe room's SpecialBound flag making road
  surfaces triangles; the nearest hit's polygon material is reported.
  OpenMM2's probe soup (`CityLevel`) is built from `CityMesh` (render
  triangles plus facade bounds) and instance bounds, so the wheels miss the
  curb faces, the 0.15 m sidewalk rise, divider walls and tops, tunnel
  walls and railings, the SpecialBound triangles and the deep-water fan skip.
  `collectRoomPolygons(..., probeRoom)` already implements the MM2 side.
- rendering-fx: `cityTimeWeatherLighting::ComputeAmbientLightLevels` runs
  before the .lt file is loaded (the derived levels come from the previous
  values); `CityRenderer::roomAt` should pass a hint; the .rays stick figure.
- session: the traffic and pedestrians read the player at its ICS
  (centre of mass) position as `aiVehiclePlayer::Position` gives it, which
  `RaceScreen` passes (checked). `aiMap::Update` runs the ambient traffic and
  the pedestrians *before* the racers and the police and the light sets
  last; `RaceScreen` runs the opponent and police drivers before
  `World::update` (lights still last). `PlayerCar::radius` should be the
  model's bounding radius (`lvlInstance::GetRadius`); it is the box's
  half-diagonal.
- Neither the pedestrians nor the lights need a hook for the racers'
  `StopRoadTraffic` (it only holds ambient cars).
- The C-library number helpers here (`city::detail::cAtoi` and friends)
  overlap `src/data/CNumbers.h` on integration; they can be merged.
