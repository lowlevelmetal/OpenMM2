#pragma once

#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace mm2::tool {

// A subcommand. `args` excludes the program and command names.
struct Command {
    std::string_view name;
    std::string_view usage;   // e.g. "<container> <path>"
    std::string_view summary; // one line
    std::function<int(std::span<char* const> args)> run;
};

// Registers a command at static-initialization time:
//   static const mm2::tool::Registrar reg({"ls", "<container>", "list files", &cmdLs});
struct Registrar {
    explicit Registrar(Command cmd);
};

} // namespace mm2::tool
