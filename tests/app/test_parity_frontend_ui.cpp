// Settings mapped onto MM2's player configuration, checked against MM2's own
// code (MM2Recomp, midtown2.exe build 3393); see docs/parity/frontend-ui.md.
#include "app/Settings.h"

#include <gtest/gtest.h>

#include <filesystem>

using namespace mm2;

// AudioOptions::AudioOptions offers Mono, Stereo and (on a 16-bit device)
// Surround; AudioOptions::SetStereoFX keeps all three. Files from before the
// three-way option keep their Stereo choice.
TEST(FrontendParity, StereoFxHasMM2sThreeChoices) {
    const auto path = std::filesystem::temp_directory_path() / "openmm2_settings_stereofx.ini";
    {
        app::Settings s;
        EXPECT_EQ(s.stereoFx, 1);
        s.stereoFx = 2;
        ASSERT_TRUE(s.save(path));
    }
    app::Settings t;
    t.load(path);
    EXPECT_EQ(t.stereoFx, 2);
    EXPECT_EQ(t.ini.getString("Audio", "Stereo"), "");

    app::Settings old;
    old.ini.parse("[Audio]\nStereo=false\n");
    ASSERT_TRUE(old.ini.save(path));
    old.load(path);
    EXPECT_EQ(old.stereoFx, 0);
    std::filesystem::remove(path);
}

// SOUND QUALITY sets MM2's channel count only; the 22 kHz sounds are used at
// every quality (InitAudioManager).
TEST(FrontendParity, SoundQualityKeepsTheTwentyTwoKilohertzSounds) {
    const auto path = std::filesystem::temp_directory_path() / "openmm2_settings_quality.ini";
    app::Settings s;
    s.soundQuality = 0;
    ASSERT_TRUE(s.save(path));
    app::Settings t;
    t.load(path);
    EXPECT_EQ(t.soundQuality, 0);
    EXPECT_TRUE(t.audioHighQuality);
    std::filesystem::remove(path);
}
