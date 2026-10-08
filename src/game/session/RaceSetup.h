#pragma once

// Everything a session needs to know about one event, gathered from the
// city's race files (race/<dir>/...), for a game::RaceConfig.

#include "city/CityData.h"
#include "game/RaceConfig.h"
#include "game/session/Types.h"
#include "vfs/Vfs.h"

#include <cstdint>
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

// Crash course event types (column "Event" of crash<N>data.csv): the cases
// of mmSingleStunt::UpdateGame, named after their Update functions' purpose.
enum class LessonType : std::uint8_t {
    Jump = 0,         // UpdateJump: checkpoints in any order, the last one ends it (206-209, 614-615)
    Collide = 1,      // UpdateCollide: drive cleanly to the end (210-216); not in the retail data
    Follow = 2,       // UpdateChase: be within 10 m of the car when it arrives (218-223)
    Evade = 3,        // UpdateEvade: reach the end with no cop pursuing (193-198, 652)
    MinimumSpeed = 4, // UpdateCorner: keep `cornerspeed` through the checkpoints (232-240)
    Clean = 5,        // UpdateFrogger: MaxDamage 10, any damage fails (225-230)
    Acceleration = 6, // UpdateAccel (199-204); not in the retail data
    Course = 7,       // UpdateBlitz: checkpoints in order against the clock (241-245, 609)
    Destroy = 8,      // UpdateStop: wreck the car before it arrives (617-622)
    Map = 9,          // UpdateBlitz, like Course (the Knowledge)
};

// One row of crash<N>data.csv (mmSingleStunt::LoadEventFile reads it into
// mmCCData). The retail header of crash6data names the extra columns
// "cornerspeed, chkflags, numopp".
struct LessonEvent {
    LessonType type = LessonType::Course;
    int rawType = 7;
    std::string file;                    // event point list name (race/<dir>/<file>.csv)
    bool hasCheckpoints = true;          // column "Checkpoints": 0 = the event has no checkpoints
    std::vector<Checkpoint> checkpoints; // [0] = start
    float timeLimit = 0.0f;              // seconds
    float ambientDensity = 0.0f;
    float minimumSpeedMph = 0.0f;        // "cornerspeed"; MinimumSpeed events use 50 below 1
    bool singleCheckpoint = false;       // "chkflags" bit 0: only the next checkpoint is shown
    int opponents = 0;                   // "numopp": AI cars taking part in this event
    std::vector<float> extra;            // all extra columns
};

struct RaceSetup {
    RaceConfig config;
    const city::RaceDefinition* race = nullptr; // null for cruise
    city::RaceSettings settings;                // for the chosen difficulty
    std::vector<Checkpoint> checkpoints;        // race waypoints, [0] = start
    float timeLimit = 0.0f;                     // blitz: seconds; 0 = none
    int laps = 0;                               // circuit
    std::vector<OpponentSetup> opponents;
    std::vector<PoliceSetup> police;
    std::optional<city::AiMapConfig> aiMap;     // race .aimap(_p), or roam.aimap(_p) for cruise
    std::vector<LessonEvent> lessonEvents;      // crash course
    Mat34 playerSpawn;
};

// Loads the event described by `config`. Fails (returns std::nullopt and
// sets `error`) when the race does not exist or its waypoints are missing.
//
// (See also applyRaceTableDefaults below.)
// `seed` drives the random parts (the cruise start).
std::optional<RaceSetup> loadRaceSetup(const RaceConfig& config, const city::CityData& city, const vfs::Vfs& vfs,
                                       std::string* error = nullptr, std::uint32_t seed = 1);

// Waypoint list as mmWaypoints::LoadCSV / ReInit build it: the radius is
// read as an integer (0 means 15 m) and a zero heading is replaced by the
// direction to the next waypoint (from the third row on; `loop`: the last
// row also turns towards the first, as circuits do).
std::vector<Checkpoint> buildCheckpoints(const std::vector<city::Waypoint>& points, bool loop);

// Start transform for a waypoint: at its position, facing its heading.
Mat34 spawnAt(const Checkpoint& cp);

// Driving direction of an Angel heading (degrees): (sin h, 0, -cos h).
Vec3 headingDirection(float headingDeg);

// mmGame::RespawnXYZ: a random AI intersection (not the first), skipping
// those in underground, road or building rooms and those on freeways or
// alleys; 2 m above its centre. Nothing when the city has no AI map.
std::optional<Vec3> randomIntersectionStart(const city::CityData& city, std::uint32_t& rng);

// The settings a race runs with by default (RaceMenuBase::SetStateRace,
// the Crash Course page's GO): the race table's time of day, weather and
// densities for the driver's difficulty, a circuit's laps and opponents.
// The modes' RegisterFinish register a finish only under these. `race` is
// the race's definition (null for cruise).
void applyRaceTableDefaults(RaceConfig& cfg, const city::RaceDefinition* race);

// mmGameMulti::StartXYZ: where the player in start slot `slot` (0..7)
// starts a multiplayer race, relative to the start waypoint in its own frame
// (+X right, +Z behind). Cars with a trailer or a model radius over 6 m use
// the wider grid (16 and 34 m back), the others one 6 m apart.
Vec3 multiplayerGridOffset(int slot, bool longVehicle);

} // namespace mm2::game::session
