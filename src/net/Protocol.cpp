#include "net/Protocol.h"

namespace mm2::net {

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
