# Multiplayer desync: the players' cars

Reviewed and reworked on 2026-10-09 from 0.3.1 (cd5ae29).

The maintainer's report, for multiplayer cruise over the internet with
everyone on 0.3.1: "another player's car is somewhere other than where they
are, teleports, or a collision happens on one screen but not the other".
While this work was under way the maintainer decided that the host is the
authority for everything ("I think the server needs to be the authority of
everything"), which replaced porting MM2's peer-to-peer network cars: the
host now simulates every player's car from the players' inputs, each client
predicts its own car and is corrected by the host's states, and the host
decides every car's damage and Cops and Robbers' limits. MM2's model was
read first and is recorded below, because it explains the original game's
behaviour and what OpenMM2 does differently.

## How it was measured

* Host and one or two clients on one machine (UDP port 2380, own config and
  data directories, `[Display] VSync=off`, `FrameCap=120`), each client
  through `netprobe relay` (60 ± 20 ms each way, 2% loss, reordering; the
  harsher case 150 ± 20 ms and 5% loss), a multiplayer cruise in San
  Francisco (the shared traffic off, and once on), 75 s of race.
* `OPENMM2_DEBUG_INPUT` drives every car; `OPENMM2_DEBUG_START` places them.
  Layouts: **ram** (host and client face each other 26 m apart on the hill
  at intersection 150 and ram, back off and ram again; with three players
  the second client starts behind the host), **chase** (the client starts
  10 m behind and 3 m beside the host and they weave), **rear** (the client
  starts 12 m behind the host in the same lane at full throttle and keeps
  shunting it while the host slows and brakes).
* `OPENMM2_NET_TRACE` (added: `D`, `K`, `C` lines, see docs/multiplayer.md
  "Diagnosing replication") on every machine and `netprobe syncreport`
  (added) on the traces: **screen divergence** is where a machine draws a
  player's car against where that player's own screen draws it at the same
  moment of the machine's monotonic clock (shared by the processes on one
  computer); **jumps** are frames in which a drawn car moves more than its
  velocity explains; **collisions** are each machine's collision episodes
  between two players' cars (impacts of a pair less than 500 ms apart),
  whether the machines that report the pair had one within 750 ms and 4 m,
  and how close every machine drew the two cars within 0.6 s of it;
  **corrections** are a client's own car moved by the host's states.
* The 0.3.1 numbers come from 0.3.1 with only the trace added (80b8b8e and
  the clock fix below), built separately; the "host authority" numbers from
  c367288 (merged with the shared traffic's work, 52aa601), release builds.
* A crash is chaotic: the same scenario run again moves by tens of percent
  (rear: 543, 786 and 423 corrections a minute in three runs, the second
  while the opponent sweep was loading the machine). The tables show single
  runs; where a scenario was run twice both are given.
* A jump's velocity for a machine's own car is its body's frame velocity,
  which includes the sample's pushes out of contacts.

A first round of "before" numbers was wrong: the trace's clock was each
process's own (`net::monotonicMs` counts from the process's start), so
traces of instances started seconds apart compared moments seconds apart
and showed divergences of 5-30 m. The divergence lines now use the
machine's monotonic clock.

The scenario scripts are kept with the trace format: the commands are in
"Reproducing" below.

## MM2's network cars

Read from MM2Recomp (`mmNetObject`, `mmGameMulti`, `mmMultiRoam`, the
decompile and the asm of `SendPosition` and `PositionUpdate`):

* **What is sent** (`mmNetObject::SetPositionData`, message 0x1f5, 68
  bytes): the car's time, steering, throttle and brake as bytes, the
  manual gear, the orientation as Euler angles, the position as three
  floats, the acceleration (`mmAccelCompute`: the velocity's change per
  packet averaged over ten packets), the velocity and the spin each as a
  magnitude and three signed bytes of direction, the CurrentDamage, the
  score, flags (horn, siren, neutral, handbrake over 0.8, a manual gearbox
  with its gear, not drivable), a reset bit and a packet counter.
* **How often** (`mmGameMulti::SendPosition`, called from `UpdateGame`):
  to each other player separately, in the first frame more than 0.05 s
  after the last packet to that player (0.1 s for a player whose session
  data carry a flag `mmNetObject::Init` is given, inferred to be a slow
  link); a dial-up session sends one broadcast at its own interval.
* **Prediction and correction** (`mmNetObject::PositionUpdate`): a packet
  older than the newest is dropped. The network car takes the packet's
  velocity and momentum (a global switch, on), pedals, damage (clearing it
  when the level drops from above 1 to 0), horn, siren and gearbox. The
  average interval of the last ten packets, T, predicts the car forward:
  position + velocity T + ½ acceleration T², velocity + acceleration T, the
  orientation turned by the spin times T. With the reset bit, or more than
  10 m from the packet (`GetPositionApproach`, distances under 2 m count as
  0), the car is put at the packet at once. Otherwise it keeps its place,
  takes the packet's orientation, and gets a Hermite path (`mmNetPath`) from
  where it is to the predicted place, its tangents along the two forward
  axes; `mmNetObject::Predict`, every frame, puts the car on the path at
  the share of the average packet interval the frames since the packet
  make, when the car was further than its radius from the packet, and
  otherwise moves its place and orientation toward the predicted ones by
  the frame's seconds as a fraction (`Matrix34::Interpolate`).
* **A physical body** (`mmNetObject::Update`, `mmGameMulti::EnableRacers`):
  the network car is a `vehCar` of the level, declared every frame as a
  type-3 mover with flags 0x1b (it updates and collides with the city, the
  instances and the other movers), drivable from the start with its damage
  on. Between packets it drives itself on the last pedals and is pushed by
  whatever it touches; the next packet's path or snap pulls it back.
* **Two players colliding** therefore happens on every machine separately:
  each machine's own car hits the other's car as that machine shows it (a
  path behind the true car), both give way there, and each owner's next
  packets carry its own outcome to the others, whose copies are pulled or
  snapped to it.

## What diverged in 0.3.1, and why

| | 0.3.1 | cause |
| --- | --- | --- |
| A player's car on another screen | 0.2-0.9 m from its own screen at the median, 1-3 m at the 99th percentile (table below) | every remote car is drawn a playout delay in the past (50-500 ms, what its snapshots need to arrive in time) |
| Collisions on one screen only | ramming head-on: 1 of 4 and 2 of 8 of the host's collisions had none on the client; shunting from behind: 13 of 21 of the host's had none on the client, 6 of 15 of the client's none on the host, 3 of those drawn more than 5 m apart on the host | each machine resolves its own car against the other's past image (sync review S3), which never gives way (S2): the two machines have different collisions at different places and times |
| Teleports | none over 1 m in any run (the remote cars are interpolated, the own car never corrected) | (the maintainer's teleports were not reproduced on these links) |

## What changed

1. **Trace and report** (80b8b8e): the `D`/`K`/`C` trace lines and
   `netprobe syncreport`.
2. **Physics support** (48d1a70): `CarSim`/`Trailer` save and restore their
   whole state; `World::replaySample` runs one car (and its trailer) alone
   with everything else held still; `World::setSampleHooks` for per-sample
   inputs; `CarSim::ownRandom` (each player's car draws its wheels' bump
   numbers from its own stream on every machine).
3. **Protocol 5** (5f2f10d): `PlayerInput` and `CarStates`
   (net/PlayerCars.h), checked within ranges, with a budget.
4. **Game layer** (24dbd3d): `NetCarDriver`, `HostInputQueue`,
   `CarPrediction`, `CorrectionBlend` (game/net/PlayerCars).
5. **The race** (3f73059): the host simulates every client's car from its
   inputs; clients predict their own car, reconcile and draw the other cars
   interpolated from the host's states; resets travel as commands with the
   inputs; the host decides every car's damage.
6. **Replays meet the other cars where they were** (19e8271), then every
   body within 40 m of the car (c367288: the police, knocked traffic cars
   and props too), plus the `OPENMM2_NET_OTHERS` experiment. The traffic
   cars still on their rails are held where the frame placed them (the
   shared traffic's `TrafficClient::poseAt` could put them back too).
7. **Cops and Robbers' limits on the host** (276e508, sync review S6).
8. **Catch-up** (127ac41): a client's backlog (it loaded before the host)
   is dropped after a second instead of lagging its car by seconds.
9. Development hooks: `OPENMM2_DEBUG_NETCARS` (corrections and costs),
   `OPENMM2_DEBUG_NETCARS_NOISE` (921513d, 65733d3).

The design is in docs/multiplayer.md, "Players' cars".

## Before and after

Screen divergence (metres: median / 99th percentile; "A ← B" is A's screen
showing B's car):

| Scenario | view | 0.3.1 | host authority |
| --- | --- | --- | --- |
| ram | host ← client | 0.60 / 1.53 | 0.53 / 1.19 |
| ram | client ← host | 0.58 / 1.50 | 0.61 / 1.76 |
| chase | host ← client | 0.21 / 2.85 | 0.24 / 2.20 |
| chase | client ← host | 0.19 / 1.56 | 0.22 / 1.67 |
| ram, 150 ms, 5% | host ← client | 0.95 / 2.46 | 1.05 / 2.40 |
| ram, 150 ms, 5% | client ← host | 0.90 / 2.45 | 0.99 / 2.53 |
| ram, 3 players | host ← clients | 0.53-0.56 / 1.41-1.42 | 0.50-0.51 / 1.26-1.44 |
| ram, 3 players | clients ← host | 0.56 / 1.43 | 0.56-0.57 / 1.50-1.51 |
| ram, 3 players | client ← client | 0.80-0.85 / 2.08-2.12 | 0.99-1.04 / 2.66-2.98 |
| ram, shared traffic | host ← client | 0.49 / 1.47 | 0.58 / 1.37 |
| ram, shared traffic | client ← host | 0.58 / 1.44 | 0.59 / 1.61 |
| rear (two runs) | host ← client | 0.65 / 1.89 | 1.28-1.30 / 4.47-6.01 |
| rear (two runs) | client ← host | 0.31 / 1.05 | 0.69-1.01 / 2.46-2.53 |

The other players' cars are still drawn in the past (the maintainer's
choice: interpolated from the host's states), and a client's own car now
runs ahead of the host's by its inputs' trip and the host's margin, so the
distance between screens is about what it was. A client's view of another
client grew a little (that car's states now come through the host, a trip
older than its own snapshots were). Where one car shunts another the
client's own car shows where its prediction put it and the host corrects
it (below): that view grew most.

Collisions between players (host authority: the host's collisions are the
ones that happen; the client's are its prediction of them):

| Scenario | 0.3.1: one screen only | host authority: host's collisions the client predicted / client predictions the host did not have |
| --- | --- | --- |
| ram | 1 of 4 | 18 of 18 / 0 of 18 |
| ram, 150 ms, 5% | 2 of 8 | 18 of 18 / 0 of 18 |
| ram, 3 players | 1 of 9, 1 of 4 | 7 of 7, 6 of 7, 5 of 5 (host and client 1, host and client 2, the clients) / 1 of 8, 1 of 7, 0 of 5 |
| ram, shared traffic | 0 of 3 | 6 of 6 / 0 of 6 |
| rear (two runs) | 13 of 21 (host), 6 of 15 (client) | 12 of 12, 4 of 4 / 10 of 22, 4 of 8 |

Every collision now happens once, on the host, with both cars giving way by
their masses, and every machine shows its outcome: a client draws its own
car through the host's states and the others' from them. A collision the
client did not predict reaches its car as a correction a round trip later;
one it predicted that the host did not have (the other car was elsewhere by
then: shunting from behind against its past image) is corrected away.

Jumps over 1 m in a frame: none in 0.3.1; after, none for any other
player's car, and none for a client's own car except in the first rear
run: 81 frames in two spells of under a second, where the client's car,
corrected to the host's place right behind the host's car, sat 2 m inside
that car's past image and was pushed out of it every sample, shaking from
side to side by up to 1.5 m (6 of those were corrections over 4 m, drawn at
once; finding O1). An earlier build's harsh run had one real teleport: the
client's car fell through the city at the foot of the hill (about
z = 255) and the fall rule reset it on both machines at the same sample.

Corrections of a client's own car (host authority only):

| Scenario | a minute | median | 99th pct. | over 10 cm | over 1 m | samples run again |
| --- | --- | --- | --- | --- | --- | --- |
| chase (no contact) | 0 | | | 0 | 0 | |
| ram | 252 | 5.2 cm | 0.82 m | 121 | 3 | 11 |
| ram, 150 ms, 5% | 254 | 10.4 cm | 1.49 m | 150 | 16 | 24 |
| ram, 3 players | 298-312 | 4.9-8.7 cm | 1.2-1.6 m | 126-167 | 5-12 | 12-13 |
| ram, shared traffic | 182 | 1.2 cm | 0.86 m | 52 | 0 | 13 |
| rear (two runs) | 786, 423 | 20, 7.8 cm | 3.8, 3.2 m | 666, 232 | 187, 32 | 13 |

A car that touches nothing is never corrected: the host and the client run
the same code on the same inputs from the same state (chase: 75 s without
one). Corrections come in bursts around contacts with another player's car
(and with props, which each machine still runs itself until the props work
lands); half are under 1-10 cm (the state after a correction is the host's
body and main parts, not every last field), and the drawing eases each one
away over about 60 ms, so the client's own car showed no jump over 1 m in
any run but the one above. Running the samples again cost at most 12.8 ms
a second and 2.5 ms in a frame over every run (shunting, 13 samples a
correction); it is bounded at 120 samples.

The host ran short of a client's inputs once in these runs (one sample
repeated, in the rear run on the loaded machine); the client ran at 1.000
speed except for spells at 0.994 in the rear runs (the host holding more
than three of its inputs).

### Builds that round differently

* A release host against a client built with `-D_GLIBCXX_ASSERTIONS` (and
  no debug information): ram, 277 corrections a minute, 2 over 1 m: the
  same as two identical builds (GCC at -O2 both, the floats come out the
  same).
* `OPENMM2_DEBUG_NETCARS_NOISE=0.000001` on the client (its car's momentum
  nudged by about a millionth every sample, as a compiler or a maths
  library that rounds differently might): chase, which needs no correction
  at all with identical builds, gave 418 corrections a minute, 54 over
  10 cm and 8 over 1 m (at most 2.2 m, eased away) where the cars crashed
  into the city. Small differences stay small while driving and grow in a
  crash, which is where an MSVC client against a GCC host would be
  corrected. That pairing could not be tested here.

### Where the other cars are placed (an experiment)

`OPENMM2_NET_OTHERS=ahead` places and draws the other players' cars where
the host will have them when it runs this machine's sample (extrapolated
from the newest states by the samples not yet acknowledged less half the
round trip) instead of in the past; `=ghost` keeps the client's car from
touching them at all (these runs with the build before c367288 and the
merge, so the default row is that build's):

| | rear: corrections over 1 m | rear: other car's jumps over 1 m a minute | ram: corrections over 1 m | ram: other car's jumps over 1 m a minute |
| --- | --- | --- | --- | --- |
| in the past (default) | 29 | 0 | 1 | 0 |
| ahead | 6 | 5.9 | 27 | 28.6 |
| ghost | 4 (cars drawn overlapping, 0.4 m apart) | 0 | | |

Placing the others ahead helps a shunt and hurts a head-on crash (the
extrapolation overshoots where the other car brakes or swerves, and the
newest state then moves it back); not touching them lets the cars drive
into each other on screen. The default stays as the maintainer chose.

## Bandwidth

* A client's inputs: one message a frame that ran a sample (60 a second),
  about 15 bytes of payload (0.9 KB/s), about 3.5 KB/s on the wire with
  UDP and ENet's headers. A frame like the one before costs one bit.
* The host's states: 20 a second to each client, 270 bytes for the
  client's own car plus 35 for each other car: 6.2 KB/s of payload with two
  players (measured), about 11 KB/s down per client and 78 KB/s up for the
  host with eight (0.3.1: about 6.5 and 46 KB/s), plus the shared traffic in
  cruise.

## Deviations from MM2

* The host simulates every player's car; MM2 ran every network car on every
  machine with its own collisions (the maintainer's decision).
* A player's car takes its input once a physics sample (MM2: once a
  frame; at 60 fps the same), and Cops and Robbers' regeneration with it.
* Each player's car draws its wheels' bump numbers from its own random
  stream (MM2: one `rand()` for the game).
* The host decides every car's damage and Cops and Robbers' limits (MM2:
  each machine its own car's damage; the limits were already the host's in
  MM2, `UpdateLimit` / `SendLimitReached`).

## Findings

| # | Severity | Location (0.3.1) | Finding | Status |
| --- | --- | --- | --- | --- |
| C1 | visible | `RaceScreen::updateRemoteCars` | Each machine resolved its own car against the other's past image, which never gave way (sync review S2, S3): collisions happened at different places and times on each machine, and up to 13 of 21 on one screen only. | fixed (3f73059): the host's collision is the only one; every car gives way by its mass |
| C2 | visible | `RaceScreen::sendLocalState`, `DamageSync` | Each car's damage was decided by its owner. | fixed (3f73059): the host decides every car's damage |
| C3 | minor (parity) | `CopsAndRobbers::tickLimits` | Every machine checked the limits (sync review S6). | fixed (276e508) |
| C4 | visible | `HostInputQueue` (new) | A client that loaded before the host started seconds behind its car. | fixed (127ac41) |
| C5 | visible | `CarPrediction::acknowledge` (new) | Replays against the other cars' newest places made shunting corrections swing by up to 4 m. | fixed (19e8271) |
| O1 | visible | the prediction | Corrections of up to 2-3 m while shunting another player's car, which the client sees in the past. At worst (one rear run of six) a correction puts the client's car where the host has it, right behind the other car, which is inside that car's past image: the car is pushed out every sample and shakes from side to side by up to 1.5 m for under a second. | open: inherent to drawing the others in the past and predicting one's own car; the experiment above trades it for the other cars jumping. Narrower ways out: let a corrected car pass through another player's image until they part, or limit the push a car takes from an image in a sample |
| O2 | visible | props | Each machine runs its own props, so a car knocking them is corrected (the props work makes them the host's). | for the props agent |
| O3 | minor | rules | Checkpoints, laps, finishes and Cops and Robbers' gold were decided on each player's own machine and relayed (a client could claim them); only the limits were the host's. | fixed (protocol 8, docs/review/multiplayer-desync-rules.md): the host runs every car's waypoints after every sample (`game::session::RaceReferee`), the finish exchange, timeout and standings, and Cops and Robbers' rules for every car (`CopsAndRobbers::updateHost`), and tells each player (`game::NetRules`); a client predicts its checkpoints and a pickup, which the host confirms or corrects, and the host refuses a player's own rule events |
| O4 | minor | commands | The host checks a client's resets only against the city's box and four a second, not against the race's checkpoints and the water. | partly fixed (docs/review/multiplayer-desync-rules.md): in a race the host takes a respawn only at the start or a checkpoint its referee counted for that car (`RaceReferee::mayRespawnAt`); whether the car was in the water is still the client's word |
| O5 | minor | `CarStatesMsg` | The own car's state is 270 bytes at 20 Hz to every client (78 KB/s for the host with eight players). | open: it could go only when it changed beyond the tolerance |
| O6 | minor | MSVC against GCC | Not testable here; the noise test suggests corrections in crashes, eased away. | open |
| O7 | minor | replays | The shared traffic's cars still on their rails are held where the frame placed them while a client runs its samples again. | open: `TrafficClient::poseAt` could put each where its sample met it |

## Reproducing

```
# per machine, with its own XDG directories and port 2380:
OPENMM2_NET_TRACE=<machine>.trace OPENMM2_DEBUG_INPUT=<inputs> OPENMM2_DEBUG_INPUT_MS=1000 \
  OPENMM2_DEBUG_START=<x,y,z,angle> OPENMM2_DEBUG_NETCARS=1 openmm2 ...
netprobe relay 127.0.0.1:2380 --port 2381 --delay 60 --jitter 20 --loss 2 --reorder
netprobe syncreport host.trace c1.trace [c2.trace]
```

The layouts above: host at (-1149.945, 111.9, 163.194) facing -Z (angle 0);
ram: client at (-1149.345, 114.9, 137.194), angle π, inputs
`1,0,0,0/1,0,0,0/0,1,0,0/0,0.6,0,0/1,0,0.1,0/1,0,0.1,0/0,1,0,0/0,0.6,0,0`
(host) and the same with steering -0.08 (client); rear: client at
(-1149.945, 111.9, 175.194), angle 0, host
`0.5,0,0,0/0.5,0,0,0/0,0.7,0,0/0.6,0,0.08,0/0.6,0,-0.08,0/0,1,0,0/0.5,0,0,0`,
client `1,0,0,0/1,0,0.02,0/1,0,0,0/1,0,-0.03,0/0.8,0,0,0/1,0,0.04,0/0,1,0,0`.
