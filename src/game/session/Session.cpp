#include "game/session/Session.h"

#include "core/Log.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

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
        case GameMode::CopsAndRobbers: t.go = 113; t.wreck = 114; break;              // mmMultiCR
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

// The multiplayer races' finish lines (mmMultiRace / Circuit / Blitz::
// GameMessage, UpdateGame): another player "finished in" (the host's line on
// 0x206, a client's on 0x1f7) and the finish timeout's "Race over".
struct NetText {
    std::uint32_t otherFinished = 0, raceOver = 0;
    float timeout = 0.0f; // the timeout armed at the first finish (0: none)
};

NetText netText(GameMode m, bool host) {
    switch (m) {
    case GameMode::Checkpoint: return {host ? 152u : 150u, host ? 143u : 153u, 60.0f};
    case GameMode::Circuit: return {host ? 110u : 107u, host ? 100u : 111u, 120.0f};
    case GameMode::Blitz: return {host ? 99u : 96u, 0, 0.0f};
    default: return {};
    }
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

namespace {
bool g_cheating = false;
}

bool cheating() { return g_cheating; }
void setCheating(bool on) { g_cheating = on; }

std::unique_ptr<Session> Session::create(const RaceConfig& config, const city::CityData& city, const vfs::Vfs& vfs,
                                         const Strings& strings, std::string* error, const SessionOptions& options) {
    auto setup = loadRaceSetup(config, city, vfs, error);
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

std::optional<RespawnPick> Session::placeRespawnStart(const city::CityData& city, std::uint32_t globalSeed,
                                                      const RoomLookup& findRoom) {
    if (!m_setup.respawnStart)
        return std::nullopt;
    const auto pick = cruiseStart(city, findRoom, multiplayer(), globalSeed, m_options.seed);
    if (!pick)
        return std::nullopt;
    // vehCarSim::SetResetPos(start) and the reset angle (+0x250), 0.
    m_setup.playerPlace = {pick->position, pick->angle};
    m_setup.playerSpawn = Mat34::rotationY(pick->angle);
    m_setup.playerSpawn.m3 = pick->position;
    m_respawn = m_setup.playerSpawn;
    return pick;
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

bool Session::wantsMap() const {
    const LessonEvent* lesson = currentLesson();
    return lesson && lesson->type == LessonType::Evade && m_phase == Phase::Countdown && m_stage == Stage::Ready;
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
    m_hudTime = 0.0f;
    m_hudClock = false;
    m_lapStart = m_lastLap = m_bestLap = 0.0f;
    m_lapTimes.clear();
    m_timeUp = false;
    m_penaltyLeft = 0.0f;
    m_penaltyHeld = false;
    m_repairPending = false;
    m_waterTimer = 0.0f;
    m_waterHandled = false;
    m_postWait = 0.0f;
    m_endHold = PlayerHold::None;
    m_damagedOut = m_engineSilenced = false; // the modes' Reset: SilenceEngine(0)
    m_postRaceCam = m_musicStop = false;
    m_netResults.clear(); // mmGameMulti::Reset: the results table
    m_netTimeoutOn = m_netTimedOut = false;
    m_netRacerCount = 1;
    // DisableRacers: the player takes no damage before "Go!".
    m_playerDamage = mode() == GameMode::Cruise || mode() == GameMode::CopsAndRobbers;
    m_resultFinished = m_resultWon = false;
    m_resultPosition = 0;
    m_resultTime = 0.0f;
    m_message = m_message2 = HudMessage{};
    m_vehicleHits = m_objectHits = 0;
    m_baseVehicleImpacts = m_baseObjectImpacts = -1;
    m_lessonDone = false; // mmGame::Reset clears the "race over" flag (+0x7c)
    // mmSingleStunt::Reset (not InitNewEvent) clears the minimum speed state.
    m_reachedMinSpeed = false;
    m_belowMinSpeed = 0.0f;
    stopTimerWarning(); // the modes' Reset
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

    m_phase = mode() == GameMode::Cruise || mode() == GameMode::CopsAndRobbers ? Phase::Racing : Phase::Countdown;
    m_stage = Stage::Intro;
    m_wait = 0.0f;
    // A later exam event goes straight to "Go"; UpdateCollide and
    // UpdateAccel have no such skip and play their countdown again.
    m_skipToGo = index > 0 && !(lesson && (lesson->type == LessonType::Collide ||
                                            lesson->type == LessonType::Acceleration));
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
    speech(SpeechCue::PreRace); // mmGame::Reset
    if (mode() == GameMode::Cruise) {
        // mmSingleRoam has no countdown; mmMultiRoam says "Go!".
        m_phase = Phase::Racing;
        if (multiplayer()) {
            setMessage(modeText(mode(), true).go, "Go!", kStep, false);
            sound(GameSound::StartRaceHigh); // mmMultiRoam::UpdateGame state 1
        }
        m_released = true;
        return;
    }
    if (mode() == GameMode::CopsAndRobbers) {
        // mmMultiCR::UpdateGame state 2: no countdown, "Go!" for 2 s at the
        // top with its first sound ("Startracehigh"); the rules are
        // CopsAndRobbers'.
        m_phase = Phase::Racing;
        setMessage(modeText(mode(), multiplayer()).go, "Go!", 2.0f, true);
        sound(GameSound::StartRaceHigh);
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

PlayerHold Session::playerHold() const {
    // mmGameSingle::DisableRacers (and mmGameMulti's) until "Go!".
    if (!m_started || m_phase == Phase::Countdown)
        return PlayerHold::Undrivable;
    if (m_phase == Phase::PostRace || m_phase == Phase::Done)
        return m_endHold;
    // The wreck penalties: SetDrivable(0, 1), then (1, 1) when repaired.
    return m_penaltyHeld && m_penaltyLeft > 0.0f ? PlayerHold::Undrivable : PlayerHold::None;
}

// --- Countdown ---------------------------------------------------------------------

void Session::enableLessonOpponents() {
    // mmSingleStunt::EnableRacers: opponents from the earlier events' count
    // up to this event's "numopp" (the original's loop bound).
    const LessonEvent* lesson = currentLesson();
    if (!lesson)
        return;
    const int enabled = std::min(lesson->opponents, static_cast<int>(m_oppEnabled.size()));
    for (int i = lessonOpponentOffset(); i < enabled; ++i)
        m_oppEnabled[static_cast<std::size_t>(i)] = 1;
}

void Session::updateCountdown(float dt) {
    if (m_skipToGo) {
        // A later event of an exam: mmSingleStunt::Update* state 0 resets the
        // timers, enables the event's cars and goes to state 2 with no wait,
        // so "Go" comes with the next update.
        m_skipToGo = false;
        enableLessonOpponents();
        m_stage = Stage::Set;
        m_wait = 0.0f;
        return;
    }
    // mmSingleCircuit / mmSingleRace::UpdateGame state 0 waits for the
    // pre-race camera to finish (mmPlayer +0xE5A); Blitz does not.
    if (m_stage == Stage::Intro && m_preRaceCamera && !multiplayer() &&
        (mode() == GameMode::Circuit || mode() == GameMode::Checkpoint))
        return;
    // mmMultiBlitz / mmMultiCircuit / mmMultiRace::UpdateGame state 0 waits
    // for the host's start message.
    if (m_stage == Stage::Intro && !m_startSignal)
        return;
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
        sound(GameSound::StartRaceLow);
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
        sound(GameSound::StartRaceLow);
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
    m_hudTime = 0.0f; // mmHUD::StartTimers, ResetTimers
    m_hudClock = true;
    m_lapStart = 0.0f;
    if (const LessonEvent* lesson = currentLesson()) {
        const LessonText lt = lessonText(lesson->type);
        if (lesson->type == LessonType::Evade && m_lessonEvent > 0)
            setMessage(652, "Get going!  Lose the tail\\nbefore you finish", 3.0f, true);
        else
            setMessage(lt.go, "Go!", kStep, true);
        m_clockRunning = m_hasClock;
        // Every Update* but UpdateJump enables the event's cars at "Go"
        // (UpdateJump only for a later event, in state 0; no retail jump
        // lesson has cars). EnableRacers also turns the held player's damage
        // on; UpdateJump only makes the car drivable, so a jump lesson is
        // driven without damage.
        if (lesson->type != LessonType::Jump) {
            enableLessonOpponents();
            m_playerDamage = true;
        }
        if (lesson->type == LessonType::Destroy && !m_opponents.empty()) {
            const int target = lessonOpponentOffset();
            if (target < static_cast<int>(m_opponents.size()))
                push(EventType::OpponentDamageLimits, target, kDestroyMaxDamage);
        }
    } else {
        setMessage(modeText(mode(), multiplayer()).go, "Go!", kStep, true);
        m_clockRunning = m_hasClock;
        std::fill(m_oppEnabled.begin(), m_oppEnabled.end(), 1);
        m_playerDamage = true; // mmGameSingle / mmGameMulti::EnableRacers
    }
    push(EventType::CountdownGo);
    sound(GameSound::StartRaceHigh);
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
    // mmSingleRace / mmMultiRace::UpdateGameInput: "Next Checkpoint" and
    // "Prev. Checkpoint" only for waypoint type 2 (Blitz and the crash
    // course ignore them).
    if (rule() == WaypointRule::CheckpointRace)
        cycleCurrent(forward);
}

void Session::cycleCurrent(bool forward) {
    // mmWaypoints::CycleCurrentWaypoint (GetNextWaypoint / GetLastWaypoint).
    // OpenMM2 also stops for fewer than three waypoints, where the original
    // could index past the list.
    const int n = static_cast<int>(m_checkpoints.size());
    if (n < 3 || m_wp.finished)
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
    // mmWaypoints::DisplayHUDMessage's sound: "Waypoint" in the crash
    // course; for any-order and in-order waypoints, except for the one that
    // leaves only the finish; "Lastwaypoint" when a circuit lap is
    // completed.
    const int n = static_cast<int>(m_checkpoints.size());
    switch (rule()) {
    case WaypointRule::Circuit:
        sound((m_wp.count - 1) % std::max(1, n) == 0 ? GameSound::LastWaypoint : GameSound::Waypoint);
        break;
    case WaypointRule::Blitz: sound(GameSound::Waypoint); break;
    case WaypointRule::CheckpointRace:
    case WaypointRule::AnyOrderEnd:
    case WaypointRule::InOrder:
        if (mode() == GameMode::CrashCourse || m_wp.count != n - 1)
            sound(GameSound::Waypoint);
        break;
    case WaypointRule::None: break;
    }
    // mmHUD::ShowSplitTime: the race time (in circuits the time since the
    // lap started) for 1 s, placement 0; not in the crash course.
    if (mode() != GameMode::CrashCourse) {
        const float split = mode() == GameMode::Circuit ? m_raceTime - m_lapStart : m_raceTime;
        setMessage(formatTime(split), 1.0f, false);
    }
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
                cycleCurrent(true);
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
                m_lapTimes.push_back(lapTime);
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
            r.finishTime = m_hudTime; // mmTimer::GetTime on mmHUD's +0xA24 timer
            push(EventType::OpponentFinished, static_cast<int>(i), static_cast<float>(r.place));
            // mmSingleCircuit::UpdateOpponentStatus shows it only in state 3,
            // not during the wreck penalty (state 6).
            if (m_phase == Phase::Racing && !(mode() == GameMode::Circuit && m_penaltyLeft > 0.0f)) {
                // Handle 4 (circuit) / 5 (race): "Messagenote" in both.
                if (!multiplayer())
                    sound(GameSound::MessageNote);
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

    // The second half of UpdateScore: each opponent's place for its icon.
    // A finished opponent keeps its finishing place; otherwise 1 + the other
    // opponents with more waypoints passed, or finished, or level and nearer
    // its own next waypoint (its count modulo the waypoints), + the player
    // when ahead the same way.
    m_opponentPlaces.assign(m_opponents.size(), 10);
    if (mode() != GameMode::Checkpoint && mode() != GameMode::Circuit)
        return;
    for (std::size_t i = 0; i < m_opponents.size(); ++i) {
        const Racer& r = m_opponents[i];
        if (r.finished) {
            m_opponentPlaces[i] = r.place;
            continue;
        }
        const Vec3 next = m_checkpoints[static_cast<std::size_t>(r.count % n)].position;
        const float own = i < opponents.size() ? dist2(opponents[i].transform.m3, next) : 0.0f;
        int place = 1;
        for (std::size_t j = 0; j < m_opponents.size(); ++j) {
            if (j == i)
                continue;
            const Racer& o = m_opponents[j];
            if (r.count < o.count || o.finished)
                ++place;
            else if (r.count == o.count && j < opponents.size() && dist2(opponents[j].transform.m3, next) < own)
                ++place;
        }
        if (r.count < m_wp.count || (m_wp.count == r.count && dist2(player.transform.m3, next) < own))
            ++place;
        m_opponentPlaces[i] = place;
    }
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
    if (m_phase == Phase::PostRace) {
        // mmGame::Update checks the water after the ending too, and the
        // modes' handlers do not look at their state.
        switch (mode()) {
        case GameMode::Blitz:
        case GameMode::Checkpoint:
            // mmSingleBlitz / mmSingleRace::HitWaterHandler: state 4, 0.5 s.
            // After a finish (race-over set) state 4 never opens the menu nor
            // shows the results: they wait for Escape on the locked menu.
            sound(GameSound::DamageLose);
            if (mode() == GameMode::Checkpoint)
                m_engineSilenced = true;
            m_postWait = m_resultFinished ? std::numeric_limits<float>::infinity() : kWaterLoseDelay;
            break;
        case GameMode::Circuit: respawnAtLastCheckpoint(); break;
        case GameMode::CrashCourse:
            // mmSingleStunt::HitWaterHandler, unless the race-over flag is set.
            if (!m_lessonDone) {
                sound(GameSound::DamageLose);
                m_postWait = kWaterLoseDelay;
            }
            break;
        default: break;
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
        // mmSingleBlitz / mmSingleRace::HitWaterHandler: the race is lost
        // (the race's handler also silences the engine).
        m_wp.stopped = true;
        sound(GameSound::DamageLose);
        if (mode() == GameMode::Checkpoint)
            m_engineSilenced = true;
        endRace(false, false, kWaterLoseDelay, PlayerHold::None);
        break;
    case GameMode::Circuit: respawnAtLastCheckpoint(); break;
    case GameMode::CrashCourse:
        // mmSingleStunt::HitWaterHandler.
        if (!m_lessonDone) {
            sound(GameSound::DamageLose);
            lessonFailed(kWaterLoseDelay, PlayerHold::None, false);
        }
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
    if (!m_checkpoints.empty()) {
        const Checkpoint& cp = m_checkpoints[static_cast<std::size_t>(m_wp.lastCleared)];
        m_respawn = spawnAt(cp);
    }
    m_waterHandled = false;
    m_waterTimer = 0.0f;
    push(EventType::Respawn);
}

// --- Clocks ------------------------------------------------------------------------

void Session::updateClock(float dt) {
    if (m_raceClock)
        m_raceTime += dt;
    if (m_hudClock)
        m_hudTime += dt;
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
    m_warnActive = false;
}

void Session::stopTimerWarning() {
    // The modes stop the warning sound when it has been started (+0x76E8)
    // and set its beat back to 1.
    if (m_warnActive)
        push(EventType::Sound, static_cast<int>(GameSound::TimerWarning), -1.0f);
    resetTimerWarning();
}

void Session::timerWarning(float dt) {
    // PlayTimerWarning: a beep about once a second, continuous from 3 s.
    if (m_clock > kTimerWarningLoop) {
        if (m_warnAcc >= 1.0f && !m_warnBeeped) {
            m_warnBeeped = true;
            push(EventType::TimerWarning, 0, m_clock);
            sound(GameSound::TimerWarning); // played unless still playing
        }
        if (m_warnAcc > 1.0f) {
            m_warnAcc = 0.0f;
            m_warnBeeped = false;
        }
        m_warnAcc += dt;
    } else if (!m_warnLoop) {
        m_warnLoop = true;
        push(EventType::TimerWarning, 1, m_clock);
        sound(GameSound::TimerWarning, 1.0f);
    }
    m_warnActive = true;
}

void Session::startPenalty(float seconds, bool held) {
    m_penaltyLeft = seconds;
    m_penaltyHeld = held;
    push(EventType::WreckPenalty, -1, seconds);
}

float Session::timeRemaining() const { return m_hasClock ? m_clock : -1.0f; }

float Session::lapTime() const { return m_raceTime - m_lapStart; }

// --- Race modes --------------------------------------------------------------------

void Session::endRace(bool finished, bool won, float delay, PlayerHold hold) {
    if (m_phase == Phase::PostRace || m_phase == Phase::Done)
        return;
    m_phase = Phase::PostRace;
    // The single-player modes set mmPlayer +0x2258 at most endings and make
    // the car undrivable at a wreck; the multiplayer ones brake it with the
    // throttle tapering off for the wait, then call SetDrivable(0, 1)
    // (mmMultiRace / mmMultiCircuit / mmMultiBlitz::UpdateGame state 4;
    // OpenMM2 makes it undrivable at once).
    m_endHold = multiplayer() ? PlayerHold::Undrivable : hold;
    m_postWait = delay;
    // The finish stops only the race timer (mmTimer::Stop); a lost race
    // stops both (mmHUD::StopTimers in mmSingleRace's wreck state and
    // HitWaterHandler), so opponents still finishing get a time that keeps
    // counting after the player's own finish.
    m_raceClock = false;
    if (!finished)
        m_hudClock = false;
    m_clockRunning = false;
    m_penaltyLeft = 0.0f;
    m_resultFinished = finished;
    m_resultWon = won;
    m_resultTime = m_raceTime;
    // mmPlayer::SetPostRaceCam (mmGameMulti::SetFinishCam): at a finish and
    // at the single-player wrecks that make the car undrivable; not after the
    // water, a Blitz run out of time or most failed lessons, which keep the
    // driving (or water) camera. The lessons that set it anyway say so.
    m_postRaceCam = finished || (hold == PlayerHold::Undrivable && !multiplayer());
}

void Session::deactivateFinish() {
    // mmWaypoints::DeactivateFinish: the last waypoint's stand disappears.
    if (!m_wp.visible.empty())
        m_wp.visible.back() = 0;
}

bool Session::netWaitsForAll() const {
    // mmMultiRace / mmMultiCircuit stay in state 4 until every player is
    // counted (0x211) or the timeout runs out ("wait for all finishers", on
    // by default); Blitz ends with its clock.
    return multiplayer() && (mode() == GameMode::Checkpoint || mode() == GameMode::Circuit);
}

void Session::addNetResult(std::string name, float time, bool self) {
    // mmGameMulti::SortResults: a player is listed once, by time (a later
    // equal time goes after; kNetDnf sorts last).
    for (const auto& r : m_netResults)
        if (r.self == self && r.name == name)
            return;
    auto at = std::find_if(m_netResults.begin(), m_netResults.end(), [&](const NetResult& r) { return r.time > time; });
    m_netResults.insert(at, NetResult{std::move(name), time, self});
    // SetTimeoutOn: the first finish (fewer than 2 counted) arms the
    // finish timeout of a race (60 s) or circuit (120 s).
    const NetText nt = netText(mode(), m_options.netHost);
    if (netWaitsForAll() && nt.timeout > 0.0f && m_netResults.size() < 2 && m_phase != Phase::Done) {
        m_netTimeoutOn = true;
        m_netTimeout = nt.timeout;
    }
}

void Session::remoteFinished(const std::string& name, float seconds) {
    if (!multiplayer() || m_phase == Phase::Done)
        return;
    const NetText nt = netText(mode(), m_options.netHost);
    // GameMessage 0x206 (host) / 0x1f7 (client): Messagenote and the line,
    // unless the player did not finish.
    sound(GameSound::MessageNote);
    if (seconds < kNetDnf) {
        setMessage(name, 5.0f, false);
        setMessage2(std::format("{} {}", str(nt.otherFinished, "finished in"), formatTime(seconds)));
    }
    addNetResult(name, seconds, false);
}

void Session::updateNetRace(float dt, const PlayerState& player) {
    // mmGameMulti::UpdateScore: the place among the other players, while
    // the player's waypoints are not done ("Place: n/N").
    const int n = static_cast<int>(m_checkpoints.size());
    if (!m_wp.finished && n >= 2) {
        const Vec3 target = m_checkpoints[static_cast<std::size_t>(std::clamp(m_wp.current, 0, n - 1))].position;
        const float mine = dist2(player.transform.m3, target);
        int place = 1, racers = 1;
        for (const auto& r : m_netRacers) {
            if (!r.present) {
                // A slot without a car counts only once finished.
                if (r.finished) {
                    ++racers;
                    ++place;
                }
                continue;
            }
            ++racers;
            if (m_wp.count < r.waypoints || r.finished)
                ++place;
            else if (m_wp.count == r.waypoints && dist2(r.position, target) < mine)
                ++place;
        }
        m_rank = place;
        m_netRacerCount = racers;
    }
    // Each other player's rank over its icon (IconIndex); a finished
    // player's icon is turned off (0 here), a missing car has none.
    m_netPlaces.assign(m_netRacers.size(), 0);
    if (n >= 2) {
        for (std::size_t i = 0; i < m_netRacers.size(); ++i) {
            const NetRacer& r = m_netRacers[i];
            if (!r.present || r.finished)
                continue;
            const Vec3 wp = m_checkpoints[static_cast<std::size_t>(r.waypoints % n)].position;
            const float own = dist2(r.position, wp);
            int place = 1;
            for (std::size_t j = 0; j < m_netRacers.size(); ++j) {
                const NetRacer& o = m_netRacers[j];
                if (j == i || (!o.present && !o.finished))
                    continue;
                if (o.waypoints > r.waypoints || o.finished)
                    ++place;
                else if (o.present && o.waypoints == r.waypoints && dist2(o.position, wp) < own)
                    ++place;
            }
            if (m_wp.count > r.waypoints || m_wp.finished ||
                (m_wp.count == r.waypoints && dist2(player.transform.m3, wp) < own))
                ++place;
            m_netPlaces[i] = place;
        }
    }
    if (!m_netTimeoutOn)
        return;
    // The finish timeout (the host's in MM2; here every machine runs it from
    // the first finish it hears of).
    m_netTimeout -= dt;
    if (m_netTimeout > 0.0f)
        return;
    m_netTimeoutOn = false;
    m_netTimedOut = true;
    if (m_phase == Phase::Racing) {
        // Not finished: braked, "Race over" at the bottom for 5 s, the race
        // timer stopped, and a did-not-finish to the others.
        const NetText nt = netText(mode(), m_options.netHost);
        setMessage(nt.raceOver, "Race over", 5.0f, false);
        push(EventType::NetFinished, -1, kNetDnf);
        addNetResult(m_options.playerName, kNetDnf, true);
        endRace(false, false, 3.0f, PlayerHold::Undrivable);
    }
}

void Session::playerFinished() {
    const int place = ++m_finishers;
    m_resultPosition = place;
    m_rank = place;
    push(EventType::PlayerFinished, place, m_raceTime);
    // The single-player modes' RegisterFinish -> mmGameSingle::UpdateRewards
    // (OpenMM2: whether the finish is registered, or unlocks a reward, is
    // decided later by the frontend, so the results line always plays).
    if (!multiplayer())
        speech(SpeechCue::Results, static_cast<float>(place));
}

void Session::updateRace(float dt, const PlayerState& player) {
    const ModeText mt = modeText(mode(), multiplayer());
    const bool pro = m_setup.config.difficulty == Difficulty::Professional;
    const bool penalty = m_penaltyLeft > 0.0f;
    auto wreckPenalty = [&] {
        if (wrecked(player) && !penalty) {
            push(EventType::Wrecked);
            if (multiplayer() && mode() == GameMode::Blitz)
                stopTimerWarning(); // mmMultiBlitz::UpdateGame
            if (!multiplayer() && mode() == GameMode::Circuit)
                sound(GameSound::MessageNote); // mmSingleCircuit::UpdateGame
            setMessage(mt.wreck, "Wait...5 second penalty!", 5.0f, false);
            startPenalty(kWreckPenalty);
        }
    };

    if (multiplayer()) {
        // mmMultiRoam / Blitz / Circuit / Race: wrecks cost 5 s, never the race.
        // While the penalty runs (states 6 / 7) the timer warning, the finish
        // and the clock are not checked (only state 3 checks them).
        if (penalty)
            return;
        if (mode() == GameMode::Blitz && m_hasClock && m_clock < kTimerWarning)
            timerWarning(dt);
        wreckPenalty();
        if (m_wp.finished) {
            stopTimerWarning();
            playerFinished();
            // The player's name, and "finished in M:SS:HH" under it.
            setMessage(m_options.playerName, 5.0f, false);
            setMessage2(std::format("{} {}", str(mt.finishedIn, "finished in"), formatTime(m_raceTime)));
            // SendFinishReq / SendFinishAck, SortResults, SetTimeoutOn.
            push(EventType::NetFinished, -1, m_raceTime);
            addNetResult(m_options.playerName, m_raceTime, true);
            // Blitz shows the results when the clock would have run out;
            // the others 3 s after the finish.
            const float wait = mode() == GameMode::Blitz ? m_clock : 3.0f;
            endRace(true, true, wait);
            return;
        }
        if (mode() == GameMode::Blitz && m_hasClock && m_clock <= 0.0f) {
            stopTimerWarning();
            m_wp.stopped = true;
            m_timeUp = true;
            push(EventType::TimeUp);
            setMessage(mt.timeUp, "Time's up!", 5.0f, false);
            deactivateFinish();
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
            stopTimerWarning();
            if (m_timeUp) {
                // Finished after the time ran out: no post-race camera and
                // the music goes on.
                m_wp.stopped = true;
                deactivateFinish();
                speech(SpeechCue::ResultsPoor);
                sound(GameSound::YouLose);
                setMessage(mt.timeUp, "Time's up!", 5.0f, false);
                endRace(false, false, kPostRace);
            } else {
                playerFinished();
                sound(GameSound::EndOfRaceTag);
                setMessage(mt.won, "You Won!", 5.0f, true);
                m_musicStop = true; // StopSegment(0)
                endRace(true, true, kPostRace);
            }
            return;
        }
        if (m_hasClock && m_clock <= 0.0f && !m_timeUp) {
            // Time's up, but the race goes on: finishing now only ends it.
            stopTimerWarning();
            m_timeUp = true;
            push(EventType::TimeUp);
            sound(GameSound::YouLose);
            setMessage(mt.timeUp, "Time's up!", 5.0f, false);
        }
        if (wrecked(player)) {
            stopTimerWarning();
            m_wp.stopped = true;
            deactivateFinish();
            push(EventType::Wrecked);
            sound(GameSound::DamageLose);
            if (mode() == GameMode::Blitz)
                speech(SpeechCue::DamagePenalty); // mmSingleBlitz::UpdateGame
            m_damagedOut = m_engineSilenced = true; // StopSegment(1), SilenceEngine(1)
            setMessage(mt.wreck, "Game over!", 5.0f, false);
            endRace(false, false, kPostRace, PlayerHold::Undrivable);
        }
        break;
    case GameMode::Circuit:
        // mmSingleCircuit::UpdateGame states 3 and 6.
        wreckPenalty();
        if (m_wp.finished) {
            playerFinished();
            const int place0 = m_resultPosition - 1;
            sound(place0 == 0 ? GameSound::EndOfRaceTag : GameSound::YouLose);
            setMessage(place0 < 8 ? mt.place + static_cast<std::uint32_t>(place0) : mt.loaf,
                       std::format("You finished #{}", m_resultPosition), 5.0f, true);
            // mmSingleCircuit::ProgressCheck: top three, professionals first.
            m_musicStop = true;     // StopSegment(0)
            m_playerDamage = false; // the circuit finish: EnableDamage = 0
            endRace(true, m_resultPosition < (pro ? 2 : 4), kPostRace);
        }
        break;
    case GameMode::Checkpoint:
        // mmSingleRace::UpdateGame state 3.
        if (m_wp.finished) {
            playerFinished();
            const int place0 = m_resultPosition - 1;
            sound(place0 == 0 ? GameSound::EndOfRaceTag : GameSound::YouLose);
            setMessage(place0 < 8 ? mt.place + static_cast<std::uint32_t>(place0) : mt.loaf,
                       std::format("You finished #{}", m_resultPosition), 5.0f, place0 < 8);
            m_musicStop = true; // StopSegment(0)
            endRace(true, m_resultPosition < (pro ? 2 : 4), kPostRace);
            return;
        }
        if (wrecked(player)) {
            m_wp.stopped = true;
            push(EventType::Wrecked);
            sound(GameSound::DamageLose);
            if (mode() == GameMode::Blitz)
                speech(SpeechCue::DamagePenalty); // mmSingleBlitz::UpdateGame
            m_damagedOut = m_engineSilenced = true; // StopSegment(1), SilenceEngine(1)
            setMessage(mt.wreck, "Game over!", 5.0f, false);
            endRace(false, false, kPostRace, PlayerHold::Undrivable);
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

void Session::lessonFailed(float delay, PlayerHold hold, bool registerFinish) {
    // Several failures can come in one frame (UpdateCorner and UpdateFrogger
    // go on checking after one): report the lesson failed once.
    const bool alreadyFailed = (m_phase == Phase::PostRace || m_phase == Phase::Done) && !m_resultWon;
    // The race-over flag (+0x7c) is set only by the endings that set it
    // themselves (a finish with pursuers, the Clean and Collide time-ups);
    // the other failures leave it clear, so the water handler still runs.
    m_wp.stopped = true;
    if (!alreadyFailed) {
        push(EventType::LessonFailed, m_lessonEvent);
        // RegisterFinish(0) (not after the water): mmCCSpeech::PlayResults.
        if (registerFinish)
            speech(SpeechCue::LessonResults, 0.0f);
    }
    endRace(false, false, delay, hold);
}

void Session::lessonPassedOrNext(std::uint32_t passMessage, float seconds, bool top, float delay, bool latch) {
    const int events = static_cast<int>(m_setup.lessonEvents.size());
    if (m_lessonEvent == events - 1) {
        m_lessonDone = true;
        setMessage(passMessage, "Good driving!", seconds, top);
        push(EventType::LessonPassed, m_lessonEvent);
        speech(SpeechCue::LessonResults, 1.0f); // RegisterFinish(1)
        endRace(true, true, delay);
        return;
    }
    // An exam continues with its next event where the car is
    // (mmSingleStunt::InitNewEvent; no respawn). Most Update* functions set
    // the "race over" flag before they get here, and only mmGame::Reset
    // clears it: in the later events the water no longer fails the lesson
    // and the Clean lesson's clock is no longer checked. (They also set the
    // post-race camera and brake, which OpenMM2 does not carry over; no
    // retail exam has such an event before another one.)
    if (latch)
        m_lessonDone = true;
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
        startPenalty(kWreckPause, false); // no SetDrivable in the lessons' state 5
    };
    const bool timeOut = m_hasClock && m_clock <= 0.0f;
    const int targetIndex = lessonOpponentOffset();
    const OpponentState* target = targetIndex < static_cast<int>(opponents.size())
                                      ? &opponents[static_cast<std::size_t>(targetIndex)]
                                      : nullptr;

    switch (lesson->type) {
    case LessonType::Jump: // UpdateJump
        if (m_wp.finished) {
            if (lastEvent())
                sound(GameSound::EndOfRaceTag);
            lessonPassedOrNext(209, 5.0f, true, kPostRace, true);
        } else if (timeOut) {
            stopTimerWarning();
            sound(GameSound::YouLose);
            setMessage(614, "Time's up!", 5.0f, false);
            push(EventType::TimeUp);
            lessonFailed();
            deactivateFinish();
        } else if (wrecked(player)) {
            stopTimerWarning();
            sound(GameSound::DamageLose);
            push(EventType::Wrecked);
            setMessage(615, "Game over!", 5.0f, false);
            lessonFailed(5.0f, PlayerHold::Undrivable);
            deactivateFinish();
        }
        break;
    case LessonType::Collide: // UpdateCollide (no retail lesson; nothing is recorded)
        if (m_hasClock && m_clock < kTimerWarning && !m_lessonDone)
            timerWarning(dt);
        if (timeOut && !m_lessonDone) {
            m_lessonDone = true;
            setMessage(214, "Time's up!", kStep, true);
            stopTimerWarning();
            push(EventType::TimeUp);
            endRace(false, false, kPostRace);
            m_postRaceCam = true; // UpdateCollide: SetPostRaceCam at the time-up
        } else if (m_wp.finished) {
            m_lessonDone = true;
            setMessage(215, "Good driving!", kStep, true);
            stopTimerWarning();
            endRace(true, false, 3.0f);
        } else if (wrecked(player)) {
            push(EventType::Wrecked);
            sound(GameSound::MessageNote);
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
            lessonPassedOrNext(221, 5.0f, true, kPostRace, false);
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
                sound(GameSound::YouLose);
                lessonFailed();
                m_postRaceCam = true; // set before the pursuit check
                m_lessonDone = true;   // and the race-over flag (+0x7c)
                deactivateFinish();
            } else {
                setMessage(196, "You survived the gauntlet!", 5.0f, true);
                if (lastEvent())
                    sound(GameSound::EndOfRaceTag);
                lessonPassedOrNext(196, 5.0f, true, kPostRace, true);
            }
        } else if (timeOut) {
            sound(GameSound::YouLose);
            setMessage(198, "Time's up!  Drive faster to win!", 5.0f, false);
            push(EventType::TimeUp);
            lessonFailed();
            deactivateFinish();
        }
        break;
    case LessonType::MinimumSpeed: { // UpdateCorner
        const float speed = player.speedMph;
        const float minimum = lesson->minimumSpeedMph;
        if (m_wp.finished) {
            if (copPursuit(player, police)) {
                setMessage(197, "Lose your pursuers before you finish!", 5.0f, true);
                sound(GameSound::YouLose);
                lessonFailed();
                m_postRaceCam = true; // set before the pursuit check
                m_lessonDone = true;   // and the race-over flag (+0x7c)
            } else {
                if (lastEvent())
                    sound(GameSound::EndOfRaceTag);
                lessonPassedOrNext(234, 5.0f, true, kPostRace, true);
            }
            break;
        }
        if (lesson->timeLimit != 0.0f && timeOut) {
            stopTimerWarning();
            sound(GameSound::YouLose);
            setMessage(244, "Time's up!", 5.0f, false); // CheckTimeUp
            push(EventType::TimeUp);
            lessonFailed();
            deactivateFinish(); // CheckTimeUp's branch only
            break;
        }
        if (!m_reachedMinSpeed) {
            // UpdateCorner does not return after this failure: the rest of
            // the frame's checks still run (and their messages win).
            if (m_wp.count > 1) {
                setMessage(235, "You need to get up to speed", 3.0f, true);
                setMessage2(str(236, "before hitting a checkpoint"));
                lessonFailed();
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
            lessonFailed(5.0f, PlayerHold::None); // UpdateCorner sets no hold here
        }
        break;
    }
    case LessonType::Clean: { // UpdateFrogger
        if (m_hasClock && m_clock < kTimerWarning && !m_lessonDone)
            timerWarning(dt);
        if (timeOut && !m_lessonDone) {
            setMessage(228, "Time's up!", kStep, true);
            stopTimerWarning();
            push(EventType::TimeUp);
            lessonFailed();
            m_postRaceCam = true; // UpdateFrogger: SetPostRaceCam at the time-up
            m_lessonDone = true;   // and +0x7c
            break;
        }
        bool passed = false;
        if (m_wp.finished) {
            passed = lastEvent();
            if (passed)
                stopTimerWarning();
            lessonPassedOrNext(229, kStep, true, 3.0f, true);
            if (!passed)
                break; // InitNewEvent: the next event starts
        }
        // UpdateFrogger checks the damage after the finish too: a car wrecked
        // in the frame it finishes still fails the lesson.
        if (wrecked(player)) {
            push(EventType::Wrecked);
            sound(GameSound::YouLose);
            setMessage(230, "You scraped the paint!", 5.0f, false);
            lessonFailed(5.0f, PlayerHold::None); // nor does UpdateFrogger
            if (passed) {
                m_resultFinished = m_resultWon = false;
                m_postWait = kPostRace;
            }
        }
        break;
    }
    case LessonType::Acceleration: // UpdateAccel (no retail lesson)
        if (m_accelWait >= 0.0f) {
            m_accelWait -= dt;
            if (m_accelWait < 0.0f) {
                if (lastEvent())
                    sound(GameSound::EndOfRaceTag);
                lessonPassedOrNext(204, 5.0f, true, kPostRace, true);
            }
            break;
        }
        if (wrecked(player))
            pause();
        if (m_wp.finished) {
            m_accelWait = 2.0f;
        } else if (timeOut) {
            sound(GameSound::YouLose);
            setMessage(203, "Put the pedal to the metal!", 5.0f, false);
            push(EventType::TimeUp);
            lessonFailed();
            deactivateFinish();
        }
        break;
    case LessonType::Course:
    case LessonType::Map: // UpdateBlitz
        if (m_wp.finished) {
            stopTimerWarning();
            if (lastEvent())
                sound(GameSound::EndOfRaceTag);
            lessonPassedOrNext(609, 5.0f, false, kPostRace, false);
        } else if (timeOut) {
            stopTimerWarning();
            sound(GameSound::YouLose);
            setMessage(244, "Time's up!", 5.0f, false); // CheckTimeUp
            push(EventType::TimeUp);
            lessonFailed();
            deactivateFinish();
        } else if (wrecked(player)) {
            stopTimerWarning();
            sound(GameSound::DamageLose);
            push(EventType::Wrecked);
            setMessage(245, "Game over!", 5.0f, false);
            lessonFailed(5.0f, PlayerHold::Undrivable);
            deactivateFinish();
        }
        break;
    case LessonType::Destroy: // UpdateStop
        if (target && target->currentDamage >= kDestroyMaxDamage) {
            m_oppEnabled[static_cast<std::size_t>(targetIndex)] = 0; // the car stops
            lessonPassedOrNext(620, 5.0f, true, kPostRace, true);
            break;
        }
        if (target && target->finished) {
            setMessage(621, "Car reached destination", 5.0f, true);
            setMessage2(str(622, "You lost!"));
            lessonFailed();
            break;
        }
        if (timeOut) {
            stopTimerWarning();
            sound(GameSound::YouLose);
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
    updateRules(dt, player, opponents, police);
    // mmHUD::Update runs after the game's UpdateGame: a message set this
    // frame already loses this frame's time.
    tickMessage(dt);
}

void Session::tickMessage(float dt) {
    // mmHUD::Update: the time counts down while it is not 0; below 0 both
    // lines are cleared.
    if (m_message.timeLeft != 0.0f) {
        m_message.timeLeft -= dt;
        m_message2.timeLeft = m_message.timeLeft;
        if (m_message.timeLeft < 0.0f)
            m_message = m_message2 = HudMessage{};
    }
}

void Session::updateRules(float dt, const PlayerState& player, std::span<const OpponentState> opponents,
                          std::span<const OpponentState> police) {
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
        if (multiplayer() && mode() != GameMode::Cruise && mode() != GameMode::CopsAndRobbers)
            updateNetRace(dt, player);
        return;
    case Phase::PostRace: {
        updateClock(dt);
        updateOpponents(opponents);
        if (multiplayer() && mode() != GameMode::Cruise && mode() != GameMode::CopsAndRobbers)
            updateNetRace(dt, player);
        // mmGame::Update's water and fall checks run in every state.
        if (updateHazards(dt, player) && m_phase != Phase::PostRace)
            return; // a fall restarted the race
        m_postWait -= dt;
        // A multiplayer race or circuit waits (braked) until everyone is
        // counted or the finish timeout has run out (0x211, state 5).
        const bool counted = m_netResults.size() >= m_netRacers.size() + 1 || m_netTimedOut;
        if (m_postWait <= 0.0f && (!netWaitsForAll() || counted)) {
            m_phase = Phase::Done;
            push(EventType::SessionOver);
        }
        return;
    }
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
        if (multiplayer() && mode() != GameMode::Cruise && mode() != GameMode::CopsAndRobbers)
            updateNetRace(dt, player);
        if (m_phase == Phase::Racing && updateHazards(dt, player))
            return;
    } else if (m_phase == Phase::PostRace) {
        // The race just ended: UpdateOpponentStatus still ends this
        // UpdateGame, so an opponent finishing in the same frame is placed
        // behind the player now.
        updateOpponents(opponents);
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
    if (mode() == GameMode::Circuit) {
        // mmCircuitHUD::SetWPCleared shows the index of the waypoint just
        // passed; mmWaypoints::Reset shows the first target (1) at the start.
        if (m_wp.count == 1)
            return std::min(1, std::max(0, n - 1));
        // mmWaypoints::Update skips SetWPCleared on the final crossing (the
        // waypoints are done): the readout keeps the last checkpoint.
        if (m_wp.finished)
            return std::max(0, n - 1);
        return m_wp.current == 0 ? std::max(0, n - 1) : std::max(0, m_wp.current - 1);
    }
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
    r.cheated = cheating();
    r.ended = m_phase == Phase::PostRace || m_phase == Phase::Done;
    r.finished = m_resultFinished;
    r.won = m_resultWon;
    r.position = m_resultFinished ? (m_resultPosition > 0 ? m_resultPosition : 1) : 0;
    r.timeSeconds = m_resultTime;
    r.bestLapSeconds = m_bestLap;
    r.lapSeconds = m_lapTimes;
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
    // Multiplayer (mmGameMulti::UpdateResults): the players by time, the
    // did-not-finish ones last as losers; the player's place is its row.
    if (raced && multiplayer()) {
        int place = 0;
        for (const auto& nr : m_netResults) {
            ++place;
            RaceStanding st{nr.self ? -1 : -2, place, nr.time, nr.name, nr.time >= kNetDnf};
            if (nr.self && r.finished && !st.dnf)
                r.position = place;
            r.standings.push_back(std::move(st));
        }
        return r;
    }
    // The results list (PUResults::AddName): everyone who finished, by place.
    if (raced) {
        if (r.finished && r.position > 0)
            r.standings.push_back({-1, r.position, m_resultTime});
        for (std::size_t i = 0; i < m_opponents.size(); ++i)
            if (m_opponents[i].finished)
                r.standings.push_back({static_cast<int>(i), m_opponents[i].place, m_opponents[i].finishTime});
        std::ranges::sort(r.standings, {}, &RaceStanding::place);
    }
    return r;
}

} // namespace mm2::game::session
