#pragma once

// A loaded sample that can be played on demand, like MM2's AudSoundBase
// wrapping one DirectSound buffer: one instance plays at a time, volume and pan
// are in Angel units (dB-linear, see ageVolumeToGain / agePanToMixer) and pitch
// is a multiple of the sample's own rate (AudSoundBase::PlayLoop / PlayOnce /
// Stop / IsPlaying / SetVolume / SetFrequency / SetPan).

#include "audio/Mixer.h"
#include "audio/SoundBank.h"

#include <memory>
#include <string_view>

namespace mm2::audio::game {

class SoundSlot {
public:
    SoundSlot() = default;
    SoundSlot(const SoundSlot&) = delete;
    SoundSlot& operator=(const SoundSlot&) = delete;
    SoundSlot(SoundSlot&& other) noexcept;
    SoundSlot& operator=(SoundSlot&& other) noexcept;
    ~SoundSlot();

    // Loads `wave` from the bank. "NOSOUND" and empty names leave the slot empty.
    bool load(Mixer& mixer, SoundBank& bank, std::string_view wave, Bus bus, int priority = 0);
    bool valid() const { return m_buffer != nullptr; }
    const SoundBuffer* buffer() const { return m_buffer.get(); }

    // Starts looping if not already playing, then applies the parameters.
    // `emitter` makes the voice 3D (mixer-positioned) when non-null; MM2's own
    // sounds are 2D voices with a pan (setPan) instead.
    void playLoop(float volume, float pitch, const Emitter3D* emitter = nullptr);
    // Starts a one-shot from the beginning (restarting it if playing, as
    // DirectSound's Play on a playing buffer does after SetCurrentPosition(0)).
    void playOnce(float volume, float pitch = 1.0f, const Emitter3D* emitter = nullptr);
    void stop();
    bool playing() const;

    void setVolume(float volume);
    void setPitch(float pitch);
    // Angel pan, kept for later plays like a DirectSound buffer's pan.
    void setPan(float pan);
    void setEmitter(const Emitter3D& emitter);

    float volume() const { return m_volume; }
    float pitch() const { return m_pitch; }
    float pan() const { return m_pan; }

private:
    VoiceParams params(bool loop, const Emitter3D* emitter) const;

    Mixer* m_mixer = nullptr;
    std::shared_ptr<const SoundBuffer> m_buffer;
    Bus m_bus = Bus::Effects;
    int m_priority = 0;
    VoiceHandle m_voice = 0;
    bool m_looping = false;
    float m_volume = 0.0f;
    float m_pitch = 1.0f;
    float m_pan = 0.0f;
};

// audSound::SetPitch: the buffer frequency is rate * pitch, clamped to
// 100..100000 Hz (a negative product wraps to the top of the range in the
// original's unsigned comparison). Returns the resulting pitch multiplier.
float clampPitch(float pitch, int sampleRate);

} // namespace mm2::audio::game
