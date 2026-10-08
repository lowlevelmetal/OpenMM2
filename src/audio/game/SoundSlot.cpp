// MM2's AudSoundBase over one buffer, with the audObject / audSound clamps; see
// SoundSlot.h.
#include "audio/game/SoundSlot.h"

#include "audio/game/AudioTables.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace mm2::audio::game {

float clampPitch(float pitch, int sampleRate) {
    if (sampleRate <= 0)
        return std::max(pitch, 0.0f);
    float hz = pitch * static_cast<float>(sampleRate);
    if (hz < 0.0f)
        hz = 100000.0f;
    hz = std::clamp(hz, 100.0f, 100000.0f);
    return hz / static_cast<float>(sampleRate);
}

SoundSlot::SoundSlot(SoundSlot&& o) noexcept { *this = std::move(o); }

SoundSlot& SoundSlot::operator=(SoundSlot&& o) noexcept {
    if (this != &o) {
        release();
        m_mixer = std::exchange(o.m_mixer, nullptr);
        m_buffer = std::move(o.m_buffer);
        m_bus = o.m_bus;
        m_priority = o.m_priority;
        m_voice = std::exchange(o.m_voice, 0);
        m_looping = o.m_looping;
        m_volume = o.m_volume;
        m_pitch = o.m_pitch;
        m_pan = o.m_pan;
        m_echo = std::move(o.m_echo);
        m_echoOn = std::exchange(o.m_echoOn, false);
    }
    return *this;
}

SoundSlot::~SoundSlot() { release(); }

void SoundSlot::release() {
    if (m_voice && m_mixer)
        m_mixer->stop(m_voice);
    m_voice = 0;
    m_echo.reset();
    m_echoOn = false;
}

std::uint64_t SoundSlot::currentPosition() const {
    // A stopped buffer was rewound (audSound::Stop) or played to its end.
    return playing() ? m_mixer->position(m_voice) : 0;
}

bool SoundSlot::load(Mixer& mixer, SoundBank& bank, std::string_view wave, Bus bus, int priority) {
    release();
    m_mixer = &mixer;
    m_bus = bus;
    m_priority = priority;
    m_buffer.reset();
    m_volume = 0.0f;
    m_pitch = 1.0f;
    m_pan = 0.0f;
    if (wave.empty() || str::iequals(wave, "NOSOUND"))
        return false;
    m_buffer = bank.get(wave);
    return m_buffer != nullptr;
}

VoiceParams SoundSlot::params(bool loop, const Emitter3D* emitter) const {
    VoiceParams p;
    p.volume = m_volume; // Angel volume: the mixer applies the bus master (audObject::SetVolume)
    p.angel = true;
    p.pitch = m_pitch;
    p.pan = agePanToMixer(m_pan);
    p.loop = loop;
    p.bus = m_bus;
    p.priority = m_priority;
    if (emitter)
        p.spatial = *emitter;
    return p;
}

void SoundSlot::start(bool loop, const Emitter3D* emitter) {
    if (m_voice && m_mixer && m_mixer->isPlaying(m_voice)) {
        // audSound::Play does nothing to a buffer that is playing; the new
        // volume and frequency were applied to it above.
        m_mixer->setVolume(m_voice, m_volume);
        m_mixer->setPitch(m_voice, m_pitch);
        m_mixer->setPan(m_voice, agePanToMixer(m_pan));
        if (emitter)
            m_mixer->setEmitter(m_voice, *emitter);
        return;
    }
    m_voice = m_mixer->play(m_buffer, params(loop, emitter));
    m_looping = loop;
}

void SoundSlot::playLoop(float volume, float pitch, const Emitter3D* emitter) {
    if (!m_buffer || !m_mixer)
        return;
    // audSound::SetVolume passes (v - 1) * 10000 to DirectSound, which rejects
    // values outside -10000..0 and keeps the buffer's volume.
    if (-1.0f < volume && 0.0f <= volume && volume <= 1.0f)
        m_volume = volume;
    // audSound::SetPitch: only the buffer frequency range applies.
    if (-1.0f < pitch)
        m_pitch = clampPitch(pitch, m_buffer->sampleRate);
    if (m_echoOn && m_echo)
        m_echo->queuePlay(true, currentPosition());
    start(true, emitter);
}

void SoundSlot::playOnce(float volume, float pitch, const Emitter3D* emitter) {
    if (!m_buffer || !m_mixer)
        return;
    if (-1.0f < volume)
        m_volume = volume;
    if (-1.0f < pitch)
        m_pitch = clampPitch(std::clamp(pitch, 0.0f, 2.0f), m_buffer->sampleRate);
    if (m_echoOn && m_echo)
        m_echo->queuePlay(false, currentPosition());
    start(false, emitter);
}

void SoundSlot::stop() {
    if (!m_mixer)
        return;
    if (m_voice)
        m_mixer->stop(m_voice);
    m_voice = 0;
    if (m_echoOn && m_echo)
        m_echo->queueStop();
}

bool SoundSlot::playing() const { return m_voice && m_mixer && m_mixer->isPlaying(m_voice); }

void SoundSlot::setVolume(float volume) {
    m_volume = volume;
    if (m_voice && m_mixer)
        m_mixer->setVolume(m_voice, m_volume);
    if (m_echoOn && m_echo && m_mixer)
        m_echo->queueVolume(m_mixer->busMaster(m_bus) * volume);
}

void SoundSlot::setPitch(float pitch) {
    if (!m_buffer)
        return;
    m_pitch = clampPitch(std::clamp(pitch, 0.0f, 2.0f), m_buffer->sampleRate);
    if (m_voice && m_mixer)
        m_mixer->setPitch(m_voice, m_pitch);
    if (m_echoOn && m_echo) {
        // The buffer's frequency in whole Hz (audObject +0x90); rounded here
        // because m_pitch is stored as a multiplier.
        const float hz = m_pitch * static_cast<float>(m_buffer->sampleRate);
        m_echo->queueFrequency(static_cast<std::uint32_t>(std::lround(hz)));
    }
}

void SoundSlot::setPan(float pan) {
    m_pan = std::clamp(pan, -1.0f, 1.0f);
    if (m_voice && m_mixer)
        m_mixer->setPan(m_voice, agePanToMixer(m_pan));
    if (m_echoOn && m_echo)
        m_echo->calculatePan(pan);
}

void SoundSlot::enableEcho() {
    // SetEchoEffect: needs a loaded sample (an audControl and a handle).
    if (!m_buffer || !m_mixer)
        return;
    if (!m_echo) {
        // audFX::EnablePCEcho for the sample's handle. The duplicate copies the
        // buffer's DirectSound volume, i.e. the volume after the master.
        auto echo = std::make_unique<EchoEffect>();
        const float volume = std::clamp(m_volume * m_mixer->busMaster(m_bus), 0.0f, 1.0f);
        if (echo->enable(*m_mixer, m_buffer, m_bus, volume, m_pan, m_pitch, currentPosition()))
            m_echo = std::move(echo);
    }
    m_echoOn = true;
}

void SoundSlot::disableEcho() {
    if (!m_echoOn)
        return;
    m_echoOn = false;
    if (m_echo)
        m_echo->disable();
}

void SoundSlot::setEchoDelay(float seconds) {
    if (m_echo)
        m_echo->setDelayTime(seconds, playing() && m_looping);
}

void SoundSlot::setEchoAttenuation(float attenuation) {
    if (m_echo)
        m_echo->setAttenuation(attenuation);
}

void SoundSlot::setEchoFrequency(float factor) {
    if (m_echoOn && m_echo)
        m_echo->setFrequency(factor);
}

void SoundSlot::updateEcho(float dt) {
    if (m_echoOn && m_echo)
        m_echo->update(dt);
}

void SoundSlot::setEmitter(const Emitter3D& emitter) {
    if (m_voice && m_mixer)
        m_mixer->setEmitter(m_voice, emitter);
}

} // namespace mm2::audio::game
