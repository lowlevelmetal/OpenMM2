#pragma once

// Internal interfaces of the frontend (menus). See docs/frontend.md for the
// screen flow and where the layout comes from.

#include "app/Context.h"
#include "city/CityData.h"
#include "game/Profile.h"
#include "game/RaceConfig.h"
#include "ui/MenuLayout.h"
#include "ui/Text.h"
#include "ui/TextureCache.h"
#include "ui/Widgets.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mm2::app::frontend {

class Frontend;

// One screen or dialog of the frontend.
class Page {
public:
    virtual ~Page() = default;
    ui::Menu menu;
    // Dialogs draw on top of the page below them and offset their widgets by
    // `origin` (the dialog picture's position on the 640x480 screen).
    bool dialog = false;
    Vec2 origin{0, 0};
    std::string dialogPicture; // jpg/..._dlg.jpg

    virtual void update(Frontend&, double /*dt*/) {}
    virtual void drawBelow(Frontend&, ui::UiFrame&) {} // after background, before widgets
    virtual void drawAbove(Frontend&, ui::UiFrame&) {} // after widgets
    virtual void onEnter(Frontend&) {}                 // when it becomes the top page again
};

// State shared by all pages and the page stack.
class Frontend {
public:
    Frontend(Context& ctx);

    Context& ctx;
    ui::TextureCache textures;
    ui::TextRenderer text;
    ui::NavReader navReader;
    ui::MenuLayout layout; // tune/widget.csv + tune/menu.csv
    game::ProfileStore store;
    game::Progress progress;
    game::HallOfFame hallOfFame; // <players dir>/records.ini
    std::optional<game::Profile> profile;
    std::vector<city::CityInfo> cities;
    std::vector<std::vector<city::RaceDefinition>> races; // per city, parallel to `cities`
    game::RaceConfig config;                                // being assembled by the menus
    double time = 0.0;

    // Page stack.
    void push(std::unique_ptr<Page> page);
    void pop();
    void replace(std::unique_ptr<Page> page); // pops the top page, pushes `page`
    void popTo(std::size_t depth);            // keeps the bottom `depth` pages
    std::size_t depth() const { return m_pages.size(); }
    Page* top() const { return m_pages.empty() ? nullptr : m_pages.back().get(); }

    // Message box (msg_dlg) with an OK button.
    void message(std::string text, std::function<void()> then = {});
    // Yes/No question on a dialog picture.
    void ask(std::string picture, Vec2 size, std::string text, std::function<void()> yes);

    // Profile helpers.
    void selectProfile(const std::string& name);
    void saveProfile();

    // City/race lookups.
    int cityIndex(std::string_view name) const;
    const city::CityInfo* currentCity() const;
    // Races of the current mode in the current city (blitz/circuit/checkpoint/crash).
    std::vector<const city::RaceDefinition*> racesFor(game::GameMode mode, std::string_view city) const;

    // Fills `config` from the profile's last choices.
    void configFromProfile();
    // Applies the selected race's default environment (MM2
    // `RaceMenuBase::SetStateRace`) for `cfg.difficulty`.
    void applyRaceDefaults(game::RaceConfig& cfg) const;
    // Name of the race `cfg` describes ("Cruise" etc. for the modes without races).
    std::string raceName(const game::RaceConfig& cfg) const;
    // Records a finished race in the driver's profile and the race records;
    // returns the reward it announces.
    std::optional<game::Reward> recordResult(const game::RaceResult& result);
    // Starts the session described by `config`.
    void startRace();

    void update(double dt);
    void draw();

private:
    void drawPage(Page& p, ui::UiFrame& f);
    std::vector<std::unique_ptr<Page>> m_pages;
    std::vector<std::unique_ptr<Page>> m_graveyard; // popped this frame; freed after drawing
};

// Page factories.
std::unique_ptr<Page> makeTitlePage(Frontend& fe);
std::unique_ptr<Page> makeDriverPage(Frontend& fe);
std::unique_ptr<Page> makeNewDriverDialog(Frontend& fe);
std::unique_ptr<Page> makeDriverStatsDialog(Frontend& fe);
std::unique_ptr<Page> makeRacesPage(Frontend& fe);
std::unique_ptr<Page> makeVehiclePage(Frontend& fe);
std::unique_ptr<Page> makeShowcasePage(Frontend& fe, std::string vehicle);
std::unique_ptr<Page> makeOptionsPage(Frontend& fe);
std::unique_ptr<Page> makeGraphicsPage(Frontend& fe);
std::unique_ptr<Page> makeAudioPage(Frontend& fe);
std::unique_ptr<Page> makeControlPage(Frontend& fe);
std::unique_ptr<Page> makeCustomizeControlsPage(Frontend& fe);
std::unique_ptr<Page> makeAboutPage(Frontend& fe);
std::unique_ptr<Page> makeCrashIntroPage(Frontend& fe);
std::unique_ptr<Page> makeCrashCoursePage(Frontend& fe, std::string city);
std::unique_ptr<Page> makeResultsPage(Frontend& fe, const game::RaceResult& result, std::optional<game::Reward> reward);
std::unique_ptr<Page> makeSessionsPage(Frontend& fe);
// Multiplayer (PagesMulti.cpp).
std::unique_ptr<Page> makeHostOptionsDialog(Frontend& fe);
std::unique_ptr<Page> makeAddressDialog(Frontend& fe);
std::unique_ptr<Page> makeLobbyPage(Frontend& fe);
std::unique_ptr<Page> makeHostSettingsPage(Frontend& fe);
std::unique_ptr<Page> makeEjectDialog(Frontend& fe);
// Automation helpers: host with the current config / join an address.
bool frontendHostSession(Frontend& fe);
void frontendJoinSession(Frontend& fe, const std::string& address, const std::string& password);

// MM2's menu ids: the first column of tune/menu.csv and the second of
// tune/widget.csv (the UIMenu constructor arguments in MM2
// `mmInterface::mmInterface`). Widget positions are looked up with these and
// the widget's creation index in the original menu (ui::MenuLayout).
namespace menu_id {
inline constexpr int kNavBar = 0;
inline constexpr int kMain = 1;
inline constexpr int kOptions = 2;
inline constexpr int kAudio = 3;
inline constexpr int kGraphics = 4;
inline constexpr int kControl = 5;
inline constexpr int kRace = 7;
inline constexpr int kVehicle = 8;
inline constexpr int kShowcase = 9;
inline constexpr int kNetSelect = 10;
inline constexpr int kHostRace = 11;
inline constexpr int kNetArena = 12;
inline constexpr int kNewDriver = 17;
inline constexpr int kDriverRecord = 19;
inline constexpr int kHallOfFame = 20;
inline constexpr int kRaceEnvironment = 22;
inline constexpr int kLocked = 23;
inline constexpr int kQuit = 27;
inline constexpr int kDeleteDriver = 28;
inline constexpr int kDuplicateDriver = 29;
inline constexpr int kDefaults = 31;
inline constexpr int kAbout = 34;
inline constexpr int kCrashCourse = 39;
inline constexpr int kCrashIntro = 40;
inline constexpr int kControlCustom = 41;
} // namespace menu_id

// Common layout positions recovered from the art (see docs/frontend.md).
namespace layout {
// Top-right navigation strip: OPTIONS, help, minimise, exit.
inline constexpr Vec2 kNavOptions{439, 1};
inline constexpr Vec2 kNavHelp{540, 1};
inline constexpr Vec2 kNavMinimize{564, 1};
inline constexpr Vec2 kNavExit{594, 1};
// Bottom navigation: back (left arrow) and continue (right arrow).
inline constexpr Vec2 kBack{290, 415};
inline constexpr Vec2 kNext{439, 415};
// Right-hand column of big arrow buttons (main screen and options).
inline constexpr float kColumnX = 439;
inline constexpr float kRow59 = 242, kRow58 = 301, kRow56 = 359, kRow65 = 415;
// Left panel rows (lamp items): x 40, 27 px apart.
inline constexpr float kLampX = 40;
// Value boxes in the right half.
inline constexpr float kBoxX = 400;
inline constexpr float kBoxWide = 210, kBoxMid = 129, kBoxSmall = 48, kBoxH = 28;
} // namespace layout

// Adds the top navigation strip (options / help / minimise / exit).
void addNavStrip(Frontend& fe, Page& page, bool optionsButton = true);
// Adds a BACK button that pops the page.
ui::SpriteButton& addBack(Frontend& fe, Page& page, const char* sprite = "texture/mnav_prv.tga");

// Display names.
const char* timeOfDayName(game::TimeOfDay t);
const char* weatherName(game::Weather w);
std::string modeDisplayName(Frontend& fe, game::GameMode m);
std::string formatTime(float seconds);

} // namespace mm2::app::frontend
