# MM2 -> OpenMM2: frontend

Audited from MM2Recomp (midtown2.exe build 3393) on 2026-10-08.

Summary: 1309 reachable functions in 102 classes; ported 738 (of which newly ported 85), replaced 163, not needed 404, open 4.

Scope: the menus (MenuManager, UIMenu and every UI* widget, the main,
race, garage, options and multiplayer menus with their dialogs and
composite lists), mmInterface, the in-race popup (PUMenuBase and the PU*
menus), the player data (mmPlayerData and its records, mmPlayerConfig,
mmMiscData), and the infrastructure classes handed to the frontend
(mmRewardRecord, mmCCData, dgStatePack, WArray, the `_global` exception
funclets). The garage's camera and car nodes (asViewCS and asDofCS, on the
hud-views list) and the garage menu `Vehicle` (on the vehicle-physics list)
are recorded here as well, because the showroom ported them. OpenMM2's
own widget classes (`src/ui/Widgets.*`) stand in for MM2's; HiDPI scaling
is an allowed deviation. MM2's DirectPlay transport is replaced by
OpenMM2's UDP sessions (`game/net`), so the menus' DirectPlay glue is
"replaced"; the host settings and the lobby's display are ported.
docs/frontend.md and docs/multiplayer.md describe the result for players.

## Ported in this audit

- The in-race OPTIONS pages (PUOptions, PUAudioOptions, PUControl, PUGraphics) with MM2's layout, ranges, callbacks and Cancel (5eff552).
- The key map on F1 (PUKey, 21f0e34), the host's quit menu (PUQuit, 10b5e4e) and the roster on F6 with booting (PURoster, d83bb67).
- The garage's turning 3D car: MM2's camera, light, reflection and render states (VehicleSelectBase, mmVehicleForm, asViewCS, asDofCS; 4dd4114, bfeadf5).
- MM2's scroll bar (VSWidget) on the race records and the customize list, with the record list's keys and drag (e1292b1).
- Network results over the lobby, with the Cops and Robbers team rows (3f1de68).
- The lobby as NetArena lays it out: the button row, race map, city name, host settings text, ready resets, roster icons and chat (33a72e8, cecdcfa, 41cf9c5).
- The password prompt for address joins and MM2's dialog layouts (0ebdad4); booting by picking a name (c7c3f26); the host options' last values (cecdcfa).
- Locked cars kept out of multiplayer and the network event saved as the driver's last (9271663); each driver's last TCP/IP address (3851125).
- The lobby's garage as a sub-menu without GO DRIVE (Vehicle::SetSubMenu, 775f48a).
- The option pages' strip OPTIONS, Customize opening on DEFAULTS, the refused-key OK button, no steering bar on the control page (4c0d84d).
- Widget sounds, exclusive-lamp clicks, drop arrows, pressed arrows, popup key sounds, F4 in the popup and the restarted Switch sound (1ffb905, 357fcb4, ba296e0).
- The race menu's lamp help, and the host's race defaults, checkpoint locks and gold mass (2a46a62).
- Menus' focus and help on entry, after dialogs and in the popups; a click needs the press and the release on the same widget; the sessions screen's blinking search label (c162521, 5c1cd3c).
- The results' Exit, Next Race details, BeginPhase's 10 % loading step and San Francisco as the first city (36faf55).
- The Hall of Fame's "passed" flag (4199e54).

## MenuManager

The menu system's root node (`MenuManager::Instance`). Two instances of the class exist in a game: the frontend one,
`MenuManager::Init(64 menus, read tables, "bgframe")` from `mmInterface::mmInterface` after
`AllocateMenuSwitchAudio`, and the in-race popup one, `MenuManager::Init(camera, 30 menus, no tables, 0.2, 0.1, 0.6,
0.8)` from `mmPopup::mmPopup`. It owns the menu array (`AddMenu2` from every `UIMenu` constructor with an id > 0), the
navigation bar (frontend only), the open dialog, the focused menu (page or nav bar), the widget/menu tables
(`WArray`/`MArray`), the three menu sounds, the pointer (`sfPointer`) and the fonts; `mmInterface::Update` and
`mmPopup::Update` call `CheckInput` once a frame and then poll `CurrentMenuSelected`/`MenuState`/widget ids. OpenMM2
splits it: `app/frontend/Frontend` (page stack, sounds, layout) + `ui::Menu` (focus, keys, mouse) for the frontend,
`RaceScreen` (popup section) + `app/frontend/PopupOptions.*` for the popup; MM2's state polling is replaced by
callbacks (`onClick`, `onBack`).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `MenuManager::MenuManager` | ported | `app/frontend/PagesCrash.cpp` `lessonStringId`; `ui::Menu` | Builds the "\|"-joined lesson name lists (London strings 532-544, San Francisco 545-557) that `LoadRaceNames` hands the crash course box; OpenMM2 reads the same strings per lesson. Default scale 0.33 is overwritten by the popup `Init`; the int read from string 531 ("0") is never read again (no reader found, inferred). |
| `MenuManager::~MenuManager`, ``MenuManager::`scalar_deleting_destructor'``, `MenuManager::Kill` | not needed | — | destructors; `Kill` deletes the event queue, view, sounds and camera. |
| `MenuManager::Init` (int, int, char*: frontend) | ported | `app/frontend/FrontendScreen.cpp` `Frontend::Frontend`, `addNavStrip`; `ui/Widgets.cpp` `style::*Font`; `app/frontend/Showroom.cpp` | Fonts strings 558-565 into the GetFont slots (12, 14, 16, 20, 24, 32, 48, 64); OpenMM2 defines the ones its pages use (559, 560; the frontend's constant `GetFont` arguments are all 16, the widgets' register arguments were checked page by page in the first audit). Nav bar (`uiNavBar(0)`) = `addNavStrip`. Default background "bgframe" (see `SetBackgroundImage`). The 3D camera (view 0.6 rad, aspect 3.2, 1-100 m, viewport 0.05, 0.115, 0.95 x 0.4, distance 10, black clear, sun from (-1,-1,-1)) is the garage showroom's, ported in `Showroom.*` (4dd4114, which cites `MenuManager::Init`; not re-audited here). |
| `MenuManager::Init` (asCamera*, ...: in-race popup) | ported | `app/frontend/PopupOptions.h/.cpp` `popup::kCard`, `cardColor`, `drawCard`, `PopupSounds`; `ui/Widgets.cpp` `style::popup*Font` | Popup mode flag, card (0.2, 0.1, 0.6 x 0.8) from `mmPopup`, `Card2D` (16, 31, 93) alpha 0x80 hidden until `EnablePU`, no background, no tables, popup fonts 566-573 (OpenMM2 uses 568-571). |
| `MenuManager::InitCommonStuff` | ported | `ui/MenuLayout.cpp` `MenuLayout::load`; `ui/Widgets.h` `style::kPopupLineHeight`; `PopupSounds`, `Frontend::playSound` | Event queue (32), menu array, `WArray`/`MArray` (tables read only when asked: frontend yes, popup no), label font 16 (14 below 300 px), line height = height of "Test" in GetFont 16 over 480 (16 px), sounds Moveselector/Selectionmade/Switch, the pointer, `InitGlobalStrings`. |
| `MenuManager::InitGlobalStrings` | ported | `PagesOptions.cpp`, `PopupOptions.cpp`, `PagesRace.cpp`, `PagesMulti.cpp`, `PagesMain.cpp` (`s.get(574...)` etc.) | "\|" lists: 574-576 and +577 (detail), 660/661/576 (cloud shadows), 578/579 (transmission), 580-584 (controllers), 585-589 (modes), 590-597 and +598/599 (numbers 1-8, 1-10), 600-603 (collision). OpenMM2 reads the same ids where the options are built. |
| `MenuManager::LoadRaceNames` | ported | `FrontendScreen.cpp` `Frontend::racesFor`; `PagesCrash.cpp` `lessonStringId` | Current city's checkpoint, circuit and blitz names, and the lesson list for "sf"/"london" (empty for other cities); called again on city change (`RaceMenuBase::AnotherCityChangeCB`, `mmInterface::Update`). OpenMM2 looks them up live. |
| `MenuManager::AddPointer` | not needed | — | empty body. |
| `MenuManager::GetScale` | ported | `PopupOptions.cpp` `popup::at` | popup: the card; frontend: (0, 0, 1, 1); feeds `UIMenu::UIMenu` → `ScaleWidget`. |
| `MenuManager::MouseAction` | ported | `ui/Widgets.cpp` `Menu::update` (hovered widget) | open dialog only, else current page then nav bar; first widget hit wins (see `UIMenu::MouseHitCheck`, findings A1.4-A1.5). |
| `MenuManager::ClearAllWidgets` | ported | `Menu::update` (`m_highlight = false`) | pointer moved over nothing: highlights (and help picture) off. |
| `MenuManager::GetControllerName` | ported | `PagesMain.cpp` `controllerName` | "\|"-list 580-584 by index, "undefined" past the end; OpenMM2 clamps 0..4. |
| `MenuManager::GetFont` (with its table 0x4e4d3d) | ported | `ui/Widgets.cpp` `style::valueFont`, `smallFont`, `popupFont`, `popupButtonFont`, `popupSmallFont`, `popupTitleFont` | sizes 12/14/20/24/32/48/64 to their slots, anything else the 16 slot (strings 560 / 568). |
| `MenuManager::GetFGColor` (with 0x4e4f10) | ported | `ui/Widgets.h` `style::kValueText`, `kValueTextFocus`, `kRecordText`, `kPopupText`, `kPopupFocus`, `kPopupDisabled` | frontend: 0 and 4 yellow, 1 grey 0.1, 2 white, 3 and anything else red; popup: 0/1 white, 5 grey 0.35, else (0.933, 1, 0.129). Verified again from the asm. |
| `MenuManager::CheckBG`, `MenuManager::SetBackgroundImage`, `MenuManager::SetDefaultBackgroundImage` | ported | `Frontend::drawPage`, `ui::Menu::draw` (`Menu::background`) | the entered menu's background as the camera underlay; a menu without one gets the default; when nothing loads (and not in popup mode) "bgframe". OpenMM2 draws nothing for a missing picture (every page sets one; only visible with broken data). |
| `MenuManager::EnablePU` | ported | `app/RaceScreen.cpp` `openPopup`, `buildPopup`; `Menu::popupSounds` | enable the current popup menu, clear all highlights, size the card, show it, mark the popup open. OpenMM2 too: `Menu::unlight`, `Menu::park` (finding A1.3). |
| `MenuManager::AdjustPopupCard` | ported | `PopupOptions.cpp` `popup::cardFor`, `drawCard`; `RaceScreen.cpp` `drawPopup` | card = the menu's dimensions (`UIMenu::GetDimensions`). |
| `MenuManager::DisablePU` | ported | `RaceScreen.cpp` `closePopup` | hide card, popup closed, disable the menu. |
| `MenuManager::TogglePU` | not needed | — | unreachable (no callers). |
| `MenuManager::OpenDialog` | ported | `Frontend::push` (dialog pages), `Frontend::drawPage` / `Menu::drawContent(active = false)` | closes an open dialog first, remembers the focused menu, enables the dialog and clears the page's action, tooltips and highlights. OpenMM2's flows close a dialog before opening the next (e.g. `NewDriverDialog::create`). |
| `MenuManager::CloseDialog` | ported | `Frontend::pop` | disables the dialog and gives the focus back to the menu that had it, without re-highlighting anything. OpenMM2 too (`Menu::unlight`; finding A1.2). |
| `MenuManager::Enable`, `MenuManager::Disable` | ported | `Frontend::push`/`pop`/`replace`, `Menu::resetFocus`, `Page::onEnter` | `Enable` sets the current menu id; in popup mode only while the popup is open (then also `AdjustPopupCard`); `CheckBG`. |
| `MenuManager::EnableNavBar`, `MenuManager::DisableNavBar` | ported | `addNavStrip` (pages with/without the strip) | show the nav bar (and PREV via `TurnOnPrev`) / hide it. |
| `MenuManager::PlaySound` | ported | `ui::Menu::update` (Moveselector), `TextButton::activate`, `TextToggle::activate`, `Roller::adjust`, `Slider::adjust`/`mouse` (Switch); `PopupSounds::play` | 0 Moveselector and 1 Selectionmade only while the popup is open (stop, play once at 0.75); 2 Switch always (stop, rewind, play once at 0.85). OpenMM2 plays them the same way since findings A1.6 and A1.7 (`Frontend::playSound` restarts Switch). |
| `MenuManager::DeclareLastDrawn` | ported | `ui::Widget::drawPopup` (drawn after all widgets) | the node updated (drawn) after the tree: open drop-down lists, a button's tooltip text. |
| `MenuManager::ResChange` | replaced | renderer / UI layout (`render::computeUiLayout`) | forwards to the pointer (reload `midcursor`, clamp limits) and the nodes. |
| `MenuManager::Update` | ported | `Frontend::update`/`draw`, `RaceScreen::updatePopup`/`drawPopup` | node tree, then the "last drawn" node, then the pointer (`sfPointer::Update`, mouse events). |
| `MenuManager::Flush` | replaced | per-frame `ui::NavReader` input; `RaceScreen` opens the popup after updating input | drops queued key events (mmPopup on every open/switch so the opening key does not act). OpenMM2 does not run a new popup's menu in the frame that opened it. |
| `MenuManager::CheckInput` | ported | `ui::Menu::update` via `Frontend::update`, `RaceScreen::updatePopup` | frontend always, popup only while open: dialog's `CheckInput` and mouse hits, else the focused menu's `CheckInput`, then mouse hits of the page and the nav bar. |
| `MenuManager::ToggleFocus` | ported | `ui::Menu::step` | page ↔ nav bar (page only when the nav bar is missing or hidden); both directions land on the other group's first focusable widget (`SetBstate` then `FindTheFirstFocusWidget`). |
| `MenuManager::RegisterWidgetFocus` | ported | `ui::Widget::modal`/`modalInput` (`ValueBox`, `TextEntry`, `BindingList`) | a widget captures all input in a hit rectangle; global keys off, mouse only to it; a press outside ends the capture and clears every highlight (`sfPointer::Update`). OpenMM2 closes the box and keeps it highlighted (minor, part of finding A1.5). |
| `MenuManager::ScanGlobalKeys` (with its table 0x4e5755) | ported | `ui::Menu::update`, `ui::NavReader::read` | keys (DirectInput codes): Escape (Selectionmade in popup; from the nav bar first back to the page; `SetAction`, then the focused menu's `BackUp`), Tab and Down (`Increment`, wrap in a dialog, else `ToggleFocus(0)`; Moveselector), Up (`Decrement`, `ToggleFocus(-1)`; Moveselector), Enter (Selectionmade in popup; `SetAction(0)`), F4 (popup only: menu state 6). Nothing while a widget captures. F4 missing (finding A1.8, Missing features); popup sounds (finding A1.6). OpenMM2 adds keypad 2/4/6/8 and keypad Enter (MM2's codes are the extended arrows 0xc8/0xd0 and main Enter 0x1c only) and gamepads. |
| `MenuManager::SwitchFocus`, `MenuManager::NotifyMouseSelect`, `MenuManager::SetFocus` | ported | `ui::Menu::setFocus`, `focus`, `Menu::update` (mouse) | focus moves between the page and the strip groups; the old group's highlight goes off, the new group's current widget lights. |
| `MenuManager::GetCurrentMenu`, `MenuManager::FindMenu`, `MenuManager::CurrentMenuSelected`, `MenuManager::MenuState`, `MenuManager::AddMenu2` | replaced | `Frontend` page stack (`top`, `push`), widget callbacks | menu registry and the state polling `mmInterface::Update` does (state 1 back, 4 action with widget id); OpenMM2 calls back directly. |
| `MenuManager::Switch` | ported | `Frontend::push`/`replace`/`pop`, `Frontend::topChanged` | disable old, enable new, switch sound unless the popup is open, clear the old focused menu's action/highlights and focus the new menu. |
| `MenuManager::SetPreviousMenu`, `MenuManager::GetPreviousMenu` | ported | `Frontend` page stack, `addBack` (`fe.pop()`) | the menu PREV / Escape returns to. |
| `MenuManager::PlayMenuSwitchSound`, `MenuManager::AllocateMenuSwitchAudio` | ported | `Frontend::topChanged` (`kSounds`) | menu 1 Selectionmade 0.87, 2-5 UIoptions 0.9, 7 and 40 UIraces 0.9, 8 and 11 UIvehicles 0.9, 10 and 12 UImulti 0.87, not while that sound plays. Menu 6 → UIdriver 0.93 is missing in OpenMM2 but no menu 6 ("Driver Select Menu") is ever created in build 3393 (`mmInterface::mmInterface` builds 1-5, 7-12, 34, 39-41). |
| `MenuManager::PlayRecordsSound` | ported | `PagesMain.cpp` `DriverPage` (RACE RECORDS: `fe.playSound("UIrecords", 0.84f)`) | slot 6 at 0.84. |
| `MenuManager::PlayReplaySound` | not needed | — | only for the REPLAY button (`mmInterface::Update` id 0x6c), never enabled. |
| `MenuManager::Help` | replaced | `FrontendScreen.cpp` `addNavStrip` (HELP shows a message) | kills a running help process, starts the WinHelp watcher thread, minimises the window. Deviation (documented). |
| `MenuManager::AddBrackets`, `MenuManager::DeleteMenu`, `MenuManager::ActionID`, `MenuManager::ForceCurrentFocus` | not needed | — | unreachable (no callers). |

## UIMenu

The base of every menu, dialog (`PUMenuBase`) and the nav bar: a node holding the widgets in creation order, the
keyboard position ("bstate", the focused widget index), the menu state `mmInterface`/`mmPopup` poll (1 back, 2 idle, 3
off, 4 action, 6 popup F4), the last acted widget id, the initial widget (`SetFocusWidget`), the background name and
the description label (+0x40, the "desc icons" picture). Its `Add*` functions create a widget, scale the code
rectangle into the menu (`ScaleWidget`), let tune/widget.csv override it (`WArray::RetrieveWidgetData` with the
menu id and the creation index, then the menu origin added), and `AddWidget` it. OpenMM2: `ui::Menu` (widgets, focus,
input), `app/frontend` `Page` (background, dialog origin, help pictures), `ui::MenuLayout::widget`/`position`
(positions), the `ui::` widget classes for the widgets.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `UIMenu::UIMenu` | ported | `ui::Menu::Menu`, `app/frontend` `Page` (`menuId`) | id, state 3 (off), no previous menu, `AddMenu2` for ids > 0, the scale from `MenuManager::GetScale`. |
| `UIMenu::~UIMenu`, ``UIMenu::`scalar_deleting_destructor'`` | not needed | — | destructors. |
| `UIMenu::AssignName` | ported | `PopupOptions.cpp` `popup::drawTitle` (page titles), `RaceScreen.cpp` `drawPopup` | the name `PUMenuBase::CreateTitle` prints; nav bar name string 604. |
| `UIMenu::AssignBackground` | ported | `ui::Menu::background` (each page's `menu.background`) | set in `mmInterface::mmInterface` per menu. |
| `UIMenu::Enable` | ported | `ui::Menu::resetFocus` (from `Frontend::push`/`pop`), `Page::onEnter`, `RaceScreen::buildPopup` | active, state 2, keyboard position = initial widget, which is updated and highlighted, but only outside popup mode; then the menu's `PreSetup`. The help picture (finding A1.1) and the popup's initial highlight (finding A1.3) follow it. |
| `UIMenu::Disable` | ported | `Frontend::pop`/`replace` | inactive, state 3, hidden, then `PostSetup`. |
| `UIMenu::PreSetup` | ported | `Page::onEnter`, page constructors | base: turns the description label off. Every menu's own `PreSetup` (`MainMenu`, `OptionsMenu`, `AudioOptions`, `GraphicsOptions`, `ControlSetup`, `VehicleSelectBase`, `CrashCourse`; `RaceMenuBase` via `FocusDescription(0, 0)`) does the same after `Enable` lit the initial widget, so a menu opens without a help picture. OpenMM2 too for those five menus (`Page::helpOffOnEntry`, `Menu::hideHelpUntilFocusMoves`; finding A1.1). |
| `UIMenu::PostSetup` | not needed | — | empty in the base class (overrides belong to their menus). |
| `UIMenu::Update` | ported | `ui::Menu::drawContent` | updates (draws) the visible child widgets. |
| `UIMenu::SetAction`, `UIMenu::ClearAction`, `UIMenu::BackUp`, `UIMenu::GetWidgetID`, `UIMenu::SetBstate` | replaced | widget callbacks (`SpriteButton::onClick`, `TextButton::onClick`), `ui::Menu::onBack`, `ui::Menu::setFocus` | the action/back state and the acted widget id that `mmInterface::Update` / `mmPopup::Update` poll; OpenMM2 calls the page's code directly. `SetBstate` clamps the keyboard position to the widgets. |
| `UIMenu::ClearToolTip` | not needed | — | tooltips are never created in build 3393 (`uiWidget::AddToolTip` is empty). |
| `UIMenu::SetFocusWidget` | ported | `ui::Menu::setInitialFocus` | the initial widget by widget id (a negative id: the last index). |
| `UIMenu::GetDimensions` | ported | `PopupOptions.cpp` `popup::cardFor` | the menu rectangle `AdjustPopupCard` gives the card. |
| `UIMenu::DisableIME` | replaced | `platform::Input` text input (SDL), started by `FrontendScreen` | detaches the IME on keyboard navigation and mouse actions. |
| `UIMenu::FindTheFirstFocusWidget`, `UIMenu::Increment`, `UIMenu::Decrement` | ported | `ui::Menu::firstFocusable`, `ui::Menu::step` | focus stops are enabled and not read-only; past the end (start) a dialog wraps to widget 0 (the last), a popup menu wraps, a frontend page hands over to `ToggleFocus`. MM2 lights widget 0 / the last widget on a wrap without checking that it can take the focus; OpenMM2 picks the first/last focusable one (no retail menu has a disabled first or last widget there; inferred). |
| `UIMenu::ScanInput`, `UIMenu::CheckInput`, `UIMenu::KeyboardAction` | ported | `ui::Menu::update` (`adjust`, `activate`, `activateSpace`, `modalInput`) | key-down events of an active menu go to `ScanGlobalKeys`, then to the capturing widget's `CaptureAction` or the focused widget's `Action`; Enter also records the acted widget id. |
| `UIMenu::ScaleWidget` | ported | `PopupOptions.cpp` `popup::at` | code rectangle × menu size + origin. |
| `UIMenu::AddBMButton` | ported | `ui::SpriteButton`, `ui::LampItem` with `MenuLayout::position` (all pages, `addNavStrip`) | hit area = one frame (`UIBMButton::GetHitArea`). |
| `UIMenu::AddBMLabel` | ported | `ui::Widget::help` + `ui::Menu::helpPos` | the "desc icons" picture label at (40, 396) that `FocusDescription` callbacks switch. |
| `UIMenu::AddButton` | ported | `PopupOptions.cpp` `popup::addButton` → `ui::TextButton`; `PagesResults.cpp` `ResultsPage`; `RaceScreen.cpp` `buildPopup` | the popup menus' text buttons (`PUMain`, `PUExit`, `PUOptions`, `PUQuit`, `PUResults`, `PUMenuBase::AddExit`/`AddOKCancel`/`AddPrevious`); `Dialog_Eject`'s button is `EjectDialog`'s `ui::ListBox` (group D). |
| `UIMenu::AddCWArray` | ported | `PagesOptions.cpp` `BindingList` | the customize-controls list (keyboard only, see first audit). |
| `UIMenu::AddCompScroll` | ported | `PagesMain.cpp` record dialogs, `PagesMulti.cpp` `LobbyPage::drawAbove` | composite lists (records, lobby roster); `Dialog_City2`'s and `Dialog_Replay`'s are never opened. |
| `UIMenu::AddHotSpot` | ported | `addBack` (PREV at nav-bar widget 4, (290, 415)) | hidden read-only spots that only carry a position: the PREV spots of `OptionsMenu` and `NetSelectMenu` (turned off; `SetPrevPos` moves PREV there, and tune/widget.csv puts both at (290, 415), the nav bar's own place), `CrashCourseIntro`'s (turned off and never used; its widget.csv row is malformed, the widget name in the index column, so it reads as a second row for widget 0 and loses to cci_lon in MM2 and OpenMM2 alike), and `AboutMenu`'s "Credits" spot (read-only but enabled, so it takes the mouse focus: finding A1.4). |
| `UIMenu::AddIcon` | ported | `ui::Picture` (race map, lobby map) | |
| `UIMenu::AddLabel` | ported | `popup::drawTitle`, `RaceScreen::drawPopup` (PUExit question), `PagesOptions.cpp` `AboutPage` (product id) | `OptionsBase::CreateTitle` is unreachable. |
| `UIMenu::AddSlider` | ported | `ui::Slider` | |
| `UIMenu::AddTextDropdown` | ported | `ui::ValueBox` | the hit rectangle becomes the drop-down's box. |
| `UIMenu::AddTextField` | ported | `ui::TextEntry` | |
| `UIMenu::AddTextRoller2` | ported | `ui::Roller` | |
| `UIMenu::AddTextScroll` | ported | `ui::ListBox` (`PagesMulti.cpp` sessions list) | `NetSelectMenu`'s session list. |
| `UIMenu::AddToggle2` | ported | `ui::TextToggle` (`PopupOptions.cpp` graphics page) | |
| `UIMenu::AddWidget` | ported | `ui::Menu::add` | appends, hit rectangle (x, y, x + w, y + h), id (or the index), tooltip (inert). |
| `UIMenu::MouseAction`, `UIMenu::MouseHitCheck`, `UIMenu::CheckMouseHits` | ported | `ui::Menu::update` (hovered widget, `Widget::mouse`) | hit test of enabled widgets only (read-only ones too), first in creation order; a pending hit moves the keyboard position there; a move or press focuses and lights it, a press also runs the widget's action, a release runs the action and sets the menu's action state. findings A1.4 and A1.5. |
| `UIMenu::SetSelected`, `UIMenu::ClearSelected`, `UIMenu::ClearWidgets` | ported | `ui::Menu::m_highlight`, `Menu::drawContent(active)` | light / unlight the focused widget, unlight all. |
| `UIMenu::IsAnOptionMenu` | ported | `addNavStrip` `NavOptions::Cancel` | 0 here; `OptionsBase` answers 1 so the strip's OPTIONS cancels an option page. |
| `UIMenu::AddIconW`, `UIMenu::AddMex`, `UIMenu::AddTextRoller`, `UIMenu::AddToggle`, `UIMenu::AddUIControl`, `UIMenu::AddVScrollBar`, `UIMenu::ForceWidgetAction`, `UIMenu::GetBstate` | not needed | — | unreachable (no callers). |

## uiWidget

The base of every menu widget: hit rectangle, pending mouse event, selected (lit) flag, widget id, read-only flag,
enabled flag, owning menu, tooltip. Its virtuals are no-ops or flag setters that the widget classes override.
OpenMM2: `ui::Widget` (`box`, `visible`, `enabled`, `readOnly`, virtual `draw`/`activate`/`adjust`/`mouse`/
`modalInput`).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `uiWidget::uiWidget` | ported | `ui::Widget` defaults | enabled, writable, not lit, no tooltip. |
| `uiWidget::Enable`, `uiWidget::Disable`, `uiWidget::TurnOn`, `uiWidget::TurnOff`, `uiWidget::SetReadOnly` | ported | `ui::Widget::enabled`, `visible`, `readOnly`, `focusable` | `TurnOff` = disabled and hidden, `Disable` = disabled (still drawn), read-only = skipped by the keyboard. MM2's mouse still hits read-only widgets (finding A1.4). |
| `uiWidget::Action`, `uiWidget::CaptureAction`, `uiWidget::EvalMouseX`, `uiWidget::ReturnDescription`, `uiWidget::SetPosition`, `uiWidget::GetScreenHeight` | ported | `ui::Widget` virtual defaults (`activate`, `adjust`, `mouse`, `modalInput` return false / do nothing) | empty base virtuals. |
| `uiWidget::Switch` | ported | `ui::Menu::drawContent` (`focused` argument of `Widget::draw`) | base: forwards to the tooltip only when the widget's last mouse event was a move; the lit state is each widget's own `Switch`. |
| `uiWidget::AddToolTip`, `uiWidget::ResetToolTip`, `uiWidget::SetToolTipText` | not needed | — | `AddToolTip` is empty in build 3393, so no widget has a tooltip and the other two never reach `mmToolTip` (`SetToolTipText` is called only by `Dialog_RaceEnvironment`, never opened). |
| `uiWidget::~uiWidget`, ``uiWidget::`scalar_deleting_destructor'`` | not needed | — | destructors. |

## uiNavBar

The top-right navigation strip, menu 0 "Navigation Bar" (string 604), created by the frontend `MenuManager::Init`:
OPTIONS `mnav_opt` (id 100, 5 frames), HELP `mnav_hlp` (102), MINIMISE `mnav_sto` (101), EXIT `mnav_ext` (103), PREV
`mnav_prv` (104, 4 frames) in that creation order (tune/widget.csv menu 0 rows 0-4); `mmInterface::Update`/
`UpdateLobby` act on its ids. OpenMM2: `addNavStrip` and `addBack` in `app/frontend/FrontendScreen.cpp` (group 1 of
each page's `ui::Menu`).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `uiNavBar::uiNavBar` | ported | `addNavStrip`, `addBack` | order, sprites, frame counts and widget.csv indices match; MM2's code x for EXIT (0.65 + HELP and MINIMISE widths) is overridden by the table. |
| `uiNavBar::Help` | replaced | `addNavStrip` (HELP shows a message) | calls `MenuManager::Help` (WinHelp); deviation. |
| `uiNavBar::Minimize` (with its tail 0x4e647c) | ported | `addNavStrip` (MINIMISE: `SDL_MinimizeWindow`) | logs "NAVBAR line 68", then posts a system minimise to the main window. |
| `uiNavBar::OptionActive`, `uiNavBar::OptionInActive` | ported | `NavOptions::Lit` (`SpriteButton::lit`) | OPTIONS lit on the options menu (`mmInterface::Switch`). |
| `uiNavBar::Update` | ported | `SpriteButton::draw` (lit frames 2/3), `addBack` per page | keeps OPTIONS on its lit frames while active; shows PREV whenever the current menu has a previous menu and PREV was not turned off, else hides it. |
| `uiNavBar::SetPrevPos`, `uiNavBar::TurnOnPrev`, `uiNavBar::TurnOffPrev` | ported | `addBack` (position from nav-bar widget 4), pages with / without `addBack` | `OptionsMenu`/`NetSelectMenu` move PREV to their hotspot on entry and back on exit; both hotspots sit at the nav bar's (290, 415). |
| `uiNavBar::BackUp` | not needed | — | empty; Escape on the strip first hands the focus to the page (`ScanGlobalKeys`), so the page backs up. |
| `uiNavBar::ResetState`, `uiNavBar::SetPrevBitmap` | not needed | — | unreachable (no callers). |
| `uiNavBar::~uiNavBar`, ``uiNavBar::`scalar_deleting_destructor'`` | not needed | — | destructors. |

## WArray

tune/widget.csv: `MenuManager::InitCommonStuff` reads it once (frontend only, `Init(300)`: room for 300 rows; the
retail table has 294) and every `UIMenu::Add*` looks its widget up by (menu id, creation index). OpenMM2:
`ui/MenuLayout` (verified by the first audit; re-checked here).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `WArray::Read`, `WArray::AddWidgetData` | ported | `ui/MenuLayout.cpp` `MenuLayout::parseWidgets`, `load` | header skipped, 127-character lines, `strtok` fields, `atoi` numbers, divided by the screen size (the frontend is always 640x480). A missing file is an error message in MM2 (`Errorf`), a warning in OpenMM2. |
| `WArray::RetrieveWidgetData` | ported | `MenuLayout::find`, `widget`, `position` | first matching row; each component replaced when both the code value and the table value are non-zero. MM2 skips the x replacement when every pair already agrees within 0.0021, which only happens when x would not change anyway (code values are fractions, table values pixels). |
| `WArray::WArray`, `WArray::Init`, `WArray::~WArray` | not needed | — | storage. |
| `WArray::Write`, `WArray::DumpMenu`, `WArray::Flush` | not needed | — | unreachable (no callers): debug dump of the menus' widget rectangles. |

## Card2D

A flat 2D rectangle node in screen fractions (`SetDimensions` clamps x, y to 0..1 and w, h to the rest of the
screen), `Init` sets two of its colour bytes to 0x3f and the alpha to 255 × the given value clamped to 0..1 (the
users then write their own colour); `Cull` draws it untextured
over the whole viewport in screen pixels. Users: the popup card (`MenuManager::Init`, `AdjustPopupCard`),
`UITextField` (the black card behind an edited field), `UIButton` (`Switch` changes its alpha), `UITextScroll`,
`UICompositeScroll`, `UIControlWidget`, `UICWArray`, the garage (`VehicleSelectBase::InitCarSelection`). OpenMM2:
`render::Overlay2D::rect` calls in the users.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `Card2D::Card2D`, `Card2D::Init`, `Card2D::SetDimensions`, `Card2D::SetPosition`, `Card2D::SetAlpha` | ported | `PopupOptions.cpp` `popup::cardColor`, `cardFor`; `ui::TextEntry::draw` (black card) | popup card (16, 31, 93) alpha 0x80 (`Init` gives 0.5 → 127, then `MenuManager::Init` writes 0x80). |
| `Card2D::Update`, `Card2D::Cull` | ported | `popup::drawCard`, `TextEntry::draw` (`Overlay2D::rect`) | MM2 enables blending whenever the alpha byte is not exactly 1 (so also for opaque 255, which looks the same). Drawn in 640x480 virtual space scaled by the UI layout instead of screen pixels (any-resolution design). |
| `Card2D::~Card2D`, ``Card2D::`scalar_deleting_destructor'`` | not needed | — | destructors. |

## mmTextNode

All MM2 text: a node owning a GDI bitmap the size of its box (`Init` in screen fractions: width rounded down to an
even pixel count) and up to N lines (pixel offset, effects, font, 255 characters). A change marks it dirty; `Cull`
re-renders with GDI (`RenderText`) and copies the bitmap to the screen at the node position rounded to whole pixels,
black transparent, so text is cut at the node's box. `RenderText` per line: transparent background, white pen and no
brush; effects 0 = plain top-left `DrawText`; otherwise bit 0 DT_VCENTER + DT_SINGLELINE, bit 1 DT_CENTER, bit 2 a
white-pen rectangle from the line's offset to the bitmap's edges, bit 3 a drop shadow in near-black (0x0f0f0f, so it
is not keyed out) offset right and down by text height / 9 (halved with bit 5, at least 1 px), bit 4 left + 5 px,
bit 5 DT_WORDBREAK (only without bit 0), bit 6 draw the rectangle even for an empty string, bit 7 the highlight colour
instead of the foreground. (The DT flags of one node accumulate over its lines and are never reset; inferred from the
call sites below, no reachable node has a later line with fewer flags, so this never shows.)
OpenMM2 replaces the GDI path with `ui::TextRenderer` (stb_truetype atlases, `ui/Text.cpp`, `ui/Font.cpp`) and
reproduces the effects in the widgets that use them.

Effects used, from every `AddText`/`SetEffects`/`UIButton::SetType` call: `UIButton` types 0/1/2 → 1/7/3
(`ui::TextButton::type`), `UILabel` 1 or 3 (`popup::drawTitle`, the PUExit question), `UITextField` 0 / popup 0x45
(`TextEntry::draw` popup outline), `UITextDropdown` 1 and `TextDropWidget` 0x11 / popup 0x15 (`ValueBox::draw`: value
5 px in, popup outline), `mmDropDown` cells 0x40 / lit 0x04 and `UITextScroll` lit line 0x04 (`ValueBox::drawPopup`,
`ListBox::draw` outlines), `UITextRoller2` 1/3 (`Roller`), `UISlider` label 1 (`Slider::label`), `UIToggleButton2` 3
(`TextToggle`), `PUResults` 0 and 0x20 (results page, wrapped message), `mmHUD` messages 0x2a (`game/session/Hud.cpp`:
centred, wrapped, shadow cell / 9 / 2 in 0x0f0f0f). Bit 3 (shadow) is used by no menu or popup widget, only by the
HUD messages, so OpenMM2's shadow-less menu text matches. Bit 7 is used only by `mmCompCity`/`mmCompReplay` (City
Selection and replay dialogs, never opened in build 3393).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmTextNode::RenderText` | replaced | `ui::TextRenderer::draw`, `drawWrapped`; effects in `TextButton::draw`, `TextToggle::draw`, `TextEntry::draw`, `ValueBox::draw`/`drawPopup`, `ListBox::draw`, `TextBox::draw`, `Hud.cpp` messages | GDI rendering → stb_truetype; every effect a retail node uses is reproduced (above). |
| `mmTextNode::Cull`, `mmTextNode::Update` | replaced | `ui::TextRenderer::draw` (pen snapped to output pixels) | re-render when dirty, copy at floor(x·W + 0.5) (values ≥ 1 are pixels); declared only while some line is enabled. Text is not cut at the box in OpenMM2 (finding A1.9). |
| `mmTextNode::GetTextDimensions` | replaced | `ui::TextRenderer::measure`, `lineHeight` | GDI extent over the screen size. |
| `mmTextNode::Init` (both) | replaced | widget boxes (`ui::Widget::box`) | pixel and fraction variants; bitmap clamped to the screen. |
| `mmTextNode::AddText`, `mmTextNode::SetString`, `mmTextNode::SetTextPosition`, `mmTextNode::SetPosition` | replaced | the widgets' text callbacks and boxes | `AddText` truncates at 255 characters, `SetString` ignores strings of 256 or more; `SetPosition` keeps the bitmap inside the screen width and y ≥ 0 (used by `UIBMButton` tooltips, `UISlider`, `UIControlWidget`, `UIIconW`). |
| `mmTextNode::SetEffects`, `mmTextNode::GetEffects` | ported | per-widget drawing (above) | |
| `mmTextNode::SetFGColor`, `mmTextNode::GetFGColor` | ported | colour arguments of `TextRenderer::draw` (`ui::style` colours) | |
| `mmTextNode::SetBGColor`, `mmTextNode::SetHlColor` | not needed | — | only `mmHUD::mmHUD` sets them; the background colour is never drawn (transparent mode) and no HUD line uses bit 7. |
| `mmTextNode::mmTextNode`, `mmTextNode::~mmTextNode`, ``mmTextNode::`vector_deleting_destructor'`` | not needed | — | construction (foreground white, highlight yellow, all lines enabled, black transparent) and destructors. |
| `mmTextNode::Printf`, ``mmTextNode::`scalar_deleting_destructor'`` | not needed | — | unreachable (no callers). |

## mmNumberFont

Pre-rendered digit glyphs for the race HUD: one fitted bitmap per character of "0123456789:,/." in a font and colour,
drawn by `mmNumber`. Built by `mmHUD` (Broadway, string 58, white), `mmCRHUD` (Gill Sans MT, yellow), `mmCircuitHUD`
(strings 256/257) and `mmWPHUD` (strings 251/252). Not frontend; OpenMM2 draws the HUD's numbers as text.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmNumberFont::mmNumberFont`, `mmNumberFont::LoadFont`, `mmNumberFont::LoadLocFont`, `mmNumberFont::~mmNumberFont` | replaced | `game/session/Hud.cpp` (`Hud::font`, digits through `ui::TextRenderer`) | glyph bitmaps → font atlas; the HUD audit owns the fonts and colours. |

## mmToolTip

Widget tooltips. Disabled in build 3393: `uiWidget::AddToolTip` is empty, so no tooltip is ever created.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmToolTip::mmToolTip`, `mmToolTip::Init` | not needed | — | unreachable (no callers). |
| `mmToolTip::Switch`, `mmToolTip::Update` | not needed | — | empty bodies. |
| `mmToolTip::SetText` | not needed | — | only through `uiWidget::SetToolTipText`, which finds no tooltip. |
| `mmToolTip::~mmToolTip`, ``mmToolTip::`scalar_deleting_destructor'`` | not needed | — | destructors. |

## sfPointer (GetPointerHeight, WaitForRelease)

The menu pointer node (`MenuManager::InitCommonStuff`): `midcursor` drawn at the mouse (clamped to 4 px inside the
screen, not drawn in windowed mode or, outside the popup, with an IME), and the mouse state machine that feeds
`MenuManager::MouseAction` (press, release, move; a release counts only over the widget that took the press). The
pointer picture is `app/frontend/MenuPointer.cpp` (infrastructure's; frontend pages only); the state machine is
`ui::NavReader` + `ui::Menu::update` here (finding A1.5).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `sfPointer::GetPointerHeight` | not needed | — | 0.05 of the screen: `UIBMButton::Update` puts a button's tooltip text that far below the pointer after 0.5 s of focus, but no button has a tooltip (`UIBMButton::Init` clears the flag and nothing sets it), so it never shows. No effect on `ui::SpriteButton`. |
| `sfPointer::WaitForRelease` | ported | `controls::CaptureReader` (input-ff's), `BindingList` (`m_swallow`) | sets the pointer to "wait for release": the held button gives no press or release event until let go. Only `UICWArray::AcceptCapture` calls it, after a control is assigned, so a mouse button captured as a binding does not also click the menu. OpenMM2's capture waits for the starting click's release before it listens and swallows the capturing key for a frame; a mouse button just bound still gives its release to the menu (inferred from the code: released over DONE or CANCEL it would press that button). |

## Widget events (MM2)

Conventions used below: the uiWidget vtable is Disable 0x34, Enable 0x38,
TurnOn 0x3c, TurnOff 0x40, SetReadOnly 0x44, Action 0x48, CaptureAction 0x4c,
Switch 0x50, EvalMouseX 0x54, ReturnDescription 0x58, SetPosition 0x5c,
GetScreenHeight 0x60 (rdata vtable of UIBMButton). uiWidget +0x3c is the
focus state, +0x44 read-only, +0x60 enabled, +0x38 the last mouse event kind
(UIMenu::MouseHitCheck: 0 press, 1 release, 2 move), +0x2c/+0x30 the mouse
position. Event routing that every widget below depends on:

- Keys: UIMenu::CheckInput hands every key-down event to
  MenuManager::ScanGlobalKeys (Escape, Tab/Down, Up, Enter; F4 only in the
  popup MenuManager) and then, for any key code above 7, UIMenu::KeyboardAction
  calls the focused widget's Action with the event (after Up/Down have already
  moved the focus, so the newly focused widget sees the Up/Down key); Enter
  also sets the menu's action id to the widget's id. While MenuManager +0x2c
  (capture) is set, ScanGlobalKeys does nothing and the event goes to the
  capture widget's CaptureAction instead.
- Mouse (sfPointer::Update): a press hits the enabled widget under the pointer
  (Action with kind 0) and remembers it; the release hits again (kind 1) but a
  widget other than the pressed one gets its hit cleared, so only a release on
  the widget that took the press makes UIMenu::CheckMouseHits call Switch,
  Action and SetAction (the menu acts). Moves (kind 2) focus the widget under
  the pointer; over nothing (and no capture) MenuManager::ClearAllWidgets
  clears every highlight. A press outside the capture widget's hot spot ends a
  capture. The hit test checks only "enabled" (+0x60), so read-only widgets
  take the mouse focus (keyboard focus skips them: UIMenu::Increment/Decrement).
- MenuManager::PlaySound: 0 Moveselector and 1 Selectionmade (0.75) only while
  a popup is shown (MenuManager +0x34; +0x30 marks the in-game popup manager),
  2 Switch (0.85, rewound) always.

## UIBMButton

The bitmap button of every frontend menu (UIMenu::AddBMButton): sprite sheets
of 3 (type 3), 4 (type 4), 5 (types 5 and 6: toggles) or 7 (type 7) frames.
A type 5 button with a data pointer, or type 6 with a data pointer and a value,
is a "Mex" radio item (lit while `*data == value`); type 6 without a value is
a check box writing 0/1 to `*data`. OpenMM2: `ui::SpriteButton` (types 3, 4,
the strip's 5-frame OPTIONS through `lit`) and `ui::LampItem` (types 5/6 radio
items and check boxes). Each button has a sound slot (AllocateSounds) chosen
by its creator; recovered for every AddBMButton call in the asm (see
findings A2.3-A2.4).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `UIBMButton::UIBMButton`, `UIBMButton::Init` | ported | `ui/Widgets.cpp` `SpriteButton::SpriteButton`, `LampItem::LampItem`; pages pass the sheet, frame count and callbacks | Type to frame count and toggle/Mex kind as above; a type 6 check box starts lit when `*data` is non-zero (OpenMM2 reads `isOn` every frame). The mmToggle2 companion (+0xa0) and the tool-tip flag (+0x98) are cleared here and never set by any code (asm: the only writes to a button's +0x98/+0xa0 are these zeroes), so both are dead. |
| `UIBMButton::LoadBitmap`, `UIBMButton::GetDiv`, `UIBMButton::GetSize`, `UIBMButton::GetHitArea`, `UIBMButton::SetPosition`, `UIBMButton::GetScreenHeight` | ported | `spriteFrameSize`, `SpriteButton::draw`/`LampItem::draw` (box = one frame) | Frame height = bitmap height / frames (3, 4, 5, 7); the hit area is one frame (plus the never-created mmToggle2's width). GetScreenHeight is only read by ControlSetup::SetControlPosition to stack rows; OpenMM2 positions from tune/widget.csv. |
| `UIBMButton::Cull` | ported | `SpriteButton::draw`, `LampItem::draw` | Plain buttons: states 0/1/2 draw frames 0/1/2, disabled (4) frame 3 (frame 4 for 7-frame sheets). Toggles: frames = state 0-4. A read-only toggle in state 0 draws the disabled frame 4; toggles become read-only only through Kill (which sets that frame anyway), so `LampItem::draw` ignoring `readOnly` shows nowhere. A 3-frame button in state 4 would read a fourth frame past its sheet; no 3-frame button is ever disabled (they are hidden instead), so OpenMM2's greyed frame 0 never shows either. |
| `UIBMButton::Switch` | ported | `SpriteButton::draw`, `LampItem::draw` (`focused`), `Widget::help` | Calls the focus callback (the pages' FocusDescription, i.e. the help picture) with the focus state before anything else, read-only or not (finding A2.7), then, unless read-only or disabled: focused 1 (3 for a lit toggle), unfocused 0 (2 for a lit toggle). The tool-tip timer branch is dead (+0x98 never set). |
| `UIBMButton::Action` | ported | `SpriteButton::mouse`/`activate`, `LampItem::mouse`/`activate` | Nothing when read-only or disabled. Mouse press (kind 0 only): MenuManager::PlaySound(1) (heard only in popups), toggles DoToggle and stop; other buttons show the pressed frame (state 2), play their own sound and call their datCallback. Keys: only Enter (0x1c) and Space (0x39): a toggle (a Mex only when not already selected) DoToggle, then every button plays its sound and calls its datCallback. The menu acts (id) on Enter or on the release. findings A2.1, A2.2, A2.5. |
| `UIBMButton::DoToggle`, `UIBMButton::MexOn`, `UIBMButton::MexOff` | ported | `LampItem::activate`, `LampItem::mouse`; `addNavStrip` (`lit` for OPTIONS) | Toggles only: plays the button's sound, flips; a check box writes 0/1 and calls back; a Mex item not yet selected writes its value (MexOn) and calls back; a Mex item already selected only flips its internal flag (no callback, finding A2.5). MexOn/MexOff are also used by uiNavBar::OptionActive/OptionInActive (OPTIONS lit), which OpenMM2 does with `lit`. |
| `UIBMButton::MexToggle` | not needed | - | No callers (reachable 0). |
| `UIBMButton::Update` | ported | `LampItem::draw` (`isOn()` each frame) | Re-syncs the lit state with `*data` every frame (a Mex item turns off when another value is written). The tool-tip block (reads sfPointer::GetPointerHeight, a constant 0.05 of the screen, to put the tip text just below the pointer after a delay) never runs because +0x98 is never set; nothing to port. |
| `UIBMButton::Kill`, `UIBMButton::Unkill` | ported | `RacesPage::refresh` (`m_lamps[i]->enabled`), `PagesMulti.cpp` provider lamps | Kill = disabled frame + read-only (still enabled, so the mouse can focus it); Unkill = lit or unlit by `*data == 1` and writable. Callers: RaceMenuBase (lamps of modes the city lacks), Dialog_Message, NetSelectMenu providers. OpenMM2 uses `enabled = false` (same frame, no input; the mouse-focus difference is finding A2.7). |
| `UIBMButton::Enable`, `UIBMButton::Disable` | ported | `Widget::enabled` | State 0 / 4 plus uiWidget's enabled flag. |
| `UIBMButton::AllocateSounds`, `UIBMButton::PlaySound` | ported | `SpriteButton::sound`, `LampItem::sound`, `soundVolume` | Slots: 0 Selectionmade 0.86, 1 Uisunny, 2 Uicloudy, 3 Uirain, 4 Uisnow, 5 Uimorning, 6 Uinoon, 7 Uisunset, 8 Uinight, 9 Moveselector, 10 Uigo (all 0.9); slot -1 is silent. Per-button slots checked against every AddBMButton call: findings A2.3, A2.4. |
| `UIBMButton::ReturnDescription` | not needed | - | "3 State"/"4 State"/"5 State"/"7 State"; only WArray::DumpMenu (the widget-table writer, debug) calls it. |
| `UIBMButton::~UIBMButton`, ``UIBMButton::`scalar_deleting_destructor'`` | not needed | - | Memory (frees the bitmap and the shared sound bank). |

## UIBMLabel

A read-only picture label whose name is a space-separated list of pictures and
whose shown picture is the list entry at `*index` (UIMenu::AddBMLabel); every
frontend page uses one for its help ("desc icons") picture, which the pages'
FocusDescription callbacks select by writing the index; CrashCourse::PreSetup
and VehicleSelectBase::SetLockedLabel change the list. OpenMM2 draws the help
picture of the focused widget in `ui::Menu::drawContent` at `helpPos`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `UIBMLabel::UIBMLabel`, `UIBMLabel::Init`, `UIBMLabel::SetBitmapName` | ported | `ui/Widgets.cpp` `Menu::drawContent`, `Widget::help`/`helpFn`, `Menu::helpPos`, `defaultHelp` | Read-only; position from the table; a negative index loads nothing. Per-page picture lists are the pages' `help` strings. |
| `UIBMLabel::LoadBitmap`, `UIBMLabel::Update`, `UIBMLabel::Cull` | ported | `Menu::drawContent` | Reloads when `*index` changes; drawn 1:1 at its pixel position. |
| `UIBMLabel::~UIBMLabel`, ``UIBMLabel::`scalar_deleting_destructor'`` | not needed | - | Memory. |

## UIButton

The text button (UIMenu::AddButton): a text node in the box with the
GetFont(n) font and the SetType effects, colour 0, 3 when focused, 5 when
read-only. Used by the in-game popups (PUMain, PUExit, PUQuit, PUOptions,
PUMenuBase's Resume/Previous/OK/Cancel, PUResults, PURoster, PUReplay*,
PUDebug) and the lobby's Dialog_Eject. OpenMM2: `ui::TextButton` (popup pages
in `app/frontend/PopupOptions.cpp` and `RaceScreen.cpp`, the results page).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `UIButton::UIButton`, `UIButton::Init`, `UIButton::SetString`, `UIButton::Update` | ported | `ui/Widgets.cpp` `TextButton::TextButton`, `TextButton::label`, `font` | The label in the given font, SetType, Switch(0); the Card2D highlight (+0x80) is never created (only its alpha would change on focus). |
| `UIButton::SetType` | ported | `TextButton::type`, `TextButton::draw` | Effects 1 (type 0), 7 (type 1: centred and outlined), 3 (type 2: centred). |
| `UIButton::Switch` | ported | `TextButton::draw` | Not when read-only: colour 3 focused, 0 otherwise (popup: yellow-green / white). |
| `UIButton::SetReadOnly` | ported | `TextButton::draw` (`!enabled`: `kPopupDisabled`), e.g. `restart.enabled = !net` | Read-only = colour 5 (0.35 grey in popups) and no action; MM2 keeps it enabled, so the mouse can still focus it (no highlight); OpenMM2 uses `enabled = false`. PUResults (Next/Replay) and PUMain (Restart/Quit/Replay RO) are the callers. |
| `UIButton::Action` | ported | `TextButton::activate`, `TextButton::mouse` | Not when read-only. Enter (0x1c) or the mouse press (kind 0): MenuManager::PlaySound(1) (Selectionmade 0.75, popups) and the datCallback; Space is ignored. The menu's id action follows on Enter or on the release over the same button. OpenMM2 plays the sound and acts on the release (finding A2.1). |
| `UIButton::Enable`, `UIButton::Disable` | ported | `Widget::enabled`, `visible` | Disable also hides the button (clears the node's active bit); only PUMenuBase::DisableExit uses it (mmPopup::Lock), so OpenMM2's greyed `enabled = false` never stands in for it. |
| `UIButton::DrawOff`, `UIButton::DrawOn`, `UIButton::TestHit` | not needed | - | No callers (reachable 0): "%s" / "%s on" label variants and a hit test that returns 0. |
| `UIButton::~UIButton`, ``UIButton::`scalar_deleting_destructor'`` | not needed | - | Memory. |

## UILabel

A read-only text label (UIMenu::AddLabel disables it, so the mouse never hits
it), colour 0, flags: 8 centres, 4 drops the vertical centring, 1 blinks
between colour 1 and colour 4 every 0.75 s. Users: PUMenuBase::CreateTitle
(popup titles), PUExit's question, PUReplaySave, AboutMenu's product-ID label
("PID Label", set to the registry's product ID or string 325) and
NetSelectMenu's blinking "Looking for games..." (string 657, flags 1, at
0.0625, 0.825, i.e. over the help picture). OptionsBase::CreateTitle has no
callers. OpenMM2: `ui::TextBox` (About page), `popup::drawTitle`, the PUExit
text in `RaceScreen.cpp`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `UILabel::UILabel`, `UILabel::Init`, `UILabel::SetText` | ported | `ui/Widgets.cpp` `TextBox`; `app/frontend/PagesOptions.cpp` `AboutPage::AboutPage`; `PopupOptions.cpp` `popup::drawTitle` | Font GetFont(n), colour 0 (yellow in the menus, white in popups), effects from the flags; Init also measures "Just the Height" once for the global label height. |
| `UILabel::Update`, `UILabel::SwitchState` | ported | `app/frontend/PagesMulti.cpp` session page `drawAbove` | The blink only exists for NetSelectMenu's string 657 (flags 1): dark grey (0.1) / yellow, 0.75 s each, in the help-picture spot. OpenMM2's session page blinks it the same way while its search has found nothing (finding A2.8). |
| `UILabel::Switch`, `UILabel::Action` | ported | `TextBox::focusable` (false) | Both do nothing. |
| `UILabel::SetBlink` | not needed | - | No callers. |
| `UILabel::~UILabel`, ``UILabel::`scalar_deleting_destructor'`` | not needed | - | Memory. |

## UIIcon

A static picture drawn 1:1 at its pixel position (UIMenu::AddIcon: the race
map in RaceMenuBase and NetArena; created directly as decorations by
UITextDropdown::Init (the dropdown_bx frame), RaceMenuBase, HostRaceMenu,
VehicleSelectBase). Added to a menu it is enabled and writable (a focus stop
with no highlight). LoadBitchmap (sic) swaps the picture (race maps, the car
description in VehicleSelectBase::ShowCarDesc). OpenMM2: `ui::Picture` and
page drawing.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `UIIcon::UIIcon`, `UIIcon::Init`, `UIIcon::LoadBitmap`, `UIIcon::LoadBitchmap` | ported | `ui/Widgets.cpp` `Picture::Picture`, `Picture::path` (`RacesPage` map, lobby map); `PagesRace.cpp` `carDescription`; `PagesMain.cpp` `drawBelow` (dropdown_bx) | The picture can change at any time; a missing picture draws nothing. LoadBitmap also sets the (unused) tool-tip text. |
| `UIIcon::GetHitArea`, `UIIcon::Cull`, `UIIcon::Update` | ported | `Picture::draw`, `Picture::focusable` | The hit area and the drawing are the picture's own size; OpenMM2 stretches the picture to the layout box, which matches only while the tune/widget.csv box equals the picture (not checked against the data; inferred). |
| `UIIcon::Switch` | ported | `Picture::focusStop`, `Picture::draw` (no highlight) | Only records the focus. |
| `UIIcon::~UIIcon`, ``UIIcon::`scalar_deleting_destructor'`` | not needed | - | Memory. |

## UIIconW

A picture button with an mmToggle frame and a delayed tool-tip text under the
pointer (UIMenu::AddIconW). AddIconW has no callers, so no UIIconW is ever
created; its vtable methods are "reachable" only through the vtable.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `UIIconW::UIIconW`, `UIIconW::Init`, `UIIconW::Action`, `UIIconW::Switch`, `UIIconW::Update`, `UIIconW::~UIIconW`, ``UIIconW::`scalar_deleting_destructor'`` | not needed | - | Unreachable: never constructed (UIMenu::AddIconW has no callers). |

## UIToggleButton

A text button with a "button_tgl" check box (mmToggle) at its left, acting on
the mouse release or Enter (UIMenu::AddToggle). AddToggle has no callers, so
it is never created.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `UIToggleButton::UIToggleButton`, `UIToggleButton::Init`, `UIToggleButton::Action`, `UIToggleButton::DoToggle`, `UIToggleButton::DrawOff`, `UIToggleButton::DrawOn`, `UIToggleButton::Update`, `UIToggleButton::~UIToggleButton`, ``UIToggleButton::`scalar_deleting_destructor'`` | not needed | - | Unreachable: never constructed (UIMenu::AddToggle has no callers). |

## UIToggleButton2

The popup ON/OFF toggle (UIMenu::AddToggle2), used twice by PUGraphics
(Vehicle Reflections 647, Textured Sky 645). A UIButton whose label box is the
given width less 0.075 of the screen, followed by a second text node 0.075 of
the screen wide showing string 607 "ON" or 606 "OFF" centred (effects 3) in the
same font. OpenMM2: `ui::TextToggle` in `PopupOptions::buildGraphics`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `UIToggleButton2::UIToggleButton2`, `UIToggleButton2::Init` | ported | `ui/Widgets.cpp` `TextToggle::TextToggle`; `app/frontend/PopupOptions.cpp` `buildGraphics` (`toggle`) | The ON/OFF part is 0.075 of the screen (48 px) after UIMenu::ScaleWidget; OpenMM2 too since finding A2.6. Initial text from `*data`. |
| `UIToggleButton2::DrawOn`, `UIToggleButton2::DrawOff`, `UIToggleButton2::DoToggle` | ported | `TextToggle::draw` (`isOn`), `TextToggle::activate` (`flip`) | DoToggle writes 1/0 to `*data` and sets "ON"/"OFF". |
| `UIToggleButton2::Action` | ported | `TextToggle::activate`, `TextToggle::mouse` | Mouse press: PlaySound(1), DoToggle; Enter: DoToggle; then UIButton::Action (PlaySound(1) again and the callback, unless read-only). Space does nothing. MM2 flips on the press, OpenMM2 on the release (finding A2.1). A read-only toggle still flips its data in MM2 (no callback); PUGraphics never makes them read-only. |
| `UIToggleButton2::Switch` | ported | `TextToggle::draw` | Records the focus and calls UIButton::Switch (label colour 3 / 0); the ON/OFF node keeps its default colour. |
| `UIToggleButton2::Enable`, `UIToggleButton2::Disable` | not needed | - | They only set the internal draw state (3 / 1) used by DrawOn/DrawOff; the widget is never disabled. |
| `UIToggleButton2::Update` | ported | `TextToggle::draw` | Updates its text nodes. |
| `UIToggleButton2::Unkill` | not needed | - | No callers. |
| `UIToggleButton2::~UIToggleButton2`, ``UIToggleButton2::`scalar_deleting_destructor'`` | not needed | - | Memory. |

## UIMexButton

A text radio button with an "onoff_radio" lamp (mmToggle2) at its left
(UIMenu::AddMex). AddMex has no callers, so it is never created.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `UIMexButton::UIMexButton`, `UIMexButton::Init`, `UIMexButton::Action`, `UIMexButton::DoToggle`, `UIMexButton::DrawOff`, `UIMexButton::DrawOn`, `UIMexButton::Update`, `UIMexButton::~UIMexButton`, ``UIMexButton::`scalar_deleting_destructor'`` | not needed | - | Unreachable (reachable 0): UIMenu::AddMex has no callers. |

## UISlider

The slider widget (UIMenu::AddSlider: id, label, float data, box, min, max,
an int Init ignores, label mode, a second ignored int (callers pass 16, 20
or 24, which look like font ids; the label uses MenuManager's label font),
balance flag, change and focus callbacks).
Label mode 0 (every frontend slider): no label; > 0: label in the left third,
slider in the other two thirds (no callers use it); -1 (the popups): label
above in colour 0 and the label font, slider one popup line below. The
slider itself is an mmSlider child. Users: GraphicsOptions (far clip 100-1000,
lighting 0-3), AudioOptions (volumes 0-1, balance -1..1), ControlSetup (4),
RaceMenuBase densities, VehicleSelectBase stat bars (read-only), the popups
PUAudioOptions/PUControl/PUGraphics, and Dialog_RaceEnvironment (never
opened). OpenMM2: `ui::Slider`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `UISlider::UISlider`, `UISlider::Init` | ported | `ui/Widgets.cpp` `Slider::Slider`, `Slider::label`/`labelHeight` (popups: `PopupOptions.cpp` `addSlider`), `Slider::balance` | Label modes 0 and -1 as above (mode > 0 has no callers); the value is read from the data pointer and pushed into the mmSlider; hot spots computed. |
| `UISlider::SetPosition`, `UISlider::SetMouseParams`, `UISlider::GetFudgeWidth`, `UISlider::GetScreenHeight` | ported | `Slider::rowY`, `Slider::draw`, `Slider::mouse` (box from tune/widget.csv or the popup layout) | The hit rectangle covers the label (label modes) and the arrows; the hot spots are the arrow and track rectangles one arrow frame high. GetScreenHeight feeds ControlSetup's row stacking only. |
| `UISlider::Action` | ported | `Slider::adjust`, `Slider::mouse` | Read-write only. Left (0xcb) Dec, Right (0xcd) Inc, then the change callback and MenuManager::PlaySound(2) ("Switch" 0.85, also at a limit); the mouse press goes to EvalMouseXY; any other mouse event (the release) clears the pressed-arrow frames. OpenMM2 too since finding A2.9. |
| `UISlider::EvalMouseXY` | ported | `Slider::mouse` | Outside the hot-spot row (one arrow frame high) nothing happens; left arrow area Dec, right arrow area Inc, the track sets min + (max - min) x the fraction across it (no snapping), each with the callback and "Switch". OpenMM2 too since finding A2.10. |
| `UISlider::Switch` | ported | `Slider::draw` (`focused` band), `Slider::focusChanged`, `Widget::help` | Read-write: the focused band and pressed arrows cleared; always the focus callback (help picture), read-only too (finding A2.7). |
| `UISlider::SetReadWrite` | ported | `Widget::readOnly`, `Slider::draw` (slider_ro bitmaps, no arrows) | Read-only also marks the widget read-only (no keyboard focus) and unfocuses it. |
| `UISlider::Update`, `UISlider::SetValue`, `UISlider::SetData` | ported | `Slider::draw` (reads `get()` every frame), `Slider::set` | An external change to the data is shown at once (and calls the change callback). |
| `UISlider::GetValue`, `UISlider::IsReadWrite`, `UISlider::SetText`, `UISlider::TestHit` | not needed | - | No callers (reachable 0); SetText and TestHit are empty. |
| `UISlider::~UISlider`, ``UISlider::`scalar_deleting_destructor'`` | not needed | - | Memory. |

## mmSlider

UISlider's drawing and value model: arrows slider_larr/slider_rarr (or
slider_lbal/slider_rbal with the balance flag, only PUAudioOptions' Balance),
5 frames each; a track of 2 px segments, (round(w x screen width) - 2 x arrow
width) / 2 rounded, less 1, clamped 2..300; 20 positions on more than 20
segments, else 5; the value bar in three bands of slider_actl (unfocused,
focused, disabled), the empty part one row of slider_inactl; read-only uses
slider_roactl/slider_roinactl whole and no arrows. OpenMM2: `ui::Slider`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmSlider::mmSlider`, `mmSlider::Init`, `mmSlider::LoadBitmap`, `mmSlider::SetReadWrite`, `mmSlider::IsReadWrite` | ported | `ui/Widgets.cpp` `Slider::draw` (bitmap names, `balance`, `readOnly`) | Band height = slider_actl height / 3 (read-write) or the whole height (read-only); the bar sits (arrow frame height - band height) / 2 below the row (OpenMM2 hard-codes 6-row bands and 11 px, matching an 18 px slider_actl and 29 px arrow frames; not re-checked against the data). |
| `mmSlider::SetStep`, `mmSlider::SetRange` | ported | `Slider::segments`, `Slider::step` | As above; a range with min >= max only warns. |
| `mmSlider::SetValue`, `mmSlider::UpdatePosition`, `mmSlider::Inc`, `mmSlider::Dec` | ported | `Slider::adjust`, `Slider::mouse`, `Slider::draw` (`filled`) | Clamped to the range; position = whole part of fraction x (segments + 1); Inc/Dec step by (max - min) / (positions - 1) and mark their arrow pressed. At the maximum MM2's bar is one segment longer than the track (hidden under the right arrow); OpenMM2 clamps it to the track. |
| `mmSlider::Cull`, `mmSlider::Update` | ported | `Slider::draw` | Arrow frames: 0 unfocused, 1 focused, 2 focused and pressed, 4 when the UISlider is disabled; the bar band likewise. |
| `mmSlider::GetSliderHotSpots`, `mmSlider::FudgeWidth`, `mmSlider::GetScreenHeight`, `mmSlider::SetPosition` | ported | `Slider::mouse`, `Slider::rowY` | Hot spots: left arrow, track, right arrow, one arrow frame high. In the popup MenuManager GetScreenHeight is the popup line height (layout only). |
| `mmSlider::~mmSlider`, ``mmSlider::`scalar_deleting_destructor'`` | not needed | - | Memory. |

## UITextDropdown

The value box with a drop-down list (UIMenu::AddTextDropdown): the value in a
box (height: drop_arrow frame + 2, kept within 16..100 px), drop_arrow
(drop_arrow2 in the popup MenuManager) 1 px inside its right edge, an
optional icon (dropdown_bx) 1 px right of and below its corner, an optional
label above (popups). Enter or a press inside the box opens the list
(SetSliderFocus registers the list's rectangle as MenuManager's capture hot
spot); the open list takes every key (CaptureAction). Users: MainMenu DRIVER,
RaceMenuBase (race name, locale, time, weather), CrashCourse, VehicleSelectBase
(colour, transmission), Graphics/Audio/Control options, HostRaceMenu, the
popups PUControl and PUGraphics, Dialog_Serial (DirectPlay). OpenMM2:
`ui::ValueBox` (+ `stepOption` for the separate roller buttons; `popup` mode).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `UITextDropdown::UITextDropdown`, `UITextDropdown::Init` | ported | `ui/Widgets.cpp` `ValueBox::ValueBox` (23 px box), `ValueBox::draw`, `popup`/`label`/`labelHeight` (`PopupOptions.cpp` `addDropdown`); `PagesMain.cpp` `drawBelow` (icon) | Arrow at x + w - (arrow width + 1), y + 1; label (colour 0, label font) one popup line above the box; the value from `*data`. |
| `UITextDropdown::Action` | ported | `ValueBox::activate`, `ValueBox::mouse`, `ValueBox::adjust` | Not when read-only. Enter opens and plays MenuManager::PlaySound(1) (heard only in popups: finding A2.13); a press inside the box (not the label) opens silently; Left/Right/Space do nothing. |
| `UITextDropdown::CaptureAction` (with its split piece at 0x4e847d) | ported | `ValueBox::modalInput` | Keys: Escape closes through Switch(0) (the box also loses its focus colour); Enter closes and commits the highlighted entry to `*data` and calls back; Home first entry, Up/Left DecDrop, Down/Right IncDrop, End last entry, each with PlaySound(1) (popups only). Mouse inside the list: every event moves the highlight (enabled entries only), the release commits the entry (a locked one becomes the first enabled one), PlaySound(1), calls back. A press outside the list (including on the box itself) ends the capture through sfPointer and MenuManager::ClearAllWidgets, which also clears every highlight. findings A2.12, A2.13. |
| `UITextDropdown::SetSliderFocus` | ported | `ValueBox::m_open`, `ValueBox::modal` | Registers/unregisters the list rectangle as the capture hot spot and shows/hides the list. |
| `UITextDropdown::Switch` | ported | `ValueBox::draw` (colour 3 focused or open, 0 otherwise), `Widget::help` | Nothing at all when read-only (no colour change, no focus callback); unfocusing closes the list. |
| `UITextDropdown::Cull` | ported | `ValueBox::draw` | Arrow frame 0 unfocused, 1 focused, 2 open, drawn whenever the box is shown, read-only or not; OpenMM2 too since finding A2.11. |
| `UITextDropdown::Update`, `UITextDropdown::SetValue`, `UITextDropdown::SetData` | ported | `ValueBox::get`/`set`, `ValueBox::draw` | An external change of `*data` updates the box; the open list is drawn last (MenuManager::DeclareLastDrawn) = `drawPopup`. |
| `UITextDropdown::AssignString` | ported | `ValueBox::options` | Replaces the option list (driver list, race names). |
| `UITextDropdown::SetDisabledMask` | ported | `ValueBox::optionEnabled` | Bit mask of entries that cannot be picked (32 entries at most). |
| `UITextDropdown::GetScreenHeight` | ported | box + `labelHeight` | Label + box height, for layout. |
| `UITextDropdown::AnyEnabled`, `UITextDropdown::GetValue`, `UITextDropdown::SetPos`, `UITextDropdown::SetText` | not needed | - | No callers (SetPos and SetText are empty). |
| `UITextDropdown::~UITextDropdown`, ``UITextDropdown::`scalar_deleting_destructor'`` | not needed | - | Memory. |

## TextDropWidget

UITextDropdown's value text node plus its mmDropDown list: text effects 0x11
(vertically centred, 5 px inset) in the menus, 0x15 (plus the white
rectangle) in popups; keeps the value (+0x24) and the highlighted entry
(+0x1c). OpenMM2: inside `ui::ValueBox`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `TextDropWidget::TextDropWidget`, `TextDropWidget::Init` | ported | `ValueBox::draw` (value 5 px in; popup outline) | |
| `TextDropWidget::SetValue` | ported | `ValueBox::modalInput` (`settle`), `ValueBox::draw` | An entry that cannot be picked becomes mmDropDown::FindFirstEnabled; clamped to the list; sets value, highlight and the box text at once, so key steps in the open list change the box text before anything is committed (finding A2.12). |
| `TextDropWidget::IncDrop`, `TextDropWidget::DecDrop` | ported | `ValueBox::modalInput` | One step from the highlighted entry, clamped, then SetValue. |
| `TextDropWidget::Capture` | ported | `ValueBox::modalInput` (`under`) | Mouse hit in the list: records the hit entry even when locked, highlights only enabled ones. After hovering a locked entry MM2's next key step starts from that entry; OpenMM2's from the last enabled one (edge case). |
| `TextDropWidget::GetCount`, `TextDropWidget::SetString`, `TextDropWidget::SetDisabledMask` | ported | `ValueBox::options`, `optionEnabled` | |
| `TextDropWidget::Switch`, `TextDropWidget::IsActive`, `TextDropWidget::SetActive`, `TextDropWidget::Update` | ported | `ValueBox::draw`, `m_open` | Text colour; list shown flag. |
| `TextDropWidget::Inc`, `TextDropWidget::Dec`, `TextDropWidget::SetHighlight`, `TextDropWidget::GetDisabledMask` | not needed | - | No callers (reachable 0). |
| `TextDropWidget::~TextDropWidget`, ``TextDropWidget::`scalar_deleting_destructor'`` | not needed | - | Memory. |

## mmDropDown

The list of a TextDropWidget: one text node per option, black cells of the
box's size, yellow (1, 1, 0) text from the cell's top-left, olive (0.5, 0.5,
0) for locked entries, a white rectangle on the highlight; rows below the box,
a new column to the right when the next row would pass the screen bottom, the
whole list shifted left one box width per extra column when it is too tall
and two columns would not fit right of the box. OpenMM2: `ValueBox::listCells`
and `ValueBox::drawPopup` (first audit fixed the layout).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmDropDown::mmDropDown`, `mmDropDown::Init`, `mmDropDown::InitString`, `mmDropDown::SetString` | ported | `ValueBox::listCells`, `ValueBox::drawPopup` | Cell height = box height (positions rounded to 1/1024 of the screen). |
| `mmDropDown::GetHit`, `mmDropDown::GetXmin`, `mmDropDown::GetYmin`, `mmDropDown::GetW`, `mmDropDown::GetH` | ported | `ValueBox::listCells`, `modalInput` (`under`) | Column x rows-per-column + row; the list rectangle is the capture hot spot. |
| `mmDropDown::SetHighlight` | ported | `ValueBox::modalInput`, `drawPopup` (outline on `m_hover`) | Only enabled entries take the highlight. |
| `mmDropDown::SetDisabledColors` | ported | `drawPopup` (`kOptionDisabled`) | |
| `mmDropDown::FindFirstEnabled` | ported | `ValueBox::modalInput` (`settle`) | First enabled entry, 0 when none. |
| `mmDropDown::GetCurrentString` | ported | `ValueBox::draw` | Text of an entry. |
| `mmDropDown::Update` | ported | `ValueBox::drawPopup` | Drawn only while open. |
| `mmDropDown::~mmDropDown`, ``mmDropDown::`scalar_deleting_destructor'`` | not needed | - | Memory. |

## UITextRoller2

The number roller with built-in arrows (UIMenu::AddTextRoller2): the value
centred (effects 3) left of roller_up and roller_down (3 frames each, the down
arrow 1 px below the up arrow's frame); optional label (mode 0 above, 1 left
half, 2 a picture; RaceMenuBase passes -1: none). Users: RaceMenuBase LAPS
and OPPONENTS (clamping, no label), Dialog_Host (DirectPlay host dialog,
replaced). OpenMM2: `ui::Roller` (race menu and host settings LAPS/OPPONENTS).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `UITextRoller2::UITextRoller2`, `UITextRoller2::Init` | ported | `ui/Widgets.cpp` `Roller::Roller`, `Roller::draw` | Value colour 0 (3 focused), font from the caller; a wrap flag (only the value 1 wraps; RaceMenuBase passes 10, so it clamps). The "[%Ns]|" string Init builds is thrown away; drop_frame is loaded but never drawn. |
| `UITextRoller2::Action` | ported | `Roller::adjust`, `Roller::mouse` | Not when read-only. Left Dec, Right Inc (plus MenuManager::PlaySound(1) unless at the limit, heard only in popups), the pressed-arrow state cleared, callback. Mouse press: EvalMouseXY; any other mouse event clears the pressed arrow (finding A2.9). |
| `UITextRoller2::EvalMouseXY` | ported | `Roller::mouse` | Press on the up arrow Inc, on the down arrow Dec (with the callback); elsewhere clears the pressed arrow. |
| `UITextRoller2::Inc`, `UITextRoller2::Dec`, `UITextRoller2::SetValue` | ported | `Roller::adjust`, `Roller::maxIndex` | MenuManager::PlaySound(2) ("Switch" 0.85) on every step, also at a limit; the pressed arrow marked; clamped to the list. |
| `UITextRoller2::Switch` | ported | `Roller::draw` (`focused`), `Roller::focusChanged` | Not when read-only: value (and label) colour 3 / 0. |
| `UITextRoller2::Cull` | ported | `Roller::draw` | Arrows only when writable: frame 0 unfocused, 1 focused, 2 focused and pressed. |
| `UITextRoller2::Update` | ported | `Roller::draw` (reads `get()`) | An external change of `*data` is shown (and calls back). |
| `UITextRoller2::SetString`, `UITextRoller2::SetText` | ported | `Roller::options` | Option list and the shown entry. |
| `UITextRoller2::SetData` | not needed | - | No callers. |
| `UITextRoller2::~UITextRoller2`, ``UITextRoller2::`scalar_deleting_destructor'`` | not needed | - | Memory. |

## UITextRoller

The older roller (UIMenu::AddTextRoller). AddTextRoller has no callers, so it
is never created.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `UITextRoller::UITextRoller`, `UITextRoller::Init`, `UITextRoller::Action`, `UITextRoller::Cull`, `UITextRoller::Dec`, `UITextRoller::Inc`, `UITextRoller::EvalMouseXY`, `UITextRoller::SetString`, `UITextRoller::SetText`, `UITextRoller::SetValue`, `UITextRoller::Switch`, `UITextRoller::Update`, `UITextRoller::~UITextRoller`, ``UITextRoller::`scalar_deleting_destructor'`` | not needed | - | Unreachable (reachable 0): UIMenu::AddTextRoller has no callers. |

## UITextField

The single-line text entry (UIMenu::AddTextField: id, label, buffer, box,
maximum length 1..40, a second count 1..20, flags, font). Flags: 1 editable,
2 no label (else a label in the left 0.2, or above with 0x10), 4 no callback
per typed character, 8 Enter-only toggle mode, 0x20 "first key replaces"
re-armed after Enter, 0x40 letters and digits only, 0x80 no double-byte
characters, 0x100 no IME, 0x400 Escape calls the callback. The field text node
has effects 0x45 (vertically centred, white-pen rectangle round the node,
drawn even when empty) in every MenuManager; a Card2D behind it is opaque
black while editing. Reachable users: Dialog_NewPlayer (18 chars, 0x17),
PUChat (40 chars, 0x503: per-character callback, Escape calls back), NetArena
chat (0x27) and the DirectPlay dialogs (replaced). OpenMM2: `ui::TextEntry`
(`popup` for PUChat in `RaceScreen.cpp`).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `UITextField::UITextField`, `UITextField::Init` | ported | `ui/Widgets.cpp` `TextEntry::TextEntry`, `TextEntry::maxLength`, `popup` | Init empties the caller's buffer; " %s" format without a label; "first key replaces" armed at creation; capture mode off (string 605 "0"). The white outline is drawn in the menus too, but OpenMM2 draws it only in popup mode (finding A2.15). |
| `UITextField::Action`, `UITextField::CaptureAction`, `UITextField::KeyAction` | ported | `TextEntry::modalInput`, `TextEntry::activate`, `Menu::update` | Editable fields take every key event (mouse events are ignored): characters go to WmCharHandler; Backspace removes one (double-byte aware) character; Enter calls back, ends editing, re-arms with 0x20 and plays MenuManager::PlaySound(0); Tab ends editing (ScanGlobalKeys moves the focus); Escape calls back only with 0x400 (PUChat clears and closes); other keys only redraw. Enter-only mode (8) has no reachable user. |
| `UITextField::WmCharHandler`, `UITextField::IsValidChar` | ported | `TextEntry::modalInput` | First character replaces the text while armed; refuses control characters (C1_CNTRL), double-byte lead bytes without IME, anything past the maximum length; calls back per character unless flag 4 (PUChat's ChatEntry ignores those calls); plays MenuManager::PlaySound(0) per accepted character ("Moveselector", heard only in popups, i.e. PUChat: finding A2.14). Backspace does not disarm "first key replaces" in MM2 (OpenMM2 disarms it); no reachable field starts non-empty, so this never shows. |
| `UITextField::ToggleField`, `UITextField::Switch`, `UITextField::SetTextField` | ported | `TextEntry::draw`, `TextEntry::focusChanged` | Focus = editing: colour 3 and the black card on, colour 0 and the card off; label colour 3/0; IME association. |
| `UITextField::SetField` | ported | `TextEntry::value` | Sets the text (PUChat::ClearChat, NetSelectMenu). |
| `UITextField::SetCompositionWindow` | replaced | `platform` SDL text input (`Input::startTextInput`) | IME composition window placement. |
| `UITextField::ClearField`, `UITextField::SetText` | not needed | - | No callers (reachable 0). |
| `UITextField::Update` | ported | `TextEntry::draw` | Updates its nodes. |
| `UITextField::~UITextField`, ``UITextField::`scalar_deleting_destructor'`` | not needed | - | Memory. |

## VSWidget

The vertical scroll bar embedded in UICompositeScroll, UICWArray and
UITextScroll: scroll_uarr on top, a trough of N segments (scroll_inact,
scroll_act for the thumb; segment height = scroll_act height / 2, two bands:
unfocused and focused), scroll_darr at the bottom (4 frames: 0 unfocused, 1
focused, 2 focused and pressed; frame 3 is never used), 2 px gaps; N = (bar
height - 2 x arrow height) / segment height, 2..200; the thumb spans
max(1, whole part of N x visible/total - 1) segments; the data is the first
visible row. Arrows and the press/drag handling exist only when enabled (all
three users enable it). OpenMM2: `ui::ScrollBar` (newly ported), used by the race
records' `RecordList` and the customize list's `BindingList`; `ListBox` (OpenMM2's
session list) draws none.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `VSWidget::VSWidget`, `VSWidget::Init`, `VSWidget::LoadBitmap`, `VSWidget::CalcTroughRatio`, `VSWidget::SetStep`, `VSWidget::SetTrough`, `VSWidget::SetHotSpots` | ported (new) | `ui/Widgets.cpp` `ScrollBar::place`, `setRatio`, `metrics` | Segment = scroll_act's width and half height, arrow = scroll_darr's width and quarter height, the arrows centred over the trough, 2 px gaps; segments = (height - 2 arrows) / segment, 2..200; the thumb's extra segments = whole part of segments x ratio - 1, at least 1, at most segments - 1; the hot spots leave out the gaps (finding A2.16). |
| `VSWidget::Cull`, `VSWidget::Update`, `VSWidget::Switch` | ported (new) | `ScrollBar::draw` | Focused band and arrow frames (0 unfocused, 1 focused, 2 focused and pressed) follow the owning list's focus; frame 3 never shows. |
| `VSWidget::Action`, `VSWidget::EvalMouseXY` | ported (new) | `ScrollBar::mouse`, `release`; `RecordList::adjust`, `BindingList::barMouse` | Mouse press or drag (kind 0 or 2): up arrow Dec, down arrow Inc, trough moves the thumb step by step to the pointer (dragging captures the mouse through the owner); "Switch" (MenuManager::PlaySound(2)) on the press only; other mouse events clear the pressed arrows. Keys: Left Dec, Right Inc with "Switch". |
| `VSWidget::Inc`, `VSWidget::Dec`, `VSWidget::SyncData` | ported | `RaceRecordsDialog` `m_scroll` clamp; `BindingList::m_scroll` | One row per step, clamped, callback to the owner. |
| `VSWidget::~VSWidget`, ``VSWidget::`scalar_deleting_destructor'`` | not needed | - | Memory. |

## UICompositeScroll

The record list (UIMenu::AddCompScroll): rows of mmComp* components
(mmCompDRecord, mmCompRaceRecord, mmCompRoster...) drawn into a text node of
rows x 5 white columns, an optional VSWidget (InitVScroll places it at the
list's right edge + an x offset, as tall as the rows) and an optional
selection. Reachable users: Dialog_DriverRec (12 rows, no scroll bar, no
selection), Dialog_HallOfFame (11 rows of 0.0375, scroll bar at the list's
right edge less 0.0329 of the screen, i.e. x 546 px, from y 91, 198 px tall;
no selection), NetArena's roster (replaced lobby). Dialog_City2 and
Dialog_Replay also create one (their own groups). The list is a focus stop in
every case (enabled, writable). OpenMM2: `PagesMain.cpp`
`DriverStatsDialog::drawAbove`, `RaceRecordsDialog` (drawn rows, `m_scroll`,
arrow buttons, mouse wheel).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `UICompositeScroll::UICompositeScroll`, `UICompositeScroll::Init` (with its unwind piece at 0x5ac735), `UICompositeScroll::InitVScroll` | ported | `app/frontend/PagesMain.cpp` `DriverStatsDialog`, `RaceRecordsDialog` | Geometry verified by the first audit (rows from (81,87)/(81,91), 18 px); the scroll bar part is finding A2.16. The list's own Card2D is never added to the scene (not drawn). |
| `UICompositeScroll::AddComponent`, `UICompositeScroll::RemoveAllComponentChildren`, `UICompositeScroll::Clear`, `UICompositeScroll::Reset`, `UICompositeScroll::GetSelectedCount`, `UICompositeScroll::Redraw`, `UICompositeScroll::Update` | ported | `DriverStatsDialog::drawAbove`, `RaceRecordsDialog::drawAbove` (rows rebuilt every frame) | Rows from the scroll position, components without content skipped, unused rows blanked. |
| `UICompositeScroll::VScrollCB`, `UICompositeScroll::SetVScrollVals`, `UICompositeScroll::SetVScrollPos`, `UICompositeScroll::SetPosition` | ported | `RaceRecordsDialog::drawAbove` (`m_scroll` clamped to rows - 11) | Top row = the scroll bar's value, clamped to count - rows; the thumb size is rows / count. |
| `UICompositeScroll::Action` (with its split piece at 0x4ebcde, the key jump table) | ported (new) | `RecordList::adjust`, `page`, `mouse` | Only with a scroll bar (the driver record list ignores all input). The VSWidget gets the event first (arrows, trough, drag with mouse capture and MenuManager::PlaySound(1)); then a press on a row (left 0.9 of the width) plays "Switch" and calls the (null) callback; keys without a selection: Left/Right scroll one row (after VSWidget::Action already stepped once, so two rows per key press: read from the code, not observed), Page Up/Page Down one page. OpenMM2 does the same, plus the wheel (OpenMM2's); finding A2.16. |
| `UICompositeScroll::CaptureAction` | ported (new) | `RecordList::modalInput` | While dragging the scroll bar every mouse event goes to the VSWidget; the release ends the capture. |
| `UICompositeScroll::Switch` | ported (new) | `RecordList::draw` | Lights the scroll bar's focused band while the list has the focus. |
| `UICompositeScroll::GetHit` | ported (new) | `RecordList::mouse` | Row under the pointer (in reachable lists only used for the click sound). |
| `UICompositeScroll::SetHighlight`, `UICompositeScroll::SetHighlightComp` | not needed | - | Act only with a selection pointer; no reachable list has one. |
| `UICompositeScroll::AddTitle`, `UICompositeScroll::GetHeight` | not needed | - | No callers (reachable 0). |
| `UICompositeScroll::~UICompositeScroll`, ``UICompositeScroll::`scalar_deleting_destructor'`` | not needed | - | Memory. |

## UITextScroll

A list of text lines with a VSWidget (UIMenu::AddTextScroll): only
NetSelectMenu's DirectPlay session list uses it (a press on a line selects
it with MenuManager::PlaySound(1), "Switch" and the callback).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `UITextScroll::UITextScroll`, `UITextScroll::Init`, `UITextScroll::InitVScroll`, `UITextScroll::InitTextScroll`, `UITextScroll::ResetTextScroll`, `UITextScroll::AddTextScrollLine` (both overloads), `UITextScroll::FillScroll`, `UITextScroll::Recalc`, `UITextScroll::SetText`, `UITextScroll::SetTextColor`, `UITextScroll::GetHit`, `UITextScroll::Action`, `UITextScroll::Switch`, `UITextScroll::VScrollCB`, `UITextScroll::SetVScrollVals`, `UITextScroll::~UITextScroll`, ``UITextScroll::`scalar_deleting_destructor'`` | replaced | `ui/Widgets.cpp` `ListBox` (sessions page in `app/frontend/PagesMulti.cpp`) | The DirectPlay session list is replaced by OpenMM2's LAN session list. |
| `UITextScroll::GetCurrentString`, `UITextScroll::KeyAction`, `UITextScroll::SetHighlight`, `UITextScroll::SetVScrollPos` | not needed | - | No callers (reachable 0). |

## UIVScrollBar

A stand-alone scroll bar widget (UIMenu::AddVScrollBar). AddVScrollBar has no
callers, so it is never created; the reachable scroll bars are VSWidgets.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `UIVScrollBar::UIVScrollBar`, `UIVScrollBar::Init`, `UIVScrollBar::Action`, `UIVScrollBar::Cull`, `UIVScrollBar::Dec`, `UIVScrollBar::Inc`, `UIVScrollBar::Disable`, `UIVScrollBar::Enable`, `UIVScrollBar::EvalMouseXY`, `UIVScrollBar::LoadBitmap`, `UIVScrollBar::SetHotSpots`, `UIVScrollBar::SetPosition`, `UIVScrollBar::SetRange`, `UIVScrollBar::SetStep`, `UIVScrollBar::SetTrough`, `UIVScrollBar::Switch`, `UIVScrollBar::Update`, `UIVScrollBar::~UIVScrollBar`, ``UIVScrollBar::`scalar_deleting_destructor'`` | not needed | - | Unreachable (reachable 0): UIMenu::AddVScrollBar has no callers. |

## UICWArray

The CUSTOMIZE CONTROLS list ("CW Array", ControlCustom, menu 0x29): 34
UIControlWidget rows, one per mmInput slot, of which the slots the current
device can bind are listed, 15 at a time (ControlCustom passes 15 rows; the
row height is MenuManager +0xe8, the measured height of GetFont(16) text),
from (0.4687, 0.1) of the menu, 0.5 wide, the first row 0.005 below the top;
a VSWidget 0.015625 (10 px) right of the list, hidden while every row fits.
The constructor makes it read-only, so the keyboard focus never stops on it:
only the mouse focuses it. OpenMM2: `app/frontend/PagesOptions.cpp`
`BindingList` and `CustomizePage`, with `controls::CaptureReader` /
`controls::Rebinder` (`app/GameInput.cpp`) capturing keys, mouse buttons and
joystick controls (current worktree; the first audit still recorded
keyboard-only bindings).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `UICWArray::UICWArray`, `UICWArray::Init` (with its unwind piece at 0x5ac9f5) | ported | `BindingList::BindingList`, `CustomizePage::CustomizePage` | 15 rows verified by the first audit; OpenMM2 makes the list keyboard-focusable and uses 20 px rows (finding A2.17); the page opens on DEFAULTS as MM2's does. |
| `UICWArray::Redraw`, `UICWArray::Update` | ported | `BindingList::draw`, `BindingList::listActions` (`controls::slotListed`), `CaptureReader::poll` (`m_waitRelease`) | Lists the bindable slots from the scroll offset; re-lists when the input device changes; refreshes the captured row; while capturing, checks the capture only once the mouse button that started it is up (+0xac), as `CaptureReader` does. |
| `UICWArray::SetVScrollVals`, `UICWArray::SetVScrollPos`, `UICWArray::VScrollCB`, `UICWArray::Reset` | ported | `BindingList::placeBar`, `barMouse`, `m_scroll` | Thumb = rows / count; the bar disappears when everything fits; `BindingList::placeBar` keeps the bar on the selection's first row (finding A2.16). |
| `UICWArray::Action` | ported | `BindingList::mouse`, `BindingList::modalInput` | The VSWidget first (arrows, trough, drag with capture). A press on a row (left of the scroll bar) while not capturing starts a capture of that row (EnterCapture) and waits for the button's release. Keys (only once the mouse has focused the list): Enter calls ControlCustom::BadAssignCB with the last capture result (an MM2 quirk that can open a bad-assignment dialog), Left/Right scroll (twice per press, as in UICompositeScroll). OpenMM2: Up/Down select a row, Enter or a click starts the capture, the wheel scrolls (finding A2.17). |
| `UICWArray::CaptureAction` | ported | `BindingList::modalInput` | A release ends a scroll-bar drag; other events drive the VSWidget. |
| `UICWArray::EnterCapture`, `UICWArray::CheckCapture`, `UICWArray::ResetCapture`, `UICWArray::ForceCapture` | ported | `BindingList::startCapture`, `BindingList::capture` (`Rebinder::capture`, `forceAssign`); `CustomizePage` `onRefused`/`onDuplicate` | mmInput::CaptureState(1) captures any device; Escape (0x10001) cancels; F1-F10 refused (callback, xasn_dlg); mmInput::BuildCaptureIO: 1 accepted, 2 duplicate / 0 invalid go to ControlCustom::BadAssignCB (ctrl_dlg; OK = ForceCapture with mmInput::ForceAssignment, CANCEL = ResetCapture). Same in OpenMM2. |
| `UICWArray::AcceptCapture` | ported | `BindingList::capture` (`m_swallow`), `BindingList::modal` | Shows the new binding, ends the capture, then sfPointer::WaitForRelease (pointer state 3): until the mouse button is released the pointer makes no press, release or drag events, so a mouse button just bound as a control neither clicks the menu nor clicks on its release. OpenMM2 swallows one frame after the capture and stays modal while the list keeps the keyboard, which covers the press; but once the pointer moves off the list (`m_active = false`) a release over another widget is a click (`SpriteButton::mouse` acts on any release over it): binding the left mouse button and releasing it over DONE or CANCEL would press that button. Edge case (inferred from the code, not tried). |
| `UICWArray::DefaultCFG` | ported | `CustomizePage::resetDefaults` | mmInput::SetDefaultConfig for the 5 devices, then redraw. |
| `UICWArray::Switch` | ported | `BindingList::draw` | Only lights the scroll bar's focused band. |
| `UICWArray::LoadCFG`, `UICWArray::SaveCFG`, `UICWArray::SetStartOffset`, `UICWArray::DebugForceSetting` | not needed | - | No callers (reachable 0): mminput.cfg load/save and debug. |
| `UICWArray::~UICWArray`, ``UICWArray::`scalar_deleting_destructor'`` | not needed | - | Memory. |

## UIControlWidget

One row of the UICWArray: the action's name (white, left half, vertically
centred) and its binding (mmIO::GetDescription, white, centred in the right
half; red while capturing), both in GetFont(MenuManager +0xdc = 16). Its two
Card2Ds are never added to the scene. It is not a menu widget (the UICWArray
owns it), so the menu never calls its Switch or Action; while capturing it is
MenuManager's capture widget, whose (empty) CaptureAction swallows keys.
OpenMM2: `BindingList::draw`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `UIControlWidget::UIControlWidget`, `UIControlWidget::Init`, `UIControlWidget::SetPosition`, `UIControlWidget::TurnOn`, `UIControlWidget::TurnOff` | ported | `BindingList::draw` | The binding is centred in the right half, as OpenMM2 draws it now (finding A2.18). |
| `UIControlWidget::Update`, `UIControlWidget::UpdateField` | ported | `BindingList::draw` (`binding()` each frame) | |
| `UIControlWidget::EnableField`, `UIControlWidget::DisableField` | ported | `BindingList::draw` (`kValueTextFocus` while capturing) | Red while capturing, with the binding half as the capture hot spot; DisableField flushes the event queue. OpenMM2 also shows the selected row's key red while the list has the focus (no such state in MM2: finding A2.17). |
| `UIControlWidget::Action`, `UIControlWidget::Switch` | not needed | - | Never called: the rows are not menu widgets (Action would call a null callback; Switch would colour the binding). |
| `UIControlWidget::~UIControlWidget`, ``UIControlWidget::`scalar_deleting_destructor'``, ``UIControlWidget::`vector_deleting_destructor'`` | not needed | - | Memory. |

## mmToggle

A check-box picture node used by UIToggleButton and UIIconW, which are never
created.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmToggle::mmToggle`, `mmToggle::Init`, `mmToggle::LoadBitmap`, `mmToggle::SetSize`, `mmToggle::Cull`, `mmToggle::Update`, `mmToggle::~mmToggle`, ``mmToggle::`scalar_deleting_destructor'`` | not needed | - | Unreachable: its only owners (UIToggleButton, UIIconW) are never constructed. |

## mmToggle2

The onoff_toggle / onoff_radio lamp node of UIBMButton's companion mode and
of UIMexButton. UIBMButton never enables that mode (+0xa0 stays 0) and
UIMexButton is never created, so no mmToggle2 exists.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmToggle2::mmToggle2`, `mmToggle2::Init`, `mmToggle2::LoadBitmap`, `mmToggle2::SetPosition`, `mmToggle2::Cull`, `mmToggle2::Update`, `mmToggle2::~mmToggle2`, ``mmToggle2::`scalar_deleting_destructor'`` | not needed | - | Unreachable: never constructed (UIBMButton::Init's branch is dead, UIMexButton unreachable). |

## AboutMenu

The About page, menu 0x22 (34, `about_bk`), created in `mmInterface::mmInterface`, entered from the options menu
(`mmInterface::Update` options id 0x67 → `Switch(0x22)`, which disables the navigation strip). Widgets: a read-only
"Credits" hotspot (index 0), DONE `opt_done` (index 1), the product-ID label (index 2, font 20) filled from the
registry value "PID" with string 325 "UNKNOWN" as default. The credits bitmap scrolls in the hotspot's box. OpenMM2:
`app/frontend/PagesOptions.cpp` `AboutPage`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `AboutMenu::AboutMenu` | ported | `PagesOptions.cpp` `AboutPage::AboutPage` | credits box (widget 0), DONE (1), PID label (2, string 325; no registry read, documented deviation). MM2 sets no focus widget, so the page opens on the read-only credits hotspot and DONE is not lit (finding B.11). |
| `AboutMenu::PreSetup`, `AboutMenu::Update`, `AboutMenu::Cull` | ported | `AboutPage::update`, `AboutPage::drawCredits` | PreSetup resets the offset and stores `datTimeManager::ElapsedTime`; Update, after 1.5 s, sets the offset to the whole part of (elapsed − 1.5) × 50 + 0.5 modulo the bitmap height (constants 1.5, 50.0, 0.5 read from the asm); Cull copies one band or two (wrap). OpenMM2 accumulates its own frame `dt`, which `platform::FrameClock::tick` clamps to 0.1 s, while MM2's ElapsedTime is the sum of unclamped frame times (see finding B.12). The page is re-created on every entry, which matches PreSetup's reset. |
| `AboutMenu::~AboutMenu`, ``AboutMenu::`scalar_deleting_destructor'`` | not needed | — | destructors. |

## OptionsMenu

The options menu, menu 2 (`opt_bk`), created in `mmInterface::mmInterface`. Buttons ABOUT `opt_abt` (id 0x67), AUDIO
`opt_aud` (100), CONTROLS `opt_ctl` (0x65), GRAPHICS `opt_gfx` (0x66), then a turned-off `mnav_prev` hotspot whose
position (widget.csv 290,415) PreSetup gives the strip's PREV (`uiNavBar::SetPrevPos`; PostSetup puts it back to 0,0);
help label list `opt_tabt|opt_taud|opt_tctl|opt_tgfx`; initial focus index 0 (ABOUT). OpenMM2: `PagesOptions.cpp`
`OptionsPage`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `OptionsMenu::OptionsMenu` | ported | `PagesOptions.cpp` `OptionsPage::OptionsPage` | order, ids → pages (`mmInterface::Update` case 2: 100 → 3, 0x65 → 5, 0x66 → 4, 0x67 → 0x22), help pictures, ABOUT first; PREV via `addBack` at the strip's widget 4 position, the same 290,415. |
| `OptionsMenu::PreSetup`, `OptionsMenu::PostSetup` | ported | `addBack`, `addNavStrip(NavOptions::Lit)` | PREV placement as above. PreSetup also stores the menu's previous menu in `GraphicsPreviousMenu` unless `GraphicsChange` is set; `GraphicsChange` is never set to 1 anywhere in build 3393 (only `ShowMain` clears it), so that part is dead (not needed). |
| `OptionsMenu::FocusDescription` | ported | `ui::Menu` help pictures (`SpriteButton::help`) | index → picture, label hidden when nothing is focused. |
| `OptionsMenu::~OptionsMenu`, ``OptionsMenu::`scalar_deleting_destructor'`` | not needed | — | destructors. |

## OptionsBase

Base of the four option sub-pages (Audio 3, Graphics 4, Control 5, Customize 0x29): creates DEFAULTS `opt_def`
(id 0x1f6), CANCEL `opt_can` (id 500, callback = the page's CancelAction) and DONE `opt_dn` (id 0x1f5, callback =
DoneAction) as widgets 0-2, and holds a page-local `mmPlayerConfig` snapshot. `mmInterface::Update` does the rest
(DEFAULTS opens dialog 0x1f; CANCEL/Escape re-apply mmInterface's saved config; DONE copies the globals into it and
saves the driver's `.cfg`). OpenMM2: `PagesOptions.cpp` `SettingsPage`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `OptionsBase::OptionsBase` | ported | `PagesOptions.cpp` `SettingsPage::SettingsPage` | widgets 0-2 at their widget.csv places; DEFAULTS asks (odef_dlg), CANCEL/Escape restore, DONE saves (global `openmm2.ini`, documented deviation from per-driver `.cfg`). |
| `OptionsBase::IsAnOptionMenu` | ported | `SettingsPage::finish` (`NavOptions::Cancel`) | tells `mmInterface::Update` that the strip's OPTIONS must run the page's CancelAction; see findings B.1 and B.2 for what that does per page. |
| `OptionsBase::ResetDefaultAction`, `OptionsBase::StoreCurrentSetup` | ported | `SettingsPage::resetDefaults` (pure virtual), snapshot in `SettingsPage::SettingsPage` | empty base virtuals. |
| `OptionsBase::CreateTitle` | not needed | — | unreachable (no callers): a title label at 0.3, 0.005. |
| `OptionsBase::~OptionsBase`, ``OptionsBase::`scalar_deleting_destructor'`` | not needed | — | destructors. |

## GraphicsOptions

The graphics page, menu 4 (`gfx_bk`). Widgets (after OptionsBase's three): toggles `gfx_sky` (bound to
`gfxEnableSky`), `gfx_rflx` (`gfxUseEnvMap`), `gfx_peds` (bound to a page flag that is the inverse of
`pedestriansEnabled`, callback `TogglePeds`), `gfx_port` (`gfxUsePortals`, turned off at once); drop-downs DISPLAY
(adapters, callback rebuilds the renderer list, piece 0x4f4f30), 3D DEVICE (strings 394-396 by adapter capability),
RESOLUTION (callback `SetResolution`; `SetFocusWidget` right after it); sliders FAR CLIP 100-1000 and LIGHTING QUALITY
0-3; drop-downs TEXTURE (390-393), OBJECT DETAIL, CLOUD SHADOWS; help list `gfx_tsky … gfx_shd`. OpenMM2:
`PagesOptions.cpp` `GraphicsPage`, with the adapter/renderer/mode boxes replaced by window mode, Vulkan/OpenGL and size.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `GraphicsOptions::GraphicsOptions` (with piece 0x4f4f30) | ported | `PagesOptions.cpp` `GraphicsPage::GraphicsPage` | widget order, toggles, sliders, help pictures and RESOLUTION focus verified (first audit). Piece 0x4f4f30 is the DISPLAY callback: renderer list "Software only" (394), "+ Hardware" (395), "+ T&L" (396) by the adapter's renderer count, then `SetupResChoices`; replaced by OpenMM2's Automatic/Vulkan/OpenGL list. |
| `GraphicsOptions::SetResolution` (= piece 0x4f4b60) | ported | `GraphicsPage::GraphicsPage` (TEXTURE QUALITY list) | the RESOLUTION callback only rebuilds the TEXTURE QUALITY list: strings 390-393 with " - Recommended" (389) after the level `AutoDetect` stored; nothing else changes. OpenMM2 builds the list once with the marker on High (AutoDetect's top tier, documented deviation). The resolution choice itself is replaced (`GraphicsPage::setResolution`). |
| `GraphicsOptions::SetRenderer` | not needed | — | empty. |
| `GraphicsOptions::TogglePeds` | ported | `GraphicsPage` toggle `ShowPedestrians`; `app/RaceScreen.cpp` `loadAi` | the lamp is lit when the global is 0; `aiMap::Init` creates no pedestrians when mmStatePack +0x64 (`pedestriansEnabled`, despite its name a "pedestrians off" flag) is non-zero; AutoDetect clears it on a ≥300 MHz hardware machine. OpenMM2's lamp-lit = pedestrians matches. |
| `GraphicsOptions::SetLightQuality` | ported | `GraphicsPage` lighting slider setter | snapping verified by the first audit. |
| `GraphicsOptions::PreSetup` | ported | `SettingsPage::SettingsPage` snapshot, widgets reading `[Graphics]` | hides the help label, snapshots the config, reloads the gfx settings and sets the drop-downs. |
| `GraphicsOptions::ResetDefaultAction` | ported | `GraphicsPage::resetDefaults` | `AutoDetect(-1)` tier; OpenMM2 always the top tier (documented deviation). |
| `GraphicsOptions::CancelAction` | ported | `GraphicsPage::stripOptions` | empty in MM2: CANCEL/Escape restore through `mmInterface::Update`'s `SetGraphics`, but the strip's OPTIONS calls only this, so the changes stay (finding B.1). |
| `GraphicsOptions::DoneAction` | replaced | `GraphicsPage::done` | stores the adapter/renderer/mode choice, `gfxSaveSettings`, sets `NeedFullShutdown`: the display change happens when the frontend next ends a phase (`EndPhase`/`BeginPhase` re-create DirectDraw, i.e. on the way into the next race); the frontend itself stays 640x480. OpenMM2 applies display changes at once (renderer changes after a restart). |
| `GraphicsOptions::FocusDescription` | ported | `ui::Menu` help pictures | indices 0-10 = `gfx_tsky, gfx_tfl, gfx_tped, gfx_tdis, gfx_tren, gfx_tres, gfx_tfp, gfx_tlq, gfx_ttq, gfx_tobj, gfx_shd`, matching OpenMM2's assignments. |
| `GraphicsOptions::~GraphicsOptions`, ``GraphicsOptions::`scalar_deleting_destructor'`` | not needed | — | destructors. |

## mmGfxCFG

The graphics part of `mmPlayerConfig` (per driver): `Get` copies the globals (texture quality, object detail, cloud
shadows, env map, light quality, sky, far clip, portals, object-detail multiplier, `pedestriansEnabled`, two words
nothing else reads) into the config, `Set` applies them. OpenMM2 keeps these as `[Graphics]` keys of `openmm2.ini`
for all drivers (documented deviation).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmGfxCFG::mmGfxCFG`, `mmGfxCFG::Get`, `mmGfxCFG::Set` | replaced | `app/Settings.*`, `[Graphics]` keys read by `RaceScreen` | the constructor's defaults (sky on, far clip 600, light 1.0, multiplier 1.0) are overwritten by AutoDetect/the driver's file before use. The object-detail multiplier is derived from OBJECT DETAIL elsewhere (rendering area). |

## ControlSetup

The control page, menu 5 (`ctrl_bk`). `CreateDeviceOptions` adds AUTO REVERSE, POV, FORCE FEEDBACK (callback
`SetFFPermissions`), CONTROLLERS (string 331, callback `ControlSelect`), STEERING SENSITIVITY (0.5-2, callback
`SetSensitivityCB`), DEAD ZONE (0-0.33), CALIBRATE (`LaunchJoyCpl`), the two force-feedback intensities (0-2) and
CUSTOMIZE (id 0x3e9); the constructor greys the joystick types without a device (mask −4), inits sensitivity and the
custom controls, and creates an `mmMouseSteerBar` (piece 0x534420 is its constructor) that is never attached to the
menu, so it is never drawn. Initial focus index 0 (DEFAULTS). OpenMM2: `PagesOptions.cpp` `ControlPage`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `ControlSetup::ControlSetup` (with piece 0x534420), `ControlSetup::CreateDeviceOptions` | ported | `PagesOptions.cpp` `ControlPage::ControlPage` | widgets, ranges, joystick types greyed without a device, help list verified (first audit). The steering bar (`mouse_bar`/`mouse_ar` at 0.1, 0.85) is created and deleted but never added as a child node: not drawn, not needed. |
| `ControlSetup::ActivateDeviceOptions` (jump table 0x502342), `ControlSetup::DeactivateAllDeviceOptions`, `ControlSetup::SetControlPosition`, `ControlSetup::InitCustomControls`, `ControlSetup::SetFFPermissions` | ported | `ControlPage::update` | everything disabled, then per type: mouse/game pad AUTO REVERSE + sensitivity; keyboard AUTO REVERSE; joystick also POV, CALIBRATE, dead zone, FORCE FEEDBACK; wheel like joystick without POV; intensities with a force-feedback device. `InitCustomControls` then enables POV only for a joystick with a hat and FORCE FEEDBACK + intensities whenever a FF device exists; `SetFFPermissions` ties the intensities to FF on. `SetControlPosition` enables a widget and returns its height + 0.005 (the positions are widget.csv's). Small difference after a controller change (finding B.13). |
| `ControlSetup::ControlSelect` | ported | `ControlPage` CONTROLLERS setter, `ControlPage::update` | `mmInput::Init` with the new device, resets and redraws the customize list, re-activates the widgets. OpenMM2 writes `[Controls] Controller`; the race reads it. |
| `ControlSetup::SetSensitivityCB` | ported | `ControlPage` sensitivity slider → `[Controls] Sensitivity` | `ControlBase::SetSensitivity` (ControlBase skipped, done elsewhere). |
| `ControlSetup::PreSetup`, `ControlSetup::StoreCurrentSetup` | ported | `SettingsPage::SettingsPage` snapshot, `ControlPage::update` | PreSetup snapshots the controls (StoreCurrentSetup = `GetControls` into the page config) and re-activates widgets on every entry, also when coming back from Customize; OpenMM2 keeps the page's construction-time snapshot but `ControlPage::cancel` keeps bindings saved by Customize, which gives the same result. |
| `ControlSetup::CancelAction` | ported | `ControlPage::cancel` | `SetControls` from the snapshot. |
| `ControlSetup::DoneAction` | ported | `SettingsPage::done` | empty (mmInterface saves). |
| `ControlSetup::ResetDefaultAction` | ported | `ControlPage::resetDefaults` | `DefaultControls`, `SetControls`, `mmInput::AutoSetup`, `ControlSelect`, `SetFFPermissions`; OpenMM2 picks the keyboard (first audit, inferred). |
| `ControlSetup::LaunchJoyCpl` (pieces 0x5025db, 0x50260c) | replaced | `ControlPage` CALIBRATE → message | terminates a running panel, starts the `CalibrateWatcher` thread (runs the joystick control panel, waits, restores the window), minimises; the pieces pump events and re-activate the window. OpenMM2 leaves calibration to the OS (documented). |
| `ControlSetup::Update` | not needed | — | feeds the current steering value to the steering bar that is never drawn, then `UIMenu::Update`. |
| `ControlSetup::FocusDescription` | ported | `ui::Menu` help pictures | list `ctl_trev, ctl_tpov, ctl_tffb, ctl_tcal, ctl_tcus, ctl_titn, ctl_trf, ctl_tcon, ctl_tss, ctl_dd`; the dead-zone slider passes index 9, so `ctl_dd` is MM2's own choice (OpenMM2's comment calls it inferred; see Comment citations). |
| `ControlSetup::POVCB` | not needed | — | unreachable (no callers), empty. |
| `ControlSetup::~ControlSetup`, ``ControlSetup::`scalar_deleting_destructor'`` | not needed | — | destructors (also delete the unused steering bar). |

## ControlCustom

The customize page, menu 0x29 (41, `cuss_bk`): OptionsBase's three buttons, then the `UICWArray` (15 rows, callback
`BadAssignCB`). No focus widget is set, so the page opens on DEFAULTS. A refused key sets the bad-assignment flag to 2
(dialog 0x20 `xasn_dlg`), a key in use to 1 (dialog 0x15 `ctrl_dlg`); `mmInterface::Update` opens them. OpenMM2:
`PagesOptions.cpp` `CustomizePage`, `BindingList`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `ControlCustom::ControlCustom` | ported | `PagesOptions.cpp` `CustomizePage::CustomizePage`, `BindingList` | list at widget 3, 15 rows (first audit; keyboard only, documented). Opens on DEFAULTS, as MM2's (finding B.3). |
| `ControlCustom::BadAssignCB` | ported | `BindingList::capture` → `onRefused` / `onDuplicate` | capture status 0 → refused, 2 → duplicate; also clears the focus picture. |
| `ControlCustom::VerifyBadAssignment`, `ControlCustom::CancelBadAssignment`, `ControlCustom::ClearBadAssignment` | ported | `CustomizePage` `onDuplicate` OK / Cancel lambdas; `BindingList::capture` ends the capture | OK forces the capture (key assigned, other action unbound), Cancel/Escape resets it. |
| `ControlCustom::ResetDefaultAction` | ported | `CustomizePage::resetDefaults` | `UICWArray::DefaultCFG` + redraw. |
| `ControlCustom::CancelAction` | ported | `SettingsPage::restore`, `CustomizePage::stripOptions` | `SetCustom` from the snapshot, redraw. From the strip's OPTIONS MM2 then goes to the options menu, not to Control (finding B.2). |
| `ControlCustom::DoneAction` | ported | `SettingsPage::done` | empty (mmInterface's `GetCustom` + save, then back to Control). |
| `ControlCustom::~ControlCustom`, ``ControlCustom::`scalar_deleting_destructor'`` | not needed | — | destructors. |

## Dialog_ControlAssign

`ctrl_dlg`, dialog 0x15 (21, "Control Warning"), a PUMenuBase with OK `dlg_ok` (id 100) and CANCEL `dlg_can` (0x65);
widget.csv puts them at 180,176 and 18,176. Opened by `mmInterface::Update` when the customize list reports a key in
use. OpenMM2: the `onDuplicate` dialog in `CustomizePage`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `Dialog_ControlAssign::Dialog_ControlAssign`, `Dialog_ControlAssign::PreSetup` | ported | `PagesOptions.cpp` `CustomizePage::CustomizePage` (`list.onDuplicate`, `Frontend::dialog`) | sprites dlg_ok / dlg_can, positions and actions match (`mmInterface::Update` case 0x15: 100 → `VerifyBadAssignment`, 0x65 or Escape → `CancelBadAssignment`). PreSetup is empty. |
| `Dialog_ControlAssign::~Dialog_ControlAssign`, ``Dialog_ControlAssign::`scalar_deleting_destructor'`` | not needed | — | destructors. |

## Dialog_Message

MM2's picture dialog: a PUMenuBase with `dlg_done` (id 100) and `dlg_can` (id 0x65); `Init(type, first, second)`
replaces the button bitmaps and, for type 100, kills the second button. Instances made by `mmInterface::mmInterface`:
rtrv 0x12 (one button, dlg_can), lock 0x17 (dlg_ok), badp 0x18 (dlg_ok), quit 0x1b (dlg_ok + dlg_can), delp 0x1c
(dlg_yes + dlg_no), dupp 0x1d (dlg_ok), xasn 0x20 (dlg_ok), plim 0x21 (dlg_ok), lstp 0x1e (dlg_ok), zone 0x1a
(dlg_can), odef 0x1f (dlg_ok + dlg_can), boot 0x2b (dlg_ok). OpenMM2: `FrontendScreen.cpp` `PictureDialog`,
`Frontend::dialog`, `notice`, `askQuit`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `Dialog_Message::Dialog_Message`, `Dialog_Message::Init`, `Dialog_Message::PreSetup` | ported | `FrontendScreen.cpp` `PictureDialog::PictureDialog`, `Frontend::dialog`, `Frontend::notice`, `Frontend::askQuit` | buttons per instance as listed match OpenMM2's calls, `xasn_dlg` with its OK too (finding B.4). PreSetup is empty. |
| `Dialog_Message::~Dialog_Message`, ``Dialog_Message::`scalar_deleting_destructor'`` | not needed | — | destructors. |

## Dialog_NewPlayer

"Create a New Driver", dialog 0x11 (17, `newp_dlg`): name text field (id 200, 18 characters, callback
`EnterNewPlayer`), two `checkbox` radio buttons bound to mmStatePack +0x5C (the global difficulty: 0 amateur,
1 professional), the hidden `ama_rank_desc|pro_rank_desc` label, DONE `dlg_done` (0xc9, `EnterNewPlayer`) and CANCEL
`dlg_can` (0xca). PreSetup sets the global difficulty to amateur and clears the name. `EnterNewPlayer` calls the
callback mmInterface set (`PlayerCreateCB`). OpenMM2: `PagesMain.cpp` `NewDriverDialog`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `Dialog_NewPlayer::Dialog_NewPlayer`, `Dialog_NewPlayer::PreSetup` | ported | `PagesMain.cpp` `NewDriverDialog::NewDriverDialog` | widgets verified (first audit); a new dialog each time gives PreSetup's empty name and amateur. OpenMM2 keeps the choice in the dialog; MM2 writes the global difficulty directly, which a Cancel leaves changed (finding B.8). |
| `Dialog_NewPlayer::EnterNewPlayer` | ported | `NewDriverDialog::create` (DONE and the field's `onCommit`) | |
| `Dialog_NewPlayer::~Dialog_NewPlayer`, ``Dialog_NewPlayer::`scalar_deleting_destructor'`` | not needed | — | destructors. |

## MainMenu

The main menu, menu 1 (`main_bk`): CRASH COURSE `dvrcc` (id 100, the focus widget), RACES `main_sp` (0x65),
MULTIPLAYER `main_mp` (0x66), QUICK RACE `main_qck` (0x67), then `InitDriver`'s DRIVER drop-down (id 0x68,
`dropdown_bx`, callback `TDPickCB`) with `roller_up`/`roller_down` (`DecPlayer`/`IncPlayer`), the driver text node
(10 lines from 0.34375 of the screen, labels 635-638 and 640), CREATE `DVRNEW` (0x69), DELETE `DVRDEL` (0x6a), STATS
`DVRSTS` (0x6b); then REPLAY `main_rpl` (0x6c, turned off at once), RACE RECORDS `race_rec` (0x6d) and the help
label (`mn_cc|mn_sp|mn_mp|mn_qck|mn_new|mn_del|mn_sts|mn_rpl|mn_rec|mn_drv`). OpenMM2: `PagesMain.cpp` `DriverPage`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `MainMenu::MainMenu`, `MainMenu::InitDriver`, `MainMenu::PreSetup` | ported | `PagesMain.cpp` `DriverPage::DriverPage` | order, ids, positions, CRASH COURSE focus, REPLAY created and hidden (first audit). PreSetup hides the help label. |
| `MainMenu::EnableReplay`, `MainMenu::IsReplayReadOnly` | not needed | — | `EnableReplay` has no callers; it would only toggle read-only, never `TurnOn`. The button stays off (`uiWidget::TurnOff` clears its enabled flag) and nothing else references it, so `mmInterface::Update`'s id 0x6c case (the only `IsReplayReadOnly` call) cannot run. |
| `MainMenu::DisplayDriverInfo`, `MainMenu::SetController`, `MainMenu::SetNetName` | ported | `DriverPage::drawAbove` | lines 1/3/5/7 = rank, last race, last vehicle, controller; 8/9 = "SCORE:" (640) and the score only when it is ≥ 0 (professionals), else blank. `SetNetName` is empty, so the net name (string 639) is never shown, as in OpenMM2. |
| `MainMenu::AddPlayer`, `MainMenu::RemovePlayer`, `MainMenu::RemoveAllPlayers`, `MainMenu::SetPlayerPick` | ported | `DriverPage::refresh`, the DRIVER `ValueBox` | the '|'-separated name list in directory order; the pick follows the loaded driver. |
| `MainMenu::DecPlayer`, `MainMenu::IncPlayer`, `MainMenu::TDPickCB` | ported | roller arrows (`ui::stepOption(..., true)`), DRIVER setter → `Frontend::selectProfile` | wrap, then `PlayerLoadCB`. |
| `MainMenu::DeleteCB` | ported | `DriverPage::askDelete` (YES) | calls `PlayerRemoveCB`. |
| `MainMenu::FocusDescription` | ported | `SpriteButton::help`, `ValueBox::help` | indices as in the list above. |
| `MainMenu::EnterNewPlayer` | not needed | — | unreachable (no callers). |
| `MainMenu::~MainMenu`, ``MainMenu::`scalar_deleting_destructor'`` (pieces 0x506af0..0x506bb0) | not needed | — | destructors. |

## CrashCourseIntro

The school selection, menu 0x28 (40, `ilon_bk`): London `cci_lon` (id 0), San Francisco `cci_sf` (id 1) and a
turned-off `mnav_prev` hotspot; PreSetup sets the event to the crash course, race 0. `mmInterface::Update` case 0x28
does the city switch. OpenMM2: `PagesCrash.cpp` `CrashIntroPage`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `CrashCourseIntro::CrashCourseIntro`, `CrashCourseIntro::PreSetup`, `CrashCourseIntro::PostSetup` | ported | `PagesCrash.cpp` `CrashIntroPage::CrashIntroPage`, `onEnter` | PostSetup is empty. |
| `CrashCourseIntro::~CrashCourseIntro`, ``CrashCourseIntro::`scalar_deleting_destructor'`` | not needed | — | destructors. |

## ccStatus

One tick/cross of the Crash Course curriculum (a child node of `CrashCourse`): `LoadBitmap(name, xPass, xFail, y)`
stores the bitmap (three frames), `SetStatus` 1 draws frame 1 at the pass column, 2 frame 2 at the fail column, 0
nothing. OpenMM2: `PagesCrash.cpp` `CrashCoursePage::drawAbove`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `ccStatus::ccStatus`, `ccStatus::LoadBitmap`, `ccStatus::SetStatus`, `ccStatus::Update`, `ccStatus::Cull` | ported | `PagesCrash.cpp` `CrashCoursePage::drawAbove` | `cc_smchk`, x 179 / 225, row tops verified by the first audit; status from `mmInterface::PlayerFillCrashRecords`. |
| `ccStatus::~ccStatus`, ``ccStatus::`scalar_deleting_destructor'`` | not needed | — | destructors. |

## Dialog_Replay

Replay selection, dialog 0x25 (37, `rpl_bk`): city list, LOAD/EDIT/DELETE/CANCEL. Constructed at startup by
`mmInterface::mmInterface`, but opened only by `mmInterface::Update`'s main-menu id 0x6c (REPLAY) and by the
replay-description dialog 0x26, which is itself opened only from 0x25. REPLAY is turned off and never turned on (see
MainMenu), so the dialog is never shown. OpenMM2 has no replays.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `Dialog_Replay::Dialog_Replay`, `Dialog_Replay::PreSetup`, `Dialog_Replay::PostSetup`, `Dialog_Replay::LoadAll`, `Dialog_Replay::ScrollCB`, `Dialog_Replay::SetCurrentReplay`, `Dialog_Replay::GetSelectedReplay`, `Dialog_Replay::SetDescription`, `Dialog_Replay::SetDriverStats`, `Dialog_Replay::DoneCB`, `Dialog_Replay::EditCB`, `Dialog_Replay::DeleteCB` (piece 0x4fa9d8), `Dialog_Replay::CancelCB` | not needed | — | never opened (above). The constructor runs at startup but only builds widgets. |
| `Dialog_Replay::GetDescription` | not needed | — | unreachable (no callers). |
| `Dialog_Replay::~Dialog_Replay`, ``Dialog_Replay::`scalar_deleting_destructor'`` | not needed | — | destructors. |

## Dialog_ReplayEdit

Replay description, dialog 0x26 (38, `redit_bk`), opened only by Dialog_Replay's EDIT (`mmInterface::Update` case
0x25 id 0x66). Never shown.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `Dialog_ReplayEdit::Dialog_ReplayEdit`, `Dialog_ReplayEdit::PreSetup`, `Dialog_ReplayEdit::ReplayDescCallback`, `Dialog_ReplayEdit::SetDesc` | not needed | — | only reachable from the replay dialog. |
| `Dialog_ReplayEdit::~Dialog_ReplayEdit`, ``Dialog_ReplayEdit::`scalar_deleting_destructor'`` | not needed | — | destructors. |

## mmCompReplay

One row of the replay list (an `mmCompBase`), made only by `Dialog_Replay`'s constructor and its load callback
(`LoadDlgReplayCB`). Never shown.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmCompReplay::mmCompReplay`, `mmCompReplay::Init`, `mmCompReplay::Reset`, `mmCompReplay::SetGeometry`, `mmCompReplay::SetSubwidgetGeometry`, `mmCompReplay::SetPosition`, `mmCompReplay::Box`, `mmCompReplay::Highlight`, `mmCompReplay::Update`, `mmCompReplay::Cull` | not needed | — | only used by the replay dialog. |
| `mmCompReplay::InitTitle`, `mmCompReplay::SetTitleGeometry` | not needed | — | unreachable (no callers). |
| `mmCompReplay::~mmCompReplay`, ``mmCompReplay::`scalar_deleting_destructor'``, ``mmCompReplay::`vector_deleting_destructor'`` | not needed | — | destructors. |

## mmInterface

MM2's frontend controller (an `asNode` child of ROOT while the menus run). The constructor builds every menu and
dialog (main 1, about 0x22, garage 8, showcase 9, race 7, net select 10, crash course 0x27, intro 0x28, options 2,
graphics 4, audio 3, customize 0x29, control 5, host race 0xb, lobby 0xc, and dialogs 0xe, 0x10-0x14, 0x15, 0x16,
0x17, 0x18, 0x19, 0x1a-0x21, 0x23, 0x24, 0x25, 0x26, 0x2a, 0x2b), wires their callbacks, loads the drivers
(`InitPlayerInfo`) and starts the menu music. `Update` is one switch over the current menu id, reading the menu's
state (1 = Escape, 4 = a widget acted) and widget id; `Switch` does the per-menu side effects before
`MenuManager::Switch`. Driver handling (`Player*`), the records (`HOF*`, `PlayerFillRecords`) and the DirectPlay
session glue also live here. OpenMM2 spreads this over `app/frontend/FrontendScreen.cpp` (`Frontend`,
`FrontendScreen`) and the pages' callbacks in `PagesMain.cpp`, `PagesRace.cpp`, `PagesCrash.cpp`, `PagesOptions.cpp`,
`PagesResults.cpp` and `PagesMulti.cpp`; the network part is replaced by `game/net` and `net/` (group D audited the
lobby menus).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmInterface::mmInterface` (piece 0x412490) | ported | `FrontendScreen.cpp` `Frontend::Frontend`, `FrontendScreen::FrontendScreen`; `PagesMain.cpp` `LoadingPage`; the page factories | loading steps 20…100 % (first audit), backgrounds, previous-menu links (race and net select → main, garage → race, sub-pages → options, showcase → garage, course → intro, intro → main; OpenMM2's page stack), callbacks, `InitPlayerInfo`, audio device and options reset, `PlayUIMusic`, `InitLobby`, a default car (the list's first) when none is set, the host car list (`GetHostCars`), `cclon_bk` for London. Piece 0x412490 is the lost-device callback for a joiner in a session (ready cleared, `RefreshMe`, `SendReadyStatus`): OpenMM2 has no lost device, not needed. |
| `mmInterface::~mmInterface`, ``mmInterface::`vector_deleting_destructor'``, `mmInterface::Reset` | not needed | — | destructor; `Reset` forwards to the child nodes. |
| `mmInterface::InitPlayerInfo` | ported | `FrontendScreen::FrontendScreen` | first start (no `players` directory): `mmInput::AutoSetup`, graphics `ResetDefaultAction` + save, "DriverX" (65) with net name 66, tag id, the current city, default view settings; then the London/SF race-records files, the last driver loaded (`PlayerLoad`) and the list filled; string 67 kept for the net menu. OpenMM2 creates DriverX and loads the last-used driver (first audit); options are global (documented). |
| `?HOFInitRecords@mmInterface@@AAEXHPAD@` | replaced | `game::HallOfFame` (`records.ini`) | makes `players/<city>/amateur` or `pro` with the city's blitz/circuit/checkpoint counts and empty entries (`mmMiscData::Init`, no seeded names); called again when the counts changed. OpenMM2 starts from an empty `records.ini`. |
| `mmInterface::ShowMain` | ported | `FrontendScreen::FrontendScreen` (result branches) | first start: the main menu. After a race: a multiplayer race with the network up → lobby (ready cleared, message callbacks re-armed); network gone → main menu then net select (`Switch(10)`); single player → the Crash Course page when the race was started there, else the race menu (first audit). The `GraphicsChange` branch (back to the options menu after a display change) never runs (flag never set); the lobby-launched branches are DirectPlay lobby launching (not needed). |
| `mmInterface::BeDone` | ported | `FrontendScreen.cpp` `Frontend::startRace` | with a driver: last car, city, paint, net name, event (mmStatePack mode and race) and last TCP/IP address saved to the driver's file, the current options copied into and saved to the driver's `.cfg`; then the done flags. OpenMM2 saves car, paint, transmission, city, mode and race; the net name is saved when edited, options are global, the TCP/IP address per driver (`Profile::address`). MM2 also runs it for every multiplayer start (`MultiStartGame`), as OpenMM2 now does (`Frontend::saveNetEvent`, finding B.6). |
| `mmInterface::ShowLockedVehicleMessage` | ported | `PagesRace.cpp` `VehiclePage` GO / lobby PREV → `Frontend::notice("jpg/lock_dlg.jpg", 23)` | dialog 0x17. |
| `mmInterface::Update` (navigation strip) | ported | `FrontendScreen.cpp` `addNavStrip`, `addBack` | OPTIONS (100): on the options menu nothing; on an option page the page's CancelAction then `Switch(2,0)` (findings B.1, B.2); elsewhere `Switch(2,1)`; from the garage it first sets the garage's showcase flag (VehicleSelectBase +0xA4) so the garage keeps a locked car when it comes back (OpenMM2 keeps the garage page alive: same effect). MINIMISE 0x65, HELP 0x66 (WinHelp; OpenMM2 message, documented), EXIT 0x67 → quit dialog, PREV 0x68 → the previous menu. |
| `mmInterface::Update` (main menu, case 1) | ported | `PagesMain.cpp` `DriverPage::DriverPage`, `askDelete`; `Frontend::askQuit` | Escape → quit dialog; CRASH COURSE → 0x28; RACES → 7; MULTIPLAYER → crash course becomes cruise, crash-course return cleared, 10; QUICK RACE → the same, `Vehicle::SetSubMenu(0)`, 8; CREATE → 0x11 below 18 drivers else 0x21; DELETE → 0x1e for the last driver else 0x1c; STATS → 0x13; RACE RECORDS → `PlayRecordsSound`, `HOFCB`, 0x14. QUICK RACE and MULTIPLAYER only change the mode and keep the environment (finding B.7). REPLAY (0x6c) cannot be pressed; ids 0x6e/0x6f have no widget on this menu (dead). |
| `mmInterface::Update` (options: cases 2, 3, 4, 5, 0x29, 0x22 and dialogs 0x15, 0x1f, 0x20) | ported | `PagesOptions.cpp` `OptionsPage`, `SettingsPage`, `ControlPage`, `CustomizePage`, `AboutPage` | options buttons → 3/5/4/0x22, Escape → previous menu; customize's bad-assignment flag opens 0x15 or 0x20; CUSTOMIZE (0x3e9) → 0x29; Escape/CANCEL (500) re-apply mmInterface's saved config (`SetAudio`/`SetGraphics`/`SetControls`/`SetCustom`), DONE (0x1f5) copies the globals in and saves the driver's `.cfg` (only with a driver loaded), DEFAULTS (0x1f6) → 0x1f, whose OK calls the page's ResetDefaultAction; then back to the options menu (customize: to Control). About: Escape or an action → previous menu. 0x15: OK → `VerifyBadAssignment`, CANCEL/Escape → `CancelBadAssignment`; 0x20: OK/Escape → `CancelBadAssignment`. |
| `mmInterface::Update` (race menu 7, garage 8, showcase 9, Crash Course 0x27, intro 0x28) | ported | `PagesRace.cpp` `RacesPage`, `VehiclePage`, `ShowcasePage`; `PagesCrash.cpp` `CrashCoursePage::proceed`, `CrashIntroPage` | race GO (9999) → crash-course return cleared, `SetSubMenu(0)`, 8; Escape → main. Garage: Escape → previous (race menu when none; in a session without one: destroy player, disconnect, 10); showcase 0x32 → 9; GO → `BeDone` or the lock dialog. Showcase: Escape or an action → garage. Course: Escape → intro; GO → crash-course return set; a lesson not yet passed starts at once in the school car (`vpbullet` in SF, else `vpcab`, paint 0) via `VehicleSelectBase::AllSetCar` + `BeDone`, else `SetSubMenu(0)`, 8. Intro: Escape → main; London (id 0) / SF (id 1) set the current city, reload the race names, `CityChange` the race and host race menus keeping mode and race, → 0x27. Verified by the first audit. |
| `mmInterface::Update` (driver dialogs 0x11, 0x13, 0x14, 0x17, 0x1b, 0x1c, 0x1d, 0x1e, 0x21) | ported | `PagesMain.cpp` `NewDriverDialog`, `RecordDialog`, `DriverPage::askDelete`; `FrontendScreen.cpp` `PictureDialog`, `askQuit`, `notice` | 0x11: the field's Enter, DONE (0xc9), CANCEL (0xca) and Escape close; 0x13/0x14: DONE or Escape close; 0x17/0x1e/0x21 close; 0x1b: OK quits, CANCEL/Escape close; 0x1c: YES → `MainMenu::DeleteCB`, NO/Escape close; 0x1d: OK or Escape → reopen 0x11. |
| `mmInterface::Update` (network: cases 10, 0xb, 0xc, 0xe, 0x10, 0x12, 0x18, 0x19, 0x1a, 0x24, 0x2a, 0x2b; pieces 0x40b2a9, 0x40b887; the start-of-frame call of piece 0x411dd0) | replaced | `PagesMulti.cpp` (`SessionsPage`, `AddressDialog`, `PasswordDialog`, `HostOptionsDialog`, `LobbyPage`, `HostSettingsPage`, `EjectDialog`), `game/net/NetGame.*` | protocol buttons, TCP/IP and serial dialogs, host/join, lobby buttons (garage → auditor; host settings; GO/READY; EJECT), the "retrieving game settings" dialog re-sending its request every 15 s, the boot dialog (OK → main menu + quit dialog when lobby-launched, else net select), the Zone button (quits to launch the Zone site: not needed), then 20 `asNetwork::Update` calls per frame. Piece 0x411dd0 is "you were dropped": close the retrieving dialog, destroy the player, disconnect, open `boot_dlg` (0x2b). |
| `mmInterface::Update` (replay 0x25/0x26, city 0x23, race environment 0x16) | not needed | — | 0x25/0x26 never open (REPLAY is off); 0x23 (`Dialog_City2`) and 0x16 (`Dialog_RaceEnvironment`) are opened by no `OpenDialog` call. |
| `mmInterface::Switch` (jump table 0x40d3f9) | ported | `Frontend::push`/`pop`, `addNavStrip`, `Frontend::topChanged` | every switch enables the strip and unlights OPTIONS; 2 lights it; 3/4/5/0x29 and 0xb hide PREV; 9 and 0x22 hide the strip; 7 leaves multiplayer and turns Cops & Robbers into cruise (`SyncRaceState`); 8 does the same outside multiplayer and clears a joiner's ready when coming from the lobby; 0x27/0x28 leave multiplayer; 0x1b opens the quit dialog instead of switching; 1 copies the driver's difficulty into the state pack (OpenMM2 keeps the difficulty in the profile); 10 inits the network, re-resolves the unlocks and replaces a locked car by vpbug and a locked paint job by 0 (finding B.5), drops a leftover session player ("you quit the session") and clears the list; 0xc is the lobby entry (group D and the auditor). The second argument records the previous menu. |
| `mmInterface::UpdateLobby` (piece 0x40cdbd), `mmInterface::LobbySwitch` (jump table 0x40d78a), `mmInterface::InitLobby` (piece 0x4100ac), `mmInterface::LobbyCreate`, `mmInterface::JoinLobbyGame` | replaced | `PagesMulti.cpp` lobby pages | the "launched by a DirectPlay lobby" (Zone) variants of Update/Switch: `InitLobby` asks DirectPlay whether the game was lobby-launched, `LobbyCreate` builds the session from the lobby's settings. OpenMM2 has no external lobby launching. The garage-from-lobby parts are the auditor's. |
| `mmInterface::InitNetwork`, `mmInterface::InitProtocols`, `mmInterface::SetProtocol`, `mmInterface::SetHostProtocol`, `mmInterface::SetProtocol2`, `mmInterface::ShowSessions`, `mmInterface::ClearSessions`, `mmInterface::RefreshSessions`, `mmInterface::NetJoinCB`, `mmInterface::JoinLAN`, `mmInterface::JoinSerial`, `mmInterface::JoinModem`, `mmInterface::JoinSession`, `mmInterface::JoinPasswordSession`, `mmInterface::JoinGame`, `mmInterface::CreateSession`, `mmInterface::CreatePlayer`, `mmInterface::mmInterface_IsAutodialEnabled` (piece 0x40c0cb) | replaced | `PagesMulti.cpp` `SessionsPage`, `joinSession`, `frontendHostSession`; `game/net/NetGame.*`; `net/Session.cpp` | IPX/TCP/serial/modem, 10 session rows ("-----" when empty), the list refreshed every 3 s, game version 3 checked (string 83 "ERROR: Network versions do not match." otherwise), sessions of a missing city refused, password sessions, joining a running Cruise/C&R session (late join, group D), session name "<net name> <race> <city> 23", 8 players. The player data sent: car, paint, team, ready, host flag, the 0.75 control value (mmStatePack +0x104), the car's tuning CRC. |
| `mmInterface::NetNameCB` | ported | `PagesMulti.cpp` sessions page net-name field → `Profile::netName` | saved with the driver. |
| `mmInterface::GetUniquePlayerName` | replaced | `net/Session.cpp` `Session::uniqueName` | MM2 appends a counter, "%s%d" (e.g. "noname1"), while another player's name starts with ours; OpenMM2 appends " (2)" on an exact, case-insensitive match (finding B.10). |
| `mmInterface::GetSessionData` (piece 0x411642), `mmInterface::SetSessionData`, `mmInterface::SetCRStateData`, `mmInterface::GetHostPlayerData` | replaced | `game::NetGame::raceConfig`, `setRaceConfig` | the session description packs version 3, mode, race, weather, time of day, the host's difficulty, a racers flag, the C&R type/limit/gold mass or the laps fields, pedestrian density (×255) and the city name; a joiner without that city gets the error flag (red locale line, group D). The joiner then races at the host's difficulty. |
| `mmInterface::RefreshPlayers`, `mmInterface::RefreshMe`, `mmInterface::MultiAllReady`, `mmInterface::MultiFillRoster`, `mmInterface::ChangePlayerData`, `mmInterface::SendReadyStatus`, `mmInterface::SendReadyReq`, `mmInterface::SendChatMessage`, `mmInterface::SendMsg`, `mmInterface::SendStartMsg`, `mmInterface::BootPlayerCB` | replaced | `PagesMulti.cpp` `LobbyPage`, `EjectDialog`; `NetGame::players`, `setLocalCar`, `setReady`, `everyoneReady`, `sendChat`, `startRace`, `kick` | roster rows, "Driver/Car/Color" panel, everyone-but-host-ready test, eject list, chat "%s> %s", start positions (`NetStartArray`). `ChangePlayerData` also sets the Free-For-All team from the car (team 0 for `vpcop`), verified by the first audit. |
| `mmInterface::MultiStartGame` (pieces 0x410881, 0x410898) | replaced | `FrontendScreen::update` (`NetGame::takeRaceStart` → `makeRaceScreen`) | replaces a locked paint job by 0 and a locked car by vpbug, sends the player data, clears the network callbacks, disables the menus, sets the multiplayer state and calls `BeDone` in both branches (dial-up or not). The fallback (`Frontend::unlockedNetCar`) and the `BeDone` (`Frontend::saveNetEvent`) were added in this audit (findings B.5, B.6). |
| `mmInterface::MessageCallback`, `mmInterface::MessageCallback2` (pieces 0x409d6b, 0x409e03, 0x409f8e, jump table 0x40a0d6) | replaced | `game/net/NetGame.cpp` `NetGame::handleEvents`, `addSystemLine` | "%s has joined" (68), "has left" (69), being dropped → boot dialog, host migration "**You are now the host**" (70) (in Cops & Robbers it can end the session instead; the condition on the players' data was not traced), new session data → ready cleared and the host settings reposted, player data changes → roster, chat lines, start (with an unavailable locale: roster and chat reset, 71 and 72, disconnect), host-car request/ack (`LimitToHostCars`, then join), "**Session returning to Lobby**" (73). Group D covers the lobby side. |
| `mmInterface::RequestProverb` | ported | `game::NetGame::raceConfig`, `PagesMulti.cpp` `LobbyPage` | Cops vs. Robbers: team 1 → vpmustang99, team 0 → vpcop, every other car locked; otherwise the normal unlocks (first audit; the garage lock in C&R is group D's item 18). |
| `mmInterface::LimitToHostCars`, `mmInterface::SendHostCars` | open | — | a joiner asks the host for its car list (0x20a, every 15 s while waiting) and locks every unlocked non-stock car (`IsStock` false) the host lacks, moving to vpbug if needed. Matters only with add-on cars; nothing shows with the retail data (Missing features). |
| `mmInterface::SendBootMsg` | not needed | — | unreachable (no callers). |
| `mmInterface::GetUnlockedCar`, `mmInterface::GetUnlockedColor` | ported (new) | `Frontend::unlockedNetCar` (sessions page, the lobby's garage) | vpbug with paint 0, and paint 0, for a locked car or paint job. Used by `Switch(10)`, `MultiStartGame`, the lobby entry and `ShowMain` after a network race; OpenMM2 checks on entering the sessions page and on leaving the lobby's garage (a race only unlocks). |
| `mmInterface::PlayerCreateCB`, `mmInterface::PlayerCreate` | ported | `PagesMain.cpp` `NewDriverDialog::create`; `game::ProfileStore::create` | closes the dialog first; 18 drivers → `plim_dlg`, empty name → nothing, duplicate → `dupp_dlg`; the new driver: name, difficulty from the state pack, net name 77, tag id, the list's first car with paint 0, the current city, saved; options inherited from the current driver (`PlayerReadState`) except the view settings (defaults) and the 0.75 control value; empty SF/London records; added to the list and loaded (`PlayerSetState`, which picks vpbug for the empty last car). The tag id (the unclamped `ElapsedTime`) only ties the per-city `.rec` files to the driver: OpenMM2 keeps records inside the profile, not needed. A flag at mmInterface +0x7331 is written for amateurs and never read. |
| `mmInterface::PlayerLoadCB`, `mmInterface::PlayerLoad` | ported | `FrontendScreen.cpp` `Frontend::selectProfile` | loads the driver (an unreadable one is deleted with `PlayerRemove`), its config (a missing `.cfg` gets graphics defaults and the current options), sets it as last player, creates missing city records, `PlayerSetState`. |
| `mmInterface::PlayerRemoveCB`, `mmInterface::PlayerRemove` (piece 0x40dc15) | ported | `PagesMain.cpp` `DriverPage::askDelete`; `game::ProfileStore::remove` | refuses the last driver, deletes `.sav`, `.cfg` and every city's `.rec` (the piece is that loop), removes the name, loads the directory's first driver when the deleted one was current, pick 0, `PlayerSetState`. |
| `mmInterface::PlayerSetState` | ported | `FrontendScreen.cpp` `Frontend::configFromProfile` | applies the driver's options (OpenMM2: global), difficulty, event, race, city (empty → the current one), resolves score, stats, records and unlocks, the car (empty → vpbug, paint 0) and paint, the net name, the last TCP/IP address into `Dialog_TCPIP`, `CitySetupCB`, `SyncRaceState`. For a city the game no longer has, MM2 keeps the current city via `Dialog_City2::SetCurrentCity`, OpenMM2 uses London (`FrontendScreen.cpp`); only reachable when a city's files disappear. |
| `mmInterface::PlayerFillStats` | ported | `PagesMain.cpp` `DriverPage::drawAbove` | verified by the first audit ("---" without a last car, 79 for C&R, 80 for cruise, `GetRaceName` otherwise; 82/81 rank; controller name; SF + London score for professionals). |
| `mmInterface::PlayerSwitchCityCB`, `mmInterface::PlayerFillRecords` (piece 0x40f6fa) | ported | `PagesMain.cpp` `DriverStatsDialog::drawAbove` | rows per mode verified by the first audit; the piece re-creates a city record whose race counts changed. MM2 also scrolls the list to the event currently set up (`Dialog_DriverRec::SetRecordPosition`) and gives `CrashCourse` the lesson grades; OpenMM2's list always starts at the top (only matters for a mode with more than 12 races). |
| `mmInterface::PlayerFillCrashRecords` | ported | `PagesCrash.cpp` `CrashCoursePage::drawAbove` | grade per lesson: 0 never driven, 1 passed, 2 failed. |
| `mmInterface::PlayerInitStats` | replaced | `game::Profile` (`[Races]` in the profile INI) | creates `players/<city>/<driver>.rec` sized for the city, stamped with the tag id. |
| `mmInterface::PlayerResolveScore` | not needed | — | sums SF and London scores and passed counts per mode into mmInterface fields nothing reads (the main menu uses `mmPlayerData::GetTotalScore`). |
| `mmInterface::PlayerResolveCars` | ported | `game::Progress::vehicleUnlocked`, `variantUnlocked` | clears every car's lock and paint mask, then `mmRewardList::UnlockPlayerRewards` for SF and London unless `unlockRewards` is set (never set: only `mmStatePack::SetDefaults` writes it). |
| `mmInterface::PlayerReadState` | replaced | `app/Settings.*` | copies the current controls, audio and graphics into the driver's config (per-driver options: documented deviation). |
| `mmInterface::PlayerGraphicsCB` | not needed | — | stored in `GraphicsOptions` (+0x7210) but nothing calls it. |
| `mmInterface::RefreshDriverList` | ported | `PagesMain.cpp` `DriverPage::refresh` | directory order, pick = the loaded driver. |
| `mmInterface::CitySetupCB` | ported | `game::Progress::openMask`, `raceOpen`; `PagesCrash.cpp` `CrashCoursePage` background | SF and London with a driver: checkpoint progress mask to the race menu and the host race menu, passed masks, crash course masks, `cc<sf|lon>_bk`, crash grades; other cities or no driver: everything open. The host race menu's checkpoint mask is `HostSettingsPage::raceOpen` (finding B.9). |
| `mmInterface::GetTimeString`, `mmInterface::GetScoreString`, `mmInterface::GetRaceString`, `mmInterface::GetRaceName` | ported | `FrontendScreen.cpp` `formatTime`, `Frontend::raceName`; `PagesMain.cpp` `RaceRecordsDialog::drawAbove` | "  ---  " for no time or a score ≤ 0, "%d" for a score; race names from the city's '|' lists (checkpoint, circuit, blitz) by index, "Crash Course" (78) for lessons, empty for cruise/C&R (callers use 80/79). |
| `mmInterface::HOFCB`, `mmInterface::HOFFillRecords` | ported | `PagesMain.cpp` `RaceRecordsDialog::drawAbove` | five rows per race and table, an empty car "---" (64); the passed state is passed on but never drawn (first audit). |
| `mmInterface::GetReplayDescCB` | not needed | — | replay dialog callback (never opened). |
| `mmInterface::SetStateRace`, `mmInterface::SetStateDefaults`, `mmInterface::SetupArchiveTest` | not needed | — | SetStateRace is empty; the other two are unreachable (no callers; an "archive test" that cycles through every race). |
| `mmInterface::PlayUIMusic` | ported | `FrontendScreen::FrontendScreen` → `audio::MusicPlayer::playMenu` | creates the DirectMusic manager when music is allowed and none exists, sets the volume, plays the menu segment; details are the audio audit's. |

## Garage showroom (VehicleSelectBase, mmVehicleForm, asViewCS, asDofCS)

Everything MM2 (build 3393) does to show the turning 3D car in the garage
(menu 8, `Vehicle`, built on `VehicleSelectBase`), as read from
`MenuManager::Init` (the frontend overload at 0x4e3820, signature
`(int,int,char*)`), `VehicleSelectBase::InitCarSelection`,
`VehicleSelectBase::SetPick`, `VehicleSelectBase::Update`,
`VehicleSelectBase::PreSetup` / `PostSetup`, `mmVehicleForm::mmVehicleForm`,
`mmVehicleForm::SetShape`, `mmVehicleForm::Update`, `mmVehicleForm::Cull`,
`asDofCS::asDofCS` / `Update`, `asViewCS::asViewCS` / `Update` /
`UpdatePolar`, `Matrix34::PolarView`, `Matrix34::FromEulersZXY`,
`Matrix34::MakeRotateUnitAxis`, `asCamera::SetView` / `SetViewport`,
`gfxViewport::Perspective` / `SetWindow`, `asCullManager::Update`,
`gfxRenderState::Init` / `Default`, `modShader::BeginEnvMap`,
`modStatic::DrawEnvMapped`, `Card2D::Init` / `Cull`. Floats were decoded
from the asm. OpenMM2 follows it in `app/frontend/Showroom.cpp` (newly
ported in this audit); the rows below note where it differs.

### Scene graph and draw order

* `MenuManager::Init` creates the menu camera (`asCamera`, MenuManager
  +0x1c) as the child of an `asViewCS` (MenuManager +0x20), which it adds
  as a child of the MenuManager node (before the nav bar; the menus are
  added later, so the view is updated before the cars). `InitCarSelection`
  adds, for every car of `mmVehList` (all
  cars, locked or not, in list order), one `asDofCS` as a child of the
  garage menu, with one `mmVehicleForm` as the dof's child. Every car's
  package is loaded when the garage menu is built (MM2 builds all menus at
  startup); OpenMM2 can load the picked car lazily.
* Per frame, `asCullManager::Update` clears the whole target and depth
  (`gfxPipeline::Clear(3, ...)`; the clear colour is asCullManager +0x8c,
  set to 0xff000000 = opaque black by `MenuManager::Init`), then draws the
  2D background list (the camera's underlay = the menu background JPG,
  `MenuManager::SetBackgroundImage` -> `asCamera::SetUnderlay`), then the 3D
  cullables through each declared camera (the car), then the 2D foreground
  list (all widgets). So the order is: background JPG, then the 3D car
  (depth-tested inside the camera viewport), then every widget on top.
* Only the picked car is drawn: a node is updated (and so declares itself
  for drawing) only while its flag bit 0 is set (`asNode::Update`).
  `InitCarSelection` clears bit 0 on every car's dof. `VehicleSelectBase::
  PreSetup` (and `Vehicle::PreSetup`, same code) sets it on the current
  pick's dof (VehicleSelectBase +0x8c); `PostSetup` clears it again when
  the menu is left. `SetPick` clears it on all dofs and sets it on the new
  pick. The menu itself is only updated while it is the active menu, so the
  car exists only on the garage screen (the showcase, menu 9, is a separate
  menu with no 3D: `VehShowcase::PreSetup` only assigns `<car>_show` as
  the background).
* `mmVehicleForm::Update` declares the form to the cull manager
  (`DeclareCullable`, which records the current world matrix = the dof's)
  only when it has a body or a shadow.

### Camera: projection and viewport

* `asCamera::SetView(0.6, 3.2, 1.0, 100.0)`: vertical field of view
  0.6 rad (34.377 degrees; `gfxViewport::Perspective` takes degrees, uses
  tan(fov/2) for y and divides x by the aspect), projection aspect 3.2
  passed explicitly (not the viewport's), near 1.0, far 100.0. Horizontal
  field of view = 2 atan(3.2 tan 0.3) = 89.42 degrees.
* Right after, camera +0x64 (the far distance kept by `SetView`) is
  overwritten with 24.0. It is only read by `asCamera::SphereVisible`,
  which the vehicle form never calls: no effect on the car (the projection
  keeps far = 100).
* `asCamera::SetViewport(0.05, 0.115, 0.95, 0.4, 1)`: the arguments are
  x, y, width, height as fractions of the screen; `gfxViewport::SetWindow`
  gets x = ftol(0.05 W), y = ftol(0.115 H), w = ftol(0.95 W),
  h = ftol(0.4 H), computed on the x87 from the float constants and
  truncated (`__ftol`). The 0.95 constant is 0x3f733333 = 0.94999999, but
  the x87 runs at 24-bit precision by then (`gfxPipeline::BeginGfx2D` calls
  `_control87` with `_PC_24`, the DirectDraw level includes FPUSETUP, and
  `AgeDevice::BeginScene` re-asserts `_PC_24`), so 640 x 0.94999999 rounds
  to 608.0 before the truncation, exactly as 32-bit float math does. On
  640x480: x 32, y 55 (55.2), w 608, h 192, i.e. the strip
  (32,55)-(640,247), reaching the right edge. Its centre is (336,151),
  16 px right of the screen centre. Viewport aspect 608/192 = 3.1667
  against the projection's 3.2, so the image is squeezed horizontally by
  about 1% (faithful detail). Depth range 0-1.
* `VehicleSelectBase::SetPick` also writes camera +0x54 (the field of view
  `asCamera::Update` uses for its culling planes) from a per-car array
  (VehicleSelectBase +0xe0) that `InitCarSelection` fills with 0.6 for
  every car and nothing else writes: always 0.6, no visible effect.
* Fog: the camera's fog is zero (`asCamera::asCamera` -> `SetFog(0,0,0,0)`)
  and the form draws without fog (inferred: nothing in the frontend turns
  fog on).

### Camera: position and orientation (asViewCS)

`asViewCS` layout used here: +0x80 mode (0 = polar, from the constructor),
+0x88 target node (none), +0x98..+0xa0 offset added to the position,
+0xa4 distance, +0xa8 azimuth, +0xac incline, +0xb0 twist, +0xe4 azimuth
goal (12345.0 = none, so the azimuth never eases).

* `MenuManager::Init` sets distance 10.0, azimuth 0, incline 0.534, twist 0,
  offset (0, 0.86, 0). `VehicleSelectBase::Update` (every frame while the
  garage is active, after `UIMenu::Update`) then forces azimuth 0,
  incline 0.18 and twist 0, so the incline used is 0.18 rad (10.31 deg).
* The distance eases linearly towards the picked car's `UIDist` at
  21.0 units per second (`datTimeManager::Seconds` x 21, clamped at the
  goal). The goal is a global initialised to 10.0 and set by `SetPick` to
  `mmVehInfo` +0x114 (`UIDist` from `tune/<car>.info`, default 6.0; OpenMM2
  has it as `VehicleInfo::uiDistance`). `InitCarSelection` calls `SetPick`,
  so the first time the garage shows, the camera slides from 10 to the
  car's UIDist (about 0.2 s); afterwards from the previous car's distance.
* `asViewCS::Update` -> `UpdatePolar` -> `Matrix34::PolarView(distance,
  azimuth, incline, twist)` = `FromEulersZXY(-incline, azimuth, twist)` then
  position = distance x the Z row; then the offset is added. With azimuth
  and twist 0 the camera's world matrix is:
  * X row (1, 0, 0)
  * Y row (0, cos 0.18, -sin 0.18) = (0, 0.98384, -0.17903)
  * Z row (0, sin 0.18, cos 0.18) = (0, 0.17903, 0.98384)
  * position (0, 0.86 + d sin 0.18, d cos 0.18) for distance d
    (d = 6: (0, 1.934, 5.903)).
  The camera looks along -Z, i.e. at the point (0, 0.86, 0) from distance
  d, pitched down 0.18 rad, from the +Z side. OpenMM2 already has this
  function: `game/CamMath.h` `cam::polarView`.
* The camera is a child of the view node, so its world matrix is the view's
  (`asCamera::Update` copies the current matrix; `DrawBegin` ->
  `gfxRenderState::SetCamera`).

### Each car's node (asDofCS) and spin

`asDofCS` fields (constructor defaults): +0x80 type 0 (rotate), +0x84 mode
0 (bounce), +0x88 axis (1,0,0), +0x94 offset (0,0,0), +0xa0 origin
(0,0,0), +0xb8 value 0, +0xc0/+0xc4 bounds -1e6 / +1e6, +0xc8 rate 0,
+0xd4 sine amplitude 0, +0xe8 clamp on.

* `InitCarSelection` sets axis (0, 1, 0), offset (0, 0, 0), rate 0, adds the
  dof to the menu, then sets rate +0xc8 = 1.0. `SetPick` sets the picked
  dof's rate to 1.0 again (no change).
* `asDofCS::Update`: value += rate x frame seconds (bounce mode only acts
  at the +-1e6 bounds, never reached), then the local matrix = identity
  rotated by `Matrix34::RotateFullUnitAxis((0,1,0), value)` and the
  position = offset (0,0,0). So the car turns about the world Y axis at
  1 rad/s (one turn in 6.28 s), centred on the origin with no offset or
  scale. The rotation rows are X (c, 0, -s), Y (0, 1, 0), Z (s, 0, c)
  (c, s of the angle; same as `Mat34::rotationY`): seen from above, counter-
  clockwise. At angle 0 the car faces -Z, away from the camera (the camera
  first sees the rear); the nose then swings to screen left.
* The angle starts at 0 and only advances while that car's dof is active;
  nothing resets it (`VehicleSelectBase::Reset` is empty), so a car shown
  again resumes the angle it had (inferred from the code).
* `InitCarSelection` also rotates a local 2D vector by sin/cos(pi/4) per
  car (starting at (0,0), adding 0.3 sin(pi/4) each step), but the result is
  never stored anywhere: dead code (probably left from a layout with the
  cars on a circle). All cars sit at the origin.

### The vehicle form (mmVehicleForm)

Layout (matches mm2hook): +0x18 body static, +0x1c shadow static, +0x20
the 4 wheel statics, +0x24 shader sets (one per paint job), +0x28 wheel
pivots (4 x Vec3), +0x2c extra part count, +0x30 extra statics (12),
+0x34 extra pivots (12 x Vec3), +0x38 paint job in use, +0x3c pointer to
the paint job to use, +0x40 colour (see the tint below), +0x44 shaders per
paint job, +0x48 `mmDamage`.

* `mmVehicleForm::mmVehicleForm` loads `refl_showroom` (`gfxGetTexture`,
  texture/refl_showroom.tga), shared by all forms.
* `InitCarSelection` calls `SetShape(<car basename>, "H", "SHADOW_H",
  null)`; `SetShape` ignores its second to fourth arguments and opens
  `geometry/<basename>.pkg`:
  * `BODY_H` (expected as the first chunk), then `SHADOW_H`, then
    `whl0_H`..`whl3_H`, each wheel with its pivot from `GetPivot(basename,
    "whl<i>")` (`geometry/<basename>_whl<i>.mtx`, the matrix's position).
    Only the high LOD is used.
  * Then every following chunk up to `shaders` whose name has `_H` right
    after its first underscore and equals (case-insensitive) one of the 12
    names `break0`, `break1`, `break2`, `break3`, `break01`, `break12`,
    `break23`, `break03`, `fndr0`, `fndr1`, `whl4`, `whl5` plus `_H` is an
    extra part (at most 12, in package order), with its pivot from
    `<basename>_<name>.mtx`, or (0,0,0) when there is none.
  * The `shaders` chunk: all paint jobs (`modShader::LoadShaderSet`). For
    every textured shader of every paint job: a texture name ending in
    `_dmg` (last underscore) loses that suffix (the clean texture is used),
    and the material's diffuse, ambient and specular alpha are set to 1.0
    and its emissive alpha to 0.
  * `mmDamage::Init(body)`; the damage is never applied in the garage.
* Paint job: `InitCarSelection` points the form's +0x3c at that car's entry
  of the per-car paint array (VehicleSelectBase +0xe4). `mmVehicleForm::
  Cull` copies it to +0x38 every frame and draws every part with that paint
  job's shader set, so `IncColor` / `DecColor` / `ColorCB` (CAR COLOR)
  repaint the turning car at once.
* `mmVehicleForm::Cull` (all parts with the paint job's shaders):
  1. Saves and turns on lighting, depth test (render state +0x18) and alpha
     blending (+0x20); world = the dof's matrix; lazily loads the paint
     job's textures (`modShader::PreLoad`).
  2. `SHADOW_H` at the car's own matrix (no ground probe: the shadow mesh
     sits under the car as modelled), with the same state as the body:
     lit, depth-tested, alpha-blended; no depth bias (the race's
     `vehCarModel::DrawShadow` state is not used).
  3. `BODY_H`.
  4. If `refl_showroom` loaded (and not software rendering): the body again
     through `modShader::BeginEnvMap(refl_showroom, node matrix)` +
     `modStatic::DrawEnvMapped(..., 1.0)` + `EndEnvMap`: the same
     reflection pass as the race car's (intensity 1.0, each section's grey
     = ftol(1.0 x material power x 255), skipped when 0, added), but with
     `refl_showroom` instead of the city's `refl_dc`, and without fog.
  5. Wheels 0-3: world = translation(pivot) x node matrix (no spin, no
     steer, no suspension drop); draws `whl<i>_H` when present.
  6. The extra parts in package order, each at translation(pivot) x node
     matrix.
  7. Restores the states.
  Not drawn: decal, hubs, variant parts, lights or glows, other LODs.
* Tint 0xff202020: `InitCarSelection` writes it to form +0x40 for every car
  whose `mmVehInfo` +0xf4 is set (+0xf4 is the car's locked flag: 0 from
  the constructor, set to 1 by `mmRewardList::UnlockPlayerRewards` while
  the reward is not earned), and `SetPick` writes 0xff202020 for a picked
  car or paint job that is locked (`CurrentVehicleIsLocked`) and 0
  otherwise. Nothing in build 3393 reads form +0x40 (`mmVehicleForm::Cull`
  and every other function of the class ignore it), so locked cars are
  drawn exactly like unlocked ones; the LOCKED sign is the only cue.
  OpenMM2 should not darken them.

### Lighting

* `gfxRenderState::Init` makes `gfxLight::Sun` a directional light
  (type 3) with diffuse (1,1,1,1); its specular and ambient stay 0.
  `MenuManager::Init` sets the Sun's position to (1,1,1) and direction to
  (-1,-1,-1) (Sun +0x34 and +0x40, the addresses 0x6855ac..0x6855c0;
  D3D7 normalises it: (-0.577, -0.577, -0.577), light travelling down,
  towards -X and -Z, i.e. coming from above, screen right and the camera's
  side), and sets it as light 0 (`gfxRenderState::SetLight(0, Sun)`).
  `gfxRenderState::Default` (on device creation) enabled light 0.
* The render state's ambient colour (+0x30) is never set by the frontend:
  0 (black) from static memory at startup, so the car is lit by that one
  white directional light plus material emissive. Inferred: after a race
  it may keep the city's ambient and fill lights (`cityLevel::SetupLighting`
  sets them and enables lights 1-2; nothing in the frontend resets them
  unless the device is recreated); a port should use ambient 0 and the one
  light.
* Material ambient = diffuse (pkg loader), specular light colour 0: no
  highlights.

### Card2D behind the car

`InitCarSelection` creates a `Card2D` (VehicleSelectBase +0xc8) with
`Card2D::Init(menu camera, x = 0.025, y = 0.2, w = 0.5, h = 0.5, 0.75)`
(screen fractions; on 640x480 (16,96) 320x240) and then sets its colour
bytes to r 0, g 0, b 0, alpha 0xff (opaque black). It is never added to the
node tree (no `AddChild`, no other reference except the destructor), so it
is never drawn: no black card behind the car. (The rectangle is the
garage's widget area from the `InitCarSelection` arguments 0.025, 0.3, 0.5,
0.5: +0xf8 x 0.025, +0x100 y 0.3, +0xfc 0.575, +0x104 width 0.5, +0x108
0.375, +0x10c height 0.5, +0x110 0.025, +0x114 0.2, +0x118 0.05; those feed
the widgets, whose real positions come from `widget.csv`.)

## RaceMenuBase

The shared body of the Single Race menu (menu 7, `RaceMenu`) and the host's
race settings (menu 11, `HostRaceMenu`): mode lamps, RACE NAME with clamping
arrows, LAPS/OPPONENTS rollers, RACE LOCALE, TIME OF DAY, WEATHER, density
sliders, the help label ("race desc icons": race_btz|race_cir|race_cp|
race_rom|race_cop|host_ffa|host_cvr|host_rt|host_n|host_t|host_p|host_gm,
indexed by +0xa0) and the race map icon. `Init(0)` builds the single-player
variant, `Init(1)` the multiplayer one (cops_m lamp, host_rnm/host_lap icons,
no opponents/traffic/cop widgets, environment always editable). Created by
`mmInterface::mmInterface`, reached from the main menu. OpenMM2:
`app/frontend/PagesRace.cpp` `RacesPage` (single player) and
`app/frontend/PagesMulti.cpp` `HostSettingsPage` (host); the race-table
defaults are `Frontend::applyRaceDefaults` -> `game::session::
applyRaceTableDefaults`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `RaceMenuBase::RaceMenuBase` | ported | `app/frontend/FrontendScreen.cpp` `Frontend::racesFor`, `game/Profile.cpp` `Progress::openMask` | loads mmracedata / mmblitzdata / mmcircuitdata for the city's race dir and sets the blitz and circuit "open" masks (+0xac, +0xb0) to all ones: blitz and circuit races are never locked, only checkpoint races (+0xa8 from `RaceMenu::SetProgressMask`). OpenMM2's `openMask` returns all ones for blitz and circuit. |
| `RaceMenuBase::~RaceMenuBase`, ``RaceMenuBase::`scalar_deleting_destructor'`` | not needed | — | memory plumbing. |
| `RaceMenuBase::Init` | ported | `app/frontend/PagesRace.cpp` `RacesPage::RacesPage`; `app/frontend/PagesMulti.cpp` `HostSettingsPage::HostSettingsPage` | widget order verified by the first audit; lamps of modes the city has no races for are killed (`UIBMButton::Kill`), the map icon is the initial focus; ends with `ChangeLocalVals` + `CityChange`. |
| `RaceMenuBase::InitCRWidgets`, `RaceMenuBase::SetCRWidgets` | not needed | — | empty virtual defaults (the host menu overrides them). |
| `RaceMenuBase::PreSetup` | ported | `app/frontend/PagesRace.cpp` `RacesPage::onEnter`, `RacesPage::RacesPage` | hides the help label (`FocusDescription(0,0)`); a leftover crash-course state (6) becomes cruise with `ChangeLocalVals` + `SetRW` but without `SetStateRace` (finding C.2). |
| `RaceMenuBase::FocusDescription` | ported | `app/frontend/PagesRace.cpp` `RacesPage::update` | shows/hides the help label with the index the focused lamp registered (0 blitz, 1 circuit, 2 checkpoint, 3 cruise, 4 cops), called by `UIBMButton::Switch` on focus gain and loss and by `ChangeLocalVals`; OpenMM2 does the same (`RacesPage::update`, finding C.1). Single-player index 4 maps to 3 (unreachable: no cops lamp there). |
| `RaceMenuBase::WidgetOnOff` | ported | `app/frontend/PagesRace.cpp` `RacesPage::refresh` (`visible`) | turns a widget on or off; the RACE NAME box takes its two arrows with it. |
| `RaceMenuBase::SetRW` | ported | `app/frontend/PagesRace.cpp` `RacesPage::refresh`, `drawBelow` | per mode: LAPS / OPPONENTS only for circuits and read-only until passed; time, weather and densities read-only until the race is passed (passed masks +0xb4 blitz, +0xb8 checkpoint, +0xbc circuit), the time/weather arrows hidden then, circuit traffic always read-only; cruise always editable; race_cov except for circuits, host_lap only for circuits; multiplayer: always editable, no opponents roller. With mmStatePack +0x10c set (mm2hook: DisableProfile) everything is open (OpenMM2: no driver = all open). |
| `RaceMenuBase::CheatCallback` | not needed | — | no callers in build 3393 (reachable 0); it only clears the byte flag at 0x6b1b84. The first audit's "unlock cheat not ported" row can be closed as dead code. |
| `RaceMenuBase::SyncRaceState` | ported | `app/frontend/PagesRace.cpp` `RacesPage::RacesPage`; `app/frontend/FrontendScreen.cpp` `Frontend::configFromProfile` | maps the game mode to the lamp index, then `SetStateRace` (single player) and `ChangeLocalVals`; called by `mmInterface::Switch` (Cops & Robbers -> cruise), `LobbySwitch`, `PlayerSetState`. |
| `RaceMenuBase::CityChange` (with its split tail 0x508372) | ported | `app/frontend/PagesRace.cpp` `RacesPage::selectCity` | reloads the city's three race tables, unkills/kills the checkpoint, circuit and blitz lamps by the city's race counts (mmCityInfo +0x90 / +0x94 / +0x8c), a mode the city lacks becomes cruise, calls the menu's +0xc0 callback, then `GameCallback` (race 0, `SetStateRace`, `ChangeLocalVals`). |
| `RaceMenuBase::AnotherCityChangeCB`, `RaceMenuBase::IncLocale`, `RaceMenuBase::DecLocale` | ported | `app/frontend/PagesRace.cpp` RACE LOCALE box + `addArrows(..., false)`, `selectCity` | the LOCALE arrows clamp (no wrap); `MenuManager::LoadRaceNames`, the city name globals, `CityChange`. |
| `RaceMenuBase::GameCallback` | ported | `app/frontend/PagesRace.cpp` `RacesPage::selectMode` | lamp -> game mode (blitz 4, circuit 3, checkpoint 1, cruise 0, cops 2), race 0, `SetStateRace`, `ChangeLocalVals`. |
| `RaceMenuBase::LapsCallback` | ported | `app/frontend/PagesRace.cpp` `m_laps` setter | laps = roller index + 1. |
| `RaceMenuBase::AICallback` | ported | `app/frontend/PagesRace.cpp` `m_opponents` + `refresh` (`maxIndex`) | clamps the OPPONENTS index to the circuit race's opponent count - 1; opponent density = index + 1. |
| `RaceMenuBase::ChangeLocalVals` (with its split piece 0x5088a1) | ported | `app/frontend/PagesRace.cpp` `RacesPage::refresh`, `selectMode` | per mode: hides RACE NAME (cruise, cops) or fills it with the mode's race names, clamps the race to the count (circuit, blitz), laps index from the state; then shows the help label for the mode (`FocusDescription(mode, 1)`). |
| `RaceMenuBase::SetStateRace` (with its split tail 0x508c7a) | ported | `app/frontend/FrontendScreen.cpp` `Frontend::applyRaceDefaults` -> `game/session/RaceSetup.cpp` `applyRaceTableDefaults`; `app/frontend/PagesRace.cpp` `RacesPage::validateRace` | cruise: noon, clear, 0.25 / 0.5 / 1; races: time, weather, densities, cops, opponents, laps (checkpoint 1, circuit from the table), time limit and difficulty from the race table by skill; RACE NAME disabled mask = not open (none with +0x10c set); then `SetRW`, `LoadRaceMap`. Also runs in the host menu (`HostSettingsPage::applyDefaults`, finding C.3). The cop count is kept in the density (OpenMM2 clamps it to 1, documented there). |
| `RaceMenuBase::LoadRaceMap` | ported | `app/frontend/PagesRace.cpp` `RacesPage::mapPicture`; `app/frontend/PagesMulti.cpp` `HostSettingsPage::drawBelow` | `<RaceDir>_map` + `roam` / `race%d` / `multicop` / `circuit%d` / `blitz%d` (the `dgGameModeNames` formats), icon hidden when missing. The host page builds the name from the city's map name instead of its race dir (same for the retail cities). |
| `RaceMenuBase::DecRaceName`, `RaceMenuBase::IncRaceName` | ported | `app/frontend/PagesRace.cpp` `addArrows(..., false)` + `optionEnabled` | clamp at both ends (the decrement clamps to the count, not count - 1: harmless) and only move when the target race is open, then `SetStateRace`. |
| `RaceMenuBase::IncTime`, `RaceMenuBase::DecTime`, `RaceMenuBase::IncWeather`, `RaceMenuBase::DecWeather` | ported | `app/frontend/PagesRace.cpp` time and weather `addArrows(..., false)` | clamp 0..3 (no snow). |

## RaceMenu

The Single Race menu (menu 7, "Single Race Menu"): `RaceMenuBase` with
`Init(0)`, zero masks, and the race_veh button (id 9999) to the garage. The
masks come from `mmInterface::CitySetupCB` (progress and passed masks of San
Francisco and London; all ones elsewhere or without a driver). OpenMM2:
`RacesPage` and `game::Progress`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `RaceMenu::RaceMenu` | ported | `app/frontend/PagesRace.cpp` `RacesPage::RacesPage` | race_veh at the "next" position (widget 23). |
| `RaceMenu::~RaceMenu`, ``RaceMenu::`scalar_deleting_destructor'`` | not needed | — | memory plumbing. |
| `RaceMenu::SetProgressMask` | ported | `game/Profile.cpp` `Progress::openMask` (checkpoint groups of three) | stores the checkpoint open mask and re-runs `SetStateRace`. |
| `RaceMenu::SetBlitzMask`, `RaceMenu::SetCheckpointMask`, `RaceMenu::SetCircuitMask` | ported | `app/frontend/PagesRace.cpp` `racePassed` (profile records) | passed masks for `SetRW`, from `mmPlayerData::GetPassedMask` via `mmInterface::CitySetupCB`. |
| `RaceMenu::GetRaceID`, `RaceMenu::GetRaceName` | not needed | — | unreachable (no callers). |

## HostRaceMenu

The host's race settings (menu 11, "Host Race Menu"): DONE (host_dn, id
1000) first, then `RaceMenuBase::Init(1)` with this class's Cops & Robbers
widgets (host_cr panel, free_cr / cpsvr_cr / robrs_cr type lamps, none_cr /
time_cr / point_cr limit lamps, LIMIT VALUE 510-513 or 514-517, GOLD MASS
506-508, help indices 5-11). The four masks start as all ones;
`mmInterface::CitySetupCB` then writes the driver's checkpoint progress
into +0xa8 (finding B.9), so the host picks only checkpoint races he has
opened. The session data packing (`EncodeCRData` / `DecodeCRData`) is
DirectPlay plumbing. OpenMM2: `app/frontend/PagesMulti.cpp`
`HostSettingsPage` over `game::NetGame`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `HostRaceMenu::HostRaceMenu` | ported | `app/frontend/PagesMulti.cpp` `HostSettingsPage::HostSettingsPage`, `clampRace` | DONE first; masks +0xa8 / +0xb4 / +0xb8 / +0xbc all ones at first (`CitySetupCB` then writes the checkpoint mask into +0xa8); limit indices and gold mass index start at 0 (finding C.4). |
| `HostRaceMenu::~HostRaceMenu`, ``HostRaceMenu::`scalar_deleting_destructor'`` | not needed | — | memory plumbing. |
| `HostRaceMenu::PreSetup` | ported | `app/frontend/PagesMulti.cpp` `HostSettingsPage::HostSettingsPage`, `update` | `SetRW`, then `RaceMenuBase::PreSetup` (crash course -> cruise). |
| `HostRaceMenu::InitCRWidgets` | ported | `app/frontend/PagesMulti.cpp` `HostSettingsPage::HostSettingsPage` (widgets 10-21) | lamps, LIMIT VALUE + arrows, GOLD MASS (title 509) + arrows; verified by the first audit. |
| `HostRaceMenu::SetCRWidgets`, `HostRaceMenu::SetLimitControl` | ported | `app/frontend/PagesMulti.cpp` `HostSettingsPage::update` | the Cops & Robbers widgets and host_cr only in Cops & Robbers; LIMIT VALUE and its arrows only with a time or points limit, listing 510-513 (time index) or 514-517 (points index). |
| `HostRaceMenu::SetGameClassCallback` | ported | `app/frontend/PagesMulti.cpp` type lamps (`m_cfg.copsAndRobbers`) | calls the menu's +0x1a8 callback, which nothing sets (inferred: no writer found), so the lamp's radio value is the only effect. |
| `HostRaceMenu::LimitInc`, `HostRaceMenu::LimitDec`, `HostRaceMenu::SetLimit`, `HostRaceMenu::GetLimit` | ported | `app/frontend/PagesMulti.cpp` `setLimit`, `setLimitIndex` | separate time and points indices 0..3; with no limit the arrows step the points index (hidden, no effect). |
| `HostRaceMenu::MassInc`, `HostRaceMenu::MassDec` | ported | `app/frontend/PagesMulti.cpp` GOLD MASS box + `arrows` | clamp 0..2. |
| `HostRaceMenu::GetLimitVal`, `HostRaceMenu::GetGoldMassVal` | ported | `app/frontend/PagesMulti.cpp` `kTimeLimits`, `kPointLimits`, `apply` -> `NetGame::setGoldMass` | 5/10/20/30 min, 100/250/500/1000 pts, gold mass 0/100/200; read by `mmInterface::SetCRStateData`. |
| `HostRaceMenu::EncodeCRData`, `HostRaceMenu::DecodeCRData` | replaced | `game/net/NetGame.cpp` session settings extras | packs mode, mass index, limit kind and index into one int of the DirectPlay session data (`mmInterface::SetSessionData` / `GetSessionData`); OpenMM2 sends named fields. |
| `HostRaceMenu::GetGoldMass`, `HostRaceMenu::SetGoldMass` | not needed | — | unreachable (no callers); `SetGoldMass` would be off by one. |

## VehicleSelectBase

The garage (menu 8, built by `Vehicle::Vehicle` with
`InitCarSelection(1, 0.025, 0.3, 0.5, 0.5)`, also opened from the lobby):
VEHICLES and CAR COLOR drop-downs with wrapping arrows, TRANSMISSION, four
read-only stat sliders, VEHICLE SHOWCASE (id 0x32), the LOCKED sign, the car
description icon (`ShowCarDesc`) and the 3D showroom (see "Showroom
(detailed)"). It keeps a paint job per car for the session and the last
unlocked car picked. OpenMM2: `app/frontend/PagesRace.cpp` `VehiclePage`
(widgets, locks, sounds, description) and `app/frontend/Showroom.cpp`
`Showroom` (the turning car, newly ported in this audit).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `VehicleSelectBase::VehicleSelectBase` | ported | `app/frontend/PagesRace.cpp` `VehiclePage::playSelectSound`, stat bars | one sound slot per car with `<car>_select` (valid cars) at 0.91; stat array; min 1e6 / max 0 trackers; no last unlocked car (-1). |
| `VehicleSelectBase::~VehicleSelectBase`, ``VehicleSelectBase::`scalar_deleting_destructor'`` | not needed | — | memory plumbing. |
| `VehicleSelectBase::InitCarSelection` | ported (new) | `app/frontend/PagesRace.cpp` `VehiclePage::VehiclePage`, `app/frontend/Showroom.cpp` `Showroom::setCar` | widgets ported and verified by the first audit (VEHICLES initial focus, CAR COLOR, TRANSMISSION 633/634, sliders, VEHICLE SHOWCASE, LOCKED sign); the per-car `asDofCS` + `mmVehicleForm` (3D cars) are `Showroom`'s cars, each loaded when first shown (MM2 builds them all here); the per-car field of view is always 0.6 and the tint is never read. The unused "Car Description" label (`<car>_desc`) is switched off at once and never shown; the `Card2D` is never attached. |
| `VehicleSelectBase::PreSetup` | ported | `app/frontend/PagesRace.cpp` `VehiclePage::enterWithUnlockedCar`, `playSelectSound`, `onEnter` | activates the picked car's 3D node (`Showroom` draws the picked car); a locked car or paint job moves to the last unlocked car unless coming back from the showcase; plays `<car>_select` if not playing; restores the car's paint job; `LockColor`; hides the veh_tsc label. |
| `VehicleSelectBase::PostSetup` | ported (new) | `Frontend::scenePage` | only deactivates the picked car's 3D node: the car is drawn while the garage (or a dialog over it) is up. |
| `VehicleSelectBase::Update` | ported (new) | `VehiclePage::update`, `Showroom::update`, `Showroom::easeDistance`, `Showroom::cameraMatrix` | `UIMenu::Update`, then the showroom camera: distance eases to the car's UIDist at 21/s, azimuth 0, incline 0.18, twist 0 (see the showroom section). MenuManager::OpenDialog leaves the garage enabled, so it keeps updating (and the car turning) under the LOCKED dialog; so does OpenMM2's (`Frontend::update`). |
| `VehicleSelectBase::SetPick` | ported | `app/frontend/PagesRace.cpp` `VehiclePage::pickVehicle`, LOCKED sign `Custom` | wraps (`CarMod`), LOCKED sign by `CurrentVehicleIsLocked`, remembers the last unlocked pick, `vehicleId` / name, CAR COLOR list from the car's Colors, the car's paint job, `FillStats`, `LockColor`, `ShowCarDesc`; showroom parts (`Showroom::setCar`): only the picked dof active (rate 1.0), camera field of view from the per-car array (always 0.6), UIDist as the camera's goal, tint 0xff202020 (never read). Its centred `%*s%*s` description string is built and never used. |
| `VehicleSelectBase::CarMod`, `VehicleSelectBase::IncCar`, `VehicleSelectBase::DecCar`, `VehicleSelectBase::TDPickCB` | ported | `app/frontend/PagesRace.cpp` VEHICLES box + `addArrows(..., true)` | wrap; arrows and the drop-down pick play the car's sound (`SetPick(..., 1)`). |
| `VehicleSelectBase::AllSetCar` | ported | `app/frontend/FrontendScreen.cpp` `Frontend::configFromProfile` | `mmInterface` sets the car and paint job by name (driver load, lobby, unlocked car); OpenMM2 writes `config.vehicle` / `vehicleColor`, which the garage reads. |
| `VehicleSelectBase::IncColor`, `VehicleSelectBase::DecColor`, `VehicleSelectBase::ColorCB`, `VehicleSelectBase::LockColor` | ported | `app/frontend/PagesRace.cpp` CAR COLOR box + `addArrows(..., true)`, `garageSession`, LOCKED sign | wrap over the Colors count; per-car paint job; the LOCKED sign is on while the paint job (mmVehInfo +0x108 bit) or the car (+0xf4) is locked. |
| `VehicleSelectBase::IncTrans`, `VehicleSelectBase::DecTrans` | ported | `app/frontend/PagesRace.cpp` TRANSMISSION box + `addArrows(..., false)` | down = Automatic (1), up = Manual (0), clamping. |
| `VehicleSelectBase::CurrentVehicleIsLocked`, `VehicleSelectBase::SetLastUnlockedVehicle` | ported | `app/frontend/PagesRace.cpp` `carUnlocked`, `paintUnlocked`, `enterWithUnlockedCar` | car locked (+0xf4) or paint job bit set in +0x108; last unlocked pick, else the first unlocked car. |
| `VehicleSelectBase::LoadStats`, `VehicleSelectBase::AssignVehicleStats`, `VehicleSelectBase::FillStats` | ported | `app/frontend/PagesRace.cpp` stat bars | horsepower, top speed, durability, mass of valid cars; ranges 0.5 x min to 1.1 x max. |
| `VehicleSelectBase::GetCarTitle` | ported | `app/frontend/PagesRace.cpp` `playSelectSound`, VEHICLES / CAR COLOR lists | base name, description, Colors list; plays `<car>_select` when asked and not playing. |
| `VehicleSelectBase::ShowCarDesc` | ported | `app/frontend/PagesRace.cpp` `carDescription` | `<car>_lck<n>[_p]`, `<car>_lck[_p]`, `<car>_ulck`, hidden when none (icon at 0.0625, 0.825). |
| `VehicleSelectBase::FocusDescription` | ported | `app/frontend/PagesRace.cpp` VEHICLE SHOWCASE `help` | veh_tsc while the button is focused (the description icon hidden meanwhile), else the car description. |
| `VehicleSelectBase::SetShowcaseFlag` | ported | `app/frontend/PagesRace.cpp` `m_inShowcase` | coming back from the showcase skips the locked-car fallback. |
| `VehicleSelectBase::SetLockedLabel` | not needed | — | builds the "Locked Text" label's names (`ulock_am<car>` / `ulock_pr<car>` by skill), but that label is only ever switched off (`InitCarSelection`, `SetPick`, `SetLockedLabel`): never visible. |
| `VehicleSelectBase::Reset` | not needed | — | empty. |

## VehShowcase

The car's spec sheet (menu 9, "Vehicle Showcase"), opened by VEHICLE
SHOWCASE: background `<car>_show` (`PreSetup` picks entry `vehicleId` of the
list), DONE (host_dn at 0.05, 0.9). OpenMM2: `app/frontend/PagesRace.cpp`
`ShowcasePage`. No 3D here.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `VehShowcase::VehShowcase`, `VehShowcase::PreSetup` | ported | `app/frontend/PagesRace.cpp` `ShowcasePage::ShowcasePage`, `showPicture` | background `<car>_show`; OpenMM2 falls back to `<car>.jpg` when `_show` is missing (an OpenMM2 addition; MM2 just assigns the missing name). |
| `VehShowcase::PostSetup` | not needed | — | empty. |
| `VehShowcase::~VehShowcase`, ``VehShowcase::`scalar_deleting_destructor'`` | not needed | — | memory plumbing. |

## mmVehicleForm

One car's showroom model (an `asNode` child of the car's `asDofCS`, one per
car in `mmVehList`, built by `InitCarSelection`): body, shadow, four wheels
and up to 12 extra parts at their pivots, with the session paint job and a
`refl_showroom` reflection. Only the garage uses it (the race uses
`vehCarModel`). Described in the showroom section above. OpenMM2:
`app/frontend/Showroom.cpp` (`Showroom::setCar`, `materials`, `draw`),
newly ported in this audit.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmVehicleForm::mmVehicleForm` | ported (new) | `Showroom::Showroom` (its texture and model libraries) | loads `refl_showroom`; arrays for wheels, pivots, 12 extras; paint pointer to its own field. |
| `mmVehicleForm::SetShape` | ported (new) | `Showroom::setCar` (`asset::loadVehicleModel`, `game::ModelLibrary`), `Showroom::materials` | `geometry/<car>.pkg`: BODY_H, SHADOW_H, whl0-3_H with `.mtx` pivots, the 12 extra names + `_H`, all paint jobs' shaders with `_dmg` textures cleaned and material alphas forced (diffuse/ambient/specular 1, emissive 0); `mmDamage::Init`. |
| `mmVehicleForm::Update` | ported (new) | `Frontend::drawScene` (the garage's scene pass) | declares the form to the cull manager when it has a body or shadow. |
| `mmVehicleForm::Cull` | ported (new) | `Showroom::draw` | shadow, body, refl_showroom pass (1.0), wheels and extras (package order) at their pivots, lit, depth-tested with depth writes, alpha-blended, alpha test "not 0" (the frontend's render state, inferred); ignores the +0x40 tint. |
| `mmVehicleForm::~mmVehicleForm`, ``mmVehicleForm::`vector_deleting_destructor'``, ``mmVehicleForm::`scalar_deleting_destructor'`` | not needed | — | memory plumbing (the scalar one is unreachable). |
| `mmVehicleForm::LoadAllModLOD` | not needed | — | unreachable (no callers): would load `<name>_H/_M/_L/_VL`. |

## mmCompDRecord

One row of the driver record list (`Dialog_DriverRec`'s
`UICompositeScroll`): lock picture (`lock`, 3 frames) and race, time,
vehicle and, for professionals, points, in white (colour 2). Status 0
(locked) draws frame 1, 1 (open, not passed) frame 0, 2 (passed) frame 2,
3 px above the text row; columns from `SetSubwidgetGeometry` (lock at 2 px,
text at 18 px, time a third of the row further, vehicle a quarter further
for professionals or a third less 26 px for amateurs, points a quarter
further). OpenMM2: `app/frontend/PagesMain.cpp` `DriverStatsDialog::drawAbove`
(verified and fixed by the first audit).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmCompDRecord::mmCompDRecord`, `mmCompDRecord::~mmCompDRecord`, ``mmCompDRecord::`scalar_deleting_destructor'`` | not needed | — | memory plumbing. |
| `mmCompDRecord::Init`, `mmCompDRecord::LoadBitmap` | ported | `app/frontend/PagesMain.cpp` `DriverStatsDialog::drawAbove` | race name, `GetLocTime(time)`, vehicle (or the empty-car text), points `%4d` when a score is given (professionals: 0 for undriven rows), the `lock` sheet of 3 frames. |
| `mmCompDRecord::InitTitle` | ported | `app/frontend/PagesMain.cpp` `DriverStatsDialog::drawAbove` (titles) | RACE 345, TIME 346, VEHICLE 347, POINTS 344 for professionals. |
| `mmCompDRecord::SetGeometry`, `mmCompDRecord::SetSubwidgetGeometry`, `mmCompDRecord::SetPosition`, `mmCompDRecord::SetBltXY` | ported | `app/frontend/PagesMain.cpp` `DriverStatsDialog::drawAbove` | columns above; lock blitted at the row's left, 3 px up. |
| `mmCompDRecord::Cull`, `mmCompDRecord::Update`, `mmCompDRecord::Reset` | ported | `app/frontend/PagesMain.cpp` `DriverStatsDialog::drawAbove` (`drawSpriteFrame`) | frame mapping above; `Update` declares the bitmap unless blitting is disabled. |
| `mmCompDRecord::DisableBlt` | not needed | — | empty override (the lock can never be disabled; the list never scrolls). |

## mmCompRaceRecord

One row of the race records (`Dialog_HallOfFame`): race, driver, time or
score, vehicle (columns at 4 px, then a quarter, 0.2857 and 0.1923 of the
row less 26 px), filtered by `SelectIfRaceType` (mode in bits 8-15 and
table in bits 16-23 of the row's type). It loads a `passed` picture but its
`Update` never declares it, so `Cull` never runs. OpenMM2:
`app/frontend/PagesMain.cpp` `RaceRecordsDialog::drawAbove`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmCompRaceRecord::mmCompRaceRecord`, `mmCompRaceRecord::~mmCompRaceRecord`, ``mmCompRaceRecord::`scalar_deleting_destructor'`` | not needed | — | memory plumbing. |
| `mmCompRaceRecord::Init`, `mmCompRaceRecord::InitTitle` | ported | `app/frontend/PagesMain.cpp` `RaceRecordsDialog::drawAbove` | texts (empty vehicle -> the default text), titles RACE 351, DRIVER 353, TIME 354 / 350 or SCORE 349, VEHICLE 355. |
| `mmCompRaceRecord::SetGeometry`, `mmCompRaceRecord::SetSubwidgetGeometry`, `mmCompRaceRecord::SetPosition` | ported | `app/frontend/PagesMain.cpp` `RaceRecordsDialog::drawAbove` (`driverX`, `valueX`, `vehicleX`) | columns verified by the first audit; white text. |
| `mmCompRaceRecord::SelectIfRaceType` | ported | `app/frontend/PagesMain.cpp` `RaceRecordsDialog::drawAbove` (`m_mode`, `m_table`) | rows of the chosen mode and table only. |
| `mmCompRaceRecord::LoadBitmap`, `mmCompRaceRecord::Cull` | not needed | — | the `passed` picture is never drawn (`Update` does not declare it); OpenMM2 correctly draws none. |
| `mmCompRaceRecord::Update`, `mmCompRaceRecord::Reset` | ported | `app/frontend/PagesMain.cpp` `RaceRecordsDialog::drawAbove` | update/reset children only. |

## mmCompBase

The base of the composite-scroll rows (`mmCompDRecord`, `mmCompRaceRecord`,
`mmCompCity`): geometry and blit position setters and empty event hooks.
OpenMM2 draws the rows directly in the dialogs' `drawAbove`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmCompBase::SetGeometry`, `mmCompBase::SetBltXY`, `mmCompBase::DisableBlt`, `mmCompBase::Switch`, `mmCompBase::Update` | ported | `app/frontend/PagesMain.cpp` `DriverStatsDialog::drawAbove`, `RaceRecordsDialog::drawAbove` | store the row rectangle and blit point, disable the blit (+0x3c), clear the focus flag, update active children. |
| `mmCompBase::Action` (both overloads), `mmCompBase::CaptureAction`, `mmCompBase::EvalMouseXY`, `mmCompBase::Highlight`, `mmCompBase::Box`, `mmCompBase::Reset` | not needed | — | empty virtual defaults: the rows take no input and draw no highlight. |
| `mmCompBase::~mmCompBase`, ``mmCompBase::`scalar_deleting_destructor'`` | not needed | — | memory plumbing. |

## mmCompCity

One row of `Dialog_City2`'s city list (name and the city's blitz,
checkpoint and circuit race counts). Only `Dialog_City2` creates it, and
that dialog is never opened (below).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmCompCity::mmCompCity`, `mmCompCity::Init`, `mmCompCity::Reset`, `mmCompCity::Update`, `mmCompCity::SetGeometry`, `mmCompCity::SetSubwidgetGeometry`, `mmCompCity::SetPosition`, `mmCompCity::Highlight`, `mmCompCity::Box`, `mmCompCity::Cull`, `mmCompCity::~mmCompCity`, ``mmCompCity::`vector_deleting_destructor'`` | not needed | — | built by `Dialog_City2::Dialog_City2` for a list nobody sees. |
| `mmCompCity::InitTitle`, `mmCompCity::SetTitleGeometry`, ``mmCompCity::`scalar_deleting_destructor'`` | not needed | — | unreachable (no callers). |

## Dialog_DriverRec

The driver record dialog (dialog 19, drec_dlg): a composite scroll (0.0578,
0.1674, 0.8 wide, no scroll bar) of `mmCompDRecord` rows for every race of
every mode, filtered by the mode check box (blitz, circuit, checkpoint;
starts on blitz), city check boxes (San Francisco, London; `SetCityState`
calls the city callback that refills the rows through
`mmInterface::PlayerFillRecords`), DONE (id 100). OpenMM2:
`app/frontend/PagesMain.cpp` `RecordDialog` + `DriverStatsDialog`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `Dialog_DriverRec::Dialog_DriverRec` | ported | `app/frontend/PagesMain.cpp` `RecordDialog::RecordDialog` | check boxes and DONE in widget.csv order; opens on blitz, San Francisco. |
| `Dialog_DriverRec::~Dialog_DriverRec`, ``Dialog_DriverRec::`scalar_deleting_destructor'`` | not needed | — | memory plumbing. |
| `Dialog_DriverRec::InitDriverRecord` | ported | `app/frontend/PagesMain.cpp` `DriverStatsDialog::drawAbove` | titles, POINTS only for professionals. |
| `Dialog_DriverRec::AddDriverRecord` | ported | `app/frontend/PagesMain.cpp` `fit`, `DriverStatsDialog::drawAbove` | race name cut to 16 characters + "..." when wider than 0.207 of the screen (string 664), car to 11 + "..." when wider than 0.15 (665), no length check (a wide short text also gets "..."), as `fit` does; amateurs get no score. |
| `Dialog_DriverRec::ResetDriverRecord` | ported | `app/frontend/PagesMain.cpp` `DriverStatsDialog::drawAbove` (rows rebuilt every frame) | empties the list. |
| `Dialog_DriverRec::PreSetup`, `Dialog_DriverRec::SetSortState` | ported | `app/frontend/PagesMain.cpp` `RecordDialog` mode radios, `drawAbove` | shows the rows of the chosen mode, scroll to the top. |
| `Dialog_DriverRec::SetCityState` | ported | `app/frontend/PagesMain.cpp` `RecordDialog` city radios (`m_city`) | refill for the city, then `SetSortState`. |
| `Dialog_DriverRec::SetRecordPosition` | ported | — (no visible effect) | `PlayerFillRecords` scrolls to the current race's row, but `SetSortState` / `PreSetup` reset the position to 0 before the list is seen, and 12 rows always fit (inferred). |

## Dialog_HallOfFame

The race records dialog (dialog 20, hoff_dlg): a composite scroll (0.0578,
0.177, 0.9 wide) with a vertical scroll bar of `mmCompRaceRecord` rows,
table check boxes (amateur times, pro times, pro points; starts on amateur
times), mode check boxes (starts on blitz), city check boxes (`SortByCity`
-> `mmInterface::HOFFillRecords`), DONE (id 100). OpenMM2:
`app/frontend/PagesMain.cpp` `RecordDialog` + `RaceRecordsDialog`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `Dialog_HallOfFame::Dialog_HallOfFame` | ported | `app/frontend/PagesMain.cpp` `RecordDialog::RecordDialog`, `RaceRecordsDialog::RaceRecordsDialog` | check boxes, DONE, scroll bar (OpenMM2: two arrows, positions inferred, plus the mouse wheel). |
| `Dialog_HallOfFame::~Dialog_HallOfFame`, ``Dialog_HallOfFame::`scalar_deleting_destructor'`` | not needed | — | memory plumbing. |
| `Dialog_HallOfFame::InitRaceRecord` | ported | `app/frontend/PagesMain.cpp` `RaceRecordsDialog::drawAbove` (titles) | RACE, DRIVER, TIME, VEHICLE in white. |
| `Dialog_HallOfFame::AddRaceRecord` | ported | `app/frontend/PagesMain.cpp` `fit`, `RaceRecordsDialog::drawAbove` | race cut at 0.15 (666), vehicle at 0.207 (667) as in OpenMM2; the driver column differs (finding C.5). |
| `Dialog_HallOfFame::ResetRaceRecord` | ported | `app/frontend/PagesMain.cpp` `RaceRecordsDialog::drawAbove` (rows rebuilt) | empties the list. |
| `Dialog_HallOfFame::PreSetup`, `Dialog_HallOfFame::SetSortState` | ported | `app/frontend/PagesMain.cpp` `RaceRecordsDialog::drawAbove` | rows of the mode and table, scroll to the top, third title 349 SCORE for pro points else 350 TIME. |
| `Dialog_HallOfFame::SortByCity` | ported | `app/frontend/PagesMain.cpp` `RecordDialog` city radios | refill for the city, then `SetSortState`. |

## Dialog_City2

A "City Selection" dialog (id 0x23 = 35: a list of `mmCompCity` rows with
DONE / CANCEL), created by `mmInterface::mmInterface` (pointer at
mmInterface +0x20). No code opens it: the only pushes of 0x23 are its
construction, every `MenuManager::OpenDialog` call passes another constant,
and the only use of the pointer is `mmInterface::PlayerSetState` calling
`SetCurrentCity`, which (through `DoneCB`) switches the current city to the
driver's, reloads the race names and copies the city names. That part is
live.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `Dialog_City2::SetCurrentCity`, `Dialog_City2::DoneCB` | ported | `app/frontend/FrontendScreen.cpp` `Frontend::configFromProfile` | the driver's city becomes the current city (`config.city`); race names follow from `Frontend::racesFor`. |
| `Dialog_City2::Dialog_City2`, `Dialog_City2::PreSetup`, `Dialog_City2::PostSetup`, `Dialog_City2::ScrollCB`, `Dialog_City2::CancelCB`, `Dialog_City2::~Dialog_City2`, ``Dialog_City2::`scalar_deleting_destructor'`` | not needed | — | the dialog is never opened in build 3393 (see above). |

## Dialog_RaceEnvironment

A "Race Environment" dialog (id 0x16 = 22): time lamps renv_imorn/inoon/
isuns/inite, weather lamps renv_isun/ifog/isnow/irain (with the Ui* sounds),
traffic/cop/pedestrian sliders, a pedestrians check box, DONE / CANCEL.
Created by `mmInterface::mmInterface` (pointer at mmInterface +0x9c, never
read again); no code opens it (no push of 0x16 except the construction, no
`OpenDialog` with 22). Confirms the first audit.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `Dialog_RaceEnvironment::Dialog_RaceEnvironment`, `Dialog_RaceEnvironment::PreSetup`, `Dialog_RaceEnvironment::ControlPedSlider`, `Dialog_RaceEnvironment::DoneCallback`, `Dialog_RaceEnvironment::~Dialog_RaceEnvironment`, ``Dialog_RaceEnvironment::`scalar_deleting_destructor'`` | not needed | — | never opened: copies the environment in (`PreSetup`) and out (`DoneCallback`: weather, time, cop / pedestrian / traffic density, pedestrians on) of a dialog nobody sees. |
| `Dialog_RaceEnvironment::CancelCallback`, `Dialog_RaceEnvironment::SetMultiRaceOptions` | not needed | — | unreachable (no callers). |

## NetArena

The multiplayer lobby, menu 12 ("Network Arena"), created once in `mmInterface::mmInterface` and entered by
`mmInterface::Switch(0xc)` after hosting or joining. Widgets in creation order (= widget.csv menu 12): 0 chat entry
(274,274,355x20), 1 roster composite scroll (274,64,355x120, 8 rows of `mmCompRoster`), 2 SELECT VEHICLE `lobb_veh`
id 100 (508,380), 3 HOST SETTINGS `lobb_hst` id 101 (395,380), 4 `lobb_red` (474,238), 5 `lobb_blu` (474,207, with
`lobb_tem` as its child at 474,191), 6 GO/READY `lobb_rdy` id 9999 (439,415), 7 race-map icon (22,194,242x184), 8
EJECT `lobb_ejt` id 1001 (279,380). Text nodes: host settings (36,66, 223 wide, 6 lines used), city name (36,396,
226x34) under the map, YOU (274,212) and a 3-line chat log (274,300,355x66). OpenMM2: `app/frontend/PagesMulti.cpp`
`LobbyPage` over `game::NetGame`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `NetArena::NetArena` | ported | `PagesMulti.cpp` `LobbyPage::LobbyPage` | chat entry, roster, team lamps, `lobb_tem` and GO/READY at MM2's places; the three buttons in their row, the race map and the city name too (finding D.2). |
| `NetArena::SetHost` | ported | `LobbyPage::LobbyPage` | host: `lobb_srt`, HOST SETTINGS and EJECT shown; joiner: `lobb_nr` (or `lobb_jn` after a late join), both hidden. MM2 also calls it at run time on host migration (finding D.9). |
| `NetArena::SetMyStatus`, `NetArena::EnablePlayButton` | ported | `LobbyPage::update` | joiner button swaps `lobb_rdy`/`lobb_nr` (not while late-joining or host). the host's GO is never greyed (finding D.7); a joiner's ready is reset when the host changes the settings and when SELECT VEHICLE opens (finding D.4). |
| `NetArena::SyncJoin` | ported | (implicit) | normal join: READY button `lobb_nr`; OpenMM2 only has this state. |
| `NetArena::LateJoin` | open | — | Cruise / Cops & Robbers sessions stay open during a race; a joiner gets `lobb_jn` and pressing it enters the running race (`mmInterface::Update` lobby, id 9999 → `MultiStartGame`). OpenMM2 refuses joins in progress: it needs the session to stay open for those modes and the race to spawn a late joiner (finding D.5). |
| `NetArena::SetTeamWidgets`, `NetArena::TeamCallback` | ported | `LobbyPage::update`, `LobbyPage::setTeam` | team buttons only for C&R with a team type; `lobb_cop`/`lobb_rob` (Cops vs. Robbers) or `lobb_blu`/`lobb_red` (Robber Teams); verified by the first audit. |
| `NetArena::ShowRosterTeam` | ported | `LobbyPage::drawAbove` (PLAYERS) | the rows' team dots in the team games (finding D.6). |
| `NetArena::AddRosterName` (LocString overload), `NetArena::ResetRoster`, `NetArena::ChangeRosterData` | ported | `LobbyPage::drawAbove` (PLAYERS) from `NetGame::players()` | one row per player: name, car description; names wider than 0.09 of the screen are cut to "%.6s..." (finding D.6). |
| `NetArena::SetStatus`, `NetArena::GetStatus` | ported | `LobbyPage::drawAbove`, `game/net/NetGame.cpp` `NetGame::everyoneReady` | ready flag per row; `GetStatus` feeds `mmInterface::MultiAllReady` (everyone but the host ready; a lone host may start). The error flag (`mmCompRoster::SetError`, locale missing) has no counterpart. |
| `NetArena::PostHostSettings`, `NetArena::GetRaceName` | ported | `LobbyPage::drawAbove` (HOST SETTINGS), `raceTitle` | MM2's six white lines (finding D.3); the red "Race locale: ERROR" mode (argument 1) is open with `mmCompRoster::SetError`. |
| `NetArena::PostPlayerInfo` | ported | `LobbyPage::drawAbove` (YOU) | "Driver: ", "Car: ", "Color: " (428-430) + name, car description, colour name. |
| `NetArena::ChatEntry`, `NetArena::RetrieveChatLine` | ported | `PagesMulti.cpp` `ChatEntry::modalInput`, `NetGame::sendChat` | prompt 398 in the field; Enter sends unless the field still holds the prompt, adds the own line locally as "%s> %s", clears the field. Field max 128 chars. Inferred: MM2 sends an empty "Name> " line on Enter in an empty field; OpenMM2 drops blank lines. |
| `NetArena::AddGameChatLine`, `NetArena::PostChatMessages`, `NetArena::ResetGameChat` | ported | `NetGame::addSystemLine`, `NetGame::handleEvents`, `LobbyPage::drawAbove` (chat log) | ring of 3 lines, each stored as " %s" (leading space), empty lines ignored, top-filled then newest at the bottom; reset (and the prompt restored) whenever the lobby is entered except from vehicle select, options or host settings (finding D.10). |
| `NetArena::LoadRaceMap` | ported (new) | `LobbyPage` race map `Picture`, `mapPicture` | lobby race map `<city>_map` + `roam` / `race%d` / `multicop` / `circuit%d` / `blitz%d` (dgGameModeNames, raceId) at (22,194,242x184), hidden when the file is missing. |
| `NetArena::AddRosterName` (char* overload), `NetArena::FindRosterName`, `NetArena::RemoveRosterEntry`, `NetArena::RemoveRosterName`, `NetArena::DisablePlayButton` | not needed | — | unreachable (no callers). |
| `NetArena::PreSetup`, `NetArena::~NetArena`, ``NetArena::`scalar_deleting_destructor'`` | not needed | — | empty / destructors. |

## mmCompRoster

One lobby roster row (`mmCompBase` in the NetArena composite scroll), made by `NetArena::AddRosterName`. Layout
(`SetSubwidgetGeometry`): 2 px pad, a 24 px icon column, name at +26 px, car description at +26 + width/3, a third
column at +26 + 2·width/3 that `Init` never fills (the colour argument is dropped, `SetColor` is empty). `Cull` blits
the "ready" bitmap (or "error" after `SetError`) at the row's x + 2 px when ready/error, and the team dot (`blue_dot`
team 0, `red_dot` team 1) 2 px right of it when team display is on. OpenMM2 draws the rows in
`LobbyPage::drawAbove`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmCompRoster::mmCompRoster`, `mmCompRoster::Init`, `mmCompRoster::SetCar`, `mmCompRoster::SetGeometry`, `mmCompRoster::SetSubwidgetGeometry`, `mmCompRoster::SetPosition`, `mmCompRoster::SetBltXY` | ported | `PagesMulti.cpp` `LobbyPage::drawAbove` (PLAYERS) | name at x 300 and car at 418 (274 + 26, then a third of 355 on); row colour `MenuManager::GetFGColor(2)`. |
| `mmCompRoster::SetReady`, `mmCompRoster::SetTeam` | ported | `LobbyPage::drawAbove` | the ready icon and the team dot (finding D.6). |
| `mmCompRoster::LoadBitmap`, `mmCompRoster::LoadTeamBitmap`, `mmCompRoster::Update`, `mmCompRoster::Cull` | ported (new) | `LobbyPage::drawAbove` (PLAYERS) | the `ready` icon at the row's x + 2 while ready (the host always), the team dot (`blue_dot` team 0, `red_dot` team 1) its width + 2 px further in the team games. |
| `mmCompRoster::SetError` | open | — | the `error` icon for a player who lacks the race's locale; OpenMM2 has no such check (needs a per-player "race locale installed" flag in the session). |
| `mmCompRoster::InitTitle`, `mmCompRoster::SetColor` | not needed | — | empty bodies (InitTitle also unreachable). |
| `mmCompRoster::SetName` | not needed | — | unreachable. |
| `mmCompRoster::Reset`, `mmCompRoster::~mmCompRoster`, ``mmCompRoster::`scalar_deleting_destructor'`` | not needed | — | child reset / destructors. |

## NetStartArray

The multiplayer start grid: 10 player-id slots in `mmStatePack`. The host's `mmInterface::SendStartMsg` clears it,
calls `AssignOpenIndex` for every player in player order (packed 0..n-1) and sends it (message 0x1f9); joiners copy
it with `Init`; every mode's `InitNetworkPlayers` reads the local slot with `GetIndex` (0 when absent) for
`mmGameMulti::StartXYZ` (races always; Cruise and C&R only when the random respawn fails).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `NetStartArray::NetStartArray`, `NetStartArray::Clear`, `NetStartArray::Init`, `NetStartArray::AssignOpenIndex`, `NetStartArray::GetIndex` | ported | `app/RaceScreen.cpp` (multiplayer spawn), `game/session/RaceSetup.cpp` `multiplayerGridOffset` | slot = the player's place in the host's player list (`RaceScreen::startSlot`, game flow's; finding D.17). |
| `NetStartArray::ClearIndex` | not needed | — | unreachable. |
| `NetStartArray::~NetStartArray` | not needed | — | destructor. |

## NetSelectMenu

The provider / sessions screen, menu 10 (`sess_bk`), entered from the main menu's MULTIPLAYER. Creation order =
widget.csv menu 10: provider lamps `sess_zn` (MSN Zone, id 0x6c), `sess_ipx` (100), `sess_tcp` (0x66), `sess_ser`
(0x67), `sess_mod` (0x68) at x 40, y 54..162; 5 the provider description label (40,396, pictures
`mpst_tpx|tzn|tcp|tsr|tmd`); 6 NET NAME field (404,70); 7 HOST `sess_hst` (439,98); 8 JOIN `sess_jn` (439,167); 9 the
session text scroll (289,243,329x131, 10 rows "Session1..10"); 10 the BACK hot spot; plus the "Looking for games..."
label (657) at (40,396). DirectPlay provider choice, comm packs and session enumeration are replaced by OpenMM2's
UDP/ENet LAN discovery: `PagesMulti.cpp` `SessionsPage`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `NetSelectMenu::NetSelectMenu` | ported | `PagesMulti.cpp` `SessionsPage::SessionsPage` | lamps, name box, HOST, JOIN and the session box at the csv places; OpenMM2's list rows are its own (finding D.16). |
| `NetSelectMenu::FocusDescription` | ported | `ui::LampItem::help` (mpst_* at `helpPos` 40,396) | the five provider pictures match; MM2 shows no picture for HOST / JOIN (finding D.16). |
| `NetSelectMenu::HostCB`, `NetSelectMenu::JoinCB`, `NetSelectMenu::GetHostJoin` | ported | `SessionsPage` HOST / JOIN buttons | HOST opens Host Options (dialog 36); JOIN joins the selected session or asks for an address. |
| `NetSelectMenu::JoinCallback` | ported | `SessionsPage` `m_list->onDoubleClick` | picking a session joins it (`mmInterface::NetJoinCB` → `JoinLAN`). |
| `NetSelectMenu::SetNetname`, `NetSelectMenu::NetNameCB` (+ piece 0x505024) | ported | `SessionsPage` name `TextEntry::onCommit` | commits to the driver (`mmInterface::NetNameCB`: `mmPlayerData::SetNetName`, main menu). 12 characters (finding D.15). |
| `NetSelectMenu::EnableSearchLabel` | ported | `SessionsPage::drawAbove` | "Looking for games..." (657) blinking at 40,396 while nothing is found (OpenMM2 searches all the time; finding D.16). |
| `NetSelectMenu::PreSetup` | replaced | `SessionsPage` lamps (`available` flags) | MM2 kills/unkills IPX, TCP/IP, modem by `asNetwork::GetNetworkCaps` on first entry; OpenMM2 offers TCP/IP only. |
| `NetSelectMenu::SetIPXButton`, `NetSelectMenu::SetModemButton`, `NetSelectMenu::SetSerialButton`, `NetSelectMenu::SetTCPNetButton` | replaced | `SessionsPage` lamps | provider availability. |
| `NetSelectMenu::ProtocolBack`, `NetSelectMenu::ClearProtocol` | replaced | — | DirectPlay provider state; OpenMM2 has none. |
| `NetSelectMenu::BuildComs` (+ jump table 0x504cbe), `NetSelectMenu::SetComs`, `NetSelectMenu::GetCommPack`, `NetSelectMenu::SetIPAddress`, `NetSelectMenu::SetPhoneNumber`, `NetSelectMenu::IPAddressCallback` | replaced | `net::Address::resolve`, `NetGame::join` | NETCOMMPACK (IP, phone, serial port/baud/stop/flow/parity). The last address is kept per driver (see Dialog_TCPIP). |
| `NetSelectMenu::GetSessions`, `NetSelectMenu::GetSessionID`, `NetSelectMenu::SetSession` | replaced | `SessionsPage::rows`, `NetGame::lanSessions` | MM2 lists up to 10 session names (first field of the session string), "....." in empty rows, "ERROR: Network versions do not match." (83) for other versions; OpenMM2 adds players, city, password, racing, ping. |
| `NetSelectMenu::SetDescription`, `NetSelectMenu::ShowTCPIPNetSessions`, `NetSelectMenu::DisableSessions`, `NetSelectMenu::SetTCPLocalButton`, `NetSelectMenu::WidgetSwitch` | not needed | — | empty bodies. |
| `NetSelectMenu::PostSetup` | not needed | — | resets the nav bar's BACK position that PreSetup moved to the hot spot (both 290,415). |
| `NetSelectMenu::AddModem`, `NetSelectMenu::ReparentWidgets`, `NetSelectMenu::AddWidgetToList` | not needed | — | unreachable (no callers). |
| `NetSelectMenu::~NetSelectMenu`, ``NetSelectMenu::`scalar_deleting_destructor'`` | not needed | — | destructors. |

## Dialog_TCPIP

"Enter an Address" (dialog 14, `tcp_dlg`): TCP/IP address field (72,90,203x22, 15 chars), Cancel `dlg_can` id 0x66
at (18,276), DONE `dlg_done` id 0x65 at (280,276), relative to the centred card. Opened by JOIN with the TCP/IP
provider; DONE (or Enter) stores the address and enumerates that host's sessions into the list. OpenMM2:
`PagesMulti.cpp` `AddressDialog` joins the address directly (host name and `ip:port` allowed, 40 chars).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `Dialog_TCPIP::Dialog_TCPIP` | ported | `PagesMulti.cpp` `AddressDialog::AddressDialog` | field, Cancel (left) and DONE (right) at MM2's places on the centred card (finding D.11). |
| `Dialog_TCPIP::IPAddressCallback`, `Dialog_TCPIP::PreSetup` | ported | `AddressDialog::go` | MM2: Enter in the field acts as DONE (flag read by `mmInterface::Update` case 0xe); so does OpenMM2's (finding D.12). Blank = search again in both. |
| `Dialog_TCPIP::SetIPAddress` | ported (new) | `AddressDialog`, `Frontend::tcpAddress`, `game::Profile::address` | prefilled with the driver's last address (`mmInterface::PlayerSetState`, mmPlayerData +0x100), saved with the event when a race starts (BeDone). |
| `Dialog_TCPIP::~Dialog_TCPIP`, ``Dialog_TCPIP::`scalar_deleting_destructor'`` | not needed | — | destructors. |

## Dialog_Serial

"Setup Serial Communications" (dialog 16, `srl_dlg`): PORT, BAUD RATE, STOP BITS, FLOW CONTROL, PARITY drop-downs,
Cancel, DONE; feeds DirectPlay's serial provider (`mmInterface::JoinSerial`, `SetProtocol2`). OpenMM2 has no
serial link; its SERIAL lamp is disabled.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `Dialog_Serial::Dialog_Serial`, `Dialog_Serial::PreSetup`, `Dialog_Serial::BuildComs` (+ piece 0x4fe45a), `Dialog_Serial::GetCommPack` | replaced | `SessionsPage` (serial lamp disabled) | DirectPlay serial transport. |
| `Dialog_Serial::IPAddressCallback` | not needed | — | unreachable. |
| `Dialog_Serial::~Dialog_Serial`, ``Dialog_Serial::`scalar_deleting_destructor'`` | not needed | — | destructors. |

## Dialog_Host

"Host Options" (dialog 36, `host_dlg`): Password field (72,91,203x22, 25 chars), Max Players roller (248,156,60x32)
over strings 590-597 ("1".."8", index 0..7, initial 8 = "8"), Cancel id 0x67 (18,276), DONE id 0x66 (280,276).
`mmInterface::Update` case 0x24: DONE → `CreateSession` (max players = index + 1) → lobby; Cancel → `Clear`.
OpenMM2: `PagesMulti.cpp` `HostOptionsDialog`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `Dialog_Host::Dialog_Host` | ported | `PagesMulti.cpp` `HostOptionsDialog::HostOptionsDialog` | password (25 chars) and max players 2-8 (MM2 allows 1-8, finding D.14); buttons as MM2's (finding D.11). |
| `Dialog_Host::PreSetup`, `Dialog_Host::Clear` | ported (new) | `HostOptionsDialog` (`s_lastPassword`, `s_lastMaxPlayers`) | the dialog lives as long as the game: the last accepted password and max players come back, Cancel drops what was typed. |
| `Dialog_Host::PasswordCallback` | not needed | — | sets a flag nothing reads (no reads of Dialog_Host +0x110 in mmInterface; inferred from the asm). |
| `Dialog_Host::~Dialog_Host`, ``Dialog_Host::`scalar_deleting_destructor'`` | not needed | — | destructors. |

## Dialog_Password

"Enter a valid password" (dialog 25, `pass_dlg`): Password field (72,91,203x22, 25 chars), Cancel id 0x66 (18,276),
DONE id 0x65 (280,276). Opened when a join attempt answers "password needed" (`mmInterface::JoinLAN` →
`JoinSession` returns 2); DONE or Enter → `JoinPasswordSession`; a wrong password opens `badpass_dlg` (24), whose OK
re-opens this dialog (`mmInterface::Update` cases 0x18 / 0x19). OpenMM2: `PagesMulti.cpp` `PasswordDialog`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `Dialog_Password::Dialog_Password` | ported | `PagesMulti.cpp` `PasswordDialog::PasswordDialog`, `joinSession` | asked when a join answers that a password is needed (`NetGame::joinFailure`), listed or by address; a wrong one shows `badp_dlg` and asks again (finding D.1). |
| `Dialog_Password::PasswordCallback`, `Dialog_Password::PreSetup` | ported | `PasswordDialog` OK button | Enter submits (finding D.12). The typed password survives between attempts in MM2 (PreSetup only clears the Enter flag). |
| `Dialog_Password::~Dialog_Password`, ``Dialog_Password::`scalar_deleting_destructor'`` | not needed | — | destructors. |

## Dialog_Eject

"Player Eject" (dialog 42, `ejct_dlg`), opened by the host's EJECT (lobby id 1001) after
`mmInterface::MultiFillRoster` (`ClearNames` + `AddName` for every player but the host). DONE `dlg_done` id 0x65 at
(280,288) and 8 text buttons (initially "   "), one per name. Clicking a name (`BootButtonCB`) calls the boot
callback (`mmInterface::BootPlayerCB`: host only, not self → `asNetwork::BootPlayer`), removes the name and keeps the
dialog open. OpenMM2: `PagesMulti.cpp` `EjectDialog`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `Dialog_Eject::Dialog_Eject`, `Dialog_Eject::ClearNames`, `Dialog_Eject::AddName`, `Dialog_Eject::PostNames`, `Dialog_Eject::RemoveName` (ulong), `Dialog_Eject::FindRosterName` (ulong) | ported | `PagesMulti.cpp` `EjectDialog::EjectDialog`, `EjectDialog::others`, `EjectDialog::names` | list of the other players (max 8 in MM2, names copied to 32 chars). Picking a name boots that player and the dialog stays open (finding D.13). |
| `Dialog_Eject::BootButtonCB` (+ piece 0x4f95cf), `Dialog_Eject::SetBootCB` | ported | `EjectDialog` (`ListBox::onPick`) → `NetGame::kick` | boots on picking a name, as MM2. |
| `Dialog_Eject::FindRosterName` (char*), `Dialog_Eject::RemoveName` (char*, ulong) | not needed | — | unreachable. |
| `Dialog_Eject::PostSetup`, `Dialog_Eject::PreSetup` | not needed | — | empty bodies. |
| `Dialog_Eject::~Dialog_Eject`, ``Dialog_Eject::`scalar_deleting_destructor'`` | not needed | — | destructors. |

## PURoster

The in-race "Roster" popup page (`mmPopup` menu 10, string 500, no title), built in `mmPopup::mmPopup`. Eight
full-width text buttons 0.1 apart under the title plus an Exit button; `mmGameMulti::InitRoster` adds the local
player first, then every other player, the host as " %s (%s)" with string 501 "Host"; late joiners are added and
leavers removed (`mmGameMulti::SystemMessageCB`, `mmMulti*::SystemMessage` → `RemoveName`). Clicking a name calls
`mmGameMulti::BootPlayerCB` (host only, not self → `asNetwork::BootPlayer`). Opened with F6 during a network race
(`mmGame::UpdateDebugInput` → `mmPopup::ShowRoster`, inferred DirectInput key 0x40) and by "Show Roster" (494) on the
network results popup (`mmPopup::Update` → `ForceRoster`). OpenMM2: `PopupOptions::buildRoster`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `PURoster::PURoster`, `PURoster::Reset`, `PURoster::ClearNames`, `PURoster::AddName`, `PURoster::PostNames`, `PURoster::RemoveName` (ulong), `PURoster::FindRosterName` (ulong), `PURoster::SetBootCB`, `PURoster::BootButtonCB` (+ piece 0x50aaa2) | ported (new) | `app/frontend/PopupOptions.cpp` `PopupOptions::buildRoster`, `RaceScreen` (F6, `debugKeys`) | F6 in a network race (mmGame::UpdateDebugInput -> mmPopup::ShowRoster, no pause) opens it: eight type-2 rows from 0.11, 0.1 apart, the local player first and the host as " Name (Host)" (501), the others " Name" (mmGameMulti::InitRoster); the host clicks a row to boot that player (BootButtonCB -> mmGameMulti::BootPlayerCB -> `NetGame::kick`); Resume Driving and Escape close it. The rows follow the session's player list (AddName, RemoveName, FindRosterName). |
| `PURoster::SetHost`, `PURoster::FindRosterName` (char*), `PURoster::RemoveName` (char*, ulong) | not needed | — | unreachable (so the rows' button type never changes; type only changes text effects anyway). |
| `PURoster::~PURoster`, ``PURoster::`scalar_deleting_destructor'`` | not needed | — | destructors. |

## mmPlayerData

The current driver (`MMCURRPLAYER`, 0x6B19F8): name +0x88 (40), net name +0xB0
(40), file name +0xD8 (40), last TCP/IP address +0x100 (40, file version 3
only), difficulty byte +0x128, tag ID float +0x12C, last car +0x130 (80),
paint job +0x180, mode +0x184, race +0x188, "name set" byte +0x18C, city +0x18D
(80), and the open city record (`mmPlayerCityRecord`) at +0x1E0. It is saved as
`players/<playerN>.sav` (version 3, CRC). `mmInterface` (InitPlayerInfo,
PlayerCreate, PlayerLoad, BeDone) and the single-player modes (RegisterFinish,
NextRace) drive it. Every progress query opens a temporary city record from
the file. In OpenMM2 the driver is `game::Profile` (players/<file>.ini) and the
progress rules are `game::Progress` (`game/Profile.*`).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmPlayerData::mmPlayerData`, `mmPlayerData::~mmPlayerData`, ``mmPlayerData::`scalar_deleting_destructor'`` | not needed | | The constructor's values (version 3, mode 1, cleared strings) are overwritten by Load or Reset in mmInterface::InitPlayerInfo before anything reads them. |
| `mmPlayerData::operator=`, `mmPlayerData::GetScore` | not needed | — | Unreachable (no callers). GetScore would sum the record scores of one mode in a city. |
| `mmPlayerData::Reset` | ported | `game/Profile.h` `Profile` member defaults, `game/Profile.cpp` `ProfileStore::create` | Sets difficulty 0 (amateur), an empty car, paint job 0, mode 0 (cruise), race 0, tag 0, and clears the name, file, address and city. The net name is copied from a global buffer (inferred empty), and then PlayerCreate sets string 77 and the current city. OpenMM2 matches: an empty `vehicle`, and `PagesMain.cpp` `NewDriverDialog::create` sets `netName` to 77 and `city` to the current one. |
| `mmPlayerData::GetName`, `mmPlayerData::SetName`, `mmPlayerData::GetNetName`, `mmPlayerData::SetNetName`, `mmPlayerData::GetCity`, `mmPlayerData::SetCity`, `mmPlayerData::GetFileName`, `mmPlayerData::SetFileName` | ported | `Profile::name`, `netName`, `city`, `file` | Plain string copies. SetName also sets the +0x18C "name set" byte, which has no use in OpenMM2. |
| `mmPlayerData::GetTagID`, `mmPlayerData::SetTagID` | not needed | | The tag ties the `.rec` files to their driver (mmPlayerCityRecord::Open refuses another tag). INI profiles do not need it. |
| `mmPlayerData::Load`, `mmPlayerData::LoadBinary`, LoadBinary split piece 0x52807e, `mmPlayerData::Save`, `mmPlayerData::SaveBinary`, `mmPlayerData::ComputeCRC` | replaced | `Profile::load`, `Profile::save` | The .sav file holds the version, CRC, tag, difficulty, car, paint job, mode, race, name, net name, file name and city, plus the address in version 3. A file below version 2 is refused, and a version-2 file gets a cleared address. The piece at 0x52807e is LoadBinary's tail: on a CRC mismatch in a version-3 file it prints "crc failed" and returns 0 without closing the stream; otherwise it closes the stream and returns 1. (Ghidra wrongly shows the Displayf as non-returning.) Save's text branch (flag 0) is never taken: the callers checked pass 1. |
| `mmPlayerData::OpenCityRecord`, `mmPlayerData::CloseCityRecord` | replaced | `Profile::races` (held in memory) | They open players/<city>/<file>.rec into the embedded record. OpenMM2 keeps every city's records in the profile. |
| `mmPlayerData::GetProgress`, `mmPlayerData::ResolveCheckpointProgress`, `mmPlayerData::ResolveCrashProgress`, `mmPlayerData::GetCheckpointProgress` | ported | `game/Profile.cpp` `Progress::openMask`, `Progress::raceOpen` | GetProgress handles type 1 (checkpoint) and type 6 (crash); any other type gives -1, so all races are open. I re-checked the masks. Checkpoint: 0x7 starts open, and each fully passed group of three adds the next group (0x38, 0x1C0, 0xE00). Crash: 0x777 starts open; full 0x7, 0x70 and 0x700 add 0x8, 0x80 and 0x800; all of 0xFFF gives -1. OpenMM2 is identical. If the record cannot be opened, MM2 logs "Can't open city record" and returns -1 (everything open). That only happens with a corrupt or foreign file (PlayerLoad recreates a missing one), so OpenMM2 does not need it. |
| `mmPlayerData::GetPassedMask`, `mmPlayerData::GetNumPassed` | ported | `Progress::passedMask`, `Progress::passedCount` | Both return 0 when the record cannot be opened. |
| `mmPlayerData::GetTotalScore` | ported | `Progress::totalScore(profile, city)`, `Progress::totalScore(profile)` | Sums the scores of the checkpoint and blitz records up to the counts in the opened record. The circuit loop's count instead comes from the driver's embedded record (+0x27C, the last city opened); see finding E.4. mmInterface::PlayerFillStats adds sf and london, which OpenMM2 matches. The modes' RegisterFinish also call it but discard the result. |
| `mmPlayerData::GetTotalPassed` | not needed | | The city's blitz, circuit and checkpoint passes. Only the four modes' RegisterFinish call it, and each throws the result away (checked in the asm). |
| `mmPlayerData::RegisterFinish` | ported | `Progress::record` -> `merge` | Calls the embedded record's NewRecord with the record, the MMSTATE game mode and the race id. Its "changed" result is discarded by every caller. |

## mmPlayerCityRecord

One driver's records for one city: `players/<city>/<playerN>.rec`. The header
holds the version (float 2.0), CRC, tag, the race counts (checkpoint +0x98,
circuit +0x9C, blitz +0xA0, crash +0xA4) and four passed masks (+0x88 to
+0x94). One `mmPlayerRecord` follows per race, in mode order. The file is
created by mmInterface::PlayerInitStats (InitCityRecord with the city's
counts and 13 lessons) for "sf" and "london" when a driver is created or a
record will not open. In OpenMM2 it is `Profile::races`, keyed
`<city>.<mode>.<index>`, with the masks derived from the records' passed flags
(`Progress::passedMask`).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmPlayerCityRecord::mmPlayerCityRecord`, `mmPlayerCityRecord::~mmPlayerCityRecord`, ``mmPlayerCityRecord::`scalar_deleting_destructor'`` | not needed | | Zeroes the masks and sets the header size 0x2C. |
| `mmPlayerCityRecord::Reset` | not needed | — | Unreachable (no callers). The function body is empty. |
| `mmPlayerCityRecord::InitCityRecord` | replaced | absent `Profile::races` entries; `game/Profile.cpp` `Progress::load` (counts) | Writes a file of empty records with the counts. In OpenMM2 a record that does not exist answers as an empty one, and the counts come from `CityProgressInfo`. |
| `mmPlayerCityRecord::Open`, `mmPlayerCityRecord::Close`, `mmPlayerCityRecord::ComputeCRC`, `mmPlayerCityRecord::GetFileOffset` | replaced | `Profile::load`, `Profile::save`, `Profile::raceKey` | Open requires version 2.0, a matching CRC and the driver's tag. On a CRC or tag mismatch it returns 0 but leaves the stream open. Close rewrites the header and masks. GetFileOffset is header + (preceding modes' counts + index) x 0x60. |
| `mmPlayerCityRecord::GetRecord` | ported | `Profile::record` | When a record fails its CRC, MM2 resets it, clears its pass bit and writes it back (file integrity; not needed). |
| `mmPlayerCityRecord::NewRecord` | ported | `game/Profile.cpp` `merge`, `Progress::record` | Checked in the asm. A passed finish first sets the mode's mask bit. If the stored time is exactly 0, the new record is written as it is. Otherwise the lower time wins together with its car (strictly lower, so a tie keeps the old car), the score is the maximum, and passed is ORed. It returns whether the stored record changed (operator==). OpenMM2 is identical, and keeps the pass across the time-0 overwrite as MM2's mask does. |
| `mmPlayerCityRecord::GetNumPassed`, `mmPlayerCityRecord::GetNumRaces`, `mmPlayerCityRecord::GetPassedMask` | ported | `Progress::passedCount`, `Progress::raceCount`, `Progress::passedMask` | GetNumPassed counts mask bits among the mode's first `count` races. Types other than 1, 3, 4 and 6 give 0. |

## mmPlayerRecord

One race's record for a driver: time +0x88 (float), car +0x8C (80), score
+0xDC, passed +0xE0. It takes 0x60 bytes on disk (`SizeOf`). In OpenMM2 it is
`game::RaceRecord` (`game/Profile.h`).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmPlayerRecord::mmPlayerRecord`, `mmPlayerRecord::mmPlayerRecord` (copy), `mmPlayerRecord::Reset` | ported | `game/Profile.h` `RaceRecord` | Everything zero, as in `RaceRecord{}`. The copy constructor passes records by value. |
| `mmPlayerRecord::~mmPlayerRecord`, ``mmPlayerRecord::`scalar_deleting_destructor'`` | not needed | | |
| `mmPlayerRecord::operator=` | not needed | — | Unreachable (no callers).  |
| `mmPlayerRecord::operator==` | not needed | | Compares time, car (strcmp), passed and score, only to compute NewRecord's "changed" result, which every caller discards. |
| `mmPlayerRecord::LoadBinary`, `mmPlayerRecord::SaveBinary`, `mmPlayerRecord::ComputeCRC` | replaced | `Profile::load` / `save` (`[Races]` lines) | On disk: CRC, time, car, passed, score. |

## mmPlayerDirectory

`players/players.dir` (version 6): the count, then each driver's name (40)
and file name (40), then the last driver's name (80). mmInterface uses it to
list, create, remove and pick the startup driver. In OpenMM2 it is
`game::ProfileStore`, which scans players/*.ini (creation order in `Order`)
and keeps the last driver in players.ini.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmPlayerDirectory::mmPlayerDirectory`, `mmPlayerDirectory::~mmPlayerDirectory`, ``mmPlayerDirectory::`scalar_deleting_destructor'`` | not needed | | |
| `mmPlayerDirectory::CreatePlayer`, `mmPlayerDirectory::GetPlayerName` | not needed | — | Unreachable (no callers).  |
| `mmPlayerDirectory::AddPlayer`, `mmPlayerDirectory::FindPlayer` | ported | `game/Profile.cpp` `ProfileStore::create` | Appends at the end (OpenMM2 uses `Order` = max + 1) and refuses an exact, case-sensitive duplicate (strcmp). The check is skipped on an empty directory, where it cannot matter. |
| `mmPlayerDirectory::MakeFileName` | replaced | `ProfileStore::create` (file name from the sanitised driver name) | MM2 uses "player0", "player1" and so on, bumping the number while it matches an existing file. |
| `mmPlayerDirectory::RemovePlayer` | ported | `ProfileStore::remove` | The other drivers keep their order. |
| `mmPlayerDirectory::GetNumPlayers`, `mmPlayerDirectory::GetPlayer` | ported | `ProfileStore::list` | Creation order. |
| `mmPlayerDirectory::GetFileName`, `GetFileName(int)`, `mmPlayerDirectory::NewDirectory`, `mmPlayerDirectory::SetPlayer` | replaced | `Profile::file` | Plumbing for the name and file arrays. GetFileName(int)'s bound check allows index == count (off by one; harmless). |
| `mmPlayerDirectory::GetLastPlayer`, `mmPlayerDirectory::SetLastPlayer` | ported | `ProfileStore::lastUsed`, `setLastUsed`; `app/frontend/FrontendScreen.cpp` startup (falls back to `all.back()`) | If the saved name is gone, MM2 falls back to the last entry, which is the newest driver, and so does OpenMM2. SetLastPlayer ignores an empty name; OpenMM2 never passes one. |
| `mmPlayerDirectory::Load`, `mmPlayerDirectory::LoadBinary`, `mmPlayerDirectory::Save`, `mmPlayerDirectory::SaveBinary` | replaced | `ProfileStore::list` (directory scan), `setLastUsed` | Version 6; a directory with 0 drivers is refused. |

## mmPlayerConfig

The per-driver options file `players/<playerN>.cfg`. Section flags +0x88 are
0x1F. Audio is at +0xC4..+0xD8: effects volume, music volume, balance, flags,
channel count, and a 200-byte device name. Graphics are an `mmGfxCFG` at
+0x8C (0x38 bytes). Controls are at +0x1A0..+0x1D0, followed by 170 custom
bindings (`mmIODev`, +0x1D8). The 12 view-setting bytes at +0x7168 mirror
MMSTATE +0x36C. The Get* functions copy the live globals into the object and
the Set* functions apply it: on driver selection (mmInterface::PlayerSetState,
mmGame::PlayerSetState), and the object is saved at BeDone and NextRace. The
options pages use it for DEFAULT and CANCEL. OpenMM2 keeps audio, graphics and
controls for all drivers in openmm2.ini (`[Audio]`, `[Display]`/`[Graphics]`,
`[Controls]`). The camera, wide angle, dashboard and mirror are per driver in
`Profile` `[Prefs]`; the HUD bytes are in the global `[HUD]` section.

Field map of the controls: +0x1A0 controller (`controls::Options::controller`);
+0x1A4 transmission, 1 = automatic (`Profile::automatic`, per driver as in MM2);
+0x1A8 auto reverse (mmInput +0x18C, `AutoReverse`); +0x1AC POV hat
(mmInput +0x184, the ctrl_pov button, `UsePovHat`); +0x1B0 force feedback
(`ForceFeedback`); +0x1B4 and +0x1B8 FF collision and road force scales
(stored only); +0x1BC "physics realism", 0.75 from Reset (no gameplay reader
found; sent only in the multiplayer player data and replay info; mm2hook calls
it unused; not kept); +0x1C0 mouse sensitivity (mmInput +0x1C0, no option;
`AnalogSteering` uses 1); +0x1C4 dead zone (`DeadZone`); +0x1C8 (file only);
+0x1CC keyboard steering amount (mmInput +0x1A8, 1, no option); +0x1D0
steering sensitivity (mmInput +0x1C4, `Sensitivity`).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmPlayerConfig::mmPlayerConfig`, `mmPlayerConfig::~mmPlayerConfig`, ``mmPlayerConfig::`scalar_deleting_destructor'`` | not needed | | The constructor reads every section from the globals and sets flags 0x1F. |
| `mmPlayerConfig::operator=`, `mmPlayerConfig::SetDefaults`, `mmPlayerConfig::DefaultGraphics` | not needed | — | Unreachable (no callers). SetDefaults only sets flags 0x1F; DefaultGraphics is empty. |
| `mmPlayerConfig::Load`, `mmPlayerConfig::LoadBinary`, `mmPlayerConfig::Save`, `mmPlayerConfig::SaveBinary` | replaced | `app/Settings.cpp` `Settings::load` / `save`, `Profile::load` / `save` | Load: a file whose flags are not 0x1F takes the current globals for every section. Load also flags the input devices as changed. |
| `mmPlayerConfig::GetAudio`, `mmPlayerConfig::SetAudio` | replaced | `Settings` `[Audio]`, `Context::applyAudioSettings` | SetAudio also applies the wave and CD balance and the music volume, and restarts or shuts down the audio manager when flag 0x1 changes (that part is the audio group's). The device name is replaced by the platform's audio device. |
| `mmPlayerConfig::DefaultAudio` | ported | `app/frontend/PagesOptions.cpp` `AudioPage::resetDefaults`, `Settings` defaults | Sets both volumes to 1, balance 0 and clears the device name. The memory test compares GlobalMemoryStatus's dwTotalPhys (bytes) with 64 and 48, so it always takes the top branch: flags 0x473 and 32 channels (High). That is effects, commentary and stereo on, with the music (0x4) and city (0x800) bits off; the 0x800 OR before it is overwritten. AudioOptions::ResetDefaultAction then sets its own lamps (music off, city on) through ToggleMusic and ToggleAmbient (the AudioOptions group's). |
| `mmPlayerConfig::GetControls`, `mmPlayerConfig::SetControls` | replaced | `app/Controls.cpp` `controls::Options::load`, `Profile::automatic`, `app/RaceScreen.cpp` controller fallback (:1936) | SetControls falls back to the keyboard when a saved device type above 0 is not connected; OpenMM2's race does the same. |
| `mmPlayerConfig::DefaultControls` | ported | `app/Controls.h` `controls::Options` defaults, `PagesOptions.cpp` ControlPage defaults | Controller 0, then ControlSetup::ResetDefaultAction runs mmInput::AutoSetup. Automatic, auto reverse on, POV off, force feedback off, FF scales 1, mouse sensitivity 1, dead zone 0.1, steering sensitivity 1. The keyboard steering amount keeps mmInput's current value. |
| `mmPlayerConfig::GetCustom`, `mmPlayerConfig::SetCustom` | replaced | `app/Controls.cpp` `controls::Bindings` (`[Controls]` `Bind.*`) | MM2 copies all 170 bindings of every device into and out of the global IODev table and re-inits mmInput. OpenMM2 keeps keyboard bindings only (see Missing features). |
| `mmPlayerConfig::GetGraphics`, `mmPlayerConfig::SetGraphics` | replaced | `render::DisplaySettings` (`[Display]`), `[Graphics]` keys | mmGfxCFG::Get / Set. |
| `mmPlayerConfig::GetViewSettings`, `mmPlayerConfig::SetViewSettings` | ported | `Profile::camera`, `wideAngle`, `dashboard`, `mirror`; `app/RaceScreen.cpp` `leaveRace`, `loadViewSettings`, `saveViewSettings` | The 12 bytes are: camera +0x7168, map mode +0x7169, wide +0x716A, dash +0x716B, mirror +0x716C, cluster/external view +0x716D, camera-changed +0x716E, opponent icons +0x716F, +0x7170 (no byte reader found), +0x7171, HUD orientation +0x7172 and HUD zoom +0x7173. The first five and the mirror are kept per driver; the HUD bytes go to the global `[HUD]` (documented deviation, session.md). |
| `mmPlayerConfig::DefaultViewSettings`, `mmPlayerConfig::Reset` | ported | `Profile` defaults (camera 0, all off), `game/session/Hud.h` `HudOptions` | Sets camera 0, map mode 0, wide, dash and mirror off, cluster +0x716D = 0, icons +0x716F = 1, and +0x7170 = 0. It leaves the orientation and zoom at the live globals (on and off at startup). Reset also sets physics realism to 0.75. It runs for every new driver (PlayerCreate) and for the first driver in InitPlayerInfo. The cluster default differs: see finding E.2. |

## mmRecord

One Hall of Fame slot: driver name +0xDC (40), car +0x8C (80), passed +0x104
(0/1), and a single value at +0x88. The value is the time in a time slot or,
in a score slot, the score stored as a float. It takes 0x84 bytes on disk. In
OpenMM2 it is `game::HallEntry` (`game/Profile.h`).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmRecord::mmRecord`, `mmRecord::Reset`, `mmRecord::operator=` | ported | `HallEntry` (value copies in `HallOfFame::submit`) | operator= copies name, car, value and passed; NewRecord uses it to shift the list. |
| `mmRecord::~mmRecord`, ``mmRecord::`scalar_deleting_destructor'`` | not needed | | |
| `mmRecord::GetName`, `mmRecord::SetName`, `mmRecord::GetCarName`, `mmRecord::SetCarName`, `mmRecord::GetTime`, `mmRecord::SetTime` | ported | `HallEntry::driver`, `vehicle`, `time` | SetCarName copies with strncpy (80). SetName is an unbounded strcpy into 40 bytes; names are at most 18 characters. |
| `mmRecord::GetScore`, `mmRecord::SetScore` | ported | `HallEntry::score` | Checked in the asm: SetScore stores the integer as a float in +0x88 (the time field), and GetScore truncates it back. |
| `mmRecord::SetPassed` | ported | `HallEntry::passed` | Normalised to 0/1. OpenMM2 fills it with the race's pass (finding E.1). |
| `mmRecord::GetPassed` | not needed | — | Unreachable (no callers). mmInterface::HOFFillRecords reads +0x104 directly. |
| `mmRecord::LoadBinary`, `mmRecord::SaveBinary`, `mmRecord::ComputeCRC` | replaced | `HallOfFame::load` / `save` | On disk: CRC, name, car, value, passed. |

## mmMiscData

The race records ("Hall of Fame") per city and difficulty:
`players/<city>/amateur.dat` and `pro.dat`. The file holds version 1, the
counts (checkpoint, circuit, blitz), then for each race five slot pairs,
time slot then score slot. mmGame::PlayerSetState opens amateur or pro by the
MMSTATE skill level and records only if the open succeeded (mmGame +0x264).
mmSingleRace, mmSingleCircuit and mmSingleBlitz::RegisterFinish call
NewRecord; Crash Course has none. mmInterface::HOFInitRecords (Init) and
HOFFillRecords (Open / GetRecord) serve the RACE RECORDS dialog. In OpenMM2 it
is `game::HallOfFame` in players/records.ini, filled by
`Frontend::recordResult` and shown by `RaceRecordsDialog` (`PagesMain.cpp`).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmMiscData::mmMiscData`, `mmMiscData::~mmMiscData`, ``mmMiscData::`scalar_deleting_destructor'``, `mmMiscData::Reset` | not needed | | Reset zeroes the three counts. |
| `mmMiscData::Init` | replaced | absent `HallOfFame` tables | Creates the file with 5 + 5 empty slots per race. |
| `mmMiscData::Open`, `mmMiscData::Close`, `mmMiscData::GetFileOffset` | replaced | `HallOfFame::load`, `save`, `key` | Open requires version 1. Offset = header + (preceding modes' counts + race) x 10 slots + slot x 2, plus 1 for the score slot. |
| `mmMiscData::GetRecord` | ported | `HallOfFame::table` | A slot of 5 or more, a race beyond the count, or a type other than 1, 3 or 4 gives an empty record (shown as "---" in OpenMM2 too). |
| `mmMiscData::NewRecord` | ported | `game/Profile.cpp` `HallOfFame::submit`, `app/frontend/FrontendScreen.cpp` `Frontend::recordResult` | Checked in the asm. Time list: the entry goes before the first slot whose time is greater than the new one or 0, so equal times stay ahead, and 5 full slots of lower-or-equal times keep it out. Score list: it goes before the first slot with a lower score, so a score of 0 never enters. The rest shift down and the fifth drops out. Each list's entry gets the name, car, passed flag and its own value. The score comes with every circuit lap's call but is non-zero only for lap 0; OpenMM2 matches. A CRC failure while loading resets that slot and every following one (integrity; not needed). The passed argument is the race's pass (finding E.1). |

## mmRewardRecord

One row of a city's reward table: the variant, car id, mode and race number
bytes, plus the message pointer at +4 (8 bytes in all). It is built by
mmRewardList::Init(32) and Load. In OpenMM2 it is `game::Reward`
(`game/Profile.h`), loaded by `Progress::load`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmRewardRecord::mmRewardRecord` | ported | `game/Profile.h` `Reward` | Zeroes the fields. |
| `mmRewardRecord::~mmRewardRecord` | not needed | | Frees the message. |
| ``mmRewardRecord::`vector_deleting_destructor'`` | not needed | — | Unreachable (no callers). mmRewardList's destructor frees the array itself. |

## mmCCData

The Crash Course event table of mmSingleStunt: 10 entries of 0x3C bytes at
+0x76E8, filled row by row by mmSingleStunt::LoadEventFile from
crash<N>data.csv. Each entry holds the event type (+0), checkpoints, time limit,
density, corner speed, chkflags, numopp and the point-list file name (+0x1C,
32 characters). In OpenMM2 it is `game::session::LessonEvent`
(`game/session/RaceSetup.h`), parsed by `city::parseCrashEvents`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmCCData::mmCCData` | ported | `game/session/RaceSetup.h` `LessonEvent` | Sets type 10 and zeroes the next six fields. The defaults only matter for entries LoadEventFile never fills, which the stunt does not use (inferred). OpenMM2's own default (type 7) is likewise never used. |
| `mmCCData::~mmCCData` | not needed | | Empty. |

## dgStatePack

The base of `mmStatePack` (`MMSTATE`, 0x6B1610), the game state block (mm2hook
names, documentation only). The constructor zeroes 0x6C bytes, then sets
traffic, pedestrian and cop density 1.0, opponents 8, three unused 0.5 values
and a flag byte (MC1 leftovers), max ambient 100, cable cars and subways on,
texture quality 1, field +0x50 = 99, and registers `dgStatePack::Instance`,
which aiMap::Init and aiRaceData read. mmStatePack::SetDefaults overwrites the
densities, time of day, weather and the ambient limits at startup (another
group). In OpenMM2 it is `game::RaceConfig` (`game/RaceConfig.h`) plus
`app::Settings` (session.md: `RaceConfig` = mmStatePack, verified).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `dgStatePack::dgStatePack` | replaced | `game/RaceConfig.h` `RaceConfig` | A plain config struct passed to the session instead of a global with an instance pointer. |
| `dgStatePack::~dgStatePack` | not needed | | Clears Instance. |
| `dgStatePack::InitFromArgs` | not needed | — | Unreachable (no callers). Empty. |

## UpdateCrc (global)

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `UpdateCrc` | not needed | | A table-driven 32-bit CRC with a running global state. A null pointer resets it to all ones, and every call returns the complement. Its table is not the standard CRC-32 one (entry 1 is 0x7763A455). It is used by the four save-file ComputeCRC functions (not needed with INI files) and by mmVehInfo::ComputeTuningCRC, the multiplayer tuning check. The tuning check is already listed as open for session/multiplayer in camera-props.md; OpenMM2 has its own network protocol. |

## In-race popup (PUMenuBase and the PU* menus)

mmGame::Init creates mmPopup with a card at x 0.2-0.8, y 0.1-0.9 of the
screen; mmPopup's constructor builds every popup menu once (PUMain 1,
PUQuit 2, PUExit 3, PUChat 4, PUOptions 5, PUAudioOptions 6, PUControl 7,
PUGraphics 8, PUResults 9, PURoster 10, PUKey 11, PUReplay 12,
PUReplaySave 13, PUDebug 14) and mmPopup::Update (game-flow's) switches
between them. OpenMM2's `RaceScreen` keeps that state machine; the pages
are built by `app/frontend/PopupOptions.cpp` (PUMenuBase layout, the
OPTIONS pages, PUQuit, PUKey, PURoster) and the popup section of
`RaceScreen.cpp` (PUMain, PUChat). The results are a frontend page
(`PagesResults.cpp`, deviation recorded by frontend-ui).

### PUMenuBase

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `PUMenuBase::PUMenuBase` | ported (new) | `app/frontend/PopupOptions.cpp` `popup::cardFor`, `popup::addButton` | Uses only the width and height it is given and centres the card ((1 - w) / 2, (1 - h) / 2); the x and y callers pass are unused (so PUChat's line is centred, see PUChat). Button font +0xa0 = GetFont 24 (string 570), button height 0.1, widget height 0.075, exit place (0.5, 0.9, 0.5 x 0.1). A bitmap argument (PUResults' background) sizes the card from the picture. OpenMM2 used a guessed card and 20-pixel buttons 0.075 high: fixed. |
| `PUMenuBase::CreateTitle` | ported (new) | `popup::drawTitle` | With 1, a label (id 0x68) with the menu's name at (0, 0, 1 x 0.1), GetFont 32, left, centred vertically; returns 0.11 either way. |
| `PUMenuBase::AddExit`, `PUMenuBase::AddPrevious` | ported (new) | `RaceScreen::buildPopup` (Resume Driving), `PopupOptions::buildOptions` (Previous Menu), `buildKeyMap` | Resume Driving (473, id 0x65) and Previous Menu (470, id 100) at the exit place, UIButton type 1; their position arguments only feed the returned value. |
| `PUMenuBase::AddOKCancel` | ported (new) | `PopupOptions::addOkCancel` | Cancel (471, id 0x66) at (0, 0.9) and OK (472, id 0x67) at (0.6, 0.9), 0.4 x 0.1, type 1; the first callback is Cancel's. |
| `PUMenuBase::DisableExit`, `PUMenuBase::EnableExit` | ported | `RaceScreen::buildPopup` (`popupLocked`, game flow's) | mmPopup::Lock / Unlock: Resume Driving is off once a single-player race is over. |
| `PUMenuBase::Update`, `PUMenuBase::Cull`, `PUMenuBase::CreateDummyBitmap` | replaced | `popup::drawCard` | Declare and copy the background bitmap (PUResults' picture); the dummy bitmap stands in for a missing one. The card itself is MenuManager's Card2D (16, 31, 93) at alpha 0x80: OpenMM2 drew black at 160 (fixed). |
| `PUMenuBase::~PUMenuBase`, ``PUMenuBase::`scalar_deleting_destructor'`` | not needed | - | Memory. |

### PUMain

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `PUMain::PUMain` | ported | `RaceScreen::buildPopup` (Popup::Main) | Resume Driving first (AddExit), then Restart Race / Restart Lesson (464 / 655, id 10), Options (466, id 0xb), Quit to Race Menu / Back to School (468 / 656, id 0xd), Exit to Windows (469, id 0xe) across the card at 0.125, 0.25, 0.375, 0.5, 0.1 high, type 2 (centred). No title. Fixed: the rows were drawn from their top-left in the 20-pixel font, 0.075 high, Resume after the rows in focus order, Options disabled. Exit to Windows ends the game at once (mmPopup::Update id 0xe: the exit flag and mmGame::BeDone; nothing switches to PUExit); F4 on it restarts the race (MenuManager::ScanGlobalKeys state 6). |
| `PUMain::RestartRO` | ported | `restart.enabled = !net` | mmGameMulti::Init: read-only in network games. |
| `PUMain::RaceMenuRO`, `PUMain::IsRaceMenuReadOnly` | not needed | - | mmGameMulti::Init makes Quit read-only for a client only when a DirectPlay lobby service (MSN Gaming Zone) launched the game (asNetwork::InitializeLobby's flag); a later system message sets it to "not host" (host migration). OpenMM2 has neither. |
| `PUMain::RplRO` | not needed | - | The Instant Replay row (never added: PUMain's replay pointer stays empty), set by the modes' UpdateGame. |
| `PUMain::~PUMain`, ``PUMain::`scalar_deleting_destructor'`` | not needed | - | Memory. |

### PUOptions, PUAudioOptions, PUControl, PUGraphics, ControlBase

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `PUOptions::PUOptions` | ported (new) | `PopupOptions::buildOptions` | No title; Previous Menu, then Audio / Control / Graphics Options (475-477, ids 0x65-0x67) at 0.2 / 0.4 / 0.6, type 2. mmPopup::Update: 0x65-0x67 open menus 6-8, Previous Menu and Escape PUMain. |
| `PUAudioOptions::PUAudioOptions` | ported (new) | `PopupOptions::buildAudio` | Title 442; OK/Cancel; Sound FX Volume (443), Music/City Volume (444), Balance (445, -1..1) as labelled sliders (label above, AddSlider label mode -1) at x 0.05, 0.6 wide, from y 0.11 every 2 x WIDGET_HEIGHT (1/15) + 0.11; the balance slider has mmSlider's balance arrows (slider_lbal / slider_rbal). |
| `PUAudioOptions::SetWaveVolume`, `PUAudioOptions::SetCDVolume`, `PUAudioOptions::SetBalance` | ported (new) | `buildAudio` setters, `Context::applyAudioSettings` | Each change goes to the audio manager at once (sound effects, music and CD, wave and CD balance plus the music pan). MM2's log curve is the audio area's (open there). |
| `PUAudioOptions::PreSetup`, `PUAudioOptions::CancelAction` | ported (new) | `buildAudio` (copy of the settings), the Cancel callback | mmPlayerConfig::GetAudio on entry, SetAudio on Cancel. |
| `PUControl::PUControl` | ported (new) | `PopupOptions::buildControl` | Card 0.9 x 0.8; title 448; Steering Sensitivity (449, 0.5..2) and Collision Intensity (451, 0..2) at y 0.36, Controller Dead Zone (450, 0..0.33) and Road Force Intensity (452, 0..2) at 0.61, columns 0.05 / 0.55, 0.4 wide; then the CONTROL drop-down (453, strings 580-584) at 0.05, 0.11 with a label above. |
| `PUControl::SetRWStates`, `ControlBase::InitSensitivity` | ported (new) | `popup::setRWStates`, `popup::initSensitivity` | Everything greyed, then for a joystick or wheel sensitivity and dead zone (and the two intensities while mmInput::DoingFF), for a mouse the sensitivity; InitSensitivity turns sensitivity and dead zone on and off again for keyboard and game pad. PreSetup runs SetRWStates then InitSensitivity, ControlSelect the other way round, so a mouse has the dead zone on entry but not after being picked (kept). DoingFF is inferred as "FORCE FEEDBACK on with a joystick or pad connected". |
| `PUControl::ControlSelect` | ported (new) | `buildControl` drop-down setter, `RaceScreen::applyControlOptions` | mmInput::Init with the new device: a joystick type without a joystick falls back to the keyboard. |
| `PUControl::SetSensitivityCB`, `ControlBase::SetSensitivity`, `ControlBase::ControlBase` | ported (new) | `buildControl` slider setters, `applyControlOptions` | mmInput +0x1C4 is the inverse of the sensitivity shown (mouse, joystick, wheel); OpenMM2 stores the shown value (`[Controls] Sensitivity`), which the race's steering divides by. |
| `PUControl::PreSetup`, `PUControl::CancelAction` | ported (new) | `buildControl` | GetControls on entry; SetControls and InitSensitivity on Cancel. |
| `PUGraphics::PUGraphics` | ported (new) | `PopupOptions::buildGraphics` | Card 0.9 x 0.8, title 460. Left: Object Detail (648, 574-577), Visibility (461, 100..1000), Lighting Quality (644, 1..3); right: Cloud Shadows (659: 660, 661, 576), Vehicle Reflections (647) and Textured Sky (645) as UIToggleButton2; rows 0.11, 0.385, 0.66 (each 0.075 + 0.2 below the last). Object detail calls the level's SetObjectDetail, cloud shadows set vglCloudMapEnable (0, 4, 2), reflections the environment-map switch (all read from the asm: the decompile drops these callbacks). |
| `PUGraphics::FixClip`, `PUGraphics::SetFarClip` | ported (new) | `buildGraphics` Visibility setter, `RaceScreen::applyGraphicsOptions` | The viewport's far plane (unless orthographic), then mmGame::FarClipCB. mmGame::PlayerSetState and Init call SetFarClip with the driver's far clip. |
| `PUGraphics::RenderQualityCB` and the lighting slider's callback (unnamed piece 0x509f50) | ported (new) | `popup::lightQualityFromSlider`, `applyGraphicsOptions` | Raised: whole part of value + 1; lowered: whole part; within 1..3; then mmGame::SetLevelGraphics (sky, light quality, object detail, environment maps, cloud map). |
| `PUGraphics::PreSetup` | ported (new) | `buildGraphics` getters | Shows the current far clip, light quality, cloud shadows and object detail. |
| `PUGraphics::CancelAction` | ported (new) | `addOkCancel` with no cancel action | Empty: Cancel keeps the changes. |
| `PUOptions::~PUOptions`, ``PUOptions::`scalar_deleting_destructor'``, `PUAudioOptions::~PUAudioOptions`, ``PUAudioOptions::`scalar_deleting_destructor'``, `PUControl::~PUControl`, ``PUControl::`scalar_deleting_destructor'``, `PUGraphics::~PUGraphics`, ``PUGraphics::`scalar_deleting_destructor'``, `ControlBase::~ControlBase` | not needed | - | Memory. |

### PUQuit, PUExit, PUKey, PUChat

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `PUQuit::PUQuit` | ported (new) | `PopupOptions::buildQuit`, `RaceScreen` (PUMain's Quit for a network host) | Quit to Lobby (479, id 10), the session button (id 0xc), Cancel (482, id 0xd) at 0.35, 0.45, 0.55, type 2. Quit to Lobby: mmGameMulti::BeDone(1), everyone back to the lobby (`NetGame::returnToLobby`); Cancel and Escape: back to PUMain. Checked with two local instances. |
| `PUQuit::EnableMigrateHost` | replaced | `buildQuit` | MM2's session button reads Quit Game (641) and leaves the session to a migrated DirectPlay host (BeDone(2)); mmMultiCR::Init switches it to End Session (481, BeDone(0): the host ends the session). OpenMM2's sessions have no host migration, so it always reads End Session and leaves (`NetGame::leave`, which ends the session for everyone). |
| `PUExit::PUExit` | not needed | — | Built by mmPopup's constructor (menu 3), but nothing in build 3393 switches to it: PUMain's Exit to Windows ends the game at once. OpenMM2 drew its question until the merge with game flow. |
| `PUKey::PUKey`, `PUKey::PreSetup`, `PUKey::PostSetup` | ported (new) | `PopupOptions::buildKeyMap`, `drawKeyMap`, `popup::keyMapSlots`, `RaceScreen::processKeymap` | F1 in the race (mmGame::Update, UpdatePaused: mmPopup::ProcessKeymap) opens it, pausing like Escape; over another page it switches to it; on it, F1 and Escape close the popup. Card 0.9 x 0.9; the active action slots (mmInput::Init's per-device switches; 33 slots walked, 34 when fewer than 32 are on) in two columns, name "%-23s" at x 0.05 / 0.5 and binding "%.23s" at 0.25 / 0.7 of a text node at (0.05, 0.075) of the screen, rows every 0.03 from 0.05, GetFont 16; Resume Driving. The analog devices' steering shows the controller's name (inferred: OpenMM2 binds keys only). PostSetup frees the text node. Below 512 pixels wide MM2 uses a full-screen card (OpenMM2's virtual screen is always 640). |
| `PUChat::PUChat`, `PUChat::ChatEntry`, `PUChat::ClearChat` | ported | `RaceScreen::buildPopup` (Popup::Chat), `openChat` | One text field (no label shown, 40 characters, GetFont 16, outlined) filling a card 0.75 wide and one line high which PUMenuBase centres on the screen. Fixed: OpenMM2 placed it at the bottom (the position mmPopup passes, which PUMenuBase ignores) 24 pixels high in the frontend's font. ChatEntry sends on Enter (mmPopup::ChatCB, session's). |
| `PUQuit::~PUQuit`, ``PUQuit::`scalar_deleting_destructor'``, `PUExit::~PUExit`, ``PUExit::`scalar_deleting_destructor'``, `PUKey::~PUKey`, ``PUKey::`scalar_deleting_destructor'``, `PUChat::~PUChat`, ``PUChat::`scalar_deleting_destructor'`` | not needed | - | Memory. |

### PUResults, PUReplay, PUReplaySave, PUDebug

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `PUResults::PUResults`, `PUResults::Init640`, `PUResults::AddTitle`, `PUResults::AddName` (3), `PUResults::AddLoser`, `PUResults::SetMessage`, `PUResults::Reset`, `PUResults::ClearNames` | ported | `app/frontend/PagesResults.cpp` `ResultsPage` | Shown as a frontend page after the race (deviation, frontend-ui). Fixed now: the buttons are UIButton type 0 in GetFont 24 (centred vertically, from the left), drawn from their top in the 20-pixel font before. Network games (new): every player by time, DNF last without a place (mmGameMulti::UpdateResults, AddLoser); Cops and Robbers the team rows and the players by points (mmMultiCR::FillResults, `crResultRows`). |
| `PUResults::DisableNextRace`, `PUResults::EnableNextRace` | ported | `ResultsPage::hasNextRace` | The modes' Init / UpdateGame decide (frontend-ui verified). |
| `PUResults::RestartRO`, `PUResults::RosterRO`, `PUResults::IsRosterReadOnly` | ported (new) | `ResultsPage` (network results) | mmGameMulti::Init: Restart read-only (MM2 leaves it to the host, inferred from the flag it tests; OpenMM2 restarts network races from the lobby), Next off; Show Roster (494) between Next and Race Menu. OpenMM2 shows the results over the lobby, so Show Roster and Race Menu lead back to it. |
| `PUResults::RaceMenuRO`, `PUResults::IsRaceMenuReadOnly` | not needed | — | Race Menu is read-only only for a joiner of a game a DirectPlay lobby (MSN Gaming Zone) launched. |
| `PUResults::Init320` | not needed | - | The layout for screens narrower than 640; OpenMM2's virtual screen is 640 x 480. |
| `PUReplay::PUReplay`, `PUReplay::SaveRO`, `PUReplay::GetSaveRO`, `PUReplaySave::PUReplaySave` | not needed | - | The replay controls (menus 12, 13): OpenMM2 has no replays (the replay manager's flags are never set; MM2's main-menu REPLAY is switched off). |
| `PUDebug::PUDebug`, `PUDebug::RecordCB` | not needed | - | Debug menu 14 ("Record Pos"), opened only by mmGame::UpdateDebugInput's debug keys. |
| `PUResults::~PUResults`, ``PUResults::`scalar_deleting_destructor'``, `PUReplay::~PUReplay`, ``PUReplay::`scalar_deleting_destructor'``, `PUReplaySave::~PUReplaySave`, ``PUReplaySave::`scalar_deleting_destructor'``, `PUDebug::~PUDebug`, ``PUDebug::`scalar_deleting_destructor'`` | not needed | - | Memory. |

## Vehicle

The garage menu (menu 8) is a `Vehicle`: `VehicleSelectBase` built with
`InitCarSelection(1, 0.025, 0.3, 0.5, 0.5)` plus GO DRIVE. Recorded here
for the vehicle-physics list, which holds the class. OpenMM2:
`app/frontend/PagesRace.cpp` `VehiclePage`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `Vehicle::Vehicle` | ported | `VehiclePage::VehiclePage` | the garage's widgets (see VehicleSelectBase) and GO DRIVE (`veh_go`, "Uigo"). |
| `Vehicle::PreSetup`, `Vehicle::PostSetup` | ported | `VehiclePage::onEnter`, `enterWithUnlockedCar`, `Showroom` | VehicleSelectBase's PreSetup and PostSetup: the locked-car fallback, the car's select sound and paint job, the picked car's 3D node on and off. |
| `Vehicle::SetSubMenu`, `Vehicle::SetSubMenuButtons` | ported (new) | `VehiclePage` (opened from the lobby), `Frontend::applyLobbyCar` | Opened from the lobby's SELECT VEHICLE the garage is a sub-menu: GO DRIVE is switched off (TurnOff; TurnOn otherwise), PREV takes the car back to the lobby (`mmInterface::ChangePlayerData`) and Escape does nothing. |
| `Vehicle::~Vehicle`, ``Vehicle::`scalar_deleting_destructor'`` | not needed | — | destructors. |

## asViewCS

The frontend camera's controller: `MenuManager::Init` creates the only
`asViewCS` (the race's cameras are other classes) and runs it in polar mode
about the garage's car. Recorded here for the hud-views list, which holds
the class. OpenMM2: `app/frontend/Showroom.cpp`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `asViewCS::asViewCS`, `asViewCS::Reset` | ported (new) | `Showroom` (distance, angle state) | the polar parameters and the interest offset; MenuManager::Init's 0.534 incline never shows (VehicleSelectBase::Update overrides it every frame). |
| `asViewCS::Update`, `asViewCS::UpdatePolar` | ported (new) | `Showroom::cameraMatrix`, `game::cam::polarView` | `Matrix34::PolarView(distance, azimuth, incline, twist)` plus the offset (0, 0.86, 0); the azimuth's easing towards its target (split piece 0x596562) has nothing to do with the azimuth held at 0. |
| `asViewCS::UpdateLookAt`, `asViewCS::UpdatePOV`, `asViewCS::UpdateRoam`, `asViewCS::UpdateStereo`, `asViewCS::UpdateTrack` | not needed | — | the other modes of `Update`'s switch; the frontend's camera stays in polar mode. |
| `asViewCS::SetAzimuth` | not needed | — | unreachable (no callers). |
| `asViewCS::~asViewCS`, ``asViewCS::`scalar_deleting_destructor'`` | not needed | — | destructors. |

## asDofCS

One per car (the vector `VehicleSelectBase::InitCarSelection` builds), the
parent of the car's `mmVehicleForm`: a rotation about Y. Recorded here for
the hud-views list.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `asDofCS::asDofCS`, `asDofCS::Reset` | ported (new) | `Showroom::Car::angle` | rotate type, rate 1.0 (InitCarSelection, SetPick); each car keeps its angle. |
| `asDofCS::Update` | ported (new) | `Showroom::update` | the value grows by the rate times the frame's seconds; only the active (picked) car's node updates. |
| `asDofCS::~asDofCS`, ``asDofCS::`vector_deleting_destructor'``, ``asDofCS::`scalar_deleting_destructor'`` | not needed | — | destructors. |
| `asDofCS::AddWidgets`, `asDofCS::SetTime`, `asDofCS::operator=` | not needed | — | unreachable (no callers). |

## Compiler and Windows helpers (`_global`)

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `??0AboutMenu@@QAE@H@Z_SEH`, `??0Dialog_City2@@QAE@HMMMMPAD@Z_SEH`, `??0Dialog_ControlAssign@@QAE@HMMMMPAD@Z_SEH`, `??0Dialog_DriverRec@@QAE@HMMMMPAD@Z_SEH`, `??0Dialog_Eject@@QAE@HMMMMPAD@Z_SEH`, `??0Dialog_HallOfFame@@QAE@HMMMMPAD@Z_SEH`, `??0Dialog_Host@@QAE@HMMMMPAD@Z_SEH`, `??0Dialog_Message@@QAE@HMMMMPAD@Z_SEH`, `??0Dialog_NewPlayer@@QAE@HMMMMPAD@Z_SEH`, `??0Dialog_Password@@QAE@HMMMMPAD@Z_SEH`, `??0Dialog_RaceEnvironment@@QAE@HMMMMPAD@Z_SEH`, `??0Dialog_Replay@@QAE@HMMMMPAD@Z_SEH`, `??0Dialog_ReplayEdit@@QAE@HMMMMPAD@Z_SEH`, `??0Dialog_Serial@@QAE@HMMMMPAD@Z_SEH`, `??0Dialog_TCPIP@@QAE@HMMMMPAD@Z_SEH`, `??0HostRaceMenu@@QAE@H@Z_SEH`, `??0MainMenu@@QAE@H@Z_SEH`, `??0MenuManager@@QAE@XZ_SEH`, `??0NetSelectMenu@@QAE@H@Z_SEH`, `??0OptionsMenu@@QAE@H@Z_SEH`, `??0PUMenuBase@@QAE@HMMMMPAD_N@Z_SEH`, `??0RaceMenu@@QAE@H@Z_SEH`, `??0RaceMenuBase@@QAE@H@Z_SEH`, `??0UIMenu@@QAE@H@Z_SEH`, `??1Dialog_City2@@UAE@XZ_SEH`, `??1Dialog_DriverRec@@UAE@XZ_SEH`, `??1Dialog_Eject@@UAE@XZ_SEH`, `??1Dialog_HallOfFame@@UAE@XZ_SEH`, `??1Dialog_RaceEnvironment@@UAE@XZ_SEH`, `??1Dialog_Replay@@UAE@XZ_SEH`, `??1Dialog_Serial@@UAE@XZ_SEH`, `??1HostRaceMenu@@UAE@XZ_SEH`, `??1MainMenu@@UAE@XZ_SEH`, `??1MenuManager@@UAE@XZ_SEH`, `??1NetSelectMenu@@UAE@XZ_SEH`, `??1PUMenuBase@@UAE@XZ_SEH`, `??1RaceMenuBase@@UAE@XZ_SEH`, `??1UIMenu@@UAE@XZ_SEH`, `?AddBMButton@UIMenu@@QAEPAVUIBMButton@@HPADMMHVdatCallback@@PAHHH1@Z_SEH`, `?AddBMLabel@UIMenu@@QAEPAVUIBMLabel@@HPADPAVstring@@MMPAH@Z_SEH`, `?AddBrackets@MenuManager@@QAEXPAVUIIcon@@0PAVuiWidget@@MM@Z_SEH`, `?AddButton@UIMenu@@QAEPAVUIButton@@HPAULocString@@MMMMHHVdatCallback@@H@Z_SEH`, `?AddCWArray@UIMenu@@QAEPAVUICWArray@@HMMMMHVdatCallback@@@Z_SEH`, `?AddCompScroll@UIMenu@@QAEPAVUICompositeScroll@@HMMMMMHPAHHHVdatCallback@@@Z_SEH`, `?AddDriverRecord@Dialog_DriverRec@@QAEHHPADHM0H@Z_SEH`, `?AddHotSpot@UIMenu@@QAEPAVuiWidget@@HPADMMMMVdatCallback@@@Z_SEH`, `?AddIcon@UIMenu@@QAEPAVUIIcon@@HPADMM@Z_SEH`, `?AddIconW@UIMenu@@QAEPAVUIIconW@@HPAULocString@@PADMMMMVdatCallback@@@Z_SEH`, `?AddLabel@UIMenu@@QAEPAVUILabel@@HPAULocString@@MMMMHH@Z_SEH`, `?AddMex@UIMenu@@QAEPAVUIMexButton@@HPAULocString@@PAHHMMMMHHVdatCallback@@@Z_SEH`, `?AddRaceRecord@Dialog_HallOfFame@@QAEXHPAD00H00@Z_SEH`, `?AddSlider@UIMenu@@QAEPAVUISlider@@HPAULocString@@PAMMMMMMMHHHHVdatCallback@@2@Z_SEH`, `?AddTextDropdown@UIMenu@@QAEPAVUITextDropdown@@HPAULocString@@PAHMMMMVstring@@HHHVdatCallback@@PAD3@Z_SEH`, `?AddTextField@UIMenu@@QAEPAVUITextField@@HPAULocString@@PADMMMMHHHHHVdatCallback@@@Z_SEH`, `?AddTextRoller2@UIMenu@@QAEPAVUITextRoller2@@HPAULocString@@PAHMMMMVstring@@HHHHVdatCallback@@@Z_SEH`, `?AddTextRoller@UIMenu@@QAEPAVUITextRoller@@HPAULocString@@PAHMMMMVstring@@HHHHVdatCallback@@@Z_SEH`, `?AddTextScroll@UIMenu@@QAEPAVUITextScroll@@HPAXMMMMVstring@@HPAHVdatCallback@@@Z_SEH`, `?AddToggle2@UIMenu@@QAEPAVUIToggleButton2@@HPAULocString@@PAHMMMMHHVdatCallback@@@Z_SEH`, `?AddToggle@UIMenu@@QAEPAVUIToggleButton@@HPAULocString@@PAHMMMMHHVdatCallback@@@Z_SEH`, `?AddUIControl@UIMenu@@QAEPAVUIControlWidget@@HMMMMMPAVmmIO@@VdatCallback@@@Z_SEH`, `?AddVScrollBar@UIMenu@@QAEPAVUIVScrollBar@@HPAHMMMMMMHHVdatCallback@@@Z_SEH`, `?GetDescription@Dialog_Replay@@QAEPADXZ_SEH`, `?Init@MenuManager@@QAEXHHPAD@Z_SEH`, `?Init@MenuManager@@QAEXPAVasCamera@@HHMMMM@Z_SEH`, `?Init@RaceMenuBase@@QAEXH@Z_SEH`, `?Init@UISlider@@QAEXPAULocString@@PAMMMMMMMHHHHVdatCallback@@PAVUIMenu@@2@Z_SEH`, `?Init@UITextField@@QAEXPAULocString@@PADMMMMHHHHHVdatCallback@@PAVUIMenu@@@Z_SEH`, `?Init@UITextRoller2@@QAEXPAULocString@@PAHMMMMVstring@@HHHHVdatCallback@@PAVUIMenu@@@Z_SEH`, `?Init@UITextRoller@@QAEXPAULocString@@PAHMMMMVstring@@HHHHVdatCallback@@PAVUIMenu@@@Z_SEH`, `?InitCRWidgets@HostRaceMenu@@UAEXXZ_SEH`, `?InitCommonStuff@MenuManager@@QAEXHH@Z_SEH`, `?InitDriver@MainMenu@@QAEXXZ_SEH`, `?InitRaceRecord@Dialog_HallOfFame@@QAEXXZ_SEH` | not needed | — | the compiler's exception-unwinding funclets of those constructors and functions (destroying the parts built so far when one throws); C++ does this by itself. |
| `FirstRunEula` | not needed | — | on the first start `mmInterface` shows the licence (`warranty.rtf`) through a separate DLL and quits when it is declined. OpenMM2 is GPL-3.0 and asks nothing. |
| `mmInterface_IsAutodialEnabled` | not needed | — | reads Windows' dial-up "autodial" setting before DirectPlay starts, so that a modem is not dialled by surprise; OpenMM2's UDP sessions dial nothing. |

## Findings

What the audit found against OpenMM2 as it stood when each group was read, with the
state now. "Fixed" names the commit; line numbers in the text are from the audit.

### Menu framework (A1)

- **A1.1. Fixed in c162521.** **Help picture on entering a menu** (`UIMenu::Enable`, `UIMenu::PreSetup`). MM2: `Enable` lights the initial
   widget (its `Switch(1)` callback turns the description label on) and only then calls the menu's `PreSetup`
   (vtable +0x34, the last call in the asm), and `MainMenu::PreSetup`, `OptionsMenu::PreSetup`,
   `AudioOptions::PreSetup`, `GraphicsOptions::PreSetup` and `ControlSetup::PreSetup` (like the base
   `UIMenu::PreSetup`) end by turning the description label (+0x40) off; nothing turns it on again until the focus
   moves to another widget (hovering the already-lit widget does not either: `UIMenu::SetSelected` switches only an
   unlit widget). So the main menu, the options menu and the three option pages open with CRASH COURSE / ABOUT /
   RESOLUTION / DEVICE lit but an empty description box. (`CrashCourse` and `RaceMenuBase` turn their help on again
   in `ChangeLocalVals` / `FocusDescription`; `VehicleSelectBase::PreSetup` also ends with its label off, not followed
   further.) OpenMM2: `ui::Menu::drawContent` (`ui/Widgets.cpp`) shows the focused widget's `help` picture from
   the first frame (`DriverPage` `jpg/mn_*.jpg`, `OptionsPage` `opt_tabt.jpg`, `GraphicsPage` `gfx_tres.jpg`, ...).
   Visible on every entry of those five menus. Read from the call order; worth confirming in the game.
- **A1.2. Fixed in c162521.** **Closing a dialog** (`MenuManager::CloseDialog`, `MenuManager::OpenDialog`). MM2: `OpenDialog` unlights the
   page's widgets (`UIMenu::ClearWidgets`) and `CloseDialog` only disables the dialog and hands the focus back; the
   page keeps its keyboard position (the widget that opened the dialog, e.g. STATS) with nothing lit, and the next
   Down/Up moves on from there (`mmInterface::Update` cases 0x13/0x14 and the others just call `CloseDialog`).
   OpenMM2: `Frontend::pop` (`app/frontend/FrontendScreen.cpp`) calls `Menu::resetFocus` (`ui/Widgets.cpp`),
   which moves the focus to the page's initial widget and lights it (with its help picture). Visible after every
   dialog that returns to the same page (STATS, RACE RECORDS, CREATE/DELETE cancelled, quit cancelled, notices).
- **A1.3. Fixed in c162521.** **In-race popup: nothing lit on entry** (`MenuManager::EnablePU`, `UIMenu::Enable`, `MenuManager::Switch`). MM2:
   in popup mode `UIMenu::Enable` skips lighting the initial widget and `EnablePU` / `Switch` then unlight
   everything, so every popup page opens with no highlight and the keyboard position on the menu's widget 0 (the
   constructors' `SetBstate(0)`). On PUMain that is Resume Driving (only the highlight differs); on the titled pages
   (AUDIO, CONTROL, GRAPHICS OPTIONS) widget 0 is the title label: the first Down lands on Cancel and Enter right
   after opening does nothing (`KeyboardAction` acts on the label). OpenMM2: `RaceScreen::buildPopup`
   (`app/RaceScreen.cpp`) and `PopupOptions` (`app/frontend/PopupOptions.cpp`) set a
   focusable initial widget, which `ui::Menu` lights at once; on the titled pages Cancel is lit, the first Down goes
   past it and Enter cancels the page. Visible every time the popup or one of its pages opens.
- **A1.4. Open.** **The mouse focuses read-only widgets; the first widget wins** (`UIMenu::MouseHitCheck`,
   `MenuManager::MouseAction`). MM2 tests only the menu's active flag and the widget's enabled flag (+0x60), not
   read-only (+0x44), and returns the first widget in creation order; a hit moves the keyboard position there
   (`CheckMouseHits`), unlighting the old widget (read-only widgets such as `UITextRoller2` do not light themselves).
   So hovering a read-only roller, slider or the About credits spot moves the keyboard position onto it and the next
   Down continues from there. OpenMM2: `Menu::update` (`ui/Widgets.cpp`) hit-tests `focusable()` widgets only
   (read-only excluded) and keeps the last match; hovering a read-only widget is "empty space" (highlight off, focus
   stays on the old widget). Small: only the keyboard continuation after hovering a read-only widget differs; no
   retail overlap of two enabled widgets was found, so the first/last rule does not show.
- **A1.5. Fixed in 5c1cd3c.** **A click needs the press and the release on the same widget** (`sfPointer::Update`, `UIMenu::CheckMouseHits`,
   `MenuManager::RegisterWidgetFocus`). MM2 remembers the widget hit by the press; on the release, a hit on any other
   widget is cancelled (its pending flag cleared), so dragging from one button to another, or from empty space onto a
   button, does nothing; a press outside a capturing widget (open drop-down) ends the capture and unlights
   everything, and its release cannot act either. OpenMM2: `SpriteButton::mouse` (`ui/Widgets.cpp`),
   `TextButton::mouse` (438) and `TextToggle::mouse` (475) act on any release over the focused widget, and the hover
   moves the focus while the button is held, so a drag onto a button clicks it. (The first audit wrote "wherever the
   press began" for `UIBMButton`; the asm of `sfPointer::Update` compares the release hit with the press hit.) Small,
   but a real input difference for every button.
- **A1.6. Fixed in 357fcb4.** **Popup menu sounds** (`MenuManager::ScanGlobalKeys`, `MenuManager::PlaySound`). MM2 (popup open): Escape and
   Enter always play Selectionmade at 0.75 (before the widget acts), Tab/Down/Up always play Moveselector at 0.75,
   whatever the focus does. OpenMM2: `Menu::update` plays Moveselector only when the focus moved
   (`ui/Widgets.cpp`), nothing on Escape (970), and Selectionmade on Enter only through `TextButton::activate` /
   `TextToggle::activate` (not on sliders or drop-downs). Audible on Escape (closing the popup or a page) and on
   Enter over a slider or drop-down.
- **A1.7. Fixed in ba296e0.** **Frontend "Switch" is restarted** (`MenuManager::PlaySound`). MM2 stops the Switch sound, rewinds it and plays it
   again at 0.85 on every step of a slider or text roller (`UISlider::Action`, `UITextRoller2::Inc`/`Dec`). OpenMM2:
   `Roller::adjust`, `Slider::adjust`/`mouse` go through `Frontend::playSound`
   (`app/frontend/FrontendScreen.cpp`), whose `SoundSlot::playOnce` leaves a playing sound alone
   (`audio/game/SoundSlot.cpp`), so a step taken while the previous click still plays is silent (key repeat is
   0.075 s). The popup's `PopupSounds::play` already stops first. Audible when holding Left/Right on a slider.
- **A1.8. Fixed in 357fcb4 (single player).** **F4 in the popup** (`MenuManager::ScanGlobalKeys`). In popup mode F4 sets the focused menu's state to 6, which
   `mmPopup::Update` handles on PUMain only: `DisablePU` and the replay manager's restart flag, i.e. F4 restarts the
   race like Restart Race, but without the game-type check that button's branch has (inferred from the branch). In
   the frontend F4 is swallowed. OpenMM2: no F4 handling in `RaceScreen` (`app/RaceScreen.cpp`). Small
   (keyboard shortcut); see Missing features.
- **A1.9. Open (no visible effect with the retail text).** **Text is not cut at its box** (`mmTextNode::Cull`, `mmTextNode::Init`). MM2 renders each node into a bitmap the
   size of its box, so text longer than the box is cut there. OpenMM2 clips only `ValueBox`, `ListBox` and
   `TextEntry`; `TextBox::draw` (`ui/Widgets.cpp`), `TextButton::draw` (410), `TextToggle::draw` (449),
   `Roller::draw` (503) and the pages' `drawAbove` text overflow. No visible effect with the retail strings and
   18-character names (inferred); shows only with long translated or user text.

### Widgets (A2)

- **A2.1. Partly fixed in 5c1cd3c (the release must be on the pressed widget); the buttons' callbacks still run on the release, not the press.** **Mouse press vs release, and the release rule** (`UIBMButton::Action`,
   `UIButton::Action`, `UIToggleButton2::Action`, with sfPointer::Update and
   UIMenu::CheckMouseHits). MM2 runs a widget's own datCallback on the mouse
   *press* (kind 0): the roller arrows (MainMenu::DecPlayer/IncPlayer,
   RaceMenuBase, CrashCourse, VehicleSelectBase, HostRaceMenu callbacks; id -1,
   no menu action) step, the popup buttons' datCallbacks (e.g. those
   PUMenuBase::AddOKCancel passes) and the PUGraphics ON/OFF toggles run as
   the button goes down, and MenuManager::PlaySound(1) plays then too. The
   menu's id action (what mmPopup::Update and mmInterface::Update read)
   happens on the release, and sfPointer::Update
   clears the release hit of any widget other than the one that took the
   press, so a release elsewhere (or after a press on empty space) does
   nothing. OpenMM2 acts on the release for every SpriteButton, TextButton and
   TextToggle (`ui/Widgets.cpp` `SpriteButton::mouse`
   `TextButton::mouse` `TextToggle::mouse`) and acts on whatever
   button the release is over, wherever the press began (the hover moves the
   focus during the drag). Visible: arrows step on release instead of press;
   press-drag-release across buttons triggers the second button in OpenMM2,
   nothing in MM2.
- **A2.2. Open.** **Space on bitmap buttons** (`UIBMButton::Action`). MM2 treats Space (0x39)
   like Enter for the button itself: plays its sound slot and calls its
   datCallback (only Enter also triggers the menu's id action). So Space on a
   roller arrow steps the value (driver, race name, colour, lesson...), Space
   on CREATE/DELETE/STATS plays "Moveselector", Space on the strip's buttons
   plays "Selectionmade", without navigating (OPTIONS, a type 5 toggle
   without data, also flips its lit frame). OpenMM2's SpriteButton has no
   `activateSpace` (`ui/Widgets.h` `SpriteButton`), so Space does nothing.
- **A2.3. Fixed in 1ffb905.** **Missing UIBMButton sound slots.** Recovered from every AddBMButton call
   in the asm: RaceMenuBase's mode lamps (cruise, blitz, checkpoint, circuit,
   and the fifth) use slot 0 ("Selectionmade" 0.86), in the race menu and in
   HostRaceMenu; HostRaceMenu::InitCRWidgets' six Cops & Robbers lamps slot 0;
   NetArena's team lamps, DONE/back buttons and SELECT VEHICLE (0x3e9) slot 0
   and its GO (9999) slot 10 "Uigo". OpenMM2's lamps and buttons there have
   no sound: `app/frontend/PagesRace.cpp` (race lamps),
   `PagesMulti.cpp` (host lamps) (lobby). Clicking a mode lamp is silent in OpenMM2 and clicks in
   MM2. (Every other slot matched: nav strip 0 except PREV -1, OptionsMenu -1,
   OptionsBase and dialogs 0, CREATE/DELETE/STATS 9, VEHICLE GO 10, roller
   arrows and option toggles -1.)
- **A2.4. Fixed in 1ffb905.** **Extra "Uigo" on the Crash Course GO** (`UIBMButton::PlaySound`).
   CrashCourse::CrashCourse creates GO (9999) with slot -1 and
   CrashCourse::SetVehicleNext only swaps the bitmap (veh_go / race_veh);
   "Uigo" is only reachable through UIBMButton slot 10 (no other code plays
   it). OpenMM2 plays "Uigo" when GO starts a lesson in the school car
   (`app/frontend/PagesCrash.cpp`). Remove the sound.
- **A2.5. Fixed in 1ffb905.** **Clicking the lamp that is already lit** (`UIBMButton::DoToggle`, Mex).
   With the mouse, a radio lamp that is already selected only flips its
   internal flag: no callback, and it shows frame 1 (unlit, highlighted)
   until the focus leaves it; Enter/Space on it skip DoToggle but still call
   the callback. OpenMM2's `LampItem::mouse`/`activate`
   (`ui/Widgets.cpp`) always call `onClick`; on the race menu that
   is `RacesPage::selectMode` (`PagesRace.cpp`), which resets the race to
   the first one and its defaults: clicking the lit mode lamp loses the chosen
   race in OpenMM2, not in MM2 (Enter does reset it in both).
- **A2.6. Fixed in 1ffb905.** **ON/OFF width of the popup toggles** (`UIToggleButton2::Init`). The ON/OFF
   node is 0.075 of the *screen* wide (48 px), taken off the toggle's width
   after UIMenu::ScaleWidget (read from the asm: Init subtracts the constant
   0.075 from the already scaled width and gives the second text node a
   width of 0.075, both in screen fractions). OpenMM2 uses 0.075 of the card
   (`app/frontend/PopupOptions.cpp`: 43.2 px on the 576 px card), so the
   label box is 4.8 px too wide and ON/OFF is centred 2.4 px too far right.
- **A2.7. Open (same as A1.4).** **Read-only widgets take the mouse focus in MM2** (`UIBMButton::Switch`,
   `UISlider::Switch`, UIMenu::MouseHitCheck tests only "enabled"). Hovering
   a killed lamp (a mode the city lacks), a read-only density slider (race
   not passed) or a garage statistics bar makes it the menu's focused widget:
   the previous highlight goes, and UIBMButton/UISlider call their focus
   callback (the page's help picture) even when read-only; Up/Down then
   continue from it. OpenMM2 treats them as empty space
   (`ui/Widgets.cpp`: only `focusable()` widgets are hovered), hiding the
   highlight and the help picture.
- **A2.8. Fixed in c162521.** **"Looking for games..." does not blink** (`UILabel::Update`,
   `UILabel::SwitchState`). NetSelectMenu's label (string 657, flags 1) blinks
   between colour 1 (0.1 grey) and colour 4 (yellow) every 0.75 s at
   (0.0625, 0.825) = (40, 396), the help-picture spot. OpenMM2's replacement
   session page draws it steadily in its own grey inside the list
   (`app/frontend/PagesMulti.cpp`). Low priority (replaced page).
- **A2.9. Fixed in 1ffb905.** **Pressed arrows stay pressed** (`UISlider::Action`,
   `UITextRoller2::Action`, mmSlider::Cull, UITextRoller2::Cull). MM2 clears
   the pressed-arrow frame on the mouse release and on Left/Right; OpenMM2's
   `m_clicked` is only reset by `focusChanged` (and, for sliders, by a key):
   after clicking an arrow it keeps the pressed frame until the focus moves
   (`ui/Widgets.cpp` `Roller::mouse` `Roller::adjust` never
   clears it;  `Slider::mouse`).
- **A2.10. Fixed in 1ffb905.** **Slider clicks ignore the row's height** (`UISlider::EvalMouseXY`). MM2
    acts only inside the hot-spot row (one arrow frame high); OpenMM2 tests x
    only (`ui/Widgets.cpp` `Slider::mouse`), and a popup slider's box
    includes its label above, so a click on the label steps the slider (left
    25 px) or sets it from x.
- **A2.11. Fixed in 1ffb905.** **Drop arrow on read-only drop-downs** (`UITextDropdown::Cull`). MM2 draws
    drop_arrow (frame 0) whenever the box is shown, read-only or not (only
    the roller buttons beside TIME and WEATHER are hidden by
    RaceMenuBase::SetRW). OpenMM2 hides it when `readOnly` or `!enabled`
    (`ui/Widgets.cpp`), e.g. on the race menu's TIME and WEATHER before
    the race is passed.
- **A2.12. Kept (an MM2 bug; the maintainer's call).** **Closing the drop-down list** (`UITextDropdown::CaptureAction`,
    `TextDropWidget::SetValue`). Every key step in the open list rewrites the
    box text; Escape (Switch(0)) or a press outside the list
    (MenuManager::ClearAllWidgets) closes it without restoring the text and
    without writing `*data`, so the box keeps showing the entry moved to while
    the setting is unchanged, until the list is committed with Enter (which
    commits that entry) or the value changes otherwise; both also remove the
    box's focus colour (and ClearAllWidgets every highlight); a press on the
    box itself while open closes it too. OpenMM2 shows `get()` after closing,
    keeps the focus, and ignores presses on the box while open
    (`ui/Widgets.cpp` `ValueBox::modalInput`). The text quirk is an MM2
    bug a player can see; port it or record it as a deliberate deviation.
- **A2.13. Fixed in 1ffb905.** **Popup drop-down sounds** (`UITextDropdown::Action`, `CaptureAction`). In
    the popups (PUControl's CONTROL, PUGraphics' Object Detail / Cloud
    Shadows...) MenuManager::PlaySound(1) ("Selectionmade" 0.75) plays on
    opening with Enter, on every Home/Up/Left/Down/Right/End in the open list
    and on a mouse pick. OpenMM2's ValueBox plays nothing
    (`ui/Widgets.cpp`).
- **A2.14. Fixed in 1ffb905.** **PUChat typing sounds** (`UITextField::WmCharHandler`, `KeyAction`).
    MenuManager::PlaySound(0) ("Moveselector" 0.75) plays for every accepted
    character and on Enter; mmPopup::ProcessChat enables the popup
    (MenuManager +0x34), so they are heard. OpenMM2's TextEntry is silent
    (`ui/Widgets.cpp`; chat line at `app/RaceScreen.cpp`).
- **A2.15. Open (needs a look at the original game first).** **Text field outline in the menus** (`UITextField::Init`). The field's
    text node gets effects 0x45 in every MenuManager (read from the asm: the
    constant is passed to mmTextNode::AddText unconditionally, with no
    popup test), and mmTextNode::RenderText draws effect bit 4 with GDI
    Rectangle() using the stock white pen and the hollow brush
    over the node (transparent copy, black is the key colour): a 1 px white
    frame round the field, in the new-driver dialog too. OpenMM2 draws it only
    in popup mode (`ui/Widgets.cpp`). This contradicts the first audit,
    which removed the frame from `TextEntry::draw` as not MM2's ("the frame is
    painted on the backgrounds"); unless a screenshot shows it hidden by the
    painted frame, restore it for every TextEntry.
- **A2.16. Fixed in e1292b1.** **No VSWidget scroll bar** (VSWidget, `UICompositeScroll::Action`,
    `UICWArray::SetVScrollVals`). The race records list (Dialog_HallOfFame) has
    a scroll bar at x 546 px (the list's right edge less 0.0329 of the
    screen), from y 91, 198 px tall: scroll_uarr, a trough of scroll_inact
    segments with a scroll_act thumb (size rows / count), scroll_darr; arrows
    and trough act on the press with "Switch", the thumb can be dragged; the
    list itself is a focus stop whose focus lights the bar, Left/Right scroll
    (two rows per press per the code), Page Up/Down a page, and a click on a
    row plays "Switch". OpenMM2 (`app/frontend/PagesMain.cpp`) has two
    separate arrow buttons at guessed positions (543,88) and (543,266) that
    act on release and are their own focus stops, no trough or thumb, no keys
    (the wheel is OpenMM2's). The customize list's bar
    (`PagesOptions.cpp`) also lacks trough and thumb, is always drawn
    (MM2 hides it when all rows fit) and uses frame 3, which VSWidget never
    shows; its comment names UIVScrollBar, which MM2 never creates. The arrow
    and segment sizes need the scroll_* pictures to place exactly.
- **A2.17. Partly fixed in 4c0d84d (the page opens on DEFAULTS); the list's keyboard model and row height are open.** **Customize list keyboard model** (`UICWArray::UICWArray`, `Action`).
    MM2's list is read-only: the keyboard focus never stops on it (the page
    opens on its buttons), a mouse press on a row starts the capture, and the
    keys only reach it after the mouse has focused it (Enter then calls
    BadAssignCB, Left/Right scroll). OpenMM2 makes it the initial focus
    (`PagesOptions.cpp`), selects rows with Up/Down and shows the selected
    row's control red (), which MM2 never does (red only while
    capturing). Its rows are 20 px (); MM2's are MenuManager +0xe8, the
    measured height of GetFont(16) text ("Arial Bold, 16, 16": about 16 px,
    inferred, not measured), which would make the list visibly shorter.
- **A2.18. Fixed in c162521.** **Binding column alignment** (`UIControlWidget::Init`). The binding is
    centred (effects 3) in the right half of the row; OpenMM2 draws it
    left-aligned from x + 125 (`app/frontend/PagesOptions.cpp`).

### mmInterface, main and options menus (B)

- **B.1. Fixed in 4c0d84d.** **Graphics page: the strip's OPTIONS keeps the changes** (`mmInterface::Update`, strip id 100;
   `GraphicsOptions::CancelAction`). On an option page MM2 calls only the page's CancelAction (vtable +0x48) and
   then `Switch(2,0)`; the restore from mmInterface's saved config (`mmPlayerConfig::SetGraphics`) runs only for
   CANCEL (id 500) and Escape. `GraphicsOptions::CancelAction` is empty, so far clip, lighting, texture, object
   detail, shadows and the toggles stay as changed (and reach the driver's `.cfg` when `BeDone` copies the globals
   at the next race start). The Audio, Control and Customize CancelActions do restore from their page snapshot.
   OpenMM2 gives every page `cancel` (`PagesOptions.cpp` `SettingsPage::finish`, restore at ). Effect:
   Graphics + OPTIONS keeps the settings in MM2 and discards them in OpenMM2. Low-medium.
- **B.2. Fixed in 4c0d84d.** **Customize: the strip's OPTIONS goes to the options menu.** Same code path: `ControlCustom::CancelAction`
   (`SetCustom` from the snapshot, redraw), then `Switch(2,0)`. Only Escape and CANCEL go back to Control
   (`Update` case 0x29 → 5). OpenMM2's `SettingsPage::cancel` pops to the Control page (`PagesOptions.cpp`).
   Small.
- **B.3. Fixed in 4c0d84d.** **Customize opens on DEFAULTS.** `ControlCustom::ControlCustom` sets no focus widget (UIMenu's focus index starts
   at 0 and `UIMenu::Enable` restores it), so DEFAULTS (widget 0) is lit on entry and Enter asks the defaults
   question. OpenMM2 focuses the action list (`PagesOptions.cpp`), where Enter starts a key capture. Small.
- **B.4. Fixed in 4c0d84d.** **`xasn_dlg` shows the wrong button.** `mmInterface::mmInterface` makes the refused-key dialog (0x20) with
   `Dialog_Message::Init(100, "dlg_ok", none)`: a single OK button. OpenMM2 draws `texture/dlg_done.tga`
   (`PagesOptions.cpp`); the position (296,38, widget.csv "xassign_dlg") is right. Small, visible.
- **B.5. Fixed in 9271663.** **A locked car can be taken into multiplayer.** `mmInterface::Switch(10)` (MULTIPLAYER), the lobby entry
   `Switch(0xc)`, `ShowMain` after a network race and `MultiStartGame` re-resolve the unlocks and replace a locked
   car by vpbug with paint 0 (`GetUnlockedCar`) and a locked paint job by 0 (`GetUnlockedColor`). OpenMM2 sends
   `fe.config`'s car unchanged (`PagesMulti.cpp` `netCar`, used when hosting/joining at ), and `fe.config.vehicle` is a locked car after browsing to one in the garage and backing out
   (`PagesRace.cpp` `VehiclePage::pickVehicle`). Effect: locked cars and paint jobs are drivable online. Medium.
- **B.6. Fixed in 9271663.** **A multiplayer start is not saved as the driver's last event.** `MultiStartGame` calls `BeDone` in both
   branches (pieces 0x410881/0x410898), for the host and the joiners: the driver's file gets the lobby car and
   paint, the session's mode, race and city (copied into the state pack by `GetSessionData`) and the net name, so
   the main menu then shows the network event as LAST RACE (the "Cops & Robbers" case, string 79, of
   `PlayerFillStats` exists for this) and LAST VEHICLE, and `PlayerSetState` restores that event. OpenMM2 starts a
   network race in `FrontendScreen::update` (`FrontendScreen.cpp`) without touching the profile beyond
   `applyLobbyCar`'s car and paint (`FrontendScreen.cpp`). Low-medium.
- **B.7. Open (same as C.2).** **QUICK RACE and MULTIPLAYER after the Crash Course reset the cruise environment.** MM2's main-menu handler
   only turns mode 6 (crash course) into 0 (cruise) (`mmInterface::Update` case 1, ids 0x66/0x67), and `Switch(8)`
   re-syncs the race state only for Cops & Robbers, so the cruise keeps the time, weather and densities already
   set: the race menu's cruise settings when only the intro was visited, or the last lesson's
   (`CrashCourse::SetEnvironment` through `SetRaceState`: no traffic, cop density 1, opponent density 8, the
   lesson's time, weather and pedestrians) when a course page was shown. OpenMM2's `leaveCrashCourse`
   (`PagesMain.cpp`) applies the cruise defaults (noon, clear, 0.25/0.5/1). Inferred from the absence of
   any restore in `Update`, `Switch` and the garage; visible as a different cruise. Low-medium.
- **B.8. Kept (an MM2 bug; the maintainer's call).** **Cancelling CREATE changes the current difficulty (MM2 quirk).** The new-driver radio buttons are bound to the
   state pack's difficulty (mmStatePack +0x5C) and `Dialog_NewPlayer::PreSetup` sets it to amateur; only
   `Switch(1)` (entering the main menu) and `PlayerSetState` copy the driver's difficulty back, and closing the
   dialog does neither. A professional who opens CREATE and cancels (or an amateur who ticks Professional and
   cancels) then goes on from the main menu (RACES, QUICK RACE, CRASH COURSE) at the other difficulty: race
   defaults (`RaceMenuBase::SetStateRace`), the `_p` car descriptions, `mmGame` and the pass/record rules read the
   global. OpenMM2 keeps the choice in the dialog (`PagesMain.cpp`) and the profile's difficulty. Low;
   replicating an MM2 bug is the maintainer's call.
- **B.9. Fixed in 2a46a62.** **The host can pick only checkpoint races he has opened.** `CitySetupCB` writes the driver's checkpoint
   progress mask into the host race menu (HostRaceMenu +0xA8, the field `RaceMenu::SetProgressMask` sets for the
   race menu; −1 for other cities or no driver), and `RaceMenuBase::IncRaceName`/`DecRaceName` step only onto a
   race whose bit is set. OpenMM2's `HostSettingsPage` offers every race (`PagesMulti.cpp`, no
   `optionEnabled`). Medium for multiplayer. The drop-down's own disabled mask is RaceMenuBase's (another group);
   confirm there.
- **B.10. Open (network: net/Session.cpp names the duplicates).** **Duplicate network names.** `GetUniquePlayerName` appends a counter with "%s%d" ("noname1", counting the
    players whose name starts with ours) until no other name starts with it; OpenMM2 appends " (2)", " (3)" on an
    exact case-insensitive match (`net/Session.cpp`). Every new driver's net name is "noname" (77), so
    this shows whenever two drivers who never changed it meet. Small.
- **B.11. Open (same as A1.4).** **About opens with nothing lit.** `AboutMenu::AboutMenu` sets no focus widget, so the focus starts on widget 0,
    the read-only credits hotspot (no picture); DONE lights only after Down. OpenMM2 focuses DONE
    (`PagesOptions.cpp`). Small. (Not verified: whether Enter at entry leaves in MM2. `MenuManager::
    ScanGlobalKeys` marks the menu as acted on Enter and `Update` case 0x22 leaves unless the menu's acted-widget
    id is 0; that id was not traced for the hotspot.)
- **B.12. Kept (negligible).** **About scroll clock.** MM2 measures from `datTimeManager::ElapsedTime`, the sum of unclamped real frame times;
    OpenMM2 sums its frame `dt`, clamped to 0.1 s by `platform::FrameClock::tick` (`platform/Clock.cpp`) and
    not advanced while the game is frozen as inactive (`App.cpp`). After a long stall MM2's credits jump ahead by the real
    time, OpenMM2's by at most 0.1 s per frame. Negligible. (The tag ids MM2 also takes from ElapsedTime have no
    OpenMM2 counterpart; see `PlayerCreate`.)
- **B.13. Open (negligible; needs a force-feedback device).** **Force-feedback intensities after a controller change.** `ControlSetup::ControlSelect` runs
    `InitCustomControls` but not `SetFFPermissions`, so with a force-feedback device present both intensity
    sliders become usable even with FORCE FEEDBACK off, until the page is re-entered or the toggle flipped. OpenMM2
    ties them to the toggle at all times (`PagesOptions.cpp` `ControlPage::update`). Negligible (needs a
    force-feedback device; OpenMM2 has no force feedback).

### Race menu, garage and records (C)

- **C.1. Fixed in 2a46a62.** `RaceMenuBase::FocusDescription` (race menu help label). MM2 registers
   `FocusDescription` with a fixed index on each lamp (cruise 3, blitz 0,
   checkpoint 2, circuit 1; `RaceMenuBase::Init`) and `UIBMButton::Switch`
   calls it with the focus flag, so focusing a lamp shows *that lamp's*
   picture (race_rom / race_btz / race_cp / race_cir) and losing the focus
   hides it; `ChangeLocalVals` (after a mode or city change) shows the
   current mode's picture, which then stays until a lamp loses the focus or
   `PreSetup` (only lamps have the callback). OpenMM2
   (`app/frontend/PagesRace.cpp`, `RacesPage::update`) shows
   `modeHelp(fe.config.mode)` (the current mode, not the focused lamp)
   while any lamp is focused, and hides the picture on every focus change.
   Visible: moving the focus down the lamps without pressing shows the
   wrong mode's picture; after a city change the picture disappears as soon
   as the focus moves to another non-lamp widget. Fix: give each lamp its
   own help picture (`item.help` as `HostSettingsPage` does) and keep the
   post-change picture until a lamp loses the focus.
- **C.2. Open.** `RaceMenuBase::PreSetup` with a leftover crash-course state. MM2 turns
   game mode 6 into cruise with `ChangeLocalVals` + `SetRW` only (no
   `SetStateRace`), so the environment `CrashCourse::SetEnvironment` wrote
   while the Crash Course page was open (lesson time, weather, pedestrians,
   no traffic, all cops) stays in the cruise settings, and the cruise help
   picture is shown (`FocusDescription(3, 1)`). (Cops & Robbers goes
   through `mmInterface::Switch` -> `SyncRaceState` -> `SetStateRace`,
   which does apply the cruise defaults.) OpenMM2
   (`app/frontend/PagesRace.cpp`) applies the cruise defaults (noon,
   clear, 0.25 / 0.5 / 1) for both and shows no picture. Visible but rare:
   Crash Course page -> main -> Single Race shows the lesson's environment
   in MM2. Low priority (an MM2 quirk; porting it needs the crash course's
   environment to survive in `fe.config`).
- **C.3. Fixed in 2a46a62.** `RaceMenuBase::SetStateRace` in the host menu. `HostRaceMenu` uses the
   same `GameCallback` (race type lamp: race 0 + `SetStateRace`),
   `Inc/DecRaceName` (`SetStateRace`) and `CityChange` (-> `GameCallback`),
   so choosing a race type, a race or a city resets the time of day,
   weather, pedestrian density and laps (checkpoint 1, circuit from the
   table; cruise: noon, clear, 0.25) to the race table's values, which the
   host can then edit. OpenMM2's `HostSettingsPage`
   (`app/frontend/PagesMulti.cpp` race name setter city `selectMode`) only clamps the race index (keeping it on a
   city change instead of going to race 0) and defaults circuit laps to 3:
   the previous environment is kept. Visible to every host. Fix: set the
   race to 0 on a type or city change and call `fe.applyRaceDefaults(m_cfg)`
   after each of the three changes.
- **C.4. Fixed in 2a46a62 (the index starts at 0 for each session; MM2 keeps it for the run).** `HostRaceMenu::HostRaceMenu` / `InitCRWidgets`: the gold mass index
   (+0x148) starts at 0, "Weightless" (mass 0 from `GetGoldMassVal`, copied
   to the state by `mmInterface::SetCRStateData`), and the menu is built
   once per run so the index persists. OpenMM2 starts at 1, "Quarter Ton"
   (`game/net/NetGame.cpp`, `app/frontend/PagesMulti.cpp`).
   Visible in Cops & Robbers when the host does not touch GOLD MASS: the
   gold weighs 100 instead of nothing.
- **C.5. Kept (an MM2 bug; the maintainer's call).** `Dialog_HallOfFame::AddRaceRecord`, driver column. MM2 shortens the
   driver's name only when it is wider than 0.207 of the screen *and*
   longer than N = 15 characters (string 668), and then (an MM2 bug: the
   format is applied to the 7th argument) shows the *vehicle's* text cut to
   16 characters + "..." in the driver column; a wide name of 15 characters
   or fewer is shown whole. OpenMM2 (`app/frontend/PagesMain.cpp`,
   `fit(..., r.driver, kWideColumn, 668)`) appends "..." to any too-wide
   name and keeps the driver's text. Visible only for driver names of 16-18
   characters (or very wide 15-character ones) in the race records. Low
   priority; replicating the bug is the auditor's call.

### Multiplayer menus (D)

- **D.1. Fixed in 0ebdad4.** **Password for address joins** (`Dialog_Password`, `mmInterface::JoinLAN` / `JoinPasswordSession`). MM2 tries
   the join, and when the host answers "password needed" (join result 2) opens dialog 25; a wrong password shows
   `badpass_dlg` (24) and re-opens dialog 25 (`mmInterface::Update` cases 0x18, 0x19). OpenMM2 asks only when a LAN
   advert says `hasPassword`; `AddressDialog::go` calls `joinSession(fe, *addr, false, {})`
   (`PagesMulti.cpp`), the client closes on the challenge with `BadPassword` (`net/Session.cpp`) and the
   player gets "Enter a valid password" with no prompt and no retry (`PagesMulti.cpp`). Large: a passworded
   session reached by address (every Internet game) cannot be joined from the UI.
- **D.2. Fixed in 33a72e8.** **Lobby layout: button row, race map, city name** (`NetArena::NetArena`, `NetArena::LoadRaceMap`). widget.csv
   menu 12 puts EJECT (279,380), HOST SETTINGS (395,380), SELECT VEHICLE (508,380) in a row above BACK / GO DRIVE,
   the race map (22,194,242x184) in the left middle panel and the city name (white text node at 36,396) in the box
   below it. OpenMM2 stacks the three buttons in the left panel at x 148, y 206/250/294
   (`PagesMulti.cpp`, commented as inferred), draws no race map, and shows help pictures `lobb_srv.jpg`,
   `lobb_set.jpg`, `mn_mp.jpg` at (40,396) (`PagesMulti.cpp`) — build 3393 never references
   `lobb_srv`/`lobb_set` and NetArena has no description label. Large (whole left half of the lobby).
- **D.3. Fixed in 33a72e8.** **HOST SETTINGS text** (`NetArena::PostHostSettings`, `NetArena::GetRaceName`). MM2, white, 6 lines: (0) mode
   name from the race-type list (Blitz / Circuit / Checkpoint / Cops & Robbers; " " for Cruise); (1) race name: 399
   "Cruise", 400-402 "C&R Free-For-All / C&R Cops & Robbers / C&R Robber Teams", or entry raceId+1 of the city's
   blitz/checkpoint/circuit name list ("Open" otherwise); (2) weather 408 "Weather: Clear", 624 "Cloudy" (no prefix,
   an MM2 quirk), 409 "Weather: Foggy", 410 "Weather: Raining"; (3) time 412-415 "Time: ..."; (4) Circuit "Laps: N"
   (418) or C&R "Gold Weight: None / Quarter Ton / Half Ton" (423, 420-422 for mass 0/100/200); (5) C&R
   "Limit: N Points" / "Limit: N minutes" / "Limit: None" (427, 424-426). City name only under the map. OpenMM2
   (`PagesMulti.cpp`) shows the session name, "Locale:" (87), "Event: <mode> - <race>", laps,
   "Limit ... Gold: Weightless/..." on one line, "Time: X Weather: Y", traffic/peds/cops percentages and
   "Players: n/m Password". Medium.
- **D.4. Fixed in 33a72e8.** **Ready resets** (`NetArena::SetMyStatus`). MM2 clears a joiner's ready and resends it when the host changes the
   settings (`mmInterface::MessageCallback` session-data message 0x31: `SetMyStatus(0)`, `EnablePlayButton`, status
   message 0x202) and when the joiner opens SELECT VEHICLE (`mmInterface::Switch` case 8). OpenMM2 keeps the flag in
   both cases (`net/Session.cpp`, `843-855`; `LobbyPage` has no reset). Medium: the host can start a race
   whose new settings nobody re-confirmed, or while someone is still picking a car.
- **D.5. Open (network and session).** **Join in progress** (`NetArena::LateJoin`). MM2 seals only Blitz/Checkpoint/Circuit sessions at start
   (`mmInterface::Update` lobby id 9999: `SealSession` unless the mode is Cruise or C&R); joiners of a running
   Cruise/C&R game get `lobb_jn` and drop into the race (`JoinSession` → `LateJoin`; message 0x32 also switches a
   waiting joiner to it). OpenMM2 never sets `allowJoinInProgress` (`net/Protocol.h`) so the host rejects them
   (`net/Session.cpp`, "The race has already started."). Medium.
- **D.6. Fixed in cecdcfa (the error icon is open: mmCompRoster::SetError).** **Roster rows** (`mmCompRoster`, `NetArena::AddRosterName`, `ShowRosterTeam`, `SetStatus`). MM2: "ready" icon
   (or "error") at row x+2 when ready, team dot (`blue_dot`/`red_dot`) beside it in C&R team modes, name at +26 px
   cut to 6 chars + "..." when wider than 0.09 of the screen width (7 if the 6th byte is a DBCS lead byte), car at
   +26+w/3, no host marker; the host's row shows the ready icon (`mmInterface::CreatePlayer` sets the host ready).
   OpenMM2 (`PagesMulti.cpp`): full name + " (Host)", "COPS/ROBBERS/BLUE/RED" before the car, "Ready"/"..."
   text right-aligned at 626, nothing for the host. Small-medium.
- **D.7. Fixed in 33a72e8.** **GO DRIVE enable** (`NetArena::EnablePlayButton`, `mmInterface::MultiAllReady`). MM2's host button is always
   enabled; pressing it while someone is not ready does nothing. OpenMM2 greys it until everyone is ready
   (`PagesMulti.cpp`). Small.
- **D.8. Fixed in 33a72e8.** **Leaving the lobby**. MM2's Escape/BACK switches straight to menu 10 (`mmInterface::Update` lobby state 1 →
   `Switch(10)`, which destroys the player and disconnects). OpenMM2 asks "End Session?" / "Quit to Lobby?"
   (`PagesMulti.cpp`), strings 481/479 that MM2 uses only in the in-race quit popup. Small.
- **D.9. Open (network).** **Host migration** (`NetArena::SetHost` at run time). When the host leaves, MM2 keeps the session: the new host
   gets "**You are now the host**" (70) and the host buttons (`mmInterface::MessageCallback` 0x2f). OpenMM2 ends the
   session for everyone (docs/multiplayer.md "no host migration") and instead prints string 70 when a session is
   created (`game/net/NetGame.cpp`), which MM2 never does. Medium (transport design; at least drop the line
   on creation).
- **D.10. Fixed in 33a72e8 and 41cf9c5.** **Chat** (`NetArena::AddGameChatLine`, `PostChatMessages`, `ResetGameChat`, `ChatEntry`). MM2 lines are
    " Name> text" (" %s" around "%s> %s", own and received lines alike), 3 visible, unwrapped, field max 128; the log
    is cleared whenever the lobby is entered except from vehicle select / options / host settings (so after every
    race and on joining). OpenMM2: "Name: text" (`PagesMulti.cpp`), word-wrapped, up to 64 kept, 200 chars
    (`net::kMaxChatLength`), system lines in another colour, kept across races. Small.
- **D.11. Fixed in 0ebdad4.** **Dialog buttons and placement** (`Dialog_TCPIP`, `Dialog_Host`, `Dialog_Password`, `Dialog_Eject`). MM2: Cancel
    `dlg_can` on the left (18,276), DONE `dlg_done` on the right (280,276); eject has only DONE (280,288); cards are
    centred by their picture size (`PUMenuBase::PUMenuBase`); Max Players roller at (248,156,60x32). OpenMM2: `dlg_ok`
    left (+60,+280), `dlg_can` right (+240,+280) and a fixed origin (120,75) (`PagesMulti.cpp,
    406-412, 1101-1107`; `ui::dialogOrigin(pictureSize)` exists and `PagesMain.cpp` already uses
    dlg_done right / dlg_can left); max players box (236,162,38x23). Small but visible on every dialog.
- **D.12. Fixed in 0ebdad4.** **Enter in address / password fields** (`Dialog_TCPIP::IPAddressCallback`, `Dialog_Password::PasswordCallback`):
    MM2 treats Enter as DONE; OpenMM2's `onCommit` is empty (`PagesMulti.cpp`). Small.
- **D.13. Fixed in c7c3f26 (the kicked player's messages are the network's).** **Eject flow** (`Dialog_Eject::BootButtonCB`). MM2 boots on clicking a name, removes it and stays open until
    DONE; the booted player gets `boot_dlg` (dialog 43). OpenMM2 selects, OK kicks one player and closes
    (`PagesMulti.cpp`); the kicked player reads "You have been ejected" and the others "X has been
    ejected" (`game/net/NetGame.cpp`), texts MM2 does not have (others see "has left", 69). Small.
- **D.14. Partly fixed in cecdcfa (the last values, 25 characters); one player is open (NetGame clamps to 2).** **Host Options values** (`Dialog_Host`). MM2: max players 1-8, password 25 chars, and the dialog remembers the
    last accepted values (PreSetup / Clear). OpenMM2: 2-8 (`PagesMulti.cpp`, clamped again in
    `NetGame.cpp`), 24 chars, fresh each time. Small.
- **D.15. Fixed in c7c3f26.** **Net name length** (`NetSelectMenu::NetSelectMenu`): MM2's NET NAME field takes 12 characters; OpenMM2 24
    (`PagesMulti.cpp`, `net::kMaxNameLength`). Small.
- **D.16. Fixed in c162521 (OpenMM2's list rows are its own).** **Sessions screen extras** (`NetSelectMenu::EnableSearchLabel`, `FocusDescription`, `SetSession`). MM2 shows
    "Looking for games..." in the description box (40,396) only while enumerating; OpenMM2 draws it inside the list at
    (306,256) whenever the list is empty (`PagesMulti.cpp`). List box (289,243,329x131) vs
    `Box{298,246,306,163}` (`PagesMulti.cpp`). HOST/JOIN/default help pictures `lobb_srv.jpg` / `mn_mp.jpg`
    (`PagesMulti.cpp`) have no MM2 counterpart. Small.
- **D.17. Fixed by the game-flow audit (RaceScreen::startSlot).** **Start slots** (`NetStartArray`). MM2 packs slots 0..n-1 in player order at start and distributes them; OpenMM2
    uses `NetGame::localId()` (`app/RaceScreen.cpp`), so after a player leaves the grid has a gap, and an id
    above 7 gets no offset (`RaceSetup.cpp`, cars overlap at the start point). Small.
- **D.18. Open.** **SELECT VEHICLE in Cops vs. Robbers**: MM2 keeps the button and locks every car but the team's
    (`mmInterface::RequestProverb`: vpmustang99 for team 1), so the paint can still be chosen; OpenMM2 disables the
    button (`PagesMulti.cpp`). Small.

### Player data (E)

- **E.1. Fixed in 4199e54.** **Hall of Fame entries never record "passed".** MM2's
   mmMiscData::NewRecord stores its passed argument (mmRecord::SetPassed) in
   both the time slot and the score slot. The callers pass the mode's
   ProgressCheck result: mmSingleRace::RegisterFinish once, and
   mmSingleCircuit::RegisterFinish with every lap. mmSingleBlitz records only
   passed finishes, so its entries are always 1.
   mmInterface::HOFFillRecords reads mmRecord +0x104 and passes style 2
   (passed) or 1 to Dialog_HallOfFame::AddRaceRecord.

   In OpenMM2, `app/frontend/FrontendScreen.cpp` build each
   `HallEntry` with only four fields, so `passed` is always false. Every entry
   is stored as not passed, and the passed style can never show. OpenMM2's
   `RaceRecordsDialog` does not draw the style yet; that is the
   Dialog_HallOfFame group's. The fix is one field: `result.won`.
- **E.2. Open (hud-views).** **A new driver's instrument cluster (inferred from the asm; confirm in
   the retail game).** DefaultViewSettings sets +0x716D to 0, and
   SetViewSettings copies it to MMSTATE +0x371 (0x6B1981). Three functions use
   that byte:

   * mmPlayer::Reset activates the mmExternalView node (mmHUD +0x6F8, flags
     +0x708 bit 0) only when the byte is set and the dashboard is off.
   * mmHUD::ToggleExternalView ("HUD Toggle") writes it.
   * mmHUD::DeactivateDash brings the cluster back only when it is set.

   mmStatePack::SetDefaults never sets it. So, per the asm, a new driver
   (PlayerCreate, and the first driver from InitPlayerInfo) races without the
   speed and tach cluster until pressing HUD Toggle, and the choice is kept per
   driver.

   OpenMM2's `game/session/Hud.h` `HudOptions::cluster` defaults to true
   and is read from the global `[HUD] Cluster` (`app/RaceScreen.cpp`, `loadViewSettings`).
   The visible effect is that OpenMM2 shows the cluster on a new driver's
   first race.
- **E.3. Kept (documented deviation, session.md).** **The HUD view bytes are not reset for a new driver.** MM2 gives every new
   driver map off, cluster off and icons on (DefaultViewSettings via
   mmPlayerConfig::Reset), keeping orientation and zoom from the live state.
   OpenMM2 keeps `[HUD]` globally (`app/RaceScreen.cpp` `loadViewSettings`),
   so a new driver inherits the last driver's map mode, cluster and icons.
   This is a documented deviation (session.md:298) with a small effect.
- **E.4. Open (no visible effect with the retail data).** **GetTotalScore's circuit count.** MM2's circuit loop uses the count in the
   driver's embedded city record (mmPlayerData +0x27C, the last city opened),
   not the city being summed. OpenMM2's `game/Profile.cpp` uses each
   city's own count. There is no visible effect with retail data: London and
   San Francisco both have 10 circuits (camera-props.md, documented
   deviation).
- **E.5. Fixed in adc6a6d.** **A comment only.** `game/Profile.h` says that MM2 "writes only the
   time into a time slot and only the score into a score slot, so the other
   field of a slot is stale". In fact an mmRecord has a single value field
   (+0x88): SetScore stores the score there as a float and GetScore
   truncates it. A score slot has no time at all. Behaviour is unaffected.

## Open

What is left, and what it needs:

- Button callbacks on the press (A2.1): MM2 runs a button's own callback (roller arrows, popup OK / Cancel, toggles) when the mouse goes down and the menu's action on the release; OpenMM2's buttons have one action, on the release. Needs the two split per button.
- Space on bitmap buttons (A2.2): the button's sound and callback without the menu's action.
- Read-only widgets taking the mouse focus (A1.4, A2.7, B.11): the hit test on enabled widgets, read-only ones included, without lighting them; About would then open on its credits spot.
- Text cut at its box (A1.9).
- The text field's white outline in the menus (A2.15): to be checked against the original game first.
- The customize list (A2.17): MM2 gives it no keyboard stop (the mouse only), red only while capturing, rows about 16 px; OpenMM2 keeps Up / Down selection for keyboard players, which the maintainer may want to keep.
- The crash course's environment carried into cruise (B.7, C.2): `fe.config` would have to keep the last lesson's settings.
- Cops vs. Robbers in the lobby's garage (D.18): keep SELECT VEHICLE and lock every car but the team's.
- `mmCompRoster::SetError`: a per-player "race locale installed" flag in the session.
- `mmInterface::SendHostCars` / `LimitToHostCars`: only matter with add-on cars.
- GetTotalScore's circuit count (E.4) and the force-feedback intensities after a controller change (B.13): no visible effect.
- Network items (D.5 late join for Cruise and Cops and Robbers, D.9 host migration, D.14 one-player sessions, B.10 "noname1" duplicates): see below.

Kept on purpose: A2.12, B.8 and C.5 (MM2 bugs a player can see; the maintainer's call), B.12 and E.3.

## For other subsystems

- hud-views: asViewCS and asDofCS are ported here with the showroom; their rows in hud-views.md can point to this record. A new driver's instrument cluster starts off in MM2 (E.2: DefaultViewSettings writes +0x716D = 0), while OpenMM2's global `[HUD] Cluster` defaults to on.
- vehicle-physics: the `Vehicle` menu class is recorded here.
- input-ff: ControlSetup creates an `mmMouseSteerBar` but never adds it to the menu, so MM2 never draws it (only the constructor and destructor touch ControlSetup +0x7224); the control page's bar is gone (4c0d84d). input-ff.md's `mmMouseSteerBar` row now says not needed, and the unused `GameInput::steeringUnfiltered` is removed. mmInput::Init's keyboard fallback for a joystick type without a device runs only when the in-race options change the controller, not when a race starts.
- network: duplicate names (`GetUniquePlayerName` makes "noname1", counting the names that start with ours; `net/Session.cpp` makes "noname (2)"); "You are now the host" (70) is printed when a session is created (`NetGame`), which MM2 prints only on host migration; the kicked player's "You have been ejected" and the others' "X has been ejected" are OpenMM2's (MM2: `boot_dlg`, and "has left" for the others); sessions clamp to 2 players (MM2 allows 1); host migration (D.9) and late joins (D.5).
- game-flow: merged; PUExit is unreachable, as game flow found; PUKey and PURoster use the frontend's pages; the host leaving a race takes everyone back to the lobby (game flow's choice), so the network results come up over the lobby.
- infrastructure: the popup's MenuManager has its own pointer (`MenuManager::InitCommonStuff` creates the `sfPointer`, drawn also with an IME), so the in-race popup should draw `midcursor` (`drawMenuPointer`) as the frontend pages do.
