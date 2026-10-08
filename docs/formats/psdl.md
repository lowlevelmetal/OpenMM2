# City streets (`city/<map>.psdl`)

Parser: `src/city/Psdl.{h,cpp}`. Geometry builder: `src/city/CityMesh.{h,cpp}`.
Tool: `mm2tool psdlinfo <source> <city> [room]`, `mm2tool psdl2obj <source> <city> out.obj`.

Retail coverage: all 5 PSDL files on the disc (`london`, `sf`, plus the
unused `city`, `sfai`, `variant`) parse to their last byte, and every room's
attribute list decodes to exactly its stored length (London 1,341 rooms,
SF 1,171). Structural validation (every vertex, height, texture and
neighbour index in range) passes for both cities. A top-down render of the
generated mesh shows the expected street layouts; see "Verification" below.

"Verified" below means confirmed by exact-length parsing and/or index and
geometry checks across all retail data. "Inferred" means a best guess that
fits the data but has not been confirmed.

## Layout (little-endian)

```
char[4]  "PSD0"
u32      version = 2
u32      vertexCount
float3   vertices[vertexCount]                 absolute world positions, Y up
u32      heightCount
f32      heights[heightCount]                  absolute Y values (verified: roofs sit 11..20 m above street vertices)
u32      textureCount                          number of names + 1
textures[textureCount - 1]:
    u8   length                                includes the terminating NUL; 0 = empty slot
    char name[length]
u32      roomCount                             includes the dummy room 0, which has no record
u32      firstRoadRoom                         rooms [1, firstRoadRoom) are building blocks (verified: flag 0x04/0x84 only)
room[roomCount - 1]:                           rooms 1..roomCount-1
    u32  perimeterCount
    u32  attributeWords
    perimeter[perimeterCount]: u16 vertex, u16 neighborRoom    0 = no neighbour
    u16  attributes[attributeWords]
u8       roomFlags[roomCount]                  index = room id (entry 0 = dummy)
u8       roomPropRule[roomCount]               entry 0 is 0xCD (uninitialised)
float3   boundsMin, boundsMax
float3   sphereCenter
f32      sphereRadius
u32      roadCount
road[roadCount]:
    u32  flags                                  lvlAiRoad +0: bit 1 blocked, 2 no pedestrians,
                                                3 divided, 4 alley, 5 freeway (lvlAiMap::Is*);
                                                bits 10-11 / 16-17 intersection type at each end
                                                (lvlAiMap::GetIntersectionType)
    u8   leftCount, rightCount                  lanes per side (lvlAiMap::GetNumLanes)
    f32  leftValues[leftCount], rightValues[rightCount]   one per lane, fractions in (0,1); use inferred
    u8   stopLights[2]                          per end: stop light / sign type, masked with 0x15
                                                (lvlAiMap::GetStopLightType); values 0/1/128/129
    u16  startCrossroads[4], endCrossroads[4]   corner vertices of the end intersections (verified)
    u8   roomCountOfRoad
    i16  rooms[roomCountOfRoad]                 negative = negated room id (SF only, 23 roads)
```

The road table has the same count and order as the AI map's paths
(`city/<map>.bai`), and each AI path lists the road's rooms (verified for
over 90% of roads; the rest add intersection rooms). The editor generated the
AI map from it. Whether the negative room ids mean "traversed in reverse" is
unknown; the `.bai` stores them positive.

### Neighbours

Perimeter point *i* and *i+1* bound an edge; `neighborRoom` of point *i* is the
room across that edge. Links are symmetric except for 53 of about 5,800 in
London and 138 in SF, mostly around tunnels and bridges.

### Room flags

Names follow mm2hook's `RoomFlags`; the observed use differs for 0x04.

| Bit  | mm2hook name  | Observed |
|------|---------------|----------|
| 0x01 | UnhitBanger   | unused in retail PSDLs |
| 0x02 | Subterranean  | subway tunnels and stations (with 0x40) |
| 0x04 | Water         | every building block; also water rooms (0x46) |
| 0x08 | Road          | road rooms |
| 0x10 | Intersection  | intersection rooms (verified: >90% of `.bai` intersections) |
| 0x20 | SpecialBound  | a few road rooms |
| 0x40 | Warp          | tunnels, bridges and the empty water rooms |
| 0x80 | Instance      | building blocks with landmarks, some roads |

The twelve London rooms flagged 0x46 have no attributes. They are water.

### The game's room flags (lvlRoomInfo)

MM2 keeps a second, separate set of room flags in each room's
`lvlRoomInfo` (+0). `cityLevel::Load` starts them at 0 and sets them from
the PSDL; `lvlLevel::LoadInstances` and `gizBridge::Init` add to them.
mmPlayer::Update (cameras), dgPhysManager::Collide (warp), vehCar
(sinking, skid marks) and aiPoliceOfficer (giving up a chase) read these,
not the PSDL byte above. `city::levelRoomFlags` builds them
(`CityData::levelRoomFlags`, `city::LevelRoomFlag`):

| Bit  | Set when |
|------|----------|
| 0x01 | an intersection (PSDL 0x10) that is not a PSDL warp room, or a road (0x08) whose first attribute after its texture and tunnel attributes is not a divided road, in a room that is not a PSDL warp room, after no tunnel or a tunnel whose header has neither of its two low bits. Only `dgPhysManager::CollideTerrain` reads it, behind a switch mmGame keeps off |
| 0x02, 0x08 | PSDL subterranean (0x02) |
| 0x04 | "Water of Death": the room's first attribute is a texture whose material is lvlMaterialMgr's second entry (deepwater), or the room is listed in `city/<map>.water` (23 rooms in London, 45 in SF; the .water files list 3 each) |
| 0x10 | `gizBridge::Init`: the rooms at a bridge and 5 m above it (at run time; not built here) |
| 0x20 | a `.inst` / `_ai.inst` record with instance flag 0x100 (its own terrain bound) is in the room |
| 0x40 | rooms 411, 412, 423 and 625 when the city's name contains "sf" |

`cityLevel::GetWaterLevel` returns the `.water` file's level whatever the
room.

### Room lookup (cityLevel::FindRoomId)

`city::RoomLocator` follows MM2 (build 3393): the caller's last room if
the position is inside its perimeter (`sdlPage16::PointInPerimeter`, XZ),
else the highest-numbered neighbour across its perimeter edges that holds
it, else `FullProbe`: a 64 x 64 grid over the extent of all perimeters,
each room listed (in id order) in the cells its perimeter bounds overlap,
cells indexed by truncation, and the *last* room of the cell that holds the
position wins. A Warp (0x40) room also needs the height inside its span
(`cityLevel::Load`): subterranean rooms reach from -1000 to their highest
perimeter point plus 7 m (or plus the height of a leading tunnel attribute,
after an optional texture), other rooms from 1 m below their lowest
perimeter point to 1000. An ordinary room that a warp room overlaps (an end
or the 0.33, 0.66, 0.5, 0.16 or 0.86 point of any of the warp room's
perimeter edges inside it) lists it as a warp and does not claim positions
inside the warp room's perimeter and span. London adds four hand-made
warps: rooms 597, 599, 600 and 1297 to room 382.

## Attributes

Each attribute starts with a header word:

```
bit 7      last geometry attribute of the room (Texture/Tunnel words may follow it)
bits 3..6  type
bits 0..2  subtype
```

For the variable-length types marked *count* below, subtype 0 means an
explicit count word follows the header; otherwise the subtype is the count.
Vertex arguments index `vertices`, height arguments index `heights`.

| Type | Name | Arguments | Geometry |
|------|------|-----------|----------|
| 0 | RoadStrip | *count* × 4 vertices | per section: outer left, curb left, curb right, outer right |
| 1 | SidewalkStrip | *count* × 2 vertices | pairs (curb at road level, outer edge at sidewalk level) |
| 2 | RectangleStrip | *count* × 2 vertices | quad strip |
| 3 | Sliver | top height, texture scale, 2 vertices | thin wall from the vertices up to `top` |
| 4 | Crosswalk | 4 vertices | pairs (0,1) and (2,3) across the road |
| 5 | RoadTriangleFan | *count* + 2 vertices | triangle fan, intersection surface |
| 6 | TriangleFan | *count* + 2 vertices | triangle fan |
| 7 | FacadeBound | angle, top height, 2 vertices | invisible wall (collision/lighting) |
| 8 | DividedRoadStrip | *count*: 2 header words + count × 6 vertices | outer L, curb L, median L, median R, curb R, outer R |
| 9 | Tunnel | *count* words | flags, two heights, then edge masks (see below) |
| 10 | Texture | 1 word | texture value = (subtype << 8) + word |
| 11 | Facade | bottom height, top height, u repeat, v repeat, 2 vertices | textured building wall |
| 12 | RoofTriangleFan | *count* + 2 words: height, then count + 1 vertices | flat roof polygon |

Every field layout above is verified: every room decodes to its exact stored
length, and every index argument is in range. The geometry meanings are
verified for road, sidewalk, rectangle, crosswalk, fan, facade, sliver and
roof attributes by rendering (roads join intersections, curbs line roads,
roofs close building outlines).

### Textures

A Texture attribute selects a texture *group*: base index = value - 1
(value 0 = no texture, used by some invisible tunnel floors). Attributes then
pick from the group by offset (verified from the texture names in both cities):

| Group kind | +0 | +1 | +2 | +3 |
|-----------|----|----|----|----|
| road | road surface | sidewalk | low-detail road (`*_lo_l`) | |
| intersection | intersection surface | sidewalk | crosswalk | low-detail intersection |

RoadStrip, RectangleStrip and both fans use +0. SidewalkStrip and the sidewalk
part of road strips use +1. Crosswalk uses +2. Facade, Sliver, Roof, Tunnel use +0.

### Facades and walls

Facade/Sliver/FacadeBound walls run from the first to the second vertex. Their
outward normal is `(v2 - v1) × up`. This is verified: 19,436 of 19,454 London
facades (11,508 of 11,533 in SF) face out of the roof polygon of their
building. `u repeat` and `v repeat` are signed. Some facades store negative
values, and whether those mirror the texture is unknown, so the builder uses
the magnitude.

### Slivers

A Sliver's two arguments are both indices into the height table: the wall's
top height and its texture density. The density values used are 1/10, 1/8,
1/6, 1/5, 1/4, 1/2 and 1 (London: indices 93, 230, 4, 9, 14, 399, 42; SF:
62, 361, 17, 15, 5, 2), i.e. texture repeats per metre. The builder repeats
the texture `length × density` times along the wall and `height × density`
times up it. Verified: the argument only ever points at these entries;
inferred: that the same density applies vertically.

### Sidewalk markers

A SidewalkStrip whose first pair is `(0,0)` or `(1,1)` uses that pair as a marker,
not geometry (58 of each in London, 36 of each in SF, always first). Inferred:
start/end curb caps. The builder closes the curb with a vertical face.

### Divided roads

Header word 0: low byte = flags, high byte = divider texture value.
Header word 1 = divider height in 8.8 fixed point.

- flags & 3: divider type. 1 = flat (value 5.0), 2 = elevated (0.15 m), 3 = wedged
  (1.0 m; texture `s_jersey_rail`, a jersey barrier). Inferred from textures and heights.
- flags & 0x40 / 0x80: closed divider at start / end (inferred).
- Divider texture: values come in pairs such as `swalk_f` / `s_grass`. The
  builder uses value-1 for the sides and value for the top (inferred).

### Tunnels

Tunnel attributes describe walls, ceilings and railings for the next road-type
attribute in the room (RoadStrip, RectangleStrip, DividedRoadStrip, or the
RoadTriangleFan of a junction). Words: `flags`, `height1`, `height2`
(8.8 fixed point), then for junctions (subtype 0) per-perimeter-edge bit
masks.

Observed combinations (London/SF):

| flags | heights | texture | where |
|-------|---------|---------|-------|
| 0x0103, 0x0B53, 0x15A3, 0x1FF3 | 5.5 / 7.0 | `sub_tunnel` | subway (rooms 0x4A) |
| 0x0103, 0x0B53, 0x15A3, 0x0353 | 9–10 / 7.0 | `s_tun_sw` | SF road tunnels |
| 0x40C2, 0x4003, ... | 1.5 / 1.0 | `s_conc_rail`, `tl_bridgerail_l` | bridge railings |
| 0x0007, 0x4007, 0x00A7 | 5.0 / 2.0 | `s_wall_veg` | SF retaining walls |

Inferred meanings used by the builder: bit 0 = left wall, bit 1 = right wall,
0x0100 = ceiling (present on every enclosed tunnel and no railing). The other
bits (0x04..0x80, 0x0200..0x1000, 0x4000) and the role of `height2` are
unknown. Junction walls are built along perimeter edges whose bit is set in
any mask word.

## Drawing (sdlPage16::Draw)

MM2 draws the PSDL in `sdlPage16::Draw` (immediate mode, four levels of
detail chosen per room by `cityLevel::DrawRooms`, the primitives coloured by
the room colour or `GetShadedColor`). `src/city/SdlDraw.{h,cpp}`
(`buildSdlRoomDraw`, `sdlArcMap`, `sdlRoomCentroid`, `sdlRoomBoundSphere`,
`sdlRoomLod`, `sdlBackface`) ports it, with `sdlPage16::ArcMap`,
`GetCentroid`, `ComputeBoundSphere` and `sdlCommon::BACKFACE`, and
`CityRenderer` draws its primitives:

- Road and divided road strips: level 0 one strip from outer edge to outer
  edge with the group's third texture over every other section; level 1 the
  same over every section with the outer edges lowered 0.15 m; levels 2 and
  3 the sidewalks (second texture) and the road (first, in two halves
  mirrored about the centre line) separately, level 3 with the curb line
  raised 0.15 m and half-bright curb faces. `ArcMap` gives s along the strip
  (whole repeats of about the average width, run back and forth) and t
  across. Dividers by type (flat, raised with bevels, wedged) at levels 2
  and 3.
- Sidewalk strips: planar 4 m repeats; curb faces and end caps half bright
  at level 3.
- Crosswalks, road fans and roofs: drawn only when not above the camera;
  fans and roofs planar 8 m repeats.
- Facades (repeats read unsigned, v 0 at the bottom) and slivers: back-face
  culled and shaded by the light of the last FacadeBound attribute.
- An untextured flat-colour facade path at level 0 hangs on a switch that is
  never set.
- Tunnels (words: flags, height in 8.8, an unused word; a junction's ten
  words add its first ceiling corner and three edge masks). A junction
  (count 10, nothing when the height is 0): walls on the perimeter edges of
  the first mask (u = max(1, length / height)), inner walls too with flag
  0x4000 unless 0x4; with 0x8 a ceiling fan (third texture) at the highest
  corner + height from the stored corner backwards; with 0x4 an apron 1 m
  below the highest corner (sixth texture), its corners pushed out by
  height x 0.333 beside a wall (0.25 m elsewhere), and on each walled edge
  a railing face and top (fifth texture) whose ends the second and third
  masks bevel (x 1.414). A strip tunnel follows the next attribute (past a
  Texture attribute): a road, divided road or rectangle strip's outer
  edges. 0x1/0x2 left/right walls (first/second texture; 0x4000 both sides;
  0x2000 bulging out to height x 0.333 at a quarter and three quarters of
  the height, capped by 0x10/0x20 and 0x40/0x80); 0x4 railings outside
  them (fourth/fifth texture, ends pushed along the road by 0x200/0x400 and
  0x800 — MM2's right-hand loop never reaches the last section, so 0x1000
  does nothing there — and a deck between them, sixth texture); 0x8 a flat
  ceiling, 0x100 an arch rising 1.5 m at the quarters and 2 m in the middle
  (both third texture, `ArcMap` across). Walls take `WallMap` coordinates
  (whole repeats of the height along the left edge). Drawn at every level.
- An untextured primitive list (`GetDrawnSDLPrims`) is not called anywhere
  in the executable.

## Geometry builder (CityMesh)

`buildCityMesh()` produces per-room batches keyed by (texture index, surface
kind), CCW front faces, with flat normals and Direct3D-style UVs. The game
uses it for the static probe soup (`World::probe`: line-of-sight tests;
the wheels probe the collision polygons below) and the minimap; mm2tool
exports it. The following
parts are *reconstructions*, not known original behaviour:

- UVs: road and rectangle strips run u 0→1 across, v along the length / width.
  Fans and roofs use planar world mapping at 8 m per repeat. Facades use
  their repeat counts. Slivers use their texture density (see below).
- Curbs: the raised edge of a sidewalk is the curb vertex lifted to the
  outer vertex's height. The curb face connects the two.
- Crosswalks and flat medians are lifted 1 cm to avoid z-fighting.
- Tunnel walls rise `height1` (or `max(height1, height2)` with a ceiling). Railings
  are double-sided.
- Low-detail textures are not used.

CityMesh differs from what MM2 draws (see "Drawing" above):

- Levels of detail (road strips): 0 draws one strip from outer edge to
  outer edge with the group's third texture (road LOD) over every other
  section; 1 the same strip over every section with the outer edges lowered
  0.15 m; 2 and 3 draw the sidewalks (second texture) and the road (first)
  separately, and only level 3 raises the curb line by 0.15 m and adds the
  curb faces, at half brightness. The builder raises curbs to the outer
  vertex height.
- Road and rectangle strips: `ArcMap` texture coordinates: t is 1 at the
  curbs and 0 at the road's centre line (the texture mirrored about it),
  s the distance along the strip scaled to a whole number of repeats of
  about the strip's average width and run back and forth (each segment's s
  added while the running value is not positive, subtracted while it is).
- Sidewalk strips: planar 4 m texture repeats (x / 4 and z / 4, offset by
  the whole repeats at the first vertex); curb end caps are half-bright
  triangles.
- Crosswalks: texture (1, 0) and (0, 0) on the first pair, v = the length
  over the width on the second pair. Road fans, crosswalks and roofs are
  only drawn when they are not above the camera.
- Facades: u and v are the stored repeats read *unsigned*, v 0 at the
  bottom and the repeat at the top. Slivers: u = round(length x density),
  v = (vertex height - top) x density. Both are back-face culled and
  coloured by the light of the last FacadeBound attribute (its first
  word indexes `sdlCommon::sm_LightTable`).
- Fans and roofs: planar 8 m repeats, as the builder (MM2 subtracts the
  whole repeats at the first vertex, which wrap addressing ignores).

## Collision polygons (sdlPage16::Collect)

Port: `src/city/SdlCollect.{h,cpp}` (`collectRoomPolygons`,
`sdlTextureMaterials`, `sdlMaterialIndex`). Everything in this section was read
from MM2 build 3393 (MM2Recomp) unless marked *inferred*. Functions:
`sdlPage16::Collect`, `sdlPage16::FindBoundingIsoParams`, `sdlPoly::InitNoArea`,
`sdlPoly::SetQuad` (two overloads), `SetFlatQuad`, `SetTri`, `SetFlatTri`,
`SetWall` (two overloads), `lvlSDL::LoadBinary`, `lvlSDL::CollidePolyToLevel`,
`lvlSDL::CollideProbe`, `sdlPage16::CollideSegment`,
`lvlLevelBound::GetMaterial`, `lvlMaterialMgr::Load`.

The level's collision polygons are not the render mesh. `Collect` generates
them per room on demand from the attribute list, with its own rules: which
attributes collide, how curbs, medians, tunnel walls and railings become
walls, and a crude cull against a query sphere.

### Callers

- `lvlSDL::CollidePolyToLevel` (bodies against the city): for each room the
  body touches, with the sphere (body matrix position, bound radius
  bounding-sphere radius) into a static buffer of 256 phPolygons, capacity 256 minus
  what earlier rooms added. One `int` state (initially 0) is shared by all the
  rooms of a call; whenever it is nonzero after a room it reports
  "CollidePolyToLevel: buffer overflow" and carries on (the next room then
  *starts* at that state's offset: an original quirk after an overflow).
- `sdlPage16::CollideSegment` (wheel probes, from `lvlSDL::CollideProbe`):
  sphere = the segment's midpoint with radius 0.51 x its length, capacity
  256, calling again with the state until it is 0 ("Primitive too large"
  when the state does not advance).
- Before either, the generated-vertex part of lvlSDL's vertex array is reset:
  `sdlPoly::sm_Count` = PSDL vertex count, budget counter = 0x200.
  lvlSDL::LoadBinary allocates exactly 0x200 spare vertices; the counter is
  decremented per generated vertex and never checked. With car-sized spheres
  the retail cities stay far below it; a whole building block without culling
  can generate over 1,200.

### Polygons

`sdlPoly::InitNoArea(material, a, b, c, d)` stores the vertex indices (16-bit;
`d == 0` makes it a triangle, so a quad whose fourth corner is vertex 0 is a
triangle) and the material byte (low byte of phPolygon's area field). The normal is exactly
(0, 1, 0) when a, b and c have the same y (bit-equal floats), whatever the
winding; otherwise `normalize((c - b) x (a - b))` (32-bit floats, |n|² summed
y, x, z). A polygon whose normalized normal has |n|² < 0.9 (zero area) is
rejected: not counted, though any vertices generated for it stay. InitNoArea
also computes the edge normals (`phPolygon::ComputeEdgeNormalCross`).

| Setter | Vertices | Winding |
|--------|----------|---------|
| `SetQuad(m, v0,h0, v1,h1, v2,h2, v3,h3)` | a new vertex (x, y + h, z) for each h ≠ 0. If v1 == v3: triangle v0 v1 v2 (v2 takes h3's place); else if v0 == v2: triangle v0 v1 v3 | v0 v1 v3 v2 |
| `SetFlatQuad(m, a, b, c, d, y)` | a new vertex at height y for each corner not already at y | a b d c |
| `SetQuad(m, p0, p1, p2, p3)` | 4 new | p0 p1 p3 p2 |
| `SetTri(m, p0, p1, p2)` | 3 new | p0 p1 p2 |
| `SetFlatTri(m, a, b, c, y)` | as SetFlatQuad | a b c |
| `SetWall(m, a, b, ya, yb)` | a' = (a.x, ya, a.z), b' (absolute heights) | a b b' a' |
| `SetWall(m, pa, pb, ya, yb)` | pa, pa', pb, pb' all new | pa pb pb' pa' |

Strip quads (v0, v1 = section k; v2, v3 = section k + 1) therefore wind
across-then-along.

### Sphere culling

All tests are in the xz plane, with r² = radius² computed once. Without a
sphere nothing is culled.

- Quad or wall with test corners p, q: kept when |S - p|² < 2 (|p - q|² + r²).
- Triangle a, b, c: with ab, bc, ca the squared edge lengths and m the
  largest, kept when |S - v|² < 2 (m + r²), where v = c if ab <= bc, else a.
- Flat fan or roof at height y: kept when (S.y - r - y)(S.y + r - y) < 0.
- Strips first narrow their sections with `FindBoundingIsoParams`: per
  section a cross line (two of its vertices); a binary search for a section
  whose line passes within the radius of the centre (signed xz distance,
  assuming the distance decreases along the strip), then widening backwards
  until a line is >= r on the positive side and forwards until one is <= -r.
  The quads between those bounding sections are tested; strips of fewer than
  3 sections are not narrowed. (A global scale on r is 1.0 and its enable flag
  is set in the retail executable.)

These are coarse: a 2 km water triangle in SF is "near" a sphere 500 m outside
the city.

### The walk

`Collect` starts at the state's word offset (`state >> 11`) with the current
texture `state & 0x7ff` but with the "no texture" flag clear, and stops after
the attribute with bit 7 set: Texture attributes stored after it are never
read. Every attribute whose subtype is 0 has its count word consumed (true of
all count types; the fixed-size types always have a nonzero subtype in the
retail files). Before every polygon it tries, it decrements the capacity,
including polygons InitNoArea then rejects; when that goes negative it writes
the state of the attribute it is in (`word offset << 11 | texture`, or 0 if
that attribute is the first one processed in this call) and returns the
count. It does not write the state when it finishes.

"tex" below is the current Texture value (1-based; materials come from the
table below, indexed by tex plus an offset).

| Attribute | Collision polygons | Material |
|-----------|--------------------|----------|
| Texture | sets tex; value 0 sets "no texture" | |
| RoadStrip | three passes over sections (cross lines outer L-curb L, curb L-curb R, curb R-outer R): left sidewalk quad (curb corners raised 0.15) and curb wall (curb vertex to +0.15) when outer != curb; road quad; right sidewalk and curb wall | sidewalk, curb: tex+1; road: tex |
| SidewalkStrip | per pair step: walk quad (curb raised 0.15) and curb wall (next curb to this curb). Two pairs with first pair (0,0)/(1,1): one end-cap triangle (curb+0.15, outer, curb) resp. (curb, outer, curb+0.15) | tex+1 |
| RectangleStrip | quads | tex |
| Sliver, Facade | nothing | |
| Crosswalk | flat quad (w1, w0, w2, w3) at w0's height | tex+2 |
| RoadTriangleFan | flat triangles at the hub's height; skipped when material(tex) == 2 | tex |
| TriangleFan | triangles as stored; skipped when material(tex) == 2 | tex |
| FacadeBound | wall from the two vertices up to the top height (even with no texture) | tex |
| DividedRoadStrip | left sidewalk and curb; per section left road, median, right road; right sidewalk and curb. Median (flags & 0x3f) == 1: flat quad at road level; otherwise two walls up the divider height (median R/L edges) and a top quad at that height | sidewalk, curb, road as RoadStrip; flat median: divider+1; raised top: divider+2; median walls: tex+1 |
| Tunnel | see below | tex (at the tunnel) |
| RoofTriangleFan | flat triangles at the height (the word after the count word, if any); no texture check | tex |

All strip, crosswalk and fan types produce nothing while "no texture" is set
(Texture value 0: 120 attributes in London, 1 in SF). FacadeBound,
Tunnel and roofs ignore it. Material 2 is "deepwater" with the retail
materials (`s_thames`, `s_ocean`): those water fans have no collision, so
cars sink (*inferred* intent; the code compares with 2).

Tunnels: walls are max(height2, 3 m) above their base vertex (height2 <= 3
gives 3; railings with height2 = 1 m are 3 m walls). Junction tunnels (count
10): a wall along perimeter edge (point j, point j-1) for every set bit j
(j & 31) of words 4-5 read as a 32-bit mask. Strip tunnels (count 3) describe
the next attribute, skipping one Texture attribute: RoadStrip (stride 4),
DividedRoadStrip (6, after its 2 header words) or RectangleStrip (2); its
section count is the subtype or the count word's low byte. Flags (names
*inferred* from their use):

- 0x0001 / 0x0002 left / right, 0x0004 railing.
- Without 0x2000: walls along the strip's outer edges for each side set;
  with the railing bit, walls along a railing line (each section's outer
  vertex moved height1 x 0.333 outwards, away from its neighbour vertex), with
  start/end caps back to the road edge (left 0x10/0x20, right 0x40/0x80).
- With 0x2000 ("sloped", both lines computed): per section and side set, a
  quad from the road edge to the railing line raised by a quarter of the wall
  height, and a wall on the railing line. 0x200/0x400 (left) and 0x800/0x1000
  (right) pull the first/last line point back onto the road edge.
- No ceiling polygons. Other tunnel types (count not 3 or 10) produce
  nothing.

In a room with `RoomFlag::SpecialBound` (0x20), while `lvlSDL::CollideProbe`
collides that room (it stores the room id in a global; `CollidePolyToLevel`
does not), road, rectangle and divided strips use two triangles per quad
(`SetTri` with vertex copies, sidewalk corners raised 0.15) instead of quads,
all with the material of tex+1; the median top stays at road level.

### Materials

`lvlSDL::LoadBinary` builds the texture -> material table: for each row of
`city/materials.csv` (header line skipped, two fields) whose material is not
`none` and that `lvlMaterialMgr::Find` knows, texture name -> (manager index
+ 1); the first row for a name wins. Texture i of the PSDL (1-based, as Texture
values count) gets its name's entry, 0 if none; a name ending in a movie frame
suffix `-0nnn` is looked up without it (`s_thames-0009`). The polygon stores
that byte; `lvlLevelBound::GetMaterial` maps 0 to the default material and n
to `lvlMaterialMgr::Lookup(n - 1)`. The manager's entry 0 is its built-in
"default" material; `cityLevel::Load` then adds `city/materials.mtl` in file
order (`_default` is a new name, not the built-in one), so the retail indices
are deepwater 2, _default 3, grass 4, water 5, dirt 6, sand 7, cobblestone 8,
wood 9. `materials.csv` also names `mud` and `ash`, which the .mtl lacks: 0.

### Port notes

Memory-safety deviations only, none reachable with the retail files (checked
by `tests/city/test_sdlcollect.cpp` and a scan of every attribute): reads past
the vertex, height or material tables give 0; a state that does not point at
an attribute boundary returns nothing; a strip tunnel with no following
attribute, or followed by a type that leaves its stride unset, produces
nothing (MM2 would read past the room / use a stale stride); MM2's skip of a
DividedRoadStrip under texture 0 (5 words per section instead of 6) is not
reproduced. The vertex buffer grows instead of overrunning, and indices are
kept 32-bit.

## Verification

- `mm2tool citycheck` parses every city/race file (0 failures among files the
  game references).
- `tests/city/test_city_retail.cpp`: index validation, symmetric neighbours,
  bounds, mesh sanity (finite data, unit normals, upward road/roof normals).
- `tests/city/test_sdlcollect.cpp`: collision polygons for synthetic rooms;
  for every retail room, car-sized spheres at its perimeter corners collect
  within 256 polygons and 0x200 generated vertices, unit normals, a subset of
  the unculled set; a road polygon under the middle of every road room's
  first road quad. Unculled, 8 London and 33 SF building blocks exceed 256
  polygons (up to 478 / 626).
- Visual: `psdl2obj` + a top-down raster of London and SF (kept under
  `local/out/`) shows the street grids, roundabouts, parks, the Tower of London
  walls, Market Street and the Golden Gate Bridge.
