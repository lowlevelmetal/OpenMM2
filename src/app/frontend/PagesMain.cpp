// Title screen, driver selection, new driver and driver's stats.
#include "app/frontend/Frontend.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <format>
#include <random>

namespace mm2::app::frontend {
namespace {

using ui::Box;
using ui::SpriteSheet;

// --- Title -----------------------------------------------------------------------------

// splash.jpg with the loading bar. The original filled it while loading the
// game data; here loading is already done, so the bar fills quickly and the
// title waits for a key (inferred behaviour).
class TitlePage final : public Page {
public:
    explicit TitlePage(Frontend&) { menu.background = "jpg/splash.jpg"; }

    void update(Frontend& fe, double dt) override {
        m_progress = std::min(1.0, m_progress + dt * 1.5);
        const auto& in = fe.ctx.input;
        bool any = in.anyKeyPressed() || in.mousePressed(platform::MouseButton::Left);
        for (const auto& pad : in.gamepads())
            any |= pad.pressed.any();
        if (m_progress >= 1.0 && any) {
            fe.replace(makeDriverPage(fe));
            if (!fe.profile)
                fe.push(makeNewDriverDialog(fe));
        }
    }

    void drawAbove(Frontend& fe, ui::UiFrame& f) override {
        constexpr float x = 366, y = 447;
        ui::drawSpriteFrame(f, {"texture/pbar_inact.tga", 1}, 0, x, y);
        const ui::UiTexture& act = fe.textures.get("texture/pbar_act.tga");
        if (act) {
            const float w = static_cast<float>(act.width) * static_cast<float>(m_progress);
            f.overlay.image(act.handle, x + 2, y + 2, w, static_cast<float>(act.height), {0, 1},
                            {static_cast<float>(m_progress), 0});
        }
        if (m_progress >= 1.0) {
            const auto a = static_cast<std::uint8_t>(150 + 100 * std::sin(fe.time * 4.0));
            f.text.draw(f.overlay, ui::style::smallFont(), "Press any key", 490, 426,
                        render::packColor(255, 255, 255, a), ui::Align::Center);
        }
    }

private:
    double m_progress = 0.0;
};

// --- Driver selection (main_bk) ---------------------------------------------------------

class DriverPage final : public Page {
public:
    explicit DriverPage(Frontend& fe) {
        using namespace layout;
        menu.background = "jpg/main_bk.jpg";
        menu.defaultHelp = "jpg/mn_drv.jpg";
        refresh(fe);

        menu.add<ui::TextBox>(Box{174, 124, 209, 28}, [&fe] { return fe.profile ? fe.profile->name : std::string(); });
        auto& list = menu.add<ui::ListBox>(
            Box{176, 167, 206, 204}, [this] { return m_names; },
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
        list.help = "jpg/mn_drv.jpg";
        list.onDoubleClick = [&fe] { fe.push(makeRacesPage(fe)); };

        auto& create = menu.add<ui::SpriteButton>(SpriteSheet{"texture/dvrnew.tga", 4}, 40, 156,
                                                  [&fe] { fe.push(makeNewDriverDialog(fe)); });
        create.help = "jpg/mn_new.jpg";
        m_delete = &menu.add<ui::SpriteButton>(SpriteSheet{"texture/dvrdel.tga", 4}, 40, 216, [this, &fe] {
            if (!fe.profile)
                return;
            fe.ask("jpg/delp_dlg.jpg", {300, 225}, "", [this, &fe] {
                fe.store.remove(*fe.profile);
                fe.profile.reset();
                refresh(fe);
                if (!m_names.empty())
                    fe.selectProfile(m_names.front());
            });
        });
        m_delete->help = "jpg/mn_del.jpg";
        m_stats = &menu.add<ui::SpriteButton>(SpriteSheet{"texture/dvrsts.tga", 4}, 40, 283,
                                              [&fe] { fe.push(makeDriverStatsDialog(fe)); });
        m_stats->help = "jpg/mn_sts.jpg";

        m_crash = &menu.add<ui::SpriteButton>(SpriteSheet{"texture/dvrcc.tga", 4}, kColumnX, kRow59,
                                              [&fe] { fe.push(makeCrashIntroPage(fe)); });
        m_crash->help = "jpg/mn_cc.jpg";
        m_races = &menu.add<ui::SpriteButton>(SpriteSheet{"texture/main_sp.tga", 4}, kColumnX, kRow58,
                                              [&fe] { fe.push(makeRacesPage(fe)); });
        m_races->help = "jpg/mn_sp.jpg";
        m_multi = &menu.add<ui::SpriteButton>(SpriteSheet{"texture/main_mp.tga", 4}, kColumnX, kRow56,
                                              [&fe] { fe.push(makeSessionsPage(fe)); });
        m_multi->help = "jpg/mn_mp.jpg";
        m_quick = &menu.add<ui::SpriteButton>(SpriteSheet{"texture/main_qck.tga", 4}, kColumnX, kRow65,
                                              [this, &fe] { quickRace(fe); });
        m_quick->help = "jpg/mn_qck.jpg";
        addNavStrip(fe, *this);
        menu.focus(m_races);
        menu.onBack = [&fe] { fe.ask("jpg/quit_dlg.jpg", {400, 76}, "", [&fe] { fe.ctx.quit = true; }); };
    }

    void onEnter(Frontend& fe) override { refresh(fe); }

    void update(Frontend& fe, double) override {
        const std::string current = fe.profile ? fe.profile->name : std::string();
        if (current != m_lastProfile) {
            m_lastProfile = current;
            refresh(fe);
        }
        const bool has = fe.profile.has_value();
        for (auto* b : {m_delete, m_stats, m_crash, m_races, m_multi, m_quick})
            b->enabled = has;
    }

private:
    void refresh(Frontend& fe) {
        m_names.clear();
        for (const auto& p : fe.store.list())
            m_names.push_back(p.name);
    }

    // Quick Race: a random event the driver can already enter, in a random
    // unlocked vehicle (inferred; the original's rules are not known).
    void quickRace(Frontend& fe) {
        if (!fe.profile)
            return;
        std::mt19937 rng(std::random_device{}());
        struct Choice {
            std::string city;
            game::GameMode mode;
            int index;
        };
        std::vector<Choice> choices;
        for (const auto& c : fe.cities)
            for (auto m : {game::GameMode::Blitz, game::GameMode::Circuit, game::GameMode::Checkpoint}) {
                const int n = fe.progress.availableRaces(*fe.profile, c.mapName, game::modeKey(m));
                for (int i = 0; i < n; ++i)
                    choices.push_back({c.mapName, m, i});
            }
        std::vector<std::string> cars;
        for (const auto& v : fe.ctx.game->catalog.vehicles())
            if (fe.progress.vehicleUnlocked(*fe.profile, v.baseName))
                cars.push_back(v.baseName);
        if (choices.empty() || cars.empty())
            return;
        const Choice c = choices[std::uniform_int_distribution<std::size_t>(0, choices.size() - 1)(rng)];
        fe.config.city = c.city;
        fe.config.mode = c.mode;
        fe.config.raceIndex = c.index;
        fe.config.vehicle = cars[std::uniform_int_distribution<std::size_t>(0, cars.size() - 1)(rng)];
        fe.config.vehicleColor = 0;
        const auto races = fe.racesFor(c.mode, c.city);
        if (c.index < static_cast<int>(races.size()) && races[static_cast<std::size_t>(c.index)]->settings) {
            const auto& s = races[static_cast<std::size_t>(c.index)]->settings->amateur;
            fe.config.timeOfDay = static_cast<game::TimeOfDay>(std::clamp(s.timeOfDay, 0, 3));
            fe.config.weather = static_cast<game::Weather>(std::clamp(s.weather, 0, 3));
            fe.config.opponents = s.opponents;
            fe.config.laps = s.numLaps;
        }
        fe.startRace();
    }

    std::vector<std::string> m_names;
    std::string m_lastProfile;
    ui::SpriteButton* m_delete = nullptr;
    ui::SpriteButton* m_stats = nullptr;
    ui::SpriteButton* m_crash = nullptr;
    ui::SpriteButton* m_races = nullptr;
    ui::SpriteButton* m_multi = nullptr;
    ui::SpriteButton* m_quick = nullptr;
};

// --- Create a New Driver (newp_dlg) ------------------------------------------------------

class NewDriverDialog final : public Page {
public:
    explicit NewDriverDialog(Frontend& fe) {
        dialog = true;
        dialogPicture = "jpg/newp_dlg.jpg";
        origin = {120, 75}; // tune/menu.csv: "Create a New Driver,...,120,75,400,330"
        const float ox = origin.x, oy = origin.y;
        auto& entry = menu.add<ui::TextEntry>(Box{ox + 70, oy + 88, 208, 26}, &m_name, 20);
        entry.onCommit = [] {};
        menu.add<ui::LampItem>(
            SpriteSheet{"texture/dlg_chkb.tga", 5}, ox + 76, oy + 166,
            [this] { return m_difficulty == game::Difficulty::Amateur; },
            [this] { m_difficulty = game::Difficulty::Amateur; });
        menu.add<ui::LampItem>(
            SpriteSheet{"texture/dlg_chkb.tga", 5}, ox + 76, oy + 216,
            [this] { return m_difficulty == game::Difficulty::Professional; },
            [this] { m_difficulty = game::Difficulty::Professional; });
        menu.add<ui::SpriteButton>(SpriteSheet{"texture/dlg_ok.tga", 4}, ox + 60, oy + 280, [this, &fe] { create(fe); });
        menu.add<ui::SpriteButton>(SpriteSheet{"texture/dlg_can.tga", 4}, ox + 240, oy + 280, [&fe] { fe.pop(); });
        menu.onBack = [&fe] { fe.pop(); };
        menu.focus(&entry);
        entry.beginEdit();
    }

    void drawAbove(Frontend&, ui::UiFrame& f) override {
        if (!m_error.empty())
            f.text.draw(f.overlay, ui::style::smallFont(), m_error, origin.x + 200, origin.y + 250, ui::style::kHelpText,
                        ui::Align::Center);
    }

private:
    void create(Frontend& fe) {
        std::string error;
        auto p = fe.store.create(m_name, &error);
        if (!p) {
            m_error = error;
            return;
        }
        p->difficulty = m_difficulty;
        p->save();
        fe.pop();
        fe.selectProfile(p->name);
        if (Page* top = fe.top())
            top->onEnter(fe);
    }

    std::string m_name;
    std::string m_error;
    game::Difficulty m_difficulty = game::Difficulty::Amateur;
};

// --- Driver's Stats (drec_dlg) -------------------------------------------------------------

class DriverStatsDialog final : public Page {
public:
    explicit DriverStatsDialog(Frontend& fe) {
        dialog = true;
        dialogPicture = "jpg/drec_dlg.jpg";
        origin = {50, 10};
        const float ox = origin.x, oy = origin.y;
        const game::GameMode modes[] = {game::GameMode::Blitz, game::GameMode::Circuit, game::GameMode::Checkpoint};
        for (int i = 0; i < 3; ++i) {
            const auto m = modes[i];
            menu.add<ui::LampItem>(
                SpriteSheet{"texture/dlg_chkb.tga", 5}, ox + 42, oy + 323 + 25.0f * static_cast<float>(i),
                [this, m] { return m_mode == m; }, [this, m] { m_mode = m; });
        }
        // Cities as painted: San Francisco, then London.
        const char* cityRows[] = {"sf", "london"};
        for (int i = 0; i < 2; ++i) {
            const std::string c = cityRows[i];
            menu.add<ui::LampItem>(
                SpriteSheet{"texture/dlg_chkb.tga", 5}, ox + 362, oy + 323 + 25.0f * static_cast<float>(i),
                [this, c] { return m_city == c; }, [this, c] { m_city = c; });
        }
        menu.add<ui::SpriteButton>(SpriteSheet{"texture/dlg_done.tga", 4}, ox + 418, oy + 420, [&fe] { fe.pop(); });
        menu.onBack = [&fe] { fe.pop(); };
        m_city = fe.config.city;
    }

    void drawAbove(Frontend& fe, ui::UiFrame& f) override {
        const float ox = origin.x + 22, oy = origin.y + 58;
        const auto font = ui::style::smallFont();
        const auto& s = fe.ctx.game->strings;
        f.text.draw(f.overlay, font, s.get(351, "RACE"), ox + 8, oy + 1, ui::style::kHelpText);
        f.text.draw(f.overlay, font, "PLACE", ox + 300, oy + 1, ui::style::kHelpText);
        f.text.draw(f.overlay, font, s.get(350, "TIME"), ox + 390, oy + 1, ui::style::kHelpText);
        if (!fe.profile)
            return;
        const auto races = fe.racesFor(m_mode, m_city);
        const auto diff = fe.profile->difficulty;
        float y = oy + 19;
        for (std::size_t i = 0; i < races.size() && i < 12; ++i) {
            const auto* rec = fe.profile->record(diff, m_city, game::modeKey(m_mode), static_cast<int>(i));
            f.text.draw(f.overlay, font, races[i]->name, ox + 8, y, ui::style::kValueText);
            f.text.draw(f.overlay, font, rec && rec->bestPosition ? std::to_string(rec->bestPosition) : "-", ox + 300, y,
                        ui::style::kValueText);
            f.text.draw(f.overlay, font, formatTime(rec ? rec->bestTime : 0.0f), ox + 390, y, ui::style::kValueText);
            y += 18;
        }
        // Driver record summary (string table 635-640).
        const auto& p = *fe.profile;
        const float sx = origin.x + 230, sy = origin.y + 320;
        f.text.draw(f.overlay, font, std::format("{} {}", s.get(640, "SCORE:"), p.score), sx, sy, ui::style::kValueText);
        f.text.draw(f.overlay, font, std::format("{} {}", s.get(636, "LAST RACE:"), p.lastRace), sx, sy + 18,
                    ui::style::kValueText);
        const auto* v = fe.ctx.game->catalog.vehicle(p.lastVehicle);
        f.text.draw(f.overlay, font, std::format("{} {}", s.get(637, "LAST VEHICLE:"), v ? v->description : ""), sx,
                    sy + 36, ui::style::kValueText);
        f.text.draw(f.overlay, ui::style::valueFont(),
                    std::format("{} ({})", p.name, p.difficulty == game::Difficulty::Professional ? "Professional" : "Amateur"),
                    origin.x + 22, origin.y + 22, ui::style::kValueText);
    }

private:
    game::GameMode m_mode = game::GameMode::Blitz;
    std::string m_city = "london";
};

} // namespace

std::unique_ptr<Page> makeTitlePage(Frontend& fe) { return std::make_unique<TitlePage>(fe); }
std::unique_ptr<Page> makeDriverPage(Frontend& fe) { return std::make_unique<DriverPage>(fe); }
std::unique_ptr<Page> makeNewDriverDialog(Frontend& fe) { return std::make_unique<NewDriverDialog>(fe); }
std::unique_ptr<Page> makeDriverStatsDialog(Frontend& fe) { return std::make_unique<DriverStatsDialog>(fe); }

} // namespace mm2::app::frontend
