#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mm2::game {

// Game modes offered by the original (string table 518-524, 585-589).
enum class GameMode : std::uint8_t {
    Cruise,         // free roam
    Blitz,          // checkpoints in order against the clock
    Checkpoint,     // checkpoints in any order against opponents ("Checkpoint Race")
    Circuit,        // laps against opponents
    CrashCourse,    // driving school lessons
    CopsAndRobbers, // multiplayer gold game
};

// Cops & Robbers variants (string table 399-402).
enum class CopsAndRobbersMode : std::uint8_t { FreeForAll, CopsVsRobbers, RobberTeams };

// Same numbering as the city lighting tables (city/Environment.h).
enum class TimeOfDay : std::uint8_t { Morning, Noon, Evening, Night };
enum class Weather : std::uint8_t { Clear, Cloudy, Fog, Rain, Snow };
enum class Difficulty : std::uint8_t { Amateur, Professional };

// Everything needed to start a session in the world, produced by the
// frontend (single player menus or the multiplayer lobby) and consumed by the
// race screen.
struct RaceConfig {
    GameMode mode = GameMode::Cruise;
    std::string city = "london"; // map name (tune/<city>.cinfo)
    int raceIndex = -1;          // index within the mode's race list; -1 for cruise

    std::string vehicle = "vpbug"; // VehicleInfo::baseName
    int vehicleColor = 0;          // index into VehicleInfo::colors (paint job)
    bool automatic = true;         // transmission

    TimeOfDay timeOfDay = TimeOfDay::Noon;
    Weather weather = Weather::Clear;
    Difficulty difficulty = Difficulty::Amateur;

    int laps = 0;      // circuit races
    int opponents = 0; // AI opponents
    // Densities as chosen in the menus, 0 (none) .. 1 (maximum).
    float pedestrianDensity = 0.5f;
    float trafficDensity = 0.5f;
    float copDensity = 0.5f;

    // Multiplayer (filled from the lobby; unused in single player).
    bool multiplayer = false;
    CopsAndRobbersMode copsAndRobbers = CopsAndRobbersMode::FreeForAll;
    float timeLimitMinutes = 0.0f;
    int pointLimit = 0;
};

// One line of the results table: a racer who reached the finish.
struct RaceStanding {
    int opponent = -1; // index of the AI opponent, -1 = the player
    int place = 0;     // 1-based
    float timeSeconds = 0.0f;
};

// Outcome of a session, shown by the results screens and recorded in the
// driver's profile.
struct RaceResult {
    RaceConfig config;
    bool ended = false;    // the session reached its end (finish, lesson pass/fail, wreck, time up), not quit
    bool finished = false; // crossed the finish / passed the lesson
    bool won = false;      // race passed (amateur top 3, pro 1st, blitz in time) / lesson passed
    int position = 0;      // 1-based, 0 = not ranked
    float timeSeconds = 0.0f;
    float bestLapSeconds = 0.0f;   // circuit races
    std::vector<float> lapSeconds; // circuit races, every lap
    int score = 0;
    int damage = 0;
    std::vector<RaceStanding> standings; // finishers by place (player and opponents)
    bool cheated = false; // the cheat flag was set: nothing is registered (RegisterFinish)
};

} // namespace mm2::game
