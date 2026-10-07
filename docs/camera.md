# Car cameras

`src/game/Cam*.{h,cpp}` reimplements the Angel engine's car cameras. The
logic is ported from Midtown Madness 1 build 1560. That build's code is
available as symbol-named MASM in Open1560's `code/midtown/game.asm`
(GPL-3.0, Copyright (C) Brick); its class layouts are in
`code/midtown/mmcamcs/*.h`. MM2's own executable was not used: it is
SafeDisc-protected.

MM2 kept the same classes and tune files, and added a few fields. Where MM2
data shows a difference, the handling is marked **inferred** below and in
the code.

## Classes

| Angel (MM1) | OpenMM2 | File |
|---|---|---|
| `BaseCamCS`, `AppCamCS`, `CarCamCS` | `CarCamera` | `CamCar.*` |
| `TrackCamCS` (chase) | `TrackCamera` | `CamTrack.*` |
| `PovCamCS` (hood / dashboard) | `PovCamera` | `CamPov.*` |
| `mmViewCS` + `TransitionCS` | `CameraView` | `CamView.*` |
| `mmPlayer` camera set-up / `ToggleCam` | `PlayerCameras` | `CamPlayer.*` |
| `Matrix34` / `Vector3` helpers | `cam::` functions | `CamMath.*` |
| tune file fields (`DeclareFields`) | `TrackCamParams`, `PovCamParams` | `CamParams.*` |

Every camera keeps a goal matrix (AGE `matrix_`) and the matrix actually
rendered (AGE `camera_`). Each update the camera computes the goal, then
`ApproachIt` moves `camera_` towards it. Position moves per axis with
`DApproach` at `AppXZPos` / `AppYPos`. Orientation moves per Euler angle
("zxy" order) at `AppRot` / `AppXRot`, eased in within `AppPosMin` /
`AppRotMin`, and optionally low-pass filtered (`AppAppOn`, `AppApp`).
`UpdateMaxDist` keeps it between `MinDist` and `MaxDist` of `TrackTo`.

## Using it

```cpp
game::PlayerCameras cams;
cams.load(vfs, "vpbug");                 // tune/camera/vpbug_near.camtrackcs, ...
game::CameraTarget target;               // car matrix, velocity, angular velocity,
                                         // steering, wheels on ground, reverse
cams.reset(target);                      // after placing the car
// every simulation update (refresh target first):
game::CameraInput input;
input.camPan = game::cameraPanFor(lookLeft, lookRight, lookBack, lookForward);
cams.update(dt, target, probe, input);   // probe: nearest segment hit (world collision)
cams.apply(camera);                      // game::Camera for the renderer
cams.toggleCamera();                     // "Change Camera"
cams.select(game::PlayerCameras::View::Far); // jump to a view (0.8 s blend)
cams.setDashboard(on);                   // "Dashboard On/Off"
cams.display();                          // draw body / hide it / draw the dash model
```

* **Car matrix.** `CameraTarget::matrix` is the car's frame as the
  simulation sees it (AGE `mmCar+0x298`). `TrackTo` and `Offset` are in that
  frame; it faces -m2.
* **Timing.** `dt` is the simulation update delta (AGE
  `Sim()->GetUpdateDelta()`). Angel updated nodes once per simulation
  sample, with a variable step clamped to 0.0001–0.25 s. The cameras work
  with any step.
* **Probe.** `CameraProbe(from, to, hit, normal)` returns the nearest hit on
  the segment. It is used for the floor/ceiling clamp (`MinMax`) and for
  pulling the camera in front of walls (`Collide`). Without a probe the
  cameras still work, just without collision.
* **FOV.** `Camera::horizontalFov` is `CameraFOV` converted to radians. It is
  authored for 4:3, and `render::computeProjection` applies Hor+.

## Camera files and views

`mmPlayer::Init` loads per car:

| View | Class | File |
|---|---|---|
| near | TrackCamCS | `tune/camera/<car>_near.camtrackcs` |
| far | TrackCamCS | `<car>_far.camtrackcs` |
| ind | TrackCamCS | `<car>_ind.camtrackcs` (only vpbug and vpbus have it; not in the cycle) |
| pov | PovCamCS | `<car>.campovcs` (hood) |
| dash | PovCamCS | `<car>_dash.campovcs` |

A missing file leaves the camera at the constructor defaults; `load()`
reports it. vpvwcup's far camera ships as `vpvwcup__far.camtrackcs` (typo),
so the original used defaults for it too. `<car>_pov.campovcs` (cab, coop,
coop2k) is not loaded by the MM1 code; what MM2 used it for is unknown.

"Change Camera" (`mmPlayer::ToggleCam`) cycles `CarCams[0..2]` = near → pov →
far, with `mmViewCS::NewCam(cam, 3, 0.8)`: a 0.8 s ease-in-out blend.
Whether MM2 changed the cycle is not known. The dashboard toggle blends in
with mode 1 and out with mode 2 over 0.3 s; that is **inferred** from MM1's
pre-race camera code, which uses those modes.

Look around comes from `mmInput::GetCamPan`, a fraction of a full turn:
left 0.25, back 0.5, right 0.75, forward 0, back+left 0.375, back+right
0.625, forward+left 0.125, forward+right 0.875. An analog "Camera Pan" axis
overrides the buttons.

## Port status

| Function | Status |
|---|---|
| `AppCamCS::DApproach`, `UpdateApproach`, `UpdateMaxDist`, `ApproachIt` | ported, same operation order |
| `TrackCamCS::Update`, `UpdateCar`, `UpdateTrack`, `MinMax`, `Collide`, `Reset`, constructor defaults | ported |
| `TrackCamCS::PreApproach` (speed-dependent `AppXZPos`) | Open1560's reconstruction; the MM1 original body is not in game.asm. Enabled because MM2 tunes these fields per camera (**inferred**) |
| `TrackCamCS::UpdateSwing`, `SwingToRear`, `Rear`, `Spline` (swing transitions) | not ported: they only start when SplineState2/3 are set, and no MM1 code sets them |
| `PovCamCS::UpdatePOV`, `Update`, `Reset`, `AfterLoad` (`CameraNear = 0.1`) | ported |
| `mmViewCS::NewCam`, `SetCurrentCam`, `Update`, `Reset` | ported |
| `TransitionCS::Update`, `NewTransition`, `NextTransition`, `StartTransition`, `StartNextTransition`, `ReverseTransition` | ported: polar blend about the look point, FOV and near blend, blend curves 1–3 |
| `Matrix34::LookAt`, `GetEulers`/`FromEulers("zxy")`, `RotateFull`, `ArbitraryRotation`, `PolarView`, `Vector3::Approach` | ported. Special-case paths for zero axis components compute the same values with the zero terms dropped |
| `PreCamCS`, `PostCamCS`, `PolarCamCS`, `PointCamCS`, `AICamCS` (pre/post race, wreck, attract) | not ported yet |
| rear-view mirror camera, wide-angle letterbox viewport | not ported (`CameraView::setWideAngle` only sets the 1.74 rad FOV) |

The math is 32-bit float, like the original's single-precision x87. It can
differ from the original in the last bit where x87 kept intermediates on
the register stack. Two places knowingly differ: `Collide` uses an exact
`1/sqrt` where the original used the table-seeded `invsqrtf_fast`, and the
POV shake uses a seeded xorshift for `frand()`, whose original generator is
unknown.

## Tune fields

The MM1 names have an `m_` prefix (`m_AppRot`); MM2 drops it.

| Field | Class | MM1 use | OpenMM2 |
|---|---|---|---|
| BlendTime | Base | none | loaded |
| BlendGoal | Base | fraction of a transition that is blended | ported |
| CameraFOV, CameraNear | Base | view | ported |
| CameraFar | Base | ignored; the render quality far clip was used | passed through as `Camera::farPlane` |
| ApproachOn, AppAppOn, AppRot, AppXRot, AppYPos, AppXZPos, AppApp, AppRotMin, AppPosMin, LookAbove, TrackTo, MaxDist, MinDist, LookAt | App | approach | ported |
| Offset, TrackBreak, MinMaxOn, VertOffset, SteerOn, SteerMin, SteerAmt, MinSpeed, MaxSpeed | Track | tracking | ported. SteerOn is 0 in every MM2 file |
| CollideType | Track | 2 = collide; anything else does nothing | MM2 uses **1 everywhere**. **Inferred**: 1 is treated like 2 (`TrackCamera::options.collideType1`) |
| MinAppXZPos, MaxAppXZPos, AppInc, AppDec | Track | PreApproach (see above) | **inferred** |
| DriftDelay | Track | compared against a timer that is never advanced, so no effect | ported (no effect) |
| MinHardSteer, FrontRate, RearRate, FlipDelay | Track | declared, never read | loaded, unused |
| HillMin, HillMax, HillLerp | Track (MM2 only) | MM1 `UpdateHill` is empty | **inferred**: car pitch clamped to [HillMin, HillMax], blended with HillLerp per update at 30 updates/s; the goal's offset from the target is tilted about the camera's right axis by that angle |
| ReverseOn, RevDelay, RevOnApp, RevOffApp | Track (MM2 only) | not in MM1 | **inferred**: after RevDelay s of backing up in reverse, the chase camera swings to the front (shared swing angle π). `AppXZPos` is set to RevOnApp, and to RevOffApp when driving forward again |
| Pitch | Pov | camera pitch | ported |
| POVJitterAmp | Pov | declared, never read (0 in every MM2 file) | loaded, unused |
| ReverseOffset | Pov (MM2 only) | not in MM1 | **inferred**: eye position while the look direction includes "back" (CamPan 0.375–0.625) |

The chase cameras look around (CamPan) through the shared swing angle that
MM1 reserved for spline swings (`TrackCamData::YRot`). MM1 only applied
CamPan to the POV camera; applying it to the chase cameras is **inferred**.

POV shake (`UpdatePOV`) reads three `mmCar` fields whose meaning is only
partly known (`+0xBA4` wheel spin, `+0xB64` a phase angle, `+0x1904` an
amplitude). They are inputs on `CameraTarget` and default to zero (no
shake) until the simulation provides matching values.

## Verification

`tests/game/test_camera.cpp` (`test_game`):

* **Math helpers:** Euler round trip, plus agreement with `Mat34` rotations
  for zxy order, RotateFull, ArbitraryRotation and PolarView; `LookAt`
  orthonormality; `Vector3::Approach`.
* **Synthetic car paths through `TrackCamera`** with the vpbug near
  tuning: straight at 20 m/s, a hard turn, reversing past RevDelay, a stop,
  look back, and a wall behind the car. They check that the camera stays
  within MinDist/MaxDist and above the floor, never moves more than twice
  the car's travel per update, keeps the car in view, settles behind the car
  at Offset.z, ends in front when reversing, and stays in front of the
  wall.
* **Determinism:** two identical runs give bit-identical matrices.
* **POV:** eye position, look back with ReverseOffset, and look left.
* **View blending:** near→far blends without jumps; FOV stays between the
  two cameras' values; the blend ends after 0.8 s; "Change Camera" cycles
  on from the selected view.
* **Retail data** (with `OPENMM2_GAME_DATA`): all 96 camera files parse;
  the vpvwcup typo is detected; all 20 player cars' near, far, pov and dash
  cameras run the scripted path without NaNs and keep the car framed.
* **`Camera.PlotPaths`** (with `OPENMM2_CAMERA_PLOT=<dir>`) writes car and
  camera paths as CSV for plotting.
