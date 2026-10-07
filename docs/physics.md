# Physics and vehicle simulation

`src/phys` (library `mm2_phys`, namespace `mm2::phys`) simulates rigid bodies
and Midtown Madness 2 vehicles. The vehicle model is a port of MM2's own
classes, verified against the code of `midtown2.exe` build 3393 (the
MM2Recomp reference, see CLAUDE.md): `phInertialCS`, `vehCarSim`, `vehWheel`,
`vehDrivetrain`, `vehEngine`, `vehTransmission`, `vehAero`, `vehAxle`,
`vehGyro`, `vehStuck` and parts of `vehCar`, `vehCarDamage`, `vehTrailer` and
`dgPhysManager`. The code follows the original's operation order and 32-bit
float arithmetic (the game runs the x87 in single precision); `mm2_phys` is
built with `-ffp-contract=off`.

## Evidence levels

| Level | Meaning |
|---|---|
| **MM2** | Ported from the build 3393 code (function named in the code comments). |
| **Ported (MM1)** | Translated from MM1 (Open1560 `game.asm`), not yet replaced by MM2's version. |
| **OpenMM2** | Our own code where the original is not ported (collision). |
| **inferred** | A reasoned mapping that the code does not settle. |

## How a sample runs

`World::step(dt)` follows MM2's per-sample order for a car (`dgPhysManager::Update`
calls `vehCar::Update` for each mover, then collides them):

1. `CarSim::beforeIntegrate`: `vehCarSim::Update` up to the integration: the
   forward speed |v . Z|, speed-sensitive steering, and the wheel inputs.
2. `InertialCS::update`: gravity (`dgPhysEntity::Update`: mass * -19.6), then
   `phInertialCS::Update`, integrating last sample's forces.
3. `CarSim::afterIntegrate`: the world matrix, then `vehCarSim`'s children in
   order — `vehEngine`, `vehTransmission`, `vehAero`, the two free drivetrains
   and the engine-driven one (each probes its wheels, solves its speed and
   updates the wheels, which apply their forces for the next sample),
   `vehAxle` — followed by `vehCar::Update`'s `vehGyro`, `vehStuck` and
   `vehCarDamage`.
4. Collisions (OpenMM2), impact reports.

MM2 oversamples each frame: n = min(ceil((frame - 0.001) / (1/60)),
6) samples of frame / n (`dgPhysManager` SampleStep 1/60 s, MaxSamples 6).
At 60 fps that is one 1/60 s sample per frame. `World::advanceFixed` (default
1/60 s) therefore behaves exactly as the original did at 60 fps, whatever the
display rate, and is deterministic for replays and network play;
`World::advanceOversampled` reproduces the original's frame-rate dependent
scheme. (Several terms below scale with the sample length, e.g. the
drivetrain's AngInertia * dt, so the original drove slightly differently at
other frame rates.)

## Rigid body (phInertialCS) — MM2

* Momentum p += impulse + dt * F; v = p / m. Angular momentum likewise; the
  angular velocity is found about each body axis and, with
  `limitAngVelocity`, each component is limited on its own (cars: 4 pi rad/s,
  `vehCarSim::Init`), the momentum then recomputed from the limited velocity.
* Speed limit 500 m/s. Position += push + dt * v; the accumulated turn
  (rotational push + dt * w) rotates the matrix about its axis.
* **Implicit contacts.** `applyContactForce(F, point, K)` adds a force and its
  stiffness d(force)/d(velocity) (K = c n n^T for a wheel, c = damping +
  dt * spring). A body with such contacts integrates linearly implicitly: the
  linear step through M = (I + dt/m sum K)^-1 and the angular one by solving
  B dw = rhs with B = world inertia + dt sum (X K X^T) - dt^2/m (X K) M
  (X K)^T (`Matrix34::SolveSVD`; OpenMM2 uses Gaussian elimination, which
  gives the same result for the non-singular matrices that occur).
* `filteredVelocity` (GetLocalFilteredVelocity2): a point's velocity with the
  part along the last sample's push removed, used by the wheels.
* Pushes (`applyPush`, CalcNetPush) and turns (`applyTurn`) only add the part
  of a new correction not already covered by the pending one.
* Kept from MM1's asInertialCS: the sleep test (MM2 moved sleeping to
  `phSleep`, not ported), constraints, and the CMatrix helpers the trailer
  joint uses. OpenMM2 re-orthonormalises the matrix when it drifts.

## Car (vehCarSim) — MM2

| Field | Use |
|---|---|
| Mass, InertiaBox | box inertia (y^2+z^2, x^2+z^2, x^2+y^2) * m / 12 |
| CenterOfGravity | the model's origin is the body position + R * CenterOfGravity (`SetWorldMatrix`), i.e. the centre of mass sits at -CenterOfGravity in model space; also splits the static wheel loads (below) |
| BoundFriction, BoundElasticity | the body's impact friction and elasticity (`RestoreImpactParams`). `SetHackedImpactParams` (friction 2, no bounce) exists but nothing in MM2 calls it |
| DrivetrainType | 0 rear (free front wheels, one drivetrain each), 1 front, 2 all (`ConfigureDrivetrain`) |
| SSSValue, SSSThreshold | speed-sensitive steering: factor 1 at rest, linearly to SSSValue at SSSThreshold m/s, SSSValue above; off with threshold 0 (`GetSSSFactor`) |
| CarFrictionHandling | surface friction f < 1 becomes f / CFH (CFH >= 1) or f + (1 - CFH)(1 - f) |

Inputs (`vehCarSim::Update`): the front wheels steer with the speed-sensitive
steering; the back wheels steer opposite (their SteeringLimit is usually 0).
The handbrake acts on the back wheels only, eased off the wheel on the inside
of the turn ((1 - steering) on the left one when steering right, and the
mirror). Brake and throttle both above 0.95 below 1 m/s release the back
brakes (a burnout). Displayed speed = |velocity . car Z| * MetricFactor
2.2360249 (mph; MM2 never switches units).

Defaults for fields a file lacks are the constructors' values (Mass 2000,
InertiaBox 2 1 3, BoundFriction 0.3, BoundElasticity 0.2, ...;
`TuneParams.h`).

## Wheel (vehWheel) — MM2

Geometry from the wheel's pivot `.mtx` (`vehWheel::Init`): centre = the
pivot in model space, radius = half the box height, width = the box width.
The steered wheel turns about its inner edge.

Constants (`ComputeConstants`, `SetNormalLoad`), with static load
L = mass * 19.6 / 4 * |z - cg.z| / |z| (z the wheel's model z, cg the
CenterOfGravity field; without a car, mass * 19.6 / 4):

| Constant | Formula |
|---|---|
| spring | (SuspensionFactor * Extent + Limit) / ((Limit + Extent) * Extent) * L; SuspensionFactor >= 0.75 |
| progression | (SuspensionFactor - 1) / ((Limit + Extent) * Extent) * L / spring (force * (1 + progression * travel)) |
| damping | 2 sqrt(spring * L) * SuspensionDampCoef |
| tyre stiffness | 2 L / TireDispLimitLong (Lat) |
| tyre damping | 2 sqrt(stiffness * L / 19.6) * TireDampCoefLong (Lat) |
| brake torque | StaticFric * Radius * L * BrakeCoef (handbrake: HandbrakeCoef) |

Suspension (`ComputeDwtdw`, `CalcSuspensionForce`): a probe from
SuspensionLimit + 0.3 above the centre to SuspensionExtent + Radius below it.
The travel (compression positive) is limited to -Extent; the force is
(rate * damping + travel * spring) * (1 + progression * travel) + L, rate
limited to ±10 m/s; a lifting wheel relaxes without pulling. The contact
hands its stiffness damping * progression + dt * spring / cos(slope) to the
body (implicit, above). Past SuspensionLimit the wheel bottoms out: an
impulse a quarter of the one stopping the closing velocity, per sample, and a
push out of the overlap.

Surface: material friction × WeatherFriction (0.8 in snow, 0.75 in snow at
night, `mmGame::InitWeather`; applied everywhere, tunnels included) then
CarFrictionHandling; walls (|n.y| < 0.001) have no friction. Materials with a
`height` make bumps: a sine of wavelength `width` along the distance
travelled (randomised with the game's `frand`), scaled down below 1 m/s.
While skidding a wheel sinks into the material's `depth` at |w| R * 0.1 per
second, and the sinking adds to the drag. OpenMM2: deep water (`depth` >= 1)
carries no wheel (inferred).

Tyre (`Update`): per direction a contact-patch displacement moves with the
slip velocity (longitudinal: w R + forward velocity), limited to the
displacement where the force reaches friction(slip) * load; past it the
displacement relaxes towards the limit by |w| R dt * 0.1 per sample and
contributes no damping. friction(s) = StaticFric (2 s/s0 - s^2/s0^2) up to
the optimum slip s0 (OptimumSlipPercent), then the parabola until it falls
to SlidingFric (`ComputeFriction`). The direction slipping more sets the
friction of both. Force = -stiffness * disp - damping * rate, clipped to the
friction circle mu * load (sliding: against the slip velocity). Drag:
-TireDragCoef * load * material drag * |v| v per direction (quadratic; zero
on surfaces without drag), the longitudinal part × (1 + sinking).

Visual: the wheel drops by its travel less the tyre squash
(Radius * 0.05 * force / L, at most Radius * 0.3) and spins; with
CamberLimit > 0 it cambers with its travel, otherwise it rolls with its axle.

## Drivetrain (vehDrivetrain) — MM2

| Term | Formula |
|---|---|
| brake | 50 + sum(wheel brake torque) * (BrakeStaticCoef if the speed is 0, else BrakeDynamicCoef) |
| net (resists the rotation) | (ratio * w + w_engine) * Engine.AngInertia / dt * ratio + ratio * engine torque - sum(tyre torque), the tyre torques being last sample's |
| with the brake | at rest: max(net - brake, 0) or min(net + brake, 0); turning: net + brake * sign(w), and a wheel the brake can hold does not change direction |
| step | w' = w - dt * net / (dt * AngInertia + I), I = ratio^2 * Engine.AngInertia + 0.02 attached, Mass * 0.005 free |
| limit | the wheels cannot outrun the engine at MaxRPM in gear; the engine speed follows: -ratio * w, never negative |

AngInertia is a damping term: it is multiplied by the sample length. On a
free drivetrain it makes the wheels lag while the car accelerates (vpbug's
free rear wheels pull about 1 kN back at 3 m/s^2), and less so at higher
frame rates in the original.

The wheels report a breakpoint (the speed where the slip reaches the
optimum) for a piecewise solve. MM2 searches the largest breakpoint starting
from 1e9 and the smallest from -1e10, so only the 1e10/1e11 sentinels of
wheels in the air or beyond the optimum qualify, and those are never crossed:
in practice the coupling is explicit, as in MM1. Ported as is.

Limited-slip differential: paired wheels spin at w * r and w / r, r moving
each sample a tenth of the way to the ratio balancing their tyre torques,
limited to 1.25 at rest falling to 1.03 at 50 rad/s (`diffRatioMax`,
`diffRatioMaxHighSpeed`, `diffRatioHighSpeedLevel`); 1 when nearly stopped.

## Engine (vehEngine) — MM2

* Full throttle, w <= OptRPM: T = (phi w_opt - w)(w_opt/phi + w) * k,
  phi = 1.618034, k = MaxHorsePower * 746 / w_opt^3 (T(w_opt) = Pmax / w_opt).
  Between OptRPM and MaxRPM it is multiplied by (w_max - w)(w + w_max -
  2 w_opt) / (w_max - w_opt)^2, zero above MaxRPM.
* Closed throttle: T0 = (w_idle - w) * Pmax/w_opt * 0.75 / (w_opt - w_idle);
  the throttle blends T0 and T.
* Clutch: the engine-driven drivetrain detaches in neutral or below IdleRPM
  and attaches above twice IdleRPM; detached, the engine revs against its
  AngInertia.
* GCL: after a shift the torque is 0 for GCL seconds and the displayed RPM
  blends from the old value.
* Revving in neutral rocks the car: a reaction torque about the engine's axis
  (the `engine` pivot if the car has one; otherwise X for front-wheel drive,
  Z otherwise).

## Transmission (vehTransmission) — MM2

MM2 reads only ManualNumGears, AutoNumGears, Reverse, Low, High, GearBias,
UpshiftBias, DownshiftBiasMin, DownshiftBiasMax and GearChangeTime. The MM1
fields still in a few retail files (GearRatios, UpshiftRPM, ...) are ignored.

* Ratios: GearRatioFromMPH(v) = OptRPM / wheel RPM at v mph (1609.344 m per
  mile; the primary drivetrain's first wheel). **Low and High are the
  speeds at OptRPM**, so a car can rev on to MaxRPM in top gear. Gears in
  between: Low * q^(i + i (m - i) GearBias / m), q = (High/Low)^(1/m), m =
  forward gears - 1.
* Shift points: the RPM x where the next gear gives the same full-throttle
  power (bisection). Up above (1 + UpshiftBias) x (top gear: MaxRPM); down
  below throttle * (1 - DownshiftBiasMin) x f + (1 - throttle) *
  (1 - DownshiftBiasMax) x f in the next gear (f its ratio step), from third
  gear up.
* The automatic shifts only with a wheel on the ground, after GearChangeTime
  in gear and once the engine's gear-change lag is over.
* Manual: Upshift/Downshift step through the manual box; the automatic only
  goes reverse -> neutral -> drive with them.
* Auto reverse is game logic (`mmGame::UpdateSteeringBrakes`): brake > 0.8,
  throttle < 0.1, below 5 m/s swaps the pedals and selects reverse; in
  reverse, throttle (the brake pedal) < 0.8 returns to drive.

## Aero (vehAero) — MM2

Angular damping about each body axis: -(AngCDamp sign w + AngVelDamp w +
AngVel2Damp |w| w) * inertia, never more than stops the rotation in one
sample, faded out below 1 rad/s (the original tests the world-space component
here). Drag: -Drag * forward speed * v. Downforce: -Down * forward speed^2
along the car's up axis.

## Axle (vehAxle) — MM2

TorqueCoef and DampCoef make an anti-roll coupling: a torque about the body's
length of -(Δtravel * TorqueCoef * Izz + Δrate * 2 sqrt(TorqueCoef) Izz *
DampCoef) between the axle's wheels. The axle also rolls its wheels visually
(by half the travel difference over the left wheel's offset from the `axleN`
pivot; factor 1 without a pivot).

## Gyro (vehGyro) — MM2

With all four wheels down (`OnGround() / 4` in integer arithmetic): Drift
adds a yaw torque Iyy * Drift * |s| s * (-w) with s the speed-sensitive
steering and w the drivetrain speed; with the handbrake held, Spin180 (rolling
forward) or Reverse180 (rolling back) add Iyy * k * steering * (-w). With
the brake held and not all wheels down, Pitch and Roll turn the car level.

## Stuck (vehStuck) — MM2

After an impact the car is watched (TimeThresh 0.3 s, PosThresh 1.25 m
horizontally, MoveThresh 1.75 m to give up). In the air it is nudged
(Rotation > 0: an upward impulse and a roll while it leans past ~45°, throttle
off, brakes on) or set upright and lifted by Translation; on the ground with
the throttle pegged and the steering turned it yaws in place at
|steer| steer * Turn rad/s (backwards in reverse).

## Damage (vehCarDamage) — MM2 with an inferred mapping

CurrentDamage falls by RegenerateRate per second; impacts above
ImpactThreshold add their value while the car moves at 10 mph or more, or when
the other party is a vehicle; damage is the fraction between MedDamage and
MaxDamage and the car is wrecked at MaxDamage. MM2's impact value is the
impulse vector of its collision callback times the other body's share of the
two masses (`vehCarDamage::InsertImpact`); OpenMM2 feeds its own contact
impulse (inferred). A wrecked car stops responding with the brakes on
(`vehCar::PreUpdate`'s disabled state; which state MM2 uses for a wreck is
inferred).

## Opponents and police

MM2 builds AI cars with `vehCar::Init(<car>)` (`aiVehiclePhysics::Init`):
the same tune as the player's. The retail `*_opp.vehCarSim` and
`vpcop_cop.vehCarSim` files (MM1-era layouts) are never loaded by the game.

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

(These were measured before the [wheel spin fix](#wheel-spin-and-the-stiff-_opp-tyres).
Since then vpsemi does 0–60 mph in 9.83 s (¼ mile 17.33 s) and vpcentury in
17.87 s (19.73 s), same top speeds; max hitch gap ≤ 0.3 mm and hitch angle
≤ 0.9° at full throttle, 1.5° / 2.7° in the steady circle, and a full brake
at 12 s takes vpsemi from 64.1 to 2.4 mph and vpcentury from 52.7 to 3.5 mph
in 1 s.)

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
port: MM2's phBound family, phContact and phImpact remain to be done.

## simcar results

`mm2tool simcars <game> 60`: full throttle from rest on flat asphalt
(`_default` material), automatic gearbox, fixed 1/60 s step, retail geometry
(wheel pivots from `.mtx`, body box from the bound). The `.info` Top Speed is
the menu's statistic, not a measurement. vpsemi and vpcentury tow their
trailers.

| car | drive | mass | hp | High (mph) | 0-60 (s) | 1/4 mile (s) | top (mph) | .info Top Speed |
|---|---|---|---|---|---|---|---|---|
| vp4x4 | 4WD | 2500 | 550 | 85 | 4.00 | 12.48 | 104.5 | 57 |
| vpauditt | RWD | 1300 | 551 | 120 | 4.17 | 12.28 | 146.5 | 182 |
| vpbug | FWD | 1000 | 260 | 90 | 7.10 | 15.55 | 115.9 | 91 |
| vpbullet | RWD | 1300 | 550 | 110 | 4.23 | 12.40 | 133.1 | 137 |
| vpbus | RWD | 5000 | 450 | 83 | 13.48 | 19.52 | 103.5 | 60 |
| vpcab | RWD | 1000 | 300 | 95 | 6.75 | 15.03 | 126.7 | 103 |
| vpcaddie | RWD | 1300 | 550 | 110 | 4.40 | 12.62 | 133.0 | 136 |
| vpcentury | RWD | 3500 | 750 | 75 | 14.83 | 18.97 | 84.0 | 91 |
| vpcoop | FWD | 800 | 250 | 80 | 9.28 | 17.27 | 104.3 | 60 |
| vpcoop2k | FWD | 800 | 300 | 108 | 7.93 | 16.05 | 131.9 | 115 |
| vpcop | RWD | 1300 | 750 | 140 | 2.93 | 10.68 | 164.9 | 160 |
| vpdb7 | FWD | 1573 | 550 | 150 | 4.60 | 12.70 | 177.3 | 206 |
| vpddbus | RWD | 4915 | 456 | 65 | 9.87 | 17.38 | 99.4 | 25 |
| vpdune | FWD | 1000 | 400 | 106 | 4.45 | 12.73 | 136.7 | 170 |
| vpford | RWD | 2500 | 550 | 85 | 6.42 | 14.88 | 104.1 | 58 |
| vpmustang99 | RWD | 1300 | 500 | 115 | 5.45 | 13.62 | 135.3 | 160 |
| vppanoz | FWD | 1300 | 650 | 151 | 4.37 | 12.40 | 177.2 | 216 |
| vppanozgt | RWD | 1200 | 902 | 180 | 2.67 | 10.28 | 275.0 | 240 |
| vpsemi | RWD | 3500 | 896 | 85 | 11.73 | 18.68 | 106.4 | 69 |
| vpvwcup | FWD | 1000 | 550 | 122 | 3.78 | 11.97 | 155.7 | 194 |

Top speeds are set by power against drag, or by MaxRPM in top gear: High is
the top gear's speed at OptRPM. (Before the MM2 port, OpenMM2 took Low/High
as speeds at MaxRPM and capped every car at High.)

## Known gaps

- Collision response and bounds are OpenMM2's own (see above); car bodies are
  single boxes.
- The trailer joint is still MM1's Joint3Dof (below), not MM2's
  `dgTrailerJoint`; bodies it links integrate explicitly.
- `phSleep`, the per-axis angular velocity limits of non-car bodies and
  `vehSuspension` (the visual shocks) are not ported.
- Damage: MM2's impact list (relax times, texel damage positions) is not
  ported; the impact value mapping is inferred.
- The engine pivot (`<car>_engine.mtx`) and axle pivots are not loaded yet;
  the original's fallbacks apply.

