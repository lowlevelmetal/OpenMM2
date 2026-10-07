// netprobe: manual testing of OpenMM2 networking.
//
//   netprobe info                          LAN address, default routes, broadcast addresses
//   netprobe host [--port N] [--name S] [--password P] [--upnp] [--seconds N]
//   netprobe join <host[:port]> [--name S] [--password P] [--chat TEXT] [--seconds N]
//   netprobe scan [--port N] [--seconds N] [--passive]
//   netprobe portmap [--port N] [--seconds N] [--discover-only] [--no-upnp] [--no-natpmp]
//
// --seconds 0 runs until Ctrl-C. Port mappings are removed on exit.
#include "core/Log.h"
#include "core/StringUtil.h"
#include "net/Discovery.h"
#include "net/PortMapper.h"
#include "net/Session.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <map>
#include <print>
#include <thread>

using namespace mm2;
using namespace mm2::net;

namespace {

std::atomic<bool> g_quit{false};

struct Args {
    std::vector<std::string> positional;
    std::map<std::string, std::string> options;

    bool flag(const std::string& name) const { return options.contains(name); }
    std::string get(const std::string& name, const std::string& fallback = {}) const {
        const auto it = options.find(name);
        return it == options.end() ? fallback : it->second;
    }
    int getInt(const std::string& name, int fallback) const {
        const auto v = str::parseInt(get(name));
        return v ? static_cast<int>(*v) : fallback;
    }
};

Args parseArgs(int argc, char** argv, int first) {
    static const char* kValueOptions[] = {"--port", "--name", "--password", "--seconds", "--chat", "--car"};
    Args a;
    for (int i = first; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.starts_with("--")) {
            const bool takesValue = std::ranges::any_of(kValueOptions, [&](const char* o) { return arg == o; });
            a.options[arg] = takesValue && i + 1 < argc ? argv[++i] : "";
        } else {
            a.positional.push_back(arg);
        }
    }
    return a;
}

bool keepRunning(std::chrono::steady_clock::time_point start, int seconds) {
    if (g_quit)
        return false;
    return seconds <= 0 || std::chrono::steady_clock::now() - start < std::chrono::seconds(seconds);
}

void printEvent(const Session& s, const SessionEvent& e) {
    auto name = [&](std::uint8_t id) {
        const PlayerInfo* p = s.player(id);
        return p ? p->name : std::format("#{}", id);
    };
    std::visit(
        [&](const auto& ev) {
            using T = std::decay_t<decltype(ev)>;
            if constexpr (std::is_same_v<T, ev::JoinAccepted>)
                std::println("joined as player {} ({} players)", ev.localId, s.players().size());
            else if constexpr (std::is_same_v<T, ev::JoinFailed>)
                std::println("join failed: {}", ev.message);
            else if constexpr (std::is_same_v<T, ev::Disconnected>)
                std::println("disconnected: {}", ev.message);
            else if constexpr (std::is_same_v<T, ev::PlayerJoined>)
                std::println("+ {} joined (car {}, colour {})", ev.player.name, ev.player.car, ev.player.color);
            else if constexpr (std::is_same_v<T, ev::PlayerLeft>)
                std::println("- {} left: {}", ev.player.name, describe(ev.reason));
            else if constexpr (std::is_same_v<T, ev::PlayerUpdated>)
                std::println("* {}: car {}, colour {}, {}", ev.player.name, ev.player.car, ev.player.color,
                             ev.player.ready ? "ready" : "not ready");
            else if constexpr (std::is_same_v<T, ev::Chat>)
                std::println("<{}> {}", name(ev.from), ev.text);
            else if constexpr (std::is_same_v<T, ev::SettingsChanged>)
                std::println("settings: city {}, mode {}", ev.settings.city, static_cast<int>(ev.settings.mode));
            else if constexpr (std::is_same_v<T, ev::CountdownStarted>)
                std::println("countdown: race starts at session time {} ms", ev.startTime);
            else if constexpr (std::is_same_v<T, ev::GameStarted>)
                std::println("game started");
            else if constexpr (std::is_same_v<T, ev::ReturnedToLobby>)
                std::println("back in the lobby");
            else if constexpr (std::is_same_v<T, ev::GameEvent>)
                std::println("game event {} from {} ({} bytes)", ev.type, name(ev.from), ev.payload.size());
        },
        e);
}

void printStatus(const PortMappingStatus& s) {
    std::println("[portmap] {}", s.message);
    if (s.state == PortMappingStatus::State::Failed || s.state == PortMappingStatus::State::Mapped)
        for (const auto& d : s.details)
            std::println("[portmap]   {}", d);
}

int cmdInfo() {
    NetLibrary lib;
    const std::uint32_t lan = primaryLocalAddress();
    std::println("LAN address: {}", lan ? Address{lan, 0}.ipString() : std::string("(no route)"));
    std::println("broadcast addresses:");
    for (std::uint32_t b : broadcastAddresses())
        std::println("  {}", Address{b, 0}.ipString());
    return 0;
}

int cmdHost(const Args& a) {
    HostParams p;
    p.bind = Address::any(static_cast<std::uint16_t>(a.getInt("--port", kDefaultGamePort)));
    p.player.name = a.get("--name", "Host");
    p.player.car = a.get("--car", "vpbug");
    p.password = a.get("--password");
    p.settings.name = p.player.name + "'s session";
    Session s;
    std::string err;
    if (!s.host(p, &err)) {
        std::println(stderr, "host failed: {}", err);
        return 1;
    }
    std::println("hosting on UDP {} (LAN discovery on {})", s.port(), kDefaultDiscoveryPort);

    PortMapper mapper;
    if (a.flag("--upnp")) {
        mapper.setCallback(printStatus);
        PortMapperConfig cfg;
        cfg.internalPort = s.port();
        mapper.start(cfg);
    }
    const auto start = std::chrono::steady_clock::now();
    while (keepRunning(start, a.getInt("--seconds", 0))) {
        s.update();
        for (const auto& e : s.takeEvents())
            printEvent(s, e);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    s.leave();
    mapper.stop();
    return 0;
}

int cmdJoin(const Args& a) {
    if (a.positional.empty()) {
        std::println(stderr, "usage: netprobe join <host[:port]>");
        return 2;
    }
    const auto addr = Address::resolve(a.positional[0], kDefaultGamePort);
    if (!addr) {
        std::println(stderr, "cannot resolve '{}'", a.positional[0]);
        return 1;
    }
    JoinParams p;
    p.host = *addr;
    p.player.name = a.get("--name", "Player");
    p.player.car = a.get("--car", "vpbug");
    p.password = a.get("--password");
    Session s;
    std::string err;
    if (!s.join(p, &err)) {
        std::println(stderr, "join failed: {}", err);
        return 1;
    }
    const std::string chat = a.get("--chat");
    bool chatted = false;
    const auto start = std::chrono::steady_clock::now();
    while (keepRunning(start, a.getInt("--seconds", 0)) && s.state() != Session::State::Closed) {
        s.update();
        for (const auto& e : s.takeEvents())
            printEvent(s, e);
        if (!chat.empty() && !chatted && s.state() == Session::State::Active) {
            s.sendChat(chat);
            chatted = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (s.state() == Session::State::Active) {
        const auto st = s.peerStats(kHostPlayerId);
        std::println("rtt {} ms, loss {:.1f}%, clock offset synced: {}", st.rttMs, st.packetLoss * 100.0f,
                     s.clockSynced());
    }
    s.leave();
    return 0;
}

int cmdScan(const Args& a) {
    LanScanner scanner;
    std::string err;
    // Passive listening shares the discovery port with local hosts; on Linux a
    // unicast query to 127.0.0.1 then lands in whichever socket bound last, so
    // it is opt-in here to keep same-machine discovery reliable.
    if (!scanner.start(static_cast<std::uint16_t>(a.getInt("--port", kDefaultDiscoveryPort)), a.flag("--passive"),
                       &err)) {
        std::println(stderr, "scan failed: {}", err);
        return 1;
    }
    scanner.addTarget(Address::loopback(0));
    const int seconds = a.getInt("--seconds", 3);
    const auto start = std::chrono::steady_clock::now();
    auto lastScan = start - std::chrono::seconds(10);
    while (keepRunning(start, seconds)) {
        if (std::chrono::steady_clock::now() - lastScan > std::chrono::seconds(1)) {
            scanner.scan();
            lastScan = std::chrono::steady_clock::now();
        }
        scanner.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const auto sessions = scanner.sessions();
    std::println("{} session(s) found", sessions.size());
    for (const auto& s : sessions)
        std::println("  {:<24} {:<21} {}/{} players  {}  ping {} ms{}{}", s.advert.sessionName, s.address.toString(),
                     s.advert.players, s.advert.maxPlayers, s.advert.city, s.pingMs,
                     s.advert.hasPassword ? "  [password]" : "", s.compatible() ? "" : "  [incompatible]");
    return 0;
}

int cmdPortmap(const Args& a) {
    const auto port = static_cast<std::uint16_t>(a.getInt("--port", kDefaultGamePort));
    if (a.flag("--discover-only")) {
        // Read-only: finds the gateway and its external address, maps nothing.
        int found = 0;
        auto probe = [&](std::unique_ptr<PortMappingBackend> b, const char* label) {
            GatewayInfo gw;
            std::string err;
            const auto t0 = std::chrono::steady_clock::now();
            const bool ok = b->discover(2500, gw, err);
            const auto ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
            if (ok) {
                ++found;
                std::println("{}: {} (LAN {}, external {}{}) [{} ms]", label, gw.description, gw.internalIp,
                             gw.externalIp.empty() ? "unknown" : gw.externalIp, gw.doubleNat ? ", DOUBLE NAT" : "", ms);
            } else {
                std::println("{}: {} [{} ms]", label, err, ms);
            }
        };
        if (!a.flag("--no-upnp"))
            probe(makeUpnpBackend(), "UPnP");
        if (!a.flag("--no-natpmp"))
            probe(makeNatPmpBackend(), "PCP/NAT-PMP");
        return found ? 0 : 1;
    }
    PortMapper mapper;
    mapper.setCallback(printStatus);
    PortMapperConfig cfg;
    cfg.internalPort = port;
    cfg.enableUpnp = !a.flag("--no-upnp");
    cfg.enableNatPmp = !a.flag("--no-natpmp");
    mapper.start(cfg);
    const auto start = std::chrono::steady_clock::now();
    while (keepRunning(start, a.getInt("--seconds", 10)))
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    mapper.stop();
    return mapper.status().state == PortMappingStatus::State::Failed ? 1 : 0;
}

int usage() {
    std::println(stderr, "usage: netprobe <info|host|join|scan|portmap> [options]\n"
                         "  info\n"
                         "  host    [--port N] [--name S] [--car S] [--password P] [--upnp] [--seconds N]\n"
                         "  join    <host[:port]> [--name S] [--car S] [--password P] [--chat TEXT] [--seconds N]\n"
                         "  scan    [--port N] [--seconds N] [--passive]\n"
                         "  portmap [--port N] [--seconds N] [--discover-only] [--no-upnp] [--no-natpmp]");
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    log::setLevel(log::Level::Warn);
    std::signal(SIGINT, [](int) { g_quit = true; });
    int first = 1;
    if (argc > 1 && std::string_view(argv[1]) == "-v") {
        log::setLevel(log::Level::Debug);
        ++first;
    }
    if (argc <= first)
        return usage();
    const std::string cmd = argv[first];
    const Args args = parseArgs(argc, argv, first + 1);
    if (cmd == "info")
        return cmdInfo();
    if (cmd == "host")
        return cmdHost(args);
    if (cmd == "join")
        return cmdJoin(args);
    if (cmd == "scan")
        return cmdScan(args);
    if (cmd == "portmap")
        return cmdPortmap(args);
    return usage();
}
