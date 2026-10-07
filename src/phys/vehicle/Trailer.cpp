// Port of mmTrailer (Init, Reset, Update, RestoreImpactParams,
// SetHackedImpactParams) and of the trailer setup in mmCar::Init from
// Open1560 (https://github.com/0x1F9F1/Open1560), GPL-3.0:
// code/midtown/game.asm, Midtown Madness 1 beta build 1560. The mapping of
// MM2's vehTrailer / dgTrailerJoint data onto it is described in
// docs/physics.md; parts marked "inferred" have no MM1 counterpart.

#include "phys/vehicle/Trailer.h"

#include "phys/vehicle/CarSim.h"

namespace mm2::phys {

void Trailer::init(const TrailerParams& p, const TrailerJointParams& j, const TrailerGeometry& g,
                   CarSim& tractor) {
    params = p;
    jointParams = j;
    // The hitch points live in one of two files: dgTrailerJoint Offset0/1
    // (vpcentury) or vehTrailer CarHitchOffset/TrailerHitchOffset (vpsemi,
    // whose joint file has no offsets). Use whichever is present; the joint
    // file wins if both are (no retail car has both, so the order is
    // inferred).
    if (!j.hasOffsets && p.carHitchOffset && p.trailerHitchOffset) {
        jointParams.offset0 = *p.carHitchOffset;
        jointParams.offset1 = *p.trailerHitchOffset;
        jointParams.hasOffsets = true;
    }
    centerOfGravity = g.centerOfGravity;
    m_tractor = &tractor;

    body = Body{};
    body.controller = this;
    InertialCS& ics = body.ics;
    // mmTrailer::Init: SetMass from the trailer's box (MM2: vehTrailer
    // InertiaBox and Mass), gravity from the physics manager, angular
    // velocity limited (MaxAngVelocity keeps the asInertialCS default).
    ics.setMass(p.inertiaBox.x, p.inertiaBox.y, p.inertiaBox.z, p.mass);
    ics.gravity = {0.0f, -kGravity, 0.0f};
    ics.limitAngVelocity = true;
    ics.state = InertialCS::Off;
    // MM1 reads BoundElasticity/BoundFriction from the trailer's own file;
    // vehTrailer has no such fields, so the tractor's are used (inferred).
    boundElasticity = tractor.params.boundElasticity;
    boundFriction = tractor.params.boundFriction;

    Aabb box = g.body;
    if (!box.valid()) {
        for (const auto& w : g.wheels) {
            box.expand(w.center + Vec3{w.radius, w.radius, w.radius});
            box.expand(w.center - Vec3{w.radius, 0.0f, w.radius});
        }
        box.max.y += 1.5f;
    }
    body.shape.kind = Shape::Kind::Box;
    body.shape.offset = box.center() - centerOfGravity;
    body.shape.half = box.extent();

    // Wheels: mmWheel::Init(..., Center, &ICS, 4, nullptr, flags) with flags
    // 1 (front) and 3 (back, handbrake), each in its own free drivetrain
    // (mmDrivetrain::Init with the tractor's mmCarSim).
    DrivetrainParams free;
    if (p.drivetrain) {
        free = *p.drivetrain;
    } else {
        // MM1's free drivetrain: dyn_coeff = 2 * tractor mass (see
        // Drivetrain::configure for the AngInertia mapping).
        free.angInertia = tractor.body.ics.mass * 2.0f;
    }
    for (int i = 0; i < 4; ++i) {
        const bool front = i < 2;
        const auto ii = static_cast<std::size_t>(i);
        const WheelGeometry& wg = g.wheels[ii];
        wheels[ii].init(front ? p.wheelFront : p.wheelBack, wg, wg.center - centerOfGravity, p.mass, kGravity,
                        4, front ? Wheel::kUseIcsWorld : Wheel::kUseIcsWorld | Wheel::kHandbrake);
        drivetrains[ii] = Drivetrain{};
        drivetrains[ii].configure(free);
        drivetrains[ii].mm1ExplicitSpin = tractor.options.mm1ExplicitSpin;
        drivetrains[ii].addWheel(&wheels[ii]);
    }

    // mmCar::Init: Joint3Dof::Init, InitJoint3Dof(&car ICS, car offset,
    // &trailer ICS, trailer offset) with offsets relative to each body's
    // centre, SetRestOrientMat(O, O) with O = rows (1,0,0) (0,0,-1) (0,1,0),
    // then the limits and frictions. MM1 hard-codes them; MM2 reads them from
    // dgTrailerJoint (names match, see docs/physics.md).
    // InitJoint3Dof's SetPosition(tractor CG) moves the tractor so that its
    // hitch lands on its old CG; MM1 relies on the following mmCar::Reset to
    // place it again. Keep the tractor where it is and place the trailer
    // behind it (reset() below) instead.
    const Mat34 tractorMatrix = tractor.body.ics.matrix;
    joint.init();
    joint.initJoint3Dof(&tractor.body.ics, jointParams.offset0 - tractor.centerOfGravity, &ics,
                        jointParams.offset1 - centerOfGravity);
    tractor.body.ics.matrix = tractorMatrix;
    Mat34 rest;
    rest.m0 = {1.0f, 0.0f, 0.0f};
    rest.m1 = {0.0f, 0.0f, -1.0f};
    rest.m2 = {0.0f, 1.0f, 0.0f};
    rest.m3 = {};
    joint.setRestOrientMat(rest, rest);
    joint.setRollLimit(j.negativeRollLimit.value_or(-3.1415927f), j.positiveRollLimit.value_or(3.1415927f),
                       j.limitElasticityRoll);
    joint.setLeanLimit(j.leanLimit, j.limitElasticityLean);
    joint.setFrictionLean(j.restoreForceLean, j.dampConstLean, j.dampLinearLean);
    joint.setFrictionRoll(j.restoreForceRoll, j.dampConstRoll, j.dampLinearRoll);
    // ForceLimit: vpsemi's 40 would break the joint at once if it were in
    // newtons, so its MM2 unit is unknown; joints stay unbreakable (inferred).
    joint.forceLimit = 0.0f;
    // FreeLean/FreeRoll (vpsemi): free play before the restoring springs act
    // (inferred from the field names and MM2's CosFreeLean member).
    joint.freeLean = j.freeLean;
    joint.freeRoll = j.freeRoll;
    reset();
}

void Trailer::reset() {
    // mmTrailer::Reset: ICS reset, placed at the tractor's reset pose, wheels
    // reset, RestoreImpactParams. MM1's trailer model shares the tractor's
    // origin; MM2's has its own, so the hitch points are made to coincide.
    const Mat34 tractorModel = m_tractor->modelMatrix();
    Mat34 m = tractorModel;
    m.m3 = tractorModel.transform(jointParams.offset0) - tractorModel.transformDir(jointParams.offset1);
    InertialCS& ics = body.ics;
    ics.zero();
    ics.matrix = m;
    ics.matrix.m3 = m.transform(centerOfGravity);
    ics.frameVelocity = {};
    for (Wheel& w : wheels)
        w.reset();
    restoreImpactParams();
    // OpenMM2: refresh the joint position too (MM1 leaves it stale until the
    // next Joint3Dof::Update, which computes its first constraint at the old
    // point).
    if (joint.ics1)
        joint.setPosition(tractorModel.transform(jointParams.offset0));
}

void Trailer::restoreImpactParams() {
    body.ics.elasticity = boundElasticity;
    body.ics.friction = boundFriction;
    wheels[0].brakingInput = 0.0f;
}

void Trailer::setHackedImpactParams() {
    body.ics.elasticity = 0.0f;
    body.ics.friction = 2.0f;
    wheels[0].brakingInput = 0.5f;
}

void Trailer::addTo(World& world) {
    world.add(&body);
    world.add(&joint);
}

void Trailer::removeFrom(World& world) {
    world.remove(&joint);
    world.remove(&body);
}

Mat34 Trailer::modelMatrix() const {
    Mat34 m = body.ics.matrix;
    m.m3 = m.m3 - m.transformDir(centerOfGravity);
    return m;
}

void Trailer::beforeIntegrate(Body&, float, const World&) {
    // mmTrailer::Update: the trailer wheels take the tractor's inputs (the
    // back ones steer opposite, vpsemi's tiller axle). CopyVars FL->FR and
    // BL->BR is implicit: init gives both wheels of an axle the same tune.
    const float steer = m_tractor->steering;
    const float brakes = m_tractor->brakes;
    wheels[0].setInputs(steer, wheels[0].brakeRatio * brakes);
    wheels[1].setInputs(steer, wheels[1].brakeRatio * brakes);
    wheels[2].setInputs(-steer, wheels[2].brakeRatio * brakes);
    wheels[3].setInputs(-steer, wheels[3].brakeRatio * brakes);
}

void Trailer::afterIntegrate(Body& b, float dt, const World& world) {
    // The ICS's children: the four drivetrains, each updating its wheel.
    WheelEnv env;
    env.ics = &b.ics;
    env.frame = &b.ics.matrix;
    env.ground = &world;
    env.dt = dt;
    env.invDt = 1.0f / dt;
    env.weatherFriction = m_tractor->options.weatherFriction;
    env.indoors = m_tractor->options.indoors;
    env.carFrictionHandling = m_tractor->params.carFrictionHandling;
    env.realism = m_tractor->options.realism;
    for (Drivetrain& d : drivetrains)
        d.update(b.ics, env, m_tractor->brakes, m_tractor->handBrake);
}

} // namespace mm2::phys
