#include "game/TrafficBodies.h"

#include "phys/Collide.h"

#include <algorithm>
#include <vector>

namespace mm2::game {
namespace {

constexpr float kReleaseDistance = 200.0f;
// Restitution for car-car hits before the full contact solver takes over;
// MM1 used the bound elasticities, the value here is inferred.
constexpr float kElasticity = 0.3f;

// The rail car's collision box in the world. The box sits on the ground
// under the model origin (inferred from the .aivehicledata Size field).
phys::Obb obbOf(const ai::AmbientCar& car) {
    const Vec3 size = car.data ? car.data->size : Vec3{2.0f, 1.5f, 4.5f};
    phys::Obb o;
    o.center = car.transform.transform({0, size.y * 0.5f, 0});
    o.axis[0] = car.transform.m0.normalized();
    o.axis[1] = car.transform.m1.normalized();
    o.axis[2] = car.transform.m2.normalized();
    o.half = size * 0.5f;
    return o;
}

} // namespace

TrafficBodies::TrafficBodies(ai::World& ai, phys::World& world) : m_ai(ai), m_world(world) {
    m_ai.traffic().setImpactHandler([this](ai::AmbientCar& car, const Vec3& impulse) { onImpact(car, impulse); });
}

TrafficBodies::~TrafficBodies() {
    m_ai.traffic().setImpactHandler({});
    for (auto& [id, a] : m_active)
        m_world.remove(a.body.get());
}

void TrafficBodies::beforeStep(std::span<phys::Body* const> vehicles) {
    phys::Contact contacts[phys::kMaxContacts];
    // Copy the ids first: impact() changes the AI's bookkeeping.
    struct Hit {
        int id;
        phys::Body* vehicle;
        Vec3 point, normal;
        float closing;
    };
    std::vector<Hit> hits;
    for (phys::Body* v : vehicles) {
        const phys::Obb vo = v->obb();
        const Aabb vb = vo.aabb();
        for (const auto& car : m_ai.cars()) {
            if (car.physical)
                continue;
            const phys::Obb co = obbOf(car);
            const Aabb cb = co.aabb();
            if (cb.max.x < vb.min.x || cb.min.x > vb.max.x || cb.max.y < vb.min.y || cb.min.y > vb.max.y ||
                cb.max.z < vb.min.z || cb.min.z > vb.max.z)
                continue;
            if (phys::collideObbObb(vo, co, contacts, phys::kMaxContacts) <= 0)
                continue;
            const Vec3 n = contacts[0].normal; // towards the vehicle
            const Vec3 rel = v->ics.getVelocity(&contacts[0].point) - car.velocity;
            hits.push_back({car.id, v, contacts[0].point, n, std::max(0.0f, -rel.dot(n))});
        }
    }
    for (const auto& h : hits) {
        // Impulse from the closing speed and both masses (the car's mass from
        // its .aivehicledata).
        const ai::AmbientCar* car = nullptr;
        for (const auto& c : m_ai.cars())
            if (c.id == h.id)
                car = &c;
        if (!car)
            continue;
        const float mv = h.vehicle->ics.mass;
        const float mc = car->data ? car->data->mass : 1000.0f;
        const float j = (1.0f + kElasticity) * h.closing * (mv * mc / (mv + mc));
        const Vec3 impulse = -h.normal * j; // into the traffic car
        if (j > 0.0f) {
            h.vehicle->ics.applyImpulse(-impulse, h.point);
            if (h.vehicle->ics.state == phys::InertialCS::Asleep)
                h.vehicle->ics.state = phys::InertialCS::Awake;
        }
        m_pending = {h.vehicle, h.point};
        m_ai.traffic().impact(h.id, impulse);
        m_pending = {};
    }
}

void TrafficBodies::onImpact(ai::AmbientCar& car, const Vec3& impulse) {
    if (m_active.contains(car.id))
        return;
    const ai::VehicleData* d = car.data;
    const Vec3 size = d ? d->size : Vec3{2.0f, 1.5f, 4.5f};
    Active a;
    a.body = std::make_unique<phys::Body>();
    a.cg = d ? d->cg : Vec3{0, size.y * 0.4f, 0};
    phys::Body& b = *a.body;
    b.shape.kind = phys::Shape::Kind::Box;
    b.shape.half = size * 0.5f;
    b.shape.offset = Vec3{0, size.y * 0.5f, 0} - a.cg;
    b.ics.setMass(size.x, size.y, size.z, d ? d->mass : 1000.0f);
    Mat34 frame = car.transform;
    frame.m3 = car.transform.transform(a.cg);
    b.ics.place(frame);
    b.ics.elasticity = d ? d->elasticity : 0.5f;
    b.ics.friction = d ? d->friction : 0.5f;
    b.ics.state = phys::InertialCS::Awake;
    // Carry the rail velocity, then the hit.
    b.ics.applyImpulseNow(car.velocity * b.ics.mass, frame.m3);
    if (impulse.mag2() > 0.0f)
        b.ics.applyImpulseNow(impulse, m_pending.vehicle ? m_pending.point : frame.m3);
    m_world.add(&b);
    a.model = car.transform;
    m_active.emplace(car.id, std::move(a));
}

void TrafficBodies::afterStep(const Vec3& playerPos) {
    std::vector<int> release;
    for (auto& [id, a] : m_active) {
        a.model = a.body->ics.matrix;
        a.model.m3 = a.body->ics.matrix.m3 - a.body->ics.matrix.transformDir(a.cg);
        if (a.model.m3.dist2(playerPos) > sq(kReleaseDistance))
            release.push_back(id);
    }
    for (int id : release) {
        m_world.remove(m_active[id].body.get());
        m_active.erase(id);
        m_ai.traffic().release(id);
    }
}

const Mat34* TrafficBodies::transformOf(int carId) const {
    const auto it = m_active.find(carId);
    return it == m_active.end() ? nullptr : &it->second.model;
}

} // namespace mm2::game
