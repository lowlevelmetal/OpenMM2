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
    u8   flags
    u8   unknown
    u16  propRule                               inferred name
    u8   leftCount, rightCount
    f32  leftValues[leftCount], rightValues[rightCount]   fractions in (0,1); inferred prop spacing
    u8   unknown2, unknown3                     values 0/1/128/129 (two bit flags)
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

## Geometry builder (CityMesh)

`buildCityMesh()` produces per-room batches keyed by (texture index, surface
kind), CCW front faces, with flat normals and Direct3D-style UVs. The
following parts are *reconstructions*, not known original behaviour:

- UVs: road and rectangle strips run u 0→1 across, v along the length / width.
  Fans and roofs use planar world mapping at 8 m per repeat. Facades use
  their repeat counts. Slivers use their texture density (see below).
- Curbs: the raised edge of a sidewalk is the curb vertex lifted to the
  outer vertex's height. The curb face connects the two.
- Crosswalks and flat medians are lifted 1 cm to avoid z-fighting.
- Tunnel walls rise `height1` (or `max(height1, height2)` with a ceiling). Railings
  are double-sided.
- Low-detail textures are not used yet.

## Verification

- `mm2tool citycheck` parses every city/race file (0 failures among files the
  game references).
- `tests/city/test_city_retail.cpp`: index validation, symmetric neighbours,
  bounds, mesh sanity (finite data, unit normals, upward road/roof normals).
- Visual: `psdl2obj` + a top-down raster of London and SF (kept under
  `local/out/`) shows the street grids, roundabouts, parks, the Tower of London
  walls, Market Street and the Golden Gate Bridge.
