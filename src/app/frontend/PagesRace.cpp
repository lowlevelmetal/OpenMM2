// Race setup (race_bk), vehicle selection (veh_bk), vehicle showcase and the
// results screens (rshi_bk / crshi_bk).
//
// Layout and behaviour follow MM2's RaceMenu / RaceMenuBase (menu 7),
// Vehicle / VehicleSelectBase (menu 8) and VehShowcase (menu 9). Widget
// positions come from tune/widget.csv by the widget's creation index in the
// original menu (the comments give the index); the numbers in the code are
// the table's values, used when it has no row.
#include "app/frontend/Frontend.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <format>
#include <map>

namespace mm2::app::frontend {
namespace {

using ui::Box;
using ui::SpriteSheet;
using game::GameMode;

// The separate roller_up / roller_down buttons MM2 places beside a drop-down
// (3-frame sprites; up steps to the previous entry, down to the next).
struct Arrows {
    ui::SpriteButton* up = nullptr;
    ui::SpriteButton* down = nullptr;
    void show(bool on) const {
        up->visible = on;
        down->visible = on;
    }
};

Arrows addArrows(Frontend& fe, Page& page, int menuId, int upIndex, Vec2 upCode, Vec2 downCode, ui::ValueBox& box,
                 bool wrap) {
    const Vec2 u = fe.layout.position(menuId, upIndex, upCode);
    const Vec2 d = fe.layout.position(menuId, upIndex + 1, downCode);
    Arrows a;
    a.up = &page.menu.add<ui::SpriteButton>(SpriteSheet{"texture/roller_up.tga", 3}, u.x, u.y,
                                            [&box, wrap] { ui::stepOption(box, -1, wrap); });
    a.down = &page.menu.add<ui::SpriteButton>(SpriteSheet{"texture/roller_down.tga", 3}, d.x, d.y,
                                              [&box, wrap] { ui::stepOption(box, 1, wrap); });
    return a;
}

std::vector<std::string> stringRange(Frontend& fe, std::uint32_t first, int count) {
    std::vector<std::string> v;
    for (int i = 0; i < count; ++i)
        v.push_back(fe.ctx.game->strings.get(first + static_cast<std::uint32_t>(i), std::to_string(i + 1)));
    return v;
}

// RaceMenuBase's help label: race_btz|race_cir|race_cp|race_rom indexed by
// the mode.
const char* modeHelp(GameMode m) {
    switch (m) {
    case GameMode::Blitz: return "jpg/race_btz.jpg";
    case GameMode::Circuit: return "jpg/race_cir.jpg";
    case GameMode::Checkpoint: return "jpg/race_cp.jpg";
    default: return "jpg/race_rom.jpg";
    }
}

bool racePassed(Frontend& fe, const game::RaceConfig& cfg) {
    if (!fe.profile)
        return true; // no driver: nothing is locked
    const auto* rec = fe.profile->record(cfg.city, game::modeKey(cfg.mode), cfg.raceIndex);
    return rec && rec->passed;
}

// --- Races (Single Race Menu, menu 7) ---------------------------------------------------

class RacesPage final : public Page {
public:
    explicit RacesPage(Frontend& fe) {
        menuId = menu_id::kRace;
        menu.background = "jpg/race_bk.jpg";
        auto& cfg = fe.config;
        // A crash course or Cops & Robbers event left over becomes cruise.
        if (cfg.mode == GameMode::CrashCourse || cfg.mode == GameMode::CopsAndRobbers || !modeAvailable(fe, cfg.mode)) {
            cfg.mode = GameMode::Cruise;
            cfg.raceIndex = -1;
            fe.applyRaceDefaults(cfg);
        }
        validateRace(fe);
        const auto& s = fe.ctx.game->strings;
        constexpr int id = menu_id::kRace;

        // 0-3: the mode lamps (radio buttons on the mode).
        const struct {
            GameMode mode;
            const char* sprite;
            Vec2 pos;
        } lamps[] = {{GameMode::Cruise, "texture/cruise.tga", {40, 62}},
                     {GameMode::Blitz, "texture/blitz.tga", {40, 90}},
                     {GameMode::Checkpoint, "texture/cp.tga", {40, 118}},
                     {GameMode::Circuit, "texture/circuit.tga", {40, 146}}};
        for (int i = 0; i < 4; ++i) {
            const GameMode m = lamps[i].mode;
            const Vec2 p = fe.layout.position(id, i, lamps[i].pos);
            m_lamps[i] = &menu.add<ui::LampItem>(
                SpriteSheet{lamps[i].sprite, 5}, p.x, p.y, [&fe, m] { return fe.config.mode == m; },
                [this, &fe, m] { selectMode(fe, m); });
            m_modes[i] = m;
        }

        // 4-6: RACE NAME and its arrows (clamping, stopping at locked races).
        m_raceName = &menu.add<ui::ValueBox>(
            fe.layout.widget(id, 4, {404, 66, 205, 24}),
            [&fe] {
                std::vector<std::string> names;
                for (const auto* r : fe.racesFor(fe.config.mode, fe.config.city))
                    names.push_back(r->name);
                return names;
            },
            [&fe] { return fe.config.raceIndex; },
            [&fe](int i) {
                fe.config.raceIndex = i;
                fe.applyRaceDefaults(fe.config);
            });
        m_raceName->optionEnabled = [&fe](int i) {
            return fe.progress.raceOpen(fe.profile ? &*fe.profile : nullptr, fe.config.city,
                                        game::modeKey(fe.config.mode), i);
        };
        m_raceArrows = addArrows(fe, *this, id, 5, {609, 60}, {609, 78}, *m_raceName, false);

        // 7-8: LAPS (1-10) and OPPONENTS (1 up to the race's count), circuits only.
        m_laps = &menu.add<ui::Roller>(
            fe.layout.widget(id, 7, {418, 98, 60, 32}), [&fe] { return stringRange(fe, 590, 10); },
            [&fe] { return std::clamp(fe.config.laps - 1, 0, 9); }, [&fe](int i) { fe.config.laps = i + 1; });
        m_opponents = &menu.add<ui::Roller>(
            fe.layout.widget(id, 8, {418, 133, 60, 32}), [&fe] { return stringRange(fe, 590, 8); },
            [&fe] { return std::clamp(fe.config.opponents - 1, 0, 7); }, [&fe](int i) { fe.config.opponents = i + 1; });

        // 9-11: RACE LOCALE, every city (San Francisco first), clamping arrows.
        m_city = &menu.add<ui::ValueBox>(
            fe.layout.widget(id, 9, {404, 210, 205, 24}),
            [&fe] {
                std::vector<std::string> v;
                for (const auto& c : fe.cities)
                    v.push_back(c.localizedName);
                return v;
            },
            [&fe] { return fe.cityIndex(fe.config.city); }, [this, &fe](int i) { selectCity(fe, i); });
        addArrows(fe, *this, id, 10, {610, 203}, {610, 221}, *m_city, false);

        // 12-14: TIME OF DAY (strings 629-632); 15-17: WEATHER (625-628).
        m_time = &menu.add<ui::ValueBox>(
            fe.layout.widget(id, 12, {404, 249, 123, 24}),
            [t = stringRange(fe, 629, 4)] { return t; }, [&fe] { return static_cast<int>(fe.config.timeOfDay); },
            [&fe](int i) { fe.config.timeOfDay = static_cast<game::TimeOfDay>(i); });
        m_timeArrows = addArrows(fe, *this, id, 13, {528, 242}, {528, 260}, *m_time, false);
        m_weather = &menu.add<ui::ValueBox>(
            fe.layout.widget(id, 15, {404, 281, 123, 21}),
            [w = std::vector<std::string>{s.get(625, "Clear"), s.get(626, "Cloudy"), s.get(627, "Foggy"),
                                          s.get(628, "Raining")}] { return w; },
            [&fe] { return std::min(static_cast<int>(fe.config.weather), 3); },
            [&fe](int i) { fe.config.weather = static_cast<game::Weather>(i); });
        m_weatherArrows = addArrows(fe, *this, id, 16, {528, 276}, {528, 294}, *m_weather, false);

        // 18-20: pedestrian, traffic and cop density.
        float* densities[] = {&fe.config.pedestrianDensity, &fe.config.trafficDensity, &fe.config.copDensity};
        for (int i = 0; i < 3; ++i) {
            float* d = densities[i];
            m_sliders[i] = &menu.add<ui::Slider>(
                fe.layout.widget(id, 18 + i, {450, 316.0f + 34.0f * static_cast<float>(i), 183, 29}),
                [d] { return *d; }, [d](float v) { *d = v; });
        }

        // 22: the race map (RaceMenuBase::LoadRaceMap).
        m_mapBox = fe.layout.widget(id, 22, {22, 194, 242, 184});
        menu.add<ui::Custom>([this, &fe](ui::UiFrame& f) { drawMap(fe, f); });

        // 23: on to the garage.
        const Vec2 next = fe.layout.position(id, 23, layout::kNext);
        menu.add<ui::SpriteButton>(SpriteSheet{"texture/race_veh.tga", 4}, next.x, next.y,
                                   [&fe] { fe.push(makeVehiclePage(fe)); });
        addBack(fe, *this);
        addNavStrip(fe, *this);
        refresh(fe);
        menu.resetFocus();
        m_lastFocus = menu.focused();
    }

    void onEnter(Frontend& fe) override {
        refresh(fe);
        m_lastFocus = menu.focused();
        m_showHelp = false;
    }

    void update(Frontend& fe, double) override {
        refresh(fe);
        // The help label shows the mode's picture while a lamp has the focus
        // and after a mode or city change; it is hidden on entry and whenever
        // the focus moves elsewhere (RaceMenuBase::FocusDescription, PreSetup).
        const ui::Widget* focused = menu.focused();
        if (focused != m_lastFocus) {
            m_lastFocus = focused;
            m_showHelp = std::ranges::find(m_lamps, focused) != std::end(m_lamps);
        }
        menu.defaultHelp = m_showHelp ? modeHelp(fe.config.mode) : "";
    }

    void drawBelow(Frontend& fe, ui::UiFrame& f) override {
        // race_cov covers the LAPS / OPPONENTS labels and boxes outside
        // circuits (RaceMenuBase::GameCallback).
        if (fe.config.mode != GameMode::Circuit)
            ui::drawImage(f.overlay, fe.textures.get("jpg/race_cov.jpg"), 290, 93);
    }

private:
    bool modeAvailable(Frontend& fe, GameMode m) const {
        return m == GameMode::Cruise || !fe.racesFor(m, fe.config.city).empty();
    }

    // A race that is out of range or locked moves to the first open one.
    void validateRace(Frontend& fe) {
        auto& cfg = fe.config;
        if (cfg.mode == GameMode::Cruise) {
            cfg.raceIndex = -1;
            return;
        }
        const int n = static_cast<int>(fe.racesFor(cfg.mode, cfg.city).size());
        const auto* p = fe.profile ? &*fe.profile : nullptr;
        if (cfg.raceIndex >= 0 && cfg.raceIndex < n && fe.progress.raceOpen(p, cfg.city, game::modeKey(cfg.mode), cfg.raceIndex))
            return;
        cfg.raceIndex = 0;
        for (int i = 0; i < n; ++i)
            if (fe.progress.raceOpen(p, cfg.city, game::modeKey(cfg.mode), i)) {
                cfg.raceIndex = i;
                break;
            }
    }

    // Pressing a lamp sets the mode, goes back to its first race and applies
    // that race's defaults (RaceMenuBase::GameCallback).
    void selectMode(Frontend& fe, GameMode m) {
        fe.config.mode = m;
        fe.config.raceIndex = m == GameMode::Cruise ? -1 : 0;
        validateRace(fe);
        fe.applyRaceDefaults(fe.config);
        m_showHelp = true;
    }

    // RaceMenuBase::CityChange: a mode the city has no races for falls back to
    // cruise; the race goes back to the first one.
    void selectCity(Frontend& fe, int i) {
        fe.config.city = fe.cities[static_cast<std::size_t>(i)].mapName;
        if (!modeAvailable(fe, fe.config.mode))
            fe.config.mode = GameMode::Cruise;
        fe.config.raceIndex = fe.config.mode == GameMode::Cruise ? -1 : 0;
        validateRace(fe);
        fe.applyRaceDefaults(fe.config);
        m_showHelp = true;
    }

    // Visibility and the environment lock (RaceMenuBase::SetRW): outside
    // cruise the environment is read-only until the race has been passed, and
    // a circuit's traffic always is; laps and opponents only exist for
    // circuits and are read-only until it is passed.
    void refresh(Frontend& fe) {
        const auto& cfg = fe.config;
        for (int i = 0; i < 4; ++i)
            m_lamps[i]->enabled = modeAvailable(fe, m_modes[i]);
        const bool cruise = cfg.mode == GameMode::Cruise;
        const bool circuit = cfg.mode == GameMode::Circuit;
        m_raceName->visible = !cruise;
        m_raceArrows.show(!cruise);
        const bool passed = cruise || racePassed(fe, cfg);
        m_laps->visible = m_opponents->visible = circuit;
        m_laps->readOnly = m_opponents->readOnly = !passed;
        if (circuit) {
            game::RaceConfig defaults = cfg;
            fe.applyRaceDefaults(defaults);
            m_opponents->maxIndex = std::max(0, defaults.opponents - 1);
        }
        m_time->readOnly = m_weather->readOnly = !passed;
        m_timeArrows.show(passed);
        m_weatherArrows.show(passed);
        for (int i = 0; i < 3; ++i)
            m_sliders[i]->readOnly = !passed || (circuit && i == 1);
    }

    // jpg/<RaceDir>_map<roam|race<n>|circuit<n>|blitz<n>>.jpg, hidden when missing.
    void drawMap(Frontend& fe, ui::UiFrame& f) const {
        const auto& cfg = fe.config;
        const auto* c = fe.currentCity();
        if (!c)
            return;
        const std::string name =
            cfg.mode == GameMode::Cruise ? std::string("roam") : std::format("{}{}", game::modeKey(cfg.mode), cfg.raceIndex);
        const ui::UiTexture& t = fe.textures.get(std::format("jpg/{}_map{}.jpg", str::lower(c->raceDir), name));
        if (t)
            ui::drawImage(f.overlay, t, m_mapBox.x, m_mapBox.y, m_mapBox.w, m_mapBox.h);
    }

    ui::LampItem* m_lamps[4] = {};
    GameMode m_modes[4] = {};
    ui::ValueBox* m_raceName = nullptr;
    Arrows m_raceArrows;
    ui::Roller* m_laps = nullptr;
    ui::Roller* m_opponents = nullptr;
    ui::ValueBox* m_city = nullptr;
    ui::ValueBox* m_time = nullptr;
    Arrows m_timeArrows;
    ui::ValueBox* m_weather = nullptr;
    Arrows m_weatherArrows;
    ui::Slider* m_sliders[3] = {};
    Box m_mapBox;
    const ui::Widget* m_lastFocus = nullptr;
    bool m_showHelp = false;
};

// --- Vehicle (Garage Menu, menu 8) ---------------------------------------------------------

std::string jpgIfExists(Frontend& fe, const std::string& name) {
    const std::string path = "jpg/" + name + ".jpg";
    return fe.ctx.game->vfs.exists(path) ? path : std::string();
}

// VehicleSelectBase::ShowCarDesc: a locked paint job's picture, else a
// locked car's, else the car's description; professionals first try the
// "_p" pictures ("finish first").
std::string carDescription(Frontend& fe, const std::string& car, bool carLocked, bool paintLocked, int paint) {
    const bool pro = fe.profile && fe.profile->difficulty == game::Difficulty::Professional;
    std::string pic;
    if (paintLocked) {
        if (pro)
            pic = jpgIfExists(fe, std::format("{}_lck{}_p", car, paint));
        if (pic.empty())
            pic = jpgIfExists(fe, std::format("{}_lck{}", car, paint));
    }
    if (pic.empty() && carLocked) {
        if (pro)
            pic = jpgIfExists(fe, car + "_lck_p");
        if (pic.empty())
            pic = jpgIfExists(fe, car + "_lck");
    }
    if (pic.empty())
        pic = jpgIfExists(fe, car + "_ulck");
    return pic;
}

std::string showPicture(Frontend& fe, const std::string& car) {
    for (const auto& c : {car + "_show", car})
        if (fe.ctx.game->vfs.exists("jpg/" + c + ".jpg"))
            return "jpg/" + c + ".jpg";
    return {};
}

// What the garage remembers for the rest of the session (process): each
// car's last paint job and the last unlocked car picked.
struct GarageSession {
    std::map<std::string, int, std::less<>> paint;
    std::string lastUnlocked;
};

GarageSession& garageSession() {
    static GarageSession session;
    return session;
}

class VehiclePage final : public Page {
public:
    explicit VehiclePage(Frontend& fe) {
        menuId = menu_id::kVehicle;
        menu.background = "jpg/veh_bk.jpg";
        constexpr int id = menu_id::kVehicle;
        const auto& cars = fe.ctx.game->catalog.vehicles();
        enterWithUnlockedCar(fe);

        // The showroom (no widget): a photo stands in for the 3D car.
        menu.add<ui::Custom>([this, &fe](ui::UiFrame& f) { drawShowroom(fe, f); });
        // 0: the LOCKED sign (locked.tga, black transparent).
        const Vec2 sign = fe.layout.position(id, 0, {200, 160});
        menu.add<ui::Custom>([this, &fe, sign](ui::UiFrame& f) {
            if (!carUnlocked(fe) || !paintUnlocked(fe))
                ui::drawImage(f.overlay, fe.textures.getColorKeyed("texture/locked.tga"), sign.x, sign.y);
        });

        // 2-4: CAR COLOR and its arrows (wrapping).
        m_colorBox = &menu.add<ui::ValueBox>(
            fe.layout.widget(id, 2, {404, 315, 205, 19}),
            [&fe] {
                const auto* v = fe.ctx.game->catalog.vehicle(fe.config.vehicle);
                return v ? v->colors : std::vector<std::string>{};
            },
            [&fe] { return fe.config.vehicleColor; },
            [&fe](int i) {
                fe.config.vehicleColor = i;
                garageSession().paint[fe.config.vehicle] = i;
            });
        addArrows(fe, *this, id, 3, {608, 309}, {608, 327}, *m_colorBox, true);

        // 5-7: VEHICLES and its arrows (wrapping); every car, locked or not.
        m_vehicleBox = &menu.add<ui::ValueBox>(
            fe.layout.widget(id, 5, {404, 277, 205, 19}),
            [&fe] {
                std::vector<std::string> v;
                for (const auto& info : fe.ctx.game->catalog.vehicles())
                    v.push_back(info.description);
                return v;
            },
            [this, &fe] { return vehicleIndex(fe); }, [this, &fe](int i) { pickVehicle(fe, i); });
        addArrows(fe, *this, id, 6, {608, 270}, {608, 288}, *m_vehicleBox, true);

        // 9-12: read-only bars for horsepower, top speed, durability and mass;
        // each runs from half the smallest to 1.1 x the largest value of all
        // cars (VehicleSelectBase).
        using Stat = int game::VehicleInfo::*;
        const Stat stats[] = {&game::VehicleInfo::horsepower, &game::VehicleInfo::topSpeedMph,
                              &game::VehicleInfo::durability, &game::VehicleInfo::massLb};
        const float rows[] = {270, 293, 319, 344};
        for (int i = 0; i < 4; ++i) {
            const Stat stat = stats[i];
            int lo = 0, hi = 0;
            for (std::size_t c = 0; c < cars.size(); ++c) {
                const int v = cars[c].*stat;
                lo = c == 0 ? v : std::min(lo, v);
                hi = c == 0 ? v : std::max(hi, v);
            }
            auto& bar = menu.add<ui::Slider>(
                fe.layout.widget(id, 9 + i, {125, rows[i], 187, 6}),
                [&fe, stat] {
                    const auto* v = fe.ctx.game->catalog.vehicle(fe.config.vehicle);
                    return v ? static_cast<float>(v->*stat) : 0.0f;
                },
                [](float) {}, 0.5f * static_cast<float>(lo), 1.1f * static_cast<float>(hi));
            bar.readOnly = true;
        }

        // 13: VEHICLE SHOWCASE.
        const Vec2 show = fe.layout.position(id, 13, {347, 379});
        auto& showButton = menu.add<ui::SpriteButton>(SpriteSheet{"texture/veh_show.tga", 4}, show.x, show.y,
                                                      [this, &fe] {
                                                          m_inShowcase = true;
                                                          fe.push(makeShowcasePage(fe, fe.config.vehicle));
                                                      });
        showButton.help = "jpg/veh_tsc.jpg";

        // 14-16: TRANSMISSION, "Manual|Automatic" (633, 634), clamping arrows.
        const auto& s = fe.ctx.game->strings;
        m_transmission = &menu.add<ui::ValueBox>(
            fe.layout.widget(id, 14, {404, 348, 123, 21}),
            [t = std::vector<std::string>{s.get(633, "Manual"), s.get(634, "Automatic")}] { return t; },
            [&fe] { return fe.config.automatic ? 1 : 0; }, [&fe](int i) { fe.config.automatic = i == 1; });
        addArrows(fe, *this, id, 15, {530, 342}, {530, 360}, *m_transmission, false);

        // 18: GO DRIVE (Uigo); a locked car or paint job shows lock_dlg.
        const Vec2 go = fe.layout.position(id, 18, layout::kNext);
        auto& goButton = menu.add<ui::SpriteButton>(SpriteSheet{"texture/veh_go.tga", 4}, go.x, go.y, [this, &fe] {
            if (!carUnlocked(fe) || !paintUnlocked(fe))
                fe.notice("jpg/lock_dlg.jpg", menu_id::kLocked);
            else
                fe.startRace();
        });
        goButton.sound = "Uigo";
        goButton.soundVolume = 0.9f;
        addBack(fe, *this);
        addNavStrip(fe, *this);
        playSelectSound(fe);
    }

    void onEnter(Frontend& fe) override {
        if (m_inShowcase) {
            m_inShowcase = false;
            playSelectSound(fe);
        }
    }

    void update(Frontend& fe, double) override {
        // The car's description picture stays in the help box (ShowCarDesc);
        // VEHICLE SHOWCASE shows veh_tsc while it has the focus.
        menu.defaultHelp = carDescription(fe, fe.config.vehicle, !carUnlocked(fe), !paintUnlocked(fe),
                                          fe.config.vehicleColor);
    }

private:
    int vehicleIndex(Frontend& fe) const {
        const auto& v = fe.ctx.game->catalog.vehicles();
        for (std::size_t i = 0; i < v.size(); ++i)
            if (str::iequals(v[i].baseName, fe.config.vehicle))
                return static_cast<int>(i);
        return -1;
    }
    bool carUnlocked(Frontend& fe) const {
        return !fe.profile || fe.progress.vehicleUnlocked(*fe.profile, fe.config.vehicle);
    }
    bool paintUnlocked(Frontend& fe) const {
        return !fe.profile || fe.progress.variantUnlocked(*fe.profile, fe.config.vehicle, fe.config.vehicleColor);
    }

    // Entering with a locked car moves to the last unlocked car picked this
    // session, else the first unlocked one in list order.
    void enterWithUnlockedCar(Frontend& fe) {
        const auto& cars = fe.ctx.game->catalog.vehicles();
        if (cars.empty())
            return;
        if (vehicleIndex(fe) < 0)
            fe.config.vehicle = cars.front().baseName;
        auto& session = garageSession();
        if (!carUnlocked(fe)) {
            std::string car = session.lastUnlocked;
            if (car.empty() || !fe.profile || !fe.progress.vehicleUnlocked(*fe.profile, car)) {
                car.clear();
                for (const auto& v : cars)
                    if (!fe.profile || fe.progress.vehicleUnlocked(*fe.profile, v.baseName)) {
                        car = v.baseName;
                        break;
                    }
            }
            if (!car.empty()) {
                fe.config.vehicle = car;
                const auto it = session.paint.find(car);
                fe.config.vehicleColor = it != session.paint.end() ? it->second : 0;
            }
        }
        const auto* v = fe.ctx.game->catalog.vehicle(fe.config.vehicle);
        if (v && fe.config.vehicleColor >= static_cast<int>(v->colors.size()))
            fe.config.vehicleColor = 0;
        session.paint[fe.config.vehicle] = fe.config.vehicleColor;
        if (carUnlocked(fe))
            session.lastUnlocked = fe.config.vehicle;
    }

    void pickVehicle(Frontend& fe, int i) {
        const auto& cars = fe.ctx.game->catalog.vehicles();
        auto& session = garageSession();
        session.paint[fe.config.vehicle] = fe.config.vehicleColor;
        fe.config.vehicle = cars[static_cast<std::size_t>(i)].baseName;
        const auto it = session.paint.find(fe.config.vehicle);
        fe.config.vehicleColor = it != session.paint.end() ? it->second : 0;
        if (carUnlocked(fe))
            session.lastUnlocked = fe.config.vehicle;
        playSelectSound(fe);
    }

    // aud/aud22/<car>_select.22k.wav at 0.91 when a car is picked or the
    // garage is entered (VehicleSelectBase).
    void playSelectSound(Frontend& fe) const { fe.playSound(fe.config.vehicle + "_select", 0.91f); }

    // MM2 draws the selected car in 3D here (viewport 32,55 608x192, field of
    // view 0.6 rad, camera 0.18 rad above the car at its UIDist, the car
    // turning at 1 rad/s, refl_showroom.tga reflections). That viewer is not
    // implemented; the car's showcase photo stands in for it.
    void drawShowroom(Frontend& fe, ui::UiFrame& f) const {
        const std::string pic = showPicture(fe, fe.config.vehicle);
        const ui::UiTexture& t = fe.textures.get(pic);
        if (!t)
            return;
        // The photo occupies (48,84)-(360,300) of the showcase screen.
        constexpr float px0 = 48, py0 = 84, px1 = 360, py1 = 300;
        constexpr Box view{32, 55, 608, 192};
        const float h = view.h - 12.0f, w = h * (px1 - px0) / (py1 - py0);
        f.overlay.image(t.handle, view.x + (view.w - w) * 0.5f, view.y + 6.0f, w, h,
                        {px0 / 640.0f, 1.0f - py0 / 480.0f}, {px1 / 640.0f, 1.0f - py1 / 480.0f});
    }

    ui::ValueBox* m_vehicleBox = nullptr;
    ui::ValueBox* m_colorBox = nullptr;
    ui::ValueBox* m_transmission = nullptr;
    bool m_inShowcase = false;
};

// --- Vehicle showcase (menu 9) -----------------------------------------------------------------

// VehShowcase: the car's spec sheet with a DONE arrow; no navigation strip.
class ShowcasePage final : public Page {
public:
    ShowcasePage(Frontend& fe, std::string vehicle) {
        menuId = menu_id::kShowcase;
        menu.background = showPicture(fe, vehicle);
        const Vec2 p = fe.layout.position(menu_id::kShowcase, 0, layout::kNext);
        auto& done = menu.add<ui::SpriteButton>(SpriteSheet{"texture/host_dn.tga", 4}, p.x, p.y, [&fe] { fe.pop(); });
        done.sound = "Selectionmade";
        menu.onBack = [&fe] { fe.pop(); };
    }
};

// --- Results ---------------------------------------------------------------------------------------

class ResultsPage final : public Page {
public:
    ResultsPage(Frontend& fe, const game::RaceResult& r, std::optional<game::Reward> reward)
        : m_result(r), m_reward(std::move(reward)) {
        const bool crash = r.config.mode == GameMode::CrashCourse;
        menu.background = crash ? "jpg/crshi_bk.jpg" : "jpg/rshi_bk.jpg";
        // Buttons have transparent surroundings; their positions are inferred.
        constexpr float x = 380;
        float y = 60;
        auto add = [&](const char* sprite, std::function<void()> fn) -> ui::SpriteButton& {
            auto& b = menu.add<ui::SpriteButton>(SpriteSheet{sprite, 4}, x, y, std::move(fn));
            y += 72;
            return b;
        };
        auto& next = add("texture/result_nextrace.tga", [this, &fe] { nextRace(fe); });
        next.enabled = hasNextRace(fe);
        add("texture/result_restart.tga", [this, &fe] {
            fe.config = m_result.config;
            fe.startRace();
        });
        auto& menuBtn = add("texture/result_racemenu.tga", [&fe] { fe.pop(); });
        auto& replay = add("texture/result_replay.tga", [] {});
        replay.enabled = false; // replays are not implemented yet
        add("texture/result_exitw.tga", [&fe] { fe.askQuit(); });
        menu.focus(next.enabled ? &next : &menuBtn);
        menu.onBack = [&fe] { fe.pop(); };
        // No navigation strip: the results backgrounds have the logo there.
    }

    void drawAbove(Frontend& fe, ui::UiFrame& f) override {
        const auto& s = fe.ctx.game->strings;
        const auto& cfg = m_result.config;
        const auto races = fe.racesFor(cfg.mode, cfg.city);
        std::string name = modeDisplayName(fe, cfg.mode);
        if (cfg.raceIndex >= 0 && cfg.raceIndex < static_cast<int>(races.size()))
            name = races[static_cast<std::size_t>(cfg.raceIndex)]->name;
        if (cfg.mode == GameMode::CrashCourse && cfg.raceIndex >= 0)
            name = s.get(static_cast<std::uint32_t>(game::Strings::kFirstCrashCourseLesson +
                                                    (cfg.city == "sf" ? 13 : 0) + cfg.raceIndex),
                         name);
        float y = 60;
        const auto title = ui::style::titleFont();
        f.text.draw(f.overlay, title, name, 40, y, ui::style::kHelpText);
        y += 34;
        const auto font = ui::style::valueFont();
        std::string line;
        if (!m_result.finished)
            line = s.get(499, "DNF");
        else if (cfg.mode == GameMode::CrashCourse || cfg.mode == GameMode::Blitz)
            line = m_result.won ? s.get(229, "You won!") : s.get(239, "You lost!");
        else if (m_result.position >= 1 && m_result.position <= 8)
            line = s.get(static_cast<std::uint32_t>(game::Strings::kYouFinished1st + m_result.position - 1));
        f.text.draw(f.overlay, font, line, 40, y, ui::style::kValueText);
        y += 22;
        if (m_result.timeSeconds > 0) {
            f.text.draw(f.overlay, font, std::format("{} {}", s.get(354, "TIME"), formatTime(m_result.timeSeconds)), 40, y,
                        ui::style::kValueText);
            y += 22;
        }
        if (m_reward)
            f.text.drawWrapped(f.overlay, ui::style::smallFont(), m_reward->message, 40, y + 8, 240, ui::style::kHelpText);
    }

private:
    bool hasNextRace(Frontend& fe) const {
        const auto& cfg = m_result.config;
        if (cfg.mode == GameMode::Cruise || cfg.raceIndex < 0 || !fe.profile)
            return false;
        return cfg.raceIndex + 1 < static_cast<int>(fe.racesFor(cfg.mode, cfg.city).size()) &&
               fe.progress.raceOpen(&*fe.profile, cfg.city, game::modeKey(cfg.mode), cfg.raceIndex + 1);
    }
    void nextRace(Frontend& fe) {
        fe.config = m_result.config;
        ++fe.config.raceIndex;
        fe.applyRaceDefaults(fe.config);
        fe.startRace();
    }

    game::RaceResult m_result;
    std::optional<game::Reward> m_reward;
};

} // namespace

std::unique_ptr<Page> makeRacesPage(Frontend& fe) { return std::make_unique<RacesPage>(fe); }
std::unique_ptr<Page> makeVehiclePage(Frontend& fe) { return std::make_unique<VehiclePage>(fe); }
std::unique_ptr<Page> makeShowcasePage(Frontend& fe, std::string vehicle) {
    return std::make_unique<ShowcasePage>(fe, std::move(vehicle));
}
std::unique_ptr<Page> makeResultsPage(Frontend& fe, const game::RaceResult& result, std::optional<game::Reward> reward) {
    return std::make_unique<ResultsPage>(fe, result, std::move(reward));
}

} // namespace mm2::app::frontend
