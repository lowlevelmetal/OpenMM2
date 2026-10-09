#pragma once

// What MM2's audio manager does every frame on its own, outside the sound
// objects: AudManager::Update, with AudManagerBase::UpdatePaused while the
// game is paused. It runs twice a frame: GameLoop calls it before the game
// updates, and asRoot::Update calls it again as asRoot's first node
// (BeginPhase adds the manager to asRoot).
//
// * While the game is paused (the popup menu, the full-screen map) every wave
//   sound stops, on the first two paused updates (UpdatePaused stops all
//   sounds while its counter is at most 1, AudManagerBase +0x1e): both in the
//   first paused frame. Loops start again from the beginning when the game
//   runs on (their owners' updates play them when they are not playing);
//   one-shots and the announcer's line are cut. The music is DirectMusic, not
//   one of the manager's sounds, and plays on.
// * While the game runs the announcer's queue is updated here
//   (mmSpeechContainer::Update), twice, and mmGame::Update updates it again
//   later in the same frame, so its delays count down three times as fast as
//   the clock. While the game is paused only mmGame::Update's call is left:
//   queued lines keep counting down and can start (after the two stops).

namespace mm2::audio {
class Mixer;
}

namespace mm2::audio::game {

class Announcer;

class AudioManager {
public:
    // AudManagerBase::UpdatePaused stops all sounds while its counter is at
    // most this (a constant 1 in MM2's data), so on two paused updates.
    static constexpr int kPausedStopLimit = 1;

    // AudManager::Update for one frame: `paused` is asRoot's pause state as the
    // frame starts, `speech` the race's announcer (null without commentary:
    // mmPlayer::InitSpeechAudio builds no mmSpeechContainer then). Returns
    // whether every sound was stopped (audManager::StopAllSounds; OpenMM2's
    // Mixer::stopAll, which leaves the music streams alone).
    bool update(bool paused, Mixer& mixer, Announcer* speech, float dt);
    // One frame's AudManager::Update calls: GameLoop's, then asRoot's.
    // Returns whether either stopped every sound.
    bool updateFrame(bool paused, Mixer& mixer, Announcer* speech, float dt);

    // The paused updates counted so far (AudManagerBase +0x1e); 0 while the
    // game runs.
    int pausedUpdates() const { return m_pausedUpdates; }

private:
    int m_pausedUpdates = 0;
};

} // namespace mm2::audio::game
