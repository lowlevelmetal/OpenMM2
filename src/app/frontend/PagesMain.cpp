// Loading screen, main menu (driver selection), new driver, driver record
// and race records.
#include "app/frontend/Frontend.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <array>
#include <format>

namespace mm2::app::frontend {
namespace {

using ui::Box;
using ui::SpriteSheet;
using game::GameMode;

// Shortens text that is wider than its column (Dialog_DriverRec::
// AddDriverRecord, Dialog_HallOfFame::AddRaceRecord): it keeps N + 1
// characters, N read from the string table (`lengthString`, a number; a
// missing string counts as 0), and gets "...". The widths are screen
// fractions in MM2: 0.207 (132.48 px) or 0.15 (96 px).
std::string fit(Frontend& fe, ui::UiFrame& f, const ui::FontSpec& font, std::string text, float width,
                std::uint32_t lengthString) {
    if (!(f.text.measure(f.overlay, font, text) > width))
        return text;
    const auto n = str::parseInt(fe.ctx.game->strings.get(lengthString, ""));
    const std::size_t keep = static_cast<std::size_t>(std::max<long long>(0, n.value_or(0))) + 1;
    // Whole UTF-8 characters (MM2 counts a double-byte character as one).
    std::size_t pos = 0;
    for (std::size_t chars = 0; pos < text.size() && chars < keep; ++chars)
        ui::nextCodepoint(text, pos);
    return text.substr(0, pos) + "...";
}

constexpr float kWideColumn = 0.207f * 640.0f;  // 132.48 px
constexpr float kNarrowColumn = 0.15f * 640.0f; // 96 px

// The controller named on the main menu (mmInterface::PlayerFillStats ->
// MenuManager::GetControllerName, strings 580-584: Mouse, Keyboard,
// Joystick, Game Pad, Steering Wheel): the type the Control page's
// CONTROLLER box writes.
std::string controllerName(Frontend& fe) {
    const auto& s = fe.ctx.game->strings;
    const auto i = std::clamp<long long>(fe.ctx.settings.ini.getInt("Controls", "Controller", 1), 0, 4);
    return s.get(static_cast<std::uint32_t>(580 + i));
}

// --- Loading screen ------------------------------------------------------------------------

// splash.jpg with MM2's loading bar (ProgressCB -> ProgressRect): a flat
// #0D2CBA bar at (349,448), 10 px tall and percent * 640 / 284 px wide in
// whole pixels (225 px at 100 %). mmInterface::mmInterface reports 20, 30,
// 35, 40, 50, 55, 60, 65, 70, 75, 80, 85, 90, 95, 97, 99 and 100 % while the
// frontend loads (lvlProgress::UpdateTask; BeginTask's 0 draws nothing) and
// then shows the main menu. OpenMM2 has already loaded, so it shows one step
// per frame (pacing inferred).
class LoadingPage final : public Page {
public:
    explicit LoadingPage(Frontend&) { menu.background = "jpg/splash.jpg"; }

    void update(Frontend& fe, double) override {
        if (++m_step >= static_cast<int>(kSteps.size()))
            fe.replace(makeDriverPage(fe));
    }

    void drawAbove(Frontend&, ui::UiFrame& f) override {
        if (m_step < 0 || m_step >= static_cast<int>(kSteps.size()))
            return;
        const int width = kSteps[static_cast<std::size_t>(m_step)] * 640 / 284;
        f.overlay.rect(349, 448, static_cast<float>(width), 10, render::packColor(13, 44, 186));
    }

private:
    static constexpr std::array<int, 17> kSteps = {20, 30, 35, 40, 50, 55, 60, 65, 70,
                                                   75, 80, 85, 90, 95, 97, 99, 100};
    int m_step = -1;
};

// --- Main menu (main_bk) -----------------------------------------------------------------

// MM2 `MainMenu::MainMenu` / `InitDriver` (menu 1); widget indices are the
// creation order and tune/widget.csv rows.
class DriverPage final : public Page {
public:
    explicit DriverPage(Frontend& fe) {
        menu.background = "jpg/main_bk.jpg";
        menuId = menu_id::kMain;
        auto pos = [&fe](int i, Vec2 code) { return fe.layout.position(menu_id::kMain, i, code); };
        auto button = [&](int i, const char* sprite, Vec2 code, const char* help, std::function<void()> fn) {
            const Vec2 p = pos(i, code);
            auto& b = menu.add<ui::SpriteButton>(SpriteSheet{sprite, 4}, p.x, p.y, std::move(fn));
            b.help = help;
            return &b;
        };
        auto& crash = *button(0, "texture/dvrcc.tga", {439, 242}, "jpg/mn_cc.jpg", [&fe] {
            fe.push(makeCrashIntroPage(fe));
        });
        button(1, "texture/main_sp.tga", {439, 301}, "jpg/mn_sp.jpg", [&fe] { fe.push(makeRacesPage(fe)); });
        button(2, "texture/main_mp.tga", {439, 359}, "jpg/mn_mp.jpg", [&fe] {
            leaveCrashCourse(fe);
            setCrashCourseReturn(false);
            fe.push(makeSessionsPage(fe));
        });
        // Quick Race: the garage with the event currently set up, which is
        // the driver's last one unless the race menu changed it since
        // (mmInterface::Update, main menu QUICK RACE; mmInterface::Switch to
        // the garage turns Cops & Robbers into cruise).
        button(3, "texture/main_qck.tga", {439, 415}, "jpg/mn_qck.jpg", [&fe] {
            leaveCrashCourse(fe);
            setCrashCourseReturn(false);
            fe.push(makeVehiclePage(fe));
        });

        // DRIVER: a drop-down of the drivers in creation order with roller
        // arrows that wrap (MainMenu::DecPlayer / IncPlayer).
        const Box nameBox = fe.layout.widget(menu_id::kMain, 4, {177, 128, 205, 24});
        m_driver = &menu.add<ui::ValueBox>(
            nameBox, [this] { return m_names; },
            [this, &fe] {
                for (std::size_t i = 0; i < m_names.size(); ++i)
                    if (fe.profile && m_names[i] == fe.profile->name)
                        return static_cast<int>(i);
                return -1;
            },
            [this, &fe](int i) {
                if (i >= 0 && i < static_cast<int>(m_names.size()))
                    fe.selectProfile(m_names[static_cast<std::size_t>(i)]);
            });
        m_driver->help = "jpg/mn_drv.jpg";
        const Vec2 up = pos(5, {384, 121}), down = pos(6, {384, 139});
        menu.add<ui::SpriteButton>(SpriteSheet{"texture/roller_up.tga", 3}, up.x, up.y,
                                   [this] { ui::stepOption(*m_driver, -1, true); });
        menu.add<ui::SpriteButton>(SpriteSheet{"texture/roller_down.tga", 3}, down.x, down.y,
                                   [this] { ui::stepOption(*m_driver, 1, true); });

        auto* create = button(7, "texture/dvrnew.tga", {40, 156}, "jpg/mn_new.jpg", [&fe] {
            if (static_cast<int>(fe.store.list().size()) >= game::ProfileStore::kMaxDrivers)
                fe.notice("jpg/plim_dlg.jpg", 33); // "cannot have more than 18 drivers"
            else
                fe.push(makeNewDriverDialog(fe));
        });
        auto* remove = button(8, "texture/dvrdel.tga", {40, 216}, "jpg/mn_del.jpg", [this, &fe] { askDelete(fe); });
        auto* stats = button(9, "texture/dvrsts.tga", {40, 283}, "jpg/mn_sts.jpg",
                             [&fe] { fe.push(makeDriverStatsDialog(fe)); });
        for (auto* b : {create, remove, stats}) {
            b->sound = "Moveselector"; // UIBMButton sound slot 9
            b->soundVolume = 0.9f;
        }
        // REPLAY is created and switched off at once (MainMenu::EnableReplay
        // is never called).
        button(10, "texture/main_rpl.tga", {267, 430}, "jpg/mn_rpl.jpg", [] {})->visible = false;
        button(11, "texture/race_rec.tga", {264, 388}, "jpg/mn_rec.jpg", [&fe] {
            fe.playSound("UIrecords", 0.84f);
            fe.push(makeRaceRecordsDialog(fe));
        });

        addNavStrip(fe, *this);
        menu.setInitialFocus(&crash); // MainMenu::MainMenu: SetFocusWidget after DVRCC
        // Escape asks to quit (mmInterface::Update, main menu back); the
        // Escape key's own sound plays only in the in-game popups.
        menu.onBack = [&fe] { fe.askQuit(); };
        refresh(fe);
    }

    void onEnter(Frontend& fe) override { refresh(fe); }

    void update(Frontend& fe, double) override {
        const std::string current = fe.profile ? fe.profile->name : std::string();
        if (current != m_lastProfile) {
            m_lastProfile = current;
            refresh(fe);
        }
    }

    void drawBelow(Frontend&, ui::UiFrame& f) override {
        // The drop-down's own box picture (UITextDropdown with dropdown_bx).
        ui::drawImage(f.overlay, f.textures.get("texture/dropdown_bx.tga"), m_driver->box.x + 1, m_driver->box.y + 1);
    }

    // The driver panel (MainMenu::DisplayDriverInfo, filled by
    // mmInterface::PlayerFillStats): labels and values on alternate lines.
    void drawAbove(Frontend& fe, ui::UiFrame& f) override {
        if (!fe.profile)
            return;
        const auto& s = fe.ctx.game->strings;
        const auto& p = *fe.profile;
        const auto font = ui::style::valueFont();
        constexpr float kLines[] = {165, 180, 196, 212, 228, 244, 260, 276, 292, 308};
        auto put = [&](int line, const std::string& text) {
            f.text.draw(f.overlay, font, text, line % 2 == 0 ? 177.0f : 209.0f, kLines[line], ui::style::kValueText);
        };
        const bool pro = p.difficulty == game::Difficulty::Professional;
        put(0, s.get(635, "RANKING:"));
        put(1, pro ? s.get(81, "Professional") : s.get(82, "Amateur"));
        game::RaceConfig last;
        last.mode = p.mode;
        last.city = p.city;
        last.raceIndex = p.raceIndex;
        put(2, s.get(636, "LAST RACE:"));
        put(3, fe.raceName(last));
        const auto* v = fe.ctx.game->catalog.vehicle(p.vehicle);
        put(4, s.get(637, "LAST VEHICLE:"));
        put(5, v ? v->description : s.get(64, "---"));
        put(6, s.get(638, "CONTROLLER:"));
        put(7, controllerName(fe));
        // The score only for professionals: London plus San Francisco.
        if (pro) {
            put(8, s.get(640, "SCORE:"));
            put(9, std::format("{:5}", fe.progress.totalScore(p)));
        }
    }

private:
    static void leaveCrashCourse(Frontend& fe) {
        if (fe.config.mode == GameMode::CrashCourse || fe.config.mode == GameMode::CopsAndRobbers) {
            fe.config.mode = GameMode::Cruise;
            fe.config.raceIndex = -1;
            fe.applyRaceDefaults(fe.config);
        }
    }

    void refresh(Frontend& fe) {
        m_names.clear();
        for (const auto& p : fe.store.list())
            m_names.push_back(p.name);
    }

    // DVRDEL (mmInterface::Update): the only driver cannot be deleted
    // (lstp_dlg); otherwise delp_dlg, YES first.
    void askDelete(Frontend& fe) {
        if (!fe.profile)
            return;
        if (m_names.size() < 2) {
            fe.notice("jpg/lstp_dlg.jpg", 30);
            return;
        }
        fe.dialog("jpg/delp_dlg.jpg", menu_id::kDeleteDriver,
                  {{"texture/dlg_yes.tga", {180, 176},
                    [this, &fe] {
                        // mmInterface::PlayerRemove: the oldest remaining driver is loaded.
                        fe.store.remove(*fe.profile);
                        fe.profile.reset();
                        refresh(fe);
                        if (!m_names.empty())
                            fe.selectProfile(m_names.front());
                    }},
                   {"texture/dlg_no.tga", {18, 176}, {}}});
    }

    std::vector<std::string> m_names;
    std::string m_lastProfile;
    ui::ValueBox* m_driver = nullptr;
};

// --- Create a New Driver (newp_dlg) ------------------------------------------------------

// MM2 `Dialog_NewPlayer` (dialog 17): name field, Amateur / Professional
// radio boxes, OK (dlg_done, right) and Cancel (dlg_can, left).
class NewDriverDialog final : public Page {
public:
    explicit NewDriverDialog(Frontend& fe) {
        dialog = true;
        dialogPicture = "jpg/newp_dlg.jpg";
        origin = ui::dialogOrigin({400, 330});
        menuId = menu_id::kNewDriver;
        const auto& l = fe.layout;
        constexpr int id = menu_id::kNewDriver;
        auto& entry = menu.add<ui::TextEntry>(l.widget(id, 0, {72, 89, 203, 22}, origin), &m_name,
                                              game::ProfileStore::kMaxNameLength);
        entry.onCommit = [this, &fe] { create(fe); }; // Enter in the field is OK
        auto radio = [&](int index, Vec2 code, game::Difficulty d) {
            const Vec2 p = l.position(id, index, code, origin);
            auto& r = menu.add<ui::LampItem>(SpriteSheet{"texture/checkbox.tga", 5}, p.x, p.y,
                                             [this, d] { return m_difficulty == d; },
                                             [this, d] { m_difficulty = d; });
            r.sound = "Selectionmade";
        };
        radio(1, {72, 161}, game::Difficulty::Amateur);
        radio(2, {72, 211}, game::Difficulty::Professional);
        // Widget 3 is the rank description label, created hidden (its pictures
        // are not in the data).
        const Vec2 ok = l.position(id, 4, {280, 276}, origin), cancel = l.position(id, 5, {18, 276}, origin);
        menu.add<ui::SpriteButton>(SpriteSheet{"texture/dlg_done.tga", 4}, ok.x, ok.y, [this, &fe] { create(fe); })
            .sound = "Selectionmade";
        menu.add<ui::SpriteButton>(SpriteSheet{"texture/dlg_can.tga", 4}, cancel.x, cancel.y, [&fe] { fe.pop(); })
            .sound = "Selectionmade";
        menu.onBack = [&fe] { fe.pop(); };
        menu.setInitialFocus(&entry);
    }

private:
    // mmInterface::PlayerCreateCB / PlayerCreate.
    void create(Frontend& fe) {
        const std::string name = m_name;
        const game::Difficulty difficulty = m_difficulty;
        fe.pop();
        if (name.empty())
            return; // an empty name does nothing (a name of spaces is a name)
        game::ProfileStore::CreateError error{};
        auto p = fe.store.create(name, &error);
        if (!p) {
            if (error == game::ProfileStore::CreateError::TooMany)
                fe.notice("jpg/plim_dlg.jpg", 33);
            else if (error == game::ProfileStore::CreateError::Duplicate)
                fe.notice("jpg/dupp_dlg.jpg", menu_id::kDuplicateDriver,
                          [&fe] { fe.push(makeNewDriverDialog(fe)); });
            return;
        }
        p->difficulty = difficulty;
        p->netName = fe.ctx.game->strings.get(77, "noname");
        // The new driver has no last car or event: mmInterface::PlayerSetState
        // then sets up cruise in "vpbug" (the profile's defaults).
        p->city = fe.config.city;
        p->save();
        fe.selectProfile(p->name);
        if (Page* top = fe.top())
            top->onEnter(fe);
    }

    std::string m_name;
    game::Difficulty m_difficulty = game::Difficulty::Amateur;
};

// --- Record dialogs (drec_dlg, hoff_dlg) ----------------------------------------------------

// The shared parts of MM2's Dialog_DriverRec (19) and Dialog_HallOfFame (20):
// a 540x460 picture centred at (50,10), mode and city check boxes, DONE.
// Both open on Blitz in San Francisco (DEFAULT_CITY).
class RecordDialog : public Page {
public:
    enum class Table { AmateurTimes, ProTimes, ProPoints };

    // The race records have three table boxes (widgets 1-3) before the
    // mode boxes.
    RecordDialog(Frontend& fe, const char* picture, int id, bool tables) : m_id(id) {
        dialog = true;
        dialogPicture = picture;
        origin = ui::dialogOrigin({540, 460});
        menuId = id;
        const int firstRadio = tables ? 4 : 1;
        if (tables) {
            const Table kinds[] = {Table::AmateurTimes, Table::ProTimes, Table::ProPoints};
            for (int i = 0; i < 3; ++i) {
                const Table t = kinds[i];
                radio(fe, 1 + i, {200, 323 + 25.0f * static_cast<float>(i)}, [this, t] { return m_table == t; },
                      [this, t] {
                          m_table = t;
                          m_scroll = 0;
                      });
            }
        }
        const GameMode modes[] = {GameMode::Blitz, GameMode::Circuit, GameMode::Checkpoint};
        for (int i = 0; i < 3; ++i) {
            const GameMode m = modes[i];
            radio(fe, firstRadio + i, {40, 323 + 25.0f * static_cast<float>(i)}, [this, m] { return m_mode == m; },
                  [this, m] {
                      m_mode = m;
                      m_scroll = 0;
                  });
        }
        const char* cities[] = {"sf", "london"};
        for (int i = 0; i < 2; ++i) {
            const std::string c = cities[i];
            radio(fe, firstRadio + 3 + i, {360, 323 + 25.0f * static_cast<float>(i)}, [this, c] { return m_city == c; },
                  [this, c] {
                      m_city = c;
                      m_scroll = 0;
                  });
        }
        const Vec2 done = fe.layout.position(id, firstRadio + 5, {421, 418}, origin);
        m_done = &menu.add<ui::SpriteButton>(SpriteSheet{"texture/dlg_done.tga", 4}, done.x, done.y, [&fe] { fe.pop(); });
        m_done->sound = "Selectionmade";
        menu.onBack = [&fe] { fe.pop(); };
    }

protected:
    void radio(Frontend& fe, int index, Vec2 code, std::function<bool()> on, std::function<void()> pick) {
        const Vec2 p = fe.layout.position(m_id, index, code, origin);
        auto& r = menu.add<ui::LampItem>(SpriteSheet{"texture/dlg_chkb.tga", 5}, p.x, p.y, std::move(on), std::move(pick));
        r.sound = "Selectionmade";
    }

    int m_id;
    Table m_table = Table::AmateurTimes;
    GameMode m_mode = GameMode::Blitz;
    std::string m_city = "sf";
    int m_scroll = 0;
    ui::SpriteButton* m_done = nullptr;
};

// MM2 `Dialog_DriverRec` (mmInterface::PlayerFillRecords, mmCompDRecord):
// every race of the mode in the city with a lock or tick, the best time,
// the car that set it and, for professionals, the best score. The list is a
// UICompositeScroll at 0.0578, 0.1674 of the 540x460 dialog, 0.8 of it wide
// (432 px), 12 rows of 0.03746 of the screen (18 px), no scroll bar: rows
// from (81,87). mmCompDRecord::SetSubwidgetGeometry and SetBltXY put the lock
// at the row's left edge 3 px up, the race at 18 px, the time a third of the
// width further, then the vehicle (a quarter further for professionals, a
// third less 26 px for amateurs) and the points (a quarter); white text.
class DriverStatsDialog final : public RecordDialog {
public:
    explicit DriverStatsDialog(Frontend& fe) : RecordDialog(fe, "jpg/drec_dlg.jpg", menu_id::kDriverRecord, false) {}

    void drawAbove(Frontend& fe, ui::UiFrame& f) override {
        if (!fe.profile)
            return;
        const auto& s = fe.ctx.game->strings;
        const auto font = ui::style::valueFont();
        const auto white = ui::style::kRecordText;
        const bool pro = fe.profile->difficulty == game::Difficulty::Professional;
        constexpr float x = 81;
        const float vehicleX = x + (pro ? 270.0f : 280.0f);
        f.text.draw(f.overlay, font, s.get(345, "RACE"), x + 18, 72, white);
        f.text.draw(f.overlay, font, s.get(346, "TIME"), x + 162, 72, white);
        f.text.draw(f.overlay, font, s.get(347, "VEHICLE"), vehicleX, 72, white);
        if (pro)
            f.text.draw(f.overlay, font, s.get(344, "POINTS"), x + 378, 72, white);
        const auto races = fe.racesFor(m_mode, m_city);
        const std::string mode = game::modeKey(m_mode);
        const float top = 10.0f + 0.1674f * 460.0f, rowH = 0.0374583f * 480.0f;
        for (std::size_t i = 0; i < races.size() && i < 12; ++i) {
            const float y = top + rowH * static_cast<float>(i);
            const int index = static_cast<int>(i);
            const auto* rec = fe.profile->record(m_city, mode, index);
            // lock.tga frames (mmInterface::PlayerFillRecords, mmCompDRecord::
            // Cull): open, locked (only checkpoint races lock), passed.
            const int status = rec && rec->passed ? 2 : (fe.progress.raceOpen(&*fe.profile, m_city, mode, index) ? 0 : 1);
            ui::drawSpriteFrame(f, {"texture/lock.tga", 3, true}, status, x, y - 3);
            f.text.draw(f.overlay, font, fit(fe, f, font, races[i]->name, kWideColumn, 664), x + 18, y,
                        white);
            f.text.draw(f.overlay, font, rec ? formatTime(rec->time) : "  ---  ", x + 162, y, white);
            // Not driven: "----"; driven without a car name: string 64 "---".
            std::string car = "----";
            if (rec) {
                const auto* v = fe.ctx.game->catalog.vehicle(rec->vehicle);
                car = v ? fit(fe, f, font, v->description, kNarrowColumn, 665) : s.get(64, "---");
            }
            f.text.draw(f.overlay, font, car, vehicleX, y, white);
            if (pro)
                f.text.draw(f.overlay, font, std::format("{:4}", rec ? rec->score : 0), x + 378, y, white);
        }
    }
};

// MM2 `Dialog_HallOfFame` (mmInterface::HOFFillRecords): the five best
// times (amateur or pro) or scores (pro) of each race of the mode in the
// city, from the race records shared by all drivers, five rows per race
// whether or not they are filled. The list is a UICompositeScroll at 0.0578,
// 0.177 of the 540x460 dialog, 0.9 of it wide (486 px), 11 rows of 18 px
// with a scroll bar: rows from (81,91). mmCompRaceRecord::
// SetSubwidgetGeometry puts the race at 4 px, the driver a quarter of the
// width further, the time or score 0.2857 further and the vehicle 0.1923
// less 26 px further. The scroll arrows' positions are inferred.
class RaceRecordsDialog final : public RecordDialog {
public:
    explicit RaceRecordsDialog(Frontend& fe) : RecordDialog(fe, "jpg/hoff_dlg.jpg", menu_id::kHallOfFame, true) {
        auto& up = menu.add<ui::SpriteButton>(SpriteSheet{"texture/scroll_uarr.tga", 4}, 543, 88,
                                              [this] { m_scroll = std::max(0, m_scroll - 1); });
        auto& down = menu.add<ui::SpriteButton>(SpriteSheet{"texture/scroll_darr.tga", 4}, 543, 266,
                                                [this] { ++m_scroll; });
        up.sound = down.sound = "Switch";
    }

    void update(Frontend& fe, double) override {
        // The mouse wheel scrolls the list (an OpenMM2 convenience).
        if (const float wheel = fe.ctx.input.mouseWheel().y; wheel != 0.0f)
            m_scroll = std::max(0, m_scroll - static_cast<int>(wheel));
    }

    void drawAbove(Frontend& fe, ui::UiFrame& f) override {
        const auto& s = fe.ctx.game->strings;
        const auto font = ui::style::valueFont();
        const auto white = ui::style::kRecordText;
        constexpr float x = 81, w = 0.9f * 540.0f;
        constexpr float driverX = x + 4 + 0.25f * w, valueX = driverX + 0.2857143f * w,
                        vehicleX = valueX + 0.1923077f * w - 26;
        const bool points = m_table == Table::ProPoints;
        // Titles (Dialog_HallOfFame::InitRaceRecord); the third follows the
        // table (SetSortState: 349 SCORE for pro points, else 350 TIME).
        f.text.draw(f.overlay, font, s.get(351, "RACE"), x + 4, 72, white);
        f.text.draw(f.overlay, font, s.get(353, "DRIVER"), driverX, 72, white);
        f.text.draw(f.overlay, font, points ? s.get(349, "SCORE") : s.get(350, "TIME"), valueX, 72, white);
        f.text.draw(f.overlay, font, s.get(355, "VEHICLE"), vehicleX, 72, white);
        struct Row {
            std::string race, driver, value, vehicle;
        };
        std::vector<Row> rows;
        const auto difficulty = m_table == Table::AmateurTimes ? game::Difficulty::Amateur : game::Difficulty::Professional;
        const auto races = fe.racesFor(m_mode, m_city);
        for (std::size_t i = 0; i < races.size(); ++i) {
            const auto* t = fe.hallOfFame.table(difficulty, m_city, game::modeKey(m_mode), static_cast<int>(i));
            for (std::size_t k = 0; k < 5; ++k) {
                // An empty slot: no driver, "  ---  " (GetTimeString /
                // GetScoreString) and string 64 "---" for the car.
                Row r{races[i]->name, "", "  ---  ", s.get(64, "---")};
                const auto* list = t ? (points ? &t->byScore : &t->byTime) : nullptr;
                if (list && k < list->size()) {
                    const auto& e = (*list)[k];
                    r.driver = e.driver;
                    if (points ? e.score > 0 : e.time > 0.0f)
                        r.value = points ? std::to_string(e.score) : formatTime(e.time);
                    if (!e.vehicle.empty()) {
                        const auto* v = fe.ctx.game->catalog.vehicle(e.vehicle);
                        r.vehicle = v ? v->description : e.vehicle;
                    }
                }
                rows.push_back(std::move(r));
            }
        }
        m_scroll = std::clamp(m_scroll, 0, std::max(0, static_cast<int>(rows.size()) - 11));
        const float top = 10.0f + 0.177f * 460.0f, rowH = 0.0375f * 480.0f;
        for (int i = m_scroll; i < static_cast<int>(rows.size()) && i < m_scroll + 11; ++i) {
            const Row& r = rows[static_cast<std::size_t>(i)];
            const float y = top + rowH * static_cast<float>(i - m_scroll);
            f.text.draw(f.overlay, font, fit(fe, f, font, r.race, kNarrowColumn, 666), x + 4, y, white);
            f.text.draw(f.overlay, font, fit(fe, f, font, r.driver, kWideColumn, 668), driverX, y, white);
            f.text.draw(f.overlay, font, r.value, valueX, y, white);
            f.text.draw(f.overlay, font, fit(fe, f, font, r.vehicle, kWideColumn, 667), vehicleX, y, white);
        }
    }
};

} // namespace

std::unique_ptr<Page> makeTitlePage(Frontend& fe) { return std::make_unique<LoadingPage>(fe); }
std::unique_ptr<Page> makeDriverPage(Frontend& fe) { return std::make_unique<DriverPage>(fe); }
std::unique_ptr<Page> makeNewDriverDialog(Frontend& fe) { return std::make_unique<NewDriverDialog>(fe); }
std::unique_ptr<Page> makeDriverStatsDialog(Frontend& fe) { return std::make_unique<DriverStatsDialog>(fe); }
std::unique_ptr<Page> makeRaceRecordsDialog(Frontend& fe) { return std::make_unique<RaceRecordsDialog>(fe); }

} // namespace mm2::app::frontend
