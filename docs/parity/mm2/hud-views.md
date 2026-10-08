# MM2 -> OpenMM2: hud-views

Audited from MM2Recomp (midtown2.exe build 3393) on 2026-10-08.

Summary: 373 reachable functions in 39 classes; ported 222 (of which newly ported 35, and 16 ported by the frontend audit's showroom), replaced 35, not needed 115, open 1.

Scope: the race HUD (mmHUD and its parts, the overhead map, the opponent
icons, the mode readouts, the dashboard), the view keys (mmViewMgr), the
rear-view mirror and every camera class, from MM2's side. The counts
include the three HUD classes left in the infrastructure list
(mmSlidingGauge, mmAccelCompute, netScoreInfo). Rows the first audit
already compared operation by operation point to its records
([session.md](../session.md) for the HUD, [camera-props.md](../camera-props.md)
for the cameras and the mirror); this pass checked that each cited port
covers the whole MM2 function (every branch, mode and caller) and ported
what was missing. Tests: `tests/game/test_parity_hud_views.cpp`; screenshots
of the split map, the numbered icons, the dashboard and a two-player Cops
and Robbers game were checked by eye.

"ported (new)" marks functions whose port this pass added or completed.
Where a row says "Fixed", the existing port differed and was corrected.

## Frame order and the 3D view

MM2's frame (mmGameManager::Update and ::Cull): the game manager's own
cull runs first and draws the full-screen map (map mode 3) or clears the
letterbox bars; then mmHUD::PostUpdate, the level into gfxPipeline::VP,
the dashboard (while the HUD's dash flag is set and its node is on), the
small or split map (modes 1 and 2), the mirror, and the 2D bitmaps. The
3D view is the whole screen, the wide angle's letterbox (0.18 down, 0.66
tall, mmPlayer::SetWideFOV), the top half (split map) or the small map's
place (full-screen map, mmHudMap::SetMapMode). RaceScreen now follows
this order with `Hud::sceneRect`.

## Clock

The infrastructure record notes that MM2's ElapsedTime runs unclamped and
while paused. Of this subsystem's classes only asViewCS reads it (the
menus' showroom camera, not ported); the race cameras and the HUD use
datTimeManager::Seconds, the clamped frame time, as OpenMM2 does. The
Cops and Robbers gold the HUD draws spins on RaceScreen's clock, which
runs while paused like ElapsedTime. Nothing is changed.

## mmHUD

The race HUD's root node (mmPlayer +0x288): it owns the dashboard
(mmDashView), the instrument cluster (mmExternalView), the message, second
message and chat text nodes, the clock digits, the checkpoint arrow, the
three timers, the mode's readouts (mmWPHUD, mmCircuitHUD or mmCRHUD, made
by `Init` from the game state), a debug position line and the CD player.
OpenMM2's `session::Hud` draws it; `Session` keeps the timers and the
message texts.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmHUD::mmHUD`, `mmHUD::Init` | ported | `game/session/Hud.cpp` `Hud::Hud`, `drawMessage`, `drawChat`, `hud::clockShown` | Nodes at 0.8 / 0.875 (messages, Gill Sans string 60), chat at (0, 0.65) 0.75 x 0.25 with five lines 0.05 apart (string 61), the mode HUD by MMSTATE (WPHUD for checkpoint, Blitz and crash course; circuit HUD; Cops and Robbers HUD; nothing in cruise), the clock off in cruise and Cops and Robbers. The "Position" debug line and the CD player node are not drawn (TogglePositionDisplay(0); mmCDPlayer is another class). The Carhorn1double net alert sound is not loaded (see PlayNetAlert); the Damagewarning sound is loaded but nothing plays it. |
| `mmHUD::~mmHUD`, `mmHUD::'scalar_deleting_destructor'` | not needed |  | Destruction. |
| `mmHUD::ResChange` | replaced | `HudOptions::pixelSize`, `Hud::drawClock` | Reloads digi_<n> / digi_colon, or their _half variants below 640 pixels wide. OpenMM2 lays the HUD out at 640 x 480 and scales it, so the _half art is never used (deviation kept from the first audit). |
| `mmHUD::StartTimers`, `mmHUD::StopTimers`, `mmHUD::ResetTimers`, `mmHUD::GetTime` | ported | `game/session/Session.cpp` `updateClock`, `formatTime` | The race timer (+0xA54) and the HUD timer (+0xA24); session record. |
| `mmHUD::Enable`, `mmHUD::Disable` | ported | `HudOptions::visible` (RaceScreen `syncHudView`) | Disable hides the container (cluster, dash, readouts, single-player messages) and the arrow; Disable(1) (the single-player Escape popup) also stops the timer nodes, which changes nothing visible while the game is paused: the clock is drawn by mmHUD itself and stays. |
| `mmHUD::Toggle` | not needed |  | Only SetViewSetting(3) calls it, and no input event passes 3. |
| `mmHUD::ToggleExternalView` | ported | `game/session/Hud.cpp` `Hud::toggleCluster` | The instrument cluster; it does not come back while the HUD's dashboard flag is set. The black ClearRect under the letterbox is not needed: OpenMM2 clears the whole frame. |
| `mmHUD::SetDash`, `mmHUD::ActivateDash`, `mmHUD::DeactivateDash`, `mmHUD::IsDashActive` | ported | `game/CamPlayer.cpp` `PlayerCameras::setDash`, `HudOptions::dashActive` | Fixed: the cluster and the right-hand-drive map shift followed the dash camera showing; they follow the HUD's dash flag as ActivateDash / DeactivateDash set it. |
| `mmHUD::Reset` | ported | `Session` (messages cleared at the start) | Clears both message strings and the message time. |
| `mmHUD::SetTransparency` | not needed |  | Whether the clock digits are copied colour keyed (off with the letterbox); the same picture on the black bar. OpenMM2 always keys them. |
| `mmHUD::PostUpdate` | ported | `game/session/Hud.cpp` `Hud::drawWorld` | Arrow update, the mode HUD's PostUpdate (mmCRHUD::UpdateGold), the dash view's camera copy. |
| `mmHUD::Update` | ported (new) | `game/session/Hud.cpp` `hud::messageTop`, `drawMessage`, `updateChat`, `clockText` | Fixed: the message nodes move to 0.05 / 0.1 when the 3D view does not start at the top of the screen (wide angle, full-screen map). Message expiry, the chat's 15 s, the clock digits verified (session record). |
| `mmHUD::UpdatePaused` | ported | `game/session/Hud.cpp` `drawOverlay` | While paused only the dash and the debug line update; OpenMM2 keeps drawing the HUD as it stands. |
| `mmHUD::Cull` | ported | `game/session/Hud.cpp` `Hud::drawClock` | Clock digits at the top centre (session record). |
| `mmHUD::ShowSplitTime`, `mmHUD::PostLapTime`, `mmHUD::SetLapTime`, `mmHUD::SetWPCleared`, `mmHUD::SetStandings` | ported | `Session.cpp` (split and lap messages), `Hud::drawReadouts`, `trackLapTimes` | Session record. |
| `mmHUD::SetMessage`, `mmHUD::SetMessage2`, `mmHUD::PostChatMessage` | ported | `Session::setMessage`, `setMessage2`, `Hud::postChat` | Session record. |
| `mmHUD::SetScore`, `mmHUD::AddPlayer`, `mmHUD::RemovePlayer`, `mmHUD::ActivateGold`, `mmHUD::DeactivateGold` | ported (new) | `game/session/Hud.cpp` `drawCrReadouts`, `drawCrGoldIcon`; RaceScreen fills `CrDisplay` | Forwarders to mmCRHUD (below). |
| `mmHUD::PlayNetAlert` | open |  | Plays Carhorn1double (2D, priority 0x17, volume 0.85) with the multiplayer system and game messages (a player leaving, finishing, taking or delivering the gold). Needs the multiplayer message handlers (game-flow) to call an alert sound. |
| `mmHUD::TogglePositionDisplay`, `mmHUD::GetPosHdg` | not needed |  | The debug position line; only called with 0 (off). |

## mmHudMap

The overhead map: `hudmap_<city>` seen from above the car with
`hudmap_square` indicators for the waypoints and car arrows, in four modes
that also move the 3D view. Its mode is a view setting
(`PlayerCameras::mapMode`); `session::Hud::drawMap` draws it.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmHudMap::mmHudMap`, `mmHudMap::FileIO`, `mmHudMap::Init` | ported | `game/session/Hud.cpp` `loadHudMapParams` | Defaults, the one-token datParser field names, the city ocean colours, the right-hand-drive flag (session record). |
| `mmHudMap::~mmHudMap`, `mmHudMap::'scalar_deleting_destructor'`, `mmHudMap::GetClassName` | not needed |  |  |
| `mmHudMap::Reset` | ported | RaceScreen `loadViewSettings` (`PlayerCameras::setMapMode`) | Applies the kept map mode. |
| `mmHudMap::RegisterOpponents`, `mmHudMap::DrawOpponents` | ported (new) | `game/session/Hud.cpp` `Hud::drawMap`, `hud::mapIconColor`; RaceScreen `hudBlips` | Fixed: network players were not on the map. They show with IconType slot + 4; the last two slots index past MM2's ten colours and read the bytes after the table, kept as the colours 0x40F051EC and 0x6D647568. |
| `mmHudMap::RegisterCopsnRobbers`, `mmHudMap::DrawCopsnRobbers` | ported (new) | `game/session/Hud.cpp` `Hud::drawMap` | New: in Cops and Robbers, after the player, GOLD_DOT on the gold, BANK_DOT / HIDEOUT_DOT on the bank and the hideout, or BLUE_DOT / RED_DOT on the Robber Teams bases. |
| `mmHudMap::Activate`, `mmHudMap::Deactivate` | ported | RaceScreen (`mapShown`) | Off in mode 0 and while the Escape popup is up. |
| `mmHudMap::GetNextMapMode`, `mmHudMap::GetCurrentMapMode` | ported | `game/CamPlayer.cpp` `nextMapMode`, `PlayerCameras::mapMode` |  |
| `mmHudMap::SetMapMode` | ported (new) | `game/session/Hud.cpp` `hud::sceneRect`, `hud::mapRect`; RaceScreen `drawScene` | Fixed: the 3D view moves with the map: the top half for the split map (the map the bottom half, aspect 2.5) and the small map's place (Pos x screen, Size x screen, no 10-pixel inset) for the full-screen map. The map rectangles and zoom targets verified. |
| `mmHudMap::Update` | not needed |  | Empty. |
| `mmHudMap::Cull` | ported (new) | `game/session/Hud.cpp` `Hud::drawMap`; RaceScreen `drawScene` | Camera, zoom approach, ocean clear verified. Fixed: the full-screen map is drawn before the level (mmGameManager::Cull) and the 3D view in its inset after a black clear. |
| `mmHudMap::DrawIcon`, `mmHudMap::DrawPlayer`, `mmHudMap::DrawCops`, `mmHudMap::DrawWaypoints`, `mmHudMap::DrawIndicator` | ported | `game/session/Hud.cpp` `Hud::drawMap`, `hud::mapIconMatrix` | Session record. DrawCops also draws every police car while +0x38 is set, which nothing sets. |
| `mmHudMap::ToggleMapRes`, `mmHudMap::ToggleMapOrient` | ported | `game/session/Hud.cpp` `toggleMapZoom`, `toggleMapRotation` | Only with the map on (SetViewSetting 7 / 8). |

## mmViewMgr

The view keys: `SetViewSetting` reads the camera, the dashboard flag, the
map mode and the wide flag, changes them together and always ends with
mmPlayer::SetCamera, SetWideFOV, mmHUD::SetDash and mmHudMap::SetMapMode.
Ported as `PlayerCameras::setViewSetting`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmViewMgr::mmViewMgr`, `mmViewMgr::~mmViewMgr`, `mmViewMgr::'scalar_deleting_destructor'` | not needed |  |  |
| `mmViewMgr::Init` | ported | RaceScreen (`m_mirror.load`, `setEnabled`) | Creates the mirror, on only when the driver had it on (camera-props record). |
| `mmViewMgr::SetViewSetting` | ported (new) | `game/CamPlayer.cpp` `PlayerCameras::setViewSetting`; RaceScreen `updateGameInput` | Fixed: the map modes were the HUD's alone. Now the split map forces the wide view and turns the dashboard off (remembered in the dash view's activated flag), leaving it restores the player's wide choice (the view settings' wide byte) and the dashboard; wide angle refused with the split or full-screen map, the dashboard with the split map; the mirror key runs the common tail too (which forgets a remembered dashboard). The infrastructure record's split piece 0x4320bd is a misdecoded fragment of the switch, not code. |
| `mmViewMgr::Update` | not needed |  | Empty. |

## mmExternalView

The instrument cluster: damage meter (mmSlidingGauge), gear
(mmGearIndicator), tachometer (mmLinearGauge) and speed
(mmSpeedIndicator), 100 pixels above the bottom left corner, and the
mouse steering bar.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmExternalView::mmExternalView`, `mmExternalView::Init`, `mmExternalView::ResChange`, `mmExternalView::Cull` | ported | `game/session/Hud.cpp` `Hud::drawCluster` | Positions and order verified (session record). The _half art below 640 pixels: see mmHUD::ResChange. The mouse steering bar (mouse_bar / mouse_ar with the mouse controller) is drawn by the input-ff change (`PlayerState::mouseSteer`); this audit's own copy was taken back. |
| `mmExternalView::~mmExternalView`, `mmExternalView::'scalar_deleting_destructor'`, `mmExternalView::GetClassNameA`, `mmExternalView::Reset` | not needed |  |  |
| `mmExternalView::Update` | ported | `game/session/Hud.cpp` `drawOverlay` | Declares the cluster for the 2D pass. |

## mmLinearGauge

The tachometer bar (speed_ticks lit up to rpm / MaxRPM).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmLinearGauge::mmLinearGauge`, `mmLinearGauge::Init`, `mmLinearGauge::InitOverlay`, `mmLinearGauge::Draw` | ported | `game/session/Hud.cpp` `hud::linearGaugeLength`, `drawCluster` | Session record; the overlay is the damage label. |
| `mmLinearGauge::~mmLinearGauge` | not needed |  |  |

## mmGearIndicator

The gear digit of the cluster.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmGearIndicator::mmGearIndicator`, `mmGearIndicator::Init`, `mmGearIndicator::Draw` | ported | `game/session/Hud.cpp` `hud::gearArt` | Session record. |

## mmSpeedIndicator

The cluster's speed digits.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmSpeedIndicator::mmSpeedIndicator`, `mmSpeedIndicator::Init`, `mmSpeedIndicator::Draw` | ported | `game/session/Hud.cpp` `hud::speedDigits` | mmPlayer +0xE5C (vehCarSim speed in mph); OpenMM2's km/h option is its own. |

## mmDamage

An empty damage stub.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmDamage::mmDamage`, `mmDamage::~mmDamage`, `mmDamage::Init` | not needed |  | Apply and Reset do nothing; nothing visible. |

## mmDashView

The in-car dashboard: `<car>_dash.pkg` parts placed in the camera's frame
(an asLinearCS with DashPos / RoofPos children), three RadialGauge needles,
the gear indicator and the steering wheel, unlit and without depth test.
`session::Hud::drawDash`.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmDashView::mmDashView`, `mmDashView::FileIO`, `mmDashView::Init`, `mmDashView::LoadPkg`, `mmDashView::LoadPivotInfo` | ported | `game/session/Hud.cpp` `loadDashParams` | Defaults, the asnode fields, the pivots; speed against 160, rpm 8000 with a floor of 800, damage against its maximum (session record). |
| `mmDashView::~mmDashView`, `mmDashView::'scalar_deleting_destructor'`, `mmDashView::GetClassNameA` | not needed |  |  |
| `mmDashView::Activate`, `mmDashView::Deactivate` | ported | `game/CamPlayer.cpp` `setDash` (`m_xcamDash`), `HudOptions::dashboard` | The activated flag (+0x5DE) is what SetViewSetting remembers for XCams and the split map. |
| `mmDashView::Reset`, `mmDashView::BeforeSave`, `mmDashView::AfterLoad` | not needed |  | Node plumbing and the editor's save of the gauge offsets. |
| `mmDashView::Update` | ported | `game/session/Hud.cpp` `drawDash` | The dash follows the camera matrix; ActivateUntilTransitionIsOver is never called. |
| `mmDashView::Cull` | ported (new) | `game/session/Hud.cpp` `drawWorld`, `drawDash`; RaceScreen `setDashFrame` | Parts, order, gear paint job, needles and wheel verified. Fixed: the wheel turns by the recorded steering (mmPlayer +0x2264, RaceScreen `m_steerApplied`); the dash is drawn with a 0.01 m near plane (gfxViewport::Perspective) and the scene's put back after. Not drawn while looking around (mmPlayer +0x1D6C, the camera pan): the HUD is disabled then. |

## RadialGauge

A dashboard needle.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `RadialGauge::RadialGauge`, `RadialGauge::Init`, `RadialGauge::GetArrowAngle`, `RadialGauge::Cull` | ported | `game/session/Hud.cpp` `hud::gaugeAngle`, `drawDash` | Session record. OpenMM2 guards a zero maximum, which MM2 never has. |
| `RadialGauge::~RadialGauge`, `RadialGauge::'scalar_deleting_destructor'` | not needed |  |  |
| `RadialGauge::Update` | not needed |  | Turns its own linear frame; nothing draws from it (Cull(Matrix34*) builds the matrix). |

## mmArrow

The checkpoint arrow 2.5 m up and 6.1 m ahead of the camera.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmArrow::mmArrow`, `mmArrow::Update`, `mmArrow::SetInterest` | ported | `game/session/Hud.cpp` `hud::arrowPose`, `drawArrow` | Session record. |
| `mmArrow::Init`, `mmArrow::~mmArrow`, `mmArrow::'scalar_deleting_destructor'` | not needed |  | Init is empty. |
| `mmArrow::Reset` | ported | `Session::arrowTarget` | Clears the interest until the mode sets it again. |
| `mmArrow::ReColorArrow` | ported | `game/session/Hud.cpp` `drawArrow` (`m_arrowPaint`) | Paint job 1 while the target is behind; a level target keeps the colour. |

## mmIcons

The opponent icons ("Opponent Position"): per OppIconInfo a coloured
triangle card above the car, a digit of `opp_icon.tex` for places 1-9, and
in network games the players' names.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmIcons::mmIcons`, `mmIcons::Init`, `mmIcons::RegisterOpponents` | ported (new) | `game/session/Hud.cpp` `drawIcons`; RaceScreen `hudBlips` | Card corners, size 2, single-player violet, network players by slot (mmGame::mmGame's table; red / blue by team in Cops and Robbers), Blitz cyan cards. |
| `mmIcons::~mmIcons`, `mmIcons::'scalar_deleting_destructor'` | not needed |  |  |
| `mmIcons::Update` | ported | `game/session/Hud.cpp` `drawWorld`, `drawOverlay` |  |
| `mmIcons::Cull` | ported (new) | `game/session/Hud.cpp` `drawIcons`, `drawIconLabels`, `hud::iconDrawOrder`, `hud::iconDigitCell` | New: the place digits (13 to 17 m up at size 2: the 4 m lift is scaled there, unlike the triangle's), drawing by place in one pass per registered icon (places over 7 first; one placed past the passes is skipped), the network players' names in their colours with a 0x101010 outline within 300 m, clipped to the 3D view. Fixed: labels are drawn only in network games, so single-player Blitz no longer shows its checkpoint numbers. |

## mmCRHUD

The Cops and Robbers readouts: team labels and totals, the player's name
and score, the roster of the other players, and the spinning gold while
the player carries it.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmCRHUD::mmCRHUD`, `mmCRHUD::Init`, `mmCRHUD::SetName`, `mmCRHUD::SetScore`, `mmCRHUD::SetBlueScore`, `mmCRHUD::SetRedScore`, `mmCRHUD::AddPlayer`, `mmCRHUD::UnPackColor` | ported (new) | `game/session/Hud.cpp` `drawCrReadouts`; RaceScreen `updateCopsAndRobbers` | New: the name (blue, red on team 1), the player's score, the roster (font string 263 at 16 / 640, scores 0.025 under, rows 0.0625 apart), team labels in font string 262. Fixed: the numbers are yellow Gill Sans MT (20 and 16 pixels), not the team colours. The node's corner is inferred as the top left. |
| `mmCRHUD::~mmCRHUD`, `mmCRHUD::'scalar_deleting_destructor'`, `mmCRHUD::Update`, `mmCRHUD::Reset` | not needed |  | Node plumbing. |
| `mmCRHUD::RemovePlayer` | ported (new) | RaceScreen (roster rebuilt from the players each frame) | Rows close up when a player leaves. MM2 copies each moved row's colour back through GetFGColor (a COLORREF) and UnPackColor with red and blue swapped back and alpha 0; OpenMM2 keeps the colour (inferred to look the same). |
| `mmCRHUD::ActivateRosterGold`, `mmCRHUD::DeactivateRosterGold` | ported (new) | `game/session/Hud.cpp` `drawCrReadouts` | The "$" (string 268, yellow) by the carrier's row. |
| `mmCRHUD::ActivateGold`, `mmCRHUD::DeactivateGold`, `mmCRHUD::UpdateGold`, `mmCRHUD::PostUpdate` | ported (new) | `game/session/Hud.cpp` `drawCrGoldIcon` | New: wpobj_gold 5.5 m up and 13.1 m ahead of the camera, turning 0.05 rad a frame, without depth test, while the player carries the gold; drawn unlit (lit in MM2), like the gold in the world. |

## mmWPHUD

Place and Check readouts of checkpoint races, Blitz and the crash course.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmWPHUD::mmWPHUD`, `mmWPHUD::Init`, `mmWPHUD::Reset`, `mmWPHUD::Update`, `mmWPHUD::SetWPCleared`, `mmWPHUD::SetStandings` | ported | `game/session/Hud.cpp` `drawReadouts` | Session record (strings 254 / 255, 3.5 % and 8.5 %). |
| `mmWPHUD::~mmWPHUD`, `mmWPHUD::'scalar_deleting_destructor'`, `mmWPHUD::Cull`, `mmWPHUD::PostUpdate` | not needed |  | Empty or destruction. |

## mmCircuitHUD

Place, Check, Lap and the lap times of circuits.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmCircuitHUD::mmCircuitHUD`, `mmCircuitHUD::Init`, `mmCircuitHUD::Reset`, `mmCircuitHUD::SetLapTime`, `mmCircuitHUD::SetWPCleared`, `mmCircuitHUD::SetStandings`, `mmCircuitHUD::Update` | ported | `game/session/Hud.cpp` `drawReadouts`, `trackLapTimes` | Session record. |
| `mmCircuitHUD::~mmCircuitHUD`, `mmCircuitHUD::'scalar_deleting_destructor'`, `mmCircuitHUD::PostUpdate` | not needed |  |  |

## mmTimer

The HUD's count-up and count-down timers.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmTimer::mmTimer`, `mmTimer::Init`, `mmTimer::Reset`, `mmTimer::Start`, `mmTimer::Stop`, `mmTimer::Update`, `mmTimer::GetTime` | ported | `Session.cpp` `updateClock`, `timeRemaining` | Session record. |
| `mmTimer::~mmTimer`, `mmTimer::'scalar_deleting_destructor'` | not needed |  |  |

## mmPositions

The race waypoint CSV reader (x, y, z, heading, radius, flags).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmPositions::mmPositions`, `mmPositions::Init`, `mmPositions::Load`, `mmPositions::Register`, `mmPositions::Recall`, `mmPositions::GetCount`, `mmPositions::GetVector4`, `mmPositions::GetFrameRate` | ported | `city/Race.cpp`, `game/session/RaceSetup.cpp` | ai-ambient-city and session records. The infrastructure record's split piece 0x52a2b0 is the rest of Load's line loop. |
| `mmPositions::~mmPositions` | not needed |  |  |

## mmText

GDI fonts and text bitmaps.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmText::CreateLocFont`, `mmText::CreateFont`, `mmText::CreateFitBitmap` | replaced | `ui::FontSpec`, `ui::TextRenderer`; `Hud::font` | Fonts from the string table at their second size; fit bitmaps (the icon labels' 1-pixel outline drawn as nine passes). |
| `mmText::mmText`, `mmText::~mmText`, `mmText::GetDC`, `mmText::ReleaseDC`, `mmText::DeleteFont` | replaced | `ui::TextRenderer` | GDI plumbing. |

## mmNumber

A text node of number-font digits.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmNumber::mmNumber`, `mmNumber::Init`, `mmNumber::SetString`, `mmNumber::Printf`, `mmNumber::Update`, `mmNumber::Cull` | replaced | `ui::TextRenderer` in the readouts |  |
| `mmNumber::~mmNumber`, `mmNumber::'vector_deleting_destructor'` | not needed |  |  |

## mmMirror

The rear-view mirror.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmMirror::mmMirror`, `mmMirror::FileIO`, `mmMirror::Init`, `mmMirror::Reset`, `mmMirror::Cull` | ported | `game/CamMirror.cpp`, RaceScreen `drawMirror` | camera-props and rendering-fx records; drawn whatever the map mode, after the HUD map. |
| `mmMirror::~mmMirror`, `mmMirror::'scalar_deleting_destructor'`, `mmMirror::GetClassName`, `mmMirror::Update` | not needed |  | Update is empty. |

## camBaseCS

The camera base: blend time, FOV, near and far, ForceMatrixDelta.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `camBaseCS::camBaseCS`, `camBaseCS::FileIO`, `camBaseCS::ForceMatrixDelta` | ported | `game/CamParams.cpp`, `CamCar.cpp` | camera-props record. |
| `camBaseCS::AfterLoad`, `camBaseCS::UpdateView` | ported | `CameraView::setPerspective` | Sets the viewport perspective from the camera's FOV and near. |
| `camBaseCS::~camBaseCS`, `camBaseCS::'scalar_deleting_destructor'`, `camBaseCS::GetDirName`, `camBaseCS::MakeActive`, `camBaseCS::SetST`, `camBaseCS::UpdateInput` | not needed |  | Empty or plumbing (tune/camera). |
| `camBaseCS::IsViewCSInTransition` | not needed |  | Only camPostCS::Update calls it (camPostCS is never shown). |

## camAppCS

The approach (smoothing) of the car cameras.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `camAppCS::camAppCS`, `camAppCS::FileIO`, `camAppCS::ApproachIt`, `camAppCS::DApproach`, `camAppCS::UpdateApproach`, `camAppCS::UpdateMaxDist` | ported | `game/CamCar.cpp` | camera-props record. |
| `camAppCS::~camAppCS`, `camAppCS::'scalar_deleting_destructor'` | not needed |  |  |

## camCarCS

A camera bound to a car.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `camCarCS::camCarCS`, `camCarCS::Init`, `camCarCS::FileIO` | ported | `game/CamCar.cpp`, `CamParams.cpp` | FileIO is camAppCS::FileIO. |
| `camCarCS::~camCarCS`, `camCarCS::'scalar_deleting_destructor'` | not needed |  |  |

## camTrackCS

The chase cameras (near, far, _ind).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `camTrackCS::camTrackCS`, `camTrackCS::FileIO`, `camTrackCS::AfterLoad`, `camTrackCS::Reset`, `camTrackCS::Update`, `camTrackCS::UpdateCar`, `camTrackCS::UpdateHill`, `camTrackCS::UpdateTrack`, `camTrackCS::UpdateInput`, `camTrackCS::PreApproach`, `camTrackCS::MinMax`, `camTrackCS::Collide` | ported | `game/CamTrack.cpp` | camera-props record. |
| `camTrackCS::~camTrackCS`, `camTrackCS::'scalar_deleting_destructor'`, `camTrackCS::GetClassName`, `camTrackCS::MakeActive` | not needed |  | MakeActive is empty. |
| `camTrackCS::UpdateSwing` | not needed |  | Empty in build 3393 (SwingToRear, Front and Rear have no callers). |

## camPovCS

The hood and dashboard cameras.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `camPovCS::camPovCS`, `camPovCS::FileIO`, `camPovCS::AfterLoad`, `camPovCS::Reset`, `camPovCS::Update`, `camPovCS::UpdatePOV` | ported | `game/CamPov.cpp` | camera-props record. |
| `camPovCS::~camPovCS`, `camPovCS::'scalar_deleting_destructor'`, `camPovCS::GetClassName`, `camPovCS::MakeActive`, `camPovCS::UpdateInput` | not needed |  | Empty. |

## camPreCS

The pre-race view.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `camPreCS::camPreCS`, `camPreCS::Init`, `camPreCS::MakeActive`, `camPreCS::Update` | ported | `game/CamRace.cpp` | camera-props record. |
| `camPreCS::~camPreCS`, `camPreCS::'scalar_deleting_destructor'`, `camPreCS::GetClassName`, `camPreCS::FileIO`, `camPreCS::Reset` | not needed |  | No file is loaded; Reset only resets children. |

## camPointCS

The post-race and water point camera.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `camPointCS::camPointCS`, `camPointCS::SetPos`, `camPointCS::SetVel`, `camPointCS::SetMaxDist`, `camPointCS::SetMinDist`, `camPointCS::SetAppRate`, `camPointCS::Update` | ported | `game/CamRace.cpp` `PointCamera` | camera-props record. |
| `camPointCS::~camPointCS`, `camPointCS::'scalar_deleting_destructor'`, `camPointCS::GetClassName`, `camPointCS::MakeActive`, `camPointCS::Reset` | not needed |  | Empty. |

## camPolarCS

The orbit cameras (XCams, multiplayer finish).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `camPolarCS::camPolarCS`, `camPolarCS::FileIO`, `camPolarCS::Update` | ported | `game/CamRace.cpp` `PolarCamera` | camera-props record. |
| `camPolarCS::~camPolarCS`, `camPolarCS::'scalar_deleting_destructor'`, `camPolarCS::GetClassName`, `camPolarCS::MakeActive`, `camPolarCS::Reset` | not needed |  | Empty. |

## camPostCS

A post-race orbit; mmPlayer::SetPostRaceCam calls its MakeActive but never shows it.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `camPostCS::camPostCS`, `camPostCS::~camPostCS`, `camPostCS::'scalar_deleting_destructor'`, `camPostCS::GetClassName`, `camPostCS::FileIO`, `camPostCS::Init`, `camPostCS::MakeActive`, `camPostCS::Reset`, `camPostCS::Update` | not needed |  | Never the view's camera (camera-props record). |

## camAICS

A free camera mmPlayer::Init sets up but nothing shows.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `camAICS::camAICS`, `camAICS::~camAICS`, `camAICS::'scalar_deleting_destructor'`, `camAICS::Init`, `camAICS::MakeActive`, `camAICS::Reset`, `camAICS::Update` | not needed |  | Never the view's camera (camera-props record). |

## camViewCS

The player's view: current camera and switching.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `camViewCS::camViewCS`, `camViewCS::Init`, `camViewCS::Instance`, `camViewCS::Reset`, `camViewCS::Update`, `camViewCS::SetCam`, `camViewCS::NewCam` | ported | `game/CamView.cpp` `CameraView` | camera-props record. |
| `camViewCS::~camViewCS`, `camViewCS::'scalar_deleting_destructor'`, `camViewCS::FileIO` | not needed |  | FileIO is empty. |

## camTransitionCS

The blend between two cameras.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `camTransitionCS::camTransitionCS`, `camTransitionCS::Init`, `camTransitionCS::NewTransition`, `camTransitionCS::NextTransition`, `camTransitionCS::ReverseTransition`, `camTransitionCS::StartTransition`, `camTransitionCS::StartNextTransition`, `camTransitionCS::Update` | ported | `game/CamView.cpp` | camera-props record. |
| `camTransitionCS::~camTransitionCS`, `camTransitionCS::'scalar_deleting_destructor'`, `camTransitionCS::Reset` | not needed |  | Reset is empty. |
| `camTransitionCS::ForceMatrixDelta` | not needed |  | Only through camViewCS::ForceMatrixDelta, which has no callers. |

## asCamera

The engine's camera node (viewport, FOV, fog, lighting, an unused fade).
The race's and the menus' views; OpenMM2's renderer and `game::Camera`
replace it.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `asCamera::asCamera`, `asCamera::~asCamera`, `asCamera::'vector_deleting_destructor'`, `asCamera::SetView`, `asCamera::SetViewport`, `asCamera::SetClipArea`, `asCamera::SetFog`, `asCamera::SetLighting`, `asCamera::SetUnderlay`, `asCamera::Update`, `asCamera::DrawBegin`, `asCamera::DrawEnd` | replaced | `game::Camera`, `render::computeProjection`, RaceScreen `drawScene` | The fade (+0x14C) never moves: FadeIn and FadeOut have no callers. The infrastructure record's split pieces 0x4a32b0-0x4a32d0 are destructor thunks. |

## asViewCS

The menus' camera (MenuManager::Init: the vehicle showroom), with polar,
roam, track, POV and stereo modes and a sine wobble on the unclamped
ElapsedTime.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `asViewCS::asViewCS`, `asViewCS::~asViewCS`, `asViewCS::'scalar_deleting_destructor'`, `asViewCS::Reset`, `asViewCS::Update`, `asViewCS::UpdateLookAt`, `asViewCS::UpdatePolar`, `asViewCS::UpdatePOV`, `asViewCS::UpdateRoam`, `asViewCS::UpdateStereo`, `asViewCS::UpdateTrack` | ported | `app/frontend/Showroom` | Ported by the frontend audit with the garage's 3D car (VehicleSelectBase, mmVehicleForm); see docs/parity/mm2/frontend.md |

## asDofCS

An animated node (rotation, translation or scale on a time curve); VehicleSelectBase turns the showroom car with it.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `asDofCS::asDofCS`, `asDofCS::~asDofCS`, `asDofCS::'vector_deleting_destructor'`, `asDofCS::Reset`, `asDofCS::Update` | ported | `app/frontend/Showroom` | Ported by the frontend audit with the garage's 3D car (VehicleSelectBase, mmVehicleForm); see docs/parity/mm2/frontend.md |

## asLinearCS

A node with a local matrix composed onto its parent's.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `asLinearCS::asLinearCS`, `asLinearCS::~asLinearCS`, `asLinearCS::'vector_deleting_destructor'`, `asLinearCS::Update`, `asLinearCS::Cull` | replaced | `game/session/Hud.cpp` `drawDash` (matrix products) | The dash's DashPos / RoofPos children. |

## HUD classes from the infrastructure list

### mmSlidingGauge

The damage meter: a window sliding along damage.tga.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmSlidingGauge::Init`, `mmSlidingGauge::Draw` | ported | `game/session/Hud.cpp` `hud::slidingGaugeOffset`, `drawCluster` | Session record; Init passes speed_ticks' width as the window. |

### mmAccelCompute

Averages a network object's velocity over ten samples (mmNetObject::SetPositionData).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmAccelCompute::Init`, `mmAccelCompute::SetLatest` | replaced | `game/net/NetGame.cpp` snapshots | OpenMM2 places network cars from its own snapshots (session deviation). |

### netScoreInfo

The lobby results record (mmGameMulti / mmMultiCR::SendLobbyResults).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `netScoreInfo::netScoreInfo` | replaced | OpenMM2's lobby | DirectPlay lobby results; replaced by OpenMM2's own lobby. |

## Open

| MM2 | What it does | What porting needs |
| --- | --- | --- |
| `mmHUD::PlayNetAlert` | Carhorn1double (2D, priority 0x17, volume 0.85) with the multiplayer messages: a player leaving or finishing, the gold taken, dropped or delivered | The multiplayer message handlers (game-flow, `mmMulti*::SystemMessage` / `GameMessage`) to raise an alert the race plays |
| `mmGameMulti::UpdateScore` places on the icons | Network players' race places drawn on their icons | Per-player waypoint counts from the network events (game-flow); until then network icons show no number |
| `asViewCS`, `asDofCS`, menus' `asCamera` | The vehicle showroom's turning car and its camera | The showroom of `VehicleSelectBase` (frontend) |

## For other subsystems

- **input-ff**: the race's mouse steering bar (mmExternalView::Cull) is
  drawn by your change (`PlayerState::mouseSteer`); this branch drew it too
  and took its copy back (commit "Leave the race's mouse steering bar to
  the input-ff change"). The dashboard's wheel now reads the same recorded
  steering (`m_steerApplied`, mmPlayer +0x2264), which stays set after the
  race when the car's own input is cleared. Looking around (GameInput's
  camPan, the POV hat included) turns the hood and dashboard cameras
  (`PlayerCameras::update`, camPovCS +0x144) and hides the HUD and the dash
  model, as mmGame::UpdateGameInput and mmDashView::Cull do.
- **game-flow**: PlayNetAlert and the network players' places (above).
  The view keys now go through `PlayerCameras::setViewSetting`, which keeps
  the map mode; code that changes the map mode should call
  `cycleMap` / `toggleFullScreenMap` / `setMapMode` there.
- **frontend**: asViewCS / asDofCS for the showroom (above).
- `mmCDPlayer` (the CD player display, toggled by input events 0x1A-0x1D)
  is not in this subsystem's list and is not drawn.

## Edits outside this subsystem's files

- `src/game/session/Session.{h,cpp}`: `Session::opponentPlace`, the
  second half of mmSingleRace / mmSingleCircuit::UpdateScore, for the icon
  digits.
- `src/app/RaceScreen.cpp` (shared): `syncHudView`, `hudBlips`, the 3D
  view's rectangle and the draw order in `drawScene`, the view keys through
  `PlayerCameras`, the Cops and Robbers roster fields, `setDashFrame`.
