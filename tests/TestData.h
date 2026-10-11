#pragma once

#include "vfs/Vfs.h"

#include <memory>

// Network tests that host a session pick their ports from their own block,
// all below 49152 (Windows' dynamic range): two files sharing a block can bind
// the same port when ctest runs them in parallel. Taken blocks:
//   17000-19999 game/test_netgame_start     21000-24999 net/test_player_cars
//   25000-28999 game/test_net_race_rules    29000-32999 game/test_damage_sync
//   33000-36999 game/test_netgame_lobby     37000-40999 game/test_netgame_sync
//   41000-44999 game/test_netgame           45000-48999 game/test_traffic_net
// Other network tests bind port 0. Within a block, test::processPortSlot
// (NetTestPorts.h) picks this process's ports.

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
