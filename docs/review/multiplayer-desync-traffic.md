# Multiplayer desync review: shared traffic and police

Reviewed on 2026-10-09 from `main` at cd5ae29 (release 0.3.1). The
maintainer's report for this area: in a multiplayer cruise over the
Internet, "different cars, or the same cars in different places, on each
screen".

MM2's network cruise has no traffic and no police (`mmGameMulti::Init`), so
the shared traffic is OpenMM2's own design (docs/multiplayer.md, "Shared
traffic"; docs/parity/openmm2-only.md). The choices below are recorded as
OpenMM2 extras. Where MM2 has a comparable mechanism, it predicts its
network cars forward from their last packet (`mmNetObject::PositionUpdate`);
the traffic now does the same.

During the work the maintainer decided that the host is to be the authority
for everything (players' cars included: clients upload inputs, the host
simulates every car, each client predicts its own and reconciles). The
traffic was already host-run; the client side below is designed for that
model too (see "The host-authoritative model").

## Summary

| Measure (moving traffic cars within 150 m of the client's car) | Before | After |
| --- | --- | --- |
| Error against the host at the same moment, 60 ± 20 ms each way, 2 % loss | TBD | TBD |
| ... 150 ± 30 ms, 5 % loss | TBD | TBD |
| Police in a chase | TBD | TBD |
| Cars near the client on one machine only | TBD | TBD |
| A collision with a traffic car shows on the client | a round trip and a playout delay later | at once |
| Bandwidth per client (traffic density 0.5) | TBD | TBD |

## How it was measured

* A host and one or two clients of a San Francisco cruise on this machine,
  each with its own XDG directories, `[Display] VSync=off`, `FrameCap=120`,
  UDP port 2390, each client behind its own `netprobe relay` (60 ± 20 ms each
  way and 2 % loss with reordering; the harsh case 150 ± 30 ms and 5 %; three
  players with 40 and 120 ms). Scripted driving (`OPENMM2_DEBUG_INPUT`,
  `OPENMM2_DEBUG_SPAWN`, `OPENMM2_DEBUG_START_NEAR_POLICE`,
  `OPENMM2_DEBUG_RESPAWN_MS`): through downtown traffic, weaving into it,
  rear-ending cars again and again, ramming a police car and fleeing it, and at
  traffic density 1.
* `OPENMM2_NET_TRACE` now writes the shared traffic (`game/net/TrafficTrace.h`):
  the host each car near a player at the session time its state belongs to,
  a client each car where its car meets it every frame and its own car's
  session time, and the hits, knocks and hand-overs. `mm2tool nettrace
  <host trace> <client trace>` (added) compares them. The error is measured on
  the ground plane, against the host's car at the session time the client's
  car was at: what one player sees of a car next to the other player's car.
* `tests/game/test_traffic_prediction.cpp`
  (`TrafficPrediction.OverALossyLinkTheClientSeesTheHostsPresent`)
  reproduces the measurement without game data: a host's AI steps, its
  messages over a simulated 60 ± 20 ms link with 2 % loss, and the client's
  cars against the true paths.

## Findings

Severity: crash / gameplay-breaking / visible / minor / code quality.
Locations are at cd5ae29.

### Fixed

| # | Severity | Location | Scenario | Confirmed | Fix |
| --- | --- | --- | --- | --- | --- |
| T1 | visible | `RaceScreen.cpp:4110` (`updateNetTraffic`) | A client drew the traffic and police a trip and a playout delay in the past (at least the host car's playout delay, 100 ms or more; 160-260 ms over the relays) while its own car was in the present, and its car met them there. Next to the other player's car a moving traffic car was 2.6 m (median, 60 ms each way) to 4.0 m (150 ms) from where the host had it at the same moment; a client rear-ending a car hit it where it had been. | traces: error in session time 165-264 ms at their speed; test `TrafficPrediction.OverALossyLinkTheClientSeesTheHostsPresent` (1.29 m median 160 ms back on a synthetic link) | 2c8679c: every car shown at the session time of the client's car, predicted from its newest state (rail cars along an arc, bodies along their velocity and yaw rate); the drawing blends corrections away |
| T2 | visible | `RaceScreen.cpp:3998` (`sendNetTraffic`) | The messages were stamped with the session time when they were sent, while the rail cars were at the AI's last 30 Hz step (up to a step and a frame earlier) and the police and knocked cars at the last physics step: every car was drawn 17 ms (median, 8-26 ms) behind on the client, the offset changing from message to message (a surge of up to 0.5 m at speed), and the light sets ran on the same wrong time. | traces: client behind along the rail by 17.2 ms (median) at the time it showed the car; after, -0.1 ms | 2c8679c: stamped with the AI step's time, the bodies moved to it along their velocity |
| T3 | visible | `game/net/TrafficProxies.cpp:19`, `RaceScreen.cpp:3877` | A client's car met the received rail cars as kinematic instances of infinite mass: it bounced off a car as off a moving wall, where the host's (and single player's) car pushes a rail car that takes a body (`aiVehicleInstance::AttachEntity`, `aiVehicleActive`); the traffic car drove on through the client's car until the host's knock came back, a round trip later, from wherever the host had knocked it. | traces of the rear-end runs; test `NetTrafficCars.*` | e6802c2: the client's `TrafficBodies` holds the received rail cars (`NetTrafficCars`); its own car knocks one loose at once with the host's physics, and the host's messages confirm or withdraw it |
| T4 | visible | `ai/Traffic.cpp:2617` (`publish`), `TrafficSync.cpp:373` | A rail car's published velocity is its AI speed along its heading, but `ai::Traffic` moves it along its curves by their parameter: in a turn the host's car covered 70-80 % of the ground its speed said, and a car held at the end of its lane covered none while its speed stood at 10-23 m/s. Predicted with that speed, such cars ran ahead of the host's. | host traces: ground speed against speed for every car (scratch analysis) | e6802c2: the host sends the speed over the ground where it differs; the prediction uses it, the velocity a hit gives the body stays the AI's |
| T5 | minor | `TrafficSync.cpp:374` | A car the AI drives off its rail (avoiding a player, regaining its lane) was sent without spin, so a client could not follow its curve. | by reading | 2c8679c: its yaw rate (curvature × speed) |
| T7 | visible | `TrafficSync.cpp:411-430` (`TrafficHost::build`), `net/AmbientState.h:39` | At traffic density 1 the cars within 200 m did not all fit in a message (at most 96, 1100 bytes): 12 % of the host's cars within 200 m of the client were missing on the client, and cars at the cut came and went (45 times in 100 s at 170-195 m) as the order changed. | traces at density 1; test `TrafficHost.TheCarsThatFitStayAndTheFarthestComeEveryFourthMessage` | 2a97bb0: up to 160 cars, a known far car's state every fourth message beyond 160 m, and the cars the client has count as 25 m nearer at the cut |
| T8 | visible | `TrafficSync.cpp:417-424` | Whether a far car's turn for its state fell in a message changed its size from 13 bits to 130, and with it which cars at the edge still fit; a car left out was gone for the client and came back new, needing its state and pushing out others. With more cars fitting (T7) this made the edge flicker nearly 2000 times in 100 s. | traces at density 1 | 217db24: the cars are chosen by their usual cost, then written, a far car whose turn it is going without its state when it would not fit |
| T6 | minor | `TrafficBodies` (host) | Once a knocked car's body stood still on the host for 40 s with 6.8 m/s left in its velocity; every client predicted it 1.2 m ahead all that time. Not reproduced in nine later runs; the cause (a body resting against something while its velocity is not cleared) is plausible only. | one trace | 8af6833: a body that moved less than 5 cm in 100 ms is shared standing |

### Measured and left as they are

* **Spawns and despawns at the interest edges.** The cars near the client
  that only one machine had are cars the host populated or recycled on the
  roads round the player (the AI's `AdjustAmbients`, MM2's behaviour) in the
  time it takes a message to arrive: 0.1-0.6 % of the car-frames within
  130 m before, 0.1-0.3 % after (now one trip late instead of a trip and a
  playout delay). The 200 m / 230 m hysteresis already keeps cars at the edge
  from coming and going; the 1100-byte budget was never full (at most 75
  cars, 770 bytes a message at traffic density 0.5, TBD at 1).
* **Recycled slots, lost and reordered messages.** Unchanged and correct: a
  new generation is a new car; an older message never brings a car back; the
  prediction starts from the newest state, so a lost message only lengthens
  its horizon (by 50 ms, 100 ms for a far car between its states).
* **Jumps the host's AI makes itself.** Rail cars that finish regaining
  their lane, and some far from every player, are put 10-35 m away in one AI
  step on the host; the client follows a trip later. This is the AI's
  behaviour (not compared with MM2 here), not a desync.
* **Hit reports** (`RaceScreen.cpp:3877-3913`, `:4078-4101`): of 1, 2, 15 and
  14 reports in the four baseline runs 0, 1, 2 and 5 were applied; the others
  were refused because the host had knocked the car itself already (its
  delayed copy of the client's car reaching the car later). The once-a-second
  limit kept a car the client was stuck against from being reported more
  often. With the host simulating every player's car (below) the reports go,
  so they were not changed beyond the client now reporting its own knocks.
* **The host's view of the clients for the police and the traffic's
  avoidance** (`RaceScreen.cpp:1661`, `collectNetPlayers`): the host's
  delayed copy of each client's car. Superseded by the host simulating every
  car (the police and the traffic then see each car where it is on the host).
* **Police chases.** The police target travels with every message; the
  client never disagreed with the host on who was chased (0 of 12 259
  car-frames in the chase). Their places: see the summary.

## The host-authoritative model

With the host simulating every player's car from uploaded inputs, a client's
own car is predicted ahead of the host (by about half a round trip and the
host's input buffer), while the traffic still arrives a trip old. The client
side is built for that:

* **Present the traffic at the predicted car's time.** The client shows the
  received cars at `RaceScreen::netTrafficPresentTime`, today its car's
  session time (`netTrafficOwnTime`). With a predicted car it must return the
  session time that car is at, the time the host will simulate the inputs
  being applied now; the cars are then predicted that much further.
  `OPENMM2_DEBUG_TRAFFIC_LEAD_MS` emulates the longer horizon today: TBD.
* **Collide as the host does.** The client's car meets the received rail
  cars in `TrafficBodies`, as the host's car meets its own: a hit gives the
  rail car a body with its rail velocity and the two push each other, so the
  client's predicted car takes the host's response when their states agree,
  and the knocked car moves off at once. With the host simulating the hit,
  the host's knock is in the message stamped at the hit's time, so the window
  after which the client withdraws a knock the host did not make should drop
  the half round trip (`kNetKnockConfirmMs` alone, `NetTrafficCars::update`'s
  `confirmMs`).
* **Correct quickly.** The collisions take each newer prediction at once;
  the drawing blends the difference away in 100 ms (above 4 m or a radian it
  jumps); a knock the host confirms or withdraws is handed back the same way.
* **Replays.** A client that rewinds its car to a host state and replays its
  inputs can place the received cars at each replayed step's time with
  `TrafficClient::poseAt` (the physics pose, without the drawing's
  correction). The cars it knocked loose itself are not rewound.
* **What goes.** The hit reports (`TrafficHitEvent`, `applyNetTrafficHits`,
  the report block of `updateNetTraffic`): the host sees every hit itself.
  The host's police and traffic then see each player's car where the host
  simulates it (`collectNetPlayers` reads `m_remotes`' cars).

Where this depends on the players' cars work: the present time (above), the
confirmation window, the replays, and taking the hit reports out.

## Bandwidth

TBD

## Open

TBD
