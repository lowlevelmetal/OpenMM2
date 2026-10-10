#pragma once

// OpenMM2's own elementary functions for everything a host and its clients
// both simulate (docs/physics.md, "The same results on every platform").
//
// The C runtimes round sin, cos, atan2, exp, log, pow and the rest
// differently in the last bit: glibc, the MSVC runtime and MinGW's (or
// Wine's) each have their own implementations. A network game whose host
// and client were built against different runtimes then simulated the same
// sample differently, and every difference became a correction (the
// players' cars record's O6). These functions use only the basic
// operations (+ - * /, which IEEE 754 rounds the same way everywhere when
// the compiler neither fuses nor reorders them: cmake/CompilerOptions.cmake)
// and the exact ones (sqrt, floor, the exponent bits), so they give the same
// bits on every platform and compiler.
//
// Accuracy. The double versions are within about one unit in the last place.
// The float versions are the double result rounded to float: that is what
// MM2's code got from the x87, whose fsin, fcos, fpatan, fyl2x and f2xm1
// work in extended precision whatever the precision control, the result
// being stored to a float. They are correctly rounded except in the rare
// cases a double result lies within its error of a float's rounding
// boundary, which no test of the game's ranges met (docs/physics.md).
//
// Not here, because IEEE 754 makes their results exact (or correctly
// rounded) everywhere: std::sqrt, std::fmod, std::remainder, std::floor,
// std::ceil, std::trunc, std::round / std::lround, std::abs, std::copysign.

namespace mm2::libm {

double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
double exp(double x);
double exp2(double x);
double log(double x);
double log2(double x);
double pow(double x, double y);

inline float sin(float x) { return static_cast<float>(sin(static_cast<double>(x))); }
inline float cos(float x) { return static_cast<float>(cos(static_cast<double>(x))); }
inline float tan(float x) { return static_cast<float>(tan(static_cast<double>(x))); }
inline float asin(float x) { return static_cast<float>(asin(static_cast<double>(x))); }
inline float acos(float x) { return static_cast<float>(acos(static_cast<double>(x))); }
inline float atan(float x) { return static_cast<float>(atan(static_cast<double>(x))); }
inline float atan2(float y, float x) {
    return static_cast<float>(atan2(static_cast<double>(y), static_cast<double>(x)));
}
inline float exp(float x) { return static_cast<float>(exp(static_cast<double>(x))); }
inline float exp2(float x) { return static_cast<float>(exp2(static_cast<double>(x))); }
inline float log(float x) { return static_cast<float>(log(static_cast<double>(x))); }
inline float log2(float x) { return static_cast<float>(log2(static_cast<double>(x))); }
inline float pow(float x, float y) {
    return static_cast<float>(pow(static_cast<double>(x), static_cast<double>(y)));
}
// sqrt(x^2 + y^2): the squares are exact in double, so this is the sum
// rounded once and its square root rounded twice (to double, then float).
float hypot(float x, float y);

} // namespace mm2::libm
