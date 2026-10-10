# Multiplayer desync: props and damage

Worked on 2026-10-09 from `integration` at cd5ae29 (release 0.3.1), merged
with integration at 3f73059 (the host simulates every player's car), 52aa601
(the shared traffic) and cae6989 (the players' cars' final work); and in the
rough edges round (protocol 16, "Rough edges round" below) at 782f469 (the
players' cars' second round, the host's rules) and b33bebc (the
cross-platform determinism work), and in a third round (protocol 18, "Third
round" below) at 7fe48bb (the players' cars' third round), on the
maintainer's report that in a multiplayer cruise over the Internet
"knocked-over props, parked cars, objects or car damage look different on
each screen", and on the maintainer's decision that the host is the
authority for everything: props simulated on the host and replicated, a
client predicting the knocks of its own car, props knocked earlier reaching
players who arrive later.

## Summary

Before, **no prop knock was shared**: in 0.3.1 a prop another player knocked
stood on every other screen, the props the host's shared traffic and police
knocked stood on every client, and props two cars hit separately rested
metres apart. Parked cars (network races) and traffic lights (cruise) are
props and behaved the same. With the host simulating the players' cars
(integration) a client's knocks happened on the host too, but in two
separate simulations (pieces 0.1-2.4 m apart), and the host's own knocks and
its traffic's still reached no client.

After, the host simulates every prop for everyone. In every run every knock
reached every machine a playout delay later, no prop differed at the end, and
every knocked-over prop, piece, parked car and traffic light rested within
3 mm of the host's (the position's quantization is 4 mm). A client's own car
knocks props at once and hands them over to the host's when they rest; of 65
such predictions in the final runs 4 were undone, all where the host's
simulation of the car had met a traffic car the client's had not. A client
driving alone through props had no correction of its car in 40 s, and its
car's impacts and damage were the host's to the unit; a client's
corrections just after a prop hit with nothing else around went from 31 over
30 cm (up to 2.3 m and 8.6 m/s) to 7 (up to 0.7 m and 2.4 m/s).

| Run (a minute of driving; host and client) | Knocked on one machine only, at the end | Knocked on both | Knock times apart | Resting apart (both at rest) |
| --- | --- | --- | --- | --- |
| slalom, 80 ±20 ms, 2 % loss, 0.3.1 | 16 of 18 | 2 (separate hits) | 3.8 s | 10.6 m |
| slalom, 80 ±20 ms, 2 % loss, integration | 14 of 31; 6 of 9 | 17; 3 (separate simulations) | 172 ms median | 0.11-1.2 m median, 2.4 m max |
| slalom, 80 ±20 ms, 2 % loss, after | 0 of 14 | 14 | 176 ms median, 226 max | 3 mm max (10 pieces) |
| slalom, 150 ±20 ms, 5 % loss, 0.3.1 | 11 of 12 | 1 (separate hits) | 7.8 s | (still moving) |
| slalom, 150 ±20 ms, 5 % loss, after | 0 of 20 | 20 | 235 ms median, 508 max | 2 mm max (11) |
| follow, 80 ±20 ms, 2 % loss, 0.3.1 | 6 of 6 | 0 | | |
| follow, 80 ±20 ms, 2 % loss, after | 0 of 9 | 9 | 188 ms median, 190 max | 3 mm max (10) |
| follow, 150 ±20 ms, 5 % loss, 0.3.1 | 12 of 12 | 0 | | |
| follow, 150 ±20 ms, 5 % loss, after | 0 of 29 | 29 | 240 ms median, 527 max | 3 mm max (24) |
| parked cars (checkpoint race), 80 ±20 ms, 0.3.1 | 5 of 5 | 0 | | |
| parked cars (checkpoint race), 80 ±20 ms, integration | 2 of 5 | 3 | 3.2 s median | 0.37 m median, 0.59 m max |
| parked cars (checkpoint race), 80 ±20 ms, after | 0 of 5 | 5 | 177 ms median, 185 max | 3 mm max (6) |
| host and two clients, 150 ±20 ms, 5 % loss, after | 0 of 15 (each pair) | 15 | 245-249 ms median (clients 492) | 3 mm max (14), clients 9 mm |
| one client alone through props, 80 ±20 ms, after | 0 of 7 | 7 | 173 ms | 3 mm max (4) |

In the rough edges round (protocol 16, below) a client also predicts the
knocks of the other players' cars it runs (before, such a car drove through
a prop that fell 140-400 ms later), undoes a missed prediction in 0.7-1.8 s
instead of 2 s, no longer lets its own pieces knock props, and gets its own
message with the states near its car first; the ring grows in a pile-up.
In the third round (protocol 18) the host sends the pieces round a client's
car in full, and a client's car pushing them is predicted exactly: in a test
of a car pushing a pile of 18 props through the rest, no correction instead
of 269; in the race runs, its corrections near knocked pieces fell by 58 %.

Props down on one machine and standing on the other, at any 250 ms tick:
6-16 at the end of the 0.3.1 runs (in 72-93 % of the ticks), 2-6 at the end
of the integration runs (72-93 %); after, at most 2 or 3 for a moment (a
knock between the host's simulation and its display on the client, 3-20 % of
the ticks), 0 at every end.

## How it was measured

* Host and clients on this machine, UDP port 2400 (relays 2401, 2402), each
  with its own config and data directories, `FrameCap=120`, `VSync=false`,
  `OPENMM2_NO_PORTMAP`, `netprobe relay` in front of the host for each client
  (80 ±20 ms each way, 2 % loss, reordering; and 150 ±20 ms, 5 % loss).
* The cars driven by `OPENMM2_DEBUG_INPUT` from fixed starts
  (`OPENMM2_DEBUG_START` in cruise, `OPENMM2_DEBUG_SPAWN` in a race):
  **slalom** (San Francisco, the host leading and the client 15 m behind,
  weaving along a sidewalk of street lights, parking meters, bus stops and
  bins through two signalled intersections, with the shared traffic and
  police on), **follow** (the same with longer swerves), **parked cars** (a
  checkpoint race, both cars driving into a row of parked cars in a San
  Francisco car park), the slalom with **a second client** behind the first,
  and the slalom by **one client alone** (the host parked, traffic off).
* `OPENMM2_NET_TRACE` writes the props' lines next to the cars' (added,
  docs/multiplayer.md "Diagnosing replication": every placed prop once,
  every knock and what made it, every undone prediction, and every 250 ms of
  session time the props down, the pieces and parts shown, each car's damage
  level and dents, and each simulated car's damaging impacts with their
  cause). `netprobe syncreport <trace>...` compares every pair of machines'
  props at the same session times, after the cars.
* "0.3.1" is this work's first build (on cd5ae29) with `OPENMM2_NETPROPS=local`
  (added), which keeps each machine's own props; "integration" a build of
  the merged branch with `OPENMM2_NETPROPS=local`, which is integration's
  props (the first figure at 3f73059, the second at cae6989); "after" the
  final build (59ca116).
* A client's corrections after a prop hit: the trace's corrections of its
  car (`C`) over 30 cm within 1.5 s of an impact of its car with a prop or a
  knock it predicted, with no impact of the car with the city, a car or the
  traffic in that time on either machine.
* Same-moment screenshots (`OPENMM2_DEBUG_NET_SHOT_MS`) with
  `OPENMM2_DEBUG_FOCUS=prop:<index>` (added: a placed prop, or its first
  piece once knocked, framed alike on every machine): a parking meter a
  client knocked lies in the same place on both screens once at rest.

The scripts that ran these (`run.sh`, `cmp.sh`, the scenario files) were kept
in the session's scratch directory; everything they set is listed above.

## What MM2 does

Read: `mmGameMulti::GameMessageCB`, `SystemMessageCB`, `Init`,
`mmMultiRoam::GameMessage`, `Init`, `InitGameObjects`, the other modes'
`GameMessage`, `mmNetObject` (`PositionUpdate`, `Update`, `SetPositionData`),
`mmGame::InitGizmos`, `dgBangerManager`, `dgBangerActiveManager`,
`dgUnhitBangerInstance::Impact`, `dgImpact::CalcImpact`, `dgPhysManager`,
`vehBreakableMgr::Eject`; every use of the network flag (`mmGame`,
`mmGameManager`, `mmPlayer`, `mmGameMulti`): none touches props.

* **Nothing about props travels.** No game message names a prop, a banger or
  a gizmo; `mmNetObject` carries the cars only.
* **Every machine knocks its own.** Each network car is a `vehCar` simulated
  on every machine (`mmNetObject::Update` declares it a type-3 mover, which
  also keeps its room's props alive in `dgPhysManager::Update`), pulled
  toward its owner's positions. Its collisions knock that machine's props,
  break its parts off (`vehBreakableMgr::Eject` with that machine's random
  numbers) and dent it, so in MM2 a knocked prop shows on every screen only
  roughly and rests where each machine's simulation leaves it.
* **What a network game has**: parked cars only in the network races
  (`mmGame::InitGizmos` skips them in cruise and Cops and Robbers), no ferries,
  no cable cars (`mmGameMulti::Init`), traffic lights wherever `aiMap::Init`
  runs (cruise and Cops and Robbers; the races skip it). OpenMM2 already
  followed these (`world::GizmoKinds::forSession`, `RaceScreen::loadAi`).

What MM2's model gives when two machines simulate the same hits: in a build
where every machine's copies of the others' cars could knock props, 4 props
knocked on both machines rested 7.9-21.5 m apart; at integration, where the
host's simulation of a client's car repeats the client's knocks, 0.1-2.1 m.
This is why OpenMM2 does not follow MM2 here (the maintainer's decision).

## Findings (before)

| # | Severity | Location (cd5ae29) | Scenario | Confirmed | Fix |
| --- | --- | --- | --- | --- | --- |
| D1 | gameplay-breaking | `phys/Impact.cpp` `calcBangerImpact`, `RaceScreen.cpp` `updateRemoteCars` | A network car was a kinematic body with no phInertialCS in its collider: dgImpact's impulse to stop it was zero, never beyond a banger's limit, so it never broke one (and passed through it). Every prop another player knocked stood on every other screen, a solid obstacle at its old place. | traces: 0 of 18, 0 of 12, 0 of 6 knocks shared by the same hit | 0ab89fe: the host's props are everyone's; at 3f73059 the host simulates the players' cars |
| D2 | visible | `RaceScreen.cpp` `loadAi`, `TrafficProxies` | The shared traffic and police run on the host only: their knocks (2-4 a minute in the runs) happened on the host only; a client's received cars are kinematic proxies. | traces: "traffic" and "police" knocks on the host only | 0ab89fe: the host's knocks reach everyone |
| D3 | visible | `BangerSet` (every machine its own) | Props two cars hit separately were knocked on both machines by different hits, seconds apart, and rested 10.6 m apart; with the host simulating the client's car, 0.1-2.1 m apart. | slalom runs | 0ab89fe: one simulation, the host's |
| D4 | visible | `RaceScreen.cpp` `updateNetCarDamage`, `breakParts` | A car's thrown parts were bangers of each machine: thrown when the damage event was fresh, with each machine's own random direction, resting in different places (or not thrown at all when the event came late). | by reading; the parts' descriptors in the trace | 0ab89fe: the host throws them, every client shows the host's ring |
| D5 | visible | `world::initGizmos` (network races) | Parked cars are props: the same as D1-D3. | parked runs: 5 knocked, none on both machines | as D1 |
| D6 | visible | `RaceScreen::addTrafficLightProps` | Traffic lights are props (round 3): the same as D1-D3, the signal gone on one screen and standing on the other. | 1-5 traffic lights a run on one machine only | as D1 |
| D7 | minor | `DamageSync` (0.3.0) | The cars' damage converged (each car's dents and level were its owner's; at most 1-3 dents apart for an event in flight), but the prop hits that caused a client's dents were knocks no other screen showed. | traces: dents apart mean ≤ 0.2 | as D1; damage is the host's at 3f73059 |
| D8 | visible (found after the merge) | `World::collideHeld` (3f73059) | A client replays its car when the host corrects it, with everything else held still. A prop there held like a wall (calcBangerImpact against a body without an ICS stops the car past the break limit) and a knocked-over piece like an infinitely heavy body: corrections after a prop hit of up to 1.5 m and 6.6 m/s that the real samples had not had. | merged build, slalom: 5 corrections over 30 cm within 1.5 s of a prop hit, 6.6 m/s | 0791996: the replay meets a prop with the body a real hit gives it; 784b3b1: a prop the car broke loose stands again for the replay, which takes the same knock |
| D9 | visible (found after the merge) | `PropClient` mirrors | With three machines, a traffic light's two pieces lay together on one client 6.4 m from where the host's rested: a piece the client simulated woke the other and they kept each other creeping down the hill. | three-machine run | 6764d4d: the host's pieces move on a client only for its own car; a local simulation of one ends after 4 s |
| D10 | minor (found in the final runs) | `BangerSet::acceptsFrom` | A client's own simulated piece knocked a parking meter 3 s after its car hit a street light; the host's piece of the same knock had gone elsewhere, and the meter stood up again 2 s later (an undone prediction). | client alone, one undone of 4 | f1f3b56: a piece this machine simulates knocks placed props for its first 0.5 s only |
| D11 | visible (found in the final runs) | `BangerSet::replayGhost` (784b3b1) | A prop the client's car broke loose stood again for every replay in the next second, so a replay that started after the knock (the host's correction a few samples later) knocked the car a second time. | client alone: corrections of 1.3-1.6 m and up to 8.6 m/s 250-550 ms after a hit; 31 corrections over 30 cm after prop hits alone in eight clients' runs | 59ca116: it stands for the samples up to the one that broke it loose (`World::replayTime`) |
| D12 | visible (found in the final runs) | `World::collideHeld` (0791996) | The client puts the bodies around its car back where each sample started them (c367288), but a real sample moves a simulated body on before the collisions: the replayed car met the pieces a sample behind and sank into them; and a light piece gave way after one hit, though the real car pushed it all along. | test: a replay from 1-8 samples after a knock ended 0.1-0.2 m from the real samples | 59ca116: the held copy moves on over the sample; no giving way (the replay ends within 15 mm and 0.04 m/s) |
| D13 | visible (rough edges round) | `RaceScreen` props toucher (`ownCarBody`), with c5c1b4c | A client runs the other players' cars near its own (`NearCarState`) ahead to its own time, but only its own car could knock its props: such a car drove through a prop that fell when the host's knock came. | before: props fell 143-404 ms after the car (as drawn there) came closest, the car 2.2-8.5 m from the prop's place (median per run) | 993c285: a client predicts the knocks of the cars it simulates (dcca773: the police and knocked traffic cars too; cd9a409: waiting up to 4 s while the host has that car by the prop) |
| D14 | visible (rough edges round) | `PropClient::update` | A prediction the host never confirmed stood until 2 s had passed. | every undone prediction, 2.0 s | 993c285: undone once the host's messages have passed it by the host's lag (another player's car's: once the host also had that car away from the prop and this machine's car is not about to hit it) |
| D15 | visible (rough edges round) | `BangerSet::acceptsFrom` (f1f3b56) | A client's own pieces still knocked placed props in their first 0.5 s: 4 of 9 such knocks were confirmed, and one left the client's car tangled in pieces the host never had (157 corrections over 30 cm in a minute, the prop knocked and undone three times). | follow 150 ms | 993c285: a client's pieces knock no placed prop (the host's knocks bring its chain knocks) |
| D16 | minor (rough edges round) | `PropHost::build`, `BangerSet::getBanger` | Every client got the same message, the whole ring whatever was near it; and the ring of 40 wraps in a pile-up, so props knocked moments ago disappeared in mid-flight. | by reading; tests | 993c285: each client its own message, the states near its car first within a datagram; the ring grows instead of wrapping onto a busy prop |
| D17 | minor (rough edges round) | `PropClient::update` (mirror hold) | A host's piece the client's car pushed was held where it stopped for 2 s even when the host's car never touched it, before going back to the host's. | parked cars: a piece 1.5-2 m from the host's for up to 2 s | 993c285: held as long as the host's push would take to show (its lag and the playout delay) |

Damaging impacts on each machine's own car by cause, 0.3.1 runs: the city
47, props 3, traffic 2; no player-versus-player impact. After, each car's
impacts as its own machine predicted them and as the host simulated them:
identical (cause, value and damage) for a client alone among props; where
its car also met the host's car or the traffic, the counts differed by one
or two (the players' cars' corrections).

## The design (OpenMM2 extra)

See docs/multiplayer.md, "Props", for the protocol and the rules; in short:

* **Host**: the props as in single player with every car it simulates (its
  own, the players', the shared traffic and police). Each frame, after
  `BangerSet::update`: the placed props that broke loose go to everyone as a
  reliable `PropKnocks` event (a catch-up of all of them to a machine that
  reports the race loaded), and the ring of knocked-over props
  (`dgBangerManager`, 40 slots) as `PropState`, the same unreliable message to
  every client: 20 a second while a prop flies, 5 while props only creep, 2
  otherwise.
* **Identity**: a placed prop is its index; every machine places the same
  props in the same order (verified on retail data, `PropSyncRetail`, and in
  the runs: 7068 props, catalog `f5672068`, in a San Francisco cruise on host
  and clients; 6637, `bf4d06a4`, in its checkpoint race). A piece is its prop
  and BREAKnn part, a car's part its car, part and paint job.
* **Client**: only its own car may touch its props; the host's knocks apply
  when it shows the host's props at their time, a playout delay in the past
  as the players' cars; the host's ring is shown in mirror slots; its own
  car's knocks are predictions, handed over to the host's pieces when both
  rest, undone after 2 s without the host's knock; its own pieces knock
  other props for their first 0.5 s only; its replays meet props with their
  mass, a prop its car broke loose standing again for the samples up to
  that knock, the pieces after it where they were.
* **Thrown car parts** are the host's ring like the rest.

Deviations from MM2: the host's props on every screen instead of each
machine's own simulation; a client's props ignore the other cars (the host
decides); a prediction the host does not confirm stands up again; a mirror
attached here starts from the host's motion (`dgBangerActive::Attach` starts
at rest). Recorded in docs/parity/openmm2-only.md.

## Damage

The damage is the host's (the players' cars agent): every car's dents,
parts and level from the host's own collisions. This work's part:

* **Props hitting cars and cars hitting props on the host are single
  player's.** The host's props are a single-player `BangerSet` (its replica
  mode is a client's only) and the players' cars are `CarSim`s of the host's
  world set up as its own car (`options.player`, the polygonal bound, damage
  on); `calcBangerImpact` is MM2's for every simulated body, bit for bit. The
  only difference from a single-player car is the mover type (3, not 4),
  which marks what it hits as "hit by the player" for the AI, not for damage.
  Measured: a client driving alone through 7 props in 40 s had no
  correction, and the trace's impacts of its car on the host and on the
  client (a prop at 2356.5, the city six times, damage 53690.3 in all) were
  the same.
* A prop that dents a client's car is now a knock every screen shows, at the
  same place, and rests where the host's does.
* A client's car meets the host's flying pieces with their motion (before,
  they were ghosts to it), so its prediction takes the hits the host's
  simulation of it does.
* Thrown parts are the host's: a client takes the parts the host's records
  take off (its own car's included) without a throw.
* A client's corrections just after a prop hit, with no impact of its car
  with the city, a car or the traffic on either machine in the 1.5 s before
  (the car's own corrections being the players' cars' work): at integration
  (each machine its own props) 184 of 336 corrections within 1.5 s of a
  prop hit, 65 over 30 cm, the largest 5.6 m and 13.4 m/s; with the host's
  props before D11 and D12, 31 over 30 cm in eight clients' runs, the
  largest 2.3 m and 8.6 m/s; after, 7 in eight (two runs), the largest 0.7 m
  and 2.4 m/s. A client alone, and the first client of three, had no
  correction at all while knocking 7-8 props.

Damaging impacts on each machine's own car by cause, 0.3.1 runs: the city
47, props 3, traffic 2; no player-versus-player impact. After, each car's
impacts as its own machine predicted them and as the host simulated them:
identical (cause, value and damage) for a client alone among props; where
its car also met the host's car or the traffic, the counts differed by one
or two (the players' cars' and the traffic's corrections).

## Bandwidth

Per client, from the host's 10-second logs over the final runs: 121 bytes/s
(props at rest) to 5.0 KB/s with one client, 4.4 KB/s to each of two clients
at most (20 messages a second, 18-23 slots, 5-9 states a message). The ring
bounds a message at 40 slots (a flying piece 26 bytes, about 1 KB); knocks
are 31 bits each in reliable events (250 per 1 KiB event). A big crash in the
test (a car through 60 props, 27 knocked) cost 5 KB/s for a few seconds, 425
bytes at most a message. The host sends the same message to every client: for
seven clients about 1-35 KB/s in these conditions, at most about 140 KB/s
in a crash that keeps all 40 slots flying.

## Tests

* `tests/net/test_prop_state.cpp`: the messages' round trips, sizes (a full
  ring in one datagram, a grown ring of 256 listed in 6 bits a slot, 250 live
  and 500 catch-up knocks in one event) and malformed input (slots out of
  order among them); `test_fuzz.cpp`: both in the corpus and the decoders'
  checks (ascending slots), and sent mutated to a client.
* `tests/game/test_prop_sync.cpp`: a host and a client in one process through
  a lossy link: the host's knocks and pieces on the client, a client's knock
  predicted and handed over, one undone, other cars passing through a
  client's props, a client's own pieces not moving the host's, a replayed
  car meeting a prop as a real sample does (from before the knock, and from
  1, 4 and 8 samples after it, the bodies put back for each sample as the
  race screen does), a replay knocking and moving no prop for real, 30 %
  loss with reordering, a big crash, a pile-up growing the ring, two clients
  far apart each getting its own area's states first within a small
  datagram, a pile-up beyond a datagram taking turns, a client's piece
  knocking no placed prop, a near car's knock predicted, one undone once the
  host had that car elsewhere, one held while this machine's car comes and
  one the host's braking car makes late, a missed own knock undone once the
  host's messages passed it, the catch-up, thrown car parts, knocks
  counted from the host only, refused input and a different placement, and
  on retail data the same placement on every machine (cruise at the host's
  and a client's traffic density, a checkpoint race with parked cars) for
  San Francisco and London.
* Full suite with game data at 59ca116: 985 of 985 (3 skipped as always),
  the same with `-D_GLIBCXX_ASSERTIONS`; the opponent sweep 516/517
  finished, 508/517 across the line (as before); single-player smoke
  screenshots (a San Francisco cruise, a London circuit) as before.
* Rough edges round, merged with integration at 65529a6 (the determinism
  work, the traffic's and the rules' second rounds): 1045 of 1045 (3
  skipped; integration's 1035 and 10 new), the determinism hashes
  unchanged; the same with `-D_GLIBCXX_ASSERTIONS`; the sweep 515/517
  finished, 507/517 across the line (the new baseline); the smoke
  screenshots as before.

## Rough edges round (protocol 16; 13 on its branch)

The maintainer accepted more bandwidth and asked for the remaining rough
edges. Measured as above on the merged build before (integration 782f469,
protocol 10) and after (cd9a409 merged with integration at 65529a6: the
traffic's and the rules' second rounds), one run each:

| Run | Knocks shared; on one machine only at the end | Resting apart, host and client | Another player's car's knocks: that car from the prop's place when it fell, and when it fell against when that car (as drawn there) came closest (medians) | Predicted (undone) |
| --- | --- | --- | --- | --- |
| slalom, 80 ms, before | 25; 0 | 3 mm | 12: 3.5 m, 222 ms after | 11 (1, at 2 s) |
| slalom, 80 ms, after | 13; 0 | 15 mm | 1: 2.3 m, 257 ms before | 11 (0) |
| slalom, 150 ms, before | 7; 0 | 2 mm | 1: 2.2 m, 143 ms after | 4 (0) |
| slalom, 150 ms, after | 5; 0 | 2 mm | (none this run) | 3 (0) |
| follow, 80 ms, before | 12; 0 | 2 mm | 8: 3.1 m, 84 ms before | 0 |
| follow, 80 ms, after | 14; 0 | 3 mm | 5: 2.3 m, 131 ms before | 7 (0) |
| follow, 150 ms, before | 10; 0 | 3 mm | 6: 4.6 m, 369 ms after | 1 (0) |
| follow, 150 ms, after | 15; 0 | 3 mm | 6: 2.4 m, 102 ms before | 8 (0) |
| parked cars, before | 5; 0 | 27 mm | 3: 3.9 m, 171 ms after | 2 (0) |
| parked cars, after | 5; 0 | 3 mm | 4: 3.7 m (pushed slowly) | 4 (0) |
| three machines, before | 28; 1-3 (knocked in the last second, after a client's trace ended) | 3 mm (one piece still falling: 0.9 m) | 11: 2.7 m, 25 ms before; 6: 8.5 m, 404 ms after | 14 (0) |
| three machines, after | 32; 0 | 3 mm | 12: 2.6 m, 116 ms before; 22: 2.8 m, 128 ms before | 33 (0) |
| one client alone, before | 12; 0 | 2 mm | | 7 (0); no correction |
| one client alone, after | 4; 0 | 3 mm | | 4 (0); no correction |

A car's centre is about 2.2 m behind its front: "2.2-2.7 m" is a prop
falling as the car's front meets it, and a prop falling 130-190 ms before
the car's centre came closest is one falling at the contact.

* **Another player's car's knocks** (D13) are now predicted by the client
  that runs that car (the near ones, `NearCarState`), as its own car's are,
  and replayed with it (`World::collideHeld`, `replayGhost`); since the
  traffic's second round also the shared police and knocked traffic cars it
  runs (`TrafficFull`; 7 of 8 confirmed in the last two batches). Over the
  nine batches of the round with it the host confirmed 179 of 198 knocks by
  another player's car; the 19 others were knocks the client's run-ahead of
  that car made and the real car did not (the prop stands again; none in
  the last batch, all 66 predictions there confirmed). Better inputs for
  the near cars (the players' cars' work) will lower that; the client's own
  car's predictions: 378 of 382 confirmed.
* **Undoing** (D14): a missed prediction stands again as soon as the host's
  messages have passed it by the host's usual lag (the most of the last 16
  confirmed, plus 0.3 s, at least 0.6 s), in the runs 0.7-1.8 s after it
  fell (2 s before); one by another player's car waits until the host also
  had that car more than 6 m from the prop and this machine's car is more
  than 15 m from it (the host's knock may still come from either: in two
  runs it came 1.7-2.2 s later, from the client's own car); while the host
  still has that car within 6 m of the prop it waits up to 4 s (a braking
  car reached a parked car 1.9 s after this machine's run-ahead of it, and
  the knock stood up and fell again at 2 s, cd9a409).
* **Chain knocks** (D15): a client's own pieces meet one another and no
  placed prop; the host's knocks bring the ones its pieces make.
* **Interest** (D16): each client's message lists every occupied slot and
  carries the states its area needs first (150 m from its car), within 1100
  bytes; far states less often; the knocks go to everyone, reliably, so no
  client misses one in its area or anywhere else. In the runs 88-100 % of
  the states sent were for the client's area while props lay there, and no
  state waited for room. Tests: two clients 300 m apart with a 300-byte
  budget got every flying state of their own area in every message (890 of
  890) and the far ones as room allowed (85 deferred), and both ended with
  every piece where the host's rests; 24 props flying around one client with
  a 200-byte budget took turns, none waiting more than 6 messages.
* **The ring** grows by 40 slots, up to 40 a player (at least 80, at most
  256), when it is about to wrap onto a prop that still moves or was knocked
  less than 10 s ago (test: a car through 60 props in 9 s: 80 slots, every
  knocked prop still there; single player: 40, wrapped). The measured runs
  never needed it (at most 29 knocks a minute).
* **Mirrors** (D17): a host's piece a client's car pushed goes back to the
  host's as soon as the host's push would have shown.

Bandwidth after: 31 bytes/s to 6.4 KB/s to each client (10 s averages over
the round's runs), at most 5.5 KB/s before; a message at most 1100 bytes.

## Third round (protocol 18)

On the merged build (integration 7fe48bb: the players' cars' third round,
protocol 17, merged at f5e2f92), three items: a regression the players' cars
agent measured in its chase scenario, a client's car pushing knocked props
and pieces (O11), and the near cars' knock predictions again.

### The chase's corrections are not the props' second round

The players' cars agent's chase (the host leading, the client 10 m behind,
both driven by looping inputs, 60 ±20 ms each way, 2 % loss, 75 s) corrected
the client's car 114-405 times a minute on integration after the props'
second round, and not at all on integration before it. The same scenario
here, its scripts copied (port 2400), corrections a minute:

| Build | Runs |
| --- | --- |
| 782f469 (integration, before the props' second round) | 36, 111, 152 |
| b33bebc (the determinism work) | 205, 118, 201 |
| 8e74c96 (the traffic's second round; the cars agent's "before", 0 and 0 there) | 149, 167, 112 |
| 65529a6 (the props branch before its second round) | 169, 99, 0 |
| 482ac2c (after it) | 102, 251, 113 |
| f5e2f92 (merged with the cars' third round) | 99, 34, 123, 284; with every second-round change turned back (debug switches): 152, 211; with none: 260, 8 |
| the cars' third round before it merged the props' second (the cars agent's own runs) | 426, 15 |

The same binary gives both: 8e74c96 corrected 0 times in both the cars
agent's runs and 112-167 times a minute in all three here, and every build
has runs with almost none. The corrections are velocity errors of 3-10 cm/s
with the position 1-3 mm off (just over the 3 cm/s tolerance), most of them
with no knocked piece within 4 m and the other car farther than 6 m; once
they begin in a run they go on, and a run either has them from its first
seconds or barely at all. The client's car hits the city at the same
places, as hard, in every run, with them or without. Nothing of the props'
second round brings them; they are the players' cars agent's to find
(below).

### Pieces round a client's car in full (O11)

A client's car leaning on knocked-over props and pieces met them where the
host's messages showed them (a playout delay in the past), or as its own
simulation had them, and was corrected at nearly every state. The host now
sends, with each client's `CarStates`, the pieces round its car in full
(`net::PropFullMsg`, docs/multiplayer.md "Props"), and the client runs them
with its car when its samples run again. A deterministic test
(`tests/game/test_prop_push.cpp`: host and client in one process, the
client's car pushing a pile of 18 props through the rest, 100 ms each way)
found what such a replay needs to give the host's samples exactly, step by
step (20 states a second unless said; the pile's first rows knock the
next, so the farther pieces matter as the cap allows):

| The client's replays | Corrections |
| --- | --- |
| without the pieces in full | 95 |
| the pieces' bodies, the nearest 8 within 1200 bytes | 71 |
| and their hardest pusher; the other bodies put back for each sample as the race screen puts them | 78 |
| and the bound where the sample's collisions saw it (the push moves the body after them; the next sweep starts from the bound) | 89 (a piece now about 250 bytes: 4 in a message) |
| forces and pushes sent only when not zero, the bound's turn only when it turned (about 190 bytes: 6 in a message) | 69 |
| a piece in full takes this machine's own piece of the same knock before the slot is shown | 68 |
| three messages a state, the nearest 18 | 8 |
| 60 states a second, the pieces in full 20 times; a piece in full knocks a placed prop as the host's does, with the replays' ghost | 1 (as committed: 0, and 269 without) |

With the bound, a replay of one row of three pieces reproduced the host's
states to the last bit for the whole run (before it, the impulses already
differed after the first sample run again; the hardest pusher, also sent,
made no measurable difference there); a pile of two rows went from 95 to
none once the bound and the early stand-in were in. The pieces in full every
third of 60 states corrected the car about as rarely as at every state (at
every state and every third: two rows 0 and 0, six rows 0 and 8) with a
third of the bytes.

In the race runs, five each of the cars agent's rear-end scenario (the
client's car into the back of the host's, knocking props on the way), on
f5e2f92 and on this round's build:

| | Corrections a minute | Over 1 cm with a knocked piece within 4 m, a minute | Largest of those | Over 1 cm near a piece only (the other car farther than 6 m), each run |
| --- | --- | --- | --- | --- |
| before | 530 (294-744) | 208 (96-382) | 2.81 m | 148, 63, 192, 108, 129 (41 knocks) |
| after | 550 (136-1521) | 87 (0-242) | 0.77 m | 0 (no knock), 13, 8, 5, 60 (24 knocks) |

The corrections left are the two cars against each other (the run with
1521 a minute had no knock at all). In the chase (four runs each) the
corrections over 1 cm near a piece only went from 58 to 27 (25 and 17
knocks). The pieces still rest where the host's do (2 mm median), but a
piece the client ran itself at the end of a run was 0.14-0.21 m from the
host's, being handed back. The host sent 0.2-9 KB/s of pieces in full to a
client while they moved round its car (10 s averages: 0.2-1.6 KB/s in the
chase, 2-9 KB/s in the rear-end runs), at most three 1200-byte datagrams 20
times a second.

### Another player's car's knocks on the merged build (item 3)

The seven scenarios again (slalom and follow at 80 and 150 ms, parked cars,
three machines, one client alone), on this round's build:

| | Round 2's last batch | This round |
| --- | --- | --- |
| knocks by another player's car the client predicted, of the host's | 15 of 50, none undone | 31 of 50, 3 more undone |
| its own car's predictions confirmed | 52 of 52 | 49 of 49 |
| the police's and knocked traffic cars' | 1 of 1 | 11 of 12 |
| a piece in full knocking a placed prop | | 2 of 2 |
| resting apart (both at rest) | 3 mm (one 15 mm) | 3 mm (one 0.14 m, being handed back at the end) |

The cars' third round (three near cars with the inputs the host holds for
them) doubled the share of another player's car's knocks a client predicts;
the three undone ones, all in the three-machine run, are knocks a client's
run-ahead of another car made and that car on the host did not. Where they
were predicted the prop fell here 165-261 ms (medians, session time) before
it fell on the host, as the car met it here.

Checks: 1050 of 1050 tests (3 skipped), the determinism hashes unchanged;
the same with `-D_GLIBCXX_ASSERTIONS`; the sweep 515/517 finished, 507/517
across the line (the baseline); the smoke screenshots as before.

## For the players' cars agent

* A client's props accept contacts from its own car and trailer
  (`ownCarBody`), the other players' cars it simulates (`propMover`,
  `predictedBody`'s cars) and its own pieces among themselves
  (`BangerSet::setReplica`). The companions' replays meet props as the own
  car's do (`replayGhost` for the knocks a companion made here).
* `World::beginReplay(from)` (called by `CarPrediction::acknowledge`) starts
  a replay; `from` is the world time the first sample run again first ran at
  (the samples ran one after another up to the world's time now), and
  `World::replayTime` the current one's. A prop the car broke loose stands
  again for the samples up to the one that broke it loose.
* `World::collideHeld` moves a simulated body's copy on by its velocity over
  the sample before the test (the race screen puts it back where the sample
  started it); the light bodies' giving way is gone.
* `hostCarState` reads `m_remoteCars` (the host's state of a player's car as
  the client shows it) to tell whether the host's car went away from a prop
  its run-ahead knocked.
* In the checkpoint race the client's corrections still run at every
  acknowledgement for the first 3-4 s after the start (0.56 m each, the sign
  alternating, 58-59 samples run again), with the props kept local as well:
  the race's start, not the props.
* The chase's corrections (third round, above): the same binary corrects a
  client's car 0 or 100-170 times a minute from run to run, 3-10 cm/s at a
  time, mostly far from pieces and from the other car, and once they begin
  they go on. Not the props.
* A body run again with the car needs, besides its rigid body and what a
  sample hands the next, the bound where the sample's collisions saw it
  (the push moves the body after them) and its hardest pusher: the pieces in
  full carry both; `TrafficFull`'s knocked cars carry neither, and the car's
  own state names no piece as its pusher (`PropFullMsg::carPusher` does).
  phSleep's jitter sums are carried by none.
* `CarPrediction::acknowledge` runs the companions in the order given; the
  pieces in full come in the host's world's order, after the car.

## Open

* A knock by another player's car that this client runs ahead is wrong about
  one time in ten (the real car passed the prop); it stands again within
  0.7-2 s (4 s while the host has that car by the prop). Re-measure once
  the near cars' inputs improve; a further step would re-run the knock from
  each newer host state and undo it as soon as that run misses the prop.
* The shared traffic on its rails (not simulated on the client) still
  knocks props only when the host's knock comes; the police and knocked
  cars the client simulates predict theirs (dcca773).
* A client's car leaning on a slowly creeping piece that the host's copy of
  the car does not touch (once, at the end of a three-machine run): the
  piece is pushed here, held, handed back, pushed again, and the car's
  corrections run at every acknowledgement until it drives away. Since the
  third round the host's piece comes in full while it moves within 10 m of
  the car and is simulated here from it, without the hand-back; a piece
  the host has at rest is not run with the car.
* More than about 18 pieces moving round a car at once: the farther ones
  wait for room (three datagrams a state).
* Props are interest-managed by distance from the client's car only, not by
  what its camera sees.
