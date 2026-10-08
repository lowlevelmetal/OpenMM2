// Parity checks for the audio audit's second pass (docs/parity/audio.md): the
// tunnel echo, the ambient objects the world owns (bridges, trains, cable
// cars) and the pedestrians' voices, from MM2's own audio code (build 3393).
#include "TestData.h"

#include "audio/AngelRandom.h"
#include "audio/EchoEffect.h"
#include "audio/Mixer.h"
#include "audio/SoundBank.h"
#include "audio/game/Ambience.h"
#include "audio/game/CarAudio.h"
#include "audio/game/Object3D.h"
#include "audio/game/PedAudio.h"
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

struct FixedSeedSource {
    explicit FixedSeedSource(std::int32_t seed) {
        setRandomizeSeedSource([seed] { return seed; });
    }
    ~FixedSeedSource() { setRandomizeSeedSource({}); }
};

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

TEST(AudioParityWorld, BridgeSoundsFollowActivate) {
    TempData data({{"aud/ambient/drawbridge.csv",
                    "Min distance,Max distance,3D priority,audible area\n0,150,12,0\n"
                    "sample name,sample volume,sample type,oneshot time limit low,"
                    "oneshot time limit high,active,min speed,max speed,doppler\n"
                    "bridgemove,1,0,0,0,0,0,999999,1\nbridgebell,0.95,2,0,0,0,0,999999,1\n"}},
                  {"bridgemove", "bridgebell"});
    SoundBank bank(data.vfs);
    Mixer mixer(48000);
    BridgeAudio bridge;
    ASSERT_TRUE(bridge.load(data.vfs, bank, mixer, "drawbridge"));
    bridge.setPosition({20, 0, 0});
    const Mat34 listener = Mat34::identity();
    // Both samples start inactive: the object holds a slot but is silent.
    bridge.update(listener, 0.0f, 0.02f);
    EXPECT_TRUE(bridge.audible());
    EXPECT_EQ(mixer.activeVoices(), 0);
    // gizBridge: Activate(-1) when the span starts to move.
    bridge.activate(-1);
    bridge.update(listener, 0.0f, 0.02f);
    EXPECT_TRUE(bridge.samplePlaying(0)); // the moving loop
    EXPECT_TRUE(bridge.samplePlaying(1)); // the bell (an interval of 0: again once it ends)
    // Deactivate(-1) when it stops: the loop stops at once, the bell plays out.
    bridge.deactivate(-1);
    EXPECT_FALSE(bridge.samplePlaying(0));
    EXPECT_TRUE(bridge.samplePlaying(1));
    bridge.update(listener, 0.0f, 0.02f);
    EXPECT_FALSE(bridge.samplePlaying(0));
    // Out of range the object gives up its slot and its sounds.
    bridge.setPosition({200, 0, 0});
    bridge.update(listener, 0.0f, 0.02f);
    EXPECT_FALSE(bridge.audible());
    EXPECT_EQ(mixer.activeVoices(), 0);
}

TEST(AudioParityWorld, SubwaySwitchesSamplesAtOneMetrePerSecond) {
    TempData data({{"aud/ambient/subwaycar.csv",
                    "Min distance,Max distance,3D priority,audible area\n0,150,12,1\n"
                    "sample name,sample volume,sample type,oneshot time limit low,"
                    "oneshot time limit high,active,min speed,max speed,doppler\n"
                    "LondonTube,0.98,0,0,0,1,0,999999,1\nNOTHING,0.98,0,0,0,1,0,999999,1\n"}},
                  {"londontube"});
    SoundBank bank(data.vfs);
    Mixer mixer(48000);
    Object3DManager manager;
    SubwayAudio train;
    ASSERT_TRUE(train.load(data.vfs, bank, mixer, "subwaycar", &manager));
    train.setPosition({30, 0, 0});
    const Mat34 listener = Mat34::identity();
    // Audible area 1: only underground (the tunnel echo state).
    train.update(listener, 10.0f, 0.02f);
    EXPECT_FALSE(train.audible());
    manager.setTunnel(true);
    train.update(listener, 10.0f, 0.02f);
    EXPECT_TRUE(train.audible());
    EXPECT_TRUE(train.echoOn());
    EXPECT_TRUE(train.samplePlaying(0));
    // Below 1 m/s: the loop is deactivated (and stops), sample 1 activated.
    train.update(listener, 0.5f, 0.02f);
    EXPECT_TRUE(train.stopped());
    EXPECT_FALSE(train.active(0));
    EXPECT_TRUE(train.active(1));
    EXPECT_FALSE(train.samplePlaying(0));
    train.update(listener, 1.0f, 0.02f);
    EXPECT_FALSE(train.stopped());
    EXPECT_TRUE(train.samplePlaying(0));
    // Back above ground the set loses its slot.
    manager.setTunnel(false);
    train.update(listener, 10.0f, 0.02f);
    EXPECT_FALSE(train.audible());
    EXPECT_FALSE(train.samplePlaying(0));
}

TEST(AudioParityWorld, CableCarStatesFollowItsSpeed) {
    using S = CableCarAudio::State;
    // aiCableCarAudioData::UpdateState(speed, previous speed).
    EXPECT_EQ(CableCarAudio::nextState(S::Running, 0.0f, 0.0005f, false), S::Stopped);
    EXPECT_EQ(CableCarAudio::nextState(S::Stopped, 0.1f, 0.05f, false), S::Starting);
    EXPECT_EQ(CableCarAudio::nextState(S::Running, 0.5f, 0.6f, false), S::Stopping);
    EXPECT_EQ(CableCarAudio::nextState(S::Starting, 2.0f, 2.0f, true), S::Starting);
    EXPECT_EQ(CableCarAudio::nextState(S::Starting, 2.0f, 2.0f, false), S::Running);
    EXPECT_EQ(CableCarAudio::nextState(S::Stopping, 0.3f, 0.3f, false), S::Stopping);

    TempData data({}, {"cablecargobell", "cablecarstop", "cablecar", "cablecarstart", "streetcable"}, 2205);
    SoundBank bank(data.vfs);
    Mixer mixer(48000);
    CableCarAudio car;
    ASSERT_TRUE(car.load(bank, mixer));
    const Mat34 listener = Mat34::identity();
    const Vec3 at{10, 0, 0};
    car.update(listener, at, 0.0f, 0.02f);
    EXPECT_TRUE(car.audible());
    EXPECT_EQ(car.state(), S::Stopped);
    EXPECT_EQ(mixer.activeVoices(), 0);
    // Setting off: the start sound and the bell.
    car.update(listener, at, 0.2f, 0.02f);
    EXPECT_EQ(car.state(), S::Starting);
    EXPECT_EQ(mixer.activeVoices(), 2);
    // Once the start sound is over (0.1 s here) the running loop takes over.
    std::vector<float> scratch(2 * 9600);
    mixer.mix(scratch.data(), 9600);
    car.update(listener, at, 3.0f, 0.02f);
    EXPECT_EQ(car.state(), S::Running);
    EXPECT_EQ(mixer.activeVoices(), 1);
    // Slowing to 0.5 m/s: the loop stops and the stop sound plays.
    car.update(listener, at, 0.5f, 0.02f);
    EXPECT_EQ(car.state(), S::Stopping);
    EXPECT_EQ(mixer.activeVoices(), 1);
    car.stop();
}

TEST(AudioParityWorld, SurfaceSoundIsTheMaterialsShort) {
    // vehWheel::GetSurfaceSound: the material's sound as a short, -1 as 0.
    EXPECT_EQ(surfaceSoundIndex("default", -1), 0);
    EXPECT_EQ(surfaceSoundIndex("grass", 2), 2);
    EXPECT_EQ(surfaceSoundIndex("odd", 65535), 0); // -1 as a short
    EXPECT_EQ(surfaceSoundIndex("odd", -2), -2);   // used as it is
}

TEST(AudioParityWorld, PedestrianVoicesFollowMm2) {
    EXPECT_TRUE(PedestrianAudio::isWoman("pedmodel_woman"));
    EXPECT_TRUE(PedestrianAudio::isWoman("PEDMODEL_WOMANW"));
    EXPECT_TRUE(PedestrianAudio::isWoman("schoolgirl"));
    EXPECT_TRUE(PedestrianAudio::isWoman("hooker"));
    EXPECT_FALSE(PedestrianAudio::isWoman("pedmodel_man"));
    EXPECT_FALSE(PedestrianAudio::isWoman("wwoman")); // the search does not back up

    const std::string voice = "Min speed,Max speed,min time in range,max time out of range\n0,500,0.01,0\n"
                              "sample name,volume,,\nFEMALESCREAM1,0.98,,\n";
    TempData data({{"aud/creaturedata/numfemalepedvoicefiles.csv", "Num files\n1\n"},
                   {"aud/creaturedata/nummalepedvoicefiles.csv", "Num files\n3\n"},
                   {"aud/creaturedata/default_fpedvoice1.csv", voice}},
                  {"femalescream1"});
    SoundBank bank(data.vfs);
    Mixer mixer(48000);
    Object3DManager manager;
    PedestrianAudio peds;
    peds.load(data.vfs, bank, mixer, &manager);
    // One voice file per sex for the session: RandomizeNumber(1, n + 0.25).
    EXPECT_EQ(peds.femaleFile(), 1);
    EXPECT_GE(peds.maleFile(), 1);
    EXPECT_LE(peds.maleFile(), 3);

    const Mat34 listener = Mat34::identity();
    PedestrianSoundInput woman{7, "pedmodel_woman", {10, 0, 0}, false};
    for (int i = 0; i < 5; ++i)
        peds.update({&woman, 1}, listener, 20.0f, 0.02f);
    EXPECT_FALSE(peds.audible(7)); // a slot only for a reaction

    // A dodge asks for a slot and queues a scream half of the time; the next
    // update says it (AudCreatureAvoid::Update within 50 m).
    bool screamed = false;
    for (int seed = 1; seed < 40 && !screamed; ++seed) {
        FixedSeedSource fixed(seed);
        woman.avoiding = true;
        peds.update({&woman, 1}, listener, 20.0f, 0.02f);
        woman.avoiding = false;
        peds.update({&woman, 1}, listener, 20.0f, 0.02f);
        screamed = peds.speaking(7);
    }
    ASSERT_TRUE(screamed);
    EXPECT_TRUE(peds.audible(7));
    EXPECT_EQ(manager.used(), 1);
    // Once the scream is over the pedestrian gives its slot back.
    std::vector<float> scratch(2 * 48000);
    mixer.mix(scratch.data(), 48000);
    mixer.mix(scratch.data(), 4800);
    peds.update({&woman, 1}, listener, 20.0f, 0.02f);
    EXPECT_FALSE(peds.speaking(7));
    EXPECT_FALSE(peds.audible(7));
    EXPECT_EQ(manager.used(), 0);

    // Beyond 40 m a dodge gets no slot; a man has no voice file here.
    PedestrianSoundInput far{8, "pedmodel_woman", {45, 0, 0}, true};
    peds.update({&far, 1}, listener, 20.0f, 0.02f);
    EXPECT_FALSE(peds.audible(8));
    PedestrianSoundInput man{9, "pedmodel_man", {5, 0, 0}, true};
    peds.update({&man, 1}, listener, 20.0f, 0.02f);
    EXPECT_FALSE(peds.audible(9));

    // A pedestrian put on another road (aiPedestrian::Reset) gives up its
    // slot at once, mid-scream (AudCreatureContainer::Reset).
    screamed = false;
    for (int seed = 1; seed < 40 && !screamed; ++seed) {
        FixedSeedSource fixed(seed);
        woman.avoiding = true;
        peds.update({&woman, 1}, listener, 20.0f, 0.02f);
        woman.avoiding = false;
        peds.update({&woman, 1}, listener, 20.0f, 0.02f);
        screamed = peds.speaking(7);
    }
    ASSERT_TRUE(screamed);
    ASSERT_TRUE(peds.audible(7));
    woman.reset = true;
    peds.update({&woman, 1}, listener, 20.0f, 0.02f);
    EXPECT_FALSE(peds.audible(7));
    EXPECT_EQ(manager.used(), 0);
    peds.stop();
}
