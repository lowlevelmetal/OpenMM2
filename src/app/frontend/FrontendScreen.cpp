// The game's menus: page stack, shared state and the Screen wrapper.
#include "app/Screens.h"
#include "app/frontend/Frontend.h"
#include "audio/Music.h"
#include "core/Log.h"
#include "core/StringUtil.h"
#include "render/Projection.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <format>

namespace mm2::app {
namespace frontend {

// --- Frontend --------------------------------------------------------------------------

Frontend::Frontend(Context& c)
    : ctx(c), textures(c.device(), c.game->vfs), text(c.device()), store(game::ProfileStore::defaultDir()),
      progress(game::Progress::load(c.game->vfs)) {
    cities = city::listCities(c.game->vfs);
    for (const auto& info : cities)
        races.push_back(city::listRaces(c.game->vfs, info));
}

void Frontend::push(std::unique_ptr<Page> page) { m_pages.push_back(std::move(page)); }

void Frontend::pop() {
    if (m_pages.empty())
        return;
    m_graveyard.push_back(std::move(m_pages.back()));
    m_pages.pop_back();
    if (!m_pages.empty())
        m_pages.back()->onEnter(*this);
}

void Frontend::replace(std::unique_ptr<Page> page) {
    if (!m_pages.empty()) {
        m_graveyard.push_back(std::move(m_pages.back()));
        m_pages.pop_back();
    }
    push(std::move(page));
}

void Frontend::popTo(std::size_t d) {
    while (m_pages.size() > d) {
        m_graveyard.push_back(std::move(m_pages.back()));
        m_pages.pop_back();
    }
    if (!m_pages.empty())
        m_pages.back()->onEnter(*this);
}

int Frontend::cityIndex(std::string_view name) const {
    for (std::size_t i = 0; i < cities.size(); ++i)
        if (str::iequals(cities[i].mapName, name))
            return static_cast<int>(i);
    return cities.empty() ? -1 : 0;
}

const city::CityInfo* Frontend::currentCity() const {
    const int i = cityIndex(config.city);
    return i >= 0 ? &cities[static_cast<std::size_t>(i)] : nullptr;
}

std::vector<const city::RaceDefinition*> Frontend::racesFor(game::GameMode mode, std::string_view cityName) const {
    std::vector<const city::RaceDefinition*> out;
    const int ci = cityIndex(cityName);
    if (ci < 0)
        return out;
    city::RaceMode rm;
    switch (mode) {
    case game::GameMode::Blitz: rm = city::RaceMode::Blitz; break;
    case game::GameMode::Circuit: rm = city::RaceMode::Circuit; break;
    case game::GameMode::Checkpoint: rm = city::RaceMode::Checkpoint; break;
    case game::GameMode::CrashCourse: rm = city::RaceMode::CrashCourse; break;
    default: return out;
    }
    for (const auto& r : races[static_cast<std::size_t>(ci)])
        if (r.mode == rm)
            out.push_back(&r);
    return out;
}

void Frontend::selectProfile(const std::string& name) {
    for (auto& p : store.list()) {
        if (p.name == name) {
            profile = std::move(p);
            store.setLastUsed(name);
            configFromProfile();
            return;
        }
    }
}

void Frontend::saveProfile() {
    if (profile && !profile->save())
        log::warn("frontend: cannot save driver '{}'", profile->name);
}

void Frontend::configFromProfile() {
    if (!profile)
        return;
    const game::Profile& p = *profile;
    config.vehicle = p.vehicle;
    config.vehicleColor = p.vehicleColor;
    config.automatic = p.automatic;
    config.difficulty = p.difficulty;
    config.city = cityIndex(p.city) >= 0 ? cities[static_cast<std::size_t>(cityIndex(p.city))].mapName : "london";
    config.mode = p.mode == game::GameMode::CopsAndRobbers ? game::GameMode::Cruise : p.mode;
    config.raceIndex = p.raceIndex;
    config.timeOfDay = p.timeOfDay;
    config.weather = p.weather;
    config.pedestrianDensity = p.pedestrianDensity;
    config.trafficDensity = p.trafficDensity;
    config.copDensity = p.copDensity;
    config.opponents = p.opponents;
    config.laps = p.laps;
}

void Frontend::startRace() {
    // In a multiplayer lobby the vehicle page's GO DRIVE only picks the car;
    // the host starts the race for everyone.
    if (ctx.netGame && ctx.netGame->inSession()) {
        game::NetCar car = ctx.netGame->localCar();
        car.vehicle = config.vehicle;
        car.color = config.vehicleColor;
        ctx.netGame->setLocalCar(car);
        if (profile) {
            profile->vehicle = config.vehicle;
            profile->vehicleColor = config.vehicleColor;
            saveProfile();
        }
        pop();
        return;
    }
    if (profile) {
        game::Profile& p = *profile;
        p.vehicle = config.vehicle;
        p.vehicleColor = config.vehicleColor;
        p.automatic = config.automatic;
        p.city = config.city;
        p.mode = config.mode;
        p.raceIndex = config.raceIndex;
        p.timeOfDay = config.timeOfDay;
        p.weather = config.weather;
        p.pedestrianDensity = config.pedestrianDensity;
        p.trafficDensity = config.trafficDensity;
        p.copDensity = config.copDensity;
        if (config.mode != game::GameMode::Cruise) {
            p.opponents = config.opponents;
            p.laps = config.laps;
        }
        config.difficulty = p.difficulty;
        saveProfile();
    }
    log::info("frontend: starting {} in {} with {}", game::modeKey(config.mode), config.city, config.vehicle);
    ctx.nextScreen = makeRaceScreen(ctx, config);
}

void Frontend::update(double dt) {
    time += dt;
    m_graveyard.clear();
    Page* page = top();
    if (!page)
        return;
    const render::UiLayout layout = render::computeUiLayout(ctx.device().outputExtent(), ctx.display.uiScale);
    const ui::NavInput nav = navReader.read(ctx.input, layout, dt);
    ui::UiFrame f{*ctx.overlay, textures, text, nav, time};
    page->update(*this, dt);
    if (top() == page)
        page->menu.update(f);
}

void Frontend::drawPage(Page& p, ui::UiFrame& f) {
    if (!p.menu.background.empty())
        ui::drawImage(f.overlay, textures.get(p.menu.background), 0, 0, 640, 480);
    if (!p.dialogPicture.empty()) {
        const ui::UiTexture& t = textures.get(p.dialogPicture);
        ui::drawImage(f.overlay, t, p.origin.x, p.origin.y);
    }
    p.drawBelow(*this, f);
    p.menu.drawContent(f);
    p.drawAbove(*this, f);
}

void Frontend::draw() {
    auto& ov = *ctx.overlay;
    ov.begin(ctx.display.uiScale);
    const ui::NavInput none;
    ui::UiFrame f{ov, textures, text, none, time};
    // Draw from the topmost full-screen page up to the top dialog.
    std::size_t first = m_pages.empty() ? 0 : m_pages.size() - 1;
    while (first > 0 && m_pages[first]->dialog)
        --first;
    for (std::size_t i = first; i < m_pages.size(); ++i)
        drawPage(*m_pages[i], f);
    ov.end();
}

// --- Common widgets ------------------------------------------------------------------------

void addNavStrip(Frontend& fe, Page& page, bool optionsButton) {
    using namespace layout;
    // On option pages themselves the OPTIONS button is shown disabled.
    auto& opt = page.menu.add<ui::SpriteButton>(ui::SpriteSheet{"texture/mnav_opt.tga", 5}, kNavOptions.x, kNavOptions.y,
                                                 [&fe] { fe.push(makeOptionsPage(fe)); });
    opt.enabled = optionsButton;
    page.menu.add<ui::SpriteButton>(ui::SpriteSheet{"texture/mnav_hlp.tga", 3}, kNavHelp.x, kNavHelp.y, [&fe] {
        fe.message("Help: use the arrow keys or the mouse to choose, Enter to select and Escape to go back.");
    });
    page.menu.add<ui::SpriteButton>(ui::SpriteSheet{"texture/mnav_sto.tga", 3}, kNavMinimize.x, kNavMinimize.y,
                                    [&fe] { SDL_MinimizeWindow(fe.ctx.window().sdl()); });
    page.menu.add<ui::SpriteButton>(ui::SpriteSheet{"texture/mnav_ext.tga", 3}, kNavExit.x, kNavExit.y, [&fe] {
        fe.ask("jpg/quit_dlg.jpg", {400, 76}, "", [&fe] { fe.ctx.quit = true; });
    });
}

ui::SpriteButton& addBack(Frontend& fe, Page& page, const char* sprite) {
    auto& b = page.menu.add<ui::SpriteButton>(ui::SpriteSheet{sprite, 4}, layout::kBack.x, layout::kBack.y,
                                              [&fe] { fe.pop(); });
    page.menu.onBack = [&fe] { fe.pop(); };
    return b;
}

// --- Message and question dialogs ----------------------------------------------------------

namespace {

class MessageDialog final : public Page {
public:
    MessageDialog(Frontend& fe, std::string text, std::function<void()> then) : m_text(std::move(text)) {
        dialog = true;
        dialogPicture = "jpg/msg_dlg.jpg";
        origin = {120, 202};
        auto done = [&fe, then = std::move(then)] {
            fe.pop();
            if (then)
                then();
        };
        menu.add<ui::SpriteButton>(ui::SpriteSheet{"texture/dlg_ok.tga", 4}, origin.x + 290, origin.y + 40, done);
        menu.onBack = done;
    }
    void drawAbove(Frontend&, ui::UiFrame& f) override {
        f.text.drawWrapped(f.overlay, ui::style::valueFont(), m_text, origin.x + 80, origin.y + 10, 200,
                           ui::style::kValueText);
    }

private:
    std::string m_text;
};

class QuestionDialog final : public Page {
public:
    QuestionDialog(Frontend& fe, std::string picture, Vec2 size, std::string text, std::function<void()> yes)
        : m_text(std::move(text)) {
        dialog = true;
        dialogPicture = std::move(picture);
        origin = {(640 - size.x) * 0.5f, (480 - size.y) * 0.5f};
        // Small dialogs (400x76) put the buttons on the right; taller ones at the bottom.
        const bool wide = size.y < 100;
        const Vec2 yesPos = wide ? Vec2{origin.x + 180, origin.y + 23} : Vec2{origin.x + 40, origin.y + size.y - 50};
        const Vec2 noPos = wide ? Vec2{origin.x + 290, origin.y + 23} : Vec2{origin.x + size.x - 140, origin.y + size.y - 50};
        menu.add<ui::SpriteButton>(ui::SpriteSheet{"texture/dlg_yes.tga", 4}, yesPos.x, yesPos.y,
                                   [&fe, yes = std::move(yes)] {
                                       fe.pop();
                                       yes();
                                   });
        auto& no = menu.add<ui::SpriteButton>(ui::SpriteSheet{"texture/dlg_no.tga", 4}, noPos.x, noPos.y,
                                              [&fe] { fe.pop(); });
        menu.focus(&no);
        menu.onBack = [&fe] { fe.pop(); };
    }
    void drawAbove(Frontend&, ui::UiFrame& f) override {
        if (!m_text.empty())
            f.text.drawWrapped(f.overlay, ui::style::valueFont(), m_text, origin.x + 30, origin.y + 60, 240,
                               ui::style::kValueText, ui::Align::Center);
    }

private:
    std::string m_text;
};

} // namespace

void Frontend::message(std::string t, std::function<void()> then) {
    push(std::make_unique<MessageDialog>(*this, std::move(t), std::move(then)));
}

void Frontend::ask(std::string picture, Vec2 size, std::string t, std::function<void()> yes) {
    push(std::make_unique<QuestionDialog>(*this, std::move(picture), size, std::move(t), std::move(yes)));
}

// --- Names ------------------------------------------------------------------------------------

const char* timeOfDayName(game::TimeOfDay t) {
    switch (t) {
    case game::TimeOfDay::Morning: return "Morning";
    case game::TimeOfDay::Noon: return "Noon";
    case game::TimeOfDay::Evening: return "Evening";
    case game::TimeOfDay::Night: return "Night";
    }
    return "";
}

const char* weatherName(game::Weather w) {
    switch (w) {
    case game::Weather::Clear: return "Clear";
    case game::Weather::Cloudy: return "Cloudy";
    case game::Weather::Fog: return "Foggy";
    case game::Weather::Rain: return "Raining";
    case game::Weather::Snow: return "Snowing";
    }
    return "";
}

std::string modeDisplayName(Frontend& fe, game::GameMode m) {
    const auto& s = fe.ctx.game->strings;
    switch (m) {
    case game::GameMode::Cruise: return s.get(588, "Cruise");
    case game::GameMode::Blitz: return s.get(585, "Blitz");
    case game::GameMode::Circuit: return s.get(586, "Circuit");
    case game::GameMode::Checkpoint: return s.get(587, "Checkpoint");
    case game::GameMode::CrashCourse: return s.get(78, "Crash Course");
    case game::GameMode::CopsAndRobbers: return s.get(589, "Cops & Robbers");
    }
    return {};
}

std::string formatTime(float seconds) {
    if (seconds <= 0)
        return "--:--.--";
    const int total = static_cast<int>(seconds * 100.0f + 0.5f);
    return std::format("{}:{:02}.{:02}", total / 6000, (total / 100) % 60, total % 100);
}

// --- Automation ------------------------------------------------------------------------------

// OPENMM2_FRONTEND_SCRIPT drives the menus for automated screenshots, e.g.
//   "profile:Test;page:races;wait:5;nav:down;nav:right"
// Commands: profile:<name> (create/select), page:<name>, nav:<up|down|left|
// right|accept|back|tab>, wait:<frames>, mode:<cruise|blitz|circuit|race|crash>,
// city:<map>, vehicle:<name>, result:<position> (opens the results screen),
// mp:<host|join:addr|chat:text|ready|start|team:n|mode:m> (multiplayer).
class Script {
public:
    explicit Script(const char* text) {
        if (text)
            for (auto part : str::split(text, ';'))
                if (!str::trim(part).empty())
                    m_commands.emplace_back(str::trim(part));
    }
    bool active() const { return m_next < m_commands.size() || m_wait > 0; }

    // Runs commands for this frame; may inject navigation into `nav`.
    void step(Frontend& fe) {
        if (m_wait > 0) {
            --m_wait;
            return;
        }
        while (m_next < m_commands.size()) {
            const std::string& c = m_commands[m_next++];
            const auto colon = c.find(':');
            const std::string cmd = c.substr(0, colon);
            const std::string arg = colon == std::string::npos ? "" : c.substr(colon + 1);
            if (cmd == "wait") {
                m_wait = static_cast<int>(str::parseInt(arg).value_or(1));
                return;
            }
            run(fe, cmd, arg);
            if (cmd == "nav") // one key per frame
                return;
        }
    }

    platform::Key pendingKey = platform::Key::Unknown;

private:
    void run(Frontend& fe, const std::string& cmd, const std::string& arg);
    std::vector<std::string> m_commands;
    std::size_t m_next = 0;
    int m_wait = 0;
};

void Script::run(Frontend& fe, const std::string& cmd, const std::string& arg) {
    if (cmd == "profile") {
        bool found = false;
        for (const auto& p : fe.store.list())
            found |= p.name == arg;
        if (!found)
            fe.store.create(arg);
        fe.selectProfile(arg);
    } else if (cmd == "mode") {
        using game::GameMode;
        fe.config.mode = arg == "blitz"     ? GameMode::Blitz
                         : arg == "circuit" ? GameMode::Circuit
                         : arg == "race"    ? GameMode::Checkpoint
                         : arg == "crash"   ? GameMode::CrashCourse
                                            : GameMode::Cruise;
        fe.config.raceIndex = fe.config.mode == GameMode::Cruise ? -1 : 0;
    } else if (cmd == "city") {
        fe.config.city = arg;
    } else if (cmd == "vehicle") {
        fe.config.vehicle = arg;
    } else if (cmd == "go") {
        fe.startRace();
    } else if (cmd == "mp") {
        // Multiplayer automation: mp:host, mp:join:<address>, mp:chat:<text>,
        // mp:ready, mp:start, mp:team:<0|1>, mp:mode:<cruise|blitz|circuit|race|cr|crteams|crffa>.
        const auto sub = arg.substr(0, arg.find(':'));
        const auto rest = arg.find(':') == std::string::npos ? std::string() : arg.substr(arg.find(':') + 1);
        if (sub == "host") {
            if (frontendHostSession(fe))
                fe.push(makeLobbyPage(fe));
        } else if (sub == "join") {
            frontendJoinSession(fe, rest, {});
        } else if (fe.ctx.netGame && sub == "chat") {
            fe.ctx.netGame->sendChat(rest);
        } else if (fe.ctx.netGame && sub == "ready") {
            fe.ctx.netGame->setReady(true);
        } else if (fe.ctx.netGame && sub == "start") {
            fe.ctx.netGame->startRace();
        } else if (fe.ctx.netGame && sub == "team") {
            game::NetCar car = fe.ctx.netGame->localCar();
            car.team = static_cast<int>(str::parseInt(rest).value_or(0));
            fe.ctx.netGame->setLocalCar(car);
        } else if (fe.ctx.netGame && sub == "mode") {
            game::RaceConfig c = fe.ctx.netGame->raceConfig();
            using game::GameMode;
            using CR = game::CopsAndRobbersMode;
            c.mode = rest == "blitz" ? GameMode::Blitz : rest == "circuit" ? GameMode::Circuit
                   : rest == "race" ? GameMode::Checkpoint : rest.starts_with("cr") ? GameMode::CopsAndRobbers
                                                                                    : GameMode::Cruise;
            c.copsAndRobbers = rest == "crteams" ? CR::RobberTeams : rest == "crffa" ? CR::FreeForAll : CR::CopsVsRobbers;
            c.raceIndex = c.mode == GameMode::Cruise || c.mode == GameMode::CopsAndRobbers ? -1 : 0;
            if (c.mode == GameMode::CopsAndRobbers)
                c.timeLimitMinutes = 10;
            fe.ctx.netGame->setRaceConfig(c);
        }
    } else if (cmd == "result") {
        game::RaceResult r;
        r.config = fe.config;
        r.finished = true;
        r.position = static_cast<int>(str::parseInt(arg).value_or(1));
        r.won = r.position <= 3;
        r.timeSeconds = 125.43f;
        fe.push(makeResultsPage(fe, r, {}));
    } else if (cmd == "nav") {
        using platform::Key;
        pendingKey = arg == "up"       ? Key::Up
                     : arg == "down"   ? Key::Down
                     : arg == "left"   ? Key::Left
                     : arg == "right"  ? Key::Right
                     : arg == "accept" ? Key::Return
                     : arg == "back"   ? Key::Escape
                     : arg == "tab"    ? Key::Tab
                                       : Key::Unknown;
    } else if (cmd == "page") {
        if (arg == "title")
            fe.push(makeTitlePage(fe));
        else if (arg == "driver")
            fe.push(makeDriverPage(fe));
        else if (arg == "newdriver")
            fe.push(makeNewDriverDialog(fe));
        else if (arg == "stats")
            fe.push(makeDriverStatsDialog(fe));
        else if (arg == "races")
            fe.push(makeRacesPage(fe));
        else if (arg == "vehicle")
            fe.push(makeVehiclePage(fe));
        else if (arg == "showcase")
            fe.push(makeShowcasePage(fe, fe.config.vehicle));
        else if (arg == "options")
            fe.push(makeOptionsPage(fe));
        else if (arg == "graphics")
            fe.push(makeGraphicsPage(fe));
        else if (arg == "audio")
            fe.push(makeAudioPage(fe));
        else if (arg == "control")
            fe.push(makeControlPage(fe));
        else if (arg == "customize")
            fe.push(makeCustomizeControlsPage(fe));
        else if (arg == "about")
            fe.push(makeAboutPage(fe));
        else if (arg == "crashintro")
            fe.push(makeCrashIntroPage(fe));
        else if (arg == "crashlondon")
            fe.push(makeCrashCoursePage(fe, "london"));
        else if (arg == "crashsf")
            fe.push(makeCrashCoursePage(fe, "sf"));
        else if (arg == "sessions")
            fe.push(makeSessionsPage(fe));
        else if (arg == "hostoptions")
            fe.push(makeHostOptionsDialog(fe));
        else if (arg == "address")
            fe.push(makeAddressDialog(fe));
        else if (arg == "hostsettings" && fe.ctx.netGame && fe.ctx.netGame->inSession())
            fe.push(makeHostSettingsPage(fe));
        else if (arg == "eject" && fe.ctx.netGame && fe.ctx.netGame->inSession())
            fe.push(makeEjectDialog(fe));
        else if (arg == "quit")
            fe.ask("jpg/quit_dlg.jpg", {400, 76}, "", [] {});
        else if (arg == "message")
            fe.message("You cannot pick a locked vehicle");
        else if (arg == "delete")
            fe.ask("jpg/delp_dlg.jpg", {300, 225}, "", [] {});
        else
            log::warn("script: unknown page '{}'", arg);
    } else {
        log::warn("script: unknown command '{}'", cmd);
    }
}

} // namespace frontend

namespace {

using frontend::Frontend;

// The automation script runs once per process: when a scripted race ends,
// the menus come back on the results screen instead of replaying it.
const char* scriptOnce() {
    static bool used = false;
    if (used)
        return nullptr;
    used = true;
    return std::getenv("OPENMM2_FRONTEND_SCRIPT");
}

class FrontendScreen final : public Screen {
public:
    FrontendScreen(Context& ctx, const game::RaceResult* result)
        : m_fe(ctx), m_script(scriptOnce()) {
        ctx.input.startTextInput(ctx.window());
        if (auto* music = ctx.music()) {
            music->setAmbience("");
            music->playMenu();
        }
        const std::string last = m_fe.store.lastUsed();
        if (!last.empty())
            m_fe.selectProfile(last);
        if (!m_fe.profile) {
            auto all = m_fe.store.list();
            if (!all.empty())
                m_fe.selectProfile(all.front().name);
        }
        if (result && result->config.multiplayer && ctx.netGame && ctx.netGame->inSession()) {
            // Back from a multiplayer race: the lobby, over the sessions list.
            m_fe.config = result->config;
            m_fe.push(frontend::makeDriverPage(m_fe));
            m_fe.push(frontend::makeSessionsPage(m_fe));
            m_fe.push(frontend::makeLobbyPage(m_fe));
        } else if (result && m_fe.profile) {
            // Back from a race: driver page underneath, then the results.
            m_fe.config = result->config;
            m_fe.push(frontend::makeDriverPage(m_fe));
            m_fe.push(frontend::makeRacesPage(m_fe));
            std::vector<game::Reward> earned;
            if (recordable(*result))
                earned = m_fe.progress.record(*m_fe.profile, *result);
            m_fe.profile->lastRace = raceName(*result);
            m_fe.saveProfile();
            m_fe.push(frontend::makeResultsPage(m_fe, *result, std::move(earned)));
            if (auto* music = ctx.music())
                music->setState(audio::MusicState::Results);
        } else if (m_script.active()) {
            // Automation starts from the driver page.
            m_fe.push(frontend::makeDriverPage(m_fe));
        } else {
            m_fe.push(frontend::makeTitlePage(m_fe));
        }
    }

    ~FrontendScreen() override { m_fe.ctx.input.stopTextInput(m_fe.ctx.window()); }

    void update(Context& ctx, double dt) override {
        // Multiplayer: keep the session alive on every page and follow the
        // host into the race when the countdown starts.
        if (ctx.netGame) {
            ctx.netGame->update();
            if (ctx.netGame->takeRaceStart()) {
                ctx.nextScreen = makeRaceScreen(ctx, ctx.netGame->raceConfig());
                return;
            }
        }
        if (m_script.active()) {
            m_script.step(m_fe);
            if (m_script.pendingKey != platform::Key::Unknown) {
                injectKey(ctx, m_script.pendingKey);
                m_script.pendingKey = platform::Key::Unknown;
            }
        }
        m_fe.update(dt);
    }

    void drawOverlay(Context&) override { m_fe.draw(); }

private:
    // Synthesizes a key press through SDL so it flows through the normal input path.
    static void injectKey(Context& ctx, platform::Key key) {
        SDL_Event down{};
        down.type = SDL_EVENT_KEY_DOWN;
        down.key.scancode = static_cast<SDL_Scancode>(key);
        down.key.down = true;
        ctx.input.handleEvent(down);
        SDL_Event up = down;
        up.type = SDL_EVENT_KEY_UP;
        up.key.down = false;
        // Release on the next frame so keyDown() is seen once.
        SDL_PushEvent(&up);
    }

    // The driver record says statistics are kept "for races under default
    // conditions only": customised laps or opponents are not recorded.
    bool recordable(const game::RaceResult& r) {
        const auto races = m_fe.racesFor(r.config.mode, r.config.city);
        if (r.config.mode == game::GameMode::CrashCourse || r.config.mode == game::GameMode::Cruise)
            return true;
        if (r.config.raceIndex < 0 || r.config.raceIndex >= static_cast<int>(races.size()))
            return false;
        const auto* def = races[static_cast<std::size_t>(r.config.raceIndex)];
        if (!def->settings)
            return true;
        const auto& s = r.config.difficulty == game::Difficulty::Professional ? def->settings->professional
                                                                               : def->settings->amateur;
        const bool lapsDefault = r.config.mode != game::GameMode::Circuit || s.numLaps <= 0 || r.config.laps == s.numLaps;
        return lapsDefault && r.config.opponents == s.opponents;
    }

    std::string raceName(const game::RaceResult& r) {
        const auto races = m_fe.racesFor(r.config.mode, r.config.city);
        if (r.config.raceIndex >= 0 && r.config.raceIndex < static_cast<int>(races.size()))
            return races[static_cast<std::size_t>(r.config.raceIndex)]->name;
        return frontend::modeDisplayName(m_fe, r.config.mode);
    }

    Frontend m_fe;
    frontend::Script m_script;
};

} // namespace

std::unique_ptr<Screen> makeFrontendScreen(Context& ctx) { return std::make_unique<FrontendScreen>(ctx, nullptr); }

std::unique_ptr<Screen> makeFrontendScreen(Context& ctx, const game::RaceResult& result) {
    return std::make_unique<FrontendScreen>(ctx, &result);
}

} // namespace mm2::app
