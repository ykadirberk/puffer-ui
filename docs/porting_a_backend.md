# Porting a backend

PufferUI owns layout, input and widget state. Every backend supplies a complete
semantic painter that owns drawing preparation and execution. The contract has
three interfaces declared in
`include/pufferui/pufferui.h`:

- `render_device` — one per application. Owns textures and offscreen targets,
  creates a painter for each context.
- `render_painter` — receives logical drawing operations before tessellation or
  glyph preparation; chooses geometry, procedural shapes, text representation,
  caching, batching and blur for its renderer configuration.
- `render_surface` — one per window. Owns the backbuffer and presentation.

The SDL3 backend (`create_sdl3_device`) is the reference implementation, and
`null_device` in the same header is the smallest working headless one (it is what
the test suite runs against). `test_device_contract` in `tests/test_render.cpp`
exercises the whole contract; make it pass with your device before trusting
anything else.

The optional native Vulkan implementation is documented in [vulkan.md](vulkan.md).
It also implements these interfaces, accepts application-owned Vulkan handles,
and can use SDL solely as the window/input host. Link `pufferui::vulkan` for
that route; the default SDL renderer and core-only consumption stay independent.
Native image interoperability uses `<pufferui/vulkan.h>` rather than exposing
SDK types in the core declaration section.

## render_device

Required:

| member | contract |
| --- | --- |
| `caps()` | report what the backend supports (see Capabilities) |
| `create_painter(context&)` | return an owned, complete `render_painter`; must not return null; the context deletes it before device destruction |
| `create_texture(w, h, rgba)` | RGBA8, straight (non-premultiplied) alpha; returns a handle or `nullptr` |
| `update_texture(tex, x, y, w, h, rgba)` | partial upload; the font atlas streams dirty rects through this |
| `destroy_texture(tex)` | free the texture; also used for targets unless `destroy_target` overrides it |
| `draw(tex, vertices, vertex_count, indices, index_count)` | one triangle list; `tex == nullptr` means solid-color geometry (vertex color only) |

Optional overrides, and when they matter:

- `prepare_geometry(const geometry_capacity&)` — optional CPU scratch preparation.
  SDL3 initializes its retained color prefix here. Return `false` when unsupported;
  normal drawing must still work. Preserve existing initialized data, do not
  draw/flush or change target/clip state. Validate both counts against i32 draw
  limits before allocating. This is independent of `ui::prepare_geometry`, which
  delegates storage preparation to the active painter. Wrappers can forward the
  device call; applications may also prepare their owned device directly.
- `begin_frame()` / `end_frame()` — bracket each window frame. The core calls
  `device->begin_frame()` before that surface's `make_current()`, then
  `device->end_frame()` before its `present()`. Shared storage must respect
  other windows' outstanding work; these hooks are not a global frame reset.
- `create_surface(native_window)` — needed for anything past the primary window.
- `destroy_surface(surface)` — releases a closed window's surface (after
  `remove_window`, before the native window goes away).
- Native window chrome (`window_host::install_system_chrome`): map the
  platform's caption hit-test to `window::caption.contains(x, y)` (local logical
  coordinates). The core rebuilds it every frame from the titlebar's drag
  region minus the widgets over it, so buttons on the bar keep their clicks.
- `clear(color)` — called once per frame with the theme background. The default
  is a no-op, so a backend that does not clear will smear the previous frame.
- `set_clip(const rect *)` — scissor in surface pixels; `nullptr` clears it.
- `set_cursor(cursor)` — the core already dedupes per hovered window; just map
  the enum and skip if unchanged.
- `set_vsync(bool)` — best effort; backends without control may ignore it.
  With several windows, let one surface wait for the vblank and present the
  others without waiting (the SDL3 device picks the first window on screen):
  the app presents its windows one after another, so a wait per window would
  divide the frame rate by the window count.
- `create_target` / `destroy_target` / `blit` / `set_target` / `current_target` —
  required for `backend_caps::RENDER_TARGETS` (backdrop blur). `blit` is a
  filtered (bilinear) copy used by the blur pyramid.

## render_painter

Every operation is pure virtual. Missing handlers are compile errors; core never
selects a tessellator when a backend omits an operation. Implement mesh, rectangle,
line, rounded rectangle, skin image, fitted image, nine-slice, polygon, sector,
arc, blur, text, ellipsis, wrapping, ring, stroke and gradient operations, including
the portable rounded gradient used by showcase. `ui::draw_rounded_gradient`
submits its options without building a fan. Its mesh implementation retains the
original underlay and six-segment corner fan; Vulkan selects its analytic
representation from device configuration. Text
width and glyph-advance measurement also go through the painter so layout and
painting use its chosen font representation. `font_bytes(context, handle)` exposes
borrowed source bytes for custom font caches, without requiring implementation
headers. The theme's font, text size and gamma remain context data.

Each operation takes a named options struct. Coordinates and thicknesses are
logical units; the active context supplies DPI, clip stack, target/window, theme
and disabled alpha. Inputs are borrowed for the duration of the call. A deferred
painter must copy the operation and any referenced mesh, string or image data,
and snapshot all state it will need before returning. It must preserve submission
order, including the scene prefix sampled by blur. Zero clips must paint nothing.

The painter owns its storage. `geometry()` returns a borrowed introspection pointer
or null for a backend without CPU geometry; core neither allocates nor deletes a
draw list. `prepare` reserves renderer-owned storage without changing live draws.
`begin` starts painting after the device and surface have become current; `flush`
finishes the submitted prefix; `end` finishes state for presentation. `invalidate`
forgets cached target/clip/texture bindings without painting or clearing. It is
used when switching to and from a popup recorder. `release_effects` releases the
painter's per-window effect scratch before that window/context is destroyed.
`release_text` releases renderer-owned glyph/atlas caches when rebinding or
destroying a context. Source font bytes remain alive through painter destruction;
the new renderer rebuilds its own representation after a device change. Rebind
only between frames. Mesh effect scratch is prepared in `begin`, before drawing.

Backends that deliberately choose CPU tessellation can explicitly use the complete
shared mesh implementation:

```cpp
render_painter *create_painter(context &c) override
{
    return create_mesh_painter(c);
}
```

SDL3, the recording null backend and the CPU example choose this implementation.
Vulkan supplies the same complete operation set and selects native triangle
shapes, the original blur and optional HarfBuzz/Slug text inside its own factory.
Core contains no Vulkan painter selection. This branch has no SDF shape or
compute painter. Backend wrappers should forward `create_painter`
to their wrapped device while keeping the context bound to the wrapper for upload,
mesh and lifecycle accounting.

Popup overlays use a complete recording painter of their own. Their shifted mesh
stream is replayed after the parent presents; they deliberately retain the
existing bitmap/raster text policy and skip backdrop effects. Their paint storage
is retained with the overlay, and the parent painter is restored afterward.

This is a backend API change: existing custom devices must add the factory and
either explicitly choose the complete mesh painter or implement every painter
handler. Existing UI drawing calls are unchanged. The no-device headless context
retains its explicit recording mesh policy. See `tests/test_renderer_boundary.cpp`
for a complete renderer that has no CPU draw list or atlas.

## render_surface

- `make_current(device)` — make this window's context current before drawing.
- `default_target()` — the texture the core draws into (usually the backbuffer).
- `scene_target()` — offscreen target the blur samples; may equal the default.
- `output_size(w, h)` — client size in pixels.
- `present()` — swap/present after the frame. `end_frame` calls it once per
  window frame; apps do not call it again (with vsync on, a second present of
  the same frame waits another vblank and halves the frame rate).
- `resize()` — called when the window size changed.

## Vertex format and conventions

```cpp
struct vertex
{
    f32 x = 0, y = 0, u = 0, v = 0;
    color c{};
};
```

- Positions are in surface pixels, origin top-left, y down.
- `u/v` are normalized texture coordinates, origin top-left.
- `c` is straight RGBA8: use `src_alpha, one_minus_src_alpha` blending.
- Triangle lists, no culling: winding is not guaranteed, do not rely on it.
- Indices refer to the vertex array passed in the same `draw` call.
- Runs that share a texture are batched, so expect few `draw` calls per frame.

## Capabilities

- `SCISSOR` — the core assumes clip rects are honored; report it only if
  `set_clip` actually clips.
- `RENDER_TARGETS` — offscreen targets plus filtered blit. Without it `blur`
  degrades gracefully (see `test_blur_fallback`).
- `STREAMING_TEXTURES` — textures are updated incrementally (font atlas).
- `SHARED_DEVICE` — one device can serve several windows. The core refuses a
  second window unless this is reported (`test_multi_window`).

## Checklist

1. Implement `caps()`, `create_texture`, `update_texture`, `destroy_texture`,
   `draw`, and a real `clear`.
2. Implement a `render_surface` per window: target, size, present, resize.
3. Point the conformance tests at your device (the suite's device tests use
   `null_device`; swap in yours or copy `test_device_contract`) and make them
   pass.
4. Verify in the demo: blur, multi-window, cursors, and a resize while running.
5. Keep the per-frame path allocation-free; the core never allocates in `draw`.

Notes:

- The declaration section stays SDL-free: `create_sdl3_device(void *)` takes an
  `SDL_Renderer *` as an opaque pointer, and is only compiled when the
  implementation TU defines `PUFFERUI_ENABLE_SDL3`.
- Multi-window backends should map `render_surface::make_current` to their
  per-window context switch; the core calls it before every window's geometry.

## Worked example

`examples/atomic/renderer.cpp` (target `pui_ex_renderer`) is a complete backend:
a CPU `render_device` + `render_surface` that keeps an RGBA framebuffer,
rasterizes triangles with barycentric weights, samples textures in the vertex
color, honors `set_clip`, and writes a PPM. It reports `SCISSOR` and
`STREAMING_TEXTURES` without `RENDER_TARGETS`, so it also documents the blur
fallback path. Run it with:

```sh
out/build/x64-debug/Debug/pui_ex_renderer.exe    # writes pui_renderer_example.ppm
```
