# Path sets (`*.pathset`)

Parser: `src/city/PathSet.{h,cpp}`. Retail coverage: 98 of 101 files parse to
their last byte. The three failures (`race/london/blitz10.pathset`,
`blitz11.pathset`, `london_bridge_blitz10.pathset`) are editor leftovers.
One is truncated and two were corrupted by an LF→CRLF conversion (stray 0x0D
bytes before 0x0A inside floats). No race references them (London has blitz
0..9).

```
char[4] "PTH1"
u32 pathCount
u32 unknown                (0, 2 or 3)
path[pathCount]:
    char name[32]           model, sometimes with a prefix ("open:giz_bridge02_l")
    u32 pointCount
    u32 count2              dgPath +0x2c (0 or = pointCount)
    point[pointCount]: u32 flags, float3 position   (flags often look uninitialised)
    u8  type                dgPath +0x30 (0 single points, 1 directed pairs, 2 line strip)
    u8  spacing             quarter metres (dgPath +0x34 = spacing x 0.25; 0 means 5 m)
    u8  unused[2]
```

This is how MM2's `dgPath::Load` (build 3393) reads a path. The parser keeps
an older reading of the same bytes for compatibility: a header word
(`PathSetPath::unknown`, the first point's flags) and per point the position
followed by 4 bytes (`PathSetPoint::extra`, the next point's flags; the
last point's are the trailer). `PathSetPath::flags`, `type` and `spacing`
give MM2's fields.

Uses: race props (`race/<dir>/<race>N.pathset`: barricades, ramps),
city gizmos (`race/<dir>/<map>_{bridge,ferry,parkedcar,sailboat,train}.pathset`),
and per-race gizmo overrides (`<map>_bridge_circuit3.pathset`). The type
and spacing are the trailer bytes above (`dgPath::Enumerate` places the
props by them).
