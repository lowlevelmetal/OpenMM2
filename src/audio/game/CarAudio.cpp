// Car sounds, ported from MM2 (vehCarAudioContainer, vehCarAudio,
// vehEngineAudio, vehEngineSampleWrapper, vehSurfaceAudio,
// vehSurfaceAudioData, vehPoliceCarAudio, vehSemiCarAudio, AudImpact,
// AudImpactData, aiAmbientVehicleAudio, aiEngineAudio, vehHornAudio,
// vehHornAudioTiming). See docs/audio.md.
#include "audio/game/CarAudio.h"

#include "core/Log.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace mm2::audio::game {
namespace {

// vehSurfaceAudio::UpdateSurface: no rolling sound at or below 2 m/s.
constexpr float kSurfaceMinSpeed = 2.0f;
// vehSemiCarAudio::UpdateAirBlow: the air brake hisses once the truck is
// (almost) stopped.
constexpr float kAirBlowMaxSpeed = 0.04f;
// vehPoliceCarAudio::UpdateSiren: positioned sirens never drop below this.
constexpr float kSirenMinVolume = 0.75f;
// vehPoliceCarAudio::UpdateExplosion.
constexpr float kExplosionMinVolume = 0.85f;
// aiAmbientVehicleAudio::UpdateAudio: a speed drop above this per update is
// a crash, not braking; the engine pitch then decays by 5% per update.
constexpr float kAmbientSpeedDrop = 4.0f;
// vehHornAudio::PlayImpact: RandomizeNumber(10) must reach 7.5.
constexpr float kImpactHornChance = 7.5f;

// vehSemiCarAudio's air brake latch is a single global in MM2, shared by
// every semi (DAT_006b0088).
bool g_airBlown = false;
// vehPoliceCarAudio::s_iNumCopsPursuingPlayer.
int g_copsPursuingPlayer = 0;

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

// vehCarAudio::Init: rain picks the wet table, every other weather (snow
// included) the dry one; default_surfaceice.csv is never loaded. The tables
// always come from aud/cardata/player, for opponents too.
const char* surfaceFile(SurfaceWeather w) {
    return w == SurfaceWeather::Wet ? "aud/cardata/player/default_surfacewet.csv"
                                    : "aud/cardata/player/default_surfacedry.csv";
}

VehicleTypes loadVehicleTypes(const vfs::Vfs& vfs) {
    if (auto text = readText(vfs, "aud/cardata/shared/vehtypes.csv"))
        return parseVehicleTypes(*text);
    return {};
}

// vehCarAudio::Init loads suspension and tyre wobble for every car.
void loadWheelExtras(const vfs::Vfs& vfs, SoundBank& bank, Mixer& mixer, SurfaceSounds& surfaces) {
    if (auto d = loadTable<SuspensionDef>(vfs, "aud/cardata/player/suspensionaudio.csv", parseSuspension))
        surfaces.loadSuspension(mixer, bank, *d, Bus::Effects);
    if (auto d = loadTable<TireWobbleDef>(vfs, "aud/cardata/player/tirewobble.csv", parseTireWobble))
        surfaces.loadTireWobble(mixer, bank, *d, Bus::Effects);
}

int mm2Gear(int gear) { return gear + 1; } // MM2: 0 reverse, 1 neutral, 2 first

} // namespace

float impactStrength(const Vec3& impulse) { return std::abs(impulse.x) + std::abs(impulse.y) + std::abs(impulse.z); }

int surfaceSoundIndex(std::string_view, int mtlSound) { return std::max(mtlSound, 0); }

std::string carAudioPath(const vfs::Vfs& vfs, std::string_view folder, std::string_view car) {
    const std::string specific = std::format("aud/cardata/{}/{}.csv", folder, str::lower(car));
    if (vfs.exists(specific))
        return specific;
    return std::format("aud/cardata/{}/default.csv", folder);
}

// --- EngineSound --------------------------------------------------------------------

EngineSound::Evaluation EngineSound::evaluate(const EngineSampleDef& d, float rpm) {
    Evaluation e;
    // ParseCSVBuffer precomputes the slopes (0 for an empty range).
    const float inRange = d.fadeInEndRpm - d.fadeInStartRpm;
    const float outRange = d.fadeOutEndRpm - d.fadeOutStartRpm;
    const float inSlope = (d.maxVolume - d.minVolume) * (inRange != 0.0f ? 1.0f / inRange : 0.0f);
    const float outSlope = (d.maxVolume - d.minVolume) * (outRange != 0.0f ? 1.0f / outRange : 0.0f);
    // CalculateVolume.
    if (rpm <= d.fadeInStartRpm || d.fadeOutEndRpm <= rpm)
        e.volume = d.minVolume;
    else if (rpm < d.fadeInEndRpm)
        e.volume = (rpm - d.fadeInStartRpm) * inSlope + d.minVolume;
    else if (rpm <= d.fadeOutStartRpm)
        e.volume = d.maxVolume;
    else
        e.volume = (d.fadeOutEndRpm - rpm) * outSlope + d.minVolume;
    // CalculatePitch.
    const float pitchRange = d.pitchEndRpm - d.pitchStartRpm;
    const float pitchSlope = (d.maxPitch - d.minPitch) * (pitchRange != 0.0f ? 1.0f / pitchRange : 0.0f);
    if (rpm <= d.pitchStartRpm)
        e.pitch = d.minPitch;
    else if (d.pitchEndRpm <= rpm)
        e.pitch = d.maxPitch;
    else
        e.pitch = rpm * pitchSlope + d.minPitch;
    e.audible = e.volume >= kSilentVolume;
    return e;
}

void EngineSound::load(Mixer& mixer, SoundBank& bank, const std::vector<EngineSampleDef>& samples, Bus bus) {
    stop();
    m_defs = samples;
    m_samples.clear();
    m_samples.resize(samples.size());
    m_states.assign(samples.size(), {});
    m_silenced = false;
    for (std::size_t i = 0; i < samples.size(); ++i)
        m_samples[i].load(mixer, bank, samples[i].wave, bus, 5);
}

void EngineSound::silence(bool on) { m_silenced = on; }

void EngineSound::update(float rpm, float volumeScale, float pitchScale, float pan) {
    for (std::size_t i = 0; i < m_defs.size(); ++i) {
        EngineSampleDef def = m_defs[i];
        if (m_silenced)
            def.minVolume = def.maxVolume = 0.0f; // vehEngineSampleWrapper::Silence
        Evaluation e = evaluate(def, rpm);
        m_states[i] = e;
        // vehEngineSampleWrapper::UpdateRPM: the cut-off uses the table volume.
        if (!e.audible) {
            if (m_samples[i].playing())
                m_samples[i].stop();
            continue;
        }
        m_samples[i].setPan(pan);
        m_samples[i].playLoop(e.volume * volumeScale, e.pitch * pitchScale);
    }
}

void EngineSound::stop() {
    for (auto& s : m_samples)
        s.stop();
}

// --- SurfaceSounds ------------------------------------------------------------------

bool SurfaceSounds::skidInRange(const SkidSampleDef& skid, float slip) { return skid.min <= slip && slip <= skid.max; }

float SurfaceSounds::skidVolumeFor(const SurfaceSoundDef& def, float slip) {
    return slip * (def.maxSkidVolume - def.minSkidVolume) + def.minSkidVolume;
}

float SurfaceSounds::surfaceVolumeFor(const SurfaceSoundDef& def, float speed) {
    if (def.maxSpeed <= speed)
        return def.maxVolume;
    const float inv = def.maxSpeed != 0.0f ? 1.0f / def.maxSpeed : 0.0f;
    return speed * ((def.maxVolume - def.minVolume) * inv) + def.minVolume;
}

float SurfaceSounds::surfacePitchFor(const SurfaceSoundDef& def, float speed) {
    if (def.maxSpeed <= speed)
        return def.maxPitch;
    const float inv = def.maxSpeed != 0.0f ? 1.0f / def.maxSpeed : 0.0f;
    return speed * ((def.maxPitch - def.minPitch) * inv) + def.minPitch;
}

void SurfaceSounds::load(Mixer& mixer, SoundBank& bank, const SurfaceTable& table, Bus bus) {
    stop();
    m_entries.clear();
    m_entries.resize(table.surfaces.size());
    for (std::size_t i = 0; i < table.surfaces.size(); ++i) {
        Entry& e = m_entries[i];
        e.def = table.surfaces[i];
        e.surface.load(mixer, bank, e.def.wave, bus, 3);
        e.skids.resize(e.def.skids.size());
        for (std::size_t k = 0; k < e.def.skids.size(); ++k)
            e.skids[k].load(mixer, bank, e.def.skids[k].wave, bus, 3);
    }
    m_tunnelIndex = table.tunnelIndex;
    m_surface = 0;
    m_previous = -1;
    m_skidding = false;
    m_airborne = false;
}

void SurfaceSounds::loadSuspension(Mixer& mixer, SoundBank& bank, const SuspensionDef& def, Bus bus) {
    m_suspensionDef = def;
    m_suspension.load(mixer, bank, def.wave, bus, 2);
}

void SurfaceSounds::loadTireWobble(Mixer& mixer, SoundBank& bank, const TireWobbleDef& def, Bus bus) {
    m_wobbleDef = def;
    m_wobble.load(mixer, bank, def.wave, bus, 2);
    m_wobbleDistance = 0.0f;
}

SurfaceSounds::Entry* SurfaceSounds::entry(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= m_entries.size())
        return nullptr;
    return &m_entries[static_cast<std::size_t>(index)];
}

void SurfaceSounds::stopSurface(int index) {
    if (Entry* e = entry(index))
        e->surface.stop();
}

void SurfaceSounds::stopSkid(int index) {
    if (Entry* e = entry(index))
        for (auto& s : e->skids)
            s.stop();
}

bool SurfaceSounds::skidPlayingOn(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= m_entries.size())
        return false;
    for (const auto& s : m_entries[static_cast<std::size_t>(index)].skids)
        if (s.playing())
            return true;
    return false;
}

bool SurfaceSounds::skidPlaying(int k) const {
    if (m_surface < 0 || static_cast<std::size_t>(m_surface) >= m_entries.size())
        return false;
    const auto& skids = m_entries[static_cast<std::size_t>(m_surface)].skids;
    return k >= 0 && static_cast<std::size_t>(k) < skids.size() && skids[static_cast<std::size_t>(k)].playing();
}

bool SurfaceSounds::surfaceChanged(const CarAudioInputs& in) const {
    // Either front wheel still on the current surface keeps it.
    return in.wheels[0].surface != m_surface && in.wheels[1].surface != m_surface;
}

void SurfaceSounds::selectSurface(const CarAudioInputs& in, bool positioned) {
    if (in.inTunnel) {
        m_surface = m_tunnelIndex;
        return;
    }
    if (!surfaceChanged(in))
        return;
    if (positioned)
        stopSurface(m_surface);
    m_surface = surfaceSoundIndex({}, in.wheels[0].surface);
    if (!positioned && m_previous != m_surface)
        stopSurface(m_previous);
    if (static_cast<int>(m_entries.size()) <= m_surface)
        m_surface = 0;
}

void SurfaceSounds::stop() {
    for (auto& e : m_entries) {
        e.surface.stop();
        for (auto& s : e.skids)
            s.stop();
    }
    m_suspension.stop();
    m_wobble.stop();
    m_skidding = false;
}

void SurfaceSounds::updateSurface(const CarAudioInputs& in, bool positioned, float attenuation, float pan) {
    int grounded = 0;
    for (const auto& w : in.wheels)
        grounded += w.onGround ? 1 : 0;
    if (grounded < 2) {
        stopSurface(m_surface);
        if (positioned)
            return;
        if (grounded == 0) {
            // UpdateAir: "big air" once the ground is 3 m or more below.
            if (in.groundBelow && *in.groundBelow >= 3.0f && *in.groundBelow <= 33.0f)
                m_airborne = true;
        } else {
            m_airborne = false;
        }
        return;
    }
    m_airborne = false;
    if (in.speed > kSurfaceMinSpeed && !skidPlayingOn(m_surface)) {
        selectSurface(in, positioned);
        Entry* e = entry(m_surface);
        if (e && e->surface.valid()) {
            e->surface.setPan(pan);
            e->surface.playLoop(surfaceVolumeFor(e->def, in.speed) * attenuation, surfacePitchFor(e->def, in.speed));
            m_previous = m_surface;
            return;
        }
    }
    stopSurface(m_surface);
}

void SurfaceSounds::updateSkid(const CarAudioInputs& in, float attenuation, float pan) {
    float slip = 0.0f;
    for (const auto& w : in.wheels)
        slip = std::max(slip, w.slip);
    if (in.inTunnel) {
        m_surface = m_tunnelIndex;
    } else if (surfaceChanged(in)) {
        stopSkid(m_surface);
        m_surface = surfaceSoundIndex({}, in.wheels[0].surface);
        if (static_cast<int>(m_entries.size()) <= m_surface)
            m_surface = 0;
    }
    Entry* e = entry(m_surface);
    if (slip > 0.0f) {
        if (e) {
            for (std::size_t k = 0; k < e->skids.size(); ++k) {
                auto& s = e->skids[k];
                if (!skidInRange(e->def.skids[k], slip)) {
                    if (s.playing())
                        s.stop();
                    continue;
                }
                s.setPan(pan);
                s.playLoop(skidVolumeFor(e->def, slip) * attenuation, 1.0f);
            }
        }
        m_skidding = true;
        m_previous = m_surface;
        return;
    }
    if (m_skidding) {
        stopSkid(m_surface);
        m_skidding = false;
    }
    m_previous = m_surface;
}

void SurfaceSounds::updateSuspension(const CarAudioInputs& in, bool positioned, float attenuation, float pan) {
    if (!m_suspensionDef || !m_suspension.valid())
        return;
    const auto& d = *m_suspensionDef;
    float average = 0.0f;
    int grounded = 0;
    for (const auto& w : in.wheels) {
        average += w.suspensionSpeed;
        grounded += w.onGround ? 1 : 0;
    }
    average *= 0.25f;
    if (std::abs(d.minVelocity) > average || grounded < 2 || m_suspension.playing())
        return;
    const float inv = d.volumeDivisor != 0.0f ? 1.0f / d.volumeDivisor : 0.0f;
    const float volume = std::clamp(average * inv, d.minVolume, std::max(d.minVolume, d.maxVolume));
    if (positioned)
        m_suspension.setPan(pan);
    m_suspension.playOnce(volume * attenuation);
}

void SurfaceSounds::updateTireWobble(const CarAudioInputs& in, float dt, bool positioned, float attenuation,
                                     float pan) {
    if (!m_wobbleDef || !m_wobble.valid())
        return;
    float damage = in.tireWobble;
    if (damage <= 0.05f)
        return;
    damage = std::min(damage, 1.0f);
    // One thump per revolution of the rear left wheel.
    const float travelled = in.speed * dt + m_wobbleDistance;
    const float circumference = in.wheelRadius * 6.28318f;
    if (travelled < circumference) {
        m_wobbleDistance = travelled;
        return;
    }
    if (m_wobble.playing())
        return; // the distance is not reset: the thump comes when the last one ends
    const auto& d = *m_wobbleDef;
    const float volume = std::clamp(damage, d.minVolume, std::max(d.minVolume, d.maxVolume));
    const float inv = d.pitchDivisor != 0.0f ? 1.0f / d.pitchDivisor : 0.0f;
    if (positioned) {
        m_wobble.setPan(pan);
        m_wobble.playOnce(volume * attenuation, m_wobble.pitch());
    } else {
        const float pitch = std::clamp(in.speed * inv, d.minPitch, std::max(d.minPitch, d.maxPitch));
        m_wobble.playOnce(volume, pitch);
    }
    m_wobbleDistance = 0.0f;
}

void SurfaceSounds::update(const CarAudioInputs& in, float dt) {
    if (m_entries.empty())
        return;
    updateSuspension(in, false, 1.0f, 0.0f);
    updateSurface(in, false, 1.0f, 0.0f);
    updateSkid(in, 1.0f, 0.0f);
    updateTireWobble(in, dt, false, 1.0f, 0.0f);
}

void SurfaceSounds::update3D(const CarAudioInputs& in, float dt, float attenuation, float pan) {
    if (m_entries.empty())
        return;
    updateSuspension(in, true, attenuation, pan);
    updateSurface(in, true, attenuation, pan);
    updateSkid(in, attenuation, pan);
    updateTireWobble(in, dt, true, attenuation, pan);
}

// --- ImpactSounds ---------------------------------------------------------------------

float ImpactSounds::volumeFor(const ImpactSampleDef& s, float force) {
    const float range = s.maxForce - s.minForce;
    const float slope = (s.maxVolume - s.minVolume) * (range != 0.0f ? 1.0f / range : 0.0f);
    return slope * force + s.minVolume;
}

void ImpactSounds::load(Mixer& mixer, SoundBank& bank, const ImpactTable& table, Bus bus) {
    stop();
    m_bangers.clear();
    for (const auto& b : table.bangers) {
        Banger out;
        out.defs = b.samples;
        out.slots.resize(b.samples.size());
        out.volumes.assign(b.samples.size(), 0.0f);
        for (std::size_t i = 0; i < b.samples.size(); ++i)
            out.slots[i].load(mixer, bank, b.samples[i].wave, bus, 4);
        m_bangers.push_back(std::move(out));
    }
    m_last = -1;
}

void ImpactSounds::play(const ImpactInput& impact, float attenuation, float pan) {
    if (m_bangers.empty())
        return;
    int index = impact.audioId;
    if (index < 0 || static_cast<std::size_t>(index) >= m_bangers.size())
        index = 0;
    m_last = index;
    Banger& b = m_bangers[static_cast<std::size_t>(index)];
    for (std::size_t i = 0; i < b.defs.size(); ++i) {
        const auto& d = b.defs[i];
        if (impact.force < d.minForce || d.maxForce < impact.force)
            continue;
        auto& slot = b.slots[i];
        if (!slot.valid() || slot.playing())
            continue;
        const float volume = volumeFor(d, impact.force);
        slot.setPan(pan);
        slot.playOnce(volume * attenuation, d.frequency);
        b.volumes[i] = volume;
    }
}

void ImpactSounds::updateAttenuation(float attenuation, float pan) {
    if (m_last < 0 || static_cast<std::size_t>(m_last) >= m_bangers.size())
        return;
    Banger& b = m_bangers[static_cast<std::size_t>(m_last)];
    for (std::size_t i = 0; i < b.slots.size(); ++i) {
        if (!b.slots[i].playing())
            continue;
        b.slots[i].setVolume(attenuation * b.volumes[i]);
        b.slots[i].setPan(pan);
    }
}

void ImpactSounds::stop() {
    for (auto& b : m_bangers)
        for (auto& s : b.slots)
            s.stop();
}

// --- SirenPlayer ------------------------------------------------------------------------

int SirenPlayer::copsPursuingPlayer() { return g_copsPursuingPlayer; }
void SirenPlayer::resetPursuitCount() { g_copsPursuingPlayer = 0; }

void SirenPlayer::load(Mixer& mixer, SoundBank& bank, const SirenTable& table, Bus bus) {
    stopAll();
    m_samples.clear();
    m_samples.resize(table.samples.size());
    for (std::size_t i = 0; i < table.samples.size(); ++i) {
        m_samples[i].def = table.samples[i];
        m_samples[i].slot.load(mixer, bank, table.samples[i].wave, bus, 6);
        m_samples[i].slot.setVolume(table.samples[i].volume); // AssignSounds
        m_samples[i].step = 0;
    }
    m_explosion.load(mixer, bank, table.explosion, bus, 6);
    m_current = 0;
    m_state = 0;
    m_timer = 0;
    m_damaged = false;
    m_damageTime = 0.01f;
    // Every vehPoliceCarAudio constructor resets the pursuit count.
    g_copsPursuingPlayer = 0;
}

void SirenPlayer::start(bool pursuingPlayer, bool audible, float attenuation, float doppler) {
    if (m_state != 0 || m_samples.empty())
        return;
    m_state = pursuingPlayer ? 1 : 2;
    Sample& s = m_samples[static_cast<std::size_t>(m_current)];
    m_volume = s.def.volume;
    if (m_state == 1)
        ++g_copsPursuingPlayer;
    if (audible && !s.slot.playing())
        s.slot.playLoop(attenuation * s.def.volume, doppler);
}

void SirenPlayer::stop() {
    if (g_copsPursuingPlayer > 0 && m_state == 1)
        --g_copsPursuingPlayer;
    m_state = 0;
    if (!m_samples.empty())
        m_samples[static_cast<std::size_t>(m_current)].slot.stop();
}

void SirenPlayer::silence() {
    for (auto& s : m_samples)
        s.slot.stop();
    m_explosion.stop();
}

void SirenPlayer::stopAll() {
    stop();
    silence();
}

void SirenPlayer::fluctuate(float dt) {
    if (m_samples.size() == 1)
        return;
    Sample& s = m_samples[static_cast<std::size_t>(m_current)];
    if (s.def.steps.empty())
        return;
    const SirenStep step = s.def.steps[s.step];
    if (step.playTime <= m_timer) {
        s.step = (s.step + 1) % s.def.steps.size();
        s.slot.stop();
        m_current = std::clamp(step.next, 0, static_cast<int>(m_samples.size()) - 1);
        Sample& next = m_samples[static_cast<std::size_t>(m_current)];
        next.slot.playLoop(next.slot.volume(), next.slot.pitch());
        m_timer = 0.0f;
    }
    m_timer += dt;
}

void SirenPlayer::damage(float attenuation, float doppler) {
    Sample& s = m_samples[static_cast<std::size_t>(m_current)];
    float pitch;
    if (m_damageTime > 1.0f) {
        if (m_damageTime > 1.75f) {
            if (m_volume > 0.2f) {
                const float old = m_volume;
                m_volume = old * 0.5f;
                s.slot.setVolume(attenuation * old * 0.5f);
                return;
            }
            s.slot.stop();
            m_damaged = false;
            m_state = 0;
            return;
        }
        pitch = 0.45f;
    } else {
        pitch = 0.5f;
    }
    s.slot.setVolume(attenuation * s.def.volume);
    s.slot.setPitch(doppler * pitch);
    m_damageTime += m_dt;
}

void SirenPlayer::update(float dt) {
    if (m_state == 0 || m_samples.empty())
        return;
    m_dt = dt;
    fluctuate(dt);
    // The player's car never explodes (only aiPoliceOfficer::PerpEscapes
    // calls PlayExplosion); a non-positioned object's attenuation is 0.
    if (m_damaged)
        damage(0.0f, 1.0f);
    if (m_state == 0)
        return;
    Sample& s = m_samples[static_cast<std::size_t>(m_current)];
    if (!s.slot.playing())
        s.slot.playLoop(s.slot.volume(), s.slot.pitch());
}

void SirenPlayer::update3D(float dt, float attenuation, float doppler, float pan) {
    if (m_state == 0 || m_samples.empty())
        return;
    m_dt = dt;
    fluctuate(dt);
    Sample& s = m_samples[static_cast<std::size_t>(m_current)];
    if (!m_damaged) {
        s.slot.setVolume(std::max(s.def.volume * attenuation, kSirenMinVolume));
        s.slot.setPitch(doppler);
    } else {
        damage(attenuation, doppler);
        if (m_state == 0)
            return;
    }
    s.slot.setPan(pan);
    if (!s.slot.playing())
        s.slot.playLoop(s.slot.volume(), s.slot.pitch());
    // UpdateExplosion.
    if (m_explosion.playing()) {
        m_explosion.setVolume(std::max(attenuation, kExplosionMinVolume));
        m_explosion.setPan(pan);
    }
}

void SirenPlayer::explode(bool audible, float attenuation) {
    if (audible && m_explosion.valid() && !m_explosion.playing())
        m_explosion.playOnce(attenuation * 1.0f);
    if (m_samples.empty())
        return;
    m_volume = m_samples[static_cast<std::size_t>(m_current)].def.volume;
    // The pursuit count drops here and again in the StopSiren that follows
    // (aiPoliceOfficer::PerpEscapes): MM2 counts an exploding cop twice.
    if (g_copsPursuingPlayer > 0 && m_state == 1)
        --g_copsPursuingPlayer;
    if (audible && m_samples[static_cast<std::size_t>(m_current)].slot.valid()) {
        m_damaged = true;
        m_damageTime = 0.01f;
        m_dt = 0.0f;
        damage(attenuation, 1.0f);
    } else {
        m_state = 0;
    }
}

// --- PlayerCarAudio ---------------------------------------------------------------------

bool PlayerCarAudio::load(const vfs::Vfs& vfs, SoundBank& bank, Mixer& mixer, std::string_view car,
                          const CarAudioOptions& options, std::string* error) {
    stop();
    // vehCarAudio::Load falls back to default.csv.
    auto def = loadCarDef(vfs, carAudioPath(vfs, "player", car), error);
    if (!def)
        return false;
    m_def = std::move(*def);
    m_engine.load(mixer, bank, m_def.engine, Bus::Engine);
    m_horn.load(mixer, bank, m_def.horn, Bus::Effects, 5);
    m_horn.setVolume(m_def.hornVolume); // SetNon3DParams
    m_clutch.load(mixer, bank, m_def.clutch, Bus::Engine, 5);
    m_clutch.setVolume(m_def.clutchVolume);

    if (auto t = loadTable<SurfaceTable>(vfs, surfaceFile(options.weather),
                                         [](std::string_view s) { return parseSurfaceTable(s); }))
        m_surfaces.load(mixer, bank, *t, Bus::Effects);
    loadWheelExtras(vfs, bank, mixer, m_surfaces);
    if (auto t = loadTable<ImpactTable>(vfs, "aud/cardata/player/default_impacts.csv",
                                        [](std::string_view s) { return parseImpactTable(s); }))
        m_impacts.load(mixer, bank, *t, Bus::Effects);

    // vehCarAudioContainer: the sound class comes from vehtypes.csv, semis first.
    const VehicleTypes types = loadVehicleTypes(vfs);
    m_semi.reset();
    m_siren.reset();
    m_reverseBeep = SoundSlot();
    m_airBlow = SoundSlot();
    if (types.isFreight(car)) {
        m_semi = loadTable<SemiDef>(vfs, "aud/cardata/shared/semidata.csv", parseSemiData);
        if (m_semi) {
            m_reverseBeep.load(mixer, bank, m_semi->reverse, Bus::Effects, 4);
            m_airBlow.load(mixer, bank, m_semi->airBlow, Bus::Effects, 4);
            // vehSemiCarAudio::SetNon3DParams gives the air brake the reverse
            // beeper's volume.
            m_reverseBeep.setVolume(m_semi->reverseVolume);
            m_airBlow.setVolume(m_semi->reverseVolume);
        }
    } else if (types.isPolice(car)) {
        const std::string path = std::format("aud/cardata/player/{}policesiren.csv", str::lower(options.city));
        if (auto t = loadTable<SirenTable>(vfs, path, [](std::string_view s) { return parseSirenTable(s); })) {
            m_siren.emplace();
            m_siren->load(mixer, bank, *t, Bus::Effects);
        }
    }
    m_prevGear = -1;
    m_hornPressed = false;
    g_airBlown = false;
    return true;
}

void PlayerCarAudio::updateHorn(bool pressed) {
    // mmGame::UpdateHorn. A car with siren lights (every police-list car in
    // the retail data: vpcop, vpsemi) toggles its siren on each press; the
    // container never plays a police car's horn.
    if (!pressed) {
        if (!m_siren && m_hornPressed && m_horn.playing())
            m_horn.stop();
        m_hornPressed = false;
        return;
    }
    if (!m_hornPressed) {
        if (m_siren) {
            if (m_siren->on())
                m_siren->stop();
            else
                m_siren->start(false);
        } else if (m_horn.valid() && !m_horn.playing()) {
            m_horn.playLoop(m_def.hornVolume, 1.0f); // PlayHorn restarts from the beginning
        }
    }
    m_hornPressed = true;
}

void PlayerCarAudio::update(const CarAudioInputs& in, float dt) {
    // vehCarAudio::UpdateGear: the clutch sample plays whenever the gear
    // changes into or out of reverse (MM2 gear 0). The first update counts as
    // a change from "no gear" (-1), so starting in reverse plays it too.
    const int gear = mm2Gear(in.gear);
    if (m_clutch.valid() && ((m_prevGear == 0) != (gear == 0)))
        m_clutch.playOnce(m_def.clutchVolume);
    m_prevGear = gear;

    const float rpm = in.engineRunning ? std::max(in.rpm, in.idleRpm) : 0.0f;
    m_engine.update(rpm);
    m_surfaces.update(in, dt);
    for (const auto& impact : in.impacts)
        m_impacts.play(impact);

    if (m_semi) {
        // vehSemiCarAudio::UpdateReverse.
        if (gear == 0) {
            if (!m_reverseBeep.playing())
                m_reverseBeep.playLoop(m_semi->reverseVolume, 1.0f);
        } else if (m_reverseBeep.playing()) {
            m_reverseBeep.stop();
        }
        // UpdateAirBlow: "braking" is vehSurfaceAudio::IsBrakeing, which tests
        // the front wheels' BrakeCoef tuning (> 0.5), not the pedal. Every
        // retail semi has 0.5, so the air brake never sounds.
        const bool braking = in.wheels[0].brakeCoef > 0.5f && in.wheels[1].brakeCoef > 0.5f;
        if (in.speed > kAirBlowMaxSpeed) {
            if (!braking)
                g_airBlown = false;
        } else if (braking) {
            if (!g_airBlown) {
                m_airBlow.playOnce(m_semi->reverseVolume);
                g_airBlown = true;
            }
        } else {
            g_airBlown = false;
        }
    }

    updateHorn(in.horn);
    if (m_siren)
        m_siren->update(dt);
}

void PlayerCarAudio::stop() {
    m_engine.stop();
    m_surfaces.stop();
    m_impacts.stop();
    for (auto* s : {&m_horn, &m_clutch, &m_reverseBeep, &m_airBlow})
        s->stop();
    if (m_siren)
        m_siren->stopAll();
}

// --- OpponentCarAudio ---------------------------------------------------------------------

bool OpponentCarAudio::load(const vfs::Vfs& vfs, SoundBank& bank, Mixer& mixer, std::string_view car, bool police,
                            const CarAudioOptions& options, std::string* error) {
    stop();
    setManager(options.manager);
    auto def = loadCarDef(vfs, carAudioPath(vfs, "opponent", car), error);
    if (!def)
        return false;
    m_def = std::move(*def);
    m_engine.load(mixer, bank, m_def.engine, Bus::Engine);
    // vehCarAudio::Load: positioned cars have no clutch sample, and a horn
    // only in vehCarAudioContainer mode 0 (network players).
    m_hasHorn = options.horn;
    m_horn = SoundSlot();
    if (m_hasHorn)
        m_horn.load(mixer, bank, m_def.horn, Bus::Effects, 2);
    if (auto t = loadTable<SurfaceTable>(vfs, surfaceFile(options.weather),
                                         [](std::string_view s) { return parseSurfaceTable(s); }))
        m_surfaces.load(mixer, bank, *t, Bus::Effects);
    loadWheelExtras(vfs, bank, mixer, m_surfaces);
    if (auto t = loadTable<ImpactTable>(vfs, "aud/cardata/opponent/default_impacts.csv",
                                        [](std::string_view s) { return parseImpactTable(s); }))
        m_impacts.load(mixer, bank, *t, Bus::Effects);

    // vehCarAudioContainer::InitPolice for vehtypes.csv's police list (the
    // caller's flag also counts): the city's siren table from the player
    // folder; aud/cardata/opponent/policesiren.csv is never used.
    m_siren.reset();
    const VehicleTypes types = loadVehicleTypes(vfs);
    const bool isPolice = police || (!types.isFreight(car) && types.isPolice(car));
    if (isPolice) {
        const std::string path = std::format("aud/cardata/player/{}policesiren.csv", str::lower(options.city));
        if (auto t = loadTable<SirenTable>(vfs, path, [](std::string_view s) { return parseSirenTable(s); })) {
            m_siren.emplace();
            m_siren->load(mixer, bank, *t, Bus::Effects);
        }
    }
    // vehCarAudio::Init priority 9, vehPoliceCarAudio::Init 7.
    m_priority = m_siren ? 7 : 9;
    m_3d = Audio3D();
    m_3d.setDropOffs(0.0f, kMaxDistance);
    m_prevSiren = m_prevWrecked = false;
    return true;
}

void OpponentCarAudio::silence() {
    // vehCarAudio::UnAssignSounds: the sounds stop, the siren keeps its state.
    m_engine.stop();
    m_surfaces.stop();
    m_impacts.stop();
    m_horn.stop();
    if (m_siren)
        m_siren->silence();
    m_3d.resetDistance();
}

void OpponentCarAudio::update(const CarAudioInputs& in, float dt, const Vec3& listener) {
    Mat34 l = Mat34::identity();
    l.m3 = listener;
    update(in, dt, l);
}

void OpponentCarAudio::update(const CarAudioInputs& in, float dt, const Mat34& listener) {
    const Vec3 position = in.transform.m3;
    m_3d.updateDistance(position, listener.m3);

    // aiPoliceOfficer::StartSiren / StopSiren / PerpEscapes change the siren
    // state whether or not the car has a sound slot; only a car with a slot
    // starts the samples.
    if (m_siren) {
        const bool audible = hasSlot();
        if (in.siren && !m_prevSiren && !m_siren->on()) {
            m_priority = 10;
            m_3d.alwaysAudible = true;
            m_siren->start(true, audible, m_3d.attenuation(), m_doppler);
            if (audible)
                m_engine.stop(); // StartSiren silences a positioned car's engine
        }
        if (in.wrecked && !m_prevWrecked && m_siren->on()) {
            m_siren->explode(audible, m_3d.attenuation());
            m_siren->stop();
            m_priority = 7;
            m_3d.alwaysAudible = false;
        } else if (!in.siren && m_prevSiren) {
            m_siren->stop();
            m_priority = 7;
            m_3d.alwaysAudible = false;
        }
    }
    m_prevSiren = in.siren;
    m_prevWrecked = in.wrecked;

    // Aud3DObject::Update: ask for a slot while within the maximum distance.
    if (!hasSlot() && !acquireSlot(m_3d.withinMaxDistance()))
        return;
    // vehCarAudio::UpdateAudio3D / vehPoliceCarAudio::UpdateAudio3D.
    if (m_3d.pastMaxDistance() && !(m_siren && m_siren->explosionPlaying())) {
        releaseSlot();
        silence();
        return;
    }
    const float attenuation = m_3d.attenuation();
    const float pan = m_3d.pan(listener, position);
    const float doppler = m_3d.doppler(1.0f / kDopplerSpeed, dt);
    m_doppler = doppler;
    m_impacts.updateAttenuation(attenuation, pan);
    if (m_hasHorn && m_horn.valid()) {
        // PlayHorn / StopHorn from the network state; UpdateAudio3D sets the
        // volume, frequency and pan every update.
        m_horn.setPan(pan);
        if (in.horn)
            m_horn.playLoop(attenuation * m_def.hornVolume, doppler);
        else if (m_horn.playing())
            m_horn.stop();
    }

    // Surfaces, then either the engine or the siren.
    m_surfaces.update3D(in, dt, attenuation, pan);
    if (m_siren && m_siren->on()) {
        m_siren->update3D(dt, attenuation, doppler, pan);
    } else {
        const float rpm = in.engineRunning ? std::max(in.rpm, in.idleRpm) : 0.0f;
        m_engine.update(rpm, attenuation, doppler, pan);
    }
    for (const auto& impact : in.impacts)
        m_impacts.play(impact, attenuation, pan);
}

void OpponentCarAudio::stop() {
    releaseSlot();
    m_engine.stop();
    m_surfaces.stop();
    m_impacts.stop();
    m_horn.stop();
    if (m_siren)
        m_siren->stopAll();
}

// --- AmbientCarAudio ----------------------------------------------------------------------

std::optional<float> AmbientCarAudio::pitchFor(const AmbientEngineDef& def, float speed, bool slowing) {
    if (def.bands.empty())
        return std::nullopt;
    auto slope = [](const SpeedBand& b) {
        const float range = b.maxSpeed - b.minSpeed;
        return (b.maxPitch - b.minPitch) * (range != 0.0f ? 1.0f / range : 0.0f);
    };
    if (slowing) {
        const SpeedBand& last = def.bands.back();
        return speed * slope(last) + last.minPitch;
    }
    for (std::size_t i = 0; i + 1 < def.bands.size(); ++i) {
        const SpeedBand& b = def.bands[i];
        if (b.minSpeed <= speed && speed <= b.maxSpeed)
            return (speed - b.minSpeed) * slope(b) + b.minPitch;
    }
    return std::nullopt;
}

bool AmbientCarAudio::load(const vfs::Vfs& vfs, SoundBank& bank, Mixer& mixer, std::string_view type,
                           Object3DManager* manager) {
    stop();
    setManager(manager);
    auto find = [&](std::string_view suffix) -> std::string {
        const std::string t = str::lower(type);
        std::vector<std::string> candidates = {std::format("aud/cardata/ambient/{}_{}.csv", t, suffix)};
        // va_sedans_s -> va_sedan_s (the audio files use the singular; inferred).
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
    m_horn = SoundSlot();
    if (m_hornDef)
        m_horn.load(mixer, bank, m_hornDef->wave, Bus::Effects, 1);
    if (auto t = loadTable<ImpactTable>(vfs, "aud/cardata/opponent/default_impacts.csv",
                                        [](std::string_view s) { return parseImpactTable(s); }))
        m_impacts.load(mixer, bank, *t, Bus::Effects);
    m_3d = Audio3D();
    m_3d.setDropOffs(0.0f, kMaxDistance);
    m_pitch = 0.0f;
    m_speed = m_prevSpeed = 0.0f;
    m_hornState = 2;
    m_beep = 0;
    return m_engine.valid();
}

void AmbientCarAudio::startPattern(std::size_t index) {
    // vehHornAudioTiming::Play.
    m_pattern = index;
    m_beep = 0;
    m_hornTimer = 0.0f;
    m_hornState = 0;
    if (m_horn.valid())
        m_horn.playLoop(m_horn.volume(), m_horn.pitch());
}

bool AmbientCarAudio::honk(int pattern) {
    // Only a car holding a sound slot has its horn assigned.
    if (!hasSlot() || !m_hornDef || m_hornDef->patterns.empty() || !m_horn.valid() || m_hornState != 2)
        return false;
    const std::size_t n = m_hornDef->patterns.size();
    std::size_t choice;
    if (pattern >= 0) {
        choice = std::min(static_cast<std::size_t>(pattern), n - 1);
    } else {
        // PlayAvoidance: RandomizeNumber(2n - 0.01); n or more means no honk.
        std::uniform_real_distribution<float> u(0.0f, static_cast<float>(n * 2) - 0.01f);
        choice = static_cast<std::size_t>(u(m_rng));
        if (choice >= n)
            return false;
    }
    startPattern(choice);
    return true;
}

void AmbientCarAudio::impact(const ImpactInput& impact) {
    // aiVehicleActive: only a car holding a sound slot has its impact and
    // horn sounds assigned; they play with the last attenuation and pan.
    if (!hasSlot())
        return;
    m_impacts.play(impact, m_3d.attenuation(), m_pan);
    // vehHornAudio::PlayImpact: the last pattern, one time in four.
    if (!m_hornDef || m_hornDef->patterns.empty() || !m_horn.valid() || impact.force < m_hornDef->stuckImpactForce)
        return;
    std::uniform_real_distribution<float> u(0.0f, 10.0f);
    if (u(m_rng) < kImpactHornChance)
        return;
    const std::size_t last = m_hornDef->patterns.size() - 1;
    if (m_hornState != 2) {
        if (m_pattern == last)
            return;
        m_hornState = 2; // vehHornAudioTiming::Stop
        m_beep = 0;
        m_horn.stop();
    }
    startPattern(last);
}

void AmbientCarAudio::updateHorn(float dt) {
    // vehHornAudioTiming::Update.
    if (!m_hornDef || m_hornDef->patterns.empty())
        return;
    const auto& beeps = m_hornDef->patterns[m_pattern].beeps;
    if (m_hornState == 0) {
        m_hornTimer += dt;
        if (m_beep >= beeps.size() || beeps[m_beep].first <= m_hornTimer) {
            m_horn.stop();
            m_hornTimer = 0.0f;
            m_hornState = 1;
            return;
        }
        if (!m_horn.playing())
            m_horn.playLoop(m_horn.volume(), m_horn.pitch());
        return;
    }
    if (m_hornState == 1) {
        m_hornTimer += dt;
        if (m_beep >= beeps.size() || beeps[m_beep].second <= m_hornTimer) {
            if (m_beep + 1 < beeps.size()) {
                ++m_beep;
                m_hornState = 0;
                m_horn.playLoop(m_horn.volume(), m_horn.pitch());
            } else {
                m_hornState = 2;
                m_beep = 0;
                if (m_horn.playing())
                    m_horn.stop();
            }
            m_hornTimer = 0.0f;
            return;
        }
    }
    if (m_horn.playing())
        m_horn.stop();
}

void AmbientCarAudio::silence() {
    // aiAmbientVehicleAudio::UnAssignSounds (vehHornAudio::Reset idles the pattern).
    m_engine.stop();
    m_horn.stop();
    m_impacts.stop();
    m_hornState = 2;
    m_beep = 0;
    m_3d.resetDistance();
}

void AmbientCarAudio::update(float speed, const Mat34& transform, const Vec3& velocity, float dt, const Vec3& listener) {
    Mat34 l = Mat34::identity();
    l.m3 = listener;
    update(speed, transform, velocity, dt, l);
}

void AmbientCarAudio::update(float speed, const Mat34& transform, const Vec3&, float dt, const Mat34& listener) {
    m_speed = std::abs(speed);
    m_3d.updateDistance(transform.m3, listener.m3);
    if (!hasSlot() && !acquireSlot(m_3d.withinMaxDistance()))
        return;
    // aiAmbientVehicleAudio::UpdateAudio.
    if (m_3d.pastMaxDistance()) {
        releaseSlot();
        silence();
        return;
    }
    const float attenuation = m_3d.attenuation();
    const float pan = m_3d.pan(listener, transform.m3);
    const float doppler = m_3d.doppler(2.0f / kDopplerSpeed, dt);
    m_pan = pan;
    // A drop of more than 4 m/s since the last update decays the engine
    // pitch from the previous speed instead of following the new one.
    bool decaying = false;
    float pitchSpeed = m_speed;
    if (m_prevSpeed - m_speed > kAmbientSpeedDrop) {
        m_prevSpeed *= 0.95f;
        pitchSpeed = m_prevSpeed;
        decaying = true;
    }
    // vehHornAudio::UpdateDoppler (the values also apply to the next pattern).
    if (m_hornDef && m_horn.valid()) {
        m_horn.setPan(pan);
        m_horn.setVolume(attenuation * m_hornDef->volume);
        m_horn.setPitch(doppler * m_hornDef->pitch);
    }
    // aiEngineAudio::UpdateDoppler uses the pitch computed on the previous update.
    if (m_engine.valid()) {
        m_engine.setPan(pan);
        m_engine.playLoop(attenuation * m_engineDef.volume, doppler * m_pitch);
    }
    // aiEngineAudio::CalculatePitch: the slow-down curve while decaying or
    // slowing, else the speed bands.
    const bool slowing = decaying || pitchSpeed < m_prevSpeed;
    if (auto p = pitchFor(m_engineDef, pitchSpeed, slowing))
        m_pitch = *p;
    m_impacts.updateAttenuation(attenuation, pan);
    if (!decaying)
        m_prevSpeed = m_speed;
    updateHorn(dt);
}

void AmbientCarAudio::stop() {
    releaseSlot();
    m_engine.stop();
    m_horn.stop();
    m_impacts.stop();
    m_hornState = 2;
    m_beep = 0;
}

} // namespace mm2::audio::game
