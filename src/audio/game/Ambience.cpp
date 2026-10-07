// City ambience and rain. Rain volumes and thunder timing are ported from
// MM1's mmRainAudio (Open1560, GPL-3.0, Copyright (C) 2020 Brick:
// code/midtown/game.asm); the ambient emitter logic is OpenMM2's reading of
// MM2's aud/ambient tables (inferred, see docs/audio.md).
#include "audio/game/Ambience.h"

#include "core/Log.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <format>

namespace mm2::audio::game {

// --- CityAmbience -----------------------------------------------------------------

Vec3 CityAmbience::nearestPoint(const AmbientSoundSet& set, const Vec3& p) {
    if (set.points.empty())
        return p;
    Vec3 best = set.points.front();
    float bestD = best.dist2(p);
    if (set.audibleArea == 2 && set.points.size() >= 2) {
        for (std::size_t i = 0; i + 1 < set.points.size(); ++i) {
            const Vec3 a = set.points[i], b = set.points[i + 1];
            const Vec3 ab = b - a;
            const float len2 = ab.mag2();
            const float t = len2 > 0 ? std::clamp((p - a).dot(ab) / len2, 0.0f, 1.0f) : 0.0f;
            const Vec3 q = a + ab * t;
            if (const float d = q.dist2(p); d < bestD) {
                bestD = d;
                best = q;
            }
        }
        return best;
    }
    for (const auto& q : set.points) {
        if (const float d = q.dist2(p); d < bestD) {
            bestD = d;
            best = q;
        }
    }
    return best;
}

float CityAmbience::nextInterval(const AmbientSampleDef& d) {
    const float lo = std::max(d.intervalLow, 0.0f), hi = std::max(d.intervalHigh, lo);
    std::uniform_real_distribution<float> dist(lo, hi > lo ? hi : lo + 0.001f);
    return dist(m_rng);
}

CityAmbience::Set* CityAmbience::find(std::string_view name) {
    for (auto& s : m_sets)
        if (str::iequals(s.def.name, name))
            return &s;
    return nullptr;
}

const AmbientSoundSet* CityAmbience::set(std::string_view name) const {
    for (const auto& s : m_sets)
        if (str::iequals(s.def.name, name))
            return &s.def;
    return nullptr;
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
    Set s;
    s.def = std::move(*def);
    s.samples.resize(s.def.samples.size());
    for (std::size_t i = 0; i < s.samples.size(); ++i) {
        s.samples[i].slot.load(mixer, bank, s.def.samples[i].wave, Bus::Ambient, s.def.priority / 4);
        s.samples[i].timer = nextInterval(s.def.samples[i]);
    }
    m_sets.push_back(std::move(s));
    return static_cast<int>(m_sets.size()) - 1;
}

bool CityAmbience::load(const vfs::Vfs& vfs, SoundBank& bank, Mixer& mixer, std::string_view city) {
    stop();
    m_sets.clear();
    m_moving.clear();
    m_mixer = &mixer;
    m_bank = &bank;
    auto text = readText(vfs, std::format("aud/ambient/{}ambientcontainer.csv", str::lower(city)));
    if (!text)
        return false;
    for (const auto& name : parseAmbientContainer(*text)) {
        if (loadSet(vfs, bank, mixer, name) >= 0)
            m_sets.back().background = true;
    }
    return !m_sets.empty();
}

void CityAmbience::update(const Vec3& listener, float dt) {
    for (auto& s : m_sets) {
        if (!s.background)
            continue;
        const Vec3 near = nearestPoint(s.def, listener);
        const bool inRange = near.dist(listener) <= s.def.maxDistance;
        for (std::size_t i = 0; i < s.samples.size(); ++i) {
            const auto& d = s.def.samples[i];
            auto& smp = s.samples[i];
            if (!d.active || !smp.slot.valid())
                continue;
            Emitter3D e;
            e.minDistance = std::max(s.def.minDistance, 0.5f);
            e.maxDistance = std::max(s.def.maxDistance, e.minDistance);
            switch (d.type) {
            case AmbientSampleType::Loop:
                if (inRange) {
                    e.position = near;
                    smp.slot.playLoop(d.volume, 1.0f, &e);
                } else {
                    smp.slot.stop();
                }
                break;
            case AmbientSampleType::RandomOnce:
            case AmbientSampleType::Interval: {
                smp.timer -= dt;
                if (smp.timer > 0)
                    break;
                smp.timer = nextInterval(d);
                if (!inRange)
                    break;
                if (d.type == AmbientSampleType::RandomOnce && s.def.points.size() > 1) {
                    // A random emitter point within earshot (falls back to the nearest).
                    std::vector<Vec3> candidates;
                    for (const auto& p : s.def.points)
                        if (p.dist(listener) <= s.def.maxDistance)
                            candidates.push_back(p);
                    if (candidates.empty())
                        candidates.push_back(near);
                    std::uniform_int_distribution<std::size_t> pick(0, candidates.size() - 1);
                    e.position = candidates[pick(m_rng)];
                } else {
                    e.position = near;
                }
                smp.slot.playOnce(d.volume, 1.0f, &e);
                break;
            }
            case AmbientSampleType::Triggered: break;
            }
        }
    }
}

void CityAmbience::playAt(std::string_view name, int sample, const Vec3& position, const Vec3& velocity) {
    Set* s = find(name);
    if (!s || sample < 0 || static_cast<std::size_t>(sample) >= s->samples.size())
        return;
    Emitter3D e;
    e.position = position;
    e.velocity = s->def.samples[static_cast<std::size_t>(sample)].doppler ? velocity : Vec3{};
    e.minDistance = std::max(s->def.minDistance, 0.5f);
    e.maxDistance = std::max(s->def.maxDistance, e.minDistance);
    s->samples[static_cast<std::size_t>(sample)].slot.playOnce(s->def.samples[static_cast<std::size_t>(sample)].volume,
                                                              1.0f, &e);
}

void CityAmbience::setLoop(std::string_view name, int sample, int id, bool on, const Vec3& position,
                           const Vec3& velocity) {
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
        loop->slot.load(*m_mixer, *m_bank, s->def.samples[static_cast<std::size_t>(sample)].wave, Bus::Ambient,
                        s->def.priority / 4);
    }
    const auto& d = s->def.samples[static_cast<std::size_t>(sample)];
    Emitter3D e;
    e.position = position;
    e.velocity = d.doppler ? velocity : Vec3{};
    e.minDistance = std::max(s->def.minDistance, 0.5f);
    e.maxDistance = std::max(s->def.maxDistance, e.minDistance);
    loop->slot.playLoop(d.volume, 1.0f, &e);
}

void CityAmbience::stop() {
    for (auto& s : m_sets)
        for (auto& smp : s.samples)
            smp.slot.stop();
    for (auto& m : m_moving)
        m.slot.stop();
}

// --- RainAudio ---------------------------------------------------------------------

namespace {
// mmRainAudio constants (game.asm): initial volumes 0.82 / 0.85, shelter off
// 0.76 / 0.83, shelter on 0.65 / 0; flash at 13 s, thunder at 15 s.
constexpr float kExteriorVolume = 0.76f, kInteriorVolume = 0.83f;
constexpr float kShelterExteriorVolume = 0.65f;
constexpr float kFlashTime = 13.0f, kThunderTime = 15.0f;
} // namespace

void RainAudio::load(SoundBank& bank, Mixer& mixer) {
    m_exterior.load(mixer, bank, "Rainexterior", Bus::Ambient, 3);
    m_interior.load(mixer, bank, "Raininterior", Bus::Ambient, 3);
    m_thunder.load(mixer, bank, "Thunder", Bus::Ambient, 3);
    m_thunder2.load(mixer, bank, "Thunder", Bus::Ambient, 3);
}

void RainAudio::update(bool raining, bool interior, bool sheltered, bool storm, float dt) {
    m_flash = false;
    if (!raining) {
        stop();
        return;
    }
    const float ext = sheltered ? kShelterExteriorVolume : kExteriorVolume;
    const float in = sheltered ? 0.0f : kInteriorVolume;
    if (interior) {
        m_exterior.stop();
        m_interior.playLoop(in, 1.0f);
    } else {
        m_interior.stop();
        m_exterior.playLoop(ext, 1.0f);
    }
    if (!storm)
        return;
    m_timer += dt;
    if (m_flashState == 0 && m_timer > kFlashTime) {
        m_flashState = 1;
        m_flash = true;
    } else if (m_flashState == 1) {
        m_flashState = 2; // flash lasts one update, as DoFlash
    }
    if (m_timer >= kThunderTime && !m_thundered) {
        // Two layered thunder claps; MM1 pans them apart (0.2 right, played
        // at 0.8 speed) unless sheltered.
        m_thunder.playOnce(1.0f, 1.0f);
        m_thunder2.playOnce(sheltered ? 0.85f : 1.0f, 0.8f);
        m_thundered = true;
    }
    if (m_thundered) {
        m_timer = 0;
        m_flashState = 0;
        m_thundered = false;
    }
}

void RainAudio::stop() {
    m_exterior.stop();
    m_interior.stop();
    m_thunder.stop();
    m_thunder2.stop();
    m_timer = 0;
    m_flashState = 0;
}

} // namespace mm2::audio::game
