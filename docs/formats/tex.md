# TEX — Angel textures (`texture/*.tex`)

Parser: `src/asset/Image.{h,cpp}` (`asset::parseTex`). **Verified** against
MM2's reader `gfxLoadTexImage` (MM2Recomp, build 3393). Retail coverage:
3663 of the 3665 files in MM2TEX.AR decode (P8 2930, PA8 139, RGB888 299,
RGBA8888 295). The other two, `nl01_coit_top` (64 × 73) and
`sf_wall_brick03_blkfence_2_l` (650 × 597), are not a power of two in size;
MM2 rejects them ("Bad resolution") and nothing references them. Every file's
size equals header + palette + the declared mip levels, but 24 files declare
more levels than MM2 reads (see below).

## Layout (little-endian)

| Offset | Type | Field | Status |
|-------:|------|-------|--------|
| 0  | u16 | width (a power of two) | verified |
| 2  | u16 | height (a power of two) | verified |
| 4  | u16 | format (below) | verified |
| 6  | u16 | mip level count | verified |
| 8  | u16 | always 1; not read by MM2 | verified |
| 10 | u32 | flags ("TexEnv", below) | verified |
| 14 | 256 × 4 bytes (16 × 4 for 4-bit) | palette, paletted formats only | verified |
| …  | | mip levels, largest first, packed | verified |

`texImage_CheckRes` requires both sides to be powers of two (its bit test
also lets 0 through; OpenMM2 rejects a zero side). On failure, or for an
unknown format, MM2 logs an error and the texture loader moves on to the next
image type (below).

**Mip levels.** `gfxImage::Create` builds the chain and adds a level only
while both sides are above 1, halving each time; the reader fills at most
that many levels. A 32 × 32 texture therefore has at most 6 levels and a
128 × 16 one at most 5, whatever the header says (24 retail files declare
one or more extra levels, which MM2 never reads). A count of 0 reads the
whole chain. When the caller does not want mipmaps only the first level is
read.

### Formats

| Value | Name | Texel | MM2 image | Notes |
|------:|------|-------|-----------|-------|
| 1  | P8 | 1 byte palette index | RGB888 | the palette's alpha is ignored (drawn opaque) |
| 2  | P8A8 | index byte, then alpha byte | RGBA8888 | not in retail data |
| 6  | ARGB1555 | u16: A1 R5 G5 B5 | ARGB1555 | not in retail data; OpenMM2 expands 5-bit channels by bit replication (inferred: done by the driver) |
| 14 | PA8 | 1 byte palette index | RGBA8888 | palette alpha used (trees, glass, cut-outs) |
| 15 | P4 | 4-bit index, low nibble first | RGB888 | 16-entry palette, opaque; not in retail data |
| 16 | PA4 | 4-bit index, low nibble first | RGBA8888 | 16-entry palette with alpha; not in retail data |
| 17 | RGB888 | 3 bytes R,G,B | RGB888 | verified (dusk sky has an orange horizon) |
| 18 | RGBA8888 | 4 bytes R,G,B,A | RGBA8888 | verified (stop sign is red) |

**Palette entries are B,G,R,A** (Windows RGBQUAD order), unlike the true-colour
formats. Verified: `vpbugyellow_sd` decodes yellow only with this order, and
MM2 swaps the bytes the same way in both of its palette paths.

The 4-bit formats read `w × h / 2` bytes per level (rounded down), so a 1 × 1
level has no data; MM2 leaves that texel uninitialised and OpenMM2 uses
palette entry 0 (inferred).

`gfxTexture::Create` marks textures from the RGBA8888 and ARGB1555 images as
alpha textures; `asset::Image::alphaFormat` records this. It depends on the
format, not on the texel values (many P8 car textures are opaque but flagged
differently, see below).

When the display supports 8-bit textures (`g_Allow8BitImages`), MM2 keeps
P8, PA8, P4 and PA4 images paletted and uploads the palette instead; the
colours are the same.

### Row order

Every level is stored **bottom row first**. The game's UVs use v = 0 for that
first row: the stop-sign face in `geometry/sp_stop_f.pkg` maps v = 1 to its
top edge, and the dusk sky `sky_cd_f` has its horizon in row 0. MM2 copies
the rows into its image in file order; `asset::Image` keeps them in that order
too (row 0 = v 0). Upload rows unchanged and use the game's UVs directly.
`encodePng` flips for display.

### Flags

The flags are stored in the image and ORed into the texture's state by
`gfxTexture::Create`. MM2's render-state flush (`gfxRenderState::DoFlush`)
reads two of them for each texture stage:

| Bit | Meaning |
|-----|---------|
| 0x1 | clamp U (`D3DTSS_ADDRESSU` = CLAMP; otherwise WRAP) |
| 0x10000 | clamp V (`D3DTSS_ADDRESSV` = CLAMP; otherwise WRAP) |

Nothing else in MM2's renderer reads the file flags; the exporter's other
bits (0x2, 0x4, 0x8000) have no effect. Textures that are not `.tex` files
have no flags and repeat in both directions. Retail distribution:

| Flags | Count | Typical textures | MM2 addressing (U, V) |
|-------|------:|------------------|------------------|
| 0x2 | 1533 | building facades | wrap, wrap |
| 0x10001 | 940 | car paint, trees, cut-out cards | clamp, clamp |
| 0 | 639 | mixed | wrap, wrap |
| 0x18001 | 278 | car rears, dashboards | clamp, clamp |
| 0x8006 | 95 | `r1_*`, `r2_*` road surfaces | wrap, wrap |
| 0x18006 | 58 | road surfaces | wrap, clamp |
| 0x8000 | 44 | `fxpt*` particles, steering wheel | wrap, wrap |
| 0x10000 | 35 | skies, fences | wrap, clamp |
| 0x10002 | 21 | | wrap, clamp |
| 0x6 | 10 | `s_concrt`, `s_steps`, `s_groundbrick01` | wrap, wrap |
| 0x10003 | 6 | | clamp, clamp |
| 0x7 | 5 | `decal_zigzag*` | clamp, wrap |
| 0x10006 | 1 | | wrap, clamp |

## Placeholder data

`r1_grass.tex` and `s_grassndirt.tex` are P8 images whose every texel is
palette entry 0 (white). They are blank in the shipped data too; the real
ground uses `r1_grass_f` (RGB888) etc.

## Texture lookup

Material texture names in PKG files are base names. MM2 loads a texture
(`gfxGetTexture`) through a chain of image loaders, each trying one file and
falling through when it is missing or unreadable (`gfxLoadImageAll`, wrapped
by `InstallJPEGSupport` and `InstallTextureVariantHandler`):

1. `texture/<name>.tex`
2. `texture/<name>.tga`
3. `texture/<name>.bmp`
4. `texture/<name>.raw` with the palette `texture/<name>.act` (only when the
   display supports 8-bit textures)
5. `jpg/<name>.jpg`

In game the variant handler first tries `<name>_fa` in rain (weather 3, the
state that also selects the wet surface sounds) and `<name>_ni` at night
(time of day above 2); when a night texture has no `_ni` variant every
channel of the plain one is halved.

2215 of the 2234 names referenced by retail packages exist as `.tex` or
`.tga`. The rest are animated (`s_ocean`, `s_pond`, `s_thames` →
`tune/<name>.movie`), damage variants of `vppanozgt` that were not shipped,
and a few street surfaces.
