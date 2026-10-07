#pragma once

// Cops & Robbers rules (MM2 multiplayer), as a network-agnostic state
// machine: whoever hosts runs it and replicates the results. Structure
// follows MM1's mmMultiCR (Open1560: StealGold, OppStealGold, DropGold,
// UpdateGold/Bank/Hideout/Limit/TimeWarning); MM2's Free-For-All and Robber
// Teams variants and the location file layout are inferred.
//
// Open1560 - An Open Source Re-Implementation of Midtown Madness 1 Beta
// Copyright (C) 2020 Brick. GPL-3.0-or-later; OpenMM2 port under the same licence.

#include "core/Math.h"
#include "game/RaceConfig.h"
#include "vfs/Vfs.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mm2::game::session {

enum class CrTeam : std::uint8_t { Robber, Cop, Red, Blue };

// race/<city>/multicopwaypoints.csv read as: row 0 bank, row 1 hideout, the
// remaining rows gold positions (inferred from the file: two special places
// followed by spots spread over the map).
struct CrLocations {
    Vec3 bank;
    Vec3 hideout;
    std::vector<Vec3> gold;
};
std::optional<CrLocations> loadCrLocations(const vfs::Vfs& vfs, const std::string& raceDir);

struct CrSettings {
    CopsAndRobbersMode mode = CopsAndRobbersMode::FreeForAll;
    float goldMassKg = 0.0f;  // "Weightless", "Quarter Ton", "Half Ton", "Gold Mass"
    float timeLimitSeconds = 0.0f; // 0 = none
    int pointLimit = 0;            // 0 = none
    float pickupRadius = 4.0f;     // inferred
    float deliverRadius = 10.0f;   // inferred
    float stealImpulse = 1500.0f;  // impact impulse that knocks the gold loose (inferred)
    std::uint32_t seed = 1;
};

class CopsAndRobbers {
public:
    struct Car {
        int id = 0;
        CrTeam team = CrTeam::Robber;
        Vec3 position;
        bool wrecked = false;
    };
    struct Impact {
        int a = 0, b = 0; // car ids
        float impulse = 0.0f;
    };
    enum class EventType : std::uint8_t { GoldTaken, GoldStolen, GoldDropped, GoldDelivered, TimeWarning, TimeUp,
                                          PointLimit };
    struct Event {
        EventType type{};
        int car = -1;     // carrier / deliverer
        int value = 0;    // minutes left (TimeWarning), points
    };

    CopsAndRobbers(const CrSettings& settings, const CrLocations& locations);

    void addCar(int id, CrTeam team);
    void update(float dt, std::vector<Car> cars, const std::vector<Impact>& impacts);

    bool over() const { return m_over; }
    int goldCarrier() const { return m_carrier; } // car id or -1
    Vec3 goldPosition() const { return m_goldPos; }
    // Where the carrier must take the gold.
    Vec3 deliveryTarget(CrTeam team) const;
    int score(CrTeam team) const;
    int playerScore(int id) const;
    float timeRemaining() const { return m_settings.timeLimitSeconds > 0 ? m_timeLeft : -1.0f; }
    // Extra mass on the carrier (mmMultiCR::FondleCarMass).
    float carrierExtraMassKg() const { return m_settings.goldMassKg; }
    std::vector<Event> takeEvents();

private:
    CrTeam teamOf(int id) const;
    void respawnGold();
    void scoreFor(int id);

    CrSettings m_settings;
    CrLocations m_locations;
    std::vector<std::pair<int, CrTeam>> m_cars;
    std::vector<std::pair<int, int>> m_playerScores;
    int m_teamScore[4] = {0, 0, 0, 0};
    int m_carrier = -1;
    Vec3 m_goldPos;
    std::size_t m_goldIndex = 0;
    std::uint32_t m_rng;
    float m_timeLeft = 0.0f;
    int m_lastWarning = -1;
    bool m_over = false;
    std::vector<Event> m_events;
};

} // namespace mm2::game::session
