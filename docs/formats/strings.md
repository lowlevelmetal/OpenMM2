# UI strings (`MMLANG.DLL`)

`GAME/MMLANG.DLL` on the disc is a resource-only PE module. Its `RT_STRING`
table (language 0x0409 on the US disc, 667 entries) contains every UI and
HUD text plus font descriptions. Read with `data::readPeStringTable`
(`src/data/PeResources.cpp`); dump with `mm2tool strings <source>`. The DLL is
parsed as data, never loaded.

MM2 reads it through `AngelReadString` (**verified**): it loads the DLL with
`LoadLibrary` on first use (when it is missing the game shows "MMLANG.DLL not
found." and exits) and fetches each string with the Windows `LoadStringA`
(`MyLoadStringA`), which picks the table of the user's language, converts
the UTF-16 text to the ANSI code page, keeps at most 511 bytes and gives an
empty string for an id that does not exist. OpenMM2 reads the string blocks
itself (block *n* holds ids 16(*n*−1) … 16*n*−1; the first language found,
or a preferred one), converts to UTF-8 for its own text rendering and leaves
missing ids to the caller (**inferred** from the PE resource format; the
retail DLL has one language and no string longer than 360 characters).

Font entries have the form `Face, small height, height, charset, weight`,
e.g. `Gill Sans MT, 12, 24, 0, 400` or `Arial Bold, 32, 64, 0, 400`
(MM2 `mmText::CreateLocFont`): the game creates a GDI font whose cell height
is the second number on screens at least 640 pixels wide and the first one
below, with the given character set and weight.
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
| 558–573 | frontend and popup fonts (`MenuManager`, see below) |

Ids verified from the executable (MM2Recomp) for the in-race HUD:

| Ids | Use |
|-----|-----|
| 59, 60, 61 | fonts of the position debug line, the HUD messages (`mmHUD`) and the chat lines |
| 62, 63 | "Final lap!" / "Lap time" (`mmHUD::PostLapTime`) |
| 251, 253 | number and label fonts of the Place / Check readouts (`mmWPHUD`); 252 is created but unused |
| 254, 255 | "Place:  ", "Check:  " (`mmWPHUD`) |
| 256, 258 | number and label fonts of the circuit readouts (`mmCircuitHUD`); 257 unused |
| 259–261 | "Place:  ", "Check:  ", "Lap:  " (`mmCircuitHUD`) |
| 269, 270 | "Hit Objects:  ", "Hit Vehicles:  " (`mmCollideHUD`; the second is never shown) |

Ids verified from the executable (MM2Recomp) for the frontend:

| Ids | Use |
|-----|-----|
| 558–565 | frontend fonts by `MenuManager::GetFont` size 12, 14, 16 (default), 20, 24, 32, 48, 64; every menu text uses 16 (560) |
| 566–573 | the same sizes for the in-game popups (`MenuManager::Init` with a camera); the results use 20 (569) |
| 4, 5–7, 12 | "Opp.%d" and the results titles Circuit / Checkpoint / Blitz Race, Crash Course (`PUResults`) |
| 64, 65, 66, 77 | "---", the first driver "DriverX" and its net name, a new driver's net name "noname" (`mmInterface::InitPlayerInfo`, `PlayerCreate`) |
| 78–82 | "Crash Course", "Cops & Robbers", "Cruise", "Professional", "Amateur" (main menu driver panel) |
| 344–347, 349, 351, 353–355 | Driver Record and Race Records column headings |
| 389–393 | " - Recommended" and the texture quality choices (`GraphicsOptions`) |
| 492–499, 651, 653–656 | results buttons, "DNF", "Pass" and the lesson variants (`PUResults`) |
| 574–577, 660, 661 | object detail / sound quality and cloud shadow choices |
| 580–584 | controller types (`MenuManager::GetControllerName`) |
| 625–628, 629–632 | weather and time of day choices of the race menu |
| 633, 634 | "Manual", "Automatic" (garage) |
| 635–640 | main menu driver panel labels (639 "NETNAME:" is never shown) |
