# Multiplayer desync: props and damage

Worked on 2026-10-09 from `integration` at cd5ae29 (release 0.3.1), merged
with integration at 3f73059 (the host simulates every player's car), on the
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
(integration at 3f73059) a client's knocks happened on the host too, but in
two separate simulations (pieces 0.1-2.1 m apart), and the host's own knocks
and its traffic's still reached no client.

After, the host simulates every prop for everyone. In every run every knock
reached every machine a playout delay later, no prop differed at the end, and
every knocked-over prop, piece, parked car and traffic light rested within
5 mm of the host's (the position's quantization is 4 mm). A client's own car
knocks props at once and hands them over to the host's when they rest; of 49
such predictions in the final runs one was undone (the host's simulation of
the car missed the prop). A client driving alone through props had no correction of its car in
40 s, and its car's impacts and damage were the host's to the unit.

| Run (a minute of driving; host and client) | Knocked on one machine only, at the end | Knocked on both | Knock times apart | Resting apart (both at rest) |
| --- | --- | --- | --- | --- |
| slalom, 80 ±20 ms, 2 % loss, 0.3.1 | 16 of 18 | 2 (separate hits) | 3.8 s | 10.6 m |
| slalom, 80 ±20 ms, 2 % loss, integration | 14 of 31 | 17 (separate simulations) | 172 ms median | 0.11 m median, 2.1 m max |
| slalom, 80 ±20 ms, 2 % loss, after | 0 of 8 | 8 | 161 ms median, 181 max | 2 mm max (8 pieces) |
| slalom, 150 ±20 ms, 5 % loss, 0.3.1 | 11 of 12 | 1 (separate hits) | 7.8 s | (still moving) |
| slalom, 150 ±20 ms, 5 % loss, after | 0 of 14 | 14 | 245 ms median, 260 max | 3 mm max (17) |
| follow, 80 ±20 ms, 2 % loss, 0.3.1 | 6 of 6 | 0 | | |
| follow, 80 ±20 ms, 2 % loss, after | 0 of 11 | 11 | 188 ms median, 219 max | 3 mm max (11) |
| follow, 150 ±20 ms, 5 % loss, 0.3.1 | 12 of 12 | 0 | | |
| follow, 150 ±20 ms, 5 % loss, after | 0 of 13 | 13 | 248 ms median, 519 max | 3 mm max (17) |
| parked cars (checkpoint race), 80 ±20 ms, 0.3.1 | 5 of 5 | 0 | | |
| parked cars (checkpoint race), 80 ±20 ms, after | 0 of 5 | 5 | 185 ms median, 816 max | 2 mm max (6) |
| host and two clients, 150 ±20 ms, 5 % loss, after | 0 of 20 (each pair) | 20 | 258-260 ms median | 5 mm max (13-15) |
| one client alone through props, 80 ±20 ms, after | 0 of 9 | 9 | 171 ms median | 3 mm max (6) |

Props down on one machine and standing on the other, at any 250 ms tick:
6-16 at the end of the 0.3.1 runs (in 72-93 % of the ticks); after, at most
2 or 3 for a moment (a knock between the host's simulation and its display on
the client, 3-15 % of the ticks), 0 at every end.

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
  (added), which keeps each machine's own props; "integration" the final
  build with `OPENMM2_NETPROPS=local`, which is 3f73059's props; "after" the
  final build.
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
| D8 | visible (found after the merge) | `World::collideHeld` (3f73059) | A client replays its car when the host corrects it, with everything else held still. A prop there held like a wall (calcBangerImpact against a body without an ICS stops the car past the break limit) and a knocked-over piece like an infinitely heavy body: corrections after a prop hit of up to 1.5 m and 6.6 m/s that the real samples had not had. | merged build, slalom: 5 corrections over 30 cm within 1.5 s of a prop hit, 6.6 m/s | 0791996: the replay meets a prop with the body a real hit gives it, a light one giving way after its first hit |
| D9 | visible (found after the merge) | `PropClient` mirrors | With three machines, a traffic light's two pieces lay together on one client 6.4 m from where the host's rested: a piece the client simulated woke the other and they kept each other creeping down the hill. | three-machine run | 6764d4d: the host's pieces move on a client only for its own car; a local simulation of one ends after 4 s |

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
  rest, undone after 2 s without the host's knock; its replays meet props
  with their mass.
* **Thrown car parts** are the host's ring like the rest.

Deviations from MM2: the host's props on every screen instead of each
machine's own simulation; a client's props ignore the other cars (the host
decides); a prediction the host does not confirm stands up again; a mirror
attached here starts from the host's motion (`dgBangerActive::Attach` starts
at rest). Recorded in docs/parity/openmm2-only.md.

## Damage

The damage is the host's at 3f73059 (the players' cars agent): every car's
dents, parts and level from the host's own collisions. This work's part:

* **Props hitting cars and cars hitting props on the host are single
  player's.** The host's props are a single-player `BangerSet` (its replica
  mode is a client's only) and the players' cars are `CarSim`s of the host's
  world set up as its own car (`options.player`, the polygonal bound, damage
  on); `calcBangerImpact` is MM2's for every simulated body, bit for bit. The
  only difference from a single-player car is the mover type (3, not 4),
  which marks what it hits as "hit by the player" for the AI, not for damage.
  Measured: a client driving alone through 9 props in 40 s had no
  correction, and the trace's impacts of its car on the host and on the
  client (a prop at 2369, the city eight times at 22850 in all, damage 36001)
  were the same.
* A prop that dents a client's car is now a knock every screen shows, at the
  same place, and rests where the host's does.
* A client's car meets the host's flying pieces with their motion (before,
  they were ghosts to it), so its prediction takes the hits the host's
  simulation of it does.
* Thrown parts are the host's: a client takes the parts the host's records
  take off (its own car's included) without a throw.
* A client's corrections after a prop hit (within 1.5 s): at integration
  (separate props) 184 of 336 corrections, 65 over 30 cm, the largest 5.6 m
  and 13.4 m/s; after, slalom 80 ms: 55 of 395, 2 over 30 cm, the largest
  0.38 m and 2.4 m/s; none for the client alone.

## Bandwidth

Per client, from the host's 10-second logs over the final runs: 72 bytes/s
(props at rest, every 0.5 s) to 3.4 KB/s with two players, 5.95 KB/s to each
of two clients in the busiest 10 s (three cars knocking 14 props). The ring
bounds a message at 40 slots (a flying piece 26 bytes, about 1 KB); knocks
are 31 bits each in reliable events (250 per 1 KiB event). A big crash in the
test (a car through 60 props, 27 knocked) cost 5 KB/s for a few seconds, 425
bytes at most a message. The host sends the same message to every client: for
seven clients about 0.5-42 KB/s in these conditions, at most about 140 KB/s
in a crash that keeps all 40 slots flying.

## Tests

* `tests/net/test_prop_state.cpp`: the messages' round trips, sizes (a full
  ring in one datagram, 250 live and 500 catch-up knocks in one event) and
  malformed input; `test_fuzz.cpp`: both in the corpus and the decoders'
  checks, and sent mutated to a client.
* `tests/game/test_prop_sync.cpp`: a host and a client in one process through
  a lossy link: the host's knocks and pieces on the client, a client's knock
  predicted and handed over, one undone, other cars passing through a
  client's props, a client's own pieces not moving the host's, a replayed
  car meeting a prop as a real sample does, 30 % loss with reordering, a big
  crash, the catch-up, thrown car parts, knocks counted from the host only,
  refused input and a different placement, and on retail data the same
  placement on every machine (cruise at the host's and a client's traffic
  density, a checkpoint race with parked cars) for San Francisco and London.
* Full suite with game data: 964 of 964 (3 skipped as always); the opponent
  sweep 516/517 finished, 508/517 across the line; single-player smoke
  screenshots (a San Francisco cruise, a London circuit).

## For the players' cars agent

* A client's props accept contacts only from its own car and trailer
  (`ownCarBody`) and its own predicted pieces (`BangerSet::setReplica`).
* `World::beginReplay` (called by `CarPrediction::acknowledge`) starts a
  replay: a light prop the replayed car hits gives way for the rest of it.
* A replay through the sample in which the car knocked a prop meets the
  prop where it is now (knocked, flying away), not where it stood: the
  replayed car then keeps the speed the knock took off, until the next
  state. This is the largest remaining source of corrections after a prop
  hit among other cars (up to 0.4-0.5 m in the runs); replaying the knock
  itself needs the prediction's history to keep it.

## Open

* The client's prediction covers its own car only; a prop another player's
  car hits is shown when the host's knock comes, as that car is shown, a
  playout delay in the past.
* Props are not interest-managed: every client gets every knock and the
  whole ring; the ring's 40 slots bound it.
* The `_GLIBCXX_ASSERTIONS` suite: see the final report (the disk was full
  for a second build directory; the build was redone in place).
