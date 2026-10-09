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
| **OpenMM2** | Our own code where the original is not ported. |
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
4. Collisions (`dgPhysManager`: the city, the other movers, the objects
   around; see "Collision"), then each mover's pending push
   (`phColliderBase::UpdateMtx`).

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

* Every physics entity's update first adds its weight, Mass * -19.6 on y
  (`dgPhysEntity::Update`).
* Momentum p += impulse + dt * F; v = p / m. Angular momentum likewise; the
  angular velocity is found about each body axis and each component is
  limited on its own, the momentum then recomputed from the limited
  velocity. The limit is phInertialCS's constructor's 5 rad/s per axis for
  every body (props, traffic off its rail, trailers); `vehCarSim::Init`
  raises the car body's to 4 pi.
* Speed limit 500 m/s. Position += push + dt * v; the accumulated turn
  (rotational push + dt * w) rotates the matrix about its axis
  (`Matrix34::RotateUnitAxis`). Nothing re-orthonormalises the matrix.
* **Implicit contacts.** `applyContactForce(F, point, K)` adds a force and its
  stiffness d(force)/d(velocity) (K = c n n^T for a wheel, c = damping +
  dt * spring). A body with such contacts integrates linearly implicitly: the
  linear step through M = (I + dt/m sum K)^-1 and the angular one by solving
  B dw = rhs with B = world inertia + dt sum (X K X^T) - dt^2/m (X K) M
  (X K)^T and rhs = -(dt/m) P M (X K)^T + angular impulse + dt * torque
  (P = linear impulse + dt * F) with `Matrix34::SolveSVD`, a cofactor solve
  with rank-2 and rank-1 fallbacks. The linear velocity then changes by
  (dt dw (X K) + P) / m * M: the opposite sign, for the dw term, to the one
  the angular solve assumes. Both are as in MM2.
* The Vector3 / Matrix34 helpers sum their terms in the order the original's
  compiled code does (`phys/AgeMath`, `core/Math`), so a port of a call
  rounds as the original did.
* `filteredVelocity` (GetLocalFilteredVelocity2): a point's velocity with the
  part along the last sample's push removed, used by the wheels.
* Pushes (`applyPush`, CalcNetPush) and turns (`applyTurn`) only add the part
  of a new correction not already covered by the pending one.
* `GetInvMassMatrix` (`calcCMatrix`): the 3x3 inverse mass matrix at a point;
  `GetLocalAcceleration`, `GetForce`/`GetTorque` (the accumulated force plus
  the accumulated impulse over the sample; the `ApplyContactForce` part is
  not included) serve the joints.
* Sleeping is `phSleep`'s (`phys/Sleep`; MM1's asInertialCS sleep test and
  constraints are gone), which traffic cars off their rail and knocked-over
  props use: still for 15 updates (speed with the pushes and spin below
  their thresholds, or jittering) freezes the body and makes it inactive.
  An inactive body does not integrate, but a push still moves it
  (`MoveICS`) and its push bookkeeping runs on.

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
The steered wheel turns about its inner edge. The tune file's WheelFront and
WheelBack are the left wheels'; the right ones take them by
`vehWheel::CopyVars`, which copies every field except HandbrakeCoef and
WobbleLimit, so the right wheels keep HandbrakeCoef 1 whatever the file
says (a car with HandbrakeCoef 2 brakes its back-left wheel twice as hard as
its back-right one).

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
The probe is `dgPhysManager::Collide` with the wheels' instance mask 0x20,
never hitting the car's own instance (a trailer's wheels pass none), as
`World::wheelProbe`:

- the rooms of the segment's ends are looked up from the ones the wheel's
  segment info had;
- `lvlSDL::CollideProbe`: when neither end is in an instance room (flag
  0x80), the polygon the wheel's last probe ended on (a cached copy,
  `sdlPolyCached`) answers alone if the segment still crosses it. Otherwise
  `sdlPage16::CollideSegment` collects (`sdlPage16::Collect`, in batches of
  256, with the probed room marked so a SpecialBound room's road surfaces
  become triangles with raised sidewalks) the polygons of the start room,
  the end room, and the instance rooms across the start room's perimeter
  (at most 10), within a sphere about the segment's midpoint of 0.51 times
  its length, and keeps the nearest crossing either way
  (`phPolygon::TestSegmentUndirected`; at equal distance the later polygon).
  Each room with a hit caches its last kept polygon; each room without one
  makes the cache stale;
- `dgPhysManager::CollideProbe`: the instances with flag 0x20 in the start
  and end rooms (and in the room a warp room, flag 0x40, leads to: rooms
  411, 412, 423 and 625 lead to 102, 122, 96 and 1) whose sphere reaches the
  segment's are probed in their own frame (the bound's `TestProbe`), a
  nearer hit replacing the level's. The city's collidable instances carry
  0x20 except the terrain-bound ones whose record has flag 0x400
  (`lvlLevel::LoadInstances`); props and cars do not.

The material is the polygon's (the level's material byte, or the bound's),
looked up by name in the World's table. A hit on the cached polygon keeps
the material of the probe before (the wheel's intersection is not refreshed).
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
A damaged car's wheels wobble about their forward axis (`vehCarDamage::Update`,
see Damage).

The arithmetic follows the build 3393 assembly, including the grouping of
sums and products (Ghidra's printed expressions reorder commutative operands,
which changes the last bits with 32-bit floats).

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
  Z otherwise). With a pivot, the torque is given in the pivot's frame rocked
  by 0.05 × torque / peak torque × AngInertia about its own Z axis
  (`vehEngine::Update` keeps that rocked matrix, which would also place an
  engine model). No retail car has an `engine` pivot.

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
pivot along its X axis; factor 1 without a pivot). `vehAxle::Update` writes
that roll into the pivot matrix's m0.y and the mean travel (over the left
wheel's offset along the pivot's Z) into its m2.y, which tilts the axis the
wheels are rolled about. No retail car has an axle pivot.

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

## Water (vehSplash) — MM2

A car whose model origin drops below the level of a water room (room flag 4,
`city/<map>.water`) floats (`vehCar::Update` activates `vehSplash`): a
4 × 4 × 4 grid of points spanning the InertiaBox around the model origin,
each point below the surface adding Mass × buoyancy upwards and
-0.08 × Mass × its velocity. The buoyancy starts at 0.7 per point (against
19.6 for the whole car) and falls by 0.03 per second to 0.4, so a car bobs
high, then settles lower. The point velocity includes the unit vector of the
car's world position, a quirk of the original kept as is. The race rules
respawn or end the race after 5 s in the water. `vehCar::Reset` only clears
the splash's active flag (`vehSplash::Reset` runs once, from its
constructor), so a car reset after sinking keeps its lowered buoyancy, and
`vehCar::Update` runs the splash (and `vehStuck`) only while the car is
drivable (not while it is held on the start line). `vehSplash::Init` first
fills the points with random directions (192 calls of the game's `frand`)
and then overwrites them with the grid; OpenMM2 skips the random fill.

## Damage (vehCarDamage) — MM2

CurrentDamage falls by RegenerateRate per second; damage is the fraction
between MedDamage and MaxDamage. Impacts add to it through the impact list
(see "Collision", "Damage and sounds"). MM2 has no global damage scale
(Midtown Madness 1's GlobalDamageScale is gone).

* With damage enabled and CurrentDamage strictly past MaxDamage
  (`mmPlayer::IsMaxDamaged`), `mmPlayer::Update` takes the player's throttle,
  steering and brake away (all 0) every frame; the race modes then hold the
  wreck with `vehCar::SetDrivable(0, 1)` (brake on, neutral; game rules).
* The wheels wobble with the damage fraction (`vehCarDamage::Update`, its
  `bWobble` switch is on): front-left and back-right by -0.15 of it, the
  other two by 0.35, faded out as the front-left wheel spins faster
  (factor 1 - |w| dt 2/pi, clamped).
* Cops and Robbers (`mmPlayer::EnableRegen`): `mmPlayer::UpdateRegen` heals
  the player's car by MaxDamage / 2000 a frame above 5 m/s and clears the
  damage once that empties it (`CarSim::regenerate`).

## Opponents and police

MM2 builds AI cars with `vehCar::Init(<car>)` (`aiVehiclePhysics::Init`):
the same tune as the player's. The retail `*_opp.vehCarSim` and
`vpcop_cop.vehCarSim` files (MM1-era layouts) are never loaded by the game.
The player's own vpcop is the exception the other way round: `mmPlayer::Init`
runs `vehCarSim::Init` again with "vpmustang99" (unless the `-tune_car`
option is given), so the player's police car drives on the Mustang's tune and
wheel pivots, with vpcop's body, bound, damage, gyro and stuck tunes
(`SimVehicle::loadPlayer`).

## Player input (mmGame, mmInput, mmPlayer) — MM2

* `mmGame::UpdateSteeringBrakes` hands the car the pedals (swapped while
  reversing) and runs the automatic reverse (see Transmission).
* Keyboard and gamepad steering go through `mmInput::FilterDiscreteSteering`
  / `FilterGamepadSteering`: a filtered position moves towards the target
  (full lock or the stick) at DeltaOut per second when it turns further the
  same way and DeltaIn otherwise, and the car gets sign × |position|^Filter.
  `mmPlayer::Update` sets the three from the player's tune by speed:
  f = clamp(speed, 5, 100) / 95 (the clamped speed over the range), value =
  Lo + (Hi - Lo) f, with DeltaIn 2.5 → 1.5, DeltaOut 3.5 → 2.5, Filter 2 →
  1 by default (the constructor's values). The player node is named after
  the car and loads `tune/<car>.asnode` (`mmPlayer::FileIO`), which every
  retail car but vpcentury and vpdune ships: vpbug's, for example, has
  SpeedBaseHi 44.6, DeltaOut 2.573 → 0.8, DeltaIn 5 → 5 and Filter 1.2 →
  1.2, and the same file sets the mouse, joystick and wheel curves of
  `mmPlayer::FilterSteering` (`GameInput::setPlayerTune`).
* After `mmGame`'s input, `mmPlayer::Update`: once the race is over the car
  brakes with the wheel turned full left; a wreck loses throttle, steering
  and brake; below 4 mph without throttle the handbrake holds the car.
  `mmPlayer::UpdateHOG` (setting a flipped car upright) never fires in build
  3393: its "collided" flag (vehCarSim +0x1540) is only ever cleared.
* Before the start the game holds the car with `vehCar::SetDrivable(0, 1)`:
  `vehCar::PreUpdate` puts the brake on and the gearbox in neutral each
  frame, the throttle revs the engine freely and the steering and handbrake
  stay the player's; `SetDrivable(1, ...)` selects first gear
  (`SimVehicle::hold` / `drive`).
* The player's semi has no trailer in multiplayer cruise and Cops and Robbers
  (`mmPlayer::Init`'s flag to `vehCar::Init`); AI semis always tow theirs.

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
`GetInvMassMatrix`, `IsBroken`). `Collider::joint` and `Collider::invMassMatrix`
are `phColliderJointed`'s `Attach` and `GetInvMassMatrix`. MM1's `Joint3Dof`,
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
| WheelFront, WheelBack, Drivetrain | TWHL0 (TWHL1 copies all but HandbrakeCoef and WobbleLimit), TWHL2 (TWHL3 likewise), the first drivetrain (the others copy) | MM2 |
| (impact elasticity/friction) | vehTrailer sets none: the trailer's bound (`bound/<car>_trailer_bound.bnd`) keeps the materials it names, which MM2 looks up in the city's material manager; OpenMM2 gives it the bound default (elasticity 0.5, friction 1) | inferred |

Impacts see the inverse mass matrix through the joint while it holds
(`phColliderJointed::GetInvMassMatrix`), and the tractor and its trailer do
not collide with each other (`dgPhysManager::Update`). The
Ctrl+B debug key breaks every holding joint in the frame it goes down
(`dgTrailerJoint::Update` checks it first and then does nothing else that
sample). TWHL4/TWHL5 (vpcentury's second trailer axle) are neither simulated
nor drawn: `vehTrailerInstance::Init` loads only TWHL0–3 (drawing them at
their offset from TWHL2/3 is mm2hook's addition). A car's own WHL4/WHL5
(`vehCarModel::Draw`) are drawn with the WHL2/WHL3 matrices moved back along
the car's Z axis by 2.2 times that wheel's radius, not at their pivots.

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

## Collision — MM2

`phys/World` is MM2's collision manager, `dgPhysManager`; the bounds, the
narrow phase and the impact response are ports of the `phBound` family,
`phCollision`, `phBoundPolygonal`, `phBoundBox`, `phBoundSphere`,
`phBoundHotdog`, `phBoundTerrain`, `lvlSDL`, `phImpact`, `phContactMgr` and
`dgImpact` (build 3393, MM2Recomp). Function names are cited in the code.

### What collides

* **Movers** (`phys::Body`, a `dgPhysEntity` with its `lvlInstance`): cars,
  trailers, traffic cars that left their rail, knocked-over props. Each has a
  collider (`phColliderBase`): its bound (`GetBound(0)`), the bound's world
  matrix, its `phInertialCS`, and the matrix it had at the end of the
  previous sample. Mover flags: 2 the city, 8 the objects around, 0x10 the
  other movers (cars and traffic: all three, `DeclareMover(..., 0x1b)`).
* **The city** (`lvlSDL`): for each mover, `sdlPage16::Collect` builds the
  collision polygons of its room and of the neighbours its bounding sphere
  touches (`cityLevel::GetTouchedNeighbors`), at most 256, culled by the
  sphere (`src/city/SdlCollect`, docs/formats/psdl.md). Their materials come
  from `city/materials.csv` (texture → material name) resolved in the
  material manager (`lvlLevelBound::GetMaterial`: 0 is the manager's
  built-in default, an `lvlMaterial` as its constructor leaves it:
  elasticity 0.5, friction 1, width 1, no particles; the `_default` block of
  `city/materials.mtl` is an entry of its own). The wheels get the built-in
  default for every polygon whose texture maps to `none`.
* **Objects** (`lvlInstance` in the rooms' lists): the city's collidable
  instances (`.inst` flag 0x2000: their `bound/<name>_bound.bnd` geometry,
  scaled by the matrix's row lengths; `lvlMultiRoomInstance::Create` puts a
  stand-in in every neighbour of their room that their sphere reaches
  across its perimeter, but not in the room itself, and moves the object to
  room 0, so one reaching no neighbour is never collided; flag 0x100: a
  terrain bound of their own space, `.bbnd` + `.ter` (version 1.1, with the
  geometry's polygon count, else "Malformed terrain" and no bound), or the
  `.bnd` without a `.ter`, in their own room only), unhit and resting props,
  traffic cars on their rails. Instances with neither flag are drawn only
  (building walls are the PSDL's facade bounds). An object's sphere is its
  position and its model's radius (`lvlInstance::GetRadius`, the geometry
  set's), which a terrain-local bound raises to its box's farther corner.
  A car's sphere is centred one up axis above its centre of mass
  (`vehCarModel::GetPosition`), a trailer's on its centre of mass
  (`vehTrailerInstance::GetPosition`); their radii are the "body" and
  "trailer" geometries', which their bounds do not raise. The car's room
  follows the same point (`vehCar::Update`).
  Materials a text bound file adds are `lvlMaterial`s: the file's
  elasticity, friction, effect and sound, drag 0, width 1, height 0, depth 0
  and no particles.

### A sample (`dgPhysManager::Update`)

1. The movers' updates (`dgPhysEntity::Update` and the entity's own: see "How
   a sample runs"); the bound's world matrix follows the body (a car's is its
   model matrix, which does not see the sample's push until the next
   integration); room update (`lvlLevel::MoveToRoom`).
2. `GatherCollidables`: the instances of the mover's room and touched
   neighbours whose bounding spheres meet the mover's
   (`TrivialCollideInstances`; a prop with a YRadius is tested in the ground
   plane at its foot with that radius), at most 32.
3. Per mover: `CollideTerrain` (the city), then every later mover
   (`TrivialCollideInstances`, skipped for colliders sharing an unbroken
   joint), then its gathered instances (`CollideInstances`).
4. Movers attached by the collisions (`NewMover`) join; `UpdateMtx`: each
   mover's pending push moves it (`phInertialCS::MoveICS`) and the collider
   remembers its matrix and which collider pushed it hardest.

The penetration tolerance is 0 for the whole session (the manager's
constructor calls `phContact::DisableContacts`, which sets it), and contacts
(`phContact`) are off: every collision is an impact.

### Bounds and impacts

* Cars: the player's (and network) cars use the polygonal bound of
  `bound/<car>_bound.bnd` (`vehBound`, one own material with BoundFriction /
  BoundElasticity); AI opponents and police use a box around it
  (`dgBoundBox`, `vehCarModel::InitBound(..., false)`). Text `.bnd` files:
  `quad a b c 0` stays a quad starting at b (`phBoundGeometry::Load`); edges,
  edge normals (sum of the two faces' normals) and edge cosines are computed
  as `PostLoadCompute` does.
* Traffic cars: a box of the `.aivehicledata` Size at its CG field, with the
  default material (elasticity 0.5, friction 1). Props: `dgBangerData`'s
  CollisionPrim (geometry shifted by -CG, box, hotdog, sphere) with the
  prop's own material.
* Polygonal pairs (`phBoundPolygonal::TestBoundPolyPoly`): after a
  separating test along the line between the bodies, each bound's vertex
  sweeps (from the last matrix to the current one) and edges are run through
  the other's polygons; `FindImpacts` turns the intersections into impacts:
  a vertex that crossed a face (`DoEndPtSearch`), two edges that cross
  (`CheckSaveEdgeEdge`), an edge through a face across one of the face's
  edges (`GetCollideEdgePoly`), then the vertices left over
  (`RetryVertPolyCollide`). Box against box has its own search
  (`FindImpactsBoxToBox`), spheres and hotdogs theirs. The city works the
  same way with the city as side A and only the mover's vertices and edges
  tested (`lvlSDL::CollidePolyToLevel` and its impact search).
* An impact (`phImpact`) holds the contact point, a unit normal from B
  towards A, the depth, and the friction and elasticity of the two materials
  (`FindFrictionAndElasticity`: friction = fA · fB, elasticity =
  min(eA · eB, 1); the "/blubber" cheat raises the cap to 4).

### Response (`phImpact::CalcCollision`, `phContactMgr::CalcImpact`)

For each of a pair's n impacts, weight 1 / n:

* Unless the contact separates faster than 0.01 m/s, j = (Ma + Mb)⁻¹ (−v),
  v the relative velocity of the contact points and M each body's inverse
  mass matrix there (through the trailer joint while it holds). If its
  tangential part exceeds friction × its normal part, j points along
  n + friction · t̂ instead, sized to stop the normal motion. j × (1 +
  elasticity) × weight goes to A's impulse accumulator, −that to B's (they
  act at the next integration).
* The depth becomes pushes: each body takes |M n| / (|Ma n| + |Mb n|) of it,
  A along n, B against it; a push downwards goes to the other body when that
  one can move. Pushes are not weighted (`phInertialCS::CalcNetPush` adds
  only what the pending push does not cover).
* Each collider's impact callback gets the impulse it took (cars:
  `vehCarDamage::Impact`, traffic: `aiVehicleActive::Impact`).

Unhit props (`dgImpact::CalcImpact`): the impulse that would stop the car
against an immovable prop; up to sqrt(ImpulseLimit2) the prop holds like a
wall, beyond it breaks loose (`dgUnhitBangerInstance::Impact`) and both share
what the remaining relative motion needs. A traffic car on its rail is
attached when hit (`aiVehicleInstance::AttachEntity`: a rigid body at the
rail speed, four cheap wheels) and the impact is resolved with both bodies.

Because the impacts of one pair see the same velocities and share the
weight, a box landing flat on four corners takes a quarter of each corner's
stopping impulse: MM2 bodies land with little bounce, and a body resting on
its bound keeps a small downward velocity that the sample's push cancels
(`phSleep` judges rest by the velocity with the pushes).

### Traffic cars (`aiVehicleInstance`, `aiVehicleActive`) — MM2

`game/TrafficBodies`. A car on its rail is an instance of its room: a box of
the `.aivehicledata` Size at its CG (`aiVehicleManager::AddVehicleDataEntry`,
`dgBoundBox` with the default material: `aiVehicleData::SetFricElas` is never
called), placed by the AI's matrix; its position for the room and sphere
tests is m3 + m1 (`aiVehicleInstance::GetPosition`; the radius, the model's
in MM2, is the box's sphere about that point, inferred). Hit, it attaches one
of 32 bodies (`aiVehicleManager::Attach`; when all are taken the first slot is
let go): the InertialCS at the model origin with `InitBoxMass(Mass, Size)`,
moving at the rail speed along -Z, its collider's last matrix moved back by
one sample of that motion, four `vehWheelCheap` (a spring and damper per
wheel with locked-wheel rubber grip, 0.4 × load × WeatherFriction), and a
`phSleep` with thresholds 0.01 / 0.01. The collision manager then resolves the
impact between both bodies. Once asleep, or below y −100, the body is handed
back to the AI (`aiVehicleActive::Detach`: upright when a probe along its up
axis finds ground facing within 0.9 of it). Its impacts play AudImpact with
the car's own collider id (0) and |x| + |y| + |z| of the impulse, and above
an impulse of 100 reach its breakable parts (`vehBreakableMgr`, threshold
2500 for traffic).

### Props (`dgBangerData`, `dgUnhitBangerInstance`, `dgBangerActive`) — MM2

`game/bangers/BangerSet`. A standing prop is a banger instance of its room
(props placed without a room get `findRoom` at their placement point, as
cityLevel::LoadPath does): its bound
(`dgBangerData::InitBound` by CollisionPrim: the `<name>_bound` geometry
shifted by −CG, else a box of Size; a box; a hotdog of YRadius and Size.y; a
sphere of YRadius) with its own material (`AdjustPrim`: the data's elasticity
and friction), its matrix at the CG, its collider id the data's ColliderId,
and for the sphere test its foot with YRadius (radius otherwise the bound's
sphere about the origin, inferred). Touched, it attaches one of 32 bodies
(`dgBangerActiveManager::Attach`, reusing the oldest when full): at rest,
`InitBoxMass(Mass, Size)`, `SmoothAngInertia(40)`, phSleep thresholds 0.1 /
0.5. If `dgImpact` breaks it loose, `dgUnhitBangerInstance::Impact` turns it
into one of 40 knocked-over props (the oldest disappears; MM2's ring hands
slot 0 out twice after each wrap) that keeps the body and its pending
impulses, or splits it into its parts, each taking the velocity change those
impulses give the whole; otherwise the body is let go. A knocked-over prop
collides as a plain object and attaches a body when hit. The bodies collide
by CollisionType (0x10 or 0x40: everything; 0x4: the city only; 0x2: no
collisions) and stop being simulated asleep or below y −100. Parts thrown off
cars (`vehBreakableMgr::Eject`) get the random speed as momentum, so heavy
parts barely move (as the code does). OpenMM2 removes a broken prop's bound
for the rest of the sample instead of taking it off the lists the movers
gathered (`NewMover`'s second argument).

### Damage and sounds (`vehCarDamage::Impact`, `InsertImpact`, `ApplyImpact`)

`vehStuck::Impact` first. The impact is worth |impulse| × the other body's
share of the two masses (1 against the city and objects). A list of 12
entries keyed by the other collider: a new collider is applied
(`ApplyImpact`) and stays listed until RelaxTime (0.2 s) passes; an impact
from a listed collider worth more than 1.25 times its last value is applied
again (with the first contact's point and impulse), a weaker one above
ImpactThreshold only adds damage (at 10 mph or more, or against a body).
`ApplyImpact`: above 0.001 the impact sound (`AudImpact::Play` with
|x| + |y| + |z| of the impulse and the other collider's id: a prop's AudioId,
0 otherwise); above ImpactThreshold, at 10 mph or more or against a body:
sparks (above 15 mph), shards, damage, texel damage, breakables and the
game's impact callback (the hit counts).

## simcar results

`mm2tool simcars <game> 60`: full throttle from rest on flat asphalt
(the material manager's built-in `default` material, friction 1),
automatic gearbox, fixed 1/60 s step, retail geometry
(wheel pivots from `.mtx`, body box from the bound). The `.info` Top Speed is
the menu's statistic, not a measurement. vpsemi and vpcentury tow their
trailers. vpcop is the police car's own tune (as the AI drives it); the
player's vpcop drives as vpmustang99.

| car | drive | mass | hp | High (mph) | 0-60 (s) | 1/4 mile (s) | top (mph) | .info Top Speed |
|---|---|---|---|---|---|---|---|---|
| vp4x4 | 4WD | 2500 | 550 | 85 | 4.00 | 12.47 | 104.6 | 57 |
| vpauditt | RWD | 1300 | 551 | 120 | 4.15 | 12.27 | 146.5 | 182 |
| vpbug | FWD | 1000 | 260 | 90 | 7.08 | 15.55 | 115.9 | 91 |
| vpbullet | RWD | 1300 | 550 | 110 | 4.23 | 12.40 | 133.1 | 137 |
| vpbus | RWD | 5000 | 450 | 83 | 13.47 | 19.52 | 103.6 | 60 |
| vpcab | RWD | 1000 | 300 | 95 | 6.75 | 15.02 | 126.7 | 103 |
| vpcaddie | RWD | 1300 | 550 | 110 | 4.38 | 12.60 | 133.0 | 136 |
| vpcentury | RWD | 3500 | 750 | 75 | 10.97 | 18.20 | 112.5 | 91 |
| vpcoop | FWD | 800 | 250 | 80 | 9.27 | 17.27 | 104.3 | 60 |
| vpcoop2k | FWD | 800 | 300 | 108 | 7.92 | 16.05 | 132.0 | 115 |
| vpcop | RWD | 1300 | 750 | 140 | 2.92 | 10.67 | 164.9 | 160 |
| vpdb7 | FWD | 1573 | 550 | 150 | 4.58 | 12.68 | 177.3 | 206 |
| vpddbus | RWD | 4915 | 456 | 65 | 9.87 | 17.37 | 99.5 | 25 |
| vpdune | FWD | 1000 | 400 | 106 | 4.43 | 12.73 | 136.8 | 170 |
| vpford | RWD | 2500 | 550 | 85 | 6.40 | 14.87 | 104.1 | 58 |
| vpmustang99 | RWD | 1300 | 500 | 115 | 5.43 | 13.60 | 135.3 | 160 |
| vppanoz | FWD | 1300 | 650 | 151 | 4.35 | 12.38 | 177.2 | 216 |
| vppanozgt | RWD | 1200 | 902 | 180 | 2.60 | 10.22 | 275.1 | 240 |
| vpsemi | RWD | 3500 | 896 | 85 | 11.47 | 18.55 | 107.0 | 69 |
| vpvwcup | FWD | 1000 | 550 | 122 | 3.78 | 11.97 | 155.9 | 194 |

With the wheel centred the cars do not all run exactly straight, because
their pivots are not mirrored. vpbug's right wheels sit 3.9 cm further out
than its left ones, and vpsemi's hitch is 7.4 cm left of the tractor's
centreline. The drive and the trailer's drag therefore turn them slowly:
over a minute at full throttle, vpbug turns about 3 degrees and the vpsemi
rig about 40 degrees, both to the left. OpenMM2 did this before the parity
audit too. That MM2 does the same is inferred (same data, same forces),
not observed.

Top speeds are set by power against drag, or by MaxRPM in top gear: High is
the top gear's speed at OptRPM. (Before the MM2 port, OpenMM2 took Low/High
as speeds at MaxRPM and capped every car at High.)

## Known gaps

- Collision: lvlSDL's sphere and hotdog searches against the city (unnamed
  helpers of `dgPhysManager::CollideTerrain`) and force spheres
  (`phCollision::TestBoundForce`) are not ported; no race object uses them
  (inferred). The trailer's bound materials resolve to the bound default
  (MM2 looks their names up in the city's material manager). A body outside
  every room keeps its last room (MM2 moves it to room 0). `phys::World`
  models dgPhysManager's mover table (`Body::declare(type, flags)`: at most
  32 movers a frame, the rooms around type-3 and type-4 movers active,
  type-1 movers outside them left out and detached, the flags' update and
  collision bits). The props declare their actives by CollisionType
  (bangers.md), so knocked-over props are culled as in MM2; the other
  owners do not declare theirs yet: their bodies are type-2 movers with all
  flags, so the
  rooms around opponents within 200 m of a player (type 3 in MM2) do not
  keep props alive, and police beyond 250 m (not declared in MM2) are
  still simulated. (MM2's opponents beyond 200 m and police between 200 and
  250 m drop flag 0x8, which changes nothing while 0x2 and 0x10 are set.)
  Movers without a body (traffic on its rail while it avoids, regains its
  rail or collides) are not supported.
- Ground probes other than the wheels of physics cars and trailers (cheap
  traffic wheels, the AI, spawning, line of sight) still use OpenMM2's
  probe geometry (the PSDL's render mesh plus the instances' bounds);
  `World::wheelProbe` is MM2's.
- Trailers: OpenMM2 corrects vehTrailer::Init's static loads by default
  (MM2's values make vpcentury's trailer ride on its bump stops, see
  "Trailers"); the trailer's impact parameters are inferred.
- The per-axis angular velocity limits of non-car bodies other than
  trailers and `vehSuspension` (the visual shocks) are not ported.
- `dgPhysManager::CollideTerrain` would ask each mover
  `RequiresTerrainCollision` (`CarSim` and `Trailer` port it) before
  colliding its body with the room's terrain, but that branch is gated by a
  global `mmGame::Init` sets to 0, so MM2 never asks in a race and neither
  does the World.

