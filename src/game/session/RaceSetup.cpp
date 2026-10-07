#include "game/session/RaceSetup.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "game/session/Gate.h"

#include <cmath>
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

std::optional<std::vector<Checkpoint>> loadCheckpoints(const vfs::Vfs& vfs, const std::string& path) {
    auto text = readText(vfs, path);
    if (!text)
        return std::nullopt;
    auto points = city::parseWaypoints(*text);
    if (!points || points->empty())
        return std::nullopt;
    std::vector<Checkpoint> out;
    for (const auto& p : *points)
        out.push_back(makeCheckpoint(p.position, p.heading, p.radius));
    out.front().start = true;
    out.back().finish = out.size() > 1;
    return out;
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

} // namespace

Vec3 headingDirection(float headingDeg) {
    const float h = headingDeg * kDegToRad;
    return {std::sin(h), 0.0f, -std::cos(h)};
}

Mat34 spawnAt(const Checkpoint& cp) {
    // Mat34::rotationY(a) faces (-sin a, 0, -cos a); the waypoint heading h
    // faces (sin h, 0, -cos h), so a = -h (verified against the direction
    // from each race's first to its second waypoint).
    Mat34 m = Mat34::rotationY(-cp.headingDeg * kDegToRad);
    m.m3 = cp.position;
    return m;
}

std::optional<RaceSetup> loadRaceSetup(const RaceConfig& config, const city::CityData& city, const vfs::Vfs& vfs,
                                       std::string* error) {
    RaceSetup s;
    s.config = config;
    s.mustPlace = city.info.mustPlace > 0 ? city.info.mustPlace : 3;
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
        auto cps = loadCheckpoints(vfs, s.race->waypoints);
        if (!cps) {
            setError(error, std::format("cannot read waypoints '{}'", s.race->waypoints));
            return std::nullopt;
        }
        s.checkpoints = std::move(*cps);
        if (config.mode == GameMode::Circuit) {
            // The start line is also the finish line of every lap.
            s.checkpoints.back().finish = false;
            s.checkpoints.front().finish = true;
        }
    }

    // Time limit: used by Blitz only. Circuit and checkpoint races carry a
    // constant value (50 amateur / 40 pro) that MM1's rules for those modes
    // never read (inferred: unused in single player).
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

    // Crash course events.
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
            le.timeLimit = e.timeLimit;
            le.ambientDensity = e.ambientDensity;
            le.extra = e.extra;
            if (!e.extra.empty())
                le.minimumSpeedMph = e.extra[0];
            le.targetCar = e.extra.size() > 2 && e.extra[2] != 0.0f;
            auto cps = loadCheckpoints(vfs, dir + str::lower(e.file) + ".csv");
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
    // race. Crash courses use all of theirs (the car to follow or to ram).
    const bool racing = config.mode == GameMode::Circuit || config.mode == GameMode::Checkpoint;
    if (s.aiMap && s.race && (racing || config.mode == GameMode::CrashCourse)) {
        const std::string& any = !s.race->aiMap.empty() ? s.race->aiMap : s.race->waypoints;
        const std::string dir = any.substr(0, any.rfind('/') + 1);
        const int wanted = !racing ? 64 : (config.opponents >= 0 ? config.opponents : s.settings.opponents);
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
            // Grid place: the .opp's first row, whose fourth number is the
            // car's heading in degrees (MM2 aiRouteRacer::Init: the
            // vehCarSim reset heading is that times 0.017444445, i.e.
            // 3.14 / 180, in Mat34::rotationY's sense; checked against the
            // races' start headings).
            if (!op.path.empty()) {
                op.spawn = Mat34::rotationY(op.path.front().brake * 0.017444445f);
                op.spawn.m3 = op.path.front().position;
            } else {
                op.spawn = spawnAt(s.checkpoints.empty() ? Checkpoint{} : s.checkpoints.front());
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

    // Player start: the first waypoint. Cruise has no start in the data; use
    // the city's first Blitz start (inferred).
    if (!s.checkpoints.empty()) {
        s.playerSpawn = spawnAt(s.checkpoints.front());
    } else {
        for (const auto& r : city.races) {
            if (r.mode != city::RaceMode::Blitz)
                continue;
            if (auto cps = loadCheckpoints(vfs, r.waypoints)) {
                s.playerSpawn = spawnAt(cps->front());
                break;
            }
        }
    }
    return s;
}

} // namespace mm2::game::session
