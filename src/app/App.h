#pragma once

#include "app/CommandLine.h"

namespace mm2::app {

// Runs the interactive game: settings, first-run setup, then the game.
// Returns the process exit code.
int run(const CommandLine& cl);

} // namespace mm2::app
