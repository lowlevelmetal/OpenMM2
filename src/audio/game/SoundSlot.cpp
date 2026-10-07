#include "audio/game/SoundSlot.h"

#include "audio/game/AudioTables.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <utility>

namespace mm2::audio::game {

float clampPitch(float pitch, int sampleRate) {
    pitch = std::clamp(pitch, 0.0f, 10.0f);
    if (sampleRate > 0) {
        const float hz = std::clamp(pitch * static_cast<float>(sampleRate), 100.0f, 100000.0f);
        pitch = hz / static_cast<float>(sampleRate);
    }
    return pitch;
}

SoundSlot::SoundSlot(SoundSlot&& o) noexcept { *this = std::move(o); }

SoundSlot& SoundSlot::operator=(SoundSlot&& o) noexcept {
    if (this != &o) {
        stop();
        m_mixer = std::exchange(o.m_mixer, nullptr);
        m_buffer = std::move(o.m_buffer);
        m_bus = o.m_bus;
        m_priority = o.m_priority;
        m_voice = std::exchange(o.m_voice, 0);
        m_looping = o.m_looping;
        m_volume = o.m_volume;
        m_pitch = o.m_pitch;
    }
    return *this;
}

SoundSlot::~SoundSlot() { stop(); }

bool SoundSlot::load(Mixer& mixer, SoundBank& bank, std::string_view wave, Bus bus, int priority) {
    stop();
    m_mixer = &mixer;
    m_bus = bus;
    m_priority = priority;
    m_buffer.reset();
    if (wave.empty() || str::iequals(wave, "NOSOUND"))
        return false;
    m_buffer = bank.get(wave);
    return m_buffer != nullptr;
}

void SoundSlot::playLoop(float volume, float pitch, const Emitter3D* emitter) {
    if (!m_buffer || !m_mixer)
        return;
    m_volume = volume;
    m_pitch = clampPitch(pitch, m_buffer->sampleRate);
    if (m_voice && m_looping && m_mixer->isPlaying(m_voice)) {
        m_mixer->setVolume(m_voice, ageVolumeToGain(m_volume));
        m_mixer->setPitch(m_voice, m_pitch);
        if (emitter)
            m_mixer->setEmitter(m_voice, *emitter);
        return;
    }
    stop();
    VoiceParams p;
    p.volume = ageVolumeToGain(m_volume);
    p.pitch = m_pitch;
    p.loop = true;
    p.bus = m_bus;
    p.priority = m_priority;
    if (emitter)
        p.spatial = *emitter;
    m_voice = m_mixer->play(m_buffer, p);
    m_looping = true;
}

void SoundSlot::playOnce(float volume, float pitch, const Emitter3D* emitter) {
    if (!m_buffer || !m_mixer)
        return;
    stop();
    m_volume = volume;
    m_pitch = clampPitch(pitch, m_buffer->sampleRate);
    VoiceParams p;
    p.volume = ageVolumeToGain(m_volume);
    p.pitch = m_pitch;
    p.bus = m_bus;
    p.priority = m_priority;
    if (emitter)
        p.spatial = *emitter;
    m_voice = m_mixer->play(m_buffer, p);
    m_looping = false;
}

void SoundSlot::stop() {
    if (m_voice && m_mixer)
        m_mixer->stop(m_voice);
    m_voice = 0;
}

bool SoundSlot::playing() const { return m_voice && m_mixer && m_mixer->isPlaying(m_voice); }

void SoundSlot::setVolume(float volume) {
    m_volume = volume;
    if (m_voice && m_mixer)
        m_mixer->setVolume(m_voice, ageVolumeToGain(volume));
}

void SoundSlot::setPitch(float pitch) {
    if (!m_buffer)
        return;
    m_pitch = clampPitch(pitch, m_buffer->sampleRate);
    if (m_voice && m_mixer)
        m_mixer->setPitch(m_voice, m_pitch);
}

void SoundSlot::setEmitter(const Emitter3D& emitter) {
    if (m_voice && m_mixer)
        m_mixer->setEmitter(m_voice, emitter);
}

} // namespace mm2::audio::game
