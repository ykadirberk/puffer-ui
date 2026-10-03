# Porting a backend

PufferUI owns all state and geometry; a backend only turns the frame's draw list
into pixels. The contract is two interfaces declared in
`include/pufferui/pufferui.h`:

- `render_device` — one per application. Owns textures and offscreen targets,
  receives all geometry.
- `render_surface` — one per window. Owns the backbuffer and presentation.

The SDL3 backend (`create_sdl3_device`) is the reference implementation, and
`null_device` in the same header is the smallest working headless one (it is what
the test suite runs against). `test_device_contract` in `tests/test_render.cpp`
exercises the whole contract; make it pass with your device before trusting
anything else.

## render_device

Required:

| member | contract |
| --- | --- |
| `caps()` | report what the backend supports (see Capabilities) |
| `create_texture(w, h, rgba)` | RGBA8, straight (non-premultiplied) alpha; returns a handle or `nullptr` |
| `update_texture(tex, x, y, w, h, rgba)` | partial upload; the font atlas streams dirty rects through this |
| `destroy_texture(tex)` | free the texture; also used for targets unless `destroy_target` overrides it |
| `draw(tex, vertices, vertex_count, indices, index_count)` | one triangle list; `tex == nullptr` means solid-color geometry (vertex color only) |

Optional overrides, and when they matter:

- `begin_frame()` / `end_frame()` — bracket the whole frame across all windows.
- `create_surface(native_window)` — needed for anything past the primary window.
- `clear(color)` — called once per frame with the theme background. The default
  is a no-op, so a backend that does not clear will smear the previous frame.
- `set_clip(const rect *)` — scissor in surface pixels; `nullptr` clears it.
- `set_cursor(cursor)` — the core already dedupes per hovered window; just map
  the enum and skip if unchanged.
- `set_vsync(bool)` — best effort; backends without control may ignore it.
- `create_target` / `destroy_target` / `blit` / `set_target` / `current_target` —
  required for `backend_caps::RENDER_TARGETS` (backdrop blur). `blit` is a
  filtered (bilinear) copy used by the blur pyramid.

## render_surface

- `make_current(device)` — make this window's context current before drawing.
- `default_target()` — the texture the core draws into (usually the backbuffer).
- `scene_target()` — offscreen target the blur samples; may equal the default.
- `output_size(w, h)` — client size in pixels.
- `present()` — swap/present after the frame.
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
