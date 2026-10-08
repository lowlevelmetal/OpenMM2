# PKG — model packages (`geometry/*.pkg`)

Parser: `src/asset/Pkg.{h,cpp}` (`asset::parsePkg`); multi-part helper
`asset::loadVehicleModel` (VehicleModel.h). **Verified** against MM2's
readers (MM2Recomp, build 3393): `modPackage` (container), `modGetStatic`
(geometry), `modShader::LoadShaderSet` / `modShader::Load` (materials),
`lvlInstance::GetGeomSet` / `EndGeom` (LODs, xrefs). Retail coverage: 1037 of
1040 files parse; the three failures are broken in the shipped data and never
loaded (`thing.pkg`, a dev file with zeroed sizes/counts, and two zero-byte
files). Of 4669 geometry chunks, 4667 match their declared size and header
totals exactly; `vpvw_dune.pkg` has garbage bytes over two chunk headers and
parses with warnings.

Verified visually by exporting OBJ (`mm2tool pkg2obj`) and rendering the
Beetle, the SF police car, the fire truck, the London IMAX and St Paul's.

## Container

```
char[4] magic        "PKG3" (1036 files) or "PKG2" (2 files: sky_dawn, vpdb731)
repeat until EOF:
  char[4] "FILE"
  u8      nameLength (includes the NUL)
  char    name[nameLength]
  u32     chunkSize          PKG3 only; PKG2 chunks must be parsed to find their end
  byte    body[chunkSize]
```

MM2 reads a package sequentially (`modPackage::NextItem`): each loader asks
for the chunk it expects next by name (case-insensitive) and gets nothing if
the current chunk has another name; `Skip`/`SkipTo` jump over chunks using
the declared size (PKG3 only; `SkipTo` on a PKG2 file is fatal), and a
missing `FILE` tag is fatal. OpenMM2 parses every chunk up front and looks
them up by name, falling back to parsing a PKG3 chunk structurally when its
declared size is wrong (MM2 would misread such a file when it skips).

## Chunk names

* Geometry: `<PART>_<LOD>` or bare `<LOD>`, LOD ∈ H, M, L, VL (high to very
  low detail). Cars use parts BODY, SHADOW, HLIGHT, TLIGHT, RLIGHT, BLIGHT,
  HEADLIGHT0/1, WHL0–WHL5, SLIGHT0/1, SIREN0/1, SRN0–3, FNDR0/1, BREAKnn,
  TRAILER, TRAILER_HITCH, TWHL0–5 (see VehicleModel.h for the full list
  `vehCarModel::Init` asks for); city lights use RED/YELLOW/GREENGLOWDAY/NIGHT
  and WALK/NOWALK_DAY/NIGHT; dashboards (`<car>_dash.pkg`) use dash, roof,
  wheel, speed_needle, tach_needle, damage_needle, gear_indicator. Not every
  part has every LOD (lights often only L, headlight flares only H).
* `shaders`: material table.
* `offset`: Vec3. For city objects it is the world position the mesh was
  centred on (equal to the `(null).mtx` origin). In car packages it repeats one
  part's pivot. MM2 never reads this chunk (an exporter artefact); OpenMM2
  parses it for tools.
* `xrefs`: placed references to other models.

## LODs

`lvlInstance::GetGeomSet` loads the H, M, L and VL meshes of a part and then
fills gaps upwards only: a missing L takes VL, a missing M takes L, a missing
H takes M. A part with only a high LOD therefore has nothing to draw at the
lower levels (MM2 warns "Missing medium and low LOD"). `Pkg::findBest`
follows this rule.

## Geometry chunk

```
u32 sectionCount         MM2 keeps the low byte (see below)
u32 totalVertices        sum over all packets (checked; warning on mismatch)
u32 totalIndices
u32 sectionCount2        repeats sectionCount
u32 fvf                  Direct3D 7 FVF: 0x112 (xyz|normal|tex1) or 0x102 (xyz|tex1)
section[sectionCount]:
  u32 packetCount
  u32 shaderIndex        index into a paint job's material list; MM2 keeps the low byte
  packet[packetCount]:
    u32 primitive        drawn as D3DPRIMITIVETYPE primitive + 1; always 3 = indexed triangle list
    u32 vertexCount
    vertex[vertexCount]  FVF layout: float3 position, [float3 normal], [u32 diffuse], float2 uv
    u32 indexCount       multiple of 3
    u16 index[indexCount]
```

When bit 0x80 of the first word is set, the chunk holds no geometry: the
low 7 bits are the section count, followed by the u32 FVF and one shader
index byte per section (`modGetStatic`; no retail file uses this).

A stored diffuse vertex colour has red in its low byte; MM2 swaps red and
blue to get Direct3D's ARGB (no retail mesh has vertex colours). MM2 does not
check indices; OpenMM2 rejects an index outside its packet and a triangle
list whose length is not a multiple of three.

Positions are in model space (cars: Y up, facing −Z; city objects: centred
on `offset`). Parts with a pivot (`<model>_<part>.mtx`) are centred on their
own origin; add the pivot to place them (see mtx.md). UVs follow the game
convention v = 0 at the bottom of the texture (see tex.md).

## Shaders chunk

```
u32 shaderType           bits 0–6: paint job count; bit 7: compact materials
u32 shadersPerPaintjob
material[paintjobs][shadersPerPaintjob]:
  u8   nameLength (includes NUL; 0 = untextured)
  char textureName[nameLength]
  compact (bit 7 set):   u8[4] diffuse, u8[4] specular, u8[4] emissive, f32 power
  full (bit 7 clear):    f32[4] diffuse, ambient, specular, emissive; f32 power
```

Full materials follow Direct3D's D3DMATERIAL7 order. `modShader::Load`
rounds every diffuse, specular and emissive component down to a multiple of
1/32 (values below 0.05 become 0, above 0.95 become 1; e.g. a stored
specular 0.9 becomes 0.875). Compact colours are bytes times MM2's 1/255
constant, R,G,B,A order, not rounded; light glow materials (`fxltglow`,
headlight colour `fff8ae`) store black diffuse and their colour in the
emissive slot with power 0. In both forms the stored ambient colour is
discarded and replaced by the diffuse one. `asset::PkgMaterial` holds the
values as MM2 builds them. An untextured material's diffuse is scaled by a
global factor (0.5 at night) when it is loaded; that is left to the renderer.

Paint jobs are in the order of `Colors=` in `tune/<car>.info` (vpbug: Yellow,
Blue, Silver, Red → `vpbugyellow_*`, `vpbugblue_*`, `vpbuggrey_*`,
`vpbugred_*`). `LoadShaderSet` reads all of them but keeps at most a global
limit (9999 by default; the city loader sets it temporarily); the extra ones
are read and discarded.

The same table format is used by the pedestrian `.shaders` files
(`asset::parseShaderTable`).

## Xrefs chunk

```
u32 count
xref[count]: Matrix34 (12 floats: rows m0, m1, m2, position), char name[32]
```

Read by `lvlInstance::EndGeom` as `count` × 80 raw bytes (the chunk is
named "xrefs" with its NUL, six bytes). Used by 33 city packages to place
props (e.g. `sp_light_red_f`, trees): `lvlLevel::LoadInstances` places
each one with banger data as an unhit banger (see docs/bangers.md). Bytes after the
name's NUL are uninitialised.
