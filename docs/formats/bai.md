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
u16  flags                 0x1 divided road, 0x2 alley, 0x4 freeway, 0x8 flat (MM2: an
                           ambient car on it stays upright at the road's first centre height)
u16  roomCount, rooms[roomCount]
f32  halfWidth
f32  speedLimit            15 on every retail path; MM2 overwrites it at load (city limit,
                           freeway +12.5, race exceptions)
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
exact parsing. SF needs `numTrams` for the cable-car streets.

MM2's `aiPath::ReadBinary` / `SaveBinary` (build 3393) read a side as five
shorts (lanes, trams, trains, sidewalks, flags) followed by a cumulative
length array of (lanes + sidewalks) rows x sections (each row starting at 0;
`unknown5`/`unknown6` above are the first row's leading 0.0), one float per
row (lateral offsets), the 10 lateral parameters, the lane and sidewalk rows
of vertices, one tram and one train row when present, and two rows per
section for the curb and the outer edge. So the polylines are, in order:
lane centre lines, the **sidewalk** line, the tram and train lines, the
**curb**, the **outer edge** (verified on SF path 106: lane 7.5, sidewalk
12.5, tram 2.5, curb 10, edge 15).

`roadType` is the side's flags word: bit 0 = no ambient traffic in this
direction (set on the empty side of one-way roads, on alleys and on some
other roads; `aiMap::AdjustAmbients`, `ChooseNext*Link`), bit 1 = no
pedestrians (`aiPedestrian::PickNextRdSeg`, `aiMap::AdjustPedestrians`).

### Path end (38 bytes)

```
u32 intersection
u16 unknown1               light index; uninitialised (0xCDCD) on disk, set at load (aiTrafficLightSet::SetFourWay)
u16 vehicleRule            0 stop sign, 1 traffic light, 3 no control (MM2 OkayToEnterIntersection)
u16 unknown2               end flags: 0 on disk; 3 at four-way lights (traffic goes straight on)
u32 roadIndex              this path's index in the intersection's path list (MM2 path +0/+4);
                           the parser reads it as u16 roadIndex + u16 unknown3
float3 trafficLightPos, trafficLightAxis   pole position; the unit XZ direction to the axis point
                           is the model's X axis (away from the road on retail data)
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
(e.g. London room 396: 9 vs 6 paths). The first is the set of roads MM2
populates with ambient traffic while a player is in the room, the second
the set for pedestrians (aiMap +0x174 / +0x178, `aiMap::AdjustAmbients`,
`AdjustPedestrians`).
