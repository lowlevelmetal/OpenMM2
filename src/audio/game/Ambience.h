#pragma once

// City ambience (aud/ambient/*.csv: river traffic, gulls, tube voices,
// cable cars, buoys...) and rain. Roles of MM1's mmAmbientAudio and
// mmRainAudio (Open1560 game.asm); MM2 moved the ambient emitters into data.

#include "audio/game/AudioTables.h"
#include "audio/game/SoundSlot.h"

#include <random>
#include <string>
#include <vector>

namespace mm2::audio::game {

class CityAmbience {
public:
    // Loads the sets listed in aud/ambient/<city>ambientcontainer.csv.
    bool load(const vfs::Vfs& vfs, SoundBank& bank, Mixer& mixer, std::string_view city);
    // Loads one more set (e.g. "subwaycar", "ferry", "drawbridge",
    // "femaleped1") for sounds the game triggers itself. Returns its index.
    int loadSet(const vfs::Vfs& vfs, SoundBank& bank, Mixer& mixer, std::string_view name);

    // Positional loops follow the listener to the nearest emitter point;
    // random one-shots fire at the intervals given in the tables (inferred
    // semantics of "sample type" and "audible area", see docs/audio.md).
    void update(const Vec3& listener, float dt);
    // Plays sample `sample` of set `name` once at `position` (types 0-3).
    void playAt(std::string_view name, int sample, const Vec3& position, const Vec3& velocity = {});
    // Starts/stops a looping sample attached to a moving object (subway car,
    // ferry engine); `id` distinguishes several objects using the same set.
    void setLoop(std::string_view name, int sample, int id, bool on, const Vec3& position, const Vec3& velocity = {});
    void stop();

    std::size_t setCount() const { return m_sets.size(); }
    const AmbientSoundSet* set(std::string_view name) const;
    void seed(unsigned s) { m_rng.seed(s); }

    // Closest emitter point to `p` for a set (nearest point, or nearest
    // point on the polyline for audible area 2).
    static Vec3 nearestPoint(const AmbientSoundSet& set, const Vec3& p);

private:
    struct Sample {
        SoundSlot slot;
        float timer = -1; // seconds to the next random one-shot
    };
    struct Set {
        AmbientSoundSet def;
        std::vector<Sample> samples;
        bool background = false; // from the city container (updated automatically)
    };
    struct MovingLoop {
        std::string set;
        int sample = 0;
        int id = 0;
        SoundSlot slot;
    };
    Set* find(std::string_view name);
    float nextInterval(const AmbientSampleDef& d);

    std::vector<Set> m_sets;
    std::vector<MovingLoop> m_moving;
    Mixer* m_mixer = nullptr;
    SoundBank* m_bank = nullptr;
    std::mt19937 m_rng{1234};
};

// mmRainAudio: rain loop (exterior, or interior when the camera is inside the
// car), quieter under shelter (tunnels, MM1's echo zones), and thunder with
// a lightning flash in storms. Volumes and timings ported from MM1.
class RainAudio {
public:
    void load(SoundBank& bank, Mixer& mixer);
    void update(bool raining, bool interior, bool sheltered, bool storm, float dt);
    void stop();
    // True on the update in which the sky should flash (MM1 sets mmSky::DoFlash).
    bool lightningFlash() const { return m_flash; }

private:
    SoundSlot m_exterior, m_interior, m_thunder, m_thunder2;
    float m_timer = 0;
    int m_flashState = 0;
    bool m_thundered = false;
    bool m_flash = false;
};

} // namespace mm2::audio::game
