// Parity checks for the audio audit's second pass (docs/parity/audio.md): the
// tunnel echo, the ambient objects the world owns (bridges, trains, cable
// cars) and the pedestrians' voices, from MM2's own audio code (build 3393).
#include "TestData.h"

#include "audio/AngelRandom.h"
#include "audio/EchoEffect.h"
#include "audio/Mixer.h"
#include "audio/SoundBank.h"
#include "audio/game/CarAudio.h"
#include "audio/game/Object3D.h"
#include "audio/game/SoundSlot.h"
#include "core/File.h"
#include "vfs/DirectoryFs.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace mm2;
using namespace mm2::audio;
using namespace mm2::audio::game;

namespace {

constexpr std::int16_t kLevel = 8000; // every generated sample is a constant

std::vector<std::byte> wav(int frames) {
    std::vector<std::byte> out;
    auto put = [&](const void* p, std::size_t n) {
        const auto* b = static_cast<const std::byte*>(p);
        out.insert(out.end(), b, b + n);
    };
    const std::uint32_t rate = 22050, data = static_cast<std::uint32_t>(frames) * 2, size = 36 + data;
    const std::uint32_t fmt = 16, bytes = rate * 2;
    const std::uint16_t pcm = 1, mono = 1, align = 2, bits = 16;
    put("RIFF", 4), put(&size, 4), put("WAVE", 4), put("fmt ", 4), put(&fmt, 4), put(&pcm, 2), put(&mono, 2);
    put(&rate, 4), put(&bytes, 4), put(&align, 2), put(&bits, 2), put("data", 4), put(&data, 4);
    for (int i = 0; i < frames; ++i)
        put(&kLevel, 2);
    return out;
}

// A throwaway game-data tree with generated 22 kHz sounds and text files.
struct TempData {
    std::filesystem::path root;
    vfs::Vfs vfs;
    TempData(std::initializer_list<std::pair<const char*, std::string>> files,
             std::initializer_list<const char*> sounds, int frames = 22050) {
        root = std::filesystem::temp_directory_path() /
               ("openmm2_audio_world_" + std::to_string(std::rand()) +
                std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        for (const auto& [path, text] : files)
            file::writeAtomic(root / path, text);
        for (const char* s : sounds)
            file::writeAtomic(root / "aud/aud22" / (std::string(s) + ".22k.wav"),
                              std::span<const std::byte>(wav(frames)));
        vfs.mount(std::make_shared<vfs::DirectoryFs>(root));
    }
    ~TempData() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

// The left channel of one mixed block, as a gain of the generated level.
float mixedGain(Mixer& mixer) {
    std::vector<float> out(2 * 64);
    mixer.mix(out.data(), 64);
    return out[2 * 63] / (kLevel / 32768.0f);
}

CarAudioInputs grounded() {
    CarAudioInputs in;
    for (auto& w : in.wheels)
        w.onGround = true;
    return in;
}

} // namespace

TEST(AudioParityWorld, EffectVoicesAreOutsideTheVoiceLimit) {
    auto tone = std::make_shared<SoundBuffer>();
    tone->sampleRate = 22050;
    tone->channels = 1;
    tone->samples.assign(1000, kLevel);
    Mixer mixer(48000, 2);
    const VoiceHandle a = mixer.play(tone, {});
    const VoiceHandle b = mixer.play(tone, {});
    // EffectBase::CreateDSoundBuffer: a buffer audManager does not manage.
    const VoiceHandle e = mixer.createEffectVoice(tone, {});
    ASSERT_NE(e, 0u);
    EXPECT_FALSE(mixer.isPlaying(e)); // created stopped
    mixer.setPosition(e, 500);
    mixer.playEffect(e, false);
    EXPECT_TRUE(mixer.isPlaying(a));
    EXPECT_TRUE(mixer.isPlaying(b));
    EXPECT_TRUE(mixer.isPlaying(e));
    EXPECT_EQ(mixer.activeVoices(), 3);
    EXPECT_EQ(mixer.position(e), 500u);
    // IDirectSoundBuffer::Stop keeps the position.
    mixer.haltEffect(e);
    EXPECT_FALSE(mixer.isPlaying(e));
    EXPECT_EQ(mixer.position(e), 500u);
    // StopAllSounds stops the echo buffers too, without freeing them.
    mixer.playEffect(e, true);
    mixer.stopAll();
    EXPECT_FALSE(mixer.isPlaying(e));
    mixer.playEffect(e, true);
    EXPECT_TRUE(mixer.isPlaying(e));
    mixer.stop(e);
    EXPECT_FALSE(mixer.isPlaying(e));
}

TEST(AudioParityWorld, EchoRepeatsTheSoundAfterItsDelay) {
    TempData data({}, {"engine"});
    SoundBank bank(data.vfs);
    Mixer mixer(48000);
    SoundSlot s;
    ASSERT_TRUE(s.load(mixer, bank, "engine", Bus::Effects));
    s.setVolume(1.0f);
    // vehEngineSampleWrapper::EchoOn: SetEffect(1), SetDelayTime, SetEchoAttenuation.
    s.enableEcho();
    s.setEchoDelay(0.5f);
    s.setEchoAttenuation(kEchoAttenuation);
    ASSERT_TRUE(s.echo());
    EXPECT_FLOAT_EQ(s.echo()->delay(), 0.5f);
    s.setVolume(1.0f); // queued for the echo: (1 * 0.96 - 1) * 10000 = -400
    s.playLoop();
    EXPECT_EQ(mixer.activeVoices(), 1);
    // Every update ages the queued commands; at 0.5 s they reach the duplicate.
    s.updateEcho(0.25f);
    EXPECT_FALSE(s.echo()->playing());
    s.updateEcho(0.25f);
    EXPECT_TRUE(s.echo()->playing());
    EXPECT_EQ(mixer.activeVoices(), 2);
    // The original stops at once, the echo half a second later. Alone, the
    // echo plays at -4 dB (0.96 of an Angel volume of 1).
    s.stop();
    EXPECT_NEAR(mixedGain(mixer), std::pow(10.0f, -0.2f), 1e-3f);
    s.updateEcho(0.4f);
    EXPECT_TRUE(s.echo()->playing());
    s.updateEcho(0.1f);
    EXPECT_FALSE(s.echo()->playing());

    // DisableEchoEffect stops the duplicate; the EchoEffect stays, and only
    // the first SetDelayTime after it was made sets the delay.
    s.playLoop();
    s.disableEcho();
    EXPECT_FALSE(s.echoOn());
    s.enableEcho();
    s.setEchoDelay(0.05f);
    EXPECT_FLOAT_EQ(s.echo()->delay(), 0.5f);
    // SetDelayTime queues a play when the original is playing a loop.
    s.updateEcho(0.5f);
    EXPECT_TRUE(s.echo()->playing());
    s.stop();
}

TEST(AudioParityWorld, EchoPanIsMirroredAndQuartered) {
    TempData data({}, {"horn"});
    SoundBank bank(data.vfs);
    Mixer mixer(48000);
    SoundSlot s;
    ASSERT_TRUE(s.load(mixer, bank, "horn", Bus::Effects));
    s.setVolume(1.0f);
    s.enableEcho();
    s.setEchoDelay(0.05f);
    s.setVolume(1.0f);
    s.playLoop();
    s.updateEcho(0.05f);
    ASSERT_TRUE(s.echo()->playing());
    // EchoEffect::CalculatePan: -0.25 * pan at once; the echo of a sound
    // panned hard right sits a quarter to the left (-25 dB on the right).
    s.setPan(1.0f);
    s.stop();
    std::vector<float> out(2 * 64);
    mixer.mix(out.data(), 64);
    const float left = out[2 * 63] / (kLevel / 32768.0f), right = out[2 * 63 + 1] / (kLevel / 32768.0f);
    EXPECT_NEAR(left, std::pow(10.0f, -0.2f), 1e-3f);
    EXPECT_NEAR(right, std::pow(10.0f, -0.2f) * std::pow(10.0f, -1.25f), 1e-3f);
}

TEST(AudioParityWorld, TunnelEchoFollowsMmPlayer) {
    // mmPlayer::Update: EchoOn(0.5) when entering a tunnel (not again while in
    // one), EchoOff when leaving.
    Object3DManager manager;
    EXPECT_FALSE(manager.echo());
    manager.setTunnel(true);
    EXPECT_TRUE(manager.echo());
    EXPECT_FLOAT_EQ(manager.echoDelay(), kTunnelEchoDelay);
    manager.echoOn(0.2f);
    manager.setTunnel(true);
    EXPECT_FLOAT_EQ(manager.echoDelay(), 0.2f);
    manager.setTunnel(false);
    EXPECT_FALSE(manager.echo());
}

TEST(AudioParityWorld, PlayerCarEchoesInTunnels) {
    MM2_REQUIRE_GAME_DATA();
    const auto& v = *test::gameData();
    SoundBank bank(v);
    Mixer mixer(48000);
    Object3DManager manager;
    CarAudioOptions opts;
    opts.manager = &manager; // the player's car reads the echo state
    PlayerCarAudio car;
    std::string err;
    ASSERT_TRUE(car.load(v, bank, mixer, "vpbug", opts, &err)) << err;
    CarAudioInputs in = grounded();
    in.rpm = 900;
    for (int i = 0; i < 10; ++i)
        car.update(in, 0.02f);
    EXPECT_EQ(mixer.activeVoices(), 4); // VWIDLE, VWDRIVE, VWMID, VWHIGH
    // In the tunnel every engine sample gets an echo half a second behind.
    manager.setTunnel(true);
    for (int i = 0; i < 20; ++i)
        car.update(in, 0.02f);
    EXPECT_EQ(mixer.activeVoices(), 4);
    for (int i = 0; i < 10; ++i)
        car.update(in, 0.02f);
    EXPECT_EQ(mixer.activeVoices(), 8);
    // Out of the tunnel the echoes stop at once (DisableEchoEffect).
    manager.setTunnel(false);
    car.update(in, 0.02f);
    EXPECT_EQ(mixer.activeVoices(), 4);
    car.stop();
}
