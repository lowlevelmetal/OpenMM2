#pragma once

#include "app/Context.h"

#include <memory>

namespace mm2::app {

// Plays the original intro movie (GAME/LOGOS.AVI on the disc, or next to the
// game in an installation) at its own size in the middle of the 640 x 480
// screen, then continues to the frontend (ebolaPlayMovie). Esc, Space or the
// left mouse button skip it (checked every 250 ms); it pauses while the window
// is inactive; without the movie it continues at once.
std::unique_ptr<Screen> makeIntroScreen(Context& ctx);

} // namespace mm2::app
