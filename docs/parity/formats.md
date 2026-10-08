# Parity audit: formats

Audited against MM2Recomp (midtown2.exe build 3393) on 2026-10-07.

Summary: 119 entries (functions, or groups of trivial helpers); verified 23,
fixed 44, deviation 11, inferred 7, open 0, openmm2 34. Five MM2 features
with no retail use are listed as open under Missing. (Follow-up on
2026-10-08: datParser::Read's handling of unknown names, see DatFile.)

Scope: the asset readers (`src/asset/Image`, `Mtx`, `Pkg`, `Ped`,
`VehicleModel`, `Reader.h`), the data readers (`src/data/DatFile`,
`TextTables`, `PeResources`, and the new `CNumbers.h`) and the archive layer
(`src/vfs/DaveArchive`, `Vfs`). Every retail file of each format was also
read with both the old and the new code to see what changed.

MM2's file search order (every `*.ar` sorted by upper-case name, the first
archive that has a file wins, loose files never read once an archive is
open: `zipMultiAutoInit`, `zipFile::zipOpen`) was fixed on the integration
branch in `src/vfs/GameSource.*` (commit 23f3dd5, "Search game archives in
MM2's order and ignore loose files") and is not repeated here; `Vfs` only
provides the layering it uses.

## src/asset/Reader.h

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Reader::pos`, `size`, `remaining`, `ok`, `has` | — | openmm2 | bounds-checked cursor over a byte span; MM2 reads through `Stream::Read` |
| `Reader::seek`, `skip` | `Stream::Seek` | openmm2 | failure is recorded instead of reading past the end |
| `Reader::read<T>`, `u8`, `u16`, `u32`, `f32` | `Stream::Read`, `Stream::GetCh` | openmm2 | little-endian loads |
| `Reader::vec2`, `vec3`, `vec4` | — | openmm2 | component order x, y, z, w as MM2 stores vectors |
| `Reader::fixedString` | — | openmm2 | NUL-padded field (PKG names, xref names) |
| `Reader::bytes` | — | openmm2 | |

## src/asset/Image (textures and images)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Image::width`, `height`, `empty` | — | openmm2 | accessors |
| `Image::hasTranslucency` | — | openmm2 | content test used by OpenMM2's renderer; MM2 decides alpha by image format (`gfxTexture::Create` sets its alpha flag for RGBA8888 and ARGB1555 images), now recorded as `Image::alphaFormat` |
| `TexFormat` | `gfxLoadTexImage` switch | fixed | formats 2 (P8A8: index byte + alpha byte) and 6 (ARGB1555) were missing |
| `TexFlags` | `gfxRenderState::DoFlush`, `gfxTexture::Create` | fixed | MM2 reads two file flags: 0x1 clamps U and 0x10000 clamps V (`D3DTSS_ADDRESSU/V`), everything else repeats. OpenMM2 read 0x2/0x4 as "wrap" and clamped otherwise, which clamped facades vertically and every flagless texture; 0x1 was read as "alpha". The address-mode choice in `game/TextureLibrary.cpp` was changed with it (outside this area, two lines) |
| `paletteEntries` | `gfxLoadTexImage` | fixed | P8A8 has a 256-entry palette |
| `levelBytes` | `gfxLoadTexImage` | fixed | 4-bit levels are `w*h/2` bytes rounded down (a 1x1 level has none); P8A8 and ARGB1555 two bytes per texel |
| `knownFormat` | `gfxLoadTexImage` | fixed | unknown types fail ("unsupported .tex type") |
| `hasAlpha` | `gfxLoadTexImage` | fixed | P8, P4, RGB888 become RGB888 images, the rest carry alpha |
| `powerOfTwo` | `texImage_CheckRes` | fixed | `(v & -v) == v` |
| `expand5` | — | inferred | 5-bit to 8-bit expansion of ARGB1555 happens in the Direct3D driver; bit replication assumed |
| `parseTexHeader` | `gfxLoadTexImage` | fixed | sides must be powers of two ("Bad resolution"; two unreferenced retail files fail, as in MM2); dropped the invented 4096 and 13-level limits; zero sides are still rejected (MM2's bit test lets 0 through) |
| `parseTex` | `gfxLoadTexImage`, `gfxImage::Create` | fixed | P8 and P4 now ignore palette alpha (MM2 builds RGB888); the mip chain stops when a side reaches 1 (24 retail files declare extra levels MM2 never reads); a mip count of 0 reads the whole chain; P8A8 and ARGB1555 decoded; `alphaFormat` set |
| `decodeTga` | `gfxLoadTargaImage` | deviation | superset: MM2 ignores the ID field, colour map, image type (no RLE) and right-to-left bit, reading 32-bit as RGBA and anything else as 24-bit. Same pixels for every retail TGA. Rows are returned picture-bottom first (MM2 stores the top row first); OpenMM2's UI draws them upright like MM2's blits, but a TGA on a 3D mesh is upside down relative to MM2 unless the renderer flips it (only the symmetric HUD map dots in retail) |
| `decodeStb` | `gfxLoadBmpImage`, `gfxLoadJPEGImage` | deviation | stb_image superset: MM2 reads only uncompressed 8/24-bit BMP (rounding odd widths up, no row padding) and baseline JPEG via IJG libjpeg; PNG is an OpenMM2 addition for mods. Row order as for TGA |
| `decodeImageFile` | — | openmm2 | picks a decoder by extension; MM2's own lookup is a fallback chain (`gfxLoadImageAll` + JPEG + variant handler: .tex, .tga, .bmp, .raw/.act, jpg/.jpg), which belongs to the texture loader (rendering-fx) |
| `encodePng` | — | openmm2 | tools |
| `fail`, `u8at` | — | openmm2 | helpers |

## src/asset/Mtx

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Mtx` (min, max, center, origin), `halfExtent` | `GetPivot` | verified | GetPivot reads the 48 bytes into a `Matrix34` whose rows are these four vectors; callers use the origin row, `vehWheel::Init` the box, `vehAxle`/`vehSuspension`/`dgBangerData` the rows as a matrix |
| `parseMtx` | `GetPivot` | fixed | MM2 reads the first 48 bytes and ignores the rest; files longer than 48 bytes were rejected. Shorter files are still rejected (MM2 would leave part of the matrix unset) |

## src/asset/Pkg (model packages)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `fvfVertexSize` | `gfxFVFSize` | deviation | same sizes for every retail format (0x112, 0x102). MM2 also accepts other position types (XYZRHW, blend weights) and adds 8 bytes per set bit of 0x100/0x200 instead of 8 per texture set; OpenMM2 accepts only XYZ and sizes texture sets as Direct3D does |
| `parseGeometry` | `modGetStatic` | fixed | section count is the low byte of the first word; bit 0x80 marks a geometry-free chunk (count, FVF, one shader byte per section) that OpenMM2 could not read; packet count is a u32 (OpenMM2 split it into a u16 count and a u16 "flags"); the shader index keeps only its low byte; stored vertex colours have red in the low byte and MM2 swaps red and blue. OpenMM2 still rejects out-of-range indices and non-multiple-of-3 triangle lists (MM2 does not check) to keep the GPU safe |
| `quantizeComponent`, `quantize` | `modShader::Load` | fixed | full materials: each diffuse, specular and emissive component below 0.05 becomes 0, above 0.95 becomes 1, else floor(v x 32) x 0.03125; 13,326 retail colour components change (e.g. specular 0.9 -> 0.875) |
| `byteColor` | `modShader::Load` (byte colour helper) | fixed | byte x MM2's 1/255 float constant, the x87 product rounded once (126 of 256 values differed from byte/255.0f) |
| `readShaderTable` | `modShader::LoadShaderSet`, `modShader::Load` | fixed | compact materials store diffuse, specular and emissive (OpenMM2 read diffuse, ambient, specular and dropped emissive, so light glows lost their emissive colour); ambient is replaced by diffuse in both forms; name length 0 = untextured |
| `parseShaders` | `lvlInstance::EndGeom` -> `LoadShaderSet` | fixed | uses the table reader. MM2 keeps at most a global number of paint jobs (9999, set temporarily by the city loader); the parser keeps all of them and leaves the limit to its users |
| `parseShaderTable` | `modShader::LoadShaderSet` | fixed | new: shared with the pedestrian `.shaders` reader |
| `parseXrefs` | `lvlInstance::EndGeom` | verified | u32 count, count x 80 bytes (Matrix34 + 32-byte name) |
| `parseChunk` | `modPackage::OpenFile` users | verified | dispatch by chunk name; the `offset` chunk is never read by MM2 (parsed for tools only) |
| `atChunkBoundary` | — | openmm2 | supports the structural fallback below |
| `parsePkg` | `modPackage::Open`, `NextItem`, `Skip`, `SkipTo` | deviation | magic PKG3/PKG2 and the FILE/name/size items as MM2 reads them. MM2 reads chunks in order and skips with the declared size; OpenMM2 parses them all and falls back to structural parsing when a PKG3 size is wrong (vpvw_dune.pkg), and reports a missing FILE tag instead of quitting |
| `PkgMesh::vertexCount` | `modStatic::GetAdjunctCount` | verified | sum of packet vertex counts |
| `PkgMesh::triangleCount` | `modStatic::GetTriCount` | verified | sum of index counts / 3 |
| `PkgMesh::bounds` | — | openmm2 | |
| `Pkg::find(name)` | `modPackage::OpenFile` | verified | case-insensitive chunk name |
| `Pkg::find(part, lod)` | `lvlInstance::GetGeomSet` | verified | "%s_H/M/L/VL" or bare H/M/L/VL |
| `Pkg::findBest` | `lvlInstance::GetGeomSet` | fixed | MM2 fills a missing LOD only from a less detailed one (VL->L->M->H); OpenMM2 preferred more detailed meshes first. Its one caller asks for High, which is unchanged |
| `Pkg::parts` | — | openmm2 | |
| `splitLodName`, `lodSuffix` | `lvlInstance::GetGeomSet` | verified | LOD suffixes H, M, L, VL |
| `isKnownBrokenRetailAsset` | — | openmm2 | test/tool list |

## src/asset/VehicleModel

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `kPivotOnlyParts` | `vehCarDamage::Init`, `vehCarModel::Init`, `aiVehicleInstance` | verified | exhaust0/1, trailer_hitch, headlight0/1 are looked up with `GetPivot` without needing a mesh |
| `VehicleModel::pivot` | `GetPivot` | verified | `geometry/<base>_<part>.mtx`, case-insensitive through the archive |
| `VehicleModel::wheel` | — | openmm2 | |
| `loadVehicleModel` | `vehCarModel::Init`, `vehWheel::Init` | fixed | wheel radius is abs(max.y - min.y) / 2 (the absolute value was missing); width `max.x - min.x`. It loads every part the package has; the header now lists the parts `vehCarModel::Init` asks for (break0-3 and break01/12/23/03, not a numbered series) |

## src/asset/Ped (pedestrians)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `tokens`, `lines`, `asText`, `stem`, `fail` | — | openmm2 | line/token helpers |
| `toFloat` | `datAsciiTokenizer::GetFloat` | fixed | a token starting with a digit, '-' or '.' read by atof's prefix rule (was a whole-string parse that rejected "1.5f"); other tokens still fail (MM2 logs and uses 0) so broken files are spotted |
| `toInt` | `datAsciiTokenizer::GetInt` | fixed | token starting with a digit or '-', atoi (was a parser that accepted hex and rejected trailing text) |
| `floats`, `ints` | — | openmm2 | |
| `Skeleton::find` | `crSkeletonData::FindBone` | fixed | exact name (was case-insensitive) |
| `parseSkeleton` | `crSkeletonData::Load`, `crBoneData::Load` | fixed | optional `rotmin`/`rotmax` limits (default -pi, pi) were rejected as unknown tokens. OpenMM2 still checks NumBones against the bone count (MM2 allocates NumBones and trusts it) |
| `PedAnimation::rootTranslation`, `boneRotation` | `crAnimFrame::Pose` | verified | channel 0-2 root translation, then 3 per bone |
| `parsePedAnimation` | `crAnimation::LoadAnim` | fixed | the older layout (nonzero first word = frame count, channel word = bone count) was rejected; limits are MM2's (1-10000 frames, 1-1000 channels); data after the frames is ignored (was an error); dropped the invented non-finite check. Channel counts that are not root + 3 per bone are still rejected (posing needs them) |
| `matrixFromEulersXZY` | `Matrix34::FromEulersXZY` | fixed | products now grouped as MM2 groups them ((sz*cy)*cx, (sz*sy)*cx; was sz*(cy*cx), (cx*sz)*sy, ported from MM1). MM2 keeps cosines in x87 registers; 32-bit here |
| `posePed` | `pedAnimationInstance::Draw` (inlined `crAnimFrame::Pose`), `crBoneData::Transform` | verified | local = Eulers XZY + skeleton offset, root position from the frame, model = local x parent. Fractional frames interpolate (a tool addition; the game passes whole frames, as MM2 does) |
| `parsePedMesh` | `modGetModel`, `modModel::LoadAscii` | fixed | only versions 1.08/1.09 are accepted (2.00 is binary; anything else fails in MM2); with fewer than two colours the vertices get no colour; `texture: <n> <name>` lines, an optional fourth (reskin) count on `packet` lines, `reskin` lines and packets without matrices (5-value `adj`) are read instead of rejected. `mtxn` is read but, as in MM2, unused |
| `PedShaderSet::get` | `pedAnimationInstance::Load` | verified | variant-major, material i of a variant colours material i |
| `parsePedShaders` | `modShader::LoadShaderSet` | fixed | read with the PKG shader-table reader: colours rounded to 1/32 and ambient = diffuse (was raw), compact flag honoured, trailing data ignored (was an error) |
| `parsePedRays` | `pedAnimationInstance::Load` | verified | count, per-bone `f f f i i`, then per-variant rows. MM2 reads exactly one row per shader variant; OpenMM2 reads the rows present |
| `parsePedRemap` | — | openmm2 | MM2 never reads `.remap` (no such string in the executable); kept for tools |
| `PedAnimTable::find` | `pedAnimation::LookupSequence` | fixed | exact name, first match (was case-insensitive) |
| `parsePedAnimTable` | `pedAnimation::Load` | fixed | strtok splitting (empty fields between commas vanish), atoi/atof prefix numbers, next state optional (OpenMM2 required 9 columns and whole-string numbers). A sequence with last < first, which MM2 plays backwards, is still rejected (none ships) |
| `PedType::animation` | — | openmm2 | lookup helper; falls back to a case-insensitive state name for tools |
| `loadPedType` | `pedAnimationInstance::Load` | fixed | `.shaders` is optional (MM2 then uses one variant made of the `.mod` materials; OpenMM2 failed); a `.rays` file whose bone count differs from the skeleton is ignored ("Number of bones changed") |
| `findPedTypes` | — | openmm2 | tools |
| `isKnownBrokenPedAsset` | — | openmm2 | tools |

## src/data/DatFile ("type: a" data files)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Tokenizer::getToken` | `datBaseTokenizer::GetToken` | fixed | ported character by character, with its look-ahead character: tokens are separated by space, tab, CR, LF and NUL only; `{`/`}` are structural only as whole tokens (quoted or not); ':' is not special; a quoted token runs to the next quote across lines; a ';' comment before a token is skipped and one inside a token ends it after storing the line break; names are cut to 63 characters and numbers to 31 as in MM2's buffers. OpenMM2 split braces and colons out of tokens |
| `Tokenizer::skipToEndOfLine` | `datBaseTokenizer::SkipToEndOfLine` | fixed | reads on from behind the look-ahead character to the next line feed, so after a token that ends on a bare line feed it skips the whole following line |
| `Tokenizer::getFloat`, `getInt` | `datAsciiTokenizer::GetFloat`, `GetInt` | fixed | a token not starting with a digit, '-' or '.' (GetInt: digit or '-') is an error and reads as 0, after being consumed |
| `TreeParser::parseFile`, `parseBody` (schema-less `parseDat`) | `datParser::Load`, `datParser::Read` | deviation | without the class's record list a reader cannot tell MM2's known names from unknown ones, so this reads every field and block: a field name is one token (MM2's multi-word names such as mmHudMap's "Approach Rate" and "Ocean Color" never match), a labelled block line is a block, the first seven bytes are the header (snow.asbirthrule has none), reading stops at the class block's closing brace and a missing closing brace is an error (MM2 never returns). It differs from MM2 for an unknown labelled block, of which MM2 skips only the first line; tunes read through a record list (below) are exact |
| `SchemaParser::read` | `datParser::Read` | fixed | new: with the class's records, exactly MM2's loop: '{' in field position is skipped, '}' returns, a registered name reads its record, an unknown name reads the next token and then skips the block after it if that token is '{', else the rest of the line. So an unregistered labelled block (`AsphaltRule asBirthRule :addr {`) loses only its first line, its fields are assigned to the outer class and its '}' ends the outer block |
| `SchemaParser::readRecord`, `addNumber` | `datParser::Read` record types 0-9 | fixed | new: strings take one token each, bool/byte/short/int take GetInt truncated as MM2 stores them, float GetFloat, vectors 2/3/4 GetFloats whatever the tokens are, a parser recurses |
| `SchemaParser::parseFile` | `datParser::Load` | fixed | new: header bytes, class name token, then the class's records |
| `DatNode::child` | `datParser::Read` | fixed | the last occurrence of a field wins (records are assigned in turn); was the first |
| `DatNode::getFloat` | `datAsciiTokenizer::GetFloat` | fixed | atof prefix of a token starting with a digit, '-' or '.': "1.#QNAN0" (va_garbagetruck MaxAng) is 1 (was a non-number, leaving MaxAng at its default) |
| `DatNode::getInt` | `datAsciiTokenizer::GetInt` | fixed | atoi of the token ("1.9" is 1, ".5" is 0, wraps at 32 bits) |
| `DatNode::getVec2`, `getVec3` | `datAsciiTokenizer::GetVector` | deviation | require all components; MM2 reads the next tokens whatever they are (a short vector swallows the following field name as 0) |
| `DatNode::getString` | `datParser::Read` (string record) | verified | a quoted or plain token after the name |
| `DatNode::getFloats` | `datParser::Read` | openmm2 | variable-length lists for OpenMM2's readers; MM2 reads a fixed count per record |
| `DatNode::read` (four overloads) | — | openmm2 | assign-if-present convenience |
| `parseDat(text)` | `datParser::Load` | fixed | header rule above; `type: b` (binary, `datBinTokenizer`) is rejected (no retail file uses it); the UTF-8 BOM is no longer stripped (MM2 reads it as header bytes) |
| `parseDat(text, schema)`, `DatRecord`, `DatSchema` | `datParser::AddRecord`, `AddParser`, `Read` | fixed | new: a class's FileIO record list. `phys::carSimSchema()` (outside this area, in `phys/vehicle/TuneParams`) lists the records of vehCarSim and its nested classes, and the vehicle tunes are now read through it (`game/PlayerVehicle.cpp`, `mm2tool simcar`). Every base tune reads as before; `vpftruck.vehCarSim` (no race uses it) now reads as in MM2: Mass 0.1 from its first particle rule, the block ended at that rule's '}', ManualNumGears swallowed by the unregistered MM1 gear lists. The `_opp`/`_cop` variants with the same rules read the same way, but MM2 never loads them |

## src/data/CNumbers.h (new)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `detail::numericPrefix` | C runtime `_atof`, `_atoi`, `sscanf` | inferred | the longest numeric prefix after white space; models the statically linked VC6 runtime |
| `atofPrefix`, `cAtof` | `_atof` | inferred | decimal, fraction and exponent; overflow gives infinity, underflow 0 |
| `atoiPrefix`, `cAtoi` | `_atoi` | inferred | decimal digits only (no hex), 32-bit wrap (inferred) |

## src/data/TextTables

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `splitLines` | — | openmm2 | |
| `KeyValueFile::parse` | `mmVehInfo::Load`, `mmCityInfo::Load` | deviation | MM2 scans a fixed sequence of case-sensitive `Key=` lines and fails the file on a mismatch; OpenMM2 reads any order. Same values for every retail .info/.cinfo |
| `KeyValueFile::get`, `getString` | `fscanf` "%s" / "%[^\r]" | deviation | values are trimmed; MM2 keeps trailing spaces of `%[^\r]` fields (only sf.cinfo's CheckpointNames has one) and `%s` takes the first word |
| `KeyValueFile::getInt` | `fscanf` "%d" | fixed | decimal prefix ("124 mph" is 124, "0x10" is 0); was whole-string with hex |
| `KeyValueFile::getFloat` | `fscanf` "%f", `_atof` (UIDist) | fixed | numeric prefix |
| `KeyValueFile::getList` | `string::NumSubStrings` users | openmm2 | split at the vertical bar |
| `CsvTable::parse` | `parCsvFile::Load` and others | deviation | a generic reader; `parCsvFile` keeps at most 16 columns, cuts lines at '#', keeps blank lines as rows and does not trim (documented in the header for its users) |
| `CsvTable::column` | `parCsvFile::GetColumn` | verified | case-insensitive (`strcmpi`); MM2 quits on a missing column, OpenMM2 returns -1 |
| `CsvTable::cell` | — | openmm2 | |
| `CsvTable::cellFloat`, `cellInt` | `parCsvFile::GetFloat`, `GetInt` | fixed | atof/atoi of the cell; empty or missing cells keep the caller's fallback (MM2 gives 0 for an empty cell) |

## src/data/PeResources (MMLANG.DLL)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `PeImage::parse`, `rvaToOffset`, `dir`, `leaf`, `in`, `u16`, `u32` | `LoadLibraryA` | inferred | PE headers and resource directory read as Windows' loader does |
| `appendUtf8`, `utf16ToUtf8` | `LoadStringA` | deviation | UTF-8 for OpenMM2's text renderer; MM2 gets the ANSI code page |
| `readPeStringTable` | `AngelReadString`, `MyLoadStringA` | inferred | string blocks of 16, the first (or preferred) language; Windows picks the user's language. The retail DLL has one language and no string over 511 bytes (MM2's limit) |
| `PeStringTable::find` | `AngelReadString` | inferred | a missing id is nullptr; `LoadStringA` gives an empty string (callers supply fallbacks) |
| `setError` | — | openmm2 | |

## src/vfs/DaveArchive

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `inflateRaw` | `zipHandle::Read` (`inflate`, window -15) | verified | raw DEFLATE |
| `DaveArchive::open(path)`, `open(file, label)` | `zipFile::zipFile`, `Stream::Open` | openmm2 | file plumbing |
| `DaveArchive::load` | `zipFile::Init` (DAVE path) | verified | header at 0, directory at 0x800, names right after it, entry = name offset, data offset, size, stored size. Not DAVE -> zip path (was an error) |
| `DaveArchive::loadZip` | `zipFile::Init` (zip path) | fixed | new: PKZIP files renamed to .ar (add-on archives) were rejected. End record must be the last 22 bytes, one part, methods 0/8 only, entry count from the end record, data at local offset + 30 + name length |
| `DaveArchive::addEntry` | `zipFile::Init`, `zipFile::Open` | deviation | directory markers dropped and duplicates keep the first; a hash map replaces MM2's binary search (same answers for sorted directories, which all retail ones are; MM2 misses names in an unsorted DAVE directory). Entries running past the end are refused |
| `DaveArchive::find` | `zipFile::Open` comparator | verified | case-insensitive, '\' = '/'. `normalizeVirtualPath` also folds `.`/`..`, which game paths never contain |
| `DaveArchive::extract` | `zipHandle::Read` | verified | stored when the two sizes match, else inflated |
| `DaveArchive::open(path)` (FileSystem) | `zipFile::Open`, `zipFile::zipOpen` | verified | MM2 also caps open handles at 16, irrelevant here |
| `DaveArchive::exists` | `datAssetManager::Exists` | verified | open and close |
| `DaveArchive::forEachFile` | `zipFile::EnumFiles` | openmm2 | listing for tools and the VFS |
| `DaveArchive::describe` | — | openmm2 | |

## src/vfs/Vfs

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Vfs::mount`, `clear`, `mountCount` | `zipMultiAutoInit` (list order) | openmm2 | priority/order layering; GameSource uses it to give MM2's archive order (integration commit 23f3dd5) |
| `Vfs::resolve`, `open`, `exists` | `zipFile::zipOpen` | verified | the first mount in precedence order that has the file, as `zipOpen` walks its archive list |
| `Vfs::readAll` | `Stream::Read` | openmm2 | |
| `Vfs::listFiles` | `zipFile::zipEnumFiles` | openmm2 | union, each path from its winning mount |

## Missing

| MM2 | What it does | Status |
| --- | --- | --- |
| `datBinTokenizer` ("type: b" data files) | binary form of the Angel data files | open: no retail file uses it; tractable (fixed-size records) if mods need it |
| `modModel::LoadBinary` (.mod version 2.00) | binary pedestrian/model meshes | open: no retail file uses it |
| `modModel::LoadAscii` material reordering | moves materials with translucent textures (or alpha < 1) to the end for drawing | open: never applies to retail models (untextured, alpha 1) |
| `gfxReskin` blend data (`reskin` lines) | multi-bone vertex blending in .mod packets | open: lines are accepted and skipped; no retail model has them |
| `pedAnimation::Load` reverse sequences | a CSV row with last < first plays backwards | open: rejected by OpenMM2; none ship |
| `gfxLoadRawImage` (`texture/<name>.raw` + `.act`) | raw 8-bit square textures, only on 8-bit displays | not needed: no retail .raw files and modern displays never take this path |
| `zipFile` .CHK checksum file | loaded at archive open, never consulted | not needed |
