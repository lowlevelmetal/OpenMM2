#pragma once

#include "vfs/Vfs.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mm2::vfs {

// Where the original Midtown Madness 2 files come from. The user points the
// installer or first-run setup at any of these:
//   * a disc image (.iso, .bin/.cue, .img) or a raw optical device (/dev/sr0)
//   * a mounted disc or a copy of its contents (has GAME/MM2CORE.AR)
//   * an existing installation directory (has MM2CORE.AR next to the game)
struct GameSource {
    enum class Kind { DiscImage, DiscDirectory, InstallDirectory };

    Kind kind = Kind::InstallDirectory;
    std::filesystem::path path;

    // Volume label for discs ("MIDTOWN2"), empty otherwise.
    std::string volumeId;
    // Archives found, in mount order (base archives first, then add-ons).
    std::vector<std::string> archives;
    // Required archives that were not found; empty when the source is usable.
    std::vector<std::string> missing;

    bool usable() const { return missing.empty(); }
    std::string describe() const;
};

// Archives every retail copy has. MM2AUDEX.AR holds extra (non-English
// commentary / optional) audio and is mounted when present.
inline constexpr const char* kRequiredArchives[] = {"MM2CORE.AR", "MM2TEX.AR", "MM2AUD.AR"};
inline constexpr const char* kOptionalArchives[] = {"MM2AUDEX.AR"};
// Non-archive files read from the game folder: MMLANG.DLL carries the UI
// strings (a resource-only DLL; it is parsed, never loaded or executed).
inline constexpr const char* kLanguageModule = "MMLANG.DLL";

// Identifies what `path` is. Returns std::nullopt (and sets `error`) if it is
// not a recognisable game source at all; a recognised but incomplete source
// is returned with `missing` filled in.
std::optional<GameSource> probeGameSource(const std::filesystem::path& path, std::string* error = nullptr);

// Mounts the archives of `source` into `vfs`:
//   priority 0   base archives (MM2CORE, MM2TEX, MM2AUD, MM2AUDEX)
//   priority 10  any other *.ar next to them, in case-insensitive name order
//                (add-on content; later names override earlier ones)
//   priority 20  loose files in the install directory (only for installs)
// Returns false (and sets `error`) if a required archive cannot be opened.
bool mountGameSource(Vfs& vfs, const GameSource& source, std::string* error = nullptr);

// Opens a file from the game folder of `source` (the GAME/ directory of a
// disc), e.g. "MM2CORE.AR" or "MMLANG.DLL", as a raw file. Used to copy game
// data to the hard disk and to read the language resources.
std::shared_ptr<const RandomAccessFile> openSourceFile(const GameSource& source, const std::string& name,
                                                          std::string* error = nullptr);

// Candidate locations to offer in the setup UI: mounted optical discs and
// common install directories that look like they contain the game.
std::vector<std::filesystem::path> suggestGameSources();

} // namespace mm2::vfs
