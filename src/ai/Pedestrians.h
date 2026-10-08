#pragma once

// Pedestrians, after MM2's aiPedestrian, pedAnimationInstance and
// aiMap::AdjustPedestrians (build 3393, MM2Recomp; documentation only).
//
// A fixed pool ([Ped Pool] x density) is spread over the sidewalks of the
// roads listed for the player's PSDL room. Each pedestrian walks its road's
// sidewalk along a Hermite curve per section, steering towards a point 6 m
// ahead by at most 0.15 rad per update and moving by its animation's speed;
// at the end of the sidewalk it turns the corner onto the next road or, at
// lit intersections with a pedestrian phase, crosses a road. It braces,
// runs or dives out of the player's way. The animation states and speeds
// come from anim/pedmodel_*.csv, played at 30 frames per second.
//
// The bookkeeping is MM2's: each road keeps a list of its pedestrians and the
// pool is a list too, both with the newest entry first; the roads populated
// for the player form a list, newest first, and that is the update order.

#include "ai/PlayerCar.h"
#include "ai/Random.h"
#include "ai/RoadNetwork.h"
#include "ai/TrafficLights.h"
#include "asset/Ped.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace mm2::ai {

inline constexpr float kPedAnimFps = 30.0f;        // pedAnimationInstance::PreUpdate
inline constexpr float kPedAwareRadius = 35.0f;    // aiPedestrian::Update: 1225 (squared)
inline constexpr float kPedLookAhead = 6.0f;       // Wander: SolveTargetPoint(dist + dir * 6)
inline constexpr float kPedTurnRate = 0.15f;       // radians per update
inline constexpr float kPedMaxLateral = 1.5f;      // CalcCurve clamp
inline constexpr int kDefaultPedPool = 100;        // aiCityData [Ped Pool] default

// A pedestrian type with its animation table (anim/<type>.csv), clothing
// variant count (anim/<type>.shaders) and the frame counts of its .anim files.
struct PedTypeInfo {
    std::string name; // "pedmodel_man"
    asset::PedAnimTable table;
    int variants = 1;
    std::map<std::string, int> animFrames; // anim file -> frames
};

struct Pedestrian {
    int id = 0;
    int type = 0;         // index into the types passed to Pedestrians
    std::string typeName; // "pedmodel_man"
    int variant = 0;      // clothing variant (anim/<type>.shaders)
    int sidewalk = -1;    // RoadNetwork::sidewalks() index of its current sidewalk, if any
    Mat34 transform;      // feet on the ground, facing -Z
    std::string state;    // animation state, e.g. "WALK", "WALK_LDIVE"
    std::string animFile; // anim/<animFile>.anim
    float frame = 0.0f;   // whole frame within the .anim, from 0 (MM2 draws whole frames)
    bool scream = false;  // started an avoidance reaction this step (AudCreatureContainer)
    bool crossing = false; // on its way across a road
};

struct PedSettings {
    float density = 1.0f; // menu pedestrian density
    int pool = kDefaultPedPool; // [Ped Pool] of the city's AI map
    // Types to use, by name (anim/<name>.csv), drawn uniformly per pedestrian.
    std::vector<std::string> names;
};

class Pedestrians {
public:
    using Probe = std::function<bool(const Vec3& from, const Vec3& to, Vec3& hit)>;
    // Whether a vehicle out of normal driving is at `intersection` or on `path`.
    using AccidentQuery = std::function<bool(int intersection, int path, int dir)>;

    Pedestrians(const RoadNetwork& network, std::vector<PedTypeInfo> types, const PedSettings& settings,
                std::uint64_t seed);

    void setLights(const TrafficLights* lights) { m_lights = lights; }
    void setProbe(Probe probe) { m_probe = std::move(probe); }
    void setAccidentQuery(AccidentQuery query) { m_accident = std::move(query); }
    void populateAll(); // every road's sidewalks (tests)

    // One update; `room` is the player's PSDL room (0: outside, no change).
    void step(float dt, const PlayerCar& player, int room);

    const std::vector<Pedestrian>& peds() const { return m_public; }
    std::size_t activeCount() const;
    const std::vector<PedTypeInfo>& types() const { return m_types; }

    // Diagnostics for tests: distance from the centre line of its sidewalk.
    float distanceFromSidewalk(int pedId) const;

private:
    using State = const asset::PedAnimState*;
    struct Seqs {
        State stand = nullptr, stand2 = nullptr, standWalk = nullptr, walk = nullptr, walkStand = nullptr;
        State standAntic = nullptr, antic = nullptr, anticWalk = nullptr, walkAntic = nullptr;
        State anticLDive = nullptr, lDiveGround = nullptr, groundStandL = nullptr, groundStandR = nullptr;
        State anticRDive = nullptr, rDiveGround = nullptr, walkRDive = nullptr, walkLDive = nullptr;
        State run = nullptr, backup = nullptr, backupWalk = nullptr, runWalk = nullptr;
    };
    struct Ped {
        bool active = false; // on a road (else in the pool)
        bool lost = false;   // MM2 left it with a NaN position (AvoidObstacle); hidden until reset
        int next = -1;       // next in its road's list or in the pool (aiPedestrian +0x98)
        int type = 0, variant = 0;
        int path = -1, prevPath = -1;
        int dir = 1, prevDir = 1;
        int side = 1, prevSide = 1;
        int crossChoice = 0;
        int idx = 1; // end vertex of the current sidewalk segment; 0 or n on a corner
        int reaction = 0, lastReaction = -1;
        int cross = 0, lastCross = -1;
        bool reversingAtDive = false;
        bool wall = false;
        Vec3 wallHit;
        float heading = 0.0f;      // forward = (sin h, 0, cos h)
        float frameHeading = 0.0f; // the heading of MM2's stored matrix (rebuilt by Reset and Update)
        float lateral = 0.0f;
        float invLen = 1.0f;
        float dist = 0.0f; // the last RoadDistance (aiPedestrian +0x2c)
        float sideDist0 = 0.0f;
        float curve[2][4] = {};
        Vec3 position, target;
        // Animation (pedAnimationInstance).
        State seq = nullptr, queued = nullptr;
        int frame = 0;
        bool scream = false;
    };

    // Sidewalk geometry of (path, side): side -1 is the file's first side.
    struct Walk {
        std::vector<Vec3> points; // sidewalk vertex row (aiPath::SidewalkVertice), in section order
        std::vector<float> cum;   // its cumulative lengths, from 0
        // Cumulative lengths of every vertex row of the side (lanes, then the
        // sidewalk), as aiPath keeps them: the file's, or recomputed by
        // aiPath::ReverseDirection on drive-on-the-left roads.
        std::vector<std::vector<float>> rowCum;
        std::vector<Vec3> curb;
        float inner = 0.0f, outer = 0.0f; // lateral extent (side params)
        bool open = false;               // pedestrians allowed (side flag bit 1 clear)
        int sidewalk = -1;               // RoadNetwork sidewalk id
    };
    const Walk& walk(int path, int side) const;
    int sections(int path) const;
    Vec3 sv(int path, int side, int i) const;
    float cumAt(int path, int side, int i) const;
    float subLength(int path, int side, int a, int b) const;
    Vec3 axisX(int path, int i) const;
    Vec3 axisZ(int path, int i) const;
    Vec3 axisW(int path, int i) const;
    int sidewalkIndex(int path, int side, float dist) const;
    float getHeading(int path, float dist, int row, int dir) const;
    int crossedNode(const Ped& p) const;

    void startSeq(Ped& p, State s);
    void queueSeq(Ped& p, State s) { if (s) p.queued = s; }
    int frameCount(const Ped& p, State s) const;
    float fwdSpeed(const Ped& p, State s) const;
    float latSpeed(const Ped& p, State s) const;
    const Seqs& seqs(const Ped& p) const { return m_seqs[static_cast<std::size_t>(p.type)]; }

    // aiPath / aiMap lists.
    void pathAdd(int path, int idx);
    void pathRemove(int path, int idx);
    void poolAdd(int idx);
    void poolRemove(int idx);
    void moveToPath(Ped& p, int path);

    void adjust(const std::vector<std::uint16_t>& from, const std::vector<std::uint16_t>& to);
    void clearPeds(int path);
    void reset(int idx, int path, int side);
    void updateRoad(int path, float dt, const PlayerCar& player);
    void update(int idx, float dt, const PlayerCar& player);
    void animate(Ped& p, float dt);

    // aiPedestrian helpers.
    void calcCurve(Ped& p, int a, int b, float lateral);
    Vec3 solvePosition(const Ped& p, float t) const;
    void solveTargetPoint(Ped& p, float d);
    float roadDistance(Ped& p);
    void solveRoadSegment(Ped& p, float dist);
    int pickNextRoad(Ped& p);
    int setNextRoad(const Ped& p, int node) const;
    void steer(Ped& p, const Vec3& target);
    bool wallProbe(Ped& p);
    void backupAt(Ped& p);

    // Reactions.
    bool forwardCollision(const Ped& p, const PlayerCar& c, float& along) const;
    bool anticipateCollision(const Ped& p, const PlayerCar& c, float& along) const;
    bool playerCollision(const Ped& p, const PlayerCar& c, float& ahead) const;
    void wander(Ped& p, const PlayerCar& c);
    void anticipate(Ped& p, const PlayerCar& c);
    void avoid(Ped& p, const PlayerCar& c, float& latScale);
    void avoidObstacle(Ped& p, const Vec3& obstacle, float radius);

    // Crossing the street.
    Vec3 curbPoint(int path, int side, bool atEnd) const;
    void crossTargets(const Ped& p, Vec3& nearSide, Vec3& farSide) const;
    bool accident(const Ped& p) const;
    void abortCrossing(Ped& p);
    void preCross(Ped& p, const PlayerCar& c);
    void waitCross(Ped& p, const PlayerCar& c);
    void crossStreet(Ped& p, const PlayerCar& c);

    void publish();

    const RoadNetwork& m_net;
    std::vector<PedTypeInfo> m_types;
    std::vector<Seqs> m_seqs;
    PedSettings m_settings;
    Random m_rng;
    std::vector<Ped> m_peds;
    int m_poolHead = -1;                // aiMap +0x88
    std::vector<int> m_pathHead;        // first pedestrian of each road (aiPath +0x20)
    std::vector<std::uint8_t> m_pathActive; // road populated for the player (aiMap::FindPedAppRoad)
    std::vector<int> m_activeNext;      // the populated roads' list (aiMap +0x180, aiPath +0x34)
    int m_activeHead = -1;
    float m_animClock = 0.0f; // pedAnimationInstance::PreUpdate's frame accumulator
    std::vector<std::array<Walk, 2>> m_walks;
    std::vector<Pedestrian> m_public;
    const TrafficLights* m_lights = nullptr;
    Probe m_probe;
    AccidentQuery m_accident;
    int m_room = 0;
    bool m_started = false;
    bool m_populateAll = false;
};

} // namespace mm2::ai
