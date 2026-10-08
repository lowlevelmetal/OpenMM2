// City ambience and rain, ported from MM2 (Aud3DAmbObjContainer,
// Aud3DAmbientObject, mmRainAudio). See docs/audio.md.
#include "audio/game/Ambience.h"

#include "audio/AngelRandom.h"
#include "core/Log.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace mm2::audio::game {

// --- CityAmbience -----------------------------------------------------------------

Vec3 CityAmbience::nearestPoint(const AmbientSoundSet& set, const Vec3& p) {
    if (set.points.empty())
        return p;
    auto pseudo = [&](const Vec3& q) { return std::abs(q.x - p.x) + std::abs(q.y - p.y) + std::abs(q.z - p.z); };
    Vec3 best = set.points.front();
    float bestD = pseudo(best);
    for (std::size_t i = 1; i < set.points.size(); ++i) {
        if (const float d = pseudo(set.points[i]); d < bestD) {
            bestD = d;
            best = set.points[i];
        }
    }
    return best;
}

float CityAmbience::interval(const AmbientSampleDef& d) {
    // PendOneShot: RandomizeNumber(low, high), or exactly high when equal.
    if (d.intervalLow == d.intervalHigh)
        return d.intervalHigh;
    return static_cast<float>(randomizeNumber(d.intervalLow, d.intervalHigh));
}

void CityAmbience::Set::slotLost() {
    // Aud3DAmbientObject::UnAssignSounds.
    for (auto& smp : samples)
        if (smp.slot.playing())
            smp.slot.stop();
}

CityAmbience::Set* CityAmbience::find(std::string_view name) {
    for (auto& s : m_sets)
        if (str::iequals(s->def.name, name))
            return s.get();
    return nullptr;
}

const AmbientSoundSet* CityAmbience::set(std::string_view name) const {
    for (const auto& s : m_sets)
        if (str::iequals(s->def.name, name))
            return &s->def;
    return nullptr;
}

bool CityAmbience::audible(std::string_view name) const {
    for (const auto& s : m_sets)
        if (str::iequals(s->def.name, name))
            return s->hasSlot();
    return false;
}

int CityAmbience::loadSet(const vfs::Vfs& vfs, SoundBank& bank, Mixer& mixer, std::string_view name) {
    m_mixer = &mixer;
    m_bank = &bank;
    if (find(name))
        return -1;
    const std::string path = std::format("aud/ambient/{}.csv", str::lower(name));
    auto text = readText(vfs, path);
    if (!text) {
        log::debug("ambience: {} not found", path);
        return -1;
    }
    std::string error;
    auto def = parseAmbientSoundSet(name, *text, &error);
    if (!def) {
        log::warn("ambience: {}: {}", path, error);
        return -1;
    }
    auto s = std::make_unique<Set>();
    s->def = std::move(*def);
    s->mixer = &mixer;
    s->audio.setDropOffs(s->def.minDistance, s->def.maxDistance);
    s->samples.resize(s->def.samples.size());
    for (std::size_t i = 0; i < s->samples.size(); ++i) {
        const auto& d = s->def.samples[i];
        s->samples[i].slot.load(mixer, bank, d.wave, Bus::Effects);
        s->samples[i].timer = 0.0f;
        // ReadSoundData: any Loop, Interval or Triggered sample makes the set positional.
        if (d.type != AmbientSampleType::RandomOnce)
            s->positional = true;
    }
    if (!s->def.points.empty())
        s->position = s->def.points.front();
    m_sets.push_back(std::move(s));
    return static_cast<int>(m_sets.size()) - 1;
}

bool CityAmbience::load(const vfs::Vfs& vfs, SoundBank& bank, Mixer& mixer, std::string_view city,
                        Object3DManager* manager) {
    stop();
    m_sets.clear();
    m_moving.clear();
    m_mixer = &mixer;
    m_bank = &bank;
    m_manager = manager;
    auto text = readText(vfs, std::format("aud/ambient/{}ambientcontainer.csv", str::lower(city)));
    if (!text)
        return false;
    for (const auto& name : parseAmbientContainer(*text)) {
        if (loadSet(vfs, bank, mixer, name) >= 0) {
            m_sets.back()->background = true;
            m_sets.back()->setManager(manager);
        }
    }
    return !m_sets.empty();
}

void CityAmbience::playOneShot(Set& s, std::size_t index) {
    // Aud3DAmbientObject::PlayOneShot.
    const auto& d = s.def.samples[index];
    auto& slot = s.samples[index].slot;
    float volume, pan, pitch;
    if (d.type == AmbientSampleType::Interval || d.type == AmbientSampleType::Triggered) {
        volume = s.attenuation;
        pan = s.pan;
        pitch = s.doppler;
    } else {
        // Two draws in the same second: the volume and the pan move together.
        volume = static_cast<float>(randomizeNumber(0.75f, 1.0f));
        pan = static_cast<float>(randomizeNumber(-1.0f, 1.0f));
        pitch = 1.0f;
    }
    slot.setVolume(d.volume * volume);
    slot.setPan(pan);
    if (d.doppler)
        slot.setPitch(pitch);
    if (!slot.playing())
        slot.playOnce();
}

void CityAmbience::updateSet(Set& s, float dt, bool inTunnel) {
    // Aud3DAmbientObject::Update(speed): the container's objects are static.
    const float speed = 0.0f;
    const bool areaOk = s.def.audibleArea == 1 ? inTunnel : s.def.audibleArea == 2 ? !inTunnel : true;
    if (!s.hasSlot()) {
        if (!areaOk)
            return;
        // Aud3DObject::Update: a set with points sounds from the point nearest
        // the listener, chosen while the set has no slot.
        if (!s.def.points.empty())
            s.position = nearestPoint(s.def, m_listener.m3);
        if (!s.acquireSlot(s.audio.withinMaxDistance(s.position, m_listener.m3)))
            return;
    }
    // Aud3DAmbientObject::UpdateAudio.
    if (!areaOk || s.audio.pastMaxDistance(s.position, m_listener.m3)) {
        s.releaseSlot();
        s.slotLost();
        return;
    }
    if (s.positional) {
        s.attenuation = s.audio.attenuation();
        s.pan = s.audio.pan(m_listener, s.position);
        s.doppler = s.audio.doppler(1.0f / kDopplerSpeed, dt);
    }
    // UpdateSoundData.
    for (std::size_t i = 0; i < s.samples.size(); ++i) {
        const auto& d = s.def.samples[i];
        auto& smp = s.samples[i];
        if (!d.active || !smp.slot.valid())
            continue;
        const bool inSpeedRange = d.minSpeed <= speed && speed <= d.maxSpeed;
        if (d.type != AmbientSampleType::RandomOnce) {
            // UpdateDoppler.
            smp.slot.setVolume(s.attenuation * d.volume);
            if (d.doppler)
                smp.slot.setPitch(s.doppler);
            smp.slot.setPan(s.pan);
        }
        switch (d.type) {
        case AmbientSampleType::Loop: // UpdateLoop
            if (!inSpeedRange)
                smp.slot.stop();
            else if (!smp.slot.playing())
                smp.slot.playLoop();
            break;
        case AmbientSampleType::RandomOnce:
        case AmbientSampleType::Interval: // UpdateOneShot
            if (inSpeedRange) {
                if (smp.timer <= 0.0f) {
                    playOneShot(s, i);
                    smp.timer = interval(d);
                }
                smp.timer -= dt;
            }
            break;
        case AmbientSampleType::Triggered: break;
        }
    }
}

void CityAmbience::update(const Vec3& listener, float dt, bool inTunnel) {
    Mat34 l = Mat34::identity();
    l.m3 = listener;
    update(l, dt, inTunnel);
}

void CityAmbience::update(const Mat34& listener, float dt, bool inTunnel) {
    m_listener = listener;
    for (auto& s : m_sets)
        if (s->background)
            updateSet(*s, dt, inTunnel);
}

void CityAmbience::playAt(std::string_view name, int sample, const Vec3& position, const Vec3&) {
    Set* s = find(name);
    if (!s || sample < 0 || static_cast<std::size_t>(sample) >= s->samples.size())
        return;
    s->position = position;
    if (!s->audio.withinMaxDistance(position, m_listener.m3))
        return;
    s->attenuation = s->audio.attenuation();
    s->pan = s->audio.pan(m_listener, position);
    s->doppler = 1.0f;
    playOneShot(*s, static_cast<std::size_t>(sample));
}

void CityAmbience::setLoop(std::string_view name, int sample, int id, bool on, const Vec3& position,
                           const Vec3&) {
    Set* s = find(name);
    if (!s || sample < 0 || static_cast<std::size_t>(sample) >= s->def.samples.size() || !m_mixer || !m_bank)
        return;
    MovingLoop* loop = nullptr;
    for (auto& m : m_moving)
        if (str::iequals(m.set, name) && m.sample == sample && m.id == id)
            loop = &m;
    if (!on) {
        if (loop)
            loop->slot.stop();
        return;
    }
    if (!loop) {
        m_moving.push_back(MovingLoop{std::string(name), sample, id, {}});
        loop = &m_moving.back();
        const auto& wave = s->def.samples[static_cast<std::size_t>(sample)].wave;
        loop->slot.load(*m_mixer, *m_bank, wave, Bus::Effects);
    }
    const auto& d = s->def.samples[static_cast<std::size_t>(sample)];
    Audio3D audio;
    audio.setDropOffs(s->def.minDistance, s->def.maxDistance);
    if (!audio.withinMaxDistance(position, m_listener.m3)) {
        loop->slot.stop();
        return;
    }
    loop->slot.setVolume(audio.attenuation() * d.volume);
    loop->slot.setPan(audio.pan(m_listener, position));
    if (!loop->slot.playing())
        loop->slot.playLoop();
}

void CityAmbience::stop() {
    for (auto& s : m_sets) {
        for (auto& smp : s->samples)
            smp.slot.stop();
        s->releaseSlot();
    }
    for (auto& m : m_moving)
        m.slot.stop();
}

// --- RainAudio ---------------------------------------------------------------------

namespace {
// mmRainAudio: initial volumes; ShelterOff / ShelterOn volumes; thunder timing.
constexpr float kExteriorVolume = 0.82f, kInteriorVolume = 0.85f;
constexpr float kOpenExteriorVolume = 0.76f, kOpenInteriorVolume = 0.83f;
constexpr float kShelterExteriorVolume = 0.65f, kShelterThunderVolume = 0.85f;
constexpr float kFlashTime = 13.0f, kThunderTime = 15.0f, kSecondThunderDelay = 1.0f;
} // namespace

void RainAudio::load(SoundBank& bank, Mixer& mixer, bool night) {
    m_night = night;
    m_exterior.load(mixer, bank, "Rainexterior", Bus::Effects);
    m_exterior.setVolume(kExteriorVolume);
    m_interior.load(mixer, bank, "Raininterior", Bus::Effects);
    m_interior.setVolume(kInteriorVolume);
    m_thunder = SoundSlot();
    m_thunder2 = SoundSlot();
    if (night) {
        m_thunder.load(mixer, bank, "Thunder", Bus::Effects);
        m_thunder.setVolume(1.0f);
        m_thunder.setPan(-0.2f);
        m_thunder2.load(mixer, bank, "Thunder", Bus::Effects);
        m_thunder2.setVolume(1.0f);
        m_thunder2.setPan(0.2f);
        m_thunder2.setPitch(0.8f);
    }
    m_interiorOn = m_sheltered = false;
    m_timer = 0;
    m_flashState = 0;
    m_thunderState = false;
}

void RainAudio::shelter(bool on) {
    if (on) {
        // ShelterOn.
        m_exterior.setVolume(kShelterExteriorVolume);
        m_interior.setVolume(0.0f);
        m_thunder.setVolume(kShelterThunderVolume);
        m_thunder2.setVolume(kShelterThunderVolume);
        m_thunder.setPan(0.0f);
        m_thunder2.setPan(0.0f);
    } else {
        // ShelterOff. Its thunder pans of -20 and 20 are clamped to -1 and 1
        // (audObject::SetPan), so after the first shelter the claps are hard
        // left and hard right instead of +-0.2.
        m_exterior.setVolume(kOpenExteriorVolume);
        m_interior.setVolume(kOpenInteriorVolume);
        m_thunder.setVolume(1.0f);
        m_thunder2.setVolume(1.0f);
        m_thunder.setPan(-20.0f);
        m_thunder2.setPan(20.0f);
    }
    m_sheltered = on;
}

void RainAudio::update(bool raining, bool interior, bool sheltered, float dt) {
    m_flash = false;
    if (!raining) {
        stop();
        return;
    }
    // SetInterior.
    if (interior != m_interiorOn) {
        SoundSlot& on = interior ? m_interior : m_exterior;
        SoundSlot& off = interior ? m_exterior : m_interior;
        on.playLoop();
        off.stop();
        m_interiorOn = interior;
    }
    // Update.
    if (sheltered != m_sheltered)
        shelter(sheltered);
    SoundSlot& current = m_interiorOn ? m_interior : m_exterior;
    if (!current.playing())
        current.playLoop();
    if (!m_night) {
        m_timer = 0;
        return;
    }
    if (m_flashState == 0 && m_timer > kFlashTime) {
        m_flashState = 1;
        m_flash = true;
    } else if (m_flashState == 1) {
        m_flashState = 2;
    }
    if (kThunderTime <= m_timer && !m_thunderState) {
        if (!m_thunder.playing())
            m_thunder.playOnce();
        m_thunderState = true;
        m_timer = 0;
    }
    if (kSecondThunderDelay < m_timer && m_thunderState) {
        if (!m_thunder2.playing())
            m_thunder2.playOnce();
        m_thunderState = false;
        m_timer = 0;
        m_flashState = 0;
    }
    m_timer += dt;
}

void RainAudio::stop() {
    m_exterior.stop();
    m_interior.stop();
    m_thunder.stop();
    m_thunder2.stop();
    m_timer = 0;
    m_flashState = 0;
    m_thunderState = false;
}

} // namespace mm2::audio::game
