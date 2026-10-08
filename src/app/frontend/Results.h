// The results page's rows that a Cops and Robbers game fills in when it
// ends (MM2 mmMultiCR::FillResults into PUResults).
#pragma once

#include "game/RaceConfig.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mm2::app::frontend {

struct CrResultPlayer {
    std::string name;
    int score = 0;
    bool self = false; // the local player
};

// mmMultiCR::FillResults: in the team games the winning team first (COPS /
// ROBBERS, strings 122-127, or BLUE / RED, 124-129, with the team's points;
// team 0, the cops or blue, comes first on a tie), then the other team;
// then the players by points from the next row (`players` in the game's
// player order): the local player ahead of everyone it ties with, the
// others in their order on a tie. Free-For-All lists only the players, from
// row 1. Each row is a place with a name and points (PUResults::AddName).
std::vector<game::RaceStanding> crResultRows(game::CopsAndRobbersMode mode, int team0Points, int team1Points,
                                             const std::vector<CrResultPlayer>& players,
                                             const std::function<std::string(std::uint32_t, const char*)>& string);

} // namespace mm2::app::frontend
