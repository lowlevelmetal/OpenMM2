#include "app/GameData.h"

#include "app/Settings.h"
#include "core/File.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/StringUtil.h"

#include <format>
#include <fstream>
#include <random>

namespace mm2::app {
namespace {

void setError(std::string* error, std::string msg) {
    if (error)
        *error = std::move(msg);
}

std::string joinNames(const std::vector<std::string>& names) {
    std::string out;
    for (const auto& n : names)
        out += (out.empty() ? "" : ", ") + n;
    return out;
}

} // namespace

SourceCheck checkGameSource(const std::filesystem::path& path) {
    SourceCheck r;
    std::string error;
    r.source = vfs::probeGameSource(path, &error);
    if (!r.source) {
        r.message = error;
        return r;
    }
    if (!r.source->usable()) {
        r.message = std::format("{} is missing {}", r.source->describe(), joinNames(r.source->missing));
        return r;
    }
    // Make sure the archives are actually readable (catches bad dumps).
    vfs::Vfs probe;
    if (!vfs::mountGameSource(probe, *r.source, &error)) {
        r.message = error;
        return r;
    }
    if (!probe.exists("tune/vpbug.info") || !probe.exists("city/london.psdl")) {
        r.message = std::format("{} does not look like Midtown Madness 2 game data", r.source->describe());
        return r;
    }
    r.ok = true;
    r.message = std::format("OK: {}", r.source->describe());
    return r;
}

std::string configuredGameSource(const Settings& settings) {
    if (!settings.gameSource.empty())
        return settings.gameSource;
    return installDefaultGameSource();
}

std::filesystem::path defaultImportDir() { return paths::userDataDir() / "gamedata"; }

bool importGameData(const vfs::GameSource& source, const std::filesystem::path& destDir,
                    const std::function<bool(double)>& progress, std::string* error) {
    struct Item {
        std::string name;
        std::shared_ptr<const RandomAccessFile> file;
    };
    std::vector<Item> items;
    std::uint64_t total = 0;
    for (const auto& name : source.archives) {
        std::string err;
        auto f = vfs::openSourceFile(source, name, &err);
        if (!f) {
            setError(error, err);
            return false;
        }
        total += f->size();
        items.push_back({name, std::move(f)});
    }
    if (auto lang = vfs::openSourceFile(source, vfs::kLanguageModule)) {
        total += lang->size();
        items.push_back({vfs::kLanguageModule, std::move(lang)});
    }

    std::error_code ec;
    std::filesystem::create_directories(destDir, ec);
    if (ec) {
        setError(error, std::format("cannot create {}: {}", str::fromPath(destDir), ec.message()));
        return false;
    }

    constexpr std::size_t kChunk = 1 << 20;
    std::vector<std::byte> buffer(kChunk);
    std::uint64_t done = 0;
    std::random_device rd;
    for (const auto& item : items) {
        const auto target = destDir / str::toPath(item.name);
        const std::uint64_t size = item.file->size();
        if (std::filesystem::is_regular_file(target, ec) && std::filesystem::file_size(target, ec) == size) {
            done += size;
            log::info("import: {} already present", item.name);
            continue;
        }
        auto tmp = target;
        tmp += std::format(".{:08x}.part", rd());
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) {
                setError(error, std::format("cannot write {}", str::fromPath(tmp)));
                return false;
            }
            for (std::uint64_t off = 0; off < size;) {
                const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(kChunk, size - off));
                if (!item.file->readExact(off, std::span(buffer).first(n))) {
                    out.close();
                    std::filesystem::remove(tmp, ec);
                    setError(error, std::format("read error in {} at offset {}", item.name, off));
                    return false;
                }
                out.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(n));
                if (!out) {
                    out.close();
                    std::filesystem::remove(tmp, ec);
                    setError(error, std::format("write error on {} (disk full?)", str::fromPath(tmp)));
                    return false;
                }
                off += n;
                done += n;
                if (progress && !progress(total ? static_cast<double>(done) / static_cast<double>(total) : 1.0)) {
                    out.close();
                    std::filesystem::remove(tmp, ec);
                    setError(error, "cancelled");
                    return false;
                }
            }
        }
        std::filesystem::rename(tmp, target, ec);
        if (ec) {
            std::filesystem::remove(tmp, ec);
            setError(error, std::format("cannot finalize {}", str::fromPath(target)));
            return false;
        }
        log::info("import: copied {} ({} bytes)", item.name, size);
    }
    if (progress)
        progress(1.0);
    return true;
}

} // namespace mm2::app
