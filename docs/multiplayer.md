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
for the lobby: settings, the player list, ready flags, kicks and the countdown.
Clients send requests and the host validates and broadcasts the result.

In game, each machine simulates its own car and sends its state about 20 times
a second. The host bundles the latest state of every car into one packet per
tick for each client. Remote cars are drawn from an interpolation buffer about
100 ms in the past. When packets stop coming, a remote car is extrapolated for
up to 250 ms and then held still.

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
machine. Mappings also expire on their own once the lease runs out.

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

Passive listening for unsolicited adverts is optional and off by default. On
Linux, a UDP unicast datagram to a port that several sockets share
(`SO_REUSEADDR`) reaches only one of them. A passive scanner socket can
therefore swallow a loopback query meant for a host on the same machine.

Adverts whose protocol version differs are still listed, but flagged
`compatible() == false`, so the browser can say "different version".

## Wire protocol

Transport is ENet 1.3 with range-coder compression and three channels:

| Channel | Delivery | Traffic |
| --- | --- | --- |
| 0 Control | reliable, ordered | handshake, lobby, chat, clock sync, pings |
| 1 State | unreliable, sequenced | `VehicleState` (client→host), `WorldState` (host→clients) |
| 2 Events | reliable, ordered | `GameEvent` |

Every packet is one message: a `MsgType` byte, then the body, bit-packed with
`BitStream`. Readers check every length, count and enum range. A malformed
message is dropped, and an unparseable `Hello` disconnects the peer with
`ProtocolError`.

### Versioning

The ENet connect request carries `kConnectData = 'M2' << 16 | kProtocolVersion`.
A host disconnects mismatched peers right away with `VersionMismatch`, so
incompatible builds never parse each other's messages. **Any change to message
layouts must bump `kProtocolVersion`.** `Hello` carries the version again,
along with a free-form build string.

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
sniffing; traffic is not encrypted.

A handshake must finish within `joinTimeoutMs` (10 s), and an ENet connect
attempt within `connectTimeoutMs` (8 s).

### Messages

| Message | Direction | Content |
| --- | --- | --- |
| Challenge | H→C | nonce, password required |
| Hello | C→H | protocol version, build, name, car, colour, team, password proof |
| Welcome | H→C | your id, settings, player list, phase, host time, countdown end |
| Reject | H→C | reason, text |
| PlayerJoined / PlayerUpdate | H→C | `PlayerInfo` |
| PlayerLeft | H→C | id, reason |
| PlayerRequest | C→H | car, colour, team, ready |
| Chat | both | sender id (set by host), text ≤ 200 bytes |
| Settings | H→C | `SessionSettings` |
| Countdown | H→C | session time at which the race starts |
| ReturnToLobby | H→C | — (ready flags reset) |
| Kick | H→C | reason text (then disconnect `Kicked`) |
| TimeRequest / TimeResponse | C↔H | client send time / + host time |
| VehicleState | C→H | own `VehicleSnapshot` |
| WorldState | H→C | `(id, VehicleSnapshot)` for every other car |
| GameEvent | both | from, target, type, session time, payload ≤ 1 KiB |
| PlayerPings | H→C | measured RTT per player, every 2 s |

`SessionSettings` holds the session name, city, mode (Cruise, Checkpoint,
Circuit, Blitz, Cops & Robbers, Crash Course), race id, laps, time of day,
weather, traffic and pedestrian density (percent), cops on/off, max players
(protocol limit 16; the original allowed 8), a password flag, the
join-in-progress policy, and up to 32 extra key/value pairs. The extra pairs let
the game add options without a protocol bump.

### Session clock

The host's clock (ms since the session started) is the shared timeline.
Clients estimate it NTP-style: offset = hostTime + rtt/2 − receiveTime. They
send a quick burst of six requests after joining, then one every 2 s. Of the
last 8 samples, the one with the lowest RTT is used, since it has the least
asymmetry error. Countdown start, snapshot times and game event times are all
in session time.

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

### Game events

`Session::sendGameEvent(type, payload, target)` delivers reliably and in order.
The host relays events. A host-side filter (`setEventFilter`) can validate or
drop them, for example to check that a checkpoint is plausible. Well-known
types, with payload structs in `Protocol.h`:

`CheckpointReached{index, raceTime}`, `LapCompleted{lap, lapTime}`,
`RaceFinished{raceTime, position}`, `GoldPickedUp/GoldDropped/GoldDelivered
{position, team}`, `Collision{other, position, impulse}`,
`Damage{damage, source}`, `Wrecked`. Ids from `GameEventType::Custom` (0x8000)
up are free for the game.

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
  mismatch, unreachable host, countdown, snapshot delivery, event relay and
  filtering, kick, host shutdown, join-in-progress). It also covers
  `PortMapper` against fake backends: failure reporting, fallback, port
  conflicts, renewal and loss, crash cleanup, other-gateway records, double NAT
  and restart. None of it needs a router.
* `netprobe info | host | join | scan | portmap` for manual testing on real
  networks. `netprobe portmap --discover-only` is read-only: it finds the
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
Mustang Cruisers ... the Robber Team in Mustang GTs"): `NetGame::raceConfig()`
returns `vpcop` for team 0 and `vpmustang99` for team 1. Robber Teams lets
everyone choose. Free-For-All has no team lamps: the lobby sets the team
from the car, 0 for a police car (flag 0x08) and 1 for any other
(`game::freeForAllTeam`, MM2's `mmMultiCR::InitMyPlayer`). The transmission
is each driver's own (`NetCar::automatic`, the garage's TRANSMISSION) and
never travels: MM2's session data has none, and `mmGame::Init` sets the car's
from the player's own state.

### Menus

| Screen | Background | Notes |
|---|---|---|
| Sessions | `sess_bk` | providers: only TCP/IP is available (Internet/LAN over OpenMM2's UDP protocol); net name; HOST, JOIN; LAN session list (refreshed every second, also queries 127.0.0.1) in NetSelectMenu's box (289,243, 329 x 131); while nothing has been found, "Looking for games..." blinks dark grey / yellow every 0.75 s in the description box (40,396), where only the provider lamps show pictures. Entering it, a locked car becomes the VW New Beetle (`vpbug`) with paint job 0 and a locked paint job paint job 0 (`mmInterface::GetUnlockedCar` / `GetUnlockedColor`); the lobby's garage checks again when it closes |
| Host options | `host_dlg` | password (optional, 25 characters), max players 2-8 (a roller; MM2 offers 1-8), Cancel / DONE (`Dialog_Host`); the last accepted values come back the next time (`Dialog_Host::PreSetup`), Cancel drops what was typed |
| Enter an address | `tcp_dlg` | IP, `ip:port` or host name; blank = search the LAN again; Enter is DONE (`Dialog_TCPIP`); it starts with the driver's last address, which is saved with the driver when a race starts (`Dialog_TCPIP::SetIPAddress`, `mmInterface::BeDone`) |
| Password | `pass_dlg` | asked before joining a listed session that has one, and when a session joined by address wants one; a wrong password shows `badp_dlg` and asks again (`Dialog_Password`, `mmInterface::Update`) |
| Lobby | `lobbh_bk` / `lobbj_bk` | `NetArena`: host settings (mode, race, weather, time, laps or gold weight, limit), the race map and the city's name, players (`mmCompRoster` rows: the `ready` icon, which the host always shows, the team dot `blue_dot` / `red_dot` in team games, the name cut to six characters and "..." when wider than 0.09 of the screen, the car), YOU, team lamps for team games, chat line ("Type message here. Press ENTER to send."), the last three chat lines (" Name> text") of this visit, port forwarding status (host, where a race without a map would show it); host: EJECT PLAYER, HOST SETTINGS, SELECT VEHICLE in the row above GO DRIVE (which starts once everyone else is ready); joiner: SELECT VEHICLE, READY. A joiner's READY is cleared when the host changes the settings and when the joiner opens SELECT VEHICLE. BACK and Escape leave the session at once. When the race starts, the lobby's car and the session's event become the driver's last car and event, as for a single-player race (`MultiStartGame` calls `BeDone`). |
| Host settings | `host_bk` | game type lamps, race name + laps panels (`host_rnm`, `host_lap`) or the Cops & Robbers panel (`host_cr`: game types, limits, gold mass), location, time of day, weather (adds Snowing), pedestrian density |
| Eject | `ejct_dlg` | pick a player to remove |

Positions of the provider/race/Cops & Robbers/team lamps, the HOST/JOIN and
bottom-right arrow buttons and the three host panels were found by matching
the sprites against the backgrounds (see `docs/frontend.md`); the lobby's
widgets and the network dialogs follow tune/widget.csv (menus 12, 14, 25,
36); the eject list and the players' rows are **inferred**. All races
are selectable in multiplayer (**inferred**: joiners' progress cannot gate the
host's choice).

When the host presses GO DRIVE, `NetGame::startRace()` starts a 6 s
countdown in the session clock. `FrontendScreen` sees `takeRaceStart()` on
every machine and switches to `makeRaceScreen(ctx, netGame->raceConfig())`
(with `config.multiplayer = true`); the race itself starts at
`raceStartTime()`. A `RaceResult` with `config.multiplayer` brings the player
back to the lobby instead of the results screen.

Every countdown counts a race (`NetGame::raceNumber()`, from 1); the race
screen keeps the number of its race. A return to the lobby ends the race it
arrives in (`backToLobby(number)`), even when the player is in the menus by
then (the host's own return, which reaches the menus as an event after the
race screen has gone, or a joiner who quit the race early). Game events still
queued when a countdown starts are from an earlier race and are dropped.

### Per-frame contract for RaceScreen

When `config.multiplayer && ctx.netGame`:

1. **Every frame, first:** `ctx.netGame->update()`. Without it the session
   times out.
2. **Start:** keep the player's car held at its start position while
   `ctx.netGame->secondsToStart() > 0` (show the countdown from it; Ready / Set /
   Go are the last 3 s). `raceStarted()` becomes true at the start time.
3. **Local car:** after stepping the simulation,
   `submitLocalState(car.modelMatrix(), linearVelocity, angularVelocity,
   controls, damage01, flags)` where `controls` are the pedal/steering inputs
   and gear, and `flags` combine `net::kVehicleBrakeLights`, `kVehicleHeadlights`,
   `kVehicleHorn`, `kVehicleSiren`, `kVehicleWrecked`. Sending is rate limited
   inside (20 Hz).
4. **Remote cars:** `for (const auto& car : ctx.netGame->remoteCars())` gives
   each other player's `transform` (car model matrix, ~100 ms in the past,
   extrapolated up to 250 ms when packets are late), velocities, controls,
   damage and flags. Draw cars with `hasState`; spawn one `VehicleRenderer`
   per `car.car.vehicle` / `car.car.color`. For collisions, use them as
   kinematic bodies driven by the transform/velocity (the original also showed
   remote cars from received positions).
5. **Events:** report what the race rules decide: `sendCheckpoint(index,
   raceTimeMs)`, `sendLap(lap, lapTimeMs)`, `sendFinish(raceTimeMs, position)`,
   Cops & Robbers `sendGold(GoldPickedUp|GoldDropped|GoldDelivered, position,
   team)`, `sendCollision`, `sendDamage`. Read others' with `takeGameEvents()`
   (`event.as<net::FinishEvent>()` etc.) to rank players and show messages.
6. **End:** the host calls `ctx.netGame->returnToLobby()` when the race is
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
its PREV do), and the pages `hostoptions`, `address`, `hostsettings`,
`eject`. The script survives a race: after it the menus carry on with the
commands after the one that started it, and `wait:race` waits until a race
has been driven. Example: one process hosts
(`profile:A;page:sessions;mp:host;wait:900`), another joins
(`profile:B;page:sessions;mp:join:127.0.0.1;wait:100;mp:ready`); a second
race after changing cars in the lobby:
`profile:A;mp:host;wait:1500;mp:start;wait:race;wait:300;mp:car:vpcop;wait:300;mp:start`
with `OPENMM2_POPUP_SCRIPT="wait:500;open:quit;wait:5;nav:accept"` (Quit to
Lobby) and `profile:B;mp:join:127.0.0.1;wait:100;mp:ready;wait:race;wait:100;mp:car:vpmustang99;wait:50;mp:ready`.

`test_game` (`tests/game/test_netgame.cpp`) runs a host and a client
`NetGame` in one process: settings round trip, join by address, LAN
discovery, password refusal, chat, car and ready changes, settings
propagation, countdown, vehicle state replication, game events, return to
lobby, host shutdown and eject. Port forwarding is disabled in these tests.
