# Parity audit: frontend-ui

Audited against MM2Recomp (midtown2.exe build 3393) on 2026-10-07.

Summary: 151 functions; verified 80, fixed 39, deviation 11, inferred 3,
open 2, openmm2 16.

Scope: the menus (`src/app/frontend/*`), the widgets and layout
(`src/ui/*`) and how `src/app/Settings.*` maps onto MM2's player
configuration. MM2 names are the NuHook linker-map names; positions are in
640x480 reference pixels. Profiles' file format belongs to camera-props,
the race loop to session (see "For other areas" at the end).

## Widgets (`src/ui/Widgets.*`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `style::kValueText`, `kValueTextFocus`, `kRecordText`, `kOptionDisabled` | `MenuManager::GetFGColor` (frontend branch), `mmDropDown::SetDisabledColors` | verified | colour 0 yellow, 3 red (focused or open), 2 white (records); locked drop-down entries (0.5, 0.5, 0). Colour 1 (0.1 grey) is used only for widget labels, which the painted backgrounds replace. |
| `style::kPopupText`, `kPopupFocus`, `kPopupDisabled` | `MenuManager::GetFGColor` (popup branch) | verified | 0/1 white, 5 (0.35) grey, otherwise (0.933, 1, 0.129). |
| `style::valueFont`, `smallFont`, `titleFont`, `popupFont` | `MenuManager::GetFont`, strings 560, 559, 251, 569 | verified | string table entries checked against the retail table. |
| `style::kValueTextDisabled`, `kHelpText` | — | openmm2 | colours of OpenMM2's own text (messages, lobby). |
| `NavReader::read`, `NavReader::repeat` | `MenuManager::ScanGlobalKeys` (keys), DirectInput key repeat | inferred | key mapping verified (Down/Tab next, Up previous, Enter, Escape, Home/End); the 0.40 s / 0.075 s auto-repeat stands in for the OS key repeat MM2 received. Gamepad navigation is an OpenMM2 addition. |
| `drawSpriteFrame`, `spriteFrameSize` | `UIBMButton::Cull`, `GetHitArea` | verified | frames stacked vertically, one frame's rectangle is the hit area. |
| `SpriteButton::draw` | `UIBMButton::Switch`, `Cull` | verified | normal/highlight/pressed/disabled; 5-frame lit state for the strip's OPTIONS (`uiNavBar::OptionActive`). |
| `SpriteButton::mouse`, `SpriteButton::activate` | `UIBMButton::Action`, `UIMenu::CheckMouseHits`, `sfPointer::Update` | fixed | the sound played on the release and only after a press on the same button; MM2 plays the button's sound on the press and acts on a release over the button wherever the press began. Enter/Space play the sound and act. |
| `LampItem::*` | `UIBMButton::DoToggle`, `Action` (5-frame toggles and radio "Mex" buttons) | verified | flips on the press, Enter or Space. |
| `ValueBox::ValueBox`, `draw` | `UITextDropdown::Init`, `Cull` | verified | 23 px tall (drop_arrow frame + 2), value 5 px in, arrow frames unfocused/focused/open. |
| `ValueBox::activate`, `adjust`, `mouse` | `UITextDropdown::Action` | verified | Enter or a press in the box opens; Left/Right do nothing while closed. |
| `ValueBox::modalInput` | `UITextDropdown::CaptureAction`, `TextDropWidget::IncDrop`/`DecDrop`/`SetValue`, `mmDropDown::SetHighlight` | fixed | arrows skipped locked entries and clicks on them were ignored; MM2 steps by one and turns an entry that cannot be picked into the first one that can (`mmDropDown::FindFirstEnabled`), on keys and on a release over a locked entry. Hover highlights enabled entries only. |
| `ValueBox::listCells` | `mmDropDown::InitString` | fixed | the list moved left only as far as needed to fit; MM2 moves it one box width left per extra column, and only when the list is too tall and two columns would not fit right of the box. Rows per column unchanged. |
| `ValueBox::drawPopup` | `mmDropDown` cells, `SetDisabledColors` | verified | black cells, white outline on the highlight, olive locked entries. |
| `stepOption` | the roller_up/roller_down `UIBMButton` callbacks (`MainMenu::DecPlayer`/`IncPlayer`, `RaceMenuBase::DecRaceName`..., `CrashCourse::IncRaceName`/`DecRaceName`, `VehicleSelectBase::IncColor`...) | verified | wrap per page; clamping arrows stop at a locked entry. |
| `TextButton::*` | `UIButton` in `PUMenuBase` menus | verified | colours above; acts on Enter or a click; the popup "Selectionmade" (`MenuManager::PlaySound(1)`). |
| `TextBox::*` | `UILabel` | verified | value font, colour 0. |
| `Roller::*` | `UITextRoller2::Init`, `Action`, `EvalMouseXY`, `SetValue`, `Inc`, `Dec`, `Cull` | verified | value centred, colour 3 when focused; Left/Right and the arrows step by one, clamp, play "Switch" at 0.85 even at a limit; up arrow increments; arrows hidden when read-only; down arrow 1 px below the up arrow's frame. |
| `Slider::segments`, `step` | `mmSlider::SetStep`, `SetRange` | verified | 2 px segments, (round(w) - 50) / 2 rounded - 1, clamped 2..300; 20 positions above 20 segments, else 5. |
| `Slider::draw` | `mmSlider::Cull`, `LoadBitmap` | verified | bar from slider_actl at y + 11 in thirds (unfocused, focused, disabled), empty part a 1 px row of slider_inactl; read-only uses slider_ro* without arrows; arrow frames 0/1/2 (clicked)/4 (disabled). |
| `Slider::adjust`, `Slider::mouse` | `UISlider::Action`, `UISlider::EvalMouseXY`, `mmSlider::Inc`/`Dec`/`SetValue` | verified | step and clamp; a track click sets the value without snapping; "Switch" every time. |
| `ListBox::*` | — | openmm2 | OpenMM2's session and eject lists. |
| `TextEntry::draw` | `UITextField::ToggleField`, `SetTextField`, `Card2D::Cull` | fixed | drew a white 1 px frame that MM2 does not have; MM2 draws " %s", red on an opaque black card while editing and yellow on nothing otherwise (the frame is painted on the backgrounds). |
| `TextEntry::modalInput`, `activate`, `focusChanged`, `beginEdit` | `UITextField::Action`, `KeyAction`, `WmCharHandler`, `IsValidChar` | verified | keys go to the focused field (string 605 "0": no capture mode); the first character after creation replaces the text, control characters are refused, the length is capped, Backspace removes one character, Enter commits (the callback) and ends editing, Tab ends editing and moves on. MM2 re-arms "first key replaces" only after Enter on fields created with that flag, OpenMM2 on every focus gain; MM2's only frontend field (the new driver's name) starts empty in a new dialog, so this shows nowhere in MM2's menus. |
| `Picture::*` | `UIIcon` | fixed | new `focusStop`: an icon added to a menu is enabled and writable by default (`uiWidget::uiWidget`), so the focus stops on it while shown (`UIIcon::Switch` draws nothing); the race map uses it. |
| `Custom` | — | openmm2 | drawing hook. |
| `Menu::step`, `firstFocusable`, `setFocus`, `focus`, `setInitialFocus`, `resetFocus` | `UIMenu::Increment`, `Decrement`, `FindTheFirstFocusWidget`, `SetFocusWidget`, `Enable`, `MenuManager::ToggleFocus` | verified | focus stops are enabled, writable widgets in creation order; past the end the other group's first widget; dialogs wrap; focus returns to the initial widget on every entry. |
| `Menu::update` | `MenuManager::ScanGlobalKeys`, `UIMenu::CheckMouseHits`, `sfPointer::Update` | fixed | Escape on the strip only moved the focus back; MM2 moves it back and backs the page up. Mouse focus, highlight clearing over empty space and the popup "Moveselector" verified. |
| `Menu::draw`, `drawContent` | `MenuManager::OpenDialog` (`ClearWidgets`), the pages' `FocusDescription` | verified | a page under a dialog shows no focus or help picture. |
| `Menu::moveToEnd`, `widgetsInGroup`, `modalActive` | — | openmm2 | helpers. |

## Layout (`src/ui/MenuLayout.*`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `MenuLayout::parseWidgets` | `WArray::Read`, `WArray::AddWidgetData` | verified | header line skipped; fields split on commas with empty fields skipped (`strtok`), menu id, index and the rectangle read with `atoi`; lines are read 127 characters at a time, and the retail table's longest line is 73. |
| `MenuLayout::load` | `WArray::Read` | deviation | a missing tune/widget.csv stops MM2 with an error; OpenMM2 warns and uses the code positions. |
| `MenuLayout::find`, `widget`, `position` | `WArray::RetrieveWidgetData`, `UIMenu::AddBMButton`/`AddSlider`/`AddIcon` | verified | each non-zero code component replaced by a non-zero table value, then the menu's origin added. |
| `dialogOrigin` | `PUMenuBase::PUMenuBase` | verified | centred: ((1 - w) / 2, (1 - h) / 2). |

## Text, fonts, textures (`src/ui/Text.*`, `Font.*`, `TextureCache.*`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `FontSpec::parse`, `FontSpec::bold` | string-table font entries ("face, size, size2, charset, weight") | verified | format checked against the retail string table (558-573, 251). |
| `findFont`, `bundledFontPath`, `FontFile::*` | GDI `CreateFont` | inferred | the system font when installed, else a bundled substitute (Liberation Sans, Gillius). |
| `FontAtlas::*`, `nextCodepoint` | GDI text rasterisation | openmm2 | stb_truetype atlases. |
| `TextRenderer::*` | `mmTextNode` drawing | deviation | text rasterised at the output resolution (any-resolution / HiDPI scaling). |
| `TextureCache::get`, `load` | `nodeGetBitmap` | openmm2 | GPU upload of archive images. |
| `TextureCache::getColorKeyed` | gfxBitmap black colour key (`gfxPipeline::CopyBitmap` transparent) | inferred | black made transparent. |
| `drawImage` | `gfxPipeline::CopyBitmap` | deviation | scaled to the window (UI scale modes). |

## Frontend core (`src/app/frontend/Frontend.h`, `FrontendScreen.cpp`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `menu_id` | menu ids in `mmInterface::mmInterface` / `mmInterface::Update` | verified | every id cross-checked with the dialogs `mmInterface::Update` opens (0x11 new driver ... 0x29 customize). |
| `layout` constants | code positions | verified | fallbacks only; tune/widget.csv wins. |
| `NavOptions`, `addNavStrip` | `uiNavBar::uiNavBar`, `OptionActive`, `mmInterface::Update` (strip ids 100-104) | verified | OPTIONS, HELP, MINIMISE, EXIT, PREV in that order; OPTIONS opens the options, is lit there and cancels a sub-page (vtable CancelAction, then `Switch(2)`). |
| HELP button | `MenuManager::Help` | deviation | MM2 minimises and runs WinHelp; OpenMM2 shows a message. |
| `addBack` | `uiNavBar` PREV (index 4), `TurnOnPrev`/`TurnOffPrev` | verified | |
| `Frontend::Frontend` (city order) | `mmCityList::LoadAll` | verified | sf.cinfo first, then the enumerated .cinfo files. |
| `Frontend::push`, `pop`, `replace`, `popTo` | `mmInterface::Switch`, `MenuManager::Switch`, `UIMenu::Enable` | verified | entering a page resets its focus (page stack itself is OpenMM2's). |
| `Frontend::topChanged` | `MenuManager::Switch`, `PlayMenuSwitchSound`, `AllocateMenuSwitchAudio` | verified | ids and volumes: 1 Selectionmade 0.87, 2-5 UIoptions 0.9, 7 and 0x28 UIraces 0.9, 8 and 11 UIvehicles 0.9, 10 and 12 UImulti 0.87; not while already playing. |
| `Frontend::playSound`, `soundPlaying` | `AudSoundBase` | openmm2 | sound bank glue (always the 22 kHz files now). |
| `cityIndex`, `currentCity`, `racesFor` | — | openmm2 | lookups. |
| `selectProfile`, `configFromProfile` | `mmInterface::PlayerLoad`, `PlayerSetState` | verified | restores event, city, car, paint, difficulty; Cops & Robbers becomes cruise where `mmInterface::Switch` does it. |
| `saveProfile` | `mmPlayerData::Save` | openmm2 | profile file is camera-props'. |
| `applyRaceDefaults` | `RaceMenuBase::SetStateRace`, `CrashCourse::SetEnvironment` | verified | cruise noon/clear/0.25/0.5/1; races from the race tables per difficulty; checkpoint 1 lap; circuit laps and opponents; blitz leaves laps and opponents; lessons no traffic and all cops. MM2 also sets the opponent density to 8 for lessons (see open items). |
| `raceName` | `mmInterface::GetRaceName`, `PlayerFillStats` (strings 80, 79, 78) | verified | |
| `recordResult` | `mmPlayerCityRecord::NewRecord`, `mmMiscData::NewRecord` | open | record rules are camera-props'/session's; not re-checked here. |
| `startRace` | `mmInterface::BeDone` | verified | saves last car, paint, city, event and race. |
| `Frontend::update`, `draw`, `drawPage` | `mmInterface::Update`, `MenuManager::Update` | openmm2 | frame glue. |
| `PictureDialog`, `Frontend::dialog`, `notice` | `Dialog_Message`, `PUMenuBase`, `mmInterface::Update` dialog cases | verified | Escape does what the last button does (quit/delete close, duplicate reopens the new-driver dialog). |
| `askQuit` | `mmInterface::Switch(0x1b)`, `Update` case 0x1b | verified | OK (id 100) quits, CANCEL closes. |
| `MessageDialog`, `message`, `question` | — | openmm2 | OpenMM2's text messages. |
| `crashCourseReturn`, `setCrashCourseReturn` | the crash-course global in `mmInterface::Update` / `ShowMain` | fixed | new: work-experience races returned to the race menu; MM2 returns every race started from the Crash Course page to it. |
| `timeOfDayName`, `weatherName` | — | openmm2 | English names for the lobby panel. |
| `modeDisplayName` | strings 585-589, 78 | verified | |
| `formatTime` | `GetLocTime` | verified | "%d:%02d:%02d" of t + 0.005 (double), "  ---  " for no time. |
| `Script::*`, `scriptOnce`, `injectKey` | — | openmm2 | `OPENMM2_FRONTEND_SCRIPT` automation; still works (checked with the race menu). |
| `FrontendScreen::FrontendScreen` (first start) | `mmInterface::InitPlayerInfo` | fixed | DriverX got the first car of the list; MM2's driver without a last car gets cruise in vpbug (`PlayerSetState`), the profile's default. |
| `FrontendScreen::FrontendScreen` (after a race) | `mmInterface::ShowMain` | fixed | returns to the Crash Course page for every race started there, showing the event just driven; else the race menu. |
| Results as a frontend page | `PUResults` popup over the paused race | deviation | shown after the race instead of in it (documented). |

## Main menu and its dialogs (`PagesMain.cpp`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `LoadingPage` | `mmInterface::mmInterface`, `lvlProgress::UpdateTask`, `ProgressCB`, `ProgressRect` | fixed | showed an extra 10 % step and a fractional width; MM2 reports 20, 30, 35, 40, 50 ... 95, 97, 99, 100 % and draws percent * 640 / 284 whole pixels of #0D2CBA at 349,448, 10 high. One step per frame is inferred (OpenMM2 has already loaded). |
| `DriverPage::DriverPage` (widgets) | `MainMenu::MainMenu`, `InitDriver` | verified | creation order, ids, CRASH COURSE initial focus (`SetFocusWidget`), REPLAY created and turned off. |
| CRASH COURSE, RACES, MULTIPLAYER buttons | `mmInterface::Update` main menu ids 100-102 | verified | crash course left as cruise for multiplayer. |
| QUICK RACE | `mmInterface::Update` id 0x67, `mmInterface::Switch(8)` | fixed | reloaded the driver's last event; MM2 uses the event currently set up (crash course and Cops & Robbers become cruise). |
| CREATE / DELETE / STATS / RACE RECORDS | `mmInterface::Update` ids 0x69-0x6b, 0x6d; `PlayRecordsSound` | verified | 18-driver limit (plim_dlg), last driver (lstp_dlg), delp_dlg; UIrecords at 0.84. The Moveselector sound slot of CREATE/DELETE/STATS is from the earlier pass (sound-slot argument not recovered). |
| DRIVER box and arrows | `MainMenu::InitDriver`, `DecPlayer`, `IncPlayer`, `TDPickCB`, `mmInterface::RefreshDriverList` | verified | creation order, wrapping arrows. |
| `drawBelow` (dropdown_bx) | `MainMenu::InitDriver` (`AddTextDropdown` icon argument), `UITextDropdown::Init` | verified | the DRIVER box gets the dropdown_bx icon 1 px right of and below its corner, under the text. |
| `drawAbove` (driver panel) | `MainMenu::InitDriver`, `DisplayDriverInfo`, `mmInterface::PlayerFillStats` | verified | labels at x 177, values at 209, lines every 0.03333 of the screen from y 165, strings 635-638, 640; SCORE "%5d" for professionals only. |
| `controllerName` | `MenuManager::GetControllerName` | fixed | read `[Controls] Device`, which nothing writes; now the Control page's `[Controls] Controller`. |
| Escape | `mmInterface::Update` main menu back, `MenuManager::PlaySound` | fixed | played "Selectionmade"; MM2's Escape sound is popup-only. |
| `askDelete` | `mmInterface::PlayerRemove` | verified | the deleted driver is the current one; the first in the directory is loaded. |
| LAST RACE / LAST VEHICLE of a new driver | `PlayerFillStats` | fixed | MM2 shows "---" (string 64) for both until the driver has raced; the panel showed the default race's name. It now shows "---" while `Profile::hasLastRace()` is false (wired after the camera-props merge). |
| `NewDriverDialog` (widgets) | `Dialog_NewPlayer::Dialog_NewPlayer`, `PreSetup` | verified | name field (18 chars) first, amateur/pro radio (amateur on entry), DONE and CANCEL; Enter in the field creates. |
| `NewDriverDialog::create` | `mmInterface::PlayerCreateCB`, `PlayerCreate` | fixed | names of spaces were ignored and new drivers got the first car; MM2 ignores only an empty name and the new driver has cruise in vpbug. The dialog passes the name as typed; on this branch `ProfileStore::create` still trims it (camera-props keeps names as typed on its branch). |
| `RecordDialog` | `Dialog_DriverRec::Dialog_DriverRec`, `Dialog_HallOfFame::Dialog_HallOfFame`, `PreSetup` | verified | list (widget 0) then the check boxes in tune/widget.csv order (driver record: blitz, circuit, checkpoint, sf, london, DONE; race records: amateur times, pro times, pro points, blitz, circuit, checkpoint, sf, london, DONE); both open on blitz in San Francisco, the race records on amateur times. |
| `DriverStatsDialog::drawAbove` | `Dialog_DriverRec::InitDriverRecord`, `AddDriverRecord`, `mmCompDRecord::Init`/`SetSubwidgetGeometry`/`SetBltXY`/`Cull`, `mmInterface::PlayerFillRecords` | fixed | the lock sat 2 px right of the row's top; MM2 puts it at the row's left edge 3 px up. A driven race with no car shows "---" (string 64), an undriven one "----". Verified: titles 345-347 and 344 (professionals), 12 rows of 18 px from (81,87) without a scroll bar, columns 18 / 162 / 270 or 280 / 378, lock frames (open, locked, passed), "  ---  " for no time, points "%4d". |
| `RaceRecordsDialog` | `Dialog_HallOfFame::InitRaceRecord`, `AddRaceRecord`, `SetSortState`, `mmCompRaceRecord::SetSubwidgetGeometry`, `mmInterface::HOFFillRecords`, `GetTimeString`, `GetScoreString` | fixed | rows from y 88 at columns 2/150/262/340 and only filled entries; MM2 lists five rows for every race (empty ones with no driver, "  ---  " and "---") from (81,91), 18 px apart, columns 4 / 125.5 / 264.4 / 331.8 px (a 486 px row split 0.25, 0.2857, 0.1923 less 26 px). The third title follows the table (349 SCORE or 350 TIME). The scroll arrows' positions are inferred; the mouse wheel is an OpenMM2 addition. |
| `fit` | `Dialog_DriverRec::AddDriverRecord`, `Dialog_HallOfFame::AddRaceRecord` | fixed | kept 15/10 characters at 132/96 px; MM2 keeps N + 1 characters (N from strings 664-668, "15", "10", "10", "15", "15": 16 characters for driver-record races, 11 for their cars, 11 for record races, 16 for their cars and 16 for record drivers) when the text is wider than 132.48 or 96 px. |

## Race menu, garage, showcase (`PagesRace.cpp`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `RacesPage::RacesPage` (widgets) | `RaceMenuBase::Init`, `RaceMenu::RaceMenu` | verified | lamps, RACE NAME and arrows, LAPS, OPPONENTS, LOCALE and arrows, TIME and WEATHER with arrows (strings 629-632, 625-628), three density sliders, help label, map, race_veh (9999). |
| initial focus and map | `RaceMenuBase::Init` (`SetFocusWidget` after `AddIcon`), `LoadRaceMap`, `UIIcon` | fixed | the menu opened on the cruise lamp and the map was not a widget; MM2 opens on the map icon (nothing highlighted) and the shown map is a focus stop. |
| `modeHelp`, `update` (help label) | `RaceMenuBase::FocusDescription`, `PreSetup` | verified | race_btz/race_cir/race_cp/race_rom by mode; shown while a lamp is focused or after a change. |
| `racePassed`, `refresh` | `RaceMenuBase::SetRW`, `ChangeLocalVals` | verified | race name hidden in cruise, LAPS/OPPONENTS only for circuits and read-only until passed, time/weather/densities read-only until passed, circuit traffic always read-only, race_cov outside circuits. The unlock cheat is not ported. |
| `selectMode`, `validateRace` | `RaceMenuBase::GameCallback`, `SetStateRace` | verified | race 0 and its defaults. |
| `selectCity` | `RaceMenuBase::AnotherCityChangeCB`, `CityChange` (read from the asm), `GameCallback` | verified | the city's race tables are loaded, the lamps of modes the city lacks are disabled and such a mode becomes cruise, then race 0 with its defaults and the help picture. |
| `mapPicture` | `RaceMenuBase::LoadRaceMap` | verified | `<RaceDir>_map<mode><race>`, hidden when missing. |
| race_veh GO | `mmInterface::Update` case 7 id 9999 | fixed | now clears the crash-course return. |
| crash course / Cops & Robbers on entry | `RaceMenuBase::PreSetup`, `mmInterface::Switch(7)` | verified | both become cruise. |
| `VehiclePage::VehiclePage` | `VehicleSelectBase::InitCarSelection` | fixed | opened on CAR COLOR; MM2 opens on VEHICLES (`SetFocusWidget` after it). Widget order, read-only stat sliders, VEHICLE SHOWCASE (id 0x32), TRANSMISSION (633/634; up = Manual, down = Automatic, clamping: `DecTrans`/`IncTrans`) verified. |
| colour arrows | `VehicleSelectBase::IncColor`, `DecColor` | verified | wrap. |
| `carDescription` | `VehicleSelectBase::ShowCarDesc` | verified | `<car>_lck<n>_p` (professional) then `<car>_lck<n>` for a locked paint job, `<car>_lck_p` then `<car>_lck` for a locked car, else `<car>_ulck`; hidden when none exists. |
| `enterWithUnlockedCar` | `VehicleSelectBase::PreSetup`, `CurrentVehicleIsLocked`, `SetLastUnlockedVehicle`, `SetPick` | fixed | moved away only from a locked car; MM2 also moves away from a locked paint job, to the last unlocked car picked (else the first unlocked one) with its remembered paint; not when coming back from the showcase. |
| `pickVehicle` | `VehicleSelectBase::SetPick`, `IncCar`/`DecCar` | verified | wrapping, paint remembered per car, last unlocked car kept. |
| `playSelectSound` | `VehicleSelectBase::PreSetup`, `GetCarTitle`, `VehicleSelectBase::VehicleSelectBase` | fixed | played even while still playing; MM2 plays `<car>_select` (0.91) on entering and on a pick only when it is not already playing. |
| stat bars | `VehicleSelectBase::LoadStats`, `AssignVehicleStats`, `FillStats` | verified | horsepower, top speed, durability, mass of every valid car; each bar from 0.5 x the smallest to 1.1 x the largest. |
| `drawShowroom` | `VehicleSelectBase` 3D view | deviation | a showcase photo stands in for the turning 3D car (documented, not implemented). |
| GO DRIVE | `mmInterface::Update` case 8, `CurrentVehicleIsLocked`, `ShowLockedVehicleMessage`, `BeDone` | verified | lock_dlg (0x17) for a locked car or paint. |
| `ShowcasePage` | `VehShowcase`, `mmInterface::Switch(9)` | verified | no navigation strip; back to the garage. |

## Crash Course (`PagesCrash.cpp`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `CrashIntroPage` | `CrashCourseIntro::CrashCourseIntro`, `PreSetup`, `mmInterface::Update` case 0x28 | fixed | London (id 0) and San Francisco (id 1), hidden PREV hotspot; the crash course, lesson 0 was set only when the page was built, MM2 sets it every time the intro is entered. |
| `CrashCoursePage::CrashCoursePage` (widgets) | `CrashCourse::CrashCourse` | verified | TRAINING, lesson box and arrows, BLITZ, CHECKPOINT, race box and arrows, GO (9999). |
| initial event | `CrashCourse::PreSetup` | fixed | always started on training, lesson 0; MM2 keeps a lesson, blitz or checkpoint event, so after a race the page shows the event just driven. |
| `update` (GO sprite, help) | `CrashCourse::SetVehicleNext`, `SetEnvironment`, `PreSetup` (help list lon_cc0..., sf_cc0...) | verified | veh_go for an unpassed lesson, race_veh otherwise and for work experience; inactive box and arrows hidden (`SetRaceState`). |
| lesson and race arrows, lock masks | `CrashCourse::IncRaceName`, `DecRaceName`, `SetRaceState` | verified | clamp, stop at a locked entry; 13 lessons. |
| `select` | `CrashCourse::GameCallback`, `SetRaceState` | verified | race 0. |
| `proceed` | `mmInterface::Update` case 0x27 | fixed | school car (vpbullet in sf, else vpcab, paint 0) for an unpassed lesson, else the garage; now also sets the crash-course return. |
| `lessonStringId` | strings 532-557 | verified | |
| `drawAbove` (ticks) | `CrashCourse::CrashCourse` (`ccStatus::LoadBitmap` loop), `ccStatus::SetStatus`, `Cull`, `mmInterface::PlayerFillCrashRecords` | verified | tick (frame 1) at x 179, cross (frame 2) at 225, rows from y 98 every 15 px with 153, 183, 240, 267, 321 and 351 for lessons 3, 4, 7, 8, 11 and 12; passed 1, driven 2, never 0. |

## Results (`PagesResults.cpp`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `ResultsPage::ResultsPage` (buttons) | `PUResults::PUResults`, `PUResults::Init640`, `PUMenuBase::PUMenuBase` | fixed | buttons were 30 px apart; MM2's are the popup button height, 0.1 of the screen (48 px), from y 75 at x 440, 135 wide, Exit 200 wide. Strings 492/653, 493/654, 497/496, 498 verified. |
| `drawAbove` | `PUResults::AddTitle`, `AddName`, `AddLoser`, `SetMessage` | verified | title lines at 40,396, columns at 3 and 24 eighteenths of "ABCEFGHIJKLMNOPQR", place "%d", `GetLocTime`, "DNF" (499), message at 290,396 332 wide. The lesson line is inferred. |
| `hasNextRace` | `mmSingleRace::NextRaceAvailable`, `mmSingleStunt::NextRaceAvailable`, `mmSingleBlitz`/`mmSingleCircuit` (Next disabled on the last race) | verified | checkpoint: not the last race and the next one open (with a driver loaded); blitz and circuit: not the last race; lessons: not 2, 6 or from 10. |
| `nextRace` | `mmSingleRace::NextRace`, `mmSingleStunt::NextRace` | verified | the next race with its defaults; an unpassed next lesson in the school car. |

## Options (`PagesOptions.cpp`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| `SettingsPage` | `OptionsBase::OptionsBase`, `mmInterface::Update` (ids 500, 501, 502; dialog 0x1f) | verified | DEFAULTS asks, CANCEL/Escape restore, DONE saves. |
| settings stored in openmm2.ini | `mmPlayerConfig` per driver (`<driver>.cfg`) | deviation | global settings so they apply before a driver is chosen (documented). |
| `OptionsPage` | `OptionsMenu::OptionsMenu`, `mmInterface::Update` case 2 | verified | ABOUT, AUDIO, CONTROLS, GRAPHICS, hidden PREV hotspot, ABOUT first; help opt_tabt/taud/tctl/tgfx. |
| `GraphicsPage` (widgets, focus) | `GraphicsOptions::GraphicsOptions`, `FocusDescription` | verified | toggles, gfx_port created hidden, RESOLUTION initial focus, far clip 100-1000, lighting 0-3, help list. |
| DISPLAY / RENDERER / RESOLUTION contents | adapter, software/hardware renderer, 16-bit modes | deviation | window mode, Vulkan/OpenGL, window size. |
| lighting slider | `GraphicsOptions::GraphicsOptions` (LIGHTING QUALITY slider 0..3), `SetLightQuality`, `mmGame::SetLevelGraphics` | fixed | ceil/floor; MM2 takes the whole part of value + 1 when raised, of the value when lowered, kept in 0..3 (read from the asm: the comparisons are against the current `gxLightQuality`, the constants 1, 0 and 3). The slider holds the quality itself (0..3, not 0..1); the race sets `cityLevel::sm_LightQuality` to its whole part. Stored as `[Graphics] LightingQuality` 0..3, which the race reads. |
| TEXTURE QUALITY | `GraphicsOptions::GraphicsOptions` (TEXTURE RESOLUTION drop-down, strings 390-393), `cityLevel::Load`, `gfxSetTexReduceSize`, `gfxDefaultPrepareImage` | verified | the drop-down sets `gfxTextureQuality` (`mmStatePack` +0x4C), stored as `[Graphics] TextureQuality` 0..3 (default 2). Its only use: `cityLevel::Load` sets the texture size limit to 32 << quality (32, 64, 128, 256 px) while the city loads and puts back the previous limit (none) after `LoadPathSet`; every texture loaded meanwhile that is wider or taller than the limit drops mip levels, or is halved, until it fits. Textures loaded outside the city load (cars, HUD) have no limit. For rendering to apply. |
| `GraphicsPage::resetDefaults` | `GraphicsOptions::ResetDefaultAction` (`AutoDetect`) | deviation | always the top AutoDetect tier. |
| `addAdvanced`, `resolutions`, `setResolution`, `GraphicsPage::done` | — | openmm2 | VSync, MSAA, render scale, UI scale, field of view, display modes. |
| `AudioPage` (toggles, sliders, focus) | `AudioOptions::AudioOptions`, `SetSFXVolume`, `SetMusicVolume`, `SetBalance`, `ToggleMusic`, `ToggleAmbient` | verified | DEVICE initial focus; volumes 0..1, balance -1..1; music and city sounds exclusive; toggles greyed without a device. |
| STEREO FX | `AudioOptions::AudioOptions`, `SetStereoFX` | fixed | only Mono/Stereo; MM2 adds Surround (329) on a 16-bit device and keeps three states. |
| SOUND QUALITY | `AudioOptions::SetQuality`, `InitAudioManager` | fixed | Low switched to the 11 kHz sounds; MM2 changes only the channel count, and always plays the 22 kHz files. |
| `AudioPage::resetDefaults` | `AudioOptions::ResetDefaultAction`, `mmPlayerConfig::DefaultAudio` | verified | volumes 1, balance 0, effects and commentary on, music off, city on, stereo, high. |
| MM2's log-200 volume curve | `AudManager::AssignWaveVolume` | open | audio area. |
| `ControlPage` (widgets, ranges) | `ControlSetup::CreateDeviceOptions` | verified | sensitivity 0.5-2, dead zone 0-0.33, force feedback 0-2, CUSTOMIZE id 0x3e9; help list ctl_trev ... ctl_dd. |
| `ControlPage::update` | `ControlSetup::ActivateDeviceOptions`, `InitCustomControls`, `SetFFPermissions` | fixed | mouse had the dead zone, the game pad no sensitivity, FORCE FEEDBACK needed a stick type; MM2: sensitivity for all but the keyboard, dead zone/calibrate for joystick and wheel, FORCE FEEDBACK whenever a force-feedback device is present, intensities while it is on. |
| `[Controls]` keys | `mmPlayerConfig` controls, `mmInput` | verified | the page writes what `controls::Options` and `controls::Bindings` read: `Controller` 0-4, `Sensitivity` 0.5..2, `DeadZone` 0..0.33, `AutoReverse`, `UsePovHat`, `ForceFeedback`, `Bind.*`; `FFCollision` and `FFRoadForce` are stored only (no force feedback). |
| `ControlDefaults`, `ControlPage::resetDefaults` | `mmPlayerConfig::DefaultControls`, `ControlSetup::ResetDefaultAction` | verified | dead zone 0.1, intensities 1, force feedback off, automatic transmission; `mmInput::AutoSetup` picks the device (OpenMM2: keyboard). |
| CALIBRATE | `ControlSetup::LaunchJoyCpl` | deviation | left to the operating system. |
| `kActions`, `binding` | `mmInput::SetDefaultConfig` (keyboard) | fixed | the page kept its own copy of the action table; it now uses `app::controls` (the race's table) and `controls::boundKey`, so both read `[Controls] Bind.<string id>` the same way. Verified: | every slot's default key matches: Tab, Q, E, F, H, (steering), Left, Right, Up, Down, Space, C, V, Enter, keypad 4/6/2/8, W, D, T, A, Z, R, S, X, 2, 3, 4, 5, Backspace, (camera pan), I, Y. |
| `BindingList`, `CustomizePage` | `ControlCustom`, `UICWArray`, `mmInput::BuildCaptureIO` | deviation | 15 rows verified (`ControlCustom::ControlCustom`); only keyboard bindings are offered (MM2 also captured mouse, joystick axes with a ±0.125 threshold in `mmJaxis::Capture`, pads and wheels). |
| `AboutPage` | `AboutMenu::AboutMenu`, `PreSetup`, `Update`, `Cull` | fixed | scrolled at an inferred 30 px/s; MM2 holds 1.5 s and then scrolls 50 px/s in whole pixels, wrapping. The product ID shows MM2's default "UNKNOWN" (325): no registry read. |

## Multiplayer menus (`PagesMulti.cpp`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| sessions, address, password, connecting, lobby chat, eject | `NetSelectMenu`, `Dialog_TCPIP`, `Dialog_Password`, `NetArena`, `Dialog_Eject` over DirectPlay | openmm2 | OpenMM2's UDP/ENet lobby with LAN discovery. |
| `LobbyPage` teams | `NetArena::NetArena`, `SetTeamWidgets`, `mmMultiCR::InitMyPlayer`, `mmInterface::ChangePlayerData`, `RequestProverb` | fixed | Free-For-All kept whatever team was last chosen; MM2 shows no team buttons there and takes the team from the car: 0 with the police flag (0x08), 1 otherwise (`game::freeForAllTeam`, applied by the lobby whenever the car or the mode changes; NetGame only gains that helper). Verified: Cops vs. Robbers (lobb_cop / lobb_rob) and Robber Teams (lobb_blu / lobb_red) keep the buttons, which set the team; Cops vs. Robbers then gives team 0 the vpcop and team 1 the vpmustang99 (`NetGame::raceConfig`). |
| `HostSettingsPage` (widgets, order) | `HostRaceMenu::HostRaceMenu`, `InitCRWidgets`, `RaceMenuBase::Init` (multiplayer) | fixed | positions were matched on host_bk, there were no roller arrows, laps was a drop-down, CANCEL discarded the changes and the weather offered snow. Now: DONE first (id 1000), the five race types, race name with arrows, a LAPS roller, the Cops & Robbers type and limit lamps, LIMIT VALUE and GOLD MASS with arrows, locale, time (629-632) and weather (625-628, no snow: `IncWeather` stops at 3) with clamping arrows and the pedestrian slider, positioned by tune/widget.csv (menu 11); DONE and Escape both apply (`mmInterface::Update` host menu), there is no CANCEL. The laps, gold mass, time and slider positions are not in the table and are inferred. |
| limit value and gold mass steppers | `HostRaceMenu::LimitInc`/`LimitDec`, `MassInc`/`MassDec`, `SetLimit`, `GetLimit` | fixed | one index for both limits starting at 10 min / 250 pts; MM2 keeps a time index and a points index (0..3, both 0 at first) and the limit kind picks one; the mass clamps 0..2. |
| Cops & Robbers limits and gold mass | `HostRaceMenu::InitCRWidgets` (strings 506-517) | fixed | used OpenMM2's own "min"/"pts" texts and "1000"; now MM2's strings ("5 minutes", "1,000 pts", "Weightless"...). Values 5/10/20/30, 100/250/500/1000 and the three masses verified. |

## Settings (`src/app/Settings.*`)

| OpenMM2 | MM2 | Verdict | Notes |
| --- | --- | --- | --- |
| audio defaults | `mmStatePack::SetDefaults` (0xc73), `mmPlayerConfig::DefaultAudio` | verified | effects, commentary, city sounds on, music off, stereo, high quality, volumes 1, balance 0. |
| `stereoFx` | `AudioOptions::SetStereoFX` | fixed | was a Mono/Stereo bool. |
| `soundQuality`, `audioHighQuality` | `AudioOptions::SetQuality`, `InitAudioManager` | fixed | quality no longer selects 11 kHz files. |
| `Settings::load`, `save` (audio keys) | `mmPlayerConfig::GetAudio`/`SetAudio` | verified | ranges clamped like `AudioOptions::Set*Volume`/`SetBalance`. |
| game source, network, metric units, install defaults | — | openmm2 | |

## Missing

| MM2 | What it does | Status |
| --- | --- | --- |
| `VehicleSelectBase` 3D showroom | turns the selected car in a 3D viewport | open: needs the vehicle renderer in the frontend. |
| `Dialog_Replay`, `Dialog_ReplayEdit`, REPLAY button | instant replays | open: MM2's own REPLAY button is switched off (`MainMenu::EnableReplay` never called). |
| `mmPlayerConfig` per driver | options saved per driver | deviation (global settings). |
| `AudManager::SetNumChannels` | SOUND QUALITY's channel limit | open: OpenMM2's mixer has no settable voice limit (audio). |
| `mmInput` mouse/joystick/pad/wheel bindings, `mmJaxis::Capture` | binding non-keyboard controls, axis capture at ±0.125 | open: OpenMM2 offers keyboard bindings only (the race reads them, and the dead zone, through `app::controls`). |
| `PUOptions`, `PUAudioOptions`, `PUControl`, `PUGraphics` (`mmPopup` pages 5-8) | the in-race OPTIONS pages | open: not ported; the in-race popup shows Options disabled. MM2: OPTIONS (no title) has Previous Menu and Audio / Control / Graphics Options (475-477) at 0.2 / 0.4 / 0.6 of the card; AUDIO OPTIONS (title 442, OK/Cancel) has the Sound FX and Music/City volume and Balance sliders (443-445); CONTROL OPTIONS (448, card 0.05, 0.1, 0.9 x 0.8) has Steering Sensitivity, Collision Intensity, Controller Dead Zone (0..0.33) and Road Force Intensity sliders (449, 451, 450, 452) and the CONTROL drop-down (453); GRAPHICS OPTIONS (460, same card) has Object Detail (648), Visibility (461, the far clip, `FixClip`), Lighting Quality (644), Cloud Shadows (659) and the Vehicle Reflections and Textured Sky toggles (647, 645). Porting them needs popup sliders, drop-downs and toggles (text label + `mmSlider` / `mmDropDown`) and the race applying graphics, sound and control changes mid-race. |
| `UIBMButton` weather/time sounds (Uisunny, Uicloudy, Uirain, Uisnow, Uimorning, Uinoon, Uisunset, Uinight; `AllocateSounds` slots 1-8) | sounds of the time and weather buttons of `Dialog_RaceEnvironment` (renv_imorn ... renv_irain use slots 5-8 and 1, 2, 4, 3) | not needed: build 3393 never opens that dialog (no `OpenDialog` with its id 22), so nothing in the menus plays these sounds. |
| `RaceMenuBase::CheatCallback`, unlock-all cheat | opens every race | not ported. |
| `MenuManager::Help` | WinHelp | deviation (message). |

## For other areas

- session: in-race popup: `PUMain` calls `CreateTitle(0)`, so MM2's main
  popup has no "MAIN MENU" title (likewise OPTIONS); the titled pages are
  AUDIO OPTIONS, CONTROL OPTIONS, GRAPHICS OPTIONS and the key page. In
  Cops & Robbers Free-For-All the lobby now keeps `NetCar::team` at the
  car's team (police flag → 0), which the race can use for
  `CopsAndRobbers::addCar`.
- rendering: `[Graphics] TextureQuality` 0..3 (default 2) is MM2's texture
  size limit 32 << quality for the city's textures (see TEXTURE QUALITY
  above). `[Graphics] LightingQuality` is stored 0..3 directly (docs/rendering.md
  still describes the option as 0-1).
- session: lessons get MM2's opponent density 8 (`CrashCourse::SetEnvironment`)
  while OpenMM2 sets 0 opponents; whether a lesson uses it is the race
  loop's call.
- camera-props (merged): names kept as typed, `Profile::hasLastRace`; the
  main menu's LAST RACE and LAST VEHICLE now show "---" while
  `!hasLastRace()`.
  `HallEntry::passed` should not be drawn: the race records load a
  "passed" picture for every row (`mmCompRaceRecord::Init`) but
  `mmCompRaceRecord::Update` never declares it, so `Cull` never draws it.
- audio: `SoundBank`'s comment says the quality option chose 11 kHz files;
  MM2 always uses aud22/.22k (`InitAudioManager`). STEREO FX now has three
  states (`[Audio] StereoFx`).
