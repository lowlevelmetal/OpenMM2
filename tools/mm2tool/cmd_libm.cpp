// libm: OpenMM2's own elementary functions (core/Libm.h) against this
// platform's C runtime, over the same arguments everywhere.
//
// For each function it prints a hash of OpenMM2's float results, which must
// be the same on every platform (docs/review/multiplayer-determinism.md
// lists the Linux values), and how many of the runtime's results differ from
// them and from the long double functions rounded to float (the runtime's
// own reference; on MSVC long double is double).

#include "Command.h"
#include "core/Libm.h"
#include "core/StringUtil.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <print>

namespace mm2::tool {
namespace {

struct Counts {
    std::uint64_t mine = 1469598103934665603ull; // FNV-1a of OpenMM2's results
    std::uint64_t runtime = 1469598103934665603ull;
    int differ = 0;         // OpenMM2 against the runtime
    int mineVsRef = 0;      // against the long double result rounded to float
    int runtimeVsRef = 0;
};

void hash(std::uint64_t& h, float v) {
    const auto u = std::bit_cast<std::uint32_t>(v);
    for (int k = 0; k < 4; ++k)
        h = (h ^ ((u >> (8 * k)) & 0xffu)) * 1099511628211ull;
}

template <class Mine, class Runtime, class Ref>
void sweep(const char* name, float lo, float hi, int count, Mine mine, Runtime runtime, Ref ref) {
    Counts c;
    std::uint32_t seed = 12345u;
    for (int i = 0; i < count; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const float x = lo + (hi - lo) * (static_cast<float>(seed >> 8) * 0x1p-24f);
        const float m = mine(x), r = runtime(x), e = static_cast<float>(ref(static_cast<long double>(x)));
        hash(c.mine, m);
        hash(c.runtime, r);
        c.differ += std::bit_cast<std::uint32_t>(m) != std::bit_cast<std::uint32_t>(r);
        c.mineVsRef += std::bit_cast<std::uint32_t>(m) != std::bit_cast<std::uint32_t>(e);
        c.runtimeVsRef += std::bit_cast<std::uint32_t>(r) != std::bit_cast<std::uint32_t>(e);
    }
    std::println("{:<6} {:>8g} .. {:<8g} libm {:016x}  runtime {:016x}  differ {:>7}  libm/ref {:>5}  "
                 "runtime/ref {:>7}",
                 name, lo, hi, c.mine, c.runtime, c.differ, c.mineVsRef, c.runtimeVsRef);
}

int cmdLibm(std::span<char* const> args) {
    int count = 1000000;
    if (!args.empty()) {
        const auto n = str::parseInt(args[0]);
        if (!n || *n <= 0) {
            std::println(stderr, "libm: bad count '{}'", args[0]);
            return 2;
        }
        count = static_cast<int>(*n);
    }
    std::println("{} arguments per function; the libm hashes must match on every platform", count);
    sweep("sin", -100.0f, 100.0f, count, [](float x) { return libm::sin(x); },
          [](float x) { return std::sin(x); }, [](long double x) { return std::sin(x); });
    sweep("cos", -100.0f, 100.0f, count, [](float x) { return libm::cos(x); },
          [](float x) { return std::cos(x); }, [](long double x) { return std::cos(x); });
    sweep("tan", -1.5f, 1.5f, count, [](float x) { return libm::tan(x); },
          [](float x) { return std::tan(x); }, [](long double x) { return std::tan(x); });
    sweep("asin", -1.0f, 1.0f, count, [](float x) { return libm::asin(x); },
          [](float x) { return std::asin(x); }, [](long double x) { return std::asin(x); });
    sweep("acos", -1.0f, 1.0f, count, [](float x) { return libm::acos(x); },
          [](float x) { return std::acos(x); }, [](long double x) { return std::acos(x); });
    sweep("atan", -50.0f, 50.0f, count, [](float x) { return libm::atan(x); },
          [](float x) { return std::atan(x); }, [](long double x) { return std::atan(x); });
    sweep("atan2", -40.0f, 40.0f, count, [](float x) { return libm::atan2(x, 7.0f - x * 0.5f); },
          [](float x) { return std::atan2(x, 7.0f - x * 0.5f); },
          [](long double x) {
              const float other = 7.0f - static_cast<float>(x) * 0.5f;
              return std::atan2(x, static_cast<long double>(other));
          });
    sweep("exp", -30.0f, 30.0f, count, [](float x) { return libm::exp(x); },
          [](float x) { return std::exp(x); }, [](long double x) { return std::exp(x); });
    sweep("exp2", -30.0f, 30.0f, count, [](float x) { return libm::exp2(x); },
          [](float x) { return std::exp2(x); }, [](long double x) { return std::exp2(x); });
    sweep("log", 0.001f, 1000.0f, count, [](float x) { return libm::log(x); },
          [](float x) { return std::log(x); }, [](long double x) { return std::log(x); });
    sweep("log2", 0.001f, 1000.0f, count, [](float x) { return libm::log2(x); },
          [](float x) { return std::log2(x); }, [](long double x) { return std::log2(x); });
    sweep("pow", 0.0f, 50.0f, count, [](float x) { return libm::pow(x, 1.7f); },
          [](float x) { return std::pow(x, 1.7f); },
          [](long double x) { return std::pow(x, static_cast<long double>(1.7f)); });
    sweep("hypot", -50.0f, 50.0f, count, [](float x) { return libm::hypot(x, 3.0f - x); },
          [](float x) { return std::hypot(x, 3.0f - x); },
          [](long double x) {
              const auto other = static_cast<long double>(3.0f - static_cast<float>(x));
              return std::sqrt(x * x + other * other);
          });
    return 0;
}

const Registrar reg({"libm", "[count]", "compare OpenMM2's maths with this platform's C runtime", &cmdLibm});

} // namespace
} // namespace mm2::tool
