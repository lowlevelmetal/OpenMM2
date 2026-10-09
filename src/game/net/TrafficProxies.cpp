// The shared traffic of a network cruise in a client's physics world
// (OpenMM2 extra). See TrafficProxies.h.
#include "game/net/TrafficProxies.h"

#include <algorithm>
#include <cmath>

namespace mm2::game {

// A received traffic car: aiVehicleInstance's box and place (as
// TrafficBodies' rail cars), and its motion.
class TrafficProxies::Proxy final : public phys::Instance {
public:
    const phys::Bound* bound(int) const override { return box; }
    const Mat34& matrix() const override { return transform; }
    // aiVehicleInstance::GetPosition: one up-axis above the model origin.
    Vec3 position() const override { return transform.m3 + transform.m1; }
    float radius() const override { return sphereRadius; }
    bool kinematicMotion(Vec3& v, Vec3& w, Vec3& centre) const override {
        v = velocity;
        w = spin;
        centre = position();
        return true;
    }

    const ai::VehicleData* data = nullptr;
    const phys::BoundBox* box = nullptr;
    float sphereRadius = 0.0f;
    Mat34 transform;
    Vec3 velocity, spin;
    bool seen = false;
};

TrafficProxies::TrafficProxies(const phys::Level* level) : m_level(level) {}

TrafficProxies::~TrafficProxies() = default;

const TrafficProxies::BoundEntry& TrafficProxies::boundFor(const ai::VehicleData& data) {
    // aiVehicleManager::AddVehicleDataEntry's box, as TrafficBodies builds it.
    BoundEntry& entry = m_bounds[&data];
    if (entry.box)
        return entry;
    entry.box = std::make_unique<phys::BoundBox>();
    entry.box->makeOwnMaterial();
    entry.box->setOffset(data.cg);
    entry.box->setSize(data.size);
    const Vec3& c = entry.box->centroid;
    const float dy = c.y - 1.0f;
    entry.radius = std::sqrt(c.x * c.x + dy * dy + c.z * c.z) + entry.box->radius;
    return entry;
}

void TrafficProxies::update(std::span<const ai::AmbientCar> cars) {
    for (auto& [id, p] : m_proxies)
        p->seen = false;
    for (const ai::AmbientCar& c : cars) {
        if (!c.data)
            continue;
        auto& slot = m_proxies[c.id];
        if (!slot)
            slot = std::make_unique<Proxy>();
        Proxy& p = *slot;
        if (p.data != c.data) {
            const BoundEntry& b = boundFor(*c.data);
            p.data = c.data;
            p.box = b.box.get();
            p.sphereRadius = b.radius;
            p.room = 0;
        }
        p.transform = c.transform;
        p.velocity = c.velocity;
        p.spin = {};
        p.seen = true;
        p.collidable = true;
        p.room = m_level ? m_level->findRoom(p.position(), p.room) : 0;
    }
    std::erase_if(m_proxies, [](const auto& e) { return !e.second->seen; });
    m_byRoom.clear();
    for (auto& [id, p] : m_proxies)
        m_byRoom.emplace_back(p->room, p.get());
    std::ranges::sort(m_byRoom, [](const auto& a, const auto& b) { return a.first < b.first; });
}

std::vector<int> TrafficProxies::takeHits() {
    std::vector<int> hits;
    for (auto& [id, p] : m_proxies) {
        if (p->hitByPlayer)
            hits.push_back(id);
        p->hitByPlayer = false;
    }
    std::ranges::sort(hits);
    return hits;
}

void TrafficProxies::instancesIn(int room, std::vector<phys::Instance*>& out) const {
    const auto range = std::ranges::equal_range(m_byRoom, room, {}, &std::pair<int, Proxy*>::first);
    for (const auto& [r, p] : range)
        out.push_back(p);
}

} // namespace mm2::game
