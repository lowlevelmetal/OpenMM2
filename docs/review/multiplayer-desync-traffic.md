# Multiplayer desync review: shared traffic and police

Reviewed on 2026-10-09 from `main` at cd5ae29 (release 0.3.1), then on top
of integration at 3f73059 (the host simulates every player's car); a second
round on top of integration at 782f469 ("Round 2: protocol 12"). The
maintainer's report for this area: in a multiplayer cruise over the
Internet, "different cars, or the same cars in different places, on each
screen".

MM2's network cruise has no traffic and no police (`mmGameMulti::Init`), so
the shared traffic is OpenMM2's own design (docs/multiplayer.md, "Shared
traffic"; docs/parity/openmm2-only.md), and the choices below are OpenMM2
extras. Where MM2 has a comparable mechanism it predicts its network cars
forward from their last packet (`mmNetObject::PositionUpdate`); the traffic
now does the same.

During the work the maintainer decided that the host is the authority for
everything: clients upload inputs, the host simulates every player's car,
each client predicts its own car and reconciles. The traffic was already
host-run. The client side is built for that model (see "The
host-authoritative model"), and the last measurements are on top of it.

## Summary

Errors are on the ground plane, against the host's car at the session time
the client's car is at on the host's clock (what a player sees of a car next
to its own car, compared with the host's). Median / 90th / 99th percentile,
from `mm2tool nettrace`; "before" is 0.3.1, "present" the client's car in
the present with the players' cars as in 0.3.1 (217db24), "host-simulated"
the final build on top of integration (players' cars simulated by the host,
the client's car predicted about 95 ms ahead of the host's at 60 ms each
way).

| Measure | Before | Present | Host-simulated |
| --- | --- | --- | --- |
| Moving traffic car within 150 m, 60 ± 20 ms each way, 2 % loss | 2.61 / 3.70 / 4.37 m | 0.02 / 0.04 / 0.20 m | 0.03 / 0.12 / 0.61 m |
| ... 150 ± 30 ms, 5 % loss | 4.02 / 5.63 / 6.51 m | 0.02 / 0.09 / 0.62 m | 0.05 / 0.45 / 1.59 m |
| ... three players, the client at 40 ms | 2.44 / 3.91 / 6.74 m | 0.02 / 0.04 / 0.25 m | 0.03 / 0.11 / 0.55 m |
| ... three players, the client at 120 ms | 3.63 / 5.12 / 5.88 m | 0.02 / 0.08 / 0.45 m | 0.04 / 0.30 / 1.11 m |
| Moving police car chasing the client | 1.55 / 2.20 / 2.40 m | 0.06 / 0.19 / 0.78 m | 0.33 / 0.77 / 3.66 m |
| A car the client's car hit, the second after (60 ms) | 0.08 / 0.37 / 1.12 m (rear-end runs) | 0.03 / 0.22 / 1.13 m | 0.23-0.73 / 0.35-2.61 m |
| ... 150 ms | 1.28 / 5.82 / 5.99 m | 0.02 / 0.16 / 0.84 m | 1.50 / 6.71 m (one hit) |
| A collision with a traffic car shows on the client | a round trip and a playout delay later | at once | at once, the host simulating the same hit (6 ms apart) |
| Cars within 130 m on one machine only (density 0.5) | 0.2-0.6 % | 0.2-0.4 % | 0.2-0.7 % |
| Density 1: cars within 200 m missing on the client / cars coming and going | 12 % / 45 in 100 s | 1.4 % / 190 | 15 % / 1 |
| Bandwidth to a client, density 0.5 (60-70 cars) | 13.4-15.6 KB/s | 11.0-11.5 KB/s | 10.5-11.6 KB/s |
| ... density 1 | 18.8-21.1 KB/s (96 cars) | 20.2-21.8 KB/s (98-120) | 19.7-21.0 KB/s (100-128) |

The host-simulated column predicts further (the client's car's lead over the
host is added to the trip), so its tails are those of the longer horizon;
emulated before the merge with `OPENMM2_DEBUG_TRAFFIC_LEAD_MS`, 150 ms more
gave 0.03 / 0.15 / 0.63 m and 300 ms more 0.05 / 0.49 / 1.69 m.

## Round 2: protocol 12

On top of integration at 782f469 (protocol 10), with the maintainer's leave
to spend more bandwidth: the first round's open O1-O3 fixed, and what the
measurements turned up on the way. Same relays, scripted driving and tool
as the first round ("How it was measured"), plus a second police chase
(post 1, 30 m) and two scripts in the review's scratch work: the police
error by distance from the client's car, and the client's corrections above
10 cm with a shared car within 12 m of its car (the ones the traffic can
cause; respawns left out). The chases and pile-ups differ from run to run
(how long a police car follows, how many cars get knocked), so each row is
the range over the runs (one to four per case); the baseline is 782f469
built from the same sources.

| Measure (median / 90th / 99th percentile) | 782f469 | Protocol 12 |
| --- | --- | --- |
| Moving police car within 10 m of the client's car, 60 ± 20 ms each way | 0.26-0.38 / 0.98-1.03 / 1.35-1.62 m | 0.06-0.14 / 0.16-0.73 / 0.22-1.02 m |
| Moving police car, any distance | 0.24-0.33 / 0.58-0.84 / 0.98-2.82 m | 0.10-0.15 / 0.24-0.77 / 0.33-2.64 m |
| A car the client's car hit, the second after | 0.07-0.27 / 0.26-0.90 / 0.33-4.44 m | 0.00-0.04 / 0.02-0.24 / 0.14-0.91 m |
| A knocked car the client simulates | 0.11-1.46 / 0.16-2.18 / 0.29-2.49 m | 0.00 / 0.01-0.19 / 0.06-1.17 m |
| ... 150 ± 30 ms, 5 % loss | 0.73-1.35 / 1.51-1.67 / 1.74-1.96 m | 0.00-0.08 / 0.00-0.08 / 0.13-1.40 m |
| A car the client's car hit, 150 ms (1-2 hits a run; one run 300) | 0.57-0.89 / 1.30-1.31 / 1.45-1.60 m | 0.00-0.09 / 0.00-5.47 / 0.04-7.09 m (see O9) |
| Client's corrections above 10 cm with a shared car within 12 m: ramming the police (post 0) | 28 a minute | 2-3 |
| ... a chase from post 1 (the police car pins the client's car) | 12 | 11-20 |
| ... rear-ending traffic, put back every 4 s | 43 | 3-8 |
| ... driving through traffic | 16 | 0-3 |
| ... 150 ms, 5 % loss | 6 | 1-4 |
| Density 1: the host's cars within 200 m missing on the client | 7.9 % | 0.6 % |
| Moving rail car within 150 m, 60 ms (unchanged in kind) | 0.03 / 0.18-0.23 / 0.89-3.91 m | 0.03 / 0.11-0.24 / 0.65-4.00 m |
| Bandwidth to a client, density 0.5 | 6.2-15.7 KB/s | 6.9-15.3 KB/s, and 0.4-11.2 KB/s in full |
| ... density 1 | 19.4-20.4 KB/s (103 cars a message) | 22.5-26.9 KB/s (99-130), and 0-1.4 KB/s in full |

### Fixed

| # | Severity | Location | Scenario | Confirmed | Fix |
| --- | --- | --- | --- | --- | --- |
| T10 (O1) | visible | `RaceScreen::updateNetCops`, `TrafficPrediction::predictBody` | The police were kinematic bodies on a client, dead-reckoned along their velocity and yaw rate over the trip and the client's lead (about 200 ms at 60 ms each way): a moving police car within 10 m of the client's car was 0.38 / 1.03 / 1.62 m from the host's, and the client's car, meeting it there with infinite mass, was corrected 28 times a minute by more than 10 cm in the ramming runs. | traces | 184c739, baa3c01, 92628b2: the host sends each client, with its car's state, the police within 60 m in full (a chasing one 60-100 m behind, simulated, strayed more than predicted along its velocity: 2.2 m at the 90th percentile against 0.7 m) (`TrafficFull`, `net/TrafficFull.h`): its whole simulation and its driver's controls averaged over the frames since the last state (the driver's throttle and brake go on and off from frame to frame; the average met the host's car 0.29 m off at the 90th percentile in a chase, the last controls 0.49 m). The client puts it in at that sample's acknowledgement and runs it again with its own car (`predictNetTraffic`, a `CarPrediction::Companion` without a player) on those controls until the next. The host's police draw their wheels' bump numbers from their own streams, which travel with them. A simulated police car knocks the traffic it hits loose, as the host's does. |
| T11 (O2) | visible | `TrafficProxies`, `NetTrafficCars` | The cars the host knocked loose were kinematic on a client (infinite mass), dead-reckoned while they tumbled: the client's car bounced off them where the host's pushed them (0.73 / 1.51 / 1.96 m off at 150 ms each way), and was corrected. | traces | 184c739, baa3c01: the knocked cars within 50 m in full (the rigid body, what a sample hands the next, the wheels and the sleep state: `TrafficBodies::bodyState`); the client gives each a body (`setBodyState`, `NetTrafficCars`' host bodies) and runs it with its car as the police. Test `TrafficBodies.ABodyPutToAStateMovesOnAlike`: a body put back to a state moves on bit for bit as it did. |
| T12 (O3) | minor | `TrafficHost::build`, `net/AmbientState.h` | At traffic density 1, 7.9 % of the host's cars within 200 m of the client's were missing there: they did not fit in 1100 bytes. | density-1 traces | 184c739: up to 2600 bytes and 320 cars a message, in two of ENet's fragments sent unreliably (`ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT` on the Ambient channel; ENet sends an unreliable packet's fragments reliably otherwise); full rate within 120 m, every fourth state beyond 180 m. Test `TrafficHost.EveryCarWithin200MetresFitsAtDensityOne`. |
| T13 | minor | `RaceScreen::sendNetTraffic` (police) | A police car pressed against a wall kept 15 m/s in its velocity for 10 s while it stood; the client predicted it 3.9 m ahead (T6 for the police). | trace | baa3c01: shared standing once it has moved less than 5 cm in 100 ms (`game::StillBodies`), as the knocked cars. |
| T14 | visible | `World::replaySample`, `TrafficBodies::RailCar` | In the samples a client runs again after a correction its car met the rail cars as cars of infinite mass (`collideHeld`), where the first run knocked them loose. | by reading | baa3c01: a rail car gives the body it would take (`heldInertia`: its mass, moving along its rail), which the replayed car pushes for the rest of the replay. |
| T15 | visible | `NetTrafficCars`, `TrafficProxies` | A car standing off its rail without a body on the host (at rest after a knock) met the client's car as a kinematic instance of infinite mass, where on the host it is a rail car that takes a body when hit: ramming a pile of them the client's car was corrected by 1.6 m. | trace | baa3c01: it stands as a rail car at rest on the client, which its car knocks loose (the knock counts as confirmed at once). Test `NetTrafficCars.ACarStandingOffItsRailIsKnockedLooseAsOnTheHost`. |
| T16 | visible | `TrafficBodies::bodyState`, `RaceScreen::sendNetTrafficFull` | A knocked car the host's physics no longer ran (inferred: its table of 32 movers full, where MM2 leaves a body as it is; the car stood still, awake, for 90 s) went to the client in full with the 10 m/s its body had when it froze; the client ran it, 1.8 m from the host's for 90 s. | trace | aeb303a, 4cb28e1: only bodies and police cars the host's last step ran are sent in full. |

### Measured and left as they are (round 2)

* **Corrections without a shared car near.** At density 1 two thirds of the
  client's corrections above 10 cm had no traffic or police car within
  12 m (props and the city; other areas).
* **Respawns.** After a respawn the client's car lacks the traffic of its
  new place for a trip (92 % of the cars missing in the rear-end runs, which
  put the car back every 4 s, were in the 500 ms after a respawn).

## How it was measured

* A host and one or two clients of a San Francisco cruise on this machine,
  each with its own XDG directories, `[Display] VSync=off`, `FrameCap=120`,
  UDP port 2390, each client behind its own `netprobe relay` (60 ± 20 ms each
  way and 2 % loss with reordering; the harsh case 150 ± 30 ms and 5 %; three
  players with 40 and 120 ms each way). Scripted driving
  (`OPENMM2_DEBUG_INPUT`, `OPENMM2_DEBUG_SPAWN`,
  `OPENMM2_DEBUG_START_NEAR_POLICE`, `OPENMM2_DEBUG_RESPAWN_MS`): through
  downtown traffic, weaving into it, rear-ending cars again and again (full
  throttle, put back every 4 s), ramming a police car and fleeing it, and at
  traffic density 1. 100 s of each, about 12 300 client frames.
* `OPENMM2_NET_TRACE` writes the shared traffic (`game/net/TrafficTrace.h`):
  the host each car near a player at the session time its state belongs to,
  a client each car where its car meets it every frame and its car's time on
  the host's clock, the knocks and the hand-overs. `mm2tool nettrace <host
  trace> <client trace> [radius]` (added) compares them.
* `tests/game/test_traffic_prediction.cpp`
  (`TrafficPrediction.OverALossyLinkTheClientSeesTheHostsPresent`) reproduces
  the measurement without game data: a host's AI steps, its messages over a
  simulated 60 ± 20 ms link with 2 % loss, the client's cars against the true
  paths (prints 0.022 m median predicted against 1.29 m 160 ms back).
* The previous record of this area measured the client's cars against the
  host's messages at the same stamps (2 cm median): that hides both the
  playout delay (T1) and the stamps' own error (T2).

## Findings

Severity: crash / gameplay-breaking / visible / minor / code quality.
Locations are at cd5ae29 unless said otherwise.

### Fixed

| # | Severity | Location | Scenario | Confirmed | Fix |
| --- | --- | --- | --- | --- | --- |
| T1 | visible | `RaceScreen.cpp:4110` (`updateNetTraffic`) | A client drew the traffic and police a trip and a playout delay in the past (160-260 ms over the relays) while its own car was in the present, and its car met them there: a moving traffic car was 2.6 m (median, 60 ms each way) to 4.0 m (150 ms) from where the host had it at the same moment, and a client rear-ending a car hit it where it had been. | traces (165-264 ms at their speed); test `TrafficPrediction.OverALossyLinkTheClientSeesTheHostsPresent` | 2c8679c: every car shown at the client's car's time, predicted from its newest state (rail cars along an arc at their speed and acceleration, bodies along their velocity and yaw rate); collisions take the newest prediction, the drawing blends corrections away (100 ms) |
| T2 | visible | `RaceScreen.cpp:3998` (`sendNetTraffic`) | Messages were stamped with the session time they were sent at, while the rail cars were at the AI's last 30 Hz step and the police and knocked cars at the last physics step: every car was 17 ms (median, 8-26 ms) behind on the client, by a different amount in each message (a surge of up to 0.5 m at speed), and the light sets ran on the same wrong time. | traces: 17.2 ms behind along the rail; after, -0.1 ms | 2c8679c: stamped with the AI step's time, the bodies moved to it along their velocity |
| T3 | visible | `game/net/TrafficProxies.cpp:19`, `RaceScreen.cpp:3877` | A client's car met the received rail cars as kinematic instances of infinite mass: it bounced off a car as off a moving wall, where the host's (and single player's) car pushes a rail car that takes a body (`aiVehicleInstance::AttachEntity`, `aiVehicleActive`); the traffic car drove on through the client's car until the host's knock came back. | rear-end traces; tests `NetTrafficCars.*` | e6802c2: the client's `TrafficBodies` holds the received rail cars (`NetTrafficCars`); its car knocks one loose at once with the host's physics. df5c7cf, 438fd01, b01172e: the local body leads until it rests (the host's knocked car, a trip old and dead-reckoned while it tumbles, is the worse guess), a car it knocks knocks the next one loose as on the host, and it is handed back 3 m apart |
| T4 | visible | `ai/Traffic.cpp:2617` (`publish`), `TrafficSync.cpp:373` | A rail car's velocity is its AI speed along its heading, but `ai::Traffic` moves it along its curves by their parameter: in turns the host's cars covered 70-80 % of the ground their speed said, and a car held at the end of its lane none while its speed stood at 10-23 m/s; predicted with that speed they ran ahead. | host traces, ground speed against speed | e6802c2: the host sends the speed over the ground where it differs (12 bits); the prediction uses it, a hit body still takes the AI's speed |
| T5 | minor | `TrafficSync.cpp:374` | A car the AI drives off its rail (avoiding a player, regaining its lane) was sent without spin, so a client could not follow its curve. | by reading | 2c8679c: its yaw rate |
| T6 | minor | `TrafficBodies` (host) | Once a knocked car's body stood still for 40 s with 6.8 m/s left in its velocity, and every client predicted it 1.2 m ahead. Not reproduced in nine later runs; the cause (a body resting against something with its velocity not cleared) is plausible only. | one trace | 8af6833: a body that moved less than 5 cm in 100 ms is shared standing |
| T7 | visible | `TrafficSync.cpp:411-430` (`TrafficHost::build`), `net/AmbientState.h:39` | At traffic density 1 the cars within 200 m did not fit (at most 96 a message): 12 % of the host's cars within 200 m were missing on the client, and cars at the cut came and went (45 times in 100 s, at 170-195 m). | density-1 traces; test `TrafficHost.TheCarsThatFitStayAndTheFarthestComeEveryFourthMessage` | 2a97bb0: up to 160 cars, a known car's state every fourth message beyond 160 m, the client's cars 25 m nearer at the cut |
| T8 | visible | `TrafficSync.cpp:417-424` | Whether a far car's turn for its state fell in a message changed its size from 13 bits to 130, and with it which cars at the edge fit; a car left out was gone and came back new, needing its state and pushing out others (nearly 2000 times in 100 s once more cars fitted). Near cars' changing sizes did the same at 190. | density-1 traces | 217db24: cars chosen by their usual cost, then written (a far car whose turn it is goes without its state when it would not fit); bb00328: a new car waits while the known ones would leave less than a tenth of the budget. 1 in 100 s after |
| T9 | gameplay-breaking (integration) | `RaceScreen.cpp` `sendNetTraffic` at 3f73059 | With the host simulating the players' cars the clients send no snapshots, and the host chose each client's traffic round the snapshot it no longer had: no traffic was sent at all. | by reading (`NetGame::remoteCars` has no state without them) | 753c12b: round the host's simulated car of each client (`m_remoteCars`) |

### Measured and left as they are

* **Spawns and despawns at the interest edges.** The cars near the client
  that only one machine had are cars the host populated or recycled on the
  roads round the player (the AI's `AdjustAmbients`) in the time a message
  takes: 0.2-0.7 % of the car-frames within 130 m, now one trip late rather
  than a trip and a playout delay. The 200 m / 230 m hysteresis keeps cars at
  the edge from coming and going.
* **Recycled slots, lost and reordered messages.** Correct as they were: a
  new generation is a new car, an older message never brings a car back, and
  the prediction starts from the newest state, so a lost message only
  lengthens its horizon.
* **Jumps the host's AI makes itself.** Cars that finish regaining their
  lane, and some far from every player, are put 10-35 m away in one AI step
  on the host; the client follows a trip later. AI behaviour, not compared
  with MM2 here.
* **Police chases.** The target travels with every message; the client and
  the host agreed on who was chased in all but 11 of 7 240 car-frames (one
  switch, a trip late). The sirens and pursuit flags likewise.
* **Hit reports** (gone with T9's merge): of 1, 2, 15 and 14 reports in the
  baseline runs 0, 1, 2 and 5 were applied; the rest were refused because the
  host's delayed copy of the client's car had already knocked the car,
  later and elsewhere.

## The host-authoritative model

On top of integration the client's car is predicted ahead of the host's
simulation of it, while the traffic arrives a trip old:

* **Present the traffic at the car's time on the host's clock.** The client
  measures its car's lead over the host from the host's states of it (the
  host's session time after sample n against the time it ran sample n,
  smoothed; about 95 ms at 60 ms each way: the trip and a sample or two in the
  host's queue) and shows the received cars at its car's time plus that lead
  (`RaceScreen::netTrafficCarTime`), so its car meets them where the host's
  simulation of it will.
* **Collide as the host does.** The client's car meets the received rail
  cars in `TrafficBodies` as the host's simulation of it does: a hit gives the
  car a body and both push. The client's knock and the host's were 6 ms
  apart in the traces; where the client's car had met the car as the host's
  did, the knocked car moved 0.03-0.6 m (median) from the host's. Where they
  met it differently (the client's car then corrected by 1.5-2.3 m) it
  drifted 2-4 m, and is handed back at 3 m.
* **Correct quickly.** Collisions take each newer prediction at once; the
  drawing blends corrections away in 100 ms (above 4 m it jumps); a knock the
  host makes too is handed back when the local body rests (or 3 m apart), one
  it does not make 150 ms after the hit on the host's clock.
* **Replays.** A client's replayed samples meet everything held still
  (`World::replaySample`): its rail cars stand where this frame placed them
  and are not knocked. `TrafficClient::poseAt` gives a car's pose at any
  time, should replays want them where they were.
* **What went.** The hit reports (`TrafficHitEvent`, `applyNetTrafficHits`,
  `TrafficBodies::knock`, the reports in `updateNetTraffic`; protocol 6); the
  police and the traffic on the host see each client's simulated car.

## Bandwidth

A car on its rail costs 120 bits (136 turning or changing speed, 148 when
its ground speed differs), as many as 2600 bytes hold (at most 320; 1100 and
160 before protocol 12) per client and message, 20 a second. At the
cruise's default density (0.5) 12.5-15.3 KB/s to each client (60-70 cars);
at density 1 22.5-26.9 KB/s (100-130 cars, every car within 200 m). The
full states add one datagram of at most 1200 bytes a state: 0.4-5.4 KB/s
driving through traffic, 6.6-11.2 KB/s with a police car near. The worst
case per client is 76 KB/s (2600 + 1200 bytes 20 times a second), on top of
the players' cars; seven clients at density 1 with police near about
250 KB/s from the host.

## Open

O1-O3 of the first round are fixed in the second (T10-T12).

| # | Severity | Location | What | Needs |
| --- | --- | --- | --- | --- |
| O4 | minor | `ai::Traffic` (host) | The AI's own jumps of 10-35 m in one step (see "Measured and left as they are"). | The AI area. |
| O5 | minor | `TrafficBodies` (host) | T6's cause. | A reproduction. |
| O6 | minor | `RaceScreen::predictNetTraffic` | A simulated police car runs on its driver's averaged controls over the client's lead (about 200 ms at 60 ms each way); the driver's turns and brakes in that time, and what `aiVehiclePhysics` does to the car's body besides its controls (backing up turns its matrix, a wreck's momentum is damped), are not replayed: chasing 20-60 m away it was up to 1-2 m off at the 99th percentile, as dead reckoning was, and once a police car manoeuvring against the traffic stood 1 m off for 40 s. Within 10 m, where the cars meet, 0.06-0.14 m (median). | The police driver run on the client too (its map, targets and obstacles), or its body changes sent. |
| O7 | minor | `RaceScreen::predictNearCars` (the players' cars area) | The other players' cars a client simulates (`RemoteVehicle::predicted`) meet the rail cars as cars of infinite mass in its real samples: they cannot knock one loose there, where the host's simulation of them does. | `body.player` for them, as for the simulated police. |
| O8 | minor | `NetTrafficCars`, `TrafficBodies` (host) | A knocked car the client's car leans on goes to sleep on the host and is knocked loose again when pushed, every second or so; the client follows (a body while the full states come, a standing car between), its drawing jumping a few centimetres each time. | Nothing unless it shows. |
| O9 | minor | `NetTrafficCars` (the first round's local knock) | At 150 ms each way a car the client's car knocked loose was 3-7 m from the host's in the second after in two runs of three (one hit each; 4 cm at the 99th percentile with 300 hits in the third), then handed back: the client's car, predicted 300 ms ahead, met it otherwise than the host's simulation of it did. Before, 1.3-1.6 m in such runs. | More runs; a shorter lead for the local body at long trips. |

## Edits outside this area's files

`src/app/RaceScreen.cpp` (the hooks: tracing, stamping, rail motion,
present time and lead, the client's `TrafficBodies`, removed reports),
`src/game/TrafficBodies.{h,cpp}` (the `Source` interface: the AI's calls
unchanged, the opponent sweep 516/517 and 508/517; `bodiesHitAsPlayer`;
`knock` and `massOf` removed), `src/game/Interpolation.{h,cpp}` (a
`recordTrafficBodies` for the client's), `src/game/net/NetGame.{h,cpp}`
(`traceFile`), `src/net/AmbientState.h`, `src/net/Protocol.h` (6),
`tools/mm2tool/cmd_nettrace.cpp`. New: `game/net/TrafficPrediction`,
`NetTrafficCars`, `TrafficTrace` (parity class O).

Round 2: `src/game/net/PlayerCars.{h,cpp}` (the players' cars area:
`CarPrediction::Companion` takes a car without a player, driven by a
function, or a body that is no car, put to its state by a function; the
players' cars go through it unchanged), `src/net/{Protocol,Session,
Transport}`, `src/game/net/NetGame` (`TrafficFull`, 12, the Ambient
channel's unreliable fragments), `src/game/TrafficBodies.{h,cpp}` (a body's
state in and out, a rail car's mass in replays), `src/app/RaceScreen.cpp`
(the host's full states and police controls, the client's simulated police
and bodies). New: `src/net/TrafficFull.h` (parity class O). The single-player
game is unchanged: the opponent sweep gives integration's 515/517 and
507/517 (b33bebc).
