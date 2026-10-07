# AI road network (`city/<map>.bai`)

Parser: `src/city/AiMap.{h,cpp}`. Tool: `mm2tool baiinfo <source> <city> [path]`.
Retail coverage: `london.bai` and `sf.bai` parse to their last byte (540
paths / 328 intersections, 379 / 214). The editor files `<map>_sup.bai` use a
different layout and are not loaded.

The layout was derived empirically. Path starts were found by their headers,
then a size model was fitted until every path ended exactly where the next
one begins. Open1560's disassembly of MM1's `aiPath::ReadBinary` and mm2hook's
`aiPath` field list were consulted for names. MM1's format differs (no
`CAI1`, other field order), so only names carry over.

```
char[4] "CAI1"
u16 intersectionCount
u16 pathCount
path[pathCount]
intersection[intersectionCount]
u32 roomCount                          = PSDL roomCount
roomList near[roomCount]               u16 n, u16 pathIds[n]
roomList in[roomCount]                 u16 n, u16 pathIds[n]
```

## Path

```
u16  id                    = index
u16  sections              number of centre-line points (>= 2)
u16  flags                 mm2hook PathFlags: 0x1 ?, 0x2 alley, 0x4 freeway; 0x8 on 469/540 London but 67/379 SF paths
u16  roomCount, rooms[roomCount]
f32  halfWidth
f32  speedLimit            15 on every retail path
side left
side right
u32  unknown               always 0
f32  centerLengths[sections - 1]       cumulative distance along the centre line (verified)
float3 center[sections]
float3 xAxis[sections], yAxis[sections], zAxis[sections], wAxis[sections]
end  ends[2]
```

Section frames (verified on every path): `zAxis` points back, against
increasing section index (Angel convention). `xAxis` points to the *left* of
that direction, towards the left side's polylines, so (x, y, z) is
left-handed. `yAxis` is up. x is unit length except at 13 sharp bends. w ≈ -z.

`ends[0]` is the intersection at `center.back()`, `ends[1]` at
`center.front()` (529/540 London, 374/379 SF; the rest are loops).

### Side

```
u16 numLanes, numTrams, numTrains, numSidewalks(always 1), roadType(0..3), unknown5, unknown6
for each of numLanes + 1 entities:
    f32 lengths[sections - 1]        cumulative distances
    f32 endValue
f32 laneExtras[numLanes]
f32 params[10]                       e.g. [-7.5, 3.75, 3.75, 7.5, <uninitialised>...]
float3 polylines[3 + numLanes + numTrams + numTrains][sections]
```

The counts that drive the record size (lanes, trams, trains) are verified by
exact parsing. SF needs `numTrams` for the cable-car streets. The names
`numTrains`, `numSidewalks` and `roadType` follow mm2hook's field order. Which
polyline is which (lane centres, boundaries) is not yet known.

### Path end (38 bytes)

```
u32 intersection
u16 unknown1               0xCDCD in all files (uninitialised)
u16 vehicleRule            1 or 3 (inferred: stop sign vs. traffic light)
u16 unknown2
u16 roadIndex              index into the intersection's path list (inferred)
u16 unknown3
float3 trafficLightPos, trafficLightAxis
```

## Intersection

```
u16 id = index
u16 room                   PSDL room (intersection-flagged for >90%)
float3 center
u16 n
u32 paths[n]
```

Every path listed by an intersection names that intersection in one of its
ends (verified).

## Room lists

Two per-room lists of path ids. The first is a superset of the second
(e.g. London room 396: 9 vs 6 paths). Likely the ambient traffic spawn and
cull sets. Unverified.
