#include "game/bangers/BangerSet.h"

#include "core/Log.h"
#include "game/MeshDraw.h"
#include "phys/Collide.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace mm2::game::bangers {
namespace {

constexpr float kCell = 16.0f;

asset::Lod lodForDistance(float d, float scale) {
    if (d < 60.0f * scale)
        return asset::Lod::High;
    if (d < 130.0f * scale)
        return asset::Lod::Medium;
    if (d < 260.0f * scale)
        return asset::Lod::Low;
    return asset::Lod::VeryLow;
}

} // namespace

struct BangerSet::Active {
    phys::Body body;
    int instance = -1;
    std::uint64_t serial = 0;
    float age = 0.0f;
    fx::ParticleSystem particles;
    fx::BirthRule rule;
    Mat34 particleMatrix;
    int texNumber = 0;
    bool inWorld = false;
};

BangerSet::BangerSet(const BangerDataLibrary& data) : m_data(data) {
    for (int i = 0; i < kMaxActive; ++i) {
        auto a = std::make_unique<Active>();
        a->particles.init(64, 2, 2); // mmBangerActive: Particles.Init(64, 2, 2, ...)
        a->particles.rng().seed(0xBA9u + static_cast<std::uint32_t>(i));
        m_active.push_back(std::move(a));
    }
}

BangerSet::~BangerSet() { setWorld(nullptr); }

BangerSet::CellKey BangerSet::cellOf(const Vec3& p) const {
    const auto x = static_cast<std::int64_t>(std::floor(p.x / kCell));
    const auto z = static_cast<std::int64_t>(std::floor(p.z / kCell));
    return (x << 32) ^ (z & 0xFFFFFFFF);
}

void BangerSet::insertCell(std::size_t i) {
    const CellKey k = cellOf(m_instances[i].matrix.m3);
    m_grid[k].push_back(i);
    m_cellOfInstance[i] = k;
}

void BangerSet::removeCell(std::size_t i) {
    auto it = m_grid.find(m_cellOfInstance[i]);
    if (it != m_grid.end())
        std::erase(it->second, i);
}

void BangerSet::add(const std::vector<PlacedProp>& props) {
    for (const auto& p : props) {
        const BangerData* d = m_data.find(p.model);
        if (!d) {
            ++m_skipped;
            continue;
        }
        Instance inst;
        inst.data = d;
        inst.model = p.model;
        inst.ground = p.transform;
        inst.room = p.room;
        // The banger's frame sits at its CG (mmUnhitBangerInstance::Init: m3 = cg ^ matrix).
        inst.matrix = p.transform;
        inst.matrix.m3 = p.transform.transform(d->cg);
        m_instances.push_back(std::move(inst));
        m_initial.push_back(m_instances.back().matrix);
        m_cellOfInstance.push_back(0);
        insertCell(m_instances.size() - 1);
    }
}

void BangerSet::setWorld(phys::World* world) {
    for (auto& a : m_active) {
        if (a->inWorld && m_world)
            m_world->remove(&a->body);
        a->inWorld = false;
    }
    m_world = world;
    for (auto& a : m_active) {
        if (a->instance >= 0 && m_world) {
            m_world->add(&a->body);
            a->inWorld = true;
        }
    }
}

void BangerSet::reset() {
    for (int i = 0; i < kMaxActive; ++i)
        if (m_active[static_cast<std::size_t>(i)]->instance >= 0)
            deactivate(i, State::Unhit);
    // Parts created by splits are dropped; whole props return home.
    std::size_t keep = m_initial.size();
    m_instances.resize(keep);
    m_cellOfInstance.resize(keep);
    m_grid.clear();
    for (std::size_t i = 0; i < m_instances.size(); ++i) {
        m_instances[i].state = State::Unhit;
        m_instances[i].active = -1;
        m_instances[i].matrix = m_initial[i];
        insertCell(i);
    }
}

phys::Obb BangerSet::obbOf(const Instance& inst) const {
    phys::Obb o;
    o.center = inst.matrix.m3;
    o.axis[0] = inst.matrix.m0;
    o.axis[1] = inst.matrix.m1;
    o.axis[2] = inst.matrix.m2;
    o.half = inst.data->size * 0.5f;
    return o;
}

int BangerSet::activeCount() const {
    return static_cast<int>(std::ranges::count_if(m_active, [](const auto& a) { return a->instance >= 0; }));
}

int BangerSet::hitCount() const {
    return static_cast<int>(std::ranges::count_if(m_instances, [](const Instance& i) { return i.state == State::Hit; }));
}

int BangerSet::activate(std::size_t i, const Vec3& impulse, const Vec3& point, const Vec3& carryVelocity) {
    // Free slot, else replace the longest-active one (mmBangerActiveManager::Attach).
    int slot = -1;
    std::uint64_t oldest = ~0ull;
    for (int k = 0; k < kMaxActive; ++k) {
        const Active& a = *m_active[static_cast<std::size_t>(k)];
        if (a.instance < 0) {
            slot = k;
            break;
        }
        if (a.serial < oldest) {
            oldest = a.serial;
            slot = k;
        }
    }
    if (m_active[static_cast<std::size_t>(slot)]->instance >= 0)
        deactivate(slot, State::Hit);

    Instance& inst = m_instances[i];
    const BangerData& d = *inst.data;
    Active& a = *m_active[static_cast<std::size_t>(slot)];
    a.instance = static_cast<int>(i);
    a.serial = ++m_activationSerial;
    a.age = 0.0f;
    a.body = phys::Body{};
    a.body.shape.kind = phys::Shape::Kind::Box;
    a.body.shape.half = d.size * 0.5f;
    a.body.ics.setMass(d.size.x, d.size.y, d.size.z, d.mass);
    a.body.ics.place(inst.matrix);
    a.body.ics.elasticity = d.elasticity;
    a.body.ics.friction = d.friction;
    // mmBangerActive constructor: limited angular velocity, quick sleep.
    a.body.ics.limitAngVelocity = true;
    a.body.ics.vel2 = 0.5f;
    a.body.ics.angVel2 = 0.5f;
    a.body.ics.time = 0.5f;
    a.body.ics.state = phys::InertialCS::Awake;
    if (impulse.mag2() > 0.0f)
        a.body.ics.applyImpulseNow(impulse, point);
    if (m_world) {
        m_world->add(&a.body);
        a.inWorld = true;
    }
    removeCell(i);
    inst.state = State::Active;
    inst.active = slot;

    // Debris particles (mmBangerActive::Attach).
    a.particles.reset();
    a.texNumber = 0;
    if (d.texNumber > 0 && d.birthRule) {
        a.texNumber = d.texNumber;
        a.rule = *d.birthRule;
        a.particles.setBirthRule(&a.rule);
        if (a.rule.birthFlags & fx::BirthRule::kStationary) {
            a.rule.position = inst.matrix.m3;
            a.particles.setMatrix(nullptr);
        } else {
            a.particleMatrix = inst.matrix;
            a.particles.setMatrix(&a.particleMatrix);
            a.rule.velocity += carryVelocity;
        }
        a.particles.blast(static_cast<int>(static_cast<float>(a.rule.initialBlast) * particleMultiplier));
        if (!(a.rule.birthFlags & fx::BirthRule::kStationary))
            a.rule.velocity -= carryVelocity;
    }
    return slot;
}

void BangerSet::deactivate(int k, State newState) {
    Active& a = *m_active[static_cast<std::size_t>(k)];
    if (a.instance < 0)
        return;
    if (a.inWorld && m_world)
        m_world->remove(&a.body);
    a.inWorld = false;
    const auto i = static_cast<std::size_t>(a.instance);
    Instance& inst = m_instances[i];
    inst.active = -1;
    inst.state = newState;
    if (newState == State::Hit || newState == State::Unhit)
        insertCell(i);
    a.instance = -1;
}

void BangerSet::impact(std::size_t i, phys::Body& vehicle, const Vec3& point, const Vec3& n) {
    Instance& inst = m_instances[i];
    if (inst.state == State::Gone || inst.state == State::Active)
        return;
    const BangerData& d = *inst.data;
    // Closing speed along the direction into the prop.
    const Vec3 into = -n;
    const Vec3 vVehicle = vehicle.ics.getVelocity(&point);
    const float closing = std::max(0.0f, vVehicle.dot(into));
    const float mv = vehicle.ics.mass, mb = d.mass;
    const float e = d.elasticity;
    float j = (1.0f + e) * closing * (mb * mv / (mb + mv));
    if (d.impulseLimit2 > 0.0f)
        j = std::min(j, std::sqrt(d.impulseLimit2)); // asBound::Impact impulse limit
    const Vec3 impulse = into * j;

    // The vehicle takes the opposite impulse (accumulated for its next step).
    if (j > 0.0f) {
        vehicle.ics.applyImpulse(-impulse, point);
        if (vehicle.ics.state == phys::InertialCS::Asleep)
            vehicle.ics.state = phys::InertialCS::Awake;
    }
    const Vec3 carry = vehicle.ics.linearVelocity;

    if (d.numParts > 0 && inst.part < 0) {
        // Split into the BREAKnn parts (mmUnhitBangerInstance::Impact).
        // Copy what is needed: adding parts may reallocate m_instances.
        const Mat34 frame = inst.matrix;
        const std::string model = inst.model;
        const int room = inst.room;
        const Vec3 groundOrigin = frame.m3 - frame.transformDir(d.cg);
        int made = 0;
        for (int p = 0; p < d.numParts; ++p) {
            const BangerData* pd = m_data.part(model, p);
            if (!pd)
                continue;
            Instance part;
            part.data = pd;
            part.model = model;
            part.part = p;
            part.ground = frame;
            part.ground.m3 = groundOrigin;
            part.matrix = frame;
            part.matrix.m3 = groundOrigin + frame.transformDir(pd->cg);
            part.room = room;
            m_instances.push_back(part);
            m_cellOfInstance.push_back(0);
            const std::size_t idx = m_instances.size() - 1;
            insertCell(idx);
            activate(idx, impulse * (pd->mass / d.mass), point, carry);
            ++made;
        }
        if (made > 0) {
            removeCell(i);
            m_instances[i].state = State::Gone; // `inst` may dangle after push_back
            return;
        }
    }
    activate(i, impulse, point, carry);
}

void BangerSet::update(float dt, std::span<phys::Body* const> vehicles) {
    // Touches: vehicle boxes against nearby unhit/settled props.
    phys::Contact contacts[phys::kMaxContacts];
    for (phys::Body* v : vehicles) {
        if (!v)
            continue;
        Aabb box = v->aabb();
        box.min -= Vec3{0.3f, 0.3f, 0.3f};
        box.max += Vec3{0.3f, 0.3f, 0.3f};
        const phys::Obb vo = v->obb();
        const auto x0 = static_cast<std::int64_t>(std::floor(box.min.x / kCell)) - 1;
        const auto x1 = static_cast<std::int64_t>(std::floor(box.max.x / kCell)) + 1;
        const auto z0 = static_cast<std::int64_t>(std::floor(box.min.z / kCell)) - 1;
        const auto z1 = static_cast<std::int64_t>(std::floor(box.max.z / kCell)) + 1;
        std::vector<std::size_t> candidates;
        for (auto x = x0; x <= x1; ++x)
            for (auto z = z0; z <= z1; ++z)
                if (auto it = m_grid.find((x << 32) ^ (z & 0xFFFFFFFF)); it != m_grid.end())
                    candidates.insert(candidates.end(), it->second.begin(), it->second.end());
        for (std::size_t i : candidates) {
            if (i >= m_instances.size())
                continue;
            const Instance& inst = m_instances[i];
            if (inst.state != State::Unhit && inst.state != State::Hit)
                continue;
            const phys::Obb bo = obbOf(inst);
            const Aabb bb = bo.aabb();
            if (bb.max.x < box.min.x || bb.min.x > box.max.x || bb.max.y < box.min.y || bb.min.y > box.max.y ||
                bb.max.z < box.min.z || bb.min.z > box.max.z)
                continue;
            const int n = phys::collideObbObb(vo, bo, contacts, phys::kMaxContacts);
            if (n > 0)
                impact(i, *v, contacts[0].point, contacts[0].normal);
        }
    }

    // Active props: follow their bodies, settle, age particles.
    for (int k = 0; k < kMaxActive; ++k) {
        Active& a = *m_active[static_cast<std::size_t>(k)];
        if (a.instance < 0)
            continue;
        Instance& inst = m_instances[static_cast<std::size_t>(a.instance)];
        a.age += dt;
        inst.matrix = a.body.ics.matrix;
        a.particleMatrix = inst.matrix;
        if (a.particles.count() || a.rule.spewRate > 0.0f) {
            // mmBangerActive::Update: spew with the prop's current velocity.
            const Vec3 saved = a.rule.velocity;
            a.rule.velocity = a.body.ics.linearVelocity;
            a.particles.update(dt);
            a.rule.velocity = saved;
        }
        // mmBangerActive::PostUpdate: asleep or fallen out of the world.
        if (inst.matrix.m3.y < -100.0f)
            deactivate(k, State::Gone);
        else if (a.body.ics.state == phys::InertialCS::Asleep && a.particles.count() == 0)
            deactivate(k, State::Hit);
    }
}

void BangerSet::draw(render::Device& device, ModelLibrary& models, TextureLibrary& textures, fx::ParticleRenderer& cards,
                     const Frustum& frustum, const Camera& camera, const DrawParams& params) {
    const Vec3 eye = camera.position();
    const float maxD2 = params.maxDistance * params.maxDistance;
    std::vector<Vec3> glows;
    for (const Instance& inst : m_instances) {
        if (inst.state == State::Gone)
            continue;
        const float d2 = inst.matrix.m3.dist2(eye);
        if (d2 > maxD2)
            continue;
        const float radius = inst.data->size.mag() * 0.5f + 0.5f;
        if (!frustum.intersectsSphere(inst.matrix.m3, radius))
            continue;
        const GpuModel* model = models.get(inst.model);
        if (!model)
            continue;
        const std::string part = inst.part < 0 ? std::string() : std::format("BREAK{:02}", inst.part + 1);
        if (const GpuMesh* mesh = model->find(part, lodForDistance(std::sqrt(d2), params.lodScale)))
            drawGpuMesh(device, textures, *mesh, model->materials(0), Mat44::fromMat34(inst.matrix));
        if (params.night && inst.part < 0)
            for (const Vec3& g : inst.data->glowOffsets)
                glows.push_back(inst.matrix.transform(g));
    }

    // Lamp glows (dgBangerInstance::DrawGlow): additive cards at night. Size
    // and texture inferred.
    if (!glows.empty()) {
        cards.begin();
        for (const Vec3& g : glows) {
            fx::SparkPos p;
            p.position = g;
            p.radius = 1.2f;
            p.color = 0xFFFFFFFFu;
            cards.add(p, 1, 1, camera.transform);
        }
        cards.flush(device, textures.get("fxltglow"), {render::BlendMode::Additive, true, {1, 1, 1, 1}});
    }

    // Debris particles of active props.
    for (const auto& a : m_active) {
        if (a->instance < 0 || !a->particles.count() || a->texNumber <= 0)
            continue;
        const auto sheet = fx::EffectLibrary::bangerSheet(a->texNumber);
        cards.draw(device, camera.transform, a->particles, textures.get(sheet.texture));
    }
}

} // namespace mm2::game::bangers
