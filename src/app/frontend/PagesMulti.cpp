// Multiplayer menus: sessions (sess_bk), host options (host_dlg), address
// (tcp_dlg), password (pass_dlg), lobby (lobbh_bk / lobbj_bk, identical
// pictures), host settings (host_bk) and eject (ejct_dlg).
//
// The original offered DirectPlay providers (MSN Gaming Zone, IPX, TCP/IP,
// serial, modem); OpenMM2 replaces them with its own UDP networking over
// TCP/IP networks (Internet or LAN), so only that lamp is available.
// Positions: sprites matched against the backgrounds where they were cut
// from them (sess_hst/sess_jn, the multiplayer race lamps, the Cops & Robbers
// lamps, team lamps, the bottom-right arrow buttons, the host_rnm/host_lap/
// host_cr panels); the small lobby buttons and the eject list are inferred.
// See docs/multiplayer.md.
#include "app/frontend/Frontend.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/StringUtil.h"
#include "game/net/NetGame.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <format>

namespace mm2::app::frontend {
namespace {

using game::GameMode;
using game::NetGame;
using ui::Box;
using ui::SpriteSheet;
using namespace layout;

// Original dialogs of this size sit here (tune/menu.csv: "Host Options,
// serial_dlg,36,120,75,400,330").
constexpr Vec2 kDialogOrigin{120, 75};

// The menu ids of MM2's network dialogs (tune/widget.csv).
constexpr int kAddressDialog = 14;  // Dialog_TCPIP, "Enter an Address"
constexpr int kBadPassDialog = 24;  // badpass_dlg
constexpr int kPasswordDialog = 25; // Dialog_Password, "Enter a valid password"
constexpr int kHostDialog = 36;     // Dialog_Host, "Host Options"

// PUMenuBase::PUMenuBase centres a dialog on its picture's size.
Vec2 centredOrigin(Frontend& fe, const char* picture, Vec2 fallback) {
    const ui::UiTexture& t = fe.textures.get(picture);
    return ui::dialogOrigin(t ? Vec2{static_cast<float>(t.width), static_cast<float>(t.height)} : fallback);
}

// The dialogs' Cancel (dlg_can, left) and DONE (dlg_done, right) at their
// tune/widget.csv places (code: 0.05 and 0.55 of the card, 0.6 down).
void addCancelDone(Frontend& fe, Page& page, int id, int cancelIndex, Vec2 cancelCode, int doneIndex, Vec2 doneCode,
                   std::function<void()> cancel, std::function<void()> done) {
    const Vec2 c = fe.layout.position(id, cancelIndex, cancelCode, page.origin);
    const Vec2 d = fe.layout.position(id, doneIndex, doneCode, page.origin);
    page.menu.add<ui::SpriteButton>(SpriteSheet{"texture/dlg_can.tga", 4}, c.x, c.y, std::move(cancel)).sound =
        "Selectionmade";
    page.menu.add<ui::SpriteButton>(SpriteSheet{"texture/dlg_done.tga", 4}, d.x, d.y, std::move(done)).sound =
        "Selectionmade";
}

std::string netName(Frontend& fe) {
    if (fe.profile)
        return fe.profile->netName.empty() ? fe.profile->name : fe.profile->netName;
    return fe.ctx.settings.playerName;
}

game::NetCar netCar(Frontend& fe) {
    game::NetCar car;
    car.vehicle = fe.config.vehicle;
    car.color = fe.config.vehicleColor;
    if (fe.ctx.netGame)
        car.team = fe.ctx.netGame->localCar().team;
    return car;
}

// The NetGame lives in the context while the multiplayer menus are open (and
// during multiplayer races).
NetGame& ensureNetGame(Frontend& fe) {
    Context& ctx = fe.ctx;
    if (!ctx.netGame) {
        game::NetOptions o;
        o.playerName = netName(fe);
        o.port = static_cast<std::uint16_t>(ctx.settings.port);
        // Automatic port forwarding: the [Network] UPnP setting, which the
        // OPENMM2_NO_PORTMAP environment variable overrides (automated runs
        // must not open ports on the user's router).
        o.portMapping = ctx.settings.upnp && !std::getenv("OPENMM2_NO_PORTMAP");
        o.portMapState = paths::userDataDir() / "portmap.ini";
        ctx.netGame = std::make_unique<NetGame>(std::move(o));
    }
    return *ctx.netGame;
}

std::string cityName(Frontend& fe, const std::string& map) {
    const int i = fe.cityIndex(map);
    return i >= 0 ? fe.cities[static_cast<std::size_t>(i)].localizedName : map;
}

std::string vehicleName(Frontend& fe, const std::string& car) {
    if (const auto* v = fe.ctx.game->catalog.vehicle(car))
        return v->description;
    return car;
}

std::string colorName(Frontend& fe, const std::string& car, int color) {
    if (const auto* v = fe.ctx.game->catalog.vehicle(car))
        if (color >= 0 && color < static_cast<int>(v->colors.size()))
            return v->colors[static_cast<std::size_t>(color)];
    return std::to_string(color + 1);
}

const char* crModeName(game::CopsAndRobbersMode m) {
    switch (m) {
    case game::CopsAndRobbersMode::FreeForAll: return "Free-For-All";
    case game::CopsAndRobbersMode::CopsVsRobbers: return "Cops vs. Robbers";
    case game::CopsAndRobbersMode::RobberTeams: return "Robber Teams";
    }
    return "";
}

// The Cops & Robbers limits and gold masses HostRaceMenu::InitCRWidgets
// offers, with its strings (510-513, 514-517, 506-508).
constexpr int kTimeLimits[] = {5, 10, 20, 30};
constexpr int kPointLimits[] = {100, 250, 500, 1000};
const char* const kGoldMass[] = {"Weightless", "Quarter Ton", "Half Ton"};
constexpr std::uint32_t kTimeLimitStrings = 510, kPointLimitStrings = 514, kGoldMassStrings = 506;

std::string raceTitle(Frontend& fe, const game::RaceConfig& c) {
    std::string title = modeDisplayName(fe, c.mode);
    if (c.mode == GameMode::CopsAndRobbers)
        return std::format("{} ({})", title, crModeName(c.copsAndRobbers));
    if (c.raceIndex >= 0) {
        const auto races = fe.racesFor(c.mode, c.city);
        if (c.raceIndex < static_cast<int>(races.size()))
            title += " - " + races[static_cast<std::size_t>(c.raceIndex)]->name;
    }
    return title;
}

// Joins `address` (maybe asking for a password first) and shows progress.
void joinSession(Frontend& fe, const net::Address& address, bool needsPassword, const std::string& password);

// --- Connecting (msg_dlg) -------------------------------------------------------------------

class ConnectingDialog final : public Page {
public:
    ConnectingDialog(Frontend& fe, net::Address address, std::string password)
        : m_address(address), m_password(std::move(password)), m_target(address.toString()) {
        dialog = true;
        dialogPicture = "jpg/msg_dlg.jpg";
        origin = {120, 202}; // tune/menu.csv: "Retrieving Game Settings...,...,120,202,400,76"
        menu.add<ui::SpriteButton>(SpriteSheet{"texture/dlg_can.tga", 4}, origin.x + 290, origin.y + 40, [&fe] {
            if (fe.ctx.netGame)
                fe.ctx.netGame->leave();
            fe.pop();
        });
        menu.onBack = [&fe] {
            if (fe.ctx.netGame)
                fe.ctx.netGame->leave();
            fe.pop();
        };
    }

    void update(Frontend& fe, double) override;

    void drawAbove(Frontend&, ui::UiFrame& f) override {
        f.text.drawWrapped(f.overlay, ui::style::valueFont(),
                           std::format("Retrieving Game Settings...\n{}", m_target), origin.x + 20, origin.y + 10, 260,
                           ui::style::kValueText);
    }

private:
    net::Address m_address;
    std::string m_password;
    std::string m_target;
};

// --- Sessions (sess_bk) -----------------------------------------------------------------------

class SessionsPage final : public Page {
public:
    explicit SessionsPage(Frontend& fe) {
        menuId = menu_id::kNetSelect;
        menu.background = "jpg/sess_bk.jpg";
        menu.defaultHelp = "jpg/mn_mp.jpg";
        m_netName = netName(fe);
        NetGame& net = ensureNetGame(fe);
        std::string error;
        if (!net.startLanScan(&error))
            log::warn("multiplayer: LAN browser unavailable: {}", error);

        // Connection providers: only TCP/IP has a modern equivalent (OpenMM2's
        // own UDP protocol over the Internet or a LAN).
        const struct {
            const char* sprite;
            float y;
            bool available;
            const char* help;
        } providers[] = {{"texture/sess_zn.tga", 54, false, "jpg/mpst_tzn.jpg"},
                         {"texture/sess_ipx.tga", 81, false, "jpg/mpst_tpx.jpg"},
                         {"texture/sess_tcp.tga", 108, true, "jpg/mpst_tcp.jpg"},
                         {"texture/sess_ser.tga", 135, false, "jpg/mpst_tsr.jpg"},
                         {"texture/sess_mod.tga", 162, false, "jpg/mpst_tmd.jpg"}};
        for (const auto& p : providers) {
            auto& item = menu.add<ui::LampItem>(SpriteSheet{p.sprite, 5}, kLampX, p.y,
                                                [available = p.available] { return available; }, [] {});
            item.enabled = p.available;
            item.help = p.help;
        }
        auto& name = menu.add<ui::TextEntry>(Box{kBoxX, 66, kBoxWide, kBoxH}, &m_netName, net::kMaxNameLength);
        name.onCommit = [this, &fe] {
            const auto trimmed = std::string(str::trim(m_netName));
            m_netName = trimmed.empty() ? netName(fe) : trimmed;
            if (fe.profile) {
                fe.profile->netName = m_netName;
                fe.saveProfile();
            }
            // The name is fixed per NetGame: start a fresh one with it.
            if (fe.ctx.netGame && !fe.ctx.netGame->inSession()) {
                fe.ctx.netGame.reset();
                ensureNetGame(fe).startLanScan();
            }
        };
        auto& host = menu.add<ui::SpriteButton>(SpriteSheet{"texture/sess_hst.tga", 4}, kColumnX, 98,
                                                [&fe] { fe.push(makeHostOptionsDialog(fe)); });
        host.help = "jpg/lobb_srv.jpg";
        auto& join = menu.add<ui::SpriteButton>(SpriteSheet{"texture/sess_jn.tga", 4}, kColumnX, 167, [this, &fe] {
            // A session picked in the list joins directly; otherwise ask for an
            // address ("leave blank to search for available sessions").
            const auto list = fe.ctx.netGame ? fe.ctx.netGame->lanSessions() : std::vector<net::DiscoveredSession>{};
            if (m_selected >= 0 && m_selected < static_cast<int>(list.size()))
                joinListed(fe, list[static_cast<std::size_t>(m_selected)]);
            else
                fe.push(makeAddressDialog(fe));
        });
        join.help = "jpg/mn_mp.jpg";
        m_list = &menu.add<ui::ListBox>(
            Box{298, 246, 306, 163}, [this, &fe] { return rows(fe); }, [this] { return m_selected; },
            [this](int i) { m_selected = i; });
        m_list->onDoubleClick = [this, &fe] {
            const auto list = fe.ctx.netGame ? fe.ctx.netGame->lanSessions() : std::vector<net::DiscoveredSession>{};
            if (m_selected >= 0 && m_selected < static_cast<int>(list.size()))
                joinListed(fe, list[static_cast<std::size_t>(m_selected)]);
        };
        auto& back = addBack(fe, *this);
        back.onClick = [&fe] { leave(fe); };
        menu.onBack = [&fe] { leave(fe); };
        addNavStrip(fe, *this);
        menu.focus(&host);
    }

    void update(Frontend& fe, double) override {
        // A session that ended while we were elsewhere (e.g. back from the lobby).
        if (fe.ctx.netGame && !fe.ctx.netGame->inSession())
            if (auto notice = fe.ctx.netGame->takeNotice())
                fe.message(*notice);
    }

    void onEnter(Frontend& fe) override {
        if (fe.ctx.netGame && !fe.ctx.netGame->inSession())
            fe.ctx.netGame->startLanScan();
    }

    void drawAbove(Frontend& fe, ui::UiFrame& f) override {
        const auto list = fe.ctx.netGame ? fe.ctx.netGame->lanSessions() : std::vector<net::DiscoveredSession>{};
        if (list.empty())
            f.text.drawWrapped(f.overlay, ui::style::smallFont(), "Looking for games...", 306, 256, 290,
                               ui::style::kValueTextDisabled); // string 657
    }

private:
    static void leave(Frontend& fe) {
        fe.ctx.netGame.reset();
        fe.pop();
    }

    std::vector<std::string> rows(Frontend& fe) const {
        std::vector<std::string> out;
        if (!fe.ctx.netGame)
            return out;
        for (const auto& s : fe.ctx.netGame->lanSessions()) {
            const auto& a = s.advert;
            std::string row = std::format("{}  {}/{}  {}", a.sessionName, a.players, a.maxPlayers,
                                          cityName(fe, a.city));
            if (a.hasPassword)
                row += "  (password)";
            if (a.phase != net::SessionPhase::Lobby)
                row += "  (racing)";
            if (!s.compatible())
                row += "  (other version)";
            if (s.pingMs)
                row += std::format("  {} ms", s.pingMs);
            out.push_back(std::move(row));
        }
        return out;
    }

    static void joinListed(Frontend& fe, const net::DiscoveredSession& s) {
        if (!s.compatible()) {
            fe.message("ERROR: Network versions do not match."); // string 83
            return;
        }
        joinSession(fe, s.address, s.advert.hasPassword, {});
    }

    std::string m_netName;
    int m_selected = -1;
    ui::ListBox* m_list = nullptr;
};

// --- Host options (host_dlg) --------------------------------------------------------------

class HostOptionsDialog final : public Page {
public:
    // Dialog_Host (menu 36): the password field, the Max Players roller,
    // Cancel and DONE, at their tune/widget.csv places on the centred card.
    explicit HostOptionsDialog(Frontend& fe) {
        dialog = true;
        dialogPicture = "jpg/host_dlg.jpg";
        menuId = kHostDialog;
        origin = centredOrigin(fe, "jpg/host_dlg.jpg", {400, 330});
        const auto& l = fe.layout;
        auto& pw = menu.add<ui::TextEntry>(l.widget(kHostDialog, 0, {72, 91, 203, 22}, origin), &m_password, 24);
        pw.onCommit = [] {};
        menu.add<ui::Roller>(
            l.widget(kHostDialog, 1, {248, 156, 60, 32}, origin),
            [] {
                std::vector<std::string> v;
                for (int i = 2; i <= 8; ++i)
                    v.push_back(std::to_string(i));
                return v;
            },
            [this] { return m_maxPlayers - 2; }, [this](int i) { m_maxPlayers = i + 2; });
        addCancelDone(fe, *this, kHostDialog, 2, {18, 276}, 3, {280, 276}, [&fe] { fe.pop(); },
                      [this, &fe] { host(fe); });
        menu.onBack = [&fe] { fe.pop(); };
        menu.setInitialFocus(&pw);
    }

    void drawAbove(Frontend&, ui::UiFrame& f) override {
        if (!m_error.empty())
            f.text.drawWrapped(f.overlay, ui::style::smallFont(), m_error, origin.x + 40, origin.y + 210, 320,
                               ui::style::kHelpText, ui::Align::Center);
    }

private:
    void host(Frontend& fe) {
        NetGame& net = ensureNetGame(fe);
        game::RaceConfig cfg = fe.config;
        // Crash Course is single player only.
        if (cfg.mode == GameMode::CrashCourse)
            cfg.mode = GameMode::Cruise;
        if (cfg.mode == GameMode::Cruise || cfg.mode == GameMode::CopsAndRobbers)
            cfg.raceIndex = -1;
        else
            cfg.raceIndex = std::max(cfg.raceIndex, 0);
        if (cfg.laps <= 0)
            cfg.laps = 3;
        cfg.multiplayer = true;
        game::NetHostOptions opts;
        opts.password = std::string(str::trim(m_password));
        opts.maxPlayers = m_maxPlayers;
        net.stopLanScan();
        std::string error;
        if (!net.host(cfg, opts, netCar(fe), &error)) {
            m_error = error.empty() ? "Cannot create the session." : error;
            net.startLanScan();
            return;
        }
        fe.replace(makeLobbyPage(fe));
    }

    std::string m_password;
    int m_maxPlayers = 8;
    std::string m_error;
};

// --- Address (tcp_dlg) ---------------------------------------------------------------------

class AddressDialog final : public Page {
public:
    // Dialog_TCPIP (menu 14): the address field, Cancel and DONE; Enter in
    // the field is DONE (Dialog_TCPIP::IPAddressCallback).
    explicit AddressDialog(Frontend& fe) {
        dialog = true;
        dialogPicture = "jpg/tcp_dlg.jpg";
        menuId = kAddressDialog;
        origin = centredOrigin(fe, "jpg/tcp_dlg.jpg", {300, 225});
        auto& entry =
            menu.add<ui::TextEntry>(fe.layout.widget(kAddressDialog, 0, {72, 90, 203, 22}, origin), &m_address, 64);
        entry.onCommit = [this, &fe] { go(fe); };
        addCancelDone(fe, *this, kAddressDialog, 1, {18, 276}, 2, {280, 276}, [&fe] { fe.pop(); },
                      [this, &fe] { go(fe); });
        menu.onBack = [&fe] { fe.pop(); };
        menu.setInitialFocus(&entry);
        entry.beginEdit();
    }

    void drawAbove(Frontend&, ui::UiFrame& f) override {
        if (!m_error.empty())
            f.text.drawWrapped(f.overlay, ui::style::smallFont(), m_error, origin.x + 40, origin.y + 210, 320,
                               ui::style::kHelpText, ui::Align::Center);
    }

private:
    void go(Frontend& fe) {
        const std::string text(str::trim(m_address));
        if (text.empty()) {
            // "Leave blank to search for available sessions."
            ensureNetGame(fe).startLanScan();
            fe.pop();
            return;
        }
        const auto addr = net::Address::resolve(text, net::kDefaultGamePort);
        if (!addr) {
            m_error = std::format("Cannot find '{}'.", text);
            return;
        }
        fe.pop();
        joinSession(fe, *addr, false, {});
    }

    std::string m_address;
    std::string m_error;
};

// --- Password (pass_dlg) ------------------------------------------------------------------

class PasswordDialog final : public Page {
public:
    // Dialog_Password (menu 25): the password field, Cancel and DONE; Enter
    // in the field is DONE (Dialog_Password::PasswordCallback), which joins
    // with the password (mmInterface::JoinPasswordSession).
    PasswordDialog(Frontend& fe, net::Address address) : m_address(address) {
        dialog = true;
        dialogPicture = "jpg/pass_dlg.jpg";
        menuId = kPasswordDialog;
        origin = centredOrigin(fe, "jpg/pass_dlg.jpg", {400, 330});
        auto& entry =
            menu.add<ui::TextEntry>(fe.layout.widget(kPasswordDialog, 0, {72, 91, 203, 22}, origin), &m_password, 24);
        auto join = [this, &fe] {
            const auto target = m_address;
            const auto password = m_password;
            fe.pop();
            joinSession(fe, target, false, password);
        };
        entry.onCommit = join;
        addCancelDone(fe, *this, kPasswordDialog, 1, {18, 276}, 2, {280, 276}, [&fe] { fe.pop(); }, join);
        menu.onBack = [&fe] { fe.pop(); };
        menu.setInitialFocus(&entry);
        entry.beginEdit();
    }

private:
    net::Address m_address;
    std::string m_password;
};

void joinSession(Frontend& fe, const net::Address& address, bool needsPassword, const std::string& password) {
    if (needsPassword) {
        fe.push(std::make_unique<PasswordDialog>(fe, address));
        return;
    }
    NetGame& net = ensureNetGame(fe);
    net.stopLanScan();
    std::string error;
    if (!net.join(address, password, netCar(fe), &error)) {
        net.startLanScan();
        fe.message(error.empty() ? "Cannot connect." : error);
        return;
    }
    fe.push(std::make_unique<ConnectingDialog>(fe, address, password));
}

void ConnectingDialog::update(Frontend& fe, double) {
    if (!fe.ctx.netGame) {
        fe.pop();
        return;
    }
    NetGame& net = *fe.ctx.netGame;
    switch (net.phase()) {
    case NetGame::Phase::Lobby:
    case NetGame::Phase::Countdown:
    case NetGame::Phase::Racing: fe.replace(makeLobbyPage(fe)); break;
    case NetGame::Phase::Closed:
    case NetGame::Phase::Idle: {
        const auto notice = net.takeNotice();
        const net::Address address = m_address;
        const std::string password = m_password;
        fe.pop(); // `this` is gone from here on
        net.startLanScan();
        if (net.joinFailure() == net::DisconnectReason::BadPassword) {
            // mmInterface::Update: the host wants a password (JoinSession's
            // result 2) opens Dialog_Password; a wrong one shows badpass_dlg
            // first and then asks again.
            if (password.empty())
                fe.push(std::make_unique<PasswordDialog>(fe, address));
            else
                fe.notice("jpg/badp_dlg.jpg", kBadPassDialog,
                          [&fe, address] { fe.push(std::make_unique<PasswordDialog>(fe, address)); });
            break;
        }
        fe.message(notice.value_or("Cannot connect to the session."));
        break;
    }
    case NetGame::Phase::Connecting: break;
    }
}

// --- Chat entry ----------------------------------------------------------------------------

// The lobby's message line: Enter sends, Escape stops typing. Typing starts
// with Enter or a click.
class ChatEntry final : public ui::Widget {
public:
    ChatEntry(Box b, std::function<void(const std::string&)> send) : m_send(std::move(send)) { box = b; }

    void draw(ui::UiFrame& f, bool focused) override {
        const ui::FontSpec font = ui::style::smallFont();
        const float lh = f.text.lineHeight(f.overlay, font);
        const float y = box.y + (box.h - lh) * 0.5f;
        if (m_text.empty() && !m_editing) {
            f.text.draw(f.overlay, font, "Type message here. Press ENTER to send.", box.x + 6, y, // string 398
                        focused ? ui::style::kValueTextFocus : ui::style::kValueTextDisabled);
            return;
        }
        std::string shown = m_text;
        if (m_editing && std::fmod(f.time, 1.0) < 0.5)
            shown += '_';
        // Keep the end of long lines visible.
        while (shown.size() > 1 && f.text.measure(f.overlay, font, shown) > box.w - 12)
            shown.erase(0, 1);
        f.text.draw(f.overlay, font, shown, box.x + 6, y, focused ? ui::style::kValueTextFocus : ui::style::kValueText);
    }

    bool activate(ui::UiFrame&) override {
        m_editing = true;
        return true;
    }
    bool modal() const override { return m_editing; }

    void modalInput(ui::UiFrame& f) override {
        for (char c : f.nav.text)
            if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F && m_text.size() < net::kMaxChatLength)
                m_text.push_back(c);
        if (f.nav.backspace && !m_text.empty()) {
            std::size_t n = m_text.size() - 1;
            while (n > 0 && (static_cast<unsigned char>(m_text[n]) & 0xC0) == 0x80)
                --n;
            m_text.resize(n);
        }
        if (f.nav.enter) {
            if (!str::trim(m_text).empty())
                m_send(m_text);
            m_text.clear(); // stay in typing mode for the next line
        } else if (f.nav.back || (f.nav.mousePressed && !box.contains(f.nav.mouse))) {
            m_editing = false;
        }
    }

private:
    std::function<void(const std::string&)> m_send;
    std::string m_text;
    bool m_editing = false;
};

// --- Lobby (lobbh_bk / lobbj_bk) -------------------------------------------------------------

class LobbyPage final : public Page {
public:
    explicit LobbyPage(Frontend& fe) {
        menuId = menu_id::kNetArena;
        NetGame& net = *fe.ctx.netGame;
        const bool host = net.isHost();
        menu.background = host ? "jpg/lobbh_bk.jpg" : "jpg/lobbj_bk.jpg";
        menu.defaultHelp = host ? "jpg/lobb_srv.jpg" : "jpg/mn_mp.jpg";

        // Left panel buttons (positions inferred: the sprites do not match the
        // background, so they were drawn over the blue panel).
        auto center = [](float w) { return 148.0f - w * 0.5f; };
        float y = 206;
        if (host) {
            auto& settings = menu.add<ui::SpriteButton>(SpriteSheet{"texture/lobb_hst.tga", 4}, center(113), y,
                                                        [&fe] { fe.push(makeHostSettingsPage(fe)); });
            settings.help = "jpg/lobb_set.jpg";
            y += 44;
        }
        m_vehicle = &menu.add<ui::SpriteButton>(SpriteSheet{"texture/lobb_veh.tga", 4}, center(120), y, [&fe] {
            // The garage as the lobby's sub-menu: no GO DRIVE, PREV brings
            // the car back (VehiclePage, Frontend::applyLobbyCar).
            fe.push(makeVehiclePage(fe));
        });
        y += 44;
        if (host) {
            menu.add<ui::SpriteButton>(SpriteSheet{"texture/lobb_ejt.tga", 4}, center(116), y,
                                       [&fe] { fe.push(makeEjectDialog(fe)); });
        }

        // Team choice for team games (YOUR TEAM:).
        m_team0 = &menu.add<ui::LampItem>(SpriteSheet{"texture/lobb_cop.tga", 5}, 474, 207,
                                          [&fe] { return fe.ctx.netGame && fe.ctx.netGame->localCar().team == 0; },
                                          [&fe] { setTeam(fe, 0); });
        m_team1 = &menu.add<ui::LampItem>(SpriteSheet{"texture/lobb_rob.tga", 5}, 474, 238,
                                          [&fe] { return fe.ctx.netGame && fe.ctx.netGame->localCar().team == 1; },
                                          [&fe] { setTeam(fe, 1); });

        menu.add<ChatEntry>(Box{272, 273, 359, 23}, [&fe](const std::string& text) {
            if (fe.ctx.netGame)
                fe.ctx.netGame->sendChat(text);
        });

        auto& back = addBack(fe, *this);
        back.onClick = [&fe] { confirmLeave(fe); };
        menu.onBack = [&fe] { confirmLeave(fe); };
        if (host) {
            m_go = &menu.add<ui::SpriteButton>(SpriteSheet{"texture/lobb_srt.tga", 5}, kNext.x, kNext.y, [&fe] {
                if (fe.ctx.netGame)
                    fe.ctx.netGame->startRace();
            });
            m_go->help = "jpg/lobb_srv.jpg";
            menu.focus(m_go);
        } else {
            m_go = &menu.add<ui::SpriteButton>(SpriteSheet{"texture/lobb_nr.tga", 5}, kNext.x, kNext.y, [&fe] {
                if (fe.ctx.netGame)
                    fe.ctx.netGame->setReady(!fe.ctx.netGame->localReady());
            });
            menu.focus(m_go);
        }
        addNavStrip(fe, *this);
    }

    void update(Frontend& fe, double) override {
        if (!fe.ctx.netGame) {
            fe.pop();
            return;
        }
        NetGame& net = *fe.ctx.netGame;
        if (!net.inSession()) {
            const auto notice = net.takeNotice();
            fe.pop(); // back to the sessions list
            net.startLanScan();
            if (notice)
                fe.message(*notice);
            return;
        }
        const game::RaceConfig cfg = net.raceConfig();
        const bool cr = cfg.mode == GameMode::CopsAndRobbers;
        const bool teams = cr && cfg.copsAndRobbers != game::CopsAndRobbersMode::FreeForAll;
        const bool robberTeams = cfg.copsAndRobbers == game::CopsAndRobbersMode::RobberTeams;
        m_team0->visible = m_team1->visible = teams;
        m_team0->sheet.path = robberTeams ? "texture/lobb_blu.tga" : "texture/lobb_cop.tga";
        m_team1->sheet.path = robberTeams ? "texture/lobb_red.tga" : "texture/lobb_rob.tga";
        // Cops vs. Robbers assigns the cars by team (host_cvr.jpg).
        m_vehicle->enabled = !(cr && cfg.copsAndRobbers == game::CopsAndRobbersMode::CopsVsRobbers);
        // Free-For-All: the team follows the car (the police car is team 0).
        if (cr && !teams) {
            const game::NetCar car = net.localCar();
            const int team = game::freeForAllTeam(fe.ctx.game->catalog.vehicle(car.vehicle));
            if (car.team != team)
                setTeam(fe, team);
        }
        if (net.isHost()) {
            m_go->enabled = net.everyoneReady() && net.phase() == NetGame::Phase::Lobby;
        } else {
            m_go->sheet.path = net.localReady() ? "texture/lobb_rdy.tga" : "texture/lobb_nr.tga";
        }
    }

    void drawAbove(Frontend& fe, ui::UiFrame& f) override {
        if (!fe.ctx.netGame)
            return;
        NetGame& net = *fe.ctx.netGame;
        const auto small = ui::style::smallFont();
        const float lh = f.text.lineHeight(f.overlay, small);
        const game::RaceConfig cfg = net.raceConfig();
        const auto& s = net.settings();

        // HOST SETTINGS panel.
        {
            Vec4 clip{35, 65, 227, 120};
            f.overlay.setClip(&clip);
            float y = 68;
            auto line = [&](const std::string& text, std::uint32_t color = ui::style::kValueText) {
                f.text.draw(f.overlay, small, text, 40, y, color);
                y += lh;
            };
            line(s.name, ui::style::kHelpText);
            line(std::format("Locale: {}", cityName(fe, cfg.city))); // string 87
            line(std::format("Event: {}", raceTitle(fe, cfg)));      // string 88
            if (cfg.mode == GameMode::Circuit)
                line(std::format("Laps: {}", cfg.laps));
            if (cfg.mode == GameMode::CopsAndRobbers) {
                std::string limit = "Limit: None";
                if (cfg.timeLimitMinutes > 0)
                    limit = std::format("Limit: {} minutes", static_cast<int>(cfg.timeLimitMinutes));
                else if (cfg.pointLimit > 0)
                    limit = std::format("Limit: {} points", cfg.pointLimit);
                line(std::format("{}   Gold: {}", limit, kGoldMass[net.goldMass()]));
            }
            line(std::format("Time: {}   Weather: {}", timeOfDayName(cfg.timeOfDay), weatherName(cfg.weather)));
            line(std::format("Traffic {}  Peds {}  Cops {} %", static_cast<int>(cfg.trafficDensity * 100 + 0.5f),
                             static_cast<int>(cfg.pedestrianDensity * 100 + 0.5f),
                             static_cast<int>(cfg.copDensity * 100 + 0.5f)));
            line(std::format("Players: {}/{}{}", net.players().size(), net.maxPlayers(),
                             net.hasPassword() ? "   Password" : ""));
            f.overlay.setClip(nullptr);
        }

        // PLAYERS panel: name, car, ready.
        {
            Vec4 clip{271, 65, 361, 121};
            f.overlay.setClip(&clip);
            float y = 68;
            for (const auto& p : net.players()) {
                const bool me = p.id == net.localId();
                const std::uint32_t color = me ? ui::style::kValueTextFocus : ui::style::kValueText;
                std::string name = p.name;
                if (p.host)
                    name += " (Host)";
                f.text.draw(f.overlay, small, name, 278, y, color);
                // Cops vs. Robbers fixes the cars by team (see NetGame::raceConfig).
                const bool cvr = cfg.mode == GameMode::CopsAndRobbers &&
                                 cfg.copsAndRobbers == game::CopsAndRobbersMode::CopsVsRobbers;
                std::string car = vehicleName(fe, cvr ? (p.team == 0 ? "vpcop" : "vpmustang99") : p.car);
                if (cfg.mode == GameMode::CopsAndRobbers && cfg.copsAndRobbers != game::CopsAndRobbersMode::FreeForAll) {
                    const bool rt = cfg.copsAndRobbers == game::CopsAndRobbersMode::RobberTeams;
                    car = std::string(p.team == 0 ? (rt ? "BLUE" : "COPS") : (rt ? "RED" : "ROBBERS")) + "  " + car;
                }
                f.text.draw(f.overlay, small, car, 420, y, color);
                const std::string state = p.host ? "" : (p.ready ? "Ready" : "...");
                f.text.draw(f.overlay, small, state, 626, y, p.ready ? ui::style::kHelpText : color, ui::Align::Right);
                y += lh;
            }
            f.overlay.setClip(nullptr);
        }

        // YOU panel.
        {
            const auto car = cfg.vehicle;
            float y = 214;
            f.text.draw(f.overlay, small, std::format("Driver: {}", netName(fe)), 278, y, ui::style::kValueText); // 428
            y += lh;
            f.text.draw(f.overlay, small, std::format("Car: {}", vehicleName(fe, car)), 278, y, ui::style::kValueText);
            y += lh;
            f.text.draw(f.overlay, small, std::format("Color: {}", colorName(fe, car, cfg.vehicleColor)), 278, y,
                        ui::style::kValueText);
        }
        if (m_team0->visible)
            ui::drawImage(f.overlay, f.textures.get("jpg/lobb_tem.jpg"), 474, 191);

        // Chat log: newest lines at the bottom.
        {
            Vec4 clip{272, 303, 359, 70};
            f.overlay.setClip(&clip);
            std::vector<std::pair<std::string, std::uint32_t>> lines;
            for (const auto& c : net.chat()) {
                const std::string text = c.system ? c.text : std::format("{}: {}", c.name, c.text);
                const std::uint32_t color = c.system ? ui::style::kHelpText : ui::style::kValueText;
                // Wrap by hand so the newest lines can be bottom-aligned.
                std::string current;
                for (auto word : str::split(text, ' ')) {
                    const std::string candidate = current.empty() ? std::string(word) : current + " " + std::string(word);
                    if (!current.empty() && f.text.measure(f.overlay, small, candidate) > 345) {
                        lines.emplace_back(current, color);
                        current = std::string(word);
                    } else {
                        current = candidate;
                    }
                }
                lines.emplace_back(current, color);
            }
            const int fit = std::max(1, static_cast<int>(66 / lh));
            const std::size_t first = lines.size() > static_cast<std::size_t>(fit) ? lines.size() - fit : 0;
            float y = 305;
            for (std::size_t i = first; i < lines.size(); ++i, y += lh)
                f.text.draw(f.overlay, small, lines[i].first, 278, y, lines[i].second);
            f.overlay.setClip(nullptr);
        }

        // Status under the buttons: port forwarding for the host, waiting
        // message for joiners.
        std::string status;
        if (net.isHost())
            status = net.portMappingStatus();
        else if (net.localReady())
            status = "waiting for host to start..."; // string 432
        if (net.phase() == NetGame::Phase::Countdown)
            status = "Starting...";
        if (!status.empty())
            f.text.drawWrapped(f.overlay, small, status, 42, 336, 214, ui::style::kValueText);
    }

private:
    static void setTeam(Frontend& fe, int team) {
        if (!fe.ctx.netGame)
            return;
        game::NetCar car = fe.ctx.netGame->localCar();
        car.team = team;
        fe.ctx.netGame->setLocalCar(car);
    }

    static void confirmLeave(Frontend& fe) {
        const bool host = fe.ctx.netGame && fe.ctx.netGame->isHost();
        fe.question(host ? "End Session?" : "Quit to Lobby?", [&fe] { // strings 481, 479
            if (fe.ctx.netGame) {
                fe.ctx.netGame->leave();
                fe.ctx.netGame->startLanScan();
            }
            fe.pop();
        });
    }

    ui::SpriteButton* m_vehicle = nullptr;
    ui::SpriteButton* m_go = nullptr;
    ui::LampItem* m_team0 = nullptr;
    ui::LampItem* m_team1 = nullptr;
};

// --- Host settings (host_bk) -----------------------------------------------------------------

class HostSettingsPage final : public Page {
public:
    // MM2 `HostRaceMenu` (menu 11) on `RaceMenuBase::Init` with the
    // multiplayer widgets: creation order and tune/widget.csv positions by
    // index (0 DONE, 1-5 the race types, 6-8 race name and arrows, 9 laps,
    // 10-15 the Cops & Robbers lamps, 16-18 limit value and arrows, 19-21
    // gold mass and arrows, 22-24 locale, 25-27 time, 28-30 weather, 31
    // pedestrian density). The code positions are the fallbacks; those of
    // the laps roller, gold mass box, time box and density slider are not in
    // the table and are inferred from host_bk.
    explicit HostSettingsPage(Frontend& fe) : m_cfg(fe.ctx.netGame->raceConfig()) {
        menuId = menu_id::kHostRace;
        menu.background = "jpg/host_bk.jpg";
        constexpr int id = menu_id::kHostRace;
        const auto& s = fe.ctx.game->strings;
        m_goldMass = fe.ctx.netGame->goldMass();
        if (m_cfg.mode == GameMode::CrashCourse)
            m_cfg.mode = GameMode::Cruise;
        // HostRaceMenu keeps one index for the time limit and one for the
        // points limit (LimitInc/LimitDec, 0..3), both 0 at first.
        for (int i = 0; i < 4; ++i) {
            if (kTimeLimits[i] == static_cast<int>(m_cfg.timeLimitMinutes))
                m_timeIndex = i;
            if (kPointLimits[i] == m_cfg.pointLimit)
                m_pointIndex = i;
        }

        // 0: DONE (host_dn, id 1000), created before the menu's widgets. The
        // settings apply when the menu is left, by DONE or Escape alike
        // (mmInterface::Update, host race menu): there is no CANCEL.
        const Vec2 dn = fe.layout.position(id, 0, kNext);
        auto& done = menu.add<ui::SpriteButton>(SpriteSheet{"texture/host_dn.tga", 4}, dn.x, dn.y,
                                                [this, &fe] { apply(fe); });
        done.sound = "Selectionmade";
        menu.onBack = [this, &fe] { apply(fe); };

        // 1-5: race types.
        const struct {
            GameMode mode;
            const char* sprite;
            float y;
            const char* help;
        } lamps[] = {{GameMode::Cruise, "texture/cruise_m.tga", 51, "jpg/race_rom.jpg"},
                     {GameMode::Blitz, "texture/blitz_m.tga", 78, "jpg/race_btz.jpg"},
                     {GameMode::Checkpoint, "texture/cp_m.tga", 105, "jpg/race_cp.jpg"},
                     {GameMode::Circuit, "texture/circt_m.tga", 132, "jpg/race_cir.jpg"},
                     {GameMode::CopsAndRobbers, "texture/cops_m.tga", 159, "jpg/race_cop.jpg"}};
        for (int i = 0; i < 5; ++i) {
            const GameMode m = lamps[i].mode;
            const Vec2 p = fe.layout.position(id, 1 + i, {kLampX, lamps[i].y});
            auto& item = menu.add<ui::LampItem>(SpriteSheet{lamps[i].sprite, 5}, p.x, p.y,
                                                [this, m] { return m_cfg.mode == m; },
                                                [this, &fe, m] { selectMode(fe, m); });
            item.help = lamps[i].help;
        }

        // 6-8: race name (host_rnm panel) and its clamping arrows.
        m_raceName = &menu.add<ui::ValueBox>(
            fe.layout.widget(id, 6, {404, 64, 205, 24}), [this, &fe] { return raceNames(fe); },
            [this] { return std::max(0, m_cfg.raceIndex); }, [this](int i) { m_cfg.raceIndex = i; });
        m_raceArrows = arrows(fe, 7, {609, 60}, {609, 78}, *m_raceName);

        // 9: LAPS (host_lap panel), a roller like the race menu's.
        m_laps = &menu.add<ui::Roller>(
            fe.layout.widget(id, 9, {418, 98, 60, 32}),
            [&fe] {
                std::vector<std::string> v;
                for (std::uint32_t i = 0; i < 10; ++i)
                    v.push_back(fe.ctx.game->strings.get(590 + i, std::to_string(i + 1)));
                return v;
            },
            [this] { return std::clamp(m_cfg.laps - 1, 0, 9); }, [this](int i) { m_cfg.laps = i + 1; });

        // 10-15: Cops & Robbers type and limit lamps (host_cr panel at 266,44).
        const struct {
            game::CopsAndRobbersMode mode;
            const char* sprite;
            float y;
            const char* help;
        } crModes[] = {{game::CopsAndRobbersMode::FreeForAll, "texture/free_cr.tga", 57, "jpg/host_ffa.jpg"},
                       {game::CopsAndRobbersMode::CopsVsRobbers, "texture/cpsvr_cr.tga", 79, "jpg/host_cvr.jpg"},
                       {game::CopsAndRobbersMode::RobberTeams, "texture/robrs_cr.tga", 101, "jpg/host_rt.jpg"}};
        for (int i = 0; i < 3; ++i) {
            const auto mode = crModes[i].mode;
            const Vec2 p = fe.layout.position(id, 10 + i, {395, crModes[i].y});
            auto& item = menu.add<ui::LampItem>(SpriteSheet{crModes[i].sprite, 5}, p.x, p.y,
                                                [this, mode] { return m_cfg.copsAndRobbers == mode; },
                                                [this, mode] { m_cfg.copsAndRobbers = mode; });
            item.help = crModes[i].help;
            m_crItems.push_back(&item);
        }
        const struct {
            int limit; // 0 none, 1 time, 2 points
            const char* sprite;
            float y;
            const char* help;
        } limits[] = {{0, "texture/none_cr.tga", 137, "jpg/host_n.jpg"},
                      {1, "texture/time_cr.tga", 159, "jpg/host_t.jpg"},
                      {2, "texture/point_cr.tga", 181, "jpg/host_p.jpg"}};
        for (int i = 0; i < 3; ++i) {
            const int limit = limits[i].limit;
            const Vec2 p = fe.layout.position(id, 13 + i, {395, limits[i].y});
            auto& item = menu.add<ui::LampItem>(SpriteSheet{limits[i].sprite, 5}, p.x, p.y,
                                                [this, limit] { return limitKind() == limit; },
                                                [this, limit] { setLimit(limit); });
            item.help = limits[i].help;
            m_crItems.push_back(&item);
        }

        // 16-18: LIMIT VALUE and its clamping arrows (strings 510-513 or 514-517).
        m_limitValue = &menu.add<ui::ValueBox>(
            fe.layout.widget(id, 16, {526, 171, 81, 19}),
            [this, &fe] {
                const auto& st = fe.ctx.game->strings;
                std::vector<std::string> v;
                for (std::uint32_t i = 0; i < 4; ++i) {
                    if (limitKind() == 2)
                        v.push_back(st.get(kPointLimitStrings + i, std::format("{} pts", kPointLimits[i])));
                    else
                        v.push_back(st.get(kTimeLimitStrings + i, std::format("{} minutes", kTimeLimits[i])));
                }
                return v;
            },
            [this] { return limitKind() == 2 ? m_pointIndex : m_timeIndex; },
            [this](int i) { setLimitIndex(i); });
        m_limitArrows = arrows(fe, 17, {610, 163}, {610, 181}, *m_limitValue);

        // 19-21: GOLD MASS (strings 506-508) and its clamping arrows.
        m_goldBox = &menu.add<ui::ValueBox>(
            fe.layout.widget(id, 19, {404, 223, 123, 24}),
            [&fe] {
                std::vector<std::string> v;
                for (std::uint32_t i = 0; i < 3; ++i)
                    v.push_back(fe.ctx.game->strings.get(kGoldMassStrings + i, kGoldMass[i]));
                return v;
            },
            [this] { return m_goldMass; }, [this](int i) { m_goldMass = i; });
        m_goldBox->help = "jpg/host_gm.jpg";
        m_goldArrows = arrows(fe, 20, {530, 220}, {530, 238}, *m_goldBox);

        // 22-24: RACE LOCALE; 25-27: TIME (629-632); 28-30: WEATHER (625-628:
        // the menu offers no snow, RaceMenuBase::IncWeather stops at 3); all
        // with clamping arrows.
        auto& city = menu.add<ui::ValueBox>(
            fe.layout.widget(id, 22, {404, 275, 205, 24}),
            [&fe] {
                std::vector<std::string> v;
                for (const auto& c : fe.cities)
                    v.push_back(c.localizedName);
                return v;
            },
            [this, &fe] { return fe.cityIndex(m_cfg.city); },
            [this, &fe](int i) {
                m_cfg.city = fe.cities[static_cast<std::size_t>(i)].mapName;
                clampRace(fe);
            });
        arrows(fe, 23, {607, 271}, {607, 289}, city);
        auto& time = menu.add<ui::ValueBox>(
            fe.layout.widget(id, 25, {404, 311, 123, 24}),
            [t = std::vector<std::string>{s.get(629, "Morning"), s.get(630, "Noon"), s.get(631, "Evening"),
                                          s.get(632, "Night")}] { return t; },
            [this] { return static_cast<int>(m_cfg.timeOfDay); },
            [this](int i) { m_cfg.timeOfDay = static_cast<game::TimeOfDay>(i); });
        arrows(fe, 26, {528, 308}, {528, 326}, time);
        auto& weather = menu.add<ui::ValueBox>(
            fe.layout.widget(id, 28, {404, 347, 123, 21}),
            [w = std::vector<std::string>{s.get(625, "Clear"), s.get(626, "Cloudy"), s.get(627, "Foggy"),
                                          s.get(628, "Raining")}] { return w; },
            [this] { return std::min(static_cast<int>(m_cfg.weather), 3); },
            [this](int i) { m_cfg.weather = static_cast<game::Weather>(i); });
        arrows(fe, 29, {528, 342}, {528, 360}, weather);

        // 31: PEDESTRIAN DENSITY (multiplayer has no traffic or cop slider).
        menu.add<ui::Slider>(
            fe.layout.widget(id, 31, {472, 381, 139, 31}), [this] { return m_cfg.pedestrianDensity; },
            [this](float v) { m_cfg.pedestrianDensity = v; });

        addNavStrip(fe, *this);
        clampRace(fe);
        menu.focus(&done);
    }

    void update(Frontend& fe, double) override {
        if (!fe.ctx.netGame || !fe.ctx.netGame->inSession()) {
            fe.pop();
            return;
        }
        const bool cr = m_cfg.mode == GameMode::CopsAndRobbers;
        const bool race = m_cfg.mode == GameMode::Blitz || m_cfg.mode == GameMode::Checkpoint ||
                          m_cfg.mode == GameMode::Circuit;
        m_raceName->visible = race;
        m_raceArrows.show(race);
        m_laps->visible = m_cfg.mode == GameMode::Circuit;
        for (auto* item : m_crItems)
            item->visible = cr;
        m_limitValue->visible = cr && limitKind() != 0;
        m_limitArrows.show(m_limitValue->visible);
        m_goldBox->visible = cr;
        m_goldArrows.show(cr);
    }

    // The panels go under the widgets: their value boxes and lamps sit on them.
    void drawBelow(Frontend& fe, ui::UiFrame& f) override {
        // The original swaps the upper right panel per game type.
        const bool race = m_cfg.mode == GameMode::Blitz || m_cfg.mode == GameMode::Checkpoint ||
                          m_cfg.mode == GameMode::Circuit;
        if (m_cfg.mode == GameMode::CopsAndRobbers)
            ui::drawImage(f.overlay, f.textures.get("jpg/host_cr.jpg"), 266, 44);
        if (race)
            ui::drawImage(f.overlay, f.textures.get("jpg/host_rnm.jpg"), 267, 59);
        if (m_cfg.mode == GameMode::Circuit)
            ui::drawImage(f.overlay, f.textures.get("jpg/host_lap.jpg"), 267, 94);
        // Race map in the lower left panel, as on the races screen.
        std::string pic;
        if (race)
            pic = std::format("jpg/{}_map{}{}.jpg", m_cfg.city, game::modeKey(m_cfg.mode), std::max(0, m_cfg.raceIndex));
        else if (m_cfg.mode == GameMode::CopsAndRobbers)
            pic = std::format("jpg/{}_multicop.jpg", m_cfg.city);
        else
            pic = std::format("jpg/{}_maproam.jpg", m_cfg.city);
        if (const ui::UiTexture& t = fe.textures.get(pic))
            ui::drawImage(f.overlay, t, 30, 194, 242, 184);
    }

private:
    // The roller_up / roller_down buttons beside a box (clamping).
    struct Arrows {
        ui::SpriteButton* up = nullptr;
        ui::SpriteButton* down = nullptr;
        void show(bool on) const {
            up->visible = on;
            down->visible = on;
        }
    };
    Arrows arrows(Frontend& fe, int upIndex, Vec2 upCode, Vec2 downCode, ui::ValueBox& box) {
        const Vec2 u = fe.layout.position(menu_id::kHostRace, upIndex, upCode);
        const Vec2 d = fe.layout.position(menu_id::kHostRace, upIndex + 1, downCode);
        Arrows a;
        a.up = &menu.add<ui::SpriteButton>(SpriteSheet{"texture/roller_up.tga", 3}, u.x, u.y,
                                           [&box] { ui::stepOption(box, -1, false); });
        a.down = &menu.add<ui::SpriteButton>(SpriteSheet{"texture/roller_down.tga", 3}, d.x, d.y,
                                             [&box] { ui::stepOption(box, 1, false); });
        return a;
    }

    // DONE or Escape: the settings go to the session (mmInterface::Switch to
    // the lobby sends them) and the menu returns to the lobby.
    void apply(Frontend& fe) {
        if (fe.ctx.netGame) {
            fe.ctx.netGame->setGoldMass(m_goldMass);
            fe.ctx.netGame->setRaceConfig(m_cfg);
        }
        // Remember the host's choices for the next session.
        fe.config.mode = m_cfg.mode;
        fe.config.city = m_cfg.city;
        fe.pop();
    }

    std::vector<std::string> raceNames(Frontend& fe) const {
        std::vector<std::string> names;
        for (const auto* r : fe.racesFor(m_cfg.mode, m_cfg.city))
            names.push_back(r->name);
        if (names.empty())
            names.push_back("-");
        return names;
    }

    void clampRace(Frontend& fe) {
        if (m_cfg.mode == GameMode::Cruise || m_cfg.mode == GameMode::CopsAndRobbers) {
            m_cfg.raceIndex = -1;
            return;
        }
        // Every race is open in multiplayer (inferred: the joiners' progress
        // could not gate the host's choice).
        const int n = static_cast<int>(fe.racesFor(m_cfg.mode, m_cfg.city).size());
        m_cfg.raceIndex = std::clamp(m_cfg.raceIndex, 0, std::max(0, n - 1));
    }

    void selectMode(Frontend& fe, GameMode m) {
        m_cfg.mode = m;
        clampRace(fe);
        if (m == GameMode::Circuit && m_cfg.laps <= 0)
            m_cfg.laps = 3;
    }

    int limitKind() const { return m_cfg.timeLimitMinutes > 0 ? 1 : (m_cfg.pointLimit > 0 ? 2 : 0); }

    // HostRaceMenu::SetLimit / GetLimit: the kind picks which of the two
    // remembered indices applies.
    void setLimit(int kind) {
        m_cfg.timeLimitMinutes = kind == 1 ? static_cast<float>(kTimeLimits[m_timeIndex]) : 0.0f;
        m_cfg.pointLimit = kind == 2 ? kPointLimits[m_pointIndex] : 0;
    }

    void setLimitIndex(int i) {
        i = std::clamp(i, 0, 3);
        if (limitKind() == 2) {
            m_pointIndex = i;
            m_cfg.pointLimit = kPointLimits[i];
        } else {
            m_timeIndex = i;
            m_cfg.timeLimitMinutes = static_cast<float>(kTimeLimits[i]);
        }
    }

    game::RaceConfig m_cfg;
    int m_goldMass = 1;
    int m_timeIndex = 0, m_pointIndex = 0;
    ui::ValueBox* m_raceName = nullptr;
    Arrows m_raceArrows;
    ui::Roller* m_laps = nullptr;
    ui::ValueBox* m_limitValue = nullptr;
    Arrows m_limitArrows;
    ui::ValueBox* m_goldBox = nullptr;
    Arrows m_goldArrows;
    std::vector<ui::Widget*> m_crItems;
};

// --- Eject (ejct_dlg) -------------------------------------------------------------------------

class EjectDialog final : public Page {
public:
    explicit EjectDialog(Frontend& fe) {
        dialog = true;
        dialogPicture = "jpg/ejct_dlg.jpg";
        origin = kDialogOrigin;
        const float ox = origin.x, oy = origin.y;
        menu.add<ui::ListBox>(
            Box{ox + 72, oy + 90, 258, 165}, [&fe] { return names(fe); }, [this] { return m_selected; },
            [this](int i) { m_selected = i; });
        menu.add<ui::SpriteButton>(SpriteSheet{"texture/dlg_ok.tga", 4}, ox + 60, oy + 280, [this, &fe] {
            const auto ids = others(fe);
            if (fe.ctx.netGame && m_selected >= 0 && m_selected < static_cast<int>(ids.size()))
                fe.ctx.netGame->kick(ids[static_cast<std::size_t>(m_selected)]);
            fe.pop();
        });
        menu.add<ui::SpriteButton>(SpriteSheet{"texture/dlg_can.tga", 4}, ox + 240, oy + 280, [&fe] { fe.pop(); });
        menu.onBack = [&fe] { fe.pop(); };
    }

private:
    static std::vector<std::uint8_t> others(Frontend& fe) {
        std::vector<std::uint8_t> ids;
        if (fe.ctx.netGame)
            for (const auto& p : fe.ctx.netGame->players())
                if (p.id != fe.ctx.netGame->localId())
                    ids.push_back(p.id);
        return ids;
    }

    static std::vector<std::string> names(Frontend& fe) {
        std::vector<std::string> v;
        for (auto id : others(fe))
            if (const auto* p = fe.ctx.netGame->player(id))
                v.push_back(p->name);
        return v;
    }

    int m_selected = -1;
};

} // namespace

std::unique_ptr<Page> makeSessionsPage(Frontend& fe) { return std::make_unique<SessionsPage>(fe); }
std::unique_ptr<Page> makeHostOptionsDialog(Frontend& fe) { return std::make_unique<HostOptionsDialog>(fe); }
std::unique_ptr<Page> makeAddressDialog(Frontend& fe) { return std::make_unique<AddressDialog>(fe); }
std::unique_ptr<Page> makeLobbyPage(Frontend& fe) { return std::make_unique<LobbyPage>(fe); }
std::unique_ptr<Page> makeHostSettingsPage(Frontend& fe) { return std::make_unique<HostSettingsPage>(fe); }
std::unique_ptr<Page> makeEjectDialog(Frontend& fe) { return std::make_unique<EjectDialog>(fe); }

bool frontendHostSession(Frontend& fe, const std::string& password) {
    NetGame& net = ensureNetGame(fe);
    game::RaceConfig cfg = fe.config;
    cfg.multiplayer = true;
    if (cfg.mode == GameMode::CrashCourse)
        cfg.mode = GameMode::Cruise;
    if (cfg.laps <= 0)
        cfg.laps = 3;
    net.stopLanScan();
    std::string error;
    game::NetHostOptions opts;
    opts.password = password;
    if (!net.host(cfg, opts, netCar(fe), &error)) {
        log::warn("multiplayer: cannot host: {}", error);
        return false;
    }
    return true;
}

void frontendJoinSession(Frontend& fe, const std::string& address, const std::string& password) {
    const auto addr = net::Address::resolve(address, net::kDefaultGamePort);
    if (!addr) {
        fe.message(std::format("Cannot find '{}'.", address));
        return;
    }
    joinSession(fe, *addr, false, password);
}

} // namespace mm2::app::frontend
