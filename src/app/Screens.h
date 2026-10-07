#pragma once

#include "app/Context.h"
#include "game/RaceConfig.h"

#include <memory>

namespace mm2::app {

// First-run screen: lets the player point OpenMM2 at their game disc, disc
// image or installation, optionally copying the data to the hard disk.
std::unique_ptr<Screen> makeSetupScreen(Context& ctx);

// The game's own menus, starting at the title screen (src/app/frontend).
std::unique_ptr<Screen> makeFrontendScreen(Context& ctx);
// Re-enters the menus after a session, showing the results screen for it.
std::unique_ptr<Screen> makeFrontendScreen(Context& ctx, const game::RaceResult& result);

// A session in the city: loads the world for `config` and runs it until the
// player quits or the event ends, then returns to the frontend with a
// RaceResult (src/app/RaceScreen.cpp).
std::unique_ptr<Screen> makeRaceScreen(Context& ctx, const game::RaceConfig& config);

} // namespace mm2::app
