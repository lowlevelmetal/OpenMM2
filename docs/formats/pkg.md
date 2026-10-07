# PKG — model packages (`geometry/*.pkg`)

Parser: `src/asset/Pkg.{h,cpp}` (`asset::parsePkg`); multi-part helper
`asset::loadVehicleModel` (VehicleModel.h). Retail coverage: 1037 of 1040
files parse; the three failures are broken in the shipped data and never
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

The parser falls back to parsing a PKG3 chunk structurally when its size is
wrong.

## Chunk names

* Geometry: `<PART>_<LOD>` or bare `<LOD>`, LOD ∈ H, M, L, VL (high to very
  low detail). Cars use parts BODY, SHADOW, HLIGHT, TLIGHT, RLIGHT, BLIGHT,
  HEADLIGHT0/1, WHL0–WHL5, SLIGHT0/1, SIREN0/1, SRN0–3, FNDR0/1, BREAKnn,
  TRAILER, TRAILER_HITCH, TWHL0–5; city lights use RED/YELLOW/GREENGLOWDAY/NIGHT
  and WALK/NOWALK_DAY/NIGHT; dashboards (`<car>_dash.pkg`) use dash, roof,
  wheel, speed_needle, tach_needle, damage_needle, gear_indicator. Not every
  part has every LOD (lights often only L, headlight flares only H).
* `shaders`: material table.
* `offset`: Vec3. For city objects it is the world position the mesh was
  centred on (equal to the `(null).mtx` origin). In car packages it repeats one
  part's pivot, an exporter artefact that appears unused.
* `xrefs`: placed references to other models.

## Geometry chunk

```
u32 sectionCount
u32 totalVertices        sum over all packets (checked; warning on mismatch)
u32 totalIndices
u32 sectionCount2        repeats sectionCount
u32 fvf                  Direct3D 7 FVF: 0x112 (xyz|normal|tex1) or 0x102 (xyz|tex1)
section[sectionCount]:
  u16 packetCount
  u16 flags              always 0
  u32 shaderIndex        index into a paint job's material list
  packet[packetCount]:
    u32 primitive        always 3 = indexed triangle list
    u32 vertexCount
    vertex[vertexCount]  FVF layout: float3 position, [float3 normal], float2 uv
    u32 indexCount       multiple of 3
    u16 index[indexCount]
```

Positions are in model space (cars: Y up, facing −Z; city objects: centred
on `offset`). Parts with a pivot (`<model>_<part>.mtx`) are centred on their
own origin; add the pivot to place them (see mtx.md). UVs follow the game
convention v = 0 at the bottom of the texture (see tex.md).

## Shaders chunk

```
u32 shaderType           bits 0–6: paint job count; bit 7: compact materials
u32 shadersPerPaintjob
material[paintjobs][shadersPerPaintjob]:
  u8   nameLength (includes NUL; 1 = untextured)
  char textureName[nameLength]
  compact (bit 7 set):   u8[4] diffuse, u8[4] ambient, u8[4] third colour, f32 power
  full (bit 7 clear):    f32[4] diffuse, ambient, specular, emissive; f32 power
```

Full materials follow Direct3D's D3DMATERIAL7 order (verified values: diffuse
1, ambient 1, specular 0.9, emissive 0, power 0.25). For compact materials the
byte order R,G,B,A and the meaning of the third colour are **inferred**: light
glow materials (`fxltglow`, headlight colour `fff8ae`) store black diffuse and
ambient and their colour in the third slot with power 0, so the third colour is
treated as specular (D3D order) and behaves like a glow. Paint jobs are in the
order of `Colors=` in `tune/<car>.info` (vpbug: Yellow, Blue, Silver, Red →
`vpbugyellow_*`, `vpbugblue_*`, `vpbuggrey_*`, `vpbugred_*`).

## Xrefs chunk

```
u32 count
xref[count]: Matrix34 (12 floats: rows m0, m1, m2, position), char name[32]
```

Used by 33 city packages to place props (e.g. `sp_light_red_f`, trees).
Bytes after the name's NUL are uninitialised.
