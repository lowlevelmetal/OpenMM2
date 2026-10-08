// Parity checks for the game-source mounting (docs/parity/openmm2-only.md).
#include "core/File.h"
#include "core/StringUtil.h"
#include "vfs/GameSource.h"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace mm2;

namespace {

void putU32(std::vector<std::byte>& out, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out[at + static_cast<std::size_t>(i)] = static_cast<std::byte>((v >> (8 * i)) & 0xFF);
}

// A one-file uncompressed DAVE archive.
std::vector<std::byte> daveArchive(const std::string& path, const std::string& contents) {
    constexpr std::size_t kDir = 0x800;
    const std::size_t names = kDir + 16;
    const std::size_t data = names + path.size() + 1;
    std::vector<std::byte> out(data + contents.size());
    std::memcpy(out.data(), "DAVE", 4);
    putU32(out, 4, 1);
    putU32(out, 8, 16);
    putU32(out, 12, static_cast<std::uint32_t>(path.size() + 1));
    putU32(out, kDir + 0, 0);
    putU32(out, kDir + 4, static_cast<std::uint32_t>(data));
    putU32(out, kDir + 8, static_cast<std::uint32_t>(contents.size()));
    putU32(out, kDir + 12, static_cast<std::uint32_t>(contents.size()));
    std::memcpy(out.data() + names, path.data(), path.size());
    std::memcpy(out.data() + data, contents.data(), contents.size());
    return out;
}

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        const std::string test = ::testing::UnitTest::GetInstance()->current_test_info()->name();
        path = std::filesystem::temp_directory_path() / ("openmm2_parity_vfs_" + test);
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    void archive(const std::string& name, const std::string& file, const std::string& contents) const {
        ASSERT_TRUE(file::writeAtomic(path / name, daveArchive(file, contents)));
    }
};

std::string read(const vfs::Vfs& v, std::string_view path) {
    auto data = v.readAll(path);
    if (!data)
        return "<missing>";
    return std::string(reinterpret_cast<const char*>(data->data()), data->size());
}

} // namespace

// zipMultiAutoInit: every *.ar in the folder, in upper-case name order.
TEST(GameSourceParity, ArchivesAreSearchedInUpperCaseNameOrder) {
    TempDir dir;
    for (const char* name :
         {"MM2CORE.AR", "MM2TEX.AR", "MM2AUD.AR", "mm2audex.ar", "zz_mod.ar", "A_MOD.AR", "0mod.ar"})
        dir.archive(name, "x", "x");
    const auto source = vfs::probeGameSource(dir.path);
    ASSERT_TRUE(source);
    EXPECT_TRUE(source->usable());
    const std::vector<std::string> expected = {"0mod.ar",   "A_MOD.AR",  "MM2AUD.AR", "mm2audex.ar",
                                               "MM2CORE.AR", "MM2TEX.AR", "zz_mod.ar"};
    EXPECT_EQ(source->archives, expected);
}

// zipFile::zipOpen searches from the first archive in that order; loose files
// next to the archives are never read.
TEST(GameSourceParity, FirstArchiveWinsAndLooseFilesAreIgnored) {
    TempDir dir;
    dir.archive("MM2CORE.AR", "tune/a.txt", "core");
    dir.archive("MM2TEX.AR", "tune/b.txt", "tex");
    dir.archive("MM2AUD.AR", "aud/c.txt", "aud");
    dir.archive("A_MOD.AR", "tune/a.txt", "a_mod");
    dir.archive("ZZ_MOD.AR", "tune/b.txt", "zz_mod");
    dir.archive("ZZ_ONLY.AR", "tune/only.txt", "zz_only");
    std::filesystem::create_directories(dir.path / "tune");
    std::ofstream(dir.path / "tune" / "b.txt") << "loose";
    std::ofstream(dir.path / "tune" / "loose.txt") << "loose";

    const auto source = vfs::probeGameSource(dir.path);
    ASSERT_TRUE(source && source->usable());
    vfs::Vfs v;
    std::string error;
    ASSERT_TRUE(vfs::mountGameSource(v, *source, &error)) << error;
    EXPECT_EQ(read(v, "tune/a.txt"), "a_mod"); // A_MOD.AR sorts before MM2CORE.AR
    EXPECT_EQ(read(v, "tune/b.txt"), "tex");   // MM2TEX.AR before ZZ_MOD.AR; the loose copy is ignored
    EXPECT_EQ(read(v, "tune/only.txt"), "zz_only");
    EXPECT_EQ(read(v, "aud/c.txt"), "aud");
    EXPECT_FALSE(v.exists("tune/loose.txt"));
}
