#pragma once

// Pedestrians walking the sidewalks and getting out of the player's way.
//
// Structure after MM1's aiPedestrian (Open1560: Update, Wander, Anticipate,
// Avoid, DetectPlayerAnticipate, DetectPlayerCollision; constants decoded
// from game.asm) with MM2's skeletal animation state table
// (anim/pedmodel_*.csv, asset::PedAnimTable). MM2 changed the pedestrian
// system substantially, so most behaviour here is inferred; docs/ai.md lists
// which parts carry MM1 values.

#include "ai/PlayerCar.h"
#include "ai/Random.h"
#include "ai/RoadNetwork.h"
#include "asset/Ped.h"

#include <string>
#include <vector>

namespace mm2::ai {

inline constexpr float kPedAnimFps = 20.0f;      // inferred: gives the WALK root motion a normal walking pace
inline constexpr float kPedActiveRadius = 75.0f; // flt_61B5F8 = 5625 (squared), aiPedestrian::Update
inline constexpr float kPedAwareRadius = 35.0f;  // flt_63936C, DetectPlayerAnticipate
inline constexpr float kPedCollisionRadius = 6.0f; // flt_639360, DetectPlayerCollision
inline constexpr float kPedLateralSpread = 1.8f;   // flt_639364, aiPedestrian::Reset

// A pedestrian type with its animation table (anim/<type>.csv) and clothing
// variant count (anim/<type>.shaders).
struct PedTypeInfo {
    std::string name; // "pedmodel_man"
    asset::PedAnimTable table;
    int variants = 1;
};

struct Pedestrian {
    int id = 0;
    int type = 0;         // index into the types passed to Pedestrians
    std::string typeName; // "pedmodel_man"
    int variant = 0;      // clothing variant (anim/<type>.shaders)
    int sidewalk = -1;    // RoadNetwork::sidewalks() index it belongs to
    Mat34 transform;      // feet on the ground, facing -Z
    std::string state;    // animation state, e.g. "WALK", "WALK_LDIVE"
    std::string animFile; // anim/<animFile>.anim
    float frame = 0.0f;   // 0-based frame within the .anim (for asset::posePed)
};

struct PedSettings {
    float density = 0.5f; // pedestrians per 10 m of sidewalk times this (inferred)
    int maxPeds = 48;
};

class Pedestrians {
public:
    Pedestrians(const RoadNetwork& network, std::vector<PedTypeInfo> types, const PedSettings& settings,
                std::uint64_t seed);

    void step(float dt, const Vec3& playerPos, const Vec3& playerVel);
    void step(float dt, const PlayerCar& p) { step(dt, p.transform.m3, p.velocity); }

    const std::vector<Pedestrian>& peds() const { return m_public; }
    std::size_t activeCount() const;
    const std::vector<PedTypeInfo>& types() const { return m_types; }

    // Diagnostics for tests.
    float distanceFromSidewalk(int pedId) const;

private:
    struct Ped {
        bool active = false;
        int type = 0;
        int variant = 0;
        int sidewalk = -1;
        float s = 0.0f;       // along the sidewalk centre
        int dir = 1;          // +1 towards the sidewalk's end, -1 towards its start
        float lateral = 0.0f; // offset from the centre line, left of the walking direction
        Vec3 position;
        float heading = 0.0f; // yaw, 0 = facing -Z
        const asset::PedAnimState* state = nullptr;
        float stateTime = 0.0f;
        float standTimer = 0.0f;
        bool onSidewalk = true;
    };

    void spawn(int sidewalk, const Vec3& playerPos, std::vector<int>& freeSlots);
    void setState(Ped& p, std::string_view name);
    void wander(Ped& p, float dt);
    void react(Ped& p, const Vec3& playerPos, const Vec3& playerVel);
    void applyRootMotion(Ped& p, float dt);
    void advanceAnimation(Ped& p, float dt);
    bool isLooping(const asset::PedAnimState* s) const;
    float stateDuration(const asset::PedAnimState* s) const;
    bool busy(const Ped& p) const; // diving / on the ground / getting up
    void publish();

    const RoadNetwork& m_net;
    std::vector<PedTypeInfo> m_types;
    PedSettings m_settings;
    Random m_rng;
    std::vector<Ped> m_peds;
    std::vector<Pedestrian> m_public;
    std::vector<std::uint8_t> m_sidewalkActive;
};

} // namespace mm2::ai
