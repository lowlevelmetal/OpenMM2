#pragma once

#include "audio/Wav.h"
#include "core/Math.h"

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace mm2::audio {

// Mix groups with independent volume. MM2 has two levels: SOUND FX (every
// wave sound, AudManager::AssignWaveVolume: effects, engines, the city's wave
// ambience, rain and commentary) and MUSIC (the DirectMusic buffer,
// DMusicWaveBuffer::SetVolume: the soundtrack, or the city's ambience segment
// when music is off). OpenMM2 keeps separate buses so the options can drive
// them: Effects, Engine and Voice carry wave sounds, Ambient the DirectMusic
// ambience segment and Music the soundtrack.
enum class Bus : std::uint8_t { Effects, Engine, Ambient, Voice, Music, Count };

// 3D emitter parameters (OpenMM2 only: MM2 computes its own volume, pan and
// frequency for positioned sounds and never uses DirectSound3D for them). The
// model follows DirectSound3D: full volume inside minDistance,
// inverse-distance rolloff beyond it, no further attenuation past maxDistance.
struct Emitter3D {
    Vec3 position;
    Vec3 velocity;
    float minDistance = 1.0f;      // DS3D_DEFAULTMINDISTANCE
    float maxDistance = 1.0e9f;    // DS3D_DEFAULTMAXDISTANCE
};

struct VoiceParams {
    // Linear gain, or with `angel` an Angel volume (0..1, linear in decibels)
    // that the bus's MM2 master volume scales before it is turned into a gain
    // (audObject::SetVolume).
    float volume = 1.0f;
    bool angel = false;
    float pan = 0.0f;    // -1 (left) .. 1 (right); ignored for 3D voices
    float pitch = 1.0f;  // playback-rate multiplier
    bool loop = false;
    bool paused = false;
    Bus bus = Bus::Effects;
    std::optional<Emitter3D> spatial;
    // Higher survives voice stealing; among equals the oldest voice goes, as
    // audManager::MoveToActive stops the first (oldest) active sound of no
    // higher priority. MM2's game sounds all share one priority.
    int priority = 0;
};

// Pull-based source for streamed audio (music, video soundtracks). Called on
// the audio thread from Mixer::mix(); must fill `frames` interleaved stereo
// float frames at the mixer's sample rate and must not block.
class StreamSource {
public:
    virtual ~StreamSource() = default;
    virtual void render(float* stereo, int frames) = 0;
};

// Opaque voice id; 0 is never a valid handle. Stale handles are ignored.
using VoiceHandle = std::uint32_t;

// Software mixer producing interleaved stereo float frames. All methods are
// thread-safe; mix() is meant to be called from the audio device callback.
class Mixer {
public:
    // AudManager::Init lets 32 wave sounds play at once (SetMaxConcurrent);
    // the SOUND QUALITY option's channel count goes to AudManager::
    // SetNumChannels, which does nothing.
    static constexpr int kMaxVoices = 32;
    explicit Mixer(int sampleRate = 48000, int maxVoices = kMaxVoices);

    int sampleRate() const { return m_rate; }

    VoiceHandle play(std::shared_ptr<const SoundBuffer> sound, const VoiceParams& params);
    void stop(VoiceHandle voice);
    void stopAll();
    bool isPlaying(VoiceHandle voice) const;

    void setVolume(VoiceHandle voice, float volume);
    void setPitch(VoiceHandle voice, float pitch);
    void setPan(VoiceHandle voice, float pan);
    void setPaused(VoiceHandle voice, bool paused);
    void setEmitter(VoiceHandle voice, const Emitter3D& emitter);

    // Listener basis as in the Angel engine (m0 right, m1 up, m2 back, m3 position).
    void setListener(const Mat34& transform, const Vec3& velocity = {});
    // An option slider (0..1) for the bus. Like MM2, it becomes the master
    // volume ageMasterVolume(slider): Angel voices multiply it into their
    // volume (then clamp to 0..1), linear voices and streams take the gain of
    // that master as an Angel volume (DMusicWaveBuffer::SetVolume).
    void setBusVolume(Bus bus, float volume);
    float busVolume(Bus bus) const;
    // STEREO FX (AudManagerBase::SetStereoFlag): with mono every voice plays
    // centred. MM2 skips its SetPan calls while IsStereo is false; its sounds
    // start centred, so the result is the same.
    void setStereo(bool stereo);
    bool stereo() const;
    void setMasterVolume(float volume);
    // Speaker balance, -1 (left only) .. 1 (right only); the original's
    // Balance slider.
    void setBalance(float balance);
    // DS3D global factors: 1.0 = real-world doppler / rolloff.
    void setDopplerFactor(float f);
    void setRolloffFactor(float f);
    void pauseAll(bool paused); // e.g. while the game is paused or minimized

    int activeVoices() const;

    // Adds a streamed source on `bus` (e.g. Bus::Music). Returns an id for
    // removeStream(). The mixer keeps the source alive until it is removed.
    int addStream(std::shared_ptr<StreamSource> source, Bus bus, float volume = 1.0f);
    void setStreamVolume(int id, float volume);
    void removeStream(int id);

    // Renders `frames` stereo frames into `out` (2 * frames floats), replacing its contents.
    void mix(float* out, int frames);

private:
    struct Voice {
        std::shared_ptr<const SoundBuffer> sound;
        VoiceParams params;
        std::uint64_t position = 0; // source frames in 32.32 fixed point
        float gainL = 0, gainR = 0; // last applied gains (for ramping)
        bool started = false;
        std::uint32_t generation = 0;
        std::uint64_t serial = 0; // start order, for stealing
        bool active = false;
    };

    Voice* lookup(VoiceHandle h);
    const Voice* lookup(VoiceHandle h) const;
    void computeTargets(const Voice& v, float& gl, float& gr, double& rate) const;
    std::size_t pickSlot();

    mutable std::mutex m_mutex;
    int m_rate;
    std::vector<Voice> m_voices;
    struct Stream {
        int id;
        std::shared_ptr<StreamSource> source;
        Bus bus;
        float volume;
    };
    std::vector<Stream> m_streams;
    int m_nextStreamId = 1;
    std::vector<float> m_streamScratch;
    std::uint64_t m_serial = 0;
    Mat34 m_listener;
    Vec3 m_listenerVel;
    std::array<float, static_cast<std::size_t>(Bus::Count)> m_bus{};       // slider values
    std::array<float, static_cast<std::size_t>(Bus::Count)> m_busMaster{}; // ageMasterVolume(slider)
    std::array<float, static_cast<std::size_t>(Bus::Count)> m_busGain{};   // ageVolumeToGain(master)
    bool m_stereo = true;
    float m_master = 1.0f;
    float m_balance = 0.0f;
    float m_doppler = 1.0f;
    float m_rolloff = 1.0f;
    bool m_paused = false;
};

} // namespace mm2::audio
