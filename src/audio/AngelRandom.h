#pragma once

// The random numbers MM2's audio code draws (AudManagerBase::RandomizeNumber
// and mmGameMusicData::RandomizeNumber). Neither keeps a generator: every call
// seeds a fresh Angel Random (Knuth's subtractive generator, Random::Seed /
// Random::Number) with time(NULL) and takes its first number. Every draw made
// within the same wall-clock second therefore returns the same value, so for
// example a one-shot's random volume and random pan move together, and a horn
// pattern chosen by two cars in the same second is the same pattern.

#include <array>
#include <cstdint>
#include <functional>

namespace mm2::audio {

// Random::Seed / Random::Number: Knuth's subtractive generator (55 terms,
// modulus 10^9), returning values in [0, 1) scaled by 1e-9.
class AngelRandom {
public:
    explicit AngelRandom(std::int32_t seed = 0) { this->seed(seed); }
    void seed(std::int32_t seed);
    // The x87 product of the integer state and 1e-9f, kept in double.
    double number();

private:
    std::array<std::int32_t, 56> m_ma{}; // index 0 unused, as in the original
    int m_next = 0;
    int m_nextp = 31;
};

// The seed every draw uses: time(NULL) in MM2. Tests and tools may replace it
// to make the draws reproducible; an empty function restores the clock.
void setRandomizeSeedSource(std::function<std::int32_t()> source);
std::int32_t randomizeSeed();

// AudManagerBase::RandomizeNumber(range): range * 0.01 * number * 100, i.e. a
// value in [0, range). Returned unrounded (MM2 hands back the x87 value);
// callers truncate it like __ftol or store it as a float.
double randomizeNumber(float range);
// AudManagerBase::RandomizeNumber(low, high): a value in [low, high).
double randomizeNumber(float low, float high);

} // namespace mm2::audio
