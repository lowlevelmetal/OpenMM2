# Multiplayer

The original game used DirectPlay (TCP/IP, IPX, modem and serial links). It is
long obsolete, doesn't work through home routers, and isn't available on
Linux. OpenMM2 replaces it with its own protocol over UDP. The module is
`src/net` (`mm2_net`, namespace `mm2::net`). It carries no game rules: it moves
lobby state, vehicle states and opaque game events, and the game module
decides what they mean.

## Components

| Header | Purpose |
| --- | --- |
| `net/Net.h` | `Address` (IPv4 endpoint), `NetLibrary` (ENet/Winsock init), default ports |
| `net/Transport.h` | ENet host wrapper: peers, channels, stats |
| `net/Session.h` | Lobby + in-game session (host or client), event queue |
| `net/Protocol.h` | Wire messages, settings/player structs, game event payloads |
| `net/Snapshot.h` | `VehicleSnapshot` and the receive-side `SnapshotBuffer` |
| `net/AmbientState.h` | The shared cruise traffic: `AmbientStateMsg`, `TrafficHitEvent` |
| `net/ClockSync.h` | Session clock estimation on clients |
| `net/Discovery.h` | LAN beacon/scanner, broadcast addresses, small UDP socket |
| `net/PortMapper.h` | Automatic port forwarding (UPnP, PCP, NAT-PMP) |
| `net/BitStream.h` | Bit-packed, bounds-checked serialization |

`tools/netprobe` exercises all of this from the command line (see Testing).

### Threading

Everything except `PortMapper` is single-threaded and **polled from the game
thread**: call `Session::update()` (and `LanScanner::update()` while the server
browser is open) once per frame. None of these calls block. The exception is
`Address::resolve()`, which may do a DNS lookup. Polling from the game thread
keeps the ENet state, the player list and the event queue free of locks.

`PortMapper` runs a worker thread, because UPnP and NAT-PMP requests to the
router can take seconds. `status()` is thread-safe. The optional callback runs
on the worker thread, so the UI must marshal it to its own thread (or just poll
`status()`).

## Topology

Sessions are client/server. The host is a player too, and it is authoritative
for the lobby: settings, the player list, ready flags, kicks and the race
start.
Clients send requests and the host validates and broadcasts the result.

In game, each machine simulates its own car and sends its state 20 times a
second, stamped with the session time the simulation was at. The host sends
its own car on that cadence and passes each joiner's state on to the others
as soon as it arrives (bundling them per tick added up to a tick of latency
and dropped one of two states that arrived within a tick).
Remote cars are drawn from an interpolation buffer a playout delay in the past:
for each car, what its snapshots needed to arrive in time over the last 3 s
(50-500 ms; about 70 ms on a LAN, 150 ms over a 100 ms round trip, 250 ms from
one client to another through the host). When packets stop coming, a remote
car is extrapolated for up to 250 ms and then held still. Cars are replicated
during a race only (its loading, countdown and the game): a state submitted
in the lobby is ignored, a late one arriving there is dropped, and the race's
order and the return to the lobby both clear every car's buffer, so nothing
of one race reaches the next.

There is no host migration. If the host leaves, every client gets
`Disconnected{HostShutdown}`.

## Ports and firewalls

| Port | Protocol | Use |
| --- | --- | --- |
| 2300 (configurable) | UDP | Game traffic (ENet), host only needs it reachable |
| 2301 | UDP | LAN session discovery |

* **Hosting over the Internet** needs UDP 2300 reachable from outside. That
  means a port forwarding on the router (automatic, see below, or manual), plus
  a firewall rule on the host machine.
* **LAN discovery** needs the host to accept UDP 2301 from the LAN. The client
  must accept the host's unicast reply to its query, which arrives on an
  ephemeral port. Stateful firewalls don't link a reply to a *broadcast* query,
  so that reply counts as unsolicited. Desktop Linux firewalls such as ufw
  therefore block discovery by default; `sudo ufw allow from 192.168.0.0/16 to
  any port 2300:2301 proto udp`, or allowing the LAN subnet, fixes it. On
  Windows the firewall asks once for `openmm2.exe`. The installer should add an
  allow rule for the program.
* "Join by address" always works when discovery doesn't, because it only needs
  UDP 2300 at the host.

## Automatic port forwarding (`PortMapper`)

`PortMapper::start(config)` tries the following, in order:

1. **UPnP IGD** (miniupnpc). It discovers the router via SSDP and reads its
   external address. It then calls `AddPortMapping` for UDP with a lease
   (default 3600 s).
   * If the external port is already forwarded to *another* device, or the
     router answers `718 ConflictInMappingEntry`, it moves on to the next
     external port, up to `maxPortAttempts` times. The internal port stays the
     same, and the external port actually granted is the one to advertise.
   * A mapping that already points at this machine is taken over.
   * If the router accepts only permanent leases (`725`), it retries with lease
     0. Clean removal on exit matters even more in that case.
2. **PCP** (RFC 6887) MAP request to the default gateway on UDP 5351. If the
   gateway answers in NAT-PMP form ("unsupported version"), the backend falls
   back to:
3. **NAT-PMP** (RFC 6886).

Discovery time is bounded by `discoveryTimeoutMs` (default 2.5 s, minimum 1 s
for UPnP because of the SSDP MX value). miniupnpc's own `upnpDiscover()` would
wait about 4× longer, so the backend sends every search type and then waits
once.

While mapped, the lease is renewed at half its lifetime. After three failed
renewals the status turns `Failed` ("Lost the port mapping…"), but retries
continue and the status recovers on success. `stop()` removes the mapping.

**Crash safety.** With `config.stateFile` set (the app should use
`<user data dir>/portmap.ini`), the active mapping is recorded on disk. On the
next start, a recorded mapping is removed before anything else, but only on the
same gateway, and with UPnP only if the router still shows it pointing at this
machine. Mappings also expire on their own once the lease runs out. A record
whose ports are not 1-65535 is ignored, and the PCP/NAT-PMP backend never
sends a deletion for internal port 0: both RFCs define that as "every mapping
of this machine". An external address a UPnP device reports is shown only if
it parses as an address (any device on the LAN can answer SSDP as the router).

**Status for the UI.** `PortMappingStatus::message` is a one-line summary such
as:

* `UDP port 2300 forwarded via PCP (external address 203.0.113.7:2300)`
* `UDP port 2300 forwarded via UPnP as external address 203.0.113.7:2302`
* `Automatic port forwarding failed. Forward UDP port 2300 to 192.168.1.20 on
  your router, or play with people on the same network`

`details` lists what each method reported, for a details pane or the log.
`doubleNat` is set when the router's own Internet address is private (RFC 1918
or carrier-grade NAT 100.64/10). The mapping then only reaches the first router,
and the message says so.

Not implemented: NAT hole punching, relay servers and IPv6. ENet 1.3 is
IPv4-only. An IPv6 path would need a different transport or ENet's unofficial
IPv6 fork.

## LAN discovery

The packets are small, bit-packed, and separate from ENet:

* Query: `"M2LQ"`, protocol version (u16), nonce (u32).
* Advert: `"M2LA"`, protocol version, nonce echo (0 for unsolicited), then
  session name, host name, city, mode, race, players/max, password flag, phase,
  game port and build string.

Hosts (`LanBeacon`, owned by `Session` when `HostParams::advertiseOnLan` is
set) bind UDP 2301 with address reuse. They answer queries with a unicast
advert and broadcast an unsolicited advert every 2 s. Scanners (`LanScanner`)
send queries from an ephemeral port, both to every interface's directed
broadcast address and to 255.255.255.255. The reply's nonce gives the ping.
Entries expire after 6 s without an answer.

An advert can be 16 times the size of a query, so the beacon must not answer
anyone who asks: it answers only loopback, private addresses (RFC 1918,
CGNAT 100.64/10, link-local) and addresses on one of this machine's interface
subnets (so a VPN adapter on a public range, such as Hamachi's 25/8, still
works) (`answersLanQueryFrom`). It never answers a broadcast, multicast or zero
source address, and sends at most 20 replies a second with a burst of 40.
A scanner lists at most `kMaxLanSessions` (64) sessions, skips adverts with
game port 0, and cleans an advert's text as described under "Untrusted input".

Passive listening for unsolicited adverts is optional and off by default. On
Linux, a UDP unicast datagram to a port that several sockets share
(`SO_REUSEADDR`) reaches only one of them. A passive scanner socket can
therefore swallow a loopback query meant for a host on the same machine.

Adverts whose protocol version differs are still listed, but flagged
`compatible() == false`, so the browser can say "different version".

## Wire protocol

Transport is ENet 1.3 with range-coder compression and four channels:

| Channel | Delivery | Traffic |
| --- | --- | --- |
| 0 Control | reliable, ordered | handshake, lobby, chat, clock sync, pings |
| 1 State | unreliable, unsequenced (late snapshots still fill the buffer; ENet's throttle never drops them) | `VehicleState` (client→host), `WorldState` (host→clients) |
| 2 Events | reliable, ordered | `GameEvent` |
| 3 Ambient | unreliable, sequenced | `AmbientState` (host→clients): the shared cruise traffic |

Every packet is one message: a `MsgType` byte, then the body, bit-packed with
`BitStream`. Readers check every length, count and enum range. A malformed
message is dropped, and an unparseable `Hello` disconnects the peer with
`ProtocolError`.

### Versioning

The ENet connect request carries `kConnectData = 'M2' << 16 | kProtocolVersion`.
A host disconnects mismatched peers right away with `VersionMismatch`, so
incompatible builds never parse each other's messages. **Any change to message
layouts must bump `kProtocolVersion`.** `Hello` carries the version again,
along with a free-form build string. Version 2 added the shared cruise
traffic, version 3 the race start handshake (`RaceLoad` in place of
`Countdown`, `RaceLoaded`, `RaceStart`, the race in `Welcome`).

### Handshake

```
client                                host
  |--- ENet connect (kConnectData) ---->|  version / capacity check
  |<-------- Challenge(nonce, pw?) -----|
  |--- Hello(name, car, colour, team,   |
  |          SHA-256(nonce||password)) >|  password, capacity, join-in-progress
  |<-- Welcome(id, settings, players,   |
  |            phase, hostTime) --------|  or Reject(reason) + disconnect
  |                                     |--> PlayerJoined to everyone else
```

The password never crosses the network. The proof is a SHA-256 over a fresh
16-byte nonce and the password. This only protects the password from casual
sniffing; traffic is not encrypted, and someone who records a handshake can
still try passwords offline against it. Online guessing is slowed down: after
five wrong passwords from one address within a minute, the host disconnects
that address's new connections with `BadPassword` before the challenge until
the minute is over.

A handshake must finish within `joinTimeoutMs` (10 s), and an ENet connect
attempt within `connectTimeoutMs` (8 s).

### Messages

| Message | Direction | Content |
| --- | --- | --- |
| Challenge | H→C | nonce, password required |
| Hello | C→H | protocol version, build, name, car, colour, team, password proof |
| Welcome | H→C | your id, settings, player list, phase, host time, the race's number and order time (in the lobby the last race's); during a race its start (if set) and the players who have loaded it |
| Reject | H→C | reason, text |
| PlayerJoined / PlayerUpdate | H→C | `PlayerInfo` |
| PlayerLeft | H→C | id, reason |
| PlayerRequest | C→H | car, colour, team, ready |
| Chat | both | sender id (set by host), text ≤ 200 bytes |
| Settings | H→C | `SessionSettings` |
| RaceLoad | H→C | GO DRIVE: the race's number (the host's, from 1) and the session time of the order |
| RaceLoaded | both | C→H: this machine has loaded race N; H→C: player P has (see "Race start") |
| RaceStart | H→C | race N starts (its countdown ends) at this session time |
| ReturnToLobby | H→C | — (ready flags reset) |
| Kick | H→C | reason text (then disconnect `Kicked`) |
| TimeRequest / TimeResponse | C↔H | client send time / + host time |
| VehicleState | C→H | own `VehicleSnapshot` |
| WorldState | H→C | `(id, VehicleSnapshot)` pairs: the host's car each tick, a joiner's as it arrives |
| GameEvent | both | from, target, type, session time, payload ≤ 1 KiB |
| PlayerPings | H→C | measured RTT per player, every 2 s |
| AmbientState | H→C | the shared traffic and police near the client (see "Shared traffic") |

`SessionSettings` holds the session name, city, mode (Cruise, Checkpoint,
Circuit, Blitz, Cops & Robbers, Crash Course), race id, laps, time of day,
weather, traffic and pedestrian density (percent), cops on/off, max players
(protocol limit 16; the original allowed 8), a password flag, the
join-in-progress policy, whether a cruise shares the host's traffic and police
(`sharedTraffic`, on by default; protocol version 2), and up to 32 extra
key/value pairs. The extra pairs let
the game add options without a protocol bump.

### Untrusted input

Everything that arrives is untrusted: other players, anyone who can send a UDP
packet to the game or discovery port, and whatever answers as the router.

* **Sizes.** ENet accepts at most a 64 KiB packet and 1 MiB of a peer's
  undelivered data (`TransportConfig::maxPacketSize`, `maxWaitingData`; ENet's
  own defaults, 32 MiB each, let any connected peer make this machine allocate
  a whole packet with its first fragment). The largest message, a 16-player
  `Welcome` with 32 extra settings, is about 5 KiB. `Transport::send` refuses an
  empty packet (ENet's range coder would read its null data).
* **Text.** Names, chat, the session name, kick and reject reasons and an
  advert's strings are cleaned on receipt by `sanitizeText`: well-formed UTF-8
  only, no control characters (tab and line breaks become spaces), trimmed, cut
  on a character boundary. The host does it for what joiners send, clients for
  what the host sends.
* **Names of files.** Car and city names must be plain base names
  (`isValidAssetName`: 1-32 characters of `[A-Za-z0-9_-]`). The host gives a
  joiner with any other car `kDefaultCar` and keeps the previous car on a
  request; a client does the same with the host's player list and ignores
  (empties) such a city, which then loads nothing. A valid name this machine
  lacks, such as an add-on car, is shown as MM2's default vehicle `vpcoop`
  (`game::netVehicle`, after `mmVehList::GetVehicleInfo`).
* **Counts.** A client keeps at most `kMaxPlayers` players, never one with
  `kInvalidPlayerId`, and refuses a `Welcome` that does not list it.
* **Floods.** The host relays what one joiner sends to every other player, so
  each joiner has a budget: chat 2 lines a second (burst 8), player updates 10
  (burst 20), game events 30 (burst 60). Chat and events beyond it are dropped;
  a `PlayerRequest` is always applied and its `PlayerUpdate` relayed once the
  budget allows, so the latest car, colour, team and ready state still
  arrives.
* **Floats.** Snapshot fields are quantized to fixed ranges and event floats
  must be finite (`ReadStream::f32`), so no NaN or infinity reaches physics or
  rendering.
* **Shared traffic.** An `AmbientState` holds at most 96 cars, ids 0-511,
  generations 0-7, catalog indices 0-63 and paint jobs 0-15 (ranged fields:
  nothing else can be read), quantized positions, velocities and spin, and a
  police target that is a player id or none. A client takes it only during a
  race, keeps at most 16 unread, and the race refuses a car whose model is not
  in its own catalog, a repeated id, or a non-finite value, holds a paint job
  to the jobs the model can show, shows at most 32 police cars and loads each
  model once per car shown (a model that fails is not tried again). The host
  takes a client's `TrafficHit` only for a car still on its rail near that
  client's car, with its velocity quantized to 96 m/s.

### Session clock

The host's clock (ms since the session started) is the shared timeline.
Clients estimate it NTP-style: offset = hostTime + rtt/2 − receiveTime. They
send a quick burst of six requests after joining, then one every 2 s. Of the
last 8 samples, the one with the lowest RTT is used, since it has the least
asymmetry error. The race start, snapshot times and game event times are all
in session time.

The clock a client shows (`Session::time()` / `timeMs()`, sub-millisecond)
takes a new estimate at once in the lobby. While a race is loading or running
it slews toward it instead, at most 5% faster or slower than real time
(`SlewedClock`; errors over 250 ms are still stepped): the remote cars are
drawn at session times, and a stepped clock moves every car by its speed times
the step.

### Vehicle snapshots

`VehicleSnapshot` is 274 bits (about 35 bytes):

| Field | Encoding |
| --- | --- |
| time | u32 session ms |
| position | 3 × 24 bits over ±16384 m (≈2 mm) |
| orientation | smallest-three quaternion, 2 + 3 × 10 bits |
| linear velocity | 3 × 16 bits over ±200 m/s |
| angular velocity | 3 × 14 bits over ±64 rad/s |
| steering / throttle / brake / handbrake | 8 / 7 / 7 / 4 bits |
| gear | −1…14 |
| damage | 10 bits, 0…1 |
| flags | horn, siren, headlights, brake lights, wrecked, off-road |

A 16-car `WorldState` fits in one packet below the ENet MTU (checked by a
test). Receivers interpolate position with a cubic Hermite spline using the
replicated velocities, and orientation with slerp. Extrapolation integrates
linear and angular velocity.

**Time stamps.** A snapshot's time is the session time its state belongs to.
The game's simulation runs in fixed 1/60 s steps, so the car it sends is up to
a step older than the frame; the race passes that age
(`NetGame::submitLocalState(..., stateAgeMs)`), measured from the session time
`NetGame::update()` saw at the start of the frame. Stamping the frame's time
instead made 50 ms of stamps carry 3 or 4 steps of motion, and the car surged
back and forth by up to a third of a metre at speed. A receiver drops states
stamped more than 1 s ahead of its clock or 5 s behind it (a broken sender's
times would drag the car), and the host does not relay them. Nothing is
replicated in the lobby.

**Playout delay.** `SnapshotBuffer::push(snapshot, arrival)` records, for each
snapshot newer than all before it, its lateness (arrival minus stamp: transit,
send interval, the host's relay tick, the sender's clock error) plus the gap
since the previous one. `requiredDelay()` is the largest of these over the
last 3 s, a gap left by a lost snapshot counting as at most 1.5 times the
median gap (a single loss is bridged by a short extrapolation). The session
moves each car's delay toward it plus 4 ms, within
`SessionConfig::interpolationDelayMs` (50) and `maxInterpolationDelayMs`
(500): up at 25% of real time, down at 2%, so the shown time never jumps or
runs backwards.

### Game events

`Session::sendGameEvent(type, payload, target)` delivers reliably and in order.
The host relays events. A host-side filter (`setEventFilter`) can validate or
drop them, for example to check that a checkpoint is plausible. Well-known
types, with payload structs in `Protocol.h`:

`CheckpointReached{index, raceTime}`, `LapCompleted{lap, lapTime}`,
`RaceFinished{raceTime, position}`, `GoldPickedUp/GoldDropped/GoldDelivered
{position, team}`, `Collision{other, position, impulse}`,
`Damage{damage, source}`, `Wrecked`, `LeftRace` (a joiner quit the race it
was driving and stays in the session: the others take its car out and stop
waiting for its finish). Ids from `GameEventType::Custom` (0x8000)
up are free for the game: Cops and Robbers uses 0x8001 and 0x8002, the shared
cruise traffic's hit report (`TrafficHitEvent`, client to host) 0x8010.

### Race start

MM2's network races do not start on a timer (`mmMultiRace`, `mmMultiCircuit`,
`mmMultiBlitz::UpdateGame` state 0, `GameMessage`, `SystemMessage`;
`mmGameMulti::SendRaceReady`, `GameMessageCB`):

* After loading, a machine sits on the grid with every car held
  (`InitNetworkPlayers`: `vehCar::SetDrivable(0, 1)`) and the other players'
  cars not shown, and shows nothing.
* It counts 5 s of frames, then sends RaceReady (0x1f6, with its car's CRC;
  0x213 after a restart) to every player. Every machine starts its count of
  the others at the number of players less one, takes one off for each
  RaceReady (a second one from the same player is not counted) and one for
  each player who leaves meanwhile (`SystemMessage` 0x2d), and on a RaceReady
  places that player's car on its grid and shows it
  (`mmGameMulti::GameMessageCB` 0x1f6).
* Once it has sent its own and while its count is above 0, it shows "Waiting
  for N players" (strings 31-37, "Waiting for 1 player" ... "Waiting for 7
  players", picked from a `|`-separated list by the count) for 5 s, every
  frame.
* When the host's count reaches 0 it sends the start (0x20f) and begins its
  countdown; every other machine begins its own when 0x20f arrives: Ready...
  for 1.25 s, Set... for 1.25 s (each with the low start sound), then Go! with
  the high one, when the racers are enabled and the HUD timers start
  (`EnableRacers`, `StartTimers`, `ResetTimers`). Each machine's race time is
  its own from its Go.
* There is no time limit on the wait: only a player leaving the session (or
  DirectPlay dropping one) releases it. A player who leaves after its
  RaceReady is taken off the count a second time, so the host can then start
  while another machine is still loading; that machine finds 0x20f waiting
  in state 0, starts its countdown at once and never sends its RaceReady.
* Cops and Robbers (`mmMultiCR::Init`, `UpdateGame`, `GameMessage` 0x25c /
  0x25d) does not wait: the host starts at once, and each joiner when the
  host's game state reaches it, with "Go!" and no countdown. A network cruise
  (`mmMultiRoam::Init`, `UpdateGame`) reports its machine loaded
  (`PlayerFinishedLoading`, `SendGameSet` 0x1fa) and lets the car go at once;
  each machine shows "<name>" / "has joined" (string 42) with the net alert
  as another's report arrives (`GameMessageCB` 0x1fa).

OpenMM2 (`net::Session`, `game::NetRaceStart`) does the same over its own
protocol, with a shared start time:

1. GO DRIVE: the host numbers the race (`raceNumber`, from 1, the same on
   every machine) and sends `RaceLoad` with it and the session time of the
   order; every machine loads the race (`SessionPhase::Countdown`) and keeps
   its session serviced between the load steps.
2. Loaded: a race machine counts 5 s of frames, as MM2 does, then reports
   (`reportLoaded`: `RaceLoaded` with the race number); Cops and Robbers and
   cruise report at once. The host marks the player and relays the report
   (`RaceLoaded` with the player's id) to the others. Every machine knows who
   has loaded (`playerLoaded`), shows "Waiting for N players" while another
   player still in the session has not (from its own report on), and shows a
   network car only once its player has reported.
3. Start: once every player still in the session has reported, the host sets
   the start, the session time of the Go: now, plus a lead for the message to
   reach every machine (twice the slowest round trip plus 100 ms, 200 ms to
   1 s), plus the mode's countdown (2.5 s in the races, none in Cops and
   Robbers and cruise), and sends it (`RaceStart`). Every machine runs its
   countdown on its session clock up to that time (Ready... while more than
   1.25 s are left, Set... while any are, Go! at it), so every countdown ends
   together, whenever the message arrived; the session's phase turns to
   InGame there. MM2's machines begin their countdown when 0x20f arrives,
   one trip apart.
4. A player who leaves, is ejected or times out no longer holds the start;
   neither does a joiner who quits the race or cannot load it
   (`NetGame::sendLeftRace` reports it first). A host that leaves ends the
   session as before.
5. Upper wait (OpenMM2's own: MM2 has none): 60 s after the order
   (`SessionConfig::loadWaitMs`) the host starts without the players still
   loading, logging who. Such a machine already has the start when it
   finishes loading (its session is serviced while it loads); it reports at
   once, so the others show its car, and runs the whole countdown on its own
   from then, joining the running race: what an MM2 machine does with a start
   message that waited in its queue. Its race time is its own from its Go, as
   every MM2 machine's is; the race's finish timeout (60 s in a race, 120 s
   in a circuit after the first finish) still ends a race it never joins. This
   keeps a stalled machine from holding everyone without dropping a player
   who is only slow; removing the player was the alternative.
6. A report for another race (one sent before a return to the lobby reached
   the player), a repeat, or one in the lobby is dropped; a report that comes
   after the start no longer changes it but still tells the others the
   player is in. A client ignores a start or a report out of turn, a second
   start, a report for a player it does not know or for itself, and a
   `RaceLoad` that does not number a newer race.
7. A machine still loading leaves the race when the host takes everyone back
   to the lobby or the session ends (the loading screen checks between its
   steps).

Cops and Robbers waits for everyone too in OpenMM2 (deviation): its rules,
time limit and warnings run on the shared start's clock, and the first set
of places is seeded with the race's order time, which every machine knows
when it loads. Cruise never waits.

Measured on loopback with a joiner loading 4 s more slowly
(`OPENMM2_DEBUG_LOAD_DELAY_MS=4000`), a circuit, twice in one session with the
cars changed in between: race 1 countdown from session time 20209 (host) and
20213 (joiner), Go at 22712 and 22710 for a start at 22709; race 2 countdown
from 47474 and 47477, Go at 49979 and 49976 for 49974 (a frame is about 6
ms). Version 0.2.0's fixed start (6 s after GO DRIVE), measured the same way:
the joiner's countdown began 0.5 s late and it went 560 ms after the host
(17512 against 16952); 6 s slower, it went 2.55 s after the host (19523
against 16969). Through `netprobe relay` (60 ms each way, 20 ms jitter, 5 % loss) the
lead was 358 ms, the start reached the joiner 145 ms after it left the host,
and both went at 22993 for 22988. A joiner that never finished loading was
waited for 60 s, the host raced alone from then, and when the host returned
to the lobby the joiner left its loading screen for the lobby.

### Shared traffic

An OpenMM2 extra (MM2's network cruise has no traffic and no police,
`mmGameMulti::Init`; docs/parity/openmm2-only.md): in a multiplayer cruise
with the host's `sharedTraffic` on, the host runs the ambient traffic and the
police and sends each client, every 50 ms, an `AmbientState` with the cars near
it (`game::TrafficHost`, `src/game/net/TrafficSync.h`):

| Field | Encoding |
| --- | --- |
| header | u32 session time, u32 the host's light-set steps (1/30 s) since its AI reset, u16 catalog checksum, 3 × i16 origin (whole metres at the client's car), count |
| id, generation | 9 + 3 bits: the traffic pool slot (0-299) or 400 + the police car's place; the generation changes when the slot is reused |
| has state | 1 bit; without it only the id and generation travel (13 bits) |
| kind, model, paint | 1 + 6 + 4 bits: traffic or police, the catalog index (the traffic's vehicle types in the AI map's order, then the police posts' cars: both machines build it from their data), the paint job |
| position | 15 + 14 + 15 bits: ±512 m across, ±256 m up from the origin (3 cm) |
| orientation | smallest-three quaternion, 32 bits |
| flags | brake, horn, indicators left / right (both: hazards), off its rail, wreck (police: out of action), siren, pursuit |
| motion | a car on its rail: its speed along its heading (11 bits, ±64 m/s; the AI's velocity is exactly that); off its rail and police: velocity 3 × 12 bits (±96 m/s) and spin 3 × 11 bits (±32 rad/s) |
| police | target player (5 bits, 16 = none), damage, rpm, throttle, gear (27 bits) |

A car on its rail takes 119 bits, a police car 204. Each client gets the
police chasing it, then the cars within 200 m of its car (kept until 230 m,
so a car on the edge does not come and go), nearest first, as many as fit in
1100 bytes. Cars within 80 m, off their rails, police and cars new to the
client carry their state in every message; the others in every other one
(alternating by id), and say only "still there" in between.

Each message is complete for its client: a car the client knows that a newer
message leaves out has gone (out of range, or back in the pool) from that
message's time; an older message (reordered) adds its states to the cars
still known but never brings back or replaces one; a new generation is a new
car, shown where it is rather than blended from its old place, and so is a
car that moved 25 m (plus 30 m/s of velocity change) further than its
velocity explains; a car not heard of for 1.5 s is dropped. The client
(`game::TrafficClient`) interpolates the cars with the remote players'
`SnapshotBuffer` (Hermite on the velocities, 100 ms behind, extrapolated at
most 250 ms) and runs its light sets to the host's steps at that time (they
are deterministic from a reset). Its physics proxies stand at that time; the
cars are drawn a physics step (16.7 ms) earlier (`TrafficClient::transformAt`),
where the rest of the scene is drawn.

A client's car meets the received cars as kinematic instances moving at their
interpolated velocities (`game::TrafficProxies` for the traffic, kinematic
bodies for the police). The host's view of the client's car lags the client's
own, so a client that hits a moving traffic car often misses it on the host;
it therefore reports the hit (`TrafficHitEvent`: the car's id and generation
and its own velocity before the step, reliable, at most once a second per
car). If the car is still on its rail on the host and the client's car is near
it there (8 m plus half a second of the relative speed, at most 25 m), the
host knocks it off as the hit would have, with the impulse of a car of the
client's mass meeting it at that velocity (elasticity 0.25); the next messages
bring the result back. A hit the host saw itself has already taken the car off
its rail, and the report is dropped.

Bandwidth, two players in San Francisco at traffic density 0.5 (the
single-player cruise default) and cop density 1, measured by the host's
`nettraffic` log: 18.5 messages a second to the client, 63 cars a message,
11.0 KB/s (17.5 KB/s before the far cars' alternation); with a second client
600 m away, 10.6 and 10.0 KB/s (60 and 54 cars), each client its own cars and
the roads round each populated. The 1100-byte budget bounds it at about
20 KB/s per client, 140 KB/s for seven clients. The
client's cars within 120 m were 2 cm from the host's at the same session time
(median; 90 % within 10 cm, 27 cm at worst for far cars between their
messages).

### Disconnect reasons

`Left`, `VersionMismatch`, `ServerFull`, `BadPassword`, `GameInProgress`,
`Kicked`, `HostShutdown`, `Timeout`, `JoinTimeout` and `ProtocolError`. They
are sent as ENet disconnect data and in `Reject`/`PlayerLeft`, and turned into
text with `describe()`.

## Using it from the game

```cpp
net::Session session;
session.host({.settings = s, .player = {.name = "Me", .car = "vpbug"}});
// or: session.join({.host = *net::Address::resolve("1.2.3.4"), .player = me});

net::PortMapper mapper;            // when hosting over the Internet
mapper.start({.internalPort = session.port(), .stateFile = dataDir / "portmap.ini"});

// every frame
session.update();
for (auto& e : session.takeEvents()) std::visit(handler, e);
if (session.phase() == net::SessionPhase::InGame) {
    session.submitLocalState(myCarSnapshot);
    for (auto& p : session.players())
        if (p.id != session.localId() && session.sampleRemote(p.id, snap) != net::SnapshotBuffer::Result::Empty)
            drawRemoteCar(p.id, snap);
}
```

## Testing

* `test_net`: serialization round-trips and malformed input, SHA-256 vectors,
  PCP/NAT-PMP byte layouts, the interpolation buffer, clock sync, and loopback
  sessions (join, chat, lobby updates, password, full session, version
  mismatch, unreachable host, the race start, snapshot delivery, event relay and
  filtering, kick, host shutdown, join-in-progress). It also covers
  `PortMapper` against fake backends: failure reporting, fallback, port
  conflicts, renewal and loss, crash cleanup, other-gateway records, double NAT
  and restart. None of it needs a router. `test_hostile_input.cpp` drives a
  real session from a hand-built host or client that sends what an honest one
  never would, and `test_fuzz.cpp` feeds fixed-seed random and mutated
  messages to every decoder and to a live host and client. Build `test_net`
  with `-fsanitize=address,undefined` to run them under the sanitizers (ENet's
  range decoder shifts a byte into an `int`'s sign bit; suppress
  `shift-base:compress.c`).
* `netprobe info | host | join | scan | portmap | relay` for manual testing on
  real networks. `netprobe relay <host> --port N --delay MS --jitter MS --loss
  PERCENT [--reorder]` is a UDP relay that gives each player's link a bad
  Internet connection on one machine: players join the relay's port instead
  of the host's. `netprobe portmap --discover-only` is read-only: it finds the
  gateway and the external address without creating a mapping.

## Game integration (`game::NetGame`)

`src/game/net/NetGame.h` wraps one `net::Session`, the host's `PortMapper`
and a `LanScanner` in the game's own terms (`game::RaceConfig`, car choice,
`Mat34` transforms). The app keeps it in `Context::netGame` while the
multiplayer menus are open and during multiplayer races; leaving the
sessions screen destroys it (which leaves the session and removes the port
mapping on a background thread).

### Settings on the wire

`toSessionSettings` / `fromSessionSettings` map `RaceConfig` onto
`net::SessionSettings`. Fields the protocol has no slot for travel in
`SessionSettings::extra`: `weather` (the game's enum, so fog survives),
`copDensity`, `difficulty`, `opponents`, `crMode` (Free-For-All, Cops vs.
Robbers, Robber Teams), `timeLimit`, `pointLimit`, `goldMass`. `raceId` is the
race index (0xFFFF for cruise and Cops & Robbers). In Cops vs. Robbers the
cars are fixed by team, as the original's help text says ("The Cop Team in
Mustang Cruisers ... the Robber Team in Mustang GTs"): `game::raceCar` gives
`vpcop` to team 0 and `vpmustang99` to team 1 (paint job 0), as MM2's lobby
sets the player's car to it and sends it to the session. Every machine
applies it to every player (`raceConfig()` for its own car, `playerCar()` and
`remoteCars()` for the others), so all of them draw the same cars and count
the same teams. Robber Teams lets everyone choose. Free-For-All has no team lamps: the lobby sets the team
from the car, 0 for a police car (flag 0x08) and 1 for any other
(`game::freeForAllTeam`, MM2's `mmMultiCR::InitMyPlayer`). The transmission
is each driver's own (`NetCar::automatic`, the garage's TRANSMISSION) and
never travels: MM2's session data has none, and `mmGame::Init` sets the car's
from the player's own state. `RaceConfig::netTraffic` travels as
`sharedTraffic`, its traffic density as `trafficDensity` and its cop density
in the extras (both held to 0-100 %); they matter only in a multiplayer
cruise. A new session starts a cruise with the option on and the
single-player cruise's densities (`frontend::applyNetTrafficDefaults`), the
other modes with neither.

### Menus

| Screen | Background | Notes |
|---|---|---|
| Sessions | `sess_bk` | providers: only TCP/IP is available (Internet/LAN over OpenMM2's UDP protocol); net name; HOST, JOIN; LAN session list (refreshed every second, also queries 127.0.0.1) in NetSelectMenu's box (289,243, 329 x 131); while nothing has been found, "Looking for games..." blinks dark grey / yellow every 0.75 s in the description box (40,396), where only the provider lamps show pictures. Entering it, a locked car becomes the VW New Beetle (`vpbug`) with paint job 0 and a locked paint job paint job 0 (`mmInterface::GetUnlockedCar` / `GetUnlockedColor`); the lobby's garage checks again when it closes |
| Host options | `host_dlg` | password (optional, 25 characters), max players 2-8 (a roller; MM2 offers 1-8), Cancel / DONE (`Dialog_Host`); the last accepted values come back the next time (`Dialog_Host::PreSetup`), Cancel drops what was typed |
| Enter an address | `tcp_dlg` | IP, `ip:port` or host name; blank = search the LAN again; Enter is DONE (`Dialog_TCPIP`); it starts with the driver's last address, which is saved with the driver when a race starts (`Dialog_TCPIP::SetIPAddress`, `mmInterface::BeDone`) |
| Password | `pass_dlg` | asked before joining a listed session that has one, and when a session joined by address wants one; a wrong password shows `badp_dlg` and asks again (`Dialog_Password`, `mmInterface::Update`) |
| Lobby | `lobbh_bk` / `lobbj_bk` | `NetArena`: host settings (mode, race, weather, time, laps or gold weight, limit), the race map and the city's name, players (`mmCompRoster` rows: the `ready` icon, which the host always shows, the team dot `blue_dot` / `red_dot` in team games, the name cut to six characters and "..." when wider than 0.09 of the screen, the car), YOU, team lamps for team games, chat line ("Type message here. Press ENTER to send."), the last three chat lines (" Name> text") of this visit, port forwarding status (host, where a race without a map would show it); host: EJECT PLAYER, HOST SETTINGS, SELECT VEHICLE in the row above GO DRIVE (which starts once everyone else is ready); joiner: SELECT VEHICLE, READY. A joiner's READY is cleared when the host changes the settings and when the joiner opens SELECT VEHICLE. BACK and Escape leave the session at once. When the race starts, the lobby's car and the session's event become the driver's last car and event, as for a single-player race (`MultiStartGame` calls `BeDone`). |
| Host settings | `host_bk` | game type lamps, race name + laps panels (`host_rnm`, `host_lap`) or the Cops & Robbers panel (`host_cr`: game types, limits, gold mass) or, in cruise, OpenMM2's SHARED TRAFFIC panel (on / off, traffic density and cop density sliders with the single-player cruise's defaults 0.5 and 1; drawn by OpenMM2 in the space cruise leaves empty), location, time of day, weather (Clear, Cloudy, Foggy, Raining: `RaceMenuBase::IncWeather` stops at Raining; a snowing session shows "Weather: Snowing" in the lobby), pedestrian density |
| Eject | `ejct_dlg` | pick a player to remove |

Positions of the provider/race/Cops & Robbers/team lamps, the HOST/JOIN and
bottom-right arrow buttons and the three host panels were found by matching
the sprites against the backgrounds (see `docs/frontend.md`); the lobby's
widgets and the network dialogs follow tune/widget.csv (menus 12, 14, 25,
36); the eject list and the players' rows are **inferred**. All races
are selectable in multiplayer (**inferred**: joiners' progress cannot gate the
host's choice).

When the host presses GO DRIVE, `NetGame::startRace()` orders the race
(`RaceLoad`, with the mode's countdown for the start: 2.5 s in the races).
`FrontendScreen` sees `takeRaceStart()` on every machine and switches to
`makeRaceScreen(ctx, netGame->raceConfig())` (with `config.multiplayer =
true`). Each machine reports the race loaded (`reportLoaded()`, from
`game::NetRaceStart`), the host sends the start once all have
(`raceStartKnown()`, `raceStartTime()`), and the race starts there (see
"Race start"). A `RaceResult` with `config.multiplayer` brings the player
back to the lobby instead of the results screen.

The host numbers the races (`NetGame::raceNumber()`, from 1, the same on
every machine); the race screen keeps the number of its race. A return to
the lobby ends the race it arrives in (`backToLobby(number)`), even when the
player is in the menus by then (the host's own return, which reaches the
menus as an event after the race screen has gone, or a joiner who quit the
race early). Game events still queued when a race is ordered are from an
earlier race and are dropped.

### Per-frame contract for RaceScreen

When `config.multiplayer && ctx.netGame`:

1. **Every frame, first:** `ctx.netGame->update()`. Without it the session
   times out.
2. **Start:** once the race has loaded, a `game::NetRaceStart` for the mode
   runs every frame (`update(dt, *ctx.netGame)`): it reports the race loaded
   (after 5 s in the races), gives the "Waiting for N players" count, the
   seconds left until the Go for the session's countdown
   (`Session::setNetStart`) and whether the car is held (until the Go; never
   in cruise), and in cruise the players who have just joined.
   `raceStarted()` becomes true at the start time. While the race loads the
   screen calls `update()` between its steps and leaves when
   `backToLobby(number)` or the session ends.
3. **Local car:** after stepping the simulation,
   `submitLocalState(car.modelMatrix(), linearVelocity, angularVelocity,
   controls, damage01, flags, stateAgeMs)` where `stateAgeMs` is how far the
   simulation is behind the frame (the fixed step's unstepped remainder),
   `controls` are the pedal/steering inputs
   and gear, and `flags` combine `net::kVehicleBrakeLights`, `kVehicleHeadlights`,
   `kVehicleHorn`, `kVehicleSiren`, `kVehicleWrecked`. Sending is rate limited
   inside (20 Hz).
4. **Remote cars:** once a frame, `ctx.netGame->remoteCars(simLagMs)` gives
   each other player's `transform` (car model matrix, a playout delay in the
   past, extrapolated up to 250 ms when packets are late), velocities,
   controls, damage and flags, sampled at the frame's session time less
   `simLagMs` (the remainder the frame's fixed steps will leave,
   `phys::World::remainderAfter`). Use that one sample for the cars' bodies,
   the rules and the HUD: the local car and every simulated object are where
   the frame's fixed steps leave them, and a car sampled at the frame's own
   time shook against them by up to a step's travel (29 cm rms at 35 m/s and
   144 fps; at 60 fps, a whole step on 4-10% of frames). The race draws its
   scene between the last two steps, one step (16.7 ms) behind the frame
   (docs/rendering.md, "Drawing between simulation steps"), so it draws the
   cars from a second sample, `remoteCars(16.7)`, and their simulated wheels
   and trailers blended like its own cars'. Draw cars with `hasState` (a
   player who has not reported the race loaded has none yet); spawn one
   `VehicleRenderer` per `car.car.vehicle` / `car.car.color`. For collisions,
   use them as kinematic bodies driven by the transform/velocity: their
   colliders take the body's velocity, so a car touching one meets it at
   their relative speed (the original simulated them as cars).
5. **Events:** report what the race rules decide: `sendCheckpoint(index,
   raceTimeMs)`, `sendLap(lap, lapTimeMs)`, `sendFinish(raceTimeMs, position)`,
   Cops & Robbers `sendGold(GoldPickedUp|GoldDropped|GoldDelivered, position,
   team)`, `sendCollision`, `sendDamage`. Read others' with `takeGameEvents()`
   (`event.as<net::FinishEvent>()` etc.) to rank players and show messages.
6. **Shared traffic** (cruise with `sharedTraffic`): the host runs
   `ai::World` with the other players (`World::setOtherPlayers`: the traffic
   populates the roads round them and avoids them; the police chase them as
   players), applies the clients' `TrafficHitEvent`s before the physics step
   and after it sends each client its cars (`sendAmbientState`, every 50 ms).
   A client runs no traffic or police: before its physics step it takes
   `takeAmbientStates()` and places the received cars, and its light sets
   follow the host's (`World::advanceLightsTo`).
7. **End:** the host calls `ctx.netGame->returnToLobby()` when the race is
   over (everyone finished, or the time/point limit); every machine sees
   `backToLobby(<its race number>)` and returns with `makeFrontendScreen(ctx, result)`.
   A player who quits early just returns to the frontend (it shows the lobby;
   the others keep racing). If `ctx.netGame->inSession()` turns false (host
   quit), return to the frontend; `takeNotice()` has the message.

### Port forwarding in the game

Hosting starts the `PortMapper` for the game port when `[Network] UPnP=true`
(the default) in `openmm2.ini`; the lobby shows its status line, e.g.
"Port 2300 forwarded via UPnP (external IP x.x.x.x)". The environment variable
`OPENMM2_NO_PORTMAP` turns it off regardless of the setting; automated runs use
it so that tests never open ports on the user's router. With port forwarding
off the lobby says which UDP port must be opened by hand.

### Automation

`OPENMM2_FRONTEND_SCRIPT` has multiplayer commands: `mp:host[:<password>]`,
`mp:join:<address>[|<password>]`,
`mp:chat:<text>`, `mp:ready`, `mp:start`, `mp:team:<0|1>`,
`mp:mode:<cruise|blitz|circuit|race|cr|crteams|crffa>`,
`mp:car:<vehicle>[:<paint>]` (what SELECT VEHICLE, a pick in the garage and
its PREV do), `mp:traffic:<on|off>[:<traffic density>:<cop density>]` (the
shared cruise traffic), and the pages `hostoptions`, `address`, `hostsettings`,
`eject`. `OPENMM2_DEBUG_NETTRAFFIC` logs the shared cars near each client on
the host (every message) and on the client (twice a second), with the hits
and knocks, for comparison; `OPENMM2_DEBUG_NET_SHOT_MS=<session ms>` ends a
network race at that session time, so each machine's `--screenshot` shows the
same moment, and `OPENMM2_DEBUG_NET_SHOT_MS=+<ms>` that long after the race
was ordered, also before it has started (each machine's race of the same
order). `OPENMM2_DEBUG_LOAD_DELAY_MS=<ms>` keeps a race's loading screen up
until that long after its loading began (the frames go on and the session is
serviced): a slow loader for trying the race start; the race logs each
machine's report, countdown and Go in session time ("race N: countdown from
session time ...", "race N: GO at session time ..."). The script survives a
race: after it the menus carry on with the commands after the one that
started it, and `wait:race` waits until a race has been driven. Example: one process hosts
(`profile:A;page:sessions;mp:host;wait:900`), another joins
(`profile:B;page:sessions;mp:join:127.0.0.1;wait:100;mp:ready`); a second
race after changing cars in the lobby:
`profile:A;mp:host;wait:1500;mp:start;wait:race;wait:300;mp:car:vpcop;wait:300;mp:start`
with `OPENMM2_POPUP_SCRIPT="wait:500;open:quit;wait:5;nav:accept"` (Quit to
Lobby) and `profile:B;mp:join:127.0.0.1;wait:100;mp:ready;wait:race;wait:100;mp:car:vpmustang99;wait:50;mp:ready`.

`test_game` (`tests/game/test_netgame.cpp`) runs a host and a client
`NetGame` in one process: settings round trip, join by address, LAN
discovery, password refusal, chat, car and ready changes, settings
propagation, the race start, vehicle state replication, game events, return
to lobby, host shutdown and eject. The race start is tested by
`tests/net/test_race_start.cpp` (every order of three reports, the shared
start, a player leaving, ejected or timed out while the others wait, the
load wait and a late report, a report for an earlier race and repeats), the
race messages in `test_hostile_input.cpp` and `test_fuzz.cpp`, and
`tests/game/test_netgame_start.cpp` (`NetRaceStart`'s 5 s, waiting count,
countdown, late machine, cruise and Cops and Robbers; a slow loader both
countdowns wait for and end together with; a loader that never finishes
joining later; a joiner leaving the race). Port forwarding is disabled in these tests.
The shared traffic is tested by `tests/net/test_ambient_state.cpp` (the
message, malformed input, delivery per client), `tests/game/test_traffic_sync.cpp`
(interest, budget, spawn, update, despawn, loss, reordering, recycled slots,
hostile messages), `tests/ai/test_shared_traffic.cpp` (several players'
roads, avoidance, the light steps) and `tests/phys/test_kinematic_motion.cpp`.

### Diagnosing replication

`OPENMM2_NET_TRACE=<file>` (off unless set) makes each session write, one line
each:

* `S wall id stamp arrival delay x y z vx vy vz`: every remote snapshot taken
  in (`wall` is the local monotonic clock, `stamp` and `arrival` session ms,
  `delay` the car's playout delay);
* `F wall frameTime stateAge x y z vx vy vz`: the local car once a frame, as
  submitted (`frameTime - stateAge` is the session time its state belongs
  to);
* `R wall id stale x y z vx vy vz sampleTime`: each remote car as sampled for
  the frame.

With one trace per machine, a remote car as drawn (`R`) can be compared with
where the other machine's car really was at `sampleTime` (its `F` lines).
Latency, jitter and loss can be added between machines on one computer with
`netprobe relay` in front of the host (tc/netem needs root).
