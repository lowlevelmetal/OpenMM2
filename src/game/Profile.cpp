#include "game/Profile.h"

#include "city/CityData.h"
#include "city/Race.h"
#include "core/Ini.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <format>

namespace mm2::game {
namespace {

const char* difficultyKey(Difficulty d) { return d == Difficulty::Professional ? "pro" : "amateur"; }

template <class E>
E clampEnum(long long v, E max) {
    return static_cast<E>(std::clamp<long long>(v, 0, static_cast<long long>(max)));
}

std::string_view asText(const std::vector<std::byte>& b) { return {reinterpret_cast<const char*>(b.data()), b.size()}; }

} // namespace

const char* modeKey(GameMode mode) {
    switch (mode) {
    case GameMode::Cruise: return "cruise";
    case GameMode::Blitz: return "blitz";
    case GameMode::Checkpoint: return "race";
    case GameMode::Circuit: return "circuit";
    case GameMode::CrashCourse: return "crash";
    case GameMode::CopsAndRobbers: return "cops";
    }
    return "cruise";
}

// --- Profile ---------------------------------------------------------------------

std::string Profile::raceKey(Difficulty d, std::string_view city, std::string_view mode, int index) {
    return std::format("{}.{}.{}.{}", difficultyKey(d), str::lower(city), mode, index);
}

const RaceRecord* Profile::record(Difficulty d, std::string_view cityName, std::string_view modeName, int index) const {
    const auto it = races.find(raceKey(d, cityName, modeName, index));
    return it == races.end() ? nullptr : &it->second;
}

bool Profile::load(const std::filesystem::path& path) {
    IniFile ini;
    if (!ini.load(path))
        return false;
    *this = Profile{};
    file = path;
    name = ini.getString("Driver", "Name");
    if (name.empty())
        return false;
    score = static_cast<int>(ini.getInt("Driver", "Score", 0));
    lastRace = ini.getString("Driver", "LastRace");
    lastVehicle = ini.getString("Driver", "LastVehicle");
    netName = ini.getString("Driver", "NetName", name);

    vehicle = ini.getString("Prefs", "Vehicle", vehicle);
    vehicleColor = static_cast<int>(ini.getInt("Prefs", "Color", 0));
    automatic = ini.getBool("Prefs", "Automatic", true);
    difficulty = clampEnum(ini.getInt("Prefs", "Difficulty", 0), Difficulty::Professional);
    city = ini.getString("Prefs", "City", city);
    mode = clampEnum(ini.getInt("Prefs", "Mode", 0), GameMode::CopsAndRobbers);
    raceIndex = static_cast<int>(ini.getInt("Prefs", "Race", 0));
    timeOfDay = clampEnum(ini.getInt("Prefs", "TimeOfDay", 1), TimeOfDay::Night);
    weather = clampEnum(ini.getInt("Prefs", "Weather", 0), Weather::Snow);
    pedestrianDensity = std::clamp(static_cast<float>(ini.getDouble("Prefs", "Pedestrians", 0.5)), 0.0f, 1.0f);
    trafficDensity = std::clamp(static_cast<float>(ini.getDouble("Prefs", "Traffic", 0.5)), 0.0f, 1.0f);
    copDensity = std::clamp(static_cast<float>(ini.getDouble("Prefs", "Cops", 0.5)), 0.0f, 1.0f);
    opponents = static_cast<int>(std::clamp<long long>(ini.getInt("Prefs", "Opponents", 3), 0, 7));
    laps = static_cast<int>(std::clamp<long long>(ini.getInt("Prefs", "Laps", 3), 1, 10));

    for (const auto& key : ini.keys("Races")) {
        const std::string value = ini.getString("Races", key); // split() returns views into it
        const auto parts = str::split(value, ',');
        RaceRecord r;
        if (!parts.empty())
            r.bestPosition = static_cast<int>(str::parseInt(parts[0]).value_or(0));
        if (parts.size() > 1)
            r.bestTime = static_cast<float>(str::parseDouble(parts[1]).value_or(0.0));
        if (parts.size() > 2)
            r.wins = static_cast<int>(str::parseInt(parts[2]).value_or(0));
        races[str::lower(key)] = r;
    }
    for (const auto& key : ini.keys("Crash")) {
        const std::string v = str::lower(ini.getString("Crash", key));
        if (v == "passed")
            crashPassed.insert(str::lower(key));
        else if (v == "failed")
            crashFailed.insert(str::lower(key));
    }
    return true;
}

bool Profile::save() const {
    IniFile ini;
    ini.parse("; OpenMM2 driver profile\n");
    ini.set("Driver", "Name", name);
    ini.setInt("Driver", "Score", score);
    ini.set("Driver", "LastRace", lastRace);
    ini.set("Driver", "LastVehicle", lastVehicle);
    ini.set("Driver", "NetName", netName);
    ini.set("Prefs", "Vehicle", vehicle);
    ini.setInt("Prefs", "Color", vehicleColor);
    ini.setBool("Prefs", "Automatic", automatic);
    ini.setInt("Prefs", "Difficulty", static_cast<int>(difficulty));
    ini.set("Prefs", "City", city);
    ini.setInt("Prefs", "Mode", static_cast<int>(mode));
    ini.setInt("Prefs", "Race", raceIndex);
    ini.setInt("Prefs", "TimeOfDay", static_cast<int>(timeOfDay));
    ini.setInt("Prefs", "Weather", static_cast<int>(weather));
    ini.setDouble("Prefs", "Pedestrians", pedestrianDensity);
    ini.setDouble("Prefs", "Traffic", trafficDensity);
    ini.setDouble("Prefs", "Cops", copDensity);
    ini.setInt("Prefs", "Opponents", opponents);
    ini.setInt("Prefs", "Laps", laps);
    for (const auto& [key, r] : races)
        ini.set("Races", key, std::format("{},{:.2f},{}", r.bestPosition, r.bestTime, r.wins));
    for (const auto& key : crashFailed)
        if (!crashPassed.contains(key))
            ini.set("Crash", key, "failed");
    for (const auto& key : crashPassed)
        ini.set("Crash", key, "passed");
    return ini.save(file);
}

// --- ProfileStore ------------------------------------------------------------------

ProfileStore::ProfileStore(std::filesystem::path dir) : m_dir(std::move(dir)) {}

std::filesystem::path ProfileStore::defaultDir() { return paths::userDataDir() / "players"; }

std::vector<Profile> ProfileStore::list() const {
    std::vector<Profile> out;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(m_dir, ec), end; !ec && it != end; it.increment(ec)) {
        const auto& p = it->path();
        if (!str::iequals(str::fromPath(p.extension()), ".ini") || str::iequals(str::fromPath(p.filename()), "players.ini"))
            continue;
        Profile prof;
        if (prof.load(p))
            out.push_back(std::move(prof));
    }
    std::ranges::sort(out, [](const Profile& a, const Profile& b) { return str::lower(a.name) < str::lower(b.name); });
    return out;
}

std::optional<Profile> ProfileStore::create(std::string_view rawName, std::string* error) {
    const std::string name(str::trim(rawName));
    auto fail = [&](std::string msg) -> std::optional<Profile> {
        if (error)
            *error = std::move(msg);
        return std::nullopt;
    };
    if (name.empty())
        return fail("Please enter a name.");
    for (const auto& p : list())
        if (str::iequals(p.name, name))
            return fail("A driver with that name already exists.");
    // File name: the name reduced to safe characters, made unique.
    std::string base;
    for (char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')
            base.push_back(str::toLower(c));
        else if (c == ' ' && !base.empty() && base.back() != '_')
            base.push_back('_');
    }
    if (base.empty())
        base = "driver";
    std::error_code ec;
    std::filesystem::create_directories(m_dir, ec);
    std::filesystem::path file = m_dir / (base + ".ini");
    for (int n = 2; std::filesystem::exists(file, ec); ++n)
        file = m_dir / std::format("{}_{}.ini", base, n);
    Profile p;
    p.name = name;
    p.netName = name;
    p.file = file;
    if (!p.save())
        return fail("Cannot save the driver profile.");
    return p;
}

bool ProfileStore::remove(const Profile& p) {
    std::error_code ec;
    return !p.file.empty() && std::filesystem::remove(p.file, ec);
}

std::string ProfileStore::lastUsed() const {
    IniFile ini;
    ini.load(m_dir / "players.ini");
    return ini.getString("Players", "Last");
}

void ProfileStore::setLastUsed(std::string_view name) {
    IniFile ini;
    ini.load(m_dir / "players.ini");
    ini.set("Players", "Last", name);
    ini.save(m_dir / "players.ini");
}

// --- Progress --------------------------------------------------------------------------

Progress::Progress(std::vector<CityProgressInfo> cities, std::vector<Reward> rewards)
    : m_cities(std::move(cities)), m_rewards(std::move(rewards)) {}

Progress Progress::load(const vfs::Vfs& vfs) {
    std::vector<CityProgressInfo> cities;
    std::vector<Reward> rewards;
    for (const auto& info : city::listCities(vfs)) {
        CityProgressInfo c;
        c.name = str::lower(info.mapName);
        c.mustPlace = info.mustPlace > 0 ? info.mustPlace : 3;
        c.unlockGroup = info.unlockGroup > 0 ? info.unlockGroup : 3;
        c.blitzCount = info.blitzCount;
        c.circuitCount = info.circuitCount;
        c.checkpointCount = info.checkpointCount;
        const std::string dir = "race/" + str::lower(info.raceDir) + "/";
        while (vfs.exists(std::format("{}crash{}data.csv", dir, c.crashCount)))
            ++c.crashCount;
        if (auto bytes = vfs.readAll(std::format("{}{}_rewards.csv", dir, c.name)))
            for (const auto& r : city::parseRewards(asText(*bytes)))
                rewards.push_back({c.name, str::lower(r.raceType), str::lower(r.raceNum), str::lower(r.car), r.variant,
                                   r.message});
        cities.push_back(std::move(c));
    }
    log::info("progress: {} cities, {} rewards", cities.size(), rewards.size());
    return Progress(std::move(cities), std::move(rewards));
}

const CityProgressInfo* Progress::city(std::string_view name) const {
    for (const auto& c : m_cities)
        if (str::iequals(c.name, name))
            return &c;
    return nullptr;
}

bool Progress::isWin(const CityProgressInfo& c, Difficulty d, int position) const {
    if (position <= 0)
        return false;
    return position <= (d == Difficulty::Professional ? 1 : c.mustPlace);
}

int Progress::raceCount(const CityProgressInfo& c, std::string_view mode) const {
    if (mode == "blitz")
        return c.blitzCount;
    if (mode == "circuit")
        return c.circuitCount;
    if (mode == "race")
        return c.checkpointCount;
    if (mode == "crash")
        return c.crashCount;
    return 0;
}

int Progress::wins(const Profile& p, std::string_view cityName, std::string_view mode, Difficulty d) const {
    const CityProgressInfo* c = city(cityName);
    if (!c)
        return 0;
    int n = 0;
    for (int i = 0; i < raceCount(*c, mode); ++i)
        if (const RaceRecord* r = p.record(d, cityName, mode, i); r && isWin(*c, d, r->bestPosition))
            ++n;
    return n;
}

bool Progress::rewardEarned(const Profile& p, const Reward& r) const {
    const CityProgressInfo* c = city(r.city);
    if (!c)
        return false;
    if (r.raceType == "crash")
        return p.crashPassed.contains(std::format("{}.{}", r.city, r.raceNum));
    const int count = raceCount(*c, r.raceType);
    int need = count;
    if (r.raceNum == "half")
        need = (count + 1) / 2;
    else if (r.raceNum != "all")
        need = static_cast<int>(str::parseInt(r.raceNum).value_or(count));
    if (need <= 0)
        return false;
    return wins(p, r.city, r.raceType, Difficulty::Amateur) >= need ||
           wins(p, r.city, r.raceType, Difficulty::Professional) >= need;
}

const Reward* Progress::lockingReward(std::string_view vehicle, int variant) const {
    for (const auto& r : m_rewards)
        if (str::iequals(r.vehicle, vehicle) && r.variant == variant)
            return &r;
    return nullptr;
}

bool Progress::vehicleUnlocked(const Profile& p, std::string_view vehicle) const {
    const Reward* r = lockingReward(vehicle, 0);
    return !r || rewardEarned(p, *r);
}

bool Progress::variantUnlocked(const Profile& p, std::string_view vehicle, int variant) const {
    if (variant == 0)
        return true;
    const Reward* r = lockingReward(vehicle, variant);
    return !r || rewardEarned(p, *r);
}

int Progress::availableRaces(const Profile& p, std::string_view cityName, std::string_view mode) const {
    const CityProgressInfo* c = city(cityName);
    if (!c)
        return 0;
    const int count = raceCount(*c, mode);
    if (mode == "crash") {
        int open = 1;
        while (open < count && p.crashPassed.contains(std::format("{}.{}", c->name, open - 1)))
            ++open;
        return std::min(open, count);
    }
    int won = 0;
    for (int i = 0; i < count; ++i) {
        const RaceRecord* a = p.record(Difficulty::Amateur, cityName, mode, i);
        const RaceRecord* pr = p.record(Difficulty::Professional, cityName, mode, i);
        if ((a && isWin(*c, Difficulty::Amateur, a->bestPosition)) ||
            (pr && isWin(*c, Difficulty::Professional, pr->bestPosition)))
            ++won;
    }
    return std::min(count, c->unlockGroup + won);
}

std::vector<Reward> Progress::record(Profile& p, const RaceResult& result) const {
    const RaceConfig& cfg = result.config;
    std::vector<const Reward*> before;
    for (const auto& r : m_rewards)
        if (rewardEarned(p, r))
            before.push_back(&r);

    const std::string mode = modeKey(cfg.mode);
    p.score += std::max(0, result.score);
    p.lastVehicle = cfg.vehicle;
    if (cfg.mode == GameMode::CrashCourse) {
        const std::string key = std::format("{}.{}", str::lower(cfg.city), cfg.raceIndex);
        if (cfg.raceIndex >= 0 && result.won)
            p.crashPassed.insert(key);
        else if (cfg.raceIndex >= 0 && !p.crashPassed.contains(key))
            p.crashFailed.insert(key);
    } else if (cfg.raceIndex >= 0 && cfg.mode != GameMode::Cruise && result.finished) {
        RaceRecord& r = p.races[Profile::raceKey(cfg.difficulty, cfg.city, mode, cfg.raceIndex)];
        if (result.position > 0 && (r.bestPosition == 0 || result.position < r.bestPosition))
            r.bestPosition = result.position;
        if (result.timeSeconds > 0 && (r.bestTime == 0 || result.timeSeconds < r.bestTime))
            r.bestTime = result.timeSeconds;
        if (const CityProgressInfo* c = city(cfg.city); c && isWin(*c, cfg.difficulty, result.position))
            ++r.wins;
    }

    std::vector<Reward> earned;
    for (const auto& r : m_rewards)
        if (rewardEarned(p, r) && std::ranges::find(before, &r) == before.end())
            earned.push_back(r);
    return earned;
}

} // namespace mm2::game
