#pragma once

// Data tables for the in-game sound effects (aud/cardata, aud/ambient,
// aud/creaturedata, aud/spchdata in MM2AUD.AR). MM1 hard-coded most of these
// values in its audio classes (Open1560 game.asm: EngineAudio,
// mmPlayerCarAudio::Init, SetClearSurfaceAudioInfos, mmImpactAudio::Set*Range,
// mmRainAudio); MM2 moved them into these CSV files. Parsers accept the files
// as shipped (trailing empty cells, stray "copy of" files, CR/LF).
//
// Volumes in every table are Angel volume units, see ageVolumeToGain().

#include "core/Math.h"
#include "vfs/Vfs.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mm2::audio::game {

// Angel volume (AudSound::SetVolume) to linear gain. SoundObj::SetVolume
// passes (v * 10000 - 10000) to IDirectSoundBuffer::SetVolume, i.e. v is
// linear in decibels: 1 = 0 dB, 0.9 = -10 dB, 0.25 = -75 dB, 0 = -100 dB.
// Verified in Open1560 game.asm (?SetVolume@SoundObj@@QAEXM@Z).
float ageVolumeToGain(float v);

// MM1 EngineAudio::UpdateRPM stops a sample whose volume falls below 0.25
// (-75 dB) and restarts it above (game.asm flt_61CB2C).
inline constexpr float kSilentVolume = 0.25f;

// --- Cars: aud/cardata/{player,opponent}/<car>.csv -----------------------------

struct EngineSampleDef {
    std::string wave;
    float minVolume = 0, maxVolume = 0;
    float fadeInStartRpm = 0, fadeInEndRpm = 0;
    float fadeOutStartRpm = 0, fadeOutEndRpm = 0;
    float minPitch = 1, maxPitch = 1;
    float pitchStartRpm = 0, pitchEndRpm = 1;
};

// Car flags in the "flags" column (meaning inferred from which cars set
// them): 2 freight (vpcentury: semi reverse/air brake sounds), 4 police
// (vpcop: siren), 8 the horn is a siren loop (vpsemi, the fire truck).
namespace CarAudioFlag {
inline constexpr unsigned Freight = 2;
inline constexpr unsigned Police = 4;
inline constexpr unsigned SirenHorn = 8;
} // namespace CarAudioFlag

struct CarAudioDef {
    std::string horn;
    float hornVolume = 0.95f;
    unsigned flags = 0;
    std::string clutch; // "REVERSE" for cars, "TRUCKGEARSHIFT" for trucks
    float clutchVolume = 0.9f;
    std::vector<EngineSampleDef> engine;
};
std::optional<CarAudioDef> parseCarAudio(std::string_view text, std::string* error = nullptr);

// --- Impacts: aud/cardata/{player,opponent}/default_impacts.csv -------------

struct ImpactSampleDef {
    std::string wave;
    float minVolume = 0, maxVolume = 0;
    float minForce = 0, maxForce = 0;
    float frequency = 1; // playback rate multiplier
};
struct BangerSoundDef {
    std::string name; // WALL, LIGHT, SIGN, ... (dgBangerData AudioId = id)
    int id = 0;
    std::vector<ImpactSampleDef> samples;
};
struct ImpactTable {
    std::vector<BangerSoundDef> bangers;
    const BangerSoundDef* find(int id) const;
};
std::optional<ImpactTable> parseImpactTable(std::string_view text, std::string* error = nullptr);

// --- Surfaces: aud/cardata/{player,opponent}/default_surface{dry,wet,ice}.csv

struct SkidSampleDef {
    std::string wave;
    float min = 0, max = 1; // slippage range (dry/wet) or speed range (ice)
};
struct SurfaceSoundDef {
    std::string wave; // "NOSOUND" = none
    // dry/wet tables
    float maxSpeed = 125;
    float minVolume = 0, maxVolume = 0;
    float minPitch = 1, maxPitch = 1;
    float minSkidVolume = 0, maxSkidVolume = 0;
    // ice table: divisors instead of a max speed
    float volumeDivisor = 0, pitchDivisor = 0, skidVolumeDivisor = 0;
    bool forTunnels = false;
    std::vector<SkidSampleDef> skids;
    bool hasSurfaceSound() const;
};
struct SurfaceTable {
    bool ice = false;     // default_surfaceice.csv layout (speed-based skids)
    int tunnelIndex = 0;  // entry used for every wheel inside tunnels
    std::vector<SurfaceSoundDef> surfaces; // indexed by surface sound index
    const SurfaceSoundDef* at(int index) const;
};
std::optional<SurfaceTable> parseSurfaceTable(std::string_view text, std::string* error = nullptr);

// --- Police sirens: aud/cardata/player/{london,sf}policesiren.csv and
// opponent/policesiren.csv ----------------------------------------------------

struct SirenStep {
    float playTime = 1;
    int next = 0; // index of the sample that follows
};
struct SirenSampleDef {
    std::string wave;
    float volume = 0.95f;
    std::vector<SirenStep> steps; // one or more (play time, next) choices
};
struct SirenTable {
    std::string explosion;
    float explosionVolume = 0.95f;
    std::vector<SirenSampleDef> samples;
};
std::optional<SirenTable> parseSirenTable(std::string_view text, std::string* error = nullptr);

// --- Small player tables ---------------------------------------------------------

struct SuspensionDef { // player/suspensionaudio.csv
    std::string wave;
    float minVelocity = 2, maxVelocity = 3;
    float minVolume = 0.85f, maxVolume = 0.9f;
    float volumeDivisor = 3;
};
std::optional<SuspensionDef> parseSuspension(std::string_view text);

struct TireWobbleDef { // player/tirewobble.csv
    std::string wave;
    float minVolume = 0.97f, maxVolume = 1;
    float minPitch = 0.75f, maxPitch = 1.5f;
    float pitchDivisor = 15;
};
std::optional<TireWobbleDef> parseTireWobble(std::string_view text);

struct SemiDef { // shared/semidata.csv
    std::string reverse, airBlow;
    float reverseVolume = 0.87f, airBlowVolume = 0.9f;
};
std::optional<SemiDef> parseSemiData(std::string_view text);

struct VehicleTypes { // shared/vehtypes.csv
    std::vector<std::string> freight; // "Semi or bus"
    std::vector<std::string> police;  // "Police Car"
    bool alwaysNitro = false;
    bool isFreight(std::string_view car) const;
    bool isPolice(std::string_view car) const;
};
VehicleTypes parseVehicleTypes(std::string_view text);

// --- Ambient traffic: aud/cardata/ambient/<type>_engine.csv and _horn.csv ----

struct SpeedBand {
    float minSpeed = 0, maxSpeed = 0;
    float minPitch = 1, maxPitch = 1;
};
struct AmbientEngineDef {
    std::string wave;
    float volume = 0.97f;
    // Speed bands (m/s, inferred) in file order. Every retail file ends with
    // a 0..500 band whose purpose is unknown; it only applies when no
    // earlier band contains the speed.
    std::vector<SpeedBand> bands;
};
std::optional<AmbientEngineDef> parseAmbientEngine(std::string_view text);

struct HornPattern {
    std::vector<std::pair<float, float>> beeps; // (play seconds, pause seconds)
};
struct HornDef {
    std::string wave;
    float volume = 0.97f, pitch = 1;
    float stuckImpactForce = 5500; // impacts above this jam the horn (inferred)
    std::vector<HornPattern> patterns;
};
std::optional<HornDef> parseHorn(std::string_view text);

// --- City ambience: aud/ambient/*.csv -----------------------------------------

enum class AmbientSampleType : int {
    Loop = 0,       // continuous loop at the emitter
    RandomOnce = 1, // one-shot at random intervals [low, high] seconds
    Interval = 2,   // one-shot at random intervals, from the nearest point (inferred)
    Triggered = 3,  // played on request (pedestrian screams)
};
struct AmbientSampleDef {
    std::string wave;
    float volume = 1;
    AmbientSampleType type = AmbientSampleType::Loop;
    float intervalLow = 0, intervalHigh = 0;
    bool active = true;
    float minSpeed = 0, maxSpeed = 999999;
    bool doppler = false;
};
struct AmbientSoundSet {
    std::string name;
    float minDistance = 0, maxDistance = 100;
    int priority = 12;
    int audibleArea = 0; // 0 nearest point, 1 every point, 2 along the polyline (inferred)
    std::vector<AmbientSampleDef> samples;
    std::vector<Vec3> points; // VECTORPOINTS
};
std::optional<AmbientSoundSet> parseAmbientSoundSet(std::string_view name, std::string_view text,
                                                    std::string* error = nullptr);
// <city>ambientcontainer.csv: names of the sets loaded for a city.
std::vector<std::string> parseAmbientContainer(std::string_view text);

// --- Creature voices: aud/creaturedata/*.csv ---------------------------------

struct VoiceLine {
    std::string wave;
    float volume = 0.98f;
    float delay = 0; // impact lines: seconds after the impact
};
struct VoiceSpeedTrigger {
    float minSpeed = 0, maxSpeed = 0;
    float minTimeInRange = 0;  // seconds the speed must stay in range
    float maxTimeOutOfRange = 0;
    std::vector<VoiceLine> lines;
};
struct CreatureVoiceDef {
    std::vector<VoiceSpeedTrigger> triggers;
    float minImpactForce = 0;
    std::vector<VoiceLine> impactLines;
};
std::optional<CreatureVoiceDef> parseCreatureVoice(std::string_view text);

// --- Announcer: aud/spchdata --------------------------------------------------

// One "<EVENT> header" group: candidate lines "<prefix>NN", NN in
// (addValue, endValue], file name <announcer><prefix>NN (lower case).
struct SpeechLineSet {
    std::string prefix;
    int end = 1;
    int add = 0;
};
struct SpeechTable {
    // Event name (e.g. "PRERACE", "FINALCHECKPOINT", "RESULTSWIN") ->
    // the line sets listed under it.
    std::vector<std::pair<std::string, std::vector<SpeechLineSet>>> events;
    const std::vector<SpeechLineSet>* find(std::string_view event) const;
};
std::optional<SpeechTable> parseSpeechTable(std::string_view text);

// <city>.csv: "Num announcers" and "prefix" (AL -> al1..al6).
struct AnnouncerList {
    int count = 0;
    std::string prefix;
};
std::optional<AnnouncerList> parseAnnouncerList(std::string_view text);

// Reads a text file from the mounted game data (nullopt if missing).
std::optional<std::string> readText(const vfs::Vfs& vfs, std::string_view path);

} // namespace mm2::audio::game
