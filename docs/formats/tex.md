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

MM2's loader (`gfxLoadTexImage`) keeps the word as the image's texture
environment and `gfxTexture::Create` copies it to the texture. The renderer
(`gfxRenderState::DoFlush`) reads two bits of it: **0x1 clamps U** and
**0x10000 clamps V**; without them the texture repeats. Nothing in the
renderer reads 0x2, 0x4 or 0x8000 (Open1560's MM1 `agiTexParameters` names
0x1/0x2/0x4 Alpha/WrapU/WrapV; MM2 does not use that meaning). Whether a
texture is drawn in the alpha pass comes from its pixel format instead
(`gfxTexture::Create` sets its own bit 0x20000 for formats with alpha).

| Bits | Count | Typical textures | MM2 address modes |
|------|------:|------------------|-------------------|
| 0x2 | 1533 | building facades | repeat both |
| 0x6 | 10 | `s_concrt`, `s_steps`, `s_groundbrick01` | repeat both |
| 0x7 | 5 | `decal_zigzag*` | clamp U |
| 0x8006 | 95 | `r_alley`, `rinter_*`, `rxwalk*`, `s_grass` | repeat both |
| 0x18006 | 58 | `r1_*`, `r2_*`, `r4_*`, `r6_*` road surfaces | clamp V |
| 0x10001 | 940 | car paint, trees, cut-out cards | clamp both |
| 0x18001 | 278 | car rears, dashboards | clamp both |
| 0x8000 | 44 | `fxpt*` particles, steering wheel | repeat both |
| 0x10000 | 35 | skies, fences | clamp V |
| 0 | 639 | mixed | repeat both |

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
