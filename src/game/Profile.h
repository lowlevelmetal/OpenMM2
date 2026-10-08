#pragma once

// Driver profiles ("Select Driver" in the frontend) and the progress rules
// that depend on them: which races and lessons are open, what a finish
// records and which vehicles and paint jobs are unlocked.
//
// The rules follow MM2's own code (mmPlayerData, mmPlayerCityRecord,
// mmRewardList; see docs/frontend.md). MM2 saves profiles in binary files
// (players/<playerN>.sav for mmPlayerData, players/<city>/<playerN>.rec per
// city for mmPlayerCityRecord, <playerN>.cfg for the driver's options);
// OpenMM2 keeps the same information in its own INI files:
//
//   <userDataDir>/players/<file>.ini
//   [Driver]   Name, NetName
//   [Prefs]    Vehicle, Color, Automatic, Difficulty, City, Mode, Race,
//              Camera, WideAngle, Dashboard, Mirror
//   [Races]    <city>.<mode>.<index> = <time>,<vehicle>,<score>,<passed>
//
// mode is one of blitz, circuit, race (checkpoint), crash. Files written by
// earlier OpenMM2 versions ([Races] keyed by difficulty, [Crash]) are read
// and converted. Not kept (yet): the last TCP/IP address (mmPlayerData
// +0x100), the HUD and mirror bytes of the view settings, and the other
// per-driver options of mmPlayerConfig (controls, audio, graphics), which
// OpenMM2 keeps for all drivers in its settings.

#include "game/RaceConfig.h"
#include "vfs/Vfs.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mm2::game {

// One race's record (MM2 `mmPlayerRecord`). There is one per race, shared by
// both difficulties.
struct RaceRecord {
    float time = 0.0f;   // checkpoint/blitz: race time; circuit: best lap; crash: 1
    std::string vehicle; // the car that set `time`
    int score = 0;       // best race score
    bool passed = false; // ever passed; never cleared
};

struct Profile {
    std::string name;
    std::filesystem::path file; // where it is stored (set by ProfileStore)
    std::string netName;
    int order = 0; // creation sequence: MM2 lists drivers in the order they were created

    // Last choices in the menus: MM2's last car, paint job, event and city,
    // saved when a race starts (mmInterface::BeDone). A new driver has no
    // car yet (mmPlayerData::Reset leaves it empty): the driver record then
    // shows string 64 ("---") as LAST RACE and LAST VEHICLE
    // (mmInterface::PlayerFillStats) and the menus select vpbug
    // (mmInterface::PlayerSetState); see hasLastRace / selectedVehicle.
    std::string vehicle;
    int vehicleColor = 0;
    bool automatic = true;
    Difficulty difficulty = Difficulty::Amateur;
    std::string city = "london";
    GameMode mode = GameMode::Cruise;
    int raceIndex = 0;

    // The driver's view settings (mmPlayerConfig +0x7168, copied from and to
    // the game's globals by GetViewSettings / SetViewSettings): mmGame
    // restores them when a race starts and saves them when it ends, so the
    // camera choice carries over from race to race. A new driver has them
    // all off (mmPlayerConfig::DefaultViewSettings).
    int camera = 0;         // the cycled car camera: 0 near, 1 point of view, 2 far
    bool wideAngle = false; // letterboxed wide view
    bool dashboard = false; // dashboard view
    // The rear-view mirror on (+0x716C; mmViewMgr::Init leaves the mirror
    // node active only when it is set; off for a new driver).
    bool mirror = false;

    // Records keyed as in the file format above.
    std::map<std::string, RaceRecord> races;

    // mmInterface::PlayerSetState's fallback when the driver has no car.
    static constexpr std::string_view kDefaultVehicle = "vpbug";
    // Whether the driver has started a race: PlayerFillStats tests the car.
    bool hasLastRace() const { return !vehicle.empty(); }
    // The car the menus start on (PlayerSetState).
    std::string selectedVehicle() const { return vehicle.empty() ? std::string(kDefaultVehicle) : vehicle; }

    static std::string raceKey(std::string_view city, std::string_view mode, int index);
    // mmPlayerCityRecord::GetRecord (the per-city mmPlayerRecord).
    const RaceRecord* record(std::string_view city, std::string_view mode, int index) const;

    // Replace MM2's binary files (mmPlayerData::Load / LoadBinary / Save /
    // SaveBinary, mmPlayerCityRecord::Open / Close, mmPlayerRecord::
    // LoadBinary / SaveBinary and their CRCs) with one INI file per driver.
    bool load(const std::filesystem::path& path);
    bool save() const;
};

// Mode names used in file names and the rewards tables.
const char* modeKey(GameMode mode); // "blitz", "circuit", "race", "crash", "cruise", "cops"

// Profiles in <userDataDir>/players/ (MM2 `mmPlayerDirectory`).
class ProfileStore {
public:
    // MM2 `mmInterface::PlayerCreate`: at most 18 drivers, names of up to 18
    // characters (`Dialog_NewPlayer`).
    static constexpr int kMaxDrivers = 18;
    static constexpr std::size_t kMaxNameLength = 18;

    enum class CreateError { None, EmptyName, Duplicate, TooMany, CannotSave };

    explicit ProfileStore(std::filesystem::path dir);

    // mmPlayerDirectory::LoadBinary, GetNumPlayers, GetPlayer: the drivers in
    // creation order.
    std::vector<Profile> list() const;
    // Fails on an empty name, an exact (case-sensitive) duplicate or when
    // kMaxDrivers exist (mmInterface::PlayerCreate, mmPlayerDirectory::
    // AddPlayer). The name is kept as typed, spaces included: a name of
    // spaces only is a driver, as in MM2 (the INI file stores it quoted).
    std::optional<Profile> create(std::string_view name, CreateError* error = nullptr);
    bool remove(const Profile& p); // mmPlayerDirectory::RemovePlayer
    // mmPlayerDirectory::GetLastPlayer / SetLastPlayer: the last driver chosen.
    std::string lastUsed() const;
    void setLastUsed(std::string_view name);
    const std::filesystem::path& dir() const { return m_dir; }

    static std::filesystem::path defaultDir();

private:
    std::filesystem::path m_dir;
};

// The Race Records ("hall of fame") shared by all drivers: per difficulty,
// city and race, the five best times and the five best scores (MM2
// `mmMiscData`, players/<city>/amateur and pro). OpenMM2 stores them in
// <players dir>/records.ini:
//
//   [Index]   <table> = 1, for every table below
//   [<difficulty>.<city>.<mode>.<index>]   difficulty amateur | pro
//   time0..time4, score0..score4 = <driver>|<vehicle>|<time>|<score>|<passed>
//
// (Files without the passed field are read as not passed.)
// One MM2 mmRecord (SetName, SetCarName, SetTime / SetScore, SetPassed).
struct HallEntry {
    std::string driver;
    std::string vehicle;
    float time = 0.0f; // 0 = empty slot
    int score = 0;
    // Whether the finish passed the race (mmRecord +0x104, what the mode's
    // ProgressCheck gave); Dialog_HallOfFame shows passed entries differently.
    bool passed = false;
};

class HallOfFame {
public:
    static constexpr int kEntries = 5;
    struct Table {
        std::array<HallEntry, kEntries> byTime;
        std::array<HallEntry, kEntries> byScore;
    };

    static std::string key(Difficulty d, std::string_view city, std::string_view mode, int index);

    // MM2 `mmMiscData::NewRecord`: the entry goes into the time list before
    // the first slower or empty slot, and into the score list before the
    // first lower score (equal times and scores stay ahead); the last entry
    // drops out. (An mmRecord has a single value field, +0x88: a time slot
    // holds the time, a score slot the score as a float, which GetScore
    // truncates. OpenMM2 keeps both fields; the Hall of Fame reads only the
    // slot's own.)
    void submit(Difficulty d, std::string_view city, std::string_view mode, int index, const HallEntry& e);
    // mmMiscData::GetRecord.
    const Table* table(Difficulty d, std::string_view city, std::string_view mode, int index) const;

    // mmMiscData::Open / Init / Close and mmRecord::LoadBinary / SaveBinary,
    // as records.ini.
    bool load(const std::filesystem::path& path);
    bool save(const std::filesystem::path& path) const;

private:
    std::map<std::string, Table> m_tables;
};

// One row of race/<city>/<city>_rewards.csv (MM2 mmRewardRecord, read by
// mmRewardList::Init).
struct Reward {
    std::string city;
    std::string raceType; // blitz, circuit, race, crash
    std::string raceNum;  // half, all or a race/lesson index
    std::string vehicle;
    int variant = 0;      // 0 = unlocks the vehicle, else a paint job index
    std::string message;
};

// Per-city race counts needed by the progress rules.
struct CityProgressInfo {
    std::string name; // map name
    int blitzCount = 0, circuitCount = 0, checkpointCount = 0;
    int crashCount = 13; // MM2 keeps 13 lessons per city
};

// Bit i set = race (or lesson) i.
using RaceMask = std::uint32_t;

class Progress {
public:
    Progress() = default;
    Progress(std::vector<CityProgressInfo> cities, std::vector<Reward> rewards);

    // Loads the race counts and race/<dir>/<dir>_rewards.csv of every city.
    static Progress load(const vfs::Vfs& vfs);

    const std::vector<Reward>& rewards() const { return m_rewards; }
    const CityProgressInfo* city(std::string_view name) const;
    // mmPlayerCityRecord::GetNumRaces.
    int raceCount(const CityProgressInfo& c, std::string_view mode) const;

    // mmPlayerData::GetPassedMask / mmPlayerCityRecord::GetPassedMask and
    // mmPlayerData::GetNumPassed / mmPlayerCityRecord::GetNumPassed.
    RaceMask passedMask(const Profile& p, std::string_view city, std::string_view mode) const;
    int passedCount(const Profile& p, std::string_view city, std::string_view mode) const;

    // Races of a mode the driver may enter (MM2 `mmInterface::CitySetupCB`):
    // checkpoint races in groups of three, each group opening when the
    // previous one is all passed (`mmPlayerData::ResolveCheckpointProgress`);
    // crash course lessons open, each midterm after its three lessons and the
    // final after everything else (`ResolveCrashProgress`); blitz and circuit
    // races all open. Without a driver everything is open.
    // (mmPlayerData::GetProgress, GetCheckpointProgress.)
    RaceMask openMask(const Profile* p, std::string_view city, std::string_view mode) const;
    bool raceOpen(const Profile* p, std::string_view city, std::string_view mode, int index) const;

    // A reward row's condition (MM2 `mmRewardList::UnlockPlayerRewards`):
    // "half" = at least half (rounded down) of the city's races of that mode
    // passed, "all" = all of them, a number = that race or lesson passed.
    bool rewardMet(const Profile& p, const Reward& r) const;
    // Every row naming the vehicle (variant 0) or paint job must be met;
    // vehicles and paint jobs no row names are always available.
    bool vehicleUnlocked(const Profile& p, std::string_view vehicle) const;
    bool variantUnlocked(const Profile& p, std::string_view vehicle, int variant) const;

    // Whether a finish counts for the records: the race must run under its
    // default conditions for the driver's difficulty (MM2 game modes'
    // RegisterFinish checks). `defaults` is `played` with the race's default
    // settings applied.
    static bool recordable(const RaceConfig& played, const RaceConfig& defaults);

    // Records a finish (MM2 `mmPlayerCityRecord::NewRecord`): the best time
    // with its car, the best score, the passed flag. Returns the reward the
    // finish announces (`mmRewardList::CheckReward`): the first row of the
    // mode just driven whose condition is now met and whose vehicle or paint
    // job was still locked.
    std::optional<Reward> record(Profile& p, const RaceResult& result) const;

    // Sum of the best scores of a city's blitz, circuit and checkpoint races
    // (MM2 `mmPlayerData::GetTotalScore`), and over all cities.
    int totalScore(const Profile& p, std::string_view city) const;
    int totalScore(const Profile& p) const;

private:
    std::vector<CityProgressInfo> m_cities;
    std::vector<Reward> m_rewards;
};

} // namespace mm2::game
