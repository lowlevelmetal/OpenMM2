// The Angel audio library's echo duplicate, ported from MM2 (EchoEffect,
// audFX::EnablePCEcho, EffectBase::CreateDSoundBuffer). See EchoEffect.h.
#include "audio/EchoEffect.h"

#include "audio/AngelUnits.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mm2::audio {
namespace {

// DirectSound's units: volume in hundredths of a decibel (-10000..0), pan the
// same on the far channel (-10000..10000), frequency 100..100000 Hz.
constexpr double kDsUnits = 10000.0;
constexpr std::int32_t kMinFrequency = 100, kMaxFrequency = 100000;

// The original's float-to-integer conversion (__ftol) truncates toward zero.
std::int32_t ftol(double v) {
    if (!(v == v))
        return std::numeric_limits<std::int32_t>::min();
    v = std::clamp(v, static_cast<double>(std::numeric_limits<std::int32_t>::min()),
                   static_cast<double>(std::numeric_limits<std::int32_t>::max()));
    return static_cast<std::int32_t>(v);
}

} // namespace

void EchoEffect::Queue::push(std::int32_t value) {
    // The original writes through an unsized array here when SetDelayTime was
    // never called; nothing is queued instead.
    if (entries.empty())
        return;
    ++last;
    if (static_cast<int>(entries.size()) <= last)
        last = 0;
    entries[static_cast<std::size_t>(last)] = {value, 0.0f};
}

std::optional<std::int32_t> EchoEffect::Queue::ripen(float dt, float delay) {
    // UpdateVolume / UpdatePitch / UpdatePlay / UpdateStop.
    if (entries.empty())
        return std::nullopt;
    for (int i = 0; i <= last; ++i)
        entries[static_cast<std::size_t>(i)].age = dt + entries[static_cast<std::size_t>(i)].age;
    if (!(delay <= entries[0].age))
        return std::nullopt;
    const std::int32_t value = entries[0].value;
    while (delay <= entries[0].age && 0 <= last) {
        // Shift the queue down by one entry.
        for (int i = 0; i <= last; ++i) {
            const auto next = static_cast<std::size_t>(i + 1);
            entries[static_cast<std::size_t>(i)] = next < entries.size() ? entries[next] : Entry{};
        }
        --last;
    }
    if (last == -1)
        entries[0].age = 0.0f;
    return value;
}

EchoEffect::~EchoEffect() {
    if (m_mixer && m_voice)
        m_mixer->stop(m_voice);
}

bool EchoEffect::enable(Mixer& mixer, std::shared_ptr<const SoundBuffer> sound, Bus bus, float volume,
                        float pan, float pitch, std::uint64_t position) {
    m_mixer = &mixer;
    m_capacity = 0;
    if (!sound)
        return false;
    if (!m_voice) {
        // CreateDSoundBuffer: a duplicate with the original's volume and play
        // position (and its frequency and pan, as DuplicateSoundBuffer copies
        // the buffer's settings).
        VoiceParams p;
        p.volume = volume;
        p.angel = true;
        p.masterApplied = true;
        p.pan = agePanToMixer(pan);
        p.pitch = pitch;
        p.bus = bus;
        m_voice = mixer.createEffectVoice(sound, p);
        if (!m_voice) {
            disable();
            return false;
        }
    }
    m_mixer->setPosition(m_voice, position);
    // EnablePCEcho: the frequency factor of SetFrequency is relative to the
    // original's sample rate.
    m_rate = sound->sampleRate;
    return true;
}

void EchoEffect::disable() {
    m_frequency.last = m_volume.last = m_stop.last = m_play.last = -1;
    if (m_mixer && m_voice)
        m_mixer->haltEffect(m_voice);
}

void EchoEffect::setDelayTime(float seconds, bool originalLooping) {
    if (m_capacity == 0) {
        m_delay = seconds;
        // The queue size is truncated to a short.
        const int capacity = static_cast<std::int16_t>(ftol(static_cast<double>(seconds) * kQueueRate));
        const auto size = static_cast<std::size_t>(std::max(capacity, 0));
        for (Queue* q : {&m_volume, &m_frequency, &m_stop, &m_play})
            q->entries.assign(size, Entry{});
        m_capacity = capacity;
    }
    // OriginalBufferPlaying(1): the original is playing a loop.
    if (originalLooping)
        m_play.push(1);
}

void EchoEffect::update(float dt) {
    if (0 <= m_volume.last)
        if (auto v = m_volume.ripen(dt, m_delay))
            applyVolume(*v);
    if (0 <= m_frequency.last)
        if (auto v = m_frequency.ripen(dt, m_delay))
            applyFrequency(*v);
    auto updatePlay = [&] {
        if (auto v = m_play.ripen(dt, m_delay); v && m_mixer && m_voice)
            m_mixer->playEffect(m_voice, (*v & 1) != 0); // DSBPLAY_LOOPING
    };
    auto updateStop = [&] {
        if (m_stop.ripen(dt, m_delay) && m_mixer && m_voice)
            m_mixer->haltEffect(m_voice);
    };
    if (m_stop.last < m_play.last) {
        if (0 <= m_stop.last)
            updateStop();
        if (0 <= m_play.last)
            updatePlay();
    } else {
        if (0 <= m_play.last)
            updatePlay();
        if (0 <= m_stop.last)
            updateStop();
    }
}

void EchoEffect::queuePlay(bool loop, std::uint64_t originalPosition) {
    if (m_mixer && m_voice)
        m_mixer->setPosition(m_voice, originalPosition);
    m_play.push(loop ? 1 : 0);
}

void EchoEffect::queueStop() { m_stop.push(0); }

std::int32_t EchoEffect::volumeUnits(float volume) const {
    return ftol(static_cast<double>(volume) * m_attenuation * kDsUnits - kDsUnits);
}

void EchoEffect::queueVolume(float volume) { m_volume.push(volumeUnits(volume)); }

void EchoEffect::queueFrequency(std::uint32_t hz) {
    m_frequency.push(static_cast<std::int32_t>(std::min<std::uint32_t>(hz, 0x7fffffffu)));
}

void EchoEffect::setVolume(float volume) { applyVolume(volumeUnits(volume)); }

void EchoEffect::applyVolume(std::int32_t hundredthsDb) {
    // IDirectSoundBuffer::SetVolume rejects values outside -10000..0 and keeps
    // the previous volume.
    if (!m_mixer || !m_voice || hundredthsDb < -10000 || 0 < hundredthsDb)
        return;
    m_mixer->setVolume(m_voice, static_cast<float>(1.0 + hundredthsDb / kDsUnits));
}

void EchoEffect::applyFrequency(std::int32_t hz) {
    // IDirectSoundBuffer::SetFrequency accepts 100..100000 Hz.
    if (!m_mixer || !m_voice || m_rate <= 0 || hz < kMinFrequency || kMaxFrequency < hz)
        return;
    m_mixer->setPitch(m_voice, static_cast<float>(hz) / static_cast<float>(m_rate));
}

void EchoEffect::setFrequency(float factor) {
    // MM2 keeps the rate as a short; its 22050 Hz files fit.
    double hz = static_cast<double>(m_rate) * factor;
    if (hz < kMinFrequency)
        hz = kMinFrequency;
    else if (!(hz <= kMaxFrequency))
        hz = kMaxFrequency;
    applyFrequency(ftol(hz));
}

void EchoEffect::calculatePan(float pan) {
    double p = static_cast<double>(pan) * -0.25;
    if (p < -1.0)
        p = -1.0;
    else if (!(p <= 1.0))
        p = 1.0;
    if (m_mixer && m_voice)
        m_mixer->setPan(m_voice, agePanToMixer(static_cast<float>(ftol(p * kDsUnits) / kDsUnits)));
}

void EchoEffect::stop() {
    if (m_mixer && m_voice)
        m_mixer->haltEffect(m_voice);
}

bool EchoEffect::playing() const { return m_mixer && m_voice && m_mixer->isPlaying(m_voice); }

} // namespace mm2::audio
