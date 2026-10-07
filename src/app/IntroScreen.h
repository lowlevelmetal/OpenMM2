#pragma once

#include "app/Context.h"

#include <memory>

namespace mm2::app {

// Plays the original intro movie (GAME/LOGOS.AVI on the disc, or next to the
// game in an installation), then continues to the frontend. Any key, mouse
// button or gamepad button skips it; without the movie it continues at once.
std::unique_ptr<Screen> makeIntroScreen(Context& ctx);

} // namespace mm2::app
