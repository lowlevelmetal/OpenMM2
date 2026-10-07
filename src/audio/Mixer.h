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

// Mix groups with independent volume, matching the original's separate
// sound-effect, engine, ambient, commentary and music levels.
enum class Bus : std::uint8_t { Effects, Engine, Ambient, Voice, Music, Count };

// 3D emitter parameters. The model follows DirectSound3D, which the original
// used: full volume inside minDistance, inverse-distance rolloff beyond it,
// no further attenuation past maxDistance.
struct Emitter3D {
    Vec3 position;
    Vec3 velocity;
    float minDistance = 1.0f;      // DS3D_DEFAULTMINDISTANCE
    float maxDistance = 1.0e9f;    // DS3D_DEFAULTMAXDISTANCE
};

struct VoiceParams {
    float volume = 1.0f; // linear gain
    float pan = 0.0f;    // -1 (left) .. 1 (right); ignored for 3D voices
    float pitch = 1.0f;  // playback-rate multiplier
    bool loop = false;
    bool paused = false;
    Bus bus = Bus::Effects;
    std::optional<Emitter3D> spatial;
    int priority = 0; // higher survives voice stealing
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
    explicit Mixer(int sampleRate = 48000, int maxVoices = 96);

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
    void setBusVolume(Bus bus, float volume);
    float busVolume(Bus bus) const;
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
    std::array<float, static_cast<std::size_t>(Bus::Count)> m_bus{};
    float m_master = 1.0f;
    float m_balance = 0.0f;
    float m_doppler = 1.0f;
    float m_rolloff = 1.0f;
    bool m_paused = false;
};

} // namespace mm2::audio
