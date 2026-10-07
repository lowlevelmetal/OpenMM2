// Crash Course (driving school): school selection (ilon_bk) and the lesson
// screen (cclon_bk / ccsf_bk).
#include "app/frontend/Frontend.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <format>

namespace mm2::app::frontend {
namespace {

using ui::Box;
using ui::SpriteSheet;
using game::GameMode;
using namespace layout;

class CrashIntroPage final : public Page {
public:
    explicit CrashIntroPage(Frontend& fe) {
        menu.background = "jpg/ilon_bk.jpg";
        menu.defaultHelp = "jpg/mn_cc.jpg";
        auto& lon = menu.add<ui::SpriteButton>(SpriteSheet{"texture/cci_lon.tga", 4}, kColumnX, kRow56,
                                               [&fe] { fe.push(makeCrashCoursePage(fe, "london")); });
        menu.add<ui::SpriteButton>(SpriteSheet{"texture/cci_sf.tga", 4}, kColumnX, kRow65,
                                   [&fe] { fe.push(makeCrashCoursePage(fe, "sf")); });
        addBack(fe, *this);
        addNavStrip(fe, *this);
        menu.focus(&lon);
    }
};

// Rows of the curriculum painted on cclon_bk/ccsf_bk, in lesson order
// (lesson1-3, midterm 1, lesson4-6, midterm 2, lesson7-9, midterm 3, final).
constexpr float kLessonRowY[] = {102, 116, 130, 158, 186, 200, 214, 242, 270, 284, 298, 326, 354};
constexpr float kPassX = 194, kFailX = 236;

class CrashCoursePage final : public Page {
public:
    CrashCoursePage(Frontend& fe, std::string city) : m_city(std::move(city)) {
        menu.background = m_city == "sf" ? "jpg/ccsf_bk.jpg" : "jpg/cclon_bk.jpg";
        menu.defaultHelp = "jpg/mn_cc.jpg";
        fe.config.city = m_city;

        menu.add<ui::LampItem>(
            SpriteSheet{"texture/cc_train.tga", 5}, 290, 109, [this] { return m_training; },
            [this] { m_training = true; });
        menu.add<ui::LampItem>(
            SpriteSheet{"texture/cc_blitz.tga", 5}, 290, 237, [this] { return !m_training && m_work == GameMode::Blitz; },
            [this] {
                m_training = false;
                m_work = GameMode::Blitz;
                m_workRace = 0;
            });
        menu.add<ui::LampItem>(
            SpriteSheet{"texture/cc_cp.tga", 5}, 290, 271,
            [this] { return !m_training && m_work == GameMode::Checkpoint; },
            [this] {
                m_training = false;
                m_work = GameMode::Checkpoint;
                m_workRace = 0;
            });
        m_lessonBox = &menu.add<ui::ValueBox>(
            Box{kBoxX, 146, kBoxWide, kBoxH}, [this, &fe] { return lessonNames(fe); }, [this] { return m_lesson; },
            [this](int i) { m_lesson = i; });
        m_raceBox = &menu.add<ui::ValueBox>(
            Box{kBoxX, 310, kBoxWide, kBoxH}, [this, &fe] { return workRaceNames(fe); }, [this] { return m_workRace; },
            [this](int i) { m_workRace = i; });
        addBack(fe, *this);
        auto& next = menu.add<ui::SpriteButton>(SpriteSheet{"texture/race_veh.tga", 4}, kNext.x, kNext.y,
                                                [this, &fe] { proceed(fe); });
        addNavStrip(fe, *this);
        menu.focus(&next);
        if (fe.profile)
            m_lesson = std::max(0, fe.progress.availableRaces(*fe.profile, m_city, "crash") - 1);
    }

    void update(Frontend&, double) override {
        m_lessonBox->enabled = m_training;
        m_raceBox->enabled = !m_training;
    }

    void drawAbove(Frontend& fe, ui::UiFrame& f) override {
        if (!fe.profile)
            return;
        for (int i = 0; i < 13; ++i) {
            const std::string key = std::format("{}.{}", m_city, i);
            const float y = kLessonRowY[i] - 6;
            const bool passed = fe.profile->crashPassed.contains(key);
            const bool failed = !passed && fe.profile->crashFailed.contains(key);
            ui::drawSpriteFrame(f, {"texture/cc_smchk.tga", 3}, passed ? 1 : 0, kPassX, y);
            ui::drawSpriteFrame(f, {"texture/cc_smchk.tga", 3}, failed ? 2 : 0, kFailX, y);
        }
    }

private:
    // String table 532-544 London, 545-557 San Francisco.
    std::uint32_t lessonStringId(int i) const {
        return game::Strings::kFirstCrashCourseLesson + (m_city == "sf" ? 13u : 0u) + static_cast<std::uint32_t>(i);
    }

    std::vector<std::string> lessonNames(Frontend& fe) const {
        std::vector<std::string> v;
        const int n = fe.profile ? fe.progress.availableRaces(*fe.profile, m_city, "crash") : 1;
        for (int i = 0; i < n && i < 13; ++i)
            v.push_back(fe.ctx.game->strings.get(lessonStringId(i), std::format("Lesson {}", i + 1)));
        return v;
    }

    std::vector<std::string> workRaceNames(Frontend& fe) const {
        std::vector<std::string> v;
        const auto races = fe.racesFor(m_work, m_city);
        const int n = fe.profile ? fe.progress.availableRaces(*fe.profile, m_city, game::modeKey(m_work)) : 1;
        for (int i = 0; i < n && i < static_cast<int>(races.size()); ++i)
            v.push_back(races[static_cast<std::size_t>(i)]->name);
        return v;
    }

    void proceed(Frontend& fe) {
        auto& cfg = fe.config;
        cfg.city = m_city;
        if (m_training) {
            // Lessons are driven in the school's car (inferred: the London
            // school is a cab company; the San Francisco stunt school's car is
            // not known, so the player's choice is kept there).
            cfg.mode = GameMode::CrashCourse;
            cfg.raceIndex = m_lesson;
            cfg.opponents = 0;
            if (m_city == "london")
                cfg.vehicle = "vpcab";
            const auto races = fe.racesFor(GameMode::CrashCourse, m_city);
            if (m_lesson < static_cast<int>(races.size()) && races[static_cast<std::size_t>(m_lesson)]->settings) {
                const auto& s = races[static_cast<std::size_t>(m_lesson)]->settings->amateur;
                cfg.timeOfDay = static_cast<game::TimeOfDay>(std::clamp(s.timeOfDay, 0, 3));
                cfg.weather = static_cast<game::Weather>(std::clamp(s.weather, 0, 3));
            }
            if (m_city == "london")
                fe.startRace();
            else
                fe.push(makeVehiclePage(fe));
        } else {
            // "Work experience": the city's regular races, not for credit.
            cfg.mode = m_work;
            cfg.raceIndex = m_workRace;
            fe.push(makeVehiclePage(fe));
        }
    }

    std::string m_city;
    bool m_training = true;
    GameMode m_work = GameMode::Blitz;
    int m_lesson = 0;
    int m_workRace = 0;
    ui::ValueBox* m_lessonBox = nullptr;
    ui::ValueBox* m_raceBox = nullptr;
};

} // namespace

std::unique_ptr<Page> makeCrashIntroPage(Frontend& fe) { return std::make_unique<CrashIntroPage>(fe); }
std::unique_ptr<Page> makeCrashCoursePage(Frontend& fe, std::string city) {
    return std::make_unique<CrashCoursePage>(fe, std::move(city));
}

} // namespace mm2::app::frontend
