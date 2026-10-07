# Instances (`city/<map>.inst`, `city/<map>_ai.inst`)

Parser: `src/city/Inst.{h,cpp}`. Tool: `mm2tool instinfo <source> <city> [--all]`.
Retail coverage: all 13 `.inst` files parse to their last byte.

`<map>.inst` places buildings, props and landmarks (London 1,997, SF 3,763).
`<map>_ai.inst` places traffic signs and signals (London 67, SF 40).

Records repeat until end of file (little-endian):

```
u16   room          PSDL room id
u16   flags         lvlInstance flags (values 0..3 and 0x200/0x2000 seen; meaning unknown)
u8    nameLength    bit 7 set = compact form; low 7 bits = length including NUL
char  name[nameLength & 0x7F]
if compact (lvlFixedRotY):
    f32 xAxisX, xAxisZ      X axis in the XZ plane (its length may carry a scale)
    float3 position
else (lvlFixedMatrix):
    float3 m0, m1, m2, m3   Angel Matrix34 rows (may include scale)
```

The compact form becomes `m0 = (x, 0, z)`, `m1 = (0, 1, 0)`, `m2 = (-z, 0, x)`
(X × Y, right-handed), `m3 = position`.

Verification: the room field matches the PSDL. In London, 1,360 instance origins lie
inside their room's perimeter and 628 more within 1 m of it (facade-mounted
pieces). Only 9 are farther. All 67 `_ai.inst` origins are inside or within
1 m. `city/<map>.sdl_ai.inst` has the same instances but stale room ids (53 of 67 more
than 1 m away), so it is not loaded. It is presumably an older export.
