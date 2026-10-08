// Archive reading checked against MM2's zipFile (docs/parity/formats.md).
#include "core/File.h"
#include "vfs/DaveArchive.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string_view>
#include <vector>

using namespace mm2;

namespace {

struct Writer {
    std::vector<std::byte> data;
    Writer& u8(std::uint8_t v) {
        data.push_back(std::byte{v});
        return *this;
    }
    Writer& u16(std::uint16_t v) { return u8(v & 0xFF).u8(v >> 8); }
    Writer& u32(std::uint32_t v) { return u16(v & 0xFFFF).u16(v >> 16); }
    Writer& str(std::string_view s) {
        for (char c : s)
            u8(static_cast<std::uint8_t>(c));
        return *this;
    }
    Writer& bytes(const std::vector<std::uint8_t>& b) {
        for (auto c : b)
            u8(c);
        return *this;
    }
};

struct ZipEntry {
    std::string name;
    std::uint16_t method;
    std::vector<std::uint8_t> stored;
    std::uint32_t size;
};

std::vector<std::byte> makeZip(const std::vector<ZipEntry>& entries, std::string_view comment = {}) {
    Writer w;
    std::vector<std::uint32_t> offsets;
    for (const auto& e : entries) {
        offsets.push_back(static_cast<std::uint32_t>(w.data.size()));
        w.u32(0x04034B50).u16(20).u16(0).u16(e.method).u16(0).u16(0).u32(0);
        w.u32(static_cast<std::uint32_t>(e.stored.size())).u32(e.size);
        w.u16(static_cast<std::uint16_t>(e.name.size())).u16(0).str(e.name).bytes(e.stored);
    }
    const auto dirOffset = static_cast<std::uint32_t>(w.data.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        w.u32(0x02014B50).u16(20).u16(20).u16(0).u16(e.method).u16(0).u16(0).u32(0);
        w.u32(static_cast<std::uint32_t>(e.stored.size())).u32(e.size);
        w.u16(static_cast<std::uint16_t>(e.name.size())).u16(0).u16(0).u16(0).u16(0).u32(0).u32(offsets[i]);
        w.str(e.name);
    }
    const auto dirSize = static_cast<std::uint32_t>(w.data.size()) - dirOffset;
    w.u32(0x06054B50).u16(0).u16(0).u16(static_cast<std::uint16_t>(entries.size()));
    w.u16(static_cast<std::uint16_t>(entries.size())).u32(dirSize).u32(dirOffset);
    w.u16(static_cast<std::uint16_t>(comment.size())).str(comment);
    return w.data;
}

std::string text(const std::vector<std::byte>& b) {
    return {reinterpret_cast<const char*>(b.data()), b.size()};
}

} // namespace

// zipFile::Init reads an ordinary zip file renamed to .ar.
TEST(ParityArchive, ReadsZipArchives) {
    const std::vector<std::uint8_t> hello = {'h', 'i', '!'};
    // Raw DEFLATE of "hello hello hello hello".
    const std::vector<std::uint8_t> packed = {0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x57, 0xc8, 0x40, 0x27, 0x01};
    auto bytes = makeZip({{"Tune/A.txt", 0, hello, 3}, {"dir\\b.txt", 8, packed, 23}, {"dir/", 0, {}, 0}});
    std::string err;
    auto ar = vfs::DaveArchive::open(std::make_shared<MemoryFile>(bytes), "add-on.ar", &err);
    ASSERT_TRUE(ar) << err;
    EXPECT_EQ(ar->entries().size(), 2u); // the directory entry carries no data
    auto a = ar->open(std::string_view("tune/a.txt"));
    ASSERT_TRUE(a);
    EXPECT_EQ(text(a->readAll()), "hi!");
    auto b = ar->open(std::string_view("dir/b.txt"));
    ASSERT_TRUE(b);
    EXPECT_EQ(text(b->readAll()), "hello hello hello hello");
}

TEST(ParityArchive, RejectsWhatZipFileRejects) {
    std::string err;
    // An archive comment moves the end record away from the last 22 bytes.
    auto commented = makeZip({{"a", 0, {'x'}, 1}}, "note");
    EXPECT_FALSE(vfs::DaveArchive::open(std::make_shared<MemoryFile>(commented), "c.ar", &err));
    // Only stored and deflated entries.
    auto bzip = makeZip({{"a", 12, {'x'}, 1}});
    EXPECT_FALSE(vfs::DaveArchive::open(std::make_shared<MemoryFile>(bzip), "b.ar", &err));
    const std::vector<std::byte> zeros(64);
    EXPECT_FALSE(vfs::DaveArchive::open(std::make_shared<MemoryFile>(zeros), "z.ar", &err));
}
