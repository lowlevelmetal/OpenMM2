// Ambient traffic in the physics world: aiVehicleInstance (GetBound,
// GetMatrix, GetPosition, GetEntity, AttachEntity), aiVehicleManager (Attach,
// Detach, Update, AddVehicleDataEntry's bound), aiVehicleActive (ctor,
// Attach, Detach, Update, PostUpdate, Impact) and vehWheelCheap (Init, Reset,
// Update), from the code of midtown2.exe build 3393 (MM2Recomp;
// documentation only). See docs/physics.md, "Collision".
// Also: aiVehicleActive::DetachMe (detach + release), aiVehicleActive::Reset,
// aiVehicleActive::UpdateDamage (damage stays 0), aiVehicleActive::GetICS,
// aiVehicleActive::GetInst, aiVehicleInstance::GetData,
// aiVehicleInstance::SetMatrix (never called), aiVehicleManager::Reset (reset).

#include "game/TrafficBodies.h"

#include "phys/Collision.h"
#include "phys/Impact.h"
#include "phys/Sleep.h"

#include <algorithm>
#include <cmath>

namespace mm2::game {
namespace {

// dgPhysEntity::Update: Mass * -19.6 joins the force before each sample.
constexpr float kGravityY = -19.6f;
// aiVehicleActive ctor: the phSleep thresholds (speed^2, spin^2).
constexpr float kSleepSpeed2 = 0.01f;
constexpr float kSleepSpin2 = 0.01f;
// aiVehicleActive::PostUpdate, aiVehicleManager::Update: lost below this.
constexpr float kFallOutY = -100.0f;
// aiVehicleActive::Impact: ids above 1000 play as 0; impulses above 100
// reach the breakable parts.
constexpr int kMaxAudioId = 1000;
constexpr float kBreakImpulse = 100.0f;
// aiVehicleActive::Detach: the probe for the ground under the car, from
// half a metre above its origin to three below along its up axis; standing
// on ground facing up within acos(0.9) it is upright.
constexpr float kUprightProbeUp = 0.5f;
constexpr float kUprightProbeDown = 3.0f;
constexpr double kUprightCosine = 0.9;
// vehWheelCheap::Update.
constexpr float kMaxCompressionRate = 3.0f; // the damper sees at most 3 m/s
constexpr double kBottomedOutT = 0.1;       // the probe hit in its top tenth
constexpr float kTyreGrip = 0.4f;           // deflection limit: 0.4 * load * WeatherFriction / RubberSpring
// vehWheelCheap::Update: the drawn wheel moves back by these fractions of
// the sideways and forward tyre deflections.
constexpr float kDrawLateral = 0.2f;
constexpr float kDrawLongitudinal = 0.3f;
// vehWheelCheap::Init: a quarter of the car's weight preloads each spring.
constexpr float kPreloadShare = -0.25f;

// aiVehicleActive's body. The level sees it as the aiVehicleInstance it
// came from: its matrix is the AI's copy of the body's (aiVehicleActive::
// Update copies the ICS matrix after the integration, so it misses the
// sample's push, as Body::boundMatrix does), its centre one up-axis above
// the origin (aiVehicleInstance::GetPosition) and its radius the rail car's.
class ActiveBody final : public phys::Body {
public:
    Vec3 position() const override {
        const Mat34& m = boundMatrix;
        return {m.m3.x + m.m1.x, m.m3.y + m.m1.y, m.m3.z + m.m1.z};
    }
    float radius() const override { return sphereRadius; }

    float sphereRadius = 0.0f;
};

// Matrix34::FastInverse of an orthonormal matrix applied to a point:
// the point in the matrix's frame.
Vec3 toLocal(const Mat34& m, const Vec3& p) {
    const float tx = -((m.m0.x * m.m3.x + m.m0.y * m.m3.y) + m.m0.z * m.m3.z);
    const float ty = -((m.m1.x * m.m3.x + m.m1.y * m.m3.y) + m.m1.z * m.m3.z);
    const float tz = -((m.m2.x * m.m3.x + m.m2.y * m.m3.y) + m.m2.z * m.m3.z);
    return {((m.m0.z * p.z + m.m0.y * p.y) + m.m0.x * p.x) + tx,
            ((m.m1.x * p.x + m.m1.z * p.z) + m.m1.y * p.y) + ty,
            ((m.m2.x * p.x + m.m2.z * p.z) + m.m2.y * p.y) + tz};
}

float signOf(float v) {
    if (v > 0.0f)
        return 1.0f;
    return v < 0.0f ? -1.0f : 0.0f;
}

// vehWheelCheap's tyre deflection: from `previous` it moves by at most
// this sample's slip distance `step`, stopping at `target`.
float deflect(float previous, float step, float target) {
    float lo = previous;
    float hi = previous;
    if (step < 0.0f)
        lo = step + previous;
    else
        hi = step + previous;
    if (target < lo)
        return lo;
    if (target <= hi)
        return target;
    return hi;
}

// phInertialCS has no sleep test of its own (phSleep decides). OpenMM2's
// InertialCS keeps MM1's in state Awake, so an active body runs in state
// Off, as the cars do (CarSim); phSleep's WakeUp turns Asleep into Awake.
void keepIntegrating(phys::InertialCS& ics) {
    if (ics.state == phys::InertialCS::Awake)
        ics.state = phys::InertialCS::Off;
}

} // namespace

// --- vehWheelCheap -----------------------------------------------------------------------------

// A wheel without a model of its own: a spring and damper along the car's
// up axis and a tyre that grips by deflecting like a stiff rubber spring
// (locked wheels: the car slides to a stop).
struct TrafficBodies::Wheel {
    phys::InertialCS* ics = nullptr;
    Vec3 pivot; // model space (geometry/<model>_whlN.mtx)
    // The ctor's values; Init takes the aiVehicleData's.
    float radius = 0.3f;
    float spring = 50000.0f;
    float damping = 5000.0f;
    float rubberSpring = 40000.0f;
    float rubberDamp = 2000.0f;
    float limit = 0.07f;
    float preload = 0.0f;
    // State (Reset clears it).
    float compression = 0.0f;  // the spring's travel; -limit off the ground
    float lateral = 0.0f;      // tyre deflection across the car (m)
    float longitudinal = 0.0f; // and along it
    bool contact = false;
    bool bottomedOut = false;  // the probe hit near its top (aiVehicleActive::BottomedOut)
    // The drawing matrix (+0x128) and its model-space position (+0x158's):
    // aiVehicleInstance::Draw draws the wheel there while the car has a body.
    Vec3 drawOffset;
    Mat34 drawMatrix;

    // vehWheelCheap::Init.
    void init(const Vec3& wheelPivot, const ai::VehicleData& data, phys::InertialCS& body) {
        ics = &body;
        pivot = wheelPivot;
        radius = data.wheelRadius;
        spring = data.spring;
        damping = data.damping;
        rubberSpring = data.rubberSpring;
        rubberDamp = data.rubberDamp;
        limit = data.limit;
        preload = body.mass * kGravityY * kPreloadShare;
        // The drawing matrix: unturned, at the pivot.
        drawOffset = pivot;
        drawMatrix = Mat34::mul(Mat34::translation(drawOffset), body.matrix);
        reset();
    }

    // vehWheelCheap::Reset.
    void reset() {
        compression = 0.0f;
        longitudinal = 0.0f;
        lateral = 0.0f;
        contact = false;
        bottomedOut = false;
    }

    // vehWheelCheap::Update, after the body's integration: probes the ground
    // from the top of the wheel's travel to the bottom along the car's up
    // axis and adds the spring's and the tyre's forces at the contact. (The
    // wheel's drawing matrix follows: see placeDrawing.)
    void update(const phys::GroundQuery& ground, const phys::Instance* self, float seconds, float invSeconds,
                float weatherFriction) {
        phys::InertialCS& body = *ics;
        const Mat34& m = body.matrix;
        const Vec3 p{((m.m2.x * pivot.z + m.m0.x * pivot.x) + m.m1.x * pivot.y) + m.m3.x,
                     ((m.m2.y * pivot.z + m.m0.y * pivot.x) + m.m1.y * pivot.y) + m.m3.y,
                     ((m.m2.z * pivot.z + m.m0.z * pivot.x) + m.m1.z * pivot.y) + m.m3.z};
        const float reach = radius + limit;
        const Vec3 travel{reach * m.m1.x, reach * m.m1.y, reach * m.m1.z};
        const Vec3 top = travel + p;
        const Vec3 bottom = p - travel;
        const Vec3 span = top - bottom;
        phys::RayHit hit;
        // dgPhysManager::Collide with the wheels' mask, the car itself left out.
        contact = ground.wheelProbe(top, bottom, hit, self, nullptr);
        const Vec3& n = hit.normal;
        if (!contact || (n.x * n.x + n.y * n.y) + n.z * n.z == 0.0f) {
            compression = -limit;
            lateral = 0.0f;
            longitudinal = 0.0f;
            placeDrawing(m);
            return;
        }

        // The contact's velocity in the ground plane.
        const Vec3 r = hit.position - m.m3;
        const Vec3& w = body.angularVelocity;
        const Vec3 spin{r.z * w.y - r.y * w.z, r.x * w.z - r.z * w.x, r.y * w.x - r.x * w.y};
        const Vec3 v = spin + body.linearVelocity;
        const float vn = (v.z * n.z + v.y * n.y) + v.x * n.x;
        const Vec3 slide{v.x - vn * n.x, v.y - vn * n.y, v.z - vn * n.z};

        // The spring and damper: travel from the probe's hit, its rate
        // limited to 3 m/s, never pulling.
        bottomedOut = static_cast<double>(hit.t) < kBottomedOutT;
        const float spanLength = std::sqrt((span.z * span.z + span.y * span.y) + span.x * span.x);
        const float previous = compression;
        compression = ((radius + radius) + limit) - spanLength * hit.t;
        float rate = (compression - previous) * invSeconds;
        if (rate < -kMaxCompressionRate)
            rate = -kMaxCompressionRate;
        else if (rate > kMaxCompressionRate)
            rate = kMaxCompressionRate;
        float force = (compression * spring + rate * damping) + preload;
        if (force < 0.0f)
            force = 0.0f;
        const float load = ((n.z * m.m1.z + n.y * m.m1.y) + n.x * m.m1.x) * force;
        Vec3 total{load * n.x, load * n.y, load * n.z};

        // The tyre: the sliding moves the deflections towards the grip
        // limit in the direction of the slide; the rubber spring and damper
        // push back.
        const float slipLateral = (slide.x * m.m0.x + m.m0.z * slide.z) + m.m0.y * slide.y;
        const Vec3 forward{-m.m2.x, -m.m2.y, -m.m2.z};
        const float slipForward = (forward.z * slide.z + forward.y * slide.y) + forward.x * slide.x;
        const float grip = ((load * weatherFriction) * kTyreGrip) / rubberSpring;
        const float targetLateral = signOf(slipLateral) * grip;
        const float targetForward = signOf(slipForward) * grip;
        const float lastLateral = lateral;
        const float lastForward = longitudinal;
        lateral = deflect(lastLateral, slipLateral * seconds, targetLateral);
        longitudinal = deflect(lastForward, slipForward * seconds, targetForward);
        const float fLateral =
            -(rubberSpring * lateral) - ((lateral - lastLateral) * invSeconds) * rubberDamp;
        const float fForward =
            -(rubberSpring * longitudinal) - ((longitudinal - lastForward) * invSeconds) * rubberDamp;
        total = {(m.m0.x * fLateral - m.m2.x * fForward) + total.x,
                 total.y + (m.m0.y * fLateral - m.m2.y * fForward),
                 total.z + (fLateral * m.m0.z - m.m2.z * fForward)};
        body.applyForce(total, hit.position);
        placeDrawing(m);
    }

    // The end of vehWheelCheap::Update: the drawing matrix, unturned, at the
    // pivot moved along the car's up axis by the spring's travel and back by
    // 0.2 of the sideways and 0.3 of the forward tyre deflection, carried by
    // the body's matrix as it stands after this sample's integration.
    void placeDrawing(const Mat34& m) {
        drawOffset = {pivot.x - lateral * kDrawLateral, pivot.y + compression,
                      pivot.z - longitudinal * kDrawLongitudinal};
        drawMatrix = Mat34::mul(Mat34::translation(drawOffset), m);
    }
};

// --- aiVehicleInstance -------------------------------------------------------------------------

class TrafficBodies::RailCar final : public phys::Instance {
public:
    RailCar(TrafficBodies& owner, int carId) : m_owner(owner), id(carId) {}

    // aiVehicleInstance::GetBound: the aiVehicleData's box, whatever is asked.
    const phys::Bound* bound(int) const override { return box; }
    // aiVehicleInstance::GetMatrix: the AI's matrix, which follows the body
    // while there is one.
    const Mat34& matrix() const override;
    // aiVehicleInstance::GetPosition: one up-axis above the model origin.
    Vec3 position() const override {
        const Mat34& m = matrix();
        return {m.m3.x + m.m1.x, m.m3.y + m.m1.y, m.m3.z + m.m1.z};
    }
    // lvlInstance::GetRadius returns the model's radius; OpenMM2 uses the
    // sphere about position() that holds the box (inferred).
    float radius() const override { return sphereRadius; }
    // aiVehicleInstance::GetEntity: the body while the car has one.
    phys::Body* entity() override;
    // aiVehicleInstance::AttachEntity.
    phys::Body* attachEntity() override;

private:
    TrafficBodies& m_owner;

public:
    int id;
    const ai::VehicleData* data = nullptr;
    const phys::BoundBox* box = nullptr;
    float sphereRadius = 0.0f;
    Mat34 railMatrix; // the AI's transform (model origin)
    float speed = 0.0f;
    Active* active = nullptr; // aiVehicleInstance's active slot
    bool present = false;     // in the AI's list this frame
    // No body, but the AI's list still shows the car physical: handed back
    // since the AI last published its cars, or `lost`. It stays where the
    // body left it.
    bool held = false;
    // Its body was dropped by aiVehicleManager::Update while the AI holds
    // the car: not collidable until the AI lets it go.
    bool lost = false;
};

// --- aiVehicleActive ---------------------------------------------------------------------------

class TrafficBodies::Active final : public phys::BodyController, public phys::ImpactHandler {
public:
    explicit Active(TrafficBodies& owner) : m_owner(owner) {
        // aiVehicleActive ctor: phSleep::Init, thresholds 0.01 and 0.01.
        body.controller = this;
        body.collider.handler = this;
        sleep.init(&body.ics);
        sleep.speed2 = kSleepSpeed2;
        sleep.spin2 = kSleepSpin2;
    }

    // aiVehicleActive::Attach.
    void attach(RailCar& car);

    // aiVehicleActive::Update, per sample: phSleep::Update before the
    // integration (dgPhysEntity::Update's gravity is added by
    // InertialCS::update), the four wheels after it. The AI matrix follows
    // the body (Body::boundMatrix) and World moves it between rooms.
    void beforeIntegrate(phys::Body& b, float, const phys::World&) override {
        sleep.update(phys::sampleTime().invSeconds);
        keepIntegrating(b.ics);
    }
    void afterIntegrate(phys::Body& b, float, const phys::World& world) override {
        const phys::SampleTime& t = phys::sampleTime();
        for (Wheel& w : wheels)
            w.update(world, &b, t.seconds, t.invSeconds, m_owner.m_weatherFriction);
    }

    // aiVehicleActive::Impact (the collider's impact callback).
    void onImpact(phys::Collider& self, const phys::Impact& impact, const Vec3& impulse) override;

private:
    TrafficBodies& m_owner;

public:
    ActiveBody body;
    phys::Sleep sleep;
    std::array<Wheel, 4> wheels{};
    RailCar* rail = nullptr;
    // The damage and its maximum: UpdateDamage is empty, so the damage stays 0.
    float damage = 0.0f;
    float maxDamage = 0.0f;
};

const Mat34& TrafficBodies::RailCar::matrix() const {
    return active ? active->body.boundMatrix : railMatrix;
}

phys::Body* TrafficBodies::RailCar::entity() {
    return active ? &active->body : nullptr;
}

phys::Body* TrafficBodies::RailCar::attachEntity() {
    // aiVehicleManager::Attach gives the car a body, then the AI vehicle
    // leaves its rail (aiVehicleAmbient::Impact(1), which stops it: the
    // body has already taken its speed).
    if (active)
        return &active->body;
    Active& a = m_owner.attach(*this);
    m_owner.m_ai.traffic().impact(id, {});
    return &a.body;
}

void TrafficBodies::Active::attach(RailCar& car) {
    const ai::VehicleData& d = *car.data;
    // The rail car stops being an instance of its own (flag 0x10 cleared)
    // and points at its active.
    car.collidable = false;
    car.active = this;
    phys::InertialCS& ics = body.ics;
    ics.state = phys::InertialCS::Off;
    ics.zero();
    // The body's frame is the AI's matrix: it turns about the model origin.
    ics.matrix = car.railMatrix;
    rail = &car;
    ics.setMass(d.size.x, d.size.y, d.size.z, d.mass);
    ics.gravity = {0.0f, kGravityY, 0.0f};
    body.collisionBound = car.box;
    body.terrainBound = nullptr;
    body.boundOrigin = {};
    body.syncBoundMatrix();
    body.sphereRadius = car.sphereRadius;
    body.room = car.room;
    // phCollider::Init with the ICS (the ICS matrix places the bound),
    // SetImpactCB, Reset; the collider's id is 0.
    body.audioId = 0;
    body.resetCollider();
    body.collider.matrix = &ics.matrix;
    body.collider.reset();
    body.collider.handler = this;
    sleep.reset();
    sleep.wakeUp();
    maxDamage = d.maxDamage;
    // Moving along the rail (the model faces -Z) at the AI's speed; the last
    // matrix is where that motion started a sample ago, so the first sweep
    // covers it.
    const float alongZ = -car.speed;
    const Vec3 v{alongZ * car.railMatrix.m2.x, alongZ * car.railMatrix.m2.y, alongZ * car.railMatrix.m2.z};
    ics.linearVelocity = v;
    ics.linearMomentum = {ics.mass * v.x, ics.mass * v.y, ics.mass * v.z};
    const float seconds = phys::sampleTime().seconds;
    Mat34 last = ics.matrix;
    last.m3 = {last.m3.x - seconds * v.x, last.m3.y - seconds * v.y, last.m3.z - seconds * v.z};
    body.collider.lastMatrix = last;
    body.collider.calcMaxMoved(seconds);
    for (std::size_t i = 0; i < wheels.size(); ++i)
        wheels[i].init(d.wheels[i], d, ics);
}

void TrafficBodies::Active::onImpact(phys::Collider& self, const phys::Impact& impact, const Vec3& impulse) {
    if (!rail)
        return;
    TrafficImpact e;
    e.carId = rail->id;
    // The original takes the id of whichever side of the impact is this
    // car's collider (both branches name its own collider).
    const phys::Collider& own = impact.colliderA == &self ? *impact.colliderA : *impact.colliderB;
    e.audioId = own.id <= kMaxAudioId ? own.id : 0;
    e.strength = (std::abs(impulse.z) + std::abs(impulse.y)) + std::abs(impulse.x);
    e.impulse = std::sqrt((impulse.x * impulse.x + impulse.y * impulse.y) + impulse.z * impulse.z);
    e.localPosition = toLocal(body.ics.matrix, impact.position);
    e.room = body.room;
    e.breaks = e.impulse > kBreakImpulse;
    if (m_owner.m_onImpact)
        m_owner.m_onImpact(e);
}

// --- TrafficBodies (aiVehicleManager) ----------------------------------------------------------

TrafficBodies::TrafficBodies(ai::World& ai, phys::World& world) : m_ai(ai), m_world(world) {
    m_actives.reserve(m_order.size());
    for (std::size_t i = 0; i < m_order.size(); ++i) {
        m_actives.push_back(std::make_unique<Active>(*this));
        m_order[i] = m_actives.back().get();
    }
}

TrafficBodies::~TrafficBodies() {
    for (int i = 0; i < m_count; ++i)
        m_world.remove(&m_order[static_cast<std::size_t>(i)]->body);
}

TrafficBodies::RailCar& TrafficBodies::railCar(int id) {
    const auto index = static_cast<std::size_t>(id);
    if (index >= m_railCars.size())
        m_railCars.resize(index + 1);
    if (!m_railCars[index])
        m_railCars[index] = std::make_unique<RailCar>(*this, id);
    return *m_railCars[index];
}

const TrafficBodies::RailCar* TrafficBodies::findRailCar(int id) const {
    const auto index = static_cast<std::size_t>(id);
    return id >= 0 && index < m_railCars.size() ? m_railCars[index].get() : nullptr;
}

const TrafficBodies::BoundEntry& TrafficBodies::boundFor(const ai::VehicleData& data) {
    BoundEntry& entry = m_bounds[&data];
    if (entry.box)
        return entry;
    // aiVehicleManager::AddVehicleDataEntry: a dgBoundBox of Size centred on
    // CG. Its own material keeps lvlMaterial's elasticity 0.5 and friction
    // 1: aiVehicleData::SetFricElas, which would give it the data's, is
    // never called.
    entry.box = std::make_unique<phys::BoundBox>();
    entry.box->makeOwnMaterial();
    entry.box->setOffset(data.cg);
    entry.box->setSize(data.size);
    // The sphere about the instance's position (one metre up the model's y
    // axis) holding the box (inferred; lvlInstance::GetRadius is the
    // model's radius).
    const Vec3& c = entry.box->centroid;
    const float dy = c.y - 1.0f;
    entry.radius = std::sqrt(c.x * c.x + dy * dy + c.z * c.z) + entry.box->radius;
    return entry;
}

TrafficBodies::Active& TrafficBodies::attach(RailCar& car) {
    // aiVehicleManager::Attach: the next free active; with all 32 taken the
    // first in the list is detached and takes the new car.
    if (m_count < static_cast<int>(m_order.size())) {
        Active& a = *m_order[static_cast<std::size_t>(m_count)];
        a.attach(car);
        ++m_count;
        return a;
    }
    Active& a = *m_order[0];
    detach(a);
    a.attach(car);
    return a;
}

void TrafficBodies::release(Active& active) {
    // aiVehicleManager::Detach: the last attached active takes its place in
    // the list.
    for (int i = 0; i < m_count; ++i) {
        if (m_order[static_cast<std::size_t>(i)] != &active)
            continue;
        --m_count;
        m_order[static_cast<std::size_t>(i)] = m_order[static_cast<std::size_t>(m_count)];
        m_order[static_cast<std::size_t>(m_count)] = &active;
        return;
    }
}

void TrafficBodies::detach(Active& active) {
    // aiVehicleActive::Detach.
    RailCar* car = active.rail;
    if (!car)
        return;
    m_world.remove(&active.body); // dgPhysManager::IgnoreMover
    bool upright = active.damage <= active.maxDamage;
    const Mat34& m = active.body.ics.matrix;
    const Vec3 from{m.m1.x * kUprightProbeUp + m.m3.x, m.m1.y * kUprightProbeUp + m.m3.y,
                    m.m1.z * kUprightProbeUp + m.m3.z};
    const Vec3 to{m.m3.x - m.m1.x * kUprightProbeDown, m.m3.y - m.m1.y * kUprightProbeDown,
                  m.m3.z - m.m1.z * kUprightProbeDown};
    phys::RayHit hit;
    if (!m_world.wheelProbe(from, to, hit, &active.body, nullptr))
        upright = false;
    else if (static_cast<double>((hit.normal.y * m.m1.y + hit.normal.z * m.m1.z) + hit.normal.x * m.m1.x) <
             kUprightCosine)
        upright = false;
    // Upright: aiVehicleAmbient::Impact(0) (back to its rail); otherwise, or
    // when it already is one, a wreck (flag 2); the AI decides. Its matrix
    // is where the body last left it.
    const Mat34 pose = active.body.boundMatrix;
    m_ai.traffic().detach(car->id, pose, upright);
    // The car is an instance again (flag 0x10) without an active.
    car->railMatrix = pose;
    car->speed = 0.0f;
    car->collidable = true;
    car->active = nullptr;
    car->held = true;
    car->lost = false;
    active.rail = nullptr;
}

void TrafficBodies::drop(Active& active) {
    // No longer declared as a mover. The rail car keeps its flags (not
    // collidable) and the AI keeps the car where it stopped. (MM2 also
    // leaves the instance pointing at the active's slot; OpenMM2 clears it.)
    m_world.remove(&active.body);
    if (RailCar* car = active.rail) {
        car->railMatrix = active.body.boundMatrix;
        car->speed = 0.0f;
        car->active = nullptr;
        car->held = true;
        car->lost = true;
    }
    active.rail = nullptr;
}

void TrafficBodies::reset() {
    // aiVehicleManager::Reset: Detach for each attached active, the count to
    // 0, then aiVehicleActive::Reset (empty) for all 32.
    for (int i = 0; i < m_count; ++i)
        detach(*m_order[static_cast<std::size_t>(i)]);
    m_count = 0;
    for (auto& r : m_railCars) {
        if (!r)
            continue;
        r->held = false;
        r->lost = false;
    }
}

void TrafficBodies::beforeStep() {
    const auto& cars = m_ai.cars();
    const auto find = [&](int id) -> const ai::AmbientCar* {
        for (const ai::AmbientCar& c : cars)
            if (c.id == id)
                return &c;
        return nullptr;
    };

    // Cars the AI recycled with their road leave the level
    // (aiVehicleInstance::Detach -> DetachMe): their bodies go back to the
    // pool, with nothing to hand back.
    for (int i = m_count - 1; i >= 0; --i) {
        Active& a = *m_order[static_cast<std::size_t>(i)];
        const ai::AmbientCar* c = a.rail ? find(a.rail->id) : nullptr;
        if (c && c->physical)
            continue;
        drop(a);
        release(a);
    }

    // aiVehicleManager::Update: actives asleep, below y = -100 or out of
    // every room stop being declared movers (PostUpdate has handed the
    // first two back already); the others are declared again as plain
    // movers colliding with everything (2, 0x1b).
    for (int i = 0; i < m_count; ++i) {
        Active& a = *m_order[static_cast<std::size_t>(i)];
        if (a.sleep.state != phys::Sleep::Asleep && !(a.body.ics.matrix.m3.y < kFallOutY) && a.body.room != 0)
            continue;
        drop(a);
        release(a);
        --i;
    }
    for (int i = 0; i < m_count; ++i)
        m_order[static_cast<std::size_t>(i)]->body.declare(2, 0x1b);

    // The rail cars follow the AI.
    const phys::Level* level = m_world.level();
    for (auto& r : m_railCars)
        if (r)
            r->present = false;
    for (const ai::AmbientCar& c : cars) {
        if (!c.data || c.id < 0)
            continue;
        RailCar& r = railCar(c.id);
        r.present = true;
        if (r.data != c.data) {
            const BoundEntry& b = boundFor(*c.data);
            r.data = c.data;
            r.box = b.box.get();
            r.sphereRadius = b.radius;
        }
        if (r.active) {
            r.room = r.active->body.room;
            continue;
        }
        if (c.physical) {
            // Handed back since the AI last published its cars (its list
            // still has the pose of the hit), or lost: it stays where the
            // body left it.
            r.held = true;
        } else {
            r.held = false;
            r.lost = false;
            r.railMatrix = c.transform;
            r.speed = c.speed;
        }
        r.collidable = !r.lost;
        r.room = level ? level->findRoom(r.position(), r.room) : 0;
    }
    // The AI's DeclareMover for the cars off their rails without a body
    // (aiGoalAvoidPlayer / aiGoalRegainRail: 0x0a, aiGoalCollision for a
    // wreck: 0x08): they collide with the instances round them this frame.
    // (A car with a body is declared (2, 0x1b) above, which holds these
    // flags.)
    for (const ai::AmbientCar& c : cars) {
        if (c.moverFlags == 0 || c.id < 0)
            continue;
        RailCar* r = static_cast<std::size_t>(c.id) < m_railCars.size() ? m_railCars[static_cast<std::size_t>(c.id)].get()
                                                                       : nullptr;
        if (r && !r->active && r->collidable && r->room != 0)
            m_world.declareInstance(r, 2, c.moverFlags);
    }
    m_roomList.clear();
    for (auto& r : m_railCars) {
        if (!r)
            continue;
        if (!r->present) {
            r->collidable = false;
            r->held = false;
            r->lost = false;
            r->room = 0;
            continue;
        }
        if (!r->active && r->collidable && r->room != 0)
            m_roomList.emplace_back(r->room, r.get());
    }
    // Already in id order; stable by room keeps it within a room.
    std::ranges::stable_sort(m_roomList, {}, &std::pair<int, RailCar*>::first);
}

void TrafficBodies::afterStep() {
    // aiVehicleActive::PostUpdate for each active (dgPhysManager calls it
    // for every mover after the samples).
    std::array<Active*, ai::kMaxPhysicalCars> list{};
    const int count = m_count;
    std::copy_n(m_order.begin(), count, list.begin());
    for (int i = 0; i < count; ++i) {
        Active& a = *list[static_cast<std::size_t>(i)];
        if (!a.rail)
            continue;
        const float y = a.body.ics.matrix.m3.y;
        if (a.sleep.state != phys::Sleep::Asleep && kFallOutY <= y) {
            m_ai.traffic().setPhysicalTransform(a.rail->id, a.body.boundMatrix);
            continue;
        }
        detach(a);
        release(a);
    }
}

void TrafficBodies::instancesIn(int room, std::vector<phys::Instance*>& out) const {
    const auto range = std::ranges::equal_range(m_roomList, room, {}, &std::pair<int, RailCar*>::first);
    for (const auto& [r, car] : range)
        if (!car->active && car->collidable)
            out.push_back(car);
}

bool TrafficBodies::knock(int carId, const Vec3& impulse, const Vec3& point) {
    const auto index = static_cast<std::size_t>(carId);
    RailCar* r = carId >= 0 && index < m_railCars.size() ? m_railCars[index].get() : nullptr;
    if (!r || r->active || !r->data || !r->box || !r->collidable || !r->present || r->held)
        return false;
    phys::Body* body = r->attachEntity();
    m_world.addNewMover(body);
    body->ics.applyImpulse(impulse, point);
    return true;
}

float TrafficBodies::massOf(int carId) const {
    const RailCar* r = findRailCar(carId);
    return r && r->data ? r->data->mass : 0.0f;
}

bool TrafficBodies::motionOf(int carId, Vec3& velocity, Vec3& spin) const {
    const RailCar* r = findRailCar(carId);
    if (!r || !r->active)
        return false;
    velocity = r->active->body.ics.linearVelocity;
    spin = r->active->body.ics.angularVelocity;
    return true;
}

std::optional<TrafficBodies::Wheels> TrafficBodies::wheelsOf(int carId) const {
    // aiVehicleInstance::Draw with the car's active: WHL0-3 at the four
    // vehWheelCheaps' drawing matrices; WHL4 / WHL5 unturned at their pivots
    // raised by WHL2's / WHL3's drawn height less the wheel radius, carried
    // by GetMatrix (the matrix the wheels were placed with).
    const RailCar* r = findRailCar(carId);
    if (!r || !r->active || !r->data)
        return std::nullopt;
    const Active& a = *r->active;
    const ai::VehicleData& d = *r->data;
    Wheels w;
    for (std::size_t i = 0; i < a.wheels.size(); ++i) {
        w.matrix[i] = a.wheels[i].drawMatrix;
        w.valid[i] = true;
    }
    for (std::size_t i = 4; i < 6; ++i) {
        if (d.wheelCount <= static_cast<int>(i))
            continue;
        const Vec3& p = d.wheels[i];
        const Vec3 local{p.x, (a.wheels[i - 2].drawOffset.y - d.wheelRadius) + p.y, p.z};
        w.matrix[i] = Mat34::mul(Mat34::translation(local), a.body.boundMatrix);
        w.valid[i] = true;
    }
    return w;
}

const Mat34* TrafficBodies::transformOf(int carId) const {
    const RailCar* r = findRailCar(carId);
    if (!r)
        return nullptr;
    if (r->active)
        return &r->active->body.boundMatrix;
    return r->held ? &r->railMatrix : nullptr;
}

} // namespace mm2::game
