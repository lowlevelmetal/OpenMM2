#include "app/Settings.h"

#include "core/Paths.h"
#include "core/StringUtil.h"

#include <algorithm>

namespace mm2::app {
namespace {

float volume(const IniFile& ini, const char* key, float fallback) {
    return std::clamp(static_cast<float>(ini.getDouble("Audio", key, fallback)), 0.0f, 1.0f);
}

} // namespace

void Settings::load(const std::filesystem::path& path) {
    *this = Settings{};
    ini.load(path);
    gameSource = ini.getString("GameData", "Source");
    masterVolume = volume(ini, "Master", masterVolume);
    effectsVolume = volume(ini, "Effects", effectsVolume);
    engineVolume = volume(ini, "Engine", engineVolume);
    ambientVolume = volume(ini, "Ambient", ambientVolume);
    voiceVolume = volume(ini, "Voice", voiceVolume);
    musicVolume = volume(ini, "Music", musicVolume);
    audioHighQuality = ini.getBool("Audio", "HighQuality", audioHighQuality);
    playerName = ini.getString("Network", "PlayerName", playerName);
    port = static_cast<int>(std::clamp<long long>(ini.getInt("Network", "Port", port), 0, 65535));
    upnp = ini.getBool("Network", "UPnP", upnp);
    metricUnits = ini.getBool("Game", "MetricUnits", metricUnits);
}

bool Settings::save(const std::filesystem::path& path) {
    ini.set("GameData", "Source", gameSource);
    ini.setDouble("Audio", "Master", masterVolume);
    ini.setDouble("Audio", "Effects", effectsVolume);
    ini.setDouble("Audio", "Engine", engineVolume);
    ini.setDouble("Audio", "Ambient", ambientVolume);
    ini.setDouble("Audio", "Voice", voiceVolume);
    ini.setDouble("Audio", "Music", musicVolume);
    ini.setBool("Audio", "HighQuality", audioHighQuality);
    ini.set("Network", "PlayerName", playerName);
    ini.setInt("Network", "Port", port);
    ini.setBool("Network", "UPnP", upnp);
    ini.setBool("Game", "MetricUnits", metricUnits);
    return ini.save(path);
}

std::filesystem::path Settings::defaultPath() { return paths::userConfigDir() / "openmm2.ini"; }

std::filesystem::path installDefaultsPath() { return paths::executableDir() / "openmm2-install.ini"; }

std::string installDefaultGameSource() {
    IniFile ini;
    if (!ini.load(installDefaultsPath()))
        return {};
    return ini.getString("GameData", "Source");
}

} // namespace mm2::app
