# Frontend (menus)

The original frontend runs at a fixed 640×480. Every screen is a full-screen
JPEG from `MM2TEX.AR` (`jpg/*_bk.jpg`) with its title, field labels and empty
value boxes painted in. Interactive elements are TGA sprite sheets
(`texture/*.tga`) drawn over it, values are text drawn into the painted
boxes, and a 219×69 help picture (`jpg/mn_*.jpg`, `race_*.jpg`, `opt_t*.jpg`,
`vp*_ulck.jpg` / `vp*_lck*.jpg` …) is shown at 40,396 for the focused item.

OpenMM2 draws the same 640×480 layout scaled to the window
(`render::UiScaleMode`: fitted with bars, stretched, or pixel-exact) with
text rasterized at the real output resolution. Code: `src/ui/Widgets.*`
(widgets), `src/ui/MenuLayout.*` (positions), `src/app/frontend/*`
(screens), `src/game/Profile.*` (drivers, records and unlocks).

Everything below is MM2's own behaviour, read from build 3393's code
(MM2Recomp; function names cited), unless it is marked **inferred** or as an
OpenMM2 addition.

## Layout: `tune/widget.csv`

MM2 does not hard-code most positions. When the frontend starts
(`MenuManager::InitCommonStuff` → `WArray::Read`) it loads
`tune/widget.csv`; every `UIMenu::Add*` call then looks up its widget by the
menu's id and the widget's creation index (`WArray::RetrieveWidgetData`) and
replaces each non-zero part of the rectangle the code asked for with the
table's non-zero value. The creation order is also the keyboard focus order.
`ui::MenuLayout` parses the table the way the game does (empty fields
skipped, numbers read like `atoi`, first row wins) and the pages look their
widgets up by MM2's menu id (`frontend::menu_id`) and index, with the code's
values as fallbacks.

Dialogs are centred on the screen at the size of their picture
(`PUMenuBase::PUMenuBase`): 400×76 pictures at 120,202, 300×225 at
170,127.5, 400×330 at 120,75, 540×460 at 50,10; the widgets inside are
relative to the dialog. `tune/menu.csv` is loaded too (`MArray::Read`) but
nothing ever reads it back (`MArray::RetrieveMenuData` has no callers).

## Widgets, focus and sounds

* **Text** is Arial Bold 16 (`MenuManager::GetFont(16)`, string 560),
  yellow, red when focused or active (`MenuManager::GetFGColor`). There is no
  disabled text colour; locked entries in drop-down lists are olive.
* **Sprite buttons** (`UIBMButton`): frames normal, highlight, pressed,
  disabled; the pressed frame shows only while the mouse button is held on
  the button. Its sound plays when the mouse button goes down on it, and it
  acts when the mouse button is released over it, wherever the press began
  (`UIMenu::CheckMouseHits`), or on Enter. 5-frame
  sheets are toggles: off, off+highlight, on, on+highlight, disabled; they
  flip on the press, Enter or Space.
* **Drop-downs** (`UITextDropdown`, `mmDropDown`): 23 px tall whatever the
  layout says, text 5 px from the left, `drop_arrow` at the right (frames
  unfocused / focused / open). Enter or a click opens a list of every option
  (black cells of the box's size, a white outline on the highlight, more
  columns when it would leave the screen: when two columns would not fit
  right of the box, the list starts one box width further left per extra
  column, `mmDropDown::InitString`); Up/Left and Down/Right move by one,
  Home/End jump, Enter picks, Escape closes. Stepping onto, or releasing
  the mouse over, an entry that cannot be picked (a locked race) selects
  the first one that can (`TextDropWidget::SetValue`). Left/Right do
  nothing on a closed box. The up/down arrows beside the boxes are separate 3-frame
  `roller_up`/`roller_down` buttons; whether they wrap is per page.
* **Rollers** (`UITextRoller2`, laps and opponents): the value centred, the
  arrows built in; Left/Right and the arrows step and play "Switch".
* **Sliders** (`UISlider`, `mmSlider`): `slider_larr` at the row's left, a
  track of 2 px segments (`(width - 50) / 2 - 1`), `slider_rarr` after it;
  twenty positions (step = range / 19); a click on the track sets the value,
  there is no dragging; every change plays "Switch". The bar is drawn from
  `slider_actl` at y + 11 in 6-row bands (unfocused, focused, disabled) with
  black transparent, the rest a 1 px row of `slider_inactl` (the band
  reading is **inferred**). Read-only sliders (the garage's statistics) use
  `slider_roactl` and have no arrows.
* **Text fields** (`UITextField`): editing while focused, the first key
  replaces the text, Enter commits, no caret; drawn as " text" in a white
  frame, red on black while focused.
* **Focus** (`UIMenu::Increment/Decrement`, `MenuManager::ScanGlobalKeys`):
  Down/Tab and Up step through the widgets in creation order, skipping
  disabled and read-only ones; past the last page widget focus moves to the
  navigation strip and back to the page; Up before the first goes to the
  other group's first widget (`MenuManager::ToggleFocus`); Escape on the
  strip moves the focus back to the page and backs the page up as well.
  Widgets are focus stops while enabled and writable, which includes a
  shown picture icon (the race map; no highlight). Left/Right never move
  focus; Space only flips
  toggles. Every time a page is entered the focus goes back to its initial
  widget (`UIMenu::Enable`). The mouse focuses what it is over; over empty
  space the highlight and help picture disappear until the next key. There
  is no Shift+Tab, mouse wheel or right-click; gamepad navigation is an
  OpenMM2 addition.
* **Help pictures** follow the focus (each page's `FocusDescription`): the
  focused widget's picture or nothing; the garage keeps its car picture
  there. A page under a dialog shows neither focus nor help.
* **Sounds.** Entering a menu plays its switch sound
  (`MenuManager::PlayMenuSwitchSound`): main menu "Selectionmade", options
  "UIoptions", races and crash course intro "UIraces", garage and host
  "UIvehicles", multiplayer "UImulti". Buttons have their own sounds
  (`UIBMButton::AllocateSounds`): "Selectionmade" for the navigation strip,
  dialogs and option pages, "Moveselector" for CREATE/DELETE/STATS, "Uigo"
  for GO; RACE RECORDS plays "UIrecords". Sliders and rollers play
  "Switch". Focus moves are silent in the frontend; the generic
  "Moveselector"/"Selectionmade" sounds play only in the in-game popups (the
  results page here). Files: `aud/aud22/<name>.22k.wav` or the 11 kHz ones.
* **Navigation strip** (`uiNavBar`, menu 0): OPTIONS 439,1, HELP 540,1,
  MINIMISE 564,1, EXIT 591,1, PREV 290,415. OPTIONS is never disabled: lit
  on the options menu, the page's CANCEL on an option sub-page. EXIT opens
  the quit dialog. MM2's HELP minimises the game and runs WinHelp on
  `MM2HELP.HLP`; OpenMM2 shows a short message instead.

## Screens

| Background | Screen (menu id) | Notes |
|---|---|---|
| `splash.jpg` | loading | MM2's loading bar (`ProgressRect`): a flat `#0D2CBA` bar at 349,448, 10 px tall, 225 px at 100 %; MM2 steps it while the frontend loads and then shows the main menu by itself. OpenMM2 has already loaded and shows one step per frame (pacing **inferred**). `pbar_act`/`pbar_inact` are unused. |
| `main_bk.jpg` | main menu (1) | see below |
| `newp_dlg.jpg` | Create a New Driver (17) | name (72,89, 18 characters), Amateur / Professional (`checkbox.tga`), DONE right, CANCEL left |
| `drec_dlg.jpg` | Driver Record (19) | see below |
| `hoff_dlg.jpg` | Race Records (20) | see below |
| `race_bk.jpg` | Single Race Menu (7) | see "Race setup" |
| `veh_bk.jpg` | Garage (8) | see "Garage" |
| `vp*_show.jpg` | Vehicle Showcase (9) | |
| `opt_bk.jpg` → `gfx_bk`, `aud_bk`, `ctrl_bk`, `cuss_bk`, `about_bk` | options (2, 4, 3, 5, 0x29, 0x22) | see "Options" |
| `ilon_bk.jpg` → `cclon_bk` / `ccsf_bk` | Crash Course (0x28, 0x27) | see "Crash Course" |
| `rshi_bk.jpg`, `crshi_bk.jpg` | results | see below |
| `sess_bk.jpg`, `lobbh_bk`, `lobbj_bk`, `host_bk` | multiplayer (10, 12, 11) | OpenMM2's own sessions over UDP |

**Main menu** (`MainMenu::MainMenu`, `InitDriver`; widget.csv "Main Menu"):
CRASH COURSE 439,242, RACES 439,301, MULTIPLAYER 439,359, QUICK RACE
439,415, the DRIVER drop-down at 177,128 (205 wide, `dropdown_bx`) with
wrapping roller arrows at 384,121 / 384,139, CREATE / DELETE / STATS at
40,156 / 216 / 283, REPLAY at 267,430 (created and switched off at once),
RACE RECORDS at 264,388. CRASH COURSE has the initial focus. The grid
panel shows the driver (`MainMenu::DisplayDriverInfo`,
`mmInterface::PlayerFillStats`): RANKING (Amateur / Professional), LAST
RACE, LAST VEHICLE, CONTROLLER and, for professionals only, SCORE, labels at
x 177 and values at x 209 on alternate lines from y 165. QUICK RACE opens
the garage with the driver's last event (crash course and Cops & Robbers
become cruise); it uses the event currently set up, which the race menu
may have changed since the driver was loaded. Escape asks to quit
(`quit_dlg`: OK at +296,+38 first, CANCEL at +196,+38) without a sound.

**Drivers** (`mmInterface::PlayerCreate`, `PlayerRemove`,
`InitPlayerInfo`): at most 18, listed in creation order; names up to 18
characters, duplicates compared case-sensitively (`dupp_dlg`, after which
the dialog opens again); an empty name does nothing; the 19th driver gets
`plim_dlg`; the only driver cannot be deleted (`lstp_dlg`); `delp_dlg` asks
with YES (+180,+176, first) and NO (+18,+176), after which the oldest driver
is loaded. A new driver gets the net name "noname" and, having no last car
or event, cruise in "vpbug" (`mmInterface::PlayerSetState`); MM2 shows
"---" for its LAST RACE and LAST VEHICLE, which OpenMM2's profiles cannot
tell apart yet. Only an empty name is ignored. The first start creates
"DriverX" without a dialog.

**Driver Record** (`Dialog_DriverRec`, `mmInterface::PlayerFillRecords`,
`mmCompDRecord`): every race of the chosen mode in the chosen city, 12 rows
of 18 px from 81,87 in white: a `lock.tga` icon (open, locked, passed),
RACE (cut to 15 characters + "..." when wider than 132 px), TIME (best,
`M:SS:hh`), VEHICLE (the car of the best time) and, for professionals,
POINTS. Mode and city boxes at 40 / 360, 323 + 25 n; it opens on Blitz in
San Francisco.

**Race Records** (`Dialog_HallOfFame`, `HOFFillRecords`): the race records
shared by all drivers, per table (Amateur Times, Pro Times, Pro Points),
mode and city: the five best entries of every race with RACE, DRIVER,
TIME/SCORE and VEHICLE. The column positions and the scroll arrows are
**inferred**.

**Results** (`PUResults::Init640`). MM2 shows them in the game, over the
paused scene, five seconds after the finish; OpenMM2 shows the same layout
as the first page after the race, and only when the race reached its end
(quitting goes straight back). The finishers' place, name ("Opp.%d" for the
opponents) and time in the big panel from 40,75, the mode (strings 5, 6, 7,
12) and race in the bottom-left panel at 40,396, the reward message at
290,396, and the text buttons Restart Race / Restart Lesson, Next Race /
Next Lesson, Race Menu / Back to School and Exit to Windows from 440,75,
48 px apart (the popup button height, 0.1 of the screen), in the popups'
colours (white, focused yellow-green, disabled grey). The lesson line is
**inferred**. Next is offered
when the next checkpoint race exists and is open, for blitz and circuit
unless it was the last race, and for lessons other than 2, 6 and from 10 on
(`NextRaceAvailable`); it loads the next race's defaults (an unpassed
lesson in the school car). Race Menu / Back to School return to the race
menu or the Crash Course page (`mmInterface::ShowMain`).

### Race setup, garage, showcase and Crash Course

Positions come from `tune/widget.csv` by the widget's creation index in MM2's menus.

**Race menu** (menu 7, `race_bk.jpg`, MM2 `RaceMenu` / `RaceMenuBase`):

* Mode lamps 0–3 at 40,62/90/118/146. A lamp is disabled when the city has
  no races of its mode.
* RACE NAME 4 at 404,66, with up/down arrows 5/6 at 609,60/78. Every race of
  the mode is listed and locked ones are disabled: checkpoint races open in
  groups of three; blitz and circuit races are all open. The arrows clamp
  and stop at a locked race. Cruise hides the box.
* LAPS 7 (1–10) and OPPONENTS 8 (1 up to the race's count) are rollers,
  shown only for circuits and read-only until the race is passed. In other
  modes `race_cov.jpg` covers them at 290,93.
* LOCALE 9–11: San Francisco first (`mmCityList::LoadAll`), clamping arrows.
  A city change goes back to the first race and its defaults.
* TIME 12–14 (strings 629–632) and WEATHER 15–17 (strings 625–628).
* Density sliders 18–20 at 450,316/350/384, 183 wide.
* Map 22 at 22,194, 242×184: `<RaceDir>_map<roam|race<n>|circuit<n>|blitz<n>>.jpg`.
  It is the menu's initial focus and a focus stop while shown (a UIIcon;
  `RaceMenuBase::Init` calls `SetFocusWidget` after adding it), so the
  menu opens with nothing highlighted; Down goes to race_veh.
* race_veh 23 at 439,415.
* Defaults (`RaceMenuBase::SetStateRace`): cruise is always noon, clear,
  pedestrians 0.25, traffic 0.5, cops 1; races take their time, weather,
  densities and cops from `mm<mode>data.csv` (amateur or pro row), circuits
  also laps and opponents, checkpoint races one lap. MM2 keeps the race's
  cop count in the cop density; OpenMM2's densities are 0..1, so the count
  is clamped.
* Outside cruise the time, weather and densities are read-only until the
  race has been passed (`RaceMenuBase::SetRW`): no time/weather arrows,
  `slider_roactl` bars. A circuit's traffic always stays read-only.
* The help picture shows the current mode only while a lamp is focused or
  right after a mode or city change.

**Garage** (menu 8, `veh_bk.jpg`, `Vehicle` / `VehicleSelectBase`):

* Every car, in `mmVehList`'s built-in order (`tune/cars.txt` is never
  read), with wrapping arrows for car and paint. Each car's paint is
  remembered for the session. VEHICLES has the initial focus.
* TRANSMISSION lists "Manual|Automatic".
* Read-only statistic bars at 125,270/293/319/344, 187 wide, each scaled
  from 0.5× the smallest to 1.1× the largest value of all cars.
* VEHICLE SHOWCASE at 347,379; GO at 439,415 plays "Uigo".
* A locked car or paint job shows `locked.tga` at 200,160, and GO then
  shows `lock_dlg`.
* The help box always shows `ShowCarDesc`'s picture: `<car>_lck<n>[_p]`
  for a locked paint job, `<car>_lck[_p]` for a locked car, else
  `<car>_ulck`. It shows `veh_tsc` while VEHICLE SHOWCASE is focused.
* Entering with a locked car moves to the last unlocked car picked, else the
  first unlocked one. Picking a car and entering the garage play
  `<car>_select`.
* MM2 draws the car in 3D (viewport 32,55 608×192, field of view 0.6 rad,
  camera 0.18 rad above at the car's UIDist, turning 1 rad/s). OpenMM2
  shows the car's showcase photo there instead (**not implemented**).

**Showcase** (menu 9): `<car>_show.jpg` with a DONE arrow (`host_dn`) at
439,415; no navigation strip.

**Crash Course** (menus 0x28 and 0x27):

* The intro has no help picture; London (439,359) and San Francisco
  (439,415) open the course on lesson 0.
* The course page has TRAINING 0, LESSON 1 at 404,150 with arrows 2/3,
  BLITZ 4, CHECKPOINT 5, RACE NAME 6 at 404,315 with arrows 7/8, and GO 9.
  The inactive box and its arrows are hidden.
* All 13 lessons are listed. Locked ones are disabled: midterms open after
  their three lessons, the final after everything else.
* Work experience lists every blitz race, or the checkpoint races in groups
  of three.
* `cc_smchk` draws a tick at x 179 for a passed lesson and a cross at x 225
  for a failed one, on the painted rows (`ccStatus`).
* Entering from the intro shows training, lesson 0; coming back from a
  race shows the lesson or work-experience race just driven
  (`CrashCourse::PreSetup`). Every race started here, lessons and work
  experience alike, comes back to this page (`mmInterface::ShowMain`).
* A lesson not yet passed starts at once in the school's car (London
  `vpcab`, San Francisco `vpbullet`; GO shows `veh_go`). Otherwise GO shows
  `race_veh` and opens the garage (`CrashCourse::SetVehicleNext`). Lessons
  take their time, weather and pedestrians from `mmcrashdata.csv` (by the
  driver's skill), no traffic and all cops.
* The help label shows `lon_cc<n>` / `sf_cc<n>` in training only.

### Options

Positions and focus order come from `tune/widget.csv` (menus 2 Options, 3
Audio, 4 Graphics, 5 Control, 41 Customize, 34 About), checked against MM2's
`OptionsMenu`, `OptionsBase`, `GraphicsOptions`, `AudioOptions`,
`ControlSetup`, `ControlCustom` and `AboutMenu`.

* **Options** (`opt_bk`): ABOUT 25,328, AUDIO 439,301, CONTROLS 439,359,
  GRAPHICS 439,415, then the strip's PREV; OPTIONS on the strip is lit. Help
  pictures `opt_tabt/taud/tctl/tgfx` follow the focus.
* **Sub-pages** start with DEFAULTS (347,379; 346,379 on Control/Customize),
  CANCEL (290,415) and DONE (439,415). DEFAULTS asks with `odef_dlg` and
  applies the defaults without saving; CANCEL, Escape and the strip's
  OPTIONS button restore the settings the page was entered with; DONE saves.
  Sub-pages have no PREV. Help pictures are per widget, none otherwise.
* **Graphics** (`gfx_bk`): toggles sky/reflections/pedestrians at x 40,
  y 62/89/116 (SMART RENDERING, `gfx_port`, is created hidden and stays on);
  DISPLAY/RENDERER/RESOLUTION at 404,66/104/139 hold OpenMM2's window mode,
  renderer (Vulkan/OpenGL) and size (MM2: adapter, software/hardware, 16-bit
  modes); VISIBILITY (far clip 100–1000 m) and LIGHTING QUALITY (0–3,
  snapping up/down, `SetLightQuality`) sliders at 450,179/213; TEXTURE
  QUALITY (strings 390–393, " - Recommended" on High), OBJECT DETAIL
  (574–577) and CLOUD SHADOWS (660/661/576). Defaults are the top tier of
  `mmGfxCFG::AutoDetect`: far clip 1000, lighting 3, texture High, object
  detail Very High, shadows High, sky/reflections/pedestrians on. OpenMM2's
  own VSync, anti-aliasing, render scale, UI scale and field-of-view panel
  uses the empty art at the lower left.
* **Audio** (`aud_bk`): SOUND FX / COMMENTARY / MUSIC / CITY SOUNDS toggles
  at y 62/91/125/152 (music and city sounds exclude each other; defaults:
  music off, city on, `mmStatePack::SetDefaults`), DEVICE, STEREO FX
  (326/327), SOUND QUALITY (574–576; OpenMM2 maps Low to the 11 kHz sounds
  and Medium/High to 22 kHz, MM2 chooses 8/16/32 voices), and the SOUND FX
  VOLUME, MUSIC/CITY VOLUME (both default 1) and BALANCE (−1..1, normal
  arrows) sliders at 450,212/246/280. STEREO FX lists Mono, Stereo and
  Surround (MM2 adds Surround on a 16-bit device); SOUND QUALITY sets MM2's
  channel count only (8/16/32): the game always plays the 22 kHz sounds
  (`InitAudioManager`), and OpenMM2 stores the choice. MM2's log-200
  volume curve is not ported.
* **Control** (`ctrl_bk`): AUTO REVERSE (on), POV HAT (off), FORCE FEEDBACK
  (off); CONTROLLER = the five types 580–584 (joystick types greyed without
  one); sliders in MM2's units: sensitivity 0.5–2, dead zone 0–0.33
  (`[Controls] DeadZone`, default 0.1), collision and road force 0–2;
  sensitivity for every type but the keyboard, dead zone and calibration
  for joystick and wheel, POV for a joystick, FORCE FEEDBACK whenever a
  force-feedback device is present (any joystick in OpenMM2, **inferred**),
  the intensities while it is on (`ControlSetup::ActivateDeviceOptions`,
  `InitCustomControls`, `SetFFPermissions`); CUSTOMIZE at 348,315. DEFAULTS also
  selects the keyboard and an automatic transmission. Calibration is left to
  the operating system.
* **Customize** (`cuss_bk`): MM2's 34 action slots in list order (Steering
  and Camera Pan are not listed for the keyboard), two columns at 50,62 in
  20 px rows, 15 visible, with MM2's keyboard defaults
  (`mmInput::SetDefaultConfig`). Enter or a click waits for a key; Escape
  cancels; F1–F10 are refused (`xasn_dlg`); a key already in use asks with
  `ctrl_dlg` and unbinds the other action. Only keyboard bindings are
  offered (MM2 also had mouse, joystick, pad and wheel columns). Bindings
  are stored as `[Controls] Bind.<string id>` and are not yet read by the
  race input.
* **About** (`about_bk`): `credits.jpg` at 39,203 (215×173) from its top,
  held 1.5 s, then scrolling at 50 px/s in whole pixels and wrapping
  without a gap (`AboutMenu::Update`); DONE at 439,415; the product ID label (130,180) shows "UNKNOWN"
  (string 325), as OpenMM2 never reads CD keys; no navigation strip.
* MM2 stores these settings per driver (`mmPlayerConfig`, `<driver>.cfg`);
  OpenMM2 keeps them in `[Graphics]`, `[Audio]` and `[Controls]` of
  `openmm2.ini` so they apply before a driver is chosen. The Graphics and
  Audio pages open on RESOLUTION and DEVICE (`SetFocusWidget` in their
  constructors); the help pictures are MM2's lists (`gfx_tsky … gfx_shd`,
  with `gfx_tdis` for DISPLAY missing from the data; `ctl_trev … ctl_dd`).

## Drivers and unlocks

Driver profiles are OpenMM2's own INI files in `<user data>/players/`
(format documented in `src/game/Profile.h`); MM2's binary save files
(`players.dir`, `player<N>.sav`, per-city `.rec`) are not used, but the
profiles hold the same information. The rules below are MM2's own code
(`src/game/Profile.cpp`, tests in `tests/game/test_profile.cpp`).

**Records** (MM2 `mmPlayerRecord`, `mmPlayerCityRecord::NewRecord`). One
record per city, mode and race, shared by both difficulties: the best time
(checkpoint and blitz: race time; circuit: best lap; crash course: 1) with the
car that set it, the best score and a passed flag that is never cleared.
A race is passed by an amateur in places 1-3 and by a professional only in
1st place (checkpoint, circuit); a blitz is passed by reaching the finish in
time at either difficulty; a lesson by its pass event (the game modes'
`ProgressCheck` / `RegisterFinish`). `MustPlace` and `UnlockGroup` in
`tune/<city>.cinfo` are never read by MM2.

**When a finish is recorded.** Races at the finish line, also when lost;
lessons when passed or failed. Nothing is recorded for quitting, a wreck or a
blitz time-up, in multiplayer, or when the race did not run under its default
conditions for the driver's difficulty (the `drec_dlg.jpg` text): time of
day, weather, cops and traffic for every race, plus laps and opponents for
circuits (`mmSingleRace/Circuit/Blitz::RegisterFinish`). Blitz races from
index 12 on and lessons from 13 on are never recorded.

**Which races are open** (`mmInterface::CitySetupCB`):

* Checkpoint races in groups of three: 0-2 open; 3-5 once 0-2 are all passed;
  6-8 once 3-5 are; 9-11 once 6-8 are
  (`mmPlayerData::ResolveCheckpointProgress`).
* Crash Course: lessons 0-2, 4-6 and 8-10 are open; midterm 1 (3) after
  0-2, midterm 2 (7) after 4-6, midterm 3 (11) after 8-10, the final (12)
  after everything else (`ResolveCrashProgress`).
* Blitz and circuit races are all open; without a driver everything is.

**Rewards** (`race/<city>/<city>_rewards.csv`, `mmRewardList`). At most 32
rows per city; the message ends at the next comma. A row's condition, on that
city's passed races of its mode: `half` = at least half of the races
(rounded down), `all` = all of them, a number = that race or lesson (0-based)
passed. Every row naming a vehicle (`VariantNum` 0) or paint job must be met
before it can be picked; vehicles no row names are always available (12 of
20). After a finish the first row of the mode just driven, in table order,
that is now met and whose target was still locked is announced on the
results screen (`mmRewardList::CheckReward`). `UnlockScore` and
`UnlockFlags` in `tune/*.info` are parsed by MM2 but never used.

**Score** (`mmGame::CalculateRaceScore`, done by the race session): the car's
`ScoringBias` × 50 / 25 / 10 for 1st / 2nd / 3rd (blitz counts as 1st) × the
race's difficulty column. A driver's total is the sum of the best scores of
all blitz, circuit and checkpoint races in both cities
(`mmPlayerData::GetTotalScore`).

**Race records** (the main menu's RACE RECORDS, `mmMiscData`): per
difficulty, city and race the five best times and the five best scores of
any driver, recorded with the same conditions as the driver's records; a
circuit enters every lap's time. OpenMM2 stores them in
`<players dir>/records.ini`.

## Automation

`OPENMM2_FRONTEND_SCRIPT` drives the menus for screenshots, e.g.

```
OPENMM2_FRONTEND_SCRIPT="profile:Test;page:races;mode:blitz;nav:down;wait:3" \
  openmm2 --frames 12 --screenshot races.png
```

Commands: `profile:<name>`, `page:<title|driver|newdriver|stats|records|races|
vehicle|showcase|options|graphics|audio|control|customize|about|crashintro|
crashlondon|crashsf|sessions|quit|message|delete>`,
`mode:<cruise|blitz|circuit|race|crash>` and `city:<map>` (both apply the race
defaults), `vehicle:<name>`, `go` (start the race), `result:<position>`,
`nav:<up|down|left|right|accept|back|tab>`, `wait:<frames>`. Consecutive
`nav:` commands need a `wait:1` between them.

## Not yet done

* 3D vehicle showroom (MM2 rotates the car model; see "Garage").
* Instant replay (MM2's REPLAY button is switched off anyway).
* Options per driver (`mmPlayerConfig`), mouse/joystick bindings, MM2's
  log-200 volume curve; the race does not read `[Graphics]` and `[Controls]`
  yet.
* MM2's own multiplayer menus (`NetSelectMenu`, `HostRaceMenu`,
  `NetArena`); OpenMM2 has its own screens over UDP.
* The in-race loading bar of MM2's level loading screen.
