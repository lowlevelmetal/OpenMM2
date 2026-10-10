#include "core/Libm.h"

// The algorithms are the textbook ones (Cody and Waite's argument reduction,
// Taylor and arctangent series in Horner form, the atanh form of the
// logarithm as in fdlibm); see Libm.h for why OpenMM2 has its own. Every
// operation is written out in the order it must be evaluated: the project
// builds with floating-point contraction off and without fast-math, so the
// compilers keep that order and round each step as IEEE 754 says.

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace mm2::libm {

namespace {

// The constants are the nearest doubles to their values (computed with
// 90-digit arithmetic), written in hex so every compiler reads the same bits.

// pi/2 in three parts. The first two have 33 significant bits, so n times
// either is exact for |n| < 2^20.
constexpr double kPio2Part1 = 0x1.921fb544p+0;
constexpr double kPio2Part2 = 0x1.0b4611a6p-34;
constexpr double kPio2Part3 = 0x1.3198a2e037073p-69;
constexpr double kTwoOverPi = 0x1.45f306dc9c883p-1;
// pi/4, 3 pi/4; pi/2 and pi as the nearest double plus the rest.
constexpr double kPio4 = 0x1.921fb54442d18p-1;
constexpr double k3Pio4 = 0x1.2d97c7f3321d2p+1;
constexpr double kPio2Hi = 0x1.921fb54442d18p+0;
constexpr double kPio2Lo = 0x1.1a62633145c07p-54;
constexpr double kPiHi = 0x1.921fb54442d18p+1;
constexpr double kPiLo = 0x1.1a62633145c07p-53;
// ln 2 in two parts (the first has 42 significant bits, so n times it is
// exact for |n| < 2^11), log2(e) and sqrt(2).
constexpr double kLn2Hi = 0x1.62e42fefa38p-1;
constexpr double kLn2Lo = 0x1.ef35793c7673p-45;
constexpr double kLn2 = 0x1.62e42fefa39efp-1;
constexpr double kLog2e = 0x1.71547652b82fep+0;
constexpr double kSqrt2 = 0x1.6a09e667f3bcdp+0;
// ln(DBL_MAX) and ln of half the smallest subnormal: exp overflows above
// the first and is 0 below the second.
constexpr double kExpMax = 0x1.62e42fefa39efp+9;
constexpr double kExpMin = -0x1.74910d52d3052p+9;

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// sin's Taylor coefficients (-1)^k / (2k+1)! for x^3 .. x^17: on |r| <= pi/4
// the first term left out, r^19 / 19!, is below 2^-62 of the result.
constexpr double kSin[] = {
    -0x1.5555555555555p-3, 0x1.1111111111111p-7, -0x1.a01a01a01a01ap-13, 0x1.71de3a556c734p-19,
    -0x1.ae64567f544e4p-26, 0x1.6124613a86d09p-33, -0x1.ae7f3e733b81fp-41, 0x1.952c77030ad4ap-49};
// cos's, (-1)^k / (2k)! for x^4 .. x^18 (r^20 / 20! is below 2^-67).
constexpr double kCos[] = {
    0x1.5555555555555p-5, -0x1.6c16c16c16c17p-10, 0x1.a01a01a01a01ap-16, -0x1.27e4fb7789f5cp-22,
    0x1.1eed8eff8d898p-29, -0x1.93974a8c07c9dp-37, 0x1.ae7f3e733b81fp-45, -0x1.6827863b97d97p-53};
// exp's, 1 / k! for x^2 .. x^14: on |r| <= ln(2) / 2, r^15 / 15! is below
// 2^-62.
constexpr double kExp[] = {
    0x1p-1, 0x1.5555555555555p-3, 0x1.5555555555555p-5, 0x1.1111111111111p-7, 0x1.6c16c16c16c17p-10,
    0x1.a01a01a01a01ap-13, 0x1.a01a01a01a01ap-16, 0x1.71de3a556c734p-19, 0x1.27e4fb7789f5cp-22,
    0x1.ae64567f544e4p-26, 0x1.1eed8eff8d898p-29, 0x1.6124613a86d09p-33, 0x1.93974a8c07c9dp-37};
// atan's, (-1)^k / (2k+1) for x^3 .. x^17: on |u| <= 1/16 the first term
// left out is below 2^-76 of the result.
constexpr double kAtan[] = {
    -0x1.5555555555555p-2, 0x1.999999999999ap-3, -0x1.2492492492492p-3, 0x1.c71c71c71c71cp-4,
    -0x1.745d1745d1746p-4, 0x1.3b13b13b13b14p-4, -0x1.1111111111111p-4, 0x1.e1e1e1e1e1e1ep-5};
// atanh's, 2 / (2k+1) for s^2 .. s^22: log(1 + f) = 2 atanh(s) with
// s = f / (2 + f), |s| <= 0.1716; s^24 / 25 is below 2^-65.
constexpr double kLog[] = {
    0x1.5555555555555p-1, 0x1.999999999999ap-2, 0x1.2492492492492p-2, 0x1.c71c71c71c71cp-3,
    0x1.745d1745d1746p-3, 0x1.3b13b13b13b14p-3, 0x1.1111111111111p-3, 0x1.e1e1e1e1e1e1ep-4,
    0x1.af286bca1af28p-4, 0x1.8618618618618p-4, 0x1.642c8590b2164p-4};

// atan(k / 8) for k = 0 .. 8 as the nearest double plus the rest.
constexpr double kAtanHi[9] = {0.0,
                               0x1.fd5ba9aac2f6ep-4,
                               0x1.f5b75f92c80ddp-3,
                               0x1.6f61941e4def1p-2,
                               0x1.dac670561bb4fp-2,
                               0x1.1e00babdefeb4p-1,
                               0x1.4978fa3269ee1p-1,
                               0x1.700a7c5784634p-1,
                               0x1.921fb54442d18p-1};
constexpr double kAtanLo[9] = {0.0,
                               -0x1.cd37686760c17p-59,
                               0x1.8ab6e3cf7afbdp-57,
                               -0x1.c63aae6f6e918p-56,
                               0x1.a2b7f222f65e2p-56,
                               -0x1.928df287a668fp-58,
                               0x1.2419a87f2a458p-56,
                               -0x1.8c34d25aadef6p-56,
                               0x1.1a62633145c07p-55};

bool isNaN(double x) { return x != x; }

// c[0] + x (c[1] + x (c[2] + ...)), evaluated from the innermost term out.
template <std::size_t N>
double horner(double x, const double (&c)[N]) {
    double p = c[N - 1];
    for (std::size_t i = N - 1; i-- > 0;)
        p = c[i] + x * p;
    return p;
}

// 2^k for -1022 <= k <= 1023.
double pow2(int k) { return std::bit_cast<double>(static_cast<std::uint64_t>(k + 1023) << 52); }

// v * 2^n for v near 1 and -1076 <= n <= 1100: exact unless the result is
// subnormal (then rounded once) or overflows.
double scale2(double v, int n) {
    if (n > 1023) {
        v *= 0x1p+1023;
        n -= 1023;
    }
    if (n < -1022)
        return (v * pow2(n + 54)) * 0x1p-54;
    return v * pow2(n);
}

// The nearest integer to v (halves up), as a double.
double nearest(double v) { return std::floor(v + 0.5); }

// x = n pi/2 + (hi + lo) with |hi| <= about pi/4 and |lo| below half an ulp
// of hi; `quadrant` is n mod 4. Cody and Waite's reduction: x - n p1 is
// exact, its difference with n p2 is taken with its rounding error (Knuth's
// TwoSum), and n p3 comes off the error. With 99 bits of pi/2 the remainder
// keeps full precision for |x| up to about 2^20, the game's range by far.
struct Reduced {
    double hi = 0.0;
    double lo = 0.0;
    int quadrant = 0;
};

Reduced reduce(double x) {
    if (std::abs(x) <= kPio4)
        return {x, 0.0, 0};
    const double n = nearest(x * kTwoOverPi);
    const double r1 = x - n * kPio2Part1;
    const double w = n * kPio2Part2;
    const double r2 = r1 - w;
    const double v = r2 - r1;
    const double error = (r1 - (r2 - v)) - (w + v);
    const double tail = error - n * kPio2Part3;
    Reduced r;
    r.hi = r2 + tail;
    r.lo = (r2 - r.hi) + tail;
    r.quadrant = static_cast<int>(n - 4.0 * std::floor(n * 0.25));
    return r;
}

// sin(r + lo) for |r| <= about pi/4: r + r^3 P(r^2), plus lo cos(r).
double sinKernel(double r, double lo) {
    const double z = r * r;
    const double p = z * horner(z, kSin);
    return r + (r * p + lo * (1.0 - 0.5 * z));
}

// cos(r + lo) for |r| <= about pi/4: 1 - r^2/2 + r^4 Q(r^2), less lo sin(r).
// 1 - r^2/2 rounds; its rounding error, (1 - w) - r^2/2, is added back.
double cosKernel(double r, double lo) {
    const double z = r * r;
    const double q = (z * z) * horner(z, kCos);
    const double hz = 0.5 * z;
    const double w = 1.0 - hz;
    return w + (((1.0 - w) - hz) + (q - r * lo));
}

// atan(t) for 0 <= t <= 1 as hi + lo: atan(c) for c the nearest multiple of
// 1/8, plus atan((t - c) / (1 + t c)) (|u| <= 1/16) by its series. t - c
// is exact.
void atanCore(double t, double& hi, double& lo) {
    const int k = static_cast<int>(t * 8.0 + 0.5);
    double u = t;
    if (k > 0) {
        const double c = static_cast<double>(k) * 0.125;
        u = (t - c) / (1.0 + t * c);
    }
    const double z = u * u;
    const double p = z * horner(z, kAtan);
    hi = kAtanHi[k];
    lo = kAtanLo[k] + (u + u * p);
}

// c - (hi + lo) for c = cHi + cLo with |cHi| >= |hi|, as a new hi + lo.
void subtractFrom(double cHi, double cLo, double& hi, double& lo) {
    const double d = cHi - hi;
    const double error = (cHi - d) - hi;
    lo = (cLo - lo) - error;
    hi = d;
}

// exp(r) for |r| <= about ln(2) / 2: 1 + r + r^2 P(r).
double expKernel(double r) {
    const double p = horner(r, kExp);
    return 1.0 + (r + (r * r) * p);
}

// x > 0 finite as 2^e * m with m in [sqrt(1/2), sqrt(2)); returns log(m).
// fdlibm's form: with f = m - 1 (exact) and s = f / (2 + f),
// log(m) = f - (f^2/2 - s (f^2/2 + R)), R = 2 (s^2/3 + s^4/5 + ...).
double logReduced(double x, int& e) {
    e = 0;
    if (x < 0x1p-1022) { // subnormal
        x *= 0x1p+54;
        e = -54;
    }
    const auto bits = std::bit_cast<std::uint64_t>(x);
    e += static_cast<int>((bits >> 52) & 0x7ffu) - 1023;
    double m = std::bit_cast<double>((bits & 0x000fffffffffffffu) | 0x3ff0000000000000u);
    if (m > kSqrt2) {
        m *= 0.5;
        ++e;
    }
    const double f = m - 1.0;
    const double s = f / (2.0 + f);
    const double z = s * s;
    const double hfsq = (0.5 * f) * f;
    const double r = z * horner(z, kLog);
    return f - (hfsq - s * (hfsq + r));
}

// log(x) for x > 0 finite.
double logPositive(double x) {
    int e = 0;
    const double logm = logReduced(x, e);
    const double de = static_cast<double>(e);
    return de * kLn2Hi + (de * kLn2Lo + logm);
}

} // namespace

double sin(double x) {
    if (isNaN(x))
        return x;
    if (std::abs(x) == kInf)
        return kNaN;
    if (std::abs(x) < 0x1p-27) // x^3/6 is below half an ulp
        return x;
    const Reduced r = reduce(x);
    switch (r.quadrant) {
    case 0:
        return sinKernel(r.hi, r.lo);
    case 1:
        return cosKernel(r.hi, r.lo);
    case 2:
        return -sinKernel(r.hi, r.lo);
    default:
        return -cosKernel(r.hi, r.lo);
    }
}

double cos(double x) {
    if (isNaN(x))
        return x;
    if (std::abs(x) == kInf)
        return kNaN;
    if (std::abs(x) < 0x1p-27) // x^2/2 is below half an ulp of 1
        return 1.0;
    const Reduced r = reduce(x);
    switch (r.quadrant) {
    case 0:
        return cosKernel(r.hi, r.lo);
    case 1:
        return -sinKernel(r.hi, r.lo);
    case 2:
        return -cosKernel(r.hi, r.lo);
    default:
        return sinKernel(r.hi, r.lo);
    }
}

double tan(double x) {
    if (isNaN(x))
        return x;
    if (std::abs(x) == kInf)
        return kNaN;
    if (std::abs(x) < 0x1p-27)
        return x;
    const Reduced r = reduce(x);
    const double s = sinKernel(r.hi, r.lo);
    const double c = cosKernel(r.hi, r.lo);
    return (r.quadrant & 1) != 0 ? -c / s : s / c;
}

double atan(double x) {
    if (isNaN(x))
        return x;
    const double ax = std::abs(x);
    double hi = 0.0, lo = 0.0;
    if (ax <= 1.0) {
        atanCore(ax, hi, lo);
    } else {
        // pi/2 - atan(1/x); 1/inf is 0
        atanCore(1.0 / ax, hi, lo);
        subtractFrom(kPio2Hi, kPio2Lo, hi, lo);
    }
    const double a = hi + lo;
    return std::signbit(x) ? -a : a;
}

double atan2(double y, double x) {
    if (isNaN(x) || isNaN(y))
        return x + y;
    const bool negative = std::signbit(y);
    const double ax = std::abs(x), ay = std::abs(y);
    double a = 0.0; // the angle of (x, |y|), 0 .. pi
    if (ay == 0.0) {
        // atan2(+-0, x): +-0 for x > 0 or +0, +-pi for x < 0 or -0
        a = std::signbit(x) ? kPiHi : 0.0;
    } else if (ax == 0.0) {
        a = kPio2Hi;
    } else if (ay == kInf) {
        a = ax == kInf ? (x > 0.0 ? kPio4 : k3Pio4) : kPio2Hi;
    } else if (ax == kInf) {
        a = x > 0.0 ? 0.0 : kPiHi;
    } else {
        double hi = 0.0, lo = 0.0;
        if (ay <= ax) {
            atanCore(ay / ax, hi, lo);
        } else {
            atanCore(ax / ay, hi, lo);
            subtractFrom(kPio2Hi, kPio2Lo, hi, lo);
        }
        if (x < 0.0)
            subtractFrom(kPiHi, kPiLo, hi, lo);
        a = hi + lo;
    }
    return negative ? -a : a;
}

double asin(double x) {
    if (isNaN(x))
        return x;
    if (std::abs(x) > 1.0)
        return kNaN;
    // (1 - x)(1 + x) loses nothing near |x| = 1, where 1 - x^2 would.
    return atan2(x, std::sqrt((1.0 - x) * (1.0 + x)));
}

double acos(double x) {
    if (isNaN(x))
        return x;
    if (std::abs(x) > 1.0)
        return kNaN;
    return atan2(std::sqrt((1.0 - x) * (1.0 + x)), x);
}

double exp(double x) {
    if (isNaN(x))
        return x;
    if (x > kExpMax)
        return kInf;
    if (x < kExpMin)
        return 0.0;
    if (std::abs(x) < 0x1p-54)
        return 1.0 + x;
    // x = n ln2 + r, |r| <= ln(2)/2; x - n ln2hi is exact.
    const double n = nearest(x * kLog2e);
    const double r = (x - n * kLn2Hi) - n * kLn2Lo;
    return scale2(expKernel(r), static_cast<int>(n));
}

double exp2(double x) {
    if (isNaN(x))
        return x;
    if (x >= 1024.0)
        return kInf;
    if (x < -1075.0)
        return 0.0;
    // x = n + f, |f| <= 1/2 (exact): 2^f = exp(f ln 2). Integers are exact.
    const double n = nearest(x);
    const double f = x - n;
    return scale2(expKernel(f * kLn2), static_cast<int>(n));
}

double log(double x) {
    if (isNaN(x))
        return x;
    if (x < 0.0)
        return kNaN;
    if (x == 0.0)
        return -kInf;
    if (x == kInf)
        return kInf;
    return logPositive(x);
}

double log2(double x) {
    if (isNaN(x))
        return x;
    if (x < 0.0)
        return kNaN;
    if (x == 0.0)
        return -kInf;
    if (x == kInf)
        return kInf;
    // e + log(m) / ln 2: exact for powers of two.
    int e = 0;
    const double logm = logReduced(x, e);
    return static_cast<double>(e) + logm * kLog2e;
}

double pow(double x, double y) {
    if (y == 0.0 || x == 1.0)
        return 1.0;
    if (isNaN(x) || isNaN(y))
        return x + y;
    const double ax = std::abs(x);
    if (std::abs(y) == kInf) {
        if (ax == 1.0)
            return 1.0;
        return (ax < 1.0) == (y > 0.0) ? 0.0 : kInf;
    }
    const bool integer = std::floor(y) == y;
    const bool odd = integer && std::abs(y) < 0x1p+53 && std::floor(y * 0.5) * 2.0 != y;
    if (x == 0.0 || ax == kInf) {
        // pow(+-0, y) and pow(+-inf, y): 0 or inf, signed for odd y
        const double r = (x == 0.0) == (y > 0.0) ? 0.0 : kInf;
        return odd && std::signbit(x) ? -r : r;
    }
    if (x < 0.0 && !integer)
        return kNaN;
    const double sign = x < 0.0 && odd ? -1.0 : 1.0;
    // The simple powers exactly or correctly rounded.
    if (y == 1.0)
        return x;
    if (y == 2.0)
        return x * x;
    if (y == -1.0)
        return 1.0 / x;
    if (y == 0.5)
        return std::sqrt(x);
    return sign * exp(y * logPositive(ax));
}

float hypot(float x, float y) {
    if (std::isinf(x) || std::isinf(y))
        return std::numeric_limits<float>::infinity();
    const double dx = x, dy = y;
    return static_cast<float>(std::sqrt(dx * dx + dy * dy));
}

} // namespace mm2::libm
