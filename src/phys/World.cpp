// dgPhysManager (Update, GatherCollidables, TrivialCollideInstances,
// CollideTerrain, CollideInstances, NewMover) from the code of midtown2.exe
// build 3393 (MM2Recomp). See docs/physics.md, "How a sample runs" and
// "Collision".

#include "phys/World.h"

#include <algorithm>
#include <cmath>

namespace mm2::phys {
namespace {

// dgPhysManager::GatherCollidables keeps at most 32 per mover.
constexpr std::size_t kMaxCollidables = 32;
// cityLevel::GetTouchedNeighbors' table size in GatherCollidables and
// CollideTerrain.
constexpr int kMaxNeighbors = 8;

} // namespace

bool FlatGround::probe(const Vec3& a, const Vec3& b, RayHit& hit) const {
    const float da = a.y - m_height, db = b.y - m_height;
    if ((da > 0 && db > 0) || (da < 0 && db < 0) || da == db)
        return false;
    hit.t = da / (da - db);
    hit.position = a + (b - a) * hit.t;
    hit.normal = {0, 1, 0};
    hit.material = 0;
    hit.polygon = -1;
    return true;
}

// --- Body ------------------------------------------------------------------------------------

Body::Body() {
    boundMatrix = Mat34::identity();
    resetCollider();
}

void Body::place(const Mat34& icsMatrix) {
    ics.place(icsMatrix);
    syncBoundMatrix();
    resetCollider();
}

void Body::syncBoundMatrix() {
    // vehCarSim::SetWorldMatrix (and the plain bodies' equivalent): the
    // bound's origin at boundOrigin in the ICS frame.
    const Mat34& m = ics.matrix;
    boundMatrix.m0 = m.m0;
    boundMatrix.m1 = m.m1;
    boundMatrix.m2 = m.m2;
    boundMatrix.m3 = m.transform(boundOrigin);
}

void Body::resetCollider() {
    ImpactHandler* handler = collider.handler;
    collider.init(collisionBound, &boundMatrix, kinematic ? nullptr : &ics);
    collider.handler = handler;
    collider.joint = joint;
    collider.id = audioId;
    collider.body = this;
    collider.setKey(this);
}

const Bound* Body::bound(int which) const {
    if (which == 1 && terrainBound)
        return terrainBound;
    return collisionBound;
}

float Body::radius() const {
    if (!collisionBound)
        return 0.0f;
    const Vec3& c = collisionBound->centroid;
    return std::sqrt(c.x * c.x + c.y * c.y + c.z * c.z) + collisionBound->radius;
}

Aabb Body::aabb() const {
    Aabb box;
    if (!collisionBound) {
        box.expand(boundMatrix.m3);
        return box;
    }
    const Vec3& lo = collisionBound->boxMin;
    const Vec3& hi = collisionBound->boxMax;
    for (int i = 0; i < 8; ++i)
        box.expand(boundMatrix.transform({i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z}));
    return box;
}

// --- World -----------------------------------------------------------------------------------

World::World() {
    m_identity = Mat34::identity();
    m_levelBound.clear();
    m_levelCollider.init(&m_levelBound, &m_identity, nullptr);
    m_isectsA.resize(kMaxIntersections);
    m_isectsB.resize(kMaxIntersections);
    m_impacts.resize(kMaxImpacts);
}

World::World(MaterialTable materials) : World() {
    m_materials = std::move(materials);
}

void World::setLevel(const Level* level) {
    m_level = level;
}

void World::add(Body* body) {
    for (Mover& m : m_movers) {
        if (m.body == body) {
            m.removed = false;
            return;
        }
    }
    const InertialCS defaults;
    if (body->ics.gravity == defaults.gravity)
        body->ics.gravity = {0.0f, -kGravity, 0.0f};
    Mover m;
    m.body = body;
    m_movers.push_back(std::move(m));
}

void World::addNewMover(Body* body) {
    // dgPhysManager::NewMover: flag 0x100 until the end of the sample.
    for (Mover& m : m_movers) {
        if (m.body == body) {
            if (m.removed) {
                m.removed = false;
                m.fresh = true;
            }
            return;
        }
    }
    add(body);
    m_movers.back().fresh = true;
}

void World::remove(Body* body) {
    if (m_stepping) {
        for (Mover& m : m_movers)
            if (m.body == body)
                m.removed = true;
        return;
    }
    std::erase_if(m_movers, [&](const Mover& m) { return m.body == body; });
}

bool World::contains(const Body* body) const {
    return std::ranges::any_of(m_movers, [&](const Mover& m) { return m.body == body && !m.removed; });
}

bool World::probe(const Vec3& a, const Vec3& b, RayHit& hit) const {
    return m_static.raycast(a, b, hit);
}

void World::beginFrame() {
    // dgPhysManager::Update clears the "hit by the player" marks of its
    // movers once per frame, before the samples.
    for (Mover& m : m_movers)
        if (live(m))
            m.body->hitByPlayer = false;
}

int World::advanceFixed(float frameDelta, float sampleStep, int maxSamples) {
    beginFrame();
    m_accumulator += frameDelta;
    int n = 0;
    while (m_accumulator >= sampleStep && n < maxSamples) {
        step(sampleStep);
        m_accumulator -= sampleStep;
        ++n;
    }
    if (n == maxSamples && m_accumulator > sampleStep)
        m_accumulator = 0; // drop time rather than spiral
    return n;
}

int World::advanceOversampled(float frameDelta, float sampleStep, int maxSamples) {
    // dgPhysManager::Update. (At least one sample: OpenMM2 guard for frames
    // under a millisecond.)
    beginFrame();
    int n = static_cast<int>(std::ceil(static_cast<double>((frameDelta - 0.001f) / sampleStep)));
    n = std::max(1, std::min(n, maxSamples));
    const float dt = frameDelta / static_cast<float>(n);
    for (int i = 0; i < n; ++i)
        step(dt);
    return n;
}

void World::step(float dt) {
    if (dt <= 0)
        return;
    const float invDt = 1.0f / dt;
    // datTimeManager::SetTempOverSampling: Seconds is the sample's length.
    sampleTime() = {dt, invDt};
    m_stats = {};
    m_stepping = true;

    // The movers' own updates (dgPhysEntity::Update and the entity's
    // Update: vehCar::Update for cars).
    const std::size_t count = m_movers.size();
    for (std::size_t i = 0; i < count; ++i) {
        Body* b = m_movers[i].body;
        if (live(m_movers[i]) && b->controller)
            b->controller->beforeIntegrate(*b, dt, *this);
    }
    for (std::size_t i = 0; i < count; ++i) {
        Body* b = m_movers[i].body;
        if (!live(m_movers[i]))
            continue;
        if (!b->kinematic)
            b->ics.update(dt, invDt);
        if (!b->kinematic)
            b->syncBoundMatrix();
    }
    for (std::size_t i = 0; i < count; ++i) {
        Body* b = m_movers[i].body;
        if (!live(m_movers[i]))
            continue;
        if (b->controller)
            b->controller->afterIntegrate(*b, dt, *this);
        // lvlLevel::MoveToRoom after the entity's update (vehCar::Update,
        // aiVehicleActive::Update).
        if (m_level)
            b->room = m_level->findRoom(b->position(), b->room);
        b->collider.joint = b->joint;
        b->collider.id = b->audioId;
        b->collider.calcMaxMoved(dt);
    }

    // dgPhysManager::GatherCollidables for movers that collide with the city
    // or its objects.
    for (Mover& m : m_movers) {
        m.collidables.clear();
        if (live(m) && !m.fresh && m.body->collisionBound && (m.body->collideTerrain || m.body->collideInstances))
            gatherCollidables(m);
    }

    // Collisions, mover by mover: the city, the movers after it, then the
    // gathered instances. Movers that collisions set in motion are appended
    // (fresh) and wait for the next sample.
    for (std::size_t i = 0; i < m_movers.size(); ++i) {
        if (!live(m_movers[i]) || m_movers[i].fresh || !m_movers[i].body->collisionBound)
            continue;
        Body* a = m_movers[i].body;
        if (a->collideTerrain)
            collideTerrain(*a);
        if (a->collideMovers) {
            for (std::size_t j = i + 1; j < m_movers.size(); ++j) {
                Body* b = m_movers[j].body;
                if (!live(m_movers[j]) || m_movers[j].fresh || !b->collideMovers || !b->collisionBound)
                    continue;
                // Colliders sharing an unbroken joint (a tractor and its
                // trailer) do not collide.
                if (a->joint && !a->joint->isBroken() && a->joint == b->joint)
                    continue;
                if (trivialCollide(*a, *b))
                    collideInstances(*a, *b);
            }
        }
        if (a->collideTerrain || a->collideInstances) {
            // The list may not survive the calls (new movers reallocate).
            std::vector<Instance*> list = m_movers[i].collidables;
            for (Instance* c : list)
                if (live(m_movers[i]))
                    collideInstances(*m_movers[i].body, *c);
        }
    }

    // New movers take part from now on (0x100 -> 0x1b).
    for (Mover& m : m_movers)
        m.fresh = false;
    // phColliderBase::UpdateMtx: the pending pushes move the bodies.
    for (Mover& m : m_movers)
        if (live(m))
            m.body->collider.updateMtx();
    for (std::size_t i = 0; i < m_movers.size(); ++i) {
        Body* b = m_movers[i].body;
        if (live(m_movers[i]) && b->controller)
            b->controller->afterCollisions(*b, dt, *this);
    }
    m_stepping = false;
    std::erase_if(m_movers, [](const Mover& m) { return m.removed; });
    m_time += dt;
}

bool World::trivialCollide(const Instance& a, const Instance& b) const {
    // dgPhysManager::TrivialCollideInstances: bounding spheres. A banger
    // with a YRadius uses the point under its centre of gravity and that
    // radius, compared in the horizontal plane only (the original copies
    // the other centre's height into it).
    Vec3 ca = a.position();
    Vec3 cb = b.position();
    float ra = a.radius();
    float rb = b.radius();
    Vec3 c;
    float r = 0.0f;
    if (a.isBanger() && a.bangerSphere(c, r)) {
        ra = r;
        ca = {c.x, cb.y, c.z};
    }
    if (b.isBanger() && b.bangerSphere(c, r)) {
        rb = r;
        cb = {c.x, ca.y, c.z};
    }
    const float dx = ca.x - cb.x, dy = ca.y - cb.y, dz = ca.z - cb.z;
    const float sum = rb + ra;
    return dx * dx + dy * dy + dz * dz <= sum * sum;
}

void World::gatherCollidables(Mover& mover) {
    // dgPhysManager::GatherCollidables: the instances of the mover's room
    // and of the neighbours its sphere touches (instances listed in several
    // rooms only from the mover's own), whose spheres touch the mover's.
    Body& self = *mover.body;
    if (!m_level || self.room == 0)
        return;
    int rooms[1 + kMaxNeighbors];
    rooms[0] = self.room;
    const int n = 1 + m_level->touchedNeighbors(rooms + 1, kMaxNeighbors, self.room, self.position(), self.radius());
    for (int k = 0; k < n; ++k) {
        m_roomScratch.clear();
        m_level->instances(rooms[k], m_roomScratch);
        for (Instance* inst : m_roomScratch) {
            if (inst == &self)
                continue;
            const bool wanted = ((self.collideInstances || self.collideMovers) && inst->collidable) ||
                                (self.collideTerrain && inst->terrainCollidable);
            if (!wanted || (k != 0 && inst->multiRoom) || !trivialCollide(self, *inst))
                continue;
            // An instance listed in several of the rooms is gathered once
            // (lvlMultiRoomInstance::IsTerrainCollidable answers once per
            // gather).
            if (std::ranges::find(mover.collidables, inst) != mover.collidables.end())
                continue;
            if (mover.collidables.size() < kMaxCollidables)
                mover.collidables.push_back(inst);
        }
    }
    m_stats.collidables += static_cast<int>(mover.collidables.size());
}

void World::collideTerrain(Body& body) {
    // dgPhysManager::CollideTerrain: the body against the city's polygons
    // in its room and the touched neighbours (lvlSDL::CollidePolyToLevel).
    if (!m_level || body.room == 0)
        return;
    const Bound* bound = body.bound(0);
    if (!bound)
        return;
    Collider& collider = body.collider;
    int rooms[1 + kMaxNeighbors];
    rooms[0] = body.room;
    const Vec3 centre = collider.matrix->m3;
    const int roomCount =
        1 + m_level->touchedNeighbors(rooms + 1, kMaxNeighbors, body.room, centre, bound->radius);
    if (bound->type == BoundType::Hotdog || bound->type == BoundType::Sphere)
        bound = body.bound(1);
    int count = 0;
    switch (bound->type) {
    case BoundType::Geometry:
    case BoundType::Box: {
        const auto& poly = static_cast<const BoundPolygonal&>(*bound);
        // The sweep starts from the last matrix, moved by the last push when
        // the city pushed hardest.
        const Mat34 last = collider.copyLastMatrix(m_levelCollider.key());
        m_level->collect(rooms, roomCount, centre, bound->radius, m_levelBound);
        m_levelBound.level = m_level;
        int found = 0;
        if (collidePolyToLevel(m_levelBound, poly, nullptr, *collider.matrix, last, m_isectsA.data(),
                               kMaxIntersections, found, true) != 0)
            count = findLevelImpacts(m_levelBound, poly, *collider.matrix, last, &m_levelCollider, &collider,
                                     m_isectsA.data(), found, m_impacts.data(), kMaxImpacts);
        break;
    }
    default:
        // Spheres and hotdogs against the city (lvlSDL's sphere and hotdog
        // searches) are not ported: no MM2 mover uses them in a race
        // (inferred: cars, traffic and bangers use polygonal bounds for
        // the city).
        break;
    }
    if (count == 0)
        return;
    m_stats.terrainImpacts += count;
    const float weight = 1.0f / static_cast<float>(count);
    for (int i = 0; i < count; ++i)
        calcImpact(m_impacts[static_cast<std::size_t>(i)], weight);
}

bool World::collideInstances(Instance& a, Instance& b) {
    // dgPhysManager::CollideInstances.
    const Bound* boundA = a.bound(0);
    const Bound* boundB = b.bound(0);
    if (!boundA || !boundB)
        return false;
    m_tempMatrixA = a.matrix();
    m_tempMatrixB = b.matrix();
    const Vec3 relPos = m_tempMatrixB.m3 - m_tempMatrixA.m3;
    Body* entityA = a.entity();
    Body* entityB = b.entity();
    Collider* colA = &m_tempA;
    Collider* colB = &m_tempB;
    if (entityA) {
        colA = &entityA->collider;
    } else {
        m_tempA.init(boundA, &m_tempMatrixA, nullptr);
        m_tempA.id = a.audioId;
    }
    if (entityB) {
        colB = &entityB->collider;
    } else {
        m_tempB.init(boundB, &m_tempMatrixB, nullptr);
        m_tempB.id = b.audioId;
    }
    if (boundB->type == BoundType::ForceSphere)
        return true; // phCollision::TestBoundForce: no force spheres in a race (not ported)

    const int n = testBoundGeneric(*boundA, *colA, *boundB, *colB, m_isectsA.data(), m_isectsB.data(),
                                   m_impacts.data(), kMaxIntersections, kMaxImpacts, relPos);
    if (n == 0)
        return false;
    m_stats.instanceImpacts += n;
    Body* player = entityA && entityA->player ? entityA : nullptr;
    if (player)
        b.hitByPlayer = true;
    else if (entityB && entityB->player)
        a.hitByPlayer = true;

    std::span<Impact> impacts(m_impacts.data(), static_cast<std::size_t>(n));
    bool newA = false;
    if (!entityA) {
        if (!a.terrainCollidable) {
            if (Body* attached = a.attachEntity()) {
                newA = true;
                entityA = attached;
                for (Impact& im : impacts) {
                    if (im.colliderA != &m_tempA) {
                        im.normal = -im.normal;
                        im.colliderB = im.colliderA;
                    }
                    im.colliderA = &attached->collider;
                }
            }
        }
    } else if (entityB && impacts[0].colliderA == colB) {
        for (Impact& im : impacts)
            im.swapColliders();
    }
    bool newB = false;
    if (!entityB && !b.terrainCollidable) {
        if (Body* attached = b.attachEntity()) {
            newB = true;
            entityB = attached;
            for (Impact& im : impacts) {
                if (!newA) {
                    if (im.colliderB != &m_tempB)
                        im.normal = -im.normal;
                    im.colliderA = &entityA->collider;
                }
                im.colliderB = &attached->collider;
            }
        }
    }

    const float weight = 1.0f / static_cast<float>(n);
    if (!b.isBanger()) {
        if (newA)
            addNewMover(entityA);
        if (newB)
            addNewMover(entityB);
        for (Impact& im : impacts)
            calcImpact(im, weight);
        if (newA)
            a.attached();
        if (newB)
            b.attached();
        return true;
    }
    // An unhit banger holds until an impulse beyond its limit breaks it
    // loose (dgImpact::CalcImpact), then dgUnhitBangerInstance::Impact
    // turns it into a body.
    bool broke = false;
    const float limit2 = b.bangerImpulseLimit2();
    for (Impact& im : impacts)
        broke = calcBangerImpact(im, weight, limit2) || broke;
    if (broke)
        b.bangerHit(a, impacts[0].position);
    else if (entityB)
        b.bangerHeld();
    return true;
}

} // namespace mm2::phys
