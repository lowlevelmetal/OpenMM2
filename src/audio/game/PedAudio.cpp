// Pedestrian voices, ported from MM2 (aiPedAudio, AudCreatureContainer and
// its statics, aiPedestrian::Init / Update's audio calls). See PedAudio.h.
#include "audio/game/PedAudio.h"

#include "audio/AngelRandom.h"
#include "core/Log.h"
#include "core/StringUtil.h"

#include <format>

namespace mm2::audio::game {
namespace {

// aiPedestrian::Init: SetDropOffs(0, 40), priority 8.
constexpr float kPedMaxDistance = 40.0f;
constexpr int kPedPriority = 8;

// aiPedestrian_AreStringsEqual: `needle` somewhere in `haystack`, ignoring
// the case of a..z; after a mismatch the search goes on from the next letter
// of the haystack with the needle's first letter.
bool containsNoCase(std::string_view haystack, std::string_view needle) {
    auto upper = [](char c) { return 'a' <= c && c <= 'z' ? static_cast<char>(c - 0x20) : c; };
    std::size_t j = 0;
    for (const char c : haystack) {
        if (upper(c) == upper(needle[j])) {
            if (++j >= needle.size())
                return true;
        } else {
            j = 0;
        }
    }
    return false;
}

} // namespace

// aiPedAudio / AudCreatureContainer: one pedestrian's container.
class PedestrianAudio::Container : public SlotHolder, public CreatureVoice::Owner {
public:
    explicit Container(PedestrianAudio& audio) : m_audio(audio) {}
    ~Container() override {
        if (m_creature)
            m_creature->setOwner(nullptr);
    }

    // aiPedestrian::Init's audio part.
    void init(std::string_view type) {
        if (hasSlot())
            lose();
        m_type = std::string(type);
        setManager(m_audio.m_manager);
        m_3d = Audio3D();
        m_3d.setDropOffs(0.0f, kPedMaxDistance);
        // LoadFemaleVoices / LoadMaleVoices("default", true): "%s_fpedvoice%s%d"
        // with the empty CSV string and the session's file number.
        const bool female = isWoman(type);
        m_set = m_audio.voiceSet(female ? std::format("default_fpedvoice{}", m_audio.m_femaleFile)
                                        : std::format("default_mpedvoice{}", m_audio.m_maleFile));
    }
    const std::string& type() const { return m_type; }
    void setPosition(const Vec3& p) { m_position = p; }
    // AudCreatureContainer::Reset: Aud3DObject::Reset gives up the slot
    // (RemoveFrom3DMgr).
    void reset() {
        if (hasSlot())
            lose();
    }

    // AudCreatureContainer::Update: the slot goes once the voice is quiet.
    void releaseWhenQuiet() {
        if (m_set != -1 && hasSlot() && !(m_creature && m_creature->speaking()))
            lose();
    }
    // PlayAvoidanceReaction / PlayImpactReaction: Aud3DObject::Update (a slot
    // within range), then the voice, if the slot was given.
    void avoid() {
        if (m_set == -1)
            return;
        requestSlot();
        if (hasSlot() && m_creature)
            m_creature->avoid();
    }
    void impact(float force) {
        if (m_set == -1)
            return;
        requestSlot();
        if (hasSlot() && m_creature)
            m_creature->impact(force);
    }
    // UpdateAudio(): the echo, then UpdateAudio(doppler factor): past 40 m the
    // slot goes, else the voice gets the attenuation, pan and squared
    // distance (the doppler shift is worked out and not used).
    void updateAudio(const Mat34& listener, float dt, bool tunnel) {
        if (!m_echo) {
            if (tunnel)
                echoOn(tunnelEchoDelay(manager()));
        } else if (!tunnel) {
            echoOff();
        }
        if (m_echo && m_creature)
            m_creature->updateEcho(dt);
        if (m_3d.pastMaxDistance(m_position, listener.m3)) {
            lose();
            return;
        }
        const float attenuation = m_3d.attenuation();
        const float pan = m_3d.pan(listener, m_position);
        if (m_creature)
            m_creature->updateAttenuation(attenuation, pan, m_3d.distance2());
    }
    void stop() {
        if (hasSlot())
            lose();
    }
    bool holdsSlot() const { return hasSlot(); }
    bool speaking() const { return m_creature && m_creature->speaking(); }

    // Aud3DObject::UpdateNonVirtual (also the voice's check before a line).
    bool requestSlot() override {
        if (!hasSlot() && acquireSlot(m_3d.withinMaxDistance(m_position, m_audio.m_listener.m3)))
            assign();
        return hasSlot();
    }

private:
    float slotDistance2() const override { return m_3d.distance2(); }
    int slotPriority() const override { return kPedPriority; }
    void slotLost() override { unassign(); }

    void assign() {
        // AssignSounds: the voice of this file for the slot.
        if (m_set == -1)
            return;
        const int slot = manager() ? manager()->slotOf(this) : 0;
        m_creature = m_audio.creature(m_set, slot);
        if (m_creature)
            m_creature->setOwner(this);
    }
    void unassign() {
        // UnAssignSounds.
        if (m_echo)
            echoOff();
        if (m_creature) {
            m_creature->unassign();
            m_creature->setOwner(nullptr);
        }
        m_creature = nullptr;
    }
    void lose() {
        // RemoveFrom3DMgr.
        releaseSlot();
        unassign();
    }
    void echoOn(float delay) {
        if (m_creature)
            m_creature->echoOn(delay);
        m_echo = true;
    }
    void echoOff() {
        if (m_creature)
            m_creature->echoOff();
        m_echo = false;
    }

    PedestrianAudio& m_audio;
    std::string m_type;
    int m_set = -1;                      // +0x64: the voice file, -1 none
    CreatureVoice* m_creature = nullptr; // +0x60
    bool m_echo = false;                 // +0x71
    Audio3D m_3d;
    Vec3 m_position;
};

PedestrianAudio::PedestrianAudio() = default;
PedestrianAudio::~PedestrianAudio() = default;

bool PedestrianAudio::isWoman(std::string_view type) {
    return containsNoCase(type, "FEMALE") || containsNoCase(type, "WOMAN") || containsNoCase(type, "GIRL") ||
           containsNoCase(type, "Hooker");
}

int PedestrianAudio::numFileChoice(std::string_view name) const {
    // AudCreatureContainer::LoadNumFileChoices: the global keeps its 0 when
    // the file is missing.
    auto text = readText(*m_vfs, std::format("aud/creaturedata/{}.csv", name));
    if (!text)
        return 0;
    const auto n = parseNumFileChoices(*text);
    if (!n)
        return 0;
    return static_cast<int>(randomizeNumber(1.0f, *n + 0.25f));
}

void PedestrianAudio::load(const vfs::Vfs& vfs, SoundBank& bank, Mixer& mixer, Object3DManager* manager) {
    stop();
    m_peds.clear();
    m_sets.clear();
    m_hash.clear();
    m_vfs = &vfs;
    m_bank = &bank;
    m_mixer = &mixer;
    m_manager = manager;
    m_femaleFile = numFileChoice("numfemalepedvoicefiles");
    m_maleFile = numFileChoice("nummalepedvoicefiles");
}

int PedestrianAudio::voiceSet(std::string_view name) {
    // AudCreatureContainer::LoadVoices / AudCreature::Load: a file is loaded
    // once (a missing one is looked for again each time).
    if (!m_vfs)
        return -1;
    const std::string key(name);
    if (auto it = m_hash.find(key); it != m_hash.end() && it->second != -1)
        return it->second;
    auto text = readText(*m_vfs, std::format("aud/creaturedata/{}.csv", str::lower(name)));
    if (!text) {
        m_hash[key] = -1;
        return -1;
    }
    CreatureVoiceDef def = parseCreatureVoice(*text).value_or(CreatureVoiceDef{});
    VoiceSet set;
    set.name = key;
    // One AudCreature per 3D-manager slot.
    set.perSlot.resize(static_cast<std::size_t>(m_manager ? m_manager->capacity() : 1));
    for (auto& c : set.perSlot)
        c.load(*m_mixer, *m_bank, def);
    m_sets.push_back(std::move(set));
    const int index = static_cast<int>(m_sets.size()) - 1;
    m_hash[key] = index;
    return index;
}

CreatureVoice* PedestrianAudio::creature(int set, int slot) {
    if (set < 0 || static_cast<std::size_t>(set) >= m_sets.size())
        return nullptr;
    auto& perSlot = m_sets[static_cast<std::size_t>(set)].perSlot;
    if (slot < 0 || static_cast<std::size_t>(slot) >= perSlot.size())
        return nullptr;
    return &perSlot[static_cast<std::size_t>(slot)];
}

void PedestrianAudio::update(std::span<const PedestrianSoundInput> peds, const Mat34& listener,
                             float playerSpeed, float dt, bool inTunnel) {
    m_listener = listener;
    // AudCreatureContainer::UpdateStatics(player speed) at the start of
    // aiMap::Update. aiMap reads the speed through the player vehicle's
    // virtual at +8 (inferred to be its speed).
    CreatureVoice::advanceClock(dt);
    for (auto& set : m_sets)
        for (auto& c : set.perSlot)
            c.update(playerSpeed, dt);
    // aiPedestrian::Update for each pedestrian, with its reactions.
    for (const auto& p : peds) {
        auto& c = m_peds[p.id];
        if (!c) {
            c = std::make_unique<Container>(*this);
            c->init(p.type);
        } else if (c->type() != p.type) {
            c->init(p.type); // OpenMM2: a pool entry reused with another model
        }
        if (p.reset)
            c->reset();
        c->setPosition(p.position);
        c->releaseWhenQuiet();
        if (p.avoiding)
            c->avoid();
    }
    // Aud3DObjectManager::Update: UpdateAudio for the slot holders.
    const bool tunnel = inTunnel || (m_manager && m_manager->echo());
    for (auto& [id, c] : m_peds)
        if (c->holdsSlot())
            c->updateAudio(listener, dt, tunnel);
}

void PedestrianAudio::impact(int id, float force) {
    if (auto it = m_peds.find(id); it != m_peds.end())
        it->second->impact(force);
}

void PedestrianAudio::stop() {
    for (auto& [id, c] : m_peds)
        c->stop();
    for (auto& set : m_sets)
        for (auto& c : set.perSlot)
            c.stop();
}

bool PedestrianAudio::audible(int id) const {
    auto it = m_peds.find(id);
    return it != m_peds.end() && it->second->holdsSlot();
}

bool PedestrianAudio::speaking(int id) const {
    auto it = m_peds.find(id);
    return it != m_peds.end() && it->second->speaking();
}

} // namespace mm2::audio::game
