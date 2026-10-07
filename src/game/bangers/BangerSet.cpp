#include "game/bangers/BangerSet.h"

#include "core/Log.h"
#include "phys/Collide.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace mm2::game::bangers {
namespace {

constexpr float kCell = 16.0f;
// dgBangerActive's phSleep: spin^2 over 0.5 or speed^2 over 0.1 keeps the
// prop awake unless it reverses from one update to the next (the sum of
// the two stays within 1.8 times the limit: jitter); 15 still updates in a
// row send it to sleep.
constexpr float kSleepVel2 = 0.1f;
constexpr float kSleepAngVel2 = 0.5f;
constexpr int kSleepUpdates = 15;
// cityLevel::DrawRooms' alpha test: GREATER than 100 (bangers flagged unlit
// use 140, trees 120). The shader keeps alpha >= ref.
constexpr float kAlphaRef = 101.0f / 255.0f;
constexpr float kUnlitAlphaRef = 141.0f / 255.0f;
constexpr float kTreeAlphaRef = 121.0f / 255.0f;

// phInertialCS::SmoothAngInertia(40): no principal moment below max / 40.
void smoothInertia(phys::InertialCS& ics, float ratio) {
    const float limit = std::max({ics.inertia.x, ics.inertia.y, ics.inertia.z}) / ratio;
    ics.inertia = {std::max(ics.inertia.x, limit), std::max(ics.inertia.y, limit), std::max(ics.inertia.z, limit)};
    ics.invInertia = {1.0f / ics.inertia.x, 1.0f / ics.inertia.y, 1.0f / ics.inertia.z};
}

} // namespace

struct BangerSet::Active {
    phys::Body body;
    int instance = -1;
    Vec3 lastSpin, lastVelocity; // phSleep's jitter sums
    int stillUpdates = 0;
    fx::ParticleSystem particles;
    fx::BirthRule rule;
    Mat34 particleMatrix;
    int texNumber = 0;
    bool inWorld = false;
};

BangerSet::BangerSet(const BangerDataLibrary& data) : m_data(data) {
    for (int i = 0; i < kMaxActive; ++i) {
        auto a = std::make_unique<Active>();
        a->particles.init(64, 2, 2); // dgBangerActive: asParticles::Init(64, 2, 2)
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
        if (!p.fullMatrix) {
            // dgUnhitYBangerInstance keeps only the X row's x and z: rows
            // (c, 0, s), (0, 1, 0), (-s, 0, c), not renormalised.
            const float c = p.transform.m0.x, s = p.transform.m0.z;
            inst.ground.m0 = {c, 0.0f, s};
            inst.ground.m1 = {0.0f, 1.0f, 0.0f};
            inst.ground.m2 = {-s, 0.0f, c};
        }
        inst.room = p.room;
        // The banger's frame sits at its CG (dgUnhitBangerInstance::Init).
        inst.matrix = inst.ground;
        inst.matrix.m3 = inst.ground.transform(d->cg);
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
    // lvlLevel::ResetInstances: every prop back in place, hit instances and
    // actives released.
    while (!m_activeList.empty())
        detach(m_activeList.front());
    const std::size_t keep = m_initial.size();
    m_instances.resize(keep);
    m_cellOfInstance.resize(keep);
    m_grid.clear();
    m_ring.clear();
    m_ringNext = 0;
    for (std::size_t i = 0; i < m_instances.size(); ++i) {
        m_instances[i].state = State::Unhit;
        m_instances[i].everHit = false;
        m_instances[i].active = -1;
        m_instances[i].matrix = m_initial[i];
        insertCell(i);
    }
    m_ticker.reset();
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

int BangerSet::activeCount() const { return static_cast<int>(m_activeList.size()); }

int BangerSet::hitCount() const {
    return static_cast<int>(std::ranges::count_if(m_instances, [](const Instance& i) { return i.state == State::Hit; }));
}

// dgBangerManager::GetBanger: knocked-over props live in a ring of 40; the
// slot's previous prop disappears.
void BangerSet::takeRingSlot(std::size_t i) {
    if (m_ring.size() < static_cast<std::size_t>(kMaxHit)) {
        m_ring.push_back(i);
        return;
    }
    const std::size_t old = m_ring[m_ringNext];
    if (old < m_instances.size() && old != i) {
        Instance& o = m_instances[old];
        if (o.active >= 0)
            detach(o.active);
        if (o.state == State::Hit)
            removeCell(old);
        o.state = State::Gone;
    }
    m_ring[m_ringNext] = i;
    m_ringNext = (m_ringNext + 1) % static_cast<std::size_t>(kMaxHit);
}

// dgBangerActiveManager::Attach + dgBangerActive::Attach.
int BangerSet::attach(std::size_t i, bool wasUnhit) {
    if (static_cast<int>(m_activeList.size()) >= kMaxActive)
        detach(m_activeList.front()); // the manager reuses the first of its list
    int slot = 0;
    while (m_active[static_cast<std::size_t>(slot)]->instance >= 0)
        ++slot;

    Instance& inst = m_instances[i];
    const BangerData& d = *inst.data;
    Active& a = *m_active[static_cast<std::size_t>(slot)];
    a.instance = static_cast<int>(i);
    a.body = phys::Body{};
    a.body.shape.kind = phys::Shape::Kind::Box;
    a.body.shape.half = d.size * 0.5f;
    a.body.ics.setMass(d.size.x, d.size.y, d.size.z, d.mass);
    smoothInertia(a.body.ics, 40.0f);
    a.body.ics.place(inst.matrix);
    a.body.ics.elasticity = d.elasticity;
    a.body.ics.friction = d.friction;
    a.body.ics.limitAngVelocity = false;
    a.body.ics.vel2 = -1.0f; // the sleep test below replaces the body's own
    a.body.ics.state = phys::InertialCS::Awake;
    a.lastSpin = a.lastVelocity = {};
    a.stillUpdates = 0;
    if (m_world) {
        m_world->add(&a.body);
        a.inWorld = true;
    }
    removeCell(i);
    inst.state = State::Active;
    inst.active = slot;
    m_activeList.push_back(slot);

    // Debris (dgBangerActive::Attach): InitialBlast particles of the prop's
    // rule from fxpt<TexNumber>, born in the prop's frame; stationary rules
    // are born at the prop's CG in world space, and only from a prop that
    // was still standing.
    a.particles.reset();
    a.texNumber = 0;
    if (d.texNumber > 0 && d.birthRule) {
        a.texNumber = d.texNumber;
        a.rule = *d.birthRule;
        a.particles.setBirthRule(&a.rule);
        if (a.rule.birthFlags & fx::BirthRule::kStationary) {
            a.rule.position = inst.matrix.m3;
            a.particles.setMatrix(nullptr);
            if (wasUnhit)
                a.particles.blast(a.rule.initialBlast);
        } else {
            a.particleMatrix = inst.matrix;
            a.particles.setMatrix(&a.particleMatrix);
            a.particles.blast(a.rule.initialBlast);
        }
    }
    return slot;
}

// dgBangerActive::Detach: the prop stays where it is; its particles go.
void BangerSet::detach(int k) {
    Active& a = *m_active[static_cast<std::size_t>(k)];
    if (a.instance < 0)
        return;
    if (a.inWorld && m_world)
        m_world->remove(&a.body);
    a.inWorld = false;
    a.particles.reset();
    const auto i = static_cast<std::size_t>(a.instance);
    Instance& inst = m_instances[i];
    inst.active = -1;
    inst.state = inst.everHit ? State::Hit : State::Unhit;
    insertCell(i);
    a.instance = -1;
    std::erase(m_activeList, k);
}

void BangerSet::impact(std::size_t i, phys::Body& vehicle, const Vec3& point, const Vec3& n) {
    Instance& inst = m_instances[i];
    if (inst.state == State::Gone || inst.state == State::Active)
        return;
    const BangerData& d = *inst.data;
    // dgImpact::CalcImpact: nothing while the contact separates.
    const Vec3 vVehicle = vehicle.ics.getVelocity(&point);
    if (vVehicle.dot(n) > 0.01f)
        return;
    const Vec3 into = -n;
    const float closing = std::max(0.0f, vVehicle.dot(into));
    const float mCar = vehicle.ics.effectiveMass(into, point);
    const float e = d.elasticity;
    auto pushCar = [&](float j) {
        if (j <= 0.0f)
            return;
        vehicle.ics.applyImpulse(-into * j, point);
        if (vehicle.ics.state == phys::InertialCS::Asleep)
            vehicle.ics.state = phys::InertialCS::Awake;
    };
    // The impulse that stops the car's contact point against an immovable prop.
    const float stop = closing * mCar;
    float residual = closing;
    if (inst.state == State::Unhit) {
        if (stop * stop <= d.impulseLimit2) {
            // Too gentle to break it: the prop holds like a wall.
            pushCar((1.0f + e) * stop);
            return;
        }
        // The car spends sqrt(ImpulseLimit2) breaking it loose.
        const float limit = std::sqrt(d.impulseLimit2);
        pushCar(limit);
        residual = closing - limit / mCar;
    }
    // A normal collision between the car and the loose prop.
    const float j2 = residual > 0.0f ? (1.0f + e) * residual / (1.0f / mCar + 1.0f / d.mass) : 0.0f;
    pushCar(j2);
    const bool wasUnhit = !inst.everHit;

    if (d.numParts > 0 && inst.part < 0) {
        // dgUnhitBangerInstance::Impact with parts: each BREAKnn part flies
        // with the parent's velocity change at its own CG.
        const Mat34 frame = inst.matrix;
        const std::string model = inst.model;
        const int room = inst.room;
        const Vec3 groundOrigin = frame.m3 - frame.transformDir(d.cg);
        phys::InertialCS parent;
        parent.setMass(d.size.x, d.size.y, d.size.z, d.mass);
        smoothInertia(parent, 40.0f);
        parent.place(frame);
        const Vec3 dv = into * (j2 / d.mass);
        const Vec3 dw = parent.invInertiaWorld((point - frame.m3).cross(into * j2));
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
            part.everHit = true;
            m_instances.push_back(part);
            m_cellOfInstance.push_back(0);
            const std::size_t idx = m_instances.size() - 1;
            insertCell(idx);
            takeRingSlot(idx);
            const int slot = attach(idx, wasUnhit);
            auto& ics = m_active[static_cast<std::size_t>(slot)]->body.ics;
            const Vec3 r = m_instances[idx].matrix.m3 - frame.m3;
            ics.applyImpulseNow((dv + dw.cross(r)) * pd->mass, m_instances[idx].matrix.m3);
            ics.applyAngImpulse(ics.worldInertia().transformDir(dw));
            ++made;
        }
        if (made > 0) {
            Instance& parentInst = m_instances[i]; // `inst` may dangle after push_back
            removeCell(i);
            parentInst.state = State::Gone;
            return;
        }
    }
    if (!inst.everHit) {
        inst.everHit = true;
        takeRingSlot(i);
    }
    const int slot = attach(i, wasUnhit);
    if (j2 > 0.0f)
        m_active[static_cast<std::size_t>(slot)]->body.ics.applyImpulseNow(into * j2, point);
}

void BangerSet::update(float dt, std::span<phys::Body* const> vehicles) {
    // Touches: vehicle boxes against nearby standing or resting props.
    phys::Contact contacts[phys::kMaxContacts];
    std::vector<std::size_t> candidates;
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
        candidates.clear();
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

    // Actives follow their bodies; asleep or fallen out of the world they
    // detach (dgBangerActiveManager::Update).
    for (std::size_t n = m_activeList.size(); n-- > 0;) {
        const int k = m_activeList[n];
        Active& a = *m_active[static_cast<std::size_t>(k)];
        Instance& inst = m_instances[static_cast<std::size_t>(a.instance)];
        inst.matrix = a.body.ics.matrix;
        a.particleMatrix = inst.matrix;
        // phSleep::Update. MM2 adds the sample's positional pushes to the
        // velocity; OpenMM2's contact pushes hold a resting prop at about
        // 0.33 m/s that way, so the plain velocity is tested.
        const Vec3 spin = a.body.ics.angularVelocity, velocity = a.body.ics.linearVelocity;
        bool moving = spin.mag2() > kSleepAngVel2 && (spin + a.lastSpin).mag2() > kSleepAngVel2 * 1.8f;
        if (!moving)
            moving = velocity.mag2() > kSleepVel2 && (velocity + a.lastVelocity).mag2() > kSleepVel2 * 1.8f;
        a.lastSpin = spin;
        a.lastVelocity = velocity;
        a.stillUpdates = moving ? 0 : a.stillUpdates + 1;
        if (inst.matrix.m3.y < -100.0f || a.stillUpdates >= kSleepUpdates)
            detach(k);
    }
    // Debris at the original's per-frame rate.
    for (int steps = m_ticker.advance(dt); steps > 0; --steps)
        for (const int k : m_activeList)
            m_active[static_cast<std::size_t>(k)]->particles.update(fx::FixedTicker::kStep);
}

void BangerSet::draw(render::Device& device, ModelLibrary& models, TextureLibrary& textures, fx::ParticleRenderer& cards,
                     const Frustum& frustum, const Camera& camera, const DrawParams& params) {
    const Mat34& cam = camera.transform;
    std::vector<Vec3> glows;
    std::vector<std::pair<const Instance*, const GpuMesh*>> trees;
    for (const Instance& inst : m_instances) {
        if (inst.state == State::Gone)
            continue;
        const GpuModel* model = models.get(inst.model);
        if (!model)
            continue;
        const float radius = (model->bounds.max - model->bounds.min).mag() * 0.5f;
        // lvlInstance::IsVisible with the dynamic objects' NoDraw limit.
        const auto lod = objectLod(viewDepth(cam, inst.matrix.m3), radius, params.detail, params.detail.noDraw);
        if (!lod || !frustum.intersectsSphere(inst.matrix.m3, radius))
            continue;
        // Lamp glows of props still standing (dgBangerInstance::DrawGlow).
        if (params.glows && inst.state == State::Unhit)
            for (const Vec3& g : inst.data->glowOffsets)
                glows.push_back(inst.matrix.transform(g));
        const std::string part = inst.part < 0 ? std::string() : std::format("BREAK{:02}", inst.part + 1);
        const bool tree = inst.data->billFlags & BangerData::kTree;
        // Trees always draw their high LOD, after the other props.
        const GpuMesh* mesh = model->find(part, tree ? asset::Lod::High : *lod);
        if (!mesh)
            continue;
        if (tree) {
            trees.emplace_back(&inst, mesh);
            continue;
        }
        MeshDrawOptions options;
        options.alphaRef = kAlphaRef;
        if (inst.data->billFlags & BangerData::kUnlit) {
            options.lighting = false;
            options.alphaRef = kUnlitAlphaRef;
        }
        drawGpuMesh(device, textures, *mesh, model->materials(0), Mat44::fromMat34(inst.matrix), options);
    }
    // dgTreeRenderer::RenderTrees: unlit, alpha reference 120.
    for (const auto& [inst, mesh] : trees) {
        MeshDrawOptions options;
        options.lighting = false;
        options.alphaRef = kTreeAlphaRef;
        drawGpuMesh(device, textures, *mesh, models.get(inst->model)->materials(0), Mat44::fromMat34(inst->matrix),
                    options);
    }

    // Lamp glows: s_yel_glow cards of half size 1.5 m with a 1% flicker,
    // white, added, unfogged, without depth writes.
    if (!glows.empty()) {
        cards.begin();
        for (const Vec3& g : glows) {
            fx::SparkPos p;
            p.position = g;
            p.radius = (m_glowRand.frand() * 0.01f + 0.99f) * 1.5f;
            p.color = 0xFFFFFFFFu;
            cards.add(p, 1, 1, cam);
        }
        cards.flush(device, textures.get("s_yel_glow"), {render::BlendMode::Add, false, {1, 1, 1, 1}});
    }

    // Debris particles of active props.
    for (const int k : m_activeList) {
        const Active& a = *m_active[static_cast<std::size_t>(k)];
        if (!a.particles.count() || a.texNumber <= 0)
            continue;
        const auto sheet = fx::EffectLibrary::bangerSheet(a.texNumber);
        cards.draw(device, cam, a.particles, textures.get(sheet.texture));
    }
}

} // namespace mm2::game::bangers
