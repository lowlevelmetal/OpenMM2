# Music

Midtown Madness 2's soundtrack is interactive DirectMusic: style-based
segments (`.sgt`) that pick patterns from styles (`.sty`), played with
instruments from DLS collections (`.dls`). All of it is in `aud/dmusic/` in
`MM2AUD.AR`. OpenMM2 plays it with [GothicKit/dmusic](https://github.com/GothicKit/dmusic)
(MIT), a DirectMusic reimplementation, with a few fixes for MM2's data (see
[dmusic changes](#dmusic-changes)).

Code: `src/audio/Music.{h,cpp}` (tables, loader, engine, threaded player),
`src/audio/MusicDirector.{h,cpp}` (MM2's rules for when the music changes),
`src/audio/MusicMotif.{h,c}` (motif support, built into the dmusic library),
the dmusic section of `cmake/Dependencies.cmake`. Tools:
`mm2tool musicinfo <source>` and `mm2tool music <source> ...`.

## What the game data says

`aud/dmusic/csv_files/` drives the music:

| File | Content |
|------|---------|
| `singlerace.csv` | one row per race song: Start, Return, Idle Race, Idle Cop, Cop chase, Pause, Race results segments, plus the "Big Air" motif (style, motif name, band) |
| `singleroam.csv` | cruise songs: Start, Return (named *Restart*), Idle, Cop Chase, idle cop, Pause, Big Air motif |
| `ui.csv` | the menu segment (`UI`) |
| `londonambience.csv`, `sfambience.csv` | the city ambience segment ("SFX segment") |

Retail content (US disc, build 3390):

| Table | Songs |
|-------|-------|
| race | Enemy, Bullet, Duck (and Cover), London, PimpHand |
| cruise | SunRoof, TacoLoco, LeTigre, BackOff |
| other | `UI` (menu, Pause style), `LondonAmbience`, `SFAMbience`, `UndergrounAmbience` (sic; London Underground) |

Every song uses the Pause segment for the pause menu, one of two results
segments (VanillaNice style) and the `BigAir` motif of `GrooverStyle.sty`
played with that style's `BigAir` band. Not referenced by any table:
`GrooverStart/Return/Idle/Cops`, `NightRoamStart`, `Vanilla1/2`,
`LondonTrans1`, `useforsomething`, and `BigAir.sgt`. That last one is a
sequence-track version of the motif, which dmusic cannot play.

Observations from the files (all **verified** by parsing them):

* "Start" segments open with an intro and then loop from a loop point back
  into the body (e.g. `LondonStart` loops ticks 75264-222720).
* "Return"/"Restart" segments are the same songs starting mid-way
  (`play_start` > 0, e.g. `DuckReturn` at tick 61440). They let the game
  resume a song after idle or cop music without restarting it.
* Each state segment selects its own patterns through a groove level in its
  command track (London: Start 1-11, Idle 20, Cops 30, Idle cops 31).
* No soundtrack segment has a chord track. Everything plays against the
  DirectMusic default chord (see below).
* Some band entries reference General MIDI instruments from the system's
  `gm.dls`. They are DirectMusic Producer's placeholders for unused channels:
  in the race, cruise, menu, pause, results and ambience segments, no note is
  ever played on such a channel (checked by tracing every note-on). So the
  soundtrack is complete without `gm.dls`. If `$OPENMM2_GM_DLS` (or, on
  Windows, `%WINDIR%\System32\drivers\gm.dls`) exists it is used anyway.

## Behaviour

`audio::MusicEngine` holds three DirectMusic performances: the soundtrack, the
motif layer and the city ambience. `audio::MusicPlayer` runs the engine on a
worker thread that renders ahead into two ring buffers, music and ambience,
which the mixer pulls as `StreamSource`s. Segment files are loaded on a
separate loader thread, so the renderer never waits for disk or DLS parsing.
Commands are applied in the order they were issued.

| Call | Effect |
|------|--------|
| `playMenu()` | menu segment, immediately |
| `startRace(song, cruise, play)` | preloads the song's segments, then plays its Start segment immediately (or, with `play = false`, only selects the song for a `MusicDirector`); `song < 0` picks a random song |
| `setState(state, timing)` | that state's segment on the next beat or measure, or immediately (`MusicTiming`); `Start` / `Return` name the song's two racing segments |
| `setState(Racing)` | Start the first time, Return afterwards (tools) |
| `setState(state)` | without a timing: menus, pause and results immediately, the rest on the next measure |
| `triggerBigAir()` | the motif from the next beat of the soundtrack, played twice, following its tempo and chord |
| `setAmbience("london" / "sf" / "underground" / "")` | city ambience on its own stream |
| `stop()` | silence |

The original's "Music/City Volume" option controlled the DirectMusic buffer,
which plays either the soundtrack or the city ambience segment, so the
ambience stream goes to `Bus::Ambient` with the music slider. The slider maps
to an Angel volume log(200 v) / log(200) (`DMusicWaveBuffer::SetVolume`; the
mixer applies it, see `docs/audio.md`). In MM2 the audio options make "Music"
and "Ambient" exclusive (`AudioOptions::ToggleMusic` / `ToggleAmbient`): with
music on, the city's DirectMusic ambience segment and its 3D ambient emitters
are not loaded (`mmGameMusicData::Load`, `mmPlayer::Init`). OpenMM2 plays
both. MM2 also stops the ambience segment while the camera is underground and
restarts it outside (`MMDMusicManager::UpdateAmbientSFX`, audio flag 0x80);
OpenMM2 has no tunnel state yet. `setAmbience("underground")` plays
`UndergrounAmbience.sgt`, which MM2 never uses (a tool option).

### When the music changes (`MusicDirector`)

Ported from MM2 (`mmGame::StartMusic`, `UpdateDMusic`,
`MMDMusicManager::UpdateMusic`, `MatchMusicToPlayerSpeed`, `mmPopup`'s
`PlayPauseMusic` / `PlayReturnMusic` / `ShowResults`, the race modes'
`StopSegment` calls). The game calls `MusicDirector::update` every unpaused
frame with the player's speed, the number of cops pursuing the player
(`vehPoliceCarAudio::GetNumCopsPursuingPlayer`) and the car's airborne flag
(`vehCarAudio::IsAirBorne`, see `docs/audio.md`), and passes the director's
commands to `MusicPlayer`.

| Rule | Evidence |
|------|----------|
| The song is a row of `singlerace.csv` / `singleroam.csv` drawn with a generator seeded by the clock's second (`audio/AngelRandom.h`); every line after the header counts, so a blank line is a song without segments | MM2 (`mmGameMusicData::RandomizeNumber`, `GetNumDMusicChoiceGroups`, `mmSingleRaceMusicData::LoadMusic`, `LoadMusicSegments`) |
| The Start segment begins 1.25 s into the game, on the next beat | MM2 (`mmGame::StartMusic`) |
| Idle: speed at or below 5 m/s for 5 s; the switch waits for the next measure. The idle timer starts expired, so in cruise (no countdown) a stationary player gets the idle segment at once; the race modes hold the idle logic from the music start until "Go!", which also resets the timer | MM2 (`MatchMusicToPlayerSpeed`, both constants 5.0; flag +0x50 set by `StartMusic` for races, cleared by `mmSingleCircuit` / `mmSingleBlitz` / `mmMultiRace` at the start) |
| Leaving idle (above 5 m/s): Idle → Return, IdleCops → CopChase, on the next measure | MM2 (`MatchMusicToPlayerSpeed`) |
| Stopping during a chase gives IdleCops in races; cruise has no idle-cop segment (the cruise table's column is loaded but its index never set), so the chase music keeps playing | MM2 (`mmSingleRoamMusicData::LoadMusic` leaves +0x30 at -1) |
| Cop chase: when the pursuing-cop count goes from 0 to exactly 1; Return when it goes from 1 to 0, both on the next beat. Other changes (0 → 2, 2 → 0) switch nothing | MM2 (`UpdateMusic`) |
| Big Air: when the car becomes airborne (once per jump); the motif plays as a secondary segment with one repeat (twice) from the next beat (flags DMUS_SEGF_SECONDARY, GRID and BEAT; the beat is assumed to win); a jump while it still plays adds nothing | MM2 (`UpdateMusic`, `DMusicObject::PlayMotif`, flags 0x880 \| 0x1000, `SegmentWrapper::Play` with SetRepeats(1), nothing while playing) |
| Pause: the Pause segment on the next beat; resuming restarts the previous segment (whatever it was) from its beginning | MM2 (`mmPopup::PlayPauseMusic`, `PlayReturnMusic`) |
| Finish: the race modes stop the music at once (`StopSegment(0)`); the music logic keeps running, so standing still for 5 s brings in the idle segment; the results popup starts the results segment on the next beat (with an END embellishment, not rendered by dmusic) | MM2 (`mmSingleRace` / `mmSingleCircuit` / `mmSingleBlitz` `StopSegment`, `mmPopup::ShowResults`) |
| Wrecked: the race modes end the music with a composed ending on the next beat (`StopSegment(1)`: AutoTransition to nothing, DMUS_COMMANDT_END, DMUS_COMPOSEF_BEAT); OpenMM2 stops on the next beat (`MusicDirector::damagedOut`) | MM2 (`mmSingleRace::UpdateGame`, `DMusicObject::StopSegment`); the ending **inferred** |
| A segment switch to the segment already playing does nothing | MM2 (`DMusicObject::SegmentSwitch`) |

Notes on the port:

* MM2's measure switches are composer transitions (`AutoTransition` with the
  groove command and `DMUS_COMPOSEF_MEASURE`). dmusic's composed fill
  transitions are silent for styles without fill patterns, so the new
  segment simply starts on the measure.
* Music variation is random per play, as with DirectMusic's `rand()`-based
  pattern and variation choice. `$OPENMM2_MUSIC_SEED` fixes the seed for
  reproducible renders.

## dmusic changes

The upstream sources are patched at configure time. Each patch is checked, so
a future pin bump fails loudly if one no longer applies:

1. **Missing collections** (`Band.c`): an instrument whose DLS collection
   cannot be found (`gm.dls`) is skipped instead of failing the band and with
   it the whole segment.
2. **Empty collections** (`Band.c`): instrument lookup underflowed for an
   empty collection.
3. **Parts without variations** (`Performance.c`): a part with no valid
   variations (in `Pause.sty`) caused a division by zero; it is skipped.
4. **Late style** (`Performance.c`): a segment whose style track starts after
   its first groove command (`DuckCops.sgt`) uses that style from the start,
   instead of crashing on a NULL style.
5. **Mid-way starts** (`Performance.c`): when a segment starts mid-way
   (`play_start` > 0, all Return/Restart segments and two cop segments), the
   style, band, tempo and groove in effect at the start point apply. Upstream
   discarded them, which left those segments silent or playing with the
   previous segment's style.
6. **Motif support** (`Performance.c`): `DmPattern_generateMessages` is
   exported for `MusicMotif.c`.

Two more fixes live in `MusicMotif.c`:

* **Default chord.** Performances start with DirectMusic's default chord, C2
  major (root 12, pattern `0x91`, scale `0xab5ab5`). dmusic started from an
  all-zero chord, which, with no chord tracks in MM2, put every
  chord-relative note an octave too low; bass notes even came out negative.
  The evidence: with the default chord, the drum music values map exactly
  onto the key ranges of the MM2 DLS drum instruments. For example the
  SunRoof hi-hats `0x3010`/`0x310f`/`0x320f` land on keys 38/39/42 and the
  kick `0x3300`/`0x4000` on 47/48; dmusic's zero chord gave 26/27/30 and 35/36.
  That previously silenced the first seconds of `SunroofStart`.
* **Motifs.** DirectMusic plays a motif as a secondary segment; dmusic has
  none. `OpenMM2_DmPerformance_playPattern` plays a named pattern of a style
  with a named band of that style on the motif performance.

## Known differences from the original

* **Synthesizer.** dmusic converts DLS instruments to TinySoundFont.
  Articulations that SoundFont cannot express (some scaled DLS connection
  blocks) are dropped, and there is no reverb or chorus, which the Microsoft
  software synthesizer applied. Timbre and envelopes are close but not
  identical.
* **Level.** The synth output is scaled by 0.5, which keeps every song below
  full scale (peaks of -0.6 dBFS or lower across all states). The original's
  absolute music level relative to effects is not known.
* **Pattern selection.** dmusic follows DirectX 7-style random selection and
  never picks one-measure groove patterns (a rule added upstream for Gothic).
  For MM2 that skips `LondonIntro` and `VanillaIntro` at groove 1, so the
  longer pattern at that level is always used.
* **Unsupported content.** Sequence tracks (`BigAir.sgt`) and DirectMusic
  Producer chunks dmusic does not read (`mtfs` motif settings) are ignored.

## Verification

* `mm2tool musicinfo` loads all 70 segments referenced by the tables (and
  the tests load every `.sgt` in `aud/dmusic`).
* Every song × state (race: racing, idle, idle-cops, cop-chase, paused,
  results; cruise: the same without results) rendered for 8 s: no clipped
  samples, no silent seconds, per-second RMS between about -37 and -12 dBFS.
* A full state sequence (racing → idle → cop chase → racing → paused →
  racing → results) renders without gaps.
* The Big Air motif adds sound only from the beat after the trigger (it plays
  twice); the render before it is bit-identical with a fixed seed.
* Rendering runs at about 300× real time (release build); the player keeps
  about 85 ms (4096 frames at 48 kHz) buffered, which is also the worst-case
  latency of a command before the musical boundary it waits for.
* `tests/audio/test_music.cpp` covers table parsing, loading, audible output,
  the motif, silence after stop, the default-chord fix, streaming through
  the mixer and the `MusicDirector` rules.
