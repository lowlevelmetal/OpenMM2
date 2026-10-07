#pragma once

#include "vfs/Vfs.h"

#include <memory>

namespace mm2::test {

// Retail game files mounted from $OPENMM2_GAME_DATA (disc image, mounted
// disc, or install directory), or nullptr when unavailable. Mounted once and
// shared by all tests in the executable.
const vfs::Vfs* gameData();

} // namespace mm2::test

// Skips the current test when retail game data is not available.
#define MM2_REQUIRE_GAME_DATA()                                                                              \
    do {                                                                                                     \
        if (!::mm2::test::gameData())                                                                        \
            GTEST_SKIP() << "set OPENMM2_GAME_DATA to a Midtown Madness 2 disc image or install directory"; \
    } while (0)
