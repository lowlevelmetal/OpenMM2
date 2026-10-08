// Port of the Angel engine's Random class (Random::Seed, Random::Number) and
// of MM2's audio RandomizeNumber helpers. See AngelRandom.h.
#include "audio/AngelRandom.h"

#include <ctime>
#include <mutex>

namespace mm2::audio {
namespace {

constexpr std::int32_t kBig = 1000000000;
constexpr std::int32_t kSeed = 161803398;
// The scale factors the original multiplies by (float constants).
constexpr float kPercent = 0.01f;
constexpr float kHundred = 100.0f;
constexpr float kScale = 1e-9f;

std::mutex g_seedMutex;
std::function<std::int32_t()> g_seedSource;

} // namespace

void AngelRandom::seed(std::int32_t seed) {
    if (seed < 0)
        seed = -seed;
    std::int32_t mj = (kSeed - seed) % kBig;
    m_ma[55] = mj;
    std::int32_t mk = 1;
    for (int i = 21; i < 21 * 54 + 1; i += 21) {
        const int ii = i % 55;
        m_ma[static_cast<std::size_t>(ii)] = mk;
        mk = mj - mk;
        if (mk < 0)
            mk += kBig;
        mj = m_ma[static_cast<std::size_t>(ii)];
    }
    for (int k = 0; k < 4; ++k) {
        for (int i = 1; i <= 55; ++i) {
            auto& v = m_ma[static_cast<std::size_t>(i)];
            v -= m_ma[static_cast<std::size_t>((i + 30) % 55 + 1)];
            if (v < 0)
                v += kBig;
        }
    }
    m_next = 0;
    m_nextp = 31;
}

double AngelRandom::number() {
    if (++m_next > 55)
        m_next = 1;
    if (++m_nextp > 55)
        m_nextp = 1;
    std::int32_t mj = m_ma[static_cast<std::size_t>(m_next)] - m_ma[static_cast<std::size_t>(m_nextp)];
    if (mj < 0)
        mj += kBig;
    m_ma[static_cast<std::size_t>(m_next)] = mj;
    return static_cast<double>(mj) * static_cast<double>(kScale);
}

void setRandomizeSeedSource(std::function<std::int32_t()> source) {
    std::lock_guard lock(g_seedMutex);
    g_seedSource = std::move(source);
}

std::int32_t randomizeSeed() {
    {
        std::lock_guard lock(g_seedMutex);
        if (g_seedSource)
            return g_seedSource();
    }
    return static_cast<std::int32_t>(std::time(nullptr));
}

double randomizeNumber(float range) {
    AngelRandom r(randomizeSeed());
    const double u = r.number();
    return static_cast<double>(range) * static_cast<double>(kPercent) * u * static_cast<double>(kHundred);
}

double randomizeNumber(float low, float high) {
    // The low end is stored back as a float before use.
    const float lo = low * kPercent;
    AngelRandom r(randomizeSeed());
    const double u = r.number();
    const double hi = static_cast<double>(high) * static_cast<double>(kPercent);
    return ((hi - static_cast<double>(lo)) * u + static_cast<double>(lo)) * static_cast<double>(kHundred);
}

} // namespace mm2::audio
