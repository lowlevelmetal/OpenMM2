#pragma once

// Driver profiles ("Select Driver" in the frontend) and the unlock rules that
// depend on them. The original's save format is not known, so profiles use
// OpenMM2's own INI format (see docs/frontend.md):
//
//   <userDataDir>/players/<file>.ini
//   [Driver]   Name, Score, LastRace, LastVehicle, NetName
//   [Prefs]    Vehicle, Color, Automatic, Difficulty, City, Mode, Race, ...
//   [Races]    <difficulty>.<city>.<mode>.<index> = <bestPosition>,<bestTimeSeconds>,<wins>
//   [Crash]    <city>.<lesson> = passed | failed
//
// mode is one of blitz, circuit, race (checkpoint), crash.

#include "game/RaceConfig.h"
#include "vfs/Vfs.h"

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace mm2::game {

struct RaceRecord {
    int bestPosition = 0;   // 1-based; 0 = never finished
    float bestTime = 0.0f;  // seconds; 0 = none
    int wins = 0;           // times the race was "won" (see Progress::isWin)
};

struct Profile {
    std::string name;
    std::filesystem::path file; // where it is stored (set by ProfileStore)

    // Driver record (driver's stats screen).
    int score = 0;
    std::string lastRace;
    std::string lastVehicle;
    std::string netName;

    // Last choices in the menus.
    std::string vehicle = "vpbug";
    int vehicleColor = 0;
    bool automatic = true;
    Difficulty difficulty = Difficulty::Amateur;
    std::string city = "london";
    GameMode mode = GameMode::Cruise;
    int raceIndex = 0;
    TimeOfDay timeOfDay = TimeOfDay::Noon;
    Weather weather = Weather::Clear;
    float pedestrianDensity = 0.5f;
    float trafficDensity = 0.5f;
    float copDensity = 0.5f;
    int opponents = 3;
    int laps = 3;

    // Progress. Keys as in the file format above.
    std::map<std::string, RaceRecord> races;
    std::set<std::string> crashPassed; // "<city>.<lesson>"
    std::set<std::string> crashFailed; // attempted but never passed

    static std::string raceKey(Difficulty d, std::string_view city, std::string_view mode, int index);
    const RaceRecord* record(Difficulty d, std::string_view city, std::string_view mode, int index) const;

    bool load(const std::filesystem::path& path);
    bool save() const;
};

// Mode names used in file names and the rewards tables.
const char* modeKey(GameMode mode); // "blitz", "circuit", "race", "crash", "cruise", "cops"

// Profiles in <userDataDir>/players/.
class ProfileStore {
public:
    explicit ProfileStore(std::filesystem::path dir);

    std::vector<Profile> list() const; // sorted by name
    std::optional<Profile> create(std::string_view name, std::string* error = nullptr);
    bool remove(const Profile& p);
    std::string lastUsed() const;          // name of the last selected driver
    void setLastUsed(std::string_view name);
    const std::filesystem::path& dir() const { return m_dir; }

    static std::filesystem::path defaultDir();

private:
    std::filesystem::path m_dir;
};

// One row of race/<city>/<city>_rewards.csv.
struct Reward {
    std::string city;
    std::string raceType; // blitz, circuit, race, crash
    std::string raceNum;  // half, all or a lesson number
    std::string vehicle;
    int variant = 0;      // 0 = unlocks the vehicle, else a paint job index
    std::string message;
};

// Per-city facts needed by the progress rules (from tune/<city>.cinfo and the
// race lists).
struct CityProgressInfo {
    std::string name; // map name
    int mustPlace = 3;   // amateur: finishing position that counts as a win
    int unlockGroup = 3; // races available before any is won (inferred)
    int blitzCount = 0, circuitCount = 0, checkpointCount = 0, crashCount = 0;
};

// Unlock and race-availability rules. Evidence: the rewards tables and the
// lock messages (jpg/vp*_lck*.jpg): amateur drivers must place within
// MustPlace (1st-3rd), professionals must finish first; "half" means half of
// the city's races of that type (5 of 10 blitz, 6 of 12 checkpoint), "all"
// means every race; crash N means passing Crash Course lesson N.
class Progress {
public:
    Progress() = default;
    Progress(std::vector<CityProgressInfo> cities, std::vector<Reward> rewards);

    // Loads tune/*.cinfo and race/<dir>/<dir>_rewards.csv.
    static Progress load(const vfs::Vfs& vfs);

    const std::vector<Reward>& rewards() const { return m_rewards; }
    const CityProgressInfo* city(std::string_view name) const;

    bool isWin(const CityProgressInfo& c, Difficulty d, int position) const;
    int wins(const Profile& p, std::string_view city, std::string_view mode, Difficulty d) const;
    int raceCount(const CityProgressInfo& c, std::string_view mode) const;

    bool rewardEarned(const Profile& p, const Reward& r) const;
    bool vehicleUnlocked(const Profile& p, std::string_view vehicle) const;
    bool variantUnlocked(const Profile& p, std::string_view vehicle, int variant) const;
    // The reward that would unlock the vehicle/variant (for lock messages).
    const Reward* lockingReward(std::string_view vehicle, int variant) const;

    // Number of races of a mode the player may enter (first UnlockGroup,
    // plus one per race won; inferred). Crash course lessons open one at a
    // time as the previous one is passed (inferred).
    int availableRaces(const Profile& p, std::string_view city, std::string_view mode) const;

    // Records a finished race; returns the rewards newly earned by it.
    std::vector<Reward> record(Profile& p, const RaceResult& result) const;

private:
    std::vector<CityProgressInfo> m_cities;
    std::vector<Reward> m_rewards;
};

} // namespace mm2::game
