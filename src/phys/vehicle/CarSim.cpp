// Port of mmCarSim (Init, ConfigureDrivetrain, Update, Reset) from Open1560
// (https://github.com/0x1F9F1/Open1560), GPL-3.0: code/midtown/game.asm,
// Midtown Madness 1 beta build 1560, adapted to MM2's tune data.

#include "phys/vehicle/CarSim.h"

#include <algorithm>
#include <cmath>

namespace mm2::phys {

void CarDamage::impact(float impulse, float closingSpeed) {
    if (!enabled)
        return;
    // mmCar::Impact: "if (deflection > 4) CurrentDamage += energy".
    if (closingSpeed > 4.0f && impulse >= params.impactThreshold)
        currentDamage += impulse;
}

void CarDamage::update(float dt) {
    if (params.regenerateRate > 0.0f) // inferred
        currentDamage = std::max(0.0f, currentDamage - params.regenerateRate * dt);
    // mmCarSim::UpdateDamage.
    const float med = medScaled(), max = maxScaled();
    const float f = max > med ? (currentDamage - med) / (max - med) : 0.0f;
    damage = f <= 0.0f ? 0.0f : (f < 1.0f ? f : 1.0f);
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
    ics.gravity = {0.0f, -kGravity, 0.0f};
    ics.limitAngVelocity = true;
    ics.maxAngVelocity = kCarMaxAngVelocity;
    ics.state = InertialCS::Off;
    ics.elasticity = p.boundElasticity;
    ics.friction = p.boundFriction;

    // Collision box from the body bound, relative to the CG.
    Aabb box = g.body;
    if (!box.valid()) {
        for (const auto& w : g.wheels) {
            box.expand(w.center + Vec3{w.radius, w.radius, w.radius});
            box.expand(w.center - Vec3{w.radius, 0.0f, w.radius});
        }
        box.max.y += 0.8f;
    }
    body.shape.kind = Shape::Kind::Box;
    body.shape.offset = box.center() - centerOfGravity;
    body.shape.half = box.extent();

    engine.configure(p.engine);
    const int type = std::clamp(p.drivetrainType, 0, 2);
    const WheelGeometry& driven = type == 1 ? g.wheels[0] : g.wheels[2];
    trans.configure(p.trans, p.engine, driven.radius);

    // mmCarSim::Init: front wheels flag 0, back wheels flag 2, NumWheels 4.
    for (int i = 0; i < 4; ++i) {
        const bool front = i < 2;
        const WheelGeometry& wg = g.wheels[static_cast<std::size_t>(i)];
        wheels[static_cast<std::size_t>(i)].init(front ? p.wheelFront : p.wheelBack, wg,
                                                 wg.center - centerOfGravity, p.mass, kGravity, 4,
                                                 front ? 0 : Wheel::kHandbrake);
    }

    drivetrains = {};
    drivetrains[0].configure(p.freetrain);
    drivetrains[1].configure(p.freetrain);
    drivetrains[2].configure(p.drivetrain);
    drivetrains[2].attach(&engine, &trans);

    // mmCarSim::ConfigureDrivetrain.
    Wheel& fl = wheels[0];
    Wheel& fr = wheels[1];
    Wheel& bl = wheels[2];
    Wheel& br = wheels[3];
    switch (type) {
    case 0: // rear-wheel drive
        drivetrains[0].addWheel(&fl);
        drivetrains[1].addWheel(&fr);
        drivetrains[2].addWheel(&bl);
        drivetrains[2].addWheel(&br);
        bl.flags |= Wheel::kUseLinearVelocity;
        br.flags |= Wheel::kUseLinearVelocity;
        m_drivetrainOrder = {0, 1, 2};
        m_numDrivetrains = 3;
        break;
    case 1: // front-wheel drive
        drivetrains[0].addWheel(&bl);
        drivetrains[1].addWheel(&br);
        drivetrains[2].addWheel(&fl);
        drivetrains[2].addWheel(&fr);
        fl.flags |= Wheel::kUseLinearVelocity;
        fr.flags |= Wheel::kUseLinearVelocity;
        m_drivetrainOrder = {0, 1, 2};
        m_numDrivetrains = 3;
        break;
    default: // all-wheel drive
        for (Wheel* w : {&fl, &fr, &bl, &br}) {
            drivetrains[2].addWheel(w);
            w->flags |= Wheel::kUseLinearVelocity;
        }
        m_drivetrainOrder = {2, 0, 0};
        m_numDrivetrains = 1;
        break;
    }

    aero.configure(p.aero);
    axles[0].params = p.axleFront;
    axles[1].params = p.axleBack;
    reset(Mat34::identity());
}

void CarSim::reset(const Mat34& model) {
    Mat34 m = model;
    // mmCarSim::SetResetPos: rest the front-right wheel on the given height.
    const Wheel& fr = wheels[1];
    m.m3.y += fr.radius - (fr.center.y + centerOfGravity.y);

    InertialCS& ics = body.ics;
    ics.zero();
    ics.matrix = m;
    ics.matrix.m3 = m.transform(centerOfGravity);
    ics.frameVelocity = {};

    engine.reset();
    trans.reset();
    for (Wheel& w : wheels)
        w.reset();
    stuck.reset();
    damage.reset();
    steering = 0.0f;
    brakes = 0.0f;
    handBrake = 0.0f;
    m_speed = 0.0f;
    m_speedMph = 0.0f;
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
    Mat34 m = body.ics.matrix;
    m.m3 = m.m3 - m.transformDir(centerOfGravity);
    return m;
}

Mat34 CarSim::wheelMatrix(int i) const {
    return Mat34::mul(wheels[static_cast<std::size_t>(i)].visualMatrix, body.ics.matrix);
}

bool CarSim::onGround() const {
    return std::ranges::any_of(wheels, [](const Wheel& w) { return w.onGround; });
}

WheelEnv CarSim::makeEnv(float dt, const World& world) const {
    WheelEnv env;
    env.ics = &body.ics;
    env.frame = &body.ics.matrix;
    env.ground = m_ground ? m_ground : &world;
    env.dt = dt;
    env.invDt = 1.0f / dt;
    env.weatherFriction = options.weatherFriction;
    env.indoors = options.indoors;
    env.carFrictionHandling = params.carFrictionHandling;
    env.realism = options.realism;
    return env;
}

void CarSim::beforeIntegrate(Body& b, float dt, const World&) {
    InertialCS& ics = b.ics;
    const Vec3& fv = ics.frameVelocity;
    m_speed = std::sqrt((fv.y * fv.y + fv.z * fv.z) + fv.x * fv.x);
    m_speedMph = m_speed * kMetersPerSecondToMph;

    // Automatic "park": hold the car with half brake when stopped.
    if (trans.isAutomatic && engine.throttle == 0.0f && m_speed < 0.5f && !(brakes > 0.5f)) {
        brakes = 0.5f;
        trans.inPark = true;
    } else {
        trans.inPark = false;
    }
    if (!trans.isAutomatic && trans.getCurrentGear() == 0)
        brakes = 0.05f <= brakes ? brakes : 0.05f;

    // Speed-sensitive steering (MM2 SSSValue/SSSThreshold; inferred).
    float steer = steering;
    if (params.sssThreshold > 0.0f && m_speedMph > params.sssThreshold)
        steer *= std::max(params.sssValue, params.sssThreshold / m_speedMph);

    Wheel& fl = wheels[0];
    Wheel& fr = wheels[1];
    Wheel& bl = wheels[2];
    Wheel& br = wheels[3];
    fl.setInputs(steer, fl.brakeRatio * brakes);
    fr.setInputs(steer, fr.brakeRatio * brakes);
    bl.setInputs(-steer, bl.brakeRatio * brakes);
    br.setInputs(-steer, br.brakeRatio * brakes);
    if (handBrake > 0.0f) {
        bl.setInputs(-steer, handBrake);
        br.setInputs(-steer, handBrake);
    }

    damage.update(dt);
    if (ics.matrix.m1.y < 0.0f || (options.damage && damage.wrecked())) {
        // mmCarSim::SetHackedImpactParams.
        ics.elasticity = 0.0f;
        ics.friction = 2.0f;
        brakes = 1.0f;
    } else {
        // mmCarSim::RestoreImpactParams.
        ics.elasticity = params.boundElasticity;
        ics.friction = params.boundFriction;
    }

    engine.update(dt, trans, drivetrains[2]);
    trans.update(dt, engine);
}

void CarSim::afterIntegrate(Body& b, float dt, const World& world) {
    InertialCS& ics = b.ics;
    const WheelEnv env = makeEnv(dt, world);
    updateAxles();
    if (options.gyro)
        gyro.update(ics, dt, steering, handBrake, engine.throttle, wheels[2].latSlipPercent, onGround());
    aero.update(ics);
    for (int i = 0; i < m_numDrivetrains; ++i)
        drivetrains[static_cast<std::size_t>(m_drivetrainOrder[static_cast<std::size_t>(i)])].update(
            ics, env, brakes, handBrake);
    if (options.axleCoupling)
        applyAxleCoupling();
    stuck.update(ics, dt, engine, trans, steering);
}

void CarSim::onImpact(Body&, const Impact& impact) {
    stuck.impact();
    damage.impact(impact.impulse, impact.speed);
    if (onImpactCallback)
        onImpactCallback(impact);
}

void CarSim::updateAxles() {
    // mmAxle::Update (MM1 C++ in Open1560).
    for (int a = 0; a < 2; ++a) {
        const Wheel& l = wheels[static_cast<std::size_t>(a * 2)];
        const Wheel& r = wheels[static_cast<std::size_t>(a * 2 + 1)];
        Axle& axle = axles[static_cast<std::size_t>(a)];
        const float s = l.center.x != 0.0f ? (l.visualMatrix.m3.y - r.visualMatrix.m3.y) / l.center.x : 0.0f;
        axle.matrix = Mat34::rotationZ(std::asin(clampf(s, -1.0f, 1.0f)));
        axle.matrix.m3 = (l.visualMatrix.m3 + r.visualMatrix.m3) * 0.5f;
    }
}

void CarSim::applyAxleCoupling() {
    // MM2 AxleFront/AxleBack TorqueCoef/DampCoef (inferred): a solid axle /
    // anti-roll coupling that resists the difference in suspension travel.
    InertialCS& ics = body.ics;
    for (int a = 0; a < 2; ++a) {
        Wheel& l = wheels[static_cast<std::size_t>(a * 2)];
        Wheel& r = wheels[static_cast<std::size_t>(a * 2 + 1)];
        const AxleParams& p = axles[static_cast<std::size_t>(a)].params;
        if ((p.torqueCoef == 0.0f && p.dampCoef == 0.0f) || !l.hit || !r.hit)
            continue;
        const float dF = 0.5f * p.torqueCoef * l.spring * (l.suspension - r.suspension) +
                         0.5f * p.dampCoef * l.damping * (l.suspensionVelocity - r.suspensionVelocity);
        ics.applyForce(ics.matrix.m1 * dF, l.intersection.position);
        ics.applyForce(ics.matrix.m1 * -dF, r.intersection.position);
    }
}

} // namespace mm2::phys
