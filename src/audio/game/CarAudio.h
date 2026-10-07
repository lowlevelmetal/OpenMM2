#pragma once

// Car sounds: engine, gear/reverse, horn, siren, tyres on surfaces, skids,
// suspension, impacts. Structure ported from MM1's mmPlayerCarAudio,
// EngineAudio, mmSurfaceAudio, mmImpactAudio, mmPoliceCarAudio and
// mmOpponentCarAudio (Open1560, GPL-3.0: class layouts in code/midtown/mmcar,
// logic from the MASM in code/midtown/game.asm). MM2 replaced MM1's
// hard-coded tables with the CSV files in aud/cardata; how MM2 evaluates
// those files is inferred and marked as such (see docs/audio.md).

#include "audio/game/AudioTables.h"
#include "audio/game/SoundSlot.h"

#include <array>
#include <random>
#include <string>
#include <vector>

namespace mm2::audio::game {

// --- Inputs the game supplies every update ---------------------------------------

struct WheelAudioInput {
    bool onGround = false;
    // Tyre slip, max(|lateral|, |longitudinal|) slip percentage as the
    // wheel computes it (vehWheel Lat/LongSlipPercent; ~0 gripping, >= 1
    // fully sliding).
    float slip = 0.0f;
    // Surface sound index: 0 road, 1 water, 2 grass, 3 cobblestone,
    // 4 flagstone (order of default_surfacedry.csv; see surfaceSoundIndex()).
    int surface = 0;
    // Suspension compression speed (m/s, positive while compressing).
    float suspensionSpeed = 0.0f;
};

struct ImpactInput {
    // Impact strength: the normal impulse in N s, as mmCarSim::PlayImpactAudio
    // passes |normal . impulse| to mmImpactAudio::Play.
    float force = 0.0f;
    // dgBangerData AudioId of what was hit (0 = WALL: buildings, ground,
    // other cars).
    int audioId = 0;
    Vec3 position;
};

struct CarAudioInputs {
    float rpm = 0.0f;
    // vehEngine IdleRPM: the simulation may report 0 RPM at rest while the
    // engine is running; the engine sound never drops below this.
    float idleRpm = 800.0f;
    bool engineRunning = true;
    float throttle = 0.0f; // 0..1
    float brake = 0.0f;    // 0..1
    float speed = 0.0f;    // m/s
    int gear = 1;          // -1 reverse, 0 neutral, 1.. forward
    std::array<WheelAudioInput, 4> wheels{};
    std::vector<ImpactInput> impacts; // impacts since the previous update
    bool horn = false;                // horn button held
    bool siren = false;               // police: siren on
    bool wrecked = false;             // car destroyed (police explosion sound)
    float tireWobble = 0.0f;          // 0..1, damaged wheel wobble
    Mat34 transform;                  // car placement (3D cars)
    Vec3 velocity;
    bool inTunnel = false;
};

enum class SurfaceWeather { Dry, Wet, Snow };

// Maps a physics material (city/materials.mtl name and its "sound:" value) to
// the surface sound index used by the surface tables. Inferred: the tables
// list road, water, grass, cobblestone and flagstone in that order, while
// materials.mtl only uses sound values 0..2.
int surfaceSoundIndex(std::string_view materialName, int mtlSound);

// --- Engine -------------------------------------------------------------------------

// Multi-sample engine: every sample of the car's table is evaluated at the
// engine speed and the audible ones loop with their volume and pitch.
class EngineSound {
public:
    struct Evaluation {
        float volume = 0; // Angel volume units
        float pitch = 1;
        bool audible = false;
    };
    // Inferred evaluation of one aud/cardata row (MM1 used two samples with
    // volume = clamp(k * rpm) instead):
    //   fade  = 0 outside [fadeInStart, fadeOutEnd]; ramps 0->1 over the fade
    //           in range and 1->0 over the fade out range
    //   volume = minVolume + (maxVolume - minVolume) * fade
    //   pitch  = lerp(minPitch, maxPitch) by rpm over [pitchStart, pitchEnd]
    // A sample is audible while volume >= kSilentVolume (MM1 rule).
    static Evaluation evaluate(const EngineSampleDef& def, float rpm);

    void load(Mixer& mixer, SoundBank& bank, const std::vector<EngineSampleDef>& samples, Bus bus);
    void update(float rpm, const Emitter3D* emitter = nullptr, float volumeOffset = 0.0f);
    void stop();
    std::size_t sampleCount() const { return m_samples.size(); }
    const Evaluation& state(std::size_t i) const { return m_states[i]; }

private:
    std::vector<EngineSampleDef> m_defs;
    std::vector<SoundSlot> m_samples;
    std::vector<Evaluation> m_states;
};

// --- Surfaces and skids -----------------------------------------------------------------

// mmSurfaceAudio: rolling-surface loop and skid loop for one car.
class SurfaceSounds {
public:
    void load(Mixer& mixer, SoundBank& bank, const SurfaceTable& table, Bus bus);
    // Ported conditions (UpdateSurface / UpdateSkidClear): the surface
    // sound needs speed > 2 m/s and at least two wheels on the ground and is
    // silenced while skidding; skids need speed > 1 m/s. The surface keeps
    // its current type while either front wheel is still on it.
    void update(const CarAudioInputs& in, const Emitter3D* emitter = nullptr);
    void stop();

    int currentSurface() const { return m_surface; }
    bool skidding() const { return m_skidSample >= 0; }
    int skidSample() const { return m_skidSample; }
    float skidVolume() const { return m_skidVolume; }

    // Inferred skid evaluation: the skid sample is the one whose slippage
    // range contains the slip (ice tables: speed range); volume ramps from
    // minSkidVolume at the first sample's lower bound to maxSkidVolume at 1.
    static int chooseSkid(const SurfaceSoundDef& def, float value);
    static float skidVolumeFor(const SurfaceSoundDef& def, float slip);

private:
    struct Entry {
        SoundSlot surface;
        std::vector<SoundSlot> skids;
    };
    SurfaceTable m_table;
    bool m_loaded = false;
    std::vector<Entry> m_entries;
    int m_surface = 0;
    int m_skidEntry = -1;
    int m_skidSample = -1;
    float m_skidVolume = 0;
};

// --- Impacts ------------------------------------------------------------------------------

// mmImpactAudio: one-shot collision sounds chosen by what was hit and how hard.
class ImpactSounds {
public:
    void load(Mixer& mixer, SoundBank& bank, const ImpactTable& table, Bus bus);
    // Plays every sample of the banger whose force range contains `force`
    // and that is not already playing (as PlayWall checks IsPlaying).
    // Volume ramps from min to max volume across the force range (inferred;
    // MM1 used clamp(35 * force) between per-sample limits).
    void play(const ImpactInput& impact, const Emitter3D* emitter = nullptr);
    void stop();
    int lastPlayed() const { return m_lastPlayed; }

    static float volumeFor(const ImpactSampleDef& s, float force);

private:
    struct Banger {
        int id = 0;
        std::vector<ImpactSampleDef> defs;
        std::vector<SoundSlot> slots;
    };
    std::vector<Banger> m_bangers;
    int m_lastPlayed = 0;
};

// --- Sirens ---------------------------------------------------------------------------------

// Police siren as a little state machine from the siren tables: play sample
// i for its play time, then switch to its "next index" (opponent tables list
// several (time, next) choices per sample; one is picked at random). Replaces
// MM1's hard-coded FluctuateSlowSiren / FluctuateFastSiren (inferred).
class SirenPlayer {
public:
    void load(Mixer& mixer, SoundBank& bank, const SirenTable& table, Bus bus);
    void update(bool on, bool wrecked, float dt, const Emitter3D* emitter = nullptr);
    void stop();
    int currentSample() const { return m_current; }

private:
    SirenTable m_table;
    std::vector<SoundSlot> m_slots;
    SoundSlot m_explosion;
    int m_current = -1;
    int m_next = 0;
    float m_timer = 0;
    bool m_wasWrecked = false;
    std::mt19937 m_rng{12345};
};

// --- Player car -----------------------------------------------------------------------------

struct CarAudioOptions {
    SurfaceWeather weather = SurfaceWeather::Dry;
    std::string city = "london"; // police siren variant
};

// mmPlayerCarAudio: the player's car, heard from inside/behind (2D).
class PlayerCarAudio {
public:
    // Loads aud/cardata/player/<car>.csv (default.csv when missing), the
    // impact, surface, suspension and tyre wobble tables and, for freight
    // and police vehicles (shared/vehtypes.csv), their extra sounds.
    bool load(const vfs::Vfs& vfs, SoundBank& bank, Mixer& mixer, std::string_view car,
              const CarAudioOptions& options = {}, std::string* error = nullptr);
    void update(const CarAudioInputs& in, float dt);
    void stop();

    const CarAudioDef& definition() const { return m_def; }
    const EngineSound& engine() const { return m_engine; }
    const SurfaceSounds& surfaces() const { return m_surfaces; }
    const ImpactSounds& impacts() const { return m_impacts; }

private:
    CarAudioDef m_def;
    EngineSound m_engine;
    SurfaceSounds m_surfaces;
    ImpactSounds m_impacts;
    SoundSlot m_horn, m_clutch, m_reverseBeep, m_airBlow, m_suspension, m_wobble;
    std::optional<SuspensionDef> m_suspensionDef;
    std::optional<TireWobbleDef> m_wobbleDef;
    std::optional<SemiDef> m_semi;
    std::optional<SirenPlayer> m_siren;
    int m_prevGearState = 1;
    bool m_airBlown = false;
    bool m_prevHorn = false;
    bool m_hornLatched = false;
};

// --- Opponents / police / network cars (3D) ---------------------------------------------------------

// mmOpponentCarAudio: another simulated car, positioned in 3D. Cars farther
// than `maxDistance` from the listener are silenced (aiAudioManager only
// gave sounds to the nearest cars).
class OpponentCarAudio {
public:
    bool load(const vfs::Vfs& vfs, SoundBank& bank, Mixer& mixer, std::string_view car, bool police,
              const CarAudioOptions& options = {}, std::string* error = nullptr);
    void update(const CarAudioInputs& in, float dt, const Vec3& listener);
    void stop();
    float maxDistance = 150.0f;
    // Distance model for the car's voices (DS3D min/max distance; inferred).
    float minDistance = 6.0f;

private:
    CarAudioDef m_def;
    EngineSound m_engine;
    SurfaceSounds m_surfaces;
    ImpactSounds m_impacts;
    SoundSlot m_horn;
    std::optional<SirenPlayer> m_siren;
    bool m_audible = false;
};

// --- Ambient traffic (3D) -------------------------------------------------------------------------

// Ambient traffic: one engine loop pitched by speed band, honking patterns,
// a stuck horn after a hard hit (aiVehicleAmbient / aiAudioManager roles).
class AmbientCarAudio {
public:
    // `type` is the ambient vehicle name ("va_sedans_s"); files are looked up
    // as aud/cardata/ambient/<type>_engine.csv and _horn.csv with a trailing
    // plural "s" dropped from the model part when needed (va_sedans_s ->
    // va_sedan_s), else default_engine.csv / default_horn.csv.
    bool load(const vfs::Vfs& vfs, SoundBank& bank, Mixer& mixer, std::string_view type);
    void update(float speed, const Mat34& transform, const Vec3& velocity, float dt, const Vec3& listener);
    // Starts one of the horn patterns (random when < 0).
    void honk(int pattern = -1);
    void impact(float force);
    void stop();

    static float pitchFor(const AmbientEngineDef& def, float speed);

    float maxDistance = 120.0f;
    float minDistance = 5.0f;

private:
    AmbientEngineDef m_engineDef;
    std::optional<HornDef> m_hornDef;
    SoundSlot m_engine, m_horn;
    int m_pattern = -1;
    std::size_t m_beep = 0;
    float m_beepTimer = 0;
    bool m_beepOn = false;
    bool m_stuck = false;
    std::mt19937 m_rng{4242};
};

// Finds a file in aud/cardata/<folder>/ for a car, falling back to default.csv.
std::string carAudioPath(const vfs::Vfs& vfs, std::string_view folder, std::string_view car);

} // namespace mm2::audio::game
