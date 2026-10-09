#pragma once

#include <string>

namespace mm2::netprobe {

// netprobe propdiff: compares two machines' OPENMM2_DEBUG_NETPROPS traces
// (see PropDiff.cpp). Returns the process exit code.
int propDiff(const std::string& pathA, const std::string& pathB);

} // namespace mm2::netprobe
