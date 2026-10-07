// mm2tool: inspect, extract and convert Midtown Madness 2 game data.
#include "Command.h"
#include "core/Log.h"

#include <algorithm>
#include <cstdio>
#include <print>
#include <vector>

namespace mm2::tool {
namespace {

std::vector<Command>& registry() {
    static std::vector<Command> commands;
    return commands;
}

int usage() {
    auto cmds = registry();
    std::ranges::sort(cmds, {}, &Command::name);
    std::println(stderr, "usage: mm2tool [-v] <command> [args...]\n\ncommands:");
    for (const auto& c : cmds)
        std::println(stderr, "  {} {}\n      {}", c.name, c.usage, c.summary);
    std::println(stderr, "\n<container> is a DAVE .ar archive, a disc image, a game folder (all archives\n"
                         "mounted, as the game sees them) or any directory.");
    return 2;
}

} // namespace

Registrar::Registrar(Command cmd) { registry().push_back(std::move(cmd)); }

} // namespace mm2::tool

int main(int argc, char** argv) {
    using namespace mm2;
    log::setLevel(log::Level::Warn);
    int first = 1;
    if (argc > 1 && std::string_view(argv[1]) == "-v") {
        log::setLevel(log::Level::Debug);
        ++first;
    }
    if (argc <= first)
        return tool::usage();
    const std::string_view name = argv[first];
    for (const auto& c : tool::registry())
        if (c.name == name)
            return c.run(std::span<char* const>(argv + first + 1, argv + argc));
    std::println(stderr, "unknown command '{}'", name);
    return tool::usage();
}
