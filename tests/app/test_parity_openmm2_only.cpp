// Parity checks for the audio options' effect (docs/parity/openmm2-only.md).
#include "app/Context.h"

#include <gtest/gtest.h>

using namespace mm2;

// AudioOptions::SetAudioState: SOUND FX off silences every wave sound,
// commentary included; COMMENTARY off silences only the commentary.
TEST(AudioOptionsParity, SoundFxToggleSilencesCommentaryToo) {
    app::Context ctx;
    ctx.mixer = std::make_shared<audio::Mixer>(48000);

    ctx.settings.soundEffects = true;
    ctx.settings.commentary = true;
    ctx.applyAudioSettings();
    EXPECT_FLOAT_EQ(ctx.mixer->busVolume(audio::Bus::Effects), 1.0f);
    EXPECT_FLOAT_EQ(ctx.mixer->busVolume(audio::Bus::Engine), 1.0f);
    EXPECT_FLOAT_EQ(ctx.mixer->busVolume(audio::Bus::Voice), 1.0f);

    ctx.settings.commentary = false;
    ctx.applyAudioSettings();
    EXPECT_FLOAT_EQ(ctx.mixer->busVolume(audio::Bus::Effects), 1.0f);
    EXPECT_FLOAT_EQ(ctx.mixer->busVolume(audio::Bus::Voice), 0.0f);

    ctx.settings.commentary = true;
    ctx.settings.soundEffects = false;
    ctx.applyAudioSettings();
    EXPECT_FLOAT_EQ(ctx.mixer->busVolume(audio::Bus::Effects), 0.0f);
    EXPECT_FLOAT_EQ(ctx.mixer->busVolume(audio::Bus::Engine), 0.0f);
    EXPECT_FLOAT_EQ(ctx.mixer->busVolume(audio::Bus::Voice), 0.0f);
}
