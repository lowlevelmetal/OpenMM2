#include "net/Net.h"

#include "core/Log.h"
#include "core/StringUtil.h"

#include <enet/enet.h>

#include <chrono>
#include <format>
#include <mutex>

namespace mm2::net {
namespace {

std::mutex g_initMutex;
int g_initCount = 0;
bool g_initOk = false;

bool acquire() {
    std::lock_guard lock(g_initMutex);
    if (g_initCount++ == 0) {
        g_initOk = enet_initialize() == 0;
        if (!g_initOk)
            log::error("net: enet_initialize failed");
    }
    return g_initOk;
}

void release() {
    std::lock_guard lock(g_initMutex);
    if (--g_initCount == 0 && g_initOk) {
        enet_deinitialize();
        g_initOk = false;
    }
}

} // namespace

NetLibrary::NetLibrary() : m_ok(acquire()) {}
NetLibrary::NetLibrary(const NetLibrary&) : m_ok(acquire()) {}
NetLibrary::~NetLibrary() { release(); }

std::optional<Address> Address::parse(std::string_view text, std::uint16_t defaultPort) {
    text = str::trim(text);
    Address a;
    a.port = defaultPort;
    if (const auto colon = text.rfind(':'); colon != std::string_view::npos) {
        const auto p = str::parseInt(text.substr(colon + 1));
        if (!p || *p < 0 || *p > 65535)
            return std::nullopt;
        a.port = static_cast<std::uint16_t>(*p);
        text = text.substr(0, colon);
    }
    const auto parts = str::split(text, '.');
    if (parts.size() != 4)
        return std::nullopt;
    for (auto part : parts) {
        if (part.empty() || part.size() > 3)
            return std::nullopt;
        for (char c : part)
            if (c < '0' || c > '9')
                return std::nullopt;
        const auto v = str::parseInt(part);
        if (!v || *v > 255)
            return std::nullopt;
        a.ip = (a.ip << 8) | static_cast<std::uint32_t>(*v);
    }
    return a;
}

std::optional<Address> Address::resolve(std::string_view text, std::uint16_t defaultPort) {
    if (auto a = parse(text, defaultPort))
        return a;
    text = str::trim(text);
    std::uint16_t port = defaultPort;
    std::string host(text);
    if (const auto colon = text.rfind(':'); colon != std::string_view::npos) {
        const auto p = str::parseInt(text.substr(colon + 1));
        if (!p || *p < 0 || *p > 65535)
            return std::nullopt;
        port = static_cast<std::uint16_t>(*p);
        host = std::string(text.substr(0, colon));
    }
    if (host.empty())
        return std::nullopt;
    NetLibrary lib;
    if (!lib.ok())
        return std::nullopt;
    ENetAddress ea{};
    if (enet_address_set_host(&ea, host.c_str()) != 0)
        return std::nullopt;
    return Address{ENET_NET_TO_HOST_32(ea.host), port};
}

std::string Address::ipString() const {
    return std::format("{}.{}.{}.{}", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
}

std::string Address::toString() const { return std::format("{}:{}", ipString(), port); }

bool Address::isPrivate() const {
    const std::uint32_t a = ip >> 24, b = (ip >> 16) & 0xFF;
    return a == 10 || a == 127 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) ||
           (a == 169 && b == 254) || (a == 100 && b >= 64 && b <= 127) || ip == 0;
}

namespace {

std::chrono::steady_clock::time_point clockStart() {
    static const auto start = std::chrono::steady_clock::now();
    return start;
}

} // namespace

std::uint64_t monotonicMs() {
    const auto start = clockStart();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

double monotonicMsPrecise() {
    const auto start = clockStart();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

} // namespace mm2::net
