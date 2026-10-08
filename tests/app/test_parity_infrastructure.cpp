// Reverse parity audit, infrastructure: midtown2.exe's own command-line
// options (datArgParser), docs/parity/mm2/infrastructure.md.
#include "app/CommandLine.h"

#include <gtest/gtest.h>

#include <initializer_list>
#include <vector>

using namespace mm2;

namespace {

app::CommandLine parse(std::initializer_list<const char*> args) {
    std::vector<char*> argv{const_cast<char*>("openmm2")};
    for (const char* a : args)
        argv.push_back(const_cast<char*>(a));
    return app::parseCommandLine(static_cast<int>(argv.size()), argv.data());
}

} // namespace

// aiCityData::aiCityData reads -pedpool (atoi of its first value) after the
// city's [Ped Pool], which it overrides; without a value nothing changes.
TEST(MM2CommandLine, PedPoolOverridesTheCityPool) {
    auto a = parse({"-pedpool", "40"});
    EXPECT_TRUE(a.error.empty()) << a.error;
    EXPECT_EQ(a.pedPool, 40);
    auto z = parse({"-pedpool=0"});
    EXPECT_EQ(z.pedPool, 0);
    auto none = parse({"-pedpool"});
    EXPECT_FALSE(none.pedPool.has_value());
    EXPECT_TRUE(none.ignoredOptions.empty());
}

// Main skips LOGOS.AVI with -nomovie, and also in a window (-window, -max):
// it plays the movie only when inWindow is false.
TEST(MM2CommandLine, MovieAndWindowOptions) {
    auto a = parse({"-nomovie"});
    EXPECT_TRUE(a.error.empty());
    EXPECT_TRUE(a.skipIntro);
    EXPECT_FALSE(a.fullscreen.has_value());

    auto w = parse({"-window", "-width", "800", "-height=600"});
    EXPECT_TRUE(w.error.empty()) << w.error;
    EXPECT_EQ(w.fullscreen, false);
    EXPECT_TRUE(w.skipIntro);
    EXPECT_EQ(w.width, 800);
    EXPECT_EQ(w.height, 600);

    auto m = parse({"-max"});
    EXPECT_EQ(m.fullscreen, true);
    EXPECT_TRUE(m.skipIntro);

    auto f = parse({"-fs"});
    EXPECT_EQ(f.fullscreen, true);
    EXPECT_FALSE(f.skipIntro);
    EXPECT_EQ(parse({"-fullscreen"}).fullscreen, true);
    EXPECT_EQ(parse({"-novblank"}).vsync, false);
}

// gfxPipeline::SetRes checks -window before -max and -fs, so -window wins
// whatever the order on the command line.
TEST(MM2CommandLine, WindowBeatsMaxBeatsFullscreen) {
    auto a = parse({"-fs", "-window"});
    EXPECT_EQ(a.fullscreen, false);
    EXPECT_TRUE(a.skipIntro);
    auto b = parse({"-max", "-window"});
    EXPECT_EQ(b.fullscreen, false);
    auto c = parse({"-fullscreen", "-max"});
    EXPECT_EQ(c.fullscreen, true);
    EXPECT_TRUE(c.skipIntro); // -max is a window: no logo movie
}

// datArgParser::Init keeps the first of a repeated option and takes every
// word up to the next "-<non-digit>" as its values.
TEST(MM2CommandLine, ArgParserRules) {
    auto a = parse({"-width", "1024", "-width", "640"});
    EXPECT_EQ(a.width, 1024);

    // A value starting with '-' and a digit is a value, not an option.
    auto b = parse({"-width", "-5"});
    EXPECT_FALSE(b.error.empty());

    // An option without a value leaves the setting alone (Get fails).
    auto c = parse({"-width", "-nomovie"});
    EXPECT_TRUE(c.error.empty());
    EXPECT_FALSE(c.width.has_value());
    EXPECT_TRUE(c.skipIntro);

    // OpenMM2's own options still follow.
    auto d = parse({"-nomovie", "--backend", "opengl"});
    EXPECT_TRUE(d.error.empty()) << d.error;
    EXPECT_EQ(d.backend, "opengl");
}

// InitAudioManager (-noaudio, -nosoundfx), mmGameMusicData::Load (-nomusic,
// -noaudio) and mmPlayer::InitSpeechAudio (-nospeech, -noaudio).
TEST(MM2CommandLine, AudioSwitches) {
    EXPECT_TRUE(parse({"-noaudio"}).noAudio);
    EXPECT_TRUE(parse({"-nosoundfx"}).noAudio);
    auto m = parse({"-nomusic", "-nospeech"});
    EXPECT_FALSE(m.noAudio);
    EXPECT_TRUE(m.noMusic);
    EXPECT_TRUE(m.noSpeech);
}

// The rest of what midtown2.exe reads is accepted and reported, as are words
// it never reads (datArgParser stores any "-word").
TEST(MM2CommandLine, UnsupportedOptionsAreIgnored) {
    auto a = parse({"-blade", "-display", "1", "-level", "london", "-car", "vpbug", "-tune_car", "-console"});
    EXPECT_TRUE(a.error.empty()) << a.error;
    EXPECT_EQ(a.ignoredOptions,
              (std::vector<std::string>{"-blade", "-display", "-level", "-car", "-tune_car"}));
    EXPECT_EQ(a.unknownOptions, (std::vector<std::string>{"-console"}));
    // Double-dash options stay strict.
    EXPECT_FALSE(parse({"--nomovie"}).error.empty());
}
