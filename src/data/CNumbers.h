#pragma once

// Number parsing the way MM2's loaders do it: the C runtime's atof/atoi (and
// the scanf %f/%d conversions, which accept the same text). They read the
// longest numeric prefix after leading white space and ignore whatever
// follows ("1.#QNAN0" is 1, "12px" is 12, "0x10" is 0); text without a
// numeric prefix gives 0. atoi reads decimal digits only. str::parseDouble /
// str::parseInt are stricter (whole string, hex) and are not used for game
// data.

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace mm2::data {

namespace detail {

inline bool cSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}
inline bool cDigit(char c) { return c >= '0' && c <= '9'; }

// The numeric prefix of `s` after its leading white space, or an empty view
// when there is none. `real` selects atof's grammar (sign, digits, optional
// fraction, optional exponent) over atoi's (sign, digits).
inline std::string_view numericPrefix(std::string_view s, bool real) {
    std::size_t start = 0;
    while (start < s.size() && cSpace(s[start]))
        ++start;
    std::size_t i = start;
    if (i < s.size() && (s[i] == '+' || s[i] == '-'))
        ++i;
    std::size_t digits = 0;
    while (i < s.size() && cDigit(s[i])) {
        ++i;
        ++digits;
    }
    if (real && i < s.size() && s[i] == '.') {
        ++i;
        while (i < s.size() && cDigit(s[i])) {
            ++i;
            ++digits;
        }
    }
    if (digits == 0)
        return {};
    if (real && i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        std::size_t j = i + 1;
        if (j < s.size() && (s[j] == '+' || s[j] == '-'))
            ++j;
        if (j < s.size() && cDigit(s[j])) {
            while (j < s.size() && cDigit(s[j]))
                ++j;
            i = j;
        }
    }
    return s.substr(start, i - start);
}

} // namespace detail

// The value atof reads from `s`, or std::nullopt when `s` has no numeric
// prefix (atof then returns 0).
inline std::optional<double> atofPrefix(std::string_view s) {
    std::string_view num = detail::numericPrefix(s, true);
    if (num.empty())
        return std::nullopt;
    const bool negative = num[0] == '-';
    if (num[0] == '+' || num[0] == '-')
        num.remove_prefix(1);
    double value = 0.0;
    const auto [ptr, ec] = std::from_chars(num.data(), num.data() + num.size(), value);
    if (ec == std::errc::result_out_of_range) {
        // atof gives +-HUGE_VAL on overflow and 0 on underflow.
        const auto e = num.find_first_of("eE");
        const bool underflow = e != std::string_view::npos && e + 1 < num.size() && num[e + 1] == '-';
        value = underflow ? 0.0 : std::numeric_limits<double>::infinity();
    }
    return negative ? -value : value;
}

// atof: the numeric prefix, 0 when there is none.
inline double cAtof(std::string_view s) { return atofPrefix(s).value_or(0.0); }

// The value atoi reads from `s` (decimal digits, wrapping like the 32-bit
// runtime's accumulation), or std::nullopt when `s` has no numeric prefix.
inline std::optional<int> atoiPrefix(std::string_view s) {
    std::string_view num = detail::numericPrefix(s, false);
    if (num.empty())
        return std::nullopt;
    const bool negative = num[0] == '-';
    if (num[0] == '+' || num[0] == '-')
        num.remove_prefix(1);
    std::uint32_t value = 0;
    for (char c : num)
        value = value * 10u + static_cast<std::uint32_t>(c - '0');
    if (negative)
        value = 0u - value;
    return static_cast<int>(value);
}

// atoi: the decimal prefix, 0 when there is none.
inline int cAtoi(std::string_view s) { return atoiPrefix(s).value_or(0); }

// datAsciiTokenizer::GetFloat of a whitespace-delimited token: a token that
// does not start with a digit, '-' or '.' is an error and reads as 0; else
// atof.
inline float datTokenFloat(std::string_view token) {
    const char c = token.empty() ? '\0' : token[0];
    if (!(detail::cDigit(c) || c == '-' || c == '.'))
        return 0.0f;
    return static_cast<float>(cAtof(token));
}

// datAsciiTokenizer::GetInt: a token that does not start with a digit or
// '-' is an error and reads as 0; else atoi.
inline int datTokenInt(std::string_view token) {
    const char c = token.empty() ? '\0' : token[0];
    if (!(detail::cDigit(c) || c == '-'))
        return 0;
    return cAtoi(token);
}

} // namespace mm2::data
