#include "app/CommandLine.h"

#include "core/StringUtil.h"

#include <format>
#include <string_view>

namespace mm2::app {

CommandLine parseCommandLine(int argc, char** argv) {
    CommandLine cl;
    auto need = [&](int& i, std::string_view opt) -> std::optional<std::string> {
        if (i + 1 >= argc) {
            cl.error = std::format("{} needs a value", opt);
            return std::nullopt;
        }
        return std::string(argv[++i]);
    };
    auto needInt = [&](int& i, std::string_view opt) -> std::optional<int> {
        auto v = need(i, opt);
        if (!v)
            return std::nullopt;
        auto n = str::parseInt(*v);
        if (!n || *n <= 0 || *n > 32768) {
            cl.error = std::format("{}: invalid number '{}'", opt, *v);
            return std::nullopt;
        }
        return static_cast<int>(*n);
    };

    for (int i = 1; i < argc && cl.error.empty(); ++i) {
        const std::string_view a = argv[i];
        if (a == "--check-source") {
            cl.action = CommandLine::Action::CheckSource;
            if (auto v = need(i, a))
                cl.actionArgs = {*v};
        } else if (a == "--import-source") {
            cl.action = CommandLine::Action::ImportSource;
            if (auto src = need(i, a)) {
                cl.actionArgs = {*src};
                // Destination is optional; defaults to the per-user data directory.
                if (i + 1 < argc && !std::string_view(argv[i + 1]).starts_with("--"))
                    cl.actionArgs.emplace_back(argv[++i]);
            }
        } else if (a == "--version" || a == "-V") {
            cl.action = CommandLine::Action::Version;
        } else if (a == "--help" || a == "-h" || a == "/?") {
            cl.action = CommandLine::Action::Help;
        } else if (a == "--setup") {
            cl.forceSetup = true;
        } else if (a == "--skip-intro") {
            cl.skipIntro = true;
        } else if (a == "--source") {
            cl.source = need(i, a);
        } else if (a == "--backend") {
            cl.backend = need(i, a);
            if (cl.backend && *cl.backend != "vulkan" && *cl.backend != "opengl" && *cl.backend != "auto")
                cl.error = std::format("--backend: expected vulkan, opengl or auto, got '{}'", *cl.backend);
        } else if (a == "--fullscreen") {
            cl.fullscreen = true;
        } else if (a == "--windowed") {
            cl.fullscreen = false;
        } else if (a == "--width") {
            cl.width = needInt(i, a);
        } else if (a == "--height") {
            cl.height = needInt(i, a);
        } else if (a == "--log-level") {
            cl.logLevel = need(i, a);
        } else if (a == "--log-file") {
            cl.logFile = need(i, a);
        } else if (a == "--config") {
            cl.configPath = need(i, a);
        } else if (a == "--quickstart") {
            cl.quickstart = need(i, a);
        } else if (a == "--frames") {
            cl.frames = needInt(i, a);
        } else if (a == "--screenshot") {
            cl.screenshot = need(i, a);
        } else if (a.starts_with("-psn_")) {
            // macOS Finder adds a process serial number argument; ignore it.
        } else {
            cl.error = std::format("unknown option '{}'", a);
        }
    }
    return cl;
}

std::string commandLineHelp() {
    return "usage: openmm2 [options]\n"
           "\n"
           "  --setup                 show the game data setup screen\n"
           "  --skip-intro            don't play the intro movie\n"
           "  --source <path>         use this game source (disc image, disc or folder) for this run\n"
           "  --backend <name>        renderer: auto (default), vulkan or opengl\n"
           "  --fullscreen | --windowed\n"
           "  --width <px> --height <px>\n"
           "  --config <file>         settings file (default: per-user openmm2.ini)\n"
           "  --log-level <level>     trace, debug, info, warn or error\n"
           "  --log-file <file>       also write the log to a file\n"
           "  --quickstart <city>     drive straight into a cruise (london, sf)\n"
           "  --frames <n>            quit after n frames (automation)\n"
           "  --screenshot <file>     save the last frame as PNG (with --frames)\n"
           "\n"
           "  --check-source <path>   validate a game source; exit status 0 when usable\n"
           "  --import-source <path> [dir]\n"
           "                          copy the game archives from a disc/image/folder to dir\n"
           "                          (default: per-user data directory); prints 'progress N'\n"
           "  --version               print the version\n";
}

} // namespace mm2::app
