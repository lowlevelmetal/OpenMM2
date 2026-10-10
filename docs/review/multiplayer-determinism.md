# Multiplayer determinism review: the same results on every platform

Reviewed on 2026-10-09 on top of integration at 782f469 (protocol 10, the
host simulating every player's car). The question: does a sample of the
simulation give the same bits on a Windows build (MSVC) as on a Linux build
(GCC or Clang)? Every client predicts its own car (and the nearest players'
cars) and replays its unacknowledged inputs on every host state
(`game::PlayerCars`, `phys::World::replaySample`); the traffic agent is
moving the police and the knocked bodies to the same scheme. Any difference
between the host's machine and a client's becomes a correction. The players'
cars record had it as O6 ("MSVC against GCC: not testable here; the noise
test suggests corrections in crashes").

No wire change: the protocol stays as integration has it.

## Summary

| | Before | After |
| --- | --- | --- |
| Elementary functions in the simulation | the C runtime's: glibc against MinGW's (Wine's ucrtbase) differ for 21 % of atan2f, 14 % of tanf, 8 % of acosf, 2 % of atanf and about 1 % of sinf / cosf results (a million arguments each in the game's ranges) | OpenMM2's own (`core/Libm.h`), the same bits on every platform; float results correctly rounded, as MM2's x87 got them |
| Float contraction (a * b + c fused) | off for `mm2_phys` only; GCC (`fast` for C++) and Clang (`on`) fuse the AI, props, session and core maths on FMA targets (AArch64, `-march=native`, x86-64-v3) | off for every target, explicit on every compiler |
| A client's knocked traffic cars | collided in an unordered map's order, unstably sorted by room (differs between libstdc++ and MSVC's STL) | room, then id |
| Proof | none | `Determinism.*`: the maths (tests/core/test_libm.cpp) and a data-free minute of a car and an AI racer among props and traffic (tests/game/test_determinism.cpp) hashed against committed values on GCC, Clang, MinGW under Wine and MSVC in CI; run here on GCC, Clang (in the CI's container) and MinGW under Wine |
| Data-free scenario: Linux GCC, Linux Clang, MinGW under Wine | GCC and MinGW differ from the first checkpoint (see "Before and after") | identical, all 30 checkpoints |
| London race1 with full traffic, 40 s: the same three builds | GCC and MinGW differ (see "Before and after") | identical |
| Single-player results on Linux | | changed where glibc's own sinf, cosf, expf, logf and powf were not correctly rounded; the opponent sweep 516/517 finished and 508/517 across the line became 515/517 and 507/517 (see "Single player") |

## How it was measured

* The C runtimes: `crt_compare` (scratch) hashes the runtime's float
  functions and OpenMM2's over the same million arguments, built natively
  (glibc 2.44, GCC 16.2) and with MinGW-w64 GCC 16.2 run under Wine 11.19.
  OpenMM2's hashes agree on both; the runtimes' differ:

  | Function (range) | glibc vs OpenMM2 | Wine (MinGW) vs OpenMM2 |
  | --- | --- | --- |
  | sinf, cosf (-100 .. 100) | 1.28 %, 1.29 % | 0.13 %, 0.14 % |
  | tanf (-1.5 .. 1.5) | 0 | 14.0 % |
  | atanf (-50 .. 50) | 0 | 2.4 % |
  | atan2f | 0 | 20.6 % |
  | acosf | 0 | 7.6 % |
  | expf, exp2f, logf, log2f, powf | 0.02-0.06 % | the same as glibc |
  | sin, cos in double, rounded to float | 0 | 0 |
  | hypotf | 0 | 0 |

  (glibc 2.41 and later round atanf, atan2f, acosf, asinf, tanf and hypotf
  correctly; its sinf, cosf, expf, logf and powf come with an FMA variant
  chosen by CPU, so even two Linux machines could differ.)
* OpenMM2's functions against glibc's long double functions, every float
  argument in the game's ranges (`accuracy`, scratch): sin and cos on
  0 .. 10000 (1.18 billion arguments each), 1 and 0 results not correctly
  rounded; tan 0; atan 1 of 1.9 billion; acos 2, asin 0; exp 0, exp2 2;
  log 5 and log2 0 of 2.1 billion; pow 0 to 7 per exponent outside the
  subnormal results. The double versions are within 0.8 ulp (sin, cos,
  exp, log), 2-3 ulp (tan, atan, atan2, asin, acos) and, for pow, about
  |y log x| ulp.
* The scenario and race hashes: `test_game --gtest_filter='Determinism.*'`
  in the Linux GCC build, in a Clang build inside the CI job's container
  (`archlinux:latest` under podman, Clang 23.1.1) and in the MinGW cross
  build under Wine (also `ctest --preset mingw-cross-release -R
  '^Determinism[.]'`, as CI runs it); the race with `OPENMM2_GAME_DATA`
  set. "Before" is integration's sources (782f469) with the two new test
  files, built natively and with MinGW.
* Uninitialised values: the three `Determinism` tests of `test_game` under
  valgrind memcheck: no errors.
* The opponent sweep (`OPENMM2_AI_SWEEP=1`, OpponentRace.EveryRaceSweep)
  before and after, and with variants (see "Single player").

## Before and after

The same two tests built from integration's sources (the C runtime's
functions) and from this branch, each natively (glibc 2.44, GCC 16.2) and
with MinGW-w64 GCC 16.2 under Wine 11.19:

| Test | Before, Linux | Before, MinGW under Wine | After, both |
| --- | --- | --- | --- |
| Data-free scenario: first checkpoint where the two builds differ | 2 s (the AI racer's position, in the last bits) | | none of the 30 |
| ... the player's car at 60 s | (33.95, -177.65), 19 traffic cars knocked loose | (-14.17, -170.70), 20 cars: 48.6 m from the Linux one | (86.81, -274.64), 16 cars, on both |
| ... the AI racer at 60 s | (186.81, -16.14), 590 m along its course | (188.90, -66.28), 589 m: 50.2 m from the Linux one | (192.34, -84.44), 555 m, on both |
| London race1, 40 s, racers / traffic / props hashes | 03267684e640fe14 / 83a8e316fdd11846 / 328635f549c7579e | c09e3e7a5134bda1 / a0c821ec47d8b1a8 / 68f56a0e9133cd51 | 65eb8feebb02f101 / b8f1c125aa480ce2 / fc1321f4c3363732 |
| ... the leading racer at 40 s | (941.06, -34.23) | (941.07, -31.53): 2.7 m from the Linux one | (941.07, -33.45) on both |

The first version of the scenario (no racer, smooth ground, a few fixed
steering angles) gave the same hashes on both builds before the change too:
glibc and Wine's runtime agree on most arguments, and that scenario never
met one where they do not. The racer, the cobbles and the moving steering
were added so that the test fails on a build whose maths differ.

## Findings

### Fixed

| # | Severity | Location | What | Fix |
| --- | --- | --- | --- | --- |
| D1 | visible | 27 files: `phys` (`vehicle/Wheel.cpp` MakeRotateY and the road bumps, `TrailerJoint`, `vehicle/Transmission` and `vehicle/Controls` pow, `CarSim`, `Trailer`, `AgeMath`), `core/Math.cpp`, `ai` (Driving, DrivingTargets, DrivingRoute, PathGeometry, Traffic, Police, Opponent, Course, AmbientRoute, Pedestrians), `city/AiMap.cpp` (the intersection's road order), `game/session` (RaceSetup, Gate), `game/net` (TrafficPrediction, TrafficSync, PlayerCars, Autopilot), `net/VehicleDamage.h`, `app/Controls.cpp` | The simulation called the C runtime's sin, cos, tan, acos, atan2, exp, exp2, log2, pow and hypot, whose last bits differ between glibc, MSVC's runtime and MinGW's. The steered wheels' rotation, the AI's every steering angle and the damage encoding differed with them. | 37d2123: `core/Libm.h`, used at every call; a test fails when a simulation source calls the runtime again |
| D2 | latent | `src/phys/CMakeLists.txt`, `cmake/CompilerOptions.cmake` | Only `mm2_phys` was built with `-ffp-contract=off`. On FMA targets GCC and Clang fused the AI's, the props', the session's and the core maths' a * b + c, rounding once where the other builds round twice. The release x86-64 builds have no FMA, so it did not show there. | ee94506: `-ffp-contract=off -fno-fast-math` (MSVC `/fp:precise`) for every target |
| D3 | minor | `game/net/TrafficProxies.cpp` | A client's traffic cars that the host knocked loose were listed in an `unordered_map`'s order and sorted by room with `std::sort`; the physics collides a room's instances in the order listed, and both orders differ between standard libraries. | 222ee38: by id, stably sorted by room (as `TrafficBodies`) |
| D4 | latent | `ai/Course.cpp` (`findTurns`), `RaceScreen.cpp` (the host's nearest players for a client) | `std::sort` on keys that can tie (a circuit's turns at the same distance; two players at the same distance): equal keys come out in an order that differs between standard libraries. | 222ee38: `stable_sort` |

### Checked and found deterministic

* **Evaluation in the operands' type.** x86-64 (SSE2) and AArch64 evaluate
  float and double expressions in their own type (`FLT_EVAL_METHOD` 0) on
  every CI compiler; no `long double`, no x87 code. A 32-bit x87 build would
  differ and is not supported (a test checks `FLT_EVAL_METHOD`, the
  rounding mode and that subnormals are kept).
* **Mixed float and double.** C++ fixes the type of every operation; with
  no contraction and evaluation in the operands' type the compilers agree.
  No unqualified `sin`/`abs`/`sqrt` call whose overload could differ
  between headers.
* **Exact functions.** `std::sqrt`, `fmod`, `remainder`, `floor`, `ceil`,
  `trunc`, `round`, `lround`, `abs` are exact (or correctly rounded) by
  IEEE 754 on every runtime and stay.
* **Random numbers.** MM2's irand / frand on `std::uint32_t` (wrapping is
  defined); the cars' own streams (`CarSim::ownRandom`). `<random>`
  distributions (which differ between standard libraries) appear only in
  the music's variations and in session, port and file names.
* **Unordered containers.** Iterated only in `TrafficProxies` (D3). The
  others are looked up only: `TrafficBodies` and `BangerSet` bound caches,
  `TrafficSync`'s sets of ids sent, `NetTrafficCars`, `TrafficPrediction`
  (its iteration only clears flags), `MapView`'s player tracks, the race
  screen's sets, `CityLevel`'s load-time caches, the renderers'.
* **Sorting.** The other sorts in the simulation have no ties: the polygon
  soup's ids, the AI's ambient model names, the route search's
  (cost, node) pairs, `TrafficBodies`' stable sort of rail cars.
* **Pointer order.** No container or sort keyed on a pointer's value.
* **Threads.** The simulation runs on the main thread only (the music, the
  port mapping, the intro movie and the setup screen have their own).
* **Order of evaluation.** No expression draws twice from one random
  stream (the comma-separated declarations that do are sequenced). Function
  arguments are evaluated in an unspecified order (GCC and MSVC from the
  right, Clang from the left); a Clang build gives the GCC build's hashes
  (see "What CI should show"), so nothing the tests run depends on it.
* **Uninitialised values.** None under valgrind in the scenario and the
  40 s race.
* **Data.** Numbers are parsed with `std::from_chars` (correctly rounded in
  libstdc++ and MSVC's STL); the game files are listed sorted
  (`vfs::Vfs::list`).

## Single player

The full suite passes unchanged (1009 tests, plus the 8 new ones): no
expected value moved. The single-player simulation on Linux changed where
glibc's own float functions were not correctly rounded: sinf and cosf for
about 1 % of the arguments the AI and the wheels use, expf, logf and powf
for 0.02-0.06 %. Everything else gives the same bits as before (glibc's
atan2f, acosf, tanf and the double sin and cos rounded to float, which
`phys::age` used, are already correctly rounded).

The opponent sweep went from 516/517 finished and 508/517 across the line
to 515/517 and 507/517: one racer of sf race7 (professional) is wrecked at
92 % of its course; 81 of the 88 races' other figures (times, backups,
resets) changed too. Variants show that this is the sweep's sensitivity to
the last bit of the steering, not a fault:

| Variant | Finished | Across the line | Races whose line changed |
| --- | --- | --- | --- |
| Before (integration) | 516/517 | 508/517 | |
| After | 515/517 | 507/517 | 81 of 88 |
| After, the runtime's sinf and cosf kept everywhere | 516/517 | 508/517 | 6 of 88 (no count changed) |
| After, the runtime's sinf and cosf kept everywhere but the car's MakeRotateY and road bumps | 514/517 | 507/517 | 81 of 88 |

MM2 computed these with fsin and fcos, whose results are correctly rounded
when stored to a float; the new values are MM2's, glibc's sinf was not
(and was not the same on every CPU).

## The determinism tests

* `Determinism.LibmFloatValues`, `LibmDoubleValues`, `LibmSpecialValues`,
  `LibmSweepHashes` (test_core): known values, the C standard's special
  cases, and one hash per function over 200 000 arguments.
* `Determinism.SimulationCallsNoRuntimeTranscendentals` (test_core): the
  simulation's sources call no runtime sin, cos, atan2, exp, log, pow or
  the like (it reads the source tree; skipped without it).
* `Determinism.FloatEnvironmentIsIeee` (test_game).
* `Determinism.ScenarioHashesMatchEveryPlatform` (test_game, no game data):
  a made-up sports car (`CarSimParams` defaults with 450 hp) on fixed,
  always slightly moving inputs through the steering filter (pow), on
  cobbled ground (the wheels' road bumps), along a block of four 200 m roads
  with ambient traffic at density 1 (`ai::Traffic` at 30 Hz,
  `TrafficBodies`), through 14 made-up props (cones that break loose,
  crates, a sign that splits), over a jump, braking into reverse, a
  handbrake turn and then circling and weaving through the traffic for the
  rest of the minute, while an `ai::Opponent` (the same sports car) laps the
  block: 16 traffic cars knocked loose, 27 props moved, the racer 555 m
  along its course. The cars, the traffic and the props are hashed every
  two seconds. A mismatch reports the first checkpoint that differs and
  which of the three, then the platform's table and where the cars were at
  each checkpoint (in hex too), to compare with the Linux log. It takes
  about 50 ms.
* `Determinism.RetailRaceThroughTraffic` (test_game, game data): the first
  40 s of London's race1 with its six racers and full traffic, hashed
  (the racers' trails and states, the traffic, the props). CI has no game
  data; it runs locally and on Windows with `OPENMM2_GAME_DATA`.

CI runs them on all four builds: the Linux GCC and Clang jobs and the
Windows MSVC job with the rest of the suite, the MinGW job under Wine
(`wine` installed in its container, `wineboot --init`, then `ctest -R
'^Determinism[.]'`).

### What CI should show

* Linux GCC: everything passes (the hashes are this build's; also in a
  build with `-D_GLIBCXX_ASSERTIONS`, and under valgrind).
* Linux Clang: passes. Checked here in the CI job's own container
  (`archlinux:latest` under podman, Clang 23.1.1, the CI's packages): all
  eight `Determinism` tests pass with the GCC build's hashes, the London
  race included (with the game data mounted).
* MinGW under Wine: passes. Checked here with the CI step's own commands
  (`wineboot --init`, `ctest --preset mingw-cross-release -R
  '^Determinism[.]'`) and the same Wine (11.19) and MinGW GCC (16.2) as the
  Arch container installs; the London race passes too when run with the
  game data.
* Windows MSVC: should pass, and is the one build not run here. MSVC's
  `/fp:precise` on x64 neither fuses nor widens, and the maths no longer
  touches its runtime. A failure would point at a remaining source: the
  report names the first checkpoint and part (cars, traffic, props) that
  differ and prints where the cars were at each checkpoint, to compare with
  the Linux log; the libm tests isolate the maths.

## Open

| # | Severity | What | Needs |
| --- | --- | --- | --- |
| O1 | unknown | MSVC could not be run here (GCC, Clang and MinGW under Wine were); CI is the proof. | The PR's CI run. |
| O2 | minor | Where MM2's compiled code keeps an fsin or fpatan result in an x87 register for the next operation (e.g. `sin(a) * r` multiplied before the store), OpenMM2 rounds the function's result to float first. A parity question per call, as before this change. | The parity audits, call by call. |
| O3 | minor | Nothing resets the floating-point environment at run time: a library that set flush-to-zero or another rounding mode on the main thread would change the results (no known one does; the test checks the defaults). | A guard in the frame loop if it is ever seen. |
| O4 | minor | `libm::pow` is accurate to about \|y log x\| ulp in double (float results correctly rounded but for 1 in 10^7 or so); the game uses it for the gearing, the steering curve and the AI's per-frame factors. | Nothing unless a use needs double accuracy. |
| O5 | info | The opponent sweep moved by one car (see "Single player"). | Nothing; the new values are MM2's rounding. |
