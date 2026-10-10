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
| `net/AmbientState.h` | The shared cruise traffic: `AmbientStateMsg` |
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
the game.) The host also decides the rules (protocol 10, see "Rules"): every
car's checkpoints, laps, finish and time, the standings, the finish timeout
and the end of a race, and Cops and Robbers' gold, scores and places; a
client predicts its own checkpoints and gold pickup for its HUD, and the
host's word confirms or corrects them.
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
| 1 State | unreliable, unsequenced (late snapshots still fill the buffer; ENet's throttle never drops them) | `VehicleState` (client→host), `WorldState` (host→clients), `PropState` (host→clients), `TrafficFull` (host→clients) |
| 2 Events | reliable, ordered | `GameEvent` |
| 3 Ambient | unreliable, sequenced; a message beyond ENet's MTU in unreliable fragments | `AmbientState` (host→clients): the shared cruise traffic |

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
the cars' damage, version 5 the players' cars simulated by the host
(`PlayerInput`, `CarStates`, damage events about a player's car from the
host), version 6 what the clients predict the shared traffic with (a rail
car's acceleration, curvature and speed over the ground), up to 160 shared
cars a message and no traffic hit reports, version 7 the host's props
(`PropState`, the `PropKnocks` event), version 9 the players' cars near a
client's in full in `CarStates` and, in every car's full state, what a
sample hands the next, version 10 the host's rules (the rules event; a
player's own checkpoint, lap, finish and gold events refused; 8 on its own
branch), version 12 the shared traffic's police and knocked cars near a
client in full (`TrafficFull`) and an `AmbientState` of up to 2600 bytes and
320 cars, version 14 the water and the fall decided by the host (every car's
full state carries its `vehSplash` and the water handler's time; a client's
reset commands only for debugging) and a race's rules state also unreliable
(`RulesState`) between the reliable messages, version 16 each client its own
`PropState` (the states near its car first), a ring of up to 256 slots and a
slot's number after the one before it in a bit (13 on its own branch).

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
| TrafficFull | H→C | the police and knocked cars near the client in full, at the physics sample of its `CarStates` (see "Shared traffic") |
| PropState | H→C | the host's ring of knocked-over props and thrown car parts, the states near the client's car first (see "Props") |

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
  its input says, and carries a command out only where the game's rules
  would (`game::ResetRules`, see "Players' cars"). A `CarStates`
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
* **Props.** A `PropState` holds at most 256 slots, ascending (ranged: slot 0-255,
  generation 0-15, placed prop 0-32767, piece 0-15, car part 0-19, owner
  0-63, paint job 0-15), quantized positions (±8 km), velocities (±128 m/s)
  and spin (±64 rad/s); a client keeps at most 16 unread, takes them only
  during a race, refuses a repeated slot and a slot whose generation changes
  what it holds, ignores a prop it has not placed and, when the host's
  placement checksum differs from its own, every placed prop (it then
  simulates its props itself). A `PropKnocks` event holds at most 512 knocks
  and counts only from the host.
* **Rules.** The host refuses a player's own word on a rule (its event
  filter: `CheckpointReached`, `LapCompleted`, `RaceFinished`, the gold
  events, Cops and Robbers' 0x8001-0x8003 and the rules event) and relays
  none of it. A rules message holds at most 8 decisions, 256 waypoints, 16
  icons, results and scores, player ids other than 255, times up to 24 hours
  (the did-not-finish), places and racers 1-16, finite positions within
  ±16384 m and scores within 0-2^20; a client takes it from the host only,
  for its own race and newer than the last, shows a decision once, and
  rebuilds its waypoints from the list with its own rules (a waypoint the
  rules refuse is dropped).
* **Shared traffic.** An `AmbientState` holds at most 320 cars, ids 0-511,
  generations 0-7, catalog indices 0-63 and paint jobs 0-15 (ranged fields:
  nothing else can be read), quantized positions, velocities and spin, and a
  police target that is a player id or none. A client takes it only during a
  race, keeps at most 16 unread, and the race refuses a car whose model is not
  in its own catalog, a repeated id, or a non-finite value, holds a paint job
  to the jobs the model can show, shows at most 32 police cars and loads each
  model once per car shown (a model that fails is not tried again). A
  `TrafficFull` holds at most 8 cars (ids 0-511, generations 0-7): a police
  car's state is checked as a player's in `CarStates` and its controls lie
  within ±1 (the throttle's cap ±10); a body's matrix is a rotation (entries
  within ±2) at a position within ±16384 m, its other values are finite and
  within ±10^7, its wheels' within ±100, its sleep state 0-2 and its counters
  0-1023, or the whole message is refused. A client takes it from the host
  only, during a race, keeps at most 32 unread and 24 samples' worth, the
  first state of a car at a sample, and simulates only a police car it shows
  (of that generation) and a traffic car it lists.

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
drop them; the game's (`game::hostAcceptsGameEvent`) refuses every player's
own word on a rule (see "Rules"). Well-known types, with payload structs in
`Protocol.h` (the checkpoint, lap, finish and gold ones are no longer sent
since protocol 10: the host decides them):

`CheckpointReached{index, raceTime}`, `LapCompleted{lap, lapTime}`,
`RaceFinished{raceTime, position}`, `GoldPickedUp/GoldDropped/GoldDelivered
{position, team}`, `Collision{other, position, impulse}`,
`Damage{damage, source}`, `Wrecked`, `LeftRace` (a joiner quit the race it
was driving and stays in the session: the others take its car out and stop
waiting for its finish). Ids from `GameEventType::Custom` (0x8000)
up are free for the game: Cops and Robbers used 0x8001 to 0x8003 until
protocol 10 (the pickup request, the places and the host's limit; refused
now), the host's rules message (`net::kRulesEvent`, see "Rules") is 0x8040, a
car's damage (`VehicleDamageEvent`, see "Damage") 0x8020, the host's knocked
props (`PropKnocksEvent`, see "Props") 0x8030 (0x8010, the shared traffic's
hit report until protocol 6, is unused).

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
  started the car), `vehCar::ClearDamage` (the wreck penalty's repair) and
  the debug respawn. The client applies it at once and the host at the same
  sample, so a command is no correction. The host carries one out only
  where the game's rules would (`game::ResetRules`, on its own simulation
  of the car): the placement only first and in the city, a repair only for
  a car past its maximum damage. Since protocol 14 a client's reset or
  respawn is refused: **the water and the fall are the host's own** (see
  "The water and the fall"), and Cops and Robbers' repair at a delivery is
  the host's since protocol 10. A refused command leaves the car where the
  host has it, and the client's is corrected back
  (`OPENMM2_DEBUG_RESPAWN_MS` on the host lets any reset in the city pass,
  not more than four a second, for development).
* **The water and the fall** (protocol 14). mmGame::Update checks the
  player's car after each frame: below -50 m it calls the mode's
  `DropThruCityHandler`, which a network game turns into the water's;
  otherwise, the car's `vehSplash` latched (its model origin went under a
  water room's level), it counts the seconds and past five calls
  `HitWaterHandler`: `mmGameMulti::HitWaterHandler` puts the car back at
  the last checkpoint it cleared, facing its heading (the reset position
  kept), in a race; `mmGame`'s (the cruise) and `mmMultiCR`'s (with the
  gold dropped back at its place) reset it to its reset position. In
  OpenMM2 the checks run on the car's samples (`game::NetCarDriver::
  setWaterHandler`, before each sample on the car as the last left it; the
  time counts samples), on the host for every car it simulates, with the
  respawn point from its referee (`NetRules::respawnIndex`); a client runs
  them on its own car with its session's last checkpoint as a prediction,
  which the host's states confirm (they carry the splash, its buoyancy and
  level, and the handler's time). A client that disagrees (its car went in
  later, or never) is corrected to the host's car; a client cannot put its
  own car back. The session shows the water's message as before and the
  race screen the reset (`presentOwnWaterReset`: the car's effects, its
  dents and the camera).
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
  pedal swap and the hold; and what a sample hands the next: the force and
  torque the wheels and engine set for it, each tyre's rolling resistance,
  and in a contact the impulses and pushes: about 310 bytes, 370 in a
  contact), every other player's car as a `VehicleSnapshot`, and the two
  nearest other players' cars within 40 m of the client's (kept to 50 m;
  not one towing a trailer) in full with the input the host last applied to
  them (`net::NearCarState`).
* **Prediction** (`game::CarPrediction`). A client runs its car on its
  inputs at once and keeps each sample's input and the car's whole state
  after it (`CarSim::saveState`, 3 s). When a host state for a sample
  differs from the prediction for it (by 3 mm, 3 cm/s, 0.0015 in the
  matrix, or in the damage, gear or hold), the client puts the car back to
  its saved state for that sample with the host's on top and runs the later
  samples again on their inputs (`phys::World::replaySample`: the car and
  the other players' cars it simulates, everything else held still and
  moving at its own velocity, every body within 40 m of it (the other
  players' cars placed at their states, the police, knocked traffic cars
  and props) where each sample first met it, and one that sample did not
  meet out of the way; the shared traffic's cars on their rails where each
  sample met them; no sound or effect), at most 120 samples. The drawing keeps the car where it was and eases it
  onto the corrected place with a 60 ms half-life (`game::CorrectionBlend`;
  more than 4 m is a jump, drawn at once). The client's simulation runs up
  to 3% faster when the host had fewer than one of its inputs in hand over
  the last second and 2% slower above three, which keeps its inputs a
  sample or three ahead of the host's need.
* **The other cars near** a client's car (the ones its `CarStates` carries
  in full) are simulated there along with its own: at every state the
  client puts them to the host's state at the acknowledged sample and runs
  them with its own car through the later samples (`CarPrediction`'s
  companions), on the input the host last applied to them, and on between
  states. Its car then meets them where the host's does and both give way
  by their masses, as on the host; only the other player's change of input
  since that state is unknown. Farther ones (by their distance and how fast
  they close in the samples run again) run again without the client's car,
  which stays where each sample had it. Every machine keeps the players'
  cars in the world's movers in player order (the host's first), and a
  replay runs them in that order: two cars collide in that order, and the
  order changes the outcome. They are drawn between their last two samples
  with what each state moves them by eased away (80 ms half-life), and the
  switch between simulating one and placing it at its states eased over
  150 ms.
* **The other cars farther away** on a client are drawn interpolated from
  the host's states a playout delay in the past, as before, and its own car
  collides with them there as kinematic bodies moving at their velocity; the
  host's collision is the one that counts. (`OPENMM2_NET_OTHERS`, a
  development aid, places every other car ahead, or keeps all of them at
  their states; see `docs/review/multiplayer-desync-cars.md`.)
* **The host's own car** has no latency and no prediction.

Measured through `netprobe relay` (60 ± 20 ms each way, 2% loss,
reordering), a host and a client ramming each other: the client's car was
corrected 3-7 times a second, by 1-3 cm at the median and at most 33 times
a run over 10 cm, never over 1 m; shunting from behind, at most 7 times
over 1 m a run, and never drawn jumping over 1 m (the first round, without
the near cars: up to 53); the host never ran short of its inputs. Every
machine draws another player's car about 150 ms from its player's screen
(the host behind it, a client behind a far car and ahead of a near one),
as 0.3.1 did; at that delay a few centimetres remain at the median. The
inputs cost a client about 0.9 KB/s of payload (3.3 KB/s on the wire), the
states about 0.4 KB a message per client, 0.7 KB with a near car (8 KB/s
of payload for two players apart, 14 KB/s near; with eight players at most
about 22 KB/s per client and 160 KB/s for the host, about 11 and 78 KB/s
apart). See `docs/review/multiplayer-desync-cars.md` for the measurements
against 0.3.1.

### Shared traffic

An OpenMM2 extra (MM2's network cruise has no traffic and no police,
`mmGameMulti::Init`; docs/parity/openmm2-only.md): in a multiplayer cruise
with the host's `sharedTraffic` on, the host runs the ambient traffic and the
police and sends each client, every 50 ms, an `AmbientState` with the cars near
it (`game::TrafficHost`, `src/game/net/TrafficSync.h`):

| Field | Encoding |
| --- | --- |
| header | u32 session time (of the AI step the cars are at), u32 the host's light-set steps (1/30 s) since its AI reset, u16 catalog checksum, 3 × i16 origin (whole metres at the client's car), count |
| id, generation | 9 + 3 bits: the traffic pool slot (0-299) or 400 + the police car's place; the generation changes when the slot is reused |
| has state | 1 bit; without it only the id and generation travel (13 bits) |
| kind, model, paint | 1 + 6 + 4 bits: traffic or police, the catalog index (the traffic's vehicle types in the AI map's order, then the police posts' cars: both machines build it from their data), the paint job |
| position | 15 + 14 + 15 bits: ±512 m across, ±256 m up from the origin (3 cm) |
| orientation | smallest-three quaternion, 32 bits |
| flags | brake, horn, indicators left / right (both: hazards), off its rail, wreck (police: out of action), siren, pursuit |
| motion | a car on its rail: its speed along its heading (11 bits, ±64 m/s; the AI's velocity is exactly that), then 1 bit, and when it is set its acceleration (7 bits, ±16 m/s²), its heading's turn per metre (9 bits, ±0.5 rad/m) and 1 bit, set when its speed over the ground differs from its speed, with that speed (11 bits); off its rail and police: velocity 3 × 12 bits (±96 m/s) and spin 3 × 11 bits (±32 rad/s) |
| wheels | a car off its rail: 1 bit, whether it has a physics body; then its four wheels' drawing offsets from their pivots (80 bits, see "Damage") |
| police | target player (5 bits, 16 = none), damage (10 bits), rpm, throttle, gear (31 bits) |

A car on its rail takes 120 bits (136 while it turns or changes speed, 148
when its speed over the ground differs too), a police car 208, a knocked
car 178 (258 with a body). Each client gets the
police chasing it, then the cars within 200 m of its car (kept until 230 m,
so a car on the edge does not come and go), nearest first, as many as fit in
2600 bytes (at most 320; protocol 12, before 1100 bytes and 160 cars, which
left the farthest 8 % out at traffic density 1); when they do not all fit,
the cars the client has count as 25 m nearer than they are, so the car at
the edge of what fits does not come and go either. A message beyond ENet's
MTU (1392 bytes) goes in two fragments, sent unreliably like the message
(`ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT`; without it ENet sends the fragments
of an unreliable packet reliably). Cars within 120 m, off their rails,
police and cars new to the client carry their state in every message; the
others in every other one, beyond 180 m every fourth (taking turns by id),
and say only "still there" in between.

Each message is complete for its client: a car the client knows that a newer
message leaves out has gone (out of range, or back in the pool) from that
message's time; an older message (reordered) adds its states to the cars
still known but never brings back or replaces one; a new generation is a new
car, shown where it is rather than blended from its old place, and so is a
car that moved 25 m (plus 30 m/s of velocity change) further than its
velocity explains; a car not heard of for 1.5 s is dropped.

**Shown at the present.** A message is stamped with the session time of the
AI step its rail cars are at (the police and the knocked cars, physics
bodies, are moved to it along their velocity from the physics step's time),
and carries for each rail car, measured from the AI's last two steps
(`game::RailMotionTracker`), its acceleration, its heading's turn per metre
and, where it differs from its speed, its speed over the ground
(`ai::Traffic` moves a car along its curves by their parameter: in a turn it
covers 20-30 % more or less ground than its speed, and a car held at the end
of its lane covers none). The client (`game::TrafficClient`) shows every car
at the session time its own car's state is at on the host's clock
(`RaceScreen::netTrafficCarTime`): the time its car reaches in the frame's
steps plus its lead over the host's simulation of it (its inputs' way to the
host and their wait in the host's queue, about half a round trip and a
sample or two), which it measures from the host's states of its car (the
host's session time after sample n against the time it ran sample n
itself, smoothed). Each car is predicted there from its newest state
(`game::TrafficPrediction`): a rail car along an arc at its speed over the
ground and acceleration, stopping at 0; a police car or a knocked car along
its velocity, turning with its yaw rate, at most 500 ms ahead (a rail car
1 s). Its collisions take the newest prediction at once; its drawing keeps
where it was and blends a newer message's correction away (100 ms time
constant; above 4 m or a radian it jumps). A message older than the time
shown is interpolated as before (Hermite with the remote players'
`SnapshotBuffer`). The light sets run to the host's steps at the time shown
(they are deterministic from a reset), and the cars are drawn a physics step
(16.7 ms) earlier than their collisions stand (`TrafficClient::transformAt`),
where the rest of the scene is drawn. Shown a trip and a playout delay in
the past, as before, a moving car was met 2.6 m from where the host had it
(docs/review/multiplayer-desync-traffic.md). MM2's network cruise has no
traffic; MM2 predicts its network cars forward from their last packet
(`mmNetObject::PositionUpdate`), which is what this does for the host's.

**In full.** With each client's `CarStates` (the same physics sample) the
host sends a `TrafficFull` (`net/TrafficFull.h`, protocol 12) with the
police cars within 60 m of its car and the knocked cars with a body within
50 m, nearest first (a police car chasing it counting half as far; farther
away a police car on its driver's last controls strays more than one
predicted along its velocity), as many as one 1200-byte datagram holds (at most
eight; a car sent the last time is kept 20 m further): a police car's whole
simulation state, as the players' cars near a client travel in `CarStates`
(see "Players' cars"), with the controls its driver set averaged over the
host's frames since the last state (throttle, brake, steering, handbrake,
the throttle's cap: the driver's throttle and brake go on and off from frame
to frame); a knocked car's rigid body, what a sample hands the next, its
four wheels and its sleep state. Only the cars the host's last physics step
ran go (MM2 neither moves nor collides a police car it does not declare,
nor a body beyond its 32 movers: they stand where they are, whatever their
velocity). The host's police draw their wheels' bump numbers from their own
streams, which travel with them. The client puts each state in at the
acknowledgement of that sample (`RaceScreen::predictNetTraffic`, a
`CarPrediction::Companion` without a player) and runs it again with its own
car to the present, a police car on those controls (which it keeps until
the next state), so its car meets them with their mass where the host's
does, in the order the host's world has them (the police before the
players' cars, the bodies after). A police car is then a simulated body,
drawn between its last two samples with the host's corrections blended away
(80 ms; a switch between that and the kinematic placement 150 ms), and
knocks the traffic cars it runs into loose as the host's does (they count
as the client's own knocks, for the host's messages to confirm); a knocked
car a body of the client's `TrafficBodies`, which `NetTrafficCars` lists as
simulated (a car the client knocked loose itself goes on as the host's). A
car whose full states stop for 250 ms is predicted and kinematic again, the
host's messages leading.

**Collisions.** The received cars on their rails are instances of the
client's level in `game::TrafficBodies`, as the host's rail cars are of its
own, with the received cars as their traffic (`game::NetTrafficCars`): when
the client's car hits one, it takes a body at once
(`aiVehicleInstance::AttachEntity`, `aiVehicleActive`) and the collision runs
the host's physics, the two cars pushing each other: the host simulates the
same hit from the same inputs at the same session time (see "Players'
cars"). Only the client's own car and the police cars it simulates knock a
car loose there (the other players' cars reach the client in the host's
messages), and not while it runs its samples again after a correction
(`World::replaySample` holds everything else still): there a rail car meets
the cars run again as the body it would take, of its mass and moving along
its rail (`TrafficBodies`' `heldInertia`; protocol 12, before as a car of
infinite mass). While the knocked car moves, the local body
leads, being the better guess (it runs the host's physics from the same
hit; the host's knocked car arrives a trip old and is predicted along its
velocity while it tumbles); once the host's messages show it off its rail
too and the local body has come to rest, or the host's car is 3 m from it,
or a host state stamped 150 ms after the hit still shows it on its rail (the
host did not knock it), the host's messages lead and the drawing blends
from where the local body left it. The cars off their rails on the host meet
the client's car as kinematic instances moving at their predicted velocities
(`game::TrafficProxies`), the police as kinematic bodies, unless they come in
full (above); a car standing still off its rail without a body on the host
(at rest after a knock) stands as a rail car at rest, which the client's car
knocks loose as the host's does (protocol 12). The hits happen on
the host: until protocol 6 a client reported its hits (`TrafficHitEvent`),
because the host's copy of its car lagged.

Bandwidth, two players in San Francisco at traffic density 0.5 (the
single-player cruise default) and cop density 1, measured by the host's
`nettraffic` log: 20 messages a second to the client, 60-70 cars a message,
12.5-15.3 KB/s; at density 1, 100-130 cars (every car within 200 m) and
22.5-27 KB/s (before protocol 12 the 1100-byte budget bound there at about
21 KB/s and left 8 % of them out). The full states add 0.4-5.4 KB/s driving
through traffic and 6.6-11.2 KB/s with a police car near; at most 76 KB/s
per client in all. The client's moving cars within 150 m were 3 cm (median)
from the host's at the same moment, 12-24 cm at the 90th percentile,
through a relay of 60 ± 20 ms each way with 2 % loss; 2.6 m and 3.7 m
before they were shown at the present. A police car within 10 m of the
client's car, simulated there, was 6-14 cm (median) from the host's, 0.38 m
when it was dead-reckoned, and a car the client's car knocked loose 0-4 cm
in the second after (docs/review/multiplayer-desync-traffic.md, which has
the other cases).

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
  random numbers) come off on every machine. The host throws them as bangers
  (4 m/s, wheels at 1.3 times the car's speed) and every machine shows the
  host's (see "Props"); a client takes them off without throwing. The sparks, shards and impact sound of each
  damaging impact are replayed at the car as drawn.
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

### Props

**MM2.** Nothing about props travels: no message of `mmGameMulti::GameMessageCB`
or of the modes' `GameMessage` names one, and `mmNetObject` carries the cars
only. Every machine simulates every player's car as a `vehCar`
(`mmNetObject::Update` declares it a type-3 mover), so each machine's own
collisions knock its props (`dgUnhitBangerInstance::Impact`), its parked cars,
its traffic lights and the parts thrown off its simulation of each car
(`vehBreakableMgr::Eject`), and each rests where that machine's simulation
leaves it. What a network game has (`mmGame::InitGizmos`, `mmGameMulti::Init`):
parked cars in the races only (none in a network cruise or Cops and Robbers),
no ferries, no cable cars; traffic lights in cruise and Cops and Robbers (the
races skip `aiMap::Init`). OpenMM2 0.3 differed in one more way: a network car
was a kinematic body, against which `dgImpact::CalcImpact` holds every banger,
so a prop another player knocked stood on every other screen, and the host's
shared traffic knocked props on the host only.

**OpenMM2** (the maintainer's decision: the host is the authority for
everything; `game/net/PropSync`, `game/net/NetProps`, `net/PropState.h`): the
host simulates the props for everyone, as a single-player race does, with
every car it simulates: its own, the other players' (simulated from their
inputs, protocol 5), the shared traffic and police; the props meet
them, and dent them, through single player's code.

* **Names.** Every machine places the same props in the same order (the street
  props restart the random generator per road, the instances and path sets
  come in file order, the parked cars draw from the stream the street props,
  the player's `vehCar::Init` and the sailboats leave, the traffic lights come
  from the AI map whatever the traffic density), so a placed prop's index
  names it everywhere (test `PropSyncRetail.EveryMachinePlacesTheSameProps`;
  7068 props in a San Francisco cruise, 6637 in its checkpoint races). A
  checksum of the placement (`game::propCatalog`) travels with every
  message. A knocked-over prop is a slot of the host's ring of 40
  (`dgBangerManager`): the whole prop, one of its BREAKnn pieces, or a part
  thrown off a car, named by the car (a player's id, or a shared car's catalog
  index), the part (`damagePartIndex`) and the paint job.
* **The ring grows in a pile-up.** When the host's ring is about to wrap onto
  a prop that still moves or was knocked less than 10 s ago (more knocks than
  MM2's 40 slots hold, which would make props knocked moments ago disappear
  in mid-flight), it grows by 40 slots instead, up to 40 a player (at least
  80, at most 256; `BangerSet::setRingGrowth`, inferred: a player's knocks
  then last about as long as in single player). Otherwise it wraps as MM2's
  does; a single-player ring never grows.
* **Knocks** (`PropKnocks`, reliable, to everyone): the placed props the host
  broke loose, each with its session time. A machine that reports the race
  loaded gets every knock so far at once, so props knocked earlier, anywhere,
  are down when it gets there.
* **The ring** (`PropState`, unreliable, each client its own): every
  occupied slot, complete and ascending (a slot left out is empty from that
  message's time), with the states that client needs most. Its area is
  within 150 m of its car (inferred: well past where a prop still shows as
  more than a few pixels): a message 20 times a second while a prop there
  flies or a slot there changed (or came into it), 5 while props there only
  creep (slower than 0.5 m/s: a meter sliding down a hill, which phSleep may
  never stop) or props elsewhere move, 2 otherwise. The states go in by need
  until the message would pass 1100 bytes (one unfragmented datagram):
  the moving slots of its area every time, then its changed ones, its
  creeping ones every fourth message, the changed ones elsewhere, the moving
  ones elsewhere every fourth message, its slots at rest every tenth, the
  others at rest every fortieth; within each, the nearest first. A state
  that does not fit waits for a later message (a changed slot stays changed
  until it has gone three times). The knocks go to everyone whatever the
  area, so no client misses one:

  | Field | Encoding |
  | --- | --- |
  | header | u32 session time, u32 placement checksum, count |
  | slot, generation | 1 bit (the slot after the one before) or 1 + 8 bits, then 4 bits; without a state only these travel (still there, at rest) |
  | what | 2 bits; a prop 15, a piece 15 + 4, a car part 1 + 6 + 5 + 4 (owner, part, paint) |
  | moving | 1 bit |
  | frame at the CG | 3 × 22 bits over ±8 km (4 mm), smallest-three quaternion (32 bits) |
  | motion (moving only) | velocity 3 × 13 bits (±128 m/s), spin 3 × 12 bits (±64 rad/s) |

  A flying piece takes about 204 bits, a slot at rest 108, a slot only still
  there 6 (a ring of 256 listed in 192 bytes).
* **A client** shows the host's props as the host had them a playout delay
  in the past (what the messages needed to arrive over the last 3 s plus a
  send interval, 50-500 ms, as the other players' cars): a knock when it shows
  that time, the ring in mirror slots interpolated on their velocities
  (`net::SnapshotBuffer`). Only its own car and the cars it simulates along
  with it (the other players' near ones the host sends in full,
  `NearCarState`; the shared police and knocked traffic cars it sends in
  full, `TrafficFull`, or its car knocked loose) may touch its props
  (`phys::Instance::acceptsContact`), and
  the pieces it simulates itself one another but no standing prop (the
  host's knocks bring those its pieces knock): the other cars pass through
  them, since the host decides what they do. Those cars hit a prop at once,
  as in single player (the impulse, the damage, the prop breaking loose): a
  prediction (before protocol 16 another player's car drove through a prop
  that fell 150-400 ms later). When the
  host's knock comes (from its copy of the car, a playout delay later) the
  piece simulated here stands in for the host's until both rest, then hands
  over to the host's, blended over 0.4 s. A knock the host has not made is
  undone (the prop stands again): one by its own car once the host's
  messages have passed its time by the host's usual lag behind its knocks
  (the most of the last 16) and 0.3 s, at least 0.6 s; one by another
  player's car (which it runs ahead on that player's last input, and which
  may reach a prop seconds before the real one) once the host's states had
  that car more than 6 m from the prop by then as well, and its own car is
  more than 15 m from it (about to knock it itself, which the host's knock
  would then confirm); any after 2 s (4 s for another player's car the
  host still had by the prop). A mirror its cars touch is simulated
  here from the host's motion (so a flying cone hits its car as on the host)
  and stays where it stopped until the host's has moved and rests again, or
  as long as the host's push would take to show here (the lag above and
  the playout delay, at most 2 s). A piece simulated here
  that the host's ring does not hold disappears 2 s after it was made once at
  rest. The parts the host's damage records take off a car (its own
  included) come off without a throw: the host's ring shows the parts it
  threw.
* **Replays.** When the host's state corrects its car, a client runs the
  car's later samples again (`World::replaySample`) with the world held
  still. A prop meets the replayed car with the body a real hit gives it
  (`Instance::heldInertia`: the active `dgBangerActive::Attach` would give
  it) and moves on through the replay as the hits push it (without gravity
  or the city: a replay is short); a placed prop the car broke loose in the
  last second stands again where it stood for the samples up to the one
  that broke it loose (its pieces left out), so the replay takes the knock
  the real samples took, and the later samples meet its pieces where they
  were (the client puts the bodies around its car back where they stood for
  each sample run again); a flying piece meets it with a copy of its body,
  moved on over the sample as a real sample moves it before the collisions.
  Nothing is knocked or moved in the world by a replay. A prop a client's
  car hits in its own samples is the same knock on the host, sample for
  sample: a client driving alone through props had no correction.
* **A different placement** (an altered city, a mismatched build): the
  client follows only the cars' parts and simulates its props itself, as
  0.3 did, and logs it.

Measured on this machine through `netprobe relay` (80 ±20 ms each way, 2 %
loss, reordering; and 150 ±20 ms, 5 % loss), a host and one or two clients
driving into props, traffic lights and parked cars (the record:
`docs/review/multiplayer-desync-props.md`): with 0.3.1's props no knock on
one machine was the same knock on another, 6-16 props differed between the
screens at the end of a minute, and props two cars hit separately rested
10.6 m apart (0.1-2.4 m once the host simulated the client's car, in two
separate simulations); after, every knock reached every machine (170-250 ms
apart, a playout delay), no prop differed at the end, and the knocked-over
props, parked cars and parts rested within 3 mm of the host's (the
position's quantization is 4 mm). A client alone among props had no
correction of its car, and a client's corrections over 30 cm just after a
prop hit (nothing else around) went from 31 in eight runs to 7. Since
protocol 16 a prop another player's car (run on the client) hits falls as
that car meets it, where it fell 140-400 ms after the car came closest
(179 of 198 such knocks confirmed by the host; the others stand again
within 0.7-2 s), and a missed prediction stands again in 0.7-1.8 s, not 2 s.
The host sent 0.1-6.4 KB/s to each client (10 s averages; a big crash in the
test, 27 props knocked by one car, 5 KB/s for a few seconds, at most 425
bytes a message); a message stays within 1100 bytes, the states nearest the
client first.

**Deviations.** MM2 lets each machine knock its own props; OpenMM2 shows the
host's everywhere, as it does the traffic. The ring's wrapping (the oldest
knocked-over prop disappears when the 41st is knocked) is the host's on every
machine, and in a pile-up the ring grows instead. A client's predicted knock
that the host does not make stands up again, and a client's pieces move to
the host's place when they rest.

### Rules

**MM2** decided a network race's rules on each player's own machine:
`mmWaypoints::Update` on its own car, its waypoint count in every position
packet (`mmGameMulti::SendPosition`, mmPlayer +0x2254) for the others'
standings (`mmGameMulti::UpdateScore`), and its finish time from its own
timer, sent to the host (`mmGameMulti::SendFinishReq`, 0x206). The host
acknowledged each finish to everyone (`SendFinishAck`, 0x1f7; a client's own
"finished in" line appears only then), kept the results (`SortResults`,
`UpdateResults`), armed the finish timeout at the first finish
(`mmMultiRace::SetTimeoutOn`: 60 s in a checkpoint race, 120 s in a
circuit), told everyone still racing when it ran out (0x1fe: "Race over" and
a did-not-finish) and took everyone to the results once every player was
counted (0x211) (`mmMultiRace` / `mmMultiCircuit` / `mmMultiBlitz::
UpdateGame`, `GameMessage`). In Cops and Robbers (`mmMultiCR`) each machine
ran `ImpactCallback`, the wreck and water handlers, `UpdateGold` and
`UpdateBank` / `UpdateHideout` for its own car and told the others (0x259 a
drop, 600 a delivery); a pickup asked the host (0x25e), which granted it
while nobody carried the gold (0x25a) and drew the next places after a
delivery (`GetNewSet`, 0x261); the host alone checked the limits
(`UpdateLimit`, `SendLimitReached`).

**OpenMM2** (protocol 10; the maintainer's decision: the host is the authority
for everything) decides them on the host from the cars it simulates (see
"Players' cars"). MM2's rules themselves are unchanged; only who decides is.

* **The referee** (`game::session::RaceReferee`) runs mmWaypoints' rules
  (`game::session::WaypointTracker`, the code a player's own session runs)
  on every player's car after every physics sample (MM2: once a frame on
  each machine; the same at 60 frames a second), and does the host's part of
  mmMultiRace / Circuit / Blitz: the results by time, the finish timeout from
  the first finish, 0x1fe to everyone still racing, the end once every
  player still in the race is counted (0x211), a Blitz car out of time on its
  own clock and the host's own Blitz time running out ending everyone's race
  (0x1fe), and `mmGameMulti::UpdateScore`'s place ("Place: n/N") and icon
  numbers as each player's machine would compute them.
* **Finish times** are the host's measure: the samples (1/60 s each) the host
  simulated the car from the first one its player's input released it (that
  player's Go at the shared start; a late loader's own Go) to the one it
  crossed the line in. Every player is timed alike, on the session's clock,
  and every machine shows the same time; MM2's machines each sent their own
  timer's.
* **Cops and Robbers**: the host runs mmMultiCR's per-car rules for every car
  (`CopsAndRobbers::updateHost`): a carrier's own damaging impact of 250 or
  more from another player's car knocks the gold loose where it is and locks
  the carrier out for 2 s; a wrecked car sits out 5 s and drops the gold; a
  water handler (the host's own, see "Players' cars", "The water and the
  fall") sends the gold back to its place; free gold within 5 m in the car's
  room goes to the first car
  in order, with another player in the game; a carrier within 12 m of its
  base delivers and the host draws the next places; then the limits. The
  gold's mass, throttle cap and regeneration on the cars the host simulates
  follow its decisions, whatever their inputs say, and it repairs a
  deliverer's car itself (600).
* **The message** (`net/RulesState.h`; game event 0x8040, reliable and
  ordered, from the host to one player) carries what the host decided since
  its last message to that player, each decision numbered (`Finished` with
  the time or a did-not-finish, `TimedOut`, `AllCounted`; `GoldTaken`,
  `GoldDropped`, `GoldDelivered`, `NewSet`, `Limit`), and the state as it
  stands: the last sample of that player's car the rules saw, then in a race
  that car's waypoints hit (the newest 64), its place and the racers, the
  other cars' icon numbers, the results so far, the timeout and the end; in
  Cops and Robbers the gold's carrier and place, the set, every score and
  the end. It goes at once when something happened for that player and
  otherwise four times a second in Cops and Robbers, once a second in a
  race, where the state alone (the newest 16 waypoints, the samples seen,
  the place and the icons; no decisions, no results) also goes unreliably
  (`RulesState` on the State channel, protocol 14) at once on a new hit and
  20 times a second, numbered with the messages: whichever arrives first
  stands, so a lost packet no longer holds a checkpoint's confirmation (or
  its taking back) until ENet sends it again. A client
  shows each decision once, in order (a reliable message older than the
  newest state still brings its decisions), and a missed one (a late loader
  whose queue of events overflowed) is made good by the state, silently.
* **A client predicts** only what the HUD must show at once, and the host's
  word confirms or corrects it:
  * a checkpoint its own car clears (`Session::applyNetProgress`): shown at
    once (the sound, the split time, the lap message), stamped with the
    car's sample. When the host's list has it, nothing more happens. When
    the host has run `Session::kNetHitMargin` (30) of the car's samples past
    it without counting it, it is taken back silently: the marker shows
    again and the readout drops, with no sound. One the host counted that the
    client did not see is shown when the word arrives. The client rebuilds
    its waypoints from the host's list and its hits still on their way, with
    the same rules, so nothing is shown twice.
  * a gold pickup (`CopsAndRobbers::updatePredicted`): "You have the Gold!"
    at once (MM2's client asked the host and waited for 0x25a), unless
    another car is at the gold too or about to be (within the gold's 5 m and
    1 m more, or on its way there in the next quarter of a second, as this
    machine has it where the host will run its sample: the cars it
    simulates with its own as they are, the others run on from their
    drawing), when it waits for the host's word as MM2's did. The host's
    `GoldTaken` for it confirms it; one for another car undoes it and
    shows that car's; the state undoes it once the host has seen the car
    30 samples past the pickup without granting it.
  * the water and the fall on its own car (see "Players' cars", "The water
    and the fall"): put back at once, the host's states confirming it.
  * nothing else: the finishes, the results, the standings, drops,
    deliveries, scores, the new places and the limits are the host's word
    only. A client's own "finished in" line (MM2's client line: 149, 106,
    95) shows the host's time when the host's decision arrives, as MM2's did
    on 0x1f7, and its race clock stops at that time. Its car keeps driving
    until then: braking at a predicted finish the host had not yet seen could
    stop the host's car short of the line.
* **A player's own word is refused**: the host's event filter
  (`game::hostAcceptsGameEvent`) drops `CheckpointReached`, `LapCompleted`,
  `RaceFinished`, `GoldPickedUp`, `GoldDropped`, `GoldDelivered`, Cops and
  Robbers' old messages 0x8001-0x8003 and the rules message from any player
  but itself, before relaying them, so nobody sees them; a client takes the
  rules message from the host only, for its own race, and newer than the
  last. A client's own reset of its car is refused (`game::ResetRules`): the
  water's handler is the host's, at the last checkpoint its referee counted
  the car clearing (`NetRules::respawnIndex`).
* **Players who join late or leave**: every player in the session when the
  race is ordered is waited for. A player still loading has no car (it counts
  in the standings only once it has a finish) and its time starts at its own
  Go. A player who leaves the session or quits the race is no longer waited
  for, and a finish it had stays in the results (the deviation under "Race
  start"). In Cops and Robbers the host drops a leaver's gold where it is
  (`SystemMessage` 0x2d).
* **Cruise** has no rules to decide but the water's: its wreck penalty
  follows the host's damage of the car, and the water's reset is the host's
  (see "Players' cars", "The water and the fall").

**Deviations** (docs/review/multiplayer-desync-rules.md): the host decides
for every machine; the rules run after every physics sample rather than
once a frame; the finish time is the host's measure in samples, not each
machine's own timer; in Cops and Robbers two cars reaching the gold in the
same frame are decided in car order (MM2: whichever request reached the host
first); a client shows its own pickup before the host's answer; the water's
and the fall's checks run on the host for every car, before each sample
rather than after each frame, the water's five seconds counted in samples;
the place
uses the host's view of each player's target (the Next / Prev. Checkpoint
keys stay on that player's machine) and positions.

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
5. **Rules** (`game::NetRules`, see "Rules"): every machine's session runs
   with `SessionOptions::netRules`. The host feeds every player's car to
   `NetRules::hostSample` after every physics sample (the sample hooks), its
   Cops and Robbers decisions to `hostDecided`, and calls `hostUpdate` after
   the frame's rules (its own player takes the referee's word there; each
   other player's message goes with `sendEvent(net::kRulesEvent, payload,
   player)`). A client passes the frame's events to `NetRules::receive` before
   its rules, stamps its predicted checkpoints with `Session::setNetSample`,
   and reports a predicted pickup with `predictedPickup`. Nobody sends
   `sendCheckpoint`, `sendLap`, `sendFinish` or `sendGold` any more (the host
   refuses them). Read other events with `takeGameEvents()`.
6. **Shared traffic** (cruise with `sharedTraffic`): the host runs
   `ai::World` with the other players (`World::setOtherPlayers`: the traffic
   populates the roads round them and avoids them; the police chase them as
   players; the clients' cars are its own simulated ones) and after the
   physics step sends each client the cars round its car
   (`sendAmbientState`, every 50 ms).
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
8. **Props** (`game::NetProps`, see "Props"): set up once the props are
   placed (`setup`, with this machine's car and the other players' cars it
   simulates as the only things that may touch a client's props, and on the
   host where each client's car is); a client's `beforeStep` before the physics step (the
   host's messages and knocks, the mirrors placed for the session time the
   simulation will reach), every machine's `afterStep` after the props'
   update (`BangerSet::update`: the host sends, a client takes its own
   knocks as predictions). A part thrown off a car carries its tag
   (`game::carPartTag`).
9. **End:** the host calls `ctx.netGame->returnToLobby()` when the race is
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
shared cruise traffic), `mp:race:<index>[:<laps>]` (the host's race, after
`mp:mode`), and the pages `hostoptions`, `address`, `hostsettings`,
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
`OPENMM2_NET_OTHERS=ahead` (an experiment) places and draws every other
player's car where the host will have them when it runs this machine's
sample, `=ghost` keeps this machine's car from touching them, `=past` (or
any value) keeps them all at their interpolated states instead of
simulating the near ones; `OPENMM2_NET_TRAFFIC_REPLAY=predicted|frame`
meets the shared traffic's cars on their rails, in a client's samples run
again, where the host had them at each sample's time or where the frame put
them, instead of where each sample met them (comparisons); with
`OPENMM2_DEBUG_NETCARS` the host also logs every reset of a client's car
it refuses;
`OPENMM2_DEBUG_NETCARS_NOISE=<fraction>` nudges a client's car's momentum by
about that fraction each sample, as a machine whose compiler rounds
differently might (the prediction's tolerance);
`OPENMM2_DEBUG_FOCUS=net:<player id>` frames a player's car (this machine's
own, or another's as drawn), `police:<400 + post>` a shared police car,
`knocked:<player id>` the knocked traffic car with a body within 60 m of a
player's car with the lowest id, `traffic:<id>` a shared traffic car,
`prop:<index>` a placed prop or, once knocked, its first piece, so both
machines take the same view. For the props: `OPENMM2_NET_TRACE` traces
each machine's props too (see "Diagnosing replication"),
`OPENMM2_NETPROPS=local` leaves every machine with its own props, as 0.3 did
(for comparison); `OPENMM2_DEBUG_INPUT` takes several inputs
separated by `/` in turn, each for `OPENMM2_DEBUG_INPUT_MS` (2000) of race
time (`1,0,0.2,0/0,1,-0.2,0` rams a wall again and again);
`OPENMM2_DEBUG_RESPAWN_MS=<ms>[,...]` (or `+<ms>`) puts the car back at its
reset position as the water does; `OPENMM2_DEBUG_START_NEAR_POLICE=<post>:<m>`
starts a cruise's car that far in front of a police post's car, facing it,
and `OPENMM2_DEBUG_START=<x>,<y>,<z>,<angle>` at that place (the race logs
its start and angle); `OPENMM2_DEBUG_POLICE_TOUGHNESS=<factor>` scales the
police cars' MedDamage and MaxDamage on the host;
`OPENMM2_DEBUG_TRAFFIC_LEAD_MS=<ms>` shows a client's shared traffic that
much later than its car's time (negative: earlier), for measuring the
prediction over another horizon. The host's `nettraffic`
statistics give the share of the police damage bits and the knocked cars'
wheels, the client's how many cars it draws on their bodies' wheels, and the
per-car lines say which cars carry wheels. `OPENMM2_DEBUG_AUTOPILOT=<line>[,<m/s>]`
drives this machine's car in a network race with MM2's racer AI
(`ai::Opponent`) along the race's opponent line `line` (from 0), at up to
that speed, through the car's inputs only (`game::NetAutopilot`: the AI's
frame runs on the car and the car is put back, keeping only the throttle,
brakes, wheel and a reverse through AUTO REVERSE), so automated runs drive
through the checkpoints; in Cops and Robbers it drives to the gold, then to
the car's base, over the roads and the last 80 m straight.
`OPENMM2_DEBUG_START_GOLD=<metres>` starts a Cops and Robbers car that far
from the first gold, facing it, the two first players on opposite sides,
and `OPENMM2_DEBUG_CR_SEED=<n>` (on every machine) draws the first places
from n rather than the race's order time, so runs can be compared.
`OPENMM2_DEBUG_LOAD_DELAY_MS=<ms>` keeps a race's loading screen up
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
roads, avoidance, the light steps), `tests/phys/test_kinematic_motion.cpp`,
`tests/game/test_traffic_prediction.cpp` (the rail motion the host measures,
the prediction along an arc, braking, the ground speed, a body's yaw, the
drawing's corrections, and a host and a client over a simulated 60 ± 20 ms
link with 2 % loss, which prints the same-moment error) and
`tests/game/test_net_traffic_cars.cpp` (a client's own knocks, confirmed,
withdrawn, recycled).
The rules by `tests/game/test_net_race_rules.cpp` (the tracker taking
recorded hits as its gate test does; the referee timing each car from its
own release, timing a race out after its first finish, counting a circuit's
laps in order, ending a Blitz on each car's clock and the host's, ranking
every car as its machine would; Cops and Robbers on the host and a client's
predicted pickup confirmed or undone; a client's predicted checkpoint shown
once, taken back without a sound, one only the host counted shown once; its
finish, standings, timeout and end the host's; the host's message through
loss, duplicates, forgeries, junk and another race; a cheating player's
rule events reaching nobody through live sessions) and
`tests/net/test_rules_state.cpp` (the message, its limits, fixed-seed random
and mutated payloads).
The players' cars by `tests/net/test_player_cars.cpp` (the messages, their
limits, mutated messages, delivery and the input budget through real
sessions, the near cars in full in one packet, a client refusing its own
car or a car given twice among them), `tests/game/test_player_cars.cpp`
(the pedals' bytes, the host's queue and its catch-up, a client predicting
a host's car through a simulated network with and without losses, a client
predicting a shunt with the other car simulated, held, and braking, the
host's reset rules), `tests/game/test_traffic_bodies.cpp` (the rail cars
put elsewhere for a replay and back), `tests/game/test_net_rules.cpp`
(Cops and Robbers' limits from the host) and
`tests/phys/test_net_prediction.cpp` (a car's state saved and run again, a
car's sample alone, the sample hooks, a car's own random stream).
The damage by `tests/net/test_vehicle_damage.cpp` (the event's encoding, the
point arriving bit for bit, non-finite values, malformed events, the police
damage and wheels in `AmbientState`), `tests/game/test_damage_sync.cpp` (the
recorder's batches, splits, resets and budget; each entry at its time, the
hold, a reset clearing the car, convergence after a lost event, cars not
drawn, hostile events; the same texels on two renderers from retail data;
two `NetGame`s; the knocked cars' wheels through a message and the client's
interpolation), the damage relay budget in `test_hostile_input.cpp` and the
event in `test_fuzz.cpp`. The props by `tests/net/test_prop_state.cpp` (the
messages, their sizes and limits), `test_fuzz.cpp` (both messages, decoded
and sent to a client) and `tests/game/test_prop_sync.cpp` (a host and a
client in one process through a lossy link: the host's knocks and pieces,
a predicted knock handed over and one undone, other cars passing through a
client's props, a replayed car meeting a prop as a real sample does
(through the knock, and from 1-8 samples after it), a replay moving no prop
for real, loss and reordering, a big crash, a pile-up growing the ring, two
clients far apart each getting the states of its own area first within a
small datagram, a near car's knock predicted and one undone, the catch-up,
thrown car parts, refused input, and on retail data the same placement on
every machine).

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
  this machine's car by the host's state;
* the rules' lines (`game/net/RulesTrace.h`): `RS` a checkpoint this
  machine's HUD showed for its own car (as predicted, or on the host's
  word), `RX` one the host's word took back, `RH` the host's referee
  counting a car's checkpoint (with the car's sample), `RF` a finish in this
  machine's results, `RE` its "Place: n/N", `RC` a client learning how many
  waypoints the host counted for its car, `RW` a car the water or a fall
  put back (the host: every car it simulates; a client: its own, as
  predicted), `CG` and `CS` Cops and Robbers' gold events and scores as it
  shows them; `netprobe rulesreport <host trace> <client trace>...` compares
  them (docs/review/multiplayer-desync-rules.md);
* the props' lines (lowercase tags, `game::PropTrace`): every placed prop
  once (`p`), every prop that broke loose here and what broke it (`k`), a
  prediction undone (`x`), every 250 ms of session time (`t`) the props down
  (`b`), the knocked-over props and parts shown (`h`) and each car's damage
  level and dents (`d`), and each simulated car's damaging impacts with their
  cause (`i`).

`D`, `K` and `C` use the machine's monotonic clock (shared by every process
on it), so the traces of instances on one computer compare the same
moments: `netprobe syncreport <host trace> <client trace>...` prints how far
apart each machine draws each player's car from its own player's screen,
the frames in which a car moves further than its velocity explains, the
collisions between players each machine had (and how close every machine
drew the two cars around them) and the corrections, then the props (by
session time, see below). A car drawn a fixed time behind (or ahead of) its
player's screen is that far off at its speed however exactly it follows,
so the report also gives, for each viewer and car, the delay that brings
the two drawings closest and what remains at it.

With one trace per machine, a remote car as drawn (`R`) can be compared with
where the other machine's car really was at `sampleTime` (its `F` lines).
`netprobe syncreport <trace>...` compares the machines' props pair by pair
at the same session times: the props knocked on one machine only, how far
apart in time both knocked them, where the knocked-over props and parts
rest, each car's damage, and each car's damaging impacts as its own machine
predicted them and as the host simulated them.

In a cruise with shared traffic the trace also has the traffic and police
(`game/net/TrafficTrace.h`): the host's `TH` lines, each car near a player at
the session time its state belongs to (the AI's step or the physics'); a
client's `TC` lines, the cars where its car meets them each frame, and `TV`,
that frame's session time of its car on the host's clock; the cars the
client's car knocked loose (`TX`), the host's cars leaving their rails
(`TK`) and the client's knocks (and the bodies it had in full) going back
to the host's messages (`TL`). `mm2tool nettrace <host trace> <client
trace>` compares them: each car's error against the host's at the session
time of the client's car (apart: the police and the knocked cars the client
simulates), the cars near the client one machine had and the other did not,
the police targets, the hits and knocks.
Latency, jitter and loss can be added between machines on one computer with
`netprobe relay` in front of the host (tc/netem needs root).
