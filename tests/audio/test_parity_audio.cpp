// Parity checks for the audio audit (docs/parity/audio.md): behaviour taken
// from MM2's own audio code (build 3393).
#include "audio/AngelRandom.h"
#include "audio/AngelUnits.h"
#include "audio/Mixer.h"
#include "audio/MusicDirector.h"
#include "audio/TextFields.h"
#include "audio/game/AudioTables.h"
#include "audio/game/CarAudio.h"
#include "audio/game/Object3D.h"
#include "audio/game/SoundSlot.h"
#include "core/File.h"
#include "vfs/DirectoryFs.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <memory>

using namespace mm2;
using namespace mm2::audio;
using namespace mm2::audio::game;

namespace {

std::shared_ptr<SoundBuffer> tone(int frames, int rate = 22050) {
    auto s = std::make_shared<SoundBuffer>();
    s->sampleRate = rate;
    s->channels = 1;
    s->samples.assign(static_cast<std::size_t>(frames), 8000);
    return s;
}

struct FixedSeed {
    explicit FixedSeed(std::int32_t seed) {
        setRandomizeSeedSource([seed] { return seed; });
    }
    ~FixedSeed() { setRandomizeSeedSource({}); }
};

} // namespace

TEST(AudioParity, AngelRandomIsKnuthsSubtractiveGenerator) {
    // Random::Seed / Random::Number: the first numbers for two seeds.
    // The state times the float 1e-9.
    const double scale = static_cast<double>(1e-9f);
    AngelRandom a(0);
    EXPECT_DOUBLE_EQ(a.number(), 533923850 * scale);
    EXPECT_DOUBLE_EQ(a.number(), 323008803 * scale);
    AngelRandom b(1700000000); // a time(NULL) value of the kind MM2 seeds with
    EXPECT_DOUBLE_EQ(b.number(), 133923850 * scale);
}

TEST(AudioParity, RandomizeNumberRepeatsWithinASecond) {
    // AudManagerBase::RandomizeNumber seeds a new generator with time(NULL)
    // on every call: draws in the same second are equal.
    FixedSeed seed(1700000000);
    EXPECT_EQ(randomizeNumber(10.0f), randomizeNumber(10.0f));
    EXPECT_NEAR(randomizeNumber(10.0f), 10.0 * 0.01f * 0.133923850 * 100.0, 1e-6);
    // The low end goes through a float: (high - low) * u + low, in hundredths.
    const double v = randomizeNumber(0.75f, 1.0f);
    EXPECT_NEAR(v, 0.75 + 0.25 * 0.133923850, 1e-6);
    // A one-shot's random volume and pan come from the same number.
    const double pan = randomizeNumber(-1.0f, 1.0f);
    EXPECT_NEAR((pan + 1.0) / 2.0, (v - 0.75) / 0.25, 1e-6);
}

TEST(AudioParity, StrtokCellsAndAtofPrefixes) {
    const auto cells = strtokFields("VWHORN,0.95,,4,REVERSE");
    ASSERT_EQ(cells.size(), 4u); // the empty cell is skipped
    EXPECT_EQ(cells[2], "4");
    EXPECT_FLOAT_EQ(crtAtof("0.9x"), 0.9f);
    EXPECT_FLOAT_EQ(crtAtof("  -1.5e1abc"), -15.0f);
    EXPECT_FLOAT_EQ(crtAtof("abc"), 0.0f);
    EXPECT_FLOAT_EQ(crtAtof("1e"), 1.0f);
    EXPECT_EQ(crtAtoi("0x10"), 0); // no hex in atoi
    EXPECT_EQ(crtAtoi("12abc"), 12);
    EXPECT_EQ(crtAtoi(" -7"), -7);
    const auto lines = fgetsLines("a\r\nb\n\nc");
    ASSERT_EQ(lines.size(), 4u);
    EXPECT_EQ(lines[1], "b");
    EXPECT_EQ(lines[2], "");
    EXPECT_EQ(lines[3], "c");
}

TEST(AudioParity, TablesReadLikeTheOriginalLoaders) {
    // vehCarAudio::Load: strtok shifts the cells after an empty one.
    auto car = parseCarAudio("h\nHORN,0.9,,4,CLUTCH,0.8\n"
                             "Engine wave name,a,b,fade in start RPM\n"
                             "IDLE,0.5,0.9,1,800,2500,7000,0.8,1.6,1,7000\n");
    ASSERT_TRUE(car);
    EXPECT_EQ(car->flags, 4u);
    EXPECT_EQ(car->clutch, "0.8"); // as MM2 reads it
    ASSERT_EQ(car->engine.size(), 1u);
    // The "Volume Divisor" engine layout is not supported.
    EXPECT_FALSE(parseCarAudio("h\nHORN,0.9,0,4,CLUTCH,0.8\nEngine,a,b,Volume Divisor\nX,1,2,3\n"));

    // AudImpact::ReadCSV discards a table without ENDOFDATA.
    const char* impacts = "***\nBanger name,Num samples,ID\nWALL,1,0\n"
                          "sample name,a,b,c,d,e\nsoft,0.9,0.95,0,100,1\n";
    EXPECT_FALSE(parseImpactTable(impacts));
    EXPECT_TRUE(parseImpactTable(std::string(impacts) + "***\nBanger name,Num samples,ID\nENDOFDATA,0,0\n"));

    // ReadSirenData: rows after one "play time" header all belong to it.
    auto siren = parseSirenTable("Explosion sample,volume\nboom,0.95\nSample name,\nwail,0.9\n"
                                 "play time,next index\n1,0\n2,0\n");
    ASSERT_TRUE(siren);
    EXPECT_EQ(siren->explosion, "boom");
    ASSERT_EQ(siren->samples.size(), 1u);
    EXPECT_EQ(siren->samples[0].steps.size(), 2u);

    // vehtypes.csv: names compared exactly, TRUE in any case.
    auto types =
        parseVehicleTypes("Semi or bus\nvpbus,ENDOFDATA\nPolice\nvpcop,ENDOFDATA\nAlways nitro\ntrue\n");
    EXPECT_TRUE(types.isFreight("vpbus"));
    EXPECT_FALSE(types.isFreight("VPBUS"));
    EXPECT_TRUE(types.alwaysNitro);
}

TEST(AudioParity, SoundSlotClampsLikeAudObject) {
    SoundSlot slot;
    // audObject::SetPan clamps to -1..1 (ShelterOff's +-20 pans are hard pans).
    slot.setPan(-20.0f);
    EXPECT_FLOAT_EQ(slot.pan(), -1.0f);
    slot.setPan(0.5f);
    EXPECT_FLOAT_EQ(slot.pan(), 0.5f);

    // A buffer to play: one generated 22 kHz sample in a temporary tree.
    const auto root = std::filesystem::temp_directory_path() / "openmm2_parity_audio";
    std::vector<std::byte> wav;
    auto put = [&](const void* p, std::size_t n) {
        const auto* b = static_cast<const std::byte*>(p);
        wav.insert(wav.end(), b, b + n);
    };
    const std::uint32_t frames = 2205, rate = 22050, size = 36 + frames * 2, fmt = 16, bytes = rate * 2,
                        data = frames * 2;
    const std::uint16_t pcm = 1, mono = 1, align = 2, bits = 16;
    put("RIFF", 4), put(&size, 4), put("WAVE", 4), put("fmt ", 4), put(&fmt, 4), put(&pcm, 2), put(&mono, 2);
    put(&rate, 4), put(&bytes, 4), put(&align, 2), put(&bits, 2), put("data", 4), put(&data, 4);
    wav.resize(wav.size() + data);
    file::writeAtomic(root / "aud/aud22/beep.22k.wav", std::span<const std::byte>(wav));
    vfs::Vfs vfs;
    vfs.mount(std::make_shared<vfs::DirectoryFs>(root));
    SoundBank bank(vfs);
    Mixer mixer(48000);
    SoundSlot beep;
    ASSERT_TRUE(beep.load(mixer, bank, "beep", Bus::Effects));
    // AudSoundBase::SetFrequency -> audObject::SetPitch: 0..2, then 100..100000 Hz.
    beep.setPitch(2.5f);
    EXPECT_FLOAT_EQ(beep.pitch(), 2.0f);
    beep.setPitch(-1.0f);
    EXPECT_FLOAT_EQ(beep.pitch(), 100.0f / 22050.0f);
    // PlayLoop's own values go straight to the buffer: no 0..2 clamp.
    beep.playLoop(SoundSlot::kKeep, 2.5f);
    EXPECT_FLOAT_EQ(beep.pitch(), 2.5f);
    EXPECT_TRUE(beep.playing());
    // PlayOnce / PlayLoop leave a playing buffer playing (audSound::Play).
    beep.playOnce();
    EXPECT_TRUE(beep.playing());
    EXPECT_EQ(mixer.activeVoices(), 1);
    beep.stop();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST(AudioParity, SilencedEngineKeepsItsFadeSlopes) {
    // vehEngineSampleWrapper::Silence zeroes min and max volume but not the
    // slopes ParseCSVBuffer computed: the fade-in still rises from 0.
    const EngineSampleDef d{"E", 0.55f, 0.95f, 1000, 2000, 5000, 6000, 1, 1, 0, 1};
    const auto e = EngineSound::evaluate(d, 1999.0f, true);
    EXPECT_NEAR(e.volume, 999.0f * 0.4f / 1000.0f, 1e-5f);
    EXPECT_TRUE(e.audible); // 0.3996 >= 0.25
    EXPECT_FLOAT_EQ(EngineSound::evaluate(d, 3000.0f, true).volume, 0.0f);
}

TEST(AudioParity, ImpactStrengthAndNoBanger) {
    // aiVehicleActive / vehCarDamage: |z| + |y| + |x| in that order.
    EXPECT_FLOAT_EQ(impactStrength({1e-8f, 1.0f, 1e8f}), (1e8f + 1.0f) + 1e-8f);
    // AudImpact::Play(force, -1) plays nothing.
    auto table = parseImpactTable("***\nh\nWALL,1,0\nh\nsoft,0.9,0.95,0,999999,1\n***\nh\nENDOFDATA,0,0\n");
    ASSERT_TRUE(table);
    ImpactSounds impacts;
    impacts.play({500.0f, -1, {}});
    EXPECT_EQ(impacts.lastPlayed(), -1);
}

TEST(AudioParity, AttenuationDistanceIsMeasuredByEachCheck) {
    // WithinMaxDistance and PastMaxDistance both run CalcDistToClosestHeads2:
    // on the update a slot is taken the second call sees no movement.
    Audio3D a;
    a.setDropOffs(0.0f, 100.0f);
    const Vec3 listener{0, 0, 0};
    EXPECT_FALSE(a.withinMaxDistance({200, 0, 0}, listener));
    EXPECT_TRUE(a.withinMaxDistance({50, 0, 0}, listener));
    EXPECT_NEAR(a.doppler(1.0f, 1.0f), 151.0f, 1e-3f); // closed 150 m since the last check
    EXPECT_FALSE(a.pastMaxDistance({50, 0, 0}, listener));
    EXPECT_FLOAT_EQ(a.doppler(1.0f, 1.0f), 1.0f);
    // Before SetDropOffs the maximum is -1: never within range.
    Audio3D fresh;
    EXPECT_FALSE(fresh.withinMaxDistance({0, 0, 0}, listener));
}

TEST(AudioParity, MixerUsesMm2MasterVolumeAndVoiceLimit) {
    EXPECT_EQ(Mixer::kMaxVoices, 32);
    EXPECT_FLOAT_EQ(ageMasterVolume(1.0f), 1.0f);
    EXPECT_FLOAT_EQ(ageMasterVolume(0.0f), 0.0f);
    EXPECT_NEAR(ageMasterVolume(0.005f), 0.0f, 1e-6f);

    // An Angel voice at volume 0.9 with a half slider: clamp(0.9 * 0.869) = 0.782.
    Mixer m(1000);
    m.setBusVolume(Bus::Effects, 0.5f);
    VoiceParams p;
    p.angel = true;
    p.volume = 0.9f;
    m.play(tone(1000, 1000), p);
    std::vector<float> out(2 * 4);
    m.mix(out.data(), 4);
    EXPECT_NEAR(out[0], 8000.0f / 32768.0f * ageVolumeToGain(0.9f * ageMasterVolume(0.5f)), 1e-4f);

    // Mono: every voice centred.
    Mixer mono(1000);
    mono.setStereo(false);
    VoiceParams left;
    left.pan = -1.0f;
    mono.play(tone(1000, 1000), left);
    mono.mix(out.data(), 4);
    EXPECT_NEAR(out[0], out[1], 1e-6f);

    // Stealing: the oldest voice of the lowest priority, looping or not.
    Mixer two(1000, 2);
    VoiceParams loop;
    loop.loop = true;
    const auto a = two.play(tone(1000, 1000), loop);
    const auto b = two.play(tone(1000, 1000), {});
    two.play(tone(1000, 1000), {});
    EXPECT_FALSE(two.isPlaying(a));
    EXPECT_TRUE(two.isPlaying(b));
}

TEST(AudioParity, AmbientHornAvoidanceNeverPicksTheStuckBlast) {
    // vehHornAudio::PlayAvoidance: RandomizeNumber(2 * last - 0.01) below
    // `last` picks a pattern; the last one is kept for impacts.
    const int last = 2; // three patterns
    int picked = 0;
    for (int s = 0; s < 400; ++s) {
        FixedSeed seed(1700000000 + s * 7919);
        const int choice = static_cast<int>(randomizeNumber(static_cast<float>(last * 2) - 0.01f));
        EXPECT_LT(choice, 4);
        if (choice < last)
            ++picked;
    }
    EXPECT_GT(picked, 120);
    EXPECT_LT(picked, 280);
}

TEST(AudioParity, MusicDirectorFollowsThePopupAndDamage) {
    MusicDirector d;
    for (int i = 0; i < 100; ++i)
        d.update(0.02f, 20.0f, 0, false);
    d.raceStarted();
    (void)d.takeCommands();
    // PlayReturnMusic switches to the previous segment whatever the current one.
    d.update(0.02f, 20.0f, 1, false); // cop chase
    (void)d.takeCommands();
    d.resume();
    auto c = d.takeCommands();
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0].state, MusicState::Start);
    // StopSegment(1) on a wreck: an ending on the next beat.
    d.damagedOut();
    c = d.takeCommands();
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0].state, MusicState::Silent);
    EXPECT_EQ(c[0].timing, MusicTiming::Beat);
}
