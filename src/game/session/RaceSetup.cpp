#include "game/session/RaceSetup.h"

#include "city/RoomInfo.h"
#include "core/Log.h"
#include "core/StringUtil.h"
#include "game/session/Gate.h"

#include <cmath>
#include <cstdlib>
#include <format>

namespace mm2::game::session {
namespace {

void setError(std::string* error, std::string msg) {
    if (error)
        *error = std::move(msg);
}

std::optional<std::string> readText(const vfs::Vfs& vfs, const std::string& path) {
    auto bytes = vfs.readAll(path);
    if (!bytes)
        return std::nullopt;
    return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
}

std::optional<std::vector<Checkpoint>> loadCheckpoints(const vfs::Vfs& vfs, const std::string& path,
                                                       bool loop) {
    auto text = readText(vfs, path);
    if (!text)
        return std::nullopt;
    auto points = city::parseWaypoints(*text);
    if (!points || points->empty())
        return std::nullopt;
    return buildCheckpoints(*points, loop);
}

std::optional<city::RaceMode> raceMode(GameMode m) {
    switch (m) {
    case GameMode::Blitz: return city::RaceMode::Blitz;
    case GameMode::Circuit: return city::RaceMode::Circuit;
    case GameMode::Checkpoint: return city::RaceMode::Checkpoint;
    case GameMode::CrashCourse: return city::RaceMode::CrashCourse;
    default: return std::nullopt;
    }
}

// Heading that faces from `from` towards `to`, as mmWaypoints computes it
// for waypoints stored with heading 0.
float headingTowards(const Vec3& from, const Vec3& to) {
    return std::atan2(from.x - to.x, from.z - to.z) * -57.295776f;
}

std::uint32_t nextRandom(std::uint32_t& rng) {
    rng = rng * 1103515245u + 12345u;
    return (rng >> 16) & 0x7fffu;
}

} // namespace

std::vector<Checkpoint> buildCheckpoints(const std::vector<city::Waypoint>& points, bool loop) {
    std::vector<Checkpoint> out;
    out.reserve(points.size());
    for (const auto& p : points) {
        // mmPositions::Load reads the radius with atoi; 0 becomes 15.
        int radius = static_cast<int>(p.radius);
        if (radius == 0)
            radius = 15;
        Checkpoint cp = makeCheckpoint(p.position, p.heading, static_cast<float>(radius));
        // Column 6 ("frame rate" in the headers) is the hit flag.
        cp.hitByRadius = !p.extra.empty() && std::atoi(p.extra[0].c_str()) != 0;
        out.push_back(cp);
    }
    // A previous waypoint (from the second on) with heading 0 turns towards
    // this one; circuits also turn a zero-heading last waypoint towards the
    // first (mmWaypoints::LoadCSV / ReInit).
    auto setHeading = [](Checkpoint& cp, float heading) {
        cp.headingDeg = heading;
        calculateGatePoints(cp.position, heading, cp.radius, cp.gateA, cp.gateB);
    };
    const std::size_t n = out.size();
    for (std::size_t i = 2; i < n; ++i)
        if (out[i - 1].headingDeg == 0.0f)
            setHeading(out[i - 1], headingTowards(out[i - 1].position, out[i].position));
    if (loop && n > 1 && out[n - 1].headingDeg == 0.0f)
        setHeading(out[n - 1], headingTowards(out[n - 1].position, out[0].position));
    // Which stand is the "pt_finish" depends on the waypoint type
    // (mmWaypoints::LoadCSV): the caller marks it.
    out.front().start = true;
    return out;
}

Vec3 headingDirection(float headingDeg) {
    const float h = headingDeg * kDegToRad;
    return {std::sin(h), 0.0f, -std::cos(h)};
}

Mat34 spawnAt(const Checkpoint& cp) {
    // Mat34::rotationY(a) faces (-sin a, 0, -cos a); the waypoint heading h
    // faces (sin h, 0, -cos h), so a = -h (the original's GetStartAngle *
    // -0.017453292).
    Mat34 m = Mat34::rotationY(-cp.headingDeg * kDegToRad);
    m.m3 = cp.position;
    return m;
}

ResetPlace startPlace(const Checkpoint& cp) {
    return {cp.position, cp.headingDeg * -0.017453292f};
}

std::optional<Vec3> settleOnGround(const Vec3& body, const GroundProbe& probe) {
    if (!probe)
        return std::nullopt;
    const Vec3 from{body.x, body.y + 2.0f, body.z};
    const Vec3 to{body.x, body.y - 10.0f, body.z};
    auto hit = probe(from, to);
    if (!hit)
        return std::nullopt;
    hit->y = hit->y + 0.9f;
    return hit;
}

Vec3 findGroundPos(const Vec3& p, const GroundProbe& probe) {
    if (!probe)
        return p;
    const Vec3 from{p.x, p.y + 7.5f, p.z};
    const Vec3 to{p.x, p.y - 15.0f, p.z};
    if (auto hit = probe(from, to))
        return *hit;
    return p;
}

std::optional<Vec3> randomIntersectionStart(const city::CityData& city, std::uint32_t& rng) {
    if (!city.aiMap || city.aiMap->intersections.size() < 2)
        return std::nullopt;
    const auto& xs = city.aiMap->intersections;
    // The level's room flags (lvlRoomInfo, not the PSDL's): mmSingleRoam asks
    // for no subterranean or covered rooms (0x0A), and RespawnXYZ never takes
    // water-of-death or terrain-instance rooms (0x24).
    const auto& levelFlags = city.levelRoomFlags;
    constexpr std::uint16_t kRejected = city::LevelRoomFlag::Subterranean | city::LevelRoomFlag::Covered |
                                        city::LevelRoomFlag::WaterOfDeath | city::LevelRoomFlag::TerrainInstance;
    auto acceptable = [&](const city::AiIntersection& x) {
        if (x.room < levelFlags.size() && (levelFlags[x.room] & kRejected))
            return false;
        for (const auto pathId : x.paths) {
            if (pathId >= city.aiMap->paths.size())
                continue;
            // aiPath flags 0x4 (freeway) and 0x2 (alley), mm2hook's naming.
            if (city.aiMap->paths[pathId].flags & 0x6)
                return false;
        }
        return true;
    };
    // The original retries random picks until one fits; bound the search
    // and fall back to a scan so a city without a valid one cannot hang.
    for (int attempt = 0; attempt < 1000; ++attempt) {
        const auto& x = xs[1 + nextRandom(rng) % (xs.size() - 1)];
        if (acceptable(x))
            return x.center + Vec3{0.0f, 2.0f, 0.0f};
    }
    for (std::size_t i = 1; i < xs.size(); ++i)
        if (acceptable(xs[i]))
            return xs[i].center + Vec3{0.0f, 2.0f, 0.0f};
    return std::nullopt;
}

Vec3 multiplayerGridOffset(int slot, bool longVehicle) {
    static constexpr std::array<Vec3, 8> kShort{{{2.25f, 0, 6}, {-2.25f, 0, 6}, {4.5f, 0, 0}, {0, 0, 0},
                                                  {-4.5f, 0, 0}, {4.5f, 0, -6}, {0, 0, -6}, {-4.5f, 0, -6}}};
    static constexpr std::array<Vec3, 8> kLong{{{2.75f, 0, 16}, {-2.75f, 0, 16}, {5.5f, 0, 16}, {0, 0, 16},
                                                 {2.75f, 0, 34}, {-2.75f, 0, 34}, {0, 0, 34}, {5.5f, 0, 34}}};
    if (slot < 0 || slot > 7)
        return {};
    return (longVehicle ? kLong : kShort)[static_cast<std::size_t>(slot)];
}

void applyRaceTableDefaults(RaceConfig& cfg, const city::RaceDefinition* race) {
    if (cfg.mode == GameMode::Cruise) {
        // RaceMenuBase::SetStateRace for cruise.
        cfg.timeOfDay = TimeOfDay::Noon;
        cfg.weather = Weather::Clear;
        cfg.pedestrianDensity = 0.25f;
        cfg.trafficDensity = 0.5f;
        cfg.copDensity = 1.0f;
        return;
    }
    if (!race || !race->settings)
        return;
    const auto& s = cfg.difficulty == Difficulty::Professional ? race->settings->professional
                                                               : race->settings->amateur;
    cfg.timeOfDay = static_cast<TimeOfDay>(std::clamp(s.timeOfDay, 0, 3));
    cfg.weather = static_cast<Weather>(std::clamp(s.weather, 0, 3));
    cfg.pedestrianDensity = std::clamp(s.pedDensity, 0.0f, 1.0f);
    if (cfg.mode == GameMode::CrashCourse) {
        // Lessons: the lesson table's time, weather and pedestrians, no
        // traffic, all cops (mmInterface::Update, Crash Course GO).
        cfg.trafficDensity = 0.0f;
        cfg.copDensity = 1.0f;
        cfg.opponents = 0;
        return;
    }
    cfg.trafficDensity = std::clamp(s.ambientDensity, 0.0f, 1.0f);
    // MM2 keeps the race's cop count in the cop density; OpenMM2's densities
    // are 0..1, so the count is clamped (the slider shows full either way).
    cfg.copDensity = std::clamp(static_cast<float>(s.cops), 0.0f, 1.0f);
    if (cfg.mode == GameMode::Checkpoint) {
        cfg.opponents = std::max(0, s.opponents);
        cfg.laps = 1;
    } else if (cfg.mode == GameMode::Circuit) {
        cfg.opponents = std::max(0, s.opponents);
        cfg.laps = std::max(1, s.numLaps);
    }
}

std::optional<RaceSetup> loadRaceSetup(const RaceConfig& config, const city::CityData& city, const vfs::Vfs& vfs,
                                       std::string* error, std::uint32_t seed) {
    RaceSetup s;
    s.config = config;
    const bool pro = config.difficulty == Difficulty::Professional;

    const auto mode = raceMode(config.mode);
    if (mode) {
        for (const auto& r : city.races)
            if (r.mode == *mode && r.index == config.raceIndex)
                s.race = &r;
        if (!s.race) {
            setError(error, std::format("{} has no {} race {}", city.info.mapName, city::raceModeName(*mode),
                                        config.raceIndex));
            return std::nullopt;
        }
        if (s.race->settings)
            s.settings = pro ? s.race->settings->professional : s.race->settings->amateur;
    }

    // Waypoints (crash courses keep theirs per event).
    if (s.race && config.mode != GameMode::CrashCourse) {
        const bool circuit = config.mode == GameMode::Circuit;
        auto cps = loadCheckpoints(vfs, s.race->waypoints, circuit);
        if (!cps) {
            setError(error, std::format("cannot read waypoints '{}'", s.race->waypoints));
            return std::nullopt;
        }
        s.checkpoints = std::move(*cps);
        // mmWaypoints::LoadCSV: circuits (type 1) make waypoint 0, the start
        // and finish of every lap, the "pt_finish"; checkpoint races (type 2)
        // the last waypoint; Blitz (type 3) uses "pt_check" for every one.
        if (circuit)
            s.checkpoints.front().finish = true;
        else if (config.mode == GameMode::Checkpoint)
            s.checkpoints.back().finish = true;
    }

    // Time limit: single player Blitz only (mmSingleBlitz::InitHUD); the
    // circuit and checkpoint tables carry 50 / 40, which their single player
    // rules never start a clock for.
    if (config.mode == GameMode::Blitz)
        s.timeLimit = s.settings.timeLimit;
    s.laps = config.mode == GameMode::Circuit ? (config.laps > 0 ? config.laps : s.settings.numLaps) : 0;
    if (config.mode == GameMode::Circuit && s.laps <= 0)
        s.laps = 1;

    // AI setup: race .aimap(_p), or roam.aimap(_p) for cruise.
    if (s.race) {
        const std::string& path = pro && !s.race->aiMapPro.empty() ? s.race->aiMapPro : s.race->aiMap;
        if (!path.empty())
            if (auto text = readText(vfs, path))
                s.aiMap = city::parseAiMapConfig(*text);
    } else {
        s.aiMap = pro && city.cruisePro ? city.cruisePro : city.cruise;
    }

    // Crash course events (mmSingleStunt::LoadEventFile).
    if (config.mode == GameMode::CrashCourse && s.race) {
        const std::string& table = pro && !s.race->crashEventsPro.empty() ? s.race->crashEventsPro : s.race->crashEvents;
        auto text = readText(vfs, table);
        auto events = text ? city::parseCrashEvents(*text) : std::nullopt;
        if (!events || events->empty()) {
            setError(error, std::format("cannot read crash course events '{}'", table));
            return std::nullopt;
        }
        const std::string dir = table.substr(0, table.rfind('/') + 1);
        for (const auto& e : *events) {
            LessonEvent le;
            le.rawType = e.event;
            le.type = static_cast<LessonType>(e.event);
            le.file = e.file;
            le.hasCheckpoints = e.checkpoints != 0;
            le.timeLimit = e.timeLimit;
            le.ambientDensity = e.ambientDensity;
            le.extra = e.extra;
            // cornerspeed is a float; chkflags and numopp are read with atoi.
            if (!e.extra.empty())
                le.minimumSpeedMph = e.extra[0];
            if (le.type == LessonType::MinimumSpeed && le.minimumSpeedMph < 1.0f)
                le.minimumSpeedMph = 50.0f; // mmSingleStunt::InitHUD
            // mmSingleStunt::InitNewEvent passes "chkflags != 0" to
            // mmWaypoints::ReInit as the show-only-the-next flag.
            le.singleCheckpoint = e.extra.size() > 1 && static_cast<int>(e.extra[1]) != 0;
            le.opponents = e.extra.size() > 2 ? static_cast<int>(e.extra[2]) : 0;
            auto cps = loadCheckpoints(vfs, dir + str::lower(e.file) + ".csv", false);
            if (!cps) {
                setError(error, std::format("cannot read crash course points '{}{}.csv'", dir, e.file));
                return std::nullopt;
            }
            le.checkpoints = std::move(*cps);
            s.lessonEvents.push_back(std::move(le));
        }
        s.checkpoints = s.lessonEvents.front().checkpoints;
    }

    // Opponents: the aimap lists the field; the configuration says how many
    // race. Crash courses load all of theirs; each event's "numopp" says
    // which take part.
    const bool racing = config.mode == GameMode::Circuit || config.mode == GameMode::Checkpoint;
    if (s.aiMap && s.race && (racing || config.mode == GameMode::CrashCourse)) {
        const std::string& any = !s.race->aiMap.empty() ? s.race->aiMap : s.race->waypoints;
        const std::string dir = any.substr(0, any.rfind('/') + 1);
        // aiMap::Init loads min(table count, OpponentDensity) racers; the
        // crash course sets OpponentDensity to 8 (CrashCourse::SetEnvironment).
        const int wanted = !racing ? 8 : (config.opponents >= 0 ? config.opponents : s.settings.opponents);
        for (const auto& o : s.aiMap->opponents) {
            if (static_cast<int>(s.opponents.size()) >= wanted)
                break;
            OpponentSetup op;
            op.vehicle = str::lower(o.car);
            op.pathFile = dir + str::lower(o.pathFile);
            op.params = o.params;
            if (auto text = readText(vfs, op.pathFile))
                if (auto path = city::parseOpponentPath(*text))
                    op.path = std::move(*path);
            // Grid place (aiRouteRacer::Init): the .opp's first row; its
            // fourth column (the parser's "brake") is the car's reset angle
            // in degrees, converted with the original's 0.017444445 and not
            // negated as the player's start angle is.
            if (!op.path.empty()) {
                op.place = {op.path.front().position, op.path.front().brake * 0.017444445f};
                op.spawn = Mat34::rotationY(op.place.angle);
                op.spawn.m3 = op.place.position;
            } else {
                op.spawn = s.checkpoints.empty() ? Mat34::identity() : spawnAt(s.checkpoints.front());
                if (!s.checkpoints.empty())
                    op.place = startPlace(s.checkpoints.front());
            }
            s.opponents.push_back(std::move(op));
        }
    }
    if (s.aiMap) {
        for (const auto& p : s.aiMap->police) {
            PoliceSetup ps;
            ps.vehicle = str::lower(p.car);
            ps.spawn = Mat34::rotationY(-p.heading * kDegToRad);
            ps.spawn.m3 = p.position;
            ps.params = p.params;
            s.police.push_back(std::move(ps));
        }
    }

    // Player start: the first waypoint (mmWaypoints::GetStart /
    // GetStartAngle). Cruise starts at a random AI intersection facing -Z
    // (mmSingleRoam::InitOtherPlayers -> mmGame::RespawnXYZ); without an AI
    // map, the city's first Blitz start.
    //
    // The car is placed there (the modes' InitGameObjects: SetResetPos and
    // vehCar::Reset); then the race modes' InitOtherPlayers settle it on the
    // ground (settleOnGround), while mmSingleRoam::InitOtherPlayers leaves
    // it at RespawnXYZ's point, 2 m above the intersection.
    std::uint32_t rng = seed;
    s.playerDrop = StartDrop::OnGround;
    if (!s.checkpoints.empty()) {
        s.playerSpawn = spawnAt(s.checkpoints.front());
        s.playerPlace = startPlace(s.checkpoints.front());
        // mmMultiBlitz / mmMultiCircuit / mmMultiRace::InitNetworkPlayers:
        // the grid slot (StartXYZ, added by the race screen) goes through
        // mmGame::FindGroundPos before the one reset.
        if (config.multiplayer)
            s.playerDrop = StartDrop::FindGround;
    } else if (auto p = randomIntersectionStart(city, rng)) {
        s.playerSpawn = Mat34::identity();
        s.playerSpawn.m3 = *p;
        s.playerPlace = {*p, 0.0f};
        s.playerDrop = StartDrop::None;
    } else {
        // OpenMM2's fallback for a city without an AI map (RespawnXYZ would
        // use (0, 20, 0)): the city's first Blitz start.
        for (const auto& r : city.races) {
            if (r.mode != city::RaceMode::Blitz)
                continue;
            if (auto cps = loadCheckpoints(vfs, r.waypoints, false)) {
                s.playerSpawn = spawnAt(cps->front());
                s.playerPlace = startPlace(cps->front());
                break;
            }
        }
    }
    return s;
}

} // namespace mm2::game::session
