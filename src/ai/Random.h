#pragma once

#include <cstdint>

namespace mm2::ai {

// MM2's irand() and frand() (build 3393): the MSVC rand() linear
// congruential generator, seed = seed * 214013 + 2531011, returning bits 16..30
// (0 .. 32767); frand() is irand() * 2^-15, so [0, 32767/32768].
//
// The original has one global seed, gRandSeed, for everything. ResetRandomSeed
// sets it to 1 before each road's street props (cityLevel::Load) and in
// aiMap::Reset, and a race's set-up draws from it in a fixed order (the cars'
// sirens and splashes, the gizmos, the traffic, the pedestrians, the cable
// cars; docs/parity/round3/random-streams.md). OpenMM2 passes one Random
// through the systems that draw in that window, so their outcomes are MM2's;
// the systems that draw only during play keep their own instances.
class Random {
public:
    explicit Random(std::uint64_t seed = 1) : m_seed(static_cast<std::uint32_t>(seed)) {}

    void seed(std::uint32_t s) { m_seed = s; }
    std::uint32_t state() const { return m_seed; }

    // irand(): 0 .. 32767.
    int irand() {
        m_seed = m_seed * 214013u + 2531011u;
        return static_cast<int>((m_seed >> 16) & 0x7FFFu);
    }
    // frand(): irand() times 3.0517578e-05.
    float frand() { return static_cast<float>(irand()) * 3.0517578125e-05f; }
    // `n` draws whose values are not used.
    void discard(int n) {
        for (int i = 0; i < n; ++i)
            m_seed = m_seed * 214013u + 2531011u;
    }

private:
    std::uint32_t m_seed;
};

} // namespace mm2::ai
