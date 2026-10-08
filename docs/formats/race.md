# Race and AI setup files (`race/<dir>/`, `tune/*.cinfo`)

Parsers: `src/city/Race.{h,cpp}`. Loader: `src/city/CityData.{h,cpp}`
(`listCities`, `listRaces`, `loadCity`). Tools: `mm2tool races <source> <city>`,
`mm2tool citycheck <source>`.

Retail coverage (`citycheck`): 209/209 `.aimap(_p)`, 612/612 `.opp`,
80/80 `*waypoints.csv`, 52/52 crash data, 8/8 race tables, 28/28 crash
event point lists. All 882 files the race definitions reference exist and
parse.

## Cities: `tune/<city>.cinfo`

`Key=Value` lines: `LocalizedName`, `MapName` (selects `city/<map>.*`),
`RaceDir` (selects `race/<dir>/`), `BlitzCount`, `CircuitCount`,
`CheckpointCount`, `BlitzNames`/`CircuitNames`/`CheckpointNames`
(`|`-separated, in race order), `MustPlace`, `UnlockGroup`. MM2's
`mmCityInfo::Load` reads the keys in that order up to the name lists (the
counts with `%d`, a nonzero count then replaced by the number of names) and
never reads `MustPlace` or `UnlockGroup`. The `.cinfo.bak` files are editor
backups.

## Race events

| Mode | Prefix | Count | Settings table | Files |
|------|--------|-------|----------------|-------|
| Blitz | `blitz` | .cinfo | `mmblitzdata.csv` | `blitzNwaypoints.csv`, `blitzN.aimap(_p)`, `blitzN.pathset` |
| Circuit | `circuit` | .cinfo | `mmcircuitdata.csv` | `circuitNwaypoints.csv`, `circuitN.aimap(_p)`, `circuitN.pathset` |
| Checkpoint | `race` | .cinfo | `mmracedata.csv` | `raceNwaypoints.csv`, `raceN.aimap(_p)`, `raceN.pathset` |
| Crash Course | `crash` | while `crashNdata.csv` exists (13 per city) | `mmcrashdata.csv` | `crashNdata(_p).csv`, `crashN.aimap(_p)`, `crashN.pathset` |

`_p` files are the professional-difficulty variants. Cruise mode uses
`roam.aimap(_p)`. Other files in the race directories (`examN_N.csv`,
`r1.csv`, `*.csv.old`, `.#*` CVS backups, `dbugps*`, `cirN_strtpnts`) are
editor files or crash-course event lists.

### Settings tables: `mm<mode>data.csv`

Header plus one row per race: `Description`, then two blocks of 10 columns:
amateur then professional. Each block is `CarType, TimeofDay (0..3),
Weather (0..3), Opponents, Cops, Ambient (density 0..1), Peds (density),
NumLaps, TimeLimit (seconds), Difficulty`.

### Waypoints: `<race>Nwaypoints.csv`

`x,y,z,a,radius,...`: checkpoint position, heading in degrees, checkpoint
radius. Some files label column 5 `poly count`. The remaining columns
(`frame rate`, `state changes`, `texture changes`, `msg`) are editor profiling
fields and are kept verbatim. Crash-course event lists, `copchase.csv` and
`multicopwaypoints.csv` use the same layout.

### Opponent lines: `*.opp`

CSV `x,y,z,brake,forward offset,side offset,target speed,speed start,side start`.
Named `<race>-a-<n>.opp` (amateur) and `<race>-p-<n>.opp` (professional) and
referenced from the `[Opponent]` section of the `.aimap`.

### AI setup: `*.aimap`, `*.aimap_p`

`[Section]` headers, `#` comments. A section is a *list* when its first line is
an integer equal to the number of lines that follow. Otherwise its lines are
values. The original parser presumably knows each section's kind. This rule
reproduces it for every retail file.

| Section | Kind | Content |
|---------|------|---------|
| Speed Limit | value | default road speed limit |
| Density | value | ambient traffic density |
| CopChaseDistance | value | |
| AmbientLaneChanges | value | 0/1 |
| Ambients Drive On The Left | value | 0/1 |
| Traffic Lights | value | two model names |
| Exceptions | list | `road density speedLimit` per AI path id |
| Police | list | `car x y z heading mode lane ? ?` (the file's comment header is stale) |
| Opponent | list | `car oppFile <numbers>` |
| Ambient Types/Density | list | `model cumulativeProbability ?` |
| GoodWeatherPedName / BadWeatherPedName | list | `goodModel badModel` |
| Hookmen | list | (empty in retail files) |

### Crash course events: `crashNdata.csv`

`Filename,Event,Checkpoints,TimeLimit,AmbDensity,extra...`: one row per event.
`Filename` names the event's point list (`race/<dir>/<Filename>.csv`, verified
to exist for every event). The meanings of `Event` and the extra columns are
unknown.

### Rewards: `<city>_rewards.csv`

`RaceType,RaceNum,CarName,VariantNum,message`: `RaceNum` is `half` or `all`.
The message may contain commas.
