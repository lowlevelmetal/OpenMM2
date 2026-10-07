// Generic data commands: dat, strings.
#include "Command.h"
#include "Common.h"

#include "core/File.h"
#include "core/StringUtil.h"
#include "data/DatFile.h"
#include "data/PeResources.h"
#include "vfs/GameSource.h"

#include <print>

namespace mm2::tool {
namespace {

void dumpNode(const data::DatNode& n, int depth) {
    const std::string indent(static_cast<std::size_t>(depth) * 2, ' ');
    if (n.isBlock) {
        std::println("{}{} {{", indent, n.name);
        for (const auto& c : n.children)
            dumpNode(c, depth + 1);
        std::println("{}}}", indent);
        return;
    }
    std::string values;
    for (double d : n.numbers)
        values += std::format(" {}", d);
    for (const auto& s : n.strings)
        values += std::format(" \"{}\"", s);
    std::println("{}{} ={}", indent, n.name, values);
}

int cmdDat(std::span<char* const> args) {
    if (args.size() < 2)
        return 2;
    auto fs = openContainer(args[0]);
    if (!fs)
        return 1;
    auto bytes = readFile(*fs, args[1]);
    if (!bytes)
        return 1;
    std::string err;
    auto f = data::parseDat(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), &err);
    if (!f) {
        std::println(stderr, "error: {}", err);
        return 1;
    }
    for (const auto& c : f->root.children)
        dumpNode(c, 0);
    return 0;
}

int cmdStrings(std::span<char* const> args) {
    if (args.empty())
        return 2;
    const auto path = str::toPath(args[0]);
    std::optional<std::vector<std::byte>> bytes;
    if (str::iendsWith(args[0], ".dll")) {
        bytes = file::readBinary(path);
    } else if (auto source = vfs::probeGameSource(path)) {
        if (auto f = vfs::openSourceFile(*source, vfs::kLanguageModule))
            bytes = f->readAll();
    }
    if (!bytes) {
        std::println(stderr, "error: cannot read {}", vfs::kLanguageModule);
        return 1;
    }
    std::string err;
    auto table = data::readPeStringTable(*bytes, 0, &err);
    if (!table) {
        std::println(stderr, "error: {}", err);
        return 1;
    }
    std::println("; language 0x{:04x}, {} strings", table->language, table->strings.size());
    for (const auto& [id, s] : table->strings) {
        std::string escaped;
        for (char c : s)
            escaped += c == '\n' ? std::string("\\n") : (c == '\r' ? std::string("\\r") : std::string(1, c));
        std::println("{:5} {}", id, escaped);
    }
    return 0;
}

const Registrar r1({"dat", "<container> <path>", "parse and pretty-print an Angel 'type: a' data file", &cmdDat});
const Registrar r2({"strings", "<game-source|MMLANG.DLL>", "print the game's UI string table", &cmdStrings});

} // namespace
} // namespace mm2::tool
