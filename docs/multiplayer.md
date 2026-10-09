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
| `net/PlayerCars.h` | The players' cars simulated by the host: `PlayerInputMsg`, `CarStatesMsg` |
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

In game the host is the authority for every player's car (protocol 5, see
"Players' cars"): each client sends its car's inputs, the host simulates
every player's car from them and sends each client, 20 times a second, the
last input it applied to that client's car with the car's state after it,
and every other player's car. A client runs its own car ahead on its inputs
(no latency) and corrects it from the host's states. (Before protocol 5
each machine simulated its own car and sent its state, which the host
passed on; `VehicleState` and `WorldState` remain in the protocol, unused by
the game.)
The other players' cars are drawn from an interpolation buffer a playout
delay in the past:
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
`Countdown`, `RaceLoaded`, `RaceStart`, the race in `Welcome`), version 4
the cars' damage, version 5 the host-simulated players' cars
(`PlayerInput`, `CarStates`, damage events about a player's car from the
host).

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
| VehicleState | C→H | own `VehicleSnapshot` (unused by the game since protocol 5) |
| WorldState | H→C | `(id, VehicleSnapshot)` pairs (unused by the game since protocol 5) |
| PlayerInput | C→H | the inputs of the samples the host has not acknowledged and the commands (resets) it has not (see "Players' cars") |
| CarStates | H→C | the last input applied to the client's car, its inputs in hand, the car's state, every other player's car |
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
  (burst 20), game events 30 (burst 60), and apart from those a car's damage
  events 15 (burst 30, see "Damage"). Chat and events beyond it are dropped;
  a `PlayerRequest` is always applied and its `PlayerUpdate` relayed once the
  budget allows, so the latest car, colour, team and ready state still
  arrives.
* **Floats.** Snapshot fields are quantized to fixed ranges and event floats
  must be finite (`ReadStream::f32`), so no NaN or infinity reaches physics or
  rendering.
* **The players' cars.** A `PlayerInput` holds 1-48 input frames numbered
  from 1 (a first number that would wrap is refused), switch and key bits
  masked, the gold's mass at most 2000 kg, and at most 4 commands whose
  positions lie within ±16384 m and rotations within ±64 rad. The host takes
  a joiner's inputs during a race only, within a budget of 90 messages a
  second (burst 180), queues at most 256, ignores inputs for samples already
  applied or more than 240 ahead, holds every car before the start whatever
  its input says, and lets a command move a car only inside the city's box
  (with 200 m to spare) and at most four times a second. A `CarStates`
  holds at most 16 cars; its own-car state must be a rotation matrix (entries
  within ±2), a position within ±16384 m and every other value finite and
  within ±10^7, or the whole message is refused.
* **Damage.** A `VehicleDamage` event has at most 16 patches and 8 impacts,
  record indices below 1024, 20 part bits, and every value quantized to its
  range (points ±8 m, normals ±1, speeds 0-128 m/s, strengths 0-10^6 on a log
  scale, sound ids 0-1000). A receiver takes a police car's damage from the
  host only, keeps at most 96 cars' records with at most 1024 patches each,
  lets at most 64 events wait for their time per car (older ones go into the
  record at once) and waits at most 2 s for an entry's time.
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
up are free for the game: Cops and Robbers uses 0x8001 to 0x8003 (0x8003 the
host's word that a limit was reached, `mmMultiCR::SendLimitReached`: the
host alone checks the time and point limits, `mmMultiCR::UpdateLimit`), the shared
cruise traffic's hit report (`TrafficHitEvent`, client to host) 0x8010, a
car's damage (`VehicleDamageEvent`, see "Damage") 0x8020.

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

### Players' cars

**MM2** (`mmNetObject`, `mmGameMulti`) ran every network car on every
machine as a `vehCar` of the level, declared each frame as a type-3 mover
that collides with everything (`mmNetObject::Update`: DeclareMover(3,
0x1b)) and drivable once the race starts (`mmGameMulti::EnableRacers`).
Each machine sent its own car to every other player
(`mmGameMulti::SendPosition`: to each player in the first frame after 0.05 s
since the last, 0.1 s for a player whose session data carry a flag that
`mmNetObject::Init` is given (inferred: a slow link); a dial-up session one
broadcast at its own interval) in a 68-byte packet
(`mmNetObject::SetPositionData`, message 0x1f5): its time, the steering,
throttle and brake as bytes, the manual gear, the orientation as Euler
angles, the position as three floats, the acceleration (averaged over ten
packets by `mmAccelCompute`), velocity and spin as a magnitude and three
signed bytes of direction each, the damage, the score, the horn, siren,
handbrake, gearbox and drivable flags, a reset bit and a packet counter. The
receiver (`mmNetObject::PositionUpdate`) dropped a packet older than the
newest, gave its car the packet's velocity and momentum, pedals, damage,
horn, siren and gearbox, and predicted it forward by the average interval of the last ten packets
(position plus velocity and half the acceleration, orientation turned by
the spin); a car more than 10 m from the packet (or with the reset bit) was
put there at once, a nearer one was given the packet's orientation and a
Hermite path (`mmNetPath`) from where it was to the predicted place, which
`mmNetObject::Predict` followed every frame by the share of the average
packet interval since the packet when the car was further than its radius
from the packet; otherwise the car's place and orientation moved toward the
predicted ones by the frame's seconds as a fraction (`Matrix34::Interpolate`).
Between packets the car drove itself on the last pedals. Every machine
thus had its own version of every collision between players: each car
bounced off where the other was on that machine.

**OpenMM2** (the maintainer's decision: the host is the authority) runs
every player's car on the host:

* **Inputs.** Every player's car takes its input once a physics sample
  (`game::NetCarDriver`, the same code on every machine): the gearbox
  switch and keys (`mmGame::UpdateGameInput`), the gold's mass
  (`mmMultiCR::FondleCarMass`) and throttle cap, the finish brake
  (mmPlayer +0x2258), the grid hold (`vehCar::SetDrivable(0, 1)`), then
  `mmGame::UpdateSteeringBrakes` (MM2 applies these once a frame; a sample
  is a frame at 60 fps). A client numbers its samples from 1 and sends,
  every frame that ran one, all the inputs the host has not acknowledged
  (the newest 48; a frame like the one before it costs one bit, so a second
  of driving costs a few bytes): a lost packet loses nothing.
* **Commands.** What the client's rules decide about its car travels with
  the inputs, numbered with the sample it applies at, until the host
  acknowledges it: the first sample's placement (where that machine
  started the car), mmPlayer::Reset (a fall, the debug respawn), the
  `HitWaterHandler`s' respawn at a checkpoint and `vehCar::ClearDamage`
  (Cops and Robbers' repairs). The client applies it at once and the host at
  the same sample, so a reset is no correction.
* **The host** (`game::HostInputQueue` per client) starts a client's car
  once three of its inputs are in hand, applies one per sample, repeats the
  last one (without its keys) for a quarter of a second when the next is
  missing and then lets the car coast, ignores inputs for samples it has
  passed, and reports in each state how many inputs it had in hand at the
  least. The cars are full cars of the level (polygonal bound, type-3
  movers, the player's input overrides, damage on with the race), so
  collisions between players, with the traffic and with the props happen
  there, once. Each car draws its wheels' bump numbers from its own random
  stream (`CarSim::ownRandom`) on every machine, so the host and the car's
  own machine simulate it alike whatever else each simulates (deviation:
  MM2 has one `rand()` for the game).
* **States.** 20 times a second the host sends each client its
  `CarStates`: the number of the last input applied to its car, the car's
  state after it at full precision (the body's matrix, momenta, velocities
  and last push; the wheels' spin, turn, springs and tyre deflections; the
  engine, gearbox, drivetrains, stuck watcher, damage and random stream; the
  pedal swap and the hold: about 270 bytes), and every other player's car
  as a `VehicleSnapshot`.
* **Prediction** (`game::CarPrediction`). A client runs its car on its
  inputs at once and keeps each sample's input and the car's whole state
  after it (`CarSim::saveState`, 3 s). When a host state for a sample
  differs from the prediction for it (by 3 mm, 3 cm/s, 0.0015 in the
  matrix, or in the damage, gear or hold), the client puts the car back to
  its saved state for that sample with the host's on top and runs the later
  samples again on their inputs (`phys::World::replaySample`: the car alone,
  everything it touches held still and moving at its own velocity, the
  other players' cars where each sample first met them; no sound or effect),
  at most 120 samples. The drawing keeps the car where it was and eases it
  onto the corrected place with a 60 ms half-life (`game::CorrectionBlend`;
  more than 4 m is a jump, drawn at once). The client's simulation runs up
  to 3% faster when the host had fewer than one of its inputs in hand over
  the last second and 2% slower above three, which keeps its inputs a
  sample or three ahead of the host's need.
* **The other cars** on a client are drawn interpolated from the host's
  states a playout delay in the past, as before, and its own car collides
  with them there as kinematic bodies moving at their velocity; the host's
  collision is the one that counts. (`OPENMM2_NET_OTHERS`, a development
  aid, places them ahead instead; see
  `docs/review/multiplayer-desync-cars.md`.)
* **The host's own car** has no latency and no prediction.

Measured through `netprobe relay` (60 ± 20 ms each way, 2% loss,
reordering), a host and a client ramming each other: the client's car was
corrected about 4 times a second, by 4 cm at the median, 1 m or more about
4 times a minute (at collisions); the host never ran short of its inputs.
The inputs cost a client about 0.9 KB/s of payload (3.3 KB/s on the wire),
the states about 0.3 KB/s per client plus 35 bytes per other car per
message (6 KB/s on the wire for two players; about 9 KB/s per client and
60 KB/s for the host with eight). See
`docs/review/multiplayer-desync-cars.md` for the measurements against 0.3.1.

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
| wheels | a car off its rail: 1 bit, whether it has a physics body; then its four wheels' drawing offsets from their pivots (80 bits, see "Damage") |
| police | target player (5 bits, 16 = none), damage (10 bits), rpm, throttle, gear (31 bits) |

A car on its rail takes 119 bits, a police car 208, a knocked car 178 (258
with a body). Each client gets the
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

### Damage

**MM2.** `mmNetObject::SetPositionData` puts the car's
`vehCarDamage::CurrentDamage` in every position packet, and
`mmNetObject::PositionUpdate` sets the network car's to it, calling
`vehCar::ClearDamage` when it drops from above 1 to below 0.0001 (the owner's
reset). Nothing else about damage travels: every machine simulates the other
players' cars as `vehCar`s with their damage on (`mmGameMulti::EnableRacers`),
so that machine's own collisions of the car paint its dents
(`vehCarDamage::ApplyImpact`, `fxTexelDamage::ApplyDamage`), break its parts
(`vehBreakableMgr::Impact`) and make its sparks and shards, and the level
drives the rest: `vehCarDamage::Update`'s smoke above MedDamage in four
levels, each with its own frame of `fxpt8` (the last two the black smoke of a
car about to die; MM2 draws no flames), the tyre wobble
(`vehSurfaceAudio::UpdateTireWobble`), and at MaxDamage the wheels, hubs and
fenders thrown off by speed (`vehCarModel::EjectOneshot`). The dents are
texel damage only: `mmDamage::Apply` is empty (no vertex damage), and no
light ever breaks (`vehCarModel::BreakElectrics` has no caller).

**OpenMM2** (`game/net/DamageSync`, `net/VehicleDamage.h`) decides every
player's car's damage on the host (protocol 5), which simulates them all:
its own collisions of each car paint, break and dent it, and the host sends
what they did about each car (subject 513 + the player's id; its own car
512) to everyone, the car's own player included, whose machine shows its
own car's dents and lost parts from them and keeps only the sparks and
sounds it predicted itself. A player's word on its own car's damage is
refused. (Protocol 4 sent each car's damage from its owner; the text below
keeps that layout, with the host as every car's owner.) The clients draw the
other players' cars kinematically and replay the host's record of them:

* **The level** is the snapshot's 10-bit fraction between MedDamage and
  MaxDamage (as before; the police's in `AmbientState`, now also 10 bits).
  The receiving machine sets the car's CurrentDamage to MedDamage + fraction
  × (MaxDamage − MedDamage) and runs `vehCarDamage::Update` on it: the same
  smoke level, frame and exhaust smoke, the same tyre wobble. Nothing shows
  below MedDamage, which is why the fraction is enough. `kVehicleWrecked` is
  the wreck (engine and police explosion sounds as before).
* **What the owner's `vehCarDamage::ApplyImpact` painted and broke** travels
  as a reliable game event, `VehicleDamage` (0x8020):

  | Field | Encoding |
  | --- | --- |
  | subject | 0-528: 513 + a player's id that player's car, 512 the host's own car, below it a shared police car's id (all from the host only) |
  | epoch | u8: the car's damage resets so far |
  | time | u32: session ms the entries' delays count from |
  | first, patches | the record index of the first patch (0-1023), then up to 16: delay (u8 ms), the impact point in model space (3 × 16 bits over ±8 m, 0.24 mm), the texel damage's random state (u32) |
  | parts | 20 bits: every part broken off since the reset (BREAK0-3, BREAK01/12/23/03, the paint job's VARIANT, WHL0-3, HUB0-3, FNDR0-1, ENGINE), and the delay of the newest |
  | impacts | up to 8 damaging impacts: delay, point (3 × 12 bits), normal (3 × 8 bits), the impact's running total and AudImpact's strength (10 bits each, log scale), the car's speed (9 bits, 0-128 m/s), the sound's id (0-1000) |

* **The same dents.** The owner paints each patch at the point as it
  travels (`net::quantizeDamagePoint`) and sends the random state its
  `fxTexelDamage` starts from; the receiver's paints the same patches from
  the same numbers (`VehicleRenderer::applyDamage(point, radius, seed)`),
  texel for texel (test `DamageSync.ReplayedDentsMatchTheOwnersTexelForTexel`).
  A patch copies the damaged texture's texels, so patches can land in any
  order. The parts are the owner's: the ones it broke off
  (`vehBreakableMgr::Impact`'s nearest, `EjectOneshot`'s by speed and its own
  random numbers) come off on every machine, thrown as bangers when the event
  is fresh (4 m/s, wheels at 1.3 times the car's speed), just taken off
  otherwise. The sparks, shards and impact sound of each damaging impact are
  replayed at the car as drawn.
* **Timing.** Every entry happens when the receiving machine draws the car at
  the session time it happened on its owner's machine (the police: at the
  shared traffic's drawing time), so a dent appears when the car hits the
  wall, not a playout delay before; an entry waits at most 2 s, and its
  sparks, shards, sound and flying parts show only within 0.5 s of its time.
* **Convergence.** Each car has one record since its last reset: its patches
  in order and the parts it lost. Events are reliable and ordered; each
  carries every part broken since the reset, so the parts converge with any
  later event; an event's patches fill the record from `first` (a gap, an
  event the host's budget dropped, is counted and the rest kept). A car shown
  afresh (created when its player's first snapshot arrives, a police car that
  comes into view or into a reused slot) replays its whole record, so damage
  that came before its first snapshot or while it was out of view shows. A
  player cannot join a race that has started (`allowJoinInProgress` is never
  set), so every machine in a race has had every event of it.
* **Resets.** Every `vehCar::ClearDamage` of the owner's car (a respawn,
  `mmPlayer::Reset` at the water, the damage-out penalty's reset, Cops and
  Robbers' repair at the bank or hideout and the end of a regeneration,
  `DamageReset`) starts a new epoch: the others clear the car (dents and
  parts) when they draw it at the reset's time. A police car's reset (the
  race's restart) does the same.
* **Rate.** The owner sends what happened every 100 ms at most, keeping to
  its own budget of 10 events a second (burst 20; what does not fit waits).
  The host relays a joiner's damage events with a budget of their own, 15 a
  second (burst 30), apart from the race events' 30.

**Knocked traffic cars' wheels.** While a traffic car has a physics body on
the host (aiVehicleActive), `aiVehicleInstance::Draw` draws its wheels where
its `vehWheelCheap`s put them: each wheel's pivot moved up by the spring's
travel and back by 0.2 and 0.3 of the tyre's sideways and forward
deflection. `AmbientState` now carries, for every car off its rail, one bit
for whether it has a body and then the four wheels' offsets from their
pivots (x and z 6 bits over ±0.25 m, y 8 bits over ±1 m: 80 bits); WHL4 and
WHL5 follow WHL2 and WHL3 as on the host. A client draws such a car on them
(and lays its shadow as the host's physical cars', `trafficShadowMatrix`),
blending them between messages.

**Deviations.** A receiving machine does not paint its own collisions of a
network car, as MM2's did: MM2's machines each showed the dents of their own
collisions with their own random numbers, OpenMM2 shows the owner's
everywhere. A network car's wheels are not simulated, so it lays no tyre
tracks and throws no wheel particles (MM2's simulated network car did; open).

**Bandwidth**, measured with `OPENMM2_DEBUG_NETDAMAGE`: a heavily crashing
car (a cab ramming walls every 4 s for 5 minutes, two wrecks) sent 53 events,
2-4 every 10 s, 4-15 bytes a second of payload (about 90 bytes on the wire
each with the event header, ENet and UDP, relayed by the host to each other
player). The sender's budget bounds a car at 10 events a second (16 patches
and 8 impacts each: about 3 KB/s at worst). The shared traffic grows by 4
bits a police car and 1 bit a knocked car per message, and 80 more bits for
each knocked car with a body. Measured (the host's `nettraffic` log, a bus
ramming the downtown San Francisco traffic with the client 40 m behind, 75
cars knocked in 2 minutes): 10 bytes a second without a car on its body's
wheels, 80-770 bytes a second while one to four were (about 1 to 4 cars a
message), of 11-19 KB/s; the 1100-byte message budget still bounds it.

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
3. **The players' cars** (see "Players' cars"): the frame builds this
   machine's car's input (`net::CarInputFrame`), and the physics' sample
   hooks (`phys::World::setSampleHooks`) apply each player's input once a
   sample. A client reconciles with `takeOwnCarStates()` before the frame's
   samples (`game::CarPrediction::acknowledge`) and sends
   `sendPlayerInput()` after them; the host takes `takePlayerInputs()`
   before its samples and sends each client `sendCarStates()` every 50 ms
   after them.
4. **Remote cars:** once a frame, `ctx.netGame->remoteCars(simLagMs)` gives
   each other player's `transform` (car model matrix, a playout delay in the
   past, extrapolated up to 250 ms when packets are late), velocities,
   controls, damage and flags, sampled at the frame's session time less
   `simLagMs` (the remainder the frame's fixed steps will leave,
   `phys::World::remainderAfter`; on the host, the cars it simulates). Use
   that one sample for the cars' bodies,
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
7. **Damage** (`game::NetDamage`, see "Damage"): the frame's game events go
   to `NetDamage::receive`; each network car (and a client's police car,
   and a client's own car) is brought up to date with `NetDamage::update`
   after it is placed (`fresh` when shown afresh), the others with
   `settle`; the host records every car's patches, parts, impacts and
   resets (`DamageRecorder`: `own()`, `player(id)`, `police(id)`), sent with
   `NetDamage::send` after the frame's effects.
8. **End:** the host calls `ctx.netGame->returnToLobby()` when the race is
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
order); a comma-separated list of times saves a numbered picture at each but
the last (`<screenshot>-1.png`, ...) and ends at the last. For the damage:
`OPENMM2_DEBUG_NETDAMAGE` logs every damage event sent and received, and every
2 s the network cars' and this machine's damage levels;
`OPENMM2_DEBUG_NETCARS` logs every correction of a client's car (how far the
prediction was off, how many samples ran again) and every 10 s what the
players' cars cost and how often the host ran short of a client's inputs;
`OPENMM2_NET_OTHERS=ahead` (an experiment) places and draws the other
players' cars where the host will have them when it runs this machine's
sample, `=ghost` keeps this machine's car from touching them;
`OPENMM2_DEBUG_NETCARS_NOISE=<fraction>` nudges a client's car's momentum by
about that fraction each sample, as a machine whose compiler rounds
differently might (the prediction's tolerance);
`OPENMM2_DEBUG_FOCUS=net:<player id>` frames a player's car (this machine's
own, or another's as drawn), `police:<400 + post>` a shared police car,
`knocked:<player id>` the knocked traffic car with a body within 60 m of a
player's car with the lowest id, `traffic:<id>` a shared traffic car, so both
machines take the same view; `OPENMM2_DEBUG_INPUT` takes several inputs
separated by `/` in turn, each for `OPENMM2_DEBUG_INPUT_MS` (2000) of race
time (`1,0,0.2,0/0,1,-0.2,0` rams a wall again and again);
`OPENMM2_DEBUG_RESPAWN_MS=<ms>[,...]` (or `+<ms>`) puts the car back at its
reset position as the water does; `OPENMM2_DEBUG_START_NEAR_POLICE=<post>:<m>`
starts a cruise's car that far in front of a police post's car, facing it,
and `OPENMM2_DEBUG_START=<x>,<y>,<z>,<angle>` at that place (the race logs
its start and angle); `OPENMM2_DEBUG_POLICE_TOUGHNESS=<factor>` scales the
police cars' MedDamage and MaxDamage on the host. The host's `nettraffic`
statistics give the share of the police damage bits and the knocked cars'
wheels, the client's how many cars it draws on their bodies' wheels, and the
per-car lines say which cars carry wheels. `OPENMM2_DEBUG_LOAD_DELAY_MS=<ms>` keeps a race's loading screen up
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
The players' cars by `tests/net/test_player_cars.cpp` (the messages, their
limits, mutated messages, delivery and the input budget through real
sessions), `tests/game/test_player_cars.cpp` (the pedals' bytes, the host's
queue, a client predicting a host's car through a simulated network with
and without losses) and `tests/phys/test_net_prediction.cpp` (a car's
state saved and run again, a car's sample alone, the sample hooks, a car's
own random stream). The damage by `tests/net/test_vehicle_damage.cpp` (the event's encoding, the
point arriving bit for bit, non-finite values, malformed events, the police
damage and wheels in `AmbientState`), `tests/game/test_damage_sync.cpp` (the
recorder's batches, splits, resets and budget; each entry at its time, the
hold, a reset clearing the car, convergence after a lost event, cars not
drawn, hostile events; the same texels on two renderers from retail data;
two `NetGame`s; the knocked cars' wheels through a message and the client's
interpolation), the damage relay budget in `test_hostile_input.cpp` and the
event in `test_fuzz.cpp`.

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

* `D clock frameTime id own x y z vx vy vz`: every player's car as the frame
  draws it (`own` 1 for this machine's);
* `K clock sessionTime a b x y z strength`: a collision between players `a`
  and `b`'s cars in this machine's simulation (the host's are the ones that
  count; a client's are its prediction);
* `C clock frameTime sample replayed dx dy dz dv snapped`: a correction of
  this machine's car by the host's state.

`D`, `K` and `C` use the machine's monotonic clock (shared by every process
on it), so the traces of instances on one computer compare the same
moments: `netprobe syncreport <host trace> <client trace>...` prints how far
apart each machine draws each player's car from its own player's screen,
the frames in which a car moves further than its velocity explains, the
collisions between players each machine had (and how close every machine
drew the two cars around them) and the corrections.

With one trace per machine, a remote car as drawn (`R`) can be compared with
where the other machine's car really was at `sampleTime` (its `F` lines).
Latency, jitter and loss can be added between machines on one computer with
`netprobe relay` in front of the host (tc/netem needs root).
