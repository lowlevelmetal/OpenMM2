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
    u32 count2              mm2hook dgPath::NumPoints2 (0 or = pointCount)
    u32 unknown             mm2hook dgPath::Unk1
    point[pointCount]: float3 position, u32 extra   (extra often looks uninitialised)
```

Uses: race props (`race/<dir>/<race>N.pathset`: barricades, ramps),
city gizmos (`race/<dir>/<map>_{bridge,ferry,parkedcar,sailboat,train}.pathset`),
and per-race gizmo overrides (`<map>_bridge_circuit3.pathset`). mm2hook's
`dgPathType` (single points, directed points, line strip) and spacing are
not stored explicitly. They are probably derived from the name or the
unknown words.
