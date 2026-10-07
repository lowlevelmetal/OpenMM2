// Announcer and creature voices, ported from MM2 (mmRaceSpeech, mmCNRSpeech,
// mmCCSpeech, AudSpeech, AudSpeechData, mmPlayer::InitSpeechAudio,
// AudCreature, AudCreatureAvoid, AudCreatureImpact). See docs/audio.md.
#include "audio/game/Voices.h"

#include "core/Log.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <cmath>
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

// mmRaceSpeech::PlayPreRace waits this long (a constant it reads).
constexpr float kPreRaceDelay = 1.5f;
// PlayUnlockRace / PlayUnlockVehicle.
constexpr float kUnlockDelay = 0.1f;
// mmCNRSpeech::Play(char*).
constexpr float kCopsAndRobbersDelay = 0.01f;

// AudCreatureAvoid::Update: a queued line is dropped after 5 s, and only
// plays within 50 m (2500 m^2).
constexpr float kAvoidQueueTimeout = 5.0f;
constexpr float kAvoidMaxDistance2 = 2500.0f;
// AudCreatureImpact::QueuePlay: one impact line a minute across all creatures.
constexpr float kImpactCooldown = 60.0f;

// The shared creature state (globals in MM2): the impact clock and the last
// avoidance and impact lines chosen, which the next pick avoids.
float g_impactClock = 0.0f;
int g_lastAvoidLine = -1;
int g_lastImpactLine = -1;

std::string lineName(const std::string& announcer, const SpeechLineSet& set, int n) {
    const bool prefixed = !announcer.empty() && str::istartsWith(set.prefix, announcer);
    return std::format("{}{}{:02}", prefixed ? "" : announcer, str::lower(set.prefix), n);
}

} // namespace

// --- Announcer ----------------------------------------------------------------------

std::vector<std::string> Announcer::lineNames(std::string_view announcer, const SpeechLineSet& set) {
    std::vector<std::string> out;
    const std::string ann = str::lower(announcer);
    for (int n = set.add + 1; n <= set.end; ++n)
        out.push_back(lineName(ann, set, n));
    return out;
}

int Announcer::pickLine(const SpeechLineSet& set, int last, float uniform01) {
    // RandomizeNumber(add + 1, end + 0.99), truncated.
    const float lo = static_cast<float>(set.add) + 1.0f;
    const float hi = static_cast<float>(set.end) + 0.99f;
    int n = static_cast<int>(lo + (hi - lo) * uniform01);
    if (n == last && static_cast<float>(set.end) > 1.0f) {
        ++n;
        if (set.end < n)
            n = 1;
    }
    return n;
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
    beginSession();
    return true;
}

void Announcer::beginSession(int index) {
    if (m_count <= 0)
        return;
    if (index < 0) {
        std::uniform_real_distribution<float> u(1.0f, static_cast<float>(m_count) + 0.99f);
        m_index = static_cast<int>(u(m_rng));
        // LoadCityInfo: SF has no third announcer.
        if (m_city == "sf" && m_index == 3)
            m_index = 4;
    } else {
        m_index = std::clamp(index, 1, m_count);
    }
    m_groups.clear();
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

void Announcer::loadGroup(std::string_view name) {
    const SpeechTable* t = table(announcerId(), name);
    if (!t)
        return;
    // SetReadState: a header sets where its lines start; the rows after
    // PRERACE / UNLOCKRACE / UNLOCKVEHICLE extend a range, every other event
    // uses the first row after its header. An unknown header keeps the state.
    enum class State { None, PreRace, UnlockRace, UnlockVehicle, Single };
    State state = State::None;
    const std::string ann = announcerId();
    for (const auto& [event, rows] : t->events) {
        const int next = static_cast<int>(m_groups.size());
        if (event == "PRERACE") {
            state = State::PreRace;
            m_preRace = {next, -1};
        } else if (event == "UNLOCKRACE") {
            state = State::UnlockRace;
            m_unlockRace = {next, -1};
        } else if (event == "UNLOCKVEHICLE") {
            state = State::UnlockVehicle;
            m_unlockVehicle = {next, -1};
        } else {
            int* index = nullptr;
            if (event == "UNLOCKTEXTURE")
                index = &m_unlockTexture;
            else if (event == "FINALCHECKPOINT")
                index = &m_finalCheckpoint;
            else if (event == "FINALLAP")
                index = &m_finalLap;
            else if (event == "RACEPROGRESS")
                index = &m_raceProgress;
            else if (event == "RESULTSPOOR")
                index = &m_resultsPoor;
            else if (event == "RESULTSMID")
                index = &m_resultsMid;
            else if (event == "RESULTSWIN")
                index = &m_resultsWin;
            else if (event == "VEHICLERESULTSPOOR")
                index = &m_vehicleResultsPoor;
            else if (event == "VEHICLERESULTSMID")
                index = &m_vehicleResultsMid;
            else if (event == "VEHICLERESULTSWIN")
                index = &m_vehicleResultsWin;
            else if (event == "VEHICLEPRERACE")
                index = &m_vehiclePreRace;
            else if (event == "TIMEOFDAY")
                index = &m_timeOfDay;
            else if (event == "WEATHER")
                index = &m_weather;
            else if (event == "DAMAGEPENALTY")
                index = &m_damagePenalty;
            if (index) {
                *index = next;
                state = State::Single;
            }
        }
        for (const auto& row : rows) {
            const int at = static_cast<int>(m_groups.size());
            if (state == State::PreRace)
                m_preRace.last = at;
            else if (state == State::UnlockRace)
                m_unlockRace.last = at;
            else if (state == State::UnlockVehicle)
                m_unlockVehicle.last = at;
            m_groups.push_back({row, ann, -1});
        }
    }
}

void Announcer::beginRace(AnnouncerMode mode, std::string_view car, int timeOfDay, int weather) {
    m_groups.clear();
    m_preRace = m_unlockRace = m_unlockVehicle = {};
    m_finalCheckpoint = m_finalLap = m_raceProgress = m_damagePenalty = -1;
    m_resultsPoor = m_resultsMid = m_resultsWin = -1;
    m_vehicleResultsPoor = m_vehicleResultsMid = m_vehicleResultsWin = -1;
    m_vehiclePreRace = m_timeOfDay = m_weather = m_unlockTexture = -1;

    loadGroup(modeTable(mode)); // mmSpeechContainer::InitRace
    // mmPlayer::InitSpeechAudio.
    static const char* weathers[] = {"weaclr", "weacldy", "weafog", "wearain"};
    if (weather >= 0 && weather < 4)
        loadGroup(std::format("{}_prerace", weathers[weather]));
    const char* tod = timeOfDay == 0   ? "timemorn"
                      : timeOfDay == 1 ? "timenoon"
                      : timeOfDay == 2 ? (weather == 0 ? "timeeve" : nullptr)
                      : timeOfDay == 3 ? "timenight"
                                       : nullptr;
    if (tod)
        loadGroup(std::format("{}_prerace", tod));
    if (!car.empty()) {
        loadGroup(std::format("{}_prerace", str::lower(car)));
        loadGroup(std::format("{}_results", str::lower(car)));
    }
    loadGroup("finallap");
    loadGroup(mode == AnnouncerMode::Cruise ? "cruisedamage" : "racedamage");
}

int Announcer::randomIn(const Range& r) {
    if (r.first < 0 || r.last < r.first)
        return -1;
    std::uniform_real_distribution<float> u(static_cast<float>(r.first), static_cast<float>(r.last) + 0.99f);
    return static_cast<int>(u(m_rng));
}

std::string Announcer::lineFor(int group) {
    if (group < 0 || static_cast<std::size_t>(group) >= m_groups.size())
        return {};
    Group& g = m_groups[static_cast<std::size_t>(group)];
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    const int n = pickLine(g.set, g.last, u(m_rng));
    g.last = n;
    return lineName(str::lower(g.announcer), g.set, n);
}

std::string Announcer::play(int group, float delay) {
    std::string line = lineFor(group);
    if (line.empty() || !m_bank || m_bank->resolve(line).empty())
        return {};
    if (delay == 0.0f) {
        m_current.stop();
        if (m_mixer && m_current.load(*m_mixer, *m_bank, line, Bus::Voice, 8))
            m_current.playOnce(1.0f);
        return line;
    }
    // PutInQueue: the line waits in the free queue slot, or is dropped.
    if (m_queue.used)
        return {};
    m_queue = {line, delay, true};
    return line;
}

std::string Announcer::playEvent(int group) {
    if (group < 0)
        return {};
    stop();
    return play(group, 0.0f);
}

std::string Announcer::playPreRace() {
    // mmRaceSpeech::PlayPreRace: r in [0, 11.5): up to 7.5 the mode's lines,
    // to 8.5 the time of day, to 9.5 the weather (nothing if that group was
    // not loaded), above that the car (else the mode's lines).
    std::uniform_real_distribution<float> u(0.0f, 11.5f);
    const float r = u(m_rng);
    int group;
    if (r <= 7.5f)
        group = randomIn(m_preRace);
    else if (r <= 8.5f)
        group = m_timeOfDay;
    else if (r <= 9.5f)
        group = m_weather;
    else
        group = m_vehiclePreRace != -1 ? m_vehiclePreRace : randomIn(m_preRace);
    if (group < 0)
        return {};
    stop();
    return play(group, kPreRaceDelay);
}

std::string Announcer::playFinalCheckpoint() { return playEvent(m_finalCheckpoint); }
std::string Announcer::playFinalLap() { return playEvent(m_finalLap); }
std::string Announcer::playDamagePenalty() { return playEvent(m_damagePenalty); }
std::string Announcer::playRaceProgress() { return playEvent(m_raceProgress); }

std::string Announcer::playResults(int position, int racers) {
    if (position == 1)
        return playResults(RaceOutcome::Win);
    if (0 < position - 1 && position - 1 < racers / 2)
        return playResults(RaceOutcome::Mid);
    return playResults(RaceOutcome::Poor);
}

std::string Announcer::playResults(RaceOutcome outcome) {
    // PlayResultsWin / Mid: the car's line when r > 5 (of 10) and it has one;
    // PlayResultsPoor: when r > 8. Otherwise the mode's line, or nothing.
    std::uniform_real_distribution<float> u(0.0f, 10.0f);
    const float r = u(m_rng);
    int group = -1;
    switch (outcome) {
    case RaceOutcome::Win: group = r > 5.0f && m_vehicleResultsWin != -1 ? m_vehicleResultsWin : m_resultsWin; break;
    case RaceOutcome::Mid: group = r > 5.0f && m_vehicleResultsMid != -1 ? m_vehicleResultsMid : m_resultsMid; break;
    case RaceOutcome::Poor:
        group = r > 8.0f && m_vehicleResultsPoor != -1 ? m_vehicleResultsPoor : m_resultsPoor;
        break;
    }
    return playEvent(group);
}

std::string Announcer::playUnlockRace() {
    const int group = randomIn(m_unlockRace);
    return group < 0 ? std::string() : play(group, kUnlockDelay);
}

std::string Announcer::playUnlockVehicle(std::string_view car) {
    loadGroup(std::format("{}_unlock", str::lower(car))); // LoadVehicleUnlock
    const int group = randomIn(m_unlockVehicle);
    return group < 0 ? std::string() : play(group, kUnlockDelay);
}

std::string Announcer::playCopsAndRobbers(std::string_view event) {
    // The Cops & Robbers tables name the announcer inside each prefix.
    const SpeechTable* t = table("", std::format("cnr{}", m_city));
    if (!t)
        return {};
    const auto* sets = t->find(event);
    if (!sets || sets->empty())
        return {};
    std::uniform_int_distribution<std::size_t> pick(0, sets->size() - 1);
    const SpeechLineSet& set = (*sets)[pick(m_rng)];
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    const std::string line = lineName({}, set, pickLine(set, -1, u(m_rng)));
    if (!m_bank || m_bank->resolve(line).empty())
        return {};
    stop();
    m_queue = {line, kCopsAndRobbersDelay, true};
    return line;
}

std::string Announcer::playCrashCourse(int lesson, std::string_view event) {
    const std::string folder = m_city == "sf" ? "ccs" : "ccl";
    const SpeechTable* t = table(folder, std::format("{}{}", folder, lesson));
    if (!t)
        return {};
    const auto* sets = t->find(event);
    if (!sets || sets->empty())
        return {};
    const SpeechLineSet& set = sets->front();
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    const std::string line = lineName(folder, set, pickLine(set, -1, u(m_rng)));
    if (!m_bank || m_bank->resolve(line).empty())
        return {};
    stop();
    // mmCCSpeech::PlayPreRace waits 1.5 s; the results start at once.
    if (str::iequals(event, "PRERACE")) {
        m_queue = {line, kPreRaceDelay, true};
    } else if (m_mixer && m_current.load(*m_mixer, *m_bank, line, Bus::Voice, 8)) {
        m_current.playOnce(1.0f);
    }
    return line;
}

void Announcer::update(float dt) {
    if (!m_queue.used)
        return;
    m_queue.delay -= dt;
    if (m_current.playing() || m_queue.delay > 0.0f || !m_mixer || !m_bank)
        return;
    const std::string line = m_queue.line;
    m_queue = {};
    if (m_current.load(*m_mixer, *m_bank, line, Bus::Voice, 8))
        m_current.playOnce(1.0f);
}

bool Announcer::speaking() const { return m_current.playing() || m_queue.used; }

void Announcer::stop() {
    m_queue = {};
    m_current.stop();
}

// --- CreatureVoice --------------------------------------------------------------------

void CreatureVoice::advanceClock(float dt) { g_impactClock += dt; }

void CreatureVoice::resetGlobals() {
    g_impactClock = 0.0f;
    g_lastAvoidLine = -1;
    g_lastImpactLine = -1;
}

void CreatureVoice::load(Mixer& mixer, SoundBank& bank, CreatureVoiceDef def, float maxDistance) {
    m_def = std::move(def);
    m_avoids.clear();
    for (const auto& t : m_def.triggers) {
        Avoid a;
        a.def = t;
        a.slots.resize(t.lines.size());
        for (std::size_t i = 0; i < t.lines.size(); ++i)
            a.slots[i].load(mixer, bank, t.lines[i].wave, Bus::Voice, 2);
        m_avoids.push_back(std::move(a));
    }
    m_impactSlots.clear();
    m_impactSlots.resize(m_def.impactLines.size());
    for (std::size_t i = 0; i < m_def.impactLines.size(); ++i)
        m_impactSlots[i].load(mixer, bank, m_def.impactLines[i].wave, Bus::Voice, 2);
    m_impactQueued = false;
    m_3d = Audio3D();
    m_3d.setDropOffs(0.0f, maxDistance);
}

bool CreatureVoice::eligible(const Avoid& a) const {
    // AudCreatureAvoid::IsEligible.
    return !(a.timeIn < a.def.minTimeInRange && a.def.maxTimeOutOfRange < a.timeOut);
}

void CreatureVoice::avoid() {
    for (auto& a : m_avoids) {
        if (!eligible(a) || a.queued || a.slots.empty())
            continue;
        // QueuePlay: RandomizeNumber(2n) below n picks that line, else none;
        // the line said last by any creature moves on by one.
        const int n = static_cast<int>(a.slots.size());
        std::uniform_real_distribution<float> u(0.0f, static_cast<float>(n * 2));
        int line = static_cast<int>(u(m_rng));
        if (line >= n)
            continue;
        if (line == g_lastAvoidLine && ++line >= n)
            line = 0;
        g_lastAvoidLine = line;
        a.line = static_cast<std::size_t>(line);
        a.queued = true;
        a.queueTime = 0.0f;
    }
}

void CreatureVoice::impact(float force) {
    if (m_impactQueued || m_impactSlots.empty() || g_impactClock < kImpactCooldown || force < m_def.minImpactForce)
        return;
    const int n = static_cast<int>(m_impactSlots.size());
    std::uniform_real_distribution<float> u(0.0f, static_cast<float>(n) - 0.01f);
    int line = static_cast<int>(u(m_rng));
    if (line == g_lastImpactLine && ++line >= n)
        line = 0;
    g_lastImpactLine = line;
    m_impactLine = static_cast<std::size_t>(line);
    m_impactQueued = true;
    m_impactTime = 0.0f;
}

void CreatureVoice::playAvoid(Avoid& a) {
    auto& slot = a.slots[a.line];
    if (slot.playing() || m_attenuation <= 0.0f)
        return;
    slot.setPan(m_pan);
    slot.playOnce(m_attenuation * a.def.lines[a.line].volume);
    a.queued = false;
}

void CreatureVoice::update(float speed, float dt, const Vec3& position, const Mat34& listener) {
    m_3d.updateDistance(position, listener.m3);
    const bool inRange = m_3d.withinMaxDistance();
    m_attenuation = inRange ? m_3d.attenuation() : 0.0f;
    m_pan = inRange ? m_3d.pan(listener, position) : 0.0f;
    // UpdateAttenuation for the lines being said.
    for (auto& a : m_avoids)
        for (std::size_t i = 0; i < a.slots.size(); ++i)
            if (a.slots[i].playing()) {
                a.slots[i].setVolume(a.def.lines[i].volume * m_attenuation);
                a.slots[i].setPan(m_pan);
            }
    for (std::size_t i = 0; i < m_impactSlots.size(); ++i)
        if (m_impactSlots[i].playing()) {
            m_impactSlots[i].setVolume(m_def.impactLines[i].volume * m_attenuation);
            m_impactSlots[i].setPan(m_pan);
        }
    // AudCreatureAvoid::Update.
    for (auto& a : m_avoids) {
        if (a.def.minSpeed <= speed && speed < a.def.maxSpeed) {
            a.timeIn += dt;
            a.timeOut = 0.0f;
        } else {
            a.timeOut += dt;
            a.timeIn = 0.0f;
        }
        if (!a.queued)
            continue;
        a.queueTime += dt;
        if (kAvoidQueueTimeout <= a.queueTime) {
            a.queued = false;
            continue;
        }
        if (m_3d.distance2() <= kAvoidMaxDistance2)
            playAvoid(a);
    }
    // AudCreatureImpact::Update.
    if (m_impactQueued) {
        m_impactTime += dt;
        if (m_def.impactLines[m_impactLine].delay <= m_impactTime) {
            auto& slot = m_impactSlots[m_impactLine];
            if (slot.valid() && !slot.playing() && m_attenuation > 0.0f) {
                slot.setPan(m_pan);
                slot.playOnce(m_attenuation * m_def.impactLines[m_impactLine].volume);
                m_impactQueued = false;
                g_impactClock = 0.0f;
            }
            m_impactTime = 0.0f;
        }
    }
}

bool CreatureVoice::speaking() const {
    for (const auto& a : m_avoids)
        for (const auto& s : a.slots)
            if (s.playing())
                return true;
    for (const auto& s : m_impactSlots)
        if (s.playing())
            return true;
    return false;
}

} // namespace mm2::audio::game
