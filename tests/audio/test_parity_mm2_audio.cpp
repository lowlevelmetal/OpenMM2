// Parity checks for the reverse audit of MM2's audio classes
// (docs/parity/mm2/audio.md): what MM2's audio manager does once a frame
// (AudManager::Update, AudManagerBase::UpdatePaused), the sound objects'
// Reset (Aud3DObject::Reset through vehCarAudioContainer::Reset,
// aiAmbientVehicleAudio::Reset, Aud3DAmbientObject::Reset,
// aiCableCarAudio::Reset) and the ambient drivers' voices
// (aiAmbientVehicleAudio's AudCreature).
#include "TestData.h"

#include "audio/AngelRandom.h"
#include "audio/Mixer.h"
#include "audio/MusicDirector.h"
#include "audio/SoundBank.h"
#include "audio/game/Ambience.h"
#include "audio/game/AudioManager.h"
#include "audio/game/CarAudio.h"
#include "audio/game/Object3D.h"
#include "audio/game/SoundSlot.h"
#include "audio/game/Voices.h"
#include "core/File.h"
#include "vfs/DirectoryFs.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace mm2;
using namespace mm2::audio;
using namespace mm2::audio::game;

namespace {

constexpr std::int16_t kLevel = 8000;

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

// A throwaway game-data tree with generated 22 kHz sounds (one second long)
// and text files.
struct TempData {
    std::filesystem::path root;
    vfs::Vfs vfs;
    TempData(std::initializer_list<std::pair<const char*, std::string>> files,
             std::initializer_list<const char*> sounds, int frames = 22050) {
        root = std::filesystem::temp_directory_path() /
               ("openmm2_audio_mm2_" + std::to_string(std::rand()) +
                std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        for (const auto& [path, text] : files)
            file::writeAtomic(root / path, text);
        for (const char* s : sounds) {
            file::writeAtomic(root / "aud/aud22" / (std::string(s) + ".22k.wav"),
                              std::span<const std::byte>(wav(frames)));
            file::writeAtomic(root / "aud/aud11" / (std::string(s) + ".11k.wav"),
                              std::span<const std::byte>(wav(frames)));
        }
        vfs.mount(std::make_shared<vfs::DirectoryFs>(root));
    }
    ~TempData() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

// A constant stream, standing in for the DirectMusic soundtrack.
class ToneStream final : public StreamSource {
public:
    void render(float* stereo, int frames) override {
        for (int i = 0; i < 2 * frames; ++i)
            stereo[i] = 0.25f;
    }
};

const char* const kAmbientEngine =
    "Engine sample,engine volume,,\nengine,0.98,,\nmin speed,max speed,min pitch,max pitch\n"
    "0,33,0.317,1\n33,500,1,1.25\n0,500,0.317,13.977\n";
const char* const kAmbientHorn = "Horn sample,horn volume,horn pitch,min stuck horn impact force\nhorn,0.97,1,5500\n"
                                 "horn play duration,horn pause duration,,\n0.5,0.2,,\n1,0,,\n";

} // namespace

TEST(AudioParityMm2, PausedGameStopsEverySoundOnTwoUpdates) {
    TempData data({}, {"engine"});
    SoundBank bank(data.vfs);
    Mixer mixer(48000);
    mixer.setBusVolume(Bus::Music, 1.0f);
    const int music = mixer.addStream(std::make_shared<ToneStream>(), Bus::Music);
    SoundSlot s;
    ASSERT_TRUE(s.load(mixer, bank, "engine", Bus::Effects));
    s.setVolume(1.0f);
    s.playLoop();
    AudioManager manager;
    EXPECT_FALSE(manager.update(false, mixer, nullptr, 0.02f));
    EXPECT_TRUE(s.playing());
    // AudManagerBase::UpdatePaused: the first paused update stops everything.
    EXPECT_TRUE(manager.update(true, mixer, nullptr, 0.02f));
    EXPECT_FALSE(s.playing());
    EXPECT_EQ(mixer.activeVoices(), 0);
    // A sound the first paused frame started is stopped by the second.
    s.playLoop();
    EXPECT_TRUE(manager.update(true, mixer, nullptr, 0.02f));
    EXPECT_FALSE(s.playing());
    // From the third paused update on the counter (2) is past the limit (1).
    s.playLoop();
    EXPECT_FALSE(manager.update(true, mixer, nullptr, 0.02f));
    EXPECT_TRUE(s.playing());
    EXPECT_EQ(manager.pausedUpdates(), 2);
    // The music is DirectMusic, not one of the manager's sounds: it plays on.
    std::vector<float> out(2 * 64);
    mixer.mix(out.data(), 64);
    EXPECT_GT(out[2 * 63], 0.0f);
    // Running again resets the counter, so the next pause stops again.
    EXPECT_FALSE(manager.update(false, mixer, nullptr, 0.02f));
    EXPECT_EQ(manager.pausedUpdates(), 0);
    EXPECT_TRUE(manager.update(true, mixer, nullptr, 0.02f));
    EXPECT_FALSE(s.playing());
    mixer.removeStream(music);
}

TEST(AudioParityMm2, AnnouncerQueueCountsTwiceAFrameWhileRunning) {
    const char* blitz = "Name prefix/type header,end sufix value,sufix add value\nPRERACE header,,\nPRE,2,0\n";
    TempData data({{"aud/spchdata/london.csv", "Num announcers\n1\nprefix\nAL\n"}, {"aud/spchdata/al1/blitz.csv", blitz}},
                  {"al1pre01", "al1pre02"});
    SoundBank bank(data.vfs);
    Mixer mixer(48000);
    auto queuePreRace = [&](Announcer& a) {
        ASSERT_TRUE(a.load(data.vfs, bank, mixer, "london"));
        a.beginRace(AnnouncerMode::Blitz, {}, 1, 4);
        std::string line;
        for (int i = 0; i < 40 && line.empty(); ++i) {
            setRandomizeSeedSource([i] { return 1000 + i; });
            line = a.playPreRace();
        }
        setRandomizeSeedSource({});
        ASSERT_EQ(line, "al1pre"); // queued for 1.5 s
    };
    {
        // Running: GameLoop's AudManager::Update and mmGame::Update both
        // update the queue, so 1.5 s pass in 0.75 s.
        Announcer a;
        queuePreRace(a);
        AudioManager manager;
        auto frame = [&] {
            manager.update(false, mixer, &a, 0.25f);
            a.update(0.25f); // mmGame::Update
        };
        frame();
        frame();
        EXPECT_EQ(mixer.activeVoices(), 0); // 1.0 s counted
        frame();
        EXPECT_EQ(mixer.activeVoices(), 1); // 1.5 s: the line starts
        a.stop();
    }
    {
        // Paused: only mmGame::Update's call; the queue keeps counting and
        // the line starts after the two stops, during the pause.
        Announcer a;
        queuePreRace(a);
        AudioManager manager;
        for (int i = 0; i < 5; ++i) {
            manager.update(true, mixer, &a, 0.25f);
            a.update(0.25f);
            EXPECT_EQ(mixer.activeVoices(), 0) << i;
        }
        manager.update(true, mixer, &a, 0.25f);
        a.update(0.25f);
        EXPECT_EQ(mixer.activeVoices(), 1);
        a.stop();
    }
}

TEST(AudioParityMm2, ResetForgetsTheDistanceHistory) {
    // Aud3DObject::Reset: d^2 1000000, pseudo distance and its change 0, the
    // previous pseudo distance -1, so the next measurement has no doppler.
    Audio3D a;
    a.setDropOffs(0.0f, 150.0f);
    EXPECT_TRUE(a.withinMaxDistance({10, 0, 0}, {}));
    EXPECT_FALSE(a.pastMaxDistance({60, 0, 0}, {})); // moved away 50 m
    EXPECT_NE(a.doppler(1.0f, 0.02f), 1.0f);
    a.reset();
    EXPECT_FLOAT_EQ(a.distance2(), 1.0e6f);
    EXPECT_FLOAT_EQ(a.doppler(1.0f, 0.02f), 1.0f);
    EXPECT_FALSE(a.pastMaxDistance({110, 0, 0}, {}));
    EXPECT_FLOAT_EQ(a.doppler(1.0f, 0.02f), 1.0f);
}

TEST(AudioParityMm2, AmbientObjectAndCableCarResetGiveUpTheirSlot) {
    TempData data({{"aud/ambient/ferry.csv",
                    "Min distance,Max distance,3D priority,audible area\n0,150,12,0\n"
                    "sample name,sample volume,sample type,oneshot time limit low,"
                    "oneshot time limit high,active,min speed,max speed,doppler\n"
                    "ferryloop,1,0,0,0,1,0,999999,1\n"}},
                  {"ferryloop", "CABLECAR", "CABLECARSTART", "CABLECARSTOP", "CABLECARGOBELL", "STREETCABLE"});
    SoundBank bank(data.vfs);
    Mixer mixer(48000);
    Object3DManager manager;
    const Mat34 listener = Mat34::identity();
    // gizFerry: Aud3DAmbientObject::Update(0) every frame, Reset with the world.
    AmbientObject ferry;
    ASSERT_TRUE(ferry.load(data.vfs, bank, mixer, "ferry", &manager));
    ferry.setPosition({20, 0, 0});
    ferry.update(listener, 0.0f, 0.02f);
    EXPECT_TRUE(ferry.audible());
    EXPECT_TRUE(ferry.samplePlaying(0));
    ferry.reset();
    EXPECT_FALSE(ferry.audible());
    EXPECT_FALSE(ferry.samplePlaying(0));
    EXPECT_EQ(manager.used(), 0);
    EXPECT_TRUE(ferry.active(0)); // the active flags stay
    ferry.update(listener, 0.0f, 0.02f);
    EXPECT_TRUE(ferry.samplePlaying(0));

    // aiCableCar::Reset -> aiCableCarAudio::Reset.
    CableCarAudio cable;
    ASSERT_TRUE(cable.load(bank, mixer, &manager, 0.0f));
    cable.update(listener, {10, 0, 0}, 0.2f, 0.02f); // starting
    EXPECT_TRUE(cable.audible());
    EXPECT_EQ(cable.state(), CableCarAudio::Starting);
    cable.reset();
    EXPECT_FALSE(cable.audible());
    EXPECT_EQ(manager.used(), 1); // only the ferry
    EXPECT_EQ(cable.state(), CableCarAudio::Starting); // the state stays
    ferry.stop();
    cable.stop();
}

TEST(AudioParityMm2, OpponentCarResetGivesUpItsSlot) {
    MM2_REQUIRE_GAME_DATA();
    const auto& v = *test::gameData();
    SoundBank bank(v);
    Mixer mixer(48000);
    Object3DManager manager;
    CarAudioOptions opts;
    opts.manager = &manager;
    OpponentCarAudio car;
    std::string err;
    ASSERT_TRUE(car.load(v, bank, mixer, "vpbug", false, opts, &err)) << err;
    CarAudioInputs in;
    for (auto& w : in.wheels)
        w.onGround = true;
    in.rpm = 2000;
    in.transform.m3 = {10, 0, 0};
    const Mat34 listener = Mat34::identity();
    car.update(in, 0.02f, listener);
    EXPECT_TRUE(car.audible());
    EXPECT_GT(mixer.activeVoices(), 0);
    // vehCar::Reset -> vehCarAudioContainer::Reset -> Aud3DObject::Reset.
    car.reset();
    EXPECT_FALSE(car.audible());
    EXPECT_EQ(manager.used(), 0);
    EXPECT_EQ(mixer.activeVoices(), 0);
    car.update(in, 0.02f, listener);
    EXPECT_TRUE(car.audible());
    car.stop();
}

TEST(AudioParityMm2, AmbientDriverVoiceFollowsItsCar) {
    TempData data({{"aud/cardata/ambient/default_engine.csv", kAmbientEngine},
                   {"aud/cardata/ambient/default_horn.csv", kAmbientHorn}},
                  {"engine", "horn", "line1", "line2"});
    SoundBank bank(data.vfs);
    Mixer mixer(48000);
    Object3DManager manager;
    AmbientCarAudio car;
    ASSERT_TRUE(car.load(data.vfs, bank, mixer, "va_test", &manager));
    const auto def = parseCreatureVoice("Min speed,Max speed,min time in range,max time out of range\n0,20,5,1\n"
                                        "sample name,volume,,\nline1,0.98,,\nline2,0.98,,\n");
    ASSERT_TRUE(def);
    struct Owner : CreatureVoice::Owner {
        AmbientCarAudio* car = nullptr;
        bool requestSlot() override { return car->audible(); }
    } owner;
    owner.car = &car;
    CreatureVoice voice;
    voice.load(mixer, bank, *def);
    voice.setOwner(&owner);
    car.setVoice(&voice);
    CreatureVoice::resetGlobals();

    // AudCreatureAvoid::QueuePlay queues a line when RandomizeNumber(4) is
    // below 2: find a seed for which it is.
    std::int32_t seed = 1;
    for (; seed < 100; ++seed) {
        setRandomizeSeedSource([seed] { return seed; });
        if (randomizeNumber(4.0f) < 2.0)
            break;
    }
    ASSERT_LT(seed, 100);

    Mat34 at = Mat34::identity();
    at.m3 = {10, 0, 0};
    const Mat34 listener = Mat34::identity();
    auto frame = [&](float dt) {
        voice.update(0.0f, dt); // AudCreatureContainer::UpdateStatics
        car.update(12.0f, at, {}, dt, listener);
    };
    // Without a slot PlayAvoidanceReaction does nothing.
    car.avoidReaction();
    frame(0.02f);
    EXPECT_TRUE(car.audible());
    EXPECT_FALSE(voice.speaking());
    // With one the driver says a line (within 50 m: the squared distance the
    // car's UpdateAudio gave the voice).
    car.avoidReaction();
    frame(0.02f);
    EXPECT_TRUE(voice.speaking());
    voice.stop();

    // Losing the slot drops the queued lines (AudCreature::UnAssignSounds).
    car.avoidReaction();
    car.reset();
    EXPECT_FALSE(car.audible());
    frame(0.02f);
    frame(0.02f);
    EXPECT_FALSE(voice.speaking());
    voice.stop();

    // In a tunnel the voice's lines echo with the car's engine
    // (aiAmbientVehicleAudio::EchoOn / UpdateEcho include the AudCreature).
    car.reset();
    manager.setTunnel(true);
    for (int i = 0; i < 30; ++i) // 0.6 s: the engine and its echo
        frame(0.02f);
    EXPECT_EQ(mixer.activeVoices(), 2);
    car.avoidReaction();
    frame(0.02f);
    ASSERT_TRUE(voice.speaking());
    for (int i = 0; i < 26; ++i) // past the 0.5 s delay: the line's echo too
        frame(0.02f);
    EXPECT_EQ(mixer.activeVoices(), 4);
    setRandomizeSeedSource({});
    voice.stop();
    car.stop();
}

TEST(AudioParityMm2, FinalStretchSwitchesToTheChaseMusic) {
    // mmWaypoints::Update: SegmentSwitch(cop chase, END, BEAT) at the final
    // checkpoint and a circuit's final lap; nothing when it already plays.
    MusicDirector d(false);
    for (int i = 0; i < 100; ++i) // past the 1.25 s start, moving
        d.update(0.02f, 20.0f, 0, false);
    d.raceStarted();
    d.takeCommands();
    d.finalStretch();
    const auto c = d.takeCommands();
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0].state, MusicState::CopChase);
    EXPECT_EQ(c[0].timing, MusicTiming::Beat);
    EXPECT_EQ(d.current(), MusicState::CopChase);
    d.finalStretch();
    EXPECT_TRUE(d.takeCommands().empty());
    // A cop chase ending afterwards (1 -> 0 pursuing cops) returns as usual.
    d.update(0.02f, 20.0f, 1, false);
    d.update(0.02f, 20.0f, 0, false);
    const auto r = d.takeCommands();
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].state, MusicState::Return);
}

TEST(AudioParityMm2, OldEngineTableLayout) {
    // vehEngineAudio::Load: "Volume Divisor" in the engine header's fourth
    // cell selects ParseCSVBufferOld for every row: name, min volume, max
    // volume, divisor, min pitch, max pitch, (unused), cut RPM.
    const auto car = parseCarAudio("h\nHORN,0.9,0,4,REVERSE,0.8\n"
                                   "Engine wave name,min vol,max vol,volume divisor\n"
                                   "IDLE,0.3,0.95,4000,0.8,1.6,1000,5000\n");
    ASSERT_TRUE(car);
    ASSERT_TRUE(car->oldEngineLayout);
    ASSERT_EQ(car->engine.size(), 1u);
    const EngineSampleDef& d = car->engine[0];
    EXPECT_TRUE(d.oldLayout);
    EXPECT_FLOAT_EQ(d.volumeDivisor, 4000.0f);
    EXPECT_FLOAT_EQ(d.minPitch, 0.8f);
    EXPECT_FLOAT_EQ(d.maxPitch, 1.6f);
    EXPECT_FLOAT_EQ(d.cutRpm, 5000.0f);
    // CalculateVolumeOld: rpm / divisor below the cut, divisor / rpm from it,
    // clamped to min, then max.
    EXPECT_FLOAT_EQ(EngineSound::evaluate(d, 800.0f).volume, 0.3f);   // 0.2 -> min
    EXPECT_FLOAT_EQ(EngineSound::evaluate(d, 2000.0f).volume, 0.5f);  // 2000 / 4000
    EXPECT_FLOAT_EQ(EngineSound::evaluate(d, 4800.0f).volume, 0.95f); // 1.2 -> max
    EXPECT_FLOAT_EQ(EngineSound::evaluate(d, 8000.0f).volume, 0.5f);  // 4000 / 8000
    EXPECT_FALSE(EngineSound::evaluate(d, 2000.0f, true).audible);    // Silence: 0..0
    // The pitch range is never set: the max pitch above 0 RPM (inferred).
    EXPECT_FLOAT_EQ(EngineSound::evaluate(d, 2000.0f).pitch, 1.6f);
}
