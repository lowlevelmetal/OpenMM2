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

    // [Audio] linear volumes 0..1. The Options > Audio page sets effects,
    // engine and voice together (SOUND FX VOLUME) and music and ambient
    // together (MUSIC/CITY VOLUME), MM2's defaults being 1 for both
    // (AudioOptions::ResetDefaultAction).
    float masterVolume = 1.0f;
    float effectsVolume = 1.0f;
    float engineVolume = 1.0f;
    float ambientVolume = 1.0f;
    float voiceVolume = 1.0f;
    float musicVolume = 1.0f;
    // The audio toggles, with MM2's start-up defaults (mmStatePack::SetDefaults
    // audio flags 0xc73): music off and city sounds on; MM2 never has both on.
    bool soundEffects = true; // flag 0x1
    bool commentary = true;   // flag 0x400
    bool music = false;       // flag 0x4: interactive music
    bool citySounds = true;   // flag 0x800: city ambience
    bool stereo = true;       // STEREO FX: Mono / Stereo (flag 0x40); stored only
    // SOUND QUALITY 0 Low, 1 Medium, 2 High. MM2 chooses 8/16/32 voices;
    // OpenMM2 approximates it with the 11 kHz (Low) or 22 kHz sounds.
    int soundQuality = 2;
    bool audioHighQuality = true; // 22 kHz sounds; derived from soundQuality >= 1
    float balance = 0.0f;         // BALANCE, -1 (left) .. 1 (right)

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
