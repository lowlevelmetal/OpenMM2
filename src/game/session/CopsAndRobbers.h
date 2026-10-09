#pragma once

// Cops & Robbers rules (MM2 multiplayer), as a network-agnostic state
// machine: whoever hosts runs it and replicates the results. Ported from
// MM2's mmMultiCR (GetNewSet, GetRandomPoints, UpdateGold, StealGold,
// DropGold, ImpactCallback, UpdateHideout, UpdateBank, Score, UpdateHUD,
// UpdateLimit, UpdateTimeWarning, FondleCarMass).

#include "core/Math.h"
#include "game/RaceConfig.h"
#include "vfs/Vfs.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace mm2::game::session {

// Team 0 delivers to the bank (cops; blue in Robber Teams), team 1 to the
// hideout (robbers; red). Free-For-All puts police cars in team 0.
enum class CrTeam : std::uint8_t { Robber, Cop, Red, Blue };

// race/<city>/multicopwaypoints.csv: a pool of places. Each set (the start,
// and after every delivery) puts the bank, the gold and the hideout on
// random, different places of the pool (mmMultiCR::LoadCSV / GetRandomPoints;
// the original never picks the last row); with fewer than three rows every
// place is an AI intersection. race/<city>/multicopsets.csv (mmMultiCR::
// LoadSets: bank, gold, hideout triples) would list fixed sets but no retail
// city has one.
struct CrLocations {
    std::vector<Vec3> points;
};
std::optional<CrLocations> loadCrLocations(const vfs::Vfs& vfs, const std::string& raceDir);

// One placement of the three objects.
struct CrSet {
    Vec3 bank, gold, hideout;
};

struct CrSettings {
    CopsAndRobbersMode mode = CopsAndRobbersMode::FreeForAll;
    int goldMass = 0;              // the lobby's gold weight option: 0, 1, 2 -> 0, 100, 200 kg
    float timeLimitSeconds = 0.0f; // 0 = none
    int pointLimit = 0;            // 0 = none
    std::uint32_t seed = 1;
    // Half of the random places are AI intersections (mmGame::RespawnXYZ)
    // when this is set; the rest come from the pool.
    std::function<std::optional<Vec3>()> randomIntersection;
    // Whether dropped gold may stay where it fell (mmMultiCR::DropGold: not
    // in a water-of-death room); elsewhere it returns to its spawn.
    std::function<bool(const Vec3&)> canDropAt;
    // mmMultiCR::FindGround: the level under a point (2 m above it to 10 m
    // below), or the point itself. DropGold puts the gold there.
    std::function<Vec3(const Vec3&)> findGround;
    // UpdateGold: the gold (its room found 1.5 m above it, mmWaypointObject::
    // Move) and the car are in the same room.
    std::function<bool(const Vec3& gold, const Vec3& car)> sameRoom;
    // UpdateBank / UpdateHideout: the base's room (found 3.75 m above it) is
    // covered or underground (level flags 0x0A), or is the car's room.
    std::function<bool(const Vec3& base, const Vec3& car)> baseReachable;
    // mmMultiCR::UpdateLimit runs on the host alone, which tells the others
    // (SendLimitReached): a machine that is not the host counts the time
    // down and shows the warnings, but ends only on limitReached().
    bool limitsFromHost = false;
};

class CopsAndRobbers {
public:
    // mmMultiCR constants.
    static constexpr float kGoldRadius = 5.0f;      // "wpobj_gold" waypoint
    static constexpr float kBaseRadius = 12.0f;     // "pt_bank" / "pt_hideout"
    static constexpr float kStealImpulse = 250.0f;  // ImpactCallback threshold
    static constexpr float kDropLockout = 2.0f;     // state 7 after losing the gold
    static constexpr float kWreckPenalty = 5.0f;    // state 6
    static constexpr int kPickupPoints = 25;
    static constexpr int kDeliveryPoints = 100;

    struct Car {
        int id = 0;
        CrTeam team = CrTeam::Robber;
        Vec3 position;
        bool wrecked = false;
        bool inWater = false; // hit the water or fell out of the city this frame
    };
    struct Impact {
        int a = 0, b = 0; // car ids
        // vehDamageImpactInfo's total (the damage values summed while the
        // impact lasts), of a damaging impact.
        float impulse = 0.0f;
    };
    enum class EventType : std::uint8_t { GoldTaken, GoldDropped, GoldDelivered, NewSet, TimeWarning, TimeUp,
                                          PointLimit };
    struct Event {
        EventType type{};
        int car = -1;  // carrier / deliverer
        int value = 0; // minutes left (TimeWarning), points; GoldDropped: 1 knocked loose by a hit
    };

    CopsAndRobbers(const CrSettings& settings, const CrLocations& locations);

    void addCar(int id, CrTeam team);
    // One authority over every car (tools, tests, a single machine).
    void update(float dt, const std::vector<Car>& cars, const std::vector<Impact>& impacts);

    // --- Network play ---------------------------------------------------------------
    // mmMultiCR splits the rules between the machines: each one runs them
    // for its own car (UpdateGame's states, UpdateGold, UpdateBank /
    // UpdateHideout, ImpactCallback) and tells the others; the host grants
    // pickups and draws the sets. The messages a machine sends:
    struct Message {
        enum class Type : std::uint8_t {
            PickupRequest, // 0x25e, to the host: the local car reached the gold
            GoldTaken,     // 0x25a GoldAck: the host gives `car` the gold
            GoldDropped,   // 0x259: the carrier lost it at `position`
            GoldDelivered, // 600: the carrier delivered it
            NewSet,        // 0x261 ChangeSet: the host's new places
        };
        Type type{};
        int car = -1;
        Vec3 position;
        CrSet set;
    };
    // The local machine's frame for its car `self` (in `cars`, with the
    // others' places): `impacts` are its car's; `host` grants pickups at
    // once and draws new sets. Returns the messages to send.
    std::vector<Message> updateNetwork(float dt, int self, bool host, const std::vector<Car>& cars,
                                       const std::vector<Impact>& impacts);
    // A message from another machine (`from`); a host may answer with more.
    std::vector<Message> receive(const Message& message, int from, bool host);
    // The host's word that a limit was reached (TimeUp or PointLimit, the
    // winner's car and points as its event had them): the game is over here
    // too.
    void limitReached(EventType type, int car, int value);
    // A player left (mmMultiCR::SystemMessage 0x2d): the host drops a
    // leaver's gold where it is (2 m above the car), not back to its spawn.
    std::vector<Message> playerLeft(int id, bool host);
    // Whether the gold can be taken (mmWaypointObject active).
    bool goldActive() const { return m_goldActive; }

    bool over() const { return m_over; }
    const CrSet& set() const { return m_set; }
    int goldCarrier() const { return m_carrier; } // car id or -1
    Vec3 goldPosition() const { return m_goldPos; }
    // Where a team delivers the gold.
    Vec3 deliveryTarget(CrTeam team) const;
    // Team totals (team 0: cops / blue, team 1: robbers / red).
    int score(CrTeam team) const;
    int playerScore(int id) const;
    CrTeam teamOf(int id) const;
    float timeRemaining() const { return m_settings.timeLimitSeconds > 0 ? m_timeLeft : -1.0f; }
    // The carrier's extra mass and throttle cap (FondleCarMass: 0 / 100 /
    // 200 kg, throttle 1 / 0.9 / 0.81 above first gear).
    float carrierExtraMassKg() const;
    float carrierThrottleCap() const;
    std::vector<Event> takeEvents();

private:
    static bool teamZero(CrTeam t) { return t == CrTeam::Cop || t == CrTeam::Blue; }
    Vec3 randomPoint();
    void newSet();
    void drop(int carId, const Vec3& at, bool toSpawn, bool knocked = false);
    void take(int carId);
    void deliver(int carId);
    void tickLimits(float dt);
    void score(int id, int points);
    void checkLimits();

    CrSettings m_settings;
    CrLocations m_locations;
    std::vector<std::pair<int, CrTeam>> m_cars;
    std::vector<std::pair<int, int>> m_scores;
    std::vector<std::pair<int, float>> m_lockout; // car id, seconds it cannot pick up
    CrSet m_set;
    int m_carrier = -1;
    bool m_goldActive = true; // the gold's waypoint (+0x18 clear): not carried, not requested
    Vec3 m_goldPos;
    std::uint32_t m_rng;
    float m_timeLeft = 0.0f;
    float m_lastWarning = 0.0f;
    bool m_scored = false;
    bool m_over = false;
    std::vector<Event> m_events;
};

} // namespace mm2::game::session
