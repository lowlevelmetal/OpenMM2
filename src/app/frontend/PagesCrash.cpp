// Crash Course (driving school): school selection (ilon_bk, MM2
// CrashCourseIntro, menu 0x28) and the lesson screen (cclon_bk / ccsf_bk,
// MM2 CrashCourse, menu 0x27). Widget positions come from tune/widget.csv by
// the widget's creation index (given in the comments).
#include "app/frontend/Frontend.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <format>

namespace mm2::app::frontend {
namespace {

using ui::Box;
using ui::SpriteSheet;
using game::GameMode;

constexpr int kLessons = 13;

class CrashIntroPage final : public Page {
public:
    explicit CrashIntroPage(Frontend& fe) {
        menuId = menu_id::kCrashIntro;
        menu.background = "jpg/ilon_bk.jpg";
        onEnter(fe);
        const Vec2 lon = fe.layout.position(menuId, 0, {439, 359});
        const Vec2 sf = fe.layout.position(menuId, 1, {439, 415});
        menu.add<ui::SpriteButton>(SpriteSheet{"texture/cci_lon.tga", 4}, lon.x, lon.y, [&fe] {
            fe.push(makeCrashCoursePage(fe, "london"));
        }).sound = "Selectionmade";
        menu.add<ui::SpriteButton>(SpriteSheet{"texture/cci_sf.tga", 4}, sf.x, sf.y, [&fe] {
            fe.push(makeCrashCoursePage(fe, "sf"));
        }).sound = "Selectionmade";
        addBack(fe, *this);
        addNavStrip(fe, *this);
    }

    // CrashCourseIntro::PreSetup: entering sets the event to the crash
    // course, lesson 0 (also when coming back from the course page).
    void onEnter(Frontend& fe) override {
        fe.config.mode = GameMode::CrashCourse;
        fe.config.raceIndex = 0;
    }
};

// Tops of the curriculum rows painted on cclon_bk/ccsf_bk, in lesson order
// (lessons 1-3, midterm 1, lessons 4-6, midterm 2, lessons 7-9, midterm 3,
// final), and the columns of the pass tick and the fail cross (ccStatus).
constexpr float kLessonRowY[kLessons] = {98, 113, 128, 153, 183, 198, 213, 240, 267, 282, 297, 321, 351};
constexpr float kPassX = 179, kFailX = 225;

class CrashCoursePage final : public Page {
public:
    CrashCoursePage(Frontend& fe, std::string city) : m_city(std::move(city)) {
        menuId = menu_id::kCrashCourse;
        menu.background = m_city == "sf" ? "jpg/ccsf_bk.jpg" : "jpg/cclon_bk.jpg";
        fe.config.city = m_city;
        constexpr int id = menu_id::kCrashCourse;
        // CrashCourse::PreSetup keeps the event set up when it is a lesson or
        // a work-experience blitz or checkpoint race (coming back from one);
        // anything else becomes training (the intro has set lesson 0).
        switch (fe.config.mode) {
        case GameMode::Blitz:
            m_kind = Kind::Blitz;
            m_workRace = std::max(0, fe.config.raceIndex);
            break;
        case GameMode::Checkpoint:
            m_kind = Kind::Checkpoint;
            m_workRace = std::max(0, fe.config.raceIndex);
            break;
        case GameMode::CrashCourse: m_lesson = std::clamp(fe.config.raceIndex, 0, kLessons - 1); break;
        default: break;
        }

        // 0: TRAINING; 1-3: the lesson and its arrows.
        const Vec2 train = fe.layout.position(id, 0, {290, 109});
        menu.add<ui::LampItem>(
            SpriteSheet{"texture/cc_train.tga", 5}, train.x, train.y, [this] { return m_kind == Kind::Training; },
            [this, &fe] { select(fe, Kind::Training); });
        m_lessonBox = &menu.add<ui::ValueBox>(
            fe.layout.widget(id, 1, {404, 150, 205, 24}),
            [this, &fe] {
                std::vector<std::string> v;
                for (int i = 0; i < kLessons; ++i)
                    v.push_back(fe.ctx.game->strings.get(lessonStringId(i), std::format("Lesson {}", i + 1)));
                return v;
            },
            [this] { return m_lesson; }, [this](int i) { m_lesson = i; });
        m_lessonBox->optionEnabled = [this, &fe](int i) { return open(fe, "crash", i); };
        m_lessonArrows = addArrows(fe, 2, {610, 143}, {610, 161}, *m_lessonBox);

        // 4-5: the "work experience" blitz and checkpoint races; 6-8: the race
        // and its arrows.
        const Vec2 blitz = fe.layout.position(id, 4, {290, 237});
        menu.add<ui::LampItem>(
            SpriteSheet{"texture/cc_blitz.tga", 5}, blitz.x, blitz.y, [this] { return m_kind == Kind::Blitz; },
            [this, &fe] { select(fe, Kind::Blitz); });
        const Vec2 cp = fe.layout.position(id, 5, {290, 271});
        menu.add<ui::LampItem>(
            SpriteSheet{"texture/cc_cp.tga", 5}, cp.x, cp.y, [this] { return m_kind == Kind::Checkpoint; },
            [this, &fe] { select(fe, Kind::Checkpoint); });
        m_raceBox = &menu.add<ui::ValueBox>(
            fe.layout.widget(id, 6, {404, 315, 205, 24}),
            [this, &fe] {
                std::vector<std::string> v;
                for (const auto* r : fe.racesFor(workMode(), m_city))
                    v.push_back(r->name);
                return v;
            },
            [this] { return m_workRace; }, [this](int i) { m_workRace = i; });
        m_raceBox->optionEnabled = [this, &fe](int i) { return open(fe, game::modeKey(workMode()), i); };
        m_raceArrows = addArrows(fe, 7, {610, 307}, {610, 325}, *m_raceBox);

        // 9: GO. veh_go starts a lesson not yet passed at once in the school's
        // car; otherwise race_veh opens the garage (CrashCourse::SetVehicleNext).
        const Vec2 go = fe.layout.position(id, 9, layout::kNext);
        m_go = &menu.add<ui::SpriteButton>(SpriteSheet{"texture/race_veh.tga", 4}, go.x, go.y,
                                           [this, &fe] { proceed(fe); });
        addBack(fe, *this);
        addNavStrip(fe, *this);
        update(fe, 0.0);
    }

    void update(Frontend& fe, double) override {
        const bool training = m_kind == Kind::Training;
        // The inactive box and its arrows are hidden, not greyed.
        m_lessonBox->visible = training;
        m_lessonArrows.show(training);
        m_raceBox->visible = !training;
        m_raceArrows.show(!training);
        const bool schoolCar = training && !lessonPassed(fe);
        m_go->sheet.path = schoolCar ? "texture/veh_go.tga" : "texture/race_veh.tga";
        m_go->sound = schoolCar ? "Uigo" : "";
        m_go->soundVolume = 0.9f;
        // The help label shows the lesson's picture in training only.
        menu.defaultHelp = training ? std::format("jpg/{}_cc{}.jpg", m_city == "sf" ? "sf" : "lon", m_lesson) : "";
    }

    void drawAbove(Frontend& fe, ui::UiFrame& f) override {
        if (!fe.profile)
            return;
        // ccStatus: a tick for a passed lesson, a cross for one attempted and
        // failed, nothing for one never driven.
        for (int i = 0; i < kLessons; ++i) {
            const auto* rec = fe.profile->record(m_city, "crash", i);
            if (!rec)
                continue;
            ui::drawSpriteFrame(f, {"texture/cc_smchk.tga", 3}, rec->passed ? 1 : 2, rec->passed ? kPassX : kFailX,
                                kLessonRowY[i]);
        }
    }

private:
    enum class Kind { Training, Blitz, Checkpoint };

    struct Arrows {
        ui::SpriteButton* up = nullptr;
        ui::SpriteButton* down = nullptr;
        void show(bool on) const {
            up->visible = on;
            down->visible = on;
        }
    };

    // The roller_up / roller_down buttons beside a drop-down (clamping; they
    // do not step onto a locked entry).
    Arrows addArrows(Frontend& fe, int upIndex, Vec2 upCode, Vec2 downCode, ui::ValueBox& box) {
        const Vec2 u = fe.layout.position(menuId, upIndex, upCode);
        const Vec2 d = fe.layout.position(menuId, upIndex + 1, downCode);
        Arrows a;
        a.up = &menu.add<ui::SpriteButton>(SpriteSheet{"texture/roller_up.tga", 3}, u.x, u.y,
                                           [&box] { ui::stepOption(box, -1, false); });
        a.down = &menu.add<ui::SpriteButton>(SpriteSheet{"texture/roller_down.tga", 3}, d.x, d.y,
                                             [&box] { ui::stepOption(box, 1, false); });
        return a;
    }

    GameMode workMode() const { return m_kind == Kind::Checkpoint ? GameMode::Checkpoint : GameMode::Blitz; }

    bool open(Frontend& fe, std::string_view mode, int i) const {
        return fe.progress.raceOpen(fe.profile ? &*fe.profile : nullptr, m_city, mode, i);
    }

    bool lessonPassed(Frontend& fe) const {
        const auto* rec = fe.profile ? fe.profile->record(m_city, "crash", m_lesson) : nullptr;
        return rec && rec->passed;
    }

    // String table 532-544 London, 545-557 San Francisco.
    std::uint32_t lessonStringId(int i) const {
        return game::Strings::kFirstCrashCourseLesson + (m_city == "sf" ? 13u : 0u) + static_cast<std::uint32_t>(i);
    }

    // The lamps are radio buttons; picking one goes back to the first
    // (open) entry of its list.
    void select(Frontend& fe, Kind kind) {
        m_kind = kind;
        m_lesson = 0;
        m_workRace = 0;
        if (kind != Kind::Training) {
            const int n = static_cast<int>(fe.racesFor(workMode(), m_city).size());
            for (int i = 0; i < n; ++i)
                if (open(fe, game::modeKey(workMode()), i)) {
                    m_workRace = i;
                    break;
                }
        }
    }

    void proceed(Frontend& fe) {
        auto& cfg = fe.config;
        cfg.city = m_city;
        // mmInterface::Update, Crash Course GO: after the race the menus
        // come back to this page, for lessons and work experience alike.
        setCrashCourseReturn(true);
        if (m_kind == Kind::Training) {
            cfg.mode = GameMode::CrashCourse;
            cfg.raceIndex = m_lesson;
            fe.applyRaceDefaults(cfg);
            if (!lessonPassed(fe)) {
                // mmInterface::Update (Crash Course GO), mmSingleStunt::NextRace:
                // a lesson not yet passed is driven in the school's car.
                cfg.vehicle = m_city == "sf" ? "vpbullet" : "vpcab";
                cfg.vehicleColor = 0;
                fe.startRace();
                return;
            }
            fe.push(makeVehiclePage(fe));
            return;
        }
        // "Work experience": the city's regular races with their defaults.
        cfg.mode = workMode();
        cfg.raceIndex = m_workRace;
        fe.applyRaceDefaults(cfg);
        fe.push(makeVehiclePage(fe));
    }

    std::string m_city;
    Kind m_kind = Kind::Training;
    int m_lesson = 0;
    int m_workRace = 0;
    ui::ValueBox* m_lessonBox = nullptr;
    ui::ValueBox* m_raceBox = nullptr;
    Arrows m_lessonArrows;
    Arrows m_raceArrows;
    ui::SpriteButton* m_go = nullptr;
};

} // namespace

std::unique_ptr<Page> makeCrashIntroPage(Frontend& fe) { return std::make_unique<CrashIntroPage>(fe); }
std::unique_ptr<Page> makeCrashCoursePage(Frontend& fe, std::string city) {
    return std::make_unique<CrashCoursePage>(fe, std::move(city));
}

} // namespace mm2::app::frontend
