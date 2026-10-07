// Announcer and creature voices. The pre-race line selection follows MM1's
// mmVoiceCommentary::GetRandomPreRace (Open1560, GPL-3.0, Copyright (C) 2020
// Brick: code/midtown/game.asm); the table formats are MM2's aud/spchdata
// and aud/creaturedata (semantics partly inferred, see docs/audio.md).
#include "audio/game/Voices.h"

#include "core/Log.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <format>

namespace mm2::audio::game {
namespace {

const char* modeTable(AnnouncerMode m) {
    switch (m) {
    case AnnouncerMode::Blitz: return "blitz";
    case AnnouncerMode::Checkpoint: return "checkpoint";
    case AnnouncerMode::Circuit: return "circuit";
    default: return "cruise";
    }
}

// Time of day / weather prerace tables (aud/spchdata/<ann>/...).
const char* todTable(int t) {
    static const char* names[] = {"timemorn_prerace", "timenoon_prerace", "timeeve_prerace", "timenight_prerace"};
    return names[std::clamp(t, 0, 3)];
}

const char* weatherTable(int w) {
    static const char* names[] = {"weaclr_prerace", "weacldy_prerace", "weafog_prerace", "wearain_prerace"};
    return names[std::clamp(w, 0, 3)];
}

} // namespace

std::vector<std::string> Announcer::lineNames(std::string_view announcer, const SpeechLineSet& set) {
    std::vector<std::string> out;
    const std::string ann = str::lower(announcer);
    const bool prefixed = !ann.empty() && str::istartsWith(set.prefix, ann);
    for (int n = set.add + 1; n <= set.end; ++n)
        out.push_back(std::format("{}{}{:02}", prefixed ? "" : ann, str::lower(set.prefix), n));
    return out;
}

bool Announcer::load(const vfs::Vfs& vfs, SoundBank& bank, Mixer& mixer, std::string_view city) {
    m_vfs = &vfs;
    m_bank = &bank;
    m_mixer = &mixer;
    m_city = str::lower(city);
    m_tables.clear();
    auto text = readText(vfs, std::format("aud/spchdata/{}.csv", m_city));
    if (!text)
        return false;
    auto list = parseAnnouncerList(*text);
    if (!list)
        return false;
    m_prefix = list->prefix;
    m_count = list->count;
    m_available.clear();
    for (int i = 1; i <= m_count; ++i)
        if (vfs.exists(std::format("aud/spchdata/{}{}/cruise.csv", m_prefix, i)))
            m_available.push_back(i);
    beginSession();
    return true;
}

void Announcer::beginSession(int index) {
    if (m_count <= 0)
        return;
    if (index < 0) {
        if (m_available.empty())
            return;
        std::uniform_int_distribution<std::size_t> pick(0, m_available.size() - 1);
        m_index = m_available[pick(m_rng)];
    } else {
        m_index = std::clamp(index, 1, m_count);
    }
}

std::string Announcer::announcerId() const { return std::format("{}{}", m_prefix, m_index); }

const SpeechTable* Announcer::table(const std::string& folder, std::string_view name) {
    const std::string path = std::format("aud/spchdata/{}{}{}.csv", folder, folder.empty() ? "" : "/", str::lower(name));
    if (auto it = m_tables.find(path); it != m_tables.end())
        return it->second ? &*it->second : nullptr;
    std::optional<SpeechTable> t;
    if (m_vfs)
        if (auto text = readText(*m_vfs, path))
            t = parseSpeechTable(*text);
    auto& slot = m_tables[path] = std::move(t);
    return slot ? &*slot : nullptr;
}

std::string Announcer::enqueue(const std::vector<std::string>& candidatesIn) {
    // Only lines that exist, and not the line just spoken when there is a choice.
    std::vector<std::string> candidates;
    for (const auto& c : candidatesIn)
        if (m_bank && !m_bank->resolve(c).empty())
            candidates.push_back(c);
    if (candidates.size() > 1)
        std::erase(candidates, m_last);
    if (candidates.empty())
        return {};
    std::uniform_int_distribution<std::size_t> pick(0, candidates.size() - 1);
    std::string line = candidates[pick(m_rng)];
    if (m_queue.size() < 2)
        m_queue.push_back(line);
    update(0.0f);
    return line;
}

std::string Announcer::play(std::string_view tableName, std::string_view event, std::string_view folder) {
    const std::string dir = folder.empty() ? announcerId() : std::string(folder);
    const SpeechTable* t = table(dir, tableName);
    if (!t)
        return {};
    const auto* sets = event.empty() ? (t->events.empty() ? nullptr : &t->events.front().second) : t->find(event);
    if (!sets)
        return {};
    const std::string ann = folder.empty() ? announcerId() : std::string();
    std::vector<std::string> candidates;
    for (const auto& s : *sets) {
        auto names = lineNames(ann, s);
        candidates.insert(candidates.end(), names.begin(), names.end());
    }
    return enqueue(candidates);
}

std::string Announcer::playPreRace(AnnouncerMode mode, std::string_view car, int timeOfDay, int weather) {
    // GetRandomPreRace: r = Random * 0.3; r <= 0.05 time of day (not in
    // cruise), r <= 0.1 weather, r <= 0.15 a line about the vehicle, else
    // the mode's own pre-race lines. (MM1 picks between weather and time of
    // day from a state flag at the 0.05..0.1 step; MM2 has weather lines for
    // every condition, so they are used directly - inferred.)
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    const float r = u(m_rng) * 0.3f;
    std::string line;
    if (r <= 0.05f && mode != AnnouncerMode::Cruise)
        line = play(todTable(timeOfDay), {});
    else if (r <= 0.1f && r > 0.05f)
        line = play(weatherTable(weather), {});
    else if (r <= 0.15f && r > 0.1f && !car.empty())
        line = play(std::format("{}_prerace", str::lower(car)), "VEHICLEPRERACE");
    if (!line.empty())
        return line;
    return play(modeTable(mode), "PRERACE");
}

std::string Announcer::playFinalCheckpoint(AnnouncerMode mode) { return play(modeTable(mode), "FINALCHECKPOINT"); }

std::string Announcer::playFinalLap() { return play("finallap", "FINALLAP"); }

std::string Announcer::playResults(AnnouncerMode mode, RaceOutcome outcome, std::string_view car) {
    static const char* modeEvents[] = {"RESULTSPOOR", "RESULTSMID", "RESULTSWIN"};
    static const char* carEvents[] = {"VEHICLERESULTSPOOR", "VEHICLERESULTSMID", "VEHICLERESULTSWIN"};
    const int o = static_cast<int>(outcome);
    // Inferred: half the time comment on the vehicle when it has lines.
    std::uniform_int_distribution<int> coin(0, 1);
    if (!car.empty() && coin(m_rng) == 0) {
        auto line = play(std::format("{}_results", str::lower(car)), carEvents[o]);
        if (!line.empty())
            return line;
    }
    auto line = play(modeTable(mode), modeEvents[o]);
    if (line.empty() && outcome == RaceOutcome::Mid)
        line = play(modeTable(mode), modeEvents[0]); // blitz has no "mid" lines
    return line;
}

std::string Announcer::playDamagePenalty(bool cruise) {
    return play(cruise ? "cruisedamage" : "racedamage", "DAMAGEPENALTY");
}

std::string Announcer::playUnlockVehicle(std::string_view car) {
    return play(std::format("{}_unlock", str::lower(car)), "UNLOCKVEHICLE");
}

std::string Announcer::playCopsAndRobbers(std::string_view event) {
    // The Cops & Robbers tables name the announcer inside each prefix.
    const SpeechTable* t = table("", std::format("cnr{}", m_city));
    if (!t)
        return {};
    const auto* sets = t->find(event);
    if (!sets)
        return {};
    std::vector<std::string> candidates;
    for (const auto& s : *sets) {
        auto names = lineNames({}, s);
        candidates.insert(candidates.end(), names.begin(), names.end());
    }
    return enqueue(candidates);
}

std::string Announcer::playCrashCourse(int lesson, std::string_view event) {
    const std::string folder = m_city == "sf" ? "ccs" : "ccl";
    return play(std::format("{}{}", folder, lesson), event, folder);
}

void Announcer::update(float) {
    if (m_current.playing() || m_queue.empty() || !m_mixer || !m_bank)
        return;
    const std::string line = m_queue.front();
    m_queue.pop_front();
    if (m_current.load(*m_mixer, *m_bank, line, Bus::Voice, 8)) {
        m_current.playOnce(1.0f);
        m_last = line;
    }
}

bool Announcer::speaking() const { return m_current.playing() || !m_queue.empty(); }

void Announcer::stop() {
    m_queue.clear();
    m_current.stop();
}

// --- CreatureVoice --------------------------------------------------------------------

void CreatureVoice::load(Mixer& mixer, SoundBank& bank, CreatureVoiceDef def) {
    m_mixer = &mixer;
    m_bank = &bank;
    m_def = std::move(def);
    m_state.assign(m_def.triggers.size(), {});
}

void CreatureVoice::say(const std::vector<VoiceLine>& lines, const Emitter3D& where) {
    if (lines.empty() || !m_mixer || !m_bank || m_slot.playing())
        return;
    std::uniform_int_distribution<std::size_t> pick(0, lines.size() - 1);
    const auto& line = lines[pick(m_rng)];
    if (m_slot.load(*m_mixer, *m_bank, line.wave, Bus::Voice, 2))
        m_slot.playOnce(line.volume, 1.0f, &where);
}

void CreatureVoice::update(float speed, float dt, const Emitter3D& where) {
    for (std::size_t i = 0; i < m_def.triggers.size(); ++i) {
        const auto& t = m_def.triggers[i];
        auto& s = m_state[i];
        if (speed >= t.minSpeed && speed < t.maxSpeed) {
            s.timeIn += dt;
            s.timeOut = 0;
            if (!s.fired && s.timeIn >= t.minTimeInRange) {
                say(t.lines, where);
                s.fired = true;
            }
        } else {
            s.timeOut += dt;
            if (s.timeOut > t.maxTimeOutOfRange) {
                s.timeIn = 0;
                s.fired = false;
            }
        }
    }
    if (!m_pending.empty()) {
        m_pendingDelay -= dt;
        if (m_pendingDelay <= 0) {
            say(m_pending, m_pendingWhere);
            m_pending.clear();
        }
    }
}

void CreatureVoice::impact(float force, const Emitter3D& where) {
    if (m_def.impactLines.empty() || force < m_def.minImpactForce || !m_pending.empty())
        return;
    std::uniform_int_distribution<std::size_t> pick(0, m_def.impactLines.size() - 1);
    const auto& line = m_def.impactLines[pick(m_rng)];
    m_pending = {line};
    m_pendingDelay = line.delay;
    m_pendingWhere = where;
}

bool CreatureVoice::speaking() const { return m_slot.playing(); }

} // namespace mm2::audio::game
