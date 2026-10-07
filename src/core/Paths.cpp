#include "core/Paths.h"

#include <cstdlib>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <unistd.h>
#endif

namespace mm2::paths {
namespace {

std::filesystem::path ensureDir(std::filesystem::path p) {
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p;
}

#ifdef _WIN32
std::filesystem::path knownFolder(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &raw)))
        result = raw;
    CoTaskMemFree(raw);
    return result;
}
#else
std::filesystem::path xdgDir(const char* var, const char* fallbackRelHome) {
    if (const char* v = std::getenv(var); v && *v && std::filesystem::path(v).is_absolute())
        return v;
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / fallbackRelHome;
    return std::filesystem::temp_directory_path();
}
#endif

} // namespace

std::filesystem::path executableDir() {
#ifdef _WIN32
    std::vector<wchar_t> buf(MAX_PATH);
    while (true) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0)
            return std::filesystem::current_path();
        if (n < buf.size())
            return std::filesystem::path(std::wstring(buf.data(), n)).parent_path();
        buf.resize(buf.size() * 2);
    }
#else
    std::error_code ec;
    auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec)
        return std::filesystem::current_path();
    return exe.parent_path();
#endif
}

bool isPortable() {
    static const bool portable = [] {
        std::error_code ec;
        return std::filesystem::exists(executableDir() / "portable.txt", ec);
    }();
    return portable;
}

std::filesystem::path userConfigDir() {
    if (isPortable())
        return ensureDir(executableDir() / "user");
#ifdef _WIN32
    return ensureDir(knownFolder(FOLDERID_RoamingAppData) / "OpenMM2");
#else
    return ensureDir(xdgDir("XDG_CONFIG_HOME", ".config") / "openmm2");
#endif
}

std::filesystem::path userDataDir() {
    if (isPortable())
        return ensureDir(executableDir() / "user");
#ifdef _WIN32
    return ensureDir(knownFolder(FOLDERID_LocalAppData) / "OpenMM2");
#else
    return ensureDir(xdgDir("XDG_DATA_HOME", ".local/share") / "openmm2");
#endif
}

} // namespace mm2::paths
