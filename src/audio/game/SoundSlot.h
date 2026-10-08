#pragma once

// A loaded sample that can be played on demand, like MM2's AudSoundBase
// wrapping one DirectSound buffer: one instance plays at a time, volume and pan
// are in Angel units (dB-linear, see ageVolumeToGain / agePanToMixer) and pitch
// is a multiple of the sample's own rate (AudSoundBase::PlayLoop / PlayOnce /
// Stop / IsPlaying / SetVolume / SetFrequency / SetPan). The setters apply the
// clamps of the audObject layer underneath (audObject::SetVolume / SetPitch /
// SetPan); see the methods.

#include "audio/EchoEffect.h"
#include "audio/Mixer.h"
#include "audio/SoundBank.h"

#include <memory>
#include <string_view>

namespace mm2::audio::game {

class SoundSlot {
public:
    // The "leave as is" argument of AudSoundBase::PlayLoop / PlayOnce: a
    // volume or pitch of -1 or less is not applied.
    static constexpr float kKeep = -1.0f;

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

    // AudSoundBase::PlayLoop: applies a volume / pitch above -1 straight to the
    // buffer (audSound::SetVolume / SetPitch: no audObject clamps, and a
    // DirectSound volume outside -100..0 dB is rejected), then starts looping
    // unless the buffer is already playing. MM2's callers pass -1 for both and
    // set volume and frequency through setVolume / setPitch first.
    // `emitter` makes the voice 3D (mixer-positioned) when non-null; MM2's own
    // sounds are 2D voices with a pan (setPan) instead.
    // With the echo on, a play is queued for the echo (EchoEffect::QueuePlay).
    void playLoop(float volume = kKeep, float pitch = kKeep, const Emitter3D* emitter = nullptr);
    // AudSoundBase::PlayOnce: applies a volume / pitch above -1 through the
    // clamping setters, then plays from the start unless the buffer is already
    // playing (audSound::Play leaves a playing buffer alone; Stop rewinds).
    void playOnce(float volume = kKeep, float pitch = kKeep, const Emitter3D* emitter = nullptr);
    // AudSoundBase::Stop (rewinds); with the echo on, a stop is queued for it.
    void stop();
    bool playing() const;

    // audObject::SetVolume: the mixer multiplies in the bus master volume and
    // clamps the product to 0..1. With the echo on, master * volume is queued
    // for it (AudSoundBase::SetVolume).
    void setVolume(float volume);
    // AudSoundBase::SetFrequency / audObject::SetPitch: the multiplier is
    // clamped to 0..2, then the rate to 100..100000 Hz (clampPitch). With the
    // echo on, the resulting frequency is queued for it.
    void setPitch(float pitch);
    // audObject::SetPan: clamped to -1..1 and kept for later plays like a
    // DirectSound buffer's pan. With the echo on, its pan follows at once
    // (EchoEffect::CalculatePan, from the unclamped value).
    void setPan(float pan);

    // The tunnel echo (AudSoundBase::SetEffect(1) / SetEchoEffect): the
    // first call makes the sample's EchoEffect, a delayed duplicate, and every
    // call turns it on. A loaded sample is needed. The EchoEffect stays until
    // the slot is reloaded or destroyed, keeping its delay and its last
    // volume, pan and position when turned off and on again.
    void enableEcho();
    // DisableEffect(1) / DisableEchoEffect: off; the duplicate stops.
    void disableEcho();
    // SetDelayTime: only the first call after the EchoEffect is made sets the
    // delay; each call queues a play when the sample is playing a loop.
    void setEchoDelay(float seconds);
    // SetEchoAttenuation (the EchoEffect's 0.96 until set).
    void setEchoAttenuation(float attenuation);
    // SetEchoFrequency: the echo's frequency becomes the sample rate times
    // `factor` at once (only while the echo is on).
    void setEchoFrequency(float factor);
    // AudSoundBase::Update / UpdateEcho: ages the echo's queued commands by
    // the frame time and applies the ripe ones (only while the echo is on).
    void updateEcho(float dt);
    bool echoOn() const { return m_echoOn; }
    const EchoEffect* echo() const { return m_echo.get(); }
    void setEmitter(const Emitter3D& emitter);

    float volume() const { return m_volume; }
    float pitch() const { return m_pitch; }
    float pan() const { return m_pan; }

private:
    VoiceParams params(bool loop, const Emitter3D* emitter) const;
    void start(bool loop, const Emitter3D* emitter);
    void release();
    std::uint64_t currentPosition() const;

    Mixer* m_mixer = nullptr;
    std::shared_ptr<const SoundBuffer> m_buffer;
    Bus m_bus = Bus::Effects;
    int m_priority = 0;
    VoiceHandle m_voice = 0;
    bool m_looping = false;
    // Aud3DSampleWrapper::Load leaves every game sample at volume 0 until set.
    float m_volume = 0.0f;
    float m_pitch = 1.0f;
    float m_pan = 0.0f;
    std::unique_ptr<EchoEffect> m_echo;
    bool m_echoOn = false; // AudSoundBase +0x3c
};

// audSound::SetPitch: the buffer frequency is rate * pitch, clamped to
// 100..100000 Hz (a negative product wraps to the top of the range in the
// original's unsigned comparison). Returns the resulting pitch multiplier.
float clampPitch(float pitch, int sampleRate);

} // namespace mm2::audio::game
