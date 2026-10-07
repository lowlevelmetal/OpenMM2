# Physics and vehicle simulation

`src/phys` (library `mm2_phys`, namespace `mm2::phys`) simulates rigid bodies
and Midtown Madness 2 vehicles. Most of it is a port of the Midtown Madness 1
vehicle code (same Angel engine) from Open1560's `code/midtown/game.asm`, a
symbol-named MASM disassembly of MM1 beta build 1560, GPL-3.0
(<https://github.com/0x1F9F1/Open1560>). MM2-specific behaviour is adapted
where MM2's tune data shows a difference. Nothing comes from the MM2
executable.

## Evidence levels

| Level | Meaning |
|---|---|
| **Ported** | Translated from MM1 (Open1560 `game.asm`) operation by operation in 32-bit float. |
| **Ported (MM1 C++)** | Taken from code Open1560 already rewrote in C++. |
| **MM2 adaptation (inferred)** | MM2 renamed or replaced the MM1 mechanism; the mapping is our inference from field names, MM2 class layouts (mm2hook headers, used as documentation only) and the tune values. |
| **OpenMM2** | Our own code where nothing is known (collision). |

## How a sample runs

`World::step(dt)` follows the Angel node order of a car (`mmCarSim` children:
Engine, Transmission, ICS, Stuck; the ICS's LCS child holds the axles, gyro,
aero, force and drivetrains, whose children are the wheels):

1. `BodyController::beforeIntegrate`: `mmCarSim::Update` (automatic "park"
   brake, wheel inputs, damage, impact parameters), then `mmEngine::Update`
   and `mmTransmission::Update`.
2. `InertialCS::update`: `asInertialCS::Update` = `FinishForces` (impulse
   averaging, FrameVelocity, sleep test, gravity) + `FinishUpdate`
   (momentum-based semi-implicit Euler, angular-velocity limit, rotation by
   `invsqrtf_fast` angle). This integrates the forces accumulated during the
   previous sample.
   Bodies linked by a joint skip this (`ICS_CONSTRAIN_LINK`); right after
   the free bodies, each `Joint3Dof::update` integrates its two bodies
   together (see [Trailers](#trailers-joint3dof-and-mmtrailer)).
3. `afterIntegrate`: axles (visual), gyro, aero, then each drivetrain solves
   its wheel speed and updates its wheels, which accumulate suspension and
   tyre forces for the next sample; then `mmStuck::Update`.
4. Collisions (OpenMM2), impact reports.

The original oversampled: n = min(floor(frame / SampleStep) + 1, MaxSamples)
samples of frame / n, with MM1's physics manager at SampleStep = 1/35 s,
MaxSamples = 20 (MM2's values are unknown). `World::advanceOversampled`
reproduces that; `World::advanceFixed` (default 1/60 s, inside the original's
range of 1/70..1/35 s) is deterministic for replays and network play.

The ported code keeps the original operation order and float32 arithmetic
(the original ran the x87 in single precision); operations with double
constants go through `age::mulD` etc., and `mm2_phys` is built with
`-ffp-contract=off`. Generic helpers (matrix products, the arbitrary-axis
rotation path) use a straightforward order, so results can differ from the
original in the last bit.

## Parameters

### vehCarSim

| Field | Use | Evidence |
|---|---|---|
| Mass, InertiaBox | `asInertialCS::SetMass`: I = m/12 (y²+z², x²+z², x²+y²) | Ported |
| CenterOfGravity | Rigid body is centred here; wheel centres are relative to it (`vehWheel::Init` takes the CG) | MM2 adaptation (mm2hook layout) |
| BoundFriction, BoundElasticity | `RestoreImpactParams` → ICS friction/elasticity; upside down or wrecked: elasticity 0, friction 2, brakes 1 (`SetHackedImpactParams`) | Ported |
| DrivetrainType | 0 RWD, 1 FWD, 2 4WD; `ConfigureDrivetrain` wheel/drivetrain wiring and update order | Ported |
| SSSValue, SSSThreshold | Speed-sensitive steering: above SSSThreshold mph steering × max(SSSValue, threshold/speed); off when threshold ≤ 0 | MM2 adaptation (inferred, low) |
| CarFrictionHandling | Scales surface friction below 1: F − (CFH−1)(1−F) if CFH < 1, else F/CFH | Ported (MM1 has the same field) |

### Aero (vehAero)

| Field | Use | Evidence |
|---|---|---|
| AngCDamp, AngVelDamp, AngVel2Damp | Body-axis angular damping as acceleration × inertia: −(C·sign ω + V ω + V2·trunc(\|ω\|ω)); the quadratic term is truncated to an integer as in the original | Ported (`asAero::Update`) |
| Drag | −Drag·\|v\|·v newtons | MM2 adaptation (inferred; MM1 had per-axis CDamp/VelDamp/Vel2Damp scaled by mass) |
| Down | −Down·v² along the car's up axis, newtons | MM2 adaptation (inferred) |

### Engine (vehEngine)

| Field | Use | Evidence |
|---|---|---|
| MaxHorsePower, OptRPM | T(w) = (φ·w_opt − w)(w_opt/φ + w)·Pmax/w_opt³, φ = (√5+1)/2, Pmax = hp × 746 W | Ported (`ComputeConstants`, `CalcTorqueAtFullThrottle`); MM2's vehEngine keeps the √5±1 constants |
| MaxRPM | Rev limit: torque 0 above it; drivetrain caps wheel speed at MaxRPM in gear | Ported |
| (taper above OptRPM) | MM1 multiplies by (Max + w − 2·Opt)(Max − w)/(Max − Opt)² | Ported, **off by default**: MM2's vehEngine has no TorqueDropoff member and vpdb7 (UpshiftBias 0.002) never upshifts with it (`Engine::mm1TorqueTaper`) |
| (engine braking) | T0 = Pmax/w_opt · 0.5 · (160 − w)/(w_opt − 160); throttle blends T0 and T | Ported |
| GCL | Gear-change lag: torque 0 and displayed RPM blended for GCL seconds after a shift | Ported |
| AngInertia | Engine inertia while the clutch is open (neutral); MM1 used mass × 0.001 | MM2 adaptation (inferred) |
| IdleRPM | Not used by the physics (MM1 has no idle; zero-throttle torque crosses zero at 160 rad/s) | — |

### Transmission (vehTransmission)

| Field | Use | Evidence |
|---|---|---|
| (gear slots) | 0 reverse, 1 neutral, 2.. forward; NumGears counts all slots | Ported |
| (shift logic) | Automatic: up if RPM > UpshiftRPM[g] (not in top gear, no shift pending), down if RPM < DownshiftRPM[g] (not from first), kickdown if throttle > 0.8 and RPM < kickdown[g]; only after GearChangeDelay in gear | Ported (`mmTransmission::Update`) |
| (auto reverse) | Automatic: brake > 0.8 with throttle < 0.1 below 5 m/s toggles drive/reverse and swaps the pedals | Ported (`mmGame::UpdateSteeringBrakes`, `mmInput::SwapThrottle`) |
| (park) | Automatic, no throttle, < 0.5 m/s: brakes held at 0.5 | Ported |
| Low, High, Reverse | Gear speeds in mph at MaxRPM → ratio = w_max·R/v (driven wheel radius) | MM2 adaptation (medium: MM2 has GearRatioFromMPH; MM1-format ratios 28/6.5 give ≈ 20/90 mph like Low 20/High 90). Cars reach High as their top speed in tests |
| GearBias | Intermediate gear speeds: lerp(linear, geometric, GearBias) between Low and High | MM2 adaptation (low) |
| AutoNumGears, ManualNumGears | Slot counts of the automatic and manual tables | MM2 adaptation (high) |
| UpshiftBias | UpshiftRPM = MaxRPM (1 − bias) | MM2 adaptation (inferred) |
| DownshiftBiasMax/Min | Down when the next lower gear would be below MaxRPM (1 − Max); kickdown with Min | MM2 adaptation (inferred) |
| GearChangeTime | MM1's GearChangeDelay: minimum time in gear before an automatic shift | MM2 adaptation (medium) |
| GearRatios, UpshiftRPM, DownshiftRPM, ManualGearRatios, DownshiftBias, NumGears | MM1 layout (vpcoop_opp etc.): used as given | Ported |

### Drivetrain / Freetrain (vehDrivetrain)

The engine-driven drivetrain integrates the shared wheel speed implicitly,
w' = w + dt·(−net)/(I + dt·D), where net = torque × ratio − Σ tyre resistance
and brakes. D = 300 + Σ tyre slopes; the step stops at each wheel's
saturation speed B and continues with that wheel's slope removed. Brakes hold
a stopped wheel and never reverse a spinning one. **Ported**
(`mmDrivetrain::Update`), with these MM2 mappings:

| Field | Use | Evidence |
|---|---|---|
| BrakeDynamicCoef, BrakeStaticCoef | brake torque coefficient = AngInertia × coef (MM1: 2·mass and 2.4·mass, i.e. ratio 1.2 = BrakeStaticCoef/BrakeDynamicCoef) | MM2 adaptation (medium) |
| AngInertia | inertia = AngInertia × 0.01 + 0.2 × Engine.AngInertia × ratio² attached, AngInertia × 0.005 free (MM1: mass × (0.02 + 0.0002 ratio²) / mass × 0.01; identical for MM2's typical 2000 = 2 × 1000 kg) | MM2 adaptation (inferred); MM1-layout files default to 2·mass |
| (tyre slope) | MM1 computes min(R²(RubberSpring + RubberDamp/dt), R²/\|V\| · 2·StaticFric/s0 · load) but build 1560 discards it (A = C = 0). Heavy MM2 tunes diverge without it, so we use it below B (A) and drop it past B (C = 0) | **Deviation** from MM1 1560; formula ported |
| (brake split) | MM1: front drivetrains feel only the foot brake, back ones only the handbrake. We apply the foot brake to all and max(foot, handbrake × HandbrakeCoef) to the back | MM2 adaptation (inferred; MM2 gives every wheel BrakeCoef and HandbrakeCoef) |

### Wheel (vehWheel)

**Ported** (`mmWheel::Update`, `ComputeDwtdw`, `ComputeConstants`, `Init`):
probe from the top of travel to full droop; suspension load = Spring·s +
Damping·ṡ (ṡ clamped ±3 m/s) + static load, projected on the ground normal;
tyre displacement per direction moves with the slip velocity towards
friction(slip)·max(load, static load)·Friction/RubberSpring and never past
it; force = −RubberSpring·disp − RubberDamp·rate (lateral rate blended by
*Realism*, 1); friction(slip) = StaticFric(2s/s0 − s²/s0²) until it falls to
SlidingFric; steering rotates about world Y; rear wheels steer opposite;
surface friction × WeatherFriction outdoors; LongSlideMultiplier 1.

| MM2 field | MM1 equivalent | Evidence |
|---|---|---|
| SuspensionExtent, SuspensionFactor | Spring = SuspensionFactor · static load / SuspensionExtent (load reaches zero at full droop); droop range = Extent | MM2 adaptation (inferred) |
| SuspensionLimit | compression travel (probe top) | MM2 adaptation (high; same name) |
| SuspensionDampCoef | Damping = coef · Spring (MM1 default Damping/Spring = 4000/40000 = 0.1 = MM2's modal value) | MM2 adaptation (medium) |
| TireDispLimitLong/Lat | RubberSpring = StaticFric · static load / limit (displacement at full friction) | MM2 adaptation (inferred) |
| TireDampCoefLong/Lat | RubberDamp = coef · 2√(RubberSpring · m/4) (MM1's bus tune has ζ ≈ 0.22) | MM2 adaptation (inferred) |
| TireDragCoefLong/Lat | rolling/scrub drag = coef · load against the motion, applied to the body (faded in below 0.5 m/s) | MM2 adaptation (inferred) |
| OptimumSlipPercent, StaticFric, SlidingFric | same | Ported |
| SteeringLimit | SteeringRatio | MM2 adaptation (high) |
| BrakeCoef / HandbrakeCoef | BrakeRatio / handbrake | see drivetrain |
| SteeringOffset, CamberLimit, WobbleLimit | visual only | inferred |
| (static load) | Mass · g / 4 | Ported |
| (material) | friction, depth (sink; depth ≥ 1 = water, no support), drag; MM1 used the polygon's phys material | Ported + MM2 materials.mtl |

### Other

| Item | Use | Evidence |
|---|---|---|
| vehAxle TorqueCoef/DampCoef | anti-roll coupling between left and right travel; **off by default** | inferred (low) |
| vehGyro Drift/Spin180/Reverse180 | yaw assist with handbrake / when sliding; **off by default** (MM1's VehGyro was a different helper) | inferred (low) |
| vehStuck | after an impact, if within PosThresh for TimeThresh while pegged (throttle > 0.75, \|steer\| > 0.5, not reverse) yaw in place by (\|steer\|+1)·Turn·dt until MoveThresh away. Rotation/Translation unused | Ported (`mmStuck`); Turn = MM1 RotAmount (inferred) |
| vehCarDamage | impacts closing > 4 m/s add their impulse (if ≥ ImpactThreshold); Damage = (cur − Med)/(Max − Med) | Ported (`mmCar::Impact`, `UpdateDamage`); threshold inferred |
| vehTrailer / dgTrailerJoint | see [Trailers](#trailers-joint3dof-and-mmtrailer) | Ported (`mmTrailer`, `Joint3Dof`) + MM2 mapping |
| Gravity | 19.8 m/s² | Ported (MM1 sets PHYS gravity to −19.8); MM2 unverified |
| Materials | `city/materials.mtl` (elasticity, friction, drag, width, height, depth, sound, ptx) | data format |

## Trailers (Joint3Dof and mmTrailer)

vpsemi (a tiller ladder truck) and vpcentury (a semi with a flatbed) tow a
trailer. `phys/Joint3Dof` and `phys/vehicle/Trailer` port MM1's `Joint3Dof`
(`Init`, `InitJoint3Dof`, `SetPosition`, `SetFrictionLean/Roll`,
`SetLeanLimit`, `SetRollLimit`, `SetRestOrientMat` (both), `SetJointForceFlag`,
`Break/UnbreakJoint`, `Update`, `DoJointTorque`, `DoJointLimits`, both
`GetCMatrix` overloads, the `discrepancy` global, `CrossProdMatrix`) and
`mmTrailer` (`Init`, `Reset`, `Update`, `RestoreImpactParams`,
`SetHackedImpactParams`) plus the trailer setup in `mmCar::Init`, with the
Matrix34 routines they use (`Inverse`, `Dot`, `Dot3x3`, `Transpose`,
`RotateAbs`) and `asInertialCS::CalcCMatrix`/`GetCMatrix`. MM2's
`dgTrailerJoint` derives from `phJoint`, MM2's name for the same base
(ICS1/ICS2/Offset1/Offset2 in mm2hook's layout).

**How the joint works.** `InitJoint3Dof` marks both bodies
`ICS_CONSTRAIN_LINK`, so `asInertialCS::Update` skips them, and
`Joint3Dof::Update` integrates them instead, once per sample:

1. `FinishForces` on both bodies; collision matrices C1, C2 at the joint
   (`CalcCMatrix`: InvMass·I + (R X)ᵀ diag(InvInertia) (R X)) and
   K = (C1 + C2)⁻¹; world inertia tensors Rᵀ diag(I) R and their inverses.
2. If friction or a lean limit is set: `DoJointTorque`. With the rest
   orientation O = rows (1,0,0) (0,0,−1) (0,1,0) used for trailers, the joint
   axis of each body is its up axis: **lean** = angle between the two up axes
   (relative pitch/roll), **roll** = rotation about the trailer's up axis (the
   hitch angle). Lean torque = (RestoreForceLean·lean·kS)·axis −
   DampConstLean·kC·unit(rate) − DampLinearLean·kD·rate with
   kS = m_eff·40/π, kD = m_eff·2·√(40/π), kC = 10·m_eff,
   m_eff = (|o1||o2|)/(|o2|/m1 + |o1|/m2); the constant term only acts when
   some component of the relative rate is ≥ 1 rad/s (the original truncates
   to int). The roll torque uses the same constants over
   1/(InvInertia1.z + InvInertia2.z) and a sign(rate) constant term.
3. The constraint force: K · (free relative acceleration of the two joint
   points, including ω×(ω×r) and gyroscopic terms, + 0.33·relative
   velocity/dt).
4. Lean/roll limits (`DoJointLimits`): an angular impulse that stops the
   approach to the limit, scaled by (elasticity + 1), coupled back into the
   force through K.
5. Break test (ForceLimit), torques r×F, the force rotated by half the
   sample's mean rotation (and scaled by 1 − θ²/24 or 2 sin(θ/2)/θ), applied
   ±F; the bodies share their pushes; `FinishUpdate` on both.
6. `discrepancy` = trailer hitch − tractor hitch, then `SetPosition` moves
   the trailer onto the tractor's hitch point (the original computes
   ½(p1 + p1), i.e. the tractor's point).

World runs the joints right after the free bodies' integration, and bodies
sharing an unbroken joint do not collide with each other (their boxes
overlap at the hitch). MM1 collides them through the two-body
`GetCMatrix(ics1, ics2, …)`, which is ported (it builds both couplings from
the first body's lever, as the original does) but not used: tractor/trailer
collision is not implemented. Contacts with the world still use each body's
own effective mass, not the joint-aware `GetCMatrix`.

**MM2 data mapping.**

| MM2 field | Joint3Dof / mmTrailer | Evidence |
|---|---|---|
| dgTrailerJoint Offset0 / Offset1, or vehTrailer CarHitchOffset / TrailerHitchOffset | hitch in tractor / trailer model space; `InitJoint3Dof` takes them relative to each body's CG. vpcentury has the former, vpsemi the latter; the joint file wins if both exist | MM2 adaptation (inferred) |
| RestoreForceLean, DampConstLean, DampLinearLean | `SetFrictionLean(restore, const, linear)` | names match; MM1's hard-coded values (2, 0.9, 2) equal vpcentury's old `tune/vpcentury.dgTrailerJoint` |
| RestoreForceRoll, DampConstRoll, DampLinearRoll | `SetFrictionRoll` | as above (MM1: 2, 0.1, 2) |
| LeanLimit, LimitElasticityLean | `SetLeanLimit` | as above (MM1: 0.3, 0; retail files: 3.0, 0) |
| LimitElasticityRoll | `SetRollLimit` elasticity | names match |
| NegativeRollLimit, PositiveRollLimit | `SetRollLimit` limits; no retail file sets them, so `Init`'s −π/+π apply (MM1's `mmCar::Init` used ±0.3, a 17° hitch limit) | mm2hook layout; default inferred |
| ForceLimit | break force. vpsemi has 40, which would break at once in newtons, so the MM2 unit is unknown: joints are unbreakable | inferred |
| JointStatus | 2 in both files; joints start joined | inferred |
| FreeRange, FreeLean, FreeRoll | vpsemi only. FreeLean/FreeRoll are used as free play subtracted from the lean/roll angle before the restoring spring (MM2's layout also has CosFreeLean); FreeRange is read and unused | inferred (low) |
| vehTrailer Mass, InertiaBox | `SetMass` | MM1 used the TRAILER_H box |
| vehTrailer Drivetrain | the four free drivetrains (vpsemi: AngInertia 5000); absent: MM1's 2 × tractor mass | MM2 adaptation |
| vehTrailer WheelFront / WheelBack | TWHL0/1 (`mmWheel` flags 1) and TWHL2/3 (flags 3, handbrake), 4 wheels' share of the trailer mass each | Ported |
| (CG) | trailer **model origin**. vehTrailer has no CG; MM1 passes the TRAILER_H box centre (`GetCentroid`). That centre is 2.26 m up on vpsemi's ladder trailer, which then rolls over in fast turns at MM2's tyre grip (`simcar vpsemi --steer 0.15 --trailer-cg mesh`: up.y 0.006), while MM2 puts its tractors' CGs near the ground (CenterOfGravity y −0.1/−0.2) | inferred |
| (BoundElasticity/Friction) | the tractor's (vehTrailer has no fields; MM1 read them from the trailer file) | inferred |

`mmTrailer::Update` gives the trailer wheels the tractor's inputs: front
wheels `steer`, back wheels `−steer`, brakes × BrakeCoef. So vpsemi's tiller
axle (WheelBack SteeringLimit 0.18) steers against the tractor, and so does
vpcentury's trailer axle (its WheelBack SteeringLimit is 0.39), which keeps
its hitch angle small in turns. Whether MM2 still counter-steers vpcentury's
trailer is unverified. The free drivetrains feel the tractor's brakes and
handbrake (`mmDrivetrain::Init` with the tractor's mmCarSim).

**Wheels.** vpcentury's TWHL0/1 pivots are degenerate (radius 0.03 m, never
touch the ground; their medium/low LOD meshes are landing legs 5.7 m in front
of the pivot). vpsemi's TWHL0/1 `.mtx` files are hand-made boxes (radius
0.5 m) 3.9 m behind the hitch, and do carry load. TWHL4/TWHL5 (vpcentury's
second trailer axle) are not simulated: MM2 stores only their offset from
TWHL2/3 (vehCarSim TrailerBackBackLeft/RightWheelPosDiff in mm2hook's
layout) to draw them, so they are visual copies of the back wheels.

**Placement.** `Trailer::reset` (mmTrailer::Reset) places the trailer so its
hitch point coincides with the tractor's (MM1's trailer model shares the
tractor's origin; MM2's has its own) and refreshes the joint position (MM1
leaves it stale until the next update). `Trailer::init` keeps the tractor
where it was after `InitJoint3Dof`'s `SetPosition` (which moves the tractor so
its hitch lands on its CG; MM1 relies on a later reset).

**Results** (`mm2tool simcar`, flat asphalt, 1/60 s; before = the previous
ball joint with approximate torques):

| run | before | after |
|---|---|---|
| vpsemi, full throttle 60 s | distance 783 m at 45 s, 822 m at 50 s, 776 m at 55 s (not monotonic), stuck in 2nd, top 70 mph | 0–60 mph 11.95 s, ¼ mile 18.85 s, top 80.5 mph, distance grows every second; max hitch gap 0.1 mm, hitch angle 0.3°, trailer up.y ≥ 0.999 |
| vpcentury, full throttle 60 s | 58 m in 60 s at 8000 rpm (wheelspin), top 32.5 mph | 0–60 mph 20.48 s, ¼ mile 21.43 s, top 71.1 mph; max gap 0.2 mm, hitch angle 0.3° |
| steady circle, throttle 0.5, steer 0.3, 30 s | vpcentury top 13.8 mph, 21.6 m from the start after 25 s; vpsemi slowing from 15.7 to 2.6 mph | vpcentury 25.4 mph on a 47 m radius, hitch angle steady at 3.3°; vpsemi (tiller) 21.4 mph on an 80 m radius, ≤ 2.3° |
| full brake at 12 s | vpsemi 48.7 → 7.4 mph in 1 s, then its speed reads 40 mph at 15.5 s while it stays within 1 m (the bodies thrash); vpcentury was doing 3.8 mph | vpsemi 60.1 → 1.0 mph and vpcentury 47.5 → 6.5 mph in 1 s; hitch angle ≤ 0.3° (≤ 3.7° with steer 0.15), upright |
| `--step 1/35`, steer 0.15 | — | max gap < 1 mm |

Reversing with a trailer is unstable, as in reality: holding the brake (auto
reverse) at 20 mph jackknifes vpcentury past 90°, and the trailer then passes
through the cab because the linked bodies do not collide.
`tests/phys/test_trailer.cpp` covers rest (60 s), a 60 s straight run, coasting
energy, the steady circle, hard braking and resets.

## Collision (OpenMM2)

Bodies collide as oriented boxes (cars: the box of `bound/<car>_bound.bnd`)
against static convex polygons (one-sided, XZ grid broad phase, continuous
check for fast bodies) and against each other (SAT). Sequential impulses with
restitution = elasticity products (none below 1 m/s) and Coulomb friction act
on momentum immediately; penetration is removed with `applyPush` + `moveICS`
as in the original. Wheels only probe the ground (segments). This is not a
port: MM1's mmBoundTemplate / asBound::Impact (full-CMatrix impulse) and MM2's
phBound family remain to be done.

## simcar results

`mm2tool simcars <game> 60`: full throttle from rest on flat asphalt
(`_default` material), automatic gearbox, fixed 1/60 s step, retail geometry
(wheel pivots from `.mtx`, body box from the bound). The `.info` Top Speed is
the menu's statistic, not a measurement. vpsemi and vpcentury tow their
trailers.

| car | drive | mass | hp | High (mph) | 0-60 (s) | 1/4 mile (s) | top (mph) | .info Top Speed |
|---|---|---|---|---|---|---|---|---|
| vp4x4 | 4WD | 2500 | 550 | 85 | 5.43 | 14.52 | 85.2 | 57 |
| vpauditt | RWD | 1300 | 551 | 120 | 3.73 | 11.73 | 120.3 | 182 |
| vpbug | FWD | 1000 | 260 | 90 | 6.83 | 15.15 | 90.1 | 91 |
| vpbullet | RWD | 1300 | 550 | 110 | 4.83 | 13.12 | 110.2 | 137 |
| vpbus | RWD | 5000 | 450 | 83 | n/a | 22.83 | 53.9 | 60 |
| vpcab | RWD | 1000 | 300 | 95 | 5.03 | 13.53 | 95.2 | 103 |
| vpcaddie | RWD | 1300 | 550 | 110 | 4.42 | 12.80 | 110.3 | 136 |
| vpcentury | RWD | 3500 | 750 | 75 | 20.48 | 21.43 | 71.1 | 91 |
| vpcoop | FWD | 800 | 250 | 80 | 8.35 | 16.63 | 80.2 | 60 |
| vpcoop2k | FWD | 800 | 300 | 108 | 6.27 | 14.92 | 107.5 | 115 |
| vpcop | RWD | 1300 | 750 | 140 | 3.15 | 10.47 | 140.5 | 160 |
| vpdb7 | FWD | 1573 | 550 | 150 | 5.07 | 13.52 | 150.2 | 206 |
| vpddbus | RWD | 4915 | 456 | 65 | 19.98 | 22.00 | 62.0 | 25 |
| vpdune | FWD | 1000 | 400 | 106 | 5.23 | 13.58 | 106.1 | 170 |
| vpford | RWD | 2500 | 550 | 85 | 6.22 | 15.22 | 85.2 | 58 |
| vpmustang99 | RWD | 1300 | 500 | 115 | 4.97 | 13.15 | 115.3 | 160 |
| vppanoz | FWD | 1300 | 650 | 151 | 3.80 | 11.72 | 151.1 | 216 |
| vppanozgt | RWD | 1200 | 902 | 180 | 3.13 | 10.40 | 176.1 | 240 |
| vpsemi | RWD | 3500 | 896 | 85 | 11.95 | 18.85 | 80.5 | 69 |
| vpvwcup | FWD | 1000 | 550 | 122 | 3.95 | 11.85 | 122.1 | 194 |

Almost every car tops out at its High gear speed (MaxRPM in top gear), which
supports the gear-speed interpretation. The bus (drag) does not, and the two
trucks reach 71–81 mph with their trailers (High 75 and 85).

## Known gaps

- Collision response and bounds are not ported (see above); car bodies are
  single boxes.
- Trailers: tractor/trailer collision is not implemented (the two-body
  `Joint3Dof::GetCMatrix` it needs is ported); the trailer CG, ForceLimit units, Free* fields and
  the roll limits are inferred (see Trailers). The tractors' WHL4/WHL5
  (vpsemi, vpcentury) are not simulated; like the trailers' TWHL4/5, MM2
  keeps only their offset from WHL2/3 (vehCarSim BackBackLeft/RightWheelPosDiff
  in mm2hook's layout), which suggests they are drawn, not simulated.
- Three opponent tunes (vpcoop_opp, vpcoop2k_opp, vppanoz_opp) have front
  OptimumSlipPercent 0.01 and barely accelerate. MM2 may drive opponents with
  a simpler model (MM1 had mmCarSimCheap/mmWheelCheap).
- Gyro and axle coupling are approximations and disabled.
- MM1-specific mechanisms not carried over because MM2's vehCarSim has no
  fields for them: SpinState drift/spin friction multipliers, weight
  redistribution (RedistHeight/RedistLongRatio, unused by MM1's forces
  anyway), damage-scaled torque.

## Measurements that would most improve accuracy

Black-box recordings from the original game (speedometer/tachometer video or
memory-free observation) of:

1. Full-throttle runs from rest for a few cars: speed and gear against time
   (validates the torque curve, the taper decision, gear spacing/GearBias and
   shift points).
2. Coasting and braking from a known speed (engine braking, rolling drag,
   brake coefficients and the brake split).
3. Steady-state circles at fixed steering and speed, and the speed at which
   the car starts to slide (tyre stiffness/friction mapping, SSS).
4. Handbrake turns and J-turns (Spin180/Reverse180, handbrake coefficients).
5. Time to fall a known height and suspension bounce after a drop (gravity,
   suspension mapping).
6. Collision outcomes (speed after hitting a wall head-on, damage per hit).
