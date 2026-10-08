# Parity audit: vehicle

Audited against MM2Recomp (midtown2.exe build 3393) on 2026-10-07.

Summary: 141 functions (rows; a few group overloads or a struct's
defaults); verified 76, fixed 42, deviation 6, inferred 2, open 1,
openmm2 14. (Second pass after the merge with phys-core, 2026-10-08: see
"Second pass" at the end.)

Scope: `vehCarSim` and its parts (`vehWheel`, `vehDrivetrain`, `vehEngine`,
`vehTransmission`, `vehAero`, `vehAxle`, `vehGyro`, `vehStuck`,
`vehSplash`, `vehCarDamage`, `vehTrailer`), the physical parts of `vehCar`
and `vehCarModel`, the tune parsers, and the player's car (`mmPlayer`,
`mmGame::UpdateSteeringBrakes`, `mmInput`'s steering filters,
`aiVehiclePlayer`). Every float-heavy function was compared against the
assembly, not only the decompile: Ghidra prints commutative operands in its
own order, and the earlier port had followed that order in many sums and
products (fixed below; the results differ in the last bits with 32-bit
floats). Facts checked again and kept: gear speeds at OptRPM, no `_opp`
tunes, gravity 19.6, one 1/60 s sample per frame at 60 fps, the
drivetrain's breakpoint sentinel bug, the model origin at body + R * CG, no
park brake in vehCarSim, MetricFactor 2.2360249, WeatherFriction 0.8 / 0.75.

## src/phys/vehicle/TuneParams.h, TuneParams.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `AeroParams` defaults | `vehAero::vehAero` | verified | all zero; the enable flag (+0x18) is 1 |
| `EngineParams` defaults | `vehEngine::vehEngine` | verified | AngInertia 1, MaxHorsePower 200, IdleRPM 750, OptRPM 5000, MaxRPM 8000, GCL 0.25 |
| `TransmissionParams` defaults | `vehTransmission::vehTransmission` | verified | ManualNumGears 7, AutoNumGears 6, Reverse 20, Low 20, High 75, GearBias 0.5, UpshiftBias 0.05, DownshiftBiasMin 0.05, DownshiftBiasMax 0.3, GearChangeTime 0.8 |
| `DrivetrainParams` defaults | `vehDrivetrain::vehDrivetrain` | verified | AngInertia 5000, BrakeDynamicCoef 1, BrakeStaticCoef 1.2 |
| `WheelParams` defaults | `vehWheel::vehWheel` | verified | all 19 fields, with radius 0.3, width 0.1 and static load 5000 in `Wheel` |
| `AxleParams` defaults | `vehAxle::vehAxle` | verified | 0, 0 |
| `CarSimParams` defaults | `vehCarSim::vehCarSim` | verified | Mass 2000, InertiaBox 2 1 3, CG 0, BoundFriction 0.3, BoundElasticity 0.2, DrivetrainType 0, SSSValue 1, SSSThreshold 0, CarFrictionHandling 1 |
| `GyroParams` defaults | `vehGyro::vehGyro` | verified | all zero; the drift flag (0x20000) is set by the constructor |
| `StuckParams` defaults | `vehStuck::vehStuck` | verified | Turn 1.57, Rotation 0.39, Translation 0.1, TimeThresh 0.3, PosThresh 1.25, MoveThresh 1.75 |
| `CarDamageParams` defaults | `vehCarDamage::vehCarDamage` | verified | MaxDamage 1e6, MedDamage 5e5, ImpactThreshold 100, RegenerateRate 0, SmokeOffset (0, 0.8, -1.8), SmokeOffset2 0, TextelDamageRadius 0.4, DoublePivot / MirrorPivot off; damage enabled |
| `TrailerParams` defaults | `vehTrailer::vehTrailer`, `vehTrailer::Init` | verified | Mass 3000, InertiaBox 3 4 9; the hitches come from the pivots and the file's CarHitchOffset / TrailerHitchOffset replace them (Init loads the file after reading the pivots) |
| `TrailerJointParams` defaults | `dgTrailerJoint::Init` | verified | every value; the "<car>_trailerjoint" name Init formats is never used, the file is `<car>.dgTrailerJoint` |
| `loadWheelParams` | `vehWheel::FileIO` | verified | the 19 names |
| `loadCarSimParams` | `vehCarSim::FileIO` and the `vehAero`, `vehEngine`, `vehTransmission`, `vehDrivetrain` (Drivetrain, Freetrain), `vehWheel`, `vehAxle` FileIO | deviation | names and blocks verified; OpenMM2 clamps DrivetrainType to 0..2, where `ConfigureDrivetrain` would build no drivetrain (no retail file has another value) |
| `loadGyroParams` | `vehGyro::FileIO` | verified | |
| `loadStuckParams` | `vehStuck::FileIO` | verified | |
| `loadCarDamageParams` | `vehCarDamage::FileIO` | verified | DoublePivot / MirrorPivot are datParser bools (GetInt != 0). The same file carries the engine smoke birth rule (`EngineSmokeRule`'s FileIO), which belongs to rendering-fx |
| `loadTrailerParams` | `vehTrailer::FileIO` | verified | |
| `loadTrailerJointParams` | `dgTrailerJoint::FileIO` | verified | |
| tune values as read | `datParser::Read`, `datAsciiTokenizer::GetFloat` / `GetInt` | fixed | Checked with an emulation of datParser::Read (whitespace tokens, atof/atoi, unknown field: skip its block when the next token is '{', otherwise skip to the end of the line; last assignment wins) over every vehicle tune MM2 loads: identical values, except `vpftruck.vehCarSim` (an MM1 layout; the car has no `.info` and no race uses it). There MM2 reads the labelled `Aero asAero :addr {` block, swallows ManualNumGears in the skipped MM1 GearRatios lines, and, because `AsphaltRule asBirthRule :addr {` is skipped only to its end of line, takes vehCarSim's Mass from that rule's `Mass 0.1` and ends the block at the rule's `}`. The formats audit's record-list reader (`phys::carSimSchema`, commit 7584956) now reproduces this: vpftruck reads Mass 0.1 and no ManualNumGears; every other tune is unchanged. |
| `noteIgnored` | | openmm2 | diagnostics for unread fields |

## src/phys/vehicle/VehicleGeometry.h, VehicleGeometry.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `VehicleGeometry::wheelFromPivot` | `vehWheel::Init` | verified | centre = pivot row 3, radius = |m1.y - m0.y| / 2, width = m1.x - m0.x |
| `VehicleGeometry::placeholder` | | openmm2 | fallback geometry where a pivot is missing (MM2 would read an unset matrix) |

## src/phys/vehicle/Wheel.h, Wheel.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `physFrand` | `irand`, `frand` | verified | seed * 214013 + 2531011, bits 16..30, times 2^-15 |
| `Wheel::init` | `vehWheel::Init` | verified | trailer wheels get flag 1, which vehWheel never tests |
| `Wheel::copyVars` | `vehWheel::CopyVars` | fixed | MM2 copies every tune field except HandbrakeCoef and WobbleLimit, so the right wheels (and TWHL1/TWHL3) keep HandbrakeCoef 1 whatever the file says; OpenMM2 gave all four wheels the file's values |
| `Wheel::computeConstants` | `vehWheel::ComputeConstants` | verified | asm; static load split by |z - cg.z| / 2|z| |
| `Wheel::addNormalLoad` | `vehWheel::AddNormalLoad` | verified | at least 1 N |
| `Wheel::setNormalLoad` | `vehWheel::SetNormalLoad` | verified | asm; SuspensionFactor clamped to 0.75 |
| `Wheel::reset` | `vehWheel::Reset` | verified | OpenMM2 also puts the drawing matrix at the pivot |
| `Wheel::setInputs` | `vehWheel::SetInputs` | verified | asm; flag 4 holds the wheel at full friction |
| `Wheel::computeFriction` | `vehWheel::ComputeFriction` | verified | asm |
| `Wheel::calcSuspensionForce` | `vehWheel::CalcSuspensionForce` | fixed | asm. The bottoming impulse now calls `calcCollisionNoFriction` (phImpact::CalcCollisionNoFriction) instead of an inline copy that summed the inverse mass matrix in another order |
| `Wheel::bumpDisplacement` | `vehWheel::GetBumpDisplacement` | verified | asm; the game's `frand` on World::randomSeed |
| `Wheel::computeDwtdw` | `vehWheel::ComputeDwtdw` | fixed | asm. up . normal, |back|^2 and the lateral, forward and normal contact velocities are summed z, y, x as MM2 does; the probe's intersection is kept whenever it hits (dgPhysManager::Collide fills it before the wheel's own checks), which vehCar::RequiresTerrainCollision reads. Breakpoints, surface, sinking, CarFrictionHandling verified. After the merge the probe is `World::wheelProbe` (dgPhysManager::Collide) with the wheel's own `ProbeCache` (its lvlSegmentInfo), ignoring the car's own instance; trailer wheels ignore none, as vehWheel::Init without a vehCarSim leaves it. Re-checked against the asm |
| deep-water test in `Wheel::computeDwtdw` | | openmm2 | material depth >= 1 carries no wheel; vehWheel has no such test. With a level loaded it is a no-op: the city's collision polygons never include deep water (lvlSDL's Collect skips the deepwater fans), so the probe cannot hit it. It acts only where OpenMM2 probes its own geometry (tests, simcar) |
| `Wheel::update` | `vehWheel::Update` | fixed | asm. The friction circle squares mu * load once; the contact force is summed suspension, lateral force, longitudinal force, lateral drag, longitudinal drag (OpenMM2 had the reverse); spin and wobble go through Matrix34::Rotate (`age::rotate`). Tyre displacement model, slip, slide and drag verified |
| `Wheel::visualDispVert` / `visualDispLat` / `visualDispLong` | `vehWheel::GetVisualDispVert` / `Lat` / `Long` | verified | |
| `makeRotateY` | `Matrix34::MakeRotateY` | verified | asm |
| `WheelEnv` | | openmm2 | per-sample inputs (MM2 reads them from vehCarSim and globals) |

## src/phys/vehicle/Drivetrain.h, Drivetrain.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Drivetrain::configure` | `vehDrivetrain::FileIO`, `CopyVars` | verified | |
| `Drivetrain::reset` | `vehDrivetrain::Reset` | verified | speed 0, diff ratio 1 |
| `Drivetrain::addWheel` | `vehDrivetrain::AddWheel` | verified | |
| `Drivetrain::attach` / `detach` | `vehDrivetrain::Attach` / `Detach` | verified | |
| `Drivetrain::update` | `vehDrivetrain::Update` | verified | asm: brake (50 + sum * coef), engine coupling, brake hold, inertia, limited-slip differential (1.25 / 1.03 / 50, 0.02 = 1/50 from the static initialiser), the breakpoint loop with its sentinels (never triggers), MaxRPM clamp, engine follow, wheel speeds, then the wheels |

## src/phys/vehicle/Engine.h, Engine.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Engine::configure` / `reset` | `vehEngine::Init`, `vehEngine::Reset` | verified | |
| `Engine::computeConstants` | `vehEngine::ComputeConstants` | verified | asm |
| `Engine::calcTorqueAtFullThrottle` | `vehEngine::CalcTorqueAtFullThrottle` | fixed | asm: above OptRPM MM2 multiplies ((w + max) - 2 opt) * (opt / phi + w) * (phi opt - w) * (max - w) in that order; OpenMM2 had the reverse |
| `Engine::calcTorqueAtZeroThrottle` | `vehEngine::CalcTorqueAtZeroThrottle` | verified | asm |
| `Engine::calcTorque` / `calcHPAtFullThrottle` | `vehEngine::CalcTorque` / `CalcHPAtFullThrottle` | verified | asm |
| `Engine::update` | `vehEngine::Update` | fixed | asm. The neutral reaction torque is given in the engine pivot rocked by 0.05 × normalised torque about its Z axis (MM2 keeps that matrix); OpenMM2 used the pivot's rest frame, and summed the torque in another order. Gear-change lag, clutch, free revving verified. No retail car has an engine pivot |

## src/phys/vehicle/Transmission.h, Transmission.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `gearRatioFromMph` | `vehTransmission::GearRatioFromMPH` | verified | 1609.344 * 0.016666668 as MM2's static initialiser computes it |
| `fillRatios` | `vehTransmission::ComputeConstants` (ratios) | verified | asm; __CIpow's result rounded to float |
| `Transmission::configure` | constructor, `vehTransmission::FileIO` | verified | constructor tables (-10, 0, 30/i; 6000 / 2000 / 2000); OpenMM2 limits the gear counts to the 8 slots MM2's tables have |
| `Transmission::computeConstants` | `vehTransmission::ComputeConstants` | verified | asm: bisection, shift points, auto then manual box |
| `Transmission::reset` | `vehTransmission::Reset` | verified | |
| `Transmission::setCurrentGear` | `vehTransmission::SetCurrentGear` | verified | |
| `Transmission::upshift` / `downshift` | `vehTransmission::Upshift` / `Downshift` | verified | |
| `setDrive` / `setReverse` / `setNeutral` / `automatic` | `SetForward` / `SetReverse` / `SetNeutral` / `Automatic` | verified | |
| `Transmission::update` | `vehTransmission::Update` | verified | nothing (not even the time in gear) while no wheel is down |
| `Transmission::getCurrentGear` | | openmm2 | the gear numbered -1 reverse, 0 neutral |

## src/phys/vehicle/Aero.h, Aero.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Aero::configure` | `vehAero::FileIO` | verified | |
| `Aero::update` | `vehAero::Update` | fixed | asm: the body-axis angular velocities and the returned torque's y and z are summed in MM2's order. The world-space fade below 1 rad/s, the one-sample limit, drag and downforce verified |

## src/phys/vehicle/Gyro.h, Gyro.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Gyro::configure` | `vehGyro::FileIO` | verified | |
| `Gyro::update` | `vehGyro::Update`, `vehCar::Update` (its 0.01 handbrake / brake flags) | fixed | asm: the drift term uses s = SSS × steering rounded to a float and \|s\| s (OpenMM2 multiplied \|s\| by SSS and steering again); the pitch and roll products follow MM2's grouping. Spin180 / Reverse180 verified |

## src/phys/vehicle/Splash.h, Splash.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Splash::init` | `vehSplash::Init` | deviation | grid verified (asm); MM2 first fills the points with random directions (192 `frand` calls on the game's random stream) and overwrites them, which OpenMM2 skips |
| `Splash::reset` | `vehSplash::Reset` | verified | drag 0.08, buoyancy 0.7 |
| `Splash::activate` | `vehSplash::Activate` | verified | |
| `Splash::deactivate` | `vehCar::Reset` | fixed | vehCar::Reset clears only the active flag; CarSim::reset used to restore the buoyancy too (vehSplash::Reset runs once, from the constructor) |
| `Splash::update` | `vehSplash::Update` | verified | asm, including the unit position vector added to the point velocity |

## src/phys/vehicle/Stuck.h, Stuck.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `Stuck::configure` | `vehStuck::Init` | verified | squares of PosThresh and MoveThresh |
| `Stuck::reset` | `vehStuck::Reset` | verified | |
| `Stuck::impact` | `vehStuck::Impact` | verified | |
| `Stuck::pegged` | `vehStuck::Pegged` | verified | |
| `Stuck::update` | `vehStuck::Update` | fixed | asm: the distance from the impact point is summed z, y, x. States, thresholds, flip, nudge and pegged turn verified |

## src/phys/vehicle/CarSim.h, CarSim.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `CarImpact` | `vehDamageImpactInfo` and `ApplyImpact`'s decisions | verified | |
| `CarDamage::reset` | `vehCarDamage::ClearDamage`, `Reset` | verified | (particles and smoke level: rendering-fx) |
| `CarDamage::addDamage` | `vehCarDamage::AddDamage` | verified | |
| `CarDamage::update` | `vehCarDamage::Update` | verified | asm: regeneration, fraction, impact timers. Smoke, EjectOneshot and texel damage are rendering-fx's |
| `CarDamage::globalScale` (removed) | | fixed | Midtown Madness 1's GlobalDamageScale; MM2 has none. Accessors renamed `maxDamage()` / `medDamage()` (RaceScreen's two uses updated) |
| `CarDamage::wrecked` | `vehCarDamage::Update`'s EjectOneshot test | verified | MaxDamage <= CurrentDamage, damage enabled |
| `CarDamage::maxDamaged` | `mmPlayer::IsMaxDamaged` | fixed | added: strictly past MaxDamage |
| `Axle`, axle part of `CarSim::init` | `vehAxle::Init`, `vehAxle::ComputeConstants` | fixed | asm: the second factor (1 / the left wheel's offset along the pivot's Z) was missing and the offsets are summed z, y, x. MM2 divides unguarded; a zero offset keeps factor 1 in OpenMM2. No retail car has an axle pivot |
| `CarSimOptions` | | openmm2 | weather friction (`mmGame::InitWeather`, verified), player, bound choice; switches for the gyro and axle coupling (on as in MM2) |
| `CarSim::init` | `vehCarSim::Init`, `ConfigureDrivetrain`, `vehCar::Init` | verified | mass and box inertia, 4 pi rad/s limit, wheels (see copyVars), drivetrain wiring per type and child order (engine, transmission, aero, freetrains, drivetrain, axles), splash box from the InertiaBox |
| `CarSim::reset` | `vehCarSim::Reset`, `vehCar::Reset` | fixed | the bound's friction and elasticity are restored (RestoreImpactParams) instead of unused fields, and the splash keeps its buoyancy. Placement by model matrix is OpenMM2's API (see resetAt) |
| `CarSim::resetAt` | `vehCarSim::SetResetPos`, `vehCarSim::Reset` | fixed | added: MM2 puts the body at the position + CG, turned about Y, so the model origin lands at position + CG + R * CG |
| `CarSim::halfExtents` | | openmm2 | for the AI |
| `CarSim::setPolygonalBound` / `buildBound` | `vehCarModel::InitBound`, `vehBound` | open | polygonal bound for the player, box otherwise, own material, friction / elasticity verified. MM2 builds the bound once per model type, so the first car of a model to be set up (the player's) decides for all of them: AI opponents and police in the player's model also collide with the polygonal bound. OpenMM2 decides per car; RaceScreen (session) would call `setPolygonalBound(true)` for AI cars of the player's model |
| `CarSim::setInputs` | vehCarSim's input fields | deviation | OpenMM2 clamps the inputs; MM2 takes them as given (a negative handbrake would act on the front wheels; the game never sends one) |
| `CarSim::modelMatrix` | `vehCarSim::SetWorldMatrix` | fixed | asm: R * CG summed in MM2's order (identical for retail CG.x = 0) |
| `CarSim::wheelMatrix` | the wheel's drawing matrix | verified | |
| `CarSim::wheelsOnGround` | `vehCarSim::OnGround` | verified | |
| `CarSim::bottomedOut` | `vehCarSim::BottomedOut` | fixed | added |
| `CarSim::requiresTerrainCollision` | `vehCar::RequiresTerrainCollision` | fixed | added (asm); the World does not ask it yet (phys-core) |
| `CarSim::regenerate` | `mmPlayer::UpdateRegen` | fixed | added (asm): above 5 m/s, MaxDamage * -0.0005 a frame, cleared once it empties; Cops and Robbers must call it (session) |
| `CarSim::sssFactor` | `vehCarSim::GetSSSFactor` | verified | asm |
| `CarSim::makeEnv` | | openmm2 | |
| `CarSim::beforeIntegrate` (player part) | `mmPlayer::Update` | fixed | finished, wrecked, 4 mph handbrake verified; the wreck test is now `mmPlayer::IsMaxDamaged` (strict) with damage enabled. OpenMM2 applies them every sample (MM2: every frame; the same at one sample a frame) |
| `CarSim::beforeIntegrate` (car part) | `vehCarSim::Update` | fixed | asm: forward speed summed z, y, x. Speed-sensitive steering, front handbrake for negative input, burnout, back-wheel handbrake split verified |
| `CarSim::afterIntegrate` | `vehCarSim::Update` (children), `vehCar::Update` | fixed | vehStuck and vehSplash now run only while the car is drivable; vehCarDamage's wheel wobble (bWobble, on: FL / BR -0.15, FR / BL 0.35 of the damage fraction, faded by the FL wheel's spin) was never set. Child order, gyro inputs, water activation verified |
| `CarSim::drivable` | vehCar +0xe8 bit 2 (`vehCar::SetDrivable`) | fixed | added |
| `CarSim::raceFinished` | mmPlayer +0x2258 | verified | |
| `CarSim::updateAxles` | `vehAxle::Update` | fixed | asm: the roll and mean travel written into the pivot matrix (m0.y, m2.y) before it is carried into the world, and Matrix34::Rotate (age::rotate) for the wheels. Anti-roll torque verified |
| `CarSim::onImpact` | `vehCarDamage::Impact` | verified | vehStuck::Impact first, then the list when damage is enabled |
| `CarSim::insertImpact` | `vehCarDamage::InsertImpact` | fixed | asm: \|impulse\|^2 summed x, y, z and the impact point taken into model space in MM2's order. Mass share, 1.25 re-trigger, threshold, 10 mph rule verified |
| `CarSim::applyImpact` | `vehCarDamage::ApplyImpact` | verified | asm (0.001 sound threshold) |

## src/phys/vehicle/Controls.h, Controls.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `PedalInput` | | openmm2 | the device values |
| `ArcadeControls::apply` | `mmGame::UpdateSteeringBrakes`, `mmInput::GetThrottle` / `GetBrakes` | verified | pedal swap, auto reverse (0.8, 5 m/s, 0.1); field comments corrected (AutoReverse +0x18c, SwapThrottle +0x1d4). The Cops and Robbers throttle cap is the session's |
| `ArcadeControls::reset` | | inferred | clears the swap (MM2 clears it on the game's reset paths, e.g. mmGame::Update's drop-through handler) |
| `SteeringFilter` | `mmInput::FilterDiscreteSteering`, `FilterGamepadSteering`, steering part of `mmPlayer::Update` | fixed | added (asm): rate DeltaOut turning further the same way, DeltaIn otherwise, output sign × \|p\|^Filter, parameters blended by f = clamp(speed, 5, 100) / 95 from mmPlayer's constructor values. Replaces RaceScreen's inferred linear ramp (3 /s out, 6 /s back) |

## src/phys/vehicle/Trailer.h, Trailer.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `TrailerGeometry` | | openmm2 | |
| `TrailerOptions::mm2StaticLoads` | `vehTrailer::Init` | deviation | open decision for the maintainer: OpenMM2's corrected static loads by default, MM2's with `simcar --mm2-trailer-loads`. The MM2 path is verified against the asm |
| `Trailer::init` | `vehTrailer::Init`, trailer part of `vehCar::Init` | fixed | asm: the trailer placed with MM2's summation order; TWHL1 / TWHL3 by CopyVars. Box mass, default 5 rad/s limit, free drivetrains with the tractor's mass, joint wiring verified. Without a bound file OpenMM2 builds a box (MM2 always has one) |
| `Trailer::setStaticLoads` | `vehTrailer::Init` | deviation | see mm2StaticLoads; MM2 path verified |
| `Trailer::reset` | `vehTrailer::Reset` | fixed | asm: placed from the tractor's InertialCS in MM2's summation order |
| `Trailer::addTo` / `removeFrom` | | openmm2 | |
| `Trailer::bottomedOut` | `vehTrailer::BottomedOut` | verified | |
| `Trailer::requiresTerrainCollision` | `vehTrailer::RequiresTerrainCollision` | fixed | added (asm) |
| `Trailer::setCarHitchOffset` / `setTrailerHitchOffset` | `vehTrailer::SetCarHitchOffset` / `SetTrailerHitchOffset` | verified | |
| `Trailer::modelMatrix` | `vehTrailerInstance::GetMatrix` | verified | |
| `Trailer::hitchGap` / `hitchAngle` | | openmm2 | diagnostics |
| `Trailer::beforeIntegrate` | `vehTrailer::Update` (inputs) | verified | back wheels only, SSS steering against, handbrake split by the SSS steering |
| `Trailer::afterIntegrate` | `vehTrailer::Update`, `dgTrailerJoint::Update` | fixed | drivetrains then the joint (verified). The joint's Ctrl+B test was missing: while `Trailer::breakKeyPressed` is set (RaceScreen sets it for the frame B goes down with either Ctrl held, as ioKeyboard reports it), a holding hitch breaks and the joint does nothing else that sample |

## src/phys/vehicle/VehicleBody.h

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `VehicleBody::position` | `vehCarModel::GetPosition`, `vehTrailerInstance::GetPosition` | fixed | added (asm): a car's centre is the ICS position plus its up axis (vehCarSim +0x90 + +0x78), a trailer's the ICS position (vehTrailer +0x288). dgPhysManager's sphere tests and vehCar::Update's room tracking use it; the body used the model origin |
| `VehicleBody::radius` | `lvlInstance::GetRadius` | fixed | added: the geometry set's radius (see `geomSetRadius`), which vehCarModel::InitBound and vehTrailer::Init do not raise; the body used the bound's sphere. Without a model (tests) the bound's sphere stays |

## src/game/PlayerVehicle.h, PlayerVehicle.cpp

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `readDat` | | openmm2 | |
| `geomSetRadius` | `lvlInstance::GetGeomSet`'s radius, `modGetStatic` | fixed | added (asm): the largest (x*x + y*y) + z*z over the vertices of the part's levels of detail, then its square root; "body" for the car (vehCarModel::Init's BeginGeom) and "trailer" for its trailer (vehTrailerInstance::Init). Replaces RaceScreen's copy for aiVehiclePlayer |
| `readPivot` | `GetPivot` | fixed | added: the 12 floats of the .mtx as a matrix |
| `readSimPivots` | the pivots `vehCarSim::Init` reads | fixed | wheels via vehWheel::Init's formula (absolute radius) and the engine and axle pivots, which were never loaded (no retail car has them) |
| `SimVehicle::loadPlayer` | `mmPlayer::Init` | fixed | added: the player's vpcop runs vehCarSim::Init again with "vpmustang99" (unless `-tune_car`): Mustang tune and pivots, vpcop body, bound, damage, gyro, stuck and splash box; no trailer in multiplayer cruise and Cops and Robbers (RaceScreen passes the rule) |
| `SimVehicle::load` | `vehCar::Init`, `vehCarModel::InitBound`, `vehTrailer::Init` | deviation | tunes, bound and trailer verified. `tuneSuffix` is an OpenMM2 option no caller uses (MM2 never loads a variant); the trailer needs both tune files (MM2 builds it from the hitch alone, with defaults) |
| `SimVehicle::addTo` / `removeFrom` | | openmm2 | |
| `SimVehicle::reset` | `vehCar::Reset`, `vehTrailer::Reset` | inferred | also clears the pedal swap (see ArcadeControls::reset) |
| `SimVehicle::trailerPose` | `vehTrailerInstance::Draw` | verified | tail lights above 0.1 brake; TWHL0-3 only |
| `SimVehicle::hold` | `vehCar::SetDrivable(0, 1)`, `vehCar::PreUpdate` | fixed | brake on and neutral every frame, throttle (free revving), steering and handbrake the player's; OpenMM2 held the car in drive with throttle 0 and the handbrake. RaceScreen passes the pedals |
| `SimVehicle::drive` | `vehCar::SetDrivable(1, ...)`, `mmGame::UpdateSteeringBrakes` | fixed | releasing the hold selects first gear (SetForward) |
| `SimVehicle::reversing` | gear 0 (`vehCarModel::DrawGlow`'s reverse light) | verified | |
| `SimVehicle::pose` | `vehCarModel::Draw`, `DrawGlow` | fixed | a WHL4 / WHL5 mesh is drawn at the WHL2 / WHL3 matrix moved back by (0.2 + 2) × that wheel's radius along the car's Z axis, not at its pivot (mm2hook's improvement); brake light (brake != 0) verified |

## src/ai/PlayerCar.h

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `PlayerCar` fields | `aiVehiclePlayer` | fixed | Left/RSideDistance and Front/BackBumperDistance are half of vehCarSim's Size (its InertiaBox), not of the bound's box: RaceScreen now fills width and length from the InertiaBox. Matrix and position are the InertialCS's (verified); the radius is the car body's `lvlInstance::GetRadius` |
| `PlayerCar::speed` | `aiVehiclePlayer::Speed` | fixed | vehCarSim's forward speed, summed z, y, x |

## Missing

| MM2 | What it does | Status |
| --- | --- | --- |
| `vehCarSim::RecordReplay` / `PlaybackReplay` | quantises throttle (1/182.1), brake and handbrake (1/255), steering (1/127) and gear into bytes per frame | open: OpenMM2 has no replays |
| `vehCarSim::SetHackedImpactParams` | friction 2, no bounce, brake on | not needed: nothing calls it |
| `vehCarSim::ReconfigureDrivetrain` / `UnconfigureDrivetrain` | rewire the drivetrains at run time | not needed: only vehCarSim::Init uses them |
| `vehSuspension` (shock, arm and shaft parts) | visual suspension parts posed from the wheels | open for rendering-fx |
| `vehCar::SetDrivable` modes 2 and 3, `vehCar::PreUpdate` | hold an AI car (brake 1, throttle, steering, handbrake 0) | open for ai-vehicles (`CarSim::drivable` is there for them) |
| `vehCar::UpdateTrack`, `DrawTracks`, `vehCarDamage::SpewSmoke`, `EjectOneshot`, texel damage, `vehCarModel` drawing | tyre tracks, smoke, parts and dents | rendering-fx |
| `vehWheel::GetSurfaceSound` | the wheel material's sound id | audio |
| `mmPlayer::UpdateHOG` | sets an upside-down slow car upright after a second | verified no-op: its "collided" flag (vehCarSim +0x1540) is only ever cleared in build 3393, so it never fires |
| `mmPlayer::FilterSteering` | mouse and steering-wheel filters with their own speed-blended sensitivity | open: OpenMM2 has keyboard and gamepad only |
| `mmPlayer::UpdateFF`, `FFImpactCallback`, `ResetFF` | force feedback | open (session records it); the vehicle data it reads (front-left wheel friction, bump height and width, radius, slip percents, speed) are all on `Wheel` / `CarSim` |
| `mmPlayer::EnableRegen` / `UpdateRegen` call | Cops and Robbers regeneration | open for session: call `CarSim::regenerate` once a frame while regeneration is on |
| `dgPhysManager::CollideTerrain`'s `RequiresTerrainCollision` | skip the body's terrain collision for a car upright on its wheels | open for phys-core: `CarSim` / `Trailer::requiresTerrainCollision` exist |
| `mmNetObject::Init`'s trailer flag | whether a network car tows its trailer | open for session |

## Second pass

After the merge of every area into integration (phys-core's wheel probe,
`age::rotate` as MakeRotate + Dot3x3, InertialCS's spin limit always on):

- The merged wheel probe wiring matches vehWheel::ComputeDwtdw (see
  `Wheel::computeDwtdw`): no change needed.
- Spin limits: vehCarSim::Init writes 4 pi (12.566371) to all three
  MaxAngVelocity axes; vehTrailer::Init leaves phInertialCS's default
  5 rad/s. CarSim and Trailer no longer set the redundant switch, and no
  longer write the InertialCS impact fields nothing reads.
- The trailer's Ctrl+B hitch break is wired (`Trailer::afterIntegrate`).
- Cars and trailers have MM2's instance sphere (`VehicleBody`).
- `mm2tool simcar` drives on the built-in "default" material (friction 1),
  not materials.mtl's "_default" (0.9); docs/physics.md's table is rerun.
- simcar / simcars on the merged physics: acceleration and top speeds
  within 0.15 s and 0.2 mph of the pre-merge table (which also drove on
  friction 0.9), and no car sinks: the
  body height holds at speed, apart from vppanozgt's aero downforce, which
  lowers it 5.6 cm at 275 mph. With the wheel centred, cars whose pivots
  are not mirrored curve slowly to the left. vpbug turns 3 degrees a
  minute (right wheels 3.9 cm further out). The vpsemi rig turns about
  40 degrees a minute (hitch 7.4 cm left of centre; the tractor alone
  holds within 0.7 degrees). The pre-audit build does the same; that MM2
  does is inferred from the data, not observed.
