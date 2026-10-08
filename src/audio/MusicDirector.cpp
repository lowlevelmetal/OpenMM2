// Port of MM2's in-game music rules (MMDMusicManager, mmGame::StartMusic /
// UpdateDMusic, mmPopup's music switches). See MusicDirector.h and
// docs/music.md.
#include "audio/MusicDirector.h"

namespace mm2::audio {
namespace {
// MMDMusicManager::Init / Reset: the idle timer starts "expired".
constexpr float kTimerExpired = 10000.0f;
} // namespace

MusicDirector::MusicDirector(bool cruise) : m_cruise(cruise) {}

void MusicDirector::segmentSwitch(MusicState s) {
    if (s == m_current)
        return;
    m_previous = m_current;
    m_current = s;
    m_commands.push_back({s, MusicTiming::Beat});
}

void MusicDirector::autoTransition(MusicState s) {
    if (s == m_current)
        return;
    m_previous = m_current;
    m_current = s;
    m_commands.push_back({s, MusicTiming::Measure});
}

void MusicDirector::raceStarted() { m_blocked = false; }

void MusicDirector::startMusic() {
    // mmGame::StartMusic (music mode): the idle logic held in the race modes,
    // SegmentSwitch to the Start segment, MMDMusicManager::Reset (the idle
    // timer expired), then the started flag (mmGame +0x275).
    if (!m_cruise)
        m_blocked = true;
    segmentSwitch(MusicState::Start);
    m_idleTimer = kTimerExpired;
    m_started = true;
}

void MusicDirector::restart() {
    // mmGame::Reset clears the started flag and calls StartMusic at once,
    // which does nothing in the game's first 1.25 s (UpdateDMusic starts the
    // music later then).
    m_started = false;
    if (kStartDelay <= m_seconds)
        startMusic();
}

void MusicDirector::update(float dt, float speed, int copsPursuing, bool airborne) {
    // UpdateSeconds.
    m_seconds += dt;
    if (m_seconds < 0.0f)
        m_seconds = 100.0f;
    if (!m_started) {
        // StartMusic: 1.25 s in, the race modes block the idle logic until
        // "Go!" and the Start segment begins.
        if (m_seconds < kStartDelay)
            return;
        startMusic();
        return;
    }
    // UpdateMusic.
    matchMusicToPlayerSpeed(speed, dt);
    // The chase music starts when the count of pursuing cops goes from 0 to
    // exactly 1 and ends when it goes from 1 back to 0 (other changes, e.g.
    // 0 -> 2, switch nothing).
    if (copsPursuing == 1 && m_prevCops == 0)
        segmentSwitch(MusicState::CopChase);
    else if (copsPursuing == 0 && m_prevCops == 1)
        segmentSwitch(MusicState::Return);
    m_prevCops = copsPursuing;
    if (airborne) {
        if (!m_airborne) {
            m_bigAir = true;
            m_airborne = true;
        }
    } else {
        m_airborne = false;
    }
}

void MusicDirector::matchMusicToPlayerSpeed(float speed, float dt) {
    // Cruise has no idle-cop segment (mmSingleRoamMusicData leaves its index
    // at -1), and no results segment.
    const bool hasIdleCops = !m_cruise;
    if (!m_cruise && m_current == MusicState::Results)
        return;
    if (speed <= kIdleSpeed && !m_blocked) {
        if (kIdleDelay <= m_idleTimer && m_current != MusicState::Idle && m_current != MusicState::IdleCops &&
            m_current != MusicState::Paused) {
            if (m_current != MusicState::CopChase)
                autoTransition(MusicState::Idle);
            else if (hasIdleCops)
                autoTransition(MusicState::IdleCops);
        }
        m_idleTimer += dt;
        return;
    }
    if (m_idleTimer == 0.0f)
        return;
    if (m_current == MusicState::Idle)
        autoTransition(MusicState::Return);
    else if (hasIdleCops && m_current == MusicState::IdleCops)
        autoTransition(MusicState::CopChase);
    m_idleTimer = 0.0f;
}

void MusicDirector::pause() { segmentSwitch(MusicState::Paused); }

void MusicDirector::resume() {
    // PlayReturnMusic: back to the previous segment (DMusicObject +0x28), from
    // its start; nothing if that is the current one.
    segmentSwitch(m_previous);
}

void MusicDirector::finish() {
    // StopSegment(0): silence; the segment index stays.
    m_commands.push_back({MusicState::Silent, MusicTiming::Immediate});
}

void MusicDirector::damagedOut() {
    // StopSegment(1): an ending on the next beat; the segment index stays.
    m_commands.push_back({MusicState::Silent, MusicTiming::Beat});
}

void MusicDirector::results() {
    // Cruise has no results segment (mmSingleRoamMusicData leaves its index
    // at -1) and no results popup.
    if (m_cruise || m_current == MusicState::Results)
        return;
    // ShowResults: SegmentSwitch(results, DMUS_COMMANDT_END,
    // DMUS_COMPOSEF_BEAT): a composed transition on the next beat.
    m_previous = m_current;
    m_current = MusicState::Results;
    m_commands.push_back({MusicState::Results, MusicTiming::Beat});
}

void MusicDirector::finalStretch() {
    // mmWaypoints::Update: SegmentSwitch(+0x24, DMUS_COMMANDT_END,
    // DMUS_COMPOSEF_BEAT), which does nothing for the segment already playing.
    if (m_current == MusicState::CopChase)
        return;
    m_previous = m_current;
    m_current = MusicState::CopChase;
    m_commands.push_back({MusicState::CopChase, MusicTiming::Beat});
}

std::vector<MusicDirector::Command> MusicDirector::takeCommands() {
    std::vector<Command> out;
    out.swap(m_commands);
    return out;
}

bool MusicDirector::takeBigAir() {
    const bool b = m_bigAir;
    m_bigAir = false;
    return b;
}

} // namespace mm2::audio
