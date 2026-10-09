#pragma once

#include <string>
#include <vector>

namespace mm2::netprobe {

// The props section of `netprobe syncreport`: every pair of the machines'
// OPENMM2_NET_TRACE files compared by their props lines (see PropDiff.cpp),
// named `names`. Prints nothing when fewer than two traces have props lines.
void propReport(const std::vector<std::string>& paths, const std::vector<std::string>& names);

} // namespace mm2::netprobe
