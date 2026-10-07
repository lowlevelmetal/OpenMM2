// Race setup (races_bk), vehicle selection (veh_bk), vehicle showcase and the
// results screens (rshi_bk / crshi_bk).
#include "app/frontend/Frontend.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <format>

namespace mm2::app::frontend {
namespace {

using ui::Box;
using ui::SpriteSheet;
using game::GameMode;

const char* modeHelp(GameMode m) {
    switch (m) {
    case GameMode::Cruise: return "jpg/race_rom.jpg";
    case GameMode::Blitz: return "jpg/race_btz.jpg";
    case GameMode::Checkpoint: return "jpg/race_cp.jpg";
    case GameMode::Circuit: return "jpg/race_cir.jpg";
    default: return "jpg/race_rom.jpg";
    }
}

// --- Races ------------------------------------------------------------------------------

class RacesPage final : public Page {
public:
    explicit RacesPage(Frontend& fe) {
        using namespace layout;
        menu.background = "jpg/races_bk.jpg";
        auto& cfg = fe.config;
        if (cfg.mode == GameMode::CrashCourse || cfg.mode == GameMode::CopsAndRobbers)
            cfg.mode = GameMode::Cruise;

        const struct {
            GameMode mode;
            const char* sprite;
            float y;
            int frames;
        } lamps[] = {{GameMode::Cruise, "texture/cruise.tga", 62, 5},
                     {GameMode::Blitz, "texture/blitz.tga", 89, 5},
                     {GameMode::Checkpoint, "texture/cp.tga", 116, 5},
                     {GameMode::Circuit, "texture/circuit.tga", 143, 5}};
        for (const auto& l : lamps) {
            const GameMode m = l.mode;
            auto& item = menu.add<ui::LampItem>(
                SpriteSheet{l.sprite, l.frames}, kLampX, l.y, [&fe, m] { return fe.config.mode == m; },
                [this, &fe, m] { selectMode(fe, m); });
            item.help = modeHelp(m);
        }

        m_raceName = &menu.add<ui::ValueBox>(
            Box{kBoxX, 62, kBoxWide, kBoxH}, [this, &fe] { return raceNames(fe); },
            [&fe] { return fe.config.mode == GameMode::Cruise ? 0 : fe.config.raceIndex; },
            [&fe](int i) {
                fe.config.raceIndex = i;
                fe.applyRaceDefaults(fe.config);
            });
        m_laps = &menu.add<ui::ValueBox>(
            Box{kBoxX, 100, kBoxSmall, kBoxH}, [] { return numbers(1, 10); },
            [&fe] { return std::max(0, fe.config.laps - 1); }, [&fe](int i) { fe.config.laps = i + 1; });
        m_opponents = &menu.add<ui::ValueBox>(
            Box{kBoxX, 135, kBoxSmall, kBoxH}, [] { return numbers(0, 7); },
            [&fe] { return fe.config.opponents; }, [&fe](int i) { fe.config.opponents = i; });
        menu.add<ui::ValueBox>(
            Box{kBoxX, 207, kBoxWide, kBoxH},
            [&fe] {
                std::vector<std::string> v;
                for (const auto& c : fe.cities)
                    v.push_back(c.localizedName);
                return v;
            },
            [&fe] { return fe.cityIndex(fe.config.city); },
            [this, &fe](int i) {
                fe.config.city = fe.cities[static_cast<std::size_t>(i)].mapName;
                clampRace(fe);
                fe.applyRaceDefaults(fe.config);
            });
        menu.add<ui::ValueBox>(
            Box{kBoxX, 245, kBoxMid, kBoxH},
            [] {
                return std::vector<std::string>{"Morning", "Noon", "Evening", "Night"};
            },
            [&fe] { return static_cast<int>(fe.config.timeOfDay); },
            [&fe](int i) { fe.config.timeOfDay = static_cast<game::TimeOfDay>(i); });
        menu.add<ui::ValueBox>(
            Box{kBoxX + 1, 279, 128, kBoxH},
            [] { return std::vector<std::string>{"Clear", "Cloudy", "Foggy", "Raining"}; },
            [&fe] { return static_cast<int>(fe.config.weather); },
            [&fe](int i) { fe.config.weather = static_cast<game::Weather>(i); });
        // Density sliders in the grid box (pedestrian, traffic, cop).
        float* densities[] = {&fe.config.pedestrianDensity, &fe.config.trafficDensity, &fe.config.copDensity};
        for (int i = 0; i < 3; ++i) {
            float* d = densities[i];
            menu.add<ui::Slider>(Box{472, 313.5f + 35.0f * static_cast<float>(i), 139, 31}, [d] { return *d; },
                                 [d](float v) { *d = v; });
        }

        menu.add<ui::Custom>([this, &fe](ui::UiFrame& f) { drawPreview(fe, f); });
        addBack(fe, *this);
        auto& next = menu.add<ui::SpriteButton>(SpriteSheet{"texture/race_veh.tga", 4}, kNext.x, kNext.y,
                                                [&fe] { fe.push(makeVehiclePage(fe)); });
        addNavStrip(fe, *this);
        clampRace(fe);
        menu.focus(&next);
    }

    void update(Frontend& fe, double) override {
        const bool cruise = fe.config.mode == GameMode::Cruise;
        m_raceName->enabled = !cruise;
        m_laps->enabled = fe.config.mode == GameMode::Circuit;
        m_opponents->enabled = fe.config.mode == GameMode::Circuit || fe.config.mode == GameMode::Checkpoint;
        menu.defaultHelp = modeHelp(fe.config.mode);
    }

private:
    using Custom = ui::Custom;

    static std::vector<std::string> numbers(int lo, int hi) {
        std::vector<std::string> v;
        for (int i = lo; i <= hi; ++i)
            v.push_back(std::to_string(i));
        return v;
    }

    std::vector<std::string> raceNames(Frontend& fe) const {
        if (fe.config.mode == GameMode::Cruise)
            return {modeDisplayName(fe, GameMode::Cruise)};
        std::vector<std::string> names;
        const auto races = fe.racesFor(fe.config.mode, fe.config.city);
        for (int i = 0; i < static_cast<int>(races.size()); ++i)
            names.push_back(races[static_cast<std::size_t>(i)]->name);
        return names;
    }

    int available(Frontend& fe) const {
        return static_cast<int>(fe.racesFor(fe.config.mode, fe.config.city).size());
    }

    void clampRace(Frontend& fe) {
        if (fe.config.mode == GameMode::Cruise) {
            fe.config.raceIndex = -1;
            return;
        }
        fe.config.raceIndex = std::clamp(fe.config.raceIndex, 0, std::max(0, available(fe) - 1));
    }

    void selectMode(Frontend& fe, GameMode m) {
        if (fe.config.mode == m)
            return;
        fe.config.mode = m;
        fe.config.raceIndex = 0;
        clampRace(fe);
        fe.applyRaceDefaults(fe.config);
    }

    // Lower-left panel: the race map (<city>_map<mode><n>.jpg, 242x184).
    void drawPreview(Frontend& fe, ui::UiFrame& f) const {
        const auto& cfg = fe.config;
        std::string pic;
        if (cfg.mode == GameMode::Cruise)
            pic = std::format("jpg/{}_maproam.jpg", cfg.city);
        else
            pic = std::format("jpg/{}_map{}{}.jpg", cfg.city, game::modeKey(cfg.mode), std::max(0, cfg.raceIndex));
        const ui::UiTexture& t = fe.textures.get(pic);
        if (t)
            ui::drawImage(f.overlay, t, 30, 194, 242, 184);
    }

    ui::ValueBox* m_raceName = nullptr;
    ui::ValueBox* m_laps = nullptr;
    ui::ValueBox* m_opponents = nullptr;
};

// --- Vehicle ------------------------------------------------------------------------------

std::string lockPicture(Frontend& fe, const std::string& car, int variant) {
    const bool pro = fe.profile && fe.profile->difficulty == game::Difficulty::Professional;
    std::vector<std::string> candidates;
    if (variant == 0) {
        candidates = {car + (pro ? "_lck_p" : "_lck"), car + "_lck"};
    } else {
        candidates = {std::format("{}_lck{}{}", car, variant, pro ? "_p" : ""), std::format("{}_lck{}", car, variant),
                      std::format("{}_lck4{}", car, pro ? "_p" : ""), car + "_lck4"};
    }
    for (const auto& c : candidates)
        if (fe.ctx.game->vfs.exists("jpg/" + c + ".jpg"))
            return "jpg/" + c + ".jpg";
    return {};
}

std::string showPicture(Frontend& fe, const std::string& car) {
    for (const auto& c : {car + "_show", car})
        if (fe.ctx.game->vfs.exists("jpg/" + c + ".jpg"))
            return "jpg/" + c + ".jpg";
    return {};
}

class VehiclePage final : public Page {
public:
    explicit VehiclePage(Frontend& fe) {
        using namespace layout;
        menu.background = "jpg/veh_bk.jpg";
        const auto& cat = fe.ctx.game->catalog;
        for (const auto& v : cat.vehicles()) {
            m_maxHp = std::max(m_maxHp, v.horsepower);
            m_maxSpeed = std::max(m_maxSpeed, v.topSpeedMph);
            m_maxDur = std::max(m_maxDur, v.durability);
            m_maxMass = std::max(m_maxMass, v.massLb);
        }
        if (vehicleIndex(fe) < 0 && !cat.vehicles().empty())
            fe.config.vehicle = cat.vehicles().front().baseName;

        menu.add<ui::Custom>([this, &fe](ui::UiFrame& f) { drawDisplay(fe, f); });
        auto& show = menu.add<ui::SpriteButton>(SpriteSheet{"texture/veh_show.tga", 4}, 253, 55,
                                                [&fe] { fe.push(makeShowcasePage(fe, fe.config.vehicle)); });
        show.help = "jpg/veh_tsc.jpg";
        m_vehicleBox = &menu.add<ui::ValueBox>(
            Box{kBoxX, 273, kBoxWide, kBoxH},
            [&fe] {
                std::vector<std::string> v;
                for (const auto& info : fe.ctx.game->catalog.vehicles())
                    v.push_back(info.description);
                return v;
            },
            [this, &fe] { return vehicleIndex(fe); },
            [&fe](int i) {
                fe.config.vehicle = fe.ctx.game->catalog.vehicles()[static_cast<std::size_t>(i)].baseName;
                fe.config.vehicleColor = 0;
            });
        m_vehicleBox->wrap = true;
        m_colorBox = &menu.add<ui::ValueBox>(
            Box{kBoxX, 311, kBoxWide, kBoxH},
            [&fe] {
                const auto* v = fe.ctx.game->catalog.vehicle(fe.config.vehicle);
                return v ? v->colors : std::vector<std::string>{};
            },
            [&fe] { return fe.config.vehicleColor; }, [&fe](int i) { fe.config.vehicleColor = i; });
        m_colorBox->wrap = true;
        menu.add<ui::ValueBox>(
            Box{kBoxX, 346, kBoxMid, 27},
            [&fe] {
                const auto& s = fe.ctx.game->strings;
                return std::vector<std::string>{s.get(634, "Automatic"), s.get(633, "Manual")};
            },
            [&fe] { return fe.config.automatic ? 0 : 1; }, [&fe](int i) { fe.config.automatic = i == 0; });
        addBack(fe, *this);
        m_go = &menu.add<ui::SpriteButton>(SpriteSheet{"texture/veh_go.tga", 4}, kNext.x, kNext.y, [this, &fe] {
            if (!unlocked(fe))
                fe.push(lockedDialog(fe));
            else
                fe.startRace();
        });
        addNavStrip(fe, *this);
        menu.focus(m_vehicleBox);
    }

    void update(Frontend& fe, double) override {
        const std::string& car = fe.config.vehicle;
        if (!vehicleUnlocked(fe))
            m_vehicleBox->help = lockPicture(fe, car, 0);
        else
            m_vehicleBox->help = std::format("jpg/{}_ulck.jpg", car);
        m_colorBox->help = colorUnlocked(fe) ? m_vehicleBox->help : lockPicture(fe, car, fe.config.vehicleColor);
        menu.defaultHelp = m_vehicleBox->help;
        m_go->help = unlocked(fe) ? "jpg/veh_tsc.jpg" : m_colorBox->help;
    }

private:
    int vehicleIndex(Frontend& fe) const {
        const auto& v = fe.ctx.game->catalog.vehicles();
        for (std::size_t i = 0; i < v.size(); ++i)
            if (str::iequals(v[i].baseName, fe.config.vehicle))
                return static_cast<int>(i);
        return -1;
    }
    bool vehicleUnlocked(Frontend& fe) const {
        return !fe.profile || fe.progress.vehicleUnlocked(*fe.profile, fe.config.vehicle);
    }
    bool colorUnlocked(Frontend& fe) const {
        return !fe.profile || fe.progress.variantUnlocked(*fe.profile, fe.config.vehicle, fe.config.vehicleColor);
    }
    bool unlocked(Frontend& fe) const { return vehicleUnlocked(fe) && colorUnlocked(fe); }

    // lock_dlg.jpg already says "YOU CANNOT PICK A LOCKED VEHICLE".
    std::unique_ptr<Page> lockedDialog(Frontend& fe) {
        class LockDialog final : public Page {
        public:
            explicit LockDialog(Frontend& f) {
                dialog = true;
                dialogPicture = "jpg/lock_dlg.jpg";
                origin = {120, 202};
                menu.add<ui::SpriteButton>(SpriteSheet{"texture/dlg_ok.tga", 4}, origin.x + 290, origin.y + 40,
                                           [&f] { f.pop(); });
                menu.onBack = [&f] { f.pop(); };
            }
        };
        return std::make_unique<LockDialog>(fe);
    }

    // The 3D showroom area. The original rotated the 3D car here (it ships
    // refl_showroom.tga for its reflections); until the model viewer exists
    // the car's showcase photo is shown instead.
    void drawDisplay(Frontend& fe, ui::UiFrame& f) const {
        const std::string pic = showPicture(fe, fe.config.vehicle);
        if (const ui::UiTexture& t = fe.textures.get(pic); t) {
            // The photo occupies roughly (30,84)-(360,300) of the showcase screen.
            constexpr float px0 = 48, py0 = 84, px1 = 360, py1 = 300;
            const float h = 180, w = h * (px1 - px0) / (py1 - py0);
            const float x = 332 - w * 0.5f, y = 60;
            f.overlay.image(t.handle, x, y, w, h, {px0 / 640.0f, 1.0f - py0 / 480.0f}, {px1 / 640.0f, 1.0f - py1 / 480.0f});
        }
        if (!vehicleUnlocked(fe)) {
            const ui::UiTexture& lock = fe.textures.get("texture/locked.tga");
            ui::drawImage(f.overlay, lock, 332 - 135, 120);
        }
        // Relative performance scores (grid box 150..285 x 276..375).
        const auto* v = fe.ctx.game->catalog.vehicle(fe.config.vehicle);
        if (!v)
            return;
        const float values[] = {ratio(v->horsepower, m_maxHp), ratio(v->topSpeedMph, m_maxSpeed),
                                ratio(v->durability, m_maxDur), ratio(v->massLb, m_maxMass)};
        const float rows[] = {283, 306, 329, 352};
        for (int i = 0; i < 4; ++i) {
            const float w = 128.0f * values[i];
            f.overlay.rect(153, rows[i], w, 14, render::packColor(250, 200, 40, 230));
            f.overlay.rect(153, rows[i], w, 3, render::packColor(255, 245, 160, 230));
        }
    }

    static float ratio(int v, int max) { return max > 0 ? std::clamp(static_cast<float>(v) / static_cast<float>(max), 0.05f, 1.0f) : 0.0f; }

    int m_maxHp = 0, m_maxSpeed = 0, m_maxDur = 0, m_maxMass = 0;
    ui::ValueBox* m_vehicleBox = nullptr;
    ui::ValueBox* m_colorBox = nullptr;
    ui::SpriteButton* m_go = nullptr;
};

// --- Vehicle showcase (vp*_show.jpg) ------------------------------------------------------------

class ShowcasePage final : public Page {
public:
    ShowcasePage(Frontend& fe, std::string vehicle) {
        menu.background = showPicture(fe, vehicle);
        menu.onBack = [&fe] { fe.pop(); };
    }
    void update(Frontend& fe, double) override {
        const auto& in = fe.ctx.input;
        if (in.keyPressed(platform::Key::Return) || in.keyPressed(platform::Key::Space) ||
            in.mousePressed(platform::MouseButton::Left))
            fe.pop();
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
        add("texture/result_exitw.tga", [&fe] { fe.ask("jpg/quit_dlg.jpg", {400, 76}, "", [&fe] { fe.ctx.quit = true; }); });
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
