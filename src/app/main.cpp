// OpenMM2 entry point.
#include "app/App.h"
#include "app/CommandLine.h"
#include "app/GameData.h"
#include "core/Log.h"
#include "core/StringUtil.h"
#include "core/Version.h"

#include <SDL3/SDL_main.h>

#include <cstdio>
#include <print>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace mm2;

namespace {

#ifdef _WIN32
// The game is a GUI-subsystem program. When started from a console, attach to
// it so --help and friends are visible; when the parent redirected our output
// (e.g. the installer capturing --check-source), keep the inherited handles.
void attachParentConsole() {
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != nullptr && out != INVALID_HANDLE_VALUE && GetFileType(out) != FILE_TYPE_UNKNOWN)
        return;
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }
}
#endif

int checkSource(const std::string& path) {
    const auto r = app::checkGameSource(str::toPath(path));
    std::println("{}", r.message);
    return r.ok ? 0 : 1;
}

int importSource(const std::vector<std::string>& args) {
    const auto check = app::checkGameSource(str::toPath(args[0]));
    if (!check.ok) {
        std::println("{}", check.message);
        return 1;
    }
    const auto dest = args.size() > 1 ? str::toPath(args[1]) : app::defaultImportDir();
    int lastPercent = -1;
    std::string error;
    const bool ok = app::importGameData(*check.source, dest,
                                        [&](double p) {
                                            const int percent = static_cast<int>(p * 100.0);
                                            if (percent != lastPercent) {
                                                lastPercent = percent;
                                                std::println("progress {}", percent);
                                                std::fflush(stdout);
                                            }
                                            return true;
                                        },
                                        &error);
    if (!ok) {
        std::println("import failed: {}", error);
        return 1;
    }
    std::println("imported to {}", str::fromPath(dest));
    return 0;
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    attachParentConsole();
#endif
    const app::CommandLine cl = app::parseCommandLine(argc, argv);
    if (!cl.error.empty()) {
        std::println(stderr, "openmm2: {}\n\n{}", cl.error, app::commandLineHelp());
        return 2;
    }
    if (cl.logLevel) {
        log::Level level;
        if (log::parseLevel(*cl.logLevel, level))
            log::setLevel(level);
    }

    switch (cl.action) {
    case app::CommandLine::Action::Help: std::print("{}", app::commandLineHelp()); return 0;
    case app::CommandLine::Action::Version: std::println("{} {}", kProjectName, kProjectVersion); return 0;
    case app::CommandLine::Action::CheckSource:
        log::setLevel(log::Level::Error);
        return checkSource(cl.actionArgs.at(0));
    case app::CommandLine::Action::ImportSource:
        log::setLevel(log::Level::Error);
        return importSource(cl.actionArgs);
    case app::CommandLine::Action::Run: break;
    }

    if (cl.logFile)
        log::setFile(str::toPath(*cl.logFile));
    return app::run(cl);
}
