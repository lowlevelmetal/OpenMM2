#pragma once

// When the in-game music changes, ported from MM2: mmGame::UpdateDMusic and
// StartMusic, MMDMusicManager::UpdateMusic and MatchMusicToPlayerSpeed,
// mmPopup::PlayPauseMusic / PlayReturnMusic / ShowResults, and the race
// modes' StopSegment at the finish and when the player's car is wrecked. The
// segment indices mmSingleRaceMusicData and
// mmSingleRoamMusicData::LoadMusic assign are kept as MusicStates. Pure logic:
// the game feeds it every frame and passes the commands to a MusicPlayer.

#include "audio/Music.h"

#include <vector>

namespace mm2::audio {

class MusicDirector {
public:
    struct Command {
        MusicState state;
        MusicTiming timing;
    };

    // Speed at or below which the player counts as idle, and how long (both
    // MMDMusicManager::MatchMusicToPlayerSpeed).
    static constexpr float kIdleSpeed = 5.0f;   // m/s
    static constexpr float kIdleDelay = 5.0f;   // s
    // mmGame::StartMusic waits this long into the game.
    static constexpr float kStartDelay = 1.25f; // s

    // Cruise (single roam) has no idle-cop or results segment and no
    // countdown block.
    explicit MusicDirector(bool cruise = false);

    // mmGame::UpdateDMusic, once per unpaused frame: `speed` the player's
    // speed (m/s), `copsPursuing` vehPoliceCarAudio::GetNumCopsPursuingPlayer,
    // `airborne` vehCarAudio::IsAirBorne.
    void update(float dt, float speed, int copsPursuing, bool airborne);
    // The race modes hold the idle logic from the music start until "Go!"
    // (mmSingleCircuit / mmSingleBlitz clear MMDMusicManager +0x50).
    void raceStarted();
    // mmPopup::PlayPauseMusic / PlayReturnMusic.
    void pause();
    void resume();
    // The race modes stop the music when the player finishes (StopSegment(0),
    // at once); the music logic keeps running, so standing still for 5 s
    // brings in the idle segment.
    void finish();
    // mmSingleRace / mmSingleBlitz when the player's car is wrecked
    // (StopSegment(1)): DirectMusic composes an ending on the next beat
    // (AutoTransition to nothing, DMUS_COMMANDT_END, DMUS_COMPOSEF_BEAT);
    // OpenMM2 stops on the next beat (inferred: dmusic has no composer).
    void damagedOut();
    // mmPopup::ShowResults: the results segment, on the next beat (race
    // songs only).
    void results();

    std::vector<Command> takeCommands();
    // The "Big Air" motif: the player's car took off (rising edge).
    bool takeBigAir();

    bool started() const { return m_started; }
    MusicState current() const { return m_current; }

private:
    void segmentSwitch(MusicState s);       // DMusicObject::SegmentSwitch(int): next beat
    void autoTransition(MusicState s);      // SegmentSwitch(int, cmd, flags): next measure
    void matchMusicToPlayerSpeed(float speed, float dt);

    bool m_cruise;
    bool m_started = false;
    bool m_blocked = false;   // MMDMusicManager +0x50
    float m_seconds = 0;      // +0x10
    float m_idleTimer = 0;    // +0xc
    int m_prevCops = 0;       // +0x4c
    bool m_airborne = false;  // +0x51
    MusicState m_current = MusicState::Silent;  // DMusicObject +0x24
    MusicState m_previous = MusicState::Silent; // DMusicObject +0x28
    std::vector<Command> m_commands;
    bool m_bigAir = false;
};

} // namespace mm2::audio
