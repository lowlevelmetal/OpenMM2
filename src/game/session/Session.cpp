#include "game/session/Session.h"

#include "core/Log.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <random>

namespace mm2::game::session {
namespace {

// Countdown: each message lasts 1.25 s; the first one shows until the wait
// that started at the mode's total drops to 1.25 s (mmSingleBlitz::UpdateGame
// starts it at 5 s, the other modes at 2.5 s).
constexpr float kStep = 1.25f;
constexpr float kPostRace = 5.0f;
constexpr float kDropY = -50.0f;           // mmGame::Update
constexpr float kWaterHandler = 5.0f;      // seconds in the water before HitWaterHandler
constexpr float kWaterLoseDelay = 0.5f;    // HitWaterHandler: menu 0.5 s later
constexpr float kTimerWarning = 10.0f;     // PlayTimerWarning below 10 s
constexpr float kTimerWarningLoop = 3.0f;  // continuous below 3 s
constexpr float kWreckPenalty = 5.0f;      // "Wait...5 second penalty"
constexpr float kWreckPause = 3.0f;        // cruise and some lessons: repaired after 3 s
constexpr float kIdleMusicDelay = 3.0f;    // inferred
constexpr float kChaseArrive2 = 10.0f * 10.0f;  // mmSingleStunt::UpdateChase
constexpr float kChaseEscape2 = 100.0f * 100.0f;
constexpr float kPursuitRange = 200.0f;    // mmSingleStunt::CheckCopPursuit
constexpr float kPursuitEyeHeight = 3.5f;
constexpr float kMinSpeedGrace = 1.0f;     // mmSingleStunt constructor: below the speed for 1 s fails
constexpr float kCleanMaxDamage = 10.0f;   // mmSingleStunt::UpdateFrogger
constexpr float kDestroyMaxDamage = 150000.0f; // mmSingleStunt::UpdateStop (mmSingleStunt constructor)

// String table ids per mode (US build).
struct ModeText {
    std::uint32_t ready = 0, set = 0, go = 0;
    std::uint32_t wreck = 0;    // single: race over; multiplayer: 5 s penalty
    std::uint32_t timeUp = 0;
    std::uint32_t won = 0;      // single Blitz
    std::uint32_t place = 0;    // "You finished 1st!" .. +7
    std::uint32_t loaf = 0;     // "You are a loaf"
    std::uint32_t finishedIn = 0; // multiplayer "finished in"
    float total = 2.5f;
};

ModeText modeText(GameMode m, bool multi) {
    ModeText t;
    if (multi) {
        switch (m) {
        case GameMode::Cruise: t.go = 154; t.wreck = 155; break;                     // mmMultiRoam
        case GameMode::Blitz: t = {89, 90, 91, 92, 94, 0, 0, 0, 93}; break;          // mmMultiBlitz
        case GameMode::Circuit: t = {101, 102, 103, 104, 108, 0, 0, 0, 105}; break;  // mmMultiCircuit
        case GameMode::Checkpoint: t = {144, 145, 146, 147, 0, 0, 0, 0, 148}; break; // mmMultiRace
        default: break;
        }
        return t;
    }
    switch (m) {
    case GameMode::Blitz: t = {158, 159, 160, 162, 161, 164, 0, 0, 0, 5.0f}; break; // mmSingleBlitz
    case GameMode::Circuit: t = {165, 166, 167, 168, 0, 0, 169, 177, 0}; break;     // mmSingleCircuit
    case GameMode::Checkpoint: t = {179, 180, 181, 182, 0, 0, 183, 191, 0}; break;  // mmSingleRace
    default: break;
    }
    return t;
}

// mmSingleStunt::Update* countdown strings: the messages of states 1 and 2
// and "Go".
struct LessonText {
    std::uint32_t intro = 0; // 0 = the lesson's name
    std::uint32_t ready = 0, set = 0, go = 0;
    float total = 5.0f;
};

LessonText lessonText(LessonType t) {
    switch (t) {
    case LessonType::Jump: return {0, 206, 207, 208};
    case LessonType::Collide: return {210, 211, 212, 213};
    case LessonType::Follow: return {0, 218, 219, 220};
    case LessonType::Evade: return {0, 193, 194, 195};
    case LessonType::MinimumSpeed: return {0, 232, 0, 233};
    case LessonType::Clean: return {0, 225, 226, 227};
    case LessonType::Acceleration: return {199, 200, 201, 202};
    case LessonType::Course:
    case LessonType::Map: return {0, 241, 242, 243, 2.5f};
    case LessonType::Destroy: return {0, 617, 618, 619};
    }
    return {};
}

// GetLocTime: "M:SS:HH" (hundredths, rounded by adding 0.005 and then
// truncated), "  ---  " for no time.
std::string formatTime(float seconds) {
    if (!(seconds > 0.0f))
        return "  ---  ";
    double whole = 0.0;
    const double fraction = std::modf(static_cast<double>(seconds) + 0.005, &whole);
    const int hundredths = static_cast<int>(fraction * 100.0);
    const int minutes = static_cast<int>(whole) / 60;
    const int secs = static_cast<int>(whole - static_cast<double>(minutes * 60));
    return std::format("{}:{:02}:{:02}", minutes, secs, hundredths);
}

float dist2(const Vec3& a, const Vec3& b) { return a.dist2(b); }

} // namespace

std::unique_ptr<Session> Session::create(const RaceConfig& config, const city::CityData& city, const vfs::Vfs& vfs,
                                         const Strings& strings, std::string* error, const SessionOptions& options) {
    std::uint32_t seed = options.seed;
    if (seed == 0)
        seed = std::random_device{}() | 1u;
    auto setup = loadRaceSetup(config, city, vfs, error, seed);
    if (!setup)
        return nullptr;
    std::unique_ptr<Session> s(new Session());
    s->m_setup = std::move(*setup);
    s->m_strings = &strings;
    s->m_city = str::lower(city.info.mapName);
    s->m_options = options;
    s->m_opponents.resize(s->m_setup.opponents.size());
    s->m_oppEnabled.assign(s->m_setup.opponents.size(), 0);
    s->resetRace();
    return s;
}

std::string Session::str(std::uint32_t id, std::string_view fallback) const {
    return m_strings ? m_strings->get(id, fallback) : std::string(fallback);
}

void Session::setMessage(std::string text, float seconds, bool top) {
    // mmHUD::SetMessage: replaces the message and clears the second line.
    m_message.text = std::move(text);
    m_message.timeLeft = seconds;
    m_message.top = top;
    m_message2 = HudMessage{{}, seconds, top};
}

void Session::setMessage(std::uint32_t id, std::string_view fallback, float seconds, bool top) {
    if (id == 0 && fallback.empty())
        return;
    setMessage(id ? str(id, fallback) : std::string(fallback), seconds, top);
}

void Session::setMessage2(std::string text) {
    m_message2.text = std::move(text);
    m_message2.timeLeft = m_message.timeLeft;
    m_message2.top = m_message.top;
}

const LessonEvent* Session::currentLesson() const {
    if (mode() != GameMode::CrashCourse || m_setup.lessonEvents.empty())
        return nullptr;
    return &m_setup.lessonEvents[static_cast<std::size_t>(m_lessonEvent)];
}

int Session::lessonOpponentOffset() const {
    // mmSingleStunt::GetOpponentIndex: the opponents of the earlier events.
    int offset = 0;
    for (int i = 0; i < m_lessonEvent && i < static_cast<int>(m_setup.lessonEvents.size()); ++i)
        offset += m_setup.lessonEvents[static_cast<std::size_t>(i)].opponents;
    return offset;
}

bool Session::opponentActive(std::size_t index) const {
    return index < m_oppEnabled.size() && m_oppEnabled[index] != 0;
}

Session::WaypointRule Session::rule() const {
    switch (mode()) {
    case GameMode::Blitz: return WaypointRule::Blitz;
    case GameMode::Circuit: return WaypointRule::Circuit;
    case GameMode::Checkpoint: return WaypointRule::CheckpointRace;
    case GameMode::CrashCourse:
        if (const auto* lesson = currentLesson(); lesson && lesson->hasCheckpoints)
            return lesson->type == LessonType::Jump ? WaypointRule::AnyOrderEnd : WaypointRule::InOrder;
        return WaypointRule::None;
    default: return WaypointRule::None;
    }
}

// --- Setup -------------------------------------------------------------------------

void Session::resetRace() {
    // mmGameSingle::Reset / DisableRacers and the modes' Reset: back to the
    // first event and the countdown.
    m_finishers = 0;
    m_rank = 1;
    for (auto& r : m_opponents)
        r = Racer{};
    std::fill(m_oppEnabled.begin(), m_oppEnabled.end(), 0);
    m_released = false;
    m_raceTime = 0.0f;
    m_raceClock = false;
    m_lapStart = m_lastLap = m_bestLap = 0.0f;
    m_timeUp = false;
    m_penaltyLeft = 0.0f;
    m_penaltyHeld = false;
    m_repairPending = false;
    m_waterTimer = 0.0f;
    m_waterHandled = false;
    m_postWait = 0.0f;
    m_resultFinished = m_resultWon = false;
    m_resultPosition = 0;
    m_resultTime = 0.0f;
    m_message = m_message2 = HudMessage{};
    m_vehicleHits = m_objectHits = 0;
    m_baseVehicleImpacts = m_baseObjectImpacts = -1;
    resetTimerWarning();
    beginEvent(0);
    m_respawn = m_setup.playerSpawn;
}

void Session::beginEvent(int index) {
    // mmSingleStunt::InitNewEvent / InitHUD for lessons; the race modes'
    // InitGameObjects otherwise.
    m_lessonEvent = index;
    const LessonEvent* lesson = currentLesson();
    m_checkpoints = lesson ? lesson->checkpoints : m_setup.checkpoints;
    m_wp.singleVisible = lesson && lesson->singleCheckpoint;
    resetWaypoints();

    m_phase = mode() == GameMode::Cruise ? Phase::Racing : Phase::Countdown;
    m_stage = Stage::Intro;
    m_wait = 0.0f;
    m_skipToGo = index > 0;
    m_reachedMinSpeed = false;
    m_belowMinSpeed = 0.0f;
    m_lessonDone = false;
    m_accelWait = -1.0f;

    m_hasClock = false;
    m_clockRunning = false;
    m_clock = 0.0f;
    if (mode() == GameMode::Blitz && m_setup.timeLimit > 0.0f) {
        m_hasClock = true;
        m_clock = m_setup.timeLimit;
    } else if (lesson) {
        m_clock = lesson->timeLimit;
        switch (lesson->type) {
        case LessonType::Follow: m_hasClock = false; break;
        case LessonType::MinimumSpeed: m_hasClock = lesson->timeLimit != 0.0f; break;
        default: m_hasClock = true; break;
        }
    }
    resetTimerWarning();
    if (lesson)
        push(EventType::LessonEventStarted, index);
}

void Session::resetWaypoints() {
    // mmWaypoints::Reset.
    const int n = static_cast<int>(m_checkpoints.size());
    m_wp.cleared.assign(m_checkpoints.size(), 0);
    m_wp.visible.assign(m_checkpoints.size(), m_wp.singleVisible ? 0 : 1);
    m_wp.current = std::min(1, std::max(0, n - 1));
    m_wp.count = 1;
    m_wp.lastCleared = 0;
    m_wp.lap = 0;
    m_wp.finished = false;
    m_wp.stopped = false;
    const WaypointRule r = rule();
    if (n == 0 || r == WaypointRule::None)
        return;
    if (r != WaypointRule::Circuit) {
        m_wp.visible[0] = 0;
        if (r == WaypointRule::CheckpointRace && n >= 3)
            m_wp.visible[static_cast<std::size_t>(n - 1)] = 0; // the finish opens later
        m_wp.cleared[0] = 1;
    }
    if (m_wp.singleVisible) {
        std::fill(m_wp.visible.begin(), m_wp.visible.end(), 0);
        m_wp.visible[static_cast<std::size_t>(m_wp.current)] = 1;
    }
}

void Session::start() {
    m_started = true;
    if (mode() == GameMode::Cruise) {
        // mmSingleRoam has no countdown; mmMultiRoam says "Go!".
        m_phase = Phase::Racing;
        if (multiplayer())
            setMessage(modeText(mode(), true).go, "Go!", kStep, false);
        m_released = true;
        return;
    }
    if (m_options.skipCountdown)
        go();
}

void Session::restart() {
    resetRace();
    push(EventType::Restart);
    if (m_started)
        start();
}

bool Session::playerHeld() const {
    if (!m_started)
        return true;
    if (m_phase == Phase::Countdown)
        return true;
    // mmPlayer::Update brakes the car once the race is over (+0x2258).
    if (m_phase == Phase::PostRace || m_phase == Phase::Done)
        return true;
    return m_penaltyHeld && m_penaltyLeft > 0.0f;
}

// --- Countdown ---------------------------------------------------------------------

void Session::updateCountdown(float dt) {
    if (m_skipToGo) {
        // Later events of an exam start at once (state 0 -> 2 with no wait).
        go();
        return;
    }
    const LessonEvent* lesson = currentLesson();
    ModeText mt = modeText(mode(), multiplayer());
    LessonText lt;
    if (lesson) {
        lt = lessonText(lesson->type);
        mt.ready = lt.ready;
        mt.set = lt.set;
        mt.total = lt.total;
    }
    switch (m_stage) {
    case Stage::Intro:
        if (lesson) {
            if (lesson->type == LessonType::Collide) {
                setMessage(lt.intro, "", kStep, true);
            } else {
                // The lesson's name (the menu's selection) or the event's own
                // line, until 1.25 s have passed.
                const auto lessonIndex = static_cast<std::uint32_t>(std::max(0, m_setup.config.raceIndex));
                const std::uint32_t nameId =
                    Strings::kFirstCrashCourseLesson + (m_city == "sf" ? 13u : 0u) + lessonIndex;
                setMessage(lt.intro ? str(lt.intro, "") : str(nameId, ""), kStep, true);
                if (m_wait <= kStep) {
                    m_wait += dt;
                    return;
                }
            }
        }
        m_stage = Stage::Ready;
        m_wait = mt.total;
        push(EventType::CountdownReady);
        return;
    case Stage::Ready:
        m_wait -= dt;
        if (m_wait > kStep) {
            if (lesson && lesson->type == LessonType::MinimumSpeed) {
                std::string text = str(mt.ready, "Maintain %.0f through the checkpoints");
                if (const auto pos = text.find("%.0f"); pos != std::string::npos)
                    text.replace(pos, 4, std::format("{:.0f}", lesson->minimumSpeedMph));
                setMessage(text, 2.0f * kStep, true);
            } else {
                setMessage(mt.ready, "Ready...", kStep, true);
            }
            return;
        }
        m_stage = Stage::Set;
        m_wait = kStep;
        push(EventType::CountdownSet);
        // mmSingleStunt::UpdateFrogger: from here a scrape wrecks the car.
        if (lesson && lesson->type == LessonType::Clean)
            push(EventType::PlayerDamageLimits, -1, kCleanMaxDamage);
        return;
    case Stage::Set: {
        m_wait -= dt;
        const bool corner = lesson && lesson->type == LessonType::MinimumSpeed;
        if (corner ? m_wait >= 0.0f : m_wait > 0.0f) {
            if (mt.set)
                setMessage(mt.set, "Set...", kStep, true);
            return;
        }
        go();
        return;
    }
    }
}

void Session::go() {
    m_phase = Phase::Racing;
    m_skipToGo = false;
    m_released = true;
    m_raceTime = 0.0f;
    m_raceClock = true;
    m_lapStart = 0.0f;
    if (const LessonEvent* lesson = currentLesson()) {
        const LessonText lt = lessonText(lesson->type);
        if (lesson->type == LessonType::Evade && m_lessonEvent > 0)
            setMessage(652, "Get going!  Lose the tail\\nbefore you finish", 3.0f, true);
        else
            setMessage(lt.go, "Go!", kStep, true);
        m_clockRunning = m_hasClock;
        // mmSingleStunt::EnableRacers: opponents from the earlier events'
        // count up to this event's "numopp" (the original's loop bound).
        const int enabled = std::min(lesson->opponents, static_cast<int>(m_oppEnabled.size()));
        for (int i = lessonOpponentOffset(); i < enabled; ++i)
            m_oppEnabled[static_cast<std::size_t>(i)] = 1;
        if (lesson->type == LessonType::Destroy && !m_opponents.empty()) {
            const int target = lessonOpponentOffset();
            if (target < static_cast<int>(m_opponents.size()))
                push(EventType::OpponentDamageLimits, target, kDestroyMaxDamage);
        }
    } else {
        setMessage(modeText(mode(), multiplayer()).go, "Go!", kStep, true);
        m_clockRunning = m_hasClock;
        std::fill(m_oppEnabled.begin(), m_oppEnabled.end(), 1);
    }
    push(EventType::CountdownGo);
}

// --- Checkpoints (mmWaypoints::Update) ---------------------------------------------

void Session::setTarget(int index) {
    // mmWaypoints::SetCurrentGoals.
    const int n = static_cast<int>(m_checkpoints.size());
    m_wp.current = index < 0 ? 0 : std::min(index, n - 1);
}

void Session::closestTarget(const Vec3& pos) {
    // mmWaypoints::GetClosestWaypoint: the nearest shown, uncleared one.
    const int n = static_cast<int>(m_checkpoints.size());
    int best = m_wp.current, found = 0;
    float bestD = 1e9f;
    for (int i = 1; i < n; ++i) {
        const auto k = static_cast<std::size_t>(i);
        if (m_wp.cleared[k] || !m_wp.visible[k])
            continue;
        ++found;
        const float d = dist2(m_checkpoints[k].position, pos);
        if (d < bestD && best != 0) {
            bestD = d;
            best = i;
        }
    }
    setTarget(found ? best : m_wp.current);
}

void Session::cycleTarget(bool forward) {
    // mmWaypoints::CycleCurrentWaypoint; the input only reaches checkpoint
    // races (mmSingleRace::UpdateGameInput; Blitz checks the same waypoint
    // type and so never cycles).
    const int n = static_cast<int>(m_checkpoints.size());
    if (n < 3 || m_wp.finished)
        return;
    if (rule() != WaypointRule::CheckpointRace && rule() != WaypointRule::AnyOrderEnd)
        return;
    if (m_wp.count == n - 1) {
        setTarget(n - 1);
        return;
    }
    const int step = forward ? 1 : -1;
    int i = m_wp.current;
    for (int guard = 0;; ++guard) {
        i = (i + step) % n;
        if (i == 0 || i == n - 1)
            i = forward ? 1 : n - 2;
        if (!m_wp.cleared[static_cast<std::size_t>(i)])
            break;
        if (i == m_wp.current || guard > n)
            return;
    }
    setTarget(i);
}

void Session::displayCleared(int index) {
    // mmWaypoints::DisplayHUDMessage.
    const auto k = static_cast<std::size_t>(index);
    m_wp.lastCleared = index;
    m_wp.cleared[k] = 1;
    m_wp.visible[k] = 0;
    ++m_wp.count;
    push(EventType::CheckpointCleared, index);
}

void Session::updateWaypoints(const PlayerState& player) {
    const int n = static_cast<int>(m_checkpoints.size());
    const WaypointRule r = rule();
    if (n < 2 || r == WaypointRule::None || m_wp.stopped)
        return;
    const Mat34& car = player.transform;
    auto hit = [&](int i) {
        const Checkpoint& cp = m_checkpoints[static_cast<std::size_t>(i)];
        if (r == WaypointRule::AnyOrderEnd && cp.hitByRadius)
            return radiusHit(cp, car.m3);
        return playerGateHit(cp, car, player.inertiaBox);
    };
    // mmWaypoints::ClearWaypoint: the first uncleared waypoint hit.
    auto firstHit = [&]() {
        for (int i = 0; i < n; ++i)
            if (!m_wp.cleared[static_cast<std::size_t>(i)] && hit(i))
                return i;
        return -1;
    };

    switch (r) {
    case WaypointRule::Blitz: {
        const int idx = firstHit();
        if (idx >= 0 && !m_wp.finished) {
            displayCleared(idx);
            if (std::any_of(m_wp.cleared.begin(), m_wp.cleared.end(), [](char c) { return c == 0; })) {
                closestTarget(car.m3);
                if (m_wp.singleVisible)
                    m_wp.visible[static_cast<std::size_t>(m_wp.current)] = 1;
            } else {
                m_wp.finished = true; // the last checkpoint ends a Blitz
            }
        }
        break;
    }
    case WaypointRule::CheckpointRace:
    case WaypointRule::AnyOrderEnd: {
        const int idx = firstHit();
        if (idx < 0 || m_wp.finished)
            break;
        if (idx == n - 1 && m_wp.count == n - 1) {
            m_wp.finished = true;
        } else if (idx > 0 && idx < n - 1) {
            displayCleared(idx);
            if (r == WaypointRule::CheckpointRace) {
                if (m_wp.count == n - 1) {
                    m_wp.visible[static_cast<std::size_t>(n - 1)] = 1;
                    push(EventType::FinishActivated);
                }
                closestTarget(car.m3);
            } else if (idx == m_wp.current) {
                cycleTarget(true);
            }
            if (m_wp.singleVisible)
                m_wp.visible[static_cast<std::size_t>(m_wp.current)] = 1;
            if (m_wp.count == n - 1) {
                m_wp.visible[static_cast<std::size_t>(m_wp.current)] = 1;
                push(EventType::FinalCheckpoint);
            }
        }
        break;
    }
    case WaypointRule::InOrder:
        if (!m_wp.finished && hit(m_wp.current)) {
            const int passed = m_wp.current;
            m_wp.visible[static_cast<std::size_t>(passed)] = 0;
            ++m_wp.current;
            displayCleared(passed);
            if (m_wp.count == n)
                m_wp.finished = true;
            else if (m_wp.singleVisible)
                m_wp.visible[static_cast<std::size_t>(m_wp.current)] = 1;
            if (m_wp.current >= n)
                m_wp.current = n - 1;
        }
        break;
    case WaypointRule::Circuit:
        if (!m_wp.finished && hit(m_wp.current)) {
            const int passed = m_wp.current;
            m_wp.visible[static_cast<std::size_t>(passed)] = 0;
            displayCleared(passed);
            if (passed == 0) {
                ++m_wp.lap;
                const float lapTime = m_raceTime - m_lapStart;
                m_lastLap = lapTime;
                m_bestLap = m_bestLap > 0.0f ? std::min(m_bestLap, lapTime) : lapTime;
                m_lapStart = m_raceTime;
                push(EventType::LapCompleted, m_wp.lap, lapTime);
                if (m_wp.lap == m_setup.laps) {
                    m_wp.finished = true;
                } else {
                    // mmHUD::PostLapTime: "Final lap!" or "Lap time" for 1 s
                    // with the time under it.
                    const bool final = m_wp.lap == m_setup.laps - 1;
                    setMessage(final ? 62 : 63, final ? "Final lap!" : "Lap time", 1.0f, false);
                    setMessage2(formatTime(lapTime));
                    if (final)
                        push(EventType::FinalLap);
                    // mmWaypoints::ResetAllTags: every gate shows again.
                    std::fill(m_wp.cleared.begin(), m_wp.cleared.end(), 0);
                    std::fill(m_wp.visible.begin(), m_wp.visible.end(), m_wp.singleVisible ? 0 : 1);
                }
            } else if (passed == n - 1 && m_wp.lap == m_setup.laps - 1) {
                push(EventType::FinalCheckpoint);
            }
            m_wp.current = passed + 1 == n ? 0 : passed + 1;
            if (m_wp.singleVisible && !m_wp.finished)
                m_wp.visible[static_cast<std::size_t>(m_wp.current)] = 1;
        }
        break;
    case WaypointRule::None: break;
    }
}

// --- Opponents ---------------------------------------------------------------------

void Session::updateOpponents(std::span<const OpponentState> opponents) {
    // mmSingleCircuit / mmSingleRace::UpdateOpponentStatus.
    const int n = static_cast<int>(m_checkpoints.size());
    if (n < 2 || (mode() != GameMode::Circuit && mode() != GameMode::Checkpoint))
        return;
    for (std::size_t i = 0; i < m_opponents.size() && i < opponents.size(); ++i) {
        Racer& r = m_opponents[i];
        const OpponentState& s = opponents[i];
        if (mode() == GameMode::Circuit) {
            if (aiGateHit(m_checkpoints[static_cast<std::size_t>(r.count % n)], s.transform, s.inertiaBox))
                ++r.count;
        } else {
            // mmWaypoints::AnyWPHits: the first unpassed one within 50 m.
            for (int j = 1; j < n && j < 32; ++j) {
                const std::uint32_t bit = 1u << j;
                const Checkpoint& cp = m_checkpoints[static_cast<std::size_t>(j)];
                if ((r.mask & bit) || dist2(s.transform.m3, cp.position) > sq(kAnyWaypointRange))
                    continue;
                if (aiGateHit(cp, s.transform, s.inertiaBox)) {
                    r.mask |= bit;
                    ++r.count;
                    break;
                }
            }
        }
        // The AI decides when it is done (aiRouteRacer::Finished).
        if (!r.finished && s.finished) {
            r.finished = true;
            r.place = ++m_finishers;
            r.finishTime = m_raceTime;
            push(EventType::OpponentFinished, static_cast<int>(i), static_cast<float>(r.place));
            if (m_phase == Phase::Racing) {
                // FinishMessage(opponent, place): "Opponent N" / "finished Nth".
                setMessage(13 + static_cast<std::uint32_t>(std::min<std::size_t>(i, 7)),
                           std::format("Opponent {}", i + 1), 5.0f, false);
                setMessage2(str(21 + static_cast<std::uint32_t>(std::min(r.place - 1, 7)), "finished"));
            }
        }
    }
}

void Session::updateRank(const PlayerState& player, std::span<const OpponentState> opponents) {
    // mmSingleCircuit / mmSingleRace::UpdateScore: opponents with more
    // waypoints passed, or finished, or level and nearer the player's target.
    const int n = static_cast<int>(m_checkpoints.size());
    if (n < 2 || m_opponents.empty())
        return;
    const Vec3 target = m_checkpoints[static_cast<std::size_t>(std::clamp(m_wp.current, 0, n - 1))].position;
    const float mine = dist2(player.transform.m3, target);
    int rank = 1;
    for (std::size_t i = 0; i < m_opponents.size(); ++i) {
        const Racer& r = m_opponents[i];
        if (m_wp.count < r.count || r.finished)
            ++rank;
        else if (m_wp.count == r.count && i < opponents.size() &&
                 dist2(opponents[i].transform.m3, target) < mine)
            ++rank;
    }
    m_rank = rank;
}

// --- Hazards (mmGame::Update) ------------------------------------------------------

bool Session::updateHazards(float dt, const PlayerState& player) {
    if (player.transform.m3.y < kDropY) {
        dropThroughCity();
        return true;
    }
    if (!player.inWater || m_waterHandled)
        return false;
    if (m_waterTimer < 0.1f) {
        if (m_waterTimer == 0.0f)
            push(EventType::HitWater);
        if (m_city == "london")
            setMessage(642, "More tea, vicar?", 3.0f, true);
        else if (m_city == "sf")
            setMessage(643, "Sleep with the fishes!", 3.0f, true);
        else
            setMessage(30, "Sleep with the fishes!", 3.0f, true);
    }
    m_waterTimer += dt;
    if (m_waterTimer > kWaterHandler) {
        m_waterHandled = true;
        m_waterTimer = 0.0f;
        hitWater();
        return true;
    }
    return false;
}

void Session::hitWater() {
    if (multiplayer()) {
        // mmGameMulti::HitWaterHandler: back to the last checkpoint (cruise:
        // the start, mmPlayer::Reset).
        if (rule() == WaypointRule::None) {
            m_respawn = m_setup.playerSpawn;
            m_waterHandled = false;
            push(EventType::Respawn);
        } else {
            respawnAtLastCheckpoint();
        }
        return;
    }
    switch (mode()) {
    case GameMode::Cruise:
        // mmSingleRoam::HitWaterHandler resets the game.
        restart();
        break;
    case GameMode::Blitz:
    case GameMode::Checkpoint:
        // mmSingleBlitz / mmSingleRace::HitWaterHandler: the race is lost.
        m_wp.stopped = true;
        endRace(false, false, kWaterLoseDelay);
        break;
    case GameMode::Circuit: respawnAtLastCheckpoint(); break;
    case GameMode::CrashCourse:
        if (!m_lessonDone)
            lessonFailed(kWaterLoseDelay);
        break;
    default: break;
    }
}

void Session::dropThroughCity() {
    // mmGame::DropThruCityHandler resets the game; mmGameMulti's treats it
    // as hitting the water.
    if (multiplayer())
        hitWater();
    else
        restart();
}

void Session::respawnAtLastCheckpoint() {
    // mmSingleCircuit / mmGameMulti::HitWaterHandler: the last waypoint
    // cleared, facing its heading.
    if (!m_checkpoints.empty())
        m_respawn = spawnAt(m_checkpoints[static_cast<std::size_t>(m_wp.lastCleared)]);
    m_waterHandled = false;
    m_waterTimer = 0.0f;
    push(EventType::Respawn);
}

// --- Clocks ------------------------------------------------------------------------

void Session::updateClock(float dt) {
    if (m_raceClock)
        m_raceTime += dt;
    if (m_clockRunning) {
        // mmTimer counting down stops at 0.
        m_clock -= dt;
        if (m_clock <= 0.0f) {
            m_clock = 0.0f;
            m_clockRunning = false;
        }
    }
}

void Session::resetTimerWarning() {
    m_warnAcc = 1.0f;
    m_warnBeeped = false;
    m_warnLoop = false;
}

void Session::timerWarning(float dt) {
    // PlayTimerWarning: a beep about once a second, continuous from 3 s.
    if (m_clock > kTimerWarningLoop) {
        if (m_warnAcc >= 1.0f && !m_warnBeeped) {
            m_warnBeeped = true;
            push(EventType::TimerWarning, 0, m_clock);
        }
        if (m_warnAcc > 1.0f) {
            m_warnAcc = 0.0f;
            m_warnBeeped = false;
        }
        m_warnAcc += dt;
    } else if (!m_warnLoop) {
        m_warnLoop = true;
        push(EventType::TimerWarning, 1, m_clock);
    }
}

void Session::startPenalty(float seconds, bool held) {
    m_penaltyLeft = seconds;
    m_penaltyHeld = held;
    push(EventType::WreckPenalty, -1, seconds);
}

float Session::timeRemaining() const { return m_hasClock ? m_clock : -1.0f; }

float Session::lapTime() const { return m_raceTime - m_lapStart; }

// --- Race modes --------------------------------------------------------------------

void Session::endRace(bool finished, bool won, float delay) {
    if (m_phase == Phase::PostRace || m_phase == Phase::Done)
        return;
    m_phase = Phase::PostRace;
    m_postWait = delay;
    m_raceClock = false;
    m_clockRunning = false;
    m_penaltyLeft = 0.0f;
    m_resultFinished = finished;
    m_resultWon = won;
    m_resultTime = m_raceTime;
}

void Session::playerFinished() {
    const int place = ++m_finishers;
    m_resultPosition = place;
    m_rank = place;
    push(EventType::PlayerFinished, place, m_raceTime);
}

void Session::updateRace(float dt, const PlayerState& player) {
    const ModeText mt = modeText(mode(), multiplayer());
    const bool pro = m_setup.config.difficulty == Difficulty::Professional;
    const bool penalty = m_penaltyLeft > 0.0f;
    auto wreckPenalty = [&] {
        if (wrecked(player) && !penalty) {
            push(EventType::Wrecked);
            setMessage(mt.wreck, "Wait...5 second penalty!", 5.0f, false);
            startPenalty(kWreckPenalty);
        }
    };

    if (multiplayer()) {
        // mmMultiRoam / Blitz / Circuit / Race: wrecks cost 5 s, never the race.
        if (mode() == GameMode::Blitz && m_hasClock && m_clock < kTimerWarning)
            timerWarning(dt);
        wreckPenalty();
        if (m_wp.finished) {
            playerFinished();
            setMessage(std::format("{} {}", str(mt.finishedIn, "finished in"), formatTime(m_raceTime)), 5.0f,
                       false);
            // Blitz shows the results when the clock would have run out;
            // the others 3 s after the finish.
            const float wait = mode() == GameMode::Blitz ? m_clock : 3.0f;
            endRace(true, true, wait);
            return;
        }
        if (mode() == GameMode::Blitz && m_hasClock && m_clock <= 0.0f) {
            m_wp.stopped = true;
            m_timeUp = true;
            push(EventType::TimeUp);
            setMessage(mt.timeUp, "Time's up!", 5.0f, false);
            endRace(false, false, kPostRace);
        }
        return;
    }

    switch (mode()) {
    case GameMode::Cruise:
        // mmSingleRoam::UpdateGame: a wreck parks the car for 3 s, then it
        // is repaired.
        if (wrecked(player) && !penalty) {
            push(EventType::Wrecked);
            startPenalty(kWreckPause);
        }
        break;
    case GameMode::Blitz:
        // mmSingleBlitz::UpdateGame state 3.
        if (m_hasClock && m_clock < kTimerWarning && !m_timeUp)
            timerWarning(dt);
        if (m_wp.finished) {
            if (m_timeUp) {
                m_wp.stopped = true;
                setMessage(mt.timeUp, "Time's up!", 5.0f, false);
                endRace(false, false, kPostRace);
            } else {
                playerFinished();
                setMessage(mt.won, "You Won!", 5.0f, true);
                endRace(true, true, kPostRace);
            }
            return;
        }
        if (m_hasClock && m_clock <= 0.0f && !m_timeUp) {
            // Time's up, but the race goes on: finishing now only ends it.
            m_timeUp = true;
            push(EventType::TimeUp);
            setMessage(mt.timeUp, "Time's up!", 5.0f, false);
        }
        if (wrecked(player)) {
            m_wp.stopped = true;
            push(EventType::Wrecked);
            setMessage(mt.wreck, "Game over!", 5.0f, false);
            endRace(false, false, kPostRace);
        }
        break;
    case GameMode::Circuit:
        // mmSingleCircuit::UpdateGame states 3 and 6.
        wreckPenalty();
        if (m_wp.finished) {
            playerFinished();
            const int place0 = m_resultPosition - 1;
            setMessage(place0 < 8 ? mt.place + static_cast<std::uint32_t>(place0) : mt.loaf,
                       std::format("You finished #{}", m_resultPosition), 5.0f, true);
            // mmSingleCircuit::ProgressCheck: top three, professionals first.
            endRace(true, m_resultPosition < (pro ? 2 : 4), kPostRace);
        }
        break;
    case GameMode::Checkpoint:
        // mmSingleRace::UpdateGame state 3.
        if (m_wp.finished) {
            playerFinished();
            const int place0 = m_resultPosition - 1;
            setMessage(place0 < 8 ? mt.place + static_cast<std::uint32_t>(place0) : mt.loaf,
                       std::format("You finished #{}", m_resultPosition), 5.0f, place0 < 8);
            endRace(true, m_resultPosition < (pro ? 2 : 4), kPostRace);
            return;
        }
        if (wrecked(player)) {
            m_wp.stopped = true;
            push(EventType::Wrecked);
            setMessage(mt.wreck, "Game over!", 5.0f, false);
            endRace(false, false, kPostRace);
        }
        break;
    default: break;
    }
}

// --- Crash course (mmSingleStunt) --------------------------------------------------

bool Session::copPursuit(const PlayerState& player, std::span<const OpponentState> police) const {
    // mmSingleStunt::CheckCopPursuit: a pursuing cop in sight within 200 m.
    const Vec3 eye = player.transform.m3 + Vec3{0.0f, kPursuitEyeHeight, 0.0f};
    for (const auto& cop : police) {
        if (!cop.pursuing)
            continue;
        const Vec3 copEye = cop.transform.m3 + Vec3{0.0f, kPursuitEyeHeight, 0.0f};
        if (m_options.lineOfSight && !m_options.lineOfSight(eye, copEye))
            continue;
        if (eye.dist(copEye) < kPursuitRange)
            return true;
    }
    return false;
}

void Session::lessonFailed(float delay) {
    m_lessonDone = true;
    m_wp.stopped = true;
    push(EventType::LessonFailed, m_lessonEvent);
    endRace(false, false, delay);
}

void Session::lessonPassedOrNext(std::uint32_t passMessage, float seconds, bool top, float delay) {
    m_lessonDone = true;
    const int events = static_cast<int>(m_setup.lessonEvents.size());
    if (m_lessonEvent == events - 1) {
        setMessage(passMessage, "Good driving!", seconds, top);
        push(EventType::LessonPassed, m_lessonEvent);
        endRace(true, true, delay);
        return;
    }
    // An exam continues with its next event where the car is
    // (mmSingleStunt::InitNewEvent; no respawn).
    beginEvent(m_lessonEvent + 1);
}

void Session::updateLesson(float dt, const PlayerState& player, std::span<const OpponentState> opponents,
                           std::span<const OpponentState> police) {
    const LessonEvent* lesson = currentLesson();
    if (!lesson)
        return;

    // Collision counters (the Clean lesson's HUD; "Hit Objects:", "Hit Vehicles:").
    if (m_baseVehicleImpacts < 0) {
        m_baseVehicleImpacts = player.vehicleImpacts;
        m_baseObjectImpacts = player.objectImpacts;
    }
    m_vehicleHits = player.vehicleImpacts - m_baseVehicleImpacts;
    m_objectHits = player.objectImpacts - m_baseObjectImpacts;

    // A wrecked car waits (state 5) before it is repaired.
    if (m_penaltyLeft > 0.0f)
        return;
    auto pause = [&] {
        push(EventType::Wrecked);
        startPenalty(kWreckPause);
    };
    const bool timeOut = m_hasClock && m_clock <= 0.0f;
    const int targetIndex = lessonOpponentOffset();
    const OpponentState* target = targetIndex < static_cast<int>(opponents.size())
                                      ? &opponents[static_cast<std::size_t>(targetIndex)]
                                      : nullptr;

    switch (lesson->type) {
    case LessonType::Jump: // UpdateJump
        if (m_wp.finished) {
            lessonPassedOrNext(209, 5.0f, true, kPostRace);
        } else if (timeOut) {
            setMessage(614, "Time's up!", 5.0f, false);
            push(EventType::TimeUp);
            lessonFailed();
        } else if (wrecked(player)) {
            push(EventType::Wrecked);
            setMessage(615, "Game over!", 5.0f, false);
            lessonFailed();
        }
        break;
    case LessonType::Collide: // UpdateCollide (no retail lesson; nothing is recorded)
        if (m_hasClock && m_clock < kTimerWarning && !m_lessonDone)
            timerWarning(dt);
        if (timeOut && !m_lessonDone) {
            m_lessonDone = true;
            setMessage(214, "Time's up!", kStep, true);
            push(EventType::TimeUp);
            endRace(false, false, kPostRace);
        } else if (m_wp.finished) {
            m_lessonDone = true;
            setMessage(215, "Good driving!", kStep, true);
            endRace(true, false, 3.0f);
        } else if (wrecked(player)) {
            push(EventType::Wrecked);
            setMessage(216, "Wait...5 second penalty", 5.0f, false);
            startPenalty(kWreckPenalty);
        }
        break;
    case LessonType::Follow: { // UpdateChase
        if (wrecked(player)) {
            pause();
            break;
        }
        if (!target)
            break;
        const float d2 = dist2(player.transform.m3, target->transform.m3);
        if ((target->finished || m_wp.finished) && d2 < kChaseArrive2) {
            lessonPassedOrNext(221, 5.0f, true, kPostRace);
            break;
        }
        if (d2 > kChaseEscape2) {
            setMessage(222, "Car escaped!", 5.0f, true);
            setMessage2(str(223, "Game over"));
            lessonFailed();
        }
        break;
    }
    case LessonType::Evade: // UpdateEvade
        if (wrecked(player))
            pause();
        if (m_wp.finished) {
            if (copPursuit(player, police)) {
                setMessage(197, "Lose your pursuers before you finish!", 5.0f, true); // HUDMessage
                lessonFailed();
            } else {
                setMessage(196, "You survived the gauntlet!", 5.0f, true);
                lessonPassedOrNext(196, 5.0f, true, kPostRace);
            }
        } else if (timeOut) {
            setMessage(198, "Time's up!  Drive faster to win!", 5.0f, false);
            push(EventType::TimeUp);
            lessonFailed();
        }
        break;
    case LessonType::MinimumSpeed: { // UpdateCorner
        const float speed = player.speedMph;
        const float minimum = lesson->minimumSpeedMph;
        if (m_wp.finished) {
            if (copPursuit(player, police)) {
                setMessage(197, "Lose your pursuers before you finish!", 5.0f, true);
                lessonFailed();
            } else {
                lessonPassedOrNext(234, 5.0f, true, kPostRace);
            }
            break;
        }
        if (lesson->timeLimit != 0.0f && timeOut) {
            setMessage(244, "Time's up!", 5.0f, false); // CheckTimeUp
            push(EventType::TimeUp);
            lessonFailed();
            break;
        }
        if (!m_reachedMinSpeed) {
            if (m_wp.count > 1) {
                setMessage(235, "You need to get up to speed", 3.0f, true);
                setMessage2(str(236, "before hitting a checkpoint"));
                lessonFailed();
                break;
            }
            if (minimum < speed) {
                m_reachedMinSpeed = true;
                setMessage(237, "You have reached the minimum speed!", 3.0f, false);
            }
        }
        if (m_belowMinSpeed > 0.0f && minimum < speed)
            m_belowMinSpeed = 0.0f;
        if (m_reachedMinSpeed && speed < minimum) {
            m_belowMinSpeed += dt;
            if (m_belowMinSpeed > kMinSpeedGrace) {
                setMessage(238, "You have not maintained \\n the minimum speed!", 5.0f, true);
                setMessage2(str(239, "You lost!"));
                lessonFailed();
                break;
            }
        }
        if (wrecked(player)) {
            push(EventType::Wrecked);
            setMessage(240, "Game over!", 5.0f, true);
            lessonFailed();
        }
        break;
    }
    case LessonType::Clean: // UpdateFrogger
        if (m_hasClock && m_clock < kTimerWarning && !m_lessonDone)
            timerWarning(dt);
        if (timeOut && !m_lessonDone) {
            setMessage(228, "Time's up!", kStep, true);
            push(EventType::TimeUp);
            lessonFailed();
        } else if (m_wp.finished) {
            lessonPassedOrNext(229, kStep, true, 3.0f);
        } else if (wrecked(player)) {
            push(EventType::Wrecked);
            setMessage(230, "You scraped the paint!", 5.0f, false);
            lessonFailed();
        }
        break;
    case LessonType::Acceleration: // UpdateAccel (no retail lesson)
        if (m_accelWait >= 0.0f) {
            m_accelWait -= dt;
            if (m_accelWait < 0.0f)
                lessonPassedOrNext(204, 5.0f, true, kPostRace);
            break;
        }
        if (wrecked(player))
            pause();
        if (m_wp.finished) {
            m_accelWait = 2.0f;
        } else if (timeOut) {
            setMessage(203, "Put the pedal to the metal!", 5.0f, false);
            push(EventType::TimeUp);
            lessonFailed();
        }
        break;
    case LessonType::Course:
    case LessonType::Map: // UpdateBlitz
        if (m_wp.finished) {
            lessonPassedOrNext(609, 5.0f, false, kPostRace);
        } else if (timeOut) {
            setMessage(244, "Time's up!", 5.0f, false); // CheckTimeUp
            push(EventType::TimeUp);
            lessonFailed();
        } else if (wrecked(player)) {
            push(EventType::Wrecked);
            setMessage(245, "Game over!", 5.0f, false);
            lessonFailed();
        }
        break;
    case LessonType::Destroy: // UpdateStop
        if (target && target->currentDamage >= kDestroyMaxDamage) {
            m_oppEnabled[static_cast<std::size_t>(targetIndex)] = 0; // the car stops
            lessonPassedOrNext(620, 5.0f, true, kPostRace);
            break;
        }
        if (target && target->finished) {
            setMessage(621, "Car reached destination", 5.0f, true);
            setMessage2(str(622, "You lost!"));
            lessonFailed();
            break;
        }
        if (timeOut) {
            setMessage(244, "Time's up!", 5.0f, false);
            push(EventType::TimeUp);
            lessonFailed();
            break;
        }
        if (wrecked(player))
            pause();
        break;
    }
}

// --- Frame -------------------------------------------------------------------------

void Session::update(float dt, const PlayerState& player, std::span<const OpponentState> opponents,
                     std::span<const OpponentState> police) {
    if (!m_started)
        return;
    if (m_message.timeLeft > 0.0f) {
        m_message.timeLeft -= dt;
        m_message2.timeLeft = m_message.timeLeft;
        if (m_message.timeLeft <= 0.0f)
            m_message = m_message2 = HudMessage{};
    }
    m_resultDamage = player.damage01;
    m_idleTime = player.speedMph < 2.0f ? m_idleTime + dt : 0.0f;

    // The DamageReset of the previous update has been applied by now.
    m_repairPending = false;
    if (m_penaltyLeft > 0.0f) {
        m_penaltyLeft -= dt;
        if (m_penaltyLeft <= 0.0f) {
            m_penaltyLeft = 0.0f;
            m_penaltyHeld = false;
            m_repairPending = true;
            push(EventType::DamageReset);
        }
    }

    switch (m_phase) {
    case Phase::Countdown:
        updateCountdown(dt);
        if (updateHazards(dt, player))
            return;
        updateWaypoints(player);
        updateRank(player, opponents);
        return;
    case Phase::PostRace:
        updateClock(dt);
        updateOpponents(opponents);
        m_postWait -= dt;
        if (m_postWait <= 0.0f) {
            m_phase = Phase::Done;
            push(EventType::SessionOver);
        }
        return;
    case Phase::Done: return;
    case Phase::Racing: break;
    }

    // The rules see the checkpoints as they were after the previous frame's
    // mmWaypoints::Update (it runs after UpdateGame in the original).
    if (mode() == GameMode::CrashCourse)
        updateLesson(dt, player, opponents, police);
    else
        updateRace(dt, player);
    if (m_phase == Phase::Racing) {
        updateOpponents(opponents);
        updateRank(player, opponents);
        if (updateHazards(dt, player))
            return;
    }
    if (m_phase == Phase::Racing || m_phase == Phase::Countdown) {
        updateClock(dt);
        updateWaypoints(player);
    }
}

std::vector<Event> Session::takeEvents() {
    std::vector<Event> out;
    out.swap(m_events);
    return out;
}

// --- Queries -----------------------------------------------------------------------

bool Session::checkpointCleared(std::size_t i) const {
    return i < m_wp.cleared.size() && m_wp.cleared[i] != 0;
}

bool Session::checkpointVisible(std::size_t i) const {
    if (i >= m_wp.visible.size() || rule() == WaypointRule::None || m_phase == Phase::Done)
        return false;
    return m_wp.visible[i] != 0;
}

std::optional<Vec3> Session::arrowTarget() const {
    // mmWaypoints::SetArrow, while the waypoints are not finished.
    if (rule() == WaypointRule::None || m_wp.finished || m_phase == Phase::Done || m_wp.current < 0 ||
        static_cast<std::size_t>(m_wp.current) >= m_checkpoints.size())
        return std::nullopt;
    return m_checkpoints[static_cast<std::size_t>(m_wp.current)].position;
}

int Session::checkpointsCleared() const {
    const int n = static_cast<int>(m_checkpoints.size());
    if (mode() == GameMode::Circuit)
        return m_wp.current == 0 ? std::max(0, n - 1) : std::max(0, m_wp.current - 1);
    // mmWPHUD counts every change of the cleared mask; a checkpoint race's
    // finish adds its bit too.
    const int finish = rule() == WaypointRule::CheckpointRace && m_wp.finished ? 1 : 0;
    return std::max(0, m_wp.count - 1 + finish);
}

int Session::checkpointsTotal() const {
    // mmSingleBlitz / mmSingleRace::InitHUD: mmWPHUD::Init(waypoints - 1);
    // mmCircuitHUD::SetWPCleared shows waypoints - 1 too.
    const int n = static_cast<int>(m_checkpoints.size());
    return std::max(0, n - 1);
}

int Session::lap() const { return std::min(m_wp.lap + 1, std::max(1, m_setup.laps)); }

MusicHint Session::musicHint() const {
    if (m_phase == Phase::PostRace || m_phase == Phase::Done)
        return MusicHint::Results;
    return m_idleTime > kIdleMusicDelay ? MusicHint::Idle : MusicHint::Racing;
}

RaceResult Session::result() const {
    RaceResult r;
    r.config = m_setup.config;
    r.finished = m_resultFinished;
    r.won = m_resultWon;
    r.position = m_resultFinished ? (m_resultPosition > 0 ? m_resultPosition : 1) : 0;
    r.timeSeconds = m_resultTime;
    r.damage = static_cast<int>(std::lround(clampf(m_resultDamage, 0.0f, 1.0f) * 100.0f));
    // mmGame::CalculateRaceScore: the car's ScoringBias and the race's
    // Difficulty column, both truncated to integers, times 50 / 25 / 10 for
    // 1st / 2nd / 3rd (Blitz always counts as 1st).
    const bool raced =
        mode() == GameMode::Blitz || mode() == GameMode::Circuit || mode() == GameMode::Checkpoint;
    if (r.finished && raced && !m_setup.config.multiplayer) {
        static constexpr int kPlacePoints[] = {0, 50, 25, 10};
        const int place = mode() == GameMode::Blitz ? 1 : r.position;
        const int points = place >= 1 && place <= 3 ? kPlacePoints[place] : 0;
        const int difficulty = static_cast<int>(m_setup.settings.difficulty);
        r.score = static_cast<int>(m_options.scoringBias) * points * difficulty;
    }
    return r;
}

} // namespace mm2::game::session
