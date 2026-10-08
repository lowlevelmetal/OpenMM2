#include "vfs/GameSource.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "vfs/DaveArchive.h"
#include "vfs/IsoImage.h"

#include <algorithm>
#include <cstdlib>
#include <format>

#ifdef _WIN32
#include <windows.h>
#endif

namespace mm2::vfs {
namespace {

void setError(std::string* error, std::string msg) {
    if (error)
        *error = std::move(msg);
}

bool isRequiredArchive(std::string_view name) {
    return std::ranges::any_of(kRequiredArchives, [&](const char* r) { return str::iequals(name, r); });
}

// zipMultiAutoInit: the archive paths are upper-cased (_strupr) and sorted
// with strcmp; zipFile objects are then created from the last to the first,
// each pushed onto the front of the list zipFile::zipOpen searches, so the
// search runs in sorted order and the first archive that has a file wins.
// All archives share one folder, so this is the order of their names.
void sortLikeMM2(std::vector<std::string>& names) {
    std::ranges::sort(names, [](const std::string& a, const std::string& b) {
        return str::upper(a) < str::upper(b);
    });
}

// Case-insensitive lookup of a direct child of `dir`.
std::optional<std::filesystem::path> findChild(const std::filesystem::path& dir, std::string_view name) {
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (str::iequals(str::fromPath(it->path().filename()), name))
            return it->path();
    return std::nullopt;
}

// Every *.ar file directly inside `dir`, in MM2's search order.
std::vector<std::string> listArchives(const std::filesystem::path& dir) {
    std::vector<std::string> names;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec))
            continue;
        const std::string name = str::fromPath(it->path().filename());
        if (str::iendsWith(name, ".ar"))
            names.push_back(name);
    }
    sortLikeMM2(names);
    return names;
}

void fillMissing(GameSource& s) {
    s.missing.clear();
    for (const char* req : kRequiredArchives) {
        const bool found =
            std::ranges::any_of(s.archives, [&](const std::string& a) { return str::iequals(a, req); });
        if (!found)
            s.missing.emplace_back(req);
    }
}

std::optional<GameSource> probeImage(const std::filesystem::path& path, std::string* error) {
    std::string isoError;
    auto iso = IsoImage::open(path, &isoError);
    if (!iso) {
        setError(error, std::format("'{}' is not a game folder or a readable disc image ({})", str::fromPath(path),
                                    isoError));
        return std::nullopt;
    }
    GameSource s;
    s.kind = GameSource::Kind::DiscImage;
    s.path = path;
    s.volumeId = iso->volumeId();
    iso->forEachFile([&](const EntryInfo& e) {
        if (!e.path.starts_with("game/") || !str::iendsWith(e.path, ".ar") ||
            e.path.find('/', 5) != std::string::npos)
            return;
        s.archives.push_back(str::upper(e.path.substr(5)));
    });
    sortLikeMM2(s.archives);
    fillMissing(s);
    return s;
}

std::shared_ptr<const RandomAccessFile> openGameDirFile(const GameSource& source, const IsoImage* iso,
                                                        const std::string& name) {
    if (iso)
        return iso->open(std::string_view("game/" + name));
    const std::filesystem::path dir =
        source.kind == GameSource::Kind::DiscDirectory ? findChild(source.path, "GAME").value_or(source.path / "GAME")
                                                       : source.path;
    if (auto p = findChild(dir, name))
        return OsFile::open(*p);
    return nullptr;
}

} // namespace

std::string GameSource::describe() const {
    switch (kind) {
    case Kind::DiscImage:
        return std::format("disc image {}{}", str::fromPath(path), volumeId.empty() ? "" : " (" + volumeId + ")");
    case Kind::DiscDirectory: return std::format("game disc at {}", str::fromPath(path));
    case Kind::InstallDirectory: return std::format("installation at {}", str::fromPath(path));
    }
    return {};
}

std::optional<GameSource> probeGameSource(const std::filesystem::path& input, std::string* error) {
    std::error_code ec;
    std::filesystem::path path = input;
    if (!std::filesystem::exists(path, ec)) {
        setError(error, std::format("'{}' does not exist", str::fromPath(path)));
        return std::nullopt;
    }

    // Pointing at one of the archives (or the game executable) means its folder.
    if (!std::filesystem::is_directory(path, ec)) {
        const std::string name = str::fromPath(path.filename());
        if (str::iendsWith(name, ".ar") || str::iendsWith(name, ".exe"))
            path = path.parent_path();
    }

    if (std::filesystem::is_directory(path, ec)) {
        GameSource s;
        s.path = path;
        if (auto game = findChild(path, "GAME"); game && findChild(*game, "MM2CORE.AR")) {
            s.kind = GameSource::Kind::DiscDirectory;
            s.archives = listArchives(*game);
        } else {
            s.kind = GameSource::Kind::InstallDirectory;
            s.archives = listArchives(path);
        }
        if (s.archives.empty()) {
            setError(error, std::format("no Midtown Madness 2 archives (MM2CORE.AR etc.) in '{}'", str::fromPath(path)));
            return std::nullopt;
        }
        fillMissing(s);
        return s;
    }
    return probeImage(path, error);
}

std::shared_ptr<const RandomAccessFile> openSourceFile(const GameSource& source, const std::string& name,
                                                          std::string* error) {
    std::shared_ptr<IsoImage> iso;
    if (source.kind == GameSource::Kind::DiscImage) {
        iso = IsoImage::open(source.path, error);
        if (!iso)
            return nullptr;
    }
    auto file = openGameDirFile(source, iso.get(), name);
    if (!file)
        setError(error, std::format("{} not found in {}", name, source.describe()));
    return file;
}

bool mountGameSource(Vfs& vfs, const GameSource& source, std::string* error) {
    std::shared_ptr<IsoImage> iso;
    if (source.kind == GameSource::Kind::DiscImage) {
        iso = IsoImage::open(source.path, error);
        if (!iso)
            return false;
    }

    // Among equal priorities the Vfs prefers the most recent mount, so the
    // archives go in from the last in MM2's search order to the first (as
    // zipMultiAutoInit creates them). An archive that cannot be opened is
    // left out, as zipFile::Init failing drops it from the list.
    for (auto it = source.archives.rbegin(); it != source.archives.rend(); ++it) {
        const std::string& name = *it;
        auto file = openGameDirFile(source, iso.get(), name);
        std::string arError;
        auto ar = file ? DaveArchive::open(file, name, &arError) : nullptr;
        if (!ar) {
            if (isRequiredArchive(name)) {
                setError(error, std::format("cannot read {}: {}", name, file ? arError : "file not found"));
                return false;
            }
            log::warn("vfs: skipping {}: {}", name, file ? arError : "file not found");
            continue;
        }
        vfs.mount(std::move(ar), 0);
        log::info("vfs: mounted {}", name);
    }
    return true;
}

std::vector<std::filesystem::path> suggestGameSources() {
    std::vector<std::filesystem::path> candidates;
    std::error_code ec;
    auto consider = [&](const std::filesystem::path& p) {
        if (std::ranges::find(candidates, p) != candidates.end())
            return;
        if (auto s = probeGameSource(p); s && s->usable())
            candidates.push_back(p);
    };

#ifdef _WIN32
    const DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(drives & (1u << i)))
            continue;
        const wchar_t root[] = {static_cast<wchar_t>(L'A' + i), L':', L'\\', 0};
        if (GetDriveTypeW(root) == DRIVE_CDROM)
            consider(root);
    }
    for (const char* var : {"ProgramFiles", "ProgramFiles(x86)"}) {
        if (const char* pf = std::getenv(var)) {
            consider(std::filesystem::path(pf) / "Microsoft Games" / "Midtown Madness 2");
            consider(std::filesystem::path(pf) / "Midtown Madness 2");
        }
    }
#else
    std::vector<std::filesystem::path> mountRoots = {"/media", "/mnt"};
    if (const char* user = std::getenv("USER")) {
        mountRoots.push_back(std::filesystem::path("/run/media") / user);
        mountRoots.push_back(std::filesystem::path("/media") / user);
    }
    for (const auto& root : mountRoots)
        for (std::filesystem::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
            if (it->is_directory(ec))
                consider(it->path());
    if (const char* home = std::getenv("HOME")) {
        const std::filesystem::path h(home);
        for (const char* sub : {".wine/drive_c/Program Files/Microsoft Games/Midtown Madness 2",
                                ".wine/drive_c/Program Files (x86)/Microsoft Games/Midtown Madness 2"})
            consider(h / sub);
        // Disc images lying around in the usual places.
        for (const char* dir : {"Downloads", "Games", "."}) {
            for (std::filesystem::directory_iterator it(h / dir, ec), end; !ec && it != end; it.increment(ec)) {
                const std::string name = str::lower(str::fromPath(it->path().filename()));
                if ((name.ends_with(".iso") || name.ends_with(".cue")) &&
                    (name.find("midtown") != std::string::npos || name.find("mm2") != std::string::npos))
                    consider(it->path());
            }
        }
    }
    for (const char* dev : {"/dev/sr0", "/dev/sr1", "/dev/cdrom"})
        if (std::filesystem::exists(dev, ec))
            consider(dev);
#endif
    return candidates;
}

} // namespace mm2::vfs
