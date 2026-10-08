#include "app/CommandLine.h"

#include "core/StringUtil.h"

#include <algorithm>
#include <array>
#include <format>
#include <string_view>

namespace mm2::app {
namespace {

// One option in midtown2.exe's syntax (datArgParser::Init): "-name" followed
// by its values, every word up to the next one starting with '-' and a
// non-digit (so "-width -5" gives -width the value "-5"); "-name=value" makes
// the part after '=' the first value.
struct MM2Option {
    std::string name;
    std::vector<std::string> values;
};

bool startsMM2Option(std::string_view a) {
    return a.size() > 1 && a[0] == '-' && !(a[1] >= '0' && a[1] <= '9');
}

MM2Option takeMM2Option(int argc, char** argv, int& i) {
    MM2Option o;
    std::string_view a = std::string_view(argv[i]).substr(1);
    if (const auto eq = a.find('='); eq != std::string_view::npos) {
        o.values.emplace_back(a.substr(eq + 1));
        a = a.substr(0, eq);
    }
    o.name = a;
    while (i + 1 < argc && !startsMM2Option(argv[i + 1]))
        o.values.emplace_back(argv[++i]);
    return o;
}

// Every option midtown2.exe reads (each datArgParser::Get call in build 3393)
// that OpenMM2 has no equivalent for. Device and driver choices (Direct3D,
// DirectDraw, DirectInput's IME, the archive and memory debugging switches)
// belong to plumbing OpenMM2 replaces; -level/-car (mmStatePack::SetDefaults'
// default city and car), -pedpool (aiCityData's pedestrian pool), -pvs
// (cityLevel::Load's PVS file name), -texframeskip (gfxGetTextureMovie),
// -nomipmap (gfxRenderState::Init), -tune_car (mmPlayer::Init keeps vpcop's
// own simulation) and -tune_ai (aiMap::Init) are development switches.
constexpr auto kMM2IgnoredOptions = std::to_array<std::string_view>({
    // Main, the archives, logging and memory debugging.
    "nolog", "nolockcheck", "nan", "ime", "noime", "archive", "checkalloc", "logopen", "config",
    // gfxPipeline::SetRes, gfxAutoDetect, gfxPipeline's vertex buffers, gfxRenderState::Init.
    "ref", "blade", "bladed", "swage", "sw", "sysmem", "triple", "nomt", "nohwtnl", "nomultitexture", "tex32",
    "primary", "display", "single", "cdepth", "zdepth", "nativevb", "nonativevb", "nomipmap",
    // Development switches.
    "texframeskip", "pvs", "pedpool", "level", "car", "tune_car", "tune_ai", "andyglasshack",
});

} // namespace

CommandLine parseCommandLine(int argc, char** argv) {
    CommandLine cl;
    std::vector<std::string> seenMM2; // datArgParser keeps the first of a repeated option
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
        } else if (startsMM2Option(a) && a[1] != '-') {
            MM2Option o = takeMM2Option(argc, argv, i);
            if (std::ranges::find(seenMM2, o.name) != seenMM2.end())
                continue;
            seenMM2.push_back(o.name);
            // datArgParser::Get(name, 0, int&): atoi of the first value; an
            // option without a value leaves the setting alone.
            auto number = [&](std::optional<int>& out) {
                if (o.values.empty())
                    return;
                auto n = str::parseInt(o.values[0]);
                if (!n || *n <= 0 || *n > 32768)
                    cl.error = std::format("-{}: invalid number '{}'", o.name, o.values[0]);
                else
                    out = static_cast<int>(*n);
            };
            if (o.name == "nomovie") {
                // Main: the logo movie plays unless -nomovie is given.
                cl.skipIntro = true;
            } else if (o.name == "window" || o.name == "max" || o.name == "fs" || o.name == "fullscreen") {
                // Resolved below, in the original's order.
            } else if (o.name == "width") {
                number(cl.width);
            } else if (o.name == "height") {
                number(cl.height);
            } else if (o.name == "novblank") {
                cl.vsync = false;
            } else if (o.name == "noaudio" || o.name == "nosoundfx") {
                // InitAudioManager: either leaves AudManager uninitialised,
                // so nothing plays (DirectMusic needs its DirectSound too).
                cl.noAudio = true;
            } else if (o.name == "nomusic") {
                // mmGameMusicData::Load loads neither the music nor the
                // city's ambience segment.
                cl.noMusic = true;
            } else if (o.name == "nospeech") {
                // mmPlayer::InitSpeechAudio returns before loading speech.
                cl.noSpeech = true;
            } else if (std::ranges::find(kMM2IgnoredOptions, o.name) != kMM2IgnoredOptions.end()) {
                cl.ignoredOptions.push_back("-" + o.name);
            } else {
                // datArgParser takes any "-word"; the game only ever asks
                // for the names above, so the rest do nothing.
                cl.unknownOptions.push_back("-" + o.name);
            }
        } else {
            cl.error = std::format("unknown option '{}'", a);
        }
    }
    // gfxPipeline::SetRes: -window runs in a window, else -max in a window
    // the size of the screen, else -fs or -fullscreen full screen; Main
    // plays the logo movie only when not in a window. OpenMM2's borderless
    // window stands in for -max.
    auto seen = [&](std::string_view n) { return std::ranges::find(seenMM2, n) != seenMM2.end(); };
    if (seen("window") || seen("max")) {
        cl.fullscreen = !seen("window");
        cl.skipIntro = true;
    } else if (seen("fs") || seen("fullscreen")) {
        cl.fullscreen = true;
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
           "  --version               print the version\n"
           "\n"
           "The original's options also work: -nomovie, -window, -max, -fs, -fullscreen,\n"
           "-width <px>, -height <px>, -novblank, -noaudio, -nosoundfx, -nomusic, -nospeech.\n";
}

} // namespace mm2::app
