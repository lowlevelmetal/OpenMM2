# World rendering

How OpenMM2 draws the city, cars and sky (`src/game/CityRenderer`,
`VehicleRenderer`, `TextureLibrary`, `ModelLibrary`), and the evidence for
each choice. "Verified" means checked against the retail data (usually by
rendering it and looking); "inferred" means a reasoned guess that should be
compared with the original game running.

| Topic | Behaviour | Evidence |
|---|---|---|
| Texture orientation | `.tex` rows are uploaded in file order (bottom row first) and the game's UVs are used unchanged | verified: road markings, facades, car bodies and signs appear upright |
| Texture wrap | models: `.tex` flags 0x2 / 0x4 select repeat in U / V, otherwise clamp; TGA/JPG repeat. Street (PSDL) geometry always repeats | verified: facades repeat their textures vertically although the textures lack flag 0x4; with clamping they smear |
| Mipmaps | file mip levels when the chain is complete, otherwise generated with a box filter | — |
| Animated textures | `tune/<name>.movie` gives `rate N` (frames per second); frames are `texture/<name>-0001.tex` and onwards | verified for s_ocean, s_thames, s_water, s_pond |
| Night textures | at night `<name>_ni` replaces `<name>` when it exists (430 such textures, 401 with a day counterpart: lit windows, shop fronts) | inferred from the data |
| Sky | `city/<map>.sky` names the dome model; its 16 paint jobs are time of day × weather in the same order as the lighting tables (texture letters: a dawn, n noon, d dusk, m midnight; c clear, p partly cloudy, f fog, r rain) | verified by viewing the sky textures |
| Lighting | `city/<map>.ltNN`: key + two fill directional lights and an ambient colour, applied as Direct3D 7-style per-vertex lighting (clamped per vertex) to city geometry and models | the light direction convention (heading about +Y from −Z, negative pitch points down) is inferred; overall brightness at night needs comparison with the original |
| Fog | `city/<map>_fog.csv` row (time × 4 + weather): linear fog colour, start, end; the far plane is the fog end | inferred: the clear colour equals the fog colour |
| Visibility | camera room from the PSDL perimeters; rooms drawn are the CPVS row of that room, or every room within the fog distance when the camera is outside the city or more than 40 m above its room | the 40 m rule is a development convenience |
| City objects | `city/<map>.inst` instances, LOD H < 60 m < M < 130 m < L < 260 m < VL, scaled by the detail setting; PKG xrefs drawn with their parent | LOD distances inferred |
| Car bodies | the left half of each body uses the clean textures (`vpbugyellow_sd`), the right half the `_dmg` ones; undamaged, both halves draw the clean texture | inferred: no faces are shared between the halves; texel damage (TextelDamageRadius) will paint `_dmg` texels in |
| Car parts | wheels at their `.mtx` pivots, spinning about X and steering about Y; SHADOW drawn without depth writes; light glows (HLIGHT, TLIGHT, BLIGHT, RLIGHT) additive | — |
| Car LOD | H < 25 m < M < 60 m < L < 140 m < VL | inferred |

Known differences still to investigate:

* Night brightness of roads and cars.
* Street light glows and flares, headlight beams, water planes
  (`city/<map>.water`), reflections (`refl_*` textures), cloud shadows
  (`shadmap_*`), rain and snow particles.
