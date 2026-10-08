#pragma once

// The static world as MM2's collision manager (dgPhysManager) sees it: rooms
// (cityLevel), the city's collision polygons (lvlSDL, built per query by
// sdlPage16::Collect) and the objects placed in each room (lvlInstance and
// its subclasses). Ported from the code of midtown2.exe build 3393. See
// docs/physics.md, "Collision".

#include "phys/Bound.h"
#include "phys/Collider.h"
#include "phys/Material.h"

#include <vector>

namespace mm2::phys {

class Body;
class Level;

// lvlInstance as dgPhysManager uses it: something in a room with a bound.
// Static city objects, unhit bangers, traffic cars on their rails and the
// simulated bodies (movers) all are instances.
class Instance {
public:
    virtual ~Instance() = default;

    // lvlInstance::GetBound: 0 is the bound the instance collides with;
    // dgPhysManager::CollideTerrain asks spheres and hotdogs for bound 1
    // (lvlInstance returns a box around the bound for 1..3).
    virtual const Bound* bound(int which) const = 0;
    // lvlInstance::GetMatrix: places the bound.
    virtual const Mat34& matrix() const = 0;
    // lvlInstance::GetPosition / GetRadius: the sphere of
    // dgPhysManager::TrivialCollideInstances and GatherCollidables.
    virtual Vec3 position() const { return matrix().m3; }
    virtual float radius() const = 0;
    // lvlInstance::GetEntity: the simulated body, null while the instance
    // is static.
    virtual Body* entity() { return nullptr; }
    // lvlInstance::AttachEntity: a static instance that something hit may
    // become a simulated body (aiVehicleInstance: a traffic car leaves its
    // rail). Null when it stays static.
    virtual Body* attachEntity() { return nullptr; }
    // The entity's call after the impacts that attached it were resolved
    // (dgPhysEntity vtable 0x24).
    virtual void attached() {}
    // lvlInstance::Detach: dgPhysManager::Update calls it on a type-1 mover
    // outside the active rooms (dgHitBangerInstance::Detach lets the body
    // go and moves the prop to room 0, out of the world).
    virtual void detach() {}

    // dgBangerInstance (lvlInstance flag 1): resolved with dgImpact and the
    // banger's impulse limit; breaking loose calls bangerHit
    // (dgUnhitBangerInstance::Impact).
    virtual bool isBanger() const { return false; }
    virtual float bangerImpulseLimit2() const { return 0.0f; }
    // TrivialCollideInstances' banger sphere: the ground point under the
    // centre and the banger's YRadius; false when YRadius is 0.
    virtual bool bangerSphere(Vec3& /*centre*/, float& /*radius*/) const { return false; }
    virtual void bangerHit(Instance& /*by*/, const Vec3& /*position*/) {}
    // dgPhysEntity vtable 0x1c on a banger's entity that did not break.
    virtual void bangerHeld() {}

    // The ids AudImpact plays for impacts against the instance
    // (the instance data's collider id): 0 for the world, cars and traffic, the banger's
    // AudioId for bangers.
    int audioId = 0;
    // lvlInstance's room: the room the instance is listed in.
    int room = 0;
    // lvlInstance's flags.
    bool collidable = true;          // 0x10: IsCollidable
    // 0x100: IsTerrainCollidable (instances with a terrain bound); the same
    // bit keeps dgPhysManager::CollideInstances from trying AttachEntity.
    bool terrainCollidable = false;
    bool multiRoom = false;          // 0x800: listed in several rooms (lvlMultiRoomInstance)
    bool hitByPlayer = false;        // 0x8000: the player's car hit it this frame
};

// The polygons lvlSDL::CollidePolyToLevel collects for one query (the
// static sdlPoly buffer and lvlSDL's vertex buffer), as a bound so the
// impact search can read their vertices and materials (lvlLevelBound).
class LevelBound final : public Bound {
public:
    LevelBound() : Bound(BoundType::Level) {}

    std::vector<Vec3> vertices;     // index 0 is unused: v[3] == 0 marks a triangle
    std::vector<Polygon> polygons;
    // The level whose materials the polygons' material bytes name
    // (lvlLevelBound::GetMaterial); null: the default material.
    const Level* level = nullptr;

    void clear();
    // sdlPoly::InitNoArea for a collected polygon (3 or 4 vertices; the
    // normal as the collector computed it).
    void addPolygon(const Vec3* corners, int count, const Vec3& normal, std::uint8_t material);

    const Vec3& vertex(int index) const override;
    const Material& material(int index) const override;
    int numMaterials() const override { return 0; }
};

// cityLevel / lvlLevel / lvlSDL services the collision manager needs.
class Level {
public:
    virtual ~Level() = default;

    // lvlLevel::FindRoomId: the room containing `position` (`hint`: the
    // room it was in), 0 when none.
    virtual int findRoom(const Vec3& position, int hint) const = 0;
    // cityLevel::GetTouchedNeighbors: up to `max` rooms next to `room` whose
    // shared perimeter edges the sphere touches.
    virtual int touchedNeighbors(int* out, int max, int room, const Vec3& centre, float radius) const = 0;
    // cityLevel::GetNeighbors: up to `max` rooms across `room`'s perimeter,
    // each once, in perimeter order (none by default).
    virtual int neighbors(int* /*out*/, int /*max*/, int /*room*/) const { return 0; }
    // lvlSDL::CollidePolyToLevel's collection: sdlPage16::Collect of each
    // room for the sphere into `out` (cleared first).
    virtual void collect(const int* rooms, int count, const Vec3& centre, float radius, LevelBound& out) const = 0;
    // The instances listed in `room` (lvlLevel's room lists), appended.
    virtual void instances(int room, std::vector<Instance*>& out) const = 0;
    // lvlLevelBound::GetMaterial: the material a polygon's material byte
    // names (0: the material manager's default material, n: its entry n - 1).
    virtual const Material& material(int index) const;
};

// lvlSDL::CollidePolyToLevel on collected polygons: the bound's vertex
// sweeps (from `last` to `m`, when `sweep`) and edges against the level's
// polygons, in world space. Returns the number of intersections (also in
// count); `collider` is stored in them (dgPhysManager passes null).
int collidePolyToLevel(const LevelBound& level, const BoundPolygonal& bound, Collider* collider, const Mat34& m,
                       const Mat34& last, Intersection* out, int max, int& count, bool sweep);

// The level's impact search (lvlSDL's variant of phBoundPolygonal::
// FindImpacts, called by dgPhysManager::CollideTerrain): impacts between
// the level (side A, levelCollider) and the bound (side B, collider), the
// level's component being the material of the first intersection.
int findLevelImpacts(const LevelBound& level, const BoundPolygonal& bound, const Mat34& m, const Mat34& last,
                     Collider* levelCollider, Collider* collider, Intersection* isects, int count, Impact* impacts,
                     int maxImpacts);

} // namespace mm2::phys
