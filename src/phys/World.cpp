// dgPhysManager (DeclareMover, IgnoreMover, NewMover, Update,
// GatherCollidables, TrivialCollideInstances, CollideTerrain,
// CollideInstances) from the code of midtown2.exe
// build 3393 (MM2Recomp). See docs/physics.md, "How a sample runs" and
// "Collision".

#include "phys/World.h"

#include "phys/AgeMath.h"

#include <algorithm>
#include <cmath>

namespace mm2::phys {
namespace {

// dgPhysManager::GatherCollidables keeps at most 32 per mover.
constexpr std::size_t kMaxCollidables = 32;
// dgPhysManager's mover table and its active room list.
constexpr std::size_t kMaxMovers = 32;
constexpr std::size_t kMaxActiveRooms = 20;
// cityLevel::GetTouchedNeighbors' table size in GatherCollidables and
// CollideTerrain.
constexpr int kMaxNeighbors = 8;

// lvlSDL's room flag the wheel probe reads.
constexpr int kRoomInstance = 0x80;
// lvlRoomInfo's warp flag dgPhysManager::Collide reads (cityLevel::Load
// sets it on rooms 411, 412, 423 and 625 of a city whose name contains
// "sf"; the PSDL's own 0x40 is a different flag).
constexpr int kRoomInfoWarp = 0x40;
// lvlSDL::CollideProbe probes at most 10 instance rooms across the start
// room's perimeter.
constexpr int kMaxProbeNeighbors = 10;
// The wheels' mask (lvlInstance flag 0x20) is Instance::wheelCollidable.

// dgPhysManager::Collide: a segment starting in a warp room also probes the
// instances of the room it leads to (room ids written into the code).
int warpTarget(int room) {
    switch (room) {
    case 411:
        return 102;
    case 412:
        return 122;
    case 423:
        return 96;
    case 625:
        return 1;
    default:
        return 0;
    }
}

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

void Body::declare(int type, unsigned flags) {
    // dgPhysManager::DeclareMover.
    moverType = type;
    updates = (flags & 0x1) != 0;
    collideTerrain = (flags & 0x2) != 0;
    collideInstances = (flags & 0x8) != 0;
    collideMovers = (flags & 0x10) != 0;
    declared = true;
    if (type == 4)
        player = true;
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
    // dgPhysManager::Reset: the level's collider (phCollider::Init with the
    // level's bound and an identity matrix).
    m_levelCollider.initStatic(&m_levelBound, &m_identity);
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
    Mover m;
    m.body = body;
    m.instance = body;
    m_movers.push_back(std::move(m));
}

void World::declareInstance(Instance* instance, int type, unsigned flags) {
    // DeclareMover: an instance already declared this frame ORs the flags.
    for (Mover& m : m_pendingInstances) {
        if (m.instance == instance) {
            m.flags |= flags;
            return;
        }
    }
    (void)type; // type 2: a plain mover (types above 2 make rooms active)
    Mover m;
    m.instance = instance;
    m.transient = true;
    m.flags = flags;
    m_pendingInstances.push_back(std::move(m));
}

void World::addNewMover(Body* body) {
    // dgPhysManager::NewMover: flag 0x100 until the end of the sample. A body
    // already listed starts over as new; a full table (32) takes no more.
    for (Mover& m : m_movers) {
        if (m.body == body) {
            m.removed = false;
            m.fresh = true;
            m.active = true;
            return;
        }
    }
    const auto taken = std::ranges::count_if(m_movers, [this](const Mover& m) { return running(m); });
    if (static_cast<std::size_t>(taken) >= kMaxMovers)
        return;
    add(body);
    m_movers.back().fresh = true;
}

void World::ignoreMover(const Body* body) {
    // dgPhysManager::IgnoreMover: the entry's flags cleared for the frame.
    for (Mover& m : m_movers)
        if (m.body == body)
            m.active = false;
}

bool World::isActive(const Body* body) const {
    return std::ranges::any_of(m_movers, [&](const Mover& m) { return m.body == body && running(m); });
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

bool World::wheelProbe(const Vec3& a, const Vec3& b, RayHit& hit, const Instance* self,
                       ProbeCache* cache) const {
    if (!m_level)
        return probe(a, b, hit);
    // dgPhysManager::Collide without a segment info uses one of its own,
    // with no polygon cache.
    ProbeCache scratch;
    ProbeCache& info = cache ? *cache : scratch;
    // lvlSegment::Set.
    Segment seg;
    seg.kind = Segment::Probe;
    seg.a = a;
    seg.b = b;
    seg.calculateInfo();

    // dgPhysManager::Collide: the rooms of the segment's ends (found from
    // the ones they were in), the level, then the instances of those rooms
    // that carry the mask, as long as they are nearer.
    info.startRoom = m_level->findRoom(a, info.startRoom);
    info.endRoom = m_level->findRoom(b, info.endRoom);
    Intersection isect;
    isect.t = 2.0f;
    isect.poly = nullptr;
    const Material* material = nullptr;
    bool found = collideLevelProbe(seg, isect, info, material);
    const auto probeInstances = [&](int room) {
        m_probeScratch.clear();
        m_level->instances(room, m_probeScratch);
        for (Instance* inst : m_probeScratch)
            if (inst->wheelCollidable && inst != self && collideProbe(seg, *inst, isect, material))
                found = true;
    };
    const int start = info.startRoom;
    probeInstances(start);
    if (info.endRoom != start)
        probeInstances(info.endRoom);
    if (m_level->roomInfoFlags(start) & kRoomInfoWarp)
        if (const int target = warpTarget(start); target != 0)
            probeInstances(target);
    if (!found)
        return false;

    hit.position = isect.position;
    hit.normal = isect.normal;
    hit.t = isect.t;
    hit.polygon = -1;
    // A hit on the cached polygon leaves the wheel's intersection with the
    // material of the probe before.
    hit.material = material ? m_materials.resolve(material->name) : info.material;
    info.material = hit.material;
    return true;
}

bool World::collideLevelProbe(const Segment& seg, Intersection& hit, ProbeCache& cache,
                              const Material*& material) const {
    // lvlSDL::CollideProbe. An end in an instance room makes the cached
    // polygon stale; a cached polygon the segment still crosses (no farther
    // than hit.t) answers alone.
    const int start = cache.startRoom;
    const int end = cache.endRoom;
    if ((m_level->roomFlags(start) & kRoomInstance) || (m_level->roomFlags(end) & kRoomInstance))
        cache.valid = false;
    if (cache.valid) {
        if (cache.polygon.testSegmentUndirected(cache.vertices, seg, hit, hit.t, 2.0f))
            return true;
        cache.valid = false;
    }
    // The rooms' polygons near the segment: the sphere about its midpoint
    // reaching a little past its ends.
    const Vec3 centre{(seg.b.x - seg.a.x) * 0.5f + seg.a.x, (seg.b.y - seg.a.y) * 0.5f + seg.a.y,
                      (seg.b.z - seg.a.z) * 0.5f + seg.a.z};
    const float radius = (1.0f / seg.invLength) * 0.51f;
    bool found = false;
    if (start != 0 && collideRoomSegment(start, centre, radius, seg, hit, cache, material))
        found = true;
    if (end != start && end != 0 && collideRoomSegment(end, centre, radius, seg, hit, cache, material))
        found = true;
    if (start != 0) {
        // The instance rooms across the start room's perimeter, each once,
        // at most 10.
        int all[256];
        const int n = m_level->neighbors(all, 256, start);
        int rooms[kMaxProbeNeighbors];
        int count = 0;
        for (int k = 0; k < n && count < kMaxProbeNeighbors; ++k)
            if (all[k] != 0 && (m_level->roomFlags(all[k]) & kRoomInstance))
                rooms[count++] = all[k];
        for (int k = 0; k < count; ++k) {
            const int room = rooms[k];
            if (room != start && room != end && room != 0 &&
                collideRoomSegment(room, centre, radius, seg, hit, cache, material))
                found = true;
        }
    }
    return found;
}

bool World::collideRoomSegment(int room, const Vec3& centre, float radius, const Segment& seg,
                               Intersection& hit, ProbeCache& cache, const Material*& material) const {
    // sdlPage16::CollideSegment: each collected polygon the segment crosses
    // either way no farther than the hit so far (a later one at the same
    // distance replacing it). The last one kept becomes the cached polygon;
    // a room without a hit makes the cache stale.
    m_level->collectProbe(room, centre, radius, m_probeBound);
    int kept = -1;
    for (std::size_t i = 0; i < m_probeBound.polygons.size(); ++i) {
        const Polygon& poly = m_probeBound.polygons[i];
        if (poly.testSegmentUndirected(m_probeBound.vertices, seg, hit, hit.t, 2.0f)) {
            hit.material = poly.material;
            kept = static_cast<int>(i);
        }
    }
    if (kept < 0) {
        cache.valid = false;
        return false;
    }
    hit.polygon = 0;
    hit.a = seg.a;
    hit.b = seg.b;
    hit.poly = nullptr;
    material = &m_level->material(hit.material);
    // sdlPolyCached::InitFromPoly: the polygon with copies of its corners.
    const Polygon& poly = m_probeBound.polygons[static_cast<std::size_t>(kept)];
    const int corners = poly.vertexCount();
    cache.polygon = poly;
    for (std::size_t k = 0; k < static_cast<std::size_t>(corners); ++k)
        cache.vertices[k] = m_probeBound.vertices[poly.v[k]];
    cache.polygon.v = {0, 1, 2, static_cast<std::uint16_t>(corners == 4 ? 3 : 0)};
    cache.valid = true;
    return true;
}

bool World::collideProbe(const Segment& seg, Instance& inst, Intersection& hit,
                         const Material*& material) const {
    // dgPhysManager::CollideProbe: when the instance's sphere reaches the
    // segment's, the segment in the instance's frame against its bound.
    const Bound* bound = inst.bound(0);
    if (!bound)
        return false; // MM2 reports the instance as having no bound
    const Vec3 mid{(seg.a.x + seg.b.x) * 0.5f, (seg.a.y + seg.b.y) * 0.5f, (seg.a.z + seg.b.z) * 0.5f};
    const float radius = inst.radius();
    const Vec3 pos = inst.position();
    const float reach = 0.5f / seg.invLength + radius;
    const float dx = mid.x - pos.x;
    const float dy = mid.y - pos.y;
    const float dz = mid.z - pos.z;
    if (reach * reach < (dz * dz + dy * dy) + dx * dx)
        return false;
    const Mat34& m = inst.matrix();
    Segment local;
    local.kind = seg.kind;
    const Vec3 da{seg.a.x - m.m3.x, seg.a.y - m.m3.y, seg.a.z - m.m3.z};
    local.a = {age::dot(m.m0, da), age::dot(m.m1, da), age::dot(m.m2, da)};
    const Vec3 db{seg.b.x - m.m3.x, seg.b.y - m.m3.y, seg.b.z - m.m3.z};
    local.b = {age::dot(m.m0, db), age::dot(m.m1, db), age::dot(m.m2, db)};
    local.vertical = false;
    local.invLength = 0.0f;
    if (!bound->testProbe(local, hit, hit.t))
        return false;
    hit.transform(m);
    if (hit.poly)
        hit.material = hit.poly->material;
    material = &bound->material(hit.material);
    return true;
}

void World::beginFrame() {
    // dgPhysManager::DeclareMover: the declared bodies in order, at most 32
    // (with a level, only those in a room); the rooms of the type-3 and
    // type-4 ones and their neighbours become active (at most 20).
    m_activeRooms.clear();
    std::size_t taken = 0;
    const auto addActiveRoom = [this](int room) {
        if (m_activeRooms.size() < kMaxActiveRooms &&
            std::ranges::find(m_activeRooms, room) == m_activeRooms.end())
            m_activeRooms.push_back(room);
    };
    // Last frame's instances without a body leave the table (it is reset
    // after every frame); this frame's join it after the bodies (OpenMM2's
    // order: MM2 lists them as the AI declares them, before the racers).
    if (!m_stepping)
        std::erase_if(m_movers, [](const Mover& m) { return m.transient; });
    for (Mover& m : m_pendingInstances)
        m_movers.push_back(std::move(m));
    m_pendingInstances.clear();
    for (Mover& m : m_movers) {
        if (!m.body) {
            m.active = live(m) && taken < kMaxMovers && (!m_level || m.instance->room != 0);
            if (m.active)
                ++taken;
            continue;
        }
        Body& b = *m.body;
        // A body placed outside the room bookkeeping finds its room first
        // (MM2's owners move their instances into a room when they place
        // them; DeclareMover refuses an instance in none).
        if (m_level && live(m) && b.declared && b.room == 0)
            b.room = m_level->findRoom(b.position(), 0);
        m.active = live(m) && b.declared && taken < kMaxMovers && (!m_level || b.room != 0);
        if (!m.active)
            continue;
        ++taken;
        if (b.moverType > 2 && m_level) {
            int rooms[64];
            rooms[0] = b.room;
            const int n = 1 + m_level->neighbors(rooms + 1, 63, b.room);
            for (int k = 0; k < n; ++k)
                addActiveRoom(rooms[k]);
        }
    }
    // dgPhysManager::Update, before the samples: a type-1 mover whose room
    // is not active takes no part this frame and is detached
    // (lvlInstance::Detach); then the "hit by the player" marks are cleared.
    // (An owner's Detach may remove its body from the world: the removal
    // waits until the loop is done.)
    const bool stepping = m_stepping;
    m_stepping = true;
    for (Mover& m : m_movers) {
        if (!m.active || !m.body || m.body->moverType != 1 || !m_level)
            continue;
        if (std::ranges::find(m_activeRooms, m.body->room) != m_activeRooms.end())
            continue;
        m.active = false;
        m.body->detach();
    }
    m_stepping = stepping;
    if (!stepping)
        std::erase_if(m_movers, [](const Mover& m) { return m.removed; });
    for (Mover& m : m_movers)
        if (running(m))
            m.instance->hitByPlayer = false;
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
    // dgPhysManager::Update: a frame under a millisecond runs no sample (its
    // time is lost, as in the original).
    beginFrame();
    int n = static_cast<int>(std::ceil(static_cast<double>((frameDelta - 0.001f) / sampleStep)));
    if (maxSamples < n)
        n = maxSamples;
    if (n <= 0)
        return 0;
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
        if (updating(m_movers[i]) && b->controller)
            b->controller->beforeIntegrate(*b, dt, *this);
    }
    for (std::size_t i = 0; i < count; ++i) {
        Body* b = m_movers[i].body;
        if (!updating(m_movers[i]))
            continue;
        if (!b->kinematic)
            b->ics.update(dt, invDt);
        if (!b->kinematic)
            b->syncBoundMatrix();
    }
    for (std::size_t i = 0; i < count; ++i) {
        Body* b = m_movers[i].body;
        if (!updating(m_movers[i]))
            continue;
        if (b->controller)
            b->controller->afterIntegrate(*b, dt, *this);
        // lvlLevel::MoveToRoom after the entity's update (vehCar::Update,
        // aiVehicleActive::Update).
        if (m_level)
            b->room = m_level->findRoom(b->position(), b->room);
        b->collider.joint = b->joint;
        b->collider.id = b->audioId;
        // (MM2 computes a collider's barely-moved flag only when a traffic
        // car leaves its rail, aiVehicleActive::Attach; every other mover
        // keeps the false phColliderBase::Reset gave it.)
    }

    // dgPhysManager::GatherCollidables for movers that collide with the city
    // or its objects.
    for (Mover& m : m_movers) {
        m.collidables.clear();
        if (running(m) && !m.fresh && m.instance->bound(0) && (collidesTerrain(m) || collidesInstances(m)))
            gatherCollidables(m);
    }

    // Collisions, mover by mover: the city, the movers after it, then the
    // gathered instances. Movers that collisions set in motion are appended
    // (fresh) and wait for the next sample.
    for (std::size_t i = 0; i < m_movers.size(); ++i) {
        if (!running(m_movers[i]) || m_movers[i].fresh || !m_movers[i].instance->bound(0))
            continue;
        Body* a = m_movers[i].body;
        // (An instance without a body has no collider of its own against the
        // city: static against static, nothing to do.)
        if (a && a->collideTerrain)
            collideTerrain(*a);
        if (a && a->collideMovers) {
            for (std::size_t j = i + 1; j < m_movers.size(); ++j) {
                Body* b = m_movers[j].body;
                if (!running(m_movers[j]) || m_movers[j].fresh || !b || !b->collideMovers || !b->collisionBound)
                    continue;
                // Colliders sharing an unbroken joint (a tractor and its
                // trailer) do not collide.
                if (a->joint && !a->joint->isBroken() && a->joint == b->joint)
                    continue;
                if (trivialCollide(*a, *b))
                    collideInstances(*a, *b);
            }
        }
        if (collidesTerrain(m_movers[i]) || collidesInstances(m_movers[i])) {
            // The list may not survive the calls (new movers reallocate).
            std::vector<Instance*> list = m_movers[i].collidables;
            for (Instance* c : list)
                if (running(m_movers[i]))
                    collideInstances(*m_movers[i].instance, *c);
        }
    }

    // New movers take part from now on (0x100 -> 0x1b).
    for (Mover& m : m_movers)
        m.fresh = false;
    // phColliderBase::UpdateMtx: the pending pushes move the bodies.
    for (Mover& m : m_movers)
        if (updating(m))
            m.body->collider.updateMtx();
    for (std::size_t i = 0; i < m_movers.size(); ++i) {
        Body* b = m_movers[i].body;
        if (updating(m_movers[i]) && b->controller)
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
    Instance& self = *mover.instance;
    const bool terrain = collidesTerrain(mover);
    const bool others = collidesInstances(mover) || collidesMovers(mover);
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
            const bool wanted = (others && inst->collidable) || (terrain && inst->terrainCollidable);
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
        m_tempA.initStatic(boundA, &m_tempMatrixA);
        m_tempA.id = a.audioId;
    }
    if (entityB) {
        colB = &entityB->collider;
    } else {
        m_tempB.initStatic(boundB, &m_tempMatrixB);
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
