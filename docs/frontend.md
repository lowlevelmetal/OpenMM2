# Frontend (menus)

The original frontend runs at a fixed 640×480. Every screen is a full-screen
JPEG from `MM2TEX.AR` (`jpg/*_bk.jpg`) with its title, field labels and empty
value boxes painted in. Interactive elements are TGA sprite sheets
(`texture/*.tga`) drawn over it, values are text drawn into the painted
boxes, and a 219×69 help picture (`jpg/mn_*.jpg`, `race_*.jpg`, `opt_t*.jpg`,
`vp*_ulck.jpg` / `vp*_lck*.jpg` …) is shown in the black box at the bottom
left for the focused item.

OpenMM2 draws the same 640×480 layout scaled to the window
(`render::UiScaleMode`: fitted with bars, stretched, or pixel-exact) with
text rasterized at the real output resolution. Code: `src/ui/Widgets.*`
(widgets), `src/app/frontend/*` (screens), `src/game/Profile.*` (drivers and
unlocks).

## How the layout was recovered

* **Button positions** were found by template matching each sprite sheet's
  first frame against every background, using only the sprite's outer
  2-pixel ring (the part that is a copy of the background). Most sprites
  match a background position with a mean error of 1–16 levels, i.e. they
  were cut from these exact backgrounds. Positions found this way are
  **verified**; see the table below.
* **Value boxes** (navy `#08022E` with a bevel) and **black panels** were
  located with a connected-component detector on the backgrounds
  (**verified** to the pixel).
* **Dialog positions** come from `tune/menu.csv` where listed
  (`Create a New Driver` 120,75; `MessageBox` 120,202); other dialogs are
  centred (**inferred**).

| Element | Position | Evidence |
|---|---|---|
| top strip OPTIONS / ? / minimise / exit (`mnav_opt`, `mnav_hlp`, `mnav_sto`, `mnav_ext`) | 439,1 / 540,1 / 564,1 / 594,1 | matched (contiguous) |
| right column (`dvrcc`, `main_sp`/`opt_aud`, `main_mp`/`opt_ctl`/`cci_lon`, `main_qck`/`opt_gfx`/`opt_done`/`veh_go`/`race_veh`/`cci_sf`) | x 439, y 242 / 301 / 359 / 415 | matched; sprite heights 59/58/56/65 fit only their slot |
| back / cancel (`mnav_prv`, `back`, `opt_can`) | 290,415 | matched |
| driver buttons (`dvrnew`, `dvrdel`, `dvrsts`) | 40,156 / 40,216 / 40,283 | matched (weaker: error 16–27) |
| race types (`cruise`, `blitz`, `cp`, `circuit`) | x 40, y 62 / 89 / 116 / 143 | matched |
| multiplayer race types (`*_m`, `cops_m`) | x 40, y 51 … 159 step 27 | matched |
| option toggles (`gfx_*`, `aud_*`, `ctrl_*`) | x 40, rows of 27 from y 64 | matched |
| connection types (`sess_zn/ipx/tcp/ser/mod`) | x 40, y 54 … 162 | matched |
| HOST / JOIN (`sess_hst`, `sess_jn`) | 439,98 / 439,167 | matched |
| CALIBRATE / CUSTOMIZE CONTROLS | 460,180 / 346,314 | matched |
| Crash Course TRAINING / BLITZ / CHECKPOINT (`cc_*`) | 290,109 / 290,237 / 290,271 | matched |
| C&R host options (`free_cr`, `cpsvr_cr`, `robrs_cr`, `none_cr`, `time_cr`, `point_cr`) | x 395, y 57 … 181 | matched (not used yet) |
| VEHICLE SHOWCASE (`veh_show`) | 253,55 | matched (weak) |
| ABOUT (`opt_abt`) | 25,328 | matched |
| dialog buttons (`dlg_*`), DEFAULTS (`opt_def`), results buttons (`result_*`) | see code | **inferred** (transparent sprites or no match) |

### Sprite state layouts (verified from the sheets)

* Arrow and box buttons: 4 frames — normal, highlighted, pressed, disabled.
  Small strip buttons (`mnav_hlp/sto/ext`): 3 frames; `mnav_opt` 5.
* Lamp rows (`blitz`, `aud_fx`, `sess_tcp`, `cc_train` …) and check boxes
  (`dlg_chkb`): 5 frames — off, off+highlight, on, on+highlight, disabled.
* Sliders: `slider_larr`/`slider_rarr` (or `slider_lbal`/`rbal` for balance)
  arrows, 5×29, around a value bar cropped from `slider_actl` (focused, red)
  or `slider_inactl` (purple). `sliderback.tga`/`sliderbutton.tga` are fully
  transparent leftovers and unused.
* Drop-down indicator `drop_arrow` (3 frames), list scroll arrows
  `scroll_uarr/darr` (4 frames), crash-course marks `cc_smchk` (empty, tick,
  cross).

## Screens

| Background | Screen | Notes |
|---|---|---|
| `splash.jpg` | title | loading bar (`pbar_inact` + `pbar_act`) then "press any key" (inferred) |
| `main_bk.jpg` | Select Driver | driver list, CREATE/DELETE/STATS, CRASH COURSE, RACES, MULTIPLAYER, QUICK RACE |
| `newp_dlg.jpg` | Create a New Driver | name, Amateur / Professional (difficulty is chosen per driver) |
| `drec_dlg.jpg` | Driver's Stats | records per mode and city, score, last race/vehicle |
| `races_bk.jpg` | Races | mode lamps, race name, laps, opponents, location, time of day, weather, density sliders, race map (`<city>_map<mode><n>.jpg`) |
| `veh_bk.jpg` | Select Vehicle | showcase photo (stand-in for the 3D showroom), LOCKED overlay, performance bars from `tune/*.info`, vehicle, colour, transmission |
| `vp*_show.jpg` | Vehicle Showcase | full-screen spec sheet |
| `opt_bk.jpg` → `gfx_bk`, `aud_bk`, `ctrl_bk`, `cuss_bk`, `about_bk` | options | CANCEL restores, DONE saves |
| `ilon_bk.jpg` → `cclon_bk` / `ccsf_bk` | Crash Course | lessons (string ids 532–557), pass/fail marks, "work experience" races |
| `rshi_bk.jpg`, `crshi_bk.jpg` | results | next race, restart, race menu, replay (disabled), exit |
| `sess_bk.jpg` | Multiplayer Sessions | layout only; hosting/joining shows "coming soon" |
| `quit_dlg`, `msg_dlg`, `lock_dlg`, `delp_dlg` | dialogs | yes/no, OK |

Graphics options map the original fields onto modern settings: DISPLAY =
window mode, RENDERER = Vulkan/OpenGL, RESOLUTION = window size or exclusive
mode. VSync, anti-aliasing, render scale, UI scale and field of view (which
the original did not have) are in a panel drawn over the lower-left picture.
Original-only options (visibility, lighting quality, texture quality,
object detail, cloud shadows, textured sky, reflections, pedestrians, smart
rendering, audio toggles, controller settings, key bindings) are stored in
`[Graphics]`, `[Audio]` and `[Controls]` of `openmm2.ini` for the game to use.

## Drivers and unlocks

Driver profiles are OpenMM2's own INI files in `<user data>/players/`
(format documented in `src/game/Profile.h`); the original save format is not
known.

Unlock rules come from the data:

* `race/<city>/<city>_rewards.csv` lists every reward: `half` of a mode's
  races unlocks a vehicle (`VariantNum` 0), `all` unlocks a paint job,
  `crash,N` is awarded for passing Crash Course lesson N (3, 7, 11 are the
  midterms, 12 the final). **Verified** against the lock pictures
  (`jpg/vp*_lck*.jpg`): "place 1st, 2nd, or 3rd in five London Blitz Races",
  "pass San Francisco Crash Course Midterm Exam Two", etc.
* A race counts as won within `MustPlace` (3, from `tune/<city>.cinfo`) for
  amateurs and only in 1st place for professionals (the `_lck_p` pictures
  say "finish first"). **Verified** by the pictures.
* Vehicles not in a rewards table are available from the start, giving 12
  of 20 cars initially. `UnlockScore`/`UnlockFlags` in `tune/*.info` do not
  line up with the rewards table and are **not used** (meaning unknown).
* Race availability: the first `UnlockGroup` (3) races of each mode, plus one
  per race won — **inferred**. Crash Course lessons open in order —
  **inferred**.
* Results are recorded only "for races under default conditions" (text of
  `drec_dlg.jpg`): changing laps or opponents from the race's defaults
  skips recording. **Verified** rule, implemented in the frontend.

## Automation

`OPENMM2_FRONTEND_SCRIPT` drives the menus for screenshots, e.g.

```
OPENMM2_FRONTEND_SCRIPT="profile:Test;page:races;mode:blitz;nav:down;wait:3" \
  openmm2 --frames 12 --screenshot races.png
```

Commands: `profile:<name>`, `page:<title|driver|newdriver|stats|races|vehicle|
showcase|options|graphics|audio|control|customize|about|crashintro|crashlondon|
crashsf|sessions|quit|message|delete>`, `mode:<cruise|blitz|circuit|race|crash>`,
`city:<map>`, `vehicle:<name>`, `go` (start the race), `result:<position>`, `nav:<up|down|left|right|
accept|back|tab>`, `wait:<frames>`.

## Not yet done

* 3D vehicle showroom (the original rotated the car model).
* Multiplayer lobby screens (`host_bk`, `lobbh_bk`, `lobbj_bk`) and the
  Cops & Robbers host options.
* Instant replay.
* Exact text colours and font sizes inside boxes, and menu sounds, are
  inferred.
