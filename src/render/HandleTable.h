#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace mm2::render::detail {

// Slot table mapping 1-based handle ids to backend objects. Freed slots are
// reused, so a stale handle may alias a newer object; callers own lifetime.
template <class T>
class HandleTable {
public:
    std::uint32_t insert(T value) {
        if (!m_free.empty()) {
            const std::uint32_t index = m_free.back();
            m_free.pop_back();
            m_slots[index].emplace(std::move(value));
            return index + 1;
        }
        m_slots.emplace_back(std::move(value));
        return static_cast<std::uint32_t>(m_slots.size());
    }

    T* get(std::uint32_t id) {
        if (id == 0 || id > m_slots.size() || !m_slots[id - 1])
            return nullptr;
        return &*m_slots[id - 1];
    }
    const T* get(std::uint32_t id) const { return const_cast<HandleTable*>(this)->get(id); }

    std::optional<T> remove(std::uint32_t id) {
        if (id == 0 || id > m_slots.size() || !m_slots[id - 1])
            return std::nullopt;
        std::optional<T> out = std::move(m_slots[id - 1]);
        m_slots[id - 1].reset();
        m_free.push_back(id - 1);
        return out;
    }

    template <class F>
    void forEach(F&& fn) {
        for (std::uint32_t i = 0; i < m_slots.size(); ++i)
            if (m_slots[i])
                fn(i + 1, *m_slots[i]);
    }

    void clear() {
        m_slots.clear();
        m_free.clear();
    }

private:
    std::vector<std::optional<T>> m_slots;
    std::vector<std::uint32_t> m_free;
};

} // namespace mm2::render::detail
