# Bangers (breakable props)

Code: `src/game/bangers/` (`BangerData`, `PropPlacement`, `BangerSet`),
tool: `mm2tool bangers <source> <city> [--png map.png]`.
Ported structure: Open1560 (MM1) `mmbangers/` (`mmBangerData`,
`mmUnhitBangerInstance::Impact`, `mmBangerActive`, `mmBangerActiveManager`) and
`asBound::Impact` in `game.asm`; MM2 layouts from mm2hook (documentation only).

## Data: `tune/banger/<model>.dgBangerData`

995 files; every one parses. Breakable parts are separate entries
`<model>_break01`, `_break02` … (271 files), one per `NumParts`.

| Field | Meaning | Evidence |
|---|---|---|
| Size, CG | collision/inertia box; CG above the model's ground origin. The prop's meshes (and its `BREAKnn` part meshes) are centred on the CG | verified: mesh bounds are symmetric about the CG |
| Mass, Elasticity, Friction | rigid body (`asInertialCS::SetMass`) | MM1 |
| ImpulseLimit2 | square of the largest impulse one hit can transfer to the prop | MM1 `asBound::Impact` caps the impulse at `sqrt(ImpulseLimit2)` |
| NumParts | breaks into the `_breakNN` entries | MM1 `Impact` |
| NumGlows, GlowOffset | lamp glows, offset from the CG | inferred (the offsets land on lamp heads) |
| TexNumber | debris particle sheet `texture/fxpt<N>` (2×2 frames: coins, letters, trash …) | MM2 keeps 20 sheets; pictures match the props |
| BirthRule | debris particles, blasted when hit | MM1 `mmBangerActive::Attach` |
| BillFlags, YRadius, ColliderId, CollisionPrim, CollisionType, SpinAxis, Flash, AudioId | read; mostly unused (CollisionPrim 2 = cylinder of YRadius, inferred; collision uses the Size box) | — |

## Where props come from

Street props are not in `city/<map>.inst` (which holds buildings). They come
from three sources, combined by `placeCityProps()`:

1. **Instances** with banger data: only the stop signs `sp_stop_f` in
   `city/<map>_ai.inst` (67 in London, 40 in SF).
2. **`city/<map>/props.pathset`** (PTH1): one path per model. MM2's
   `dgPath::Type` and `Spacing` are not separate fields in the file; in every
   retail path the **last point's spare word** encodes them: low byte = type
   (0 single points, 1 position/look-at pairs, 2 line strip), next byte =
   spacing in tenths of a metre (`0x1402` barricades every 2.0 m, `0x1C02`
   bollards every 2.8 m, `0x4002` trees every 6.4 m, `0xC802` pier cleats every
   20 m). **Inferred**, but consistent across both cities and every model.
3. **Street rules**: `city/<map>/propdefs.csv` (prop types: start, distance,
   max per sidewalk, lateral lerp, model variants) and `proprules.csv`
   (`nNNleft`/`nNNright` lists). Every PSDL road room with prop rule N gets
   rule `nNNleft` along its left sidewalk and `nNNright` along the right one
   (RoadStrip sections are outer L, curb L, curb R, outer R). Props stand
   `start + k × distance` metres along the curb, at most `maxUse` per
   sidewalk, `minLerp` of the way from curb to outer edge, at sidewalk height,
   with +X pointing away from the road (lamp arms are modelled along −X and
   reach over the street). **Inferred**: rule numbers n01–n16 (London) and
   n01–n20 (SF) match the room prop-rule values exactly. PSDL road-level prop
   values (`PsdlRoad::propRule`, left/right fractions) are not used yet.
   Model variants (`file1..4`) are used in turn.

Result: London 6,286 props (67 instances, 1,745 path-set, 4,474 street-rule),
every one with banger data.

## Behaviour

* Unhit props are static. Each frame, before the physics step, every vehicle
  box is tested against nearby props (16 m grid). A touch is a hit
  (`Impact`): the closing speed along the contact normal gives the impulse
  `(1 + e) v m_prop m_car / (m_prop + m_car)`, capped at
  `sqrt(ImpulseLimit2)`. The prop becomes a rigid body in the physics world
  with that impulse; the car gets the opposite impulse. Parking meters
  (limit 29 N·s) fly off; a 1 t car loses at most 0.8 m/s through a street
  lamp (818 N·s); statues and oak trees (limit 1e15) act as walls.
* Props with parts split into their pieces, each with its share
  (`impulse × part mass / prop mass`) at its own CG.
* At most 32 props are simulated (`MAX_ACTIVE_BANGERS`); the longest-active
  one is frozen when a new one is hit. A prop that falls asleep (MM1 sleep
  thresholds 0.5/0.5/0.5 s) stays where it lies and can be hit again;
  one below y = −100 is removed.
* Debris particles: `TexNumber` sheet with the BirthRule, `InitialBlast`
  particles at the hit (stationary rules at the prop, others moving with it
  plus the car's velocity), as `mmBangerActive::Attach`.
* Lamps with glows draw an additive `fxltglow` card at night (size inferred).

Differences from the original: the touch test and the contact response
after activation use OpenMM2's collision (MM1's collision manager is not
ported); hit props are never recycled (MM1 used a ring of hit instances).
