#pragma once

#include "data/PeResources.h"
#include "vfs/GameSource.h"

#include <optional>
#include <string>
#include <string_view>

namespace mm2::game {

// The game's localized text (MMLANG.DLL string table). Ids are those of the
// retail US build; see docs/formats/strings.md.
class Strings {
public:
    // Loads MMLANG.DLL from the game source. Returns an empty table (every
    // lookup falls back) when it is unavailable, so the game still runs.
    static Strings load(const vfs::GameSource& source);
    static Strings fromTable(data::PeStringTable table);

    bool loaded() const { return !m_table.strings.empty(); }
    // Text for `id`, or `fallback` when the id is missing.
    std::string get(std::uint32_t id, std::string_view fallback = {}) const;

    // Ids of notable entries (US build). Named here as they get used.
    enum Id : std::uint32_t {
        kReady = 89,
        kSet = 90,
        kGo = 91,
        kFalseStartPenalty = 92,
        kTimesUp = 94,
        kRaceOver = 100,
        kYouFinished1st = 169, // ... through kYouFinished1st + 7
        kFirstControlAction = 276,
        kLastControlAction = 309,
        kFirstCrashCourseLesson = 532, // London, then San Francisco (26 entries)
    };

private:
    data::PeStringTable m_table;
};

} // namespace mm2::game
