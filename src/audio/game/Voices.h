#pragma once

// Spoken lines: the race announcer (MM2's mmRaceSpeech on AudSpeech /
// AudSpeechData, driven by the aud/spchdata tables) and "creature" voices
// (ambient drivers and pedestrians: AudCreature, AudCreatureAvoid,
// AudCreatureImpact with the aud/creaturedata tables).

#include "audio/game/AudioTables.h"
#include "audio/game/Object3D.h"
#include "audio/game/SoundSlot.h"

#include <map>
#include <random>
#include <string>

namespace mm2::audio::game {

enum class AnnouncerMode { Cruise, Blitz, Checkpoint, Circuit };
enum class RaceOutcome { Poor, Mid, Win };

class Announcer {
public:
    // mmRaceSpeech::LoadCityInfo: aud/spchdata/<city>.csv ("Num announcers",
    // "prefix": AL -> al1..al6) and a random announcer for the session.
    bool load(const vfs::Vfs& vfs, SoundBank& bank, Mixer& mixer, std::string_view city);
    // index < 0 picks at random from 1..count, then (SF only) 3 becomes 4:
    // San Francisco lists five announcers but ships as1, as2, as4 and as5, so
    // as4 is picked twice as often.
    void beginSession(int index = -1);
    std::string announcerId() const; // "al3"
    int announcerCount() const { return m_count; }

    // mmSpeechContainer::InitRace + mmPlayer::InitSpeechAudio: the groups for
    // a race: the mode's table, the weather's pre-race lines (none in snow),
    // the time of day's (evening only in clear weather), the car's pre-race and
    // results lines, the final lap and the damage penalty (cruise or race).
    // weather: 0 clear, 1 cloudy, 2 fog, 3 rain, 4 snow; timeOfDay: 0 morning,
    // 1 noon, 2 evening, 3 night.
    void beginRace(AnnouncerMode mode, std::string_view car, int timeOfDay, int weather);

    // mmRaceSpeech. Each returns the line chosen, or "" when nothing plays.
    // The pre-race line waits 1.5 s; the unlock lines wait 0.1 s behind any
    // line playing; every other event cuts the current line off.
    std::string playPreRace();
    std::string playFinalCheckpoint();
    std::string playFinalLap();
    std::string playDamagePenalty();
    std::string playRaceProgress();
    // PlayResults(position, racers): 1st wins, a place above half the field
    // is "mid", anything else "poor".
    std::string playResults(int position, int racers);
    std::string playResults(RaceOutcome outcome);
    std::string playUnlockRace();
    std::string playUnlockVehicle(std::string_view car);
    // Cops & Robbers events (cnr<city>.csv: BLUETEAMHASGOLD, ROBGETLOOT, ...):
    // mmCNRSpeech::Play cuts the current line and starts after 0.01 s. Line
    // grouping is not verified (see docs/audio.md).
    std::string playCopsAndRobbers(std::string_view event);
    // Crash Course lesson lines (ccl/ccs folders): lesson 0-12, event PRERACE
    // (after 1.5 s), RESULTSPOOR or RESULTSWIN (immediate).
    std::string playCrashCourse(int lesson, std::string_view event);

    // AudSpeech::Update: a queued line starts once its delay has passed and
    // nothing is playing.
    void update(float dt);
    bool speaking() const;
    void stop(); // AudSpeech::Stop: empties the queue too
    void seed(unsigned s) { m_rng.seed(s); }

    // File names for a line set: <announcer><prefix>NN for NN in (add, end],
    // without repeating the announcer when the prefix already carries it
    // ("AL1ROBROB" in the Cops & Robbers tables).
    static std::vector<std::string> lineNames(std::string_view announcer, const SpeechLineSet& set);
    // AudSpeechData::GetRandomName: NN uniform in (add, end]; the number just
    // used moves up one, wrapping to 1 (not to add + 1).
    static int pickLine(const SpeechLineSet& set, int last, float uniform01);

private:
    struct Group {
        SpeechLineSet set;
        std::string announcer; // prefix for file names ("" when the set carries it)
        int last = -1;
    };
    struct Range {
        int first = -1, last = -1;
    };
    const SpeechTable* table(const std::string& folder, std::string_view name);
    // mmRaceSpeech::LoadGroup / SetReadState for aud/spchdata/<announcer>/<name>.csv.
    void loadGroup(std::string_view name);
    std::string lineFor(int group);
    int randomIn(const Range& r);
    // AudSpeech::Play(index, delay).
    std::string play(int group, float delay);
    std::string playEvent(int group); // Stop + Play(group, 0)

    const vfs::Vfs* m_vfs = nullptr;
    SoundBank* m_bank = nullptr;
    Mixer* m_mixer = nullptr;
    std::string m_city;
    std::string m_prefix;
    int m_count = 0;
    int m_index = 1;
    std::map<std::string, std::optional<SpeechTable>> m_tables;

    std::vector<Group> m_groups;
    Range m_preRace, m_unlockRace, m_unlockVehicle;
    int m_finalCheckpoint = -1, m_finalLap = -1, m_raceProgress = -1, m_damagePenalty = -1;
    int m_resultsPoor = -1, m_resultsMid = -1, m_resultsWin = -1;
    int m_vehicleResultsPoor = -1, m_vehicleResultsMid = -1, m_vehicleResultsWin = -1;
    int m_vehiclePreRace = -1, m_timeOfDay = -1, m_weather = -1, m_unlockTexture = -1;

    struct Queued {
        std::string line;
        float delay = 0;
        bool used = false;
    };
    Queued m_queue; // AudSpeech has one queue slot
    SoundSlot m_current;
    std::mt19937 m_rng{std::random_device{}()};
};

// One creature's voice (an ambient driver or a pedestrian): AudCreature.
class CreatureVoice {
public:
    // maxDistance: the owner's drop-off (ambient cars 100 m, pedestrians 40 m).
    void load(Mixer& mixer, SoundBank& bank, CreatureVoiceDef def, float maxDistance = 100.0f);
    // AudCreatureAvoid::Update with the creature's speed, the queued lines,
    // and the attenuation from the creature's position.
    void update(float speed, float dt, const Vec3& position, const Mat34& listener);
    // AudCreature::PlayAvoidance (the AI reports a near miss): every eligible
    // block queues one of its lines half of the time.
    void avoid();
    // AudCreature::PlayImpact: a line `delay` seconds after a hit at least as
    // hard as the table's minimum, at most once a minute across all creatures.
    void impact(float force);
    bool speaking() const;
    void seed(unsigned s) { m_rng.seed(s); }

    // AudCreatureImpact::UpdateStatics: the shared impact-line clock.
    static void advanceClock(float dt);
    static void resetGlobals();

private:
    struct Avoid {
        VoiceSpeedTrigger def;
        std::vector<SoundSlot> slots;
        float timeIn = 0, timeOut = 0;
        bool queued = false;
        float queueTime = 0;
        std::size_t line = 0;
    };
    bool eligible(const Avoid& a) const;
    void playAvoid(Avoid& a);

    CreatureVoiceDef m_def;
    std::vector<Avoid> m_avoids;
    std::vector<SoundSlot> m_impactSlots;
    bool m_impactQueued = false;
    float m_impactTime = 0;
    std::size_t m_impactLine = 0;
    Audio3D m_3d;
    float m_attenuation = 0, m_pan = 0;
    std::mt19937 m_rng{77};
};

} // namespace mm2::audio::game
