#include "game/session/Session.h"

#include "core/Log.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace mm2::game::session {
namespace {

// Countdown and post-race timings from MM1's mmSingleBlitz::UpdateGame:
// 2.5 s of "Ready..." / "Set..." (1.25 s each), results 5 s after the end.
constexpr float kCountdownStep = 1.25f;
constexpr float kPostRaceDelay = 5.0f;
constexpr float kTimerWarning = 10.0f;    // flt_61A534
constexpr float kWaterRespawn = 4.0f;     // HitWaterTimer runs 1 -> 5
constexpr float kFalseStartPenalty = 5.0f; // "Wait...5 second penalty!"
constexpr float kIdleMusicDelay = 3.0f;   // inferred
// Crash course distances (inferred).
constexpr float kFollowEscapeDistance = 120.0f;
constexpr float kEvadeClearDistance = 40.0f;

// String table ids per mode (US build). See docs/gamemodes.md.
struct ModeStrings {
    std::uint32_t ready = 241, set = 242, go = 243;
    std::uint32_t penalty = 0;
    std::uint32_t timeUp = 244, gameOver = 245;
    std::uint32_t won = 0;
    std::uint32_t finishedFirst = 0; // "You finished 1st!" .. +7
    std::uint32_t loaf = 0;
};

ModeStrings modeStrings(GameMode m) {
    ModeStrings s;
    switch (m) {
    case GameMode::Blitz:
        s = {158, 159, 160, 0, 161, 162, 164, 0, 0};
        break;
    case GameMode::Circuit:
        s = {165, 166, 167, 168, 0, 0, 0, 169, 177};
        break;
    case GameMode::Checkpoint:
        // The checkpoint group has no penalty line; circuit's is used (inferred).
        s = {179, 180, 181, 168, 0, 182, 0, 183, 191};
        break;
    default: break;
    }
    return s;
}

struct LessonStrings {
    std::uint32_t intro = 0, intro2 = 0;
    std::uint32_t ready = 241, set = 242, go = 243;
    std::uint32_t win = 609, timeUp = 244, lose = 245;
};

LessonStrings lessonStrings(LessonType t) {
    LessonStrings s;
    switch (t) {
    case LessonType::Jump: s = {206, 0, 207, 0, 208, 209, 614, 615}; break;
    case LessonType::Acceleration: s = {200, 201, 241, 242, 202, 204, 614, 615}; break;
    case LessonType::Follow: s = {0, 0, 218, 219, 220, 221, 245, 223}; break;
    case LessonType::Evade: s = {652, 0, 194, 0, 195, 196, 198, 245}; break;
    case LessonType::MinimumSpeed: s = {232, 0, 241, 242, 233, 234, 244, 239}; break;
    case LessonType::Clean: s = {225, 227, 241, 242, 243, 229, 228, 230}; break;
    case LessonType::Course:
    case LessonType::Map: s = {608, 0, 241, 242, 243, 609, 244, 245}; break;
    case LessonType::Destroy: s = {0, 0, 617, 618, 619, 620, 623, 622}; break;
    }
    return s;
}

float horizontalDistance(const Vec3& a, const Vec3& b) {
    return std::sqrt(sq(a.x - b.x) + sq(a.z - b.z));
}

} // namespace

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
    // MM1 respawns the player below y = -50 (flt_61A3B0); keep that unless the
    // city itself goes deeper.
    if (city.psdl.bounds.valid())
        s->m_dropY = std::min(-50.0f, city.psdl.bounds.min.y - 20.0f);
    s->m_respawn = s->m_setup.playerSpawn;
    for (std::size_t i = 0; i < s->m_setup.opponents.size(); ++i)
        s->m_opponents.push_back(Racer{});
    s->m_player.player = true;
    s->beginEvent(0);
    return s;
}

std::string Session::str(std::uint32_t id, std::string_view fallback) const {
    return m_strings ? m_strings->get(id, fallback) : std::string(fallback);
}

void Session::setMessage(std::string text, float seconds, bool top) {
    HudMessage& m = top ? m_message2 : m_message;
    m.text = std::move(text);
    m.timeLeft = seconds;
    m.top = top;
}

void Session::setMessage(std::uint32_t id, std::string_view fallback, float seconds, bool top) {
    if (id == 0 && fallback.empty())
        return;
    setMessage(id ? str(id, fallback) : std::string(fallback), seconds, top);
}

bool Session::anyOrder() const { return mode() == GameMode::Blitz || mode() == GameMode::Checkpoint; }

bool Session::hasOpponentRace() const { return mode() == GameMode::Circuit || mode() == GameMode::Checkpoint; }

const LessonEvent* Session::currentLesson() const {
    if (mode() != GameMode::CrashCourse || m_setup.lessonEvents.empty())
        return nullptr;
    return &m_setup.lessonEvents[static_cast<std::size_t>(m_lessonEvent)];
}

bool Session::opponentActive(std::size_t index) const {
    if (index >= m_opponents.size())
        return false;
    if (const auto* lesson = currentLesson())
        return lesson->targetCar || lesson->type == LessonType::Follow || lesson->type == LessonType::Destroy;
    return true;
}

bool Session::policeActive() const {
    if (const auto* lesson = currentLesson())
        return lesson->type == LessonType::Evade;
    return true;
}

void Session::beginEvent(int index) {
    m_lessonEvent = index;
    if (const auto* lesson = currentLesson())
        m_checkpoints = lesson->checkpoints;
    else
        m_checkpoints = m_setup.checkpoints;

    auto resetRacer = [&](Racer& r) {
        r.cleared.assign(m_checkpoints.size(), 0);
        r.clearedCount = 0;
        r.lap = 0;
        r.passedThisLap = 0;
        r.finished = false;
        r.dnf = false;
        r.havePrev = false;
        r.lapStart = 0.0f;
        r.target = m_checkpoints.size() > 1 ? 1 : -1;
    };
    resetRacer(m_player);
    for (auto& o : m_opponents)
        resetRacer(o);

    m_phase = Phase::Countdown;
    m_countdownStep = 0;
    m_stateWait = 2.0f * kCountdownStep;
    m_released = false;
    m_penalty = false;
    m_raceTime = 0.0f;
    m_lastWarnSecond = -1;
    m_reachedMinSpeed = false;
    m_lessonFailed = false;
    m_baseVehicleImpacts = m_baseObjectImpacts = -1;
    m_vehicleHits = m_objectHits = 0;
    m_hasClock = false;
    if (mode() == GameMode::Blitz && m_setup.timeLimit > 0.0f) {
        m_hasClock = true;
        m_clock = m_setup.timeLimit;
    } else if (const auto* lesson = currentLesson(); lesson && lesson->timeLimit > 0.0f) {
        m_hasClock = true;
        m_clock = lesson->timeLimit;
    }
    if (!m_checkpoints.empty())
        m_respawn = spawnAt(m_checkpoints.front());
    if (index > 0)
        push(EventType::Respawn);
    if (currentLesson())
        push(EventType::LessonEventStarted, index);
}

void Session::start() {
    m_started = true;
    if (mode() == GameMode::Cruise || m_options.skipCountdown) {
        // Cruise has no countdown (MM1 mmGameSingle roam starts driving at once).
        m_phase = Phase::Racing;
        m_released = true;
        if (mode() != GameMode::Cruise)
            push(EventType::CountdownGo);
        return;
    }
    // Lesson introduction on the upper line.
    if (const auto* lesson = currentLesson()) {
        const LessonStrings ls = lessonStrings(lesson->type);
        if (ls.intro) {
            std::string text = str(ls.intro, "");
            if (lesson->type == LessonType::MinimumSpeed) {
                // "Maintain %.0f through the checkpoints"
                const auto pos = text.find("%.0f");
                if (pos != std::string::npos)
                    text.replace(pos, 4, std::format("{:.0f}", lesson->minimumSpeedMph));
            }
            setMessage(text, 2.0f * kCountdownStep + 2.0f, true);
        }
    }
}

bool Session::playerHeld() const {
    if (!m_started)
        return true;
    if (m_phase == Phase::Countdown)
        return true;
    return m_penalty && m_penaltyLeft > 0.0f;
}

void Session::updateCountdown(float dt, const PlayerState& player) {
    std::uint32_t ready = 0, set = 0, go = 0, penalty = 0;
    if (const auto* lesson = currentLesson()) {
        const LessonStrings ls = lessonStrings(lesson->type);
        ready = ls.ready;
        set = ls.set;
        go = ls.go;
    } else {
        const ModeStrings ms = modeStrings(mode());
        ready = ms.ready;
        set = ms.set;
        go = ms.go;
        penalty = ms.penalty;
    }
    if (m_countdownStep == 0 && m_stateWait == 2.0f * kCountdownStep)
        push(EventType::CountdownReady);

    // False start (inferred trigger: throttle during the countdown, in modes
    // whose string group has a penalty line).
    if (penalty && !m_penalty && player.throttle > 0.1f) {
        m_penalty = true;
        m_penaltyLeft = kFalseStartPenalty;
        setMessage(penalty, "Wait...5 second penalty!", kCountdownStep * 2.0f, true);
        push(EventType::FalseStart);
    }

    m_stateWait -= dt;
    if (m_countdownStep == 0) {
        if (m_stateWait > kCountdownStep) {
            setMessage(ready, "Ready...", kCountdownStep);
            return;
        }
        m_countdownStep = 1;
        push(EventType::CountdownSet);
    }
    if (m_stateWait > 0.0f) {
        if (set)
            setMessage(set, "Set...", kCountdownStep);
        return;
    }
    // Go!
    m_phase = Phase::Racing;
    m_released = true;
    setMessage(go, "Go!", kCountdownStep);
    push(EventType::CountdownGo);
}

void Session::updateTarget(Racer& r, const Vec3& pos) {
    const int n = static_cast<int>(m_checkpoints.size());
    if (n < 2) {
        r.target = -1;
        return;
    }
    if (anyOrder()) {
        // "The arrow will point toward the nearest checkpoint" (race help
        // text); the finish once everything else is cleared.
        int best = -1;
        float bestD = 1e30f;
        for (int i = 1; i < n - 1; ++i) {
            if (r.cleared[static_cast<std::size_t>(i)])
                continue;
            const float d = horizontalDistance(pos, m_checkpoints[static_cast<std::size_t>(i)].position);
            if (d < bestD) {
                bestD = d;
                best = i;
            }
        }
        if (best < 0)
            best = n - 1;
        r.target = best;
    }
    if (r.target >= 0)
        r.distanceToTarget = horizontalDistance(pos, m_checkpoints[static_cast<std::size_t>(r.target)].position);
}

void Session::clearAnyOrder(Racer& r, int index, bool player) {
    r.cleared[static_cast<std::size_t>(index)] = 1;
    ++r.clearedCount;
    if (!player)
        return;
    push(EventType::CheckpointCleared, index);
    m_respawn = spawnAt(m_checkpoints[static_cast<std::size_t>(index)]);
    const int intermediate = static_cast<int>(m_checkpoints.size()) - 2;
    if (r.clearedCount == intermediate) {
        push(EventType::FinishActivated);
        push(EventType::FinalCheckpoint);
    }
}

void Session::advanceInOrder(Racer& r, bool player) {
    const int n = static_cast<int>(m_checkpoints.size());
    const int hit = r.target;
    if (player && hit > 0)
        m_respawn = spawnAt(m_checkpoints[static_cast<std::size_t>(hit)]);
    if (mode() == GameMode::Circuit) {
        // mmWaypoints::Update, circuit: crossing waypoint 0 completes a lap.
        if (hit == 0) {
            ++r.lap;
            r.passedThisLap = 0;
            const float lapTime = m_raceTime - r.lapStart;
            r.lastLap = lapTime;
            r.bestLap = r.bestLap > 0.0f ? std::min(r.bestLap, lapTime) : lapTime;
            r.lapStart = m_raceTime;
            if (player)
                push(EventType::LapCompleted, r.lap, lapTime);
            if (r.lap >= m_setup.laps) {
                finishRacer(r, player);
                return;
            }
            if (player) {
                if (r.lap == m_setup.laps - 1) {
                    setMessage(62, "Final lap!", 3.0f, true);
                    push(EventType::FinalLap);
                } else {
                    setMessage(std::format("{} {:d}:{:05.2f}", str(63, "Lap time"), static_cast<int>(lapTime) / 60,
                                           std::fmod(lapTime, 60.0f)),
                               3.0f, true);
                }
            }
        } else {
            ++r.passedThisLap;
            if (player) {
                push(EventType::CheckpointCleared, hit);
                if (hit == n - 1 && r.lap == m_setup.laps - 1)
                    push(EventType::FinalCheckpoint);
            }
        }
        r.target = (hit + 1) % n;
        return;
    }
    // Crash course (inferred: checkpoints in order, the last is the finish).
    if (player && hit > 0) {
        const LessonEvent* lesson = currentLesson();
        if (lesson && lesson->type == LessonType::MinimumSpeed &&
            m_playerSpeedMph + 0.5f < lesson->minimumSpeedMph) {
            failLesson(238, "You have not maintained \\n the minimum speed!");
            return;
        }
    }
    ++r.clearedCount;
    if (hit >= 0)
        r.cleared[static_cast<std::size_t>(hit)] = 1;
    if (hit == n - 1) {
        finishRacer(r, player);
        return;
    }
    if (player)
        push(EventType::CheckpointCleared, hit);
    r.target = hit + 1;
}

void Session::finishRacer(Racer& r, bool player) {
    if (r.finished)
        return;
    r.finished = true;
    r.finishTime = m_raceTime;
    r.finishPosition = ++m_finishOrder;
    if (!player) {
        const auto idx = static_cast<int>(&r - m_opponents.data());
        push(EventType::OpponentFinished, idx, static_cast<float>(r.finishPosition));
        return;
    }
    if (mode() == GameMode::CrashCourse)
        return; // the lesson rules decide
    // MM1 ProgressCheck: a race counts as won within MustPlace for amateurs,
    // only in first place for professionals.
    const int pos = mode() == GameMode::Blitz ? 1 : r.finishPosition;
    const int mustPlace = m_setup.config.difficulty == Difficulty::Professional ? 1 : m_setup.mustPlace;
    const ModeStrings ms = modeStrings(mode());
    if (mode() == GameMode::Blitz) {
        endRace(true, true, ms.won, "You Won!");
    } else {
        const std::uint32_t id = pos <= 8 ? ms.finishedFirst + static_cast<std::uint32_t>(pos - 1) : ms.loaf;
        endRace(true, pos <= mustPlace, id, std::format("You finished #{}", pos));
    }
    m_resultPosition = pos;
    push(EventType::PlayerFinished, pos, m_raceTime);
}

void Session::endRace(bool finished, bool won, std::uint32_t messageId, std::string_view fallback) {
    if (m_phase == Phase::PostRace || m_phase == Phase::Done)
        return;
    m_phase = Phase::PostRace;
    m_stateWait = kPostRaceDelay;
    m_resultFinished = finished;
    m_resultWon = won;
    m_resultTime = m_raceTime;
    if (messageId || !fallback.empty())
        setMessage(messageId, fallback, kPostRaceDelay);
}

void Session::updateRacer(Racer& r, const Mat34& car, bool player) {
    const Vec3 pos = car.m3;
    // A jump of more than 50 m in one update is a respawn or teleport, not
    // driving: do not test the path in between against the gates.
    if (!r.havePrev || pos.dist2(r.prevPos) > 50.0f * 50.0f) {
        r.prevPos = pos;
        r.havePrev = true;
    }
    const int n = static_cast<int>(m_checkpoints.size());
    if (!r.finished && n >= 2 && m_phase == Phase::Racing) {
        auto hits = [&](int i) {
            const Checkpoint& cp = m_checkpoints[static_cast<std::size_t>(i)];
            return player ? gateHit(cp, r.prevPos, car, m_options.playerExtent)
                          : aiGateHit(cp, r.prevPos, pos, std::min(cp.radius, 5.0f));
        };
        if (!player && mode() == GameMode::CrashCourse) {
            // Lesson cars (the cab to follow, the car to ram) only matter
            // once they reach the destination (inferred).
            const Checkpoint& last = m_checkpoints.back();
            if (aiGateHit(last, r.prevPos, pos, last.radius))
                finishRacer(r, player);
        } else if (anyOrder()) {
            // mmWaypoints types 2/3: any intermediate checkpoint, then the
            // finish once all are cleared (AnyWPHits).
            const int intermediate = n - 2;
            for (int i = 1; i < n - 1; ++i)
                if (!r.cleared[static_cast<std::size_t>(i)] && hits(i))
                    clearAnyOrder(r, i, player);
            if (r.clearedCount >= intermediate && hits(n - 1))
                finishRacer(r, player);
        } else if (r.target >= 0 && hits(r.target)) {
            advanceInOrder(r, player);
        }
    }
    updateTarget(r, pos);
    r.prevPos = pos;
}

void Session::updateRanks() {
    const int n = std::max(1, static_cast<int>(m_checkpoints.size()));
    std::vector<Racer*> all{&m_player};
    for (std::size_t i = 0; i < m_opponents.size(); ++i)
        if (opponentActive(i))
            all.push_back(&m_opponents[i]);
    auto progress = [&](const Racer& r) {
        if (mode() == GameMode::Circuit)
            return r.lap * n + r.passedThisLap;
        return r.clearedCount;
    };
    std::ranges::stable_sort(all, [&](const Racer* a, const Racer* b) {
        if (a->finished != b->finished)
            return a->finished;
        if (a->finished)
            return a->finishPosition < b->finishPosition;
        if (progress(*a) != progress(*b))
            return progress(*a) > progress(*b);
        return a->distanceToTarget < b->distanceToTarget;
    });
    for (std::size_t i = 0; i < all.size(); ++i)
        all[i]->rank = static_cast<int>(i) + 1;
}

void Session::checkHazards(float dt, const PlayerState& player) {
    // Falling out of the city (mmGame::Update: y below -50).
    if (player.transform.m3.y < m_dropY) {
        setMessage(29, "That didn't happen!", 2.0f);
        push(EventType::Respawn);
        m_waterTimer = 0.0f;
        return;
    }
    // Water: message at once, respawn 4 s later (HitWaterTimer 1 -> 5).
    if (player.inWater || m_waterTimer > 0.0f) {
        if (m_waterTimer == 0.0f) {
            // London's line is "More tea, vicar?" (string 642), the generic
            // one "Sleep with the fishes!" (30/643) - assignment inferred.
            if (m_city == "london")
                setMessage(642, "More tea, vicar?", 3.0f);
            else
                setMessage(30, "Sleep with the fishes!", 3.0f);
            push(EventType::HitWater);
        }
        m_waterTimer += dt;
        if (m_waterTimer > kWaterRespawn) {
            m_waterTimer = 0.0f;
            push(EventType::Respawn);
        }
    }
}

void Session::failLesson(std::uint32_t id, std::string_view fallback) {
    m_lessonFailed = true;
    endRace(false, false, id, fallback);
    push(EventType::LessonFailed, m_lessonEvent);
}

void Session::updateLesson(float dt, const PlayerState& player, std::span<const OpponentState> opponents) {
    const LessonEvent* lesson = currentLesson();
    if (!lesson || m_phase != Phase::Racing)
        return;
    const LessonStrings ls = lessonStrings(lesson->type);
    (void)dt;

    auto fail = [&](std::uint32_t id, std::string_view fallback) { failLesson(id, fallback); };

    // Collision counters (Clean lessons show them; "Hit Objects:", "Hit Vehicles:").
    if (m_baseVehicleImpacts < 0) {
        m_baseVehicleImpacts = player.vehicleImpacts;
        m_baseObjectImpacts = player.objectImpacts;
    }
    m_vehicleHits = player.vehicleImpacts - m_baseVehicleImpacts;
    m_objectHits = player.objectImpacts - m_baseObjectImpacts;

    switch (lesson->type) {
    case LessonType::Clean:
        if (m_vehicleHits > 0) {
            fail(ls.lose, "You scraped the paint!");
            return;
        }
        break;
    case LessonType::MinimumSpeed:
        if (!m_reachedMinSpeed && player.speedMph >= lesson->minimumSpeedMph) {
            m_reachedMinSpeed = true;
            setMessage(237, "You have reached the minimum speed!", 2.0f, true);
        } else if (!m_reachedMinSpeed && m_raceTime < 0.1f) {
            setMessage(str(235, "You need to get up to speed") + " " + str(236, "before hitting a checkpoint"), 3.0f,
                       true);
        }
        break;
    case LessonType::Follow:
    case LessonType::Destroy: {
        if (opponents.empty() || m_opponents.empty())
            break;
        const OpponentState& target = opponents[0];
        const Racer& tr = m_opponents[0];
        if (lesson->type == LessonType::Destroy) {
            if (target.wrecked || target.damage01 >= 1.0f) {
                endRace(true, true, ls.win, "You did it!");
                push(EventType::LessonPassed, m_lessonEvent);
                return;
            }
            if (tr.finished) {
                fail(621, "Car reached destination");
                return;
            }
        } else {
            const float d = horizontalDistance(player.transform.m3, target.transform.m3);
            if (d > kFollowEscapeDistance) {
                fail(222, "Car escaped!");
                return;
            }
            if (tr.finished) {
                endRace(true, true, ls.win, "You Won!");
                push(EventType::LessonPassed, m_lessonEvent);
                return;
            }
        }
        break;
    }
    default: break;
    }
}

void Session::update(float dt, const PlayerState& player, std::span<const OpponentState> opponents,
                     std::span<const OpponentState> police) {
    if (!m_started)
        return;
    m_playerSpeedMph = player.speedMph;
    if (m_message.timeLeft > 0.0f)
        m_message.timeLeft -= dt;
    if (m_message2.timeLeft > 0.0f)
        m_message2.timeLeft -= dt;
    m_resultDamage = player.damage01;

    switch (m_phase) {
    case Phase::Countdown:
        updateCountdown(dt, player);
        updateRacer(m_player, player.transform, true);
        return;
    case Phase::PostRace:
        m_stateWait -= dt;
        if (m_stateWait <= 0.0f) {
            // Crash course exams: continue with the next event after a pass.
            if (mode() == GameMode::CrashCourse && m_resultWon && !m_lessonFailed &&
                m_lessonEvent + 1 < static_cast<int>(m_setup.lessonEvents.size())) {
                beginEvent(m_lessonEvent + 1);
                m_resultWon = m_resultFinished = false;
                start();
                return;
            }
            m_phase = Phase::Done;
            push(EventType::SessionOver);
        }
        return;
    case Phase::Done: return;
    case Phase::Racing: break;
    }

    m_raceTime += dt;
    m_idleTime = player.speedMph < 2.0f ? m_idleTime + dt : 0.0f;
    if (m_penalty && m_penaltyLeft > 0.0f) {
        m_penaltyLeft -= dt;
        if (m_penaltyLeft <= 0.0f)
            push(EventType::PenaltyOver);
    }

    // Player and opponent progress.
    updateRacer(m_player, player.transform, true);
    for (std::size_t i = 0; i < m_opponents.size() && i < opponents.size(); ++i)
        if (opponentActive(i))
            updateRacer(m_opponents[i], opponents[i].transform, false);
    updateRanks();
    if (m_phase != Phase::Racing)
        return;

    checkHazards(dt, player);

    // Count-down clock (Blitz, timed lessons).
    if (m_hasClock) {
        m_clock -= dt;
        if (m_clock < kTimerWarning && m_clock > 0.0f) {
            const int second = static_cast<int>(std::ceil(m_clock));
            if (second != m_lastWarnSecond) {
                m_lastWarnSecond = second;
                push(EventType::TimerWarning, -1, m_clock);
            }
        }
        if (m_clock <= 0.0f) {
            m_clock = 0.0f;
            push(EventType::TimeUp);
            if (const auto* lesson = currentLesson()) {
                m_lessonFailed = true;
                endRace(false, false, lessonStrings(lesson->type).timeUp, "Time's up!");
                push(EventType::LessonFailed, m_lessonEvent);
            } else {
                endRace(false, false, modeStrings(mode()).timeUp, "Time's up!");
            }
            return;
        }
    }

    // Wrecked: the race is over (mmSingleBlitz: IsMaxDamaged).
    if (player.wrecked && mode() != GameMode::Cruise) {
        push(EventType::Wrecked);
        const ModeStrings ms = modeStrings(mode());
        if (currentLesson()) {
            m_lessonFailed = true;
            endRace(false, false, 226, "Your warranty just ran out!");
            push(EventType::LessonFailed, m_lessonEvent);
        } else {
            endRace(false, false, ms.gameOver ? ms.gameOver : ms.loaf, "Game over!");
        }
        return;
    }

    if (mode() == GameMode::CrashCourse) {
        updateLesson(dt, player, opponents);
        if (m_phase != Phase::Racing)
            return;
        const LessonEvent* lesson = currentLesson();
        if (lesson && m_player.finished) {
            // Reached the last checkpoint.
            if (lesson->type == LessonType::Evade) {
                for (const auto& chaser : police)
                    if (horizontalDistance(chaser.transform.m3, player.transform.m3) < kEvadeClearDistance) {
                        // Not yet: lose them first, then come back.
                        m_player.finished = false;
                        m_player.target = static_cast<int>(m_checkpoints.size()) - 1;
                        setMessage(197, "Lose your pursuers before you finish!", 2.0f, true);
                        return;
                    }
            }
            if (lesson->type == LessonType::Follow || lesson->type == LessonType::Destroy)
                return; // decided by the target car
            const bool final = m_lessonEvent + 1 >= static_cast<int>(m_setup.lessonEvents.size());
            const bool exam = m_setup.config.raceIndex == 12 && final;
            endRace(true, true, exam ? 610 : lessonStrings(lesson->type).win, "Good driving!");
            push(EventType::LessonPassed, m_lessonEvent);
        }
    }
}

std::vector<Event> Session::takeEvents() {
    std::vector<Event> out;
    out.swap(m_events);
    return out;
}

bool Session::checkpointCleared(std::size_t i) const {
    return i < m_player.cleared.size() && m_player.cleared[i] != 0;
}

bool Session::checkpointVisible(std::size_t i) const {
    const std::size_t n = m_checkpoints.size();
    if (i >= n || n < 2 || mode() == GameMode::Cruise)
        return false;
    if (m_phase == Phase::Done)
        return false;
    if (mode() == GameMode::Circuit)
        return true; // all gates stay up for every lap
    if (i == 0)
        return false; // the start line
    if (anyOrder()) {
        if (i == n - 1)
            return m_player.clearedCount >= static_cast<int>(n) - 2; // MM1 activates the finish when the rest are done
        return !checkpointCleared(i);
    }
    // In-order lessons: the next one and those after it.
    return static_cast<int>(i) >= m_player.target && !checkpointCleared(i);
}

std::optional<Vec3> Session::arrowTarget() const {
    if (mode() == GameMode::Cruise || m_player.target < 0 || m_phase == Phase::Done ||
        static_cast<std::size_t>(m_player.target) >= m_checkpoints.size())
        return std::nullopt;
    return m_checkpoints[static_cast<std::size_t>(m_player.target)].position;
}

int Session::checkpointsCleared() const {
    if (mode() == GameMode::Circuit)
        return m_player.passedThisLap;
    return m_player.clearedCount;
}

int Session::checkpointsTotal() const {
    const int n = static_cast<int>(m_checkpoints.size());
    if (mode() == GameMode::Circuit)
        return std::max(0, n - 1);
    if (anyOrder())
        return std::max(0, n - 2);
    return std::max(0, n - 1);
}

int Session::lap() const { return std::min(m_player.lap + 1, std::max(1, m_setup.laps)); }

float Session::timeRemaining() const { return m_hasClock ? m_clock : -1.0f; }

float Session::lapTime() const { return m_raceTime - m_player.lapStart; }

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
    // mmGame::CalculateRaceScore: 50 / 25 / 10 points for 1st / 2nd / 3rd,
    // times the car's ScoringBias, times a multiplier (here: 2 for
    // professionals, inferred).
    if (r.finished && mode() != GameMode::CrashCourse && mode() != GameMode::Cruise) {
        const int base = r.position == 1 ? 50 : r.position == 2 ? 25 : r.position == 3 ? 10 : 0;
        const int mult = m_setup.config.difficulty == Difficulty::Professional ? 2 : 1;
        r.score = static_cast<int>(static_cast<float>(base) * m_options.scoringBias * static_cast<float>(mult));
    }
    return r;
}

} // namespace mm2::game::session
