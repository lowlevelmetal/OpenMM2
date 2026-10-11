#pragma once

// The slot within a network test file's port block (tests/TestData.h lists
// the blocks) for this test process. ctest runs every test as a process of
// its own, several at a time; a slot from the clock let two of them pick the
// same port now and then (a bind failure in NetGameLobby on CI). Process ids
// of processes running at the same time differ, and on Linux they are handed
// out in order, so a slot from the id is distinct for them. Neighbouring
// processes get neighbouring slots: a block's slot spacing must cover every
// port one process uses.

#include <cstdint>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace mm2::test {

inline std::uint32_t processPortSlot(std::uint32_t slots) {
#ifdef _WIN32
    const auto id = static_cast<std::uint32_t>(_getpid()) / 4; // Windows process ids are multiples of 4
#else
    const auto id = static_cast<std::uint32_t>(getpid());
#endif
    return id % slots;
}

} // namespace mm2::test
