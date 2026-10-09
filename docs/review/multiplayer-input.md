# Multiplayer review: network input robustness and security

Reviewed on 2026-10-08 from `integration` at 2734bea. Line numbers below are
at that commit.

Everything that arrives from the network is treated as hostile: other players,
anyone who can send a UDP packet to the game port (2300) or the LAN discovery
port (2301), and any device that answers as the router (SSDP/UPnP, PCP,
NAT-PMP). The question for every input: can it crash, hang or exhaust the
machine, corrupt state or files, or make the UI or the race misbehave?

## What I read

* `src/net/BitStream.{h,cpp}`, `Protocol.{h,cpp}`, `Snapshot.{h,cpp}`,
  `Session.{h,cpp}`, `Transport.{h,cpp}`, `ClockSync.{h,cpp}`, `Net.{h,cpp}`,
  `Discovery.{h,cpp}`, `PortMapper.{h,cpp}`, `NatPmp.{h,cpp}`,
  `NatPmpBackend.cpp`, `UpnpBackend.cpp`, `Sha256.{h,cpp}`, all completely.
* ENet 1.3.18's packet, fragment and range-coder code (`peer.c`,
  `protocol.c`, `compress.c`) where our limits depend on it, and miniupnpc
  2.3.3's buffer contracts for the calls `UpnpBackend` makes.
* Where received strings and numbers end up: `src/game/net/NetGame.{h,cpp}`;
  `src/app/RaceScreen.cpp` (remote cars, the race and Cops and Robbers event
  handlers, net player list, city loading, `leaveRace`);
  `src/app/frontend/PagesMulti.cpp` (session list, lobby roster, chat,
  settings lines); `Frontend::saveNetEvent` in `FrontendScreen.cpp`;
  `game::session::Session` net racers, `CopsAndRobbers::receive`,
  `hud::netIconColor`, `GpuModel::materials`, `ui::nextCodepoint`,
  `city::loadCity`, `vfs::DirectoryFs`, `IniFile::serialize`.
* MM2Recomp for the one place parity applies: `mmNetObject::Init` /
  `ReInit`, `mmVehList::GetVehicleInfo`, `SetDefaultVehicle` and `LoadAll`
  (the default vehicle), and `mmMultiCR::GameMessage` (who may send what).

## How it was tested

* `tests/net/test_hostile_input.cpp`: a hand-built host or client on a raw
  `Transport` sends what an honest OpenMM2 never would (control characters,
  path-like car and city names, 250 players, floods, oversized packets,
  spoofed LAN traffic, damaged state files, password guessing) to a real
  `Session`, `LanBeacon` or `LanScanner`. Every behaviour test there fails
  without its fix.
* `tests/net/test_fuzz.cpp`: fixed seeds; 40 000 random and mutated buffers
  (bit flips, overwrites, truncation, appends, inserts, splices, type swaps,
  noise) through every message decoder, the LAN and router decoders and the
  event payloads, checking that whatever decodes stays within what a writer
  could produce; then 3 000 mutated messages each into a live host (from a
  joined peer, a peer that never sent Hello and raw datagrams below ENet) and
  into a live client (from a fake host), checking the session stays
  consistent.
* `tests/game/test_netgame_input.cpp`: received densities and the default
  vehicle for an unknown car.
* `test_net` built with `-fsanitize=address,undefined` (a separate build
  directory): all 65 tests pass with no AddressSanitizer, LeakSanitizer or
  UBSan report in OpenMM2 code. UBSan reports one shift in ENet's range
  decoder (`compress.c`, `ENET_RANGE_CODER_SEED`: a byte shifted by 24 into an
  `int`'s sign bit) on ordinary traffic; it is third-party, benign with GCC and
  Clang, and suppressed for the run (`shift-base:compress.c`).
* A host and a client on loopback (UDP 2330, frontend script) into a cruise
  race after the changes, with the default cars and with a client car the
  host's catalog does not have.

## Findings

Severity: crash / gameplay-breaking / visible / minor / code quality.

### Fixed

| # | Severity | Location | Scenario | Confirmed | Fix |
| --- | --- | --- | --- | --- | --- |
| 1 | visible | `src/net/Session.cpp:22-37` (`sanitize`), `:247` (`uniqueName`) | The host's cleanup popped every trailing byte with the high bit set, so a complete last character was dropped: "José" joined as "Jos", a chat line "à bientôt" lost its last letter. `uniqueName` cut a name to fit " (2)" in the middle of a UTF-8 sequence. | test `NonAsciiNamesAndChatSurviveTheHost` | 13634e9 |
| 2 | gameplay-breaking (profile corruption) | `Session.cpp:638-752` (`clientHandle`: Welcome, PlayerJoined, PlayerUpdate, Chat, Settings, Kick, Reject); `FrontendScreen.cpp:315`; `src/core/Ini.cpp:72` | A client used everything the host sent as is. The city went into `Profile::city` (`Frontend::saveNetEvent`) and `IniFile` writes values raw, so a host sending `city = "x\n[Races]\n..."` added arbitrary lines to the joiner's profile file. Names, chat, the session name and kick/reject reasons reached the UI and the log with control characters and invalid UTF-8. | test `ClientCleansUpWhatTheHostSends` | 13634e9: `sanitizeText` on all received text; car and city names must be base names (`isValidAssetName`), otherwise the default car / an empty city |
| 3 | minor (performance) | `Session.cpp:675-709` | A host could make a client list 255 players (PlayerJoined with every id, including `kInvalidPlayerId`), and every listed player gets a car in the race. | test `ClientKeepsAtMostMaxPlayers` | 13634e9: at most `kMaxPlayers`, never id 255; a Welcome that does not list the joiner fails the join |
| 4 | minor | `Session.cpp:501-515`, `:565` | A joiner's car name was any 32 bytes ("../../etc/passwd"), relayed to everyone and loaded by name. The VFS is a closed index (`DirectoryFs` scans its root; `..` collapses inside it), so no file outside the game data could be read, but the name reached file names and the UI. | test `HostRejectsCarNamesThatAreNotBaseNames` | 13634e9: default car in Hello, previous car kept on a request |
| 5 | gameplay-breaking (denial of service) | `Session.cpp:501-549` | One joiner's chat, PlayerRequest and GameEvent messages were each relayed to every other player on reliable channels with no limit. 300 of each from a raw client reached the other player 300 times each; ENet's outgoing queues are unbounded. | test `HostRateLimitsWhatItRelays` | cbd7079: per-joiner budgets (chat 2/s burst 8, updates 10/s burst 20, events 30/s burst 60); requests are coalesced, never lost |
| 6 | crash (memory exhaustion) | `src/net/Transport.cpp:58`, `:79` | ENet's defaults allow a 32 MiB packet and 32 MiB of waiting data per peer, and it allocates a fragmented packet's whole size on its first fragment. Any connected peer, before Hello, could make the host allocate tens of MiB with a couple of datagrams (16 slots: about 1 GiB). A 256 KiB packet was delivered. | test `OversizedPacketsAreNotDelivered` | 3029e41: 64 KiB per packet, 1 MiB waiting per peer |
| 7 | gameplay-breaking (reflection) | `src/net/Discovery.cpp:290` (`LanBeacon::update`) | The beacon answered every well-formed 10-byte query from any source with an advert of up to about 160 bytes, 64 per frame. With a reachable discovery port (a host with a public address, no firewall), a spoofed source made it a reflector of up to 16 times the traffic; a spoofed LAN broadcast source sent each reply to a whole subnet. 400 queries got 400 replies. | tests `BeaconRepliesAreRateLimited`, `BeaconAnswersOnlyLanSources` | 31cd6de: loopback, private and own-subnet sources only (VPN subnets included), never broadcast/multicast/zero; 20 replies/s, burst 40 |
| 8 | minor (memory, UI) | `Discovery.cpp:357` (`LanScanner::receive`) | Spoofed adverts (any source, any game port) each added a session: 2 000 adverts listed 2 000 sessions, with control characters in their names; a game port of 0 was listed. | test `ScannerBoundsAndCleansAdverts` | 31cd6de: at most 64 sessions, text cleaned, port 0 skipped |
| 9 | gameplay-breaking (other programs) | `src/net/PortMapper.cpp:82-83`, `PortMapper.h:99`; `NatPmpBackend.cpp:79`, `:194` | The crash-safe record's ports were cast to 16 bits unchecked (70000 became 4464) and internal port 0 passed. The next start's clean-up then sent a NAT-PMP/PCP deletion for internal port 0, which RFC 6886 3.4 and RFC 6887 11.1 define as "every mapping of this machine", other programs' included. The nonce's hex accepted signs ("0x-1"). | test `DamagedPortMappingRecordIsIgnored` (failed on the old code) | 255e792 |
| 10 | minor | `src/net/UpnpBackend.cpp:96-101` | Any LAN device can answer SSDP as the router; its "external address" string went into the lobby status line as given. | by reading | 255e792: dropped unless it parses as an address |
| 11 | visible (parity) | `src/app/RaceScreen.cpp:3335-3347` | A remote car this machine lacks (an add-on car on the other machine) failed to load, and because the condition included `!rv.renderer` the load was retried and a warning logged every frame (~200 a second) while the car stayed invisible and could not be hit. MM2's `mmNetObject::Init` goes through `mmVehList::GetVehicleInfo`, which answers an unknown name with the default vehicle, vpcoop (`mmVehList::LoadAll`). | fallback: test `UnknownRemoteCarIsTheDefaultVehicle`; the per-frame retry by reading (an honest client cannot drive a car it lacks, so it was not reproduced live) | 622dd1c: `game::netVehicle`; a load that still fails is tried once per car and paint job |
| 12 | minor | `src/game/net/NetGame.cpp:136-137` | Traffic and pedestrian densities arrive as a percent byte; a host could send 255, 2.55 times full traffic. | test `DensitiesFromTheNetworkAreAtMostFull` | 622dd1c |
| 13 | visible | `src/app/frontend/PagesMulti.cpp:777` | The roster's "%.6s..." cut six bytes, splitting a UTF-8 character ("aéééé" showed a replacement glyph). | by reading | 5e227d3: six whole characters, like `fit()` on the main menu |
| 14 | crash (latent) | `Transport.cpp:179` (`Transport::send`) | An empty packet makes ENet's range coder (`enet_range_coder_compress`) read the packet's null data: the sender segfaults. Found by the fuzz test. No path sends one today (every message has its type byte), and a remote peer cannot cause one. | fuzz test (segfault) | 810f5a9: `send` refuses empty data |
| 15 | minor (security) | `Session.cpp:489` | A wrong password cost a guesser only a reconnect; with 16 connection slots a lobby password could be guessed at round-trip speed. | test `WrongPasswordsLockTheAddressOut` | f5ee71f: five wrong passwords from an address within a minute lock it out until the minute ends |

### Checked and correct

* **BitStream** (`BitStream.cpp`): every read checks the remaining bits and the
  error is sticky (further reads return 0); varints reject more than 10 bytes
  and 64-bit overflow; `string`/`bytes` check the length against both the
  limit and the remaining bits before allocating; `ranged`/`enumeration`
  reject values above the maximum; `f32` rejects NaN and infinities; quantized
  values stay within their range; a decoded quaternion is normalized and its
  largest component is rebuilt from the others, so it is never zero.
* **Protocol** (`Protocol.h`): every count (players, extras, vehicles, pings)
  is checked before the vector is resized; `decodeMessage` rejects a type
  mismatch and trailing bytes; `maxPlayers` 0 or above 16 is rejected; event
  payloads are at most 1 KiB.
* **Snapshots**: all fields are quantized (position ±16384 m, velocity
  ±200 m/s, angular ±64 rad/s, controls 0-1/±1, gear −1-14), so no NaN or
  infinity reaches physics or rendering. Interpolation between snapshots with
  extreme times stays finite (positions up to ~2.5e8 m at a 49-day span).
* **Protocol version**: the ENet connect data (`kConnectData`) and Hello's
  version are both checked; LAN adverts of another version are listed as
  incompatible without parsing the body.
* **SHA-256** matches the FIPS 180-4 vectors (`Sha256.KnownVectors`).
* **PCP/NAT-PMP decoders** check every length before reading; replies are only
  accepted from the gateway's address, and MAP replies must echo the nonce.
* **miniupnpc**: the buffers passed to `UPNP_GetSpecificPortMappingEntry`,
  `UPNP_GetExternalIPAddress` and `UPNP_GetValidIGD` meet the library's size
  contracts (client 64 ≥ 16, port 8 ≥ 6, description 80, enabled 8 ≥ 4,
  lease 16).
* **Indexes from the network**: a paint job is taken modulo the model's
  shader sets (`GpuModel::materials`, as `vehCarModel::Init`); icon colours
  clamp the player id (`hud::netIconColor`); the team is only compared with 0;
  waypoint counts are taken modulo the course; Cops and Robbers look cars up
  by id (`CopsAndRobbers::teamOf`, `score`), never index by them; race ids
  are searched for, not indexed (`loadRaceSetup`, `hostSettingsLines`).
* **Names as paths**: car, city and race names only reach the VFS, which only
  knows the files it indexed, and `city::loadCity` failing sends the race
  screen back to the menus without leaving the session.
* **Chat history** is capped at 64 lines; the UI's UTF-8 decoder replaces
  invalid sequences rather than reading past the string.

### Open

| # | Severity | Location | Scenario | Status / what it needs |
| --- | --- | --- | --- | --- |
| O1 | minor (denial of service) | `Session.cpp:401` (`Connected`), `TransportConfig::maxPeers` | The host has 16 ENet peer slots. A peer that connects and never sends Hello keeps one for `joinTimeoutMs` (10 s), and ENet keeps half-open connects from spoofed sources for its own timeout. Sixteen such connections keep real players out. | plausible, not reproduced. Needs a per-address limit on pending handshakes and a shorter host-side handshake timeout (an honest client sends Hello one round trip after the Challenge); ENet's own half-open state cannot be limited without patching it. |
| O2 | minor (security, design) | `Session.cpp:52` (`passwordProof`) | Someone who records a handshake (nonce and proof) can try passwords offline: SHA-256 of nonce and password is fast. | by design and documented in `docs/multiplayer.md`; a fix needs a slow hash or a PAKE and a protocol version bump. |
| O3 | minor | `NatPmpBackend.cpp:252` (`request`) | When the socket wait fails (for example `EINTR`), the inner loop retries at once and spins the worker thread until the retransmit interval ends (at most 2 s). | by reading; needs a short sleep or a break on error. |
| O4 | code quality | `BitStream.cpp:132` (`readQuantized`) | With `bits == 0` it returns 0/0 = NaN. No caller passes 0 (all widths are constants of 4 or more), so the network cannot reach it. | by reading; could return `min`. |
| O5 | minor | `PortMapper.cpp:332` (stale clean-up) | Two OpenMM2 processes sharing one user data directory and hosting on different ports: the second removes the first one's live mapping as "stale". | by reading; needs the owner (process id or internal port) in the record. |
| O6 | minor | `Discovery.cpp:357` | An advert's address is its source address, which a LAN attacker can spoof: a fake session can point at any IP and port, and a player who joins it sends ENet connect attempts there. | by reading; inherent to unauthenticated LAN discovery. |

## For the other areas

* **In-race sync**: the host relays a client's `VehicleSnapshot::time` as
  sent (`Session.cpp:538`), and remote buffers interpolate between whatever
  times arrive. A client with a wildly wrong time (or a hostile one) moves its
  own car far from where it is; values stay finite. The host could clamp
  snapshot times to a window around its own clock.
* **Cops and Robbers**: any player can send the events only the host sends
  (`GoldPickedUp`, the new set `kCrNewSet`; `RaceScreen.cpp:2171-2180`) and
  every machine applies them, so a modified client can take the gold or move
  the places. MM2's `mmMultiCR::GameMessage` does not check the sender either
  (0x25a, 0x261), so a check would be an OpenMM2 deviation: accept them only
  from `kHostPlayerId`, or install a host event filter that drops them from
  joiners.
* **Lobby flow**: when a client cannot load the host's city
  (`RaceScreen.cpp:740`), its race screen goes back to the lobby page without
  a message while the others race.
* **Vehicles**: a local car the catalog lacks (only reachable through the
  `vehicle:` automation command or a hand-edited profile) leaves the player
  without a car ("geometry/<name>.pkg not found"); MM2 would load vpcoop
  (`mmVehList::GetVehicleInfo`).

## The reported problems

Nothing in this area makes a host leave the race after loading or explains the
lobby bounce or the cars bouncing: honest clients never hit the new budgets
or name checks (the loopback host/client run into cruise behaved as before),
and none of the old input handling ended a race. Those belong to the lobby
flow and in-race sync reviews.
