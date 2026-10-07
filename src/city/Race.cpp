#include "city/Race.h"

#include "core/StringUtil.h"
#include "data/TextTables.h"

#include <format>

namespace mm2::city {
namespace {

void setError(std::string* error, std::string msg) {
    if (error)
        *error = std::move(msg);
}

float toFloat(std::string_view s, float fallback = 0.0f) {
    auto d = str::parseDouble(s);
    return d ? static_cast<float>(*d) : fallback;
}

int toInt(std::string_view s, int fallback = 0) {
    if (auto i = str::parseInt(s))
        return static_cast<int>(*i);
    if (auto d = str::parseDouble(s))
        return static_cast<int>(*d);
    return fallback;
}

// Whitespace tokenizer for .aimap lines.
std::vector<std::string_view> tokens(std::string_view line) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
            ++i;
        const std::size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t')
            ++i;
        if (i > start)
            out.push_back(line.substr(start, i - start));
    }
    return out;
}

// Rows of a CSV that start with numbers (skips the header and blank rows).
template <class Fn>
bool forNumericRows(std::string_view text, std::size_t minColumns, std::string* error, Fn&& fn) {
    const auto table = data::CsvTable::parse(text, /*hasHeader=*/true);
    for (std::size_t r = 0; r < table.rows().size(); ++r) {
        const auto& row = table.rows()[r];
        if (row.empty() || (row.size() == 1 && row[0].empty()))
            continue;
        if (row.size() < minColumns || !str::parseDouble(row[0])) {
            setError(error, std::format("row {}: expected at least {} numeric columns", r + 2, minColumns));
            return false;
        }
        fn(row);
    }
    return true;
}

RaceSettings settingsFrom(const std::vector<std::string>& row, std::size_t first) {
    auto at = [&](std::size_t i) -> std::string_view {
        return first + i < row.size() ? std::string_view(row[first + i]) : std::string_view();
    };
    RaceSettings s;
    s.carType = toInt(at(0));
    s.timeOfDay = toInt(at(1));
    s.weather = toInt(at(2));
    s.opponents = toInt(at(3));
    s.cops = toInt(at(4));
    s.ambientDensity = toFloat(at(5));
    s.pedDensity = toFloat(at(6));
    s.numLaps = toInt(at(7));
    s.timeLimit = toFloat(at(8));
    s.difficulty = toFloat(at(9));
    return s;
}

} // namespace

CityInfo parseCityInfo(std::string_view text) {
    const auto kv = data::KeyValueFile::parse(text);
    CityInfo c;
    c.localizedName = kv.getString("LocalizedName");
    c.mapName = kv.getString("MapName");
    c.raceDir = kv.getString("RaceDir", c.mapName);
    c.blitzCount = kv.getInt("BlitzCount");
    c.circuitCount = kv.getInt("CircuitCount");
    c.checkpointCount = kv.getInt("CheckpointCount");
    c.blitzNames = kv.getList("BlitzNames");
    c.circuitNames = kv.getList("CircuitNames");
    c.checkpointNames = kv.getList("CheckpointNames");
    c.mustPlace = kv.getInt("MustPlace");
    c.unlockGroup = kv.getInt("UnlockGroup");
    return c;
}

std::optional<std::vector<Waypoint>> parseWaypoints(std::string_view text, std::string* error) {
    std::vector<Waypoint> out;
    const bool ok = forNumericRows(text, 5, error, [&](const std::vector<std::string>& row) {
        Waypoint w;
        w.position = {toFloat(row[0]), toFloat(row[1]), toFloat(row[2])};
        w.heading = toFloat(row[3]);
        w.radius = toFloat(row[4]);
        w.extra.assign(row.begin() + 5, row.end());
        out.push_back(std::move(w));
    });
    if (!ok)
        return std::nullopt;
    return out;
}

std::optional<std::vector<OpponentPoint>> parseOpponentPath(std::string_view text, std::string* error) {
    std::vector<OpponentPoint> out;
    const bool ok = forNumericRows(text, 9, error, [&](const std::vector<std::string>& row) {
        OpponentPoint p;
        p.position = {toFloat(row[0]), toFloat(row[1]), toFloat(row[2])};
        p.brake = toFloat(row[3]);
        p.forwardOffset = toFloat(row[4]);
        p.sideOffset = toFloat(row[5]);
        p.targetSpeed = toFloat(row[6]);
        p.speedStart = toFloat(row[7]);
        p.sideStart = toFloat(row[8]);
        out.push_back(p);
    });
    if (!ok)
        return std::nullopt;
    return out;
}

std::optional<AiMapConfig> parseAiMapConfig(std::string_view text, std::string* error) {
    // Split into sections: "[Name]" then lines up to the next header ('#'
    // comment lines and blank lines are ignored).
    AiMapConfig cfg;
    AiMapSection* cur = nullptr;
    for (auto raw : data::splitLines(text)) {
        const auto line = str::trim(raw);
        if (line.empty() || line[0] == '#')
            continue;
        if (line.front() == '[' && line.back() == ']') {
            cfg.sections.push_back({std::string(str::trim(line.substr(1, line.size() - 2))), false, {}});
            cur = &cfg.sections.back();
            continue;
        }
        if (!cur) {
            setError(error, std::format("line '{}' before any section", line));
            return std::nullopt;
        }
        cur->lines.emplace_back(line);
    }
    // A section is a list when its first line is an integer equal to the
    // number of lines that follow it.
    for (auto& sec : cfg.sections) {
        if (sec.lines.empty())
            continue;
        const auto n = str::parseInt(sec.lines[0]);
        if (n && *n >= 0 && static_cast<std::size_t>(*n) == sec.lines.size() - 1) {
            sec.isList = true;
            sec.lines.erase(sec.lines.begin());
        }
    }

    for (const auto& sec : cfg.sections) {
        const auto& name = sec.name;
        auto firstNumber = [&]() -> std::optional<float> {
            if (sec.isList || sec.lines.empty())
                return std::nullopt;
            auto d = str::parseDouble(tokens(sec.lines[0]).empty() ? "" : tokens(sec.lines[0])[0]);
            return d ? std::optional<float>(static_cast<float>(*d)) : std::nullopt;
        };
        if (str::iequals(name, "Speed Limit")) {
            cfg.speedLimit = firstNumber();
        } else if (str::iequals(name, "Density")) {
            cfg.density = firstNumber();
        } else if (str::iequals(name, "CopChaseDistance")) {
            cfg.copChaseDistance = firstNumber();
        } else if (str::iequals(name, "AmbientLaneChanges")) {
            if (auto v = firstNumber())
                cfg.ambientLaneChanges = static_cast<int>(*v);
        } else if (str::iequals(name, "Ambients Drive On The Left")) {
            if (auto v = firstNumber())
                cfg.driveOnLeft = static_cast<int>(*v);
        } else if (str::iequals(name, "Traffic Lights")) {
            for (const auto& l : sec.lines)
                for (auto t : tokens(l))
                    cfg.trafficLights.emplace_back(t);
        } else if (sec.isList) {
            for (const auto& l : sec.lines) {
                const auto t = tokens(l);
                if (t.empty())
                    continue;
                if (str::iequals(name, "Exceptions")) {
                    if (t.size() < 3) {
                        setError(error, "[Exceptions]: short line");
                        return std::nullopt;
                    }
                    cfg.exceptions.push_back({toInt(t[0]), toFloat(t[1]), toFloat(t[2])});
                } else if (str::iequals(name, "Opponent")) {
                    if (t.size() < 2) {
                        setError(error, "[Opponent]: short line");
                        return std::nullopt;
                    }
                    AiOpponentInit o{std::string(t[0]), std::string(t[1]), {}};
                    for (std::size_t j = 2; j < t.size(); ++j)
                        o.params.push_back(toFloat(t[j]));
                    cfg.opponents.push_back(std::move(o));
                } else if (str::iequals(name, "Police")) {
                    AiPoliceInit p;
                    p.car = std::string(t[0]);
                    std::vector<float> nums;
                    for (std::size_t j = 1; j < t.size(); ++j)
                        nums.push_back(toFloat(t[j]));
                    if (nums.size() >= 4) {
                        p.position = {nums[0], nums[1], nums[2]};
                        p.heading = nums[3];
                        p.params.assign(nums.begin() + 4, nums.end());
                    } else {
                        p.params = std::move(nums);
                    }
                    cfg.police.push_back(std::move(p));
                } else if (str::iequals(name, "Ambient Types/Density")) {
                    cfg.ambientTypes.push_back({std::string(t[0]), t.size() > 1 ? toFloat(t[1]) : 0.0f,
                                                t.size() > 2 ? toFloat(t[2]) : 0.0f});
                } else if (str::istartsWith(name, "GoodWeatherPedName")) {
                    cfg.pedNames.emplace_back(std::string(t[0]),
                                              t.size() > 1 ? std::string(t[1]) : std::string(t[0]));
                }
            }
        }
    }
    if (cfg.sections.empty()) {
        setError(error, "no sections");
        return std::nullopt;
    }
    return cfg;
}

std::optional<std::vector<RaceTableEntry>> parseRaceTable(std::string_view text, std::string* error) {
    const auto table = data::CsvTable::parse(text, true);
    std::vector<RaceTableEntry> out;
    for (std::size_t r = 0; r < table.rows().size(); ++r) {
        const auto& row = table.rows()[r];
        if (row.size() < 21) {
            setError(error, std::format("row {}: expected 21 columns, got {}", r + 2, row.size()));
            return std::nullopt;
        }
        out.push_back({row[0], settingsFrom(row, 1), settingsFrom(row, 11)});
    }
    return out;
}

std::optional<std::vector<CrashEvent>> parseCrashEvents(std::string_view text, std::string* error) {
    const auto table = data::CsvTable::parse(text, true);
    std::vector<CrashEvent> out;
    for (std::size_t r = 0; r < table.rows().size(); ++r) {
        const auto& row = table.rows()[r];
        if (row.empty() || row[0].empty())
            continue;
        if (row.size() < 5) {
            setError(error, std::format("row {}: expected at least 5 columns", r + 2));
            return std::nullopt;
        }
        CrashEvent e;
        e.file = row[0];
        e.event = toInt(row[1]);
        e.checkpoints = toInt(row[2]);
        e.timeLimit = toFloat(row[3]);
        e.ambientDensity = toFloat(row[4]);
        for (std::size_t i = 5; i < row.size(); ++i)
            if (!row[i].empty())
                e.extra.push_back(toFloat(row[i]));
        out.push_back(std::move(e));
    }
    return out;
}

std::vector<RaceReward> parseRewards(std::string_view text) {
    std::vector<RaceReward> out;
    bool first = true;
    for (auto line : data::splitLines(text)) {
        if (first) { // header
            first = false;
            continue;
        }
        if (str::trim(line).empty())
            continue;
        // The message may itself contain commas; split the first four fields only.
        std::vector<std::string_view> f;
        std::size_t pos = 0;
        for (int k = 0; k < 4; ++k) {
            const auto c = line.find(',', pos);
            if (c == std::string_view::npos)
                break;
            f.push_back(str::trim(line.substr(pos, c - pos)));
            pos = c + 1;
        }
        if (f.size() < 4)
            continue;
        std::string_view msg = str::trim(line.substr(pos));
        if (msg.ends_with(','))
            msg.remove_suffix(1);
        out.push_back(
            {std::string(f[0]), std::string(f[1]), std::string(f[2]), toInt(f[3]), std::string(msg)});
    }
    return out;
}

const char* raceModeName(RaceMode m) {
    switch (m) {
    case RaceMode::Blitz:
        return "Blitz";
    case RaceMode::Circuit:
        return "Circuit";
    case RaceMode::Checkpoint:
        return "Checkpoint";
    case RaceMode::CrashCourse:
        return "Crash Course";
    }
    return "?";
}

const char* raceModePrefix(RaceMode m) {
    switch (m) {
    case RaceMode::Blitz:
        return "blitz";
    case RaceMode::Circuit:
        return "circuit";
    case RaceMode::Checkpoint:
        return "race";
    case RaceMode::CrashCourse:
        return "crash";
    }
    return "";
}

} // namespace mm2::city
