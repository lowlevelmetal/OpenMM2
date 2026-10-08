#pragma once

#include <cstdint>

namespace mm2::ai {

// MM2's irand() and frand() (build 3393): the MSVC rand() linear
// congruential generator, seed = seed * 214013 + 2531011, returning bits 16..30
// (0 .. 32767); frand() is irand() * 2^-15, so [0, 32767/32768]. The original
// has one global seed for everything (traffic, pedestrians, police, audio,
// effects), set to 1 by aiMap::Reset (ResetRandomSeed). OpenMM2 gives each AI
// subsystem its own generator so identical seeds and inputs replay
// identically; the draws and their scaling are MM2's.
class Random {
public:
    explicit Random(std::uint64_t seed = 1) : m_seed(static_cast<std::uint32_t>(seed)) {}

    void seed(std::uint32_t s) { m_seed = s; }

    // irand(): 0 .. 32767.
    int irand() {
        m_seed = m_seed * 214013u + 2531011u;
        return static_cast<int>((m_seed >> 16) & 0x7FFFu);
    }
    // frand(): irand() times 3.0517578e-05.
    float frand() { return static_cast<float>(irand()) * 3.0517578125e-05f; }

private:
    std::uint32_t m_seed;
};

} // namespace mm2::ai
