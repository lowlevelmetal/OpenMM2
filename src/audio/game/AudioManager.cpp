// AudManager::Update / AudManagerBase::UpdatePaused, ported from MM2 (see
// AudioManager.h).
#include "audio/game/AudioManager.h"

#include "audio/Mixer.h"
#include "audio/game/Voices.h"

namespace mm2::audio::game {

bool AudioManager::update(bool paused, Mixer& mixer, Announcer* speech, float dt) {
    // AudManager::Update: audManager::Update (the sound objects' own
    // bookkeeping, the mixer's job here) comes first; then, while asRoot is
    // paused, the virtual UpdatePaused and nothing else.
    if (paused) {
        // AudManagerBase::UpdatePaused: StopAllSounds for every sound type,
        // then count the update.
        if (m_pausedUpdates > kPausedStopLimit)
            return false;
        mixer.stopAll();
        ++m_pausedUpdates;
        return true;
    }
    // Running: the counter goes back to 0 and the speech container updates
    // (mmSpeechContainer::Update: AudSpeech::Update of the race, Cops and
    // Robbers and crash course speech; OpenMM2's Announcer is all three).
    m_pausedUpdates = 0;
    if (speech)
        speech->update(dt);
    return false;
}

bool AudioManager::updateFrame(bool paused, Mixer& mixer, Announcer* speech, float dt) {
    // GameLoop: AudManager::Update, then asRoot::Update, whose first node is
    // the same manager.
    const bool first = update(paused, mixer, speech, dt);
    const bool second = update(paused, mixer, speech, dt);
    return first || second;
}

} // namespace mm2::audio::game
