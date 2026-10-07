#include "audio/game/SoundSlot.h"

#include "audio/game/AudioTables.h"
#include "core/StringUtil.h"

#include <algorithm>
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
        stop();
        m_mixer = std::exchange(o.m_mixer, nullptr);
        m_buffer = std::move(o.m_buffer);
        m_bus = o.m_bus;
        m_priority = o.m_priority;
        m_voice = std::exchange(o.m_voice, 0);
        m_looping = o.m_looping;
        m_volume = o.m_volume;
        m_pitch = o.m_pitch;
        m_pan = o.m_pan;
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
    m_pan = 0.0f;
    if (wave.empty() || str::iequals(wave, "NOSOUND"))
        return false;
    m_buffer = bank.get(wave);
    return m_buffer != nullptr;
}

VoiceParams SoundSlot::params(bool loop, const Emitter3D* emitter) const {
    VoiceParams p;
    p.volume = ageVolumeToGain(m_volume);
    p.pitch = m_pitch;
    p.pan = agePanToMixer(m_pan);
    p.loop = loop;
    p.bus = m_bus;
    p.priority = m_priority;
    if (emitter)
        p.spatial = *emitter;
    return p;
}

void SoundSlot::playLoop(float volume, float pitch, const Emitter3D* emitter) {
    if (!m_buffer || !m_mixer)
        return;
    m_volume = volume;
    m_pitch = clampPitch(pitch, m_buffer->sampleRate);
    if (m_voice && m_looping && m_mixer->isPlaying(m_voice)) {
        m_mixer->setVolume(m_voice, ageVolumeToGain(m_volume));
        m_mixer->setPitch(m_voice, m_pitch);
        m_mixer->setPan(m_voice, agePanToMixer(m_pan));
        if (emitter)
            m_mixer->setEmitter(m_voice, *emitter);
        return;
    }
    stop();
    m_voice = m_mixer->play(m_buffer, params(true, emitter));
    m_looping = true;
}

void SoundSlot::playOnce(float volume, float pitch, const Emitter3D* emitter) {
    if (!m_buffer || !m_mixer)
        return;
    stop();
    m_volume = volume;
    m_pitch = clampPitch(pitch, m_buffer->sampleRate);
    m_voice = m_mixer->play(m_buffer, params(false, emitter));
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

void SoundSlot::setPan(float pan) {
    m_pan = pan;
    if (m_voice && m_mixer)
        m_mixer->setPan(m_voice, agePanToMixer(pan));
}

void SoundSlot::setEmitter(const Emitter3D& emitter) {
    if (m_voice && m_mixer)
        m_mixer->setEmitter(m_voice, emitter);
}

} // namespace mm2::audio::game
