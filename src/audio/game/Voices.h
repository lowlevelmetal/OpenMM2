#pragma once

// Spoken lines: the race announcer (MM1's mmVoiceCommentary; MM2 drives it
// from aud/spchdata tables) and "creature" voices (ambient drivers yelling,
// pedestrians screaming; aud/creaturedata).

#include "audio/game/AudioTables.h"
#include "audio/game/SoundSlot.h"

#include <deque>
#include <map>
#include <random>
#include <string>

namespace mm2::audio::game {

enum class AnnouncerMode { Cruise, Blitz, Checkpoint, Circuit };
enum class RaceOutcome { Poor, Mid, Win };

class Announcer {
public:
    // Reads aud/spchdata/<city>.csv ("Num announcers", "prefix": AL -> al1..al6).
    bool load(const vfs::Vfs& vfs, SoundBank& bank, Mixer& mixer, std::string_view city);
    // Picks the announcer for a session (random when index < 0, as MM1
    // seeded its Random from the clock). Only announcers whose folder exists
    // are chosen at random.
    void beginSession(int index = -1);
    std::string announcerId() const; // "al3"
    int announcerCount() const { return m_count; }

    // Plays a random line of `event` from aud/spchdata/<folder>/<table>.csv
    // (folder defaults to the announcer). Returns the sample name or "".
    std::string play(std::string_view table, std::string_view event, std::string_view folder = {});

    // MM1 mmVoiceCommentary equivalents.
    std::string playPreRace(AnnouncerMode mode, std::string_view car, int timeOfDay, int weather);
    std::string playFinalCheckpoint(AnnouncerMode mode);
    std::string playFinalLap();
    std::string playResults(AnnouncerMode mode, RaceOutcome outcome, std::string_view car);
    std::string playDamagePenalty(bool cruise);
    std::string playUnlockVehicle(std::string_view car);
    // Cops & Robbers events (cnr<city>.csv): BLUETEAMHASGOLD, ROBGETLOOT, ...
    std::string playCopsAndRobbers(std::string_view event);
    // Crash Course lesson lines (ccl/ccs folders): lesson 0-12, event PRERACE,
    // RESULTSPOOR or RESULTSWIN.
    std::string playCrashCourse(int lesson, std::string_view event);

    // Lines play one at a time; a line requested while another plays waits.
    void update(float dt);
    bool speaking() const;
    void stop();
    void seed(unsigned s) { m_rng.seed(s); }

    // File names for a line set: <announcer><prefix>NN for NN in (add, end],
    // without repeating the announcer when the prefix already carries it
    // ("AL1ROBROB" in the Cops & Robbers tables).
    static std::vector<std::string> lineNames(std::string_view announcer, const SpeechLineSet& set);

private:
    const SpeechTable* table(const std::string& folder, std::string_view name);
    std::string enqueue(const std::vector<std::string>& candidates);

    const vfs::Vfs* m_vfs = nullptr;
    SoundBank* m_bank = nullptr;
    Mixer* m_mixer = nullptr;
    std::string m_city;
    std::string m_prefix;
    int m_count = 0;
    int m_index = 1;
    std::vector<int> m_available; // announcers whose folder exists (SF lists 5 but ships as1, as2, as4, as5)
    std::map<std::string, std::optional<SpeechTable>> m_tables;
    std::deque<std::string> m_queue;
    SoundSlot m_current;
    std::string m_last;
    std::mt19937 m_rng{std::random_device{}()};
};

// One creature's voice (an ambient driver or a pedestrian), positioned in 3D.
class CreatureVoice {
public:
    void load(Mixer& mixer, SoundBank& bank, CreatureVoiceDef def);
    // `speed` is the speed that provokes the creature (inferred: the
    // creature's own speed for drivers, the approaching car's speed for
    // pedestrians). A trigger fires once the speed has stayed in its range
    // for minTimeInRange seconds and re-arms after leaving it for longer than
    // maxTimeOutOfRange.
    void update(float speed, float dt, const Emitter3D& where);
    void impact(float force, const Emitter3D& where);
    bool speaking() const;
    void seed(unsigned s) { m_rng.seed(s); }

private:
    void say(const std::vector<VoiceLine>& lines, const Emitter3D& where);

    struct TriggerState {
        float timeIn = 0, timeOut = 0;
        bool fired = false;
    };
    CreatureVoiceDef m_def;
    std::vector<TriggerState> m_state;
    Mixer* m_mixer = nullptr;
    SoundBank* m_bank = nullptr;
    SoundSlot m_slot;
    std::vector<VoiceLine> m_pending;
    float m_pendingDelay = 0;
    Emitter3D m_pendingWhere;
    std::mt19937 m_rng{77};
};

} // namespace mm2::audio::game
