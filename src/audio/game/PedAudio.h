#pragma once

// Pedestrian voices: MM2's aiPedAudio (an AudCreatureContainer per
// pedestrian) with the containers' shared voices (AudCreatureContainer's
// statics). Every woman in a session speaks with one voice file and every man
// with another, both drawn when the game starts (mmGame::Init); a pedestrian
// asks for a 3D-manager slot when it dodges the player and screams, holds the
// slot while the scream plays and lets it go once it is quiet.

#include "audio/game/Object3D.h"
#include "audio/game/Voices.h"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mm2::audio::game {

// What the pedestrian simulation reports for one pedestrian each frame
// (src/ai Pedestrian: id, typeName, transform.m3, scream).
struct PedestrianSoundInput {
    int id = 0;            // stable for the pedestrian (its pool entry)
    std::string_view type; // its model ("pedmodel_woman"): aiPedestrian_IsWoman picks the voice
    Vec3 position;
    // aiPedestrian::Wander / Avoid started an avoidance reaction this step
    // (AudCreatureContainer::PlayAvoidanceReaction).
    bool avoiding = false;
    // aiPedestrian::Reset put it on a road this step: AudCreatureContainer::
    // Reset (Aud3DObject::Reset lets its slot go).
    bool reset = false;
};

class PedestrianAudio {
public:
    PedestrianAudio();
    ~PedestrianAudio();
    PedestrianAudio(const PedestrianAudio&) = delete;
    PedestrianAudio& operator=(const PedestrianAudio&) = delete;

    // mmGame::Init: aiPedAudio::SetCSVCatString("") and LoadNumFemaleChoices /
    // LoadNumMaleChoices: each sex's voice file number, RandomizeNumber(1,
    // n + 0.25) truncated, n from aud/creaturedata/numfemalepedvoicefiles.csv
    // and nummalepedvoicefiles.csv (0, and so no voice, when missing). The
    // voices are default_fpedvoice<N> and default_mpedvoice<N>, loaded once.
    void load(const vfs::Vfs& vfs, SoundBank& bank, Mixer& mixer, Object3DManager* manager = nullptr);

    // One frame, in MM2's order: AudCreatureContainer::UpdateStatics with the
    // player's speed (aiMap::Update: the impact clock and every voice), then
    // each pedestrian's aiPedestrian::Update (its container lets its slot go
    // once its voice is quiet; a new pedestrian is set up as aiPedestrian::
    // Init does: drop-offs 0..40 m, priority 8, its sex's voice) and its
    // avoidance reaction, then Aud3DObjectManager::Update's UpdateAudio for
    // the containers holding slots (the tunnel echo; attenuation and pan to
    // the voice; past 40 m the slot goes). Pedestrians not listed keep their
    // last position. `inTunnel` as CarAudioInputs::inTunnel.
    void update(std::span<const PedestrianSoundInput> peds, const Mat34& listener, float playerSpeed,
                float dt, bool inTunnel = false);
    // AudCreatureContainer::PlayImpactReaction for pedestrian `id` (MM2's
    // pedestrians never call it).
    void impact(int id, float force);
    void stop();

    int femaleFile() const { return m_femaleFile; }
    int maleFile() const { return m_maleFile; }
    bool audible(int id) const;
    bool speaking(int id) const;

    // aiPedestrian_IsWoman: the model name contains FEMALE, WOMAN, GIRL or
    // Hooker, ignoring case. The search restarts after a mismatch without
    // trying the mismatched letter again (aiPedestrian_AreStringsEqual), so
    // "wwoman" does not match.
    static bool isWoman(std::string_view type);

private:
    class Container;
    struct VoiceSet {
        std::string name;
        std::vector<CreatureVoice> perSlot; // one per 3D-manager slot
    };
    int voiceSet(std::string_view name); // AudCreatureContainer::LoadVoices
    CreatureVoice* creature(int set, int slot);
    int numFileChoice(std::string_view name) const; // LoadNumFileChoices

    const vfs::Vfs* m_vfs = nullptr;
    SoundBank* m_bank = nullptr;
    Mixer* m_mixer = nullptr;
    Object3DManager* m_manager = nullptr;
    int m_femaleFile = 0, m_maleFile = 0; // aiPedAudio::s_iFemaleFileNum / s_iMaleFileNum
    std::vector<VoiceSet> m_sets;
    std::unordered_map<std::string, int> m_hash; // AudCreature's hash: -1 = missing
    std::unordered_map<int, std::unique_ptr<Container>> m_peds;
    Mat34 m_listener = Mat34::identity();
};

} // namespace mm2::audio::game
