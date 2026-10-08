#pragma once

// The Angel audio library's echo (EchoEffect, made by audFX::EnablePCEcho):
// a duplicate of a sound's buffer that repeats what the sound does a little
// later and a little quieter. MM2 uses it for the tunnel echo: while the
// camera is underground every positioned sound gets one (see
// AudSoundBase::SetEchoEffect and the EchoOn methods of the game's sound
// objects).
//
// The original's play, stop, volume and frequency changes are queued with an
// age; every update adds the frame time to each queued command, and once the
// oldest one is as old as the delay it is applied to the duplicate and every
// command at least that old is dropped (only the oldest of several that ripen
// together takes effect). Pan changes apply at once, mirrored and reduced to
// a quarter. A queued play also moves the duplicate to where the original is
// playing, at the moment it is queued.

#include "audio/Mixer.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace mm2::audio {

class EchoEffect {
public:
    // EchoEffect::EchoEffect: the echo plays at 0.96 of the original's volume.
    static constexpr float kDefaultAttenuation = 0.96f;
    // SetDelayTime sizes each command queue for 180 commands per second of delay.
    static constexpr float kQueueRate = 180.0f;

    EchoEffect() = default;
    EchoEffect(const EchoEffect&) = delete;
    EchoEffect& operator=(const EchoEffect&) = delete;
    ~EchoEffect();

    // Enable / EffectBase::CreateDSoundBuffer: the first call makes the
    // duplicate of `sound` with the original's volume (`volume`: an Angel
    // volume that already includes the master), pan, pitch and play position;
    // later calls only move it to `position`. The queue size is reset, so the
    // next setDelayTime sizes the queues and sets the delay. Returns false if
    // no duplicate could be made.
    bool enable(Mixer& mixer, std::shared_ptr<const SoundBuffer> sound, Bus bus, float volume, float pan,
                float pitch, std::uint64_t position);
    // Disable: empties the queues and stops the duplicate, which keeps its
    // position.
    void disable();
    // SetDelayTime: the first call after enable() sets the delay and sizes the
    // queues (delay * 180 commands each, truncated); later calls keep both. If
    // the original is playing a loop, a play is queued.
    void setDelayTime(float seconds, bool originalLooping);
    void setAttenuation(float attenuation) { m_attenuation = attenuation; }
    float attenuation() const { return m_attenuation; }
    float delay() const { return m_delay; }

    // Update: ages the queued commands by `dt` and applies the ripe ones:
    // volume, frequency, then play and stop (the stop first when fewer stops
    // than plays are queued).
    void update(float dt);
    // QueuePlay: moves the duplicate to `originalPosition` now and queues a
    // play (looping or once).
    void queuePlay(bool loop, std::uint64_t originalPosition);
    void queueStop();
    // QueueVolume: `volume` (an Angel volume including the master) times the
    // attenuation, turned into DirectSound hundredths of a decibel when it is
    // queued.
    void queueVolume(float volume);
    // QueueFrequency: the original's frequency in Hz.
    void queueFrequency(std::uint32_t hz);
    // SetVolume: like queueVolume but at once (audManager::SetVolAllSounds).
    void setVolume(float volume);
    // SetFrequency: the sample rate times `factor`, clamped to 100..100000 Hz,
    // at once.
    void setFrequency(float factor);
    // CalculatePan: -0.25 * pan, clamped to -1..1, at once.
    void calculatePan(float pan);
    // Stop: the duplicate stops at once (audControl::StopPCEchoBuffers).
    void stop();

    VoiceHandle voice() const { return m_voice; }
    bool playing() const;

private:
    struct Entry {
        std::int32_t value = 0;
        float age = 0.0f;
    };
    // One command queue, entries 0..last (-1: empty). A push past the
    // capacity starts again at entry 0, dropping the queued commands, as the
    // original's index wraps.
    struct Queue {
        std::vector<Entry> entries;
        int last = -1;
        void push(std::int32_t value);
        std::optional<std::int32_t> ripen(float dt, float delay);
    };
    void applyVolume(std::int32_t hundredthsDb);
    void applyFrequency(std::int32_t hz);
    std::int32_t volumeUnits(float volume) const;

    Mixer* m_mixer = nullptr;
    VoiceHandle m_voice = 0;
    int m_rate = 22050; // +0x22: the original's samples per second (22050 until enabled)
    int m_capacity = 0;
    float m_delay = 0.0f;
    float m_attenuation = kDefaultAttenuation;
    Queue m_volume, m_frequency, m_stop, m_play;
};

} // namespace mm2::audio
