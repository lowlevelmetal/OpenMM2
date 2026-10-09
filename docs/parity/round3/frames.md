# Parity round 3: frames

Audited against MM2Recomp (midtown2.exe build 3393) on 2026-10-08.

Theme: every model, part, glow, shadow, decal, particle emitter and light
OpenMM2 draws, and every object it positions, uses the frame MM2 does:
which matrix (an instance's `GetMatrix` / `GetPosition`, the CG frame or the
model origin, a part's pivot from its `.mtx`, an xref's matrix), which mesh
offset (CG-centred banger bodies, parts modelled from a pivot or from the
base, the PKG `offset` chunk), and which order of multiplication and signs
(row vectors, -Z forward, `Matrix34::Dot`, `MakeRotateY`, `Vector3::RotateY`,
`Rotate` against `RotateFull`).

The maintainer's report ("the lights on the street lights do not line up
with where they are supposed to be") was the traffic lights' bodies standing
at the pole's base instead of its CG, fixed before this round (b9f0f7d). The
street lamps (banger glows) were already right; see the screenshots below.

Summary: 68 cases; verified 56, fixed 6, deviation 2, inferred 1, open 0,
not applicable (no retail data) 3. Fixes: cd820fc, ba78c68, 5eb4cd1,
657657e, abf27b8 (and b9f0f7d, verified).

Conventions checked once for all: `Mat34` rows are MM2's `Matrix34` rows
(m0 right, m1 up, m2 back, m3 position; row vectors), `Mat34::mul(a, b)` is
`Matrix34::Dot(a, b)` (a then b), `Mat34::rotationX/Y/Z` are
`Matrix34::MakeRotateX/Y/Z` (MakeRotateY: m0 = (c, 0, -s), m2 = (s, 0, c)),
`Mat34::rotationY(a).transformDir(v)` is `Vector3::RotateY(v, a)` (x' = c x +
s z, z' = c z - s x), `Matrix34::Rotate` turns the 3x3 part only and
`RotateFull` the position too, `Matrix44::Dot(r, a, b)` is r = b a and the
one-argument `Dot(this, a)` is this = this a, and MM2's view matrix is the
camera's `FastInverse` times diag(1, 1, -1) (`gfxRenderState::SetCamera`,
`sm_FullComposite`). The PKG `offset` chunk is read by neither game: the
only "offset" string in midtown2.exe is `crBoneData::Load`'s token.

## Props (bangers)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `BangerSet::addOne` / `add` (frame) | `dgUnhitBangerInstance::Init`, `dgUnhitYBangerInstance::SetMatrix` / `GetMatrix`, `dgUnhitMtxBangerInstance::GetMatrix` | verified | the placement plus R * CG with the full placement matrix, then the Y form's rows (c, 0, s), (0, 1, 0), (-s, 0, c) or the whole matrix; `GetPosition` is that frame's m3. Banger bodies are centred on their CG (sp_lightstreet_f y -3.851..3.851 about CG y 3.862; sp_traflitsingle_f -3.892..3.892 about 3.892) |
| `BangerSet::draw` body | `dgBangerInstance::Draw` | verified | the LOD mesh at GetMatrix |
| `BangerSet::draw` trees | `dgBangerInstance::DrawTree`, `dgTreeRenderer::RenderTrees` | verified | the high LOD at GetMatrix |
| `BangerSet::draw` lamp glows | `dgBangerInstance::DrawGlow`, `dgBangerData::Load` | verified | each GlowOffset through GetMatrix (the CG frame), a 1.5 m card with a 1% flicker, while the prop stands (flag 1). The offsets are in the CG frame in the data: sp_lightstreet_f's (-3.25, 3.65, -0.03) is 7.51 m up at the lamp head 3.25 m along -X (night screenshot). NumGlows missing means one glow (kept) |
| no banger shadow | `dgBangerInstance::DrawShadow` (empty), `DrawShadowMap` (no caller draws it) | verified | no retail banger PKG has a SHADOW part either |
| `BangerSet::unhitImpact` parts | `dgUnhitBangerInstance::Impact`, `InitBreakables` | verified | each BREAKnn hit instance at the prop's ground point (GetMatrix less R * CG) plus R * the part's CG; the BREAKnn meshes are centred on their own CGs (sp_lightstreet_f BREAK02, the arm, -1.727..1.727) |
| hit instances follow their actives | `dgBangerActive::Update` (SetMatrix of the ICS matrix) | verified | the ICS frame is the CG frame |
| debris | `dgBangerActive::Attach`, `asParticles` | verified | born through the active's ICS matrix; stationary rules at the CG |
| `ejectPart` frame | `vehBreakableMgr::Eject` | verified | the breakable's identity-at-pivot matrix dotted with the car's (no CG of the part's data) |
| `placeXrefs` | `lvlLevel::LoadInstances` (xrefs), `dgUnhitBangerInstance::Init` | verified | Dot(xref, record), then + R * CG |
| glass props | `dgGlassInstance::Draw`, `ptxGlass` | not applicable | no retail banger sets BillFlags 0x100 |
| parked cars | `gizParkedCarMgr::Init` | verified | full-matrix props turned a quarter turn (world-objects) |

## Traffic lights

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Signal::frame`, `AiRenderer::drawSignal` body | `aiTrafficLightInstance::Init`, `Draw` | verified | b9f0f7d: the pole's base + R * CG, X along the unit XZ direction to trafficLightAxis, Z = (-x.z, 0, x.x); the first shader set |
| `signalGlowFrame`, glows | `aiTrafficLightInstance::DrawGlow` | verified | b9f0f7d, now from the prop's matrix: GetMatrix less R * CG (summed y, z, then x of the CG); geometry slot id + 2 + NumParts (+5 at night) |
| no traffic light shadow | `dgBangerInstance::DrawShadow` | verified | Init adds a SHADOW set but nothing draws it (and no retail light has the part) |
| no banger glow cards | `dgBangerInstance::DrawGlow` (the end of DrawGlow) | verified | NumGlows 0 in the light data |
| `addTrafficLightProps`, `PlacedProp::ownerDrawn`, `AiRenderer::setSignalFrames` | `aiTrafficLightInstance` (a `dgUnhitYBangerInstance`), `aiTrafficLightSet::SetFourWay`, `dgUnhitBangerInstance::Impact` | fixed | abf27b8. The pole is an unhit Y banger of its model's data (flags 0x13, the data's bound: a hotdog of YRadius x Size.y at the CG) in the room of its CG; cars collide with it and break it into BREAK01/02, after which neither body nor glows are drawn (it leaves the rooms, flag 1 cleared) and the parts are ordinary hit bangers. OpenMM2 only drew the poles: cars drove through them. Placed where aiMap::Init places them (before the cable cars), so the pedestrians' obstacle list includes them, as aiPath::AddBangersToObsMap (after SetFourWay) does |

## World objects

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Gizmos::repositionBridge`, draw | `gizBridge::Reposition`, `gizBridge::Cull` (`dgBangerInstance::Draw`) | verified | identity, z = -Size.z / 2, RotateFull about X, Dot(hinge frame), plus the offset; no CG term |
| train cars | `gizTrainCar::Update` | verified | spline frame, position y + CG.y only |
| ferries | `gizFerry::Update` | verified | spline frame + CG.y, Y banger |
| sailboats | `gizInstance::SetMatrix`, `Draw` | verified | the matrix with m3.y + CG.y |
| cable cars | `aiCableCarInstance::Draw`, `GetMatrix` | verified | the car's own matrix; its BODY is modelled from the base (y 0..3.29), the SHADOW and HLIGHT parts are never drawn |
| gizmo glows | `gizBridge::Init` (flag 1 cleared), `lvlInstance::DrawGlow`, `dgBangerInstance::DrawGlow` | verified | none drawn |

## Checkpoints, powerups, Cops and Robbers

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `hud::standMatrix` | `mmWaypointObject::mmWaypointObject`, `Move`, `mmCheckpointInstance::Draw` | verified | Rotate(Y, -heading) at y + height / 2, drawn Dot(Scale(r, 7.5, r), GetMatrix). `SetHeading` leaves a re-headed zero-heading stand 3.75 m low (game-flow's open row): of the retail waypoint files only race/sf/circuit10 has such stands, and SF lists circuits 0-9 only |
| C&R bases | `mmBillInstance::Draw` | verified | the view's transposed 3x3 (the camera's rotation) at GetPosition, scaled |
| C&R gold | `mmPowerupInstance::Draw`, `mmWaypointObject::Move` | verified | Rotate(Y, 3 t) dotted with the waypoint's matrix 1.5 m up; its heading only shifts the spin's phase |
| carried gold icon, arrow, dash | `mmCRHUD`, `mmArrow`, `mmDashView` | verified | camera frame (hud-views) |
| opponent icons, map blips | `mmIcons::RegisterOpponents`, `mmHudMap` | verified | the opponents' ICS matrices (`+0x54` of the entity's ICS) |

## Vehicles

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `CarSim::modelMatrix`, body | `vehCarSim::SetWorldMatrix`, `vehCarModel::Draw`, `GetMatrix` | verified | the ICS matrix moved by R * CenterOfGravity; bodies are modelled from the ground origin (vpdune y 0.256..1.596) |
| wheels, hubs | `vehCarModel::Draw`, `vehWheel` | verified | the wheels' drawing matrices (vehicle area) |
| WHL4/5 | `vehCarModel::Draw` | verified | WHL2/3's matrices moved (0.2 + 2) radii along m2 |
| fenders | `vehCarModel::Draw`, `Init` | verified | m0 of wheel 0/1, m1 of the body, m2 = m0 x m1; fndr0 pivot less wheel 0's + 2.5 cm, x mirrored |
| breakables, variant part | `vehBreakableMgr::Draw` | verified | identity at the pivot dotted with the car's matrix |
| DECAL, reflection | `vehCarModel::Draw`, `modShader::BeginEnvMap` | verified | at the car's matrix; the env map is indexed by camera-space normals through the camera's rotation with z flipped back: world-space normals |
| SHOCK, ARM, SHAFT, AXLE, ENGINE | `vehCarModel::Draw` (suspension and engine matrices) | not applicable | no retail car has these meshes |
| shadow | `vehCarModel::DrawShadow`, `lvlInstance::DrawPhysics` | verified | DrawPhysics of the car's matrix (carsim + 0x1d4) |
| TLIGHT, BLIGHT, RLIGHT | `vehCarModel::DrawGlow` | verified | at the car's matrix |
| headlight cards (positions) | `vehCarModel::DrawHeadlights` | verified | pivot through the car's matrix |
| headlight sweep | `vehCarModel::DrawHeadlights`, `Vector3::RotateY`, `ltLight::Default` | fixed | 657657e: world-space directions (start (0, 0, -1)), the car's forward axis without the siren, turned by +-42.411503 rad/s of frame time from wherever they point with it; OpenMM2 turned the car's forward axis by the siren's accumulated angle (followed the car, jumped when the angle wrapped) |
| headlight sweep in the mirror | `vehCarModel::DrawHeadlights` | deviation | MM2 turns the lights again when the mirror's view draws the glows; OpenMM2 once a frame |
| siren lights, lens flares | `vehSiren::AddLight`, `Update`, `Draw`, `vehCarModel::InitSirenLight` | verified | positions through the car's matrix, directions in world space turning about Y |
| trailers | `vehTrailerInstance::Init`, `Draw`, `DrawShadow` | fixed | ba78c68: the body alone below H; at H TLIGHT (object pass: its black, fully transparent material under the alpha test, inferred invisible) while the tow car brakes over 0.1 and TWHL0-3 at the wheel matrices; no reflection, decal, breakables, fenders, hubs, TWHL4/5, texel damage or glows. OpenMM2 drew trailers as cars: TWHL0/1's medium and low meshes (modelled away from their pivots, e.g. vpsemi_trailer TWHL0_M z -3.78..-2.83) at the front wheels' matrices, a reflection, and TLIGHT as an added glow |
| trailer shadow | `vehTrailerInstance::DrawShadow` | verified | DrawPhysics of the trailer's matrix |
| rear-view mirror | `mmMirror::Cull` | verified | Dot(mirror matrix, the player's carsim + 0x1d4) |
| car cameras | `camCarCS::Init` | verified | carsim + 0x1d4 (model origin) |
| exhaust and damage smoke | `vehCarDamage::Update`, `SpewSmoke` | verified | SmokeOffset and the exhaust pivots go through the ICS matrix (the CG frame, not the model origin), as OpenMM2 does |
| shards, sparks | `vehCarDamage::ApplyImpact`, `fxShardManager::EmitShards`, `asLineSparks::RadialBlast` | verified | the ICS matrix; the impact point |
| tracks | `vehCar::UpdateTrack`, `lvlTrackManager::Update` | verified | the wheel's contact (props-fx) |
| opponents, police | `vehCarModel` | verified | the same renderer and pose |
| network cars | `mmNetObject` | deviation | OpenMM2's networking: snapshots of the model matrix, wheels turned from the received speed and steering (MM2 simulates the remote car) |

## Traffic cars

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| body | `aiVehicleInstance::Draw`, `GetMatrix` | verified | the spline's matrix (model origin); with a body the ICS matrix copied after the integration (`aiVehicleActive::Update`; the ICS frame is the model origin: `Attach` sets it to GetMatrix) |
| rooms, visibility | `aiVehicleInstance::GetPosition` | verified | m3 + m1 |
| rail wheels | `aiVehicleInstance::Draw` | verified | Dot(RotateX(-tyre rotation) at the data's pivot, GetMatrix), WHL4/5 likewise, only at H |
| wheels with a body | `aiVehicleInstance::Draw`, `vehWheelCheap::Init`, `Update` | fixed | cd820fc: WHL0-3 unturned at the vehWheelCheaps' drawing matrices (pivot + compression along m1, Limit below off the ground, less 0.2 x lateral and 0.3 x longitudinal deflection), WHL4/5 unturned at their pivots raised by WHL2/3's drawn height less the wheel radius |
| shadow | `aiVehicleInstance::DrawShadow`, `lvlInstance::DrawPhysics` | fixed | cd820fc: with a body on the ground (DrawPhysics of the active's matrix); otherwise at GetMatrix while upright and on the ground when upside down |
| shadow gate | `aiVehicleInstance::DrawShadow` (spline + 0xe8) | inferred | MM2 skips the shadow while a spline short is 0 (mm2hook: "solved stop orientation"); OpenMM2 always draws it |
| BREAK0-3 | `aiVehicleInstance::Draw`, `vehBreakableMgr::Draw` | verified | at their pivots through GetMatrix, always H |
| TLIGHT, SLIGHT0/1, headlight cards | `aiVehicleInstance::DrawGlow` | verified | at GetMatrix; the card at the headlight0 pivot (and its mirror) pulled 0.2 m towards the camera |

## Pedestrians

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| skinned model | `aiPedestrianInstance::Draw`, `pedAnimationInstance::Draw`, `crSkeleton::Attach` | verified | bones through the pedestrian's matrix (aiPedestrian + 0x3c); 35 m from the camera's position |
| stick figures | `pedAnimation::DrawSkeleton`, `gfxRenderState::Regenerate`, `SetCamera` | fixed | 5eb4cd1: the quads are offset in the pedestrian's space by the modelview's first row (sm_Modelview [0][0..2]: the pedestrian's X axis in view space, z negated by sm_FullComposite); the camera's right axis while both only turn about Y, tilted under a pitched camera. OpenMM2 used the camera's right axis in world space |
| no shadow | `aiPedestrianInstance::DrawShadow`, `pedAnimationInstance::DrawShadow` | verified | empty |

## City and the view

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| static objects | `lvlFixedAny::Draw`, `lvlFixedMatrix` / `lvlFixedRotY::GetMatrix` | verified | the record's matrix (compact Y form rebuilt); no retail city model has the mask, refl, nonrandom, opaque or shadow parts |
| city object shadows | `lvlFixedAny::DrawShadow` | not applicable | no retail city model has a SHADOW part |
| multi-room objects, landmarks | `lvlMultiRoomInstance::Draw`, `lvlLandmark` | verified | the object's matrix |
| sky | `lvlSky::DrawHat` | verified | Rotate(Y, angle) at (eye.x, eye.y x scale + offset, eye.z) |
| road decals | `dgRoadDecalInstance::DrawShadow` | verified | world-space strip (props-fx) |
| showroom | `mmVehicleForm::Cull` | verified | wheels and extra parts at their pivots through the turning node |
| lens flares, rain | `ltLensFlare::Draw`, weather particles | verified | at the lights; camera-relative |

## Screenshots checked

`OPENMM2_DEBUG_FLY` / `OPENMM2_DEBUG_CAMERA`, 1280x720:

- SF at night, camera (-1478, 42, 290, yaw -0.61): the traffic lights' poles
  under their glows, the street lamps' glows at the lamp heads (before and
  after the traffic light props: identical, the props draw at the same frame).
- SF at night, camera (-1464, 41, 272, yaw -0.85): a street lamp's glow at
  its head, traffic lights from behind (one-sided glows).
- SF Blitz 0 in vpsemi at night, from the side and behind the trailer, the
  starting commit against round 3: the trailer's tail-light glows are gone
  (vehTrailerInstance draws none).
- London Blitz 0 from above: traffic lights at the corners, pedestrians.

## Noticed outside the theme

- Traffic cars never eject their BREAK0-3 parts: `TrafficImpact::breaks`
  (aiVehicleActive::Impact's vehBreakableMgr::Impact, threshold 2500) is
  raised but nothing ejects (ai-vehicles).
- The aiIntersection obstacle maps are built before SetFourWay places the
  traffic lights (so they leave the lights out) while the aiPath maps
  include them; OpenMM2's pedestrians have one obstacle list of every prop
  (ai-ambient-city, inferred).
