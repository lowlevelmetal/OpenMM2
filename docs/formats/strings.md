# UI strings (`MMLANG.DLL`)

`GAME/MMLANG.DLL` on the disc is a resource-only PE module. Its `RT_STRING`
table (language 0x0409 on the US disc, 667 entries) contains every UI and
HUD text plus font descriptions. Read with `data::readPeStringTable`
(`src/data/PeResources.cpp`); dump with `mm2tool strings <source>`. The DLL is
parsed as data, never loaded.

Font entries have the form `Face, width?, height, 0, weight`, e.g.
`Gill Sans MT, 12, 24, 0, 400` or `Arial Bold, 32, 64, 0, 400`. The second and
third numbers appear to be GDI `LOGFONT` width/height values (**inferred**).
Neither font ships on the disc: Arial is a Windows system font and Gill Sans
MT ships with other Microsoft products. OpenMM2 uses the system font when
installed and otherwise a metric-compatible open substitute.

Which string id is used where is defined by the executable and therefore
inferred from the text itself. Notable groups (ids from the US disc):

| Ids | Content |
|-----|---------|
| 62–245 | race HUD messages per mode (countdown, penalties, results, Cops & Robbers gold events, Crash Course lesson messages) |
| 276–309 | control actions (the full list of bindable actions) |
| 357–364, 408–417, 624–632 | time of day, weather, difficulty labels |
| 390–396, 574–577, 644–662 | graphics options |
| 418–434, 506–524 | multiplayer host options (laps, gold mass, time/point limits, modes) |
| 532–557 | Crash Course lesson names (London then San Francisco) |
| 558–573 | HUD font sizes |
