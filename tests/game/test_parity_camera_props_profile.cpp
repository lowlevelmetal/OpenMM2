// Parity checks for the vehicle and city lists (mmVehList / mmVehInfo,
// mmCityList / mmCityInfo) and the driver records (mmPlayerCityRecord,
// mmRewardList, mmMiscData) against MM2's behaviour.
#include "TestData.h"
#include "game/Catalog.h"
#include "game/Profile.h"

#include <gtest/gtest.h>

#include <filesystem>

using namespace mm2;
using namespace mm2::game;

namespace {

constexpr const char* kFullInfo = "BaseName=vptest\nDescription=Test Car\nColors=Red|Blue\nFlags=0\n"
                                  "Order=-1\nScoringBias=20.0\nUnlockScore=0\nUnlockFlags=0\nHorsepower=150\n"
                                  "Top Speed=91\nDurability=760000\nMass=4250\n";

constexpr const char* kFullCity = "LocalizedName=Test Town\nMapName=test\nRaceDir=test\nBlitzCount=3\n"
                                  "CircuitCount=0\nCheckpointCount=2\nBlitzNames=A|B\nCircuitNames=C|D\n"
                                  "CheckpointNames=E|F|G\n";

std::filesystem::path tempDir(const char* name) {
    auto d = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(d);
    std::filesystem::create_directories(d);
    return d;
}

Progress twoCityProgress() {
    CityProgressInfo london{"london", 10, 10, 12};
    CityProgressInfo sf{"sf", 10, 10, 12};
    CityProgressInfo addon{"addon", 4, 4, 6};
    std::vector<Reward> rewards = {
        {"london", "blitz", "half", "vpcoop2k", 0, "unlocked"},
        {"addon", "blitz", "all", "vpbug", 0, "add-on city reward"},
    };
    return Progress({london, sf, addon}, rewards);
}

RaceResult finish(GameMode mode, int index, bool won, float time) {
    RaceResult r;
    r.config.mode = mode;
    r.config.city = "london";
    r.config.raceIndex = index;
    r.config.vehicle = "vpbug";
    r.ended = true;
    r.finished = true;
    r.won = won;
    r.timeSeconds = time;
    return r;
}

} // namespace

// mmVehInfo::Load: the twelve leading fields are required; UIDist defaults
// to 6; the force feedback modifiers are never read from the file.
TEST(ParityCatalog, VehicleInfoFieldsAndDefaults) {
    auto v = parseVehicleInfo(kFullInfo);
    ASSERT_TRUE(v);
    EXPECT_FLOAT_EQ(v->uiDistance, 6.0f);
    EXPECT_FLOAT_EQ(v->forceFeedbackModifier, 1.0f);
    EXPECT_FLOAT_EQ(v->roadForceModifier, 1.0f);
    v = parseVehicleInfo(std::string(kFullInfo) +
                         "UIDist=5.5\nForceFeedbackModifier=0.24\nRoadForceModifier=2.0\n");
    ASSERT_TRUE(v);
    EXPECT_FLOAT_EQ(v->uiDistance, 5.5f);
    EXPECT_FLOAT_EQ(v->forceFeedbackModifier, 1.0f);
    EXPECT_FLOAT_EQ(v->roadForceModifier, 1.0f);
    // Without Mass (or any other of the twelve) the car is not listed.
    std::string noMass = kFullInfo;
    noMass.erase(noMass.find("Mass="));
    EXPECT_FALSE(parseVehicleInfo(noMass));
}

// mmCityInfo::Load: a non-zero count becomes the number of names, a zero
// count drops the list; all nine fields are required.
TEST(ParityCatalog, CityInfoCountsComeFromTheNames) {
    auto c = parseCityInfo("test", kFullCity);
    ASSERT_TRUE(c);
    EXPECT_EQ(c->blitzNames.size(), 2u);      // BlitzCount=3, two names
    EXPECT_TRUE(c->circuitNames.empty());     // CircuitCount=0
    EXPECT_EQ(c->checkpointNames.size(), 3u); // CheckpointCount=2, three names
    std::string noNames = kFullCity;
    noNames.erase(noNames.find("CheckpointNames="));
    EXPECT_FALSE(parseCityInfo("test", noNames));
}

// mmCityList::LoadAll: sf.cinfo first; cities are found by RaceDir.
TEST(ParityCatalog, RetailCityOrder) {
    MM2_REQUIRE_GAME_DATA();
    const auto cat = Catalog::load(*test::gameData());
    ASSERT_EQ(cat.cities().size(), 2u);
    EXPECT_EQ(cat.cities()[0].raceDir, "sf");
    EXPECT_EQ(cat.cities()[1].raceDir, "london");
    ASSERT_TRUE(cat.city("SF"));
    EXPECT_EQ(cat.city("sf")->localizedName, "San Francisco");
    EXPECT_EQ(cat.vehicles().size(), 20u);
}

// mmPlayerCityRecord::NewRecord: an equal time keeps the old car; a record
// whose time is 0 is replaced, but a pass is never taken back.
TEST(ParityProfile, NewRecordMerge) {
    const Progress prog = twoCityProgress();
    Profile p;
    prog.record(p, finish(GameMode::Blitz, 0, true, 60.0f));
    RaceResult again = finish(GameMode::Blitz, 0, true, 60.0f);
    again.config.vehicle = "vpcab";
    prog.record(p, again);
    ASSERT_TRUE(p.record("london", "blitz", 0));
    EXPECT_EQ(p.record("london", "blitz", 0)->vehicle, "vpbug");

    p.races[Profile::raceKey("london", "race", 1)] = {0.0f, "vpbus", 0, true};
    prog.record(p, finish(GameMode::Checkpoint, 1, false, 80.0f));
    const RaceRecord* r = p.record("london", "race", 1);
    ASSERT_TRUE(r);
    EXPECT_FLOAT_EQ(r->time, 80.0f);
    EXPECT_EQ(r->vehicle, "vpbug");
    EXPECT_TRUE(r->passed);
}

// mmInterface::CitySetupCB / PlayerResolveCars / PlayerFillStats only know
// San Francisco and London.
TEST(ParityProfile, ProgressRulesOnlyForTheTwoCities) {
    const Progress prog = twoCityProgress();
    Profile p;
    EXPECT_EQ(prog.openMask(&p, "london", "race"), 0x7u);
    EXPECT_EQ(prog.openMask(&p, "addon", "race"), ~RaceMask{0});
    EXPECT_FALSE(prog.vehicleUnlocked(p, "vpcoop2k"));
    EXPECT_TRUE(prog.vehicleUnlocked(p, "vpbug")); // only named by the add-on city
    p.races[Profile::raceKey("london", "blitz", 0)].score = 100;
    p.races[Profile::raceKey("sf", "circuit", 9)].score = 20;
    p.races[Profile::raceKey("addon", "blitz", 0)].score = 1000;
    p.races[Profile::raceKey("london", "blitz", 12)].score = 5000; // past BlitzCount
    EXPECT_EQ(prog.totalScore(p, "london"), 100);
    EXPECT_EQ(prog.totalScore(p, "addon"), 1000);
    EXPECT_EQ(prog.totalScore(p), 120);
}

// mmMiscData::NewRecord: equal times and scores stay ahead of a new entry;
// the passed flag and the exact time survive a save and load.
TEST(ParityProfile, HallOfFameOrderAndRoundTrip) {
    HallOfFame hof;
    hof.submit(Difficulty::Amateur, "london", "blitz", 0, {"A", "vpbug", 61.25f, 100, true});
    hof.submit(Difficulty::Amateur, "london", "blitz", 0, {"B", "vpcab", 61.25f, 100, false});
    hof.submit(Difficulty::Amateur, "london", "blitz", 0, {"C", "vpbus", 60.537f, 50, true});
    const auto* t = hof.table(Difficulty::Amateur, "london", "blitz", 0);
    ASSERT_TRUE(t);
    EXPECT_EQ(t->byTime[0].driver, "C");
    EXPECT_EQ(t->byTime[1].driver, "A");
    EXPECT_EQ(t->byTime[2].driver, "B");
    EXPECT_EQ(t->byScore[0].driver, "A");
    EXPECT_EQ(t->byScore[1].driver, "B");
    EXPECT_EQ(t->byScore[2].driver, "C");

    const auto dir = tempDir("openmm2_parity_hof");
    ASSERT_TRUE(hof.save(dir / "records.ini"));
    HallOfFame back;
    ASSERT_TRUE(back.load(dir / "records.ini"));
    const auto* u = back.table(Difficulty::Amateur, "london", "blitz", 0);
    ASSERT_TRUE(u);
    EXPECT_EQ(u->byTime[0].time, 60.537f);
    EXPECT_TRUE(u->byTime[0].passed);
    EXPECT_FALSE(u->byTime[2].passed);
    std::filesystem::remove_all(dir);
}

// mmPlayerConfig::GetViewSettings / SetViewSettings: the camera choice is
// kept per driver; a new driver starts on the near camera, all flags off.
TEST(ParityProfile, ViewSettingsAreKeptPerDriver) {
    const auto dir = tempDir("openmm2_parity_view");
    ProfileStore store(dir);
    auto p = store.create("Viewer");
    ASSERT_TRUE(p);
    EXPECT_EQ(p->camera, 0);
    EXPECT_FALSE(p->wideAngle);
    EXPECT_FALSE(p->dashboard);
    EXPECT_FALSE(p->mirror);
    p->camera = 2;
    p->wideAngle = true;
    p->dashboard = true;
    p->mirror = true;
    ASSERT_TRUE(p->save());
    Profile q;
    ASSERT_TRUE(q.load(p->file));
    EXPECT_EQ(q.camera, 2);
    EXPECT_TRUE(q.wideAngle);
    EXPECT_TRUE(q.dashboard);
    EXPECT_TRUE(q.mirror);
    std::filesystem::remove_all(dir);
}

// mmInterface::PlayerCreate refuses the 19th driver and an empty name;
// mmPlayerDirectory::AddPlayer an exact duplicate.
TEST(ParityProfile, DriverCreationLimits) {
    const auto dir = tempDir("openmm2_parity_names");
    ProfileStore store(dir);
    ProfileStore::CreateError err{};
    EXPECT_FALSE(store.create("", &err));
    EXPECT_EQ(err, ProfileStore::CreateError::EmptyName);
    ASSERT_TRUE(store.create("Ace"));
    EXPECT_FALSE(store.create("Ace", &err));
    EXPECT_EQ(err, ProfileStore::CreateError::Duplicate);
    EXPECT_TRUE(store.create("ACE"));
    std::filesystem::remove_all(dir);
}

// mmInterface::PlayerCreate keeps the name as typed (only an empty one is
// refused); the stored files keep the spaces.
TEST(ParityProfile, DriverNamesKeepTheirSpaces) {
    const auto dir = tempDir("openmm2_parity_spaces");
    ProfileStore store(dir);
    ASSERT_TRUE(store.create(" Ace "));
    ASSERT_TRUE(store.create("   "));
    EXPECT_FALSE(store.create(" Ace "));
    ASSERT_TRUE(store.create("Ace")); // a different driver
    const auto list = store.list();
    ASSERT_EQ(list.size(), 3u);
    EXPECT_EQ(list[0].name, " Ace ");
    EXPECT_EQ(list[0].netName, " Ace ");
    EXPECT_EQ(list[1].name, "   ");
    EXPECT_EQ(list[2].name, "Ace");
    store.setLastUsed(" Ace ");
    EXPECT_EQ(store.lastUsed(), " Ace ");

    HallOfFame hof;
    hof.submit(Difficulty::Amateur, "london", "blitz", 0, {" Ace ", "vpbug", 61.25f, 100, true});
    ASSERT_TRUE(hof.save(dir / "records.ini"));
    HallOfFame back;
    ASSERT_TRUE(back.load(dir / "records.ini"));
    const auto* t = back.table(Difficulty::Amateur, "london", "blitz", 0);
    ASSERT_TRUE(t);
    EXPECT_EQ(t->byTime[0].driver, " Ace ");
    std::filesystem::remove_all(dir);
}

// A new driver has no last car until a race starts (mmPlayerData::Reset):
// PlayerFillStats then shows "---" for LAST RACE and LAST VEHICLE, and
// PlayerSetState starts the menus on vpbug in cruise.
TEST(ParityProfile, NewDriverHasNoLastRace) {
    const auto dir = tempDir("openmm2_parity_lastrace");
    ProfileStore store(dir);
    auto p = store.create("Newbie");
    ASSERT_TRUE(p);
    EXPECT_FALSE(p->hasLastRace());
    EXPECT_EQ(p->selectedVehicle(), "vpbug");
    EXPECT_EQ(p->mode, GameMode::Cruise);
    Profile loaded;
    ASSERT_TRUE(loaded.load(p->file));
    EXPECT_FALSE(loaded.hasLastRace());
    loaded.vehicle = "vpcab";
    ASSERT_TRUE(loaded.save());
    Profile again;
    ASSERT_TRUE(again.load(p->file));
    EXPECT_TRUE(again.hasLastRace());
    EXPECT_EQ(again.selectedVehicle(), "vpcab");
    std::filesystem::remove_all(dir);
}
