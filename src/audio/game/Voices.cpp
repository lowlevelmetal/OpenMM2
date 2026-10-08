// Announcer and creature voices, ported from MM2 (mmSpeechContainer,
// mmRaceSpeech, mmCNRSpeech, mmCCSpeech, AudSpeech, AudSpeechData,
// mmPlayer::InitSpeechAudio, AudCreature, AudCreatureAvoid,
// AudCreatureImpact). See docs/audio.md.
#include "audio/game/Voices.h"

#include "audio/AngelRandom.h"
#include "audio/TextFields.h"
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

// mmRaceSpeech::PlayPreRace, mmCCSpeech::PlayPreRace.
constexpr float kPreRaceDelay = 1.5f;
// PlayUnlockRace / PlayUnlockVehicle.
constexpr float kUnlockDelay = 0.1f;
// mmCNRSpeech::Play(char*), mmCCSpeech::PlayUnlock.
constexpr float kShortDelay = 0.01f;
// AudSpeechData::GetRandomName's range end, 0.99 as a float.
constexpr float kAlmostOne = 0.99f;

// AudCreatureAvoid::Update: a queued line is dropped after 5 s, and only
// plays within 50 m (2500 m^2).
constexpr float kAvoidQueueTimeout = 5.0f;
constexpr float kAvoidMaxDistance2 = 2500.0f;
// AudCreatureImpact::QueuePlay: one impact line a minute across all creatures.
constexpr float kImpactCooldown = 60.0f;

// The shared creature state (zero-initialised globals in MM2, never reset):
// the impact clock and the last avoidance and impact lines chosen, which the
// next pick avoids.
float g_impactClock = 0.0f;
int g_lastAvoidLine = 0;
int g_lastImpactLine = 0;

// The file name of a line: the part after a "AL1\" folder in the Cops &
// Robbers names. MM2 keeps the rest as is, so the trailing space of
// cnrlondon.csv's "AL1\AL1ROBROB " stays in the name and that file is never
// found (inferred from AudStream::PlayOnce's path, which nothing trims).
std::string fileName(std::string_view line) {
    if (const auto slash = line.find_last_of("\\/"); slash != std::string_view::npos)
        line.remove_prefix(slash + 1);
    return str::lower(line);
}

} // namespace

// --- Announcer ----------------------------------------------------------------------

int Announcer::pickLine(float end, float add, int last, double drawn) {
    (void)add;
    int n = static_cast<int>(drawn);
    if (last == n && 1.0f < end) {
        ++n;
        if (static_cast<int>(end) < n)
            n = 1;
    }
    return n;
}

std::string Announcer::lineName(std::string_view prefix, int n) {
    // sprintf("%d"), then a leading 0 below 10.
    return n < 10 && n >= 0 ? std::format("{}0{}", prefix, n) : std::format("{}{}", prefix, n);
}

int Announcer::randomGroup(float first, float last) {
    return static_cast<int>(randomizeNumber(first, last + kAlmostOne));
}

bool Announcer::load(const vfs::Vfs& vfs, SoundBank& bank, Mixer& mixer, std::string_view city) {
    m_vfs = &vfs;
    m_bank = &bank;
    m_mixer = &mixer;
    m_city = str::lower(city);
    m_tables.clear();
    reset(Kind::None);
    auto text = readText(vfs, std::format("aud/spchdata/{}.csv", m_city));
    if (!text)
        return false;
    auto list = parseAnnouncerList(*text);
    if (!list)
        return false;
    m_prefix = list->prefix;
    m_count = list->count;
    return loadCityInfo();
}

void Announcer::beginSession(int index) {
    m_forcedIndex = index > 0 ? index : -1;
    loadCityInfo();
}

std::string Announcer::announcerId() const { return std::format("{}{}", m_prefix, m_index); }

bool Announcer::loadCityInfo() {
    // mmRaceSpeech::LoadCityInfo: only SF and London have announcers.
    if (str::iequals(m_city, "sf"))
        m_cityKind = 1;
    else if (str::iequals(m_city, "london"))
        m_cityKind = 2;
    else
        m_cityKind = 0;
    if (m_cityKind == 0 || m_count <= 0.0f)
        return false;
    if (m_forcedIndex > 0) {
        m_index = m_forcedIndex;
        return true;
    }
    // A minimum install would use announcer 1 (AudManager::MinInstall); the
    // game data OpenMM2 reads always has them all.
    m_index = static_cast<int>(randomizeNumber(1.0f, m_count + kAlmostOne));
    if (m_cityKind == 1 && m_index == 3)
        m_index = 4;
    return true;
}

void Announcer::reset(Kind kind) {
    stop();
    m_kind = kind;
    m_groups.clear();
    m_groupCount = 0.0f;
    m_preRaceFirst = m_preRaceLast = -1.0f;
    m_unlockRaceFirst = m_unlockRaceLast = -1.0f;
    m_unlockVehicleFirst = m_unlockVehicleLast = -1.0f;
    m_unlockCount = 0;
    m_finalCheckpoint = m_raceProgress = m_resultsPoor = m_resultsMid = m_resultsWin = -1;
    m_damagePenalty = m_vehiclePreRace = m_vehicleResultsWin = m_vehicleResultsMid = -1;
    m_vehicleResultsPoor = m_timeOfDay = m_weather = m_finalLap = m_unlockTexture = -1;
    m_events.clear();
    m_ccPreRace = m_ccUnlock = m_ccCheckPoint = m_ccResultsPoor = m_ccResultsWin = -1;
    m_ccUnlockCount = 0;
    m_ccCheckPointLines.clear();
    // AudSpeech's constructor allocates one queue slot.
    m_queue.assign(1, {});
}

const SpeechTable* Announcer::table(const std::string& path) {
    if (auto it = m_tables.find(path); it != m_tables.end())
        return it->second ? &*it->second : nullptr;
    std::optional<SpeechTable> t;
    if (m_vfs)
        if (auto text = readText(*m_vfs, path))
            t = parseSpeechTable(*text);
    auto& slot = m_tables[path] = std::move(t);
    return slot ? &*slot : nullptr;
}

int Announcer::addGroup(std::string prefix, float end, float add) {
    // AudSpeech::AllocateSpeechData.
    m_groups.push_back({std::move(prefix), end, add, 0});
    return static_cast<int>(m_groups.size()) - 1;
}

bool Announcer::loadRaceGroup(std::string_view name) {
    if (m_kind != Kind::Race || m_cityKind == 0)
        return false;
    const char* folder = m_cityKind == 1 ? "as" : "al";
    const SpeechTable* t = table(std::format("aud/spchdata/{}{}/{}.csv", folder, m_index, str::lower(name)));
    if (!t)
        return false;
    // mmRaceSpeech::LoadGroup / SetReadState.
    const std::string announcer = str::upper(m_prefix) + std::to_string(m_index);
    int state = -1;
    m_unlockCount = 0;
    for (const auto& row : t->rows) {
        if (row.header()) {
            const float at = m_groupCount;
            const int index = static_cast<int>(at);
            if (row.headerIs("PRERACE")) {
                state = 0;
                m_preRaceFirst = at;
            } else if (row.headerIs("UNLOCKRACE") || row.headerIs("UNLOCKVEHICLE")) {
                const bool race = row.headerIs("UNLOCKRACE");
                state = race ? 1 : 2;
                (race ? m_unlockRaceFirst : m_unlockVehicleFirst) = at;
                if (++m_unlockCount >= 2)
                    allocateQueueSlot();
            } else if (row.headerIs("UNLOCKTEXTURE")) {
                state = 0x10;
                m_unlockTexture = index;
            } else if (row.headerIs("FINALCHECKPOINT")) {
                state = 3;
                m_finalCheckpoint = index;
            } else if (row.headerIs("FINALLAP")) {
                state = 0xe;
                m_finalLap = index;
            } else if (row.headerIs("RACEPROGRESS")) {
                state = 4;
                m_raceProgress = index;
            } else if (row.headerIs("RESULTSPOOR")) {
                state = 5;
                m_resultsPoor = index;
            } else if (row.headerIs("RESULTSMID")) {
                state = 6;
                m_resultsMid = index;
            } else if (row.headerIs("RESULTSWIN")) {
                state = 7;
                m_resultsWin = index;
            } else if (row.headerIs("VEHICLERESULTSPOOR")) {
                state = 0xb;
                m_vehicleResultsPoor = index;
            } else if (row.headerIs("VEHICLERESULTSMID")) {
                state = 10;
                m_vehicleResultsMid = index;
            } else if (row.headerIs("VEHICLERESULTSWIN")) {
                state = 9;
                m_vehicleResultsWin = index;
            } else if (row.headerIs("VEHICLEPRERACE")) {
                state = 8;
                m_vehiclePreRace = index;
            } else if (row.headerIs("TIMEOFDAY")) {
                state = 0xc;
                m_timeOfDay = index;
            } else if (row.headerIs("WEATHER")) {
                state = 0xd;
                m_weather = index;
            } else if (row.headerIs("DAMAGEPENALTY")) {
                state = 0xf;
                m_damagePenalty = index;
            }
            // Any other header keeps the state: its rows extend the last range.
            continue;
        }
        if (state == 0) {
            m_preRaceLast = m_groupCount;
        } else if (state == 1) {
            m_unlockRaceLast = m_groupCount;
        } else if (state == 2) {
            m_unlockVehicleLast = m_groupCount;
            if (++m_unlockCount > 1)
                allocateQueueSlot();
        }
        addGroup(announcer + row.name, row.end, row.add);
        m_groupCount += 1.0f;
    }
    return true;
}

void Announcer::beginRace(AnnouncerMode mode, std::string_view car, int timeOfDay, int weather) {
    // mmSpeechContainer::InitRace: a new mmRaceSpeech, a new announcer.
    reset(Kind::Race);
    if (!loadCityInfo()) {
        reset(Kind::None);
        return;
    }
    loadRaceGroup(modeTable(mode));
    // mmPlayer::InitSpeechAudio.
    static const char* weathers[] = {"weaclr", "weacldy", "weafog", "wearain"};
    if (weather >= 0 && weather < 4)
        loadRaceGroup(std::format("{}_prerace", weathers[weather]));
    const char* tod = timeOfDay == 0   ? "timemorn"
                      : timeOfDay == 1 ? "timenoon"
                      : timeOfDay == 2 ? (weather == 0 ? "timeeve" : nullptr)
                      : timeOfDay == 3 ? "timenight"
                                       : nullptr;
    if (tod)
        loadRaceGroup(std::format("{}_prerace", tod));
    loadRaceGroup(std::format("{}_prerace", car));
    loadRaceGroup(std::format("{}_results", car));
    loadRaceGroup("finallap");
    loadRaceGroup(mode == AnnouncerMode::Cruise ? "cruisedamage" : "racedamage");
}

bool Announcer::loadVehicleUnlock(std::string_view car) {
    return loadRaceGroup(std::format("{}_unlock", car));
}

bool Announcer::loadTextureUnlock(std::string_view car) {
    return loadRaceGroup(std::format("{}_texture", car));
}

std::string Announcer::start(int group, int line) {
    if (group < 0 || static_cast<std::size_t>(group) >= m_groups.size())
        return {};
    Group& g = m_groups[static_cast<std::size_t>(group)];
    // AudSpeechData::GetName (a given line) / GetRandomName.
    const int n = line >= 0 ? line
                            : pickLine(g.end, g.add, g.last,
                                       randomizeNumber(g.add + 1.0f, g.end + kAlmostOne));
    g.last = n;
    const std::string name = lineName(g.prefix, n);
    // AudStream::PlayOnce: a line whose file does not exist plays nothing.
    if (m_mixer && m_bank && m_current.load(*m_mixer, *m_bank, fileName(name), Bus::Voice)) {
        // AudSpeech::SetVolume(1) (mmPlayer::InitSpeechAudio, InitCC).
        m_current.setVolume(1.0f);
        m_current.playOnce();
    }
    return fileName(name);
}

bool Announcer::putInQueue(int group, int line, float delay) {
    // AudSpeech::PutInQueue: the first free slot, else the play is dropped.
    for (auto& q : m_queue) {
        if (q.group == -1) {
            q = {group, line, delay};
            return true;
        }
    }
    return false;
}

std::string Announcer::play(int group, float delay, int line) {
    if (group < 0 || static_cast<std::size_t>(group) >= m_groups.size())
        return {};
    if (delay == 0.0f) {
        // AudSpeech::PlayStream: stop the line playing, start this one.
        if (m_current.playing())
            m_current.stop();
        return start(group, line);
    }
    if (!putInQueue(group, line, delay))
        return {};
    return fileName(m_groups[static_cast<std::size_t>(group)].prefix);
}

std::string Announcer::playPreRace() {
    // mmRaceSpeech::PlayPreRace: r in [0, 11.5): up to 7.5 the mode's lines,
    // to 8.5 the time of day, to 9.5 the weather (nothing if that group was
    // not loaded), above that the car (else the mode's lines). An unset range
    // (-1, -1) draws group 0.
    if (m_kind != Kind::Race)
        return {};
    const double r = randomizeNumber(11.5f);
    int group;
    if (r <= 7.5)
        group = randomGroup(m_preRaceFirst, m_preRaceLast);
    else if (r <= 8.5)
        group = m_timeOfDay;
    else if (r <= 9.5)
        group = m_weather;
    else
        group = m_vehiclePreRace != -1 ? m_vehiclePreRace : randomGroup(m_preRaceFirst, m_preRaceLast);
    if (group == -1)
        return {};
    stop();
    return play(group, kPreRaceDelay);
}

std::string Announcer::playFinalCheckpoint() {
    if (m_finalCheckpoint == -1)
        return {};
    stop();
    return play(m_finalCheckpoint, 0.0f);
}

std::string Announcer::playFinalLap() {
    // mmRaceSpeech::PlayFinalLap stops the line playing even without a
    // final-lap group (and would then read past the groups; OpenMM2 plays
    // nothing).
    stop();
    return play(m_finalLap, 0.0f);
}

std::string Announcer::playDamagePenalty() {
    if (m_damagePenalty == -1)
        return {};
    stop();
    return play(m_damagePenalty, 0.0f);
}

std::string Announcer::playRaceProgress() {
    if (m_raceProgress == -1)
        return {};
    stop();
    return play(m_raceProgress, 0.0f);
}

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
    const double r = randomizeNumber(10.0f);
    int group = -1;
    switch (outcome) {
    case RaceOutcome::Win:
        group = 5.0 < r && m_vehicleResultsWin != -1 ? m_vehicleResultsWin : m_resultsWin;
        break;
    case RaceOutcome::Mid:
        group = 5.0 < r && m_vehicleResultsMid != -1 ? m_vehicleResultsMid : m_resultsMid;
        break;
    case RaceOutcome::Poor:
        group = 8.0 < r && m_vehicleResultsPoor != -1 ? m_vehicleResultsPoor : m_resultsPoor;
        break;
    }
    if (group == -1)
        return {};
    stop();
    return play(group, 0.0f);
}

std::string Announcer::playUnlockRace() {
    // No Stop: the line waits in the queue behind the one playing.
    if (m_unlockRaceFirst == -1.0f)
        return {};
    return play(randomGroup(m_unlockRaceFirst, m_unlockRaceLast), kUnlockDelay);
}

std::string Announcer::playUnlockVehicle() {
    if (m_unlockVehicleFirst == -1.0f)
        return {};
    return play(randomGroup(m_unlockVehicleFirst, m_unlockVehicleLast), kUnlockDelay);
}

std::string Announcer::playUnlockTexture() {
    if (m_unlockTexture == -1)
        return {};
    stop();
    return play(m_unlockTexture, 0.0f);
}

void Announcer::beginCopsAndRobbers() {
    // mmSpeechContainer::InitCNR: "BULLSHIT" (bullshit.csv) in SF, CNRLONDON
    // everywhere else; mmCNRSpeech::LoadGroup / SetReadState.
    reset(Kind::CopsAndRobbers);
    const SpeechTable* t =
        table(std::format("aud/spchdata/{}.csv", str::iequals(m_city, "sf") ? "bullshit" : "cnrlondon"));
    if (!t)
        return;
    for (const auto& row : t->rows) {
        if (row.header()) {
            m_events.push_back({row.eventName(), m_groupCount, m_groupCount});
            continue;
        }
        if (m_events.empty())
            continue; // MM2 would write before its first event
        m_events.back().last = m_groupCount;
        float end = row.end, add = row.add;
        // "num used": a random window of that many lines.
        if (row.numUsed < end - add) {
            const double first = randomizeNumber(add, (end - row.numUsed) + kAlmostOne);
            add = static_cast<float>(static_cast<int>(first));
            end = row.numUsed + add;
        }
        addGroup(row.name, end, add);
        m_groupCount += 1.0f;
    }
}

std::string Announcer::playCopsAndRobbers(std::string_view event) {
    // mmCNRSpeech::Play(char*).
    if (m_kind != Kind::CopsAndRobbers)
        return {};
    for (const auto& e : m_events) {
        if (!str::iequals(e.name, event))
            continue;
        const int group = e.first != e.last ? randomGroup(e.first, e.last) : static_cast<int>(e.first);
        if (group == -1)
            return {};
        stop();
        return play(group, kShortDelay);
    }
    return {};
}

bool Announcer::loadCrashCourseGroup(const std::string& name) {
    const char* folder = m_cityKind == 1 ? "ccs" : "ccl";
    const SpeechTable* t = table(std::format("aud/spchdata/{}/{}.csv", folder, name));
    if (!t)
        return false;
    // mmCCSpeech::LoadGroup / SetReadState.
    int state = -1;
    m_ccUnlockCount = 0;
    for (const auto& row : t->rows) {
        if (row.header()) {
            const int index = static_cast<int>(m_groupCount);
            if (row.headerIs("PRERACE")) {
                state = 0;
                m_ccPreRace = index;
            } else if (row.headerIs("UNLOCK")) {
                state = 1;
                m_ccUnlock = index;
                if (++m_ccUnlockCount > 1)
                    allocateQueueSlot();
            } else if (row.headerIs("CHECKPOINT")) {
                state = 2;
                m_ccCheckPoint = index;
            } else if (row.headerIs("RESULTSPOOR")) {
                state = 3;
                m_ccResultsPoor = index;
            } else if (row.headerIs("RESULTSWIN")) {
                state = 4;
                m_ccResultsWin = index;
            }
            continue;
        }
        addGroup(row.name, row.end, row.add);
        // Pre-race rows get one more queue slot per line.
        if (state == 0) {
            const int end = static_cast<int>(row.end), add = static_cast<int>(row.add);
            for (int k = add + 1; k <= end; ++k)
                allocateQueueSlot();
        }
        m_groupCount += 1.0f;
    }
    return true;
}

bool Announcer::beginCrashCourse(int lesson) {
    // mmSpeechContainer::InitCC / mmCCSpeech::SetSubPath: SF or London only.
    reset(Kind::CrashCourse);
    if (str::iequals(m_city, "sf"))
        m_cityKind = 1;
    else if (str::iequals(m_city, "london"))
        m_cityKind = 2;
    else {
        reset(Kind::None);
        return false;
    }
    const bool london = str::iequals(m_city, "london");
    loadCrashCourseGroup(std::format("{}{}", london ? "ccl" : "ccs", lesson));
    if (lesson == 4) {
        loadCrashCourseGroup("cc_cpoint_waveinfo");
        // LoadCheckPointIndexInfo: one line number (atoi) per checkpoint.
        if (m_vfs)
            if (auto text = readText(*m_vfs, std::format("aud/spchdata/{}/cc_cpoint_indexinfo.csv",
                                                         m_cityKind == 1 ? "ccs" : "ccl"))) {
                const auto lines = fgetsLines(*text);
                for (std::size_t i = 1; i < lines.size(); ++i)
                    m_ccCheckPointLines.push_back(crtAtoi(field(strtokFields(lines[i]), 0)));
            }
    }
    return true;
}

std::string Announcer::playCrashCoursePreRace() {
    // mmCCSpeech::PlayPreRace (its RandomizeNumber(10) is unused).
    if (m_kind != Kind::CrashCourse)
        return {};
    stop();
    std::string line = play(m_ccPreRace, kPreRaceDelay);
    playCrashCourseCheckPoint(0, kPreRaceDelay + kShortDelay);
    return line;
}

std::string Announcer::playCrashCourseCheckPoint(int index, float delay) {
    if (index < 0 || static_cast<std::size_t>(index) >= m_ccCheckPointLines.size())
        return {};
    const int line = m_ccCheckPointLines[static_cast<std::size_t>(index)];
    if (line == -1)
        return {};
    return play(m_ccCheckPoint, delay, line);
}

std::string Announcer::playCrashCourseResults(bool passed) {
    if (m_kind != Kind::CrashCourse)
        return {};
    stop();
    return play(passed ? m_ccResultsWin : m_ccResultsPoor, 0.0f);
}

std::string Announcer::playCrashCourseUnlock() { return play(m_ccUnlock, kShortDelay); }

void Announcer::update(float dt) {
    // AudSpeech::Update.
    for (auto& q : m_queue) {
        q.delay -= dt;
        if (m_current.playing())
            continue;
        if (q.group != -1 && q.delay <= 0.0f) {
            const Queued due = q;
            q = {};
            play(due.group, 0.0f, due.line);
            return;
        }
    }
}

bool Announcer::speaking() const {
    if (m_current.playing())
        return true;
    return std::any_of(m_queue.begin(), m_queue.end(), [](const Queued& q) { return q.group != -1; });
}

void Announcer::stop() {
    // AudSpeech::Stop: EmptyQueue, then stop the line playing.
    for (auto& q : m_queue)
        q = {};
    if (m_current.playing())
        m_current.stop();
}

// --- CreatureVoice --------------------------------------------------------------------

void CreatureVoice::advanceClock(float dt) { g_impactClock += dt; }

void CreatureVoice::resetGlobals() {
    g_impactClock = 0.0f;
    g_lastAvoidLine = 0;
    g_lastImpactLine = 0;
}

void CreatureVoice::load(Mixer& mixer, SoundBank& bank, CreatureVoiceDef def, float maxDistance) {
    m_def = std::move(def);
    m_avoids.clear();
    for (const auto& t : m_def.triggers) {
        Avoid a;
        a.def = t;
        a.slots.resize(t.lines.size());
        for (std::size_t i = 0; i < t.lines.size(); ++i)
            a.slots[i].load(mixer, bank, t.lines[i].wave, Bus::Effects);
        m_avoids.push_back(std::move(a));
    }
    m_impactSlots.clear();
    m_impactSlots.resize(m_def.impactLines.size());
    for (std::size_t i = 0; i < m_def.impactLines.size(); ++i)
        m_impactSlots[i].load(mixer, bank, m_def.impactLines[i].wave, Bus::Effects);
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
        int line = static_cast<int>(randomizeNumber(static_cast<float>(n * 2)));
        if (n <= line)
            continue;
        if (line == g_lastAvoidLine && n <= ++line)
            line = 0;
        g_lastAvoidLine = line;
        a.line = static_cast<std::size_t>(line);
        a.queued = true;
        a.queueTime = 0.0f;
    }
}

void CreatureVoice::impact(float force) {
    // AudCreatureImpact::QueuePlay.
    if (m_impactQueued || m_impactSlots.empty() || g_impactClock < kImpactCooldown || force < m_def.minImpactForce)
        return;
    const int n = static_cast<int>(m_impactSlots.size());
    int line = static_cast<int>(randomizeNumber(static_cast<float>(n) - 0.01f));
    if (line == g_lastImpactLine && n <= ++line)
        line = 0;
    g_lastImpactLine = line;
    m_impactLine = static_cast<std::size_t>(line);
    m_impactQueued = true;
    m_impactTime = 0.0f;
}

void CreatureVoice::playAvoid(Avoid& a) {
    // AudCreatureAvoid::Play: only while the owner holds a sound slot (OpenMM2:
    // while the creature is within its drop-off).
    auto& slot = a.slots[a.line];
    if (slot.playing() || m_attenuation <= 0.0f)
        return;
    slot.setVolume(m_attenuation * a.def.lines[a.line].volume);
    slot.setPan(m_pan);
    slot.playOnce();
    a.queued = false;
}

void CreatureVoice::update(float speed, float dt, const Vec3& position, const Mat34& listener) {
    const bool inRange = m_3d.withinMaxDistance(position, listener.m3);
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
                slot.setVolume(m_attenuation * m_def.impactLines[m_impactLine].volume);
                slot.setPan(m_pan);
                slot.playOnce();
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
