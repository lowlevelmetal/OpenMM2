# DAVE archives (`.AR`)

Container for all game data on the retail disc: `MM2CORE.AR` (geometry,
bounds, city, race and tune data), `MM2TEX.AR` (textures, JPEG UI art),
`MM2AUD.AR` and `MM2AUDEX.AR` (sounds, DirectMusic data, audio CSVs).
Implemented by `src/vfs/DaveArchive.cpp`. **Verified** by opening every
entry of the four retail archives (build 3390) and by round-tripping the
directory against `mm2tool ls`.

All integers are little-endian.

| Offset | Type | Meaning |
|-------:|------|---------|
| 0x000 | char[4] | magic `DAVE` |
| 0x004 | u32 | entry count |
| 0x008 | u32 | directory size in bytes (count × 16, padded to 2048) |
| 0x00C | u32 | name table size in bytes (padded to 2048) |
| 0x800 | entry[count] | directory |
| 0x800 + dirSize | char[] | name table |

Entry (16 bytes):

| Offset | Type | Meaning |
|-------:|------|---------|
| 0 | u32 | name offset into the name table |
| 4 | u32 | absolute data offset (2048-byte aligned) |
| 8 | u32 | uncompressed size |
| 12 | u32 | stored size |

Names are NUL-terminated full paths with `/` separators, e.g.
`tune/vehicle/vpbug.vehcarsim`. The name table is not in directory order.
Entries whose name ends in `/` are directory markers with no data (the
archives were built from CVS working copies, so `CVS/` folders and stray
`.bak`/`.#` files are present). When the stored size differs from the
uncompressed size the payload is a raw DEFLATE stream (RFC 1951, no zlib
header).

A variant with the magic `Dave` (prefix-compressed names) exists in other
Angel titles; no retail MM2 archive uses it and it is rejected.

## Mount order

`src/vfs/GameSource.cpp` mounts `MM2CORE`, `MM2TEX`, `MM2AUD`, `MM2AUDEX`
first, then any other `*.ar` in the same folder in case-insensitive name
order at higher priority, then (installations only) loose files in the game
folder at the highest priority. The add-on behaviour mirrors how community
content is distributed (extra `.ar` files dropped next to the game). The
exact precedence rules of the original are **inferred**, not verified.
