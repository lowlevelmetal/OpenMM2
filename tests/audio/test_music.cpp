#include "TestData.h"
#include "audio/Mixer.h"
#include "audio/Music.h"
#include "audio/MusicDirector.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <thread>

using namespace mm2;
using namespace mm2::audio;

namespace {

double rms(const std::vector<float>& v) {
    double s = 0;
    for (float f : v)
        s += static_cast<double>(f) * f;
    return v.empty() ? 0.0 : std::sqrt(s / static_cast<double>(v.size()));
}

std::vector<float> renderSeconds(MusicEngine& engine, double seconds, std::vector<float>* ambience = nullptr) {
    const int rate = engine.sampleRate();
    const auto total = static_cast<std::size_t>(seconds * rate);
    std::vector<float> out(total * 2), amb(total * 2);
    constexpr int kBlock = 512;
    for (std::size_t f = 0; f < total; f += kBlock) {
        const int n = static_cast<int>(std::min<std::size_t>(kBlock, total - f));
        engine.render(out.data() + f * 2, amb.data() + f * 2, n);
    }
    if (ambience)
        *ambience = std::move(amb);
    return out;
}

// Runs the director for `seconds` at a fixed speed and returns the commands.
std::vector<MusicDirector::Command> run(MusicDirector& d, double seconds, float speed, int cops = 0,
                                        bool airborne = false) {
    std::vector<MusicDirector::Command> all;
    for (int i = 0; i < static_cast<int>(seconds * 20.0 + 0.5); ++i) {
        d.update(0.05f, speed, cops, airborne);
        for (const auto& c : d.takeCommands())
            all.push_back(c);
    }
    return all;
}

} // namespace

TEST(MusicDirector, RaceStartIdleAndReturn) {
    MusicDirector d(false);
    EXPECT_TRUE(run(d, 1.0, 0.0f).empty()); // StartMusic waits 1.25 s
    auto c = run(d, 0.5, 0.0f);
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0].state, MusicState::Start);
    EXPECT_EQ(c[0].timing, MusicTiming::Beat);
    // The countdown holds the idle logic however long the car stands still.
    EXPECT_TRUE(run(d, 10.0, 0.0f).empty());
    d.raceStarted();
    EXPECT_TRUE(run(d, 4.9, 0.0f).empty());
    c = run(d, 0.3, 0.0f); // 5 s at or below 5 m/s: idle, on the next measure
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0].state, MusicState::Idle);
    EXPECT_EQ(c[0].timing, MusicTiming::Measure);
    c = run(d, 0.1, 6.0f); // moving again: the Return segment
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0].state, MusicState::Return);
    EXPECT_EQ(c[0].timing, MusicTiming::Measure);
}

TEST(MusicDirector, CopChaseFollowsThePursuitCount) {
    MusicDirector d(false);
    run(d, 2.0, 10.0f);
    d.raceStarted();
    auto c = run(d, 0.1, 10.0f, 1); // 0 -> 1 cop: the chase, next beat
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0].state, MusicState::CopChase);
    EXPECT_EQ(c[0].timing, MusicTiming::Beat);
    EXPECT_TRUE(run(d, 1.0, 10.0f, 2).empty()); // 1 -> 2: nothing
    EXPECT_TRUE(run(d, 1.0, 10.0f, 1).empty());
    c = run(d, 6.0, 0.0f, 1); // stopped during the chase: the idle-cop segment
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0].state, MusicState::IdleCops);
    c = run(d, 0.1, 10.0f, 1);
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0].state, MusicState::CopChase);
    c = run(d, 0.1, 10.0f, 0); // 1 -> 0: Return
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0].state, MusicState::Return);
    EXPECT_TRUE(run(d, 1.0, 10.0f, 2).empty()); // 0 -> 2: no chase music
}

TEST(MusicDirector, CruiseGoesIdleAtOnceAndKeepsChasing) {
    MusicDirector d(true);
    auto c = run(d, 1.5, 0.0f);
    ASSERT_EQ(c.size(), 2u); // Start, then idle at once: the idle timer starts expired
    EXPECT_EQ(c[0].state, MusicState::Start);
    EXPECT_EQ(c[1].state, MusicState::Idle);
    run(d, 0.1, 10.0f);
    c = run(d, 0.1, 10.0f, 1);
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0].state, MusicState::CopChase);
    EXPECT_TRUE(run(d, 8.0, 0.0f, 1).empty()); // cruise has no idle-cop segment
}

TEST(MusicDirector, BigAirPauseAndResults) {
    MusicDirector d(false);
    run(d, 2.0, 10.0f);
    d.raceStarted();
    run(d, 0.1, 10.0f, 0, true);
    EXPECT_TRUE(d.takeBigAir());
    run(d, 0.5, 10.0f, 0, true); // still in the air: once per jump
    EXPECT_FALSE(d.takeBigAir());
    run(d, 0.1, 10.0f, 0, false);
    run(d, 0.1, 10.0f, 0, true);
    EXPECT_TRUE(d.takeBigAir());

    d.pause();
    auto c = d.takeCommands();
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0].state, MusicState::Paused);
    d.resume();
    c = d.takeCommands();
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0].state, MusicState::Start); // the segment before the pause, from its start

    d.finish();
    c = d.takeCommands();
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0].state, MusicState::Silent);
    d.results();
    c = d.takeCommands();
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0].state, MusicState::Results);
    EXPECT_TRUE(run(d, 10.0, 0.0f).empty()); // the results segment is left alone
}

TEST(MusicTables, ParsesRaceAndCruiseTables) {
    const char* race =
        "Start Music,Return Music,Idle Race Music,Idle Cop Music,Cop chase music,Pause Music,Race results Music,"
        "Big air Motif style,Big Air Motif name,Big Air Motif Band\r\n"
        "EnemyStart,EnemyReturn,EnemyIdle,EnemyIdleCops,EnemyCops,Pause,Results1,GrooverStyle,BigAir,BigAir\r\n"
        "\r\n";
    auto songs = MusicTables::parseRace(race);
    ASSERT_EQ(songs.size(), 1u);
    EXPECT_EQ(songs[0].start, "EnemyStart");
    EXPECT_EQ(songs[0].idleCops, "EnemyIdleCops");
    EXPECT_EQ(songs[0].cops, "EnemyCops");
    EXPECT_EQ(songs[0].results, "Results1");
    EXPECT_EQ(songs[0].motifStyle, "GrooverStyle");
    EXPECT_EQ(songs[0].motifBand, "BigAir");

    // The cruise table has no results column and swaps the cop columns.
    const char* cruise = "Start Music,Return Music,Idle Music,Cop Chase Music,idle cop music,Pause Music,"
                         "Big air Motif style,Big Air Motif name,Big Air Motif Band\n"
                         "SunroofStart,SunRoofReturn,SunRoofIdle,SunRoofCops,SunRoofIdle2,Pause,GrooverStyle,BigAir,BigAir\n";
    auto roam = MusicTables::parseCruise(cruise);
    ASSERT_EQ(roam.size(), 1u);
    EXPECT_EQ(roam[0].cops, "SunRoofCops");
    EXPECT_EQ(roam[0].idleCops, "SunRoofIdle2");
    EXPECT_TRUE(roam[0].results.empty());
    EXPECT_EQ(roam[0].motifName, "BigAir");

    EXPECT_EQ(MusicTables::parseSingle("Music segment\r\nUI\r\n"), "UI");
    EXPECT_EQ(MusicTables::parseSingle("SFX segment\nSFAMbience\n"), "SFAMbience");
    EXPECT_EQ(MusicTables::parseSingle("header only\n"), "");
}

TEST(Music, RetailTablesAndEverySegmentLoad) {
    MM2_REQUIRE_GAME_DATA();
    const auto tables = MusicTables::load(*test::gameData());
    EXPECT_EQ(tables.race.size(), 5u);
    EXPECT_EQ(tables.cruise.size(), 4u);
    EXPECT_EQ(tables.menu, "UI");
    MusicLibrary lib(*test::gameData());
    ASSERT_TRUE(lib.ok());

    std::vector<std::string> names = {tables.menu, tables.londonAmbience, tables.sfAmbience, "UndergrounAmbience"};
    for (const auto* list : {&tables.race, &tables.cruise})
        for (const auto& s : *list)
            for (const auto& n : {s.start, s.ret, s.idle, s.idleCops, s.cops, s.pause, s.results})
                if (!n.empty())
                    names.push_back(n);
    for (const auto& n : names) {
        auto info = lib.info(n);
        ASSERT_TRUE(info) << n;
        EXPECT_GT(info->seconds, 1.0) << n;
    }

    // Every segment file in aud/dmusic loads (styles, bands and DLS are pulled in as references).
    int segments = 0;
    for (const auto& path : lib.files()) {
        if (!path.ends_with(".sgt"))
            continue;
        EXPECT_TRUE(lib.info(path.substr(path.rfind('/') + 1))) << path;
        ++segments;
    }
    EXPECT_GT(segments, 50);
}

TEST(Music, RendersAudibleRaceMusicMotifAndAmbience) {
    MM2_REQUIRE_GAME_DATA();
    auto lib = std::make_shared<MusicLibrary>(*test::gameData());
    MusicEngine engine(lib, MusicTables::load(*test::gameData()), 48000);
    engine.selectSong(0, false);
    engine.preload();
    engine.setState(MusicState::Racing);
    engine.setAmbience("london");
    std::vector<float> ambience;
    const auto music = renderSeconds(engine, 2.0, &ambience);
    EXPECT_GT(rms(music), 0.01);
    EXPECT_GT(rms(ambience), 0.001);

    // The Big Air motif adds sound on top of the music once the next beat comes.
    MusicEngine plain(lib, MusicTables::load(*test::gameData()), 48000);
    plain.triggerMotif(); // nothing playing: starts immediately
    const auto motifOnly = renderSeconds(plain, 2.0);
    EXPECT_GT(rms(motifOnly), 0.005);

    // Stopping releases the notes; once their release tails are over, output is silent.
    engine.setState(MusicState::Silent);
    engine.setAmbience("");
    renderSeconds(engine, 2.0);
    const auto silence = renderSeconds(engine, 1.0);
    EXPECT_LT(rms(silence), 1e-4);
}

TEST(Music, PlayerStreamsThroughMixer) {
    MM2_REQUIRE_GAME_DATA();
    MusicPlayer player(*test::gameData(), 48000);
    ASSERT_TRUE(player.ok());
    Mixer mixer(48000);
    mixer.addStream(player.musicStream(), Bus::Music);
    player.playMenu();
    // Pull audio like the device callback would, in real time.
    std::vector<float> block(2 * 480), all;
    for (int i = 0; i < 100; ++i) { // 1 s
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        mixer.mix(block.data(), 480);
        all.insert(all.end(), block.begin(), block.end());
    }
    EXPECT_GT(rms(all), 0.005);
}

// MM2's segments have no chord track. With dmusic's zeroed default chord the
// drum parts of SunroofStart land an octave below the DLS drum key ranges and
// the first seconds are digital silence; DirectMusic's default chord (C2
// major) fixes that.
TEST(Music, ChordlessSegmentsUseDirectMusicDefaultChord) {
    MM2_REQUIRE_GAME_DATA();
    auto lib = std::make_shared<MusicLibrary>(*test::gameData());
    MusicEngine engine(lib, MusicTables::load(*test::gameData()), 48000);
    ASSERT_TRUE(engine.playSegment("SunroofStart"));
    const auto first = renderSeconds(engine, 1.0);
    EXPECT_GT(rms(first), 0.003);
}
