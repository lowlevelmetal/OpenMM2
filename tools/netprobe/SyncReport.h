#pragma once

// netprobe syncreport: the divergence between machines in a network race,
// from each machine's OPENMM2_NET_TRACE (docs/multiplayer.md, "Diagnosing
// replication"; docs/review/multiplayer-desync-cars.md).

#include <string>
#include <vector>

namespace mm2::netprobe {

struct SyncReportOptions {
    std::vector<std::string> traces; // one per machine
    double skipSeconds = 3.0;        // of each trace's drawn cars, from the first (the start)
    double teleportMetres = 1.0;     // a frame's unexplained move that counts as a teleport
};

// Prints the report; returns 0, or 1 when a trace cannot be read.
int syncReport(const SyncReportOptions& options);

} // namespace mm2::netprobe
