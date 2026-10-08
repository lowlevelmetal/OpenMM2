#include "TestData.h"
#include "game/Profile.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <fstream>

using namespace mm2;
using namespace mm2::game;

namespace {

std::filesystem::path tempDir(const char* name) {
    auto d = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(d);
    std::filesystem::create_directories(d);
    return d;
}

Progress londonProgress() {
    CityProgressInfo c;
    c.name = "london";
    c.blitzCount = 10;
    c.circuitCount = 11;
    c.checkpointCount = 12;
    std::vector<Reward> rewards = {
        {"london", "blitz", "half", "vpcoop2k", 0, "unlocked"},
        {"london", "blitz", "all", "vpcoop2k", 4, "paint"},
        {"london", "circuit", "half", "vpvwcup", 0, "unlocked"},
        {"london", "race", "half", "vppanozgt", 0, "unlocked"},
        {"london", "crash", "7", "vpcab", 1, "cab paint"},
        {"london", "crash", "12", "vpdb7", 0, "db7"},
    };
    return Progress({c}, rewards);
}

void pass(Profile& p, const char* mode, int index, bool passed = true) {
    p.races[Profile::raceKey("london", mode, index)].passed = passed;
}

RaceResult finish(GameMode mode, int index, bool won) {
    RaceResult r;
    r.config.mode = mode;
    r.config.city = "london";
    r.config.raceIndex = index;
    r.config.vehicle = "vpbug";
    r.ended = true;
    r.finished = mode != GameMode::CrashCourse || won;
    r.won = won;
    r.timeSeconds = 90.0f;
    return r;
}

} // namespace

TEST(Profile, SaveLoadRoundTrip) {
    const auto dir = tempDir("openmm2_profile_test");
    Profile p;
    p.name = "Ünïcode Driver";
    p.file = dir / "a.ini";
    p.vehicle = "vpdb7";
    p.difficulty = Difficulty::Professional;
    p.races[Profile::raceKey("london", "blitz", 2)] = {61.5f, "vpbug", 1000, true};
    p.races[Profile::raceKey("london", "crash", 4)] = {1.0f, "vpcab", 1, false};
    ASSERT_TRUE(p.save());

    Profile q;
    ASSERT_TRUE(q.load(p.file));
    EXPECT_EQ(q.name, p.name);
    EXPECT_EQ(q.vehicle, "vpdb7");
    EXPECT_EQ(q.difficulty, Difficulty::Professional);
    const auto* r = q.record("london", "blitz", 2);
    ASSERT_TRUE(r);
    EXPECT_NEAR(r->time, 61.5f, 0.01f);
    EXPECT_EQ(r->vehicle, "vpbug");
    EXPECT_EQ(r->score, 1000);
    EXPECT_TRUE(r->passed);
    ASSERT_TRUE(q.record("london", "crash", 4));
    EXPECT_FALSE(q.record("london", "crash", 4)->passed);
    std::filesystem::remove_all(dir);
}

TEST(Profile, ReadsEarlierFormat) {
    const auto dir = tempDir("openmm2_profile_old");
    {
        std::ofstream f(dir / "old.ini");
        f << "[Driver]\nName=Old\n[Races]\namateur.london.blitz.1=2,70.00,1\npro.london.blitz.1=4,65.00,0\n"
             "amateur.london.race.0=5,80.00,0\n[Crash]\nlondon.3=passed\nsf.4=failed\n";
    }
    Profile p;
    ASSERT_TRUE(p.load(dir / "old.ini"));
    ASSERT_TRUE(p.record("london", "blitz", 1));
    EXPECT_TRUE(p.record("london", "blitz", 1)->passed);
    EXPECT_FLOAT_EQ(p.record("london", "blitz", 1)->time, 65.0f);
    EXPECT_FALSE(p.record("london", "race", 0)->passed);
    EXPECT_TRUE(p.record("london", "crash", 3)->passed);
    EXPECT_FALSE(p.record("sf", "crash", 4)->passed);
    std::filesystem::remove_all(dir);
}

TEST(ProfileStore, CreateListRemove) {
    const auto dir = tempDir("openmm2_store_test");
    ProfileStore store(dir);
    ProfileStore::CreateError err{};
    auto z = store.create("Zed", &err);
    ASSERT_TRUE(z);
    EXPECT_EQ(err, ProfileStore::CreateError::None);
    auto a = store.create("Alice", &err);
    ASSERT_TRUE(a);
    EXPECT_FALSE(store.create("Alice", &err));
    EXPECT_EQ(err, ProfileStore::CreateError::Duplicate);
    EXPECT_TRUE(store.create("alice")); // duplicates are case-sensitive, like MM2
    EXPECT_FALSE(store.create("", &err));
    EXPECT_EQ(err, ProfileStore::CreateError::EmptyName);
    // Creation order, not alphabetical.
    auto list = store.list();
    ASSERT_EQ(list.size(), 3u);
    EXPECT_EQ(list[0].name, "Zed");
    EXPECT_EQ(list[1].name, "Alice");
    auto b = store.create("Bob / Builder");
    ASSERT_TRUE(b);
    EXPECT_EQ(b->file.parent_path(), dir);
    store.setLastUsed("Bob / Builder");
    EXPECT_EQ(store.lastUsed(), "Bob / Builder");
    EXPECT_TRUE(store.remove(*a));
    list = store.list();
    ASSERT_EQ(list.size(), 3u);
    EXPECT_EQ(list[2].name, "Bob / Builder");
    std::filesystem::remove_all(dir);
}

TEST(ProfileStore, AtMostEighteenDrivers) {
    const auto dir = tempDir("openmm2_store_full");
    ProfileStore store(dir);
    for (int i = 0; i < ProfileStore::kMaxDrivers; ++i)
        ASSERT_TRUE(store.create("Driver " + std::to_string(i)));
    ProfileStore::CreateError err{};
    EXPECT_FALSE(store.create("One too many", &err));
    EXPECT_EQ(err, ProfileStore::CreateError::TooMany);
    std::filesystem::remove_all(dir);
}

TEST(Progress, CheckpointRacesOpenInGroupsOfThree) {
    const Progress prog = londonProgress();
    Profile p;
    EXPECT_EQ(prog.openMask(&p, "london", "race"), 0x7u);
    pass(p, "race", 0);
    pass(p, "race", 1);
    EXPECT_EQ(prog.openMask(&p, "london", "race"), 0x7u);
    pass(p, "race", 2);
    EXPECT_EQ(prog.openMask(&p, "london", "race"), 0x3fu);
    for (int i = 3; i < 6; ++i)
        pass(p, "race", i);
    EXPECT_EQ(prog.openMask(&p, "london", "race"), 0x1ffu);
    for (int i = 6; i < 9; ++i)
        pass(p, "race", i);
    EXPECT_EQ(prog.openMask(&p, "london", "race"), 0xfffu);
    // Each group only looks at the one before it.
    Profile q;
    pass(q, "race", 0);
    pass(q, "race", 1);
    pass(q, "race", 2);
    pass(q, "race", 3);
    pass(q, "race", 4);
    EXPECT_EQ(prog.openMask(&q, "london", "race"), 0x3fu);
}

TEST(Progress, CrashCourseExamsFollowTheirLessons) {
    const Progress prog = londonProgress();
    Profile p;
    EXPECT_EQ(prog.openMask(&p, "london", "crash"), 0x777u);
    EXPECT_FALSE(prog.raceOpen(&p, "london", "crash", 3));
    for (int i = 0; i < 3; ++i)
        pass(p, "crash", i);
    EXPECT_TRUE(prog.raceOpen(&p, "london", "crash", 3));
    for (int i = 4; i < 7; ++i)
        pass(p, "crash", i);
    EXPECT_TRUE(prog.raceOpen(&p, "london", "crash", 7));
    for (int i = 8; i < 11; ++i)
        pass(p, "crash", i);
    EXPECT_TRUE(prog.raceOpen(&p, "london", "crash", 11));
    EXPECT_FALSE(prog.raceOpen(&p, "london", "crash", 12));
    pass(p, "crash", 3);
    pass(p, "crash", 7);
    pass(p, "crash", 11);
    EXPECT_TRUE(prog.raceOpen(&p, "london", "crash", 12));
}

TEST(Progress, BlitzCircuitAndNoDriverAreOpen) {
    const Progress prog = londonProgress();
    Profile p;
    EXPECT_TRUE(prog.raceOpen(&p, "london", "blitz", 9));
    EXPECT_TRUE(prog.raceOpen(&p, "london", "circuit", 10));
    EXPECT_TRUE(prog.raceOpen(nullptr, "london", "race", 11));
    EXPECT_TRUE(prog.raceOpen(nullptr, "london", "crash", 12));
}

TEST(Progress, RewardThresholds) {
    const Progress prog = londonProgress();
    Profile p;
    EXPECT_FALSE(prog.vehicleUnlocked(p, "vpcoop2k"));
    EXPECT_TRUE(prog.vehicleUnlocked(p, "vpbug")); // in no rewards table
    for (int i = 0; i < 4; ++i)
        pass(p, "blitz", i);
    EXPECT_FALSE(prog.vehicleUnlocked(p, "vpcoop2k"));
    pass(p, "blitz", 7);
    EXPECT_TRUE(prog.vehicleUnlocked(p, "vpcoop2k")); // 5 of 10
    EXPECT_FALSE(prog.variantUnlocked(p, "vpcoop2k", 4));
    EXPECT_TRUE(prog.variantUnlocked(p, "vpcoop2k", 1));
    for (int i = 0; i < 10; ++i)
        pass(p, "blitz", i);
    EXPECT_TRUE(prog.variantUnlocked(p, "vpcoop2k", 4));
    // Half of 11 circuits rounds down to 5.
    for (int i = 0; i < 5; ++i)
        pass(p, "circuit", i);
    EXPECT_TRUE(prog.vehicleUnlocked(p, "vpvwcup"));
    // A number is that lesson, not a count.
    for (int i = 0; i < 7; ++i)
        pass(p, "crash", i);
    EXPECT_FALSE(prog.variantUnlocked(p, "vpcab", 1));
    pass(p, "crash", 7);
    EXPECT_TRUE(prog.variantUnlocked(p, "vpcab", 1));
}

TEST(Progress, RecordKeepsBestTimeScoreAndPass) {
    const Progress prog = londonProgress();
    Profile p;
    RaceResult r = finish(GameMode::Blitz, 0, true);
    r.score = 1000;
    prog.record(p, r);
    r.won = false; // a later loss keeps the pass
    r.timeSeconds = 70.0f;
    r.score = 0;
    r.config.vehicle = "vpmustang99";
    prog.record(p, r);
    const auto* rec = p.record("london", "blitz", 0);
    ASSERT_TRUE(rec);
    EXPECT_TRUE(rec->passed);
    EXPECT_FLOAT_EQ(rec->time, 70.0f);
    EXPECT_EQ(rec->vehicle, "vpmustang99");
    EXPECT_EQ(rec->score, 1000);
    // Circuits record the best lap.
    r = finish(GameMode::Circuit, 1, false);
    r.bestLapSeconds = 31.0f;
    prog.record(p, r);
    ASSERT_TRUE(p.record("london", "circuit", 1));
    EXPECT_FLOAT_EQ(p.record("london", "circuit", 1)->time, 31.0f);
    EXPECT_FALSE(p.record("london", "circuit", 1)->passed);
}

TEST(Progress, QuitAndMultiplayerRecordNothing) {
    const Progress prog = londonProgress();
    Profile p;
    RaceResult quit = finish(GameMode::CrashCourse, 0, false);
    quit.ended = false;
    prog.record(p, quit);
    EXPECT_FALSE(p.record("london", "crash", 0));
    RaceResult failed = finish(GameMode::CrashCourse, 0, false);
    prog.record(p, failed);
    ASSERT_TRUE(p.record("london", "crash", 0));
    EXPECT_FALSE(p.record("london", "crash", 0)->passed);
    RaceResult wreck = finish(GameMode::Checkpoint, 0, false);
    wreck.finished = false;
    prog.record(p, wreck);
    EXPECT_FALSE(p.record("london", "race", 0));
    RaceResult mp = finish(GameMode::Blitz, 0, true);
    mp.config.multiplayer = true;
    prog.record(p, mp);
    EXPECT_FALSE(p.record("london", "blitz", 0));
}

TEST(Progress, AnnouncesTheFirstNewReward) {
    const Progress prog = londonProgress();
    Profile p;
    for (int i = 0; i < 12; ++i)
        pass(p, "crash", i);
    EXPECT_FALSE(prog.vehicleUnlocked(p, "vpdb7"));
    const auto reward = prog.record(p, finish(GameMode::CrashCourse, 12, true));
    ASSERT_TRUE(reward);
    EXPECT_EQ(reward->vehicle, "vpdb7");
    EXPECT_TRUE(prog.vehicleUnlocked(p, "vpdb7"));
    // Driving it again announces nothing.
    EXPECT_FALSE(prog.record(p, finish(GameMode::CrashCourse, 12, true)));
}

TEST(Progress, RecordableUnderDefaultConditions) {
    RaceConfig defaults;
    defaults.mode = GameMode::Circuit;
    defaults.raceIndex = 2;
    defaults.laps = 3;
    defaults.opponents = 5;
    RaceConfig played = defaults;
    EXPECT_TRUE(Progress::recordable(played, defaults));
    played.laps = 4;
    EXPECT_FALSE(Progress::recordable(played, defaults));
    // Checkpoint races do not compare opponents or pedestrians.
    played = defaults;
    played.mode = defaults.mode = GameMode::Checkpoint;
    played.opponents = 1;
    played.pedestrianDensity = 0.0f;
    EXPECT_TRUE(Progress::recordable(played, defaults));
    played.weather = Weather::Rain;
    EXPECT_FALSE(Progress::recordable(played, defaults));
    played = defaults;
    played.mode = defaults.mode = GameMode::Cruise;
    EXPECT_FALSE(Progress::recordable(played, defaults));
}

TEST(Progress, TotalScoreSumsBestRaceScores) {
    const Progress prog = londonProgress();
    Profile p;
    p.races[Profile::raceKey("london", "blitz", 0)].score = 1000;
    p.races[Profile::raceKey("london", "race", 3)].score = 500;
    p.races[Profile::raceKey("london", "crash", 3)].score = 1;
    EXPECT_EQ(prog.totalScore(p, "london"), 1500);
    EXPECT_EQ(prog.totalScore(p), 1500);
}

TEST(Progress, RetailRewards) {
    MM2_REQUIRE_GAME_DATA();
    const Progress prog = Progress::load(*test::gameData());
    EXPECT_EQ(prog.rewards().size(), 20u);
    ASSERT_TRUE(prog.city("london"));
    EXPECT_EQ(prog.city("london")->blitzCount, 10);
    EXPECT_EQ(prog.city("london")->crashCount, 13);
    Profile fresh;
    EXPECT_TRUE(prog.vehicleUnlocked(fresh, "vpbug"));
    for (const char* locked : {"vpcoop2k", "vpvwcup", "vppanozgt", "vpdb7", "vpsemi", "vpdune", "vpauditt", "vp4x4"})
        EXPECT_FALSE(prog.vehicleUnlocked(fresh, locked)) << locked;
    // Messages end at the first comma (mmRewardList::Load).
    for (const auto& r : prog.rewards())
        EXPECT_EQ(r.message.find(','), std::string::npos);
}

TEST(HallOfFame, KeepsFiveBestTimesAndScores) {
    HallOfFame hof;
    const float times[] = {80, 70, 90, 70, 60, 100, 65};
    for (int i = 0; i < 7; ++i)
        hof.submit(Difficulty::Amateur, "london", "blitz", 0, {std::format("d{}", i), "vpbug", times[i], i * 100});
    const auto* t = hof.table(Difficulty::Amateur, "london", "blitz", 0);
    ASSERT_TRUE(t);
    // Equal times keep the earlier entry first.
    EXPECT_EQ(t->byTime[0].driver, "d4");
    EXPECT_EQ(t->byTime[1].driver, "d6");
    EXPECT_EQ(t->byTime[2].driver, "d1");
    EXPECT_EQ(t->byTime[3].driver, "d3");
    EXPECT_EQ(t->byTime[4].driver, "d0");
    EXPECT_EQ(t->byScore[0].score, 600);
    EXPECT_EQ(t->byScore[4].score, 200);
    // A zero score never enters the score list.
    hof.submit(Difficulty::Professional, "sf", "race", 1, {"x", "vpbug", 50.0f, 0});
    EXPECT_EQ(hof.table(Difficulty::Professional, "sf", "race", 1)->byScore[0].driver, "");
    EXPECT_FALSE(hof.table(Difficulty::Professional, "sf", "race", 2));

    const auto dir = tempDir("openmm2_hof");
    ASSERT_TRUE(hof.save(dir / "records.ini"));
    HallOfFame back;
    ASSERT_TRUE(back.load(dir / "records.ini"));
    const auto* b = back.table(Difficulty::Amateur, "london", "blitz", 0);
    ASSERT_TRUE(b);
    EXPECT_EQ(b->byTime[2].driver, "d1");
    EXPECT_FLOAT_EQ(b->byTime[0].time, 60.0f);
    EXPECT_EQ(b->byScore[1].vehicle, "vpbug");
    std::filesystem::remove_all(dir);
}
