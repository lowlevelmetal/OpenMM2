// vehTrailer from Midtown Madness 2 (Init, Reset, Update, BottomedOut,
// SetCarHitchOffset, SetTrailerHitchOffset, FileIO) and the trailer setup in
// vehCar::Init, verified against the build 3393 code (MM2Recomp). See
// docs/physics.md. Operation order and float32 arithmetic follow the
// original.

#include "phys/vehicle/Trailer.h"

#include "phys/vehicle/CarSim.h"

#include <algorithm>
#include <cmath>

namespace mm2::phys {
namespace {

// The gravity vehTrailer::Init uses for the static loads (as vehWheel's).
constexpr float kLoadGravity = -19.6f;
// phInertialCS's default per-axis angular velocity limit (vehTrailer keeps it).
constexpr float kTrailerMaxAngVelocity = 5.0f;

} // namespace

void Trailer::init(const TrailerParams& p, const TrailerJointParams& j, const TrailerGeometry& g,
                   CarSim& tractor, const TrailerOptions& options) {
    params = p;
    m_tractor = &tractor;
    // The hitches from the models' trailer_hitch pivots; the .vehTrailer
    // file's CarHitchOffset / TrailerHitchOffset replace them (vpsemi).
    carHitchOffset = p.carHitchOffset.value_or(g.carHitch.value_or(Vec3{}));
    trailerHitchOffset = p.trailerHitchOffset.value_or(g.trailerHitch.value_or(Vec3{}));
    originOffset = {carHitchOffset.x - trailerHitchOffset.x, carHitchOffset.y - trailerHitchOffset.y,
                    carHitchOffset.z - trailerHitchOffset.z};

    body.ics = InertialCS{};
    body.controller = this;
    InertialCS& ics = body.ics;
    ics.setMass(p.inertiaBox.x, p.inertiaBox.y, p.inertiaBox.z, p.mass);
    // A dgPhysEntity falls at 19.6 m/s^2.
    ics.gravity = {0.0f, kLoadGravity, 0.0f};
    ics.setMaxAngVelocity(kTrailerMaxAngVelocity);
    ics.limitAngVelocity = true;
    ics.state = InertialCS::Off;
    // vehTrailer sets no impact parameters: its bound's materials decide.
    ics.elasticity = tractor.params.boundElasticity;
    ics.friction = tractor.params.boundFriction;
    // Init places the trailer from the tractor's model matrix; reset() (as
    // vehCar::Reset does) places it from the tractor's InertialCS.
    const Mat34 model = tractor.modelMatrix();
    const Vec3& o = originOffset;
    ics.matrix = model;
    ics.matrix.m3 = {((model.m2.x * o.z + model.m1.x * o.y) + model.m0.x * o.x) + model.m3.x,
                     ((model.m2.y * o.z + model.m0.y * o.x) + model.m1.y * o.y) + model.m3.y,
                     ((model.m2.z * o.z + model.m0.z * o.x) + model.m1.z * o.y) + model.m3.z};

    // The collider's bound: the trailer instance's geometry bound
    // (bound/<car>_trailer_bound.bnd through lvlInstance::GetBound, with the
    // materials the file names), in the trailer's model space, which is its
    // InertialCS frame. Without one, a box around the wheels (OpenMM2).
    m_bound.reset();
    if (g.bound)
        m_bound = makeGeometryBound(*g.bound);
    if (!m_bound) {
        Aabb box = g.body;
        if (!box.valid()) {
            for (const auto& w : g.wheels) {
                box.expand(w.center + Vec3{w.radius, w.radius, w.radius});
                box.expand(w.center - Vec3{w.radius, 0.0f, w.radius});
            }
            box.max.y += 1.5f;
        }
        auto b = std::make_unique<BoundBox>();
        b->makeOwnMaterial();
        b->setOffset(box.center());
        b->setSize(box.max - box.min);
        m_bound = std::move(b);
    }
    body.collisionBound = m_bound.get();
    body.boundOrigin = {};
    body.resetCollider();

    // Wheels: vehWheel::Init without a vehCarSim (the body frame, a static
    // load of Mass * 19.6 / 4). The file's WheelFront / WheelBack are
    // TWHL0's and TWHL2's; TWHL1 and TWHL3 take them by vehWheel::CopyVars,
    // which keeps their own (constructor) HandbrakeCoef and WobbleLimit.
    // Each wheel has its own free drivetrain (vehDrivetrain::Init with the
    // tractor's vehCarSim, whose Mass sets the wheel inertia).
    for (std::size_t i = 0; i < 4; ++i) {
        const WheelParams wp = i == 0 ? p.wheelFront : (i == 2 ? p.wheelBack : WheelParams{});
        wheels[i].init(wp, g.wheels[i], p.mass, false, 0.0f);
        drivetrains[i] = Drivetrain{};
        drivetrains[i].configure(p.drivetrain);
        drivetrains[i].addWheel(&wheels[i]);
    }
    wheels[1].copyVars(wheels[0]);
    wheels[3].copyVars(wheels[2]);

    // The joint at the two hitches, attached to both bodies' colliders.
    joint.init(j, &tractor.body.ics, &ics, carHitchOffset, trailerHitchOffset);
    body.joint = &joint;
    tractor.body.joint = &joint;

    joint.mm2ForceRotation = options.mm2ForceRotation;
    setStaticLoads(options.mm2StaticLoads);
    reset();
}

void Trailer::setStaticLoads(bool mm2) {
    // Only when both trailer axles lie on the same side of the trailer's
    // origin, i.e. for a semi-trailer resting on the hitch (vpcentury;
    // vpsemi's tiller axle is in front of the origin, and its wheels keep
    // Mass * 19.6 / 4 each).
    const float z0 = wheels[0].center.z;
    const float z2 = wheels[2].center.z;
    if (std::signbit(z0) != std::signbit(z2))
        return;
    // With the centre of mass at the origin, the axle (at zm, the mean of
    // TWHL0's and TWHL2's z) carries W |hz| / L and the hitch (at hz)
    // W |zm| / L, L = |hz - zm|.
    const float weight = -(kLoadGravity * params.mass);
    const float axleZ = (z2 + z0) * 0.5f;
    const float lever = std::abs(trailerHitchOffset.z - axleZ);
    const float axleShare = (std::abs(trailerHitchOffset.z) * weight) / lever;
    float tractorShare;
    float wheelLoad;
    if (mm2) {
        // vehTrailer::Init swaps the two: each trailer wheel gets a quarter
        // of the hitch's share and the tractor the axle's.
        tractorShare = axleShare;
        wheelLoad = ((std::abs(axleZ) * weight) * 0.25f) / lever;
    } else {
        // OpenMM2 correction (see docs/physics.md): the tractor takes the
        // hitch's share and the axle's is split over the trailer wheels that
        // reach the ground with the lowest one (vpcentury's TWHL0/TWHL1 are
        // 3 cm pivots that never touch it). With MM2's values vpcentury's
        // trailer rides on its bump stops, and the bottoming push and the
        // joint's FreeRange correction then lift the whole rig.
        tractorShare = (std::abs(axleZ) * weight) / lever;
        float lowest = wheels[0].center.y - wheels[0].radius;
        for (const Wheel& w : wheels)
            lowest = std::min(lowest, w.center.y - w.radius);
        int carrying = 0;
        for (const Wheel& w : wheels)
            if (w.center.y - w.radius <= lowest + w.params.suspensionExtent)
                ++carrying;
        wheelLoad = axleShare / static_cast<float>(carrying);
    }
    for (std::size_t i : {2u, 3u, 0u, 1u})
        wheels[i].setNormalLoad(wheelLoad);
    // The tractor's share is split between its axles by the hitch position.
    CarSim& tractor = *m_tractor;
    const float front = tractor.wheels[0].center.z;
    const float rear = ((carHitchOffset.z - front) * tractorShare) / (tractor.wheels[2].center.z - front);
    const float rearEach = rear * 0.5f;
    const float frontEach = (tractorShare - rear) * 0.5f;
    tractor.wheels[2].addNormalLoad(rearEach);
    tractor.wheels[3].addNormalLoad(rearEach);
    tractor.wheels[0].addNormalLoad(frontEach);
    tractor.wheels[1].addNormalLoad(frontEach);
}

void Trailer::reset() {
    // vehTrailer::Reset: phInertialCS::Reset, the tractor's InertialCS matrix
    // with the origin offset applied in it, the joint, the drivetrains and
    // the wheels.
    InertialCS& ics = body.ics;
    ics.zero();
    const Mat34& t = m_tractor->body.ics.matrix;
    ics.matrix = t;
    const Vec3& o = originOffset;
    ics.matrix.m3 = {((t.m1.x * o.y + t.m2.x * o.z) + o.x * t.m0.x) + t.m3.x,
                     ((t.m1.y * o.y + t.m0.y * o.x) + t.m2.y * o.z) + t.m3.y,
                     ((t.m1.z * o.y + t.m0.z * o.x) + t.m2.z * o.z) + t.m3.z};
    joint.reset();
    for (Drivetrain& d : drivetrains)
        d.reset();
    for (Wheel& w : wheels) {
        w.reset();
        // OpenMM2: the wheels' drawing matrices start at rest.
        w.matrix = Mat34::mul(Mat34::translation(w.center), ics.matrix);
    }
    body.syncBoundMatrix();
    body.collider.reset();
}

void Trailer::addTo(World& world) {
    world.add(&body);
}

void Trailer::removeFrom(World& world) {
    world.remove(&body);
}

int Trailer::bottomedOut() const {
    int n = 0;
    for (const Wheel& w : wheels)
        if (w.bottomedOut)
            ++n;
    return n;
}

bool Trailer::requiresTerrainCollision() const {
    const Mat34& m = body.ics.matrix;
    if (!(0.5f < m.m1.y))
        return true;
    const Vec3& w0 = wheels[0].intersection.normal;
    const Vec3& w1 = wheels[1].intersection.normal;
    const Vec3& w2 = wheels[2].intersection.normal;
    const Vec3& w3 = wheels[3].intersection.normal;
    const Vec3 f{(w0.x + w1.x) * 0.5f, (w0.y + w1.y) * 0.5f, (w0.z + w1.z) * 0.5f};
    const Vec3 b{(w2.x + w3.x) * 0.5f, (w2.y + w3.y) * 0.5f, (w2.z + w3.z) * 0.5f};
    const Vec3 d{(f.x + b.x) * 0.5f - m.m1.x, (b.y + f.y) * 0.5f - m.m1.y, (b.z + f.z) * 0.5f - m.m1.z};
    return !((d.z * d.z + d.y * d.y) + d.x * d.x < 0.1f && bottomedOut() == 0);
}

void Trailer::setCarHitchOffset() {
    joint.offset1 = carHitchOffset;
}

void Trailer::setTrailerHitchOffset() {
    joint.offset2 = trailerHitchOffset;
}

float Trailer::hitchAngle() const {
    const Mat34& t = m_tractor->body.ics.matrix;
    const Vec3& back = body.ics.matrix.m2;
    return std::atan2(back.dot(t.m0), back.dot(t.m2));
}

void Trailer::beforeIntegrate(Body&, float, const World&) {
    // vehTrailer::Update: while hitched, the back wheels take the tractor's
    // inputs, steering opposite (vpsemi's tiller axle) with the tractor's
    // speed-sensitive steering, the handbrake eased on the inside of the
    // turn. The front wheels never steer or brake.
    if (joint.isBroken())
        return;
    const float steer = m_tractor->sssFactor(m_tractor->speed()) * m_tractor->steering;
    const float brakes = m_tractor->brakes;
    const float hand = m_tractor->handBrake;
    wheels[2].setInputs(-steer, brakes, (0.0f < steer ? 1.0f - steer : 1.0f) * hand);
    wheels[3].setInputs(-steer, brakes, (0.0f <= steer ? 1.0f : steer + 1.0f) * hand);
}

void Trailer::afterIntegrate(Body& b, float dt, const World& world) {
    // The four drivetrains, each updating its wheel, then the joint.
    WheelEnv env;
    env.ics = &b.ics;
    env.frame = &b.ics.matrix;
    env.ground = &world;
    env.dt = dt;
    env.invDt = 1.0f / dt;
    env.weatherFriction = m_tractor->options.weatherFriction;
    env.hasCar = false;
    env.randomSeed = world.randomSeed();
    for (Drivetrain& d : drivetrains)
        d.update(env, m_tractor->params.mass);
    joint.update(dt, env.invDt);
}

} // namespace mm2::phys
