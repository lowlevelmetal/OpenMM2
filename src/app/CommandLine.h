#pragma once

#include <optional>
#include <string>
#include <vector>

namespace mm2::app {

struct CommandLine {
    enum class Action { Run, CheckSource, ImportSource, Version, Help };

    Action action = Action::Run;
    std::vector<std::string> actionArgs; // operands of --check-source / --import-source

    bool forceSetup = false;           // --setup: show the first-run setup even if configured
    bool skipIntro = false;            // --skip-intro: don't play LOGOS.AVI
    std::optional<std::string> source; // --source <path>: game source for this run only
    std::optional<std::string> backend; // --backend vulkan|opengl
    std::optional<bool> fullscreen;    // --fullscreen / --windowed
    std::optional<int> width, height;  // --width / --height
    std::optional<std::string> logLevel; // --log-level trace|debug|info|warn|error
    std::optional<std::string> logFile;  // --log-file <path>
    std::optional<std::string> configPath; // --config <path>

    // Development: start a cruise in this city, skipping the menus.
    std::optional<std::string> quickstart; // --quickstart <city>

    // Automation (tests, screenshots for documentation):
    std::optional<int> frames;             // --frames N: exit after N frames
    std::optional<std::string> screenshot; // --screenshot <png>: capture the last frame

    std::string error; // set when parsing failed
};

CommandLine parseCommandLine(int argc, char** argv);
std::string commandLineHelp();

} // namespace mm2::app
