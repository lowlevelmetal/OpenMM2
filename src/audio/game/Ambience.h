#pragma once

// City ambience emitters (aud/ambient/*.csv: river traffic, gulls, tube
// voices, cable cars, buoys...) and rain. Ported from MM2's
// Aud3DAmbObjContainer, Aud3DAmbientObject and mmRainAudio.

#include "audio/game/AudioTables.h"
#include "audio/game/Object3D.h"
#include "audio/game/SoundSlot.h"

#include <memory>
#include <string>
#include <vector>

namespace mm2::audio::game {

class CityAmbience {
public:
    // Aud3DAmbObjContainer::Init: the sets listed in
    // aud/ambient/<city>ambientcontainer.csv. Each set is one positioned
    // object competing for the manager's slots with its file's priority.
    bool load(const vfs::Vfs& vfs, SoundBank& bank, Mixer& mixer, std::string_view city,
              Object3DManager* manager = nullptr);
    // Loads one more set (e.g. "subwaycar", "ferry", "drawbridge",
    // "femaleped1") for sounds the game triggers itself (playAt / setLoop).
    // Returns its index, or -1.
    int loadSet(const vfs::Vfs& vfs, SoundBank& bank, Mixer& mixer, std::string_view name);

    // Aud3DAmbObjContainer::Update. `inTunnel` is the listener's tunnel echo
    // state, which gates sets by their audible area.
    void update(const Mat34& listener, float dt, bool inTunnel = false);
    void update(const Vec3& listener, float dt, bool inTunnel = false);
    // Aud3DAmbientObject::PlayOneShot(int) for a set placed at `position`
    // (the bridge, subway and pedestrian sounds; positional types use the
    // set's distance attenuation and pan from the last listener).
    void playAt(std::string_view name, int sample, const Vec3& position, const Vec3& velocity = {});
    // A looping sample attached to a moving object (subway car, ferry
    // engine); `id` distinguishes several objects using the same set.
    void setLoop(std::string_view name, int sample, int id, bool on, const Vec3& position, const Vec3& velocity = {});
    void stop();

    std::size_t setCount() const { return m_sets.size(); }
    const AmbientSoundSet* set(std::string_view name) const;
    // Whether a background set currently holds a sound slot.
    bool audible(std::string_view name) const;

    // Aud3DObject::GetClosestPositionPtr: the VECTORPOINTS entry with the
    // smallest |dx| + |dy| + |dz| to `p` (the first on ties); `p` itself when
    // the set has no points.
    static Vec3 nearestPoint(const AmbientSoundSet& set, const Vec3& p);

private:
    struct Sample {
        SoundSlot slot;
        float timer = 0; // seconds to the next one-shot; 0 plays at once
    };
    struct Set : SlotHolder {
        AmbientSoundSet def;
        std::vector<Sample> samples;
        bool background = false; // from the city container (updated automatically)
        bool positional = false; // a Loop, Interval or Triggered sample: attenuation applies
        Audio3D audio;
        Vec3 position;
        float attenuation = 0.0f, pan = 0.0f, doppler = 1.0f;
        Mixer* mixer = nullptr;
        float slotDistance2() const override { return audio.distance2(); }
        int slotPriority() const override { return def.priority; }
        void slotLost() override;
    };
    struct MovingLoop {
        std::string set;
        int sample = 0;
        int id = 0;
        SoundSlot slot;
    };
    Set* find(std::string_view name);
    float interval(const AmbientSampleDef& d);
    void playOneShot(Set& s, std::size_t index);
    void updateSet(Set& s, float dt, bool inTunnel);

    std::vector<std::unique_ptr<Set>> m_sets;
    std::vector<MovingLoop> m_moving;
    Mixer* m_mixer = nullptr;
    SoundBank* m_bank = nullptr;
    Object3DManager* m_manager = nullptr;
    Mat34 m_listener = Mat34::identity();
};

// mmRainAudio: the rain loop (exterior, or interior while the camera is in
// the car), quieter under shelter (in tunnels), and at night thunder with a
// lightning flash.
class RainAudio {
public:
    // mmRainAudio::mmRainAudio: thunder only exists at night.
    void load(SoundBank& bank, Mixer& mixer, bool night = false);
    // mmRainAudio::Update / SetInterior; `sheltered` is the tunnel echo state.
    void update(bool raining, bool interior, bool sheltered, float dt);
    void stop();
    // mmRainAudio +2: set for one update 13 s into each cycle. MM2 tracks it
    // but nothing reads it: no lightning flash is drawn.
    bool lightningFlash() const { return m_flash; }

private:
    void shelter(bool on);

    SoundSlot m_exterior, m_interior, m_thunder, m_thunder2;
    bool m_night = false;
    bool m_interiorOn = false;
    bool m_sheltered = false;
    float m_timer = 0;
    int m_flashState = 0;
    bool m_thunderState = false; // the first clap has played, the second is due
    bool m_flash = false;
};

} // namespace mm2::audio::game
