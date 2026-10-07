# TEX — Angel textures (`texture/*.tex`)

Parser: `src/asset/Image.{h,cpp}` (`asset::parseTex`). Retail coverage:
3665/3665 files in MM2TEX.AR decode (P8 2931, PA8 139, RGB888 300,
RGBA8888 295); every file's size equals header + palette + mip chain exactly.

## Layout (little-endian)

| Offset | Type | Field | Status |
|-------:|------|-------|--------|
| 0  | u16 | width | verified |
| 2  | u16 | height | verified |
| 4  | u16 | format (below) | verified |
| 6  | u16 | mip level count | verified (level sizes halve, min 1) |
| 8  | u16 | reserved, always 1 | observed |
| 10 | u32 | flags ("TexEnv", below) | partly inferred |
| 14 | 256 × 4 bytes | palette, paletted formats only | verified |
| …  | | mip levels, largest first, packed | verified |

### Formats

| Value | Name | Texel | Notes |
|------:|------|-------|-------|
| 1  | P8 | 1 byte palette index | palette alpha is 0xFF for every referenced entry |
| 14 | PA8 | 1 byte palette index | palette alpha used (trees, glass, cut-outs) |
| 15 | P4 | 4-bit index | not in retail data; assumed 16-entry palette, low nibble first (untested) |
| 16 | PA4 | 4-bit index | as P4 |
| 17 | RGB888 | 3 bytes R,G,B | verified (dusk sky has an orange horizon) |
| 18 | RGBA8888 | 4 bytes R,G,B,A | verified (stop sign is red) |

**Palette entries are B,G,R,A** (Windows RGBQUAD order), unlike the true-colour
formats. Verified: `vpbugyellow_sd` decodes yellow only with this order.

### Row order

Every level is stored **bottom row first**. The game's UVs use v = 0 for that
first row: the stop-sign face in `geometry/sp_stop_f.pkg` maps v = 1 to its
top edge, and the dusk sky `sky_cd_f` has its horizon in row 0. `asset::Image`
keeps rows in this order (row 0 = v 0); upload rows unchanged and use the
game's UVs directly. `encodePng` flips for display.

### Flags

Low bits match the Angel engine's `agiTexParameters` flags in Open1560
(`Alpha = 0x1`, `WrapU = 0x2`, `WrapV = 0x4`, `KeepLoaded = 0x8`,
`NoMipMaps = 0x10`, `Chromakey = 0x40`). Evidence from texture names:

| Bits | Count | Typical textures | Reading |
|------|------:|------------------|---------|
| 0x2 | 1533 | building facades | WrapU (facades tile horizontally) |
| 0x6 | 10 | `s_concrt`, `s_steps`, `s_groundbrick01` | WrapU + WrapV |
| 0x7 | 5 | `decal_zigzag*` | Alpha + WrapU + WrapV |
| 0x8006 / 0x18006 | 153 | `r1_*`, `r2_*` road surfaces | wrap both + unknown high bits |
| 0x10001 | 940 | car paint, trees, cut-out cards | Alpha + 0x10000 |
| 0x18001 | 278 | car rears, dashboards | Alpha + 0x8000 + 0x10000 |
| 0x8000 | 44 | `fxpt*` particles, steering wheel | unknown |
| 0x10000 | 35 | skies, fences | unknown |
| 0 | 639 | mixed | — |

The 0x1 bit does not track texel alpha: many 0x10001 car textures are opaque
P8. It probably marks materials drawn in the alpha pass. 0x8000 and 0x10000
have no counterpart in MM1; their meaning is unknown (candidates: no LOD
reduction, clamp, colour-key). Treat as unknown until observed in game.

## Placeholder data

`r1_grass.tex` and `s_grassndirt.tex` are P8 images whose every texel is
palette entry 0 (white). They are blank in the shipped data too; the real
ground uses `r1_grass_f` (RGB888) etc.

## Texture lookup

Material texture names in PKG files are base names. 2215 of 2234 referenced
names exist as `texture/<name>.tex` (or `.tga`). The rest are animated
(`s_ocean`, `s_pond`, `s_thames` → `tune/<name>.movie`), damage variants of
`vppanozgt` that were not shipped, and a few street surfaces. The lookup
order the game uses (.tex before .tga?) is not yet confirmed.
