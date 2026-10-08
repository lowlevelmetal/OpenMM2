#pragma once

// MM2's software 3D sound model. MM2 does not use DirectSound3D for its game
// sounds: every positioned object (cars, ambient traffic, city emitters,
// creatures) plays ordinary 2D buffers whose volume, pan and frequency it
// computes itself (Aud3DObject), and only a few objects at a time get sound
// at all (Aud3DObjectManager). Ported from MM2 (Aud3DObject::SetDropOffs,
// CalcDistToClosestHeads2, CalcPercentToMaxDist2, PastMaxDistance,
// WithinMaxDistance, CalculateAttenuation, CalcSinglePlayerPan,
// CalculateDoppler; Aud3DObjectManager::Add, FindGreatestDistance, Remove).
// Only the single-listener path is ported: split-screen heads do not exist in
// OpenMM2.

#include "core/Math.h"

#include <vector>

namespace mm2::audio::game {

// MM2 divides the approach speed by this ("speed of sound") to get the
// doppler shift; 56.7166633 m/s, a sixth of 340.3 m/s. Cars, police, city
// emitters and creatures use 1 / kDopplerSpeed, ambient traffic twice that.
inline constexpr float kDopplerSpeed = 56.7166633f;

// Distance state of one positioned sound object.
class Audio3D {
public:
    // SetDropOffs: full volume inside minDistance, silent from maxDistance.
    // Until it is called the maximum is -1 (the Aud3DObject constructor), so
    // the object is never within range.
    void setDropOffs(float minDistance, float maxDistance);
    float maxDistance2() const { return m_max2; }

    // CalcDistToClosestHeads2: squared distance and |dx|+|dy|+|dz| ("pseudo
    // distance") to the listener; the change of the latter since the previous
    // call drives the doppler shift. WithinMaxDistance and PastMaxDistance
    // each make this call, so on the update in which an object takes a slot
    // (both run) the second one sees no change and the doppler shift is 0.
    void updateDistance(const Vec3& position, const Vec3& listener);
    // Aud3DObject::Reset: forget the previous pseudo distance.
    void resetDistance() { m_prevManhattan = -1.0f; }
    float distance2() const { return m_dist2; }

    // WithinMaxDistance: d^2 < max^2 (used to start sounding).
    bool withinMaxDistance(const Vec3& position, const Vec3& listener);
    // PastMaxDistance: d^2 >= max^2 unless alwaysAudible (a police car with
    // its siren on keeps its slot, and its last attenuation, at any distance).
    bool pastMaxDistance(const Vec3& position, const Vec3& listener);
    // CalculateAttenuation: 1 - (d^2 - min^2) / (max^2 - min^2), a volume
    // multiplier in Angel units (so linear in decibels against d^2).
    float attenuation() const { return 1.0f - m_percent; }
    // CalcSinglePlayerPan: 0.2 * (listener-space x) / pseudo distance; 0
    // inside the minimum distance.
    float pan(const Mat34& listener, const Vec3& position) const;
    // CalculateDoppler: 1 + (pseudo distance closed since the last update)
    // * factor * dt. MM2 multiplies by the frame time where a rate would need a
    // division, so the shift is tiny; ported as is.
    float doppler(float factor, float dt) const { return m_approach * factor * dt + 1.0f; }

    bool alwaysAudible = false; // Aud3DObject +0x49, set while a siren is on

private:
    float percentToMax(float d2) const; // CalcPercentToMaxDist2

    float m_min2 = 0.0f, m_max2 = -1.0f, m_invRange = 0.0f;
    float m_dist2 = 1.0e6f; // Aud3DObject +0x2c starts at 1000000
    float m_manhattan = 0.0f, m_prevManhattan = -1.0f, m_approach = 0.0f;
    float m_percent = -1.0f; // +0x38 starts at -1
};

// Aud3DObjectManager: a fixed number of slots for positioned objects; an
// object without a slot is silent. MM2 single player creates it with four
// slots (mmPlayer::Init) and the player's own car, which is not positioned,
// holds one of them for good (its priority gets +1000000), so three remain for
// opponents, police, traffic, city emitters and creatures.
class Object3DManager {
public:
    static constexpr int kSinglePlayerSlots = 3;

    class Client {
    public:
        virtual ~Client() = default;
        virtual float slotDistance2() const = 0; // GetDistToClosestHead2
        virtual int slotPriority() const = 0;    // Aud3DObject +0x40
        virtual void slotLost() = 0;             // RemoveFrom3DMgr -> UnAssignSounds
    };

    explicit Object3DManager(int slots = kSinglePlayerSlots);

    // Add: a free slot, else the farthest object of no higher priority loses
    // its slot to a higher-priority object, or to an equal-priority one that is
    // closer (FindGreatestDistance). Returns false if no slot was given.
    bool add(Client* client);
    void remove(Client* client);
    bool holds(const Client* client) const;
    int used() const;
    int capacity() const { return static_cast<int>(m_slots.size()); }

private:
    std::vector<Client*> m_slots;
};

// A client's slot bookkeeping: with no manager an object has a slot while it
// is within its maximum distance. The manager must outlive its clients.
class SlotHolder : public Object3DManager::Client {
public:
    SlotHolder() = default;
    SlotHolder(const SlotHolder&) = delete;
    SlotHolder& operator=(const SlotHolder&) = delete;
    ~SlotHolder() override { releaseSlot(); }

    void setManager(Object3DManager* manager) {
        releaseSlot();
        m_manager = manager;
    }
    Object3DManager* manager() const { return m_manager; }
    // Aud3DObject::Update: asks for a slot while within maxDistance.
    bool acquireSlot(bool withinMaxDistance);
    void releaseSlot();
    bool hasSlot() const { return m_manager ? m_manager->holds(this) : m_free; }

private:
    Object3DManager* m_manager = nullptr;
    bool m_free = false;
};

} // namespace mm2::audio::game
