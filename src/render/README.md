# Rendering (`mm2_render`) and platform (`mm2_platform`)

## Platform (SDL3)

| Header | Purpose |
|---|---|
| `platform/Platform.h` | `init()`/`shutdown()`, `pollEvents()` (feeds `Input`, an optional raw-event hook for ImGui, and dispatches dialog callbacks), monotonic clock, `sleepPrecise()` |
| `platform/Window.h` | `Window` (Vulkan or OpenGL), `applyMode()` for windowed / borderless / exclusive fullscreen on any display, pixel size and DPI scale, `enumerateDisplays()` |
| `platform/Input.h` | Polled keyboard (HID scancodes), mouse (window pixels), text input, gamepads (SDL mappings, hotplug, rumble) and raw joysticks (wheels, pedals) |
| `platform/Dialogs.h` | Blocking message boxes; async native file/folder pickers (callbacks run on the main thread from `pollEvents()`) |
| `platform/Clock.h` | `FrameClock` (clamped frame delta) and `FrameLimiter` (frame cap independent of vsync) |
| `platform/ImGuiPlatform.h` | Dear ImGui SDL3 platform glue |

## Render hardware interface

`render/Device.h` is the API the game uses. One `Device` draws into one
window; `render/Renderer.h` creates both, trying Vulkan first and falling back
to OpenGL 3.3 (the reason is kept in `Renderer::fallbackReason`).

```
device.beginFrame()                     // false while minimised
  device.beginScene(clear)              // offscreen 3D target: MSAA + render scale
    setFrameConstants(camera, fog, lights)
    draw(DrawCall{state, buffers, textures, constants}) ...
  device.endScene()
  device.beginOverlay(clearColor)       // scene composited onto the window
    Overlay2D / ImGuiRenderer / draw()  // native resolution
  device.endOverlay()
device.endFrame()                       // present
```

The feature set mirrors what MM2's Direct3D 7 renderer used: RGBA8 textures
with mip chains, wrap/clamp/mirror samplers with point/bilinear/trilinear
filtering and anisotropy, static and per-frame vertex/index buffers, opaque /
alpha / additive / modulate / premultiplied blending, depth test/write/bias,
culling, alpha test, linear/exp/exp2 fog, three directional lights plus
ambient (per-vertex, clamped like D3D7), vertex colour, and a second texture
stage (modulate, add, modulate2x or alpha blend; optional sphere-map
coordinates for car reflections).

Conventions (both backends):

* `Mat44` row-major with row vectors, D3D-style clip space (y up, depth 0..1).
  Vulkan flips y with a negative viewport height (core in 1.1); OpenGL remaps
  depth in the vertex shader (`clipFixup` in `shaders/prelude_gl.glsl`).
* Pixel coordinates have their origin at the top-left, in both passes.
* Everything is gamma-space RGBA8 with UNORM targets, like the original; no
  sRGB conversion is applied.
* Shaders are written once (`shaders/*.vert|frag`) against small preludes and
  compiled to SPIR-V for Vulkan and to GLSL 3.30 for OpenGL (validated by
  glslangValidator at build time when available).

## Backends

**Vulkan** (`vulkan/VulkanDevice.cpp`): Vulkan 1.1 baseline with classic
render passes rather than dynamic rendering, so older drivers work. Uses volk
and VMA. Two frames in flight; per-frame ring buffers for transient
geometry, frame constants (dynamic UBO) and staging; per-draw constants in
push constants. Uploads made during a frame go into a separate command
buffer submitted ahead of the frame; uploads made outside a frame (loading)
are batched and submitted when the next frame starts. Pipelines are created
on demand per state and cached on disk (`DeviceCreateInfo::pipelineCachePath`).
Set `Validation=true` in `[Display]` or `OPENMM2_VK_VALIDATION=1` to enable
`VK_LAYER_KHRONOS_validation`; errors are counted in `FrameStats::apiErrors`.

**OpenGL 3.3 core** (`opengl/`): function pointers via
`SDL_GL_GetProcAddress` (no loader dependency), sampler objects, one VAO per
vertex format, uniform-buffer rings orphaned per frame, `KHR_debug` output
when validation is on.

## Display options

`render/DisplaySettings.h` holds every option the game persists (`[Display]`
section): backend, window mode, display, window size, fullscreen mode, vsync
(off/on/adaptive/mailbox), frame cap, MSAA, anisotropy, render scale (0.25-2,
super/sub-sampling), UI scale mode, FOV mode, ultrawide limit, GPU, validation.
`applyDisplaySettings()` applies changes live and reports when a new renderer
is needed (backend/GPU/validation).

`render/Projection.h`:

* **FOV**: Angel cameras give a horizontal FOV meant for 4:3
  (`asCamera::SetView`). `computeProjection()` converts it to Hor+ (default:
  vertical FOV fixed at the 4:3 value), Vert- or stretched 4:3, with an
  optional maximum aspect for ultrawide screens.
* **UI**: `computeUiLayout()` maps the original 640x480 UI space to the
  window (fit with bars, stretch, or integer scale). `UiLayout::left/right`
  expose the visible virtual range so HUD elements can be anchored to the
  real screen edges. `Overlay2D` batches quads in that space.

## Testing

`tools/rendertest` draws a lit, fogged, textured scene with alpha-tested
and translucent geometry, the 640x480 overlay and an ImGui settings panel:

```
rendertest --backend vulkan --frames 60 --screenshot out.png --validation
rendertest --backend opengl --stress    # walks MSAA/scale/vsync/mode/backend changes
```

With `--frames`, animation is driven by the frame number, so screenshots from
the two backends can be compared pixel by pixel; the exit code is 3 if the API
reported validation errors. The validation layer can be used without
installing it system-wide via `VK_ADD_LAYER_PATH`.
