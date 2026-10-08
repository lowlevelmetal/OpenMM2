// The results after a race or lesson (MM2 PUResults). MM2 shows them in the
// game as a popup over the running race (mmPopup::ShowResults does not
// pause it); OpenMM2 shows the same layout as
// the first page after the race. Positions are PUResults::Init640's,
// as fractions of the 640x480 screen.
#include "app/frontend/Frontend.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <format>

namespace mm2::app::frontend {
namespace {

using game::GameMode;

// The crash course school cars (mmInterface::Update, Crash Course GO;
// mmSingleStunt::NextRace): an unpassed lesson is driven in them.
const char* schoolCar(std::string_view city) { return str::iequals(city, "sf") ? "vpbullet" : "vpcab"; }

class ResultsPage final : public Page {
public:
    ResultsPage(Frontend& fe, const game::RaceResult& r, std::optional<game::Reward> reward)
        : m_result(r), m_reward(std::move(reward)) {
        const bool crash = r.config.mode == GameMode::CrashCourse;
        const auto& s = fe.ctx.game->strings;
        menu.background = crash ? "jpg/crshi_bk.jpg" : "jpg/rshi_bk.jpg";
        menu.popupSounds = true;
        // Text buttons at x 0.6875 from y 0.15625, each the popup's button
        // height (0.1 of the screen, PUMenuBase::PUMenuBase) below the last,
        // 0.2109375 wide, the last 0.3125 (PUResults::Init640). The
        // multiplayer roster button between Next and Race Menu is not shown
        // after single-player races.
        constexpr float x = 440, w = 135, lineH = 48, lastW = 200;
        float y = 75;
        // UIMenu::AddButton in screen fractions, UIButton type 0 (centred
        // vertically, from the box's left) in PUMenuBase's GetFont 24.
        auto add = [&](std::string label, std::function<void()> fn, float width) -> ui::TextButton& {
            auto& b = menu.add<ui::TextButton>(ui::Box{x, y, width, lineH}, std::move(label), std::move(fn));
            b.type = 0;
            b.font = ui::style::popupButtonFont();
            y += lineH;
            return b;
        };
        auto& restart = add(
            crash ? s.get(653, "Restart Lesson") : s.get(492, "Restart Race"),
            [this, &fe] {
                fe.config = m_result.config;
                fe.startRace();
            },
            w);
        auto& next = add(
            crash ? s.get(654, "Next Lesson") : s.get(493, "Next Race"), [this, &fe] { nextRace(fe); }, w);
        next.enabled = hasNextRace(fe);
        add(crash ? s.get(496, "Back to School") : s.get(497, "Race Menu"), [&fe] { fe.pop(); }, w);
        add(s.get(498, "Exit to Windows"), [&fe] { fe.askQuit(); }, lastW);
        menu.setInitialFocus(&restart);
        menu.onBack = [&fe] { fe.pop(); };
    }

    void drawAbove(Frontend& fe, ui::UiFrame& f) override {
        const auto& s = fe.ctx.game->strings;
        const auto& cfg = m_result.config;
        const auto font = ui::style::popupFont();
        const float lh = f.text.lineHeight(f.overlay, font);

        // Title (two lines at 0.0625, 0.825): the mode, then the race.
        std::string mode, race = fe.raceName(cfg);
        switch (cfg.mode) {
        case GameMode::Circuit: mode = s.get(5, "Circuit Race"); break;
        case GameMode::Checkpoint: mode = s.get(6, "Checkpoint Race"); break;
        case GameMode::Blitz: mode = s.get(7, "Blitz Race"); break;
        case GameMode::CrashCourse:
            mode = s.get(12, "Crash Course");
            race = s.get(static_cast<std::uint32_t>(game::Strings::kFirstCrashCourseLesson +
                                                    (str::iequals(cfg.city, "sf") ? 13 : 0) +
                                                    std::max(0, cfg.raceIndex)),
                         race);
            break;
        default: mode = fe.raceName(cfg); race.clear(); break;
        }
        f.text.draw(f.overlay, font, mode, 40, 396, ui::style::kPopupText);
        f.text.draw(f.overlay, font, race, 40, 396 + lh, ui::style::kPopupText);

        // The table (0.0625, 0.15625): place, name, time for up to ten
        // finishers (PUResults::AddName); unfinished racers are not listed
        // because OpenMM2 leaves the race before they arrive.
        const float nameX = 40 + 3 * f.text.measure(f.overlay, font, "ABCEFGHIJKLMNOPQR") / 18.0f;
        const float timeX = 40 + 24 * f.text.measure(f.overlay, font, "ABCEFGHIJKLMNOPQR") / 18.0f;
        float y = 75;
        int row = 0;
        auto line = [&](const std::string& place, const std::string& name, const std::string& value) {
            if (row++ >= 10)
                return;
            f.text.draw(f.overlay, font, place, 40, y, ui::style::kPopupText);
            f.text.draw(f.overlay, font, name, nameX, y, ui::style::kPopupText);
            f.text.draw(f.overlay, font, value, timeX, y, ui::style::kPopupText);
            y += lh;
        };
        const std::string player = fe.profile ? fe.profile->name : s.get(3, "Player");
        if (cfg.mode == GameMode::CrashCourse) {
            // Inferred: the lesson's outcome on the driver's line.
            line("1", player, m_result.won ? s.get(651, "Pass") : s.get(499, "DNF"));
        } else if (!m_result.standings.empty()) {
            for (const auto& st : m_result.standings) {
                std::string name = player;
                if (!st.name.empty()) {
                    name = st.name; // a network player (mmGameMulti::UpdateResults)
                } else if (st.opponent >= 0) {
                    // PUResults::AddName from UpdateOpponentStatus: the
                    // mode's "Opponent N" (strings 13-20, mmGame's list).
                    name = s.get(13 + static_cast<std::uint32_t>(std::min(st.opponent, 7)),
                                 std::format("Opponent {}", st.opponent + 1));
                }
                line(std::to_string(st.place), name, st.dnf ? s.get(499, "DNF") : formatTime(st.timeSeconds));
            }
        } else if (cfg.mode != GameMode::Cruise) {
            line("", player, s.get(499, "DNF")); // PUResults::AddLoser
        }

        // The reward message (0.453125, 0.825, 0.51875 wide).
        if (m_reward)
            f.text.drawWrapped(f.overlay, font, m_reward->message, 290, 396, 332, ui::style::kPopupText);
    }

private:
    // When "Next" is offered (mmSingleRace/Blitz/Circuit/Stunt::NextRaceAvailable).
    bool hasNextRace(Frontend& fe) const {
        const auto& cfg = m_result.config;
        if (cfg.raceIndex < 0)
            return false;
        const int count = static_cast<int>(fe.racesFor(cfg.mode, cfg.city).size());
        switch (cfg.mode) {
        case GameMode::Checkpoint:
            return cfg.raceIndex + 1 < count &&
                   fe.progress.raceOpen(fe.profile ? &*fe.profile : nullptr, cfg.city, "race", cfg.raceIndex + 1);
        case GameMode::Blitz:
        case GameMode::Circuit: return cfg.raceIndex + 1 < count;
        case GameMode::CrashCourse:
            // Never into a midterm or the final.
            return cfg.raceIndex != 2 && cfg.raceIndex != 6 && cfg.raceIndex < 10;
        default: return false;
        }
    }

    void nextRace(Frontend& fe) {
        fe.config = m_result.config;
        ++fe.config.raceIndex;
        fe.applyRaceDefaults(fe.config);
        if (fe.config.mode == GameMode::CrashCourse) {
            const auto* rec = fe.profile ? fe.profile->record(fe.config.city, "crash", fe.config.raceIndex) : nullptr;
            if (!rec || !rec->passed) {
                fe.config.vehicle = schoolCar(fe.config.city);
                fe.config.vehicleColor = 0;
            }
        }
        fe.startRace();
    }

    game::RaceResult m_result;
    std::optional<game::Reward> m_reward;
};

} // namespace

std::unique_ptr<Page> makeResultsPage(Frontend& fe, const game::RaceResult& result, std::optional<game::Reward> reward) {
    return std::make_unique<ResultsPage>(fe, result, std::move(reward));
}

} // namespace mm2::app::frontend
