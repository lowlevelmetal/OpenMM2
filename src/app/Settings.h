#pragma once

#include "core/Ini.h"

#include <filesystem>
#include <string>

namespace mm2::app {

// User settings, persisted to <userConfigDir>/openmm2.ini. Sections owned by
// other subsystems (e.g. [Display]) are kept in `ini` and read/written by
// those subsystems, so unknown keys survive a load/save round trip.
struct Settings {
    // [GameData]
    std::string gameSource; // disc image, disc path or install/import directory

    // [Audio] linear volumes 0..1
    float masterVolume = 1.0f;
    float effectsVolume = 1.0f;
    float engineVolume = 1.0f;
    float ambientVolume = 1.0f;
    float voiceVolume = 1.0f;
    float musicVolume = 0.8f;
    bool audioHighQuality = true; // 22 kHz sounds; false = 11 kHz, like the original option

    // [Network]
    std::string playerName = "Player";
    int port = 0; // 0 = default game port
    bool upnp = true;

    // [Game]
    bool metricUnits = false;

    IniFile ini;

    // Loads `path`, falling back to defaults for anything missing.
    void load(const std::filesystem::path& path);
    bool save(const std::filesystem::path& path);

    static std::filesystem::path defaultPath();
};

// The Windows installer writes "<install dir>/openmm2-install.ini" with the
// game source the user picked; it is used when the per-user settings have
// no source yet.
std::filesystem::path installDefaultsPath();
std::string installDefaultGameSource();

} // namespace mm2::app
