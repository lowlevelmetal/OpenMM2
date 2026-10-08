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

    // The original's own options (datArgParser: "-name", "-name=value"),
    // accepted so shortcuts made for midtown2.exe keep working. -nomovie,
    // -window, -max, -fs/-fullscreen, -width and -height set the fields above;
    // these are the rest of what OpenMM2 can honour, for this run only.
    bool noAudio = false;       // -noaudio, -nosoundfx: no sound at all
    bool noMusic = false;       // -nomusic: no music, no city ambience segment
    bool noSpeech = false;      // -nospeech: no announcer
    std::optional<bool> vsync;  // -novblank: false
    // -pedpool <n>: the pedestrian pool, read by aiCityData's ctor after the
    // city's [Ped Pool] (datArgParser::Get, atoi of the first value), so it
    // wins over the file; aiMap::Init then takes trunc(pool x density).
    std::optional<int> pedPool;
    // MM2 options with no OpenMM2 equivalent (Direct3D device choices,
    // development switches), and single-dash words the original would not
    // have read either; both are logged at start-up and otherwise ignored.
    std::vector<std::string> ignoredOptions;
    std::vector<std::string> unknownOptions;

    std::string error; // set when parsing failed
};

CommandLine parseCommandLine(int argc, char** argv);
std::string commandLineHelp();

} // namespace mm2::app
