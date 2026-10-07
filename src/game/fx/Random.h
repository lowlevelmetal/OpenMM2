/*
    OpenMM2 - port of the Angel engine's random number generator.
    Derived from Open1560 (code/midtown/game.asm: irand, frand),
    Copyright (C) 2020 Brick, GPL-3.0-or-later.
*/
#pragma once

#include <cstdint>

namespace mm2::game::fx {

// irand()/frand() of the Angel engine: the MSVC linear congruential
// generator (seed * 214013 + 2531011), returning 15 bits. The original used
// one global seed (gRandSeed) for everything; OpenMM2 gives each subsystem
// its own instance so simulations stay deterministic independently.
class Rand {
public:
    explicit Rand(std::uint32_t seed = 0x2A) : m_seed(seed) {}

    void seed(std::uint32_t s) { m_seed = s; }
    std::uint32_t state() const { return m_seed; }

    // 0 .. 32767
    int irand() {
        m_seed = m_seed * 214013u + 2531011u;
        return static_cast<int>((m_seed >> 16) & 0x7FFF);
    }
    // [0, 1): irand() * 2^-15 (flt_621B38 = 3.0517578e-05f)
    float frand() { return static_cast<float>(irand()) * 3.0517578125e-05f; }

private:
    std::uint32_t m_seed;
};

} // namespace mm2::game::fx
