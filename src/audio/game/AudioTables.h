#pragma once

// Data tables for the in-game sound effects (aud/cardata, aud/ambient,
// aud/creaturedata, aud/spchdata in MM2AUD.AR). Column meanings follow MM2's
// own loaders: vehCarAudio::Load, vehEngineSampleWrapper::ParseCSVBuffer,
// vehSurfaceAudioData::ParseCSVBuffer, AudImpactData::ReadCSV,
// vehPoliceCarAudio::ReadSirenData, aiEngineAudio::ReadCSV,
// vehHornAudio::ReadCSV, Aud3DAmbientObject::ReadSoundData,
// AudCreatureAvoid / AudCreatureImpact::ParseCSVBuffer and
// mmRaceSpeech::LoadGroup. Parsers accept the files as shipped (trailing empty
// cells, stray "copy of" files, CR/LF).
//
// Volumes in every table are Angel volume units, see ageVolumeToGain().

#include "core/Math.h"
#include "vfs/Vfs.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mm2::audio::game {

// Angel volume (AudSoundBase::SetVolume) to linear gain. MM2's
// audSound::SetVolume passes (v - 1) * 10000 to IDirectSoundBuffer::SetVolume,
// so v is linear in decibels: 1 = 0 dB, 0.9 = -10 dB, 0.25 = -75 dB,
// 0 = -100 dB.
float ageVolumeToGain(float v);

// Angel pan (AudSoundBase::SetPan, -1 left .. 1 right) to the mixer's pan.
// audSound::SetPan passes pan * 10000 to IDirectSoundBuffer::SetPan, which
// attenuates the opposite channel by |pan| * 100 dB and leaves the near one
// at full level; the mixer attenuates the opposite channel linearly by |pan|.
// A DirectSound pan of 0.2, the most MM2's 3D sounds use, is -20 dB on the
// far channel.
float agePanToMixer(float pan);

// vehEngineSampleWrapper::UpdateRPM stops a sample whose table volume falls
// below 0.25 (-75 dB) and restarts it above.
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

// The "flags" column (2 on vpcentury, 4 on vpcop, 8 on vpsemi) and "Num Engine
// Samples" are read and discarded by vehCarAudio::Load: MM2 picks the semi,
// police and nitro sound classes from shared/vehtypes.csv
// (vehCarAudioContainer::RegisterTypes) and reads engine rows up to the end of
// the file (vehEngineAudio::Load). The flags are kept for tools only.
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
    std::string name; // WALL, LIGHT, SIGN, ...
    int id = 0;       // "ID" column
    std::vector<ImpactSampleDef> samples;
};
struct ImpactTable {
    std::vector<BangerSoundDef> bangers; // file order
    // By the "ID" column (tools; MM2 does not read the column).
    const BangerSoundDef* find(int id) const;
    // AudImpact::GetAudImpactDataPtr: bangers are indexed by their position in
    // the file; an index out of range (MM2 passes 1000 for "no banger data")
    // selects the first entry, WALL.
    const BangerSoundDef* byIndex(int index) const;
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
    // default_surfaceice.csv layout. MM2 never loads that file: vehCarAudio::Init
    // picks default_surfacewet in rain and default_surfacedry otherwise, snow
    // included. The layout is still parsed for tools.
    bool ice = false;
    int tunnelIndex = 0;  // entry used for every wheel while the tunnel echo is on
    std::vector<SurfaceSoundDef> surfaces; // indexed by surface sound index
    const SurfaceSoundDef* at(int index) const;
};
std::optional<SurfaceTable> parseSurfaceTable(std::string_view text, std::string* error = nullptr);

// --- Police sirens: aud/cardata/player/{london,sf}policesiren.csv ------------
// (opponent/policesiren.csv has the same layout but MM2 never loads it.)

struct SirenStep {
    float playTime = 1;
    int next = 0; // index of the sample that follows
};
struct SirenSampleDef {
    std::string wave;
    float volume = 0.95f;
    std::vector<SirenStep> steps; // (play time, next) pairs, used in turn
};
struct SirenTable {
    std::string explosion;
    // vehPoliceCarAudio::Load reads only the explosion sample's name: it
    // always plays at volume 1 (times distance attenuation).
    float explosionVolume = 0.95f;
    std::vector<SirenSampleDef> samples;
};
std::optional<SirenTable> parseSirenTable(std::string_view text, std::string* error = nullptr);

// --- Small player tables ---------------------------------------------------------

struct SuspensionDef { // player/suspensionaudio.csv (vehSurfaceAudio::LoadSuspension)
    std::string wave;
    float minVelocity = 2; // the wheels' average compression speed must reach |minVelocity|
    float maxVelocity = 3; // read but unused by MM2
    float minVolume = 0.85f, maxVolume = 0.9f;
    float volumeDivisor = 3; // volume = clamp(speed / divisor, min, max)
};
std::optional<SuspensionDef> parseSuspension(std::string_view text);

struct TireWobbleDef { // player/tirewobble.csv (vehSurfaceAudio::LoadTireWobble)
    std::string wave;
    float minVolume = 0.97f, maxVolume = 1;     // clamp of the damage fraction
    float minPitch = 0.75f, maxPitch = 1.5f;    // clamp of speed / pitchDivisor
    float pitchDivisor = 15;
};
std::optional<TireWobbleDef> parseTireWobble(std::string_view text);

struct SemiDef { // shared/semidata.csv (vehSemiCarAudio::Load)
    std::string reverse, airBlow;
    float reverseVolume = 0.87f, airBlowVolume = 0.9f;
};
std::optional<SemiDef> parseSemiData(std::string_view text);

struct VehicleTypes { // shared/vehtypes.csv (vehCarAudioContainer::RegisterTypes)
    std::vector<std::string> freight; // "Semi or bus": vehSemiCarAudio
    std::vector<std::string> police;  // "Police Car": vehPoliceCarAudio (vpcop, vpsemi, vpeagle)
    bool alwaysNitro = false;         // every other car would use vehNitroCarAudio
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
    // Speed bands (m/s) in file order. aiEngineAudio::CalculatePitch uses the
    // last band (0..500 in every retail file) while the car slows down and the
    // others while it holds or gains speed.
    std::vector<SpeedBand> bands;
};
std::optional<AmbientEngineDef> parseAmbientEngine(std::string_view text);

struct HornPattern {
    std::vector<std::pair<float, float>> beeps; // (play seconds, pause seconds)
};
struct HornDef {
    std::string wave;
    float volume = 0.97f, pitch = 1;
    // vehHornAudio::PlayImpact: a hit at least this hard plays the last pattern
    // (a long blast in every retail file) one time in four.
    float stuckImpactForce = 5500;
    std::vector<HornPattern> patterns;
};
std::optional<HornDef> parseHorn(std::string_view text);

// --- City ambience: aud/ambient/*.csv -----------------------------------------

// Aud3DAmbientObject::UpdateSoundData.
enum class AmbientSampleType : int {
    Loop = 0,       // positional loop
    RandomOnce = 1, // one-shot every [low, high] s at a random volume (0.75..1) and pan; not positional
    Interval = 2,   // positional one-shot every [low, high] s
    Triggered = 3,  // positional, played on request (bridges, subway, pedestrians)
};
struct AmbientSampleDef {
    std::string wave;
    float volume = 1;
    AmbientSampleType type = AmbientSampleType::Loop;
    float intervalLow = 0, intervalHigh = 0;
    bool active = true;
    float minSpeed = 0, maxSpeed = 999999; // of the object the set is attached to
    bool doppler = false;
};
struct AmbientSoundSet {
    std::string name;
    float minDistance = 0, maxDistance = 100;
    int priority = 12; // Aud3DObjectManager priority
    // Aud3DAmbientObject::Update: 0 everywhere, 1 only underground (while the
    // tunnel echo is on), 2 only above ground.
    int audibleArea = 0;
    std::vector<AmbientSampleDef> samples;
    std::vector<Vec3> points; // VECTORPOINTS: the set sounds from the one nearest the listener
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
// One AudCreatureAvoid block: lines a driver or pedestrian says when the AI
// reports a near miss (AudCreature::PlayAvoidance). The block is eligible
// unless its speed has been in [minSpeed, maxSpeed) for less than
// minTimeInRange and out of it for longer than maxTimeOutOfRange.
struct VoiceSpeedTrigger {
    float minSpeed = 0, maxSpeed = 0;
    float minTimeInRange = 0;
    float maxTimeOutOfRange = 0;
    std::vector<VoiceLine> lines;
};
struct CreatureVoiceDef {
    std::vector<VoiceSpeedTrigger> triggers;
    float minImpactForce = 0; // AudCreatureImpact
    std::vector<VoiceLine> impactLines;
};
std::optional<CreatureVoiceDef> parseCreatureVoice(std::string_view text);

// --- Announcer: aud/spchdata --------------------------------------------------

// One row under an "<EVENT> header": candidate lines "<prefix>NN", NN in
// (addValue, endValue], file name <announcer><prefix>NN (lower case).
struct SpeechLineSet {
    std::string prefix;
    int end = 1;
    int add = 0;
};
struct SpeechTable {
    // Event name (e.g. "PRERACE", "FINALCHECKPOINT", "RESULTSWIN") ->
    // the rows listed under it.
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
