# DAVE archives (`.AR`)

Container for all game data on the retail disc: `MM2CORE.AR` (geometry,
bounds, city, race and tune data), `MM2TEX.AR` (textures, JPEG UI art),
`MM2AUD.AR` and `MM2AUDEX.AR` (sounds, DirectMusic data, audio CSVs).
Implemented by `src/vfs/DaveArchive.cpp`. **Verified** by opening every
entry of the four retail archives (build 3390) and by round-tripping the
directory against `mm2tool ls`, and against MM2's reader `zipFile::Init`
(MM2 calls a DAVE file an "optimized archive"; anything else it reads as a
zip file, see below).

`zipFile::Open` finds names with a binary search over the directory, so the
directory must be sorted by name, compared byte by byte after lower-casing
`A`-`Z` and turning `\` into `/` (every retail directory is; OpenMM2 uses a
hash table, which gives the same answers for sorted directories). A
duplicate name is ambiguous in MM2; OpenMM2 keeps the first.

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

### Zip archives

An `.ar` file that does not start with `DAVE` is read as a PKZIP file
(`zipFile::Init`), which is how add-on archives are often made. MM2 supports
only part of the format, and OpenMM2 reads the same subset:

* the end-of-central-directory record must be the last 22 bytes (an archive
  comment makes MM2 fail with "zipfile comments not supported");
* the archive must be a single part (disk numbers equal);
* every entry must be stored or deflated (methods 0 and 8), else the whole
  archive is refused;
* the entry count is the end record's count for this disk;
* an entry's data is taken to start 30 + name length bytes after its local
  header offset: a local extra field is not skipped, so such entries read
  wrong in MM2 and here;
* an entry is inflated when its compressed and uncompressed sizes differ.

MM2 also loads an optional checksum file (`<archive>.CHK`) but never checks
entries against it; none ships, and OpenMM2 ignores it.

A variant with the magic `Dave` (prefix-compressed names) exists in other
Angel titles; no retail MM2 archive uses it and it is rejected.

## Mount order

`src/vfs/GameSource.cpp` mounts every `*.ar` in the game folder the way MM2
does. **Verified** against `zipMultiAutoInit` and `zipFile::zipOpen`: the
game finds all `*.ar` files in its folder (up to 256), upper-cases their
paths and sorts them with `strcmp`, then creates the archives from the last
to the first, each one pushed onto the front of the list that
`zipFile::zipOpen` searches. A file is therefore taken from the first archive
in upper-case name order that has it (`MM2AUD`, `MM2AUDEX`, `MM2CORE`,
`MM2TEX`; an add-on named `A_MOD.AR` overrides them, one named `ZZ_MOD.AR`
only adds files they lack). The retail archives share no paths, so their own
order makes no difference. Opening the first archive replaces the default
file methods with the archive ones, so loose files in the game folder are
never read as game data; OpenMM2 does not mount them either.
