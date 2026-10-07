#include "data/PeResources.h"

#include "core/File.h"

#include <cstring>
#include <vector>

namespace mm2::data {
namespace {

constexpr std::uint32_t kRtString = 6;

void setError(std::string* error, std::string msg) {
    if (error)
        *error = std::move(msg);
}

struct Section {
    std::uint32_t va, vsize, rawPtr, rawSize;
};

class PeImage {
public:
    explicit PeImage(std::span<const std::byte> d) : m_data(d) {}

    bool parse(std::string* error) {
        if (m_data.size() < 0x40 || std::memcmp(m_data.data(), "MZ", 2) != 0)
            return fail(error, "not a PE image (no MZ header)");
        const std::uint32_t peOff = u32(0x3C);
        if (!in(peOff, 24) || std::memcmp(m_data.data() + peOff, "PE\0\0", 4) != 0)
            return fail(error, "not a PE image (no PE signature)");
        const std::uint16_t numSections = u16(peOff + 6);
        const std::uint16_t optSize = u16(peOff + 20);
        const std::uint32_t opt = peOff + 24;
        if (!in(opt, optSize) || optSize < 2)
            return fail(error, "truncated optional header");
        const std::uint16_t magic = u16(opt);
        const std::uint32_t dirOff = magic == 0x20B ? opt + 112 : opt + 96;
        const std::uint32_t numDirs = u32(dirOff - 4);
        if (numDirs < 3 || !in(dirOff + 2 * 8, 8))
            return fail(error, "no resource directory");
        m_rsrcRva = u32(dirOff + 2 * 8);
        const std::uint32_t secTable = opt + optSize;
        if (!in(secTable, numSections * 40u))
            return fail(error, "truncated section table");
        for (std::uint32_t i = 0; i < numSections; ++i) {
            const std::uint32_t s = secTable + i * 40;
            m_sections.push_back({u32(s + 12), u32(s + 8), u32(s + 20), u32(s + 16)});
        }
        if (!m_rsrcRva || !rvaToOffset(m_rsrcRva))
            return fail(error, "module has no resources");
        m_rsrc = *rvaToOffset(m_rsrcRva);
        return true;
    }

    std::optional<std::uint32_t> rvaToOffset(std::uint32_t rva) const {
        for (const auto& s : m_sections) {
            const std::uint32_t size = std::max(s.vsize, s.rawSize);
            if (rva >= s.va && rva < s.va + size) {
                const std::uint32_t off = rva - s.va;
                if (off >= s.rawSize)
                    return std::nullopt;
                return s.rawPtr + off;
            }
        }
        return std::nullopt;
    }

    struct DirEntry {
        std::uint32_t id;
        bool named;
        bool isDir;
        std::uint32_t offset; // relative to the resource section start
    };

    std::vector<DirEntry> dir(std::uint32_t relOffset) const {
        std::vector<DirEntry> out;
        const std::uint32_t d = m_rsrc + relOffset;
        if (!in(d, 16))
            return out;
        const std::uint32_t count = u16(d + 12) + u16(d + 14);
        if (count > 4096 || !in(d + 16, count * 8))
            return out;
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint32_t e = d + 16 + i * 8;
            const std::uint32_t name = u32(e), data = u32(e + 4);
            out.push_back({name & 0x7FFFFFFF, (name & 0x80000000) != 0, (data & 0x80000000) != 0, data & 0x7FFFFFFF});
        }
        return out;
    }

    // Returns the bytes of a leaf IMAGE_RESOURCE_DATA_ENTRY.
    std::span<const std::byte> leaf(std::uint32_t relOffset) const {
        const std::uint32_t e = m_rsrc + relOffset;
        if (!in(e, 16))
            return {};
        const auto off = rvaToOffset(u32(e));
        const std::uint32_t size = u32(e + 4);
        if (!off || !in(*off, size))
            return {};
        return m_data.subspan(*off, size);
    }

private:
    bool in(std::uint64_t off, std::uint64_t len) const { return off + len <= m_data.size(); }
    std::uint16_t u16(std::uint32_t off) const { return in(off, 2) ? loadLE<std::uint16_t>(m_data.data() + off) : 0; }
    std::uint32_t u32(std::uint32_t off) const { return in(off, 4) ? loadLE<std::uint32_t>(m_data.data() + off) : 0; }
    static bool fail(std::string* error, const char* msg) {
        setError(error, msg);
        return false;
    }

    std::span<const std::byte> m_data;
    std::vector<Section> m_sections;
    std::uint32_t m_rsrcRva = 0;
    std::uint32_t m_rsrc = 0;
};

void appendUtf8(std::string& out, char32_t c) {
    if (c < 0x80) {
        out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (c >> 6)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (c >> 12)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (c >> 18)));
        out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
}

std::string utf16ToUtf8(std::span<const std::byte> bytes) {
    std::string out;
    for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
        char32_t c = loadLE<std::uint16_t>(bytes.data() + i);
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < bytes.size()) {
            const char32_t lo = loadLE<std::uint16_t>(bytes.data() + i + 2);
            if (lo >= 0xDC00 && lo < 0xE000) {
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                i += 2;
            }
        }
        appendUtf8(out, c);
    }
    return out;
}

} // namespace

std::optional<PeStringTable> readPeStringTable(std::span<const std::byte> image, std::uint16_t preferredLang,
                                               std::string* error) {
    PeImage pe(image);
    if (!pe.parse(error))
        return std::nullopt;
    PeStringTable table;
    bool langChosen = false;
    for (const auto& type : pe.dir(0)) {
        if (type.named || type.id != kRtString || !type.isDir)
            continue;
        for (const auto& block : pe.dir(type.offset)) {
            if (block.named || !block.isDir || block.id == 0)
                continue;
            const auto langs = pe.dir(block.offset);
            const PeImage::DirEntry* pick = nullptr;
            for (const auto& l : langs)
                if (!l.isDir && (langChosen ? l.id == table.language : (preferredLang == 0 || l.id == preferredLang)))
                    pick = &l;
            if (!pick && !langs.empty() && !langs.front().isDir && !langChosen)
                pick = &langs.front();
            if (!pick)
                continue;
            if (!langChosen) {
                table.language = static_cast<std::uint16_t>(pick->id);
                langChosen = true;
            }
            const auto data = pe.leaf(pick->offset);
            std::size_t pos = 0;
            for (std::uint32_t i = 0; i < 16 && pos + 2 <= data.size(); ++i) {
                const std::uint16_t len = loadLE<std::uint16_t>(data.data() + pos);
                pos += 2;
                const std::size_t bytes = std::min<std::size_t>(len * 2u, data.size() - pos);
                if (len)
                    table.strings[(block.id - 1) * 16 + i] = utf16ToUtf8(data.subspan(pos, bytes));
                pos += bytes;
            }
        }
    }
    if (table.strings.empty()) {
        setError(error, "no string table");
        return std::nullopt;
    }
    return table;
}

} // namespace mm2::data
