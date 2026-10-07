#pragma once

#include <cstdint>

namespace mm2::ai {

// Deterministic random numbers standing in for the Angel engine's global
// frand()/irand(). Each AI world owns one, so identical seeds and inputs
// replay identically.
class Random {
public:
    explicit Random(std::uint64_t seed = 0x6D6D32u) : m_state(seed ? seed : 1) {}

    std::uint32_t next() {
        // xorshift64*
        m_state ^= m_state >> 12;
        m_state ^= m_state << 25;
        m_state ^= m_state >> 27;
        return static_cast<std::uint32_t>((m_state * 0x2545F4914F6CDD1Dull) >> 32);
    }
    // [0, 1), like frand().
    float frand() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }
    // [0, n), like irand(n).
    int irand(int n) { return n > 0 ? static_cast<int>(next() % static_cast<std::uint32_t>(n)) : 0; }

private:
    std::uint64_t m_state;
};

} // namespace mm2::ai
