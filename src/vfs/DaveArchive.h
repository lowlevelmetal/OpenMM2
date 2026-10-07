#pragma once

#include "vfs/FileSystem.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mm2::vfs {

// Angel Game Engine "DAVE" archive (.AR), as used by MM2CORE.AR, MM2TEX.AR,
// MM2AUD.AR and MM2AUDEX.AR. See docs/formats/dave.md.
//
// Layout (all little-endian):
//   0x000  char[4]  "DAVE"
//   0x004  u32      entry count
//   0x008  u32      directory table size in bytes
//   0x00C  u32      name table size in bytes
//   0x800  entry[]  16 bytes each: nameOffset, dataOffset, size, packedSize
//   0x800 + dirSize: NUL-separated UTF-8/ASCII names, indexed by nameOffset
// Entry data starts at dataOffset (2048-byte aligned). When packedSize != size
// the payload is a raw DEFLATE stream (no zlib header).
class DaveArchive final : public FileSystem {
public:
    struct Entry {
        std::string name; // original name as stored (e.g. "tune/vehicles.csv")
        std::uint32_t dataOffset;
        std::uint32_t size;
        std::uint32_t packedSize;
        bool compressed() const { return packedSize != size; }
    };

    static std::shared_ptr<DaveArchive> open(const std::filesystem::path& path, std::string* error = nullptr);
    static std::shared_ptr<DaveArchive> open(std::shared_ptr<const RandomAccessFile> file, std::string label,
                                             std::string* error = nullptr);

    std::string describe() const override;
    std::shared_ptr<RandomAccessFile> open(std::string_view path) const override;
    bool exists(std::string_view path) const override;
    void forEachFile(const std::function<void(const EntryInfo&)>& fn) const override;

    const std::vector<Entry>& entries() const { return m_entries; }
    const Entry* find(std::string_view path) const;

    // Reads and (if needed) inflates an entry. Returns false on a corrupt stream.
    bool extract(const Entry& e, std::vector<std::byte>& out) const;

private:
    DaveArchive() = default;
    bool load(std::string* error);

    std::shared_ptr<const RandomAccessFile> m_file;
    std::string m_label;
    std::vector<Entry> m_entries;
    std::unordered_map<std::string, std::size_t> m_index; // normalized path -> entry
};

// Inflates a raw DEFLATE stream of known output size.
bool inflateRaw(std::span<const std::byte> packed, std::span<std::byte> out);

} // namespace mm2::vfs
