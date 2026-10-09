#pragma once

// netprobe rulesreport: whether the machines of a network race agree on its
// rules (each player's checkpoints, laps and finish, the results, Cops and
// Robbers' gold events and scores), from each machine's OPENMM2_NET_TRACE
// (the R and C lines of game/net/RulesTrace.h; docs/review/
// multiplayer-desync-rules.md).

#include <string>
#include <vector>

namespace mm2::netprobe {

struct RulesReportOptions {
    std::vector<std::string> traces; // the host's first, then the clients'
};

// Prints the report; returns 0, or 1 when a trace cannot be read.
int rulesReport(const RulesReportOptions& options);

} // namespace mm2::netprobe
