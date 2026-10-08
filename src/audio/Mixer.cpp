#include "audio/Mixer.h"

#include "audio/AngelUnits.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mm2::audio {
namespace {

constexpr std::uint32_t kIndexBits = 10; // up to 1024 voices
constexpr std::uint32_t kIndexMask = (1u << kIndexBits) - 1;
constexpr float kSpeedOfSound = 343.3f; // DS3D uses metres and the speed of sound in air

VoiceHandle makeHandle(std::size_t index, std::uint32_t generation) {
    return (generation << kIndexBits) | static_cast<std::uint32_t>(index);
}

} // namespace

Mixer::Mixer(int sampleRate, int maxVoices)
    : m_rate(sampleRate), m_maxVoices(static_cast<std::size_t>(std::clamp(maxVoices, 1, 256))) {
    m_voices.resize(m_maxVoices);
    m_bus.fill(1.0f);
    m_busMaster.fill(1.0f);
    m_busGain.fill(1.0f);
}

Mixer::Voice* Mixer::lookup(VoiceHandle h) {
    const std::size_t index = h & kIndexMask;
    if (h == 0 || index >= m_voices.size())
        return nullptr;
    Voice& v = m_voices[index];
    return v.active && v.generation == (h >> kIndexBits) ? &v : nullptr;
}

const Mixer::Voice* Mixer::lookup(VoiceHandle h) const { return const_cast<Mixer*>(this)->lookup(h); }

std::size_t Mixer::pickSlot() {
    for (std::size_t i = 0; i < m_maxVoices; ++i)
        if (!m_voices[i].active)
            return i;
    // Steal (audManager::MoveToActive): the lowest priority, then the oldest.
    std::size_t best = 0;
    for (std::size_t i = 1; i < m_maxVoices; ++i) {
        const Voice& a = m_voices[i];
        const Voice& b = m_voices[best];
        if (a.params.priority != b.params.priority) {
            if (a.params.priority < b.params.priority)
                best = i;
        } else if (a.serial < b.serial) {
            best = i;
        }
    }
    return best;
}

VoiceHandle Mixer::claim(std::size_t slot, std::shared_ptr<const SoundBuffer> sound,
                         const VoiceParams& params) {
    Voice& v = m_voices[slot];
    const std::uint32_t generation = ((v.generation + 1) & ((1u << (32 - kIndexBits)) - 1)) | 1u;
    v = Voice{};
    v.sound = std::move(sound);
    v.params = params;
    v.generation = generation;
    v.serial = ++m_serial;
    v.active = true;
    return makeHandle(slot, generation);
}

VoiceHandle Mixer::play(std::shared_ptr<const SoundBuffer> sound, const VoiceParams& params) {
    if (!sound || sound->frames() == 0 || sound->sampleRate <= 0)
        return 0;
    std::lock_guard lock(m_mutex);
    return claim(pickSlot(), std::move(sound), params);
}

VoiceHandle Mixer::createEffectVoice(std::shared_ptr<const SoundBuffer> sound, const VoiceParams& params) {
    if (!sound || sound->frames() == 0 || sound->sampleRate <= 0)
        return 0;
    std::lock_guard lock(m_mutex);
    std::size_t slot = m_maxVoices;
    while (slot < m_voices.size() && m_voices[slot].active)
        ++slot;
    if (slot == m_voices.size()) {
        if (slot > kIndexMask)
            return 0;
        m_voices.emplace_back();
    }
    const VoiceHandle h = claim(slot, std::move(sound), params);
    m_voices[slot].effect = true;
    m_voices[slot].halted = true;
    return h;
}

void Mixer::playEffect(VoiceHandle h, bool loop) {
    std::lock_guard lock(m_mutex);
    if (Voice* v = lookup(h); v && v->effect) {
        v->params.loop = loop;
        if (v->halted)
            v->started = false; // no ramp from the level it stopped at
        v->halted = false;
    }
}

void Mixer::haltEffect(VoiceHandle h) {
    std::lock_guard lock(m_mutex);
    if (Voice* v = lookup(h); v && v->effect)
        v->halted = true;
}

std::uint64_t Mixer::position(VoiceHandle h) const {
    std::lock_guard lock(m_mutex);
    const Voice* v = lookup(h);
    return v ? v->position >> 32 : 0;
}

void Mixer::setPosition(VoiceHandle h, std::uint64_t frame) {
    std::lock_guard lock(m_mutex);
    if (Voice* v = lookup(h)) {
        const std::uint64_t frames = v->sound->frames();
        v->position = (frame < frames ? frame : 0) << 32;
    }
}

void Mixer::stop(VoiceHandle h) {
    std::lock_guard lock(m_mutex);
    if (Voice* v = lookup(h)) {
        v->active = false;
        v->sound.reset();
    }
}

void Mixer::stopAll() {
    std::lock_guard lock(m_mutex);
    for (auto& v : m_voices) {
        if (v.effect) {
            v.halted = true;
            continue;
        }
        v.active = false;
        v.sound.reset();
    }
}

bool Mixer::isPlaying(VoiceHandle h) const {
    std::lock_guard lock(m_mutex);
    const Voice* v = lookup(h);
    return v && !v->halted;
}

void Mixer::setVolume(VoiceHandle h, float volume) {
    std::lock_guard lock(m_mutex);
    if (Voice* v = lookup(h))
        v->params.volume = std::max(volume, 0.0f);
}

void Mixer::setPitch(VoiceHandle h, float pitch) {
    std::lock_guard lock(m_mutex);
    if (Voice* v = lookup(h))
        v->params.pitch = std::clamp(pitch, 0.01f, 16.0f);
}

void Mixer::setPan(VoiceHandle h, float pan) {
    std::lock_guard lock(m_mutex);
    if (Voice* v = lookup(h))
        v->params.pan = std::clamp(pan, -1.0f, 1.0f);
}

void Mixer::setPaused(VoiceHandle h, bool paused) {
    std::lock_guard lock(m_mutex);
    if (Voice* v = lookup(h))
        v->params.paused = paused;
}

void Mixer::setEmitter(VoiceHandle h, const Emitter3D& emitter) {
    std::lock_guard lock(m_mutex);
    if (Voice* v = lookup(h))
        v->params.spatial = emitter;
}

void Mixer::setListener(const Mat34& transform, const Vec3& velocity) {
    std::lock_guard lock(m_mutex);
    m_listener = transform;
    m_listenerVel = velocity;
}

void Mixer::setBusVolume(Bus bus, float volume) {
    std::lock_guard lock(m_mutex);
    const auto i = static_cast<std::size_t>(bus);
    m_bus[i] = std::clamp(volume, 0.0f, 1.0f);
    m_busMaster[i] = ageMasterVolume(m_bus[i]);
    m_busGain[i] = ageVolumeToGain(m_busMaster[i]);
}

void Mixer::setStereo(bool stereo) {
    std::lock_guard lock(m_mutex);
    m_stereo = stereo;
}

bool Mixer::stereo() const {
    std::lock_guard lock(m_mutex);
    return m_stereo;
}

float Mixer::busVolume(Bus bus) const {
    std::lock_guard lock(m_mutex);
    return m_bus[static_cast<std::size_t>(bus)];
}

float Mixer::busMaster(Bus bus) const {
    std::lock_guard lock(m_mutex);
    return m_busMaster[static_cast<std::size_t>(bus)];
}

void Mixer::setMasterVolume(float volume) {
    std::lock_guard lock(m_mutex);
    m_master = std::max(volume, 0.0f);
}

void Mixer::setBalance(float balance) {
    std::lock_guard lock(m_mutex);
    m_balance = std::clamp(balance, -1.0f, 1.0f);
}

void Mixer::setDopplerFactor(float f) {
    std::lock_guard lock(m_mutex);
    m_doppler = std::max(f, 0.0f);
}

void Mixer::setRolloffFactor(float f) {
    std::lock_guard lock(m_mutex);
    m_rolloff = std::max(f, 0.0f);
}

void Mixer::pauseAll(bool paused) {
    std::lock_guard lock(m_mutex);
    m_paused = paused;
}

int Mixer::addStream(std::shared_ptr<StreamSource> source, Bus bus, float volume) {
    std::lock_guard lock(m_mutex);
    const int id = m_nextStreamId++;
    m_streams.push_back({id, std::move(source), bus, std::max(volume, 0.0f)});
    return id;
}

void Mixer::setStreamVolume(int id, float volume) {
    std::lock_guard lock(m_mutex);
    for (auto& s : m_streams)
        if (s.id == id)
            s.volume = std::max(volume, 0.0f);
}

void Mixer::removeStream(int id) {
    std::shared_ptr<StreamSource> keep; // destroyed outside the lock
    std::lock_guard lock(m_mutex);
    for (auto it = m_streams.begin(); it != m_streams.end(); ++it) {
        if (it->id == id) {
            keep = std::move(it->source);
            m_streams.erase(it);
            break;
        }
    }
}

int Mixer::activeVoices() const {
    std::lock_guard lock(m_mutex);
    return static_cast<int>(
        std::ranges::count_if(m_voices, [](const Voice& v) { return v.active && !v.halted; }));
}

void Mixer::computeTargets(const Voice& v, float& gl, float& gr, double& rate) const {
    const VoiceParams& p = v.params;
    const auto bus = static_cast<std::size_t>(p.bus);
    // audObject::SetVolume: Angel volume times the master, clamped to 0..1.
    float gain = p.angel && p.masterApplied ? ageVolumeToGain(p.volume)
                 : p.angel ? ageVolumeToGain(std::clamp(p.volume * m_busMaster[bus], 0.0f, 1.0f))
                           : p.volume * m_busGain[bus];
    gain *= m_master;
    float pan = m_stereo ? p.pan : 0.0f;
    float pitch = p.pitch;

    if (p.spatial) {
        const Emitter3D& e = *p.spatial;
        const Vec3 rel = e.position - m_listener.m3;
        const float dist = rel.mag();
        const float minD = std::max(e.minDistance, 1e-4f);
        const float d = std::min(dist, std::max(e.maxDistance, minD));
        if (d > minD)
            gain *= minD / (minD + m_rolloff * (d - minD));
        if (dist > 1e-3f) {
            const Vec3 local = m_listener.untransformDir(rel) * (1.0f / dist);
            pan = m_stereo ? std::clamp(local.x, -1.0f, 1.0f) : 0.0f;
            if (m_doppler > 0.0f) {
                const Vec3 dir = rel * (1.0f / dist); // listener -> source
                const float vl = m_listenerVel.dot(dir) * m_doppler;
                const float vs = e.velocity.dot(dir) * m_doppler;
                const float ratio = (kSpeedOfSound + vl) / std::max(kSpeedOfSound + vs, 1.0f);
                pitch *= std::clamp(ratio, 0.5f, 2.0f);
            }
        } else {
            pan = 0.0f;
        }
    }

    // DirectSound pan semantics: both channels at full level in the centre,
    // panning attenuates only the opposite channel.
    gl = gain * (pan > 0.0f ? 1.0f - pan : 1.0f);
    gr = gain * (pan < 0.0f ? 1.0f + pan : 1.0f);
    rate = static_cast<double>(v.sound->sampleRate) / m_rate * pitch;
}

void Mixer::mix(float* out, int frames) {
    std::memset(out, 0, sizeof(float) * 2 * static_cast<std::size_t>(frames));
    std::lock_guard lock(m_mutex);
    if (m_paused || frames <= 0)
        return;
    const float invFrames = 1.0f / static_cast<float>(frames);

    for (auto& v : m_voices) {
        if (!v.active || v.halted || v.params.paused)
            continue;
        float targetL, targetR;
        double rate;
        computeTargets(v, targetL, targetR, rate);
        if (!v.started) {
            // No ramp-in on the first block: sounds start at their set level.
            v.gainL = targetL;
            v.gainR = targetR;
            v.started = true;
        }
        const SoundBuffer& s = *v.sound;
        const std::uint64_t length = static_cast<std::uint64_t>(s.frames()) << 32;
        const std::uint64_t step = static_cast<std::uint64_t>(rate * 4294967296.0);
        const int ch = s.channels;
        const std::int16_t* src = s.samples.data();
        const float dl = (targetL - v.gainL) * invFrames;
        const float dr = (targetR - v.gainR) * invFrames;
        float gl = v.gainL, gr = v.gainR;

        for (int i = 0; i < frames; ++i) {
            if (v.position >= length) {
                if (!v.params.loop) {
                    if (v.effect) {
                        v.halted = true;
                        v.position = 0;
                    } else {
                        v.active = false;
                    }
                    break;
                }
                v.position %= length;
            }
            const std::size_t idx = static_cast<std::size_t>(v.position >> 32);
            const float frac = static_cast<float>(v.position & 0xFFFFFFFFu) * (1.0f / 4294967296.0f);
            std::size_t next = idx + 1;
            if (next >= s.frames())
                next = v.params.loop ? 0 : idx;
            float l, r;
            if (ch == 1) {
                const float a = src[idx], b = src[next];
                l = r = (a + (b - a) * frac) * (1.0f / 32768.0f);
            } else {
                const float al = src[idx * 2], bl = src[next * 2];
                const float ar = src[idx * 2 + 1], br = src[next * 2 + 1];
                l = (al + (bl - al) * frac) * (1.0f / 32768.0f);
                r = (ar + (br - ar) * frac) * (1.0f / 32768.0f);
            }
            gl += dl;
            gr += dr;
            out[i * 2] += l * gl;
            out[i * 2 + 1] += r * gr;
            v.position += step;
        }
        v.gainL = targetL;
        v.gainR = targetR;
        if (!v.active)
            v.sound.reset();
    }

    m_streamScratch.resize(static_cast<std::size_t>(frames) * 2);
    for (auto& s : m_streams) {
        const float gain = s.volume * m_busGain[static_cast<std::size_t>(s.bus)] * m_master;
        std::fill(m_streamScratch.begin(), m_streamScratch.end(), 0.0f);
        s.source->render(m_streamScratch.data(), frames);
        if (gain <= 0.0f)
            continue;
        for (std::size_t i = 0; i < m_streamScratch.size(); ++i)
            out[i] += m_streamScratch[i] * gain;
    }

    if (m_balance != 0.0f) {
        const float l = m_balance > 0.0f ? 1.0f - m_balance : 1.0f;
        const float r = m_balance < 0.0f ? 1.0f + m_balance : 1.0f;
        for (int i = 0; i < frames; ++i) {
            out[i * 2] *= l;
            out[i * 2 + 1] *= r;
        }
    }
}

} // namespace mm2::audio
