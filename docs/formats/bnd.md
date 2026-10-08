# Collision bounds (`bound/*.bnd`, `*.bbnd`, `*.ter`)

Parser: `src/asset/Bound.{h,cpp}`. Retail coverage: 525/525 `.bnd`, 324/324
`.bbnd`, 184/184 `.ter`. Every `.bbnd` holds exactly the same vertices and
polygons as the `.bnd` of the same name (checked for all 324). Note that
`aud/dmusic/*.bnd` are unrelated DirectMusic band files.

## BND (text)

```
version: 1.01
verts: N
materials: M
edges: 0
polys: P

v <x> <y> <z>                 × N
mtl <name> {
    elasticity: <f>
    friction: <f>
    effect: <name>            "none" in all files
    sound: <name or 0>
}                             × M
quad <a> <b> <c> <d> <mtl>
tri  <a> <b> <c> <mtl>        × P
```

Material names in retail data: default, grass, cobblestone, water, deepwater,
sand, mud. All files declare 0 edges.

How the game reads it (`phBoundGeometry::Load`; `src/phys/Bound.cpp`
`makeGeometryBound`):

* The tokens are read in a fixed order: `version:` (anything but 1.01 is an
  error), `verts:`, `materials:`, `edges:`, `polys:`, then the vertices, the
  materials (`lvlMaterial::Load`, which also accepts the optional
  `drag:`/`width:`/`height:`/`depth:`/`ptxindex:`/`ptxthreshold:` keys of
  `city/materials.mtl`; no bound file uses them), `edges:` × `edge a b`, and
  the polygons. Zero vertices or zero polygons fail the load. The parser
  accepts the same tokens in any order; every retail file has them in the
  game's order. A declared edge would be listed before the computed ones;
  OpenMM2 ignores declared edges (no retail file has any).
* `quad a b c d` is a quad with vertices (a, b, c, d); when d is 0 the game
  starts it at b instead, (b, c, 0, a), so that it stays a quad (a `phPolygon`
  is a triangle exactly when its fourth index is 0). `tri a b c` is a
  triangle.
* Materials are looked up by name in the material manager
  (`lvlMaterialMgr::Load`): a name it does not know yet is added with the
  file's values; `default` is the manager's default material, whatever the
  file says. Cars (`vehBound`) and props (`dgBoundGeometry`) give every
  polygon their own single material instead.
* `PostLoadCompute`: the edges are listed polygon by polygon, edge
  (v[n−1], v0) first, then (v0, v1), ... (an edge already listed is not added
  again); each polygon then records the index of its edge (v[i], v[i+1]);
  each edge's normal is the normalised sum of two face normals
  (`ReComputeEdgeNormals`): the polygons are scanned in order until a face
  running the edge forwards and one running it backwards have both been
  seen, each side keeping the last face found before then; a side with no
  face mirrors the other. When the sum nearly vanishes (squared length below
  1e-6) the normal is the backward face's normal crossed with the edge, or
  (0, 1, 0). The cosine is the backward face's normal dotted with the edge
  normal, or 2.0 when the faces fold inwards; an edge no polygon uses gets
  normal (1, 0, 0) and cosine −1.
* Binary bounds (`LoadBinary`) load their materials through
  `phMaterialMgr::Load(Stream*)`: a known name returns the manager's
  material, an unknown one is added as a plain `phMaterial` (elasticity and
  friction only). The version byte is not checked.
* The car bounds and the city objects flagged collidable (`.inst` flag
  0x2000) use the text file; the terrain bounds (flag 0x100) read the `.bbnd`
  with the `.ter`.

## BBND (binary, little-endian)

```
u8  version = 1
u32 vertexCount, materialCount, polygonCount
float3 vertex[vertexCount]
material[materialCount]: char name[32], f32 elasticity, f32 friction, char effect[32], char sound[32]
polygon[polygonCount]:   u16 index[4], u16 material
```

A polygon is a quad exactly when `index[3] != 0`; triangles store 0 there
(this is also how the game's `phPolygon` tells them apart: `LoadBinary`
copies the four indices as they are and `CalculateNormal` /
`ComputeEdgeNormalCross` treat a zero fourth index as a triangle). The
material is a u16 of which the polygon keeps the low byte.

## TER (terrain acceleration grid, little-endian)

Pairs with the `.bbnd` of the same name; the polygon count always matches.

```
f32  version = 1.1
u32  polygonCount
u32  edgeCount
u8   useHotEdges
f32[3] size of the box
u32  widthSections (x), heightSections (y), depthSections (z)
u32  sectionCount = w*h*d
u32  referenceCount
f32[3] sectionSizeFactors   sections per unit length; NaN on a zero-size axis
f32[3] min, f32[3] max
u16  sectionOffset[sectionCount]
u16  sectionCount[sectionCount]
u16  sectionPolygon[referenceCount]
u16[2] edge[edgeCount]           vertex index pairs
u32[4] polygonEdges[polygonCount] edge indices of each polygon
f32[3] edgeNormal[edgeCount]
f32    edgeValue[edgeCount]
```

* `phBoundTerrain::Load` reads the `.bbnd` when it exists, else the `.bnd`
  (without computing edges), then the `.ter`. A version other than 1.1 or a
  polygon count that differs from the geometry's is an error: the game then
  computes a plain geometry bound, grows its box by 0.0001 and reports
  failure, and `lvlInstance` drops the bound. The box stored in the file is
  replaced by the box of the vertices (`SetQuickTestInfo`). Each
  `polygonEdges` entry is a u32 of which the game keeps the low 16 bits.
  OpenMM2 does not check the version (every retail file is 1.1).
* Section index = (z·h + y)·w + x with cell = floor((p − min) · factor). This
  is verified by polygon centroids (7618 of 7619 land in a section that lists
  them). The game's queries (`phBoundTerrain::InitPolyIterator`,
  `CalculateBuckets`) use only x and z, section = w·z + x, and test y against
  the box alone (the two agree when h = 1, as in nearly every retail grid).
* Edge normal and value match MM1's `mmBoundTemplate::ComputeEdgeNormals`
  (Open1560 `game.asm`): the normal is the normalised sum of the two adjacent
  face normals, and the value is the cosine between it and a face normal, or
  exactly 2.0 as a sentinel. 3740 of 19727 retail edges are 2.0; all others
  lie in [−1, 1].
* MM1 used a different binary format (magic `"2DNB"`) and a 2D grid, so only
  the concepts carry over.
