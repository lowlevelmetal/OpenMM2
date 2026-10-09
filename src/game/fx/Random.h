/*
    OpenMM2 - port of the Angel engine's random number generator.
    Derived from Open1560 (code/midtown/game.asm: irand, frand),
    Copyright (C) 2020 Brick, GPL-3.0-or-later.
*/
#pragma once

#include "ai/Random.h"

namespace mm2::game::fx {

// irand()/frand() of the Angel engine: the MSVC linear congruential
// generator (seed * 214013 + 2531011), returning 15 bits. One class for the
// whole game (ai::Random), so the systems MM2 sets up from its one global
// seed can share a stream (ai/Random.h); the effects, which MM2 draws from
// its secondary seed (DisableGlobalSeed / EnableGlobalSeed), keep their own.
using Rand = ai::Random;

} // namespace mm2::game::fx
