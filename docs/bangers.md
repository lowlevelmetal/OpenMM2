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
| CollisionPrim | 0 the `<name>_bound` geometry (else a box), 1 box of Size, 2 capsule of YRadius and length Size.y, 3 sphere of YRadius | MM2 (`dgBangerData::InitBound`); OpenMM2 collides with the Size box for all |
| YRadius | capsule/sphere radius and the unhit prop's touch radius | MM2 |
| CollisionType | mover flags (0x10 default, 0x40 for the concrete barricades) | read, not used |
| AudioId, SpinAxis, Flash | read by the loader, used by nothing | MM2 |

## Where props come from

`placeCityProps()` combines, in cityLevel::Load's order:

1. **Instances** with banger data in `city/<map>.inst` and `<map>_ai.inst`
   (the stop signs `sp_stop_f`: 67 in London, 40 in SF): the full matrix
   unless instance flag 0x80 asks for a Y rotation.
2. **Street rules** (cityPropulator, lvlSDL::Propulate): for every PSDL road
   with flag 0x40 (it has sidewalks), the prop rule N of its first room,
   rules `nNNleft` then `nNNright` from `proprules.csv` (prop1… columns),
   each prop's `propdefs.csv` row by column name. The random generator
   restarts (seed 1) per road. For each prop of a rule the whole road is
   walked along both sidewalks (curb and outer edge polylines through all
   the road's rooms: road strips use vertices 1/0 on the left and 2/3 on the
   right, divided roads 1/0 and 4/5), once per road strip of the first room:
   from `start` metres, every `distance`, at a random fraction minLerp…maxLerp
   from curb to outer edge, 0.15 m up, +X from curb to outer edge (tilting
   with the sidewalk); a prop stands only on its rule's side, at most maxUse
   per walk, choosing a random variant among file1–4. Joining the rooms'
   sections into one road (lvlAiMap::SetRoad) is approximated: sections
   repeated at a room boundary are dropped and rooms running backwards are
   reversed (inferred); MM2 also cuts interior corners by up to 0.1 m and
   pulls the curb in on roads with flag 0x400000 (not ported).
3. **`city/<map>/props.pathset`** (dgPath): the trailer after the last point
   (OpenMM2's parser reads it as the last point's spare word) holds a type
   byte and a spacing byte in quarter metres (0 = 5 m): `0x4002` trees every
   16 m, `0x1C02` bollards every 7 m, `0x1402` barricades every 5 m. Type 0
   puts an unrotated prop at every point; type 1 takes point pairs, the prop
   at the first with +X towards the second (flattened); type 2 places each
   segment on its own: floor(length / spacing) props at equal steps from the
   segment's start (none on shorter segments, none at the last point), +X
   along the segment, full matrix.

Props with a Y rotation (dgUnhitYBangerInstance) keep only m00 and m02 of
their matrix: rows (c, 0, s), (0, 1, 0), (−s, 0, c), not renormalised.

`city/<map>/decals.pathset` holds road decals (dgRoadDecalInstance): every
path of more than two points is a strip of point pairs 1 cm above the road
with the texture named like the path; u 0/1 across, v = floor(distance of
the pair from the start / the first pair's width + 0.5); drawn after the
street geometry, alpha blended, no depth writes, pulled forward, unlit.

## Behaviour

* Unhit props are static. Each frame, before the physics step, every vehicle
  box is tested against nearby props (16 m grid). On contact
  (dgImpact::CalcImpact; nothing while the contact separates):
  * J = the impulse that would stop the car's contact point against an
    immovable prop. If J² ≤ ImpulseLimit2 the prop holds like a wall and the
    car gets (1 + e) J back.
  * Otherwise the car spends sqrt(ImpulseLimit2) breaking it loose, and the
    rest is a two-body collision: the car and the prop share
    J2 = (1 + e) × residual closing speed / (1 / m_car + 1 / m_prop).
  OpenMM2 solves along the contact normal with the car's effective mass and
  the prop's mass; MM2 solves the full 3D impulse with a friction cone.
* A broken prop becomes a hit instance: a ring of 40 (dgBangerManager), so the
  oldest knocked-over prop disappears when the ring wraps. With NumParts it
  splits into its BREAKnn pieces instead, each flying with the parent's
  velocity change at its own CG (linear and angular).
* Knocked-over props are simulated by up to 32 actives
  (dgBangerActiveManager; when full the first of its list is released).
  phSleep sends a prop to sleep after 15 still updates (spin² ≤ 0.5 and
  speed² ≤ 0.1, or reversing from one update to the next); it then stays
  where it lies and can be hit again (as a normal collision). OpenMM2 tests
  the plain velocity where MM2 adds the positional pushes (OpenMM2's contact
  pushes would keep resting props awake). Below y = −100 the active is
  released too.
* Debris particles (dgBangerActive::Attach): InitialBlast particles of the
  prop's rule from `fxpt<TexNumber>`, born in the prop's frame; a stationary
  rule (BirthFlags 1) is born at the prop's CG in world space and only from a
  prop that was still standing. The rule's velocity is used as is. The
  particles end with the active.
* Drawing: lvlInstance's LOD rules with the dynamic objects' NoDraw limit
  (rendering.md), alpha test GREATER 100.
* Lamp glows (dgBangerManager::InitGlow at night only, dgBangerInstance::
  DrawGlow): `s_yel_glow` cards facing the camera, half size 1.5 m × (0.99 …
  1.00, a random flicker each frame), white, added, unfogged, no depth
  writes; only props still standing.
* Car parts (vehBreakableMgr::Eject) join as hit instances: the car's mesh
  part at its pivot with the car's paint job and the banger data
  `<car>_<part>`, leaving in a random upward direction at 4 ± 1 m/s (wrecked
  cars: 1.3 × the car's speed ± 1) and spinning at 1–3 rad/s.
* Reset (lvlLevel::ResetInstances): every prop back in place, hit instances
  and actives released.

Differences from the original: the touch test uses a box against the
vehicle's box (MM2: the collision manager with each prop's bound), and the
contact response after a prop is loose uses OpenMM2's collision.
