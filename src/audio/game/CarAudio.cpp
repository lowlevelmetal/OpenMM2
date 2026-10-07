// Car sounds, ported in structure from MM1 (Open1560, GPL-3.0, Copyright (C)
// 2020 Brick: mmPlayerCarAudio, EngineAudio, mmSurfaceAudio, mmImpactAudio,
// mmPoliceCarAudio, mmOpponentCarAudio in code/midtown/game.asm) and driven
// by MM2's aud/cardata tables. See docs/audio.md for what is ported and
// what is inferred.
#include "audio/game/CarAudio.h"

#include "core/Log.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <format>

namespace mm2::audio::game {
namespace {

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

// Unit ramp of x over [a, b]; a step at a when the range is empty.
float ramp(float x, float a, float b) {
    if (b > a)
        return clamp01((x - a) / (b - a));
    return x >= a ? 1.0f : 0.0f;
}

// Engine speed below which MM1's EngineAudio considers a clutch state
// change ("gear" stored as gear + 1: 0 reverse, 1 neutral, 2 first).
int gearState(int gear) { return gear + 1; }

// MM1 mmPlayerCarAudio::Update constants (game.asm flt_63CA44 / dbl_61CB50 /
// flt_61CB3C).
constexpr float kAirBlowMaxSpeed = 0.04f;
constexpr float kSurfaceMinSpeed = 2.0f;
constexpr float kSkidMinSpeed = 1.0f;
// Ice tables give speed ranges for skids, so a slip threshold is needed to
// decide when to skid at all (inferred: the lowest dry/wet threshold).
constexpr float kIceSkidSlip = 0.25f;

std::optional<CarAudioDef> loadCarDef(const vfs::Vfs& vfs, const std::string& path, std::string* error) {
    auto text = readText(vfs, path);
    if (!text) {
        if (error)
            *error = std::format("{} not found", path);
        return std::nullopt;
    }
    return parseCarAudio(*text, error);
}

template <class T, class Fn>
std::optional<T> loadTable(const vfs::Vfs& vfs, const std::string& path, Fn parse) {
    auto text = readText(vfs, path);
    if (!text) {
        log::debug("audio: {} not found", path);
        return std::nullopt;
    }
    return parse(*text);
}

const char* surfaceFile(SurfaceWeather w) {
    switch (w) {
    case SurfaceWeather::Wet: return "default_surfacewet.csv";
    case SurfaceWeather::Snow: return "default_surfaceice.csv";
    default: return "default_surfacedry.csv";
    }
}

Emitter3D emitterFor(const Mat34& transform, const Vec3& velocity, float minDistance, float maxDistance) {
    Emitter3D e;
    e.position = transform.m3;
    e.velocity = velocity;
    e.minDistance = minDistance;
    e.maxDistance = maxDistance;
    return e;
}

} // namespace

int surfaceSoundIndex(std::string_view name, int mtlSound) {
    if (str::iequals(name, "cobblestone"))
        return 3;
    if (str::iequals(name, "flagstone"))
        return 4;
    if (str::iequals(name, "grass"))
        return 2;
    if (str::iequals(name, "water") || str::iequals(name, "deepwater"))
        return 1;
    return std::max(mtlSound, 0);
}

std::string carAudioPath(const vfs::Vfs& vfs, std::string_view folder, std::string_view car) {
    const std::string specific = std::format("aud/cardata/{}/{}.csv", folder, str::lower(car));
    if (vfs.exists(specific))
        return specific;
    return std::format("aud/cardata/{}/default.csv", folder);
}

// --- EngineSound --------------------------------------------------------------------

EngineSound::Evaluation EngineSound::evaluate(const EngineSampleDef& d, float rpm) {
    Evaluation e;
    if (rpm < d.fadeInStartRpm || rpm > d.fadeOutEndRpm)
        return e;
    const float fadeIn = ramp(rpm, d.fadeInStartRpm, d.fadeInEndRpm);
    const float fadeOut = d.fadeOutEndRpm > d.fadeOutStartRpm ? 1.0f - ramp(rpm, d.fadeOutStartRpm, d.fadeOutEndRpm)
                                                              : 1.0f;
    const float fade = std::min(fadeIn, fadeOut);
    e.volume = d.minVolume + (d.maxVolume - d.minVolume) * fade;
    e.pitch = d.minPitch + (d.maxPitch - d.minPitch) * ramp(rpm, d.pitchStartRpm, d.pitchEndRpm);
    e.audible = e.volume >= kSilentVolume;
    return e;
}

void EngineSound::load(Mixer& mixer, SoundBank& bank, const std::vector<EngineSampleDef>& samples, Bus bus) {
    stop();
    m_defs = samples;
    m_samples.clear();
    m_samples.resize(samples.size());
    m_states.assign(samples.size(), {});
    for (std::size_t i = 0; i < samples.size(); ++i)
        m_samples[i].load(mixer, bank, samples[i].wave, bus, 5); // AudSound priority 0x15 in MM1
}

void EngineSound::update(float rpm, const Emitter3D* emitter, float volumeOffset) {
    for (std::size_t i = 0; i < m_defs.size(); ++i) {
        Evaluation e = evaluate(m_defs[i], rpm);
        e.volume += volumeOffset;
        e.audible = e.volume >= kSilentVolume;
        m_states[i] = e;
        // UpdateRPM: stop below the cut-off, (re)start looping above it.
        if (e.audible)
            m_samples[i].playLoop(e.volume, e.pitch, emitter);
        else if (m_samples[i].playing())
            m_samples[i].stop();
    }
}

void EngineSound::stop() {
    for (auto& s : m_samples)
        s.stop();
}

// --- SurfaceSounds ------------------------------------------------------------------

int SurfaceSounds::chooseSkid(const SurfaceSoundDef& def, float value) {
    if (def.skids.empty() || value < def.skids.front().min)
        return -1;
    for (std::size_t i = 0; i < def.skids.size(); ++i)
        if (value >= def.skids[i].min && value < def.skids[i].max)
            return static_cast<int>(i);
    // At or beyond the top of the last range (slip clamps at 1).
    return value >= def.skids.back().min ? static_cast<int>(def.skids.size()) - 1 : -1;
}

float SurfaceSounds::skidVolumeFor(const SurfaceSoundDef& def, float slip) {
    if (def.skids.empty())
        return def.minSkidVolume;
    const float lo = def.skids.front().min;
    return def.minSkidVolume + (def.maxSkidVolume - def.minSkidVolume) * ramp(slip, lo, 1.0f);
}

void SurfaceSounds::load(Mixer& mixer, SoundBank& bank, const SurfaceTable& table, Bus bus) {
    stop();
    m_table = table;
    m_loaded = true;
    m_entries.clear();
    m_entries.resize(table.surfaces.size());
    for (std::size_t i = 0; i < table.surfaces.size(); ++i) {
        const auto& def = table.surfaces[i];
        m_entries[i].surface.load(mixer, bank, def.wave, bus, 3);
        m_entries[i].skids.resize(def.skids.size());
        for (std::size_t k = 0; k < def.skids.size(); ++k)
            m_entries[i].skids[k].load(mixer, bank, def.skids[k].wave, bus, 3);
    }
    m_surface = 0;
}

void SurfaceSounds::stop() {
    for (auto& e : m_entries) {
        e.surface.stop();
        for (auto& s : e.skids)
            s.stop();
    }
    m_skidEntry = m_skidSample = -1;
}

void SurfaceSounds::update(const CarAudioInputs& in, const Emitter3D* emitter) {
    if (!m_loaded || m_entries.empty())
        return;
    const SurfaceTable& table = m_table;
    int grounded = 0;
    float slip = 0.0f;
    for (const auto& w : in.wheels) {
        if (!w.onGround)
            continue;
        ++grounded;
        slip = std::max(slip, std::abs(w.slip));
    }
    slip = std::min(slip, 1.0f);

    // Surface type: the first wheel's, unless either front wheel still
    // reports the current one (UpdateSurface keeps it). Tunnels use the
    // table's tunnel entry.
    int surface = m_surface;
    if (in.inTunnel) {
        surface = table.tunnelIndex;
    } else if (in.wheels[0].surface != m_surface && in.wheels[1].surface != m_surface) {
        surface = in.wheels[0].surface;
    }
    if (surface < 0 || static_cast<std::size_t>(surface) >= m_entries.size())
        surface = 0;
    if (surface != m_surface) {
        m_entries[static_cast<std::size_t>(m_surface)].surface.stop();
        m_surface = surface;
    }
    const SurfaceSoundDef& def = table.surfaces[static_cast<std::size_t>(m_surface)];
    Entry& entry = m_entries[static_cast<std::size_t>(m_surface)];

    // Skid.
    int skid = -1;
    float skidVolume = 0;
    if (in.speed > kSkidMinSpeed && grounded > 0) {
        if (table.ice) {
            if (slip >= kIceSkidSlip) {
                skid = chooseSkid(def, in.speed);
                const float div = def.skidVolumeDivisor > 0 ? def.skidVolumeDivisor : 1.0f;
                skidVolume = std::clamp(def.minSkidVolume + slip / div, def.minSkidVolume, def.maxSkidVolume);
            }
        } else {
            skid = chooseSkid(def, slip);
            skidVolume = skidVolumeFor(def, slip);
        }
    }
    if (m_skidEntry >= 0 && (m_skidEntry != m_surface || m_skidSample != skid)) {
        auto& old = m_entries[static_cast<std::size_t>(m_skidEntry)];
        if (m_skidSample >= 0 && static_cast<std::size_t>(m_skidSample) < old.skids.size())
            old.skids[static_cast<std::size_t>(m_skidSample)].stop();
        m_skidEntry = m_skidSample = -1;
    }
    if (skid >= 0 && static_cast<std::size_t>(skid) < entry.skids.size() && entry.skids[static_cast<std::size_t>(skid)].valid()) {
        entry.skids[static_cast<std::size_t>(skid)].playLoop(skidVolume, 1.0f, emitter);
        m_skidEntry = m_surface;
        m_skidSample = skid;
        m_skidVolume = skidVolume;
    } else {
        m_skidVolume = 0;
    }

    // Rolling surface.
    const bool rolling = in.speed > kSurfaceMinSpeed && grounded >= 2 && m_skidSample < 0 && def.hasSurfaceSound();
    if (!rolling) {
        entry.surface.stop();
        return;
    }
    float volume, pitch;
    if (table.ice) {
        const float vdiv = def.volumeDivisor > 0 ? def.volumeDivisor : 1.0f;
        const float pdiv = def.pitchDivisor > 0 ? def.pitchDivisor : 1.0f;
        volume = std::clamp(def.minVolume + in.speed / vdiv * 0.01f, def.minVolume, def.maxVolume);
        pitch = std::clamp(def.minPitch + in.speed / pdiv, def.minPitch, def.maxPitch);
    } else {
        const float t = clamp01(in.speed / std::max(def.maxSpeed, 0.01f));
        volume = def.minVolume + (def.maxVolume - def.minVolume) * t;
        pitch = def.minPitch + (def.maxPitch - def.minPitch) * t;
    }
    entry.surface.playLoop(volume, pitch, emitter);
}

// --- ImpactSounds ---------------------------------------------------------------------

float ImpactSounds::volumeFor(const ImpactSampleDef& s, float force) {
    return s.minVolume + (s.maxVolume - s.minVolume) * ramp(force, s.minForce, s.maxForce);
}

void ImpactSounds::load(Mixer& mixer, SoundBank& bank, const ImpactTable& table, Bus bus) {
    stop();
    m_bangers.clear();
    for (const auto& b : table.bangers) {
        Banger out;
        out.id = b.id;
        out.defs = b.samples;
        out.slots.resize(b.samples.size());
        for (std::size_t i = 0; i < b.samples.size(); ++i)
            out.slots[i].load(mixer, bank, b.samples[i].wave, bus, 4);
        m_bangers.push_back(std::move(out));
    }
}

void ImpactSounds::play(const ImpactInput& impact, const Emitter3D* emitter) {
    Banger* banger = nullptr;
    for (auto& b : m_bangers)
        if (b.id == impact.audioId)
            banger = &b;
    if (!banger)
        for (auto& b : m_bangers)
            if (b.id == 0)
                banger = &b; // unknown ids sound like a wall
    if (!banger)
        return;
    for (std::size_t i = 0; i < banger->defs.size(); ++i) {
        const auto& d = banger->defs[i];
        if (impact.force < d.minForce || impact.force >= d.maxForce)
            continue;
        auto& slot = banger->slots[i];
        if (slot.playing())
            continue;
        slot.playOnce(volumeFor(d, impact.force), d.frequency, emitter);
        m_lastPlayed = banger->id;
    }
}

void ImpactSounds::stop() {
    for (auto& b : m_bangers)
        for (auto& s : b.slots)
            s.stop();
}

// --- SirenPlayer ------------------------------------------------------------------------

void SirenPlayer::load(Mixer& mixer, SoundBank& bank, const SirenTable& table, Bus bus) {
    stop();
    m_table = table;
    m_slots.clear();
    m_slots.resize(table.samples.size());
    for (std::size_t i = 0; i < table.samples.size(); ++i)
        m_slots[i].load(mixer, bank, table.samples[i].wave, bus, 6);
    m_explosion.load(mixer, bank, table.explosion, bus, 6);
}

void SirenPlayer::update(bool on, bool wrecked, float dt, const Emitter3D* emitter) {
    if (wrecked && !m_wasWrecked && m_explosion.valid())
        m_explosion.playOnce(m_table.explosionVolume, 1.0f, emitter);
    m_wasWrecked = wrecked;
    if (!on || wrecked || m_slots.empty()) {
        if (m_current >= 0)
            m_slots[static_cast<std::size_t>(m_current)].stop();
        m_current = -1;
        return;
    }
    auto start = [&](int index) {
        if (m_current >= 0)
            m_slots[static_cast<std::size_t>(m_current)].stop();
        m_current = std::clamp(index, 0, static_cast<int>(m_slots.size()) - 1);
        const auto& def = m_table.samples[static_cast<std::size_t>(m_current)];
        m_timer = 0;
        if (!def.steps.empty()) {
            std::uniform_int_distribution<std::size_t> pick(0, def.steps.size() - 1);
            const auto& step = def.steps[pick(m_rng)];
            m_timer = step.playTime;
            m_next = step.next;
        } else {
            m_next = m_current;
        }
        m_slots[static_cast<std::size_t>(m_current)].playLoop(def.volume, 1.0f, emitter);
    };
    if (m_current < 0) {
        start(0);
        return;
    }
    m_timer -= dt;
    if (m_timer <= 0 && m_table.samples[static_cast<std::size_t>(m_current)].steps.size() > 0)
        start(m_next);
    else
        m_slots[static_cast<std::size_t>(m_current)].playLoop(m_table.samples[static_cast<std::size_t>(m_current)].volume,
                                                              1.0f, emitter);
}

void SirenPlayer::stop() {
    for (auto& s : m_slots)
        s.stop();
    m_explosion.stop();
    m_current = -1;
}

// --- PlayerCarAudio ---------------------------------------------------------------------

bool PlayerCarAudio::load(const vfs::Vfs& vfs, SoundBank& bank, Mixer& mixer, std::string_view car,
                          const CarAudioOptions& options, std::string* error) {
    stop();
    auto def = loadCarDef(vfs, carAudioPath(vfs, "player", car), error);
    if (!def)
        return false;
    m_def = std::move(*def);
    m_engine.load(mixer, bank, m_def.engine, Bus::Engine);
    m_horn.load(mixer, bank, m_def.horn, Bus::Effects, 5);
    m_clutch.load(mixer, bank, m_def.clutch, Bus::Engine, 5);

    if (auto t = loadTable<ImpactTable>(vfs, "aud/cardata/player/default_impacts.csv",
                                        [](std::string_view s) { return parseImpactTable(s); }))
        m_impacts.load(mixer, bank, *t, Bus::Effects);
    if (auto t = loadTable<SurfaceTable>(vfs, std::format("aud/cardata/player/{}", surfaceFile(options.weather)),
                                         [](std::string_view s) { return parseSurfaceTable(s); }))
        m_surfaces.load(mixer, bank, *t, Bus::Effects);
    m_suspensionDef = loadTable<SuspensionDef>(vfs, "aud/cardata/player/suspensionaudio.csv", parseSuspension);
    if (m_suspensionDef)
        m_suspension.load(mixer, bank, m_suspensionDef->wave, Bus::Effects, 2);
    m_wobbleDef = loadTable<TireWobbleDef>(vfs, "aud/cardata/player/tirewobble.csv", parseTireWobble);
    if (m_wobbleDef)
        m_wobble.load(mixer, bank, m_wobbleDef->wave, Bus::Effects, 2);

    VehicleTypes types;
    if (auto text = readText(vfs, "aud/cardata/shared/vehtypes.csv"))
        types = parseVehicleTypes(*text);
    const bool freight = types.isFreight(car) || (m_def.flags & CarAudioFlag::Freight);
    if (freight) {
        m_semi = loadTable<SemiDef>(vfs, "aud/cardata/shared/semidata.csv", parseSemiData);
        if (m_semi) {
            m_reverseBeep.load(mixer, bank, m_semi->reverse, Bus::Effects, 4);
            m_airBlow.load(mixer, bank, m_semi->airBlow, Bus::Effects, 4);
        }
    }
    m_siren.reset();
    // Police sirens: the cop car (flag 4). Other cars listed as police in
    // vehtypes.csv but with a siren horn (the fire truck, flag 8) use that.
    if ((m_def.flags & CarAudioFlag::Police) || (types.isPolice(car) && !(m_def.flags & CarAudioFlag::SirenHorn))) {
        const std::string path = std::format("aud/cardata/player/{}policesiren.csv", str::lower(options.city));
        if (auto t = loadTable<SirenTable>(vfs, path, [](std::string_view s) { return parseSirenTable(s); })) {
            m_siren.emplace();
            m_siren->load(mixer, bank, *t, Bus::Effects);
        }
    }
    m_prevGearState = gearState(1);
    m_airBlown = m_prevHorn = m_hornLatched = false;
    return true;
}

void PlayerCarAudio::update(const CarAudioInputs& in, float dt) {
    // Engine (EngineAudio::UpdateRPM).
    const float rpm = in.engineRunning ? std::max(in.rpm, in.idleRpm) : 0.0f;
    m_engine.update(rpm);

    // Clutch: EngineAudio plays it when the gear state jumps directly
    // between reverse (0) and first (2).
    const int state = gearState(in.gear);
    if (((m_prevGearState == 0 && state == 2) || (m_prevGearState == 2 && state == 0)) && m_clutch.valid())
        m_clutch.playOnce(m_def.clutchVolume);
    m_prevGearState = state;

    // Freight vehicles: reverse beeper while in reverse, air brake once the
    // vehicle stops under braking (mmPlayerCarAudio::Update).
    if (m_semi) {
        if (in.gear < 0)
            m_reverseBeep.playLoop(m_semi->reverseVolume, 1.0f);
        else
            m_reverseBeep.stop();
        const bool braking = in.brake > 0.1f;
        if (braking && in.speed <= kAirBlowMaxSpeed && !m_airBlown) {
            m_airBlow.playOnce(m_semi->airBlowVolume);
            m_airBlown = true;
        } else if (!braking) {
            m_airBlown = false;
        }
    }

    m_surfaces.update(in);
    for (const auto& impact : in.impacts)
        m_impacts.play(impact);

    // Horn: held for normal horns; the fire truck's siren horn toggles on
    // each press (inferred from flag 8 and its looping sample).
    if (m_def.flags & CarAudioFlag::SirenHorn) {
        if (in.horn && !m_prevHorn)
            m_hornLatched = !m_hornLatched;
        if (m_hornLatched)
            m_horn.playLoop(m_def.hornVolume, 1.0f);
        else
            m_horn.stop();
    } else if (in.horn) {
        m_horn.playLoop(m_def.hornVolume, 1.0f);
    } else {
        m_horn.stop();
    }
    m_prevHorn = in.horn;

    if (m_siren)
        m_siren->update(in.siren, in.wrecked, dt);

    // Suspension thumps (inferred: one-shot when a wheel compresses faster
    // than the table's minimum velocity, louder up to the maximum).
    if (m_suspensionDef && m_suspension.valid()) {
        float v = 0;
        for (const auto& w : in.wheels)
            if (w.onGround)
                v = std::max(v, w.suspensionSpeed);
        if (v > m_suspensionDef->minVelocity && !m_suspension.playing()) {
            const float t = ramp(v, m_suspensionDef->minVelocity, m_suspensionDef->maxVelocity);
            m_suspension.playOnce(m_suspensionDef->minVolume + (m_suspensionDef->maxVolume - m_suspensionDef->minVolume) * t);
        }
    }

    // Tyre wobble (MM1 mmSurfaceAudio::UpdateTireWobble; pitch from speed
    // over the table's divisor, inferred).
    if (m_wobbleDef && m_wobble.valid()) {
        if (in.tireWobble > 0.0f && in.speed > kSkidMinSpeed) {
            const auto& w = *m_wobbleDef;
            const float vol = w.minVolume + (w.maxVolume - w.minVolume) * clamp01(in.tireWobble);
            const float pitch = std::clamp(w.minPitch + in.speed / std::max(w.pitchDivisor, 0.01f), w.minPitch, w.maxPitch);
            m_wobble.playLoop(vol, pitch);
        } else {
            m_wobble.stop();
        }
    }
}

void PlayerCarAudio::stop() {
    m_engine.stop();
    m_surfaces.stop();
    m_impacts.stop();
    for (auto* s : {&m_horn, &m_clutch, &m_reverseBeep, &m_airBlow, &m_suspension, &m_wobble})
        s->stop();
    if (m_siren)
        m_siren->stop();
}

// --- OpponentCarAudio ---------------------------------------------------------------------

bool OpponentCarAudio::load(const vfs::Vfs& vfs, SoundBank& bank, Mixer& mixer, std::string_view car, bool police,
                            const CarAudioOptions& options, std::string* error) {
    stop();
    auto def = loadCarDef(vfs, carAudioPath(vfs, "opponent", car), error);
    if (!def)
        return false;
    m_def = std::move(*def);
    m_engine.load(mixer, bank, m_def.engine, Bus::Engine);
    m_horn.load(mixer, bank, m_def.horn, Bus::Effects, 2);
    if (auto t = loadTable<ImpactTable>(vfs, "aud/cardata/opponent/default_impacts.csv",
                                        [](std::string_view s) { return parseImpactTable(s); }))
        m_impacts.load(mixer, bank, *t, Bus::Effects);
    if (auto t = loadTable<SurfaceTable>(vfs, std::format("aud/cardata/opponent/{}", surfaceFile(options.weather)),
                                         [](std::string_view s) { return parseSurfaceTable(s); }))
        m_surfaces.load(mixer, bank, *t, Bus::Effects);
    m_siren.reset();
    if (police) {
        if (auto t = loadTable<SirenTable>(vfs, "aud/cardata/opponent/policesiren.csv",
                                           [](std::string_view s) { return parseSirenTable(s); })) {
            m_siren.emplace();
            m_siren->load(mixer, bank, *t, Bus::Effects);
        }
    }
    return true;
}

void OpponentCarAudio::update(const CarAudioInputs& in, float dt, const Vec3& listener) {
    const bool audible = in.transform.m3.dist2(listener) < maxDistance * maxDistance;
    if (!audible) {
        if (m_audible)
            stop();
        m_audible = false;
        return;
    }
    m_audible = true;
    const Emitter3D e = emitterFor(in.transform, in.velocity, minDistance, maxDistance);
    const float rpm = in.engineRunning ? std::max(in.rpm, in.idleRpm) : 0.0f;
    m_engine.update(rpm, &e);
    m_surfaces.update(in, &e);
    for (const auto& impact : in.impacts) {
        Emitter3D ie = e;
        ie.position = impact.position;
        m_impacts.play(impact, &ie);
    }
    if (in.horn)
        m_horn.playLoop(m_def.hornVolume, 1.0f, &e);
    else
        m_horn.stop();
    if (m_siren)
        m_siren->update(in.siren, in.wrecked, dt, &e);
}

void OpponentCarAudio::stop() {
    m_engine.stop();
    m_surfaces.stop();
    m_impacts.stop();
    m_horn.stop();
    if (m_siren)
        m_siren->stop();
}

// --- AmbientCarAudio ----------------------------------------------------------------------

float AmbientCarAudio::pitchFor(const AmbientEngineDef& def, float speed) {
    speed = std::abs(speed);
    for (const auto& b : def.bands) {
        if (speed >= b.minSpeed && speed < b.maxSpeed)
            return b.minPitch + (b.maxPitch - b.minPitch) * ramp(speed, b.minSpeed, b.maxSpeed);
    }
    return def.bands.empty() ? 1.0f : def.bands.back().maxPitch;
}

bool AmbientCarAudio::load(const vfs::Vfs& vfs, SoundBank& bank, Mixer& mixer, std::string_view type) {
    stop();
    auto find = [&](std::string_view suffix) -> std::string {
        const std::string t = str::lower(type);
        std::vector<std::string> candidates = {std::format("aud/cardata/ambient/{}_{}.csv", t, suffix)};
        // va_sedans_s -> va_sedan_s (the audio files use the singular).
        if (auto us = t.rfind('_'); us != std::string::npos && us > 0 && t[us - 1] == 's')
            candidates.push_back(std::format("aud/cardata/ambient/{}{}_{}.csv", t.substr(0, us - 1), t.substr(us), suffix));
        candidates.push_back(std::format("aud/cardata/ambient/default_{}.csv", suffix));
        for (const auto& c : candidates)
            if (vfs.exists(c))
                return c;
        return {};
    };
    if (auto text = readText(vfs, find("engine")))
        if (auto def = parseAmbientEngine(*text))
            m_engineDef = std::move(*def);
    if (auto text = readText(vfs, find("horn")))
        m_hornDef = parseHorn(*text);
    m_engine.load(mixer, bank, m_engineDef.wave, Bus::Engine, 1);
    if (m_hornDef)
        m_horn.load(mixer, bank, m_hornDef->wave, Bus::Effects, 1);
    return m_engine.valid();
}

void AmbientCarAudio::honk(int pattern) {
    if (!m_hornDef || m_hornDef->patterns.empty())
        return;
    if (pattern < 0) {
        std::uniform_int_distribution<int> pick(0, static_cast<int>(m_hornDef->patterns.size()) - 1);
        pattern = pick(m_rng);
    }
    m_pattern = std::clamp(pattern, 0, static_cast<int>(m_hornDef->patterns.size()) - 1);
    m_beep = 0;
    m_beepTimer = 0;
    m_beepOn = false;
}

void AmbientCarAudio::impact(float force) {
    if (m_hornDef && force >= m_hornDef->stuckImpactForce)
        m_stuck = true;
}

void AmbientCarAudio::update(float speed, const Mat34& transform, const Vec3& velocity, float dt, const Vec3& listener) {
    if (transform.m3.dist2(listener) > maxDistance * maxDistance) {
        m_engine.stop();
        m_horn.stop();
        return;
    }
    const Emitter3D e = emitterFor(transform, velocity, minDistance, maxDistance);
    m_engine.playLoop(m_engineDef.volume, pitchFor(m_engineDef, speed), &e);

    if (!m_hornDef)
        return;
    if (m_stuck) {
        m_horn.playLoop(m_hornDef->volume, m_hornDef->pitch, &e);
        return;
    }
    if (m_pattern < 0) {
        m_horn.stop();
        return;
    }
    const auto& beeps = m_hornDef->patterns[static_cast<std::size_t>(m_pattern)].beeps;
    m_beepTimer -= dt;
    while (m_beepTimer <= 0) {
        if (m_beep >= beeps.size() * 2) {
            m_pattern = -1;
            m_horn.stop();
            return;
        }
        const auto& [on, off] = beeps[m_beep / 2];
        m_beepOn = (m_beep % 2) == 0;
        m_beepTimer += m_beepOn ? on : off;
        ++m_beep;
        if (m_beepOn && on <= 0)
            m_beepOn = false;
    }
    if (m_beepOn)
        m_horn.playLoop(m_hornDef->volume, m_hornDef->pitch, &e);
    else
        m_horn.stop();
}

void AmbientCarAudio::stop() {
    m_engine.stop();
    m_horn.stop();
    m_pattern = -1;
    m_stuck = false;
}

} // namespace mm2::audio::game
