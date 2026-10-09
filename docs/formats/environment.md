# City environment files (`city/`)

Parsers: `src/city/Environment.{h,cpp}`; loaded by `loadCity`.

| File | Format | Content |
|------|--------|---------|
| `<map>.lt00`..`.lt15` | Angel "type: a" | Lighting per time of day and weather. Index = timeOfDay × 4 + weather (time: morning, noon, evening, night; weather: clear, cloudy, foggy, rainy; verified from the block names). Fields `KeyHeading/KeyPitch/KeyColor`, `Fill1*`, `Fill2*` (radians, 0..1 colours), `Ambient` (signed int, ARGB). |
| `<map>_fog.csv` | CSV | 16 rows in the same order: fog colour (0..255), start, end. The description column is ignored per its header. |
| `<map>.sky` | text | `model a b c` (`sky_dome_l 0 0.95 0.005`), read by `lvlSky::AutoInit` (`%s %f %f %f`): the dome sits at the camera with its height = camera height x b + a (`lvlSky::DrawHat`) and turns about +Y at c radians per second (`lvlSky::Update`). |
| `<map>.water` | text | water plane height, then room ids (inferred: rooms that use it). |
| `<map>.ext` | text | `minX minZ maxX maxZ`, a world rectangle inside the PSDL bounds. midtown2.exe has no reader for it (an editor file). |
| `<map>.reset` | text | `x y z  # comment` recovery points ("GG fallthru" = Golden Gate fall-through). midtown2.exe has no reader for it: the game respawns at checkpoints or intersections instead. |
| `<map>.lmap` | binary | `"LMP0"`, `u32 n`, `u32 argb[n]`: one colour per room id (entry 0, the dummy room, is 0xCDCDCDCD). `cityLevel::Load` takes it only when n equals the PSDL's room count (dummy room 0 included); then room i gets argb[i] (`lvlRoomInfo` +0x1c, the street colour and the instance light tint), else every room is white. Neither retail file matches (London 1,341 for 1,342, SF 1,125 for 1,172), so the retail game draws every room white. |
| `materials.mtl` | text | Physics materials: `mtl name { elasticity: f friction: f effect: s sound: n drag: f width: f height: f depth: f ptxindex: a b ptxthreshold: a b }`. |
| `materials.csv` | CSV | `texture,physics`: texture name → material name (`none`, `cobblestone`, `grass`, `water`, `deepwater`, `sand`, `mud`, `ash`). |

Editor-only files (not loaded): `*.ldef` (paths to Angel's network `.tif`
ambient maps plus corner coordinates), `.extra`, `.rid`, `.lmap` variants,
`facades.csv`, `floors.csv`, `props.csv`, `groups.txt`, `getcsv.bat`,
`<map>_chop.csv`, `*.pvs`, `*.pvshist`, `sf.geo` (Angel "Geo2" export),
`*.bak`, `*.tmp`.
