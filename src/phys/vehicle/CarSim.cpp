// vehCarSim (Init, ConfigureDrivetrain, Reset, Update, SetWorldMatrix,
// GetSSSFactor, OnGround) and the per-sample parts of vehCar::Update (gyro,
// stuck, damage) and vehAxle from Midtown Madness 2, verified against the
// build 3393 code (MM2Recomp). See docs/physics.md.

#include "phys/vehicle/CarSim.h"

#include "phys/AgeMath.h"

#include <algorithm>
#include <cmath>

namespace mm2::phys {
namespace {

// dgPhysEntity::Update: vehicles fall at 19.6 m/s^2.
constexpr float kVehicleGravity = 19.6f;
// vehCarSim::Init: the body's angular velocity is limited to 4 pi per axis.
constexpr float kMaxAngVelocity = 12.566371f;

} // namespace

void CarDamage::impact(float value, float speedMph, bool otherIsVehicle) {
    if (!enabled)
        return;
    if (params.impactThreshold < value && (10.0f <= speedMph || otherIsVehicle))
        currentDamage = std::max(0.0f, currentDamage + value);
}

void CarDamage::update(float dt) {
    currentDamage = currentDamage - dt * params.regenerateRate;
    if (currentDamage < 0.0f)
        currentDamage = 0.0f;
    const float med = medScaled(), max = maxScaled();
    float f = (currentDamage - med) / (max - med);
    damage = f < 0.0f ? 0.0f : (1.0f < f ? 1.0f : f);
}

void CarSim::init(const CarSimParams& p, const VehicleGeometry& g, const Options& o) {
    params = p;
    options = o;
    centerOfGravity = p.centerOfGravity;
    damage.enabled = o.damage;

    body = Body{};
    body.controller = this;
    InertialCS& ics = body.ics;
    ics.setMass(p.inertiaBox.x, p.inertiaBox.y, p.inertiaBox.z, p.mass);
    ics.gravity = {0.0f, -kVehicleGravity, 0.0f};
    ics.limitAngVelocity = true;
    ics.setMaxAngVelocity(kMaxAngVelocity);
    ics.state = InertialCS::Off;
    ics.elasticity = p.boundElasticity;
    ics.friction = p.boundFriction;

    // Collision box from the body bound, relative to the centre of mass
    // (which sits at -CenterOfGravity in model space).
    Aabb box = g.body;
    if (!box.valid()) {
        for (const auto& w : g.wheels) {
            box.expand(w.center + Vec3{w.radius, w.radius, w.radius});
            box.expand(w.center - Vec3{w.radius, 0.0f, w.radius});
        }
        box.max.y += 0.8f;
    }
    body.shape.kind = Shape::Kind::Box;
    body.shape.offset = box.center() + centerOfGravity;
    body.shape.half = box.extent();

    // Wheels (vehWheel::Init; the right wheels copy the left ones' tune).
    for (std::size_t i = 0; i < 4; ++i)
        wheels[i].init(i < 2 ? p.wheelFront : p.wheelBack, g.wheels[i], p.mass, true, centerOfGravity.z);

    engine.configure(p.engine);
    engine.pivot = g.enginePivot;
    trans.configure(p.trans);

    drivetrains = {};
    drivetrains[0].configure(p.freetrain);
    drivetrains[1].configure(p.freetrain);
    drivetrains[2].configure(p.drivetrain);
    drivetrains[2].attach(&engine, &trans);

    // vehCarSim::ConfigureDrivetrain.
    Wheel& fl = wheels[0];
    Wheel& fr = wheels[1];
    Wheel& bl = wheels[2];
    Wheel& br = wheels[3];
    const int type = std::clamp(p.drivetrainType, 0, 2);
    switch (type) {
    case 0: // rear-wheel drive
        drivetrains[0].addWheel(&fl);
        drivetrains[1].addWheel(&fr);
        drivetrains[2].addWheel(&bl);
        drivetrains[2].addWheel(&br);
        m_drivetrainOrder = {0, 1, 2};
        m_numDrivetrains = 3;
        break;
    case 1: // front-wheel drive
        drivetrains[0].addWheel(&bl);
        drivetrains[1].addWheel(&br);
        drivetrains[2].addWheel(&fl);
        drivetrains[2].addWheel(&fr);
        m_drivetrainOrder = {0, 1, 2};
        m_numDrivetrains = 3;
        break;
    default: // all-wheel drive
        for (Wheel* w : {&fl, &fr, &bl, &br})
            drivetrains[2].addWheel(w);
        m_drivetrainOrder = {2, 0, 0};
        m_numDrivetrains = 1;
        break;
    }

    // vehCarSim::Init: ComputeConstants of the engine, transmission and axles.
    engine.computeConstants();
    trans.computeConstants(engine, primary().wheel(0)->radius);
    for (std::size_t a = 0; a < 2; ++a) {
        Axle& axle = axles[a];
        axle.params = a == 0 ? p.axleFront : p.axleBack;
        axle.stiffness = axle.params.torqueCoef * ics.inertia.z;
        const float d = std::sqrt(axle.stiffness * ics.inertia.z) * axle.params.dampCoef;
        axle.damping = d + d;
        axle.matrix = g.axlePivots[a].value_or(Mat34::identity());
        axle.rollFactor = 1.0f;
        if (g.axlePivots[a]) {
            const float across = (wheels[a * 2].center - axle.matrix.m3).dot(axle.matrix.m0);
            if (across != 0.0f)
                axle.rollFactor = 1.0f / across;
        }
    }
    aero.configure(p.aero);
    reset(Mat34::identity());
}

void CarSim::reset(const Mat34& model) {
    InertialCS& ics = body.ics;
    ics.zero();
    ics.matrix = model;
    ics.matrix.m3 = model.m3 - model.transformDir(centerOfGravity);

    engine.reset();
    trans.reset();
    for (Drivetrain& d : drivetrains)
        d.reset();
    const Mat34 world = modelMatrix();
    for (Wheel& w : wheels) {
        w.reset();
        w.matrix = Mat34::mul(Mat34::translation(w.center), world);
    }
    stuck.reset();
    damage.reset();
    raceFinished = false;
    steering = 0.0f;
    brakes = 0.0f;
    handBrake = 0.0f;
    m_speed = 0.0f;
    m_speedMph = 0.0f;
    // vehCarSim::RestoreImpactParams.
    ics.elasticity = params.boundElasticity;
    ics.friction = params.boundFriction;
}

void CarSim::setInputs(float throttle, float brakeInput, float steer, float handBrakeInput) {
    engine.throttle = clampf(throttle, 0.0f, 1.0f);
    brakes = clampf(brakeInput, 0.0f, 1.0f);
    steering = clampf(steer, -1.0f, 1.0f);
    handBrake = clampf(handBrakeInput, 0.0f, 1.0f);
}

Mat34 CarSim::modelMatrix() const {
    // vehCarSim::SetWorldMatrix.
    Mat34 m = body.ics.matrix;
    m.m3 = m.m3 + m.transformDir(centerOfGravity);
    return m;
}

Mat34 CarSim::wheelMatrix(int i) const {
    return wheels[static_cast<std::size_t>(i)].matrix;
}

int CarSim::wheelsOnGround() const {
    return static_cast<int>(std::ranges::count_if(wheels, [](const Wheel& w) { return w.onGround; }));
}

float CarSim::sssFactor(float speed) const {
    const float threshold = params.sssThreshold;
    if (threshold == 0.0f)
        return 1.0f;
    if (speed < threshold)
        return (speed / threshold) * (params.sssValue - 1.0f) + 1.0f;
    return params.sssValue;
}

WheelEnv CarSim::makeEnv(float dt, const World& world) {
    WheelEnv env;
    env.ics = &body.ics;
    env.ground = m_ground ? m_ground : &world;
    env.dt = dt;
    env.invDt = 1.0f / dt;
    env.weatherFriction = options.weatherFriction;
    env.hasCar = true;
    env.carFrictionHandling = params.carFrictionHandling;
    env.randomSeed = world.randomSeed();
    return env;
}

void CarSim::beforeIntegrate(Body& b, float, const World&) {
    InertialCS& ics = b.ics;
    // mmPlayer::Update: after the finish the car brakes with the wheel turned
    // full left; a wrecked car gets no throttle, steering or brake; and below
    // 4 mph without throttle the handbrake holds it (the speed being the one
    // shown, from the previous sample).
    if (options.player) {
        if (raceFinished) {
            engine.throttle = 0.0f;
            brakes = 1.0f;
            steering = -1.0f;
        }
        if (damage.wrecked()) {
            engine.throttle = 0.0f;
            steering = 0.0f;
            brakes = 0.0f;
        }
        if (m_speedMph < 4.0f && engine.throttle == 0.0f)
            handBrake = 1.0f;
    }

    // vehCarSim::Update: forward speed, then the wheel inputs.
    const Vec3& v = ics.linearVelocity;
    const Vec3& z = ics.matrix.m2;
    m_speed = std::abs(z.x * v.x + z.y * v.y + z.z * v.z);
    m_speedMph = m_speed * kMetersPerSecondToMph;
    const float steer = sssFactor(m_speed) * steering;
    const float front = handBrake < 0.0f ? -handBrake : 0.0f;
    wheels[0].setInputs(steer, brakes, front);
    wheels[1].setInputs(steer, brakes, front);
    const float hand = handBrake <= 0.0f ? 0.0f : handBrake;
    // Burnout: full brake and throttle at a standstill frees the back wheels.
    float rearBrake = brakes;
    if (0.95f < brakes && 0.95f < engine.throttle && m_speed < 1.0f)
        rearBrake = 0.0f;
    // The handbrake eases off the back wheel on the inside of the turn.
    wheels[2].setInputs(-steer, rearBrake, (steering <= 0.0f ? 1.0f : 1.0f - steering) * hand);
    wheels[3].setInputs(-steer, rearBrake, (0.0f <= steering ? 1.0f : steering + 1.0f) * hand);
}

void CarSim::afterIntegrate(Body& b, float dt, const World& world) {
    InertialCS& ics = b.ics;
    WheelEnv env = makeEnv(dt, world);
    const Mat34 frame = modelMatrix();
    env.frame = &frame;

    // vehCarSim's children, in order.
    const int type = std::clamp(params.drivetrainType, 0, 2);
    engine.update(dt, trans, primary(), ics, type);
    trans.update(dt, engine, wheelsOnGround());
    aero.update(ics, m_speed, dt, env.invDt);
    for (int i = 0; i < m_numDrivetrains; ++i)
        drivetrains[static_cast<std::size_t>(m_drivetrainOrder[static_cast<std::size_t>(i)])].update(env,
                                                                                                     params.mass);
    updateAxles();

    // vehCar::Update: vehGyro, vehStuck, vehCarDamage.
    if (options.gyro) {
        Gyro::Inputs gi;
        gi.steering = steering;
        gi.sssFactor = sssFactor(m_speed);
        gi.drivetrainSpeed = primary().rotationSpeed;
        gi.brake = brakes;
        gi.handbrake = handBrake;
        gi.wheelsOnGround = wheelsOnGround();
        gyro.update(ics, gi);
    }
    Stuck::Inputs si;
    si.throttle = engine.throttle;
    si.maxThrottle = engine.maxThrottle;
    si.steering = steering;
    si.gear = trans.currentGear;
    si.wheelsOnGround = wheelsOnGround();
    if (stuck.update(ics, dt, si)) {
        engine.throttle = 0.0f;
        brakes = 1.0f;
    }
    damage.update(dt);
}

void CarSim::updateAxles() {
    // vehAxle::Update: anti-roll torque about the body's length, and the
    // wheels rolled with the axle (or by CamberLimit when both set one).
    InertialCS& ics = body.ics;
    const Mat34 world = modelMatrix();
    for (std::size_t a = 0; a < 2; ++a) {
        Axle& axle = axles[a];
        Wheel& l = wheels[a * 2];
        Wheel& r = wheels[a * 2 + 1];
        const float dl = l.suspension - l.visualDispVert();
        const float dr = r.suspension - r.visualDispVert();
        const float diff = dl - dr;
        axle.roll = diff * axle.rollFactor * 0.5f;
        if (options.axleCoupling && axle.stiffness != 0.0f) {
            const float t =
                -(diff * axle.stiffness + (l.suspensionVelocity - r.suspensionVelocity) * axle.damping);
            ics.applyTorque({t * ics.matrix.m2.x, t * ics.matrix.m2.y, t * ics.matrix.m2.z});
        }
        const Mat34 axleWorld = Mat34::mul(axle.matrix, world);
        const bool camber = 0.0f <= l.params.camberLimit && 0.0f <= r.params.camberLimit;
        for (Wheel* w : {&l, &r}) {
            const float angle = camber ? w->camber : axle.roll;
            if (angle != 0.0f) {
                const Mat34 rot = age::arbitraryRotation(axleWorld.m2, angle);
                w->matrix.m0 = rot.transformDir(w->matrix.m0);
                w->matrix.m1 = rot.transformDir(w->matrix.m1);
                w->matrix.m2 = rot.transformDir(w->matrix.m2);
            }
        }
    }
}

void CarSim::onImpact(Body& b, const Impact& impact) {
    // vehCarDamage::Impact: the stuck watcher, then the damage.
    stuck.impact(b.ics);
    float share = 1.0f;
    if (impact.other)
        share = impact.other->ics.mass / (b.ics.mass + impact.other->ics.mass);
    damage.impact(std::abs(impact.impulse) * share, m_speedMph, impact.other != nullptr);
    if (onImpactCallback)
        onImpactCallback(impact);
}

} // namespace mm2::phys
