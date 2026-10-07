#pragma once

#include "vfs/FileSystem.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mm2::tool {

// Opens a container given on the command line: a DAVE .ar archive, a disc
// image, a game source (disc/install dir; all archives are mounted) or a plain
// directory. Prints an error and returns nullptr on failure.
std::shared_ptr<vfs::FileSystem> openContainer(const std::string& arg);

// Reads a whole file from a container; prints an error on failure.
std::optional<std::vector<std::byte>> readFile(const vfs::FileSystem& fs, const std::string& path);

// Simple glob: '*' matches any run of characters (including '/'), '?' one
// character; matching is case-insensitive.
bool globMatch(std::string_view pattern, std::string_view text);

} // namespace mm2::tool
