# Physics and vehicle simulation

`src/phys` (library `mm2_phys`, namespace `mm2::phys`) simulates rigid bodies
and Midtown Madness 2 vehicles. The vehicle model is a port of MM2's own
classes, verified against the code of `midtown2.exe` build 3393 (the
MM2Recomp reference, see CLAUDE.md): `phInertialCS`, `vehCarSim`, `vehWheel`,
`vehDrivetrain`, `vehEngine`, `vehTransmission`, `vehAero`, `vehAxle`,
`vehGyro`, `vehStuck`, `vehTrailer`, `dgTrailerJoint`, `phJoint` and parts of
`vehCar`, `vehCarDamage`, `phColliderJointed` and `dgPhysManager`. The code
follows the original's operation order and 32-bit float arithmetic (the game
runs the x87 in single precision); `mm2_phys` is built with
`-ffp-contract=off`.

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
   `vehCarDamage`. A trailer is the next mover: its `vehTrailer::Update`
   runs the same three steps and ends with the hitch joint (see Trailers).
4. Collisions (OpenMM2), impact reports.

MM2 oversamples each frame: n = min(ceil((frame - 0.001) / SampleStep),
MaxSamples) samples of frame / n. `mmGame::Init` sets SampleStep 1/35 s and
MaxSamples 3 (overriding `dgPhysManager`'s own 1/60 s and 6), so a frame is
split only below 35 fps; at 60 fps it is one 1/60 s sample.
`World::advanceFixed` (default 1/60 s) therefore behaves exactly as the
original did at 60 fps, whatever the display rate, and is deterministic for replays and network play;
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
* `GetInvMassMatrix` (`calcCMatrix`): the 3x3 inverse mass matrix at a point;
  `GetLocalAcceleration`, `GetForce`/`GetTorque` (the accumulated force plus
  the accumulated impulse over the sample; the `ApplyContactForce` part is
  not included) serve the joints.
* Kept from MM1's asInertialCS: the sleep test (MM2 moved sleeping to
  `phSleep`, not ported) and constraints. OpenMM2 re-orthonormalises the
  matrix when it drifts.

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

Surface: material friction × WeatherFriction (0.8 in rain, 0.75 in rain at
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

## Trailers (vehTrailer and dgTrailerJoint) — MM2

vpsemi (a tiller ladder truck) and vpcentury (a semi with a flatbed) tow a
trailer. `phys/vehicle/Trailer` ports `vehTrailer` (`Init`, `Reset`,
`Update`, `BottomedOut`, `SetCarHitchOffset`, `SetTrailerHitchOffset`,
`FileIO`) and the trailer setup of `vehCar::Init`; `phys/TrailerJoint` ports
`dgTrailerJoint` (`Init`, `Reset`, `SetPosition`, `SetCosFreeLean`,
`SetRotate1/2`, `SetFrictionLean/Roll`, `SetLeanLimit`, both `SetRollLimit`,
`SetRestOrientation`, both `SetRestOrientMat`, `SetForceLimit`,
`SetJointForceFlag`, `Update`, `MoveICS`, `Break/UnbreakJoint`, `IsBroken`,
`DoJointTorque`, `DoJointLimits`, both `ComputeInvMassMatrix`, `FileIO`) and
`phys/Joint` its base `phJoint` (`Init`, `Reset`, `Update`,
`ComputeInvMassMatrix`, `ComputeJointForce`, `ComputeJointPush`,
`GetInvMassMatrix`, `IsBroken`). `Body::joint` and `Body::invMassMatrix` are
`phColliderJointed`'s `Attach` and `GetInvMassMatrix`. MM1's `Joint3Dof`,
which OpenMM2 used before, is gone; `dgTrailerJoint` descends from it.

**Integration.** MM2's joint does not integrate anything. The trailer is its
own mover: `mmGame::Update` declares the player's car and then its trailer
to `dgPhysManager` every frame, so each sample runs the tractor's
`vehCar::Update`, then `vehTrailer::Update`: the back wheels' inputs (only
while hitched), gravity, `phInertialCS::Update` (implicit with the wheel
contacts, like a car), the four drivetrains and their wheels, and last
`dgTrailerJoint::Update`, which adds forces and torques to both bodies for
the next sample. OpenMM2 runs the joint at the end of `Trailer::afterIntegrate`
(the trailer body is added to the World after the tractor's). Bodies sharing
an unbroken joint do not collide (`dgPhysManager::Update`).

**The joint** (`dgTrailerJoint::Update`, per sample, unless broken):

1. K = (C1 + C2)⁻¹ at the joint point (C: `phInertialCS::GetInvMassMatrix`),
   the world inertia tensors and their inverses.
2. With the force flag (JointStatus bit 2): `DoJointTorque`. MM2 leaves both
   rest orientations at identity (nothing calls `SetRestOrientMat`), so the
   joint axis is each body's Z axis and **lean** = the angle between the
   tractor's and the trailer's length: the hitch angle and the relative
   pitch together. Only while |cos lean| < cos FreeLean, i.e. lean beyond
   FreeLean: torque = RestoreForceLean·lean·kS about the axis normal to both
   Z axes, minus DampConstLean·kC along the unit relative turn rate (less
   its part about the trailer's Z axis), with kS = m_eff·40/π, kC =
   10·m_eff, m_eff = |o1||o2| / (|o2|/m1 + |o1|/m2). The torque jumps in at
   FreeLean (it is not reduced by it). MM2 also computes the DampLinearLean
   term (−DampLinearLean·2√(40/π)·m_eff·rate) but never adds it. The roll
   torque (relative roll about the trailer's Z axis) runs only when the
   roll passed in exceeds FreeRoll; `Update` always passes 0, so relative
   roll is free in MM2 and the roll limits never engage.
3. The joint force F = K·(free relative acceleration of the two joint
   points from `GetForce`/`GetTorque`, with the ω×(ω×r) and gyroscopic
   terms, + ⅓·relative velocity/dt). Gravity is added at the start of the
   next sample and accelerates both points alike.
4. Lean limit (`DoJointLimits`): once lean ≥ LeanLimit and still closing, an
   angular impulse (LimitElasticityLean + 1)·(error/axis·M·axis) and the
   matching change of F.
5. Breaks when |F| > ForceLimit·10000 N (vpsemi's 40: 400 kN; 0: never).
6. Torques (p − x1)×F and (x2 − p)×F; F turned by half the sample's mean
   rotation θ = (ω1 + ω2)·dt/2 when |θ|² > 10⁻⁵ (scaled by 1 − θ²/24, or
   2 sin(θ/2)/θ), then +F on the tractor and −F on the trailer.
7. The joint point moves to the middle of the two hitch points; if they are
   more than FreeRange apart, each body's position moves half the excess
   towards the other (no velocity change, no push).

**MM2's force rotation.** Step 6 calls `Matrix34::RotateUnitAxis` (this = this
· R) on a matrix that still holds C2, the trailer's inverse mass matrix from
step 1 (MM1 built a fresh rotation with `RotateAbs`). The part of F across the
turning axis is therefore multiplied by C2 (≈ 10⁻³/kg): while the pair turns
faster than about 0.2 rad/s the joint transmits only the force along the
turning axis, and the FreeRange correction holds the hitch. OpenMM2 does the
same (`TrailerJoint::mm2ForceRotation`, on by default); `simcar
--plain-hitch-rotation` applies the plain rotation for comparison. In a
steady circle vpsemi's trailer then lags (its body velocity reads 10 m/s at
14 m/s) and its hitch opens up to 0.21 m before each correction, against
0.14 m with the plain rotation.

**vehTrailer::Init.**

* The hitches: `vehCar::Init` builds a trailer only if the car model has a
  `trailer_hitch` pivot (`vehCarModel::GetTrailerHitch`) and passes it as
  CarHitchOffset; TrailerHitchOffset is the trailer model's own
  `trailer_hitch` pivot (`vehTrailerInstance::GetTrailerHitch`). The
  `.vehTrailer` fields replace them when present (vpsemi). The joint takes
  CarHitchOffset in the tractor's **InertialCS** frame, i.e. relative to its
  centre of mass, so the hitch sits at model + (CarHitchOffset −
  CenterOfGravity): 0.2 m above and 0.5 m ahead of vpcentury's
  `trailer_hitch` pivot. MM2 draws it that way too.
* The trailer's rigid body is its model: centre of mass at the model origin
  (vehTrailer has no CG field), `InitBoxMass(Mass, InertiaBox)`, the
  phInertialCS default angular velocity limit (5 rad/s per axis), gravity
  19.6. `Reset` puts it at the tractor's InertialCS position + R·(CarHitch −
  TrailerHitch), in line, hitched; `Init` uses the tractor's model matrix
  instead (OpenMM2's `Trailer::init` ends with a reset).
* Wheels TWHL0–3 without a vehCarSim (body frame; static load Mass·19.6/4),
  TWHL1/TWHL3 copying TWHL0/TWHL2's tune; four free drivetrains with the
  `Drivetrain` block (constructor defaults otherwise), initialised with the
  tractor's vehCarSim (its Mass sets the wheel inertia).
* `vehTrailer::Update` gives only the back wheels the tractor's inputs
  (steering opposite, with its speed-sensitive factor; brakes; the handbrake
  eased on the inside wheel), so vpsemi's tiller axle and vpcentury's trailer
  axle steer against the tractor.
* **Static loads.** When TWHL0 and TWHL2 lie on the same side of the origin
  (vpcentury; vpsemi's tiller axle is in front of it), with zm their mean z,
  hz the trailer hitch's z and L = |hz − zm|, MM2 sets each trailer wheel to
  W|zm|/(4L) and adds W|hz|/L to the tractor's wheels
  (`vehWheel::AddNormalLoad`), split between its axles by the hitch position.
  With the centre of mass at the origin these are swapped: the hitch carries
  W|zm|/L and the axle W|hz|/L. vpcentury's trailer wheels get 3511 N each
  while its two real wheels (TWHL0/1 are 3 cm pivots that never reach the
  ground) carry 12.6 kN each, so they ride on their bump stops; there the
  bottoming push hides the sinking from the wheel's filtered velocity, the
  trailer's velocity runs away, and the push and the joint's FreeRange
  correction lift the whole rig (at rest the tractor's front wheels leave the
  ground; at full throttle 0–60 mph takes 44 s). **OpenMM2 corrects this by
  default** (`TrailerOptions::mm2StaticLoads`, `simcar --mm2-trailer-loads`
  for MM2's values): the tractor gets W|zm|/L and the axle's W|hz|/L is
  shared by the wheels that reach the ground with the lowest one (within
  their SuspensionExtent). Whether MM2 itself showed the runaway is not
  known.

**Tune fields.**

| Field | Use | Evidence |
|---|---|---|
| ForceLimit | breaks above ForceLimit·10000 N; 0 never (vpsemi 40 = 400 kN; vpcentury 0) | MM2 |
| JointStatus | replaces the joint's flags after Init: bit 1 broken, bit 2 torques and limits on (both files: 2). Not recomputed after loading; `Reset` unbreaks | MM2 |
| RestoreForceLean, DampConstLean | lean restoring torque and constant damping (above) | MM2 |
| DampLinearLean | read, no effect | MM2 |
| RestoreForceRoll, DampConstRoll, DampLinearRoll | the roll torque, which never runs | MM2 |
| LeanLimit, LimitElasticityLean | lean limit (default π) and its elasticity (default 1) | MM2 |
| LimitElasticityRoll | roll limit elasticity; the limits (−0.3/+0.3 from Init) are not loadable and never engage | MM2 |
| FreeRange | hitch gap allowed before the bodies are moved (default 0.15 m; vpsemi 0.14) | MM2 |
| FreeLean | lean below which no lean torque acts (default 0.1 rad, cos kept as CosFreeLean) | MM2 |
| FreeRoll | the roll torque's threshold (default 0.1) | MM2 |
| Offset0, Offset1 (vpcentury) | not read by MM2 (an older layout) | MM2 |
| vehTrailer Mass, InertiaBox | `InitBoxMass` (defaults 3000, 3 4 9) | MM2 |
| CarHitchOffset, TrailerHitchOffset | replace the models' hitch pivots (above) | MM2 |
| WheelFront, WheelBack, Drivetrain | TWHL0 (TWHL1 copies), TWHL2 (TWHL3 copies), the first drivetrain (the others copy) | MM2 |
| (impact elasticity/friction) | vehTrailer sets none, so MM2 uses its bound's material (`default`: 0.1/0.5); OpenMM2's collision gives the trailer body the tractor's BoundElasticity/BoundFriction | inferred |

`Body::invMassMatrix` gives the inverse mass matrix through the joint as
`phColliderJointed::GetInvMassMatrix` does; OpenMM2's contact solver, which
applies impulses to one body at a time, still uses each body's own mass. The
Ctrl+B debug key that breaks the joint (`dgTrailerJoint::Update`) is not
ported. TWHL4/TWHL5 (vpcentury's second trailer axle) are not simulated: MM2
keeps only their offset from TWHL2/3 (vehCarSim
TrailerBackBackLeft/RightWheelPosDiff in mm2hook's layout) to draw them.

**Results** (`mm2tool simcar`, flat asphalt, 1/60 s, defaults; the gap is
measured before the FreeRange correction):

| run | vpcentury | vpsemi |
|---|---|---|
| full throttle 60 s | 0–60 mph 10.98 s, ¼ mile 18.20 s, top 112.4 mph; gap ≤ 0.02 m, hitch ≤ 0.1° | 0–60 mph 11.60 s, ¼ mile 18.68 s, top 106.8 mph; gap ≤ 0.011 m, hitch ≤ 1.5° |
| steady circle, throttle 0.5, steer 0.3 | 38 mph, hitch 3–4°, gap ≤ 0.56 m, lean ≤ 9.8° (plain rotation: gap ≤ 0.14 m) | 32 mph, hitch 5.5° steady, gap ≤ 0.21 m (plain rotation: 12.3°, gap ≤ 0.14 m) |
| full brake at 12 s | 63.6 → 2.7 mph in 2 s, hitch ≤ 0.2°; with steer 0.15: hitch ≤ 3°, gap ≤ 0.40 m | 61.1 → 10.5 mph in 3 s; with steer 0.15: hitch ≤ 5.8° |
| at rest | settles with all wheels loaded, gap 0.011 m (MM2's loads: see above) | gap 0.002 m |

Reversing with a trailer is unstable, as in reality: holding the brake (auto
reverse) jackknifes vpcentury past 110° at 30 mph, and the trailer then
passes through the cab because the linked bodies do not collide; vpsemi's
counter-steering tiller axle holds 5.5°. `tests/phys/test_trailer.cpp`
covers the joint's mechanics on synthetic bodies and, with the retail data,
both trucks' setup, rest, straight run, coasting energy, steady circle, hard
braking, resets and MM2's static loads.

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
| vpcentury | RWD | 3500 | 750 | 75 | 10.98 | 18.20 | 112.4 | 91 |
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
| vpsemi | RWD | 3500 | 896 | 85 | 11.60 | 18.68 | 106.8 | 69 |
| vpvwcup | FWD | 1000 | 550 | 122 | 3.78 | 11.97 | 155.7 | 194 |

Top speeds are set by power against drag, or by MaxRPM in top gear: High is
the top gear's speed at OptRPM. (Before the MM2 port, OpenMM2 took Low/High
as speeds at MaxRPM and capped every car at High.)

## Known gaps

- Collision response and bounds are OpenMM2's own (see above); car bodies are
  single boxes.
- Trailers: OpenMM2 corrects vehTrailer::Init's static loads by default
  (MM2's values make vpcentury's trailer ride on its bump stops, see
  "Trailers"); contacts of jointed bodies use each body's own mass; the
  trailer's impact parameters are inferred.
- `phSleep`, the per-axis angular velocity limits of non-car bodies other
  than trailers and `vehSuspension` (the visual shocks) are not ported.
- Damage: MM2's impact list (relax times, texel damage positions) is not
  ported; the impact value mapping is inferred.
- The engine pivot (`<car>_engine.mtx`) and axle pivots are not loaded yet;
  the original's fallbacks apply.

