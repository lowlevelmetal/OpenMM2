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
// the original never picks the last row). race/<city>/<city>sets.csv would
// list fixed sets but no retail city has one.
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
    // Whether dropped gold may stay where it fell (on a road or
    // intersection, mmMultiCR::DropGold); elsewhere it returns to its spawn.
    std::function<bool(const Vec3&)> canDropAt;
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
        float impulse = 0.0f;
    };
    enum class EventType : std::uint8_t { GoldTaken, GoldDropped, GoldDelivered, NewSet, TimeWarning, TimeUp,
                                          PointLimit };
    struct Event {
        EventType type{};
        int car = -1;  // carrier / deliverer
        int value = 0; // minutes left (TimeWarning), points
    };

    CopsAndRobbers(const CrSettings& settings, const CrLocations& locations);

    void addCar(int id, CrTeam team);
    void update(float dt, const std::vector<Car>& cars, const std::vector<Impact>& impacts);

    bool over() const { return m_over; }
    const CrSet& set() const { return m_set; }
    int goldCarrier() const { return m_carrier; } // car id or -1
    Vec3 goldPosition() const { return m_goldPos; }
    // Where a team delivers the gold.
    Vec3 deliveryTarget(CrTeam team) const;
    // Team totals (team 0: cops / blue, team 1: robbers / red).
    int score(CrTeam team) const;
    int playerScore(int id) const;
    float timeRemaining() const { return m_settings.timeLimitSeconds > 0 ? m_timeLeft : -1.0f; }
    // The carrier's extra mass and throttle cap (FondleCarMass: 0 / 100 /
    // 200 kg, throttle 1 / 0.9 / 0.81 above first gear).
    float carrierExtraMassKg() const;
    float carrierThrottleCap() const;
    std::vector<Event> takeEvents();

private:
    static bool teamZero(CrTeam t) { return t == CrTeam::Cop || t == CrTeam::Blue; }
    CrTeam teamOf(int id) const;
    Vec3 randomPoint();
    void newSet();
    void drop(int carId, const Vec3& at, bool toSpawn);
    void score(int id, int points);
    void checkLimits();

    CrSettings m_settings;
    CrLocations m_locations;
    std::vector<std::pair<int, CrTeam>> m_cars;
    std::vector<std::pair<int, int>> m_scores;
    std::vector<std::pair<int, float>> m_lockout; // car id, seconds it cannot pick up
    CrSet m_set;
    int m_carrier = -1;
    Vec3 m_goldPos;
    std::uint32_t m_rng;
    float m_timeLeft = 0.0f;
    float m_lastWarning = 0.0f;
    bool m_scored = false;
    bool m_over = false;
    std::vector<Event> m_events;
};

} // namespace mm2::game::session
