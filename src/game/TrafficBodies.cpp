// Physical ambient cars after MM2's aiVehicleManager, aiVehicleActive and
// vehWheelCheap (build 3393, MM2Recomp; documentation only).
#include "game/TrafficBodies.h"

#include "phys/Collide.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace mm2::game {
namespace {

constexpr std::size_t kMaxPhysical = static_cast<std::size_t>(ai::kMaxPhysicalCars); // aiVehicleManager slots
constexpr float kAmbientGravity = 19.6f; // dgPhysEntity::Update for aiVehicleActive
constexpr float kStillSpeed2 = 0.01f;    // phSleep thresholds set by aiVehicleActive (squared)
constexpr int kStillSteps = 15;          // phSleep: still sub-steps before sleeping
constexpr float kFallOut = -100.0f;      // aiVehicleActive::PostUpdate

// The box bound (aiVehicleManager::AddVehicleDataEntry): centred at CG, Size
// extents; also the rail car's collision box.
phys::Obb obbOf(const ai::AmbientCar& car) {
    const Vec3 size = car.data ? car.data->size : Vec3{2.0f, 1.5f, 4.5f};
    const Vec3 cg = car.data ? car.data->cg : Vec3{0.0f, size.y * 0.5f, 0.0f};
    phys::Obb o;
    o.center = car.transform.transform(cg);
    o.axis[0] = car.transform.m0.normalized();
    o.axis[1] = car.transform.m1.normalized();
    o.axis[2] = car.transform.m2.normalized();
    o.half = size * 0.5f;
    return o;
}

bool overlaps(const Aabb& a, const Aabb& b) {
    return !(a.max.x < b.min.x || a.min.x > b.max.x || a.max.y < b.min.y || a.min.y > b.max.y ||
             a.max.z < b.min.z || a.min.z > b.max.z);
}

} // namespace

TrafficBodies::TrafficBodies(ai::World& ai, phys::World& world) : m_ai(ai), m_world(world) {
    m_ai.traffic().setImpactHandler([this](ai::AmbientCar& car, const Vec3&) { attach(car); });
}

TrafficBodies::~TrafficBodies() {
    m_ai.traffic().setImpactHandler({});
    for (auto& a : m_active)
        m_world.remove(a->body.get());
}

void TrafficBodies::attach(const ai::AmbientCar& car) {
    // aiVehicleManager::Attach: with every slot taken the oldest car is let
    // go first (aiVehicleActive::Detach decides its fate).
    if (m_active.size() >= kMaxPhysical)
        detach(0);
    auto a = std::make_unique<Active>();
    a->id = car.id;
    a->data = car.data;
    const ai::VehicleData* d = car.data;
    const Vec3 size = d ? d->size : Vec3{2.0f, 1.5f, 4.5f};
    a->body = std::make_unique<phys::Body>();
    phys::Body& b = *a->body;
    // aiVehicleActive::Attach: the body's frame is the AI matrix as it is, so
    // the centre of mass is the model origin; the box sits at CG.
    b.shape.kind = phys::Shape::Kind::Box;
    b.shape.half = size * 0.5f;
    b.shape.offset = d ? d->cg : Vec3{0.0f, size.y * 0.5f, 0.0f};
    b.ics.setMass(size.x, size.y, size.z, d ? d->mass : 1000.0f);
    b.ics.place(car.transform);
    // aiVehicleData::SetFricElas is never called: the box keeps the default
    // material.
    b.ics.elasticity = 0.5f;
    b.ics.friction = 1.0f;
    b.ics.gravity = {0.0f, -kAmbientGravity, 0.0f};
    b.ics.state = phys::InertialCS::Awake;
    // Moving along its heading at the rail speed.
    b.ics.applyImpulseNow(-car.transform.m2 * (car.speed * b.ics.mass), b.ics.matrix.m3);
    if (d) {
        a->wheelRadius = d->wheelRadius > 0.0f ? d->wheelRadius : 0.4f;
        for (int w = 0; w < std::min(d->wheelCount, 4); ++w)
            a->wheels.push_back({d->wheels[static_cast<std::size_t>(w)]});
    }
    b.controller = a.get();
    a->model = car.transform;
    m_world.add(&b);
    m_active.push_back(std::move(a));
}

// vehWheelCheap::Update: a spring and damper along the car's up axis and
// locked-wheel grip of up to 0.4 N per axis through rubber deflection.
void TrafficBodies::Active::afterIntegrate(phys::Body& b, float dt, const phys::World& world) {
    const ai::VehicleData* d = data;
    auto& ics = b.ics;
    const Mat34& m = ics.matrix;
    const Vec3 up = m.m1;
    // Still-step count for phSleep.
    if (ics.linearVelocity.mag2() <= kStillSpeed2 && ics.angularVelocity.mag2() <= kStillSpeed2)
        ++stillSteps;
    else
        stillSteps = 0;
    if (!d || dt <= 0.0f)
        return;
    const float r = wheelRadius;
    const float L = d->limit;
    const float preload = ics.mass * kAmbientGravity * 0.25f;
    for (Wheel& w : wheels) {
        const Vec3 pivot = m.transform(w.pivot);
        const Vec3 from = pivot + up * (r + L);
        const Vec3 to = pivot - up * (r + L);
        phys::RayHit hit;
        if (!world.probe(from, to, hit) || hit.normal.mag2() == 0.0f) {
            w.compression = -L;
            w.latDeflection = w.lonDeflection = 0.0f;
            continue;
        }
        const Vec3 n = hit.normal.normalized();
        const float x = (2.0f * r + L) - 2.0f * (r + L) * hit.t;
        const float xDot = clampf((x - w.compression) / dt, -3.0f, 3.0f);
        w.compression = x;
        const float force = std::max(0.0f, d->spring * x + d->damping * xDot + preload);
        const float normal = n.dot(up) * force;
        // Sliding velocity at the contact, in the ground plane.
        Vec3 v = ics.getVelocity(&hit.position);
        v -= n * v.dot(n);
        const float sLat = v.dot(m.m0);
        const float sLon = v.dot(-m.m2);
        const float dmax = d->rubberSpring > 0.0f ? 0.4f * normal / d->rubberSpring : 0.0f;
        auto deflect = [&](float prev, float s) {
            const float target = s >= 0.0f ? dmax : -dmax;
            const float next = prev + s * dt;
            return s >= 0.0f ? std::min(next, target) : std::max(next, target);
        };
        const float lat = deflect(w.latDeflection, sLat);
        const float lon = deflect(w.lonDeflection, sLon);
        const float fLat = -d->rubberSpring * lat - d->rubberDamp * (lat - w.latDeflection) / dt;
        const float fLon = -d->rubberSpring * lon - d->rubberDamp * (lon - w.lonDeflection) / dt;
        w.latDeflection = lat;
        w.lonDeflection = lon;
        const Vec3 total = n * normal + m.m0 * fLat + (-m.m2) * fLon;
        ics.applyForce(total, hit.position);
    }
}

void TrafficBodies::beforeStep(std::span<phys::Body* const> vehicles) {
    phys::Contact contacts[phys::kMaxContacts];
    struct Hit {
        int id;
        phys::Body* vehicle;
        Vec3 point, normal;
        float closing;
    };
    // Anything moving collides with rail cars: the vehicles and the physical
    // traffic cars themselves (dgPhysManager::CollideInstances).
    std::vector<phys::Body*> movers(vehicles.begin(), vehicles.end());
    for (auto& a : m_active)
        movers.push_back(a->body.get());
    std::vector<Hit> hits;
    for (phys::Body* v : movers) {
        const phys::Obb vo = v->obb();
        const Aabb vb = vo.aabb();
        for (const auto& car : m_ai.cars()) {
            if (car.physical || std::ranges::any_of(hits, [&](const Hit& h) { return h.id == car.id; }))
                continue;
            const phys::Obb co = obbOf(car);
            if (!overlaps(co.aabb(), vb))
                continue;
            if (phys::collideObbObb(vo, co, contacts, phys::kMaxContacts) <= 0)
                continue;
            const Vec3 n = contacts[0].normal; // towards the vehicle
            const Vec3 rel = v->ics.getVelocity(&contacts[0].point) - car.velocity;
            hits.push_back({car.id, v, contacts[0].point, n, std::max(0.0f, -rel.dot(n))});
        }
    }
    for (const auto& h : hits) {
        const ai::AmbientCar* car = nullptr;
        for (const auto& c : m_ai.cars())
            if (c.id == h.id)
                car = &c;
        if (!car)
            continue;
        // aiVehicleInstance::AttachEntity -> aiVehicleAmbient::Impact(1).
        m_ai.traffic().impact(h.id, {});
        auto it = std::ranges::find_if(m_active, [&](const auto& a) { return a->id == h.id; });
        if (it == m_active.end())
            continue;
        // The hit itself (phContactMgr::CalcImpact for the contact): an
        // impulse from the closing speed and both masses; the restitution is
        // the product of the two bodies' elasticities (inferred).
        phys::Body& b = *(*it)->body;
        const float mv = h.vehicle->ics.mass;
        const float mc = b.ics.mass;
        const float e = std::clamp(h.vehicle->ics.elasticity * b.ics.elasticity, 0.0f, 1.0f);
        const float j = (1.0f + e) * h.closing * (mv * mc / (mv + mc));
        if (j > 0.0f) {
            const Vec3 impulse = -h.normal * j; // into the traffic car
            h.vehicle->ics.applyImpulse(-impulse, h.point);
            if (h.vehicle->ics.state == phys::InertialCS::Asleep)
                h.vehicle->ics.state = phys::InertialCS::Awake;
            b.ics.applyImpulseNow(impulse, h.point);
        }
    }
}

// aiVehicleActive::Detach: upright when a probe from 0.5 m above to 3 m
// below the car, along its own up axis, finds ground facing up (normal . up
// >= 0.9).
bool TrafficBodies::upright(const Active& a) const {
    const Mat34& m = a.body->ics.matrix;
    phys::RayHit hit;
    if (!m_world.probe(m.m3 + m.m1 * 0.5f, m.m3 - m.m1 * 3.0f, hit))
        return false;
    return hit.normal.normalized().dot(m.m1.normalized()) >= 0.9f;
}

void TrafficBodies::detach(std::size_t index) {
    Active& a = *m_active[index];
    m_ai.traffic().detach(a.id, a.body->ics.matrix, upright(a));
    m_world.remove(a.body.get());
    m_active.erase(m_active.begin() + static_cast<std::ptrdiff_t>(index));
}

void TrafficBodies::afterStep(const Vec3& /*playerPos*/) {
    for (std::size_t i = 0; i < m_active.size();) {
        Active& a = *m_active[i];
        // Recycled by the AI with its road: drop the body.
        const auto& cars = m_ai.cars();
        const auto car = std::ranges::find_if(cars, [&](const ai::AmbientCar& c) { return c.id == a.id; });
        if (car == cars.end() || !car->physical) {
            m_world.remove(a.body.get());
            m_active.erase(m_active.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        a.model = a.body->ics.matrix;
        m_ai.traffic().setPhysicalTransform(a.id, a.model);
        // aiVehicleActive::PostUpdate: asleep, or fallen out of the world.
        if (a.stillSteps >= kStillSteps || a.model.m3.y < kFallOut) {
            detach(i);
            continue;
        }
        ++i;
    }
}

const Mat34* TrafficBodies::transformOf(int carId) const {
    for (const auto& a : m_active)
        if (a->id == carId)
            return &a->model;
    return nullptr;
}

} // namespace mm2::game
