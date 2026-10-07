#pragma once

// Everything a session needs to know about one event, gathered from the
// city's race files (race/<dir>/...), for a game::RaceConfig.

#include "city/CityData.h"
#include "game/RaceConfig.h"
#include "game/session/Types.h"
#include "vfs/Vfs.h"

#include <optional>
#include <string>
#include <vector>

namespace mm2::game::session {

// A race opponent: car, driving line (race/<dir>/<race>-a|p-<n>.opp) and
// starting place. `params` are the remaining numbers of its [Opponent] line
// in the race's .aimap (ai::OpponentSettings::fromData, docs/ai.md).
struct OpponentSetup {
    std::string vehicle;
    std::string pathFile; // virtual path
    std::vector<city::OpponentPoint> path;
    Mat34 spawn;
    std::vector<float> params;
};

// A police car placed by the race's .aimap [Police] section.
struct PoliceSetup {
    std::string vehicle;
    Mat34 spawn;
    std::vector<float> params;
};

// Crash course event types (column "Event" of crash<N>data.csv), named after
// the lessons that use them and the messages in the string table.
// All inferred: MM1 has no crash course.
enum class LessonType : std::uint8_t {
    Jump = 0,         // longjump, precisionjump: hit the checkpoints (string ids 205-209)
    Acceleration = 1, // "Stunt Mode: Acceleration" (199-204); not used by retail data
    Follow = 2,       // follow / chase a car to its destination (217-223)
    Evade = 3,        // lose your pursuers before the finish (192-198, 652)
    MinimumSpeed = 4, // keep extra[0] mph through every checkpoint (231-240)
    Clean = 5,        // don't touch another car (224-230)
    Course = 7,       // timed checkpoint course: slalom, 180s (241-245, 608)
    Destroy = 8,      // ram the target car before it gets away (616-623)
    Map = 9,          // landmark to landmark with the map (241-245, 608)
};

struct LessonEvent {
    LessonType type = LessonType::Course;
    int rawType = 7;
    std::string file; // event point list name (race/<dir>/<file>.csv)
    std::vector<Checkpoint> checkpoints; // [0] = start
    float timeLimit = 0.0f;              // 0 = none
    float ambientDensity = 0.0f;
    float minimumSpeedMph = 0.0f;        // extra[0] (MinimumSpeed lessons)
    bool targetCar = false;              // extra[2]: an AI car is part of the event
    std::vector<float> extra;            // all extra columns
};

struct RaceSetup {
    RaceConfig config;
    const city::RaceDefinition* race = nullptr; // null for cruise
    city::RaceSettings settings;                // for the chosen difficulty
    std::vector<Checkpoint> checkpoints;        // race waypoints, [0] = start
    float timeLimit = 0.0f;                     // blitz: seconds; 0 = none
    int laps = 0;                               // circuit
    int mustPlace = 3;                          // tune/<city>.cinfo
    std::vector<OpponentSetup> opponents;
    std::vector<PoliceSetup> police;
    std::optional<city::AiMapConfig> aiMap;     // race .aimap(_p), or roam.aimap(_p) for cruise
    std::vector<LessonEvent> lessonEvents;      // crash course
    Mat34 playerSpawn;
};

// Loads the event described by `config`. Fails (returns std::nullopt and
// sets `error`) when the race does not exist or its waypoints are missing.
std::optional<RaceSetup> loadRaceSetup(const RaceConfig& config, const city::CityData& city, const vfs::Vfs& vfs,
                                       std::string* error = nullptr);

// Start transform for a waypoint: at its position, facing its heading.
Mat34 spawnAt(const Checkpoint& cp);

// Driving direction of an Angel heading (degrees): (sin h, 0, -cos h).
Vec3 headingDirection(float headingDeg);

} // namespace mm2::game::session
