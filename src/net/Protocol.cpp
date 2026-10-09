#include "net/Protocol.h"

#include "core/StringUtil.h"

namespace mm2::net {
namespace {

// Length of the well-formed UTF-8 sequence at `pos` (RFC 3629: no overlong
// forms, no surrogates, nothing above U+10FFFF) and its code point; 0 when the
// bytes there are not one.
std::size_t utf8Sequence(std::string_view s, std::size_t pos, char32_t& cp) {
    const auto at = [&](std::size_t i) { return static_cast<unsigned>(static_cast<unsigned char>(s[i])); };
    const unsigned b0 = at(pos);
    if (b0 < 0x80) {
        cp = b0;
        return 1;
    }
    std::size_t len = 0;
    unsigned lo = 0x80, hi = 0xBF; // allowed range of the second byte
    if (b0 >= 0xC2 && b0 <= 0xDF) {
        len = 2;
        cp = b0 & 0x1F;
    } else if (b0 >= 0xE0 && b0 <= 0xEF) {
        len = 3;
        cp = b0 & 0x0F;
        lo = b0 == 0xE0 ? 0xA0 : 0x80;
        hi = b0 == 0xED ? 0x9F : 0xBF;
    } else if (b0 >= 0xF0 && b0 <= 0xF4) {
        len = 4;
        cp = b0 & 0x07;
        lo = b0 == 0xF0 ? 0x90 : 0x80;
        hi = b0 == 0xF4 ? 0x8F : 0xBF;
    } else {
        return 0;
    }
    if (len > s.size() - pos)
        return 0;
    for (std::size_t i = 1; i < len; ++i) {
        const unsigned b = at(pos + i);
        if (b < (i == 1 ? lo : 0x80u) || b > (i == 1 ? hi : 0xBFu))
            return 0;
        cp = (cp << 6) | (b & 0x3F);
    }
    return len;
}

} // namespace

std::string sanitizeText(std::string_view text, std::size_t maxBytes) {
    std::string clean;
    clean.reserve(text.size());
    for (std::size_t pos = 0; pos < text.size();) {
        char32_t cp = 0;
        const std::size_t len = utf8Sequence(text, pos, cp);
        if (len == 0) {
            ++pos; // not UTF-8: dropped
            continue;
        }
        if (cp == U'\t' || cp == U'\n' || cp == U'\r')
            clean.push_back(' ');
        else if (cp >= 0x20 && !(cp >= 0x7F && cp <= 0x9F))
            clean.append(text.substr(pos, len));
        pos += len;
    }
    std::string_view out = str::trim(clean);
    if (out.size() > maxBytes) {
        // `clean` is valid UTF-8: back up to the start of a character.
        std::size_t cut = maxBytes;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80)
            --cut;
        out = str::trim(out.substr(0, cut));
    }
    return std::string(out);
}

bool isValidAssetName(std::string_view name) {
    if (name.empty() || name.size() > kMaxShortStringLength)
        return false;
    return std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

const char* describe(DisconnectReason reason) {
    switch (reason) {
    case DisconnectReason::None: return "disconnected";
    case DisconnectReason::Left: return "left the game";
    case DisconnectReason::VersionMismatch: return "incompatible game version";
    case DisconnectReason::ServerFull: return "the session is full";
    case DisconnectReason::BadPassword: return "wrong password";
    case DisconnectReason::GameInProgress: return "the game has already started";
    case DisconnectReason::Kicked: return "kicked by the host";
    case DisconnectReason::HostShutdown: return "the host closed the session";
    case DisconnectReason::Timeout: return "connection timed out";
    case DisconnectReason::JoinTimeout: return "the host did not respond";
    case DisconnectReason::ProtocolError: return "protocol error";
    }
    return "unknown reason";
}

std::optional<MsgType> peekMessageType(std::span<const std::byte> packet) {
    if (packet.empty())
        return std::nullopt;
    const auto t = std::to_integer<std::uint8_t>(packet[0]);
    if (t < static_cast<std::uint8_t>(MsgType::Challenge) || t > static_cast<std::uint8_t>(MsgType::Last))
        return std::nullopt;
    return static_cast<MsgType>(t);
}

} // namespace mm2::net
