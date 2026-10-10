# Multiplayer desync: the players' cars

Reviewed and reworked on 2026-10-09 from 0.3.1 (cd5ae29); a second round
the same day (below, "Second round") made shunting predictable, the host
check a client's resets and the replays meet the traffic where it was.

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

## Second round: shunting, resets, the traffic in replays

After the first round merged (cae6989) the coordinator asked for three of
its open findings: shunting from behind (O1) at least as good as 0.3.1 with
no jump of a client's own car over 1 m and no other scenario worse; the
host checking a client's resets (O4); the traffic in replays (O7).

### What the shunts showed

* **Most of the divergence is a delay.** `netprobe syncreport` now also
  finds, for each viewer and car, the delay that brings the two drawings
  closest and what remains at it. In every build and scenario the host
  draws a client's car about 140-160 ms behind that client's screen (230-260
  ms on the harsher link): in 0.3.1 the playout delay of the client's
  snapshots, now the client's inputs' trip, the host's margin of inputs in
  hand and a frame. At that delay a few centimetres remain at the median.
  The plain divergence is that delay times the car's speed, and the rear
  runs differ most in speed: the client's car moved at a median 1.9-4.6 m/s
  in 0.3.1's runs, where it bounced off the host's past image, and at
  2.8-9 m/s once the host's car gave way.
* **The past image.** A client predicts its own car about 150 ms ahead of
  the host and drew the other car about 150 ms behind: shunting it, its car
  met the other's image some 2 m short of where the host's cars met, and an
  image that does not give way. Each state then corrected it (up to 53
  corrections over 1 m in a run), and a correction could put the car inside
  the image (the first round's shake).
* **The order.** Two cars collide in the order of the world's movers
  (World::step runs a mover's collisions with the city, then with the movers
  after it, then with the instances): the host had its own car first, a
  client its own.
* **What the state left out.** Rebuilt from the full state alone (body,
  wheels, engine, ...), the other car still drifted from the host's at every
  state while the two pushed each other: a sample hands the next the force
  and torque the wheels and engine set for it, each tyre's rolling
  resistance (`Wheel::tireResistance`, which the drivetrain reads), and in a
  contact the impulses and pushes. Found by restoring the host's values
  group by group in a test of two cars in one process (below): with all of
  them a steady shunt is predicted to the bit.

### What changed

1. **The near cars in full** (c5c1b4c, protocol 9): the host sends each
   client the two nearest other players' cars within 40 m of its car (kept
   to 50 m; not one towing a trailer) in full, with the input it last
   applied to them (`net::NearCarState`). The client simulates them along
   with its own car (`CarPrediction`'s companions): at every state it puts
   them to the host's state at the acknowledged sample and runs them with
   its car through the later samples on that input, and on between states.
   Its car meets them where the host's does and both give way; only the
   other player's change of input since that state is unknown. Drawn
   between their last two samples, each state's move eased away (80 ms
   half-life) and the switch between simulating a car and placing it at its
   states over 150 ms.
2. **Player order; far companions alone** (94d542a): every machine keeps
   the players' cars in the world's movers in player order (the host's
   first), and a replay runs them in that order. Companions that cannot meet
   the car in the samples run again (farther than 8 m and the way their
   speeds close) run again without it, the car held where each sample had
   it: running the car's own samples again against props held still made it
   drift from the host's in a chase with no contact between the cars.
3. **What a sample hands the next** (38fcf08): in every car's full state
   (own and near); at most two near cars, so that a message to one of eight
   players stays one ENet packet (1356 bytes with two near cars in a
   contact; ENet splits above 1372).
4. **Resets on the host's terms** (78886cf): `game::ResetRules`.
5. **The traffic in replays** (e51e9bf, 60ddfd0): the shared traffic's
   cars on their rails where each sample met them, and a body a sample did
   not meet out of that sample.
6. **The report** (c7e1cb1, 156908f): the delay-matched divergence;
   corrections a minute of the whole race.

### Before and after

Two runs of each scenario on each build, interleaved (the same machine
load), 75 s each; "0.3.1" is 80b8b8e with the machine clock, "first round"
cae6989, "second round" this branch at 8c0a8d1. Metres, median / 99th
percentile, the two runs separated by a comma.

Host ← client (the host's drawing of the client's car against the client's
screen):

| Scenario | 0.3.1 | first round | second round |
| --- | --- | --- | --- |
| rear | 0.28 / 1.76, 0.71 / 2.51 | 0.47 / 2.89, 1.16 / 5.04 | 0.45 / 2.50, 0.43 / 2.19 |
| ram | 0.64 / 1.65, 0.65 / 1.64 | 0.61 / 1.38, 0.57 / 1.33 | 0.33 / 1.35, 0.60 / 1.33 |
| chase | 0.58 / 2.90, 0.26 / 2.84 | 0.90 / 2.70, 0.54 / 2.42 | 0.22 / 2.22, 1.07 / 2.99 |
| ram, 150 ms, 5% | 0.95 / 2.36, 0.99 / 2.39 | 1.11 / 2.67, 1.04 / 2.47 | 0.90 / 1.92, 0.92 / 2.00 |
| ram, 3 players (host ← clients) | 0.50-0.63 / 1.38-1.59 | 0.44-0.53 / 1.25-1.32 | 0.46-0.53 / 1.18-1.42 |
| ram, shared traffic | 0.56 / 1.51, 0.52 / 1.76 | 0.57 / 1.39, 0.52 / 1.39 | 0.59 / 1.42, 0.59 / 1.39 |

Client ← host (and, with three players, client ← client):

| Scenario | 0.3.1 | first round | second round |
| --- | --- | --- | --- |
| rear | 0.33 / 1.11, 0.36 / 1.10 | 0.13 / 2.21, 0.70 / 2.19 | 0.27 / 1.30, 0.07 / 1.77 |
| ram | 0.56 / 1.43, 0.59 / 1.61 | 0.60 / 1.68, 0.59 / 1.62 | 0.56 / 1.52, 0.60 / 1.59 |
| chase | 0.51 / 1.86, 0.20 / 1.49 | 0.59 / 1.91, 0.22 / 1.62 | 0.45 / 1.42, 0.23 / 1.67 |
| ram, 150 ms, 5% | 0.92 / 2.28, 0.93 / 2.43 | 1.01 / 2.54, 1.00 / 2.45 | 0.94 / 2.71, 0.96 / 2.81 |
| ram, 3 players (clients ← host) | 0.54-0.57 / 1.34-1.51 | 0.55-0.57 / 1.62-1.82 | 0.49-0.50 / 1.39-1.46 |
| ram, 3 players (client ← client) | 0.73-0.92 / 2.03-2.26 | 0.88-1.08 / 2.66-2.85 | 0.05-0.29 / 2.47-2.91 |
| ram, shared traffic | 0.56 / 1.36, 0.59 / 1.57 | 0.61 / 1.72, 0.54 / 1.69 | 0.56 / 1.52, 0.58 / 1.83 |

What remains once the delay is taken out, host ← client in the rear runs
(the delay that brings the drawings closest; median / 99th percentile):
0.3.1 160 ms 0.010 / 0.09, 175 ms 0.021 / 0.17; first round 135 ms
0.18 / 2.32, 160 ms 0.21 / 3.64; second round 135 ms 0.008 / 0.53, 135 ms
0.015 / 0.51. The client's car's median speed in those runs: 0.3.1 1.9 and
4.2 m/s, first round 2.8 and 5.5, second round 3.1 and 3.2.

The rear shunt is now drawn as 0.3.1 drew it at the median, at the same
delay, and its collisions happen on both screens (below). Its 99th
percentile, and that of a client's view of the host's car in the harsh
runs (2.7-2.8 m against 2.3-2.4), stay higher than 0.3.1's: in 0.3.1 every
player's own car was the truth and the others drew exactly its path a delay
late, never corrected (and each machine had its own collisions); with the
host the authority, what remains is the client's prediction missing the
other player's change of input in the last 150-250 ms (the host's car
braking hard in front of it), and a near car is drawn where the client
predicts it. With the near prediction off (`OPENMM2_NET_OTHERS=past`, the
same build) the harsh run's client ← host was 0.61 / 2.39 but its own car
was corrected 111 times over 10 cm, 8 over 1 m, and jumped once by 4.4 m.

Collisions between players: the host's collisions the client predicted /
the client's predictions the host did not have (0.3.1: the host's collisions
the client also had / the client's the host also had):

| Scenario | 0.3.1 | first round | second round |
| --- | --- | --- | --- |
| rear | 5 of 14, 8 of 17 / 5 of 10, 7 of 16 | 11 of 11, 5 of 5 / 6 of 17, 5 of 11 | 3 of 3, 11 of 11 / 1 of 4, 0 of 11 |
| ram | 4 of 4, 6 of 6 | 15 of 15, 18 of 18 / 1 of 16, 0 of 18 | 14 of 14, 18 of 18 / 0 of 14, 1 of 19 |
| ram, 150 ms, 5% | 6 of 7, 9 of 10 / 5 of 6, 10 of 12 | 18 of 18, 18 of 18 / 0 of 18, 2 of 20 | 18 of 18, 18 of 18 / 0, 0 |
| ram, 3 players | every pair matched in one run, one of the clients' collisions on one screen only in the other | 0-1 17 of 17, 10 of 11 / 1, 0; 0-2 1 of 3, 1 of 3 / 2 of 3, 4 of 5 | 0-1 12 of 12, 5 of 5 / 1, 0; 0-2 7 of 7, 4 of 4 / 0, 0 |
| ram, shared traffic | 4 of 4, 6 of 6 / 4 of 4, 6 of 7 | 19 of 19, 7 of 7 / 0, 0 | 16 of 16, 18 of 19 / 0, 0 |

Corrections of a client's own car (a minute; over 10 cm; over 1 m) and its
own jumps over 1 m in a frame (none in any 0.3.1 run):

| Scenario | first round | second round |
| --- | --- | --- |
| rear | 868, 610; 459, 571; 40, 53 (6 drawn at once); jumps 0, 16 | 386, 771; 31, 83; 1, 7; jumps 0, 0 |
| ram | 269, 248; 142, 133; 6, 3 | 410, 166; 33, 5; 0, 0 |
| chase (no contact between the cars) | 0, 0 | 15, 133; 0, 10; 0, 0 |
| ram, 150 ms, 5% | 236, 260; 121, 147; 5, 6 (jump 1) | 175, 181; 26, 12; 0, 0 |
| ram, 3 players (two clients) | 356 and 286, 221 and 307; 167 and 72, 99 and 63; 8 and 1, 0 and 1 | 221 and 181, 206 and 105; 9 and 22, 13 and 10; 0 |
| ram, shared traffic | 279, 345; 126, 171; 0, 7 | 219, 225; 23, 50; 2, 2 |

Most corrections now are millimetres to a few centimetres (median 0.7-2.5
cm against 1.4-21.5): a state that moves the other car moves this one a
little where they meet. The chase's are the props': since the props work
(14dc206) a client's car meets the host's props, and the same build with the
near prediction off was corrected 236 times a minute there, 16 over 10 cm.
Running the samples again costs more (every state runs the near cars
again): at most 20.6 ms a second and 2.8 ms in a frame.

### Resets (O4)

A client driving into the bay at San Francisco's cruise start: after five
seconds in the water its rules reset the car, the host carried the reset
out (the car's `vehSplash` latched for over four seconds) and the client
was not corrected at all; twice in 45 s. The same client resetting its car
on the road six seconds in (`OPENMM2_DEBUG_RESPAWN_MS` on the client only):
the host refused it (`netcars: refused player 1's Reset`) and the client's
car went back to where the host had it (a 40.9 m correction, drawn at once).
`tests/game/test_player_cars.cpp` checks every rule.

### The traffic in replays (O7)

Driving through the shared traffic along a busy street (z = -400 in San
Francisco), two runs of 75 s each per way of placing the cars on their rails
in the samples run again (corrections a minute; over 10 cm; over 1 m):

| Placing | run 1 (a jam: the car pushed through queued cars) | run 2 |
| --- | --- | --- |
| where each sample met them (kept) | 904; 60; 6 | 184; 3; 0 |
| where the host had them then (`TrafficClient::poseAt`) | 820; 89; 4 | 183; 34; 4 |
| where the frame put them (before) | 1045; 194; 1 | 1020; 205; 4 |

Where each sample met them is what the sample ran against, so a sample run
again with nothing else changed comes out the same; `poseAt`, newer, moves
the cars a sample run again meets. Pushing through a queue of cars corrects
at nearly every state whichever way (the host knocks them loose, the
replays meet them as walls): O8.

### Bandwidth

The host's states with a near car: about 700 bytes a message (the client's
car's full state, the other's, and a snapshot), 14 KB/s of payload for two
players in contact against 6.2 KB/s in the first round, 8 KB/s apart. With
eight players each client gets at most about 1.1 KB a message, 22 KB/s, and
the host sends at most about 160 KB/s (every player with two others near);
apart, as in the first round, about 11 and 78 KB/s.

### Not reproduced

* The props agent saw a client corrected at every state for 3-4 s after a
  checkpoint race's start (0.56 m, alternating, 58-59 samples run again).
  One checkpoint race on this branch (40 s, the race's own grid): one
  correction of 4.5 cm as the host caught up with the client's backlog (55
  samples run again), then millimetres.
* A client's car pressed against a wall correcting: a car driven into a
  wall at full throttle for 20 s was not corrected once.

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
* A client simulates the other players' cars near its own from the host's
  states and their last inputs (MM2 drove every network car on its last
  pedals and pulled it toward the packets, `mmNetObject::Predict`; OpenMM2
  puts it to the host's state instead), and keeps the players' cars in
  player order among the world's movers (MM2: in the order they were
  declared).
* The host resets a client's car only where the game's rules would (MM2's
  peers reset their own cars and told the others).

## Findings

| # | Severity | Location (0.3.1) | Finding | Status |
| --- | --- | --- | --- | --- |
| C1 | visible | `RaceScreen::updateRemoteCars` | Each machine resolved its own car against the other's past image, which never gave way (sync review S2, S3): collisions happened at different places and times on each machine, and up to 13 of 21 on one screen only. | fixed (3f73059): the host's collision is the only one; every car gives way by its mass |
| C2 | visible | `RaceScreen::sendLocalState`, `DamageSync` | Each car's damage was decided by its owner. | fixed (3f73059): the host decides every car's damage |
| C3 | minor (parity) | `CopsAndRobbers::tickLimits` | Every machine checked the limits (sync review S6). | fixed (276e508) |
| C4 | visible | `HostInputQueue` (new) | A client that loaded before the host started seconds behind its car. | fixed (127ac41) |
| C5 | visible | `CarPrediction::acknowledge` (new) | Replays against the other cars' newest places made shunting corrections swing by up to 4 m. | fixed (19e8271) |
| O1 | visible | the prediction | Corrections of up to 2-3 m while shunting another player's car, which the client sees in the past. At worst (one rear run of six) a correction puts the client's car where the host has it, right behind the other car, which is inside that car's past image: the car is pushed out every sample and shakes from side to side by up to 1.5 m for under a second. | fixed (c5c1b4c, 94d542a, 38fcf08): a client simulates the near cars with its own from the host's full states, in the host's order; corrections over 1 m in the rear runs 1 and 7 (first round 40 and 53), no jump over 1 m |
| O2 | visible | props | Each machine runs its own props, so a car knocking them is corrected (the props work makes them the host's). | the props work landed (14dc206): docs/review/multiplayer-desync-props.md; a chase with props knocked is now corrected about 130-240 times a minute by a few millimetres |
| O3 | minor | rules | Checkpoints, laps, finishes and Cops and Robbers' gold were decided on each player's own machine and relayed (a client could claim them); only the limits were the host's. | fixed (protocol 10, docs/review/multiplayer-desync-rules.md): the host runs every car's waypoints after every sample (`game::session::RaceReferee`), the finish exchange, timeout and standings, and Cops and Robbers' rules for every car (`CopsAndRobbers::updateHost`), and tells each player (`game::NetRules`); a client predicts its checkpoints and a pickup, which the host confirms or corrects, and the host refuses a player's own rule events |
| O4 | minor | commands | The host checks a client's resets only against the city's box and four a second, not against the race's checkpoints and the water. | fixed (78886cf): `game::ResetRules` (the water, the fall, the race's checkpoints, a wreck's or Cops and Robbers' repair) on the host's simulation of the car; in a race a respawn only at the start or at a checkpoint the host's referee counted for that car (docs/review/multiplayer-desync-rules.md) |
| O5 | minor | `CarStatesMsg` | The own car's state is 270 bytes at 20 Hz to every client (78 KB/s for the host with eight players); since protocol 9 about 310 (370 in a contact), and up to two near cars in full: at most about 160 KB/s for the host with eight players all near each other. | open: it could go only when it changed beyond the tolerance, a near car only while the two can meet |
| O6 | minor | MSVC against GCC | Not testable here; the noise test suggests corrections in crashes, eased away. | open |
| O7 | minor | replays | The shared traffic's cars still on their rails are held where the frame placed them while a client runs its samples again. | fixed (e51e9bf, 60ddfd0): where each sample met them (corrections over 10 cm driving through the traffic 3 and 60 against 194 and 205; `poseAt` 34 and 89) |
| C6 | visible | `RaceScreen` (the players' cars' movers) | Each machine had its own car first among the world's movers, so two cars collided in a different order on the host and on a client. | fixed (94d542a): player order everywhere |
| C7 | visible | `net::OwnCarState` | The full state left out what a sample hands the next (the force and torque set for it, the tyres' rolling resistance, a contact's impulses and pushes): a car rebuilt from it drifted from the host's in a contact. | fixed (38fcf08), protocol 9 |
| C8 | minor | `netprobe syncreport` | Corrections a minute were counted over the span they fell in. | fixed (156908f) |
| O8 | minor | the shared traffic | A client pushing through a queue of traffic cars is corrected at nearly every state (by a few centimetres), whichever way the replays place them: the host knocks the cars loose, a replay meets them as walls. | open (with the shared traffic) |
| O9 | minor | the near cars | A near car is drawn where the client predicts it: in the harsh runs a client's view of the host's car reached 2.7-2.8 m at the 99th percentile (2.3-2.4 placed at its states), where the host braked or turned in the last 250 ms. | open: inherent to predicting another player; the client's own car is corrected far less in exchange |

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

The second round's: water: client at (40.306, 1.918, 153.295), angle 0,
input `0.8,0,0.1,0` (San Francisco's cruise start, into the bay); traffic
(the shared traffic on): client at (-1560, 21, -401), angle -π/2, inputs
`0.8,0,0.12,0/0.8,0,-0.12,0/0.8,0,0.1,0/0.8,0,-0.1,0/0,0.5,0,0` for 0.7 s
each; wall: client at (-1149.945, 111.9, 183.194), angle π/2, input
`1,0,0,0`; the host waits at the hill (`0,0,0,0`) in all three. The checkpoint
race ran without `OPENMM2_DEBUG_START` (the race's grid). The two-car shunt
without a network is `PlayerCars.AShunt*` in
`tests/game/test_player_cars.cpp`.
