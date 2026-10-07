#include "TestData.h"
#include "game/Profile.h"

#include <gtest/gtest.h>

#include <filesystem>

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
    c.mustPlace = 3;
    c.unlockGroup = 3;
    c.blitzCount = 10;
    c.checkpointCount = 12;
    c.crashCount = 13;
    std::vector<Reward> rewards = {
        {"london", "blitz", "half", "vpcoop2k", 0, "unlocked"},
        {"london", "blitz", "all", "vpcoop2k", 4, "paint"},
        {"london", "race", "half", "vppanozgt", 0, "unlocked"},
        {"london", "crash", "12", "vpdb7", 0, "db7"},
    };
    return Progress({c}, rewards);
}

void win(Profile& p, Difficulty d, const char* mode, int index, int position) {
    p.races[Profile::raceKey(d, "london", mode, index)].bestPosition = position;
}

} // namespace

TEST(Profile, SaveLoadRoundTrip) {
    const auto dir = tempDir("openmm2_profile_test");
    Profile p;
    p.name = "Ünïcode Driver";
    p.file = dir / "a.ini";
    p.vehicle = "vpdb7";
    p.difficulty = Difficulty::Professional;
    p.score = 1234;
    p.races[Profile::raceKey(Difficulty::Amateur, "london", "blitz", 2)] = {2, 61.5f, 1};
    p.crashPassed.insert("london.3");
    p.crashFailed.insert("london.4");
    ASSERT_TRUE(p.save());

    Profile q;
    ASSERT_TRUE(q.load(p.file));
    EXPECT_EQ(q.name, p.name);
    EXPECT_EQ(q.vehicle, "vpdb7");
    EXPECT_EQ(q.difficulty, Difficulty::Professional);
    EXPECT_EQ(q.score, 1234);
    const auto* r = q.record(Difficulty::Amateur, "london", "blitz", 2);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->bestPosition, 2);
    EXPECT_NEAR(r->bestTime, 61.5f, 0.01f);
    EXPECT_TRUE(q.crashPassed.contains("london.3"));
    EXPECT_TRUE(q.crashFailed.contains("london.4"));
    std::filesystem::remove_all(dir);
}

TEST(ProfileStore, CreateListRemove) {
    const auto dir = tempDir("openmm2_store_test");
    ProfileStore store(dir);
    std::string err;
    auto a = store.create("Alice", &err);
    ASSERT_TRUE(a) << err;
    EXPECT_FALSE(store.create("alice", &err)); // case-insensitive duplicate
    EXPECT_FALSE(store.create("   ", &err));
    auto b = store.create("Bob / Builder");
    ASSERT_TRUE(b);
    EXPECT_EQ(b->file.parent_path(), dir);
    EXPECT_EQ(store.list().size(), 2u);
    store.setLastUsed("Bob / Builder");
    EXPECT_EQ(store.lastUsed(), "Bob / Builder");
    EXPECT_TRUE(store.remove(*a));
    ASSERT_EQ(store.list().size(), 1u);
    EXPECT_EQ(store.list()[0].name, "Bob / Builder");
    std::filesystem::remove_all(dir);
}

TEST(Progress, AmateurNeedsTopThreeInHalfTheRaces) {
    const Progress prog = londonProgress();
    Profile p;
    EXPECT_FALSE(prog.vehicleUnlocked(p, "vpcoop2k"));
    EXPECT_TRUE(prog.vehicleUnlocked(p, "vpbug")); // not in the rewards table
    for (int i = 0; i < 4; ++i)
        win(p, Difficulty::Amateur, "blitz", i, 3);
    win(p, Difficulty::Amateur, "blitz", 4, 4); // 4th does not count
    EXPECT_FALSE(prog.vehicleUnlocked(p, "vpcoop2k"));
    win(p, Difficulty::Amateur, "blitz", 5, 1);
    EXPECT_TRUE(prog.vehicleUnlocked(p, "vpcoop2k"));
    EXPECT_FALSE(prog.variantUnlocked(p, "vpcoop2k", 4));
    EXPECT_TRUE(prog.variantUnlocked(p, "vpcoop2k", 1));
    for (int i = 0; i < 10; ++i)
        win(p, Difficulty::Amateur, "blitz", i, 2);
    EXPECT_TRUE(prog.variantUnlocked(p, "vpcoop2k", 4));
}

TEST(Progress, ProfessionalNeedsFirstPlace) {
    const Progress prog = londonProgress();
    Profile p;
    for (int i = 0; i < 6; ++i)
        win(p, Difficulty::Professional, "race", i, 2);
    EXPECT_FALSE(prog.vehicleUnlocked(p, "vppanozgt"));
    for (int i = 0; i < 6; ++i)
        win(p, Difficulty::Professional, "race", i, 1);
    EXPECT_TRUE(prog.vehicleUnlocked(p, "vppanozgt")); // half of 12 = 6
}

TEST(Progress, CrashCourseUnlocksAndLessonOrder) {
    const Progress prog = londonProgress();
    Profile p;
    EXPECT_EQ(prog.availableRaces(p, "london", "crash"), 1);
    RaceResult r;
    r.config.mode = GameMode::CrashCourse;
    r.config.city = "london";
    r.config.raceIndex = 0;
    r.finished = true;
    r.won = false;
    EXPECT_TRUE(prog.record(p, r).empty());
    EXPECT_TRUE(p.crashFailed.contains("london.0"));
    r.won = true;
    prog.record(p, r);
    EXPECT_EQ(prog.availableRaces(p, "london", "crash"), 2);
    for (int i = 1; i < 12; ++i) {
        r.config.raceIndex = i;
        prog.record(p, r);
    }
    EXPECT_FALSE(prog.vehicleUnlocked(p, "vpdb7"));
    r.config.raceIndex = 12;
    const auto earned = prog.record(p, r);
    ASSERT_EQ(earned.size(), 1u);
    EXPECT_EQ(earned[0].vehicle, "vpdb7");
    EXPECT_TRUE(prog.vehicleUnlocked(p, "vpdb7"));
}

TEST(Progress, RacesOpenAsTheyAreWon) {
    const Progress prog = londonProgress();
    Profile p;
    EXPECT_EQ(prog.availableRaces(p, "london", "blitz"), 3);
    RaceResult r;
    r.config.mode = GameMode::Blitz;
    r.config.city = "london";
    r.config.raceIndex = 0;
    r.finished = true;
    r.position = 1;
    r.timeSeconds = 80.0f;
    prog.record(p, r);
    EXPECT_EQ(prog.availableRaces(p, "london", "blitz"), 4);
    r.position = 5; // a worse result keeps the best
    r.timeSeconds = 70.0f;
    prog.record(p, r);
    const auto* rec = p.record(Difficulty::Amateur, "london", "blitz", 0);
    ASSERT_TRUE(rec);
    EXPECT_EQ(rec->bestPosition, 1);
    EXPECT_FLOAT_EQ(rec->bestTime, 70.0f);
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
}
