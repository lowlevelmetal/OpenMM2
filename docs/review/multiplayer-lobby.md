# Multiplayer review: lobby and session flow

Reviewed on 2026-10-08 from `integration` at 2734bea (line numbers below are
at that commit), then merged with the network input review (84fda68).

The maintainer's reports for this area: "Whenever I try to start a
multiplayer match, loading screen completes then boots everyone back to the
multiplayer lobby", which "happens sometimes. Usually it breaks after I enter
a lobby and try to change my car or options etc."

## Root cause of the lobby bounce

It is the **second and every later race of a session**, whatever was changed
in the lobby (finding 1). When a race ended on the host (a finish, the
results, Cops and Robbers' limit, or Quit to Lobby), `RaceScreen::leaveRace`
called `NetGame::returnToLobby` and switched to the menus. The session's
`ReturnedToLobby` event reached `NetGame::handleEvents` on the menus' next
update and set `m_returnPending`, which only `RaceScreen` ever took. The next
race's first frame after loading took it as its own (`RaceScreen.cpp:238`),
left, and the host, still in that race's countdown, sent everyone back to the
lobby, which set the flag again: from then on every race of the session
bounced until the host left the session. The first race of a session always
worked, which is why the earlier loopback tests (one race per process) never
saw it, and why it looked tied to "changing the car or options": that is
what players do between races.

Reproduced with a host and a client on loopback (UDP 2310): race 1 in cruise,
the host quits to the lobby (popup script), both change cars, the host opens
and closes HOST SETTINGS, the client gets ready, GO. Before the fix the host's
log had `race: loaded sf in 0.72 s` followed 20 ms later by `the network race
is over (back to the lobby)` and `the host leaves the race: everyone back to
the lobby`, and the client followed. After it, four races in a row (cruise,
checkpoint, Cops and Robbers, circuit) each ran until the host quit them, with
the client on the OPTIONS page or in the garage when one started.

The other leads were checked and are not the cause:

* A mode change in HOST SETTINGS cannot leave a race index that does not fit:
  `HostSettingsPage::selectMode` and the city change go to the first open
  race (`clampRace`), cruise and Cops and Robbers to -1. A race mode with an
  index the city lacks would load with no race rules (`Session::create`
  fails) and never end; it would not bounce.
* Host and joiners build the race from the same `SessionSettings` with the
  same `fromSessionSettings`; nothing is carried differently (the city is
  cleared on a joiner only when it is not a base name, which the host's menus
  cannot produce).
* A paint job out of range is taken modulo (`GpuModel::materials`); a car
  the machine lacks is now the default vehicle (`netVehicle`, network input
  review). Neither ends a race.
* A race that starts while a player is on the OPTIONS page or in the garage
  works (both tried): the frontend switches to the race from any page.
* A load failure did not bounce anyone; it stranded the host instead
  (finding 7).

## What I read

* `src/net/Session.{h,cpp}`, `src/net/Protocol.h` (lobby messages),
  `src/net/Transport.h` (timeouts), completely.
* `src/game/net/NetGame.{h,cpp}`, completely.
* `src/app/frontend/PagesMulti.cpp`, `FrontendScreen.cpp`, `Frontend.h`,
  completely; `PagesRace.cpp` (the garage's lobby mode, the race menu's
  setup), `PagesOptions.cpp` (when options apply), `PagesMain.cpp`.
* `src/app/RaceScreen.cpp`: construction, `update`, the load steps
  (`loadCityPart`, `loadVehicle`, `createSession`, `loadFinish`),
  `updateSession`, `updateNetRace`, the chat and player-list code, Cops and
  Robbers setup and ending, `leaveRace`, `quitToMenu`, the popup and its
  quit page, remote cars; `game::session::Session`'s multiplayer race rules
  (`updateRules`, `updateRace`, `updateNetRace`, `endRace`, `result`).
* MM2Recomp: `mmInterface::MultiStartGame`, `GetSessionData`,
  `ChangePlayerData`, `MultiAllReady`, the Cops vs. Robbers car assignment
  (the `cnr_team` test in the function the map calls `RequestProverb`),
  `mmGame::Init` (the transmission, asm), `mmGameMulti::PlayerFinishedLoading`,
  `SendRaceReady`, `SendGameSet`, `GetCarCRC`, `mmMultiRace::UpdateGame`,
  `mmNetObject::Init`.

## How it was tested

* `tests/game/test_netgame_lobby.cpp`: a host and a client `NetGame` in one
  process through races, returns, car and settings changes.
* `tests/net/test_session_lobby.cpp`: three sessions on loopback through two
  races (fails on the old code at each check).
* Host and client processes on loopback, UDP 2310, driven by
  `OPENMM2_FRONTEND_SCRIPT` and `OPENMM2_POPUP_SCRIPT`. To drive more than one
  race, the frontend script now survives a race (`wait:race`, the commands
  after the one that started the race run when the menus are back) and
  `mp:car:<vehicle>[:<paint>]` does what SELECT VEHICLE, a pick and PREV do
  (docs/multiplayer.md, Automation).
* A temporary test (not kept) measured how long a host keeps a silent
  joiner on loopback: 6.6 s.

## Findings

Severity: crash / gameplay-breaking / visible / minor / code quality.

### Fixed

| # | Severity | Location | Scenario | Confirmed | Fix |
| --- | --- | --- | --- | --- | --- |
| 1 | gameplay-breaking | `NetGame.cpp:301`, `:559`; `RaceScreen.cpp:238` | The lobby bounce (above): a return to the lobby that reached the menus stayed pending and ended the next race on its first frame; the host then sent everyone back. Also hit a joiner who had quit a race early (alone). | repro (two processes); tests `NetGameLobby.ReturnToLobbyEndsOnlyItsOwnRace`, `ReturnSeenInTheMenusDoesNotEndTheNextRace`, `ReturnAndNewCountdownInOneUpdate` | ed3d80e: races are numbered (`raceNumber`), a return ends the race it arrives in (`backToLobby(number)`) |
| 2 | visible | `NetGame.cpp:296` | Game events still queued from a race (a joiner's finish sent while the host was already going back, every event of a cruise, which reads none) reached the next race: a stale finish showed "X finished in", counted X as finished and armed the 60 s finish timeout. | test `StaleGameEventsAreDroppedAtTheNextCountdown` (fails without the fix) | ed3d80e: dropped when a countdown starts |
| 3 | gameplay-breaking | `FrontendScreen.cpp:917` | Back from a network race the menus took the race's whole configuration as the driver's: multiplayer on, no traffic, police or opponents, the host's difficulty. Leaving the session, the next single-player cruise had 0 police cars instead of 19, the multiplayer rules and starts, and recorded nothing. | repro (log) | b11efc7: the menus keep the driver's state from the profile, which `saveNetEvent` already gave the network event and car (MM2's `MultiStartGame` -> `BeDone`) |
| 4 | minor | `PagesMulti.cpp:165` | The sessions page rebuilt under the lobby after a race restarted the LAN browser, which then queried every second through the following races. | by reading | b11efc7 |
| 5 | visible (parity) | `NetGame.cpp:415`; `FrontendScreen.cpp:328`, `:971` | Every network race was driven with the automatic gearbox (`RaceConfig::automatic`'s default), and the menus then saved that as the driver's choice. MM2's session data has no transmission (`GetSessionData`) and `mmGame::Init` sets `vehTransmission::Automatic` from the player's own state. | test `RaceKeepsTheDriversTransmission` | 97f2fb4, a69c5d7: `NetCar::automatic` (never sent), from the garage and the control options |
| 6 | gameplay-breaking (parity) | `NetGame.cpp:599`; `RaceScreen.cpp:2092`; `PagesMulti.cpp:782` | Cops vs. Robbers put each machine's own player in the team's car but drew and counted the others with their lobby car. A robber who had picked a police car was a police car on every other machine, and Cops and Robbers took him for a cop there (`crTeam`), so the gold rules disagreed between machines. MM2's lobby sets the player's car to the team's and sends it (`cnr_team`, `ChangePlayerData`). | test `CopsVsRobbersCarsAreTheSameOnEveryMachine`; run (robber with vpcop drove vpmustang99) | 6f77448: `game::raceCar`, `NetGame::playerCar`, used by `remoteCars`, the race's teams and the roster |
| 7 | gameplay-breaking | `RaceScreen.cpp:741`, `:893` | A city that failed to load sent the race screen straight to the menus. On the host the session stayed in the race: GO DRIVE could never start another race (`startRace` needs the lobby) and the joiners waited in a race that would not end. A missing player car left a race nobody could drive. | repro (`city:nowhere`): before, the second GO did nothing; after, both back in the lobby and the next GO starts | def868b, 419589d: through `leaveRace` (the host takes everyone back), with a notice in the lobby ("Cannot load the city ...", also on a joiner, as the input review asked) |
| 8 | gameplay-breaking (plausible on slow machines) | `RaceScreen.cpp:177` | Nothing serviced the session while a race loaded. A host keeps a silent joiner 6.6 s (measured), so a machine loading that much more slowly than the others was dropped. | measured timeout; slow load not reproduced | def868b: the session is serviced between the load steps |
| 9 | visible | `NetGame.cpp:314`, `:659`; `PagesMulti.cpp:602`; `RaceScreen.cpp:1980-1992` | The lobby and the race remembered chat lines as indices into the last 64 lines; once the list was full they stopped moving and no new line was shown again in that session. The race also began at index 0 and posted every line typed in the lobby on the HUD at the start (seen), with a network alert for each of the others'. | test `ChatLinesAreFoundBySerialAfterTheListIsFull`; screenshot | 5ac76a6: line serials |
| 10 | visible | `Session.cpp:336`, `:534`, `:733`, `:900` | A client kept sending its last race state through the lobby at 20 Hz; the host stored and relayed it. The next race began with that car drawn, and collided with as a kinematic body, where it had stopped in the last race until new states arrived. | test `SessionLobby.NoVehicleStatesBetweenRaces` (fails at each check on the old code) | d49c2e9: cars are replicated during a race only; the countdown and the return clear every buffer |
| 11 | visible | `RaceScreen.cpp:238-239` | The host ends a multiplayer race or circuit once every player is counted; the last finisher was still in its 3 s post-race wait when the return arrived and left without a results page. MM2's 0x211 takes every machine to its results (`mmMultiRace::UpdateGame` state 5). The same race exists between a Cops and Robbers limit announced on a joiner and the host's return. | by reading (needs a finished network race) | 9d4b39e: a race this machine finished or lost leaves with its results |
| 12 | minor | `PagesMulti.cpp:1228` | HOST SETTINGS gave the menus the host's mode and city but not the race or its setup. Choosing a circuit there and leaving the session, the race menu's GO started the circuit with 0 opponents (4 from the menu) and nothing could be recorded. | repro (log, before and after) | 455767f |
| 13 | minor | `PagesMulti.cpp:458` | The host's password is trimmed, a joiner's was not: a stray space was "wrong password". | by reading | 450795f |
| 14 | minor | `NetGame.cpp` `host` / `join` | Notices left from an earlier session could show in the next lobby (now that the lobby shows notices during a session). | by reading | e2ef716 |
| 15 | code quality | docs/multiplayer.md | Said HOST SETTINGS adds Snowing; the page stops at Raining (`RaceMenuBase::IncWeather`). | by reading | e2ef716 |

### Checked and correct

* Session phases Lobby -> Countdown -> InGame -> Lobby on host and joiners;
  `startRace` only from the lobby; GO DRIVE twice starts one race;
  `MultiAllReady` (a host alone may start, as MM2 outside the Zone).
* Kick (lobby and in-race roster), host quit (in the lobby, during a
  countdown, during a load, during a race), a joiner quitting early, a
  joiner leaving mid-countdown: each machine ends where it should with its
  notice ("You have been ejected", "The Host has quit").
* Password refusal and the password dialog (run), version mismatch, full
  session, joining a running race ("The race has already started."),
  rejoining after leaving or being ejected, three players (session test).
* The input review's relay budget delays the relay of a joiner's car and
  ready changes to the others, not the host's own copy: the host's
  `everyoneReady` sees a change at once, and a delayed update always carries
  the latest state, so it cannot add to the bounce.

### Open

| # | Severity | Location | Scenario | Status / what it needs |
| --- | --- | --- | --- | --- |
| O1 | gameplay (parity) | `NetGame::startRace`, `Session::startCountdown` | The race starts a fixed 6 s after GO DRIVE whatever the loading. MM2 waits: every machine sends RaceReady (0x1f6, `SendRaceReady`) 5 s into its state 0 and shows "Waiting for N players" (strings 31-37; every machine, not only the host) while another's has not arrived, then the host sends 0x20f and every machine counts Ready / Set / Go (`mmMultiRace::UpdateGame`). A machine that needs more than 3.5 s to load misses part of the countdown (Ready / Set / Go are its last 2.5 s), and one that needs more than 6 s starts behind the others (here a race loads in 0.6 s). | **fixed** (0096f5b, tests 23ccba0): protocol 3's handshake (`RaceLoad`, `RaceLoaded`, `RaceStart`; `game::NetRaceStart`). Each machine reports its race loaded as MM2 does, the host starts once every player still in the session has, at a shared session time a lead ahead, and every countdown ends together (two processes, a joiner loading 4 s more slowly: Go at 22712 / 22710 for 22709, and in a second race after car changes 49979 / 49976 for 49974). A player who leaves or times out no longer holds the start; after 60 s (OpenMM2's limit, MM2 has none) the host starts without the players still loading, who join the running race on their own countdown. docs/multiplayer.md, "Race start" |
| O2 | minor | `PagesMulti.cpp` `LobbyPage::update` | A joiner's READY is cleared on a settings change only while the lobby page is on top (MM2's `MessageCallback` clears it on any page); the host can also press GO before a joiner's clear arrives. Both machines still load the same race. | plausible; the host could clear the joiners' ready flags itself when it changes the settings |
| O3 | minor | `PagesOptions.cpp` | A ready joiner may open OPTIONS from the lobby; if the race starts there, changes made on an option page are in effect but neither saved nor cancelled. | by reading; leave as is or clear ready on OPTIONS as on SELECT VEHICLE |
| O4 | minor | `Frontend::cityIndex` | A session city the data lacks is shown as San Francisco (with its map) in the lobby; loading it now fails with a notice. | by reading; show the name as given and no map |
| O5 | minor | `SessionsPage::leave`, `NetGame::~NetGame` | Leaving the sessions page destroys the NetGame, which joins the threads removing the port mapping: the menus can stall until the router answers. | plausible; detach or hand the threads to the app |
| O6 | code quality | `Session::clientHandle` Welcome | A Welcome in the game phase (join in progress, off in the game) emits GameStarted without a countdown, so `NetGame` never starts the race for that joiner. | not reachable from the game |
| O7 | minor | the lobby's garage | A car picked in the garage without PREV is not the race's when the race starts there (only possible when the race starts before the joiner's ready-clear reaches the host). | as MM2 (LobbySwitch applies on PREV) |

## For the other reviews

* In-race sync: finding 10 removed one source of a remote car appearing at
  the wrong place and then moving at a race's start. Snapshots and events
  now also arrive while a race loads (finding 8); the buffers are cleared at
  the countdown, so nothing older than the race reaches them.
* Network input: nothing further; see "Checked and correct" for the relay
  budget.

## Changes outside this area's files

* `docs/frontend.md` (the script's `wait:race`), `docs/parity/frontend-ui.md`
  (`scriptOnce` is now `processScript`).
* `RaceScreen.cpp` beyond its entry and exit paths, all small: the chat
  posting (finding 9), the Cops and Robbers teams (finding 6) and
  `crResult`, factored out of the Cops and Robbers ending for finding 11.
