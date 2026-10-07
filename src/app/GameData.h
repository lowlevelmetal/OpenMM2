#pragma once

#include "vfs/GameSource.h"
#include "vfs/Vfs.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace mm2::app {

struct Settings;

// Result of validating a user-supplied game source.
struct SourceCheck {
    bool ok = false;
    std::optional<vfs::GameSource> source;
    std::string message; // one line, suitable for UI and installer output
};

SourceCheck checkGameSource(const std::filesystem::path& path);

// The game source to use: the per-user setting, else the installer default.
std::string configuredGameSource(const Settings& settings);

// Copies the game archives from `source` into `destDir` so the disc or image is
// no longer needed. `progress` receives 0..1 and may return false to cancel.
// Existing identical files (same size) are skipped. Writes are atomic per file.
bool importGameData(const vfs::GameSource& source, const std::filesystem::path& destDir,
                    const std::function<bool(double)>& progress, std::string* error = nullptr);

// Default import destination: <userDataDir>/gamedata.
std::filesystem::path defaultImportDir();

} // namespace mm2::app
