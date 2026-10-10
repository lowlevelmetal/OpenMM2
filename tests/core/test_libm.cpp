// OpenMM2's own elementary functions (core/Libm.h) give the same bits on
// every platform, and the float versions are the correctly rounded values.
// The expected values are the platform-independent ones; a build where they
// differ simulates differently from every other (docs/physics.md, "The same
// results on every platform"). CI runs these on GCC, Clang, MinGW (Wine)
// and MSVC.
#include "core/Libm.h"

#include <gtest/gtest.h>

#include <bit>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

using namespace mm2;

namespace {

struct FloatCase {
    const char* fn;
    float a;        // the argument (y for atan2, the base for pow)
    float b;        // the second argument (x for atan2, the exponent for pow)
    float expected; // the correctly rounded result
};

float evaluate(const FloatCase& c) {
    const std::string fn = c.fn;
    if (fn == "sin")
        return libm::sin(c.a);
    if (fn == "cos")
        return libm::cos(c.a);
    if (fn == "tan")
        return libm::tan(c.a);
    if (fn == "asin")
        return libm::asin(c.a);
    if (fn == "acos")
        return libm::acos(c.a);
    if (fn == "atan")
        return libm::atan(c.a);
    if (fn == "exp")
        return libm::exp(c.a);
    if (fn == "exp2")
        return libm::exp2(c.a);
    if (fn == "log")
        return libm::log(c.a);
    if (fn == "log2")
        return libm::log2(c.a);
    if (fn == "atan2")
        return libm::atan2(c.a, c.b);
    if (fn == "pow")
        return libm::pow(c.a, c.b);
    if (fn == "hypot")
        return libm::hypot(c.a, c.b);
    ADD_FAILURE() << "unknown function " << fn;
    return 0.0f;
}

// Each checked against glibc's long double functions rounded to float.
constexpr FloatCase kFloatCases[] = {
    {"sin", 0x1p-1f, 0, 0x1.eaee88p-2f},
    {"sin", -0x1p-1f, 0, -0x1.eaee88p-2f},
    {"sin", 0x1p+0f, 0, 0x1.aed548p-1f},
    {"sin", 0x1.921fb6p+0f, 0, 0x1p+0f},
    {"sin", 0x1.921fb6p+1f, 0, -0x1.777a5cp-24f},
    {"sin", 0x1.921fb6p+2f, 0, 0x1.777a5cp-23f},
    {"sin", 0x1.930bep-7f, 0, 0x1.930946p-7f},
    {"sin", 0x1.9p+6f, 0, -0x1.03425cp-1f},
    {"sin", -0x1.6p+1f, 0, -0x1.86d224p-2f},
    {"sin", 0x1.4484cp-100f, 0, 0x1.4484cp-100f},
    {"sin", 0x1.333334p+13f, 0, -0x1.65d4ap-2f},
    {"cos", 0x1p-1f, 0, 0x1.c1528p-1f},
    {"cos", -0x1p-1f, 0, 0x1.c1528p-1f},
    {"cos", 0x1p+0f, 0, 0x1.14a28p-1f},
    {"cos", 0x1.921fb6p+0f, 0, -0x1.777a5cp-25f},
    {"cos", 0x1.921fb6p+1f, 0, -0x1p+0f},
    {"cos", 0x1.921fb6p+2f, 0, 0x1p+0f},
    {"cos", 0x1.930bep-7f, 0, 0x1.fff616p-1f},
    {"cos", 0x1.9p+6f, 0, 0x1.b981dcp-1f},
    {"cos", -0x1.6p+1f, 0, -0x1.d93e2ap-1f},
    {"cos", 0x1.4484cp-100f, 0, 0x1p+0f},
    {"tan", 0x1p-1f, 0, 0x1.17b4f6p-1f},
    {"tan", -0x1p-1f, 0, -0x1.17b4f6p-1f},
    {"tan", 0x1.91eb86p-1f, 0, 0x1.ff97acp-1f},
    {"tan", 0x1.8p+0f, 0, 0x1.c33ed6p+3f},
    {"tan", 0x1.91eb86p+0f, 0, 0x1.39f64ap+10f},
    {"tan", 0x1.8p+1f, 0, -0x1.23ef72p-3f},
    {"asin", 0x1p-1f, 0, 0x1.0c1524p-1f},
    {"asin", -0x1p-1f, 0, -0x1.0c1524p-1f},
    {"asin", 0x1.ff7ceep-1f, 0, 0x1.86acap+0f},
    {"asin", 0x1p+0f, 0, 0x1.921fb6p+0f},
    {"asin", -0x1p+0f, 0, -0x1.921fb6p+0f},
    {"asin", 0x1.0624dep-10f, 0, 0x1.0624ep-10f},
    {"acos", 0x1p-1f, 0, 0x1.0c1524p+0f},
    {"acos", -0x1p-1f, 0, 0x1.0c1524p+1f},
    {"acos", 0x1.ccccccp-1f, 0, 0x1.cdd9fcp-2f},
    {"acos", 0x1.ffffdep-1f, 0, 0x1.752e52p-10f},
    {"acos", -0x1.ffffdep-1f, 0, 0x1.91f11p+1f},
    {"acos", 0.0f, 0, 0x1.921fb6p+0f},
    {"atan", 0x1p-1f, 0, 0x1.dac67p-2f},
    {"atan", -0x1p-1f, 0, -0x1.dac67p-2f},
    {"atan", 0x1p+0f, 0, 0x1.921fb6p-1f},
    {"atan", 0x1.8p+1f, 0, 0x1.3fc176p+0f},
    {"atan", -0x1.9p+6f, 0, -0x1.8f905ep+0f},
    {"atan", 0x1.eb851ep-5f, 0, 0x1.eaee72p-5f},
    {"atan", 0x1.2a05f2p+33f, 0, 0x1.921fb6p+0f},
    {"exp", 0x1p-1f, 0, 0x1.a61298p+0f},
    {"exp", -0x1p-1f, 0, 0x1.368b3p-1f},
    {"exp", 0x1p+0f, 0, 0x1.5bf0a8p+1f},
    {"exp", 0x1.4p+3f, 0, 0x1.5829dcp+14f},
    {"exp", -0x1.4p+4f, 0, 0x1.1b4866p-29f},
    {"exp", 0x1.0624dep-10f, 0, 0x1.004192p+0f},
    {"exp", 0x1.6p+6f, 0, 0x1.f1056ep+126f},
    {"exp2", 0x1p-1f, 0, 0x1.6a09e6p+0f},
    {"exp2", -0x1p-1f, 0, 0x1.6a09e6p-1f},
    {"exp2", 0x1p+0f, 0, 0x1p+1f},
    {"exp2", 0x1.8p+1f, 0, 0x1p+3f},
    {"exp2", -0x1.7f62b6p-6f, 0, 0x1.f7c336p-1f},
    {"exp2", 0x1.48p+4f, 0, 0x1.6a09e6p+20f},
    {"log", 0x1p-1f, 0, -0x1.62e43p-1f},
    {"log", 0x1p+1f, 0, 0x1.62e43p-1f},
    {"log", 0x1p+0f, 0, 0.0f},
    {"log", 0x1.4p+3f, 0, 0x1.26bb1cp+1f},
    {"log", 0x1.79ca1p-67f, 0, -0x1.7069e2p+5f},
    {"log", 0x1.00068ep+0f, 0, 0x1.a37aa2p-14f},
    {"log", 0x1.c363ccp+127f, 0, 0x1.62632cp+6f},
    {"log2", 0x1p-1f, 0, -0x1p+0f},
    {"log2", 0x1p+1f, 0, 0x1p+0f},
    {"log2", 0x1p+0f, 0, 0.0f},
    {"log2", 0x1.4p+3f, 0, 0x1.a934fp+1f},
    {"log2", 0x1.79ca1p-67f, 0, -0x1.09c116p+6f},
    {"log2", 0x1.00068ep+0f, 0, 0x1.2e9714p-13f},
    {"log2", 0x1p+10f, 0, 0x1.4p+3f},
    {"atan2", 0x1p+0f, 0x1p+0f, 0x1.921fb6p-1f},
    {"atan2", 0x1p+0f, -0x1p+0f, 0x1.2d97c8p+1f},
    {"atan2", -0x1p+0f, -0x1p+0f, -0x1.2d97c8p+1f},
    {"atan2", -0x1p+0f, 0x1p+0f, -0x1.921fb6p-1f},
    {"atan2", 0x1.333334p-2f, 0x1p+2f, 0x1.32a03ep-4f},
    {"atan2", 0x1p+2f, 0x1.333334p-2f, 0x1.7ef5b2p+0f},
    {"atan2", -0.0f, -0x1p+0f, -0x1.921fb6p+1f},
    {"atan2", 0x1.47ae14p-9f, -0x1.cp+2f, 0x1.921402p+1f},
    {"atan2", 0x1.ecp+6f, 0x1.0624dep-10f, 0x1.921f2cp+0f},
    {"pow", 0x1p+1f, 0x1p-1f, 0x1.6a09e6p+0f},
    {"pow", 0x1.666666p-1f, 0x1.4cccccp+0f, 0x1.4207e2p-1f},
    {"pow", 0x1p-2f, 0x1p+1f, 0x1p-4f},
    {"pow", 0x1.2p+3f, 0x1.54fdf4p-2f, 0x1.0a0e48p+1f},
    {"pow", 0x1.b33334p+0f, 0x1.8p+1f, 0x1.3a6e9ap+2f},
    {"pow", 0x1.fae148p-1f, 0x1p-1f, 0x1.fd6efep-1f},
    {"pow", -0x1p+1f, 0x1.8p+1f, -0x1p+3f},
    {"pow", 0x1.9p+3f, 0x1.ccccccp+0f, 0x1.7922dp+6f},
    {"hypot", 0x1.8p+1f, 0x1p+2f, 0x1.4p+2f},
    {"hypot", 0x1.99999ap-4f, 0x1.99999ap-3f, 0x1.c9f25cp-3f},
    {"hypot", -0x1.ep+2f, 0x1.0624dep-10f, 0x1.ep+2f},
};

struct DoubleCase {
    const char* fn;
    double x;
    double expected; // OpenMM2's result (within an ulp of the true value)
};

constexpr DoubleCase kDoubleCases[] = {
    {"sin", 0x1p-1, 0x1.eaee8744b05fp-2},
    {"sin", 0x1p+0, 0x1.aed548f090ceep-1},
    {"sin", 0x1.921fb54442d18p+0, 0x1p+0},
    {"sin", 0x1.8p+1, 0x1.210386db6d55bp-3},
    {"sin", 0x1.9p+6, -0x1.03425b78c4db8p-1},
    {"sin", -0x1p-2, -0x1.faaeed4f31577p-3},
    {"cos", 0x1p-1, 0x1.c1528065b7d5p-1},
    {"cos", 0x1p+0, 0x1.14a280fb5068cp-1},
    {"cos", 0x1.921fb54442d18p+0, 0x1.1a62633145c07p-54},
    {"cos", 0x1.8p+1, -0x1.fae04be85e5d2p-1},
    {"cos", 0x1.9p+6, 0x1.b981dbf665fdfp-1},
    {"cos", -0x1p-2, 0x1.f01549f7deea1p-1},
    {"exp", 0x1p-1, 0x1.a61298e1e069cp+0},
    {"exp", -0x1.8p+1, 0x1.97db0ccceb0afp-5},
    {"exp", 0x1.4p+3, 0x1.5829dcf95056p+14},
    {"log", 0x1p-1, -0x1.62e42fefa39efp-1},
    {"log", 0x1.8p+1, 0x1.193ea7aad030bp+0},
    {"log", 0x1.2a05f2p+33, 0x1.7069e2aa2aa5bp+4},
};

std::uint32_t bits(float v) { return std::bit_cast<std::uint32_t>(v); }
std::uint64_t bits(double v) { return std::bit_cast<std::uint64_t>(v); }

// FNV-1a over the bits of a function's results on `count` arguments spread
// over [lo, hi] (a fixed sequence, the same on every platform).
template <class F>
std::uint64_t sweepHash(F f, float lo, float hi, int count) {
    std::uint64_t h = 1469598103934665603ull;
    std::uint32_t seed = 12345u;
    for (int i = 0; i < count; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const float t = static_cast<float>(seed >> 8) * 0x1p-24f;
        const float x = lo + (hi - lo) * t;
        const auto result = f(x);
        const std::uint64_t v = bits(result);
        for (std::size_t k = 0; k < sizeof(result); ++k) {
            h ^= (v >> (8 * k)) & 0xffu;
            h *= 1099511628211ull;
        }
    }
    return h;
}

} // namespace

TEST(Determinism, LibmFloatValues) {
    for (const FloatCase& c : kFloatCases) {
        const float got = evaluate(c);
        EXPECT_EQ(bits(got), bits(c.expected))
            << std::format("{}({:a}, {:a}) = {:a}, expected {:a}", c.fn, c.a, c.b, got, c.expected);
    }
}

TEST(Determinism, LibmDoubleValues) {
    for (const DoubleCase& c : kDoubleCases) {
        const std::string fn = c.fn;
        const double got = fn == "sin"   ? libm::sin(c.x)
                           : fn == "cos" ? libm::cos(c.x)
                           : fn == "exp" ? libm::exp(c.x)
                                         : libm::log(c.x);
        EXPECT_EQ(bits(got), bits(c.expected))
            << std::format("{}({:a}) = {:a}, expected {:a}", c.fn, c.x, got, c.expected);
    }
}

// What the C standard (Annex F) asks at the edges.
TEST(Determinism, LibmSpecialValues) {
    constexpr float inf = std::numeric_limits<float>::infinity();
    constexpr float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_TRUE(std::isnan(libm::sin(nan)));
    EXPECT_TRUE(std::isnan(libm::sin(inf)));
    EXPECT_TRUE(std::isnan(libm::cos(-inf)));
    EXPECT_TRUE(std::isnan(libm::acos(1.5f)));
    EXPECT_TRUE(std::isnan(libm::log(-1.0f)));
    EXPECT_TRUE(std::isnan(libm::pow(-2.0f, 0.5f)));
    EXPECT_EQ(bits(libm::sin(-0.0f)), bits(-0.0f));
    EXPECT_EQ(bits(libm::atan(-0.0f)), bits(-0.0f));
    EXPECT_EQ(bits(libm::asin(-0.0f)), bits(-0.0f));
    EXPECT_EQ(libm::cos(0.0f), 1.0f);
    EXPECT_EQ(libm::acos(1.0f), 0.0f);
    EXPECT_EQ(bits(libm::atan2(0.0f, -0.0f)), bits(libm::atan2(0.0f, -1.0f)));
    EXPECT_EQ(bits(libm::atan2(-0.0f, 0.0f)), bits(-0.0f));
    EXPECT_EQ(libm::atan2(1.0f, 0.0f), 0x1.921fb6p+0f);
    EXPECT_EQ(libm::atan2(inf, -inf), 0x1.2d97c8p+1f);
    EXPECT_EQ(libm::atan(inf), 0x1.921fb6p+0f);
    EXPECT_EQ(libm::exp(-inf), 0.0f);
    EXPECT_EQ(libm::exp(100.0f), inf);
    EXPECT_EQ(libm::exp2(-200.0f), 0.0f);
    EXPECT_EQ(libm::log(0.0f), -inf);
    EXPECT_EQ(libm::log2(inf), inf);
    EXPECT_EQ(libm::pow(nan, 0.0f), 1.0f);
    EXPECT_EQ(libm::pow(1.0f, nan), 1.0f);
    EXPECT_EQ(bits(libm::pow(-0.0f, 3.0f)), bits(-0.0f));
    EXPECT_EQ(libm::pow(0.0f, -1.0f), inf);
    EXPECT_EQ(libm::pow(-inf, 3.0f), -inf);
    EXPECT_EQ(libm::pow(0.5f, inf), 0.0f);
    EXPECT_EQ(libm::pow(-1.0f, -inf), 1.0f);
    EXPECT_EQ(libm::hypot(-inf, nan), inf);
    // Exact cases.
    for (int k = -20; k <= 20; ++k) {
        const float p = std::ldexp(1.0f, k);
        EXPECT_EQ(libm::exp2(static_cast<float>(k)), p);
        EXPECT_EQ(libm::log2(p), static_cast<float>(k));
    }
    EXPECT_EQ(libm::pow(3.0f, 2.0f), 9.0f);
    EXPECT_EQ(libm::pow(2.0f, 0.5f), std::sqrt(2.0f));
}

// Many arguments over the ranges the game uses: one hash per function.
TEST(Determinism, LibmSweepHashes) {
    struct Sweep {
        const char* name;
        std::uint64_t hash;
        std::uint64_t expected;
    };
    constexpr int n = 200000;
    const Sweep sweeps[] = {
        {"sin", sweepHash([](float x) { return libm::sin(x); }, -100.0f, 100.0f, n),
         0x9abf115c21862788ull},
        {"cos", sweepHash([](float x) { return libm::cos(x); }, -100.0f, 100.0f, n),
         0xac728cd307444a07ull},
        {"tan", sweepHash([](float x) { return libm::tan(x); }, -1.5f, 1.5f, n),
         0x68ca500dbd5464f6ull},
        {"asin", sweepHash([](float x) { return libm::asin(x); }, -1.0f, 1.0f, n),
         0x531280a1a2a4e2f9ull},
        {"acos", sweepHash([](float x) { return libm::acos(x); }, -1.0f, 1.0f, n),
         0x31dcb7a55fbda6bfull},
        {"atan", sweepHash([](float x) { return libm::atan(x); }, -50.0f, 50.0f, n),
         0x6c81cfcd4b2e9ac8ull},
        {"atan2", sweepHash([](float x) { return libm::atan2(x, 7.0f - x * 0.5f); }, -40.0f, 40.0f, n),
         0x1685eb43a3e18fa4ull},
        {"exp", sweepHash([](float x) { return libm::exp(x); }, -30.0f, 30.0f, n),
         0xc6e049beed2c73c6ull},
        {"exp2", sweepHash([](float x) { return libm::exp2(x); }, -30.0f, 30.0f, n),
         0xe15ddc1226601472ull},
        {"log", sweepHash([](float x) { return libm::log(x); }, 0.001f, 1000.0f, n),
         0x3b4b340f90dd383dull},
        {"log2", sweepHash([](float x) { return libm::log2(x); }, 0.001f, 1000.0f, n),
         0x3e01c4c6ca9d6679ull},
        {"pow", sweepHash([](float x) { return libm::pow(x, 1.7f); }, 0.0f, 50.0f, n),
         0x082dcae6ae7bb15full},
        {"hypot", sweepHash([](float x) { return libm::hypot(x, 3.0f - x); }, -50.0f, 50.0f, n),
         0x62fb5b376c9281d8ull},
        {"dsin", sweepHash([](float x) { return libm::sin(static_cast<double>(x)); }, -10.0f, 10.0f, n),
         0xbbe99dcdc22e568aull},
        {"dcos", sweepHash([](float x) { return libm::cos(static_cast<double>(x)); }, -10.0f, 10.0f, n),
         0x4d5fc1165c83501aull},
    };
    for (const Sweep& s : sweeps)
        EXPECT_EQ(s.hash, s.expected) << std::format("{}: hash {:#018x}", s.name, s.hash);
}

// The code a host and its clients both simulate calls OpenMM2's functions,
// never the C runtime's, whose results differ between platforms. The
// drawing, the camera, the sound and the tools may use either.
TEST(Determinism, SimulationCallsNoRuntimeTranscendentals) {
#ifndef OPENMM2_SOURCE_DIR
    GTEST_SKIP() << "built without OPENMM2_SOURCE_DIR";
#else
    const std::filesystem::path root = OPENMM2_SOURCE_DIR;
    if (!std::filesystem::is_directory(root / "src"))
        GTEST_SKIP() << "no sources at " << root.string();
    const char* const trees[] = {"src/phys",         "src/ai",         "src/net",     "src/game/session",
                                 "src/game/bangers", "src/game/world", "src/game/net"};
    const char* const files[] = {"src/core/Math.cpp", "src/city/AiMap.cpp", "src/game/TrafficBodies.cpp",
                                 "src/game/PlayerVehicle.cpp", "src/app/Controls.cpp"};
    const char* const names[] = {"sin",   "cos",  "tan",  "asin",  "acos",  "atan", "atan2",
                                 "sinh",  "cosh", "tanh", "exp",   "exp2",  "expm1", "log",
                                 "log2",  "log10", "log1p", "pow", "hypot", "cbrt"};
    std::vector<std::filesystem::path> sources;
    for (const char* t : trees)
        for (const auto& e : std::filesystem::recursive_directory_iterator(root / t))
            if (e.path().extension() == ".cpp" || e.path().extension() == ".h")
                sources.push_back(e.path());
    for (const char* f : files)
        sources.push_back(root / f);
    int checked = 0;
    for (const auto& path : sources) {
        std::ifstream in(path);
        ASSERT_TRUE(in) << path.string();
        ++checked;
        std::string line;
        for (int n = 1; std::getline(in, line); ++n) {
            line = line.substr(0, line.find("//"));
            for (const char* name : names) {
                // std::sin( and the C float names (sinf() at a word start.
                const std::string forms[] = {"std::" + std::string(name) + "(", std::string(name) + "f("};
                for (const std::string& form : forms) {
                    for (auto at = line.find(form); at != std::string::npos; at = line.find(form, at + 1)) {
                        const char before = at == 0 ? ' ' : line[at - 1];
                        const bool word = !(std::isalnum(static_cast<unsigned char>(before)) ||
                                            before == '_' || before == ':' || before == '.');
                        if (form.starts_with("std::") || word)
                            ADD_FAILURE() << path.string() << ":" << n << ": " << form
                                          << " (use mm2::libm, core/Libm.h)";
                    }
                }
            }
        }
    }
    EXPECT_GT(checked, 50);
#endif
}
