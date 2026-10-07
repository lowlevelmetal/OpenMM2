#include "TestData.h"
#include "audio/game/Ambience.h"
#include "audio/game/AudioTables.h"
#include "audio/game/CarAudio.h"
#include "audio/game/Voices.h"
#include "core/File.h"
#include "core/StringUtil.h"
#include "vfs/DirectoryFs.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>

using namespace mm2;
using namespace mm2::audio;
using namespace mm2::audio::game;

namespace {

std::string_view asText(const std::vector<std::byte>& b) { return {reinterpret_cast<const char*>(b.data()), b.size()}; }

// A tiny 16-bit mono WAV.
std::vector<std::byte> wav(int frames, int rate = 22050) {
    std::vector<std::byte> out;
    auto put = [&](const void* p, std::size_t n) {
        const auto* b = static_cast<const std::byte*>(p);
        out.insert(out.end(), b, b + n);
    };
    auto u32 = [&](std::uint32_t v) { put(&v, 4); };
    auto u16 = [&](std::uint16_t v) { put(&v, 2); };
    put("RIFF", 4);
    u32(36 + frames * 2);
    put("WAVE", 4);
    put("fmt ", 4);
    u32(16);
    u16(1);
    u16(1);
    u32(static_cast<std::uint32_t>(rate));
    u32(static_cast<std::uint32_t>(rate * 2));
    u16(2);
    u16(16);
    put("data", 4);
    u32(static_cast<std::uint32_t>(frames * 2));
    for (int i = 0; i < frames; ++i) {
        const auto s = static_cast<std::int16_t>(8000 * std::sin(i * 0.1));
        put(&s, 2);
    }
    return out;
}

// A throwaway game-data tree with generated sounds and given text files.
struct FakeData {
    std::filesystem::path root;
    vfs::Vfs vfs;
    explicit FakeData(std::initializer_list<std::pair<const char*, std::string>> files,
                      std::initializer_list<const char*> sounds) {
        root = std::filesystem::temp_directory_path() /
               ("openmm2_gameaudio_" + std::to_string(std::rand()) + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        for (const auto& [path, text] : files)
            file::writeAtomic(root / path, text);
        for (const char* s : sounds)
            file::writeAtomic(root / "aud/aud22" / (std::string(s) + ".22k.wav"), std::span<const std::byte>(wav(2205)));
        vfs.mount(std::make_shared<vfs::DirectoryFs>(root));
    }
    ~FakeData() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

const char* kDefaultCar = "Horn wave name,Horn volume,flags,Num Engine Samples,clutch wave name,clutch volume,,,,,\r\n"
                          "RACECARHORN,0.95,0,2,REVERSE,0.93,,,,,\r\n"
                          "Engine wave name,Min Volume,Max Volume,fade in  start RPM,fade in end RPM,fade out start RPM,"
                          "fade out end RPM,Min Pitch,Max Pitch,Pitch shift start RPM,Pitch shift end RPM\r\n"
                          "RACECARIDLE,0.65,0.96,1,850,2500,7000,0.8,1.64,1,7000\r\n"
                          "RACECARDRIVE,0.7,0.94,500,3500,9000,12500,0.65,4.5,500,40000\r\n";

const char* kSurfaceDry = "Tunnel sound index,,,,,,,,\n0,,,,,,,,\n"
                          "surface wave,max speed,min surface volume,max surface volume,min surface pitch,max surface "
                          "pitch,min skid volume,max skid volume,num skid samples\n"
                          "NOSOUND,125,0,0,0,0,0.5,0.88,3\n"
                          "skid wave,min slippage,max slippage,,,,,,\n"
                          "tireskid1,0.55,0.65\ntireskid2,0.65,0.75\ntireskid3,0.75,1\n"
                          "surface wave,max speed,min surface volume,max surface volume,min surface pitch,max surface "
                          "pitch,min skid volume,max skid volume,num skid samples\n"
                          "surfacegrass,25,0.35,0.75,0.85,1.25,0.5,0.72,1\n"
                          "skid wave,min slippage,max slippage\nsurfacegrassskid,0.25,1\n";

const char* kImpacts = "***\nBanger name,Num samples,ID\nWALL,3,0\n"
                       "sample name,min volume,max volume,min force,max force,frequency\n"
                       "soft,0.91,0.93,1000,8000,1\nmed,0.92,0.95,8000,20000,1\nhuge,0.94,1,20000,999999,1\n"
                       "***\nBanger name,Num samples,ID\nTREE,2,8\n"
                       "sample name,min volume,max volume,min force,max force,frequency\n"
                       "bush,0.9,1,0,999999,1\nmed,0.95,1,0,999999,1\n***\nBanger name,Num samples,ID\nENDOFDATA,0,0\n";

} // namespace

TEST(GameAudio, AngelVolumeIsDecibelLinear) {
    EXPECT_FLOAT_EQ(ageVolumeToGain(1.0f), 1.0f);
    EXPECT_NEAR(ageVolumeToGain(0.9f), 0.31623f, 1e-4f); // -10 dB
    EXPECT_NEAR(ageVolumeToGain(0.8f), 0.1f, 1e-5f);     // -20 dB
    EXPECT_LT(ageVolumeToGain(kSilentVolume), 2e-4f);     // -75 dB
    EXPECT_EQ(ageVolumeToGain(0.0f), 0.0f);
}

TEST(GameAudio, EngineCurvesFollowTheTable) {
    auto def = parseCarAudio(kDefaultCar);
    ASSERT_TRUE(def);
    EXPECT_EQ(def->horn, "RACECARHORN");
    EXPECT_EQ(def->clutch, "REVERSE");
    ASSERT_EQ(def->engine.size(), 2u);
    const auto& idle = def->engine[0];
    const auto& drive = def->engine[1];

    // Idle: below the fade-in start it is silent; full volume between the
    // fade ranges; fades back to its minimum volume at fade-out end; silent after.
    EXPECT_FALSE(EngineSound::evaluate(idle, 0.0f).audible);
    EXPECT_FLOAT_EQ(EngineSound::evaluate(idle, 1000.0f).volume, 0.96f);
    EXPECT_NEAR(EngineSound::evaluate(idle, 4750.0f).volume, 0.805f, 1e-4f); // halfway down the fade out
    EXPECT_NEAR(EngineSound::evaluate(idle, 7000.0f).volume, 0.65f, 1e-5f);
    EXPECT_FALSE(EngineSound::evaluate(idle, 7001.0f).audible);
    // Pitch rises linearly over the shift range.
    EXPECT_NEAR(EngineSound::evaluate(idle, 3500.5f).pitch, 0.8f + 0.84f * 0.5f, 1e-3f);
    // Drive sample fades in from 500 to 3500 RPM.
    EXPECT_FALSE(EngineSound::evaluate(drive, 400.0f).audible);
    EXPECT_NEAR(EngineSound::evaluate(drive, 2000.0f).volume, 0.82f, 1e-5f);
    // Monotonic pitch over a sweep.
    float prev = 0;
    for (float rpm = 500; rpm <= 12000; rpm += 250) {
        const float p = EngineSound::evaluate(drive, rpm).pitch;
        EXPECT_GE(p, prev);
        prev = p;
    }
}

TEST(GameAudio, SkidSelectionAndVolume) {
    auto table = parseSurfaceTable(kSurfaceDry);
    ASSERT_TRUE(table);
    EXPECT_FALSE(table->ice);
    ASSERT_EQ(table->surfaces.size(), 2u);
    const auto& road = table->surfaces[0];
    EXPECT_FALSE(road.hasSurfaceSound());
    EXPECT_EQ(SurfaceSounds::chooseSkid(road, 0.5f), -1);
    EXPECT_EQ(SurfaceSounds::chooseSkid(road, 0.6f), 0);
    EXPECT_EQ(SurfaceSounds::chooseSkid(road, 0.7f), 1);
    EXPECT_EQ(SurfaceSounds::chooseSkid(road, 0.9f), 2);
    EXPECT_EQ(SurfaceSounds::chooseSkid(road, 1.0f), 2);
    EXPECT_NEAR(SurfaceSounds::skidVolumeFor(road, 0.55f), 0.5f, 1e-5f);
    EXPECT_NEAR(SurfaceSounds::skidVolumeFor(road, 1.0f), 0.88f, 1e-5f);
    EXPECT_TRUE(table->surfaces[1].hasSurfaceSound());
}

TEST(GameAudio, SurfaceSoundsPlayAboveThresholds) {
    FakeData data({}, {"tireskid1", "tireskid2", "tireskid3", "surfacegrass", "surfacegrassskid"});
    SoundBank bank(data.vfs);
    Mixer mixer(48000);
    SurfaceSounds s;
    s.load(mixer, bank, *parseSurfaceTable(kSurfaceDry), Bus::Effects);

    CarAudioInputs in;
    for (auto& w : in.wheels)
        w.onGround = true;
    in.speed = 10.0f;
    for (auto& w : in.wheels)
        w.slip = 0.3f;
    s.update(in);
    EXPECT_FALSE(s.skidding()); // below the first skid threshold
    for (auto& w : in.wheels)
        w.slip = 0.8f;
    s.update(in);
    EXPECT_TRUE(s.skidding());
    EXPECT_EQ(s.skidSample(), 2);
    EXPECT_EQ(mixer.activeVoices(), 1);
    in.speed = 0.5f; // too slow to skid (MM1: speed > 1 m/s)
    s.update(in);
    EXPECT_FALSE(s.skidding());
    EXPECT_EQ(mixer.activeVoices(), 0);

    // Grass: rolling sound above 2 m/s with two wheels down, none while skidding.
    for (auto& w : in.wheels) {
        w.surface = 1;
        w.slip = 0.0f;
    }
    in.speed = 10.0f;
    s.update(in);
    EXPECT_EQ(s.currentSurface(), 1);
    EXPECT_EQ(mixer.activeVoices(), 1);
    in.wheels[0].onGround = in.wheels[1].onGround = in.wheels[2].onGround = false;
    s.update(in);
    EXPECT_EQ(mixer.activeVoices(), 0);
}

TEST(GameAudio, ImpactsTriggerByForce) {
    auto table = parseImpactTable(kImpacts);
    ASSERT_TRUE(table);
    ASSERT_EQ(table->bangers.size(), 2u);
    EXPECT_EQ(table->find(8)->samples.size(), 2u);
    EXPECT_NEAR(ImpactSounds::volumeFor(table->find(0)->samples[0], 1000), 0.91f, 1e-5f);
    EXPECT_NEAR(ImpactSounds::volumeFor(table->find(0)->samples[0], 8000), 0.93f, 1e-5f);

    FakeData data({}, {"soft", "med", "huge", "bush"});
    SoundBank bank(data.vfs);
    Mixer mixer(48000);
    ImpactSounds impacts;
    impacts.load(mixer, bank, *table, Bus::Effects);
    impacts.play({500.0f, 0, {}});
    EXPECT_EQ(mixer.activeVoices(), 0); // below the softest sample
    impacts.play({12000.0f, 0, {}});
    EXPECT_EQ(mixer.activeVoices(), 1);
    impacts.play({12000.0f, 0, {}}); // the same sample is still playing
    EXPECT_EQ(mixer.activeVoices(), 1);
    impacts.play({100.0f, 8, {}}); // tree: both samples at once
    EXPECT_EQ(mixer.activeVoices(), 3);
    EXPECT_EQ(impacts.lastPlayed(), 8);
}

TEST(GameAudio, SirenFollowsItsSteps) {
    const char* text = "Explosion sample,volume\nexplosion,0.95\nSample name,\nsirena,0.95\nplay time,next index\n2,1\n"
                       "Sample name,\nsirenb,0.95\nplay time,next index\n1,0\n";
    auto table = parseSirenTable(text);
    ASSERT_TRUE(table);
    ASSERT_EQ(table->samples.size(), 2u);
    FakeData data({}, {"sirena", "sirenb", "explosion"});
    SoundBank bank(data.vfs);
    Mixer mixer(48000);
    SirenPlayer siren;
    siren.load(mixer, bank, *table, Bus::Effects);
    siren.update(true, false, 0.0f);
    EXPECT_EQ(siren.currentSample(), 0);
    siren.update(true, false, 1.5f);
    EXPECT_EQ(siren.currentSample(), 0);
    siren.update(true, false, 1.0f);
    EXPECT_EQ(siren.currentSample(), 1);
    siren.update(true, false, 1.1f);
    EXPECT_EQ(siren.currentSample(), 0);
    siren.update(false, false, 0.1f);
    EXPECT_EQ(siren.currentSample(), -1);
    EXPECT_EQ(mixer.activeVoices(), 0);
    siren.update(true, true, 0.1f); // wrecked: explosion, siren off
    EXPECT_EQ(mixer.activeVoices(), 1);
}

TEST(GameAudio, AmbientEngineAndHornPatterns) {
    auto engine = parseAmbientEngine("Engine sample,engine volume,,\nENGINEFERRARI1,0.97,,\nmin speed,max speed,min pitch,max "
                                     "pitch\n0,15,0.27,1\n15,35,1,1.25\n35,500,1.25,1.5\n0,500,0.27,24.603\n");
    ASSERT_TRUE(engine);
    EXPECT_EQ(engine->bands.size(), 4u);
    EXPECT_NEAR(AmbientCarAudio::pitchFor(*engine, 0.0f), 0.27f, 1e-5f);
    EXPECT_NEAR(AmbientCarAudio::pitchFor(*engine, 7.5f), 0.635f, 1e-4f);
    EXPECT_NEAR(AmbientCarAudio::pitchFor(*engine, 25.0f), 1.125f, 1e-4f);
    auto horn = parseHorn("Horn sample,horn volume,horn pitch,min stuck horn impact force\nBUSHORN,0.97,0.9,5500\n"
                          "horn play duration,horn pause duration,,\n0.5,0.2,,\n1,0,,\n"
                          "horn play duration,horn pause duration,,\n0,0.15,,\n3,0,,\n");
    ASSERT_TRUE(horn);
    EXPECT_EQ(horn->patterns.size(), 2u);
    EXPECT_EQ(horn->patterns[0].beeps.size(), 2u);
    EXPECT_FLOAT_EQ(horn->stuckImpactForce, 5500.0f);
}

TEST(GameAudio, CreatureVoiceTriggersAfterTimeInRange) {
    auto def = parseCreatureVoice("Min speed,Max speed,min time in range,max time out of range\n0,20,5,0\n"
                                  "sample name,volume,,\nline1,0.98,,\nline2,0.98,,\n"
                                  "min impact force,,,\n5500,,,\nsample name,volume,play after seconds,\nouch,0.98,1,\n");
    ASSERT_TRUE(def);
    ASSERT_EQ(def->triggers.size(), 1u);
    EXPECT_EQ(def->triggers[0].lines.size(), 2u);
    EXPECT_EQ(def->impactLines.size(), 1u);
    FakeData data({}, {"line1", "line2", "ouch"});
    SoundBank bank(data.vfs);
    Mixer mixer(48000);
    CreatureVoice voice;
    voice.load(mixer, bank, *def);
    Emitter3D at;
    voice.update(10, 4.0f, at);
    EXPECT_FALSE(voice.speaking());
    voice.update(10, 1.5f, at);
    EXPECT_TRUE(voice.speaking());
}

TEST(GameAudio, AnnouncerLineNames) {
    SpeechLineSet pre{"pre", 11, 0};
    auto names = Announcer::lineNames("al1", pre);
    ASSERT_EQ(names.size(), 11u);
    EXPECT_EQ(names.front(), "al1pre01");
    EXPECT_EQ(names.back(), "al1pre11");
    SpeechLineSet cnr{"al1robrob", 4, 2};
    names = Announcer::lineNames("al1", cnr);
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "al1robrob03");
    auto t = parseSpeechTable("Name prefix/type header,end sufix value,sufix add value,num used\n"
                              "BLUETEAMHASGOLD header,,,\nAL1\\AL1ROBROB ,1,0,1\nROBGETLOOT header,,,\nAL1\\AL1COPS,4,0,4\n");
    ASSERT_TRUE(t);
    ASSERT_TRUE(t->find("robgetloot"));
    EXPECT_EQ(t->find("ROBGETLOOT")->front().prefix, "al1cops");
}

// --- Retail data ---------------------------------------------------------------

TEST(GameAudioRetail, EveryTableParses) {
    MM2_REQUIRE_GAME_DATA();
    const auto& v = *test::gameData();
    int cars = 0, impacts = 0, surfaces = 0, sirens = 0, ambient = 0, creatures = 0, speech = 0, horns = 0, engines = 0;
    for (const auto& e : v.listFiles()) {
        const std::string& p = e.path;
        if (!p.ends_with(".csv") || p.find("copy of") != std::string::npos || p.find(".wrk.") != std::string::npos)
            continue;
        auto bytes = v.readAll(p);
        ASSERT_TRUE(bytes) << p;
        const auto text = asText(*bytes);
        const auto name = p.substr(p.rfind('/') + 1);
        if (p.starts_with("aud/cardata/player/") || p.starts_with("aud/cardata/opponent/")) {
            if (name == "default_impacts.csv") {
                EXPECT_TRUE(parseImpactTable(text)) << p;
                ++impacts;
            } else if (name.starts_with("default_surface")) {
                auto t = parseSurfaceTable(text);
                EXPECT_TRUE(t) << p;
                if (t) {
                    EXPECT_GE(t->surfaces.size(), 5u) << p;
                }
                ++surfaces;
            } else if (name.find("policesiren") != std::string::npos) {
                EXPECT_TRUE(parseSirenTable(text)) << p;
                ++sirens;
            } else if (name == "suspensionaudio.csv") {
                EXPECT_TRUE(parseSuspension(text)) << p;
            } else if (name == "tirewobble.csv") {
                EXPECT_TRUE(parseTireWobble(text)) << p;
            } else if (name.starts_with("vp") || name == "default.csv") {
                auto def = parseCarAudio(text);
                EXPECT_TRUE(def) << p;
                ++cars;
            }
        } else if (p.starts_with("aud/cardata/ambient/")) {
            if (name.ends_with("_engine.csv")) {
                EXPECT_TRUE(parseAmbientEngine(text)) << p;
                ++engines;
            } else if (name.ends_with("_horn.csv")) {
                auto h = parseHorn(text);
                EXPECT_TRUE(h) << p;
                if (h) {
                    EXPECT_FALSE(h->patterns.empty()) << p;
                }
                ++horns;
            }
        } else if (p.starts_with("aud/ambient/") && !name.ends_with("container.csv")) {
            EXPECT_TRUE(parseAmbientSoundSet(name, text)) << p;
            ++ambient;
        } else if (p.starts_with("aud/creaturedata/") && !name.starts_with("num")) {
            EXPECT_TRUE(parseCreatureVoice(text)) << p;
            ++creatures;
        } else if (p.starts_with("aud/spchdata/") && p.find('/', 13) != std::string::npos &&
                   name.find("index") == std::string::npos) {
            EXPECT_TRUE(parseSpeechTable(text)) << p;
            ++speech;
        }
    }
    EXPECT_GE(cars, 40);
    EXPECT_EQ(impacts, 2);
    EXPECT_EQ(surfaces, 6);
    EXPECT_GE(sirens, 3);
    EXPECT_GE(engines, 8);
    EXPECT_GE(horns, 8);
    EXPECT_GE(ambient, 12);
    EXPECT_GE(creatures, 15);
    EXPECT_GE(speech, 400);
}

TEST(GameAudioRetail, EveryReferencedSampleExists) {
    MM2_REQUIRE_GAME_DATA();
    const auto& v = *test::gameData();
    SoundBank bank(v);
    std::vector<std::string> missing;
    auto need = [&](const std::string& wave, const std::string& where) {
        if (!wave.empty() && !str::iequals(wave, "NOSOUND") && bank.resolve(wave).empty())
            missing.push_back(wave + " (" + where + ")");
    };
    for (const char* folder : {"player", "opponent"}) {
        for (const auto& e : v.listFiles()) {
            const std::string prefix = std::string("aud/cardata/") + folder + "/vp";
            if (!e.path.starts_with(prefix) || !e.path.ends_with(".csv") || e.path.find("copy") != std::string::npos ||
                e.path.find(".wrk.") != std::string::npos)
                continue;
            auto def = parseCarAudio(asText(*v.readAll(e.path)));
            ASSERT_TRUE(def) << e.path;
            need(def->horn, e.path);
            need(def->clutch, e.path);
            for (const auto& s : def->engine)
                need(s.wave, e.path);
        }
        for (const char* t : {"default_impacts.csv"}) {
            auto table = parseImpactTable(asText(*v.readAll(std::string("aud/cardata/") + folder + "/" + t)));
            for (const auto& b : table->bangers)
                for (const auto& s : b.samples)
                    need(s.wave, t);
        }
    }
    // Reported, not failed: the shipped data may reference unused samples.
    for (const auto& m : missing)
        std::printf("missing sample: %s\n", m.c_str());
    EXPECT_LT(missing.size(), 8u);
}

TEST(GameAudioRetail, PlayerCarSweepSkidAndImpact) {
    MM2_REQUIRE_GAME_DATA();
    const auto& v = *test::gameData();
    SoundBank bank(v);
    Mixer mixer(48000);
    PlayerCarAudio car;
    std::string err;
    ASSERT_TRUE(car.load(v, bank, mixer, "vpbug", {}, &err)) << err;
    ASSERT_EQ(car.engine().sampleCount(), 4u); // VWIDLE, VWDRIVE, VWMID, VWHIGH

    CarAudioInputs in;
    for (auto& w : in.wheels)
        w.onGround = true;
    in.rpm = 0; // at rest: the idle sample keeps playing at idle RPM
    car.update(in, 0.016f);
    EXPECT_TRUE(car.engine().state(0).audible);
    int audibleSamples = 0;
    float lastPitch = 0;
    for (float rpm = 800; rpm <= 8500; rpm += 100) {
        in.rpm = rpm;
        car.update(in, 0.016f);
        audibleSamples = 0;
        for (std::size_t i = 0; i < 4; ++i)
            audibleSamples += car.engine().state(i).audible ? 1 : 0;
        EXPECT_GE(audibleSamples, 1) << rpm;
        EXPECT_GE(car.engine().state(1).pitch, lastPitch - 1e-4f);
        lastPitch = car.engine().state(1).pitch;
    }
    const int engineVoices = mixer.activeVoices();

    in.speed = 15.0f;
    for (auto& w : in.wheels)
        w.slip = 0.9f;
    car.update(in, 0.016f);
    EXPECT_TRUE(car.surfaces().skidding());
    EXPECT_EQ(mixer.activeVoices(), engineVoices + 1);

    in.impacts = {{25000.0f, 0, {}}};
    car.update(in, 0.016f);
    EXPECT_EQ(mixer.activeVoices(), engineVoices + 2);
}

TEST(GameAudioRetail, AnnouncerAndAmbience) {
    MM2_REQUIRE_GAME_DATA();
    const auto& v = *test::gameData();
    SoundBank bank(v);
    Mixer mixer(48000);
    for (const char* city : {"london", "sf"}) {
        Announcer a;
        ASSERT_TRUE(a.load(v, bank, mixer, city)) << city;
        EXPECT_GE(a.announcerCount(), 5);
        for (int i = 1; i <= a.announcerCount(); ++i) {
            if (!v.exists("aud/spchdata/" + std::string(city == std::string("sf") ? "as" : "al") + std::to_string(i) +
                          "/cruise.csv"))
                continue; // SF lists five announcers but ships four
            a.beginSession(i);
            a.stop();
            const auto line = a.playPreRace(AnnouncerMode::Blitz, "vpbug", 1, 0);
            EXPECT_FALSE(line.empty()) << city << " announcer " << i;
            a.stop();
            EXPECT_FALSE(a.playResults(AnnouncerMode::Circuit, RaceOutcome::Win, {}).empty()) << city << i;
            a.stop();
        }
        a.beginSession(1);
        EXPECT_FALSE(a.playCrashCourse(0, "PRERACE").empty()) << city;
        a.stop();
        EXPECT_FALSE(a.playCopsAndRobbers("ROBGETLOOT").empty()) << city;
        a.stop();

        CityAmbience amb;
        ASSERT_TRUE(amb.load(v, bank, mixer, city)) << city;
        EXPECT_GE(amb.setCount(), 2u);
    }
}

TEST(GameAudioRetail, OpponentAmbientAndCityEmitters) {
    MM2_REQUIRE_GAME_DATA();
    const auto& v = *test::gameData();
    SoundBank bank(v);
    Mixer mixer(48000);

    OpponentCarAudio cop;
    std::string err;
    ASSERT_TRUE(cop.load(v, bank, mixer, "vpcop", true, {}, &err)) << err;
    CarAudioInputs in;
    in.rpm = 3000;
    in.siren = true;
    in.transform.m3 = {10, 0, 0};
    cop.update(in, 0.02f, {0, 0, 0});
    const int near = mixer.activeVoices();
    EXPECT_GE(near, 2); // engine + siren
    cop.update(in, 0.02f, {1000, 0, 0}); // out of earshot
    EXPECT_EQ(mixer.activeVoices(), 0);

    AmbientCarAudio sedan; // tune name va_sedans_s, audio files va_sedan_s_*
    ASSERT_TRUE(sedan.load(v, bank, mixer, "va_sedans_s"));
    Mat34 at;
    at.m3 = {5, 0, 0};
    sedan.update(12.0f, at, {}, 0.02f, {0, 0, 0});
    EXPECT_EQ(mixer.activeVoices(), 1);
    sedan.honk(0);
    sedan.update(12.0f, at, {}, 0.02f, {0, 0, 0});
    EXPECT_EQ(mixer.activeVoices(), 2);
    sedan.stop();

    // London: the river emitters are audible next to the Thames.
    CityAmbience amb;
    ASSERT_TRUE(amb.load(v, bank, mixer, "london"));
    const auto* river = amb.set("londonriver");
    ASSERT_TRUE(river);
    ASSERT_FALSE(river->points.empty());
    amb.seed(1);
    for (int i = 0; i < 30 * 50; ++i) // 30 s next to the first river point
        amb.update(river->points.front(), 0.02f);
    EXPECT_GE(mixer.activeVoices(), 1);
}
