#include "vfs/DaveArchive.h"

#include "core/Log.h"
#include "core/StringUtil.h"

#include <miniz.h>

#include <array>
#include <cstring>
#include <format>

namespace mm2::vfs {
namespace {

constexpr std::uint32_t kDirStart = 0x800;
constexpr std::uint32_t kEntrySize = 16;
// Sanity limits; the largest retail archive has ~5,700 entries.
constexpr std::uint32_t kMaxEntries = 1u << 20;
constexpr std::uint32_t kMaxNameTable = 64u << 20;

void setError(std::string* error, std::string msg) {
    if (error)
        *error = std::move(msg);
}

} // namespace

bool inflateRaw(std::span<const std::byte> packed, std::span<std::byte> out) {
    tinfl_decompressor decomp;
    tinfl_init(&decomp);
    std::size_t inBytes = packed.size();
    std::size_t outBytes = out.size();
    // The whole output buffer is available, so the decompressor never needs
    // to wrap: pass NON_WRAPPING_OUTPUT_BUF and no HAS_MORE_INPUT.
    const tinfl_status status = tinfl_decompress(
        &decomp, reinterpret_cast<const mz_uint8*>(packed.data()), &inBytes,
        reinterpret_cast<mz_uint8*>(out.data()), reinterpret_cast<mz_uint8*>(out.data()), &outBytes,
        TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    return status == TINFL_STATUS_DONE && outBytes == out.size();
}

std::shared_ptr<DaveArchive> DaveArchive::open(const std::filesystem::path& path, std::string* error) {
    auto file = OsFile::open(path);
    if (!file) {
        setError(error, std::format("cannot open '{}'", str::fromPath(path)));
        return nullptr;
    }
    return open(std::move(file), str::fromPath(path), error);
}

std::shared_ptr<DaveArchive> DaveArchive::open(std::shared_ptr<const RandomAccessFile> file, std::string label,
                                               std::string* error) {
    std::shared_ptr<DaveArchive> ar(new DaveArchive());
    ar->m_file = std::move(file);
    ar->m_label = std::move(label);
    if (!ar->load(error))
        return nullptr;
    return ar;
}

bool DaveArchive::addEntry(Entry entry, std::string* error) {
    // Directory markers ("anim/CVS/") carry no data.
    if (entry.name.empty() || entry.name.back() == '/')
        return true;
    if (std::uint64_t{entry.dataOffset} + entry.packedSize > m_file->size()) {
        setError(error, std::format("entry '{}' extends past end of archive", entry.name));
        return false;
    }
    // zipFile::Open looks names up with bsearch over the sorted directory,
    // ignoring case and treating '\' as '/'; a duplicate name is ambiguous
    // there and the first one is kept here.
    const std::string key = str::normalizeVirtualPath(entry.name);
    if (m_index.try_emplace(key, m_entries.size()).second)
        m_entries.push_back(std::move(entry));
    return true;
}

// zipFile::Init for an ordinary PKZIP file renamed to .ar (how add-on content
// is often distributed). Only what MM2 supports: no archive comment (the end
// record must be the last 22 bytes), a single part, stored or deflated
// entries. Like MM2, the data is taken to start 30 + name length bytes after
// the local header, so a local header with an extra field is misread.
bool DaveArchive::loadZip(std::string* error) {
    const std::uint64_t fileSize = m_file->size();
    std::array<std::byte, 22> eocd{};
    if (fileSize < eocd.size() || !m_file->readExact(fileSize - eocd.size(), eocd) ||
        loadLE<std::uint32_t>(eocd.data()) != 0x06054B50u) {
        setError(error, "not a DAVE archive, and no zip central directory (zip comments are not supported)");
        return false;
    }
    if (loadLE<std::uint16_t>(eocd.data() + 4) != loadLE<std::uint16_t>(eocd.data() + 6)) {
        setError(error, "multi-part zip files are not supported");
        return false;
    }
    const std::uint32_t count = loadLE<std::uint16_t>(eocd.data() + 8);
    const std::uint32_t dirSize = loadLE<std::uint32_t>(eocd.data() + 12);
    const std::uint32_t dirOffset = loadLE<std::uint32_t>(eocd.data() + 16);
    if (std::uint64_t{dirOffset} + dirSize > fileSize) {
        setError(error, "corrupt zip central directory");
        return false;
    }
    std::vector<std::byte> dir(dirSize);
    if (!m_file->readExact(dirOffset, dir)) {
        setError(error, "truncated zip central directory");
        return false;
    }
    m_entries.clear();
    m_index.clear();
    // MM2 reads headers until the signature stops matching but searches only
    // the first `count` of them (the end record's count for this disk).
    std::size_t pos = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (pos + 46 > dir.size() || loadLE<std::uint32_t>(dir.data() + pos) != 0x02014B50u)
            break;
        const std::byte* h = dir.data() + pos;
        const auto method = loadLE<std::uint16_t>(h + 10);
        if (method != 0 && method != 8) {
            setError(error, "compression method besides store or deflate encountered");
            return false;
        }
        Entry entry;
        entry.packedSize = loadLE<std::uint32_t>(h + 20);
        entry.size = loadLE<std::uint32_t>(h + 24);
        const std::uint16_t nameLen = loadLE<std::uint16_t>(h + 28);
        const std::uint16_t extraLen = loadLE<std::uint16_t>(h + 30);
        const std::uint16_t commentLen = loadLE<std::uint16_t>(h + 32);
        const std::uint32_t localOffset = loadLE<std::uint32_t>(h + 42);
        if (pos + 46 + nameLen > dir.size()) {
            setError(error, "truncated zip central directory");
            return false;
        }
        entry.name.assign(reinterpret_cast<const char*>(h + 46), nameLen);
        entry.dataOffset = localOffset + 30u + nameLen;
        if (!addEntry(std::move(entry), error))
            return false;
        pos += 46u + nameLen + extraLen + commentLen;
    }
    log::debug("dave: '{}' zip archive, {} files", m_label, m_entries.size());
    return true;
}

bool DaveArchive::load(std::string* error) {
    std::array<std::byte, 16> header{};
    if (!m_file->readExact(0, header)) {
        setError(error, "file too small for a DAVE header");
        return false;
    }
    if (std::memcmp(header.data(), "DAVE", 4) != 0) {
        // zipFile::Init: anything that is not a DAVE archive is read as a
        // zip file. ("Dave" archives with prefix-compressed names, from other
        // Angel games, are not readable by MM2 either.)
        return loadZip(error);
    }
    const auto count = loadLE<std::uint32_t>(header.data() + 4);
    const auto dirSize = loadLE<std::uint32_t>(header.data() + 8);
    const auto nameSize = loadLE<std::uint32_t>(header.data() + 12);
    if (count > kMaxEntries || dirSize < count * kEntrySize || nameSize > kMaxNameTable ||
        kDirStart + std::uint64_t{dirSize} + nameSize > m_file->size()) {
        setError(error, "corrupt DAVE header");
        return false;
    }

    std::vector<std::byte> dir(static_cast<std::size_t>(count) * kEntrySize);
    std::vector<std::byte> names(nameSize);
    if (!m_file->readExact(kDirStart, dir) || !m_file->readExact(kDirStart + std::uint64_t{dirSize}, names)) {
        setError(error, "truncated DAVE directory");
        return false;
    }

    m_entries.clear();
    m_entries.reserve(count);
    m_index.clear();
    m_index.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::byte* e = dir.data() + static_cast<std::size_t>(i) * kEntrySize;
        const auto nameOff = loadLE<std::uint32_t>(e + 0);
        Entry entry;
        entry.dataOffset = loadLE<std::uint32_t>(e + 4);
        entry.size = loadLE<std::uint32_t>(e + 8);
        entry.packedSize = loadLE<std::uint32_t>(e + 12);
        if (nameOff >= nameSize) {
            setError(error, std::format("entry {} has an out-of-range name", i));
            return false;
        }
        const char* nameStart = reinterpret_cast<const char*>(names.data()) + nameOff;
        entry.name.assign(nameStart, strnlen(nameStart, nameSize - nameOff));
        if (!addEntry(std::move(entry), error))
            return false;
    }
    log::debug("dave: '{}' {} files", m_label, m_entries.size());
    return true;
}

std::string DaveArchive::describe() const { return "archive " + m_label; }

const DaveArchive::Entry* DaveArchive::find(std::string_view path) const {
    const auto it = m_index.find(str::normalizeVirtualPath(path));
    return it == m_index.end() ? nullptr : &m_entries[it->second];
}

bool DaveArchive::extract(const Entry& e, std::vector<std::byte>& out) const {
    out.resize(e.size);
    if (!e.compressed())
        return m_file->readExact(e.dataOffset, out);
    std::vector<std::byte> packed(e.packedSize);
    if (!m_file->readExact(e.dataOffset, packed))
        return false;
    return inflateRaw(packed, out);
}

std::shared_ptr<RandomAccessFile> DaveArchive::open(std::string_view path) const {
    const Entry* e = find(path);
    if (!e)
        return nullptr;
    if (!e->compressed())
        return std::make_shared<SubFile>(m_file, e->dataOffset, e->size);
    std::vector<std::byte> data;
    if (!extract(*e, data)) {
        log::warn("dave: corrupt deflate stream for '{}' in {}", e->name, m_label);
        return nullptr;
    }
    return std::make_shared<MemoryFile>(std::move(data));
}

bool DaveArchive::exists(std::string_view path) const { return find(path) != nullptr; }

void DaveArchive::forEachFile(const std::function<void(const EntryInfo&)>& fn) const {
    for (const auto& e : m_entries)
        fn(EntryInfo{str::normalizeVirtualPath(e.name), e.size, false});
}

} // namespace mm2::vfs
