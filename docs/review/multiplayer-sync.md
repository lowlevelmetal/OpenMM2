# Multiplayer review: in-race synchronisation

Reviewed on 2026-10-08 from `integration` at 2734bea, merged with the network
input review (84fda68: line numbers below are at that commit), then with the
lobby review (77fc127) and the shared cruise traffic (d7fb35b).

The maintainer's report for this area: "There is also an intermitten issue
where the cars slightly bounce", later clarified as "sometimes the vehicles
jitter": shaking or stuttering in place or along their path.

## Root cause of the jitter

It is the other players' cars, not the local one, and it had four causes, all
measured with a host and one or two clients on this machine (method below).
Each one shows on its own; together they explain why it is intermittent:
they depend on the speed, the frame rate, the link and who looks at whom.

1. **Snapshot time stamps did not match their state** (finding 1). A car's
   state is its last fixed 60 Hz simulation step, up to 16.7 ms older than the
   frame, but it was stamped with the frame's time. 50 ms of stamps carried
   3 or 4 steps of motion (2.05 or 2.73 m at 41 m/s, measured), so even on
   loopback the remote car surged back and forth along its path by up to
   0.35 m at 18 Hz (and 10 cm up and down on a slope): drawn-vs-true error
   20 cm rms, 55 cm at worst, on loopback.
2. **Remote cars moved at the frame rate, everything else in 60 Hz steps**
   (finding 2). There is no render interpolation: the local car, the camera,
   the AI racers and the police move in whole simulation steps. A remote car
   sampled at the frame's own time shook against them by up to half a step:
   29 cm rms at 35 m/s at 144 fps; at 60 fps 4-10% of frames jumped by a whole
   step (55 cm at 35 m/s) when a frame ran 0 or 2 steps. Visible as the car
   next to yours vibrating or twitching, at any network quality.
3. **The fixed 100 ms interpolation delay was too short for the Internet**
   (finding 3). Over a 100 ms round trip with ±15 ms jitter and 1% loss, a
   client's view of the host extrapolated 14-20% of the time and the host's
   view of a client 24-40%, snapping back at each snapshot: horizontal
   errors in a sawtooth up to 40-56 cm, height errors up to 48 cm. A
   client's view of another client (two hops and the host's relay tick, 127 ms
   mean lateness) was extrapolated **100%** of the time, with errors up to
   3-5 m. That is the "sometimes" of the report: it depends on the link and on
   whether you look at the host or at another client.
4. **The client's clock stepped** (finding 4) whenever the lowest-round-trip
   sample in its window changed: every remote car jumped by its speed times
   the step (4 ms steps measured on a clean link: 14 cm at 35 m/s).

The network made it worse: ENet's sequenced unreliable channel dropped any
snapshot that arrived after a newer one (15-20% with reordering), the host
relayed only one state per player per tick (a quarter of client-to-client
snapshots lost at 30 ms of jitter), and ENet's packet throttle drops
unreliable packets after round-trip spikes (findings 5-7).

The local car's own motion is not corrected by anyone (no host correction
exists), and its contacts with a remote car did not jitter, but they were
wrong (finding 8): a remote car was a still wall to the physics.

Before and after, the same scenario (host and two clients, each client
behind a relay adding 50 ±15 ms each way and 1% loss, all three driving
flat out from a circuit start; "extrapolated" is the share of frames a car
was drawn past its newest snapshot; "error" is the drawn position against
the other machine's own trace at the same session time; "shake" the remote
car's motion against the viewer's own car from frame to frame):

| View | Extrapolated before / after | Error rms before / after | Error 99th pct. before / after | Shake rms before / after |
| --- | --- | --- | --- | --- |
| host sees client 1 | 24.1% / 0.4% | 28.2 / 1.8 cm | 45 / 7 cm | 22.4 / 0.6 cm |
| client 1 sees host | 19.6% / 0.4% | 22.2 / 1.5 cm | 38 / 5 cm | 23.6 / 0.3 cm |
| client 1 sees client 2 | 100% / 0.4% | 33.7 / 1.8 cm | 100 / 8 cm | 23.3 / 0.4 cm |
| client 2 sees client 1 | 99.9% / 0.3% | 37.0 / 1.8 cm | 63 / 9 cm | 22.4 / 0.4 cm |

Final build, harsher link (75 ±30 ms each way, 2% loss, reordering, through
`netprobe relay`): 0.1-0.8% extrapolated in all six views, 1.6-2.2 cm rms,
5-7 cm at the 99th percentile; the remaining large errors (20-50 cm) are the
moments a car hits a wall between two snapshots.

## How it was tested

* Host and clients on one machine, UDP port 2320, each with its own config
  and data directories, `[Display] VSync=off`, `FrameCap=120..144` (with
  vsync on, the compositor throttles a hidden window to a few frames a
  second), `OPENMM2_DEBUG_INPUT` driving the cars, `OPENMM2_FRONTEND_SCRIPT`
  hosting, joining and starting.
* `OPENMM2_NET_TRACE` (added, off by default, documented in
  `docs/multiplayer.md`): every snapshot taken in with its stamp and arrival,
  the local car once a frame with the time its state belongs to, and each
  remote car as drawn with its sample time. Scripts compared each machine's
  drawing of a car with the other machine's trace.
* Latency, jitter, loss and reordering: a UDP relay in front of the host for
  each client, first a scratch script, then `netprobe relay` (added). The
  scratch relay's own reordering (asyncio does not keep timers of equal time
  in order) is what first showed ENet dropping late snapshots.
* The pre-fix build was rebuilt from 84fda68 with the same trace added, for
  the before columns.
* Unit tests in `tests/net/test_sync.cpp`, `tests/game/test_netgame_sync.cpp`,
  `tests/phys/test_kinematic.cpp` and `tests/game/test_session.cpp`.

## What I read

* `src/net/Snapshot.{h,cpp}`, `ClockSync.{h,cpp}`, `Transport.{h,cpp}`,
  `BitStream` (quantisation, quaternions), `Protocol.h`, and the in-race parts
  of `Session.{h,cpp}` (replication, clock, relay, events, players leaving),
  completely; ENet 1.3.18's unreliable delivery and packet throttle
  (`protocol.c`, `peer.c`).
* `src/game/net/NetGame.{h,cpp}`: replication, events, race start.
* `src/app/RaceScreen.cpp`: the frame loop's order, `sendLocalState`,
  `updateRemoteCars`, `drawRemoteCars`, `hudBlips`, `updateNetPlayers`,
  `updateNetRace`, `updateCopsAndRobbers`, `leaveRace`/`quitToMenu`, the AI
  audio; later the shared traffic's client and host parts.
* `src/phys/World.cpp` (fixed step, kinematic bodies), `Collider`, `Impact`
  (`calcImpact`), `CarSim` (what a kinematic car's controller does),
  `src/game/session/Session.cpp` (multiplayer finishes and standings),
  `CopsAndRobbers.cpp`.
* MM2: `mmNetObject` (`SetPositionData`, `PositionUpdate`, `Predict`,
  `Update`), `mmGameMulti` (`SendPosition`, `Update`, `QuitNetwork`),
  `mmMultiRace::SystemMessage` / `GameMessage`, `mmMultiCR::UpdateGame` /
  `UpdateLimit`.

## Findings

Severity: crash / gameplay-breaking / visible / minor / code quality.

### Fixed

| # | Severity | Location (84fda68) | Scenario | Confirmed | Fix |
| --- | --- | --- | --- | --- | --- |
| 1 | visible | `NetGame.cpp:585`, `Session.cpp:996` | Snapshots stamped with the frame's time while carrying the last fixed step's state (up to 16.7 ms older): remote cars surged ±0.35 m along their path at speed, even on loopback. | trace: 50 ms stamps with 3 or 4 steps of motion; test `NetGameSync.StatesCarryTheSimulationTimeAndCarsAreSampledWithItsLag`, `SessionSync.StatesCarryTheTimeTheyWereSimulatedAt` | 3edfaf6: `submitLocalState(..., stateAgeMs)`, stamped at the frame's session time (`NetGame::frameTime`) less the step's remainder |
| 2 | visible | `RaceScreen.cpp:299`, `:489`, `:2148`, `:3330`, `:3402`; `NetGame.cpp:608` | Remote cars sampled at the frame's time (and again for each use, at slightly different times) while everything else moves in 60 Hz steps: 29 cm rms shake against the local car at 35 m/s, 144 fps; whole-step jumps on 4-10% of frames at 60 fps. | trace (cadence mismatch 8.2 ms rms before, 0.1 ms after); test `World.RemainderAfterIsWhatAdvanceFixedLeaves` | 3edfaf6: one sample a frame at the time the simulation reaches after the frame's steps (`World::remainderAfter`), used for the body, drawing, HUD and Cops and Robbers |
| 3 | visible | `Session.h:59`, `Session.cpp:1007` | Fixed 100 ms delay: 14-40% extrapolated at 100 ms round trip, 100% for client-to-client views, snapping back by up to 0.5 m (3-5 m between clients). | trace, before/after table; tests `PlayoutDelay.*`, `SessionSync.PlayoutDelayFollowsTheArrivals` | 3edfaf6: per-car playout delay from the snapshots' measured lateness plus gap over 3 s (`SnapshotBuffer::requiredDelay`), 50-500 ms, growing at 25% and shrinking at 2% of real time |
| 4 | visible | `ClockSync.cpp:12`, `Session.cpp:858` | The client's clock offset stepped when its best sample changed (and was rounded to whole ms): every remote car jumped by speed × step. | trace (4 ms steps); test `SlewedClock.StepsOutsideARaceAndSlewsInOne`, `ClockSync.KeepsSubMillisecondPrecision` | 3edfaf6: `SlewedClock` slews the shown offset at 5% during a race (steps in the lobby or above 250 ms); sub-millisecond session clock |
| 5 | visible | `Transport.cpp:30` | State channel unreliable *sequenced*: a snapshot arriving after a newer one was dropped, 15-20% of the snapshots with reordering, each a hole to extrapolate across. | relay with `--reorder`: 410-440 of 513 before, 510-514 after, 0% extrapolated | d86626c: unsequenced (the buffer orders by time) |
| 6 | visible | `Session.cpp:422`, `:610` | The host relayed a joiner's state at its own 20 Hz tick, newest only: of two arriving within a tick one was lost (25% of client-to-client snapshots at 30 ms jitter), and each waited up to 50 ms more. | gap counts in the trace; test `SessionSync.TheHostPassesEveryJoinersStateOnAtOnce` | d86626c: relayed as it arrives |
| 7 | minor | `Transport.cpp:103` | ENet's packet throttle drops unreliable packets at the sender after round-trip spikes (0.94 for seconds in jittery runs). | ENet's throttle logged | d86626c: each side's peer never throttles down |
| 8 | gameplay-breaking | `Collider.cpp:88`, `RaceScreen.cpp:3324` | A remote car was a kinematic body whose collider reported no velocity: a still wall to the physics. Closing at 1 m/s on a car doing 30 m/s cost the chaser 12.6 m/s; a rear tap threw it back; rubbing side by side dragged it. MM2's network car is a simulated vehCar with its own velocity. | test `Kinematic.TouchingAMovingKinematicBodyTakesTheRelativeSpeed` (failed before) | acb820c, then the shared traffic's `kinematicMoves` (merge ea3f6ec): the car reports its snapshot velocity and spin in every mode (it still does not give way: deviation) |
| 9 | minor (robustness) | `Session.cpp:610`, `:860` | Snapshot times were trusted and relayed: a sender stamping minutes ahead dragged its car toward that state and pinned the playout delay. | test `SessionSync.StatesStampedFarFromTheClockAreDropped` | 793a736: dropped (and not relayed) more than 1 s ahead or 5 s behind the receiver's clock |
| 10 | visible | `Session.cpp:398`, `:375` | Snapshots went out on the first frame 50 ms after the last send, drifting to 17-18 Hz, and the same state was resent when no step had run. | trace (55.6 ms gaps at 144 fps) | 3edfaf6: steady 20 Hz cadence, only new states |
| 11 | gameplay-breaking | `game/session/Session.cpp:1722` | A race or circuit counted every result against the racers still listed: with three players, one finishing and then leaving ended the race (and sent everyone to the lobby) while the third was still driving. MM2's host compares at each finish (`mmMultiRace::GameMessage` 0x206). | test `Session.MultiplayerRaceWaitsForThePlayersStillInIt` (failed before) | db18f06: waits until every player still in the race has finished, or the timeout |
| 12 | gameplay-breaking | `RaceScreen.cpp:2469` | A joiner quitting a running race stays in the session: the others kept its car frozen in the road as a solid obstacle and a race waited 60-120 s for its finish. MM2's quitter left the session (`QuitNetwork`), and the others deactivated its car (`SystemMessage` 0x2d). | host and client, the client quitting by popup script: the host logged the quit and stopped drawing the car | e328e17: `GameEventType::LeftRace`; the others take the car out, show "has left the game", drop it from the racers (Cops and Robbers: its gold drops) |
| 13 | visible | `RaceScreen.cpp:2152` | Cops and Robbers' time limit, warnings and lockouts ran in frames from each machine's own load, through the countdown: machines disagreed by their load times, one showing "Time's up" while the others played on. `mmMultiCR::UpdateGame` starts the clock when it enables the racers. | log: host and client agree to 1 ms after | f5ca06e: the rules run on the session clock from the shared start |
| 14 | visible (parity) | `RaceScreen.cpp:3315-3418` | The network cars were silent. MM2's `mmNetObject::PositionUpdate` drives the car's `vehCarAudioContainer` (engine, horn, siren) from each packet. | log: the kinematic car's engine runs 600-7000 rpm and shifts with the sender's gear | ad4357c: the AI cars' positioned audio, from the kinematic `vehCarSim` and the snapshot's pedals, gear, horn, siren, wreck |
| 15 | visible (parity) | `RaceScreen.cpp:3413` | Remote wheels at their rest positions, spun from the speed: they hung in the air or sank into the road. `vehCarModel::Draw` uses the sim's wheels. | screenshots | 0b4fe05: the kinematic car's wheel matrices |
| 16 | visible | `RaceScreen.cpp` `updateNetTraffic` (d7fb35b) | Shared traffic drawn a fixed 100 ms behind: 27% of the logged cars extrapolated over a 150 ms round trip. | `OPENMM2_DEBUG_NETTRAFFIC` counts: 27% before, 15% after (the rest far cars whose state comes every other message) | 9ecacd8: at least the host car's playout delay |
| 17 | gameplay-breaking (merge) | `RaceScreen.cpp` `applyNetTrafficHits` (d7fb35b) | After the merge, the host's traffic-hit handling read `takeGameEvents()` after the race screen had already taken the frame's events: clients' hits would never be applied. | by reading | 9ecacd8: reads the frame's list |

The race screen now takes the frame's game events once (`takeNetEvents`) for
the race rules, Cops and Robbers, the traffic hits and `LeftRace`; before,
a cruise never took them, so they piled up (also capped by d7fb35b).

### Checked and correct

* **Quantisation** (`Snapshot.h`): position 2 mm, velocity 6 mm/s, angular
  velocity 8 mrad/s, orientation smallest-three 10 bits (0.08°, 3 mm at a
  car's corner): below anything visible.
* **Rotation**: `Quat::slerp` takes the short way (flips on a negative dot),
  the encoder sends the largest component positive, extrapolation multiplies
  the world-space delta on the left, which matches `toMatrix`'s convention
  (checked against the test `ExtrapolatesRotation`). No Euler angles.
* **Hermite interpolation** with the replicated velocities follows the true
  path to 0.7 cm rms (second difference of the error on the 60 Hz grid,
  smooth stretches) once the stamps are right; what remains is the
  centre-of-mass velocity against the model origin's (sub-centimetre).
* **Remote heights and suspension**: the drawn car is the snapshot's model
  matrix; nothing in the kinematic `vehCarSim` moves its body (only
  `resetBody` writes the matrix), so the ground cannot fight the received
  height.
* **Contact with a still remote car** (both parked on the grid): stable; the
  push only resolves penetration.
* **Events**: reliable and ordered on their own channel; a client's gold
  pickup goes through the host (`PickupRequest`) so two players cannot both
  take it; drops and deliveries are announced by the carrier, new sets by the
  host, as `mmMultiCR::GameMessage`.
* **Players leaving or timing out**: `PlayerLeft` removes the car and its
  buffer on every machine; a carrier's gold drops (host); the race stops
  counting the player.
* **Time wrap**: session times are 32-bit milliseconds (49.7 days); the
  32-bit echo of a time request is rebuilt to 64 bits.
* **Bandwidth**, three players, measured (ENet overhead included): each
  client sends 0.95 KB/s and receives 0.93 KB/s per other car; the host
  sends 3.7 KB/s. For eight players: about 6.5 KB/s down per client and
  46 KB/s up for the host, plus the shared traffic in cruise.

### Open

| # | Severity | Location | Scenario | Status / what it needs |
| --- | --- | --- | --- | --- |
| S1 | visible | `RaceScreen.cpp` (frame loop), `phys::World::interpolationAlpha` | Nothing is drawn interpolated between simulation steps: above 60 fps the local car, the camera and every simulated car move in 60 Hz steps against smoothly drawn scenery and rail traffic. The remote cars now step with the simulated ones (finding 2), as the AI racers do. | outside this area (rendering): needs the drawn poses blended by `interpolationAlpha`, and then remote cars sampled at the blended time. |
| S2 | minor | `RaceScreen::updateRemoteCars` | The remote car is a kinematic body of infinite mass: a hit never moves it, and the local car takes the whole response. MM2's network car is a simulated vehCar pulled toward its packets (`mmNetObject::Predict`). | deviation, by design; giving it a mass would need the remote car simulated and corrected, as MM2 does. |
| S3 | minor | `NetGame::remoteCars` | Remote cars are shown a playout delay in the past (70 ms on a LAN, 150-300 ms over the Internet); MM2 predicts them forward to the present from the average packet interval (`mmNetObject::PositionUpdate`). Racing side by side, each player sees the other a little behind and collisions happen at the past position. | deviation (smoothness over immediacy); a forward-predicted collision body is possible but would bring back the snapping. |
| S4 | minor | `Session.cpp` (snapshots) | A car whose snapshots stop (a crashed client) is held still as a solid obstacle until ENet times the player out (5-15 s). | could drop a car silent for a few seconds; not done because a stalled link would then reload the car when it comes back. |
| S5 | visible (parity) | `RaceScreen::drawRemoteCars` | A remote car's damage (replicated, 0-1) does not show: no dents, smoke or fire. MM2 sets the remote `vehCarDamage` from each packet (`PositionUpdate`, clearing it when it drops to 0). | needs `VehicleEffects` per remote car fed from the damage value. |
| S6 | minor (parity) | `CopsAndRobbers::tickLimits` | Each machine checks the time and point limits itself; MM2's host alone does and tells the others (`mmMultiCR::UpdateLimit`, `SendLimitReached`). With the shared clock (finding 13) the time limit agrees; the point limit agrees as long as the scores do. | deviation; a host-sent limit message would make it exact. |
| S7 | minor | `RaceScreen::sendLocalState` | The local player has no siren (a police car's horn key in MM2 toggles it), so `kVehicleSiren` is never sent; the remote audio and lights would play it. | outside this area (player controls). |

Lobby review O1 (the race starts a fixed 6 s after GO DRIVE instead of after
everyone's RaceReady) touches this area only in that a slow loader starts its
own race countdown late: its clock and its finish time are measured from its
own start, while Cops and Robbers' clock (finding 13) runs from the shared
start time. Nothing here depends on it otherwise.

### MM2 rules compared

* Remote cars: MM2 sends position, Euler angles, velocities and inputs at a
  per-player rate and dead-reckons them forward with a smoothing path
  (`mmNetObject`); OpenMM2 interpolates in the past (S2, S3).
* Finishes: MM2's host counts finishes against the session's players at each
  finish (0x206) with a 60/120 s timeout; OpenMM2 waits for the players still
  in the race, which also covers a finished player leaving (finding 11).
* A player quitting a race: MM2 leaves the session; OpenMM2 stays in the
  lobby, so the others are told (finding 12).
* Cops and Robbers: clock from the racers' release (finding 13); limits per
  machine (S6).

## Notes for the other areas

* The shared traffic: its client now uses the host car's playout delay and
  the frame's session time (finding 16); its messages are stamped at the
  host's frame time, while the police and off-rail cars it carries are
  physics bodies whose state is up to a step older (as finding 1 was for the
  players' cars): `TrafficHost` could stamp those with the step's time too.
* The network players' kinematic bodies report their motion in every mode,
  not only in shared-traffic cruise (finding 8).
