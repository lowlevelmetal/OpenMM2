// MM2's software 3D sound model (Aud3DObject, Aud3DObjectManager); see
// Object3D.h.
#include "audio/game/Object3D.h"

#include <algorithm>
#include <cmath>

namespace mm2::audio::game {

// --- Audio3D ----------------------------------------------------------------------

void Audio3D::setDropOffs(float minDistance, float maxDistance) {
    m_min2 = minDistance * minDistance;
    m_max2 = maxDistance * maxDistance;
    // MM2 divides unconditionally; equal distances (no retail file has them)
    // would make the range infinite there and silent here.
    const float range = m_max2 - m_min2;
    m_invRange = range != 0.0f ? 1.0f / range : 0.0f;
}

void Audio3D::updateDistance(const Vec3& position, const Vec3& listener) {
    const float dx = position.x - listener.x;
    const float dy = position.y - listener.y;
    const float dz = position.z - listener.z;
    m_dist2 = dx * dx + dy * dy + dz * dz;
    const float manhattan = std::abs(dx) + std::abs(dy) + std::abs(dz);
    // The first call after a reset has no previous distance: no doppler shift.
    m_prevManhattan = m_prevManhattan == -1.0f ? manhattan : m_manhattan;
    m_approach = m_prevManhattan - manhattan;
    m_manhattan = manhattan;
}

float Audio3D::percentToMax(float d2) const {
    if (alwaysAudible && m_max2 < d2)
        return 1.0f;
    if (d2 <= m_min2)
        return 0.0f;
    return (d2 - m_min2) * m_invRange;
}

bool Audio3D::withinMaxDistance(const Vec3& position, const Vec3& listener) {
    updateDistance(position, listener);
    if (m_dist2 < m_max2) {
        m_percent = percentToMax(m_dist2);
        return true;
    }
    return false;
}

bool Audio3D::pastMaxDistance(const Vec3& position, const Vec3& listener) {
    updateDistance(position, listener);
    if (m_max2 <= m_dist2)
        return !alwaysAudible; // the attenuation keeps its last value
    m_percent = percentToMax(m_dist2);
    return false;
}

float Audio3D::pan(const Mat34& listener, const Vec3& position) const {
    if (m_dist2 < m_min2 || m_manhattan <= 0.0f)
        return 0.0f;
    const float x = (position - listener.m3).dot(listener.m0);
    return x / m_manhattan * 0.2f;
}

// --- Object3DManager ---------------------------------------------------------------

Object3DManager::Object3DManager(int slots) : m_slots(static_cast<std::size_t>(std::max(slots, 1)), nullptr) {}

bool Object3DManager::holds(const Client* client) const {
    return std::find(m_slots.begin(), m_slots.end(), client) != m_slots.end();
}

int Object3DManager::slotOf(const Client* client) const {
    const auto it = std::find(m_slots.begin(), m_slots.end(), client);
    return client && it != m_slots.end() ? static_cast<int>(it - m_slots.begin()) : -1;
}

int Object3DManager::used() const {
    return static_cast<int>(std::count_if(m_slots.begin(), m_slots.end(), [](const Client* c) { return c != nullptr; }));
}

bool Object3DManager::add(Client* client) {
    if (!client)
        return false;
    if (holds(client))
        return true;
    for (auto& slot : m_slots) {
        if (!slot) {
            slot = client;
            return true;
        }
    }
    // FindGreatestDistance: walk the slots for the farthest object whose
    // priority is not above the current candidate's.
    std::size_t candidate = 0;
    for (std::size_t i = 1; i < m_slots.size(); ++i) {
        if (m_slots[candidate]->slotDistance2() < m_slots[i]->slotDistance2() &&
            m_slots[i]->slotPriority() <= m_slots[candidate]->slotPriority())
            candidate = i;
    }
    Client* loser = m_slots[candidate];
    const int difference = client->slotPriority() - loser->slotPriority();
    if (difference < 1 && !(difference == 0 && client->slotDistance2() < loser->slotDistance2()))
        return false;
    m_slots[candidate] = client;
    loser->slotLost();
    return true;
}

void Object3DManager::remove(Client* client) {
    for (auto& slot : m_slots)
        if (slot == client)
            slot = nullptr;
}

// --- SlotHolder --------------------------------------------------------------------

bool SlotHolder::acquireSlot(bool withinMaxDistance) {
    if (hasSlot())
        return true;
    if (!withinMaxDistance)
        return false;
    if (!m_manager) {
        m_free = true;
        return true;
    }
    return m_manager->add(this);
}

void SlotHolder::releaseSlot() {
    if (m_manager)
        m_manager->remove(this);
    m_free = false;
}

} // namespace mm2::audio::game
