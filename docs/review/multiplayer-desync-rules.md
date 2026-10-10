# Multiplayer desync: the rules

Reviewed and reworked on 2026-10-09 on top of integration (cae6989, the
host simulating every player's car; then 14dc206, the host-run props; then
04112e1, the players' cars' second round). A second round the same day
(below, "Second round: the rough edges", protocol 14, on integration
782f469) made the water and the fall the host's, sent the race's state
unreliably too and made a client's pickup prediction wait for a car about
to reach the gold.

This is open item O3 of docs/review/multiplayer-desync-cars.md: with every
player's car simulated by the host, each player's machine still decided its
own checkpoints, laps, finish and time, and in Cops and Robbers its own
pickups, drops and deliveries, and told the others; a client could claim
any of them. The maintainer's decision ("I think the server needs to be the
authority of everything") makes the host decide them from its simulated
cars and tell the clients. MM2's rules themselves are unchanged; only who
decides is. The design is in docs/multiplayer.md, "Rules".

## MM2's rules in a network race

Read from MM2Recomp (the decompile and the asm of `mmMultiRace`,
`mmMultiCircuit`, `mmMultiBlitz`, `mmGameMulti`, `mmMultiCR`):

* **Checkpoints** (`mmWaypoints::Update`) run on each machine for its own
  car, once a frame, after `UpdateGame`. The count (mmPlayer +0x2254) goes
  in every position packet (`mmGameMulti::SendPosition`) and is all the
  others know of a player's progress.
* **Standings** (`mmGameMulti::UpdateScore`): each machine ranks its own car
  among the others' counts and positions (more waypoints, finished, or level
  and nearer its own target) and numbers the other cars' icons (the same,
  measured to that car's next waypoint, with the machine's own car measured
  to its own target). It stops ranking its own car once its waypoints are
  done.
* **The finish** (`UpdateGame` state 3): the machine's own timer. The host
  shows its own line ("finished in", 148 / 105 / 93), sends
  `SendFinishAck` (0x1f7) and sorts it in (`SortResults`); a client sends
  `SendFinishReq` (0x206) and shows its line (149 / 106 / 95) only when the
  host's 0x1f7 about it comes back with the time. The host plays the
  Messagenote and shows another player's line (152 / 110 / 99) on 0x206, a
  client on that player's 0x1f7 (150 / 107 / 96).
* **The timeout** (`mmMultiRace::SetTimeoutOn`): the first finish arms 60 s
  in a checkpoint race, 120 s in a circuit. Only the host acts when it runs
  out: 0x1fe makes every machine still racing brake, show "Race over" (143 on
  the host, 153 / 111 on a client) and send a did-not-finish (86400); the
  host ends the race once it has counted as many results as the session has
  players (0x211, state 5: the results). A Blitz ends on each machine's own
  clock (a did-not-finish), and the host's own clock running out sends 0x1fe
  (97 on a client, with the net alert).
* **Cops and Robbers** (`mmMultiCR`): `ImpactCallback` (a damaging impact of
  250 or more from another player's car knocks the carrier's gold loose, 2 s
  lockout), `UpdateGame` state 4 (a wreck: 5 s out, the gold dropped),
  `HitWaterHandler` / `DropThruCityHandler` (the gold back to its place),
  `UpdateGold` (free gold within 5 m in the car's room: the host takes it at
  once with another player in the game, a client asks with 0x25e and hides
  the gold until 0x25a), `UpdateBank` / `UpdateHideout` (delivery within
  12 m: repair, 100 points, 600 to the others, and the host's `GetNewSet`,
  0x261) all run on each machine for its own car; the host grants pickups
  while nobody carries the gold and alone checks the limits (`UpdateLimit`,
  `SendLimitReached`).
* **Cruise** (`mmMultiRoam`) has no rules beyond the wreck penalty
  (0x210 / 0x1fd) and the water's reset to the start.

## What was decided where before (integration at cae6989 / 14dc206)

OpenMM2 followed MM2's peers, with every machine also running the finish
timeout itself and counting the results against the players still in the
race. Since protocol 5 the host simulates every player's car, so each
client's car exists twice: the client's prediction, on which its own rules
ran, and the host's simulation, on which nothing did. Where they part (a
collision corrected a sample later, a frame skipping a sample, a car braking
for a finish the host's car has not reached) the client's word and the
host's simulation disagree, and the client's word won. Nothing checked it:
a modified client could send any checkpoint, finish time or gold event and
every machine took it.

## What changed

1. **The tracker** (51c9f89): mmWaypoints' rules move out of `Session` into
   `game::session::WaypointTracker`, unchanged; the session shows what it
   reports. Every single-player and race test passes unchanged.
2. **The referee, the client's corrections, Cops and Robbers on the host,
   the message** (6731eea): `game::session::RaceReferee`, `Session`'s
   net-rules mode, `CopsAndRobbers::updateHost` / `updatePredicted` /
   `applyHost` / `adopt`, `net/RulesState.h`, `game::NetRules`.
3. **Wired in** (9ab7293; protocol 8 on this branch, 10 once merged above
   the players' cars' 9): the race screen feeds the referee every car after
   every sample, sends each player its message and applies the host's word;
   the host's event filter refuses a player's own rule events.
4. **Tests** (8185a2e); the report tool, the autopilot and the trace lines,
   development aids (968a428, 760c8b0, 657a493, baf989a); the autopilot's
   impact callback (619ce6f).
5. **A respawn only where the car has been** (4128b00): O4 of the players'
   cars review, for races; since the merge of 04112e1 it is the race's
   respawn points of `game::ResetRules`.
6. **Found by the runs** (1493217, dd41ea6, 4ac627b): a client still
   counting down when the host times the race out ends it; a race this
   machine already ended stays stopped through the host's word; a client
   does not predict a pickup another car is at too.
7. **Merged with the players' cars' second round** (b80cce3, 04112e1):
   protocol 10; `game::ResetRules` takes the race's respawn points from the
   referee.

## How it was measured

* Host and one client on one machine (UDP port 2410, own configuration and
  data directories, `[Display] VSync=off`, `FrameCap=120`), the client
  through `netprobe relay` (60 ± 20 ms each way, 2 % loss, reordering; and
  150 ± 20 ms, 5 % loss), release builds.
* **Races**: San Francisco's checkpoint race 0 (6 checkpoints and the
  finish) and circuit 0 (2 laps of 9 gates), both cars driven by MM2's racer
  AI along two of the race's opponent lines (`OPENMM2_DEBUG_AUTOPILOT`, added:
  `ai::Opponent` run on the car and the car put back, only its throttle,
  brakes and wheel used as the car's input), so they race each other and
  collide; 230 s and 220 s.
* **Cops and Robbers** (Free-For-All): both cars start 12 m either side of
  the first gold facing each other (`OPENMM2_DEBUG_START_GOLD`, added; the
  places drawn from one seed, `OPENMM2_DEBUG_CR_SEED`) and ram each other
  through it again and again (`OPENMM2_DEBUG_INPUT`, the players' cars
  review's "ram" inputs); 150 s.
* `OPENMM2_NET_TRACE` with the rules lines (added, `game/net/RulesTrace.h`)
  on both machines and `netprobe rulesreport` (added) on the traces: each
  player's checkpoints as the host's referee counted them against what that
  player's HUD showed, the results on each machine against the host's
  measure, and each gold event against the host's simulation (the taker's
  car within 5 m of the gold where the host draws it, 7 m allowed; a knock
  backed by a collision of 250 or more of the carrier's own on the host).
* **Before** is integration (14dc206) with only the trace lines, the
  autopilot, `mp:race` and a shadow referee on the host (the same
  `RaceReferee`, deciding nothing, traced as the host's measure); **after**
  is this branch before the merge of 04112e1 (protocol 8), and **merged**
  the branch with it (protocol 10). The before build's tree is
  integration's plus `patch_before.py` (kept with the scratch files, see
  "Reproducing").
* The before and after times are not comparable with each other: the
  before build's autopilot ignored the AI's reverse gear (it gave the car
  throttle), the after build's brakes then (`AUTO REVERSE`), and both cars
  pull away from the grid more slowly after (1.7 against 6.9 m/s half a
  second after Go). The before build with the after build's pedals
  (`race-before-rev-60ms`) pulls away as slowly and times the checkpoint
  race's cars 0:28.683 and 0:35.308, against after's 0:28.667 and 0:35.300,
  with the client's finish again never crossed on the host. What is
  compared is each run's machines against each other and against the
  host's measure.

## Results

Player 0 is the host, player 1 the client. "Shown, never counted" is a
checkpoint the client's HUD showed that the host's referee did not count;
"lead" is how long before the host counted it the client showed a predicted
checkpoint (median, 1st-99th percentile).

### Checkpoint race (6 checkpoints and the finish)

| Run | Link | Checkpoints counted / shown (client) | Shown, never counted | Taken back | Lead | Host's time (each machine / host's measure) | Client's time (each machine / host's measure) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| before | 60 ms | 6 / 6 | 0 | 0 | 142 ms | 0:25.941 / 0:25.950 | 0:33.516 / never crossed |
| after | 60 ms | 6 / 6 | 0 | 0 | 152 (119-252) ms | 0:28.667 / 0:28.667 | 0:35.300 / 0:35.300 |
| merged | 60 ms | 6 / 6 | 0 | 0 | 138 (130-146) ms | 0:28.333 / 0:28.333 | 0:36.167 / 0:36.167 |
| before | 150 ms | 6 / 6 | 0 | 0 | 233 ms | 0:25.975 / 0:25.983 | 0:33.449 / never crossed |
| after | 150 ms | 6 / 6 | 0 | 0 | 238 (229-238) ms | 0:28.367 / 0:28.367 | 0:36.033 / 0:36.033 |

Before, the client's result was its own timer's and the host's simulation
of its car **never crossed the line**: the client braked at its predicted
finish and told the host, the host ended the race on it (every player
counted), and its simulation of the car stopped 1.8 m (60 ms) and 2.5 m
(150 ms) short of where the client's own car stopped. Every machine showed
the client's word. The host's own time was 8 ms off the host's measure
(its frame timer against its samples). After, every result is the host's
measure, the same on both machines.

### Circuit (2 laps of 9 gates)

| Run | Link | Checkpoints counted / shown (client) | Shown, never counted | Taken back | Lead | Host's time (each machine / measure) | Client's time (each machine / measure) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| before | 60 ms | 18 / 18 | 0 | 0 | 144 ms | 2:22.217 / 2:22.217 | 2:06.908 / 2:06.933 |
| after | 60 ms | 18 / 18 | 0 | 0 | 141 (132-141) ms | 2:24.350 / 2:24.350 | 2:09.933 / 2:09.933 |
| before | 150 ms | 18 / 18 | 0 | 0 | 238 ms | 2:22.483 / 2:22.483 | 2:06.958 / 2:06.983 |
| after | 150 ms | 18 / 18 | 0 | 0 | 221 (213-238) ms | 2:24.367 / 2:24.367 | 2:09.917 / 2:09.917 |
| merged | 150 ms | 18 / 18 | 0 | 0 | 236 (226-253) ms | 2:24.833 / 2:24.833 | 2:09.600 / 2:09.600 |

Before, the client's time was its own timer's, 25 ms off the host's measure
of the same car (its frame timer against the host's samples); after, 0. In
the 150 ms after run the client's car was corrected 73 times and no
checkpoint parted.

### A client that simulates differently

`OPENMM2_DEBUG_NETCARS_NOISE=0.002` on the client nudges its car's momentum
every sample, as a build from another compiler might: about 470 corrections
a run, the client's prediction parting from the host's simulation all the
time (60 ms link).

| Run | Checkpoints counted / shown (client) | Shown, never counted | Host's time / measure | Client's time / measure |
| --- | --- | --- | --- | --- |
| circuit before | 18 / 18 | 0 | 2:22.508 / 2:22.517 | 2:07.066 / 2:07.067 |
| circuit after | 18 / 18 | 0 | 2:24.767 / 2:24.767 | 2:09.867 / 2:09.867 |
| race before | 6 / 6 | 0 | 0:25.966 / 0:25.967 | 0:33.875 / never crossed |
| race after | 6 / 6 | 0 | 0:28.317 / 0:28.317 | 0:35.800 / 0:35.800 |

The checkpoints agree in every run, before and after: the gates are wide
and a correction moves the car by centimetres. What parted were the
finishes, and the place.

### The place

The machine of the player who finished second showed "Place 1/2" from its
finish on, before: its own finish set the place to its count of finishers,
which the other players' finishes never raised (the circuit's host and the
checkpoint race's client, every run). After, the place is the host's and
stops at the finish, as `UpdateScore`'s does (2/2).

### Cops and Robbers (Free-For-All, ram inputs, 150 s)

| Run | Link | Gold events (host / client, the same) | Scores (host / client) | Pickups predicted and undone | Pickups with the car over 7 m from the gold (host's simulation) | Knocks without a hit of 250 (host's simulation) |
| --- | --- | --- | --- | --- | --- | --- |
| before | 60 ms | 7 / 7, yes | 0=75 1=25 / the same | 0 | 0 of 4 | 0 of 3 |
| after | 60 ms | 3 / 3, yes | 0=50 1=0 / the same | 1 | 0 of 2 | 0 of 1 |
| merged | 60 ms | 11 / 11, yes | 0=100 1=50 / the same | 1 | 0 of 6 | 0 of 5 |
| before | 150 ms | 11 / 11, yes | 0=100 1=50 / the same | 0 | 0 of 6 | 0 of 5 |
| after | 150 ms | 3 / 3, yes | 0=50 1=0 / the same | 1 | 0 of 2 | 0 of 1 |

The ram inputs run open loop, so the runs are not the same sequence of
events (the cars end up elsewhere after the first knock); what is compared
is the machines' agreement and the host's simulation backing each event.
With honest players the two machines agreed before as well: MM2's pickup
already went through the host. The undone pickups are where both cars
reached the gold within a few samples of each other: the client showed
"You have the Gold!" and the host gave it to the host's car, which the
client had not yet seen at the gold. Before 4ac627b (no prediction when
another car is within 5 m of the gold as the client has it) the same runs
undid 1 and 2 pickups.

### Players who leave or join late (circuit, 60 ms)

* **Leaving**: the client quits the race after gate 5. The host stops
  waiting for it, finishes alone (2:24.567) and is back in the lobby with
  the results 3 s later.
* **Late**: the client's loading is held back 70 s
  (`OPENMM2_DEBUG_LOAD_DELAY_MS`). The host starts without it after 60 s
  ("starts without Client: still loading 60008 ms"), the client gets its own
  Go 9.8 s after the host's, and its time (2:13.767, from its own release)
  is the same on both machines and the host's measure; 18 checkpoints
  counted and shown.

### A client's own word

`TheHostRefusesEveryPlayersWordOnTheRules` and
`APlayersOwnWordOnTheRulesReachesNobody` (three live `NetGame`s on
loopback): a client's forged `CheckpointReached`, `LapCompleted`,
`RaceFinished`, gold events, old Cops and Robbers messages and a rules
message of its own reach neither the host's game nor the other client;
`TheHostsMessageReachesAClientWhateverIsLostOrForged` drops, repeats,
reorders and forges the host's messages to a client, which shows every
decision once and ends with the host's state. `RulesState`'s fuzz test
decodes random and mutated payloads without a fault. In every run above:
0 messages refused, malformed or stale, 0 decisions missed.

### Bandwidth

The rules message is about 4 a second to each client (at once on a
decision or a new hit, every 250 ms otherwise): 88-118 B/s of payload to
each client in a race, 284-293 B/s in Cops and Robbers (the gold's place,
the bases and the scores), plus ENet's and UDP's headers. With eight
players a race's message is about 85 bytes (340 B/s to each client,
2.4 KB/s from the host). Before, each machine sent a checkpoint and a
finish event per event (a few bytes). The players' cars' states are
6.2 KB/s to each client for comparison.

## Second round: the rough edges (protocol 14)

The maintainer accepts more bandwidth and wants the remaining rough edges
fixed. Four were open after the first round: O2 (the water was still a
client's word), O3 (a predicted pickup undone), O4 (ResetRules' Cops and
Robbers repair) and O1 (a checkpoint's confirmation a round trip late).

### MM2's water and fall

`mmGame::Update` checks the player's car after each frame's update: its
matrix below -50 m calls the game's `DropThruCityHandler`; otherwise, the
car's vehSplash latched (mmPlayer +0x2340 counts the frame's seconds,
+0x2348 is the handled flag), past 5 s it calls `HitWaterHandler`, clears
the timer and sets the flag. `mmGameMulti::DropThruCityHandler` calls the
water's handler. `mmGameMulti::HitWaterHandler`, with the race's
mmWaypoints, keeps the reset position and heading, sets them to the last
waypoint cleared (`GetWaypoint`, `GetHeading`), resets the player
(mmPlayer::Reset) and puts them back, keeping the waypoint count; without
waypoints it is `mmGame::HitWaterHandler` (mmPlayer::Reset at the reset
position); then `SendHitWater`. `mmMultiCR::HitWaterHandler` and
`DropThruCityHandler` drop a carried gold back (`DropGold`,
`FondleCarMass`, `EnableRegen`), reset the player and keep its waypoint
count. Each machine ran this for its own car.

### What was left, and what changed

1. **The water and the fall on the host** (e168a58). Before, a client's
   session ran the timer on its own frames and sent a Reset or RespawnAt
   command, which the host carried out when its own simulation of the car
   had been in the water for 4 of the 5 s or was below -40 m
   (`game::ResetRules`, a second's and 10 m's slack). Now mmGame::Update's
   checks run on the car's samples (`game::NetCarDriver::
   setWaterHandler`, before each sample on the car as the last one left
   it, the timer counting samples): on the host for every car it
   simulates, with the respawn point from its referee
   (`RaceReferee::lastCleared`, `NetRules::respawnIndex`) or the car's
   reset position; on a client for its own car, predicted, with its
   session's last checkpoint (`Session::netRespawnPoint`). Every car's full
   state carries its vehSplash (latched, buoyancy, level) and the
   handler's time, so a client resumes the host's water exactly, and a
   client that disagrees is corrected to the host's car. A client's Reset
   and RespawnAt are refused (only the debug respawn passes, with the
   host's `OPENMM2_DEBUG_RESPAWN_MS`). The session shows only the water's
   message; the race screen presents the reset (`presentOwnWaterReset`).
   Cops and Robbers' water handler follows the host's resets.
2. **ResetRules narrowed** (e168a58): a repair only for a car past its
   maximum damage (the wreck penalty's); a delivery's is the host's own.
3. **The rules state unreliably too** (e168a58, da767c3): in a race the
   host sends each player its state alone (`RulesState` on the State
   channel: the newest 16 waypoints, the samples seen, the place and the
   icons; no decisions, no results) 20 times a second and at once on a
   hit, numbered with the reliable messages, which now go once a second
   when nothing happened (Cops and Robbers' stay at four a second: its
   state goes no other way). Whichever arrives first stands; a reliable
   message older than the newest state still brings its decisions.
4. **A contested pickup waits** (e168a58): a client predicts its pickup
   only when no other car is within the gold's 5 m and 1 m more, or on its
   way there in the next quarter of a second, as the client has it where
   the host will run its sample: the near cars it simulates with its own
   (`NearCarState`) as they are, the others run on from their drawing by
   the playout delay and its car's lead (inferred margins).
5. Measurement aids (0c96102, da64b77): the RC and RW trace lines,
   rulesreport's confirmation times, water resets and a client's own
   pickups. Tests (e168a58, 8dbc9a3, 86db4c6): `NetWater.*`,
   `WaterState.*`, the narrowed ResetRules, the unreliable state (on its
   own, fuzzed, and in the live three-machine test) and the contested
   pickup.

### How it was measured

As the first round (host and one client on one machine, `netprobe relay`
at 60 ± 20 ms 2 % loss and 150 ± 20 ms 5 % loss, release builds, the
autopilot in the races, the ram inputs in Cops and Robbers, now 270 s).
**Before** is integration 782f469 with the measurement aids (0c96102);
**after** is e168a58 and, for Cops and Robbers, da767c3. Added:

* **The water**: a cruise and a Cops and Robbers game with both cars
  started over the bay (room 228, `OPENMM2_DEBUG_START=-1779,3,-1040,0` and
  `-1779,3,-1130,0`): every five seconds in the water the car goes back to
  its reset position, in the water again (150 s, 120 s). Once with a client
  whose simulation differs (`OPENMM2_DEBUG_NETCARS_NOISE=0.002`).
* **Contested gold**: both cars on opposite sides of the first gold,
  facing it, at full throttle, the host's 12 m away and the client's 10 to
  14 m (seven runs each), so that they reach it together.

### Results

**Checkpoints** (the client, player 1; every run: 0 shown and never
counted, 0 taken back, every finish the host's measure on both machines):

| Run | Link | Lead before the host counted (median) | The host's word after shown: median / 99 % / most |
| --- | --- | --- | --- |
| race before | 60 ms | 142 ms | 208 / 217 / 217 ms |
| race after | 60 ms | 117 ms | 208 / 217 / 217 ms |
| circuit before | 60 ms | 130 ms | 200 / 217 / 217 ms |
| circuit after | 60 ms | 142 ms | 217 / 233 / 233 ms |
| race before | 150 ms, 5 % | 238 ms | 458 / 776 / 776 ms |
| race after | 150 ms, 5 % | 241 ms | 395 / 417 / 417 ms |
| circuit before | 150 ms, 5 % | 227 ms | 392 / 767 / 767 ms |
| circuit after | 150 ms, 5 % | 226 ms | 400 / 441 / 441 ms |

A lost reliable message held the confirmation until ENet sent it again
(the 99th percentile twice the median at 5 % loss); the unreliable copy
takes the tail away. The median is a round trip plus the host's margin of
inputs and cannot go lower without taking the client's word.

**The water** (each machine's own car's resets against the host's):

| Run | Link | Before | After |
| --- | --- | --- | --- |
| cruise | 60 ms | 24 / 24 at the same sample, 0 corrections | 24 / 24, 0 corrections |
| cruise | 150 ms, 5 % | 21 / 21, 0 corrections | 24 / 24, 0 corrections |
| Cops and Robbers | 60 ms | 18 / 18 (one more on the client at the end of the run) | 18 / 18 |
| cruise, a client that simulates differently | 60 ms | 13 / 13 | 13 / 13 |

With an honest client the two agree: its word was checked and the
simulations match. What changed is who decides: a client can no longer
put its car back (a second early, or anywhere the checks allowed), and its
timer counts the samples the host counts. One race started with both cars
in the water (`OPENMM2_DEBUG_SPAWN`) diverged the same way before and
after: the host simulates a client's car only from its inputs after its
own loading, and this car sank while held on the grid during the load (a
held car does not float, vehCar::Update), so the client's car fell below
-50 m at its sample 140 and the host's at 436; before, the host refused the
client's reset and the car went back into the water; after, the host's
states take its prediction back the same way. It needs a car spawned in
the water by the development aid.

**Cops and Robbers**:

| Run | Link | Gold events (agree) | Scores | Pickups predicted and undone | The client's own pickups |
| --- | --- | --- | --- | --- | --- |
| ram before | 60 ms | 5 (yes) | the same | 1 | 1 predicted, 1 on the host's word |
| ram after | 60 ms | 3 (yes) | the same | 0 | 1 on the host's word |
| ram before | 150 ms, 5 % | 11 (yes) | the same | 1 | 1 predicted, 1 on the host's word |
| ram after | 150 ms, 5 % | 5 (yes) | the same | 0 | (none) |
| contested before | 60 ms, 7 runs | 7 (yes) | the same | 3 | 1 predicted (202 ms early) |
| contested after | 60 ms, 7 runs | 7 (yes) | the same | 0 | 1 on the host's word (65 ms after it decided) |

In the contested runs the host's car took the gold six times of seven
before and after; before, the client showed "You have the Gold!" and took
it back in three of them. After, it waits whenever the host's car is at
the gold or about to be, and its own pickup shows a trip later (MM2's
client always waited for 0x25a).

**Bandwidth** (per client): in a race the reliable messages fell from
93-101 B/s to 22-27 B/s and the unreliable state is 411-494 B/s (20 a
second, about 23 bytes with two players; about 40 with eight: 800 B/s to
each client, 5.6 KB/s from the host). Cops and Robbers' stays 284-292 B/s.
A car's full state costs two bits more, twelve bytes more while it is in
the water.

## Deviations from MM2

* The host decides every machine's rules (MM2: each machine its own car's,
  the host the finishes' order, the timeout and the end).
* The rules run after every physics sample (MM2: once a frame; the same at
  60 frames a second, and no frame can skip a gate).
* A finish time is the host's measure: its samples from the car's own
  release to the sample it crossed the line in (MM2: each machine's timer
  from its own Go).
* The host decides everyone's did-not-finish at the timeout at once (MM2:
  each machine sent its own after 0x1fe).
* Two cars reaching the gold in the same frame: the first in the host's
  order (MM2: the first request to reach the host; the host's own car first).
* A client shows its own pickup at once (MM2's client asked and waited for
  0x25a), unless another car is at the gold too, and its own checkpoints
  before the host has counted them; the host's word takes back what it did
  not count.
* The place and the icon numbers use the host's view of each car (its
  target is the nearest one at its last hit; the Next / Prev. Checkpoint
  keys stay on that player's machine).
* The water and the fall (protocol 14): the host runs mmGame::Update's
  checks for every car it simulates (MM2: each machine for its own car),
  before each sample rather than after each frame, the water's 5 s counted
  in samples; a client predicts its own.
* A client's pickup prediction waits for the host's word while another car
  is near the gold or heading into it (inferred margins: 1 m beyond the
  gold's 5 m, a quarter of a second).

## Findings

| # | Severity | Location (integration) | Finding | Status |
| --- | --- | --- | --- | --- |
| R1 | minor (security) | `RaceScreen::handleSessionEvents`, `updateNetRace`, `updateCopsAndRobbers` | Each player's machine decided its own checkpoints, laps, finish and time and its gold events, and every other machine took its word: a client could claim them. | fixed: the host decides them from its simulation and refuses a player's own rule events (protocol 10) |
| R2 | visible | the same | A client's finish came from its predicted car, which braked at it: the host's simulation of that car stopped short of the line (1.8-2.5 m in every checkpoint race), and its time was the client's timer's (up to 25 ms off the host's measure in the circuits). | fixed: the host's simulation decides and times every finish |
| R3 | minor | `Session` (net race) | Every machine ran the finish timeout from the first finish it heard of, so the machines ended the race at different moments. | fixed: the host's alone (as MM2's) |
| R4 | minor (parity) | `Session::updateNetRace` | The icon number measured this machine's own car against another car level with it to that car's next waypoint; `UpdateScore` measures both to this machine's own target. | fixed |
| R5 | minor (parity) | `Session::updateRace` | A client's own finish line was the host's (148 / 105 / 93) and showed at once; MM2's client shows its own (149 / 106 / 95) when the host's 0x1f7 arrives. | fixed: shown with the host's time when the host's decision arrives |
| R6 | minor | `RaceScreen::netCommandAllowed` | A client's water respawn could put its car on any checkpoint (players' cars review O4). | fixed for races: only at the start or a checkpoint the host counted for it (`game::ResetRules`' respawn points) |
| R7 | crash (development aid only) | `game::NetAutopilot` (new) | The autopilot's AI took the player's car's impact callback and left it pointing at a destroyed driver. | fixed before measuring (619ce6f) |
| R8 | visible | `Session::playerFinished` | A machine whose player finished second or later showed "Place 1/N" from the finish on: the place was its own count of finishers, which network finishes never raised. | fixed: the host's place, kept from the finish on |
| R9 | minor (security) | `Session::updateHazards`, `RaceScreen` | The water's and the fall's handler ran on the client's frames for its own car and its reset was its command, which the host took with a second's and 10 m's slack: a client could put its car back a second early, or not at all (the host never did it for it). | fixed (protocol 14): the host's own, on the car's samples; a client's reset refused |
| O1 | minor | the host's frame | The host's own car is decided with no latency; a client's a trip later. Its finish time does not depend on it (measured in its own samples), but a client's checkpoints are confirmed a round trip later (they show at once, predicted); at 5 % loss a lost message doubled that. | tightened (protocol 14): the unreliable state takes the tail away (99 %: 767-776 to 417-441 ms at 150 ms); the round trip itself is inherent |
| O2 | minor | Cops and Robbers | Whether a car was in the water (its reset) is still its player's word, checked by `game::ResetRules` (five seconds in the water on the host's simulation) before the host applies it. | fixed (protocol 14): the host runs the water and fall handlers for every car |
| O3 | minor | Cops and Robbers | A predicted pickup is undone when the host gave the gold to a car the client had not yet seen at the gold (the other car's state a trip old): one in each run. | fixed (protocol 14): no prediction while another car is at the gold or about to be, as the near-car simulation has it; 3 of 7 contested runs undone before, 0 after |
| O4 | minor | `game::ResetRules` | Its Cops and Robbers repair exception (any repair allowed) is now only needed for a client's own command; the host repairs a deliverer's car itself. | fixed (protocol 14): a repair only for a wrecked car |
| O5 | minor (development aid) | the players' cars | The host simulates a client's car from its inputs after its own loading; a car that moves while held on the grid (sinking in the water from a debug spawn) is somewhere else on the client by then, and the two put it back at different samples. | open: only with `OPENMM2_DEBUG_SPAWN` in the water; the players' cars' start |
| O6 | minor | Cops and Robbers | Its state goes only reliably (four a second); a lost message delays an undone pickup or a missed decision's stand-in until ENet sends it again. | open: its scores and gold would need the reliable decisions' order to go unreliably |

## Reproducing

The scripts are kept with the scratch files of this review
(`desync-rules/`, the second round's in `round2/`); the runs need the
retail ISO.

```
# second round: run2.sh <bin> <name> <mode> <race|-> <laps> <delay> <jitter> <loss> <seconds>
#   (AUTOPILOT=0, HOST_ENV / CLIENT_ENV add variables to either instance)
batch.sh <bin dir> <tag>   # races, circuits, Cops and Robbers (ram), the water: both links
batch_before2.sh           # the before build's Cops and Robbers (270 s) and the race in the water
batch_contest.sh           # contested gold, before and after; Cops and Robbers on the final build
batch_water2.sh            # the water with a client that simulates differently
report.sh [run]...         # rulesreport and the bandwidth and correction lines
report_contest.sh          # the contested runs
```

```
batch.sh            # before and after, both links: run.sh (races, autopilot), run_cr.sh (Cops and Robbers)
batch2.sh           # the leaver (OPENMM2_POPUP_SCRIPT quits the race), noise runs
batch3.sh           # noise, Cops and Robbers after 4ac627b, the late loader (run_late.sh)
batch4.sh           # the merged build: one race, one circuit, one Cops and Robbers
report.sh [run]...  # netprobe rulesreport <host trace> <client trace>, and the bandwidth lines
finishpos.py <run> <player>  # where each machine had a car around its finish
```

`run.sh <bin> <name> <race|circuit|crffa> <race index|-> <laps> <delay> <jitter> <loss> <seconds>`
starts the relay on 2411 in front of a host on 2410, the host
(`profile:Host;city:sf;mp:host;mp:mode:<mode>;mp:race:<index>:<laps>;wait:1500;mp:start`)
and the client (`profile:Client;mp:join:127.0.0.1:2411;wait:100;mp:ready`),
each with `OPENMM2_NET_TRACE`, `OPENMM2_DEBUG_NETCARS` and
`OPENMM2_DEBUG_AUTOPILOT=<0|1>`; `run_cr.sh` gives the cars
`OPENMM2_DEBUG_START_GOLD=12`, `OPENMM2_DEBUG_CR_SEED=12527` and the ram
inputs instead; `run_extra.sh` takes a `CLIENT_ENV`; `run_late.sh` runs at
`FrameCap=100` so that 300 s fit in `--frames`.
