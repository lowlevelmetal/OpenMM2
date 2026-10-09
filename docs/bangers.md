# Bangers (breakable props)

Code: `src/game/bangers/` (`BangerData`, `PropPlacement`, `BangerSet`,
`RoadDecals`), tool: `mm2tool bangers <source> <city> [--png map.png]`.
Behaviour follows MM2's own code (build 3393): dgBangerData(Manager),
dgBangerManager, dgUnhitBangerInstance and its Y/Mtx variants,
dgHitBangerInstance, dgBangerActive(Manager), dgImpact::CalcImpact,
cityPropulator / lvlSDL::Propulate, dgPath, dgRoadDecalInstance. The structure
was first ported from Open1560 (MM1 `mmbangers/`).

## Data: `tune/banger/<model>.dgBangerData`

995 files; every one parses. Breakable parts are separate entries
`<model>_break01`, `_break02` … (271 files), one per `NumParts`. Defaults
(dgBangerData constructor): Size (0.2, 0.5, 0.2), Mass 50, Elasticity 0.5,
Friction 0.9, CollisionType 0x10, CollisionPrim 0.

| Field | Meaning | Evidence |
|---|---|---|
| Size, CG | collision/inertia box; CG above the model's ground origin. The prop's meshes (and its `BREAKnn` part meshes) are centred on the CG | MM2; verified: mesh bounds are symmetric about the CG |
| Mass, Elasticity, Friction | rigid body (box inertia, then `SmoothAngInertia(40)`: no principal moment below max / 40) | MM2 (`dgBangerActive::Attach`) |
| ImpulseLimit2 | break threshold: a hit whose stopping impulse squared stays within it leaves the prop standing | MM2 (`dgImpact::CalcImpact`) |
| NumParts | breaks into the `_breakNN` entries | MM2 |
| NumGlows (1 when absent), GlowOffset | lamp glows in the CG frame | MM2 (`dgBangerData::Load`, `dgBangerInstance::DrawGlow`) |
| TexNumber | debris particle sheet `texture/fxpt<N>` (2×2 frames: coins, letters, trash …) | MM2 (dgBangerDataManager keeps fxpt1–20) |
| BirthRule | debris particles, blasted when the prop is knocked over | MM2 (`dgBangerActive::Attach`) |
| BillFlags | 0x80 unlit with alpha reference 140; 0x200 tree (also set for names containing `_tree`): unlit, alpha reference 120, always the high LOD, drawn after the other props; 0x100 glass (no retail prop); 0x10 no shadow pass, 0x40 lamps (no effect: banger shadows are empty) | MM2 (`dgBangerInstance::Draw`, `dgTreeRenderer`) |
| CollisionPrim | 0 the `<name>_bound` geometry shifted by −CG (else a box), 1 box of Size, 2 capsule of YRadius and length Size.y, 3 sphere of YRadius; each with its own material (AdjustPrim: Elasticity, Friction) | MM2 (`dgBangerData::InitBound`, `AdjustPrim`) |
| YRadius | capsule/sphere radius and the unhit prop's touch radius | MM2 |
| CollisionType | how a knocked-over prop is simulated (`dgBangerActiveManager::Update`): 0x2 by itself without collisions, 0x40 or 0x10 colliding with everything, 0x4 with the city only, otherwise not at all; 0x20 is copied into the instance flags. Retail: 0x10, 0x4 and 0x30 | MM2 |
| AudioId, SpinAxis, Flash | read by the loader, used by nothing | MM2 |

The loader (`dgBangerData::Load`) reads the fields by position in the order
the files store them (every retail file is written by `dgBangerData::Save`
in that order, and every value is a plain decimal); OpenMM2 reads them by
name. A mass of 0 or less becomes 1 in OpenMM2 (no retail file has one).

## Where props come from

`placeCityProps()` combines, in cityLevel::Load's order:

1. **Street rules** (cityPropulator, lvlSDL::Propulate, lvlAiMap): for every
   PSDL road, the prop rule N of its first room (none for rule 0), rules
   `nNNleft` then `nNNright` from `proprules.csv` (the cells from column
   prop1 on; empty cells skipped), each prop's `propdefs.csv` row by name
   (start, distance and the lerps read with atof, maxUse with atoi; file1–4
   as far as the row has cells: an empty cell between commas counts and
   places nothing, the one after a comma ending the line does not). Both
   files are read as parCsvFile does: at most 16 columns (proprules.csv
   names 20, so a rule holds at most 15 props), every line after the header
   a row (blank lines too), '#' ends a line, lines of more than 255
   characters split into rows, cells end at a comma or a control character
   and keep their spaces. MM2 quits when a rule or a prop is missing;
   OpenMM2 skips it. The random
   generator restarts (seed 1) per road. For each prop of a rule the whole
   road is walked along both sidewalks, the left one first, once per road
   strip of the first room: from `start` metres, every `distance`, at a
   random fraction minLerp…maxLerp from curb to outer edge, 0.15 m up, +X
   from curb to outer edge (tilting with the sidewalk); a prop stands only on
   its rule's side, at most maxUse per rule side and road, choosing a random
   variant (one use is spent even when the chosen cell is empty).
   The sidewalks are lvlAiMap's (`SetRoad` with bevel mode off,
   `GetSidewalkVertex`): one road attribute per room of the road (road strip,
   rectangle strip or divided road), the rooms after the first joining
   without their first section (no check that it repeats the previous
   room's last one); the left sidewalk is vertices 1 (curb) and 0 (outer
   edge) of each section, the right one 2 and 3 (4 and 5 on divided roads);
   without the sidewalk flag (0x40) both edges are one point and nothing
   stands. Each interior section point becomes two points pulled a third of
   the way towards its neighbours, at most 0.1 m (15 m for the second and
   second last), which cuts the corners; the walk (`lvlSDL::IsoLerp`) gives a
   prop the room of the vertex starting its segment. Roads with flag
   0x400000 would move the curb 95% of the way to the road's centre (none in
   retail data).
2. **Instances** with banger data in `city/<map>.inst` and `<map>_ai.inst`
   (the stop signs `sp_stop_f`: 67 in London, 40 in SF): the full matrix
   unless stored in the compact Y-rotation form, and the record's variant
   byte as paint job. MM2 picks them by the record's banger flag (0x200);
   retail data sets it on exactly the instances with banger data.
   After each record (banger or not) come the bangers its geometry's PKG
   xrefs place (lvlLevel::LoadInstances, the "xrefs" chunk read by
   lvlInstance::EndGeom): each xref's matrix times the record's, dropped
   when a row is zero or two rows have a dot product above 0.01, rows
   outside 0.97..1.03 squared length normalised, then requested as Y
   bangers (RequestBanger(name, 0), dgUnhitYBangerInstance: only the turn
   about Y is kept, the CG offset is turned by the whole matrix) with the
   record's variant, in the room FindRoomId finds from the record's room.
   68 retail xrefs were exported Z up (awnings, the Chinatown sign, the
   bridge and tower lights): MM2 stands them upright. An xref whose model has no banger data is not placed at all (MM2
   reports it as not exported): cl10's trees. 33 retail models have xrefs
   (tower and bridge lights, awnings, doors, windows): 65 bangers in London,
   368 in San Francisco. They are not drawn with their parent model.
3. **`city/<map>/props.pathset`** (cityLevel::LoadPathSet, dgPath): the
   trailer after the last point (OpenMM2's parser reads it as the last
   point's spare word) holds a type byte and a spacing byte in quarter
   metres (0 = 5 m): `0x4002` trees every 16 m, `0x1C02` bollards every 7 m,
   `0x1402` barricades every 5 m. Type 0 puts an unrotated prop at every
   point; type 1 takes point pairs, the prop at the first with +X towards the
   second (flattened); type 2 places each segment on its own: n =
   floor(length / spacing) props length / n apart from the segment's start
   while at least the spacing is left (none on shorter segments, none at the
   last point), +X along the segment with Z = X × Y and Y = Z × X left
   unnormalised (shorter on a slope), full matrix; other types place
   nothing. Only models with banger data are placed; a prop goes in the room
   of its placement point.
4. **The race's props**: `race/<map>/<name>.pathset` placed the same way,
   with the name `dgGameModeNames[mode]` formats with the race index:
   `roam` (cruise), `race<N>`, `circuit<N>`, `blitz<N>`, `crash<N>`,
   `multicop` (Cops and Robbers). Retail has them for circuits, blitz 10–11,
   checkpoint races 6–7 and some crash course lessons.

Props with a Y rotation (dgUnhitYBangerInstance) keep only m00 and m02 of
their matrix: rows (c, 0, s), (0, 1, 0), (−s, 0, c), not renormalised; their
CG is placed with the matrix as given (`dgUnhitBangerInstance::Init`).

`city/<map>/decals.pathset` holds road decals (dgRoadDecalInstance): every
path of more than two points is a strip of point pairs 1 cm above the road
with the texture named like the path; u 0/1 across, v = floor(distance of
the pair from the start / the first pair's width + 0.5); drawn after the
street geometry, alpha blended, no depth writes, pulled forward, unlit. A
strip with an odd number of points takes the last pair's v for its last
point (MM2 reads past its table); MM2 draws a decal only with its room
(the room of the midpoint of its points 0 and 2), OpenMM2 always.

## Behaviour

* Props collide through MM2's collision manager (physics.md, "Props"): a
  standing prop is a banger instance of its room with its data's bound; the
  sphere test of a prop with a YRadius uses the ground point under its CG
  and that radius in the horizontal plane (`dgPhysManager::
  TrivialCollideInstances`). Touched, it attaches one of 32 actives
  (`dgBangerActiveManager`; when all are attached the first of its list is
  detached and reused) and `dgImpact::CalcImpact` decides: a hit whose
  stopping impulse squared stays within ImpulseLimit2 leaves it standing like
  a wall and the active goes back; a harder one breaks it loose.
* A broken prop (`dgUnhitBangerInstance::Impact`) leaves its room (keeping
  the room for the reset) and one of a ring of 40 hit instances
  (`dgBangerManager::GetBanger`) takes its place with the active, its motion
  and pending impulses; when the ring wraps the oldest knocked-over prop
  disappears (slot 0 is handed out twice after each wrap, as in MM2). With
  NumParts it splits into its BREAKnn pieces instead, each a hit instance
  with its own active taking the prop's velocity change at its own CG
  (linear and angular), and the prop's active is let go.
* Knocked-over props are simulated by their actives (`InitBoxMass(Mass,
  Size)`, `SmoothAngInertia(40)`, at rest) as their CollisionType says, and
  let go asleep (phSleep thresholds 0.1 / 0.5) or below y = −100, or (MM2:
  only taken off the active list) once in no room; the hit instance then
  rests where it stopped and can be hit again.
* Debris particles (dgBangerActive::Attach): InitialBlast particles of the
  prop's rule from `fxpt<TexNumber>`, born in the prop's frame; a stationary
  rule (BirthFlags 1) is born at the prop's CG in world space and only from a
  prop that was still standing. The particles end with the active.
* Drawing: lvlInstance's LOD rules with the dynamic objects' NoDraw limit
  (rendering.md), alpha test GREATER 100; the paint job is the variant modulo
  the model's paint jobs (`dgBangerInstance::SetVariant`), trees included.
* Lamp glows (dgBangerManager::InitGlow at night only, dgBangerInstance::
  DrawGlow): `s_yel_glow` cards facing the camera, half size 1.5 m × (0.99 …
  1.00, a random flicker each frame), white, added, unfogged, no depth
  writes; only props still standing. MM2 draws them for every prop of a
  visible room within the NoDraw distance; OpenMM2 with the prop's own
  visibility.
* Car parts (vehBreakableMgr::Eject) join as hit instances: the car's mesh
  part at its pivot with the car's paint job and the banger data
  `<car>_<part>`, given a momentum (not a velocity) of 4 ± 1 (wrecked cars:
  1.3 × the car's speed ± 1) in a random upward direction and an angular
  impulse of 1–3 about a random upward axis, so heavy parts barely move.
* Reset (lvlLevel::ResetInstances): every prop back in place, hit instances
  and actives released.

Movers (`dgBangerActiveManager::Update`, `dgPhysManager::DeclareMover`):
each active is declared by its data's CollisionType, checked in the order
0x2 (updated by the manager without collisions), 0x40 (type 2, flags
0x1b), 0x10 (type 1, 0x1b), 0x4 (type 1, 0x3: the city only); none of them
leaves it undeclared. In the age mode (dgBangerDataManager +0x2a8a8, which
mmGame::Init clears; `BangerSet::setAgeMode`) the active's age decides:
(1, 0x1b) up to the second age, (1, 0x3) up to the first, then the
manager's own update; with mmGame::Init's ages (6 and 30000 s) that is
(1, 0x1b) for the first 6 s and the manager's update afterwards.
`dgPhysManager::Update` detaches a type-1 mover whose room is neither the
room of the player's car (type 4), an opponent's or a network player's
(type 3) nor a neighbour of one: for a knocked-over prop that is
`dgHitBangerInstance::Detach`, which detaches its active and takes it out of
its room, so a prop still moving vanishes when the cars leave it behind. A
prop still standing (`dgUnhitBangerInstance`) keeps lvlInstance's empty
Detach.

## Network games

MM2 sends nothing about props: every machine knocks its own with its own
simulation of every car (`mmNetObject`, `mmGameMulti`). OpenMM2's host is the
authority instead (docs/multiplayer.md, "Props"); BangerSet carries what that
needs, all of it inert in a single-player race:

* The placed props' indices name them on every machine (they are placed in
  the same order everywhere); a hit instance remembers the placed prop it came
  from (`Instance::source`, with `part` for a BREAKnn piece) or the tag its
  thrower gave a car part (`Instance::tag`). The ring's slots count how often
  they were handed out (`generation`).
* `takeKnocks` (with `recordKnocks(true)`): the placed props that broke loose
  (`dgUnhitBangerInstance::Impact`) and what hit them.
* A client (`setReplica`): only its own car and the props it simulates may
  touch its props (`phys::Instance::acceptsContact`); `breakPlaced` takes a
  prop out of its room as Impact does but without a body; `restoreStanding`
  undoes a knock its car predicted that the host did not make (Reset for one
  prop); `showMirror` / `hideMirror` show the host's ring slots in instances
  of their own, drawn where the host has them. An active attached to a
  mirror starts from the host's motion (`dgBangerActive::Attach` starts a
  placed or resting prop at rest).
* A moving kinematic body allowed to (`phys::Body::kinematicBreaksBangers`,
  the host's copies of the other players' cars) breaks a banger as a body of
  its mass would (`calcBangerImpact`); every other kinematic body leaves it
  standing, as before.
